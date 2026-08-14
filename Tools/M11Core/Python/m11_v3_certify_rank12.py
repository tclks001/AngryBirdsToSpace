#!/usr/bin/env python3
"""Fresh, fail-closed Rank 12 v3 discovery and bridge-closure scheduler.

The Python layer only freezes identity, schedules the authoritative portable
C++ solvers, records immutable plans/logs, and routes early stop. It never
classifies a trajectory or manufactures bridge evidence.
"""

from __future__ import annotations

import argparse
import datetime as dt
from decimal import Decimal, ROUND_CEILING, ROUND_FLOOR
import json
import os
from pathlib import Path
import sys
from typing import Any

import m11_v22_certify_rank11 as legacy


PLAN_SCHEMA = "abts.m11b.v3.rank12_certification_plan.v1"
STATUS_SCHEMA = "abts.m11b.v3.rank12_certification_status.v1"
FREEZE_SCHEMA = "abts.m11b.v3.certification_input_freeze.v1"
CANDIDATE_SCHEMA = "abts.m11b.v2_2.handfeel_candidate.v1"
EXPECTED_RANK = 12
EXPECTED_SOURCE_HASH = "0x58840ee73ddd70f5"
EXPECTED_REQUEST_HASH = "0xf76a37a38221a425"
EXPECTED_RESULT_HASH = "0xf746bbe4ca7b9748"
EXPECTED_SCORE_HASH = "0xf364c0098bec8112"

# Reuse the already-tested v2.2 process/shard mechanics with Rank 12 identity.
legacy.EXPECTED_RANK = EXPECTED_RANK
legacy.EXPECTED_SOURCE_HASH = EXPECTED_SOURCE_HASH
legacy.EXPECTED_REQUEST_HASH = EXPECTED_REQUEST_HASH
legacy.EXPECTED_RESULT_HASH = EXPECTED_RESULT_HASH
legacy.EXPECTED_SCORE_HASH = EXPECTED_SCORE_HASH


def repository_root() -> Path:
    return Path(__file__).resolve().parents[3]


def default_freeze() -> Path:
    return repository_root() / "Tools/M11Core/Certification/Rank12V3CertificationInput.json"


def default_scan_executable() -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    return repository_root() / f"Intermediate/M11CoreStandalone/bin/ABTSM11V22CertificationCLI{suffix}"


def default_bridge_executable() -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    return repository_root() / f"Intermediate/M11CoreStandalone/bin/ABTSM11V3BridgeClosureCLI{suffix}"


