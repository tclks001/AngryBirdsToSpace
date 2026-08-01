#!/usr/bin/env python3
"""Search Rank 3's assist-3-to-UFO terminal mapping via the C++ authority."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import math
import pathlib
import subprocess
from typing import Any


PRIMES = (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37)


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


def make_candidate(index: int) -> dict[str, Any]:
    if index == 0:
        values = [0.5] * len(PRIMES)
    else:
        values = [halton(index, base) for base in PRIMES]
    return {
        "index": index,
        "assist3OffsetCM": [
            round(lerp(-2000, 2000, values[0]), 3),
            round(lerp(-2000, 2000, values[1]), 3),
            round(lerp(-2000, 2000, values[2]), 3),
        ],
        "bPlaneDeltaCM": [
            round(lerp(-2500, 2500, values[3]), 3),
            round(lerp(-2500, 2500, values[4]), 3),
        ],
        "sigmaScale": round(lerp(0.75, 1.30, values[5]), 6),
        "velocityDeltaCMPerSec": [
            round(lerp(-1000, 1000, values[6]), 3),
            round(lerp(-1000, 1000, values[7]), 3),
            round(lerp(-1000, 1000, values[8]), 3),
        ],
        "targetOffsetCM": [
            round(lerp(1300, 3700, values[9]), 3),
            round(lerp(-200, 2200, values[10]), 3),
            round(lerp(-9900, -8100, values[11]), 3),
        ],
    }


def make_local_candidate(
    index: int, center: dict[str, Any], center_index: int,
    span_scale: float,
) -> dict[str, Any]:
    values = [0.5] * len(PRIMES) if index == 0 else [
        halton(index, base) for base in PRIMES
    ]
    spans = {
        "assist3OffsetCM": (800.0, 800.0, 800.0),
        "bPlaneDeltaCM": (1000.0, 1000.0),
        "velocityDeltaCMPerSec": (500.0, 500.0, 500.0),
        "targetOffsetCM": (800.0, 800.0, 800.0),
    }
    dimension = 0
    candidate: dict[str, Any] = {
        "index": index,
        "parentIndex": center_index,
    }
    for key in (
        "assist3OffsetCM", "bPlaneDeltaCM",
        "velocityDeltaCMPerSec", "targetOffsetCM",
    ):
        candidate[key] = []
        for component, span in zip(center[key], spans[key]):
            span *= span_scale
            delta = lerp(-span, span, values[dimension])
            candidate[key].append(round(component + delta, 3))
            dimension += 1
    sigma_delta = lerp(
        -0.15 * span_scale, 0.15 * span_scale, values[dimension]
    )
    candidate["sigmaScale"] = round(
        min(1.50, max(0.65, center["sigmaScale"] + sigma_delta)), 6
    )
    return candidate


def name_for(candidate: dict[str, Any], phase: str) -> str:
    return f"candidate_{candidate['index']:04d}_{phase}"


def run_candidate(
    executable: pathlib.Path,
    output_root: pathlib.Path,
    candidate: dict[str, Any],
    phase: str,
    threads: int,
) -> dict[str, Any]:
    root = output_root / name_for(candidate, phase)
    shard = root / "shard_0000"
    merged = root / "merged"
    if phase == "sparse":
        grid = ("1", "1.5", "0.025")
    else:
        grid = ("0.5", "0.75", "0.00625")
    a3 = candidate["assist3OffsetCM"]
    bp = candidate["bPlaneDeltaCM"]
    velocity = candidate["velocityDeltaCMPerSec"]
    target = candidate["targetOffsetCM"]
    a2 = candidate.get("assist2OffsetCM", [0.0, 0.0, 0.0])
    bp2 = candidate.get("assist2BPlaneDeltaCM", [0.0, 0.0])
    sigma2 = candidate.get("assist2BPlaneSigmaScale", 1.0)
    velocity2 = candidate.get(
        "assist2VelocityDeltaCMPerSec", [0.0, 0.0, 0.0]
    )
    common = [
        "--rank", "3", "--threads", str(threads),
        "--shard-index", "0", "--shard-count", "1",
        "--yaw-step", grid[0], "--pitch-step", grid[1],
        "--power-step", grid[2], "--min-yaw", "-4", "--max-yaw", "2",
        "--min-pitch", "19.5", "--max-pitch", "34.5",
        "--min-power", "0.875", "--max-power", "1",
        "--target-offset-x", str(target[0]),
        "--target-offset-y", str(target[1]),
        "--target-offset-z", str(target[2]),
        "--target-hit-radius", "12000",
        "--assist2-offset-x", str(a2[0]),
        "--assist2-offset-y", str(a2[1]),
        "--assist2-offset-z", str(a2[2]),
        "--assist2-bplane-t-delta", str(bp2[0]),
        "--assist2-bplane-r-delta", str(bp2[1]),
        "--assist2-bplane-sigma-scale", str(sigma2),
        "--assist2-velocity-delta-x", str(velocity2[0]),
        "--assist2-velocity-delta-y", str(velocity2[1]),
        "--assist2-velocity-delta-z", str(velocity2[2]),
        "--assist3-offset-x", str(a3[0]),
        "--assist3-offset-y", str(a3[1]),
        "--assist3-offset-z", str(a3[2]),
        "--assist3-bplane-t-delta", str(bp[0]),
        "--assist3-bplane-r-delta", str(bp[1]),
        "--assist3-bplane-sigma-scale", str(candidate["sigmaScale"]),
        "--assist3-velocity-delta-x", str(velocity[0]),
        "--assist3-velocity-delta-y", str(velocity[1]),
        "--assist3-velocity-delta-z", str(velocity[2]),
        "--checkpoint-every", "1024",
    ]
    preflight = subprocess.run(
        [str(executable), "preflight", "--output", str(shard), *common],
        capture_output=True, text=True, check=False,
    )
    if preflight.returncode != 0:
        return {**candidate, "phase": phase, "error": "preflight",
                "stderr": preflight.stderr[-1000:]}
    merge = subprocess.run(
        [str(executable), "merge", "--input-root", str(root),
         "--output", str(merged), *common],
        capture_output=True, text=True, check=False,
    )
    summary_path = merged / "summary.json"
    if merge.returncode not in (0, 2) or not summary_path.is_file():
        return {**candidate, "phase": phase, "error": "merge",
                "stderr": merge.stderr[-1000:]}
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    return {**candidate, "phase": phase, "summary": summary}


def rank_key(result: dict[str, Any]) -> tuple[Any, ...]:
    if "error" in result:
        return (1, 1, 1, math.inf, math.inf, math.inf)
    summary = result["summary"]
    count = summary["prefixCounts"][3]
    largest = summary["largestComponentSizes"][3]
    fragments = count - largest
    components = summary["componentCounts"]
    return (
        0 if summary["nominalF4"] else 1,
        0 if components[0] == 1 and components[1] == 1 else 1,
        0 if count >= 4 else 1,
        fragments / max(1, count),
        components[3],
        -largest,
    )


def execute_phase(
    executable: pathlib.Path,
    output: pathlib.Path,
    candidates: list[dict[str, Any]],
    phase: str,
    workers: int,
    threads: int,
) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futures = [
            pool.submit(
                run_candidate, executable, output, candidate, phase, threads
            )
            for candidate in candidates
        ]
        for ordinal, future in enumerate(
            concurrent.futures.as_completed(futures), 1
        ):
            result = future.result()
            results.append(result)
            if ordinal % 16 == 0 or ordinal == len(futures):
                best = min(results, key=rank_key)
                best_summary = best.get("summary", {})
                print(json.dumps({
                    "phase": phase, "completed": ordinal,
                    "total": len(futures), "bestIndex": best.get("index"),
                    "bestF4": best_summary.get("prefixCounts", [None] * 4)[3],
                    "bestComponents": best_summary.get(
                        "componentCounts", [None] * 4
                    )[3],
                    "bestLargest": best_summary.get(
                        "largestComponentSizes", [None] * 4
                    )[3],
                }, separators=(",", ":")), flush=True)
    results.sort(key=rank_key)
    return results


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--candidate-count", type=int, default=384)
    parser.add_argument("--refine-count", type=int, default=16)
    parser.add_argument("--max-workers", type=int, default=3)
    parser.add_argument("--threads-per-worker", type=int, default=4)
    parser.add_argument("--local-center-index", type=int, default=-1)
    parser.add_argument("--local-center-summary", type=pathlib.Path)
    parser.add_argument("--local-span-scale", type=float, default=1.0)
    args = parser.parse_args()
    executable = args.executable.resolve()
    output = args.output.resolve()
    if not executable.is_file():
        parser.error(f"executable not found: {executable}")
    if args.candidate_count <= 0 or not 0 < args.refine_count <= args.candidate_count:
        parser.error("candidate/refine counts are invalid")
    output.mkdir(parents=True, exist_ok=True)
    if not 0.0 < args.local_span_scale <= 1.0:
        parser.error("local span scale must be in (0, 1]")
    local_center: dict[str, Any] | None = None
    if args.local_center_summary is not None:
        source = json.loads(
            args.local_center_summary.resolve().read_text(encoding="utf-8")
        )
        local_center = {
            "assist3OffsetCM": source["assist3OffsetCM"],
            "bPlaneDeltaCM": source["assist3BPlaneDeltaCM"],
            "sigmaScale": source["assist3BPlaneSigmaScale"],
            "velocityDeltaCMPerSec": source[
                "assist3VelocityDeltaCMPerSec"
            ],
            "targetOffsetCM": source["targetOffsetCM"],
        }
    elif args.local_center_index >= 0:
        local_center = make_candidate(args.local_center_index)
    if local_center is not None:
        candidates = [
            make_local_candidate(
                index, local_center, args.local_center_index,
                args.local_span_scale,
            )
            for index in range(args.candidate_count)
        ]
    else:
        candidates = [
            make_candidate(index) for index in range(args.candidate_count)
        ]
    sparse = execute_phase(
        executable, output, candidates, "sparse",
        args.max_workers, args.threads_per_worker,
    )
    viable = [result for result in sparse if "error" not in result]
    refine_inputs = [
        {key: value for key, value in result.items()
         if key not in ("phase", "summary")}
        for result in viable[:args.refine_count]
    ]
    refined = execute_phase(
        executable, output, refine_inputs, "refined",
        args.max_workers, args.threads_per_worker,
    )
    report = {
        "schema": "abts.m11b.v2_2.terminal_mapping_search.v1",
        "candidateRank": 3,
        "candidateCount": args.candidate_count,
        "refineCount": len(refine_inputs),
        "sampling": "fixed Halton, baseline first",
        "localCenterIndex": args.local_center_index,
        "localCenterSummary": (
            str(args.local_center_summary.resolve())
            if args.local_center_summary is not None else None
        ),
        "localSpanScale": args.local_span_scale,
        "sparseResults": sparse,
        "refinedResults": refined,
    }
    path = output / "terminal_mapping_search.json"
    path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"RESULT={path}")
    if refined:
        print("BEST=" + json.dumps(refined[0], separators=(",", ":")))
    return 0 if refined and "error" not in refined[0] else 1


if __name__ == "__main__":
    raise SystemExit(main())
