#!/usr/bin/env python3
"""Dispatch authoritative C++ UFO-offset diagnostics for M11-B v2.2."""

from __future__ import annotations

import argparse
import concurrent.futures
import itertools
import json
import math
import pathlib
import subprocess
from typing import Any


def offset_name(offset: tuple[int, int, int]) -> str:
    def part(value: int) -> str:
        return ("p" if value >= 0 else "m") + str(abs(value))

    return "x{}_y{}_z{}".format(*(part(value) for value in offset))


def run_candidate(
    executable: pathlib.Path,
    output_root: pathlib.Path,
    offset: tuple[int, int, int],
    threads: int,
) -> dict[str, Any]:
    candidate_root = output_root / offset_name(offset)
    shard = candidate_root / "shard_0000"
    merged = candidate_root / "merged"
    common = [
        "--rank", "3",
        "--threads", str(threads),
        "--shard-index", "0",
        "--shard-count", "1",
        "--yaw-step", "0.5",
        "--pitch-step", "0.75",
        "--power-step", "0.00625",
        "--min-yaw", "-4",
        "--max-yaw", "2",
        "--min-pitch", "19.5",
        "--max-pitch", "34.5",
        "--min-power", "0.875",
        "--max-power", "1",
        "--target-offset-x", str(offset[0]),
        "--target-offset-y", str(offset[1]),
        "--target-offset-z", str(offset[2]),
        "--checkpoint-every", "1024",
    ]
    preflight = subprocess.run(
        [str(executable), "preflight", "--output", str(shard), *common],
        capture_output=True,
        text=True,
        check=False,
    )
    if preflight.returncode != 0:
        return {
            "offsetCM": offset,
            "error": "preflight",
            "returnCode": preflight.returncode,
            "stderr": preflight.stderr[-2000:],
        }
    merge = subprocess.run(
        [
            str(executable), "merge",
            "--input-root", str(candidate_root),
            "--output", str(merged),
            *common,
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    summary_path = merged / "summary.json"
    if merge.returncode not in (0, 2) or not summary_path.is_file():
        return {
            "offsetCM": offset,
            "error": "merge",
            "returnCode": merge.returncode,
            "stderr": merge.stderr[-2000:],
        }
    result = json.loads(summary_path.read_text(encoding="utf-8"))
    result["offsetCM"] = list(offset)
    result["mergeReturnCode"] = merge.returncode
    return result


def ranking_key(result: dict[str, Any]) -> tuple[Any, ...]:
    if "error" in result:
        return (1, 1, math.inf, math.inf, math.inf)
    counts = result["prefixCounts"]
    components = result["componentCounts"]
    largest = result["largestComponentSizes"]
    fragments = counts[3] - largest[3]
    return (
        0 if result["nominalF4"] else 1,
        0 if components[3] == 1 else 1,
        fragments,
        components[3],
        -largest[3],
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--extent-cm", type=int, default=2000)
    parser.add_argument("--step-cm", type=int, default=2000)
    parser.add_argument("--center-x-cm", type=int, default=0)
    parser.add_argument("--center-y-cm", type=int, default=0)
    parser.add_argument("--center-z-cm", type=int, default=0)
    parser.add_argument("--max-workers", type=int, default=3)
    parser.add_argument("--threads-per-worker", type=int, default=4)
    args = parser.parse_args()

    executable = args.executable.resolve()
    output = args.output.resolve()
    if not executable.is_file():
        parser.error(f"executable not found: {executable}")
    if args.extent_cm < 0 or args.step_cm <= 0:
        parser.error("extent and step must define a non-empty symmetric grid")

    deltas = range(-args.extent_cm, args.extent_cm + 1, args.step_cm)
    offsets = [
        (
            args.center_x_cm + dx,
            args.center_y_cm + dy,
            args.center_z_cm + dz,
        )
        for dx, dy, dz in itertools.product(deltas, repeat=3)
    ]
    output.mkdir(parents=True, exist_ok=True)
    results: list[dict[str, Any]] = []
    with concurrent.futures.ThreadPoolExecutor(
        max_workers=args.max_workers
    ) as executor:
        futures = {
            executor.submit(
                run_candidate,
                executable,
                output,
                offset,
                args.threads_per_worker,
            ): offset
            for offset in offsets
        }
        for future in concurrent.futures.as_completed(futures):
            result = future.result()
            results.append(result)
            print(
                json.dumps(
                    {
                        "offsetCM": result.get("offsetCM"),
                        "nominalF4": result.get("nominalF4"),
                        "f4": result.get("prefixCounts", [None] * 4)[3],
                        "components": result.get(
                            "componentCounts", [None] * 4
                        )[3],
                        "error": result.get("error"),
                    },
                    separators=(",", ":"),
                ),
                flush=True,
            )

    results.sort(key=ranking_key)
    report = {
        "schema": "abts.m11b.v2_2.target_offset_search.v1",
        "candidateRank": 3,
        "extentCM": args.extent_cm,
        "stepCM": args.step_cm,
        "centerCM": [
            args.center_x_cm,
            args.center_y_cm,
            args.center_z_cm,
        ],
        "candidateCount": len(results),
        "results": results,
    }
    report_path = output / "search_results.json"
    report_path.write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    print(f"RESULT={report_path}")
    if results:
        print("BEST=" + json.dumps(results[0], separators=(",", ":")))
    return 0 if results and "error" not in results[0] else 1


if __name__ == "__main__":
    raise SystemExit(main())