def validate_freeze(freeze_path: Path) -> tuple[dict[str, Any], Path, dict[str, Any]]:
    freeze = legacy.load_json(freeze_path)
    if freeze.get("schema") != FREEZE_SCHEMA:
        raise RuntimeError("FreezeSchemaMismatch")
    if freeze.get("status") != "frozen_unique_certification_input":
        raise RuntimeError("FreezeStatusMismatch")
    if freeze.get("authority") != "user_approved_rank12_v3_freeze_after_handfeel":
        raise RuntimeError("FreezeAuthorityMismatch")
    if freeze.get("candidateRank") != EXPECTED_RANK:
        raise RuntimeError("FreezeRankMismatch")
    expected = {
        "candidateSourceHash": EXPECTED_SOURCE_HASH,
        "nominalRequestHash": EXPECTED_REQUEST_HASH,
        "nominalResultHash": EXPECTED_RESULT_HASH,
        "scoreHash": EXPECTED_SCORE_HASH,
    }
    for field, expected_value in expected.items():
        if legacy.normalized_hash(freeze.get(field), f"freeze.{field}") != expected_value:
            raise RuntimeError(f"FreezeIdentityMismatch:{field}")
    candidate_path = legacy.resolve_candidate_manifest(freeze)
    candidate = legacy.load_json(candidate_path)
    if candidate.get("schema") != CANDIDATE_SCHEMA:
        raise RuntimeError("CandidateSchemaMismatch")
    if candidate.get("status") != "frozen_unique_v3_certification_input_uncertified":
        raise RuntimeError("CandidateStatusMismatch")
    if candidate.get("editorCandidateRank") != EXPECTED_RANK:
        raise RuntimeError("CandidateRankMismatch")
    candidate_fields = {
        "variantSourceHash": EXPECTED_SOURCE_HASH,
        "nominalRequestHash": EXPECTED_REQUEST_HASH,
        "nominalResultHash": EXPECTED_RESULT_HASH,
        "diagnosticEvidenceHash": EXPECTED_SCORE_HASH,
    }
    for field, expected_value in candidate_fields.items():
        if legacy.normalized_hash(candidate.get(field), f"candidate.{field}") != expected_value:
            raise RuntimeError(f"CandidateIdentityMismatch:{field}")
    acceptance = candidate.get("acceptance", {})
    if acceptance.get("certificationInput") is not True:
        raise RuntimeError("CandidateNotFrozenForCertification")
    if acceptance.get("productionBinding") is not False:
        raise RuntimeError("UncertifiedCandidateAlreadyBound")
    if freeze.get("scanContractVersion") != 3 or freeze.get("discoveryPolicyVersion") != 2:
        raise RuntimeError("V3ContractVersionMismatch")
    policy = freeze.get("bridgeClosurePolicy", {})
    expected_policy = {
        "policyVersion": 1,
        "regionConstructionVersion": 1,
        "recursiveSubdivisionVersion": 1,
        "visitOrderVersion": 1,
        "evidenceHashSchemaVersion": 1,
        "regionHaloFinalCells": 1,
        "maximumRecursionDepth": 3,
        "maximumSampleCountPerBridge": 32768,
        "finalPrecision": [0.1875, 0.25, 0.003125],
    }
    if policy != expected_policy:
        raise RuntimeError("BridgeClosurePolicyMismatch")
    refinement = freeze.get("refinementPolicy", {})
    if refinement != {
        "regionSeed": "union_of_base_and_half_step_f3_or_target_evidence",
        "gridAnchor": "nominal_input",
        "refinementHaloCoarseCells": 1,
        "maximumRefinementIterations": 3,
        "maximumRefinementSampleCount": 500000,
        "finalPrecision": [0.1875, 0.25, 0.003125],
    }:
        raise RuntimeError("RefinementPolicyMismatch")
    if [stage.get("name") for stage in freeze.get("discoveryStages", [])] != ["base", "half_step"]:
        raise RuntimeError("FreezeStageOrderMismatch")
    return freeze, candidate_path, candidate


def canonical_plan(
    args: argparse.Namespace,
    freeze_path: Path,
    freeze: dict[str, Any],
    candidate_path: Path,
) -> dict[str, Any]:
    manifest_path = repository_root() / "Intermediate/M11CoreStandalone/cmake/m11_core_source_manifest.json"
    manifest = legacy.load_json(manifest_path)
    return {
        "schema": PLAN_SCHEMA,
        "authority": ["ABTSM11V22CertificationCLI", "ABTSM11V3BridgeClosureCLI"],
        "pythonRole": "process_scheduling_resume_and_early_stop_only",
        "candidateRank": EXPECTED_RANK,
        "candidateSourceHash": EXPECTED_SOURCE_HASH,
        "nominalRequestHash": EXPECTED_REQUEST_HASH,
        "nominalResultHash": EXPECTED_RESULT_HASH,
        "freezeManifest": str(freeze_path),
        "freezeManifestSha256": legacy.sha256_file(freeze_path),
        "candidateManifest": str(candidate_path),
        "candidateManifestSha256": legacy.sha256_file(candidate_path),
        "scanExecutable": str(args.scan_executable),
        "scanExecutableSha256": legacy.sha256_file(args.scan_executable),
        "bridgeExecutable": str(args.bridge_executable),
        "bridgeExecutableSha256": legacy.sha256_file(args.bridge_executable),
        "searchSourceHashSha256": manifest.get("searchSourceHashSha256"),
        "productionCoreSourceHashSha256": manifest.get("productionCoreSourceHashSha256"),
        "shardCount": args.shards,
        "threadsPerShard": args.threads_per_shard,
        "bridgeThreads": args.bridge_threads,
        "checkpointEvery": args.checkpoint_every,
        "logicalCpuCount": os.cpu_count() or 1,
        "pythonVersion": sys.version.split()[0],
        "domain": freeze["domain"],
        "discoveryStages": freeze["discoveryStages"],
        "bridgeClosurePolicy": freeze["bridgeClosurePolicy"],
        "refinementPolicy": freeze["refinementPolicy"],
    }


