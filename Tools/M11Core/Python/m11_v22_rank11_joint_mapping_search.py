#!/usr/bin/env python3
"""Search small Assist-3 B-plane plus Target variants around frozen Rank 11."""

from __future__ import annotations

import argparse
import concurrent.futures
import itertools
import json
import math
import os
from pathlib import Path
from typing import Any

import m11_v22_certify_rank11 as frozen_rank11
import m11_v22_rank11_target_offset_search as target_search


SCHEMA = "abts.m11b.v2_2.rank11_joint_mapping_search.v1"
PLAN_SCHEMA = "abts.m11b.v2_2.rank11_joint_mapping_plan.v1"


def mapping_arguments(candidate: dict[str, Any]) -> list[str]:
    bplane = candidate["assist3BPlaneDeltaCM"]
    return [
        "--assist3-bplane-t-delta", str(bplane[0]),
        "--assist3-bplane-r-delta", str(bplane[1]),
        "--assist3-bplane-sigma-scale", str(candidate["assist3BPlaneSigmaScale"]),
    ]


def candidate_key(candidate: dict[str, Any]) -> tuple[Any, ...]:
    return (
        tuple(candidate["targetOffsetCM"]),
        tuple(candidate["assist3BPlaneDeltaCM"]),
        candidate["assist3BPlaneSigmaScale"],
    )


def make_candidates(count: int) -> list[dict[str, Any]]:
    best_target = (-950, -2300, -850)
    candidates: list[dict[str, Any]] = []
    seen: set[tuple[Any, ...]] = set()

    def add(target: tuple[int, int, int], bplane: tuple[int, int], sigma: float) -> None:
        candidate = {
            "targetOffsetCM": list(target),
            "assist3BPlaneDeltaCM": list(bplane),
            "assist3BPlaneSigmaScale": round(sigma, 6),
        }
        key = candidate_key(candidate)
        if key not in seen:
            seen.add(key)
            candidates.append(candidate)

    for target in ((0, 0, 0), best_target):
        for bplane in itertools.product((-600, 0, 600), repeat=2):
            for sigma in (0.94, 1.0, 1.06):
                add(target, bplane, sigma)

    primes = (2, 3, 5, 7, 11, 13)
    for index in range(1, count + 1):
        values = [target_search.halton(index, prime) for prime in primes]
        target = tuple(int(round((best_target[axis]
            + (2.0 * values[axis] - 1.0) * 1000.0) / 50.0) * 50)
            for axis in range(3))
        bplane = tuple(int(round((2.0 * values[axis + 3] - 1.0) * 600.0 / 25.0) * 25)
                       for axis in range(2))
        sigma = 0.90 + values[5] * 0.20
        add(target, bplane, sigma)
    return candidates


def local_domain() -> dict[str, list[float]]:
    return {
        "yawDegrees": [-8.0, 2.0],
        "pitchDegrees": [16.5, 34.5],
        "power": [0.625, 1.0],
    }


def verify_summary(candidate: dict[str, Any], summary: dict[str, Any]) -> None:
    if summary.get("targetOffsetCM") != candidate["targetOffsetCM"]:
        raise RuntimeError("TargetOffsetMismatch")
    if summary.get("assist3BPlaneDeltaCM") != candidate["assist3BPlaneDeltaCM"]:
        raise RuntimeError("Assist3BPlaneDeltaMismatch")
    if not math.isclose(
        float(summary.get("assist3BPlaneSigmaScale", 0.0)),
        float(candidate["assist3BPlaneSigmaScale"]),
        rel_tol=0.0, abs_tol=1e-9,
    ):
        raise RuntimeError("Assist3BPlaneSigmaMismatch")


def evaluate(
    executable: Path,
    output: Path,
    ordinal: int,
    candidate: dict[str, Any],
    freeze: dict[str, Any],
    threads: int,
    resume: bool,
) -> dict[str, Any]:
    root = output / f"candidate_{ordinal:04d}"
    target = tuple(candidate["targetOffsetCM"])
    extra = mapping_arguments(candidate)
    try:
        nominal = target_search.run_grid(
            executable, root / "nominal", target,
            target_search.nominal_domain(freeze), [1.0, 1.0, 1.0],
            threads, True, resume, extra,
        )
        closure = target_search.run_grid(
            executable, root / "closure", target, local_domain(),
            [1.0, 1.5, 0.0125], threads, False, resume, extra,
        )
        verify_summary(candidate, nominal)
        verify_summary(candidate, closure)
        if nominal.get("variantSourceHash") != closure.get("variantSourceHash"):
            raise RuntimeError("VariantHashMismatch")
        return {
            "ordinal": ordinal,
            **candidate,
            "variantSourceHash": closure["variantSourceHash"],
            "nominalF4": bool(nominal.get("nominalF4")),
            "closure": closure,
        }
    except Exception as error:
        return {"ordinal": ordinal, **candidate, "error": str(error)}


