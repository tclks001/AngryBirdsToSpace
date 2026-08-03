#!/usr/bin/env python3
"""Repair Rank 10's terminal topology without changing its gravity trajectory."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import math
import pathlib
import subprocess
from typing import Any


PRIMES = (2, 3, 5, 7)


def halton(index: int, base: int) -> float:
    value = 0.0
    fraction = 1.0
    while index:
        fraction /= base
        value += fraction * (index % base)
        index //= base
    return value


def lerp(lo: float, hi: float, alpha: float) -> float:
    return lo + (hi - lo) * alpha


def candidate_for(index: int, extent_cm: float,
                  center_cm: tuple[float, float, float],
                  radius_min_cm: int, radius_max_cm: int) -> dict[str, Any]:
    if index == 0:
        return {"index": 0, "targetOffsetCM": list(center_cm),
                "targetHitRadiusCM": radius_max_cm}
    values = [halton(index, base) for base in PRIMES]
    return {
        "index": index,
        "targetOffsetCM": [
            round(center_cm[0] + lerp(-extent_cm, extent_cm, values[0]), 3),
            round(center_cm[1] + lerp(-extent_cm, extent_cm, values[1]), 3),
            round(center_cm[2] + lerp(-extent_cm, extent_cm, values[2]), 3),
        ],
        "targetHitRadiusCM": int(
            round(lerp(radius_min_cm, radius_max_cm, values[3]) / 125.0) * 125
        ),
    }


def common_args(candidate: dict[str, Any], threads: int,
                phase: str) -> list[str]:
    offset = candidate["targetOffsetCM"]
    if phase == "screen":
        grid = ("0.5", "0.75", "0.025", "0.75", "1")
        angle_bounds = ("-4.25", "2.25", "27.375", "39.375")
    else:
        grid = ("0.25", "0.375", "0.00625", "0.75", "1")
        angle_bounds = ("-4", "2", "27", "39")
    return [
        "--rank", "10", "--threads", str(threads),
        "--shard-index", "0", "--shard-count", "1",
        "--min-yaw", angle_bounds[0], "--max-yaw", angle_bounds[1],
        "--yaw-step", grid[0],
        "--min-pitch", angle_bounds[2], "--max-pitch", angle_bounds[3],
        "--pitch-step", grid[1],
        "--min-power", grid[3], "--max-power", grid[4],
        "--power-step", grid[2],
        "--target-offset-x", str(offset[0]),
        "--target-offset-y", str(offset[1]),
        "--target-offset-z", str(offset[2]),
        "--target-hit-radius", str(candidate["targetHitRadiusCM"]),
        "--checkpoint-every", "2048",
    ]


def run_candidate(executable: pathlib.Path, output: pathlib.Path,
                  candidate: dict[str, Any], phase: str,
                  threads: int) -> dict[str, Any]:
    root = output / f"candidate_{candidate['index']:04d}_{phase}"
    common = common_args(candidate, threads, phase)
    preflight = subprocess.run(
        [str(executable), "preflight", "--output",
         str(root / "shard_0000"), *common],
        capture_output=True, text=True, check=False,
    )
    if preflight.returncode != 0:
        return {**candidate, "phase": phase, "error": "preflight",
                "stderr": preflight.stderr[-1000:]}
    merge = subprocess.run(
        [str(executable), "merge", "--input-root", str(root),
         "--output", str(root / "merged"), *common],
        capture_output=True, text=True, check=False,
    )
    summary_path = root / "merged" / "summary.json"
    if merge.returncode not in (0, 2) or not summary_path.is_file():
        return {**candidate, "phase": phase, "error": "merge",
                "stderr": merge.stderr[-1000:]}
    return {**candidate, "phase": phase,
            "summary": json.loads(summary_path.read_text(encoding="utf-8"))}


def ranking_key(result: dict[str, Any]) -> tuple[Any, ...]:
    if "error" in result:
        return (1, 1, 1, math.inf, math.inf, math.inf, math.inf)
    summary = result["summary"]
    counts = summary["prefixCounts"]
    components = summary["componentCounts"]
    largest = summary["largestComponentSizes"]
    fragments = counts[3] - largest[3]
    offset = result["targetOffsetCM"]
    movement = math.sqrt(sum(value * value for value in offset))
    return (
        0 if summary["earlyTargetHitCount"] == 0 else 1,
        summary["earlyTargetHitCount"],
        0 if summary["nominalF4"] else 1,
        0 if components[3] == 1 else 1,
        fragments,
        components[3],
        -counts[3],
        movement,
        6000 - result["targetHitRadiusCM"],
    )


def execute(executable: pathlib.Path, output: pathlib.Path,
            candidates: list[dict[str, Any]], phase: str,
            workers: int, threads: int) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futures = [pool.submit(run_candidate, executable, output, candidate,
                               phase, threads) for candidate in candidates]
        for ordinal, future in enumerate(
                concurrent.futures.as_completed(futures), 1):
            results.append(future.result())
            if ordinal % 32 == 0 or ordinal == len(futures):
                best = min(results, key=ranking_key)
                summary = best.get("summary", {})
                print(json.dumps({
                    "phase": phase, "completed": ordinal,
                    "total": len(futures), "bestIndex": best.get("index"),
                    "early": summary.get("earlyTargetHitCount"),
                    "f4": summary.get("prefixCounts", [None] * 4)[3],
                    "components": summary.get("componentCounts", [None] * 4)[3],
                }, separators=(",", ":")), flush=True)
    return sorted(results, key=ranking_key)


def load_screen_results(output: pathlib.Path) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    for root in sorted(output.glob("candidate_*_screen")):
        summary_path = root / "merged" / "summary.json"
        if not summary_path.is_file():
            continue
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        index = int(root.name.split("_")[1])
        results.append({
            "index": index,
            "targetOffsetCM": summary["targetOffsetCM"],
            "targetHitRadiusCM": summary["targetHitRadiusCM"],
            "phase": "screen",
            "summary": summary,
        })
    return sorted(results, key=ranking_key)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--candidate-count", type=int, default=512)
    parser.add_argument("--refine-count", type=int, default=16)
    parser.add_argument("--extent-cm", type=float, default=3000.0)
    parser.add_argument("--center-x-cm", type=float, default=0.0)
    parser.add_argument("--center-y-cm", type=float, default=0.0)
    parser.add_argument("--center-z-cm", type=float, default=0.0)
    parser.add_argument("--radius-min-cm", type=int, default=4500)
    parser.add_argument("--radius-max-cm", type=int, default=6000)
    parser.add_argument("--max-workers", type=int, default=3)
    parser.add_argument("--threads-per-worker", type=int, default=4)
    parser.add_argument("--reuse-screen", action="store_true")
    parser.add_argument("--fixed-center-radius-sweep", action="store_true")
    parser.add_argument("--ray-radius-sweep", action="store_true")
    parser.add_argument("--ray-min-scale-cm", type=int, default=3000)
    parser.add_argument("--ray-max-scale-cm", type=int, default=9000)
    parser.add_argument("--ray-step-cm", type=int, default=1000)
    args = parser.parse_args()
    executable = args.executable.resolve()
    output = args.output.resolve()
    if not executable.is_file():
        parser.error(f"executable not found: {executable}")
    if args.candidate_count <= 0 or not 0 < args.refine_count <= args.candidate_count:
        parser.error("candidate/refine counts are invalid")
    if args.extent_cm <= 0:
        parser.error("extent must be positive")
    if not 4500 <= args.radius_min_cm <= args.radius_max_cm <= 12000:
        parser.error("radius range is outside the diagnostic contract")
    output.mkdir(parents=True, exist_ok=True)
    center_cm = (args.center_x_cm, args.center_y_cm, args.center_z_cm)
    if args.ray_radius_sweep:
        length = math.sqrt(sum(value * value for value in center_cm))
        if length <= 0.0 or args.ray_step_cm <= 0:
            parser.error("ray sweep requires a non-zero center direction")
        scales = range(args.ray_min_scale_cm, args.ray_max_scale_cm + 1,
                       args.ray_step_cm)
        radii = range(args.radius_min_cm, args.radius_max_cm + 1, 250)
        candidates = []
        for scale in scales:
            for radius in radii:
                candidates.append({
                    "index": len(candidates),
                    "targetOffsetCM": [
                        round(value / length * scale, 3) for value in center_cm
                    ],
                    "targetHitRadiusCM": radius,
                })
        args.candidate_count = len(candidates)
        args.refine_count = len(candidates)
        screened = execute(executable, output, candidates, "refined",
                           args.max_workers, args.threads_per_worker)
    elif args.fixed_center_radius_sweep:
        radii = list(range(args.radius_min_cm, args.radius_max_cm + 1, 125))
        candidates = [
            {"index": index, "targetOffsetCM": list(center_cm),
             "targetHitRadiusCM": radius}
            for index, radius in enumerate(radii)
        ]
        args.candidate_count = len(candidates)
        args.refine_count = len(candidates)
        screened = execute(executable, output, candidates, "refined",
                           args.max_workers, args.threads_per_worker)
    else:
        candidates = [candidate_for(
            index, args.extent_cm, center_cm,
            args.radius_min_cm, args.radius_max_cm)
                      for index in range(args.candidate_count)]
        screened = load_screen_results(output) if args.reuse_screen else execute(
            executable, output, candidates, "screen",
            args.max_workers, args.threads_per_worker)
    if len(screened) != args.candidate_count:
        parser.error(
            f"screen coverage mismatch: {len(screened)} != {args.candidate_count}"
        )
    promoted = [{key: value for key, value in result.items()
                 if key not in ("phase", "summary")}
                for result in screened if "error" not in result][
                    :args.refine_count]
    refined = screened if (args.fixed_center_radius_sweep
                           or args.ray_radius_sweep) else execute(
        executable, output, promoted, "refined",
        args.max_workers, args.threads_per_worker)
    report = {
        "schema": "abts.m11b.v2_2.rank10_terminal_repair.v1",
        "candidateRank": 10,
        "candidateCount": args.candidate_count,
        "refineCount": len(promoted),
        "extentCM": args.extent_cm,
        "centerCM": center_cm,
        "radiusRangeCM": [args.radius_min_cm, args.radius_max_cm],
        "sampling": "fixed Halton with baseline first",
        "screenResults": screened,
        "refinedResults": refined,
    }
    report_path = output / "rank10_terminal_repair.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n",
                           encoding="utf-8")
    print(f"RESULT={report_path}")
    if refined:
        print("BEST=" + json.dumps(refined[0], separators=(",", ":")))
    return 0 if refined and "error" not in refined[0] else 1


if __name__ == "__main__":
    raise SystemExit(main())