def verify_screen_aim(root: Path, executable: Path) -> dict[str, Any]:
    output = root / "identity/screen_aim"
    summary_path = output / "screen_aim_summary.json"
    if not summary_path.is_file():
        output.mkdir(parents=True, exist_ok=True)
        code = legacy.run_process(
            [str(executable), "screen-aim", "--rank", "12", "--output", str(output),
             "--screen-aim-samples", "5000"],
            root / "logs/identity_screen_aim.stdout.log",
            root / "logs/identity_screen_aim.stderr.log",
        )
        if code != 0:
            raise RuntimeError(f"Rank12ScreenAimFailed:{code}")
    summary = legacy.load_json(summary_path)
    checks = {
        "variantSourceHash": EXPECTED_SOURCE_HASH,
        "nominalRequestHash": EXPECTED_REQUEST_HASH,
        "nominalResultHash": EXPECTED_RESULT_HASH,
    }
    for field, expected in checks.items():
        if legacy.normalized_hash(summary.get(field), f"screenAim.{field}") != expected:
            raise RuntimeError(f"Rank12ScreenAimIdentityMismatch:{field}")
    if summary.get("prefixCounts") != [615, 139, 18, 8]:
        raise RuntimeError("Rank12ScreenAimEvidenceDrift")
    return summary


def hard_failure_reasons(summary: dict[str, Any]) -> list[str]:
    reasons: list[str] = []
    prefix = summary.get("prefixCounts", [])
    if len(prefix) != 4 or prefix[3] == 0:
        reasons.append("f4_empty")
    if summary.get("nestingViolations") != 0:
        reasons.append("prefix_nesting_violation")
    if summary.get("earlyTargetHitCount") != 0:
        reasons.append("early_target_hit")
    if summary.get("bypassTargetHitCount") != 0:
        reasons.append("bypass_target_hit")
    return reasons


def write_status(
    root: Path,
    state: str,
    stage: str,
    reasons: list[str],
    summaries: dict[str, Any],
) -> None:
    legacy.atomic_write_text(
        root / "certification_status.json",
        json.dumps({
            "schema": STATUS_SCHEMA,
            "state": state,
            "stage": stage,
            "candidateRank": EXPECTED_RANK,
            "candidateSourceHash": EXPECTED_SOURCE_HASH,
            "reasons": reasons,
            "summaries": summaries,
            "updatedUtc": dt.datetime.now(dt.timezone.utc).isoformat(),
        }, indent=2, sort_keys=True) + "\n",
    )


