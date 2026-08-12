#!/usr/bin/env python3
"""Promote near-connected local variants to rule out window-edge reconnection."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
from pathlib import Path
from typing import Any

import m11_v22_certify_rank11 as frozen_rank11
import m11_v22_rank11_assist2_narrow_refinement as narrow
import m11_v22_rank11_assist2_similarity_search as search
import m11_v22_rank11_target_offset_search as target_search


SCHEMA = "abts.m11b.v2_2.rank11_diagnostic_promotion.v1"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--executable", type=Path,
                        default=frozen_rank11.default_executable())
    parser.add_argument("--freeze", type=Path,
                        default=frozen_rank11.default_freeze_manifest())
    parser.add_argument("--count", type=int, default=4)
    parser.add_argument("--component-ceiling", type=int, default=4)
    parser.add_argument("--threads-per-worker", type=int, default=4)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if min(args.count, args.component_ceiling, args.threads_per_worker) <= 0:
        parser.error("counts must be positive")
    output = args.output.resolve()
    report_path = args.report.resolve()
    executable = args.executable.resolve()
    freeze_path = args.freeze.resolve()
    if not executable.is_file() or not report_path.is_file():
        parser.error("executable and report must exist")
    freeze, candidate_path, _ = frozen_rank11.validate_freeze(freeze_path)
    report = json.loads(report_path.read_text(encoding="utf-8"))
    viable = [value for value in report.get("results", [])
              if "closure" in value and value.get("nominalF4")
              and value["handfeel"]["passed"]
              and value["closure"]["componentCounts"][3]
                  <= args.component_ceiling
              and not value["closure"]["earlyTargetHitCount"]
              and not value["closure"]["bypassTargetHitCount"]
              and not value["closure"]["nestingViolations"]]
    viable.sort(key=narrow.refinement_key)
    selected: list[dict[str, Any]] = []
    seen: set[tuple[Any, ...]] = set()
    for value in viable:
        key = search.candidate_key(value["candidate"])
        if key not in seen:
            seen.add(key)
            selected.append(value)
        if len(selected) == args.count:
            break
    plan = {
        "schema": "abts.m11b.v2_2.rank11_diagnostic_promotion_plan.v1",
        "baseCandidateSourceHash": frozen_rank11.EXPECTED_SOURCE_HASH,
        "freezeManifestSha256": target_search.sha256_file(freeze_path),
        "candidateManifestSha256": target_search.sha256_file(candidate_path),
        "sourceReport": str(report_path),
        "sourceReportSha256": target_search.sha256_file(report_path),
        "executable": str(executable),
        "executableSha256": target_search.sha256_file(executable),
        "schedulerSha256": target_search.sha256_file(Path(__file__)),
        "componentCeiling": args.component_ceiling,
        "selected": selected,
        "threadsPerWorker": args.threads_per_worker,
        "authority": "ABTSM11V22CertificationCLI",
    }
    target_search.atomic_json(output / "plan.json", plan)
    results: list[dict[str, Any]] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(2, len(selected))) as pool:
        futures = [pool.submit(search.promote, executable, output / "full",
                               index, value, freeze,
                               args.threads_per_worker, args.resume)
                   for index, value in enumerate(selected)]
        for completed, future in enumerate(concurrent.futures.as_completed(futures), 1):
            result = future.result()
            results.append(result)
            half = result.get("half_step", {})
            print(json.dumps({
                "phase": "diagnostic_full_discovery",
                "completed": completed, "total": len(futures),
                "variant": result.get("variantSourceHash"),
                "passed": result.get("passedCandidateAcceptance"),
                "f4": half.get("prefixCounts", [None] * 4)[3],
                "components": half.get("componentCounts", [None] * 4)[3],
                "early": half.get("earlyTargetHitCount"),
                "screenAim": result.get("screenAim", {}).get("prefixCounts"),
            }, separators=(",", ":")), flush=True)
    results.sort(key=search.full_key)
    accepted = [value for value in results
                if value.get("passedCandidateAcceptance")]
    result_report = {
        "schema": SCHEMA,
        "status": "candidate_found" if accepted
                  else "diagnostic_promotion_early_stopped",
        "selectedCount": len(selected),
        "fullResults": results,
        "acceptedCandidate": accepted[0] if accepted else None,
    }
    target_search.atomic_json(output / "promotion_report.json", result_report)
    if accepted:
        target_search.atomic_json(output / "candidate_handoff.json", accepted[0])
    print("RESULT=" + str(output / "promotion_report.json"), flush=True)
    return 0 if accepted else 2


if __name__ == "__main__":
    raise SystemExit(main())
