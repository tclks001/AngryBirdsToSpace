#!/usr/bin/env python3
"""Expand Rank 11 Assist-2 basins after the conservative similarity pass."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import math
import os
from pathlib import Path
from typing import Any

import m11_v22_certify_rank11 as frozen_rank11
import m11_v22_rank11_assist2_similarity_search as search
import m11_v22_rank11_target_offset_search as target_search


SCHEMA = "abts.m11b.v2_2.rank11_assist2_basin_expansion.v1"
PLAN_SCHEMA = "abts.m11b.v2_2.rank11_assist2_basin_expansion_plan.v1"
PRIMES = (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37,
          41, 43, 47, 53, 59, 61, 67, 71, 73)


def values(index: int) -> list[float]:
    if index == 0:
        return [0.5] * len(PRIMES)
    return [target_search.halton(index, prime) for prime in PRIMES]


def global_candidate(index: int) -> dict[str, Any]:
    u = values(index)
    c = search.blank()
    c["assist2OffsetCM"] = [search.q((2 * u[i] - 1) * 1500, 25)
                             for i in range(3)]
    c["assist2BPlaneDeltaCM"] = [search.q((2 * u[i + 3] - 1) * 2500, 25)
                                  for i in range(2)]
    c["assist2BPlaneSigmaScale"] = round(0.80 + u[5] * 0.45, 6)
    c["assist2VelocityDeltaCMPerSec"] = [search.q(
        (2 * u[i + 6] - 1) * 800, 10) for i in range(3)]
    return c


def clamp(value: float, lo: float, hi: float) -> float:
    return min(hi, max(lo, value))


def local_candidate(center: dict[str, Any], index: int) -> dict[str, Any]:
    u = values(index)
    c = json.loads(json.dumps(center))
    dimension = 0
    for key, spans, quantum in (
        ("assist2OffsetCM", (750, 750, 750), 25),
        ("assist2BPlaneDeltaCM", (1200, 1200), 25),
        ("assist2VelocityDeltaCMPerSec", (500, 500, 500), 10),
        ("assist3OffsetCM", (900, 900, 900), 25),
        ("assist3BPlaneDeltaCM", (1200, 1200), 25),
        ("assist3VelocityDeltaCMPerSec", (450, 450, 450), 10),
        ("targetOffsetCM", (1800, 1800, 1800), 50),
    ):
        c[key] = [search.q(component + (2 * u[dimension + axis] - 1) * span,
                              quantum)
                  for axis, (component, span) in enumerate(zip(c[key], spans))]
        dimension += len(spans)
    # Use later Halton dimensions for the two sigma scales.
    c["assist2BPlaneSigmaScale"] = round(clamp(
        center["assist2BPlaneSigmaScale"] + (2 * u[19] - 1) * 0.12,
        0.70, 1.35), 6)
    c["assist3BPlaneSigmaScale"] = round(clamp(
        center["assist3BPlaneSigmaScale"] + (2 * u[20] - 1) * 0.15,
        0.70, 1.35), 6)
    return c


def load_centers(report_path: Path, count: int) -> list[dict[str, Any]]:
    report = json.loads(report_path.read_text(encoding="utf-8"))
    if report.get("status") != "early_stopped_no_rank11_similar_candidate":
        raise RuntimeError("PriorSearchStatusMismatch")
    results = [value for key in ("initialResults", "refinedResults",
                                  "downstreamResults")
               for value in report[key]
               if "closure" in value and value["nominalF4"]
               and value["handfeel"]["passed"]]
    results.sort(key=lambda value: (
        value["closure"]["componentCounts"][3],
        value["closure"]["prefixCounts"][3]
            - value["closure"]["largestComponentSizes"][3],
        value["closure"]["componentCounts"][2],
        value["handfeel"]["penalty"],
    ))
    centers: list[dict[str, Any]] = []
    seen: set[tuple[Any, ...]] = set()
    for value in results:
        # Diversify by Assist-2 parent; downstream variants of the same parent
        # should not consume the entire expansion budget.
        c = value["candidate"]
        key = (*c["assist2OffsetCM"], *c["assist2BPlaneDeltaCM"],
               c["assist2BPlaneSigmaScale"],
               *c["assist2VelocityDeltaCMPerSec"])
        if key not in seen:
            seen.add(key)
            centers.append(c)
        if len(centers) == count:
            break
    return centers


def full_key(result: dict[str, Any]) -> tuple[Any, ...]:
    return search.full_key(result)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--prior-report", type=Path, required=True)
    parser.add_argument("--executable", type=Path,
                        default=frozen_rank11.default_executable())
    parser.add_argument("--freeze", type=Path,
                        default=frozen_rank11.default_freeze_manifest())
    parser.add_argument("--global-count", type=int, default=192)
    parser.add_argument("--center-count", type=int, default=10)
    parser.add_argument("--samples-per-center", type=int, default=32)
    parser.add_argument("--promote-count", type=int, default=8)
    parser.add_argument("--max-workers", type=int, default=4)
    parser.add_argument("--threads-per-worker", type=int, default=2)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if min(args.global_count, args.center_count, args.samples_per_center,
           args.promote_count, args.max_workers, args.threads_per_worker) <= 0:
        parser.error("all counts must be positive")
    output, executable, freeze_path, prior = (
        args.output.resolve(), args.executable.resolve(), args.freeze.resolve(),
        args.prior_report.resolve())
    if not executable.is_file() or not prior.is_file():
        parser.error("executable and prior report must exist")
    freeze, candidate_path, manifest = frozen_rank11.validate_freeze(freeze_path)
    centers = load_centers(prior, args.center_count)
    candidates = [global_candidate(index) for index in range(args.global_count)]
    for center_index, center in enumerate(centers):
        candidates.append(json.loads(json.dumps(center)))
        candidates.extend(local_candidate(
            center, center_index * args.samples_per_center + sample
        ) for sample in range(1, args.samples_per_center + 1))
    candidates = search.unique(candidates)
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
        "globalCount": args.global_count,
        "centers": centers,
        "samplesPerCenter": args.samples_per_center,
        "candidateCount": len(candidates),
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
    results = search.dispatch(
        "assist2_basin_expansion", executable, output / "closure", candidates,
        freeze, manifest, args.max_workers, args.threads_per_worker, args.resume)
    eligible_results = [value for value in results if search.eligible(value)]
    promoted_inputs = eligible_results[:args.promote_count]
    full: list[dict[str, Any]] = []
    with concurrent.futures.ThreadPoolExecutor(
        max_workers=min(2, args.max_workers)
    ) as pool:
        futures = [pool.submit(
            search.promote, executable, output / "full", index, source,
            freeze, max(2, args.threads_per_worker * 2), args.resume)
            for index, source in enumerate(promoted_inputs)]
        for completed, future in enumerate(
            concurrent.futures.as_completed(futures), 1
        ):
            result = future.result()
            full.append(result)
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
    full.sort(key=full_key)
    accepted = [value for value in full
                if value.get("passedCandidateAcceptance")]
    report = {
        "schema": SCHEMA,
        "status": "candidate_found" if accepted else "expansion_early_stopped",
        "candidateRank": 11,
        "baseCandidateSourceHash": frozen_rank11.EXPECTED_SOURCE_HASH,
        "candidateCount": len(results),
        "eligibleCandidateCount": len(eligible_results),
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
