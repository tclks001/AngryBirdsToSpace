#!/usr/bin/env python3
"""Constrained downstream angular repair for the Rank 8 radial experiment."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import math
import pathlib
import random
import subprocess
from typing import Any


def random_ball(rng: random.Random, radius: float) -> tuple[float, float, float]:
    while True:
        value = tuple(rng.uniform(-1.0, 1.0) for _ in range(3))
        length2 = sum(component * component for component in value)
        if 1.0e-9 < length2 <= 1.0:
            scale = radius * rng.random() ** (1.0 / 3.0) / math.sqrt(length2)
            return tuple(round(component * scale, 3) for component in value)


def jitter_ball(rng: random.Random, center: list[float] | tuple[float, ...],
                sigma: float, limit: float) -> tuple[float, float, float]:
    value = [center[index] + rng.gauss(0.0, sigma) for index in range(3)]
    length = math.sqrt(sum(component * component for component in value))
    if length > limit:
        value = [component * limit / length for component in value]
    return tuple(round(component, 3) for component in value)


def key(candidate: dict[str, Any]) -> str:
    values = [*candidate["assist2OffsetCM"], *candidate["assist3OffsetCM"],
              *candidate["targetOffsetCM"]]
    return "_".join(("p" if value >= 0 else "m") + str(abs(int(round(value))))
                    for value in values)


def options(candidate: dict[str, Any], rank: int, threads: int,
            yaw_step: float, pitch_step: float) -> list[str]:
    a2 = candidate["assist2OffsetCM"]
    a3 = candidate["assist3OffsetCM"]
    target = candidate["targetOffsetCM"]
    return [
        "--rank", str(rank), "--threads", str(threads),
        "--shard-index", "0", "--shard-count", "1",
        "--checkpoint-every", "1024",
        "--min-yaw", "-4", "--max-yaw", "2", "--yaw-step", str(yaw_step),
        "--min-pitch", "19.5", "--max-pitch", "34.5",
        "--pitch-step", str(pitch_step),
        "--min-power", "1", "--max-power", "1", "--power-step", "0.1",
        "--assist1-radial-delta", "5900",
        "--assist2-radial-delta", "5900",
        "--assist3-radial-delta", "5900",
        "--target-radial-delta", "5900",
        "--assist2-offset-x", str(a2[0]), "--assist2-offset-y", str(a2[1]),
        "--assist2-offset-z", str(a2[2]),
        "--assist3-offset-x", str(a3[0]), "--assist3-offset-y", str(a3[1]),
        "--assist3-offset-z", str(a3[2]),
        "--target-offset-x", str(target[0]), "--target-offset-y", str(target[1]),
        "--target-offset-z", str(target[2]),
    ]


def evaluate(executable: pathlib.Path, root: pathlib.Path, candidate: dict[str, Any],
             rank: int, threads: int, yaw_step: float,
             pitch_step: float, merge: bool = False) -> dict[str, Any]:
    trial = root / key(candidate)
    shard = trial / "shard_0000"
    common = options(candidate, rank, threads, yaw_step, pitch_step)
    process = subprocess.run(
        [str(executable), "preflight", "--output", str(shard), *common],
        capture_output=True, text=True, check=False,
    )
    summary_path = shard / "summary.json"
    if process.returncode != 0 or not summary_path.is_file():
        return {**candidate, "error": process.stderr.strip()[-1000:]}
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    if merge:
        merged = trial / "merged"
        process = subprocess.run(
            [str(executable), "merge", "--input-root", str(trial),
             "--output", str(merged), *common],
            capture_output=True, text=True, check=False,
        )
        merged_path = merged / "summary.json"
        if process.returncode not in (0, 2) or not merged_path.is_file():
            return {**candidate, "error": process.stderr.strip()[-1000:]}
        summary = json.loads(merged_path.read_text(encoding="utf-8"))
    counts = summary["prefixCounts"]
    sample_count = summary.get("sampleCount")
    if sample_count is None:
        sample_count = summary["globalSampleCount"]
    ratios = [counts[0] / sample_count]
    ratios.extend(counts[index] / counts[index - 1] if counts[index - 1] else 0.0
                  for index in range(1, 4))
    return {**candidate, "prefixCounts": counts, "ratios": ratios,
            "componentCounts": summary.get("componentCounts"),
            "largestComponentSizes": summary.get("largestComponentSizes"),
            "representativeF4Input": summary.get("representativeF4Input"),
            "representativeFlightTimeSeconds": summary.get(
                "representativeFlightTimeSeconds"),
            "representativeAssistDurationsSeconds": summary.get(
                "representativeAssistDurationsSeconds"),
            "representativeAssistDeflectionsRadians": summary.get(
                "representativeAssistDeflectionsRadians"),
            "variantSourceHash": summary["variantSourceHash"]}


def displacement(candidate: dict[str, Any]) -> float:
    limits = (3000.0, 5000.0, 8000.0)
    return sum(math.sqrt(sum(value * value for value in offset)) / limit
               for offset, limit in zip((candidate["assist2OffsetCM"],
                                         candidate["assist3OffsetCM"],
                                         candidate["targetOffsetCM"]), limits))


def score(candidate: dict[str, Any], level: int) -> tuple[Any, ...]:
    if "error" in candidate:
        return (1, math.inf, math.inf)
    ratios = candidate["ratios"]
    missing = sum(1 for index in range(level + 1)
                  if candidate["prefixCounts"][index] == 0)
    ratio_error = sum((ratios[index] - 0.5) ** 2 for index in range(level + 1))
    return (missing, ratio_error + 0.015 * displacement(candidate),
            displacement(candidate))


def fine_score(candidate: dict[str, Any]) -> tuple[Any, ...]:
    if "error" in candidate:
        return (1, 1, math.inf, math.inf)
    components = candidate["componentCounts"]
    ratios = candidate["ratios"]
    ratio_error = sum((value - 0.5) ** 2 for value in ratios)
    return (0 if candidate["prefixCounts"][3] > 0 else 1,
            0 if all(value == 1 for value in components) else 1,
            ratio_error + 0.015 * displacement(candidate),
            displacement(candidate))


def dispatch(executable: pathlib.Path, root: pathlib.Path,
             candidates: list[dict[str, Any]], rank: int, workers: int,
             threads: int, yaw_step: float, pitch_step: float,
             merge: bool = False) -> list[dict[str, Any]]:
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futures = [pool.submit(evaluate, executable, root, candidate, rank,
                               threads, yaw_step, pitch_step, merge)
                   for candidate in candidates]
        return [future.result() for future in futures]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--rank", type=int, default=8)
    parser.add_argument("--seed", type=int, default=0x11B22801)
    parser.add_argument("--workers", type=int, default=2)
    parser.add_argument("--threads-per-worker", type=int, default=6)
    parser.add_argument("--stage-samples", type=int, default=384)
    parser.add_argument("--beam", type=int, default=16)
    parser.add_argument("--children", type=int, default=32)
    parser.add_argument("--local-parents", type=int, default=8)
    parser.add_argument("--local-children", type=int, default=32)
    parser.add_argument("--verify-count", type=int, default=24)
    args = parser.parse_args()
    executable = args.executable.resolve()
    output = args.output.resolve()
    if not executable.is_file():
        parser.error(f"executable not found: {executable}")
    output.mkdir(parents=True, exist_ok=True)
    rng = random.Random(args.seed)
    zero = (0.0, 0.0, 0.0)

    stage1 = [{"assist2OffsetCM": random_ball(rng, 3000.0),
               "assist3OffsetCM": zero, "targetOffsetCM": zero}
              for _ in range(args.stage_samples)]
    stage1.append({"assist2OffsetCM": zero, "assist3OffsetCM": zero,
                   "targetOffsetCM": zero})
    first = dispatch(executable, output / "stage_assist2", stage1, args.rank,
                     args.workers, args.threads_per_worker, 1.0, 1.5)
    beam1 = sorted(first, key=lambda value: score(value, 1))[:args.beam]

    stage2: list[dict[str, Any]] = []
    for parent in beam1:
        for _ in range(args.children):
            stage2.append({"assist2OffsetCM": parent["assist2OffsetCM"],
                           "assist3OffsetCM": random_ball(rng, 5000.0),
                           "targetOffsetCM": zero})
    second = dispatch(executable, output / "stage_assist3", stage2, args.rank,
                      args.workers, args.threads_per_worker, 1.0, 1.5)
    beam2 = sorted(second, key=lambda value: score(value, 2))[:args.beam]

    stage3: list[dict[str, Any]] = []
    for parent in beam2:
        for _ in range(args.children):
            stage3.append({"assist2OffsetCM": parent["assist2OffsetCM"],
                           "assist3OffsetCM": parent["assist3OffsetCM"],
                           "targetOffsetCM": random_ball(rng, 8000.0)})
    third = dispatch(executable, output / "stage_target", stage3, args.rank,
                     args.workers, args.threads_per_worker, 1.0, 1.5)
    coarse = sorted(third, key=lambda value: score(value, 3))[:args.beam]
    fine = dispatch(executable, output / "fine", coarse, args.rank, args.workers,
                    args.threads_per_worker, 0.5, 0.75, True)
    fine.sort(key=fine_score)
    local: list[dict[str, Any]] = list(fine)
    for parent in fine[:args.local_parents]:
        if "error" in parent:
            continue
        for _ in range(args.local_children):
            local.append({
                "assist2OffsetCM": jitter_ball(
                    rng, parent["assist2OffsetCM"], 450.0, 3000.0),
                "assist3OffsetCM": jitter_ball(
                    rng, parent["assist3OffsetCM"], 750.0, 5000.0),
                "targetOffsetCM": jitter_ball(
                    rng, parent["targetOffsetCM"], 1200.0, 8000.0),
            })
    refined = dispatch(executable, output / "local_fine", local, args.rank,
                       args.workers, args.threads_per_worker, 0.5, 0.75, True)
    refined.sort(key=fine_score)
    verified = dispatch(executable, output / "verify", refined[:args.verify_count],
                        args.rank, args.workers, args.threads_per_worker,
                        0.25, 0.375, True)
    verified.sort(key=fine_score)
    report = {"schema": "abts.m11b.v2_2.constrained_angular_search.v1",
              "rank": args.rank, "seed": args.seed,
              "fixedRadialDeltaCM": 5900.0, "targetRatios": [0.5] * 4,
              "limitsCM": {"assist2": 3000, "assist3": 5000, "target": 8000},
              "coarseEvaluationCounts": [len(first), len(second), len(third)],
              "localFineEvaluationCount": len(refined),
              "verificationEvaluationCount": len(verified),
              "results": verified}
    report_path = output / "search_report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    for index, result in enumerate(verified[:8], 1):
        if "error" in result:
            print(f"{index}: ERROR {result['error']}")
        else:
            print(f"{index}: ratios={','.join(f'{v:.4f}' for v in result['ratios'])} "
                  f"counts={result['prefixCounts']} components={result['componentCounts']} "
                  f"offset={displacement(result):.3f}")
    print(report_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