def result_key(result: dict[str, Any]) -> tuple[Any, ...]:
    if "error" in result:
        return (1, 1, 1, math.inf, math.inf, math.inf, math.inf)
    summary = result["closure"]
    counts = summary["prefixCounts"]
    components = summary["componentCounts"]
    largest = summary["largestComponentSizes"]
    fragments = counts[3] - largest[3]
    sufficient = counts[3] >= 4 and largest[3] >= 4
    return (
        0 if result["nominalF4"] else 1,
        0 if summary["earlyTargetHitCount"] == 0
            and summary["bypassTargetHitCount"] == 0
            and summary["nestingViolations"] == 0 else 1,
        0 if components[3] == 1 and sufficient else 1,
        fragments,
        components[3],
        -largest[3],
        sum(value * value for value in result["targetOffsetCM"])
        + sum(value * value for value in result["assist3BPlaneDeltaCM"]),
    )


def dispatch(
    executable: Path,
    output: Path,
    candidates: list[dict[str, Any]],
    freeze: dict[str, Any],
    workers: int,
    threads: int,
    resume: bool,
) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futures = [pool.submit(
            evaluate, executable, output, index, candidate, freeze,
            threads, resume,
        ) for index, candidate in enumerate(candidates)]
        for completed, future in enumerate(
            concurrent.futures.as_completed(futures), 1
        ):
            result = future.result()
            results.append(result)
            if completed % 16 == 0 or completed == len(futures):
                best = min(results, key=result_key)
                summary = best.get("closure", {})
                print(json.dumps({
                    "phase": "joint_closure",
                    "completed": completed,
                    "total": len(futures),
                    "bestTargetOffsetCM": best.get("targetOffsetCM"),
                    "bestBPlaneDeltaCM": best.get("assist3BPlaneDeltaCM"),
                    "bestSigma": best.get("assist3BPlaneSigmaScale"),
                    "bestNominalF4": best.get("nominalF4"),
                    "bestF4": summary.get("prefixCounts", [None] * 4)[3],
                    "bestComponents": summary.get("componentCounts", [None] * 4)[3],
                    "bestEarly": summary.get("earlyTargetHitCount"),
                }, separators=(",", ":")), flush=True)
    return sorted(results, key=result_key)


def full_candidate(
    executable: Path,
    output: Path,
    index: int,
    candidate: dict[str, Any],
    freeze: dict[str, Any],
    threads: int,
    resume: bool,
) -> dict[str, Any]:
    target = tuple(candidate["targetOffsetCM"])
    extra = mapping_arguments(candidate)
    result: dict[str, Any] = {
        "promotionIndex": index,
        "targetOffsetCM": candidate["targetOffsetCM"],
        "assist3BPlaneDeltaCM": candidate["assist3BPlaneDeltaCM"],
        "assist3BPlaneSigmaScale": candidate["assist3BPlaneSigmaScale"],
        "variantSourceHash": candidate["variantSourceHash"],
        "nominalF4": candidate["nominalF4"],
    }
    try:
        for stage in freeze["discoveryStages"]:
            summary = target_search.run_grid(
                executable, output / f"promoted_{index:02d}" / stage["name"],
                target, freeze["domain"], stage["steps"], threads, False,
                resume, extra,
            )
            verify_summary(candidate, summary)
            if summary.get("variantSourceHash") != candidate["variantSourceHash"]:
                raise RuntimeError(f"VariantHashMismatch:{stage['name']}")
            result[stage["name"]] = summary
            if (not summary["prefixCounts"][3]
                    or summary["nestingViolations"]
                    or summary["earlyTargetHitCount"]
                    or summary["bypassTargetHitCount"]):
                result["earlyStopStage"] = stage["name"]
                break
        half = result.get("half_step")
        result["passedJointDiscovery"] = bool(
            half
            and half["componentCounts"][3] == 1
            and half["prefixCounts"][3] >= 4
            and half["largestComponentSizes"][3] >= 4
            and half["earlyTargetHitCount"] == 0
            and half["bypassTargetHitCount"] == 0
            and half["nestingViolations"] == 0
        )
    except Exception as error:
        result["error"] = str(error)
        result["passedJointDiscovery"] = False
    target_search.atomic_json(
        output / f"promoted_{index:02d}" / "promotion_result.json", result
    )
    return result