def run_bridge(
    root: Path,
    plan: dict[str, Any],
    half_summary: dict[str, Any],
) -> tuple[int, dict[str, Any]]:
    output = root / "half_step/bridge_closure"
    result_path = output / "closure_result.json"
    if result_path.is_file():
        result = legacy.load_json(result_path)
        return (0 if result.get("passed") is True else 2), result
    grid = half_summary["grid"]
    command = [
        plan["bridgeExecutable"], "close", "--rank", "12",
        "--samples", str(root / "half_step/merged/samples.tsv"),
        "--output", str(output), "--threads", str(plan["bridgeThreads"]),
        "--yaw-count", str(grid["yawCount"]),
        "--pitch-count", str(grid["pitchCount"]),
        "--power-count", str(grid["powerCount"]),
        "--min-yaw", str(grid["minYaw"]),
        "--min-pitch", str(grid["minPitch"]),
        "--min-power", str(grid["minPower"]),
        "--yaw-step", str(grid["yawStep"]),
        "--pitch-step", str(grid["pitchStep"]),
        "--power-step", str(grid["powerStep"]),
    ]
    code = legacy.run_process(
        command,
        root / "logs/half_step_bridge.stdout.log",
        root / "logs/half_step_bridge.stderr.log",
    )
    if code not in (0, 2) or not result_path.is_file():
        raise RuntimeError(f"BridgeClosureExecutionFailed:{code}")
    return code, legacy.load_json(result_path)


def assess_refinement_budget(
    root: Path,
    freeze: dict[str, Any],
    summaries: dict[str, Any],
) -> dict[str, Any]:
    bounds: list[list[Decimal]] | None = None
    evidence_count = 0
    for stage_name in ("base", "half_step"):
        summary = summaries[stage_name]
        grid = summary["grid"]
        minimum = [Decimal(str(grid[key])) for key in ("minYaw", "minPitch", "minPower")]
        step = [Decimal(str(grid[key])) for key in ("yawStep", "pitchStep", "powerStep")]
        sample_path = root / stage_name / "merged/samples.tsv"
        for line in sample_path.read_text(encoding="utf-8").splitlines()[1:]:
            fields = line.split()
            indices = [int(fields[1]), int(fields[2]), int(fields[3])]
            prefix_mask = int(fields[4])
            termination = int(fields[5])
            contacts = int(fields[7])
            if (prefix_mask & 0x4) == 0 and termination != 1 and contacts == 0:
                continue
            point = [minimum[axis] + step[axis] * indices[axis] for axis in range(3)]
            if bounds is None:
                bounds = [[value, value] for value in point]
            else:
                for axis, value in enumerate(point):
                    bounds[axis][0] = min(bounds[axis][0], value)
                    bounds[axis][1] = max(bounds[axis][1], value)
            evidence_count += 1
    if bounds is None:
        raise RuntimeError("NoRefinementCandidate")

    domain = freeze["domain"]
    domain_bounds = [domain["yawDegrees"], domain["pitchDegrees"], domain["power"]]
    coarse = [Decimal(str(value)) for value in freeze["discoveryStages"][0]["steps"]]
    final = [Decimal(str(value)) for value in freeze["refinementPolicy"]["finalPrecision"]]
    anchor = [Decimal(str(value)) for value in freeze["nominalInput"]]
    halo = Decimal(str(freeze["refinementPolicy"]["refinementHaloCoarseCells"]))
    refined_bounds: list[list[Decimal]] = []
    counts: list[int] = []
    for axis in range(3):
        lower_domain = Decimal(str(domain_bounds[axis][0]))
        upper_domain = Decimal(str(domain_bounds[axis][1]))
        desired_min = max(lower_domain, bounds[axis][0] - halo * coarse[axis])
        desired_max = min(upper_domain, bounds[axis][1] + halo * coarse[axis])
        minimum_steps = ((desired_min - anchor[axis]) / final[axis]).to_integral_value(
            rounding=ROUND_FLOOR)
        maximum_steps = ((desired_max - anchor[axis]) / final[axis]).to_integral_value(
            rounding=ROUND_CEILING)
        refined_min = anchor[axis] + minimum_steps * final[axis]
        refined_max = anchor[axis] + maximum_steps * final[axis]
        refined_bounds.append([refined_min, refined_max])
        counts.append(int(maximum_steps - minimum_steps) + 1)
    sample_count = counts[0] * counts[1] * counts[2]
    maximum = int(freeze["refinementPolicy"]["maximumRefinementSampleCount"])
    result = {
        "schema": "abts.m11b.v3.refinement_budget_precheck.v1",
        "passed": sample_count <= maximum,
        "regionSeedEvidenceSampleCount": evidence_count,
        "rawEvidenceBounds": [[float(value) for value in axis] for axis in bounds],
        "refinementGridBounds": [[float(value) for value in axis] for axis in refined_bounds],
        "refinementGridCounts": counts,
        "refinementSampleCount": sample_count,
        "maximumRefinementSampleCount": maximum,
        "finalPrecision": [float(value) for value in final],
        "gridAnchor": [float(value) for value in anchor],
        "failure": "" if sample_count <= maximum else "RefinementSampleBudgetExceeded",
    }
    legacy.atomic_write_text(
        root / "refinement/budget_precheck.json",
        json.dumps(result, indent=2, sort_keys=True) + "\n",
    )
    return result


