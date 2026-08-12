#!/usr/bin/env python3
"""Refine the best safe Assist-2 basin toward a single F4 component."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import math
import os
from pathlib import Path
from typing import Any

import m11_v22_certify_rank11 as frozen_rank11
import m11_v22_rank11_assist2_basin_expansion as expansion
import m11_v22_rank11_assist2_similarity_search as search
import m11_v22_rank11_target_offset_search as target_search


SCHEMA = "abts.m11b.v2_2.rank11_assist2_narrow_refinement.v1"
PLAN_SCHEMA = "abts.m11b.v2_2.rank11_assist2_narrow_refinement_plan.v1"

# Parameter order matches search.candidate_key and uses all 21 Halton axes.
FIELDS = (
    ("assist2OffsetCM", (450, 450, 450), 25),
    ("assist2BPlaneDeltaCM", (700, 700), 25),
    ("assist2BPlaneSigmaScale", (0.06,), 0.000001),
    ("assist2VelocityDeltaCMPerSec", (250, 250, 250), 10),
    ("assist3OffsetCM", (500, 500, 500), 25),
    ("assist3BPlaneDeltaCM", (700, 700), 25),
    ("assist3BPlaneSigmaScale", (0.07,), 0.000001),
    ("assist3VelocityDeltaCMPerSec", (250, 250, 250), 10),
    ("targetOffsetCM", (900, 900, 900), 50),
)


def refinement_key(result: dict[str, Any]) -> tuple[Any, ...]:
    if "error" in result:
        return (1, 1, 1, math.inf, math.inf, math.inf, math.inf, math.inf)
    summary, feel = result["closure"], result["handfeel"]
    counts = summary["prefixCounts"]
    components = summary["componentCounts"]
    largest = summary["largestComponentSizes"]
    safe = not (summary["earlyTargetHitCount"]
                or summary["bypassTargetHitCount"]
                or summary["nestingViolations"])
    return (
        0 if result["nominalF4"] else 1,
        0 if feel["passed"] else 1,
        0 if safe else 1,
        components[3],
        counts[3] - largest[3],
        components[2],
        feel["penalty"],
        result["variantSourceHash"],
    )


def load_centers(report_path: Path, count: int) -> list[dict[str, Any]]:
    report = json.loads(report_path.read_text(encoding="utf-8"))
    if report.get("status") != "expansion_early_stopped":
        raise RuntimeError("PriorExpansionStatusMismatch")
    viable = [value for value in report.get("results", [])
              if "closure" in value and value.get("nominalF4")
              and value["handfeel"]["passed"]
              and not value["closure"]["earlyTargetHitCount"]
              and not value["closure"]["bypassTargetHitCount"]
              and not value["closure"]["nestingViolations"]]
    viable.sort(key=refinement_key)
    return [json.loads(json.dumps(value["candidate"]))
            for value in viable[:count]]


def perturb(center: dict[str, Any], units: list[float], scale: float) -> dict[str, Any]:
    candidate = json.loads(json.dumps(center))
    dimension = 0
    for key, spans, quantum in FIELDS:
        values = center[key] if isinstance(center[key], list) else [center[key]]
        adjusted = []
        for value, span in zip(values, spans):
            raw = float(value) + (2 * units[dimension] - 1) * span * scale
            if "SigmaScale" in key:
                adjusted.append(round(expansion.clamp(raw, 0.70, 1.35), 6))
            else:
                adjusted.append(search.q(raw, quantum))
            dimension += 1
        candidate[key] = adjusted if isinstance(center[key], list) else adjusted[0]
    if dimension != len(expansion.PRIMES):
        raise RuntimeError("RefinementDimensionMismatch")
    return candidate


def local_candidates(centers: list[dict[str, Any]], samples: int,
                     scale: float) -> list[dict[str, Any]]:
    candidates: list[dict[str, Any]] = []
    for center_index, center in enumerate(centers):
        candidates.append(json.loads(json.dumps(center)))
        for sample in range(1, samples + 1):
            units = expansion.values(center_index * samples + sample)
            candidates.append(perturb(center, units, scale))
    return candidates


def coordinate_probes(center: dict[str, Any]) -> list[dict[str, Any]]:
    candidates = []
    for dimension in range(len(expansion.PRIMES)):
        for unit in (0.0, 1.0):
            units = [0.5] * len(expansion.PRIMES)
            units[dimension] = unit
            candidates.append(perturb(center, units, 1.0))
    return candidates


def interpolations(primary: dict[str, Any], others: list[dict[str, Any]]) -> list[dict[str, Any]]:
    candidates = []
    for other in others:
        for alpha in (0.25, 0.5, 0.75):
            candidate = json.loads(json.dumps(primary))
            for key, _, quantum in FIELDS:
                left = primary[key] if isinstance(primary[key], list) else [primary[key]]
                right = other[key] if isinstance(other[key], list) else [other[key]]
                mixed = []
                for a, b in zip(left, right):
                    raw = (1 - alpha) * float(a) + alpha * float(b)
                    mixed.append(round(raw, 6) if "SigmaScale" in key
                                 else search.q(raw, quantum))
                candidate[key] = mixed if isinstance(primary[key], list) else mixed[0]
            candidates.append(candidate)
    return candidates


def promote(executable: Path, output: Path, sources: list[dict[str, Any]],
            freeze: dict[str, Any], threads: int, workers: int,
            resume: bool) -> list[dict[str, Any]]:
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
    parser.add_argument("--center-count", type=int, default=10)
    parser.add_argument("--samples-per-center", type=int, default=32)
    parser.add_argument("--fine-center-count", type=int, default=8)
    parser.add_argument("--fine-samples-per-center", type=int, default=24)
    parser.add_argument("--promote-count", type=int, default=8)
    parser.add_argument("--max-workers", type=int, default=4)
    parser.add_argument("--threads-per-worker", type=int, default=2)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if min(args.center_count, args.samples_per_center, args.fine_center_count,
           args.fine_samples_per_center, args.promote_count,
           args.max_workers, args.threads_per_worker) <= 0:
        parser.error("all counts must be positive")
    output = args.output.resolve()
    executable = args.executable.resolve()
    freeze_path = args.freeze.resolve()
    prior = args.prior_report.resolve()
    if not executable.is_file() or not prior.is_file():
        parser.error("executable and prior report must exist")
    freeze, candidate_path, manifest = frozen_rank11.validate_freeze(freeze_path)
    centers = load_centers(prior, args.center_count)
    if not centers:
        parser.error("prior report has no safe handfeel centers")
    initial_candidates = local_candidates(centers, args.samples_per_center, 1.0)
    initial_candidates += coordinate_probes(centers[0])
    initial_candidates += interpolations(centers[0], centers[1:])
    initial_candidates = search.unique(initial_candidates)
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
        "centers": centers,
        "samplesPerCenter": args.samples_per_center,
        "fineCenterCount": args.fine_center_count,
        "fineSamplesPerCenter": args.fine_samples_per_center,
        "initialCandidateCount": len(initial_candidates),
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
    initial = search.dispatch(
        "assist2_narrow_refinement", executable, output / "initial",
        initial_candidates, freeze, manifest, args.max_workers,
        args.threads_per_worker, args.resume)
    initial.sort(key=refinement_key)
    eligible = [value for value in initial if search.eligible(value)]
    fine: list[dict[str, Any]] = []
    if not eligible:
        fine_centers = [value["candidate"] for value in initial
                        if "closure" in value and value.get("nominalF4")
                        and value["handfeel"]["passed"]
                        and not value["closure"]["earlyTargetHitCount"]
                        and not value["closure"]["bypassTargetHitCount"]
                        and not value["closure"]["nestingViolations"]]
        fine_inputs = search.unique(local_candidates(
            fine_centers[:args.fine_center_count],
            args.fine_samples_per_center, 0.35))
        fine = search.dispatch(
            "assist2_fine_refinement", executable, output / "fine",
            fine_inputs, freeze, manifest, args.max_workers,
            args.threads_per_worker, args.resume)
        fine.sort(key=refinement_key)
        eligible = [value for value in fine if search.eligible(value)]
    all_results = sorted(initial + fine, key=refinement_key)
    eligible = sorted([value for value in all_results if search.eligible(value)],
                      key=refinement_key)
    promoted_inputs = eligible[:args.promote_count]
    full = promote(executable, output / "full", promoted_inputs, freeze,
                   args.threads_per_worker, args.max_workers, args.resume)
    accepted = [value for value in full
                if value.get("passedCandidateAcceptance")]
    report = {
        "schema": SCHEMA,
        "status": "candidate_found" if accepted else "refinement_early_stopped",
        "candidateRank": 11,
        "baseCandidateSourceHash": frozen_rank11.EXPECTED_SOURCE_HASH,
        "candidateCount": len(all_results),
        "initialCandidateCount": len(initial),
        "fineCandidateCount": len(fine),
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
