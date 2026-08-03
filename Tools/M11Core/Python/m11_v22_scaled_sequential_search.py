#!/usr/bin/env python3
"""Sequentially repair the enlarged Rank 11 assist chain.

The upstream radial chain is frozen before this tool runs.  The tool first
places the terminal target against the fixed F3 image, then opens only a small
Assist-3/target neighbourhood when target placement alone cannot produce a
connected F4 island.  Every score comes from the authoritative standard C++
CLI; Python only schedules deterministic candidates and renders evidence.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import math
import pathlib
import subprocess
from typing import Any, Iterable

from PIL import Image, ImageDraw, ImageFont


PRIMES = (2, 3, 5, 7, 11, 13)
COLORS = ((51, 153, 255), (255, 165, 40), (178, 102, 255), (64, 220, 155))


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


def vector(values: Iterable[float]) -> list[float]:
    return [round(value, 3) for value in values]


def make_target_candidate(index: int, span_cm: float) -> dict[str, Any]:
    values = [0.5, 0.5, 0.5] if index == 0 else [
        halton(index, base) for base in PRIMES[:3]
    ]
    return {
        "index": index,
        "assist3OffsetCM": [0.0, 0.0, 0.0],
        "targetOffsetCM": vector(
            lerp(-span_cm, span_cm, value) for value in values
        ),
    }


def make_joint_candidate(
    index: int,
    parent: dict[str, Any],
    parent_rank: int,
    assist3_span_cm: float,
    target_span_cm: float,
) -> dict[str, Any]:
    values = [0.5] * 6 if index == 0 else [
        halton(index, base) for base in PRIMES
    ]
    assist3 = vector(
        parent["assist3OffsetCM"][axis]
        + lerp(-assist3_span_cm, assist3_span_cm, values[axis])
        for axis in range(3)
    )
    target = vector(
        parent["targetOffsetCM"][axis]
        + lerp(-target_span_cm, target_span_cm, values[axis + 3])
        for axis in range(3)
    )
    assist3_length = math.sqrt(sum(value * value for value in assist3))
    target_length = math.sqrt(sum(value * value for value in target))
    if assist3_length > 9990.0:
        assist3 = vector(value * 9990.0 / assist3_length for value in assist3)
    if target_length > 29950.0:
        target = vector(value * 29950.0 / target_length for value in target)
    return {
        "index": index,
        "parentRank": parent_rank,
        "assist3OffsetCM": assist3,
        "targetOffsetCM": target,
    }


def candidate_name(candidate: dict[str, Any], phase: str, ordinal: int) -> str:
    return f"{phase}_{ordinal:04d}_source_{candidate['index']:04d}"


def common_options(
    candidate: dict[str, Any],
    rank: int,
    threads: int,
    radial_deltas: list[float],
    yaw_step: float,
    pitch_step: float,
) -> list[str]:
    a3 = candidate["assist3OffsetCM"]
    target = candidate["targetOffsetCM"]
    return [
        "--rank", str(rank), "--threads", str(threads),
        "--shard-index", "0", "--shard-count", "1",
        "--checkpoint-every", "1024",
        "--min-yaw", "-4", "--max-yaw", "2",
        "--yaw-step", str(yaw_step),
        "--min-pitch", "19.5", "--max-pitch", "34.5",
        "--pitch-step", str(pitch_step),
        "--min-power", "1", "--max-power", "1", "--power-step", "0.1",
        "--assist1-radial-delta", str(radial_deltas[0]),
        "--assist2-radial-delta", str(radial_deltas[1]),
        "--assist3-radial-delta", str(radial_deltas[2]),
        "--target-radial-delta", str(radial_deltas[3]),
        "--assist3-offset-x", str(a3[0]),
        "--assist3-offset-y", str(a3[1]),
        "--assist3-offset-z", str(a3[2]),
        "--target-offset-x", str(target[0]),
        "--target-offset-y", str(target[1]),
        "--target-offset-z", str(target[2]),
        "--allow-off-grid-nominal",
    ]


def ratios(summary: dict[str, Any]) -> list[float]:
    counts = summary["prefixCounts"]
    sample_count = summary["sampleCount"]
    result = [counts[0] / sample_count]
    result.extend(
        counts[level] / counts[level - 1] if counts[level - 1] else 0.0
        for level in range(1, 4)
    )
    return result


def result_key(result: dict[str, Any]) -> tuple[Any, ...]:
    if "error" in result:
        return (1, 1, 1, math.inf, math.inf, math.inf)
    summary = result["summary"]
    counts = summary["prefixCounts"]
    components = summary["componentCounts"]
    values = ratios(summary)
    ratio_error = sum((value - 0.5) ** 2 for value in values)
    fragments = counts[3] - summary["largestComponentSizes"][3]
    displacement = math.sqrt(sum(
        value * value for value in result["targetOffsetCM"]
    )) / 30000.0 + math.sqrt(sum(
        value * value for value in result["assist3OffsetCM"]
    )) / 10000.0
    return (
        0 if counts[3] > 0 else 1,
        0 if components[3] == 1 else 1,
        0 if components[:3] == [1, 1, 1] else 1,
        fragments / max(1, counts[3]),
        ratio_error + 0.005 * displacement,
        displacement,
    )


def evaluate(
    executable: pathlib.Path,
    output: pathlib.Path,
    candidate: dict[str, Any],
    phase: str,
    ordinal: int,
    rank: int,
    threads: int,
    radial_deltas: list[float],
    yaw_step: float,
    pitch_step: float,
) -> dict[str, Any]:
    root = output / candidate_name(candidate, phase, ordinal)
    common = common_options(
        candidate, rank, threads, radial_deltas, yaw_step, pitch_step
    )
    preflight = subprocess.run(
        [str(executable), "preflight", "--output", str(root / "shard_0000"),
         *common], capture_output=True, text=True, check=False,
    )
    if preflight.returncode != 0:
        return {**candidate, "error": preflight.stderr.strip()[-1200:]}
    merged = subprocess.run(
        [str(executable), "merge", "--input-root", str(root),
         "--output", str(root / "merged"), *common],
        capture_output=True, text=True, check=False,
    )
    summary_path = root / "merged" / "summary.json"
    if merged.returncode not in (0, 2) or not summary_path.is_file():
        return {**candidate, "error": merged.stderr.strip()[-1200:]}
    return {
        **candidate,
        "phase": phase,
        "summary": json.loads(summary_path.read_text(encoding="utf-8")),
    }


def dispatch(
    executable: pathlib.Path,
    output: pathlib.Path,
    candidates: list[dict[str, Any]],
    phase: str,
    rank: int,
    workers: int,
    threads: int,
    radial_deltas: list[float],
    yaw_step: float,
    pitch_step: float,
) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futures = [
            pool.submit(
                evaluate, executable, output, candidate, phase, ordinal,
                rank, threads, radial_deltas, yaw_step, pitch_step,
            )
            for ordinal, candidate in enumerate(candidates)
        ]
        for completed, future in enumerate(
            concurrent.futures.as_completed(futures), 1
        ):
            results.append(future.result())
            if completed % 32 == 0 or completed == len(futures):
                best = min(results, key=result_key)
                summary = best.get("summary", {})
                print(json.dumps({
                    "phase": phase,
                    "completed": completed,
                    "total": len(futures),
                    "bestCounts": summary.get("prefixCounts"),
                    "bestComponents": summary.get("componentCounts"),
                    "bestRatios": ratios(summary) if summary else None,
                }, separators=(",", ":")), flush=True)
    return sorted(results, key=result_key)


def convex_hull(points: list[tuple[float, float]]) -> list[tuple[float, float]]:
    unique = sorted(set(points))
    if len(unique) <= 1:
        return unique
    def cross(o: tuple[float, float], a: tuple[float, float],
              b: tuple[float, float]) -> float:
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower: list[tuple[float, float]] = []
    for point in unique:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], point) <= 0.0:
            lower.pop()
        lower.append(point)
    upper: list[tuple[float, float]] = []
    for point in reversed(unique):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], point) <= 0.0:
            upper.pop()
        upper.append(point)
    return lower[:-1] + upper[:-1]


def hull_area(points: list[tuple[float, float]]) -> float:
    if len(points) < 3:
        return 0.0
    return abs(sum(
        points[index][0] * points[(index + 1) % len(points)][1]
        - points[(index + 1) % len(points)][0] * points[index][1]
        for index in range(len(points))
    )) * 0.5


def render_hulls(
    path: pathlib.Path,
    samples: list[list[tuple[float, float]]],
    hulls: list[list[tuple[float, float]]],
    counts: list[int],
    nominal: list[float],
) -> None:
    width, height = 1200, 900
    margin = (105, 70, 55, 100)
    image = Image.new("RGB", (width, height), (16, 25, 39))
    draw = ImageDraw.Draw(image, "RGBA")
    font = ImageFont.load_default(size=22)
    small = ImageFont.load_default(size=17)
    x0, y0 = margin[0], margin[1]
    x1, y1 = width - margin[2], height - margin[3]
    yaw_min, yaw_max = -18.0, 18.0
    pitch_min, pitch_max = 0.0, 60.0
    def project(point: tuple[float, float]) -> tuple[float, float]:
        return (
            x0 + (point[0] - yaw_min) / (yaw_max - yaw_min) * (x1 - x0),
            y1 - (point[1] - pitch_min) / (pitch_max - pitch_min) * (y1 - y0),
        )
    for yaw in range(-15, 16, 5):
        px, _ = project((float(yaw), 0.0))
        draw.line((px, y0, px, y1), fill=(90, 115, 145, 70), width=1)
        draw.text((px - 15, y1 + 12), str(yaw), fill=(205, 220, 235), font=small)
    for pitch in range(0, 61, 10):
        _, py = project((0.0, float(pitch)))
        draw.line((x0, py, x1, py), fill=(90, 115, 145, 70), width=1)
        draw.text((42, py - 9), str(pitch), fill=(205, 220, 235), font=small)
    for level, (points, hull, color) in enumerate(zip(samples, hulls, COLORS), 1):
        for point in points:
            px, py = project(point)
            draw.ellipse((px - 1.5, py - 1.5, px + 1.5, py + 1.5),
                         fill=(*color, 85))
        if len(hull) >= 2:
            polygon = [project(point) for point in hull]
            draw.polygon(polygon, fill=(*color, 25))
            draw.line(polygon + [polygon[0]], fill=(*color, 230), width=4)
        draw.text((x0 + 18, y0 + 12 + (level - 1) * 31),
                  f"F{level}: {counts[level - 1]} samples",
                  fill=(*color, 255), font=small)
    nx, ny = project((nominal[0], nominal[1]))
    draw.line((nx - 10, ny, nx + 10, ny), fill=(255, 255, 255), width=3)
    draw.line((nx, ny - 10, nx, ny + 10), fill=(255, 255, 255), width=3)
    draw.rectangle((x0, y0, x1, y1), outline=(190, 215, 235), width=2)
    draw.text((x0, 18), "M11 enlarged sequential candidate - ScreenAim convex hulls",
              fill=(235, 245, 255), font=font)
    draw.text((width // 2 - 90, height - 52), "Yaw (degrees)",
              fill=(220, 235, 245), font=small)
    draw.text((12, 25), "Pitch", fill=(220, 235, 245), font=small)
    image.save(path)


def screen_aim_and_hulls(
    executable: pathlib.Path,
    output: pathlib.Path,
    candidate: dict[str, Any],
    rank: int,
    radial_deltas: list[float],
) -> dict[str, Any]:
    summary = candidate["summary"]
    nominal = summary["representativeF4Input"]
    screen = output / "screen_aim_5000"
    common = common_options(candidate, rank, 1, radial_deltas, 1.0, 1.5)
    common.extend([
        "--nominal-yaw", str(nominal[0]),
        "--nominal-pitch", str(nominal[1]),
        "--nominal-power", str(nominal[2]),
    ])
    process = subprocess.run(
        [str(executable), "screen-aim", "--output", str(screen),
         "--screen-aim-samples", "5000", *common],
        capture_output=True, text=True, check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.strip())
    points: list[list[tuple[float, float]]] = [[], [], [], []]
    with (screen / "screen_aim_samples.tsv").open("r", encoding="utf-8") as stream:
        next(stream)
        for line in stream:
            fields = line.split()
            yaw, pitch, mask = float(fields[1]), float(fields[2]), int(fields[4])
            for level in range(4):
                if mask & (1 << level):
                    points[level].append((yaw, pitch))
    hulls = [convex_hull(level) for level in points]
    evidence = {
        "sampleCount": 5000,
        "prefixCounts": [len(level) for level in points],
        "conditionalRatios": [
            len(points[0]) / 5000.0,
            *[
                len(points[level]) / len(points[level - 1])
                if points[level - 1] else 0.0
                for level in range(1, 4)
            ],
        ],
        "hullAreaSquareDegrees": [hull_area(hull) for hull in hulls],
        "hullYawSpanDegrees": [
            max((point[0] for point in hull), default=0.0)
            - min((point[0] for point in hull), default=0.0)
            for hull in hulls
        ],
        "hullPitchSpanDegrees": [
            max((point[1] for point in hull), default=0.0)
            - min((point[1] for point in hull), default=0.0)
            for hull in hulls
        ],
        "hullYawPitch": [[[x, y] for x, y in hull] for hull in hulls],
    }
    (output / "screen_aim_hulls.json").write_text(
        json.dumps(evidence, indent=2) + "\n", encoding="utf-8"
    )
    render_hulls(
        output / "screen_aim_hulls.png", points, hulls,
        evidence["prefixCounts"], nominal,
    )
    return evidence


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--rank", type=int, default=11)
    parser.add_argument("--radial-deltas-cm", default="-8500,-8500,4000,4000")
    parser.add_argument("--target-samples", type=int, default=384)
    parser.add_argument("--target-span-cm", type=float, default=30000.0)
    parser.add_argument("--target-promote", type=int, default=24)
    parser.add_argument("--joint-parents", type=int, default=8)
    parser.add_argument("--joint-children", type=int, default=48)
    parser.add_argument("--assist3-span-cm", type=float, default=9000.0)
    parser.add_argument("--joint-target-span-cm", type=float, default=10000.0)
    parser.add_argument("--verify-count", type=int, default=20)
    parser.add_argument("--workers", type=int, default=3)
    parser.add_argument("--threads-per-worker", type=int, default=4)
    args = parser.parse_args()
    executable = args.executable.resolve()
    output = args.output.resolve()
    radial_deltas = [float(value) for value in args.radial_deltas_cm.split(",")]
    if not executable.is_file() or len(radial_deltas) != 4:
        parser.error("executable and four radial deltas are required")
    output.mkdir(parents=True, exist_ok=True)

    target_candidates = [
        make_target_candidate(index, args.target_span_cm)
        for index in range(args.target_samples)
    ]
    target_sparse = dispatch(
        executable, output / "target_sparse", target_candidates,
        "target_sparse", args.rank, args.workers, args.threads_per_worker,
        radial_deltas, 1.0, 1.5,
    )
    target_fine = dispatch(
        executable, output / "target_fine", target_sparse[:args.target_promote],
        "target_fine", args.rank, args.workers, args.threads_per_worker,
        radial_deltas, 0.5, 0.75,
    )

    joint_candidates: list[dict[str, Any]] = list(target_fine)
    for parent_rank, parent in enumerate(target_fine[:args.joint_parents], 1):
        if "error" in parent:
            continue
        for index in range(args.joint_children):
            joint_candidates.append(make_joint_candidate(
                index, parent, parent_rank,
                args.assist3_span_cm, args.joint_target_span_cm,
            ))
    joint_fine = dispatch(
        executable, output / "joint_fine", joint_candidates,
        "joint_fine", args.rank, args.workers, args.threads_per_worker,
        radial_deltas, 0.5, 0.75,
    )
    verified = dispatch(
        executable, output / "verify", joint_fine[:args.verify_count],
        "verify", args.rank, args.workers, args.threads_per_worker,
        radial_deltas, 0.25, 0.375,
    )
    best = verified[0]
    if "error" in best or result_key(best)[:3] != (0, 0, 0):
        report = {
            "schema": "abts.m11b.v2_2.scaled_sequential_search.v1",
            "status": "no_qualified_candidate",
            "radialDeltasCM": radial_deltas,
            "results": verified,
        }
        (output / "search_report.json").write_text(
            json.dumps(report, indent=2) + "\n", encoding="utf-8"
        )
        print(json.dumps(report, separators=(",", ":")))
        return 2

    screen_evidence = screen_aim_and_hulls(
        executable, output, best, args.rank, radial_deltas
    )
    report = {
        "schema": "abts.m11b.v2_2.scaled_sequential_search.v1",
        "status": "candidate_not_certified",
        "baseRank": args.rank,
        "radialDeltasCM": radial_deltas,
        "best": best,
        "screenAimEvidence": screen_evidence,
        "verifiedResults": verified,
    }
    report_path = output / "search_report.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("BEST=" + json.dumps(best, separators=(",", ":")))
    print(f"REPORT={report_path}")
    print(f"HULL={output / 'screen_aim_hulls.png'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
