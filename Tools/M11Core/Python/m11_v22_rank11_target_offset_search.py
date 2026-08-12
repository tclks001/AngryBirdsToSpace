#!/usr/bin/env python3
"""Search small Target-only offsets around frozen Rank 11 without mutating it.

The authoritative C++ CLI owns every trajectory, classification, hash, and
connectivity result. Python owns only deterministic candidate generation,
process scheduling, promotion, logs, and fail-closed report assembly.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import subprocess
import sys
from typing import Any

import m11_v22_certify_rank11 as frozen_rank11


SCHEMA = "abts.m11b.v2_2.rank11_target_offset_search.v1"
PLAN_SCHEMA = "abts.m11b.v2_2.rank11_target_offset_plan.v1"
EXPECTED_HALF_PREFIX = [3014, 827, 229, 54]
EXPECTED_HALF_COMPONENTS = [3, 1, 5, 9]
EXPECTED_HALF_HASH = "0x23416df242cb995a"
EXPECTED_HALF_EARLY = 1
TARGET_HIT_RADIUS_CM = 41250.0


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def atomic_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, indent=2, ensure_ascii=False, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def halton(index: int, base: int) -> float:
    value = 0.0
    fraction = 1.0
    while index:
        fraction /= base
        value += fraction * (index % base)
        index //= base
    return value


def rounded_offset(values: tuple[float, float, float]) -> tuple[int, int, int]:
    return tuple(int(round(value / 50.0) * 50) for value in values)


def initial_offsets(extent_cm: int, halton_count: int) -> list[tuple[int, int, int]]:
    offsets: set[tuple[int, int, int]] = {(0, 0, 0)}
    for radius in (500, 1000, extent_cm):
        if radius > extent_cm:
            continue
        offsets.update(itertools.product((-radius, 0, radius), repeat=3))
    for index in range(1, halton_count + 1):
        unit = (halton(index, 2), halton(index, 3), halton(index, 5))
        offsets.add(rounded_offset(tuple(
            (2.0 * component - 1.0) * extent_cm for component in unit
        )))
    return sorted(offsets, key=lambda value: (
        sum(component * component for component in value), value
    ))


def refined_offsets(
    parents: list[dict[str, Any]],
    extent_cm: int,
    known: set[tuple[int, int, int]],
) -> list[tuple[int, int, int]]:
    offsets: set[tuple[int, int, int]] = set()
    for parent in parents:
        center = tuple(int(value) for value in parent["offsetCM"])
        for delta in itertools.product((-500, 0, 500), repeat=3):
            value = tuple(center[axis] + delta[axis] for axis in range(3))
            if max(abs(component) for component in value) <= extent_cm + 500:
                offsets.add(value)
    return sorted(offsets - known, key=lambda value: (
        sum(component * component for component in value), value
    ))


def offset_arguments(offset: tuple[int, int, int]) -> list[str]:
    return [
        "--target-offset-x", str(offset[0]),
        "--target-offset-y", str(offset[1]),
        "--target-offset-z", str(offset[2]),
    ]


def grid_arguments(domain: dict[str, list[float]], steps: list[float]) -> list[str]:
    return [
        "--min-yaw", str(domain["yawDegrees"][0]),
        "--max-yaw", str(domain["yawDegrees"][1]),
        "--yaw-step", str(steps[0]),
        "--min-pitch", str(domain["pitchDegrees"][0]),
        "--max-pitch", str(domain["pitchDegrees"][1]),
        "--pitch-step", str(steps[1]),
        "--min-power", str(domain["power"][0]),
        "--max-power", str(domain["power"][1]),
        "--power-step", str(steps[2]),
    ]


def run_cli(
    command: list[str],
    stdout_path: Path,
    stderr_path: Path,
) -> int:
    stdout_path.parent.mkdir(parents=True, exist_ok=True)
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    stdout_path.write_text(completed.stdout, encoding="utf-8")
    stderr_path.write_text(completed.stderr, encoding="utf-8")
    return completed.returncode


def run_grid(
    executable: Path,
    root: Path,
    offset: tuple[int, int, int],
    domain: dict[str, list[float]],
    steps: list[float],
    threads: int,
    require_nominal: bool,
    resume: bool,
    extra_arguments: list[str] | None = None,
) -> dict[str, Any]:
    summary_path = root / "merged" / "summary.json"
    if resume and summary_path.is_file():
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        if summary.get("targetOffsetCM") == list(offset):
            return summary
        raise RuntimeError(f"ResumeOffsetMismatch:{root}")

    common = [
        "--rank", "11",
        "--threads", str(threads),
        "--shard-index", "0",
        "--shard-count", "1",
        "--checkpoint-every", "512",
        *grid_arguments(domain, steps),
        *offset_arguments(offset),
        *(extra_arguments or []),
    ]
    # The CLI defaults to requiring nominal F4. Only the discovery closure
    # needs the explicit off-grid override; --require-nominal-f4 is a valued
    # parser option and must not be emitted as a flag.
    if not require_nominal:
        common.append("--allow-off-grid-nominal")
    shard = root / "shard_0000"
    code = run_cli(
        [str(executable), "preflight", "--output", str(shard), *common],
        root / "preflight.stdout.log",
        root / "preflight.stderr.log",
    )
    if code != 0:
        raise RuntimeError(f"PreflightFailed:{root}:{code}")
    code = run_cli(
        [str(executable), "merge", "--input-root", str(root),
         "--output", str(root / "merged"), *common],
        root / "merge.stdout.log",
        root / "merge.stderr.log",
    )
    if code not in (0, 2) or not summary_path.is_file():
        raise RuntimeError(f"MergeFailed:{root}:{code}")
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    if summary.get("targetOffsetCM") != list(offset):
        raise RuntimeError(f"SummaryOffsetMismatch:{root}")
    return summary


def nominal_domain(freeze: dict[str, Any]) -> dict[str, list[float]]:
    yaw, pitch, power = freeze["nominalInput"]
    return {
        "yawDegrees": [yaw, yaw],
        "pitchDegrees": [pitch, pitch],
        "power": [power, power],
    }


def evaluate_closure_candidate(
    executable: Path,
    output: Path,
    ordinal: int,
    offset: tuple[int, int, int],
    freeze: dict[str, Any],
    closure_domain: dict[str, list[float]],
    threads: int,
    resume: bool,
) -> dict[str, Any]:
    root = output / f"candidate_{ordinal:04d}"
    try:
        nominal = run_grid(
            executable, root / "nominal", offset, nominal_domain(freeze),
            [1.0, 1.0, 1.0], threads, True, resume,
        )
        closure = run_grid(
            executable, root / "closure", offset, closure_domain,
            [1.0, 1.5, 0.0125], threads, False, resume,
        )
        if nominal.get("variantSourceHash") != closure.get("variantSourceHash"):
            raise RuntimeError("VariantHashMismatch")
        return {
            "ordinal": ordinal,
            "offsetCM": list(offset),
            "variantSourceHash": closure["variantSourceHash"],
            "nominalF4": bool(nominal.get("nominalF4")),
            "nominalResultHash": nominal.get("aggregateSampleHash"),
            "closure": closure,
        }
    except Exception as error:  # fail closed into the report
        return {"ordinal": ordinal, "offsetCM": list(offset), "error": str(error)}


def closure_key(result: dict[str, Any]) -> tuple[Any, ...]:
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
        sum(value * value for value in result["offsetCM"]),
    )


def dispatch_closure(
    executable: Path,
    output: Path,
    offsets: list[tuple[int, int, int]],
    ordinal_start: int,
    freeze: dict[str, Any],
    closure_domain: dict[str, list[float]],
    workers: int,
    threads: int,
    resume: bool,
) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futures = [
            pool.submit(
                evaluate_closure_candidate, executable, output,
                ordinal_start + index, offset, freeze, closure_domain,
                threads, resume,
            )
            for index, offset in enumerate(offsets)
        ]
        for completed, future in enumerate(
            concurrent.futures.as_completed(futures), 1
        ):
            result = future.result()
            results.append(result)
            if completed % 16 == 0 or completed == len(futures):
                best = min(results, key=closure_key)
                summary = best.get("closure", {})
                print(json.dumps({
                    "phase": "closure",
                    "completed": completed,
                    "total": len(futures),
                    "bestOffsetCM": best.get("offsetCM"),
                    "bestNominalF4": best.get("nominalF4"),
                    "bestF4": summary.get("prefixCounts", [None] * 4)[3],
                    "bestComponents": summary.get("componentCounts", [None] * 4)[3],
                    "bestEarly": summary.get("earlyTargetHitCount"),
                }, separators=(",", ":")), flush=True)
    return sorted(results, key=closure_key)


def derive_f3_closure(
    baseline_root: Path,
    summary: dict[str, Any],
) -> dict[str, list[float]]:
    grid = summary["grid"]
    values: list[tuple[float, float, float]] = []
    with (baseline_root / "merged" / "samples.tsv").open(
        "r", encoding="utf-8"
    ) as stream:
        next(stream)
        for line in stream:
            fields = line.split()
            if int(fields[4]) & 4:
                values.append((
                    grid["minYaw"] + int(fields[1]) * grid["yawStep"],
                    grid["minPitch"] + int(fields[2]) * grid["pitchStep"],
                    grid["minPower"] + int(fields[3]) * grid["powerStep"],
                ))
    if len(values) != EXPECTED_HALF_PREFIX[2]:
        raise RuntimeError("BaselineF3SampleCoverageMismatch")
    bounds = []
    for axis, step_name in enumerate(("yawStep", "pitchStep", "powerStep")):
        bounds.append((
            min(value[axis] for value in values) - grid[step_name],
            max(value[axis] for value in values) + grid[step_name],
        ))
    domain = {
        "yawDegrees": [max(-18.0, bounds[0][0]), min(18.0, bounds[0][1])],
        "pitchDegrees": [max(0.0, bounds[1][0]), min(60.0, bounds[1][1])],
        "power": [max(0.0, bounds[2][0]), min(1.0, bounds[2][1])],
    }
    return domain


def baseline_half(
    executable: Path,
    output: Path,
    freeze: dict[str, Any],
    threads: int,
    resume: bool,
) -> tuple[dict[str, Any], dict[str, list[float]]]:
    half = next(stage for stage in freeze["discoveryStages"]
                if stage["name"] == "half_step")
    root = output / "baseline_half_step"
    summary = run_grid(
        executable, root, (0, 0, 0), freeze["domain"], half["steps"],
        threads, False, resume,
    )
    if (summary.get("prefixCounts") != EXPECTED_HALF_PREFIX
            or summary.get("componentCounts") != EXPECTED_HALF_COMPONENTS
            or summary.get("aggregateSampleHash") != EXPECTED_HALF_HASH
            or summary.get("earlyTargetHitCount") != EXPECTED_HALF_EARLY):
        raise RuntimeError("FrozenRank11BaselineMismatch")
    return summary, derive_f3_closure(root, summary)


def full_candidate(
    executable: Path,
    output: Path,
    index: int,
    candidate: dict[str, Any],
    freeze: dict[str, Any],
    threads: int,
    resume: bool,
) -> dict[str, Any]:
    offset = tuple(int(value) for value in candidate["offsetCM"])
    root = output / f"promoted_{index:02d}"
    result = {
        "promotionIndex": index,
        "offsetCM": list(offset),
        "variantSourceHash": candidate["variantSourceHash"],
        "nominalF4": candidate["nominalF4"],
    }
    try:
        for stage in freeze["discoveryStages"]:
            summary = run_grid(
                executable, root / stage["name"], offset, freeze["domain"],
                stage["steps"], threads, False, resume,
            )
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
        result["passedTargetOnlyDiscovery"] = bool(
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
        result["passedTargetOnlyDiscovery"] = False
    atomic_json(root / "promotion_result.json", result)
    return result


def full_key(result: dict[str, Any]) -> tuple[Any, ...]:
    if "error" in result:
        return (1, 1, 1, math.inf, math.inf, math.inf)
    half = result.get("half_step") or result.get("base") or {}
    counts = half.get("prefixCounts", [0, 0, 0, 0])
    components = half.get("componentCounts", [0, 0, 0, 0])
    largest = half.get("largestComponentSizes", [0, 0, 0, 0])
    return (
        0 if result.get("passedTargetOnlyDiscovery") else 1,
        (half.get("earlyTargetHitCount", 10**9)
         + half.get("bypassTargetHitCount", 10**9)),
        counts[3] - largest[3],
        components[3],
        -largest[3],
        sum(value * value for value in result["offsetCM"]),
    )


def build_plan(
    args: argparse.Namespace,
    freeze_path: Path,
    candidate_path: Path,
    executable: Path,
    offsets: list[tuple[int, int, int]],
) -> dict[str, Any]:
    return {
        "schema": PLAN_SCHEMA,
        "candidateRank": 11,
        "baseCandidateSourceHash": frozen_rank11.EXPECTED_SOURCE_HASH,
        "freezeManifest": str(freeze_path),
        "freezeManifestSha256": sha256_file(freeze_path),
        "candidateManifest": str(candidate_path),
        "candidateManifestSha256": sha256_file(candidate_path),
        "executable": str(executable),
        "executableSha256": sha256_file(executable),
        "schedulerSha256": sha256_file(Path(__file__)),
        "extentCM": args.extent_cm,
        "haltonCount": args.halton_count,
        "initialOffsetsCM": [list(value) for value in offsets],
        "refineParents": args.refine_parents,
        "promoteCount": args.promote_count,
        "maxWorkers": args.max_workers,
        "threadsPerWorker": args.threads_per_worker,
        "logicalCpuCount": os.cpu_count(),
        "authority": "ABTSM11V22CertificationCLI",
        "pythonRole": "deterministic_variant_generation_and_process_scheduling_only",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--executable", type=Path,
                        default=frozen_rank11.default_executable())
    parser.add_argument("--freeze", type=Path,
                        default=frozen_rank11.default_freeze_manifest())
    parser.add_argument("--extent-cm", type=int, default=2000)
    parser.add_argument("--halton-count", type=int, default=64)
    parser.add_argument("--refine-parents", type=int, default=4)
    parser.add_argument("--promote-count", type=int, default=8)
    parser.add_argument("--max-workers", type=int, default=4)
    parser.add_argument("--threads-per-worker", type=int, default=2)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if args.extent_cm < 1000 or args.extent_cm > 3000:
        parser.error("extent must stay within the Rank11 small-offset contract")
    if args.halton_count < 0 or args.refine_parents < 0 or args.promote_count <= 0:
        parser.error("candidate counts must be non-negative and promotion non-zero")
    if args.max_workers <= 0 or args.threads_per_worker <= 0:
        parser.error("worker counts must be positive")

    output = args.output.resolve()
    executable = args.executable.resolve()
    freeze_path = args.freeze.resolve()
    if not executable.is_file():
        parser.error(f"executable not found: {executable}")
    freeze, candidate_path, _ = frozen_rank11.validate_freeze(freeze_path)
    offsets = initial_offsets(args.extent_cm, args.halton_count)
    plan = build_plan(args, freeze_path, candidate_path, executable, offsets)
    plan_path = output / "plan.json"
    if plan_path.is_file():
        existing = json.loads(plan_path.read_text(encoding="utf-8"))
        if existing != plan:
            parser.error("existing output plan differs; use a new output root")
    else:
        atomic_json(plan_path, plan)

    baseline, closure_domain = baseline_half(
        executable, output, freeze, args.threads_per_worker, args.resume
    )
    print(json.dumps({
        "phase": "baseline",
        "prefixCounts": baseline["prefixCounts"],
        "componentCounts": baseline["componentCounts"],
        "earlyTargetHitCount": baseline["earlyTargetHitCount"],
        "closureDomain": closure_domain,
    }, separators=(",", ":")), flush=True)

    initial = dispatch_closure(
        executable, output / "initial", offsets, 0, freeze, closure_domain,
        args.max_workers, args.threads_per_worker, args.resume,
    )
    viable_initial = [value for value in initial if "error" not in value]
    parents = viable_initial[:args.refine_parents]
    known = {tuple(value) for value in offsets}
    refinement = refined_offsets(parents, args.extent_cm, known)
    refined = dispatch_closure(
        executable, output / "refined", refinement, len(offsets), freeze,
        closure_domain, args.max_workers, args.threads_per_worker, args.resume,
    ) if refinement else []
    all_closure = sorted(initial + refined, key=closure_key)
    eligible = [
        value for value in all_closure
        if "error" not in value
        and value["nominalF4"]
        and value["closure"]["earlyTargetHitCount"] == 0
        and value["closure"]["bypassTargetHitCount"] == 0
        and value["closure"]["nestingViolations"] == 0
        and value["closure"]["prefixCounts"][3] >= 4
        and value["closure"]["largestComponentSizes"][3] >= 4
    ]
    promoted = eligible[:args.promote_count]
    full_results: list[dict[str, Any]] = []
    with concurrent.futures.ThreadPoolExecutor(
        max_workers=min(2, args.max_workers)
    ) as pool:
        futures = [
            pool.submit(
                full_candidate, executable, output / "full", index, candidate,
                freeze, max(2, args.threads_per_worker * 2), args.resume,
            )
            for index, candidate in enumerate(promoted)
        ]
        for completed, future in enumerate(
            concurrent.futures.as_completed(futures), 1
        ):
            result = future.result()
            full_results.append(result)
            half = result.get("half_step", {})
            print(json.dumps({
                "phase": "full",
                "completed": completed,
                "total": len(futures),
                "offsetCM": result["offsetCM"],
                "passed": result.get("passedTargetOnlyDiscovery"),
                "f4": half.get("prefixCounts", [None] * 4)[3],
                "components": half.get("componentCounts", [None] * 4)[3],
                "early": half.get("earlyTargetHitCount"),
            }, separators=(",", ":")), flush=True)
    full_results.sort(key=full_key)
    report = {
        "schema": SCHEMA,
        "status": (
            "target_only_discovery_passed"
            if any(value.get("passedTargetOnlyDiscovery") for value in full_results)
            else "target_only_early_stopped_no_promoted_variant_passed"
        ),
        "candidateRank": 11,
        "baseCandidateSourceHash": frozen_rank11.EXPECTED_SOURCE_HASH,
        "targetHitRadiusCM": TARGET_HIT_RADIUS_CM,
        "closureDomain": closure_domain,
        "initialCandidateCount": len(initial),
        "refinedCandidateCount": len(refined),
        "eligibleCandidateCount": len(eligible),
        "promotedCandidateCount": len(promoted),
        "closureResults": all_closure,
        "fullResults": full_results,
    }
    atomic_json(output / "search_report.json", report)
    print("RESULT=" + str(output / "search_report.json"), flush=True)
    if full_results:
        print("BEST=" + json.dumps(full_results[0], separators=(",", ":")),
              flush=True)
    return 0 if report["status"] == "target_only_discovery_passed" else 2


if __name__ == "__main__":
    raise SystemExit(main())
