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


def offset_name(
    offset: tuple[int, int, int], radius_cm: int, cone_degrees: float,
    face_cone_degrees: float, assist3_offset: tuple[int, int, int],
) -> str:
    def part(value: int) -> str:
        return ("p" if value >= 0 else "m") + str(abs(value))

    cone_name = str(cone_degrees).replace(".", "p")
    face_cone_name = str(face_cone_degrees).replace(".", "p")
    return "x{}_y{}_z{}_r{}_c{}_fc{}_a3x{}_a3y{}_a3z{}".format(
        *(part(value) for value in offset), radius_cm, cone_name,
        face_cone_name, *(part(value) for value in assist3_offset),
    )


def run_candidate(
    executable: pathlib.Path,
    output_root: pathlib.Path,
    offset: tuple[int, int, int],
    radius_cm: int,
    cone_degrees: float,
    face_cone_degrees: float,
    assist3_offset: tuple[int, int, int],
    threads: int,
) -> dict[str, Any]:
    candidate_root = output_root / offset_name(
        offset, radius_cm, cone_degrees, face_cone_degrees, assist3_offset
    )
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
        "--target-hit-radius", str(radius_cm),
        "--arrival-cone-degrees", str(cone_degrees),
        "--arrival-face-cone-degrees", str(face_cone_degrees),
        "--assist3-offset-x", str(assist3_offset[0]),
        "--assist3-offset-y", str(assist3_offset[1]),
        "--assist3-offset-z", str(assist3_offset[2]),
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
            "targetHitRadiusCM": radius_cm,
            "arrivalConeDegrees": cone_degrees,
            "arrivalFaceConeDegrees": face_cone_degrees,
            "assist3OffsetCM": assist3_offset,
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
            "targetHitRadiusCM": radius_cm,
            "arrivalConeDegrees": cone_degrees,
            "arrivalFaceConeDegrees": face_cone_degrees,
            "assist3OffsetCM": assist3_offset,
            "error": "merge",
            "returnCode": merge.returncode,
            "stderr": merge.stderr[-2000:],
        }
    result = json.loads(summary_path.read_text(encoding="utf-8"))
    result["offsetCM"] = list(offset)
    result["targetHitRadiusCM"] = radius_cm
    result["arrivalConeDegrees"] = cone_degrees
    result["arrivalFaceConeDegrees"] = face_cone_degrees
    result["assist3OffsetCM"] = list(assist3_offset)
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
    parser.add_argument("--radii-cm", default="12000")
    parser.add_argument("--cones-degrees", default="180")
    parser.add_argument("--face-cones-degrees", default="180")
    parser.add_argument("--assist3-extent-cm", type=int, default=0)
    parser.add_argument("--assist3-step-cm", type=int, default=1000)
    parser.add_argument("--assist3-center-x-cm", type=int, default=0)
    parser.add_argument("--assist3-center-y-cm", type=int, default=0)
    parser.add_argument("--assist3-center-z-cm", type=int, default=0)
    args = parser.parse_args()

    executable = args.executable.resolve()
    output = args.output.resolve()
    if not executable.is_file():
        parser.error(f"executable not found: {executable}")
    if args.extent_cm < 0 or args.step_cm <= 0:
        parser.error("extent and step must define a non-empty symmetric grid")
    if args.assist3_extent_cm < 0 or args.assist3_step_cm <= 0:
        parser.error("assist3 extent and step must define a symmetric grid")

    deltas = range(-args.extent_cm, args.extent_cm + 1, args.step_cm)
    offsets = [
        (
            args.center_x_cm + dx,
            args.center_y_cm + dy,
            args.center_z_cm + dz,
        )
        for dx, dy, dz in itertools.product(deltas, repeat=3)
    ]
    assist3_deltas = range(
        -args.assist3_extent_cm,
        args.assist3_extent_cm + 1,
        args.assist3_step_cm,
    )
    assist3_offsets = [
        (
            args.assist3_center_x_cm + dx,
            args.assist3_center_y_cm + dy,
            args.assist3_center_z_cm + dz,
        )
        for dx, dy, dz in itertools.product(assist3_deltas, repeat=3)
    ]
    try:
        radii = [int(value) for value in args.radii_cm.split(",")]
    except ValueError as error:
        parser.error(f"invalid --radii-cm: {error}")
    if not radii or any(radius < 4500 or radius > 12000 for radius in radii):
        parser.error("radii must be within the frozen search contract")
    try:
        cones = [float(value) for value in args.cones_degrees.split(",")]
    except ValueError as error:
        parser.error(f"invalid --cones-degrees: {error}")
    if not cones or any(cone <= 0 or cone > 180 for cone in cones):
        parser.error("arrival cones must be in (0, 180]")
    try:
        face_cones = [
            float(value) for value in args.face_cones_degrees.split(",")
        ]
    except ValueError as error:
        parser.error(f"invalid --face-cones-degrees: {error}")
    if not face_cones or any(cone <= 0 or cone > 180 for cone in face_cones):
        parser.error("arrival face cones must be in (0, 180]")
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
                radius,
                cone,
                face_cone,
                assist3_offset,
                args.threads_per_worker,
            ): (offset, radius, cone, face_cone, assist3_offset)
            for offset, radius, cone, face_cone, assist3_offset in itertools.product(
                offsets, radii, cones, face_cones, assist3_offsets
            )
        }
        for future in concurrent.futures.as_completed(futures):
            result = future.result()
            results.append(result)
            print(
                json.dumps(
                    {
                        "offsetCM": result.get("offsetCM"),
                        "radiusCM": result.get("targetHitRadiusCM"),
                        "coneDegrees": result.get("arrivalConeDegrees"),
                        "faceConeDegrees": result.get(
                            "arrivalFaceConeDegrees"
                        ),
                        "assist3OffsetCM": result.get("assist3OffsetCM"),
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
        "radiiCM": radii,
        "conesDegrees": cones,
        "faceConesDegrees": face_cones,
        "assist3OffsetsCM": assist3_offsets,
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
