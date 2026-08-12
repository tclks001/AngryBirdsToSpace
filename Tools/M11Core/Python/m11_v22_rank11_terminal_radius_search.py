#!/usr/bin/env python3
"""Shrink the terminal hit envelope to remove residual F4 islands."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
from pathlib import Path
from typing import Any

import m11_v22_certify_rank11 as frozen_rank11
import m11_v22_rank11_assist2_narrow_refinement as narrow
import m11_v22_rank11_assist2_similarity_search as search
import m11_v22_rank11_target_offset_search as target_search


SCHEMA = "abts.m11b.v2_2.rank11_terminal_radius_search.v1"


def load_sources(report_path: Path, count: int) -> list[dict[str, Any]]:
    report = json.loads(report_path.read_text(encoding="utf-8"))
    viable = [value for value in report.get("results", [])
              if "closure" in value and value.get("nominalF4")
              and value["handfeel"]["passed"]
              and not value["closure"]["earlyTargetHitCount"]
              and not value["closure"]["bypassTargetHitCount"]
              and not value["closure"]["nestingViolations"]]
    viable.sort(key=narrow.refinement_key)
    output: list[dict[str, Any]] = []
    seen: set[tuple[Any, ...]] = set()
    for value in viable:
        candidate = json.loads(json.dumps(value["candidate"]))
        candidate.setdefault("celestialRadialDeltaCM", [0, 0, 0, 0])
        candidate["targetHitRadiusCM"] = 41250
        key = search.candidate_key(candidate)
        if key not in seen:
            seen.add(key)
            copied = json.loads(json.dumps(value))
            copied["candidate"] = candidate
            output.append(copied)
        if len(output) == count:
            break
    return output


def candidates(sources: list[dict[str, Any]], minimum: int,
               maximum: int, step: int) -> list[dict[str, Any]]:
    output = []
    for source in sources:
        for radius in range(minimum, maximum + 1, step):
            candidate = json.loads(json.dumps(source["candidate"]))
            candidate["targetHitRadiusCM"] = radius
            output.append(candidate)
    return search.unique(output)


def promotions(executable: Path, output: Path,
               sources: list[dict[str, Any]], freeze: dict[str, Any],
               threads: int, workers: int, resume: bool) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(2, workers)) as pool:
        futures = [pool.submit(search.promote, executable, output, index,
                               source, freeze, max(2, threads * 2), resume)
                   for index, source in enumerate(sources)]
        for completed, future in enumerate(concurrent.futures.as_completed(futures), 1):
            result = future.result()
            results.append(result)
            half = result.get("half_step", {})
            print(json.dumps({
                "phase": "full_discovery", "completed": completed,
                "total": len(futures),
                "variant": result.get("variantSourceHash"),
                "passed": result.get("passedCandidateAcceptance"),
                "radiusCM": result["candidate"].get("targetHitRadiusCM"),
                "f4": half.get("prefixCounts", [None] * 4)[3],
                "components": half.get("componentCounts", [None] * 4)[3],
                "early": half.get("earlyTargetHitCount"),
                "screenAim": result.get("screenAim", {}).get("prefixCounts"),
            }, separators=(",", ":")), flush=True)
    return sorted(results, key=search.full_key)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--prior-report", type=Path, required=True)
    parser.add_argument("--executable", type=Path,
                        default=frozen_rank11.default_executable())
    parser.add_argument("--freeze", type=Path,
                        default=frozen_rank11.default_freeze_manifest())
    parser.add_argument("--source-count", type=int, default=8)
    parser.add_argument("--minimum-radius", type=int, default=4500)
    parser.add_argument("--maximum-radius", type=int, default=12000)
    parser.add_argument("--radius-step", type=int, default=500)
    parser.add_argument("--promote-count", type=int, default=8)
    parser.add_argument("--max-workers", type=int, default=4)
    parser.add_argument("--threads-per-worker", type=int, default=2)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if min(args.source_count, args.minimum_radius, args.radius_step,
           args.promote_count, args.max_workers, args.threads_per_worker) <= 0:
        parser.error("counts and radii must be positive")
    if (args.maximum_radius < args.minimum_radius
            or (args.maximum_radius - args.minimum_radius)
            % args.radius_step):
        parser.error("radius range must be ordered and step-aligned")
    output = args.output.resolve()
    executable = args.executable.resolve()
    freeze_path = args.freeze.resolve()
    prior = args.prior_report.resolve()
    if not executable.is_file() or not prior.is_file():
        parser.error("executable and prior report must exist")
    freeze, candidate_path, manifest = frozen_rank11.validate_freeze(freeze_path)
    sources = load_sources(prior, args.source_count)
    inputs = candidates(sources, args.minimum_radius,
                        args.maximum_radius, args.radius_step)
    plan = {
        "schema": "abts.m11b.v2_2.rank11_terminal_radius_plan.v1",
        "baseCandidateSourceHash": frozen_rank11.EXPECTED_SOURCE_HASH,
        "freezeManifestSha256": target_search.sha256_file(freeze_path),
        "candidateManifestSha256": target_search.sha256_file(candidate_path),
        "priorReport": str(prior),
        "priorReportSha256": target_search.sha256_file(prior),
        "executable": str(executable),
        "executableSha256": target_search.sha256_file(executable),
        "schedulerSha256": target_search.sha256_file(Path(__file__)),
        "sources": sources,
        "minimumRadiusCM": args.minimum_radius,
        "maximumRadiusCM": args.maximum_radius,
        "radiusStepCM": args.radius_step,
        "candidateCount": len(inputs),
        "promoteCount": args.promote_count,
        "maxWorkers": args.max_workers,
        "threadsPerWorker": args.threads_per_worker,
        "logicalCpuCount": os.cpu_count(),
        "authority": "ABTSM11V22CertificationCLI",
    }
    target_search.atomic_json(output / "plan.json", plan)
    results = search.dispatch(
        "terminal_radius", executable, output / "closure", inputs,
        freeze, manifest, args.max_workers, args.threads_per_worker,
        args.resume)
    results.sort(key=narrow.refinement_key)
    eligible = [value for value in results if search.eligible(value)]
    promoted_inputs = eligible[:args.promote_count]
    full = promotions(executable, output / "full", promoted_inputs, freeze,
                      args.threads_per_worker, args.max_workers, args.resume)
    accepted = [value for value in full
                if value.get("passedCandidateAcceptance")]
    report = {
        "schema": SCHEMA,
        "status": "candidate_found" if accepted else "radius_search_early_stopped",
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
