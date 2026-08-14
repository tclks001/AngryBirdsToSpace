#!/usr/bin/env python3
"""Remove residual F4 islands while preserving the Rank-11-like main basin."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
from itertools import product
from pathlib import Path
from typing import Any

import m11_v22_certify_rank11 as frozen_rank11
import m11_v22_rank11_assist2_basin_expansion as expansion
import m11_v22_rank11_assist2_narrow_refinement as narrow
import m11_v22_rank11_assist2_similarity_search as search
import m11_v22_rank11_target_offset_search as target_search


SCHEMA = "abts.m11b.v2_2.rank11_terminal_island_search.v1"
PLAN_SCHEMA = "abts.m11b.v2_2.rank11_terminal_island_plan.v1"


def load_sources(report_path: Path, count: int) -> list[dict[str, Any]]:
    report = json.loads(report_path.read_text(encoding="utf-8"))
    if report.get("status") != "refinement_early_stopped":
        raise RuntimeError("PriorRefinementStatusMismatch")
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
        key = search.candidate_key(value["candidate"])
        if key not in seen:
            seen.add(key)
            output.append(value)
        if len(output) == count:
            break
    return output


def target_cube(center: dict[str, Any], radius: int, step: int) -> list[dict[str, Any]]:
    deltas = range(-radius, radius + 1, step)
    output = []
    for delta in product(deltas, repeat=3):
        candidate = json.loads(json.dumps(center))
        candidate["targetOffsetCM"] = [search.q(value + change, 50)
                                        for value, change in zip(
                                            center["targetOffsetCM"], delta)]
        output.append(candidate)
    return output


def axis_probes(sources: list[dict[str, Any]], step: int) -> list[dict[str, Any]]:
    output = []
    for source in sources:
        center = source["candidate"]
        output.append(json.loads(json.dumps(center)))
        for axis in range(3):
            for direction in (-1, 1):
                candidate = json.loads(json.dumps(center))
                candidate["targetOffsetCM"][axis] = search.q(
                    center["targetOffsetCM"][axis] + direction * step, 50)
                output.append(candidate)
    return output


def terminal_refinement(sources: list[dict[str, Any]], samples: int) -> list[dict[str, Any]]:
    output = []
    for parent_index, source in enumerate(sources):
        center = source["candidate"]
        output.append(json.loads(json.dumps(center)))
        for sample in range(1, samples + 1):
            u = expansion.values(parent_index * samples + sample)
            candidate = json.loads(json.dumps(center))
            candidate["assist3OffsetCM"] = [search.q(
                value + (2 * u[axis] - 1) * 250, 25)
                for axis, value in enumerate(center["assist3OffsetCM"])]
            candidate["assist3BPlaneDeltaCM"] = [search.q(
                value + (2 * u[axis + 3] - 1) * 450, 25)
                for axis, value in enumerate(center["assist3BPlaneDeltaCM"])]
            candidate["assist3BPlaneSigmaScale"] = round(expansion.clamp(
                center["assist3BPlaneSigmaScale"] + (2 * u[5] - 1) * 0.045,
                0.70, 1.35), 6)
            candidate["assist3VelocityDeltaCMPerSec"] = [search.q(
                value + (2 * u[axis + 6] - 1) * 140, 10)
                for axis, value in enumerate(
                    center["assist3VelocityDeltaCMPerSec"])]
            candidate["targetOffsetCM"] = [search.q(
                value + (2 * u[axis + 9] - 1) * 250, 50)
                for axis, value in enumerate(center["targetOffsetCM"])]
            output.append(candidate)
    return output


def run_promotions(executable: Path, output: Path,
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
    parser.add_argument("--target-radius", type=int, default=600)
    parser.add_argument("--target-step", type=int, default=300)
    parser.add_argument("--terminal-parent-count", type=int, default=8)
    parser.add_argument("--terminal-samples-per-parent", type=int, default=24)
    parser.add_argument("--promote-count", type=int, default=8)
    parser.add_argument("--max-workers", type=int, default=4)
    parser.add_argument("--threads-per-worker", type=int, default=2)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if min(args.source_count, args.target_radius, args.target_step,
           args.terminal_parent_count, args.terminal_samples_per_parent,
           args.promote_count, args.max_workers, args.threads_per_worker) <= 0:
        parser.error("all counts and distances must be positive")
    if args.target_radius % args.target_step:
        parser.error("target radius must be divisible by target step")
    output = args.output.resolve()
    executable = args.executable.resolve()
    freeze_path = args.freeze.resolve()
    prior = args.prior_report.resolve()
    if not executable.is_file() or not prior.is_file():
        parser.error("executable and prior report must exist")
    freeze, candidate_path, manifest = frozen_rank11.validate_freeze(freeze_path)
    sources = load_sources(prior, args.source_count)
    if not sources:
        parser.error("prior report has no safe handfeel sources")
    target_inputs = target_cube(sources[0]["candidate"],
                                args.target_radius, args.target_step)
    target_inputs += axis_probes(sources[1:], args.target_step)
    target_inputs = search.unique(target_inputs)
    plan = {
        "schema": PLAN_SCHEMA,
        "candidateRank": 11,
        "baseCandidateSourceHash": frozen_rank11.EXPECTED_SOURCE_HASH,
        "freezeManifestSha256": target_search.sha256_file(freeze_path),
        "candidateManifestSha256": target_search.sha256_file(candidate_path),
        "priorReport": str(prior),
        "priorReportSha256": target_search.sha256_file(prior),
        "executable": str(executable),
        "executableSha256": target_search.sha256_file(executable),
        "schedulerSha256": target_search.sha256_file(Path(__file__)),
        "sources": sources,
        "targetRadiusCM": args.target_radius,
        "targetStepCM": args.target_step,
        "targetCandidateCount": len(target_inputs),
        "terminalParentCount": args.terminal_parent_count,
        "terminalSamplesPerParent": args.terminal_samples_per_parent,
        "promoteCount": args.promote_count,
        "maxWorkers": args.max_workers,
        "threadsPerWorker": args.threads_per_worker,
        "logicalCpuCount": os.cpu_count(),
        "authority": "ABTSM11V22CertificationCLI",
    }
    plan_path = output / "plan.json"
    if plan_path.is_file():
        if json.loads(plan_path.read_text(encoding="utf-8")) != plan:
            parser.error("existing plan differs; use a new output root")
    else:
        target_search.atomic_json(plan_path, plan)
    target_results = search.dispatch(
        "target_island_shave", executable, output / "target", target_inputs,
        freeze, manifest, args.max_workers, args.threads_per_worker, args.resume)
    target_results.sort(key=narrow.refinement_key)
    eligible = [value for value in target_results if search.eligible(value)]
    terminal_results: list[dict[str, Any]] = []
    if not eligible:
        terminal_parents = [value for value in target_results
                            if "closure" in value and value.get("nominalF4")
                            and value["handfeel"]["passed"]
                            and not value["closure"]["earlyTargetHitCount"]
                            and not value["closure"]["bypassTargetHitCount"]
                            and not value["closure"]["nestingViolations"]]
        terminal_inputs = search.unique(terminal_refinement(
            terminal_parents[:args.terminal_parent_count],
            args.terminal_samples_per_parent))
        terminal_results = search.dispatch(
            "assist3_terminal_refinement", executable, output / "terminal",
            terminal_inputs, freeze, manifest, args.max_workers,
            args.threads_per_worker, args.resume)
        terminal_results.sort(key=narrow.refinement_key)
    all_results = sorted(target_results + terminal_results,
                         key=narrow.refinement_key)
    eligible = [value for value in all_results if search.eligible(value)]
    promoted_inputs = eligible[:args.promote_count]
    full = run_promotions(executable, output / "full", promoted_inputs,
                          freeze, args.threads_per_worker, args.max_workers,
                          args.resume)
    accepted = [value for value in full
                if value.get("passedCandidateAcceptance")]
    report = {
        "schema": SCHEMA,
        "status": "candidate_found" if accepted else "island_search_early_stopped",
        "candidateRank": 11,
        "baseCandidateSourceHash": frozen_rank11.EXPECTED_SOURCE_HASH,
        "candidateCount": len(all_results),
        "targetCandidateCount": len(target_results),
        "terminalCandidateCount": len(terminal_results),
        "eligibleCandidateCount": len(eligible),
        "promotedCandidateCount": len(promoted_inputs),
        "results": all_results,
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