def run_final_refinement(
    root: Path,
    plan: dict[str, Any],
    freeze: dict[str, Any],
    refinement_budget: dict[str, Any],
    resume: bool,
) -> dict[str, Any]:
    """Schedule the frozen final-precision grid through the C++ authority."""
    bounds = refinement_budget["refinementGridBounds"]
    counts = refinement_budget["refinementGridCounts"]
    sample_count = int(refinement_budget["refinementSampleCount"])
    if sample_count != counts[0] * counts[1] * counts[2]:
        raise RuntimeError("RefinementGridCountMismatch")

    refinement_freeze = dict(freeze)
    refinement_freeze["domain"] = {
        "yawDegrees": bounds[0],
        "pitchDegrees": bounds[1],
        "power": bounds[2],
    }
    stage = {
        "name": "refinement",
        "steps": freeze["refinementPolicy"]["finalPrecision"],
        "expectedSampleCount": sample_count,
    }
    shard_plan = dict(plan)
    shard_plan["executable"] = plan["scanExecutable"]
    return legacy.run_stage(
        shard_plan, refinement_freeze, root, stage, resume)


def refinement_failure_reasons(summary: dict[str, Any]) -> list[str]:
    reasons = hard_failure_reasons(summary)
    prefix = summary.get("prefixCounts", [])
    components = summary.get("componentCounts", [])
    if len(components) != 4 or components[3] != 1:
        reasons.append("final_f4_component_count_not_one")
    if len(prefix) != 4 or not (prefix[0] > prefix[1] > prefix[2] > prefix[3] > 0):
        reasons.append("refined_prefix_difference_set_empty")
    if summary.get("nominalF4") is not True:
        reasons.append("refined_nominal_not_in_f4")
    return reasons


def command_validate(args: argparse.Namespace) -> int:
    freeze, candidate_path, _ = validate_freeze(args.freeze.resolve())
    print(json.dumps({
        "schema": "abts.m11b.v3.rank12_freeze_validation.v1",
        "passed": True,
        "candidateRank": EXPECTED_RANK,
        "candidateSourceHash": EXPECTED_SOURCE_HASH,
        "freezeManifest": str(args.freeze.resolve()),
        "candidateManifest": str(candidate_path),
        "bridgeClosurePolicy": freeze["bridgeClosurePolicy"],
    }, indent=2, sort_keys=True))
    return 0


