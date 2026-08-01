#!/usr/bin/env python3
"""Scan ordered radial-chain spacing with the authoritative M11 C++ core."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import pathlib
import subprocess
from typing import Any


def radial_deltas(increments: list[float]) -> list[float]:
    total = 0.0
    result: list[float] = []
    for increment in increments:
        total += increment
        result.append(total)
    return result


def value_name(value: float) -> str:
    return ("p" if value >= 0.0 else "m") + format(abs(value), ".3f").replace(".", "p")


def run_trial(
    executable: pathlib.Path,
    output_root: pathlib.Path,
    rank: int,
    stage: int,
    base_increments: list[float],
    value: float,
    threads: int,
    yaw_step: float,
    pitch_step: float,
    min_power: float,
    power_step: float,
) -> dict[str, Any]:
    increments = list(base_increments)
    increments[stage - 1] = value
    deltas = radial_deltas(increments)
    root = output_root / f"stage{stage}_{value_name(value)}"
    shard = root / "shard_0000"
    merged = root / "merged"
    common = [
        "--rank", str(rank), "--threads", str(threads),
        "--shard-index", "0", "--shard-count", "1",
        "--checkpoint-every", "1024",
        "--min-yaw", "-4", "--max-yaw", "2",
        "--yaw-step", str(yaw_step),
        "--min-pitch", "19.5", "--max-pitch", "34.5",
        "--pitch-step", str(pitch_step),
        "--min-power", str(min_power), "--max-power", "1",
        "--power-step", str(power_step),
        "--assist1-radial-delta", str(deltas[0]),
        "--assist2-radial-delta", str(deltas[1]),
        "--assist3-radial-delta", str(deltas[2]),
        "--target-radial-delta", str(deltas[3]),
    ]
    preflight = subprocess.run(
        [str(executable), "preflight", "--output", str(shard), *common],
        capture_output=True, text=True, check=False,
    )
    if preflight.returncode != 0:
        return {"stage": stage, "valueCM": value, "incrementsCM": increments,
                "radialDeltasCM": deltas, "error": "preflight",
                "returnCode": preflight.returncode,
                "stderr": preflight.stderr.strip()[-2000:]}
    merge = subprocess.run(
        [str(executable), "merge", "--input-root", str(root),
         "--output", str(merged), *common],
        capture_output=True, text=True, check=False,
    )
    summary_path = merged / "summary.json"
    if merge.returncode not in (0, 2) or not summary_path.is_file():
        return {"stage": stage, "valueCM": value, "incrementsCM": increments,
                "radialDeltasCM": deltas, "error": "merge",
                "returnCode": merge.returncode,
                "stderr": merge.stderr.strip()[-2000:]}
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    counts = summary["prefixCounts"]
    power_count = summary["grid"]["powerCount"]
    angular_count = summary["grid"]["yawCount"] * summary["grid"]["pitchCount"]
    maximum_counts = [0, 0, 0, 0]
    samples_path = merged / "samples.tsv"
    with samples_path.open("r", encoding="utf-8") as stream:
        next(stream)
        for line in stream:
            fields = line.split()
            if int(fields[3]) != power_count - 1:
                continue
            mask = int(fields[4])
            for prefix in range(4):
                if mask & (1 << prefix):
                    maximum_counts[prefix] += 1
    minimum_powers: list[float | None] = []
    for count, index in zip(counts, summary["minimumPowerIndices"]):
        minimum_powers.append(
            None if count == 0 else min_power + index * power_step
        )
    ratios = [maximum_counts[0] / angular_count]
    ratios.extend(
        maximum_counts[index] / maximum_counts[index - 1]
        if maximum_counts[index - 1] else 0.0
        for index in range(1, 4)
    )
    return {
        "stage": stage, "valueCM": value, "incrementsCM": increments,
        "radialDeltasCM": deltas, "maximumPowerCounts": maximum_counts,
        "maximumPowerRatios": ratios, "minimumPowers": minimum_powers,
        "componentCounts": summary["componentCounts"],
        "largestComponentSizes": summary["largestComponentSizes"],
        "nominalF4": summary["nominalF4"],
        "aggregateSampleHash": summary["aggregateSampleHash"],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--rank", type=int, default=8)
    parser.add_argument("--stage", type=int, choices=(1, 2, 3, 4), required=True)
    parser.add_argument("--base-increments-cm", default="0,0,0,0")
    parser.add_argument("--values-cm", required=True)
    parser.add_argument("--max-workers", type=int, default=2)
    parser.add_argument("--threads-per-worker", type=int, default=6)
    parser.add_argument("--yaw-step", type=float, default=1.0)
    parser.add_argument("--pitch-step", type=float, default=1.5)
    parser.add_argument("--min-power", type=float, default=0.7)
    parser.add_argument("--power-step", type=float, default=0.025)
    args = parser.parse_args()

    executable = args.executable.resolve()
    output = args.output.resolve()
    if not executable.is_file():
        parser.error(f"executable not found: {executable}")
    try:
        base = [float(value) for value in args.base_increments_cm.split(",")]
        values = [float(value) for value in args.values_cm.split(",")]
    except ValueError as error:
        parser.error(str(error))
    if len(base) != 4 or not values:
        parser.error("base increments must contain four values and scan values cannot be empty")
    if args.max_workers <= 0 or args.threads_per_worker <= 0:
        parser.error("worker and thread counts must be positive")
    output.mkdir(parents=True, exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.max_workers) as pool:
        futures = [
            pool.submit(
                run_trial, executable, output, args.rank, args.stage, base,
                value, args.threads_per_worker, args.yaw_step,
                args.pitch_step, args.min_power, args.power_step,
            )
            for value in values
        ]
        results = [future.result() for future in futures]
    results.sort(key=lambda result: result["valueCM"])
    report = {
        "schema": "abts.m11b.v2_2.radial_chain_scan.v1",
        "rank": args.rank, "stage": args.stage,
        "baseIncrementsCM": base, "results": results,
    }
    report_path = output / "scan_report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    for result in results:
        if "error" in result:
            print(f"{result['valueCM']:10.3f} ERROR {result['stderr']}")
        else:
            ratios = ",".join(f"{value:.4f}" for value in result["maximumPowerRatios"])
            powers = ",".join("-" if value is None else f"{value:.4f}"
                              for value in result["minimumPowers"])
            print(f"{result['valueCM']:10.3f} ratios={ratios} minPower={powers} "
                  f"components={result['componentCounts']}")
    print(report_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
