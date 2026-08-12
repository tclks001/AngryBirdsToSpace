#!/usr/bin/env python3
"""Remove low-quality terminal side lobes while preserving Rank-11 handfeel."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

import m11_v22_certify_rank11 as frozen_rank11
import m11_v22_rank11_assist2_narrow_refinement as narrow
import m11_v22_rank11_assist2_similarity_search as search
import m11_v22_rank11_target_offset_search as target_search
import m11_v22_rank11_terminal_radius_search as terminal


SCHEMA = "abts.m11b.v2_2.rank11_terminal_quality_search.v1"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--prior-report", type=Path, required=True)
    parser.add_argument("--executable", type=Path,
                        default=frozen_rank11.default_executable())
    parser.add_argument("--freeze", type=Path,
                        default=frozen_rank11.default_freeze_manifest())
    parser.add_argument("--source-count", type=int, default=8)
    parser.add_argument("--minimum-quality", type=float, default=0.05)
    parser.add_argument("--maximum-quality", type=float, default=0.50)
    parser.add_argument("--quality-step", type=float, default=0.025)
    parser.add_argument("--promote-count", type=int, default=8)
    parser.add_argument("--max-workers", type=int, default=4)
    parser.add_argument("--threads-per-worker", type=int, default=2)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if min(args.source_count, args.quality_step, args.promote_count,
           args.max_workers, args.threads_per_worker) <= 0:
        parser.error("counts and quality step must be positive")
    if not (0.05 <= args.minimum_quality <= args.maximum_quality <= 1.0):
        parser.error("quality range must stay within the CLI contract")
    output = args.output.resolve()
    executable = args.executable.resolve()
    freeze_path = args.freeze.resolve()
    prior = args.prior_report.resolve()
    if not executable.is_file() or not prior.is_file():
        parser.error("executable and prior report must exist")
    freeze, candidate_path, manifest = frozen_rank11.validate_freeze(freeze_path)
    sources = terminal.load_sources(prior, args.source_count)
    inputs = []
    steps = int(round((args.maximum_quality - args.minimum_quality)
                      / args.quality_step))
    for source in sources:
        for index in range(steps + 1):
            candidate = json.loads(json.dumps(source["candidate"]))
            candidate["targetHitRadiusCM"] = 41250
            candidate["targetMinimumCorridorQuality"] = round(
                args.minimum_quality + index * args.quality_step, 6)
            inputs.append(candidate)
    inputs = search.unique(inputs)
    plan = {
        "schema": "abts.m11b.v2_2.rank11_terminal_quality_plan.v1",
        "baseCandidateSourceHash": frozen_rank11.EXPECTED_SOURCE_HASH,
        "freezeManifestSha256": target_search.sha256_file(freeze_path),
        "candidateManifestSha256": target_search.sha256_file(candidate_path),
        "priorReport": str(prior),
        "priorReportSha256": target_search.sha256_file(prior),
        "executable": str(executable),
        "executableSha256": target_search.sha256_file(executable),
        "schedulerSha256": target_search.sha256_file(Path(__file__)),
        "sources": sources,
        "minimumQuality": args.minimum_quality,
        "maximumQuality": args.maximum_quality,
        "qualityStep": args.quality_step,
        "candidateCount": len(inputs),
        "promoteCount": args.promote_count,
        "maxWorkers": args.max_workers,
        "threadsPerWorker": args.threads_per_worker,
        "logicalCpuCount": os.cpu_count(),
        "authority": "ABTSM11V22CertificationCLI",
    }
    target_search.atomic_json(output / "plan.json", plan)
    results = search.dispatch(
        "terminal_quality", executable, output / "closure", inputs,
        freeze, manifest, args.max_workers, args.threads_per_worker,
        args.resume)
    results.sort(key=narrow.refinement_key)
    eligible = [value for value in results if search.eligible(value)]
    promoted_inputs = eligible[:args.promote_count]
    full = terminal.promotions(
        executable, output / "full", promoted_inputs, freeze,
        args.threads_per_worker, args.max_workers, args.resume)
    accepted = [value for value in full
                if value.get("passedCandidateAcceptance")]
    report = {
        "schema": SCHEMA,
        "status": "candidate_found" if accepted else "quality_search_early_stopped",
        "candidateCount": len(results),
        "eligibleCandidateCount": len(eligible),
        "promotedCandidateCount": len(promoted_inputs),
        "results": results,
        "fullResults": full,
        "acceptedCandidate": accepted[0] if accepted else None,
    }
    target_search.atomic_json(output / "search_report.json", report)
    if accepted:
        target_search.atomic_json(output / "candidate_handoff.json", accepted[0])
    print("RESULT=" + str(output / "search_report.json"), flush=True)
    return 0 if accepted else 2


if __name__ == "__main__":
    raise SystemExit(main())
