#!/usr/bin/env python3
"""Locally expand Candidate 353's F3/F2 retention using the C++ authority."""

from __future__ import annotations

import argparse
import importlib.util
import json
import pathlib
from typing import Any


def load_terminal(path: pathlib.Path):
    spec = importlib.util.spec_from_file_location("m11_terminal", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("terminal search module unavailable")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def rank_key(result: dict[str, Any]) -> tuple[Any, ...]:
    if "error" in result:
        return (1, 1, 999999, 1, 1.0, 0)
    summary = result["summary"]
    counts = summary["prefixCounts"]
    components = summary["componentCounts"]
    ratio = counts[2] / max(1, counts[1])
    return (
        0 if counts[3] > 0 and components[3] == 1 else 1,
        0 if components[0] == 1 and components[1] == 1 else 1,
        components[3] if counts[3] > 0 else 999999,
        0 if counts[3] >= 4 else 1,
        -ratio,
        -counts[3],
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=pathlib.Path, required=True)
    parser.add_argument("--center", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--candidate-count", type=int, default=384)
    parser.add_argument("--refine-count", type=int, default=24)
    parser.add_argument("--span-scale", type=float, default=0.5)
    parser.add_argument("--allow-target-motion", action="store_true")
    parser.add_argument("--target-only", action="store_true")
    parser.add_argument("--target-span-cm", type=float, default=800.0)
    parser.add_argument("--refined-only", action="store_true")
    parser.add_argument("--max-workers", type=int, default=3)
    parser.add_argument("--threads-per-worker", type=int, default=4)
    args = parser.parse_args()
    module = load_terminal(pathlib.Path(__file__).with_name(
        "m11_v22_terminal_mapping_search.py"))
    center_source = json.loads(args.center.resolve().read_text(encoding="utf-8"))
    center = {
        "assist3OffsetCM": center_source["assist3OffsetCM"],
        "bPlaneDeltaCM": center_source["assist3BPlaneDeltaCM"],
        "sigmaScale": center_source["assist3BPlaneSigmaScale"],
        "velocityDeltaCMPerSec": center_source["assist3VelocityDeltaCMPerSec"],
        # Keep the UFO fixed for the first search pass.
        "targetOffsetCM": center_source["targetOffsetCM"],
    }
    candidates = []
    for index in range(args.candidate_count):
        candidate = module.make_local_candidate(
            index, center, 353, args.span_scale)
        if args.target_only:
            for key in ("assist3OffsetCM", "bPlaneDeltaCM",
                        "sigmaScale", "velocityDeltaCMPerSec"):
                candidate[key] = center[key]
            values = [0.5, 0.5, 0.5] if index == 0 else [
                module.halton(index, base) for base in module.PRIMES[:3]]
            candidate["targetOffsetCM"] = [
                round(component + module.lerp(
                    -args.target_span_cm, args.target_span_cm, value), 3)
                for component, value in zip(center["targetOffsetCM"], values)]
        if not args.allow_target_motion:
            candidate["targetOffsetCM"] = center["targetOffsetCM"]
        candidate["assist2OffsetCM"] = center_source["assist2OffsetCM"]
        candidate["assist2BPlaneDeltaCM"] = center_source["assist2BPlaneDeltaCM"]
        candidate["assist2BPlaneSigmaScale"] = center_source[
            "assist2BPlaneSigmaScale"]
        candidate["assist2VelocityDeltaCMPerSec"] = center_source[
            "assist2VelocityDeltaCMPerSec"]
        candidates.append(candidate)
    module.rank_key = rank_key
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    sparse = [] if args.refined_only else module.execute_phase(
        args.executable.resolve(), output, candidates, "sparse",
        args.max_workers, args.threads_per_worker)
    refine_inputs = candidates if args.refined_only else [
        {key: value for key, value in result.items()
         if key not in ("phase", "summary")}
        for result in sparse if "error" not in result
    ][:args.refine_count]
    refined = module.execute_phase(
        args.executable.resolve(), output, refine_inputs, "refined",
        args.max_workers, args.threads_per_worker)
    report = {
        "schema": "abts.m11b.v2_2.f3_expansion_search.v1",
        "baseCandidate": 353,
        "strictTerminalOrdering": True,
        "targetFrozen": not args.allow_target_motion,
        "targetOnly": args.target_only,
        "targetSpanCM": args.target_span_cm,
        "refinedOnly": args.refined_only,
        "candidateCount": args.candidate_count,
        "refineCount": len(refine_inputs),
        "spanScale": args.span_scale,
        "sparseResults": sparse,
        "refinedResults": refined,
    }
    path = output / "f3_expansion_search.json"
    path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"RESULT={path}")
    if refined:
        print("BEST=" + json.dumps(refined[0], separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
