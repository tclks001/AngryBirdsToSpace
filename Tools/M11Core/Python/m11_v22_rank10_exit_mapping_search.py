#!/usr/bin/env python3
"""Search a small Rank 10 Assist-3 exit mapping around a fixed UFO repair."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import math
import pathlib
import subprocess
from typing import Any


def halton(index: int, base: int) -> float:
    value = 0.0
    fraction = 1.0
    while index:
        fraction /= base
        value += fraction * (index % base)
        index //= base
    return value


def candidate_for(index: int, span_cm: float,
                  sigma_span: float) -> dict[str, Any]:
    values = [0.5, 0.5, 0.5] if index == 0 else [
        halton(index, base) for base in (2, 3, 5)]
    return {
        "index": index,
        "bPlaneDeltaCM": [
            round((values[0] * 2.0 - 1.0) * span_cm, 3),
            round((values[1] * 2.0 - 1.0) * span_cm, 3),
        ],
        "sigmaScale": round(1.0 + (values[2] * 2.0 - 1.0) * sigma_span, 6),
    }


def run_candidate(executable: pathlib.Path, output: pathlib.Path,
                  candidate: dict[str, Any], phase: str,
                  threads: int) -> dict[str, Any]:
    root = output / f"candidate_{candidate['index']:04d}_{phase}"
    if phase == "screen":
        grid = ("0.5", "0.75", "0.025")
        bounds = ("-4.25", "2.25", "27.375", "39.375")
    else:
        grid = ("0.25", "0.375", "0.00625")
        bounds = ("-4", "2", "27", "39")
    bp = candidate["bPlaneDeltaCM"]
    common = [
        "--rank", "10", "--threads", str(threads),
        "--min-yaw", bounds[0], "--max-yaw", bounds[1],
        "--yaw-step", grid[0], "--min-pitch", bounds[2],
        "--max-pitch", bounds[3], "--pitch-step", grid[1],
        "--min-power", "0.75", "--max-power", "1",
        "--power-step", grid[2],
        "--target-offset-x", "-2320.312",
        "--target-offset-y", "-897.119",
        "--target-offset-z", "-3657.6",
        "--target-hit-radius", "6000",
        "--assist3-bplane-t-delta", str(bp[0]),
        "--assist3-bplane-r-delta", str(bp[1]),
        "--assist3-bplane-sigma-scale", str(candidate["sigmaScale"]),
        "--checkpoint-every", "4096",
    ]
    preflight = subprocess.run(
        [str(executable), "preflight", "--output", str(root / "shard_0000"),
         *common], capture_output=True, text=True, check=False)
    if preflight.returncode != 0:
        return {**candidate, "error": "preflight",
                "stderr": preflight.stderr[-1000:]}
    merge = subprocess.run(
        [str(executable), "merge", "--input-root", str(root),
         "--output", str(root / "merged"), *common],
        capture_output=True, text=True, check=False)
    summary_path = root / "merged" / "summary.json"
    if merge.returncode not in (0, 2) or not summary_path.is_file():
        return {**candidate, "error": "merge", "stderr": merge.stderr[-1000:]}
    return {**candidate, "phase": phase,
            "summary": json.loads(summary_path.read_text(encoding="utf-8"))}


def rank_key(result: dict[str, Any]) -> tuple[Any, ...]:
    if "error" in result:
        return (1, 1, 1, math.inf, math.inf, math.inf)
    summary = result["summary"]
    count = summary["prefixCounts"][3]
    components = summary["componentCounts"][3]
    fragments = count - summary["largestComponentSizes"][3]
    return (
        0 if summary["earlyTargetHitCount"] == 0 else 1,
        summary["earlyTargetHitCount"],
        0 if summary["nominalF4"] else 1,
        0 if components == 1 else 1,
        fragments,
        components,
        -count,
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
                best = min(results, key=rank_key)
                summary = best.get("summary", {})
                print(json.dumps({"phase": phase, "completed": ordinal,
                    "total": len(futures), "bestIndex": best.get("index"),
                    "early": summary.get("earlyTargetHitCount"),
                    "f4": summary.get("prefixCounts", [None] * 4)[3],
                    "components": summary.get("componentCounts", [None] * 4)[3]
                }, separators=(",", ":")), flush=True)
    return sorted(results, key=rank_key)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--candidate-count", type=int, default=256)
    parser.add_argument("--refine-count", type=int, default=24)
    parser.add_argument("--bplane-span-cm", type=float, default=600.0)
    parser.add_argument("--sigma-span", type=float, default=0.12)
    parser.add_argument("--max-workers", type=int, default=3)
    parser.add_argument("--threads-per-worker", type=int, default=4)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    candidates = [candidate_for(index, args.bplane_span_cm, args.sigma_span)
                  for index in range(args.candidate_count)]
    screened = execute(args.executable.resolve(), output, candidates, "screen",
                       args.max_workers, args.threads_per_worker)
    promoted = [{key: value for key, value in result.items()
                 if key not in ("phase", "summary")}
                for result in screened if "error" not in result][:args.refine_count]
    refined = execute(args.executable.resolve(), output, promoted, "refined",
                      args.max_workers, args.threads_per_worker)
    report = {"schema": "abts.m11b.v2_2.rank10_exit_mapping.v1",
              "candidateCount": args.candidate_count,
              "refineCount": len(promoted), "screenResults": screened,
              "refinedResults": refined}
    path = output / "rank10_exit_mapping.json"
    path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"RESULT={path}")
    if refined:
        print("BEST=" + json.dumps(refined[0], separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