def command_run(args: argparse.Namespace) -> int:
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    args.scan_executable = args.scan_executable.resolve()
    args.bridge_executable = args.bridge_executable.resolve()
    if not args.scan_executable.is_file() or not args.bridge_executable.is_file():
        raise RuntimeError("CertificationExecutableMissing")
    requested_threads = args.shards * args.threads_per_shard
    if max(requested_threads, args.bridge_threads) > (os.cpu_count() or 1) and not args.allow_oversubscribe:
        raise RuntimeError("OversubscriptionRejected")
    freeze_path = args.freeze.resolve()
    freeze, candidate_path, _ = validate_freeze(freeze_path)
    plan = legacy.validate_or_create_plan(
        root, canonical_plan(args, freeze_path, freeze, candidate_path), args.resume)
    verify_screen_aim(root, args.scan_executable)
    legacy.verify_nominal_f4(root, freeze, args.scan_executable)

    # The reused scheduler reads this key when it launches shards.
    shard_plan = dict(plan)
    shard_plan["executable"] = plan["scanExecutable"]
    summaries: dict[str, Any] = {}
    for stage_name in ("base", "half_step"):
        stage = legacy.stage_definition(freeze, stage_name)
        summaries[stage_name] = legacy.run_stage(
            shard_plan, freeze, root, stage, args.resume)
        reasons = hard_failure_reasons(summaries[stage_name])
        if reasons:
            write_status(root, "early_stopped", stage_name, reasons, summaries)
            print(json.dumps({"state": "early_stopped", "stage": stage_name, "reasons": reasons}, indent=2))
            return 2

    bridge_code, bridge_result = run_bridge(root, plan, summaries["half_step"])
    summaries["bridge_closure"] = bridge_result
    if bridge_code != 0 or bridge_result.get("passed") is not True:
        reasons = [bridge_result.get("failure") or "bridge_closure_failed"]
        write_status(root, "early_stopped", "bridge_closure", reasons, summaries)
        print(json.dumps({"state": "early_stopped", "stage": "bridge_closure", "reasons": reasons}, indent=2))
        return 2

    refinement_budget = assess_refinement_budget(root, freeze, summaries)
    summaries["refinement_budget"] = refinement_budget
    if refinement_budget.get("passed") is not True:
        reasons = ["refinement_sample_budget_exceeded"]
        write_status(root, "early_stopped", "refinement_budget", reasons, summaries)
        print(json.dumps({
            "state": "early_stopped",
            "stage": "refinement_budget",
            "reasons": reasons,
            "refinementSampleCount": refinement_budget["refinementSampleCount"],
            "maximumRefinementSampleCount": refinement_budget["maximumRefinementSampleCount"],
        }, indent=2))
        return 2

    summaries["refinement"] = run_final_refinement(
        root, plan, freeze, refinement_budget, args.resume)
    reasons = refinement_failure_reasons(summaries["refinement"])
    if reasons:
        write_status(root, "early_stopped", "refinement", reasons, summaries)
        print(json.dumps({
            "state": "early_stopped",
            "stage": "refinement",
            "reasons": reasons,
            "prefixCounts": summaries["refinement"].get("prefixCounts"),
            "componentCounts": summaries["refinement"].get("componentCounts"),
        }, indent=2))
        return 2

    state = "refinement_passed_requires_width_trust_and_ablation"
    write_status(root, state, "refinement", [], summaries)
    print(json.dumps({"state": state, "stage": "refinement"}, indent=2))
    return 3


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Schedule the uniquely frozen Rank 12 v3 certification input.")
    subparsers = parser.add_subparsers(dest="command", required=True)
    validate = subparsers.add_parser("validate")
    validate.add_argument("--freeze", type=Path, default=default_freeze())
    validate.set_defaults(handler=command_validate)
    run = subparsers.add_parser("run")
    run.add_argument("--output", type=Path, required=True)
    run.add_argument("--freeze", type=Path, default=default_freeze())
    run.add_argument("--scan-executable", type=Path, default=default_scan_executable())
    run.add_argument("--bridge-executable", type=Path, default=default_bridge_executable())
    run.add_argument("--shards", type=int, default=4)
    run.add_argument("--threads-per-shard", type=int, default=2)
    run.add_argument("--bridge-threads", type=int, default=8)
    run.add_argument("--checkpoint-every", type=int, default=256)
    run.add_argument("--resume", action="store_true")
    run.add_argument("--allow-oversubscribe", action="store_true")
    run.set_defaults(handler=command_run)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        return int(args.handler(args))
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"m11_v3_certify_rank12.py: error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
