#!/usr/bin/env python3
"""Find a Rank-11-like candidate by reopening the chain at Assist 2.

The C++ certification CLI remains authoritative for trajectories, prefix
classification, connectivity, and hashes. Python only creates deterministic
variants, schedules them, applies similarity gates, and promotes survivors.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import math
import os
from pathlib import Path
from typing import Any

import m11_v22_certify_rank11 as frozen_rank11
import m11_v22_rank11_target_offset_search as target_search


SCHEMA = "abts.m11b.v2_2.rank11_assist2_similarity_search.v1"
PLAN_SCHEMA = "abts.m11b.v2_2.rank11_assist2_similarity_plan.v1"
PRIMES = (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37)
BASELINE_SCREEN_AIM = [616, 137, 21, 6]


def q(value: float, quantum: float) -> float:
    value = round(value / quantum) * quantum
    return int(value) if quantum >= 1.0 else round(value, 6)


def unit(index: int, dimensions: int) -> list[float]:
    if index == 0:
        return [0.5] * dimensions
    return [target_search.halton(index, PRIMES[i]) for i in range(dimensions)]


def blank() -> dict[str, Any]:
    return {
        "celestialRadialDeltaCM": [0, 0, 0, 0],
        "targetHitRadiusCM": 41250,
        "targetMinimumCorridorQuality": 0.05,
        "assist2OffsetCM": [0, 0, 0],
        "assist2BPlaneDeltaCM": [0, 0],
        "assist2BPlaneSigmaScale": 1.0,
        "assist2VelocityDeltaCMPerSec": [0, 0, 0],
        "assist3OffsetCM": [0, 0, 0],
        "assist3BPlaneDeltaCM": [0, 0],
        "assist3BPlaneSigmaScale": 1.0,
        "assist3VelocityDeltaCMPerSec": [0, 0, 0],
        "targetOffsetCM": [0, 0, 0],
    }


def candidate_key(c: dict[str, Any]) -> tuple[Any, ...]:
    return (
        *c.get("celestialRadialDeltaCM", [0, 0, 0, 0]),
        c.get("targetHitRadiusCM", 41250),
        c.get("targetMinimumCorridorQuality", 0.05),
        *c["assist2OffsetCM"], *c["assist2BPlaneDeltaCM"],
        c["assist2BPlaneSigmaScale"], *c["assist2VelocityDeltaCMPerSec"],
        *c["assist3OffsetCM"], *c["assist3BPlaneDeltaCM"],
        c["assist3BPlaneSigmaScale"], *c["assist3VelocityDeltaCMPerSec"],
        *c["targetOffsetCM"],
    )


def unique(values: list[dict[str, Any]]) -> list[dict[str, Any]]:
    output: list[dict[str, Any]] = []
    seen: set[tuple[Any, ...]] = set()
    for value in values:
        key = candidate_key(value)
        if key not in seen:
            seen.add(key)
            output.append(value)
    return output


def upstream_candidates(count: int) -> list[dict[str, Any]]:
    output = []
    for index in range(count):
        u = unit(index, 9)
        c = blank()
        c["assist2OffsetCM"] = [q((2 * u[i] - 1) * 750, 25) for i in range(3)]
        c["assist2BPlaneDeltaCM"] = [
            q((2 * u[i + 3] - 1) * 1200, 25) for i in range(2)
        ]
        c["assist2BPlaneSigmaScale"] = round(0.90 + u[5] * 0.20, 6)
        c["assist2VelocityDeltaCMPerSec"] = [
            q((2 * u[i + 6] - 1) * 350, 10) for i in range(3)
        ]
        output.append(c)
    return unique(output)


def upstream_refinement(
    parents: list[dict[str, Any]], samples: int,
) -> list[dict[str, Any]]:
    output = []
    for parent_index, parent in enumerate(parents):
        center = parent["candidate"]
        for sample in range(1, samples + 1):
            u = unit(parent_index * samples + sample, 9)
            c = json.loads(json.dumps(center))
            c["assist2OffsetCM"] = [q(
                center["assist2OffsetCM"][i] + (2 * u[i] - 1) * 250, 25
            ) for i in range(3)]
            c["assist2BPlaneDeltaCM"] = [q(
                center["assist2BPlaneDeltaCM"][i]
                + (2 * u[i + 3] - 1) * 400, 25
            ) for i in range(2)]
            c["assist2BPlaneSigmaScale"] = round(min(1.20, max(
                0.80, center["assist2BPlaneSigmaScale"]
                + (2 * u[5] - 1) * 0.035
            )), 6)
            c["assist2VelocityDeltaCMPerSec"] = [q(
                center["assist2VelocityDeltaCMPerSec"][i]
                + (2 * u[i + 6] - 1) * 120, 10
            ) for i in range(3)]
            output.append(c)
    return unique(output)


def downstream_candidates(
    parents: list[dict[str, Any]], samples: int,
) -> list[dict[str, Any]]:
    output = []
    for parent_index, parent in enumerate(parents):
        center = parent["candidate"]
        output.append(json.loads(json.dumps(center)))
        for sample in range(1, samples + 1):
            u = unit(parent_index * samples + sample, 12)
            c = json.loads(json.dumps(center))
            c["assist3OffsetCM"] = [q((2 * u[i] - 1) * 600, 25)
                                     for i in range(3)]
            c["assist3BPlaneDeltaCM"] = [q((2 * u[i + 3] - 1) * 700, 25)
                                          for i in range(2)]
            c["assist3BPlaneSigmaScale"] = round(0.93 + u[5] * 0.14, 6)
            c["assist3VelocityDeltaCMPerSec"] = [q(
                (2 * u[i + 6] - 1) * 180, 10) for i in range(3)
            ]
            c["targetOffsetCM"] = [q((2 * u[i + 9] - 1) * 1200, 50)
                                    for i in range(3)]
            output.append(c)
    return unique(output)


def mapping_arguments(c: dict[str, Any]) -> list[str]:
    a2, bp2, v2 = (c["assist2OffsetCM"], c["assist2BPlaneDeltaCM"],
                   c["assist2VelocityDeltaCMPerSec"])
    a3, bp3, v3 = (c["assist3OffsetCM"], c["assist3BPlaneDeltaCM"],
                   c["assist3VelocityDeltaCMPerSec"])
    radial = c.get("celestialRadialDeltaCM", [0, 0, 0, 0])
    arguments = [
        "--assist1-radial-delta", str(radial[0]),
        "--assist2-radial-delta", str(radial[1]),
        "--assist3-radial-delta", str(radial[2]),
        "--target-radial-delta", str(radial[3]),
        "--target-min-corridor-quality",
        str(c.get("targetMinimumCorridorQuality", 0.05)),
        "--assist2-offset-x", str(a2[0]), "--assist2-offset-y", str(a2[1]),
        "--assist2-offset-z", str(a2[2]),
        "--assist2-bplane-t-delta", str(bp2[0]),
        "--assist2-bplane-r-delta", str(bp2[1]),
        "--assist2-bplane-sigma-scale", str(c["assist2BPlaneSigmaScale"]),
        "--assist2-velocity-delta-x", str(v2[0]),
        "--assist2-velocity-delta-y", str(v2[1]),
        "--assist2-velocity-delta-z", str(v2[2]),
        "--assist3-offset-x", str(a3[0]), "--assist3-offset-y", str(a3[1]),
        "--assist3-offset-z", str(a3[2]),
        "--assist3-bplane-t-delta", str(bp3[0]),
        "--assist3-bplane-r-delta", str(bp3[1]),
        "--assist3-bplane-sigma-scale", str(c["assist3BPlaneSigmaScale"]),
        "--assist3-velocity-delta-x", str(v3[0]),
        "--assist3-velocity-delta-y", str(v3[1]),
        "--assist3-velocity-delta-z", str(v3[2]),
    ]
    radius = float(c.get("targetHitRadiusCM", 41250))
    # The CLI's diagnostic override contract is narrower than Rank 11's
    # authored 41250 cm radius. Omit the switch to preserve the authored value.
    if 4500 <= radius <= 12000:
        arguments.extend(["--target-hit-radius", str(radius)])
    return arguments


def verify_summary(c: dict[str, Any], summary: dict[str, Any]) -> None:
    vector_keys = (
        "assist2OffsetCM", "assist2BPlaneDeltaCM",
        "assist2VelocityDeltaCMPerSec", "assist3OffsetCM",
        "assist3BPlaneDeltaCM", "assist3VelocityDeltaCMPerSec",
        "targetOffsetCM",
    )
    for key in vector_keys:
        actual, expected = summary.get(key), c[key]
        if not isinstance(actual, list) or len(actual) != len(expected) or any(
            not math.isclose(float(a), float(b), abs_tol=1e-8, rel_tol=0.0)
            for a, b in zip(actual, expected)
        ):
            raise RuntimeError(f"{key}Mismatch")
    radial = c.get("celestialRadialDeltaCM", [0, 0, 0, 0])
    actual_radial = summary.get("celestialRadialDeltaCM")
    if not isinstance(actual_radial, list) or len(actual_radial) != 4 or any(
        not math.isclose(float(a), float(b), abs_tol=1e-8, rel_tol=0.0)
        for a, b in zip(actual_radial, radial)
    ):
        raise RuntimeError("celestialRadialDeltaCMMismatch")
    if not math.isclose(float(summary.get("targetHitRadiusCM", math.nan)),
                        float(c.get("targetHitRadiusCM", 41250)),
                        abs_tol=1e-8, rel_tol=0.0):
        raise RuntimeError("targetHitRadiusCMMismatch")
    if not math.isclose(
        float(summary.get("targetMinimumCorridorQuality", math.nan)),
        float(c.get("targetMinimumCorridorQuality", 0.05)),
        abs_tol=1e-8, rel_tol=0.0
    ):
        raise RuntimeError("targetMinimumCorridorQualityMismatch")
    for key in ("assist2BPlaneSigmaScale", "assist3BPlaneSigmaScale"):
        if not math.isclose(float(summary.get(key, math.nan)), float(c[key]),
                            abs_tol=1e-8, rel_tol=0.0):
            raise RuntimeError(f"{key}Mismatch")


def local_domain() -> dict[str, list[float]]:
    return {"yawDegrees": [-8.0, 2.0], "pitchDegrees": [16.5, 34.5],
            "power": [0.625, 1.0]}


def relative_errors(actual: list[float], expected: list[float]) -> list[float]:
    if len(actual) != len(expected):
        return [math.inf]
    return [abs(float(a) - float(b)) / max(abs(float(b)), 1e-9)
            for a, b in zip(actual, expected)]


def handfeel(nominal: dict[str, Any], manifest: dict[str, Any]) -> dict[str, Any]:
    baseline = manifest["maximumPowerLocalEvidence"]
    deflections = relative_errors(
        nominal.get("representativeAssistDeflectionsRadians", []),
        baseline["representativeAssistDeflectionsRadians"])
    durations = relative_errors(
        nominal.get("representativeAssistDurationsSeconds", []),
        baseline["representativeAssistDurationsSeconds"])
    flight = float(nominal.get("representativeFlightTimeSeconds", math.nan))
    flight_error = abs(flight - baseline["representativeFlightTimeSeconds"]) \
        / baseline["representativeFlightTimeSeconds"]
    finite = all(math.isfinite(v) for v in (*deflections, *durations, flight_error))
    return {
        "passed": bool(finite and max(deflections) <= 0.18
                       and max(durations) <= 0.18 and flight_error <= 0.12),
        "deflectionRelativeErrors": deflections,
        "durationRelativeErrors": durations,
        "flightTimeRelativeError": flight_error,
        "penalty": (sum(deflections) + sum(durations) + 2 * flight_error
                    if finite else math.inf),
    }


def evaluate(executable: Path, output: Path, ordinal: int, c: dict[str, Any],
             freeze: dict[str, Any], manifest: dict[str, Any], threads: int,
             resume: bool) -> dict[str, Any]:
    root = output / f"candidate_{ordinal:04d}"
    target = tuple(c["targetOffsetCM"])
    extra = mapping_arguments(c)
    try:
        nominal = target_search.run_grid(
            executable, root / "nominal", target,
            target_search.nominal_domain(freeze), [1, 1, 1], threads, True,
            resume, extra)
        closure = target_search.run_grid(
            executable, root / "closure", target, local_domain(),
            [1, 1.5, 0.0125], threads, False, resume, extra)
        verify_summary(c, nominal)
        verify_summary(c, closure)
        if nominal.get("variantSourceHash") != closure.get("variantSourceHash"):
            raise RuntimeError("VariantHashMismatch")
        return {"ordinal": ordinal, "candidate": c,
                "variantSourceHash": closure["variantSourceHash"],
                "nominalF4": bool(nominal.get("nominalF4")),
                "nominalResultHash": nominal.get("aggregateSampleHash"),
                "handfeel": handfeel(nominal, manifest), "nominal": nominal,
                "closure": closure}
    except Exception as error:
        return {"ordinal": ordinal, "candidate": c, "error": str(error)}


def result_key(result: dict[str, Any]) -> tuple[Any, ...]:
    if "error" in result:
        return (1, 1, 1, 1, math.inf, math.inf, math.inf, math.inf)
    s, h = result["closure"], result["handfeel"]
    counts, components, largest = (s["prefixCounts"], s["componentCounts"],
                                   s["largestComponentSizes"])
    safe = not (s["earlyTargetHitCount"] or s["bypassTargetHitCount"]
                or s["nestingViolations"])
    return (0 if result["nominalF4"] else 1, 0 if h["passed"] else 1,
            0 if safe else 1,
            0 if components[3] == 1 and counts[3] >= 4 else 1,
            counts[3] - largest[3], components[3], components[2], h["penalty"])


def topology_key(result: dict[str, Any]) -> tuple[Any, ...]:
    if "error" in result:
        return (1, math.inf, math.inf, math.inf, math.inf, math.inf)
    s, h = result["closure"], result["handfeel"]
    return (0 if result["nominalF4"] else 1,
            s["earlyTargetHitCount"] + s["bypassTargetHitCount"]
            + s["nestingViolations"],
            s["prefixCounts"][3] - s["largestComponentSizes"][3],
            s["componentCounts"][3], s["componentCounts"][2], h["penalty"])


def select_parents(results: list[dict[str, Any]], count: int) -> list[dict[str, Any]]:
    viable = [r for r in results if "error" not in r and r["nominalF4"]
              and math.isfinite(r["handfeel"]["penalty"])]
    output, seen = [], set()
    for source in (sorted(viable, key=result_key), sorted(viable, key=topology_key)):
        for value in source:
            key = candidate_key(value["candidate"])
            if key not in seen:
                seen.add(key)
                output.append(value)
            if len(output) == count:
                return output
    return output


def dispatch(phase: str, executable: Path, output: Path,
             candidates: list[dict[str, Any]], freeze: dict[str, Any],
             manifest: dict[str, Any], workers: int, threads: int,
             resume: bool) -> list[dict[str, Any]]:
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futures = [pool.submit(evaluate, executable, output, i, c, freeze,
                               manifest, threads, resume)
                   for i, c in enumerate(candidates)]
        for completed, future in enumerate(concurrent.futures.as_completed(futures), 1):
            results.append(future.result())
            if completed % 16 == 0 or completed == len(futures):
                best = min(results, key=result_key)
                s = best.get("closure", {})
                print(json.dumps({"phase": phase, "completed": completed,
                    "total": len(futures), "bestVariant": best.get("variantSourceHash"),
                    "bestNominal": best.get("nominalF4"),
                    "bestHandfeel": best.get("handfeel", {}).get("passed"),
                    "bestF3": s.get("prefixCounts", [None] * 4)[2],
                    "bestF4": s.get("prefixCounts", [None] * 4)[3],
                    "bestF3Components": s.get("componentCounts", [None] * 4)[2],
                    "bestF4Components": s.get("componentCounts", [None] * 4)[3],
                    "bestEarly": s.get("earlyTargetHitCount")},
                    separators=(",", ":")), flush=True)
    return sorted(results, key=result_key)


def eligible(result: dict[str, Any]) -> bool:
    if "error" in result or not result["nominalF4"] or not result["handfeel"]["passed"]:
        return False
    s = result["closure"]
    return bool(s["componentCounts"][3] == 1 and s["prefixCounts"][3] >= 4
                and s["largestComponentSizes"][3] >= 4
                and not s["earlyTargetHitCount"] and not s["bypassTargetHitCount"]
                and not s["nestingViolations"])


def screen_aim(executable: Path, root: Path, c: dict[str, Any],
               variant_hash: str) -> dict[str, Any]:
    output = root / "screen_aim"
    output.mkdir(parents=True, exist_ok=True)
    code = target_search.run_cli([
        str(executable), "screen-aim", "--rank", "11", "--output", str(output),
        "--screen-aim-samples", "5000",
        *target_search.offset_arguments(tuple(c["targetOffsetCM"])),
        *mapping_arguments(c)], root / "screen_aim.stdout.log",
        root / "screen_aim.stderr.log")
    path = output / "screen_aim_summary.json"
    if code or not path.is_file():
        raise RuntimeError(f"ScreenAimFailed:{code}")
    summary = json.loads(path.read_text(encoding="utf-8"))
    if summary.get("variantSourceHash") != variant_hash:
        raise RuntimeError("ScreenAimVariantHashMismatch")
    counts = summary.get("prefixCounts", [0, 0, 0, 0])
    summary["similarToRank11"] = bool(450 <= counts[0] <= 800
        and 80 <= counts[1] <= 220 and 12 <= counts[2] <= 42
        and 4 <= counts[3] <= 15)
    return summary


def promote(executable: Path, output: Path, index: int, source: dict[str, Any],
            freeze: dict[str, Any], threads: int, resume: bool) -> dict[str, Any]:
    c, root = source["candidate"], output / f"promoted_{index:02d}"
    result = {"promotionIndex": index, "candidate": c,
              "variantSourceHash": source["variantSourceHash"],
              "nominalResultHash": source["nominalResultHash"],
              "handfeel": source["handfeel"]}
    try:
        for stage in freeze["discoveryStages"]:
            s = target_search.run_grid(executable, root / stage["name"],
                tuple(c["targetOffsetCM"]), freeze["domain"], stage["steps"],
                threads, False, resume, mapping_arguments(c))
            verify_summary(c, s)
            if s.get("variantSourceHash") != source["variantSourceHash"]:
                raise RuntimeError(f"VariantHashMismatch:{stage['name']}")
            result[stage["name"]] = s
            if (not s["prefixCounts"][3] or s["nestingViolations"]
                    or s["earlyTargetHitCount"] or s["bypassTargetHitCount"]):
                result["earlyStopStage"] = stage["name"]
                break
        half = result.get("half_step")
        result["passedFullDiscovery"] = bool(half
            and half["componentCounts"][3] == 1 and half["prefixCounts"][3] >= 4
            and half["largestComponentSizes"][3] >= 4
            and not half["earlyTargetHitCount"] and not half["bypassTargetHitCount"]
            and not half["nestingViolations"])
        if result["passedFullDiscovery"]:
            result["screenAim"] = screen_aim(executable, root, c,
                                               source["variantSourceHash"])
        result["passedCandidateAcceptance"] = bool(result["passedFullDiscovery"]
            and result.get("screenAim", {}).get("similarToRank11")
            and result["handfeel"]["passed"])
    except Exception as error:
        result.update(error=str(error), passedFullDiscovery=False,
                      passedCandidateAcceptance=False)
    target_search.atomic_json(root / "promotion_result.json", result)
    return result


def full_key(result: dict[str, Any]) -> tuple[Any, ...]:
    s = result.get("half_step") or result.get("base") or {}
    counts, components, largest = (s.get("prefixCounts", [0] * 4),
        s.get("componentCounts", [0] * 4), s.get("largestComponentSizes", [0] * 4))
    return (0 if result.get("passedCandidateAcceptance") else 1,
            0 if result.get("passedFullDiscovery") else 1,
            s.get("earlyTargetHitCount", 10**9), counts[3] - largest[3],
            components[3], result.get("handfeel", {}).get("penalty", math.inf))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--executable", type=Path,
                        default=frozen_rank11.default_executable())
    parser.add_argument("--freeze", type=Path,
                        default=frozen_rank11.default_freeze_manifest())
    parser.add_argument("--upstream-count", type=int, default=256)
    parser.add_argument("--refine-parents", type=int, default=12)
    parser.add_argument("--refine-per-parent", type=int, default=8)
    parser.add_argument("--downstream-parents", type=int, default=12)
    parser.add_argument("--downstream-per-parent", type=int, default=12)
    parser.add_argument("--promote-count", type=int, default=6)
    parser.add_argument("--max-workers", type=int, default=4)
    parser.add_argument("--threads-per-worker", type=int, default=2)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if min(args.upstream_count, args.refine_parents, args.refine_per_parent,
           args.downstream_parents, args.downstream_per_parent,
           args.promote_count, args.max_workers, args.threads_per_worker) <= 0:
        parser.error("all counts must be positive")
    output, executable, freeze_path = (args.output.resolve(),
        args.executable.resolve(), args.freeze.resolve())
    if not executable.is_file():
        parser.error(f"executable not found: {executable}")
    freeze, candidate_path, manifest = frozen_rank11.validate_freeze(freeze_path)
    initial_candidates = upstream_candidates(args.upstream_count)
    plan = {"schema": PLAN_SCHEMA, "candidateRank": 11,
        "baseCandidateSourceHash": frozen_rank11.EXPECTED_SOURCE_HASH,
        "freezeManifestSha256": target_search.sha256_file(freeze_path),
        "candidateManifestSha256": target_search.sha256_file(candidate_path),
        "executable": str(executable), "executableSha256": target_search.sha256_file(executable),
        "schedulerSha256": target_search.sha256_file(Path(__file__)),
        "localDomain": local_domain(), "initialCandidates": initial_candidates,
        "refineParents": args.refine_parents, "refinePerParent": args.refine_per_parent,
        "downstreamParents": args.downstream_parents,
        "downstreamPerParent": args.downstream_per_parent,
        "promoteCount": args.promote_count, "maxWorkers": args.max_workers,
        "threadsPerWorker": args.threads_per_worker, "logicalCpuCount": os.cpu_count(),
        "authority": "ABTSM11V22CertificationCLI"}
    plan_path = output / "plan.json"
    if plan_path.is_file():
        if json.loads(plan_path.read_text(encoding="utf-8")) != plan:
            parser.error("existing plan differs; use a new output root")
    else:
        target_search.atomic_json(plan_path, plan)

    initial = dispatch("assist2_initial", executable, output / "assist2_initial",
        initial_candidates, freeze, manifest, args.max_workers,
        args.threads_per_worker, args.resume)
    parents = select_parents(initial, args.refine_parents)
    refined_candidates = upstream_refinement(parents, args.refine_per_parent)
    refined = dispatch("assist2_refined", executable, output / "assist2_refined",
        refined_candidates, freeze, manifest, args.max_workers,
        args.threads_per_worker, args.resume) if refined_candidates else []
    upstream = sorted(initial + refined, key=result_key)
    parents = select_parents(upstream, args.downstream_parents)
    downstream_inputs = downstream_candidates(parents, args.downstream_per_parent)
    downstream = dispatch("assist3_target_compensation", executable,
        output / "assist3_target_compensation", downstream_inputs, freeze,
        manifest, args.max_workers, args.threads_per_worker,
        args.resume) if downstream_inputs else []
    all_results = sorted(upstream + downstream, key=result_key)
    eligible_results = [r for r in all_results if eligible(r)]
    promoted_inputs = eligible_results[:args.promote_count]
    full = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(2, args.max_workers)) as pool:
        futures = [pool.submit(promote, executable, output / "full", i, source,
            freeze, max(2, args.threads_per_worker * 2), args.resume)
            for i, source in enumerate(promoted_inputs)]
        for completed, future in enumerate(concurrent.futures.as_completed(futures), 1):
            result = future.result(); full.append(result)
            half = result.get("half_step", {})
            print(json.dumps({"phase": "full_discovery", "completed": completed,
                "total": len(futures), "variant": result.get("variantSourceHash"),
                "passed": result.get("passedCandidateAcceptance"),
                "f4": half.get("prefixCounts", [None] * 4)[3],
                "components": half.get("componentCounts", [None] * 4)[3],
                "early": half.get("earlyTargetHitCount"),
                "screenAim": result.get("screenAim", {}).get("prefixCounts")},
                separators=(",", ":")), flush=True)
    full.sort(key=full_key)
    accepted = [r for r in full if r.get("passedCandidateAcceptance")]
    report = {"schema": SCHEMA,
        "status": "candidate_found" if accepted else "early_stopped_no_rank11_similar_candidate",
        "candidateRank": 11, "baseCandidateSourceHash": frozen_rank11.EXPECTED_SOURCE_HASH,
        "baselineScreenAim": BASELINE_SCREEN_AIM,
        "initialCandidateCount": len(initial), "refinedCandidateCount": len(refined),
        "downstreamCandidateCount": len(downstream),
        "eligibleCandidateCount": len(eligible_results),
        "promotedCandidateCount": len(promoted_inputs), "initialResults": initial,
        "refinedResults": refined, "downstreamResults": downstream,
        "fullResults": full, "acceptedCandidate": accepted[0] if accepted else None}
    target_search.atomic_json(output / "search_report.json", report)
    if accepted:
        target_search.atomic_json(output / "candidate_handoff.json", accepted[0])
    print("RESULT=" + str(output / "search_report.json"), flush=True)
    return 0 if accepted else 2


if __name__ == "__main__":
    raise SystemExit(main())