def full_key(result: dict[str, Any]) -> tuple[Any, ...]:
    if "error" in result:
        return (1, 1, math.inf, math.inf, math.inf)
    summary = result.get("half_step") or result.get("base") or {}
    counts = summary.get("prefixCounts", [0, 0, 0, 0])
    components = summary.get("componentCounts", [0, 0, 0, 0])
    largest = summary.get("largestComponentSizes", [0, 0, 0, 0])
    return (
        0 if result.get("passedJointDiscovery") else 1,
        summary.get("earlyTargetHitCount", 10**9)
            + summary.get("bypassTargetHitCount", 10**9),
        counts[3] - largest[3],
        components[3],
        -largest[3],
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--target-report", type=Path, required=True)
    parser.add_argument("--executable", type=Path,
                        default=frozen_rank11.default_executable())
    parser.add_argument("--freeze", type=Path,
                        default=frozen_rank11.default_freeze_manifest())
    parser.add_argument("--halton-count", type=int, default=128)
    parser.add_argument("--promote-count", type=int, default=8)
    parser.add_argument("--max-workers", type=int, default=4)
    parser.add_argument("--threads-per-worker", type=int, default=2)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    executable = args.executable.resolve()
    freeze_path = args.freeze.resolve()
    target_report = args.target_report.resolve()
    if not executable.is_file() or not target_report.is_file():
        parser.error("executable and target report must exist")
    freeze, candidate_path, _ = frozen_rank11.validate_freeze(freeze_path)
    previous = json.loads(target_report.read_text(encoding="utf-8"))
    if previous.get("status") != "target_only_early_stopped_no_promoted_variant_passed":
        parser.error("target-only report has not reached the required early stop")
    candidates = make_candidates(args.halton_count)
    plan = {
        "schema": PLAN_SCHEMA,
        "candidateRank": 11,
        "baseCandidateSourceHash": frozen_rank11.EXPECTED_SOURCE_HASH,
        "freezeManifestSha256": target_search.sha256_file(freeze_path),
        "candidateManifestSha256": target_search.sha256_file(candidate_path),
        "targetOnlyReport": str(target_report),
        "targetOnlyReportSha256": target_search.sha256_file(target_report),
        "executable": str(executable),
        "executableSha256": target_search.sha256_file(executable),
        "schedulerSha256": target_search.sha256_file(Path(__file__)),
        "candidateCount": len(candidates),
        "candidates": candidates,
        "localDomain": local_domain(),
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

    closure = dispatch(
        executable, output / "closure", candidates, freeze,
        args.max_workers, args.threads_per_worker, args.resume,
    )
    eligible = [
        value for value in closure
        if "error" not in value
        and value["nominalF4"]
        and value["closure"]["earlyTargetHitCount"] == 0
        and value["closure"]["bypassTargetHitCount"] == 0
        and value["closure"]["nestingViolations"] == 0
        and value["closure"]["prefixCounts"][3] >= 4
        and value["closure"]["largestComponentSizes"][3] >= 4
    ]
    promoted = eligible[:args.promote_count]
    full: list[dict[str, Any]] = []
    with concurrent.futures.ThreadPoolExecutor(
        max_workers=min(2, args.max_workers)
    ) as pool:
        futures = [pool.submit(
            full_candidate, executable, output / "full", index, candidate,
            freeze, max(2, args.threads_per_worker * 2), args.resume,
        ) for index, candidate in enumerate(promoted)]
        for completed, future in enumerate(
            concurrent.futures.as_completed(futures), 1
        ):
            result = future.result()
            full.append(result)
            half = result.get("half_step", {})
            print(json.dumps({
                "phase": "joint_full",
                "completed": completed,
                "total": len(futures),
                "targetOffsetCM": result["targetOffsetCM"],
                "bplaneDeltaCM": result["assist3BPlaneDeltaCM"],
                "sigma": result["assist3BPlaneSigmaScale"],
                "passed": result.get("passedJointDiscovery"),
                "f4": half.get("prefixCounts", [None] * 4)[3],
                "components": half.get("componentCounts", [None] * 4)[3],
                "early": half.get("earlyTargetHitCount"),
            }, separators=(",", ":")), flush=True)
    full.sort(key=full_key)
    report = {
        "schema": SCHEMA,
        "status": (
            "joint_discovery_passed"
            if any(value.get("passedJointDiscovery") for value in full)
            else "joint_early_stopped_no_promoted_variant_passed"
        ),
        "candidateRank": 11,
        "baseCandidateSourceHash": frozen_rank11.EXPECTED_SOURCE_HASH,
        "candidateCount": len(closure),
        "eligibleCandidateCount": len(eligible),
        "promotedCandidateCount": len(promoted),
        "closureResults": closure,
        "fullResults": full,
    }
    target_search.atomic_json(output / "search_report.json", report)
    print("RESULT=" + str(output / "search_report.json"), flush=True)
    if full:
        print("BEST=" + json.dumps(full[0], separators=(",", ":")), flush=True)
    return 0 if report["status"] == "joint_discovery_passed" else 2


if __name__ == "__main__":
    raise SystemExit(main())
