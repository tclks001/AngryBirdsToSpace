#!/usr/bin/env python3
"""Search Rank 3's assist-2 upstream mapping around terminal candidate 131."""

from __future__ import annotations

import argparse
import importlib.util
import json
import pathlib
from typing import Any


def load_terminal_module(path: pathlib.Path):
    spec = importlib.util.spec_from_file_location("m11_terminal_search", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("terminal search module unavailable")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def lerp(lo: float, hi: float, alpha: float) -> float:
    return lo + (hi - lo) * alpha


def make_candidate(
    module, index: int, center: dict[str, Any] | None = None,
    span_scale: float = 1.0,
) -> dict[str, Any]:
    values = [0.5] * 9 if index == 0 else [
        module.halton(index, base) for base in module.PRIMES[:9]
    ]
    candidate = {
        "index": index,
        "parentIndex": 131,
        "assist2OffsetCM": [
            round(lerp(-1500, 1500, values[0]), 3),
            round(lerp(-1500, 1500, values[1]), 3),
            round(lerp(-1500, 1500, values[2]), 3),
        ],
        "assist2BPlaneDeltaCM": [
            round(lerp(-2000, 2000, values[3]), 3),
            round(lerp(-2000, 2000, values[4]), 3),
        ],
        "assist2BPlaneSigmaScale": round(
            lerp(0.80, 1.25, values[5]), 6
        ),
        "assist2VelocityDeltaCMPerSec": [
            round(lerp(-800, 800, values[6]), 3),
            round(lerp(-800, 800, values[7]), 3),
            round(lerp(-800, 800, values[8]), 3),
        ],
        "assist3OffsetCM": [1864.062, -1883.951, -345.280],
        "bPlaneDeltaCM": [731.050, 1622.652],
        "sigmaScale": 1.193424,
        "velocityDeltaCMPerSec": [-955.286, 1384.235, -1302.886],
        "targetOffsetCM": [2045.340, 2022.718, -8885.799],
    }
    if center is None:
        return candidate
    dimensions = 0
    for key, spans in (
        ("assist2OffsetCM", (1500.0, 1500.0, 1500.0)),
        ("assist2BPlaneDeltaCM", (2000.0, 2000.0)),
        ("assist2VelocityDeltaCMPerSec", (800.0, 800.0, 800.0)),
    ):
        candidate[key] = []
        for component, span in zip(center[key], spans):
            delta = lerp(
                -span * span_scale, span * span_scale,
                values[dimensions],
            )
            candidate[key].append(round(component + delta, 3))
            dimensions += 1
    sigma_delta = lerp(
        -0.225 * span_scale, 0.225 * span_scale,
        values[dimensions],
    )
    candidate["assist2BPlaneSigmaScale"] = round(
        min(1.50, max(
            0.65, center["assist2BPlaneSigmaScale"] + sigma_delta
        )), 6
    )
    for key in (
        "assist3OffsetCM", "bPlaneDeltaCM", "sigmaScale",
        "velocityDeltaCMPerSec", "targetOffsetCM",
    ):
        candidate[key] = center[key]
    return candidate


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--candidate-count", type=int, default=384)
    parser.add_argument("--refine-count", type=int, default=24)
    parser.add_argument("--max-workers", type=int, default=3)
    parser.add_argument("--threads-per-worker", type=int, default=4)
    parser.add_argument("--local-center-summary", type=pathlib.Path)
    parser.add_argument("--local-span-scale", type=float, default=1.0)
    args = parser.parse_args()
    source = pathlib.Path(__file__).with_name(
        "m11_v22_terminal_mapping_search.py"
    )
    module = load_terminal_module(source)
    executable = args.executable.resolve()
    output = args.output.resolve()
    if not executable.is_file():
        parser.error(f"executable not found: {executable}")
    if args.candidate_count <= 0 or not 0 < args.refine_count <= args.candidate_count:
        parser.error("candidate/refine counts are invalid")
    if not 0.0 < args.local_span_scale <= 1.0:
        parser.error("local span scale must be in (0, 1]")
    output.mkdir(parents=True, exist_ok=True)
    center = None
    if args.local_center_summary is not None:
        source = json.loads(
            args.local_center_summary.resolve().read_text(encoding="utf-8")
        )
        center = {
            "assist2OffsetCM": source["assist2OffsetCM"],
            "assist2BPlaneDeltaCM": source["assist2BPlaneDeltaCM"],
            "assist2BPlaneSigmaScale": source["assist2BPlaneSigmaScale"],
            "assist2VelocityDeltaCMPerSec": source[
                "assist2VelocityDeltaCMPerSec"
            ],
            "assist3OffsetCM": source["assist3OffsetCM"],
            "bPlaneDeltaCM": source["assist3BPlaneDeltaCM"],
            "sigmaScale": source["assist3BPlaneSigmaScale"],
            "velocityDeltaCMPerSec": source[
                "assist3VelocityDeltaCMPerSec"
            ],
            "targetOffsetCM": source["targetOffsetCM"],
        }
    candidates = [
        make_candidate(module, index, center, args.local_span_scale)
        for index in range(args.candidate_count)
    ]
    sparse = module.execute_phase(
        executable, output, candidates, "sparse",
        args.max_workers, args.threads_per_worker,
    )
    viable = [result for result in sparse if "error" not in result]
    refine_inputs = [
        {key: value for key, value in result.items()
         if key not in ("phase", "summary")}
        for result in viable[:args.refine_count]
    ]
    refined = module.execute_phase(
        executable, output, refine_inputs, "refined",
        args.max_workers, args.threads_per_worker,
    )
    report = {
        "schema": "abts.m11b.v2_2.upstream_mapping_search.v1",
        "candidateRank": 3,
        "terminalBaselineVariant": "0xc6c5dca2ee75fb28",
        "candidateCount": args.candidate_count,
        "refineCount": len(refine_inputs),
        "localCenterSummary": (
            str(args.local_center_summary.resolve())
            if args.local_center_summary is not None else None
        ),
        "localSpanScale": args.local_span_scale,
        "sparseResults": sparse,
        "refinedResults": refined,
    }
    path = output / "upstream_mapping_search.json"
    path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"RESULT={path}")
    if refined:
        print("BEST=" + json.dumps(refined[0], separators=(",", ":")))
    return 0 if refined and "error" not in refined[0] else 1


if __name__ == "__main__":
    raise SystemExit(main())
