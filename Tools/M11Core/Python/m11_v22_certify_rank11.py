#!/usr/bin/env python3
"""Fail-closed orchestration for the frozen Rank 11 v2.2 certification input.

Python owns only immutable plans, process scheduling, resume dispatch, logs,
and early-stop routing. ABTSM11V22CertificationCLI remains the sole authority
for trajectory integration, prefix classification, hashes, and connectivity.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
from typing import Any


PLAN_SCHEMA = "abts.m11b.v2_2.rank11_certification_plan.v1"
STATUS_SCHEMA = "abts.m11b.v2_2.rank11_certification_status.v1"
FREEZE_SCHEMA = "abts.m11b.v2_2.certification_input_freeze.v1"
CANDIDATE_SCHEMA = "abts.m11b.v2_2.scaled_sequential_candidate.v1"
EXPECTED_RANK = 11
EXPECTED_SOURCE_HASH = "0xcb23499fc6f7c9d3"
EXPECTED_REQUEST_HASH = "0x4f0e3c66a1a0a737"
EXPECTED_RESULT_HASH = "0x505f3312ac8ae07f"
EXPECTED_SCORE_HASH = "0xd71f1166493c07aa"


def repository_root() -> Path:
    return Path(__file__).resolve().parents[3]


def default_executable() -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    return (
        repository_root()
        / "Intermediate"
        / "M11CoreStandalone"
        / "bin"
        / f"ABTSM11V22CertificationCLI{suffix}"
    )


def default_freeze_manifest() -> Path:
    return (
        repository_root()
        / "Tools"
        / "M11Core"
        / "Certification"
        / "Rank11V22CertificationInput.json"
    )


def atomic_write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(text, encoding="utf-8", newline="\n")
    temporary.replace(path)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise RuntimeError(f"JsonRootNotObject:{path}")
    return value


def normalized_hash(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value.startswith("0x"):
        raise RuntimeError(f"InvalidHash:{field}:{value}")
    return value.lower()


def resolve_candidate_manifest(freeze: dict[str, Any]) -> Path:
    relative = freeze.get("candidateManifest")
    if not isinstance(relative, str) or not relative:
        raise RuntimeError("FreezeCandidateManifestMissing")
    path = (repository_root() / relative).resolve()
    try:
        path.relative_to(repository_root().resolve())
    except ValueError as error:
        raise RuntimeError("FreezeCandidateManifestOutsideRepository") from error
    return path


def validate_freeze(freeze_path: Path) -> tuple[dict[str, Any], Path, dict[str, Any]]:
    if not freeze_path.is_file():
        raise RuntimeError(f"FreezeManifestMissing:{freeze_path}")
    freeze = load_json(freeze_path)
    if freeze.get("schema") != FREEZE_SCHEMA:
        raise RuntimeError("FreezeSchemaMismatch")
    if freeze.get("status") != "frozen_unique_certification_input":
        raise RuntimeError("FreezeStatusMismatch")
    if freeze.get("authority") != "user_approved_rank11_freeze":
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
        if normalized_hash(freeze.get(field), f"freeze.{field}") != expected_value:
            raise RuntimeError(f"FreezeIdentityMismatch:{field}")

    candidate_path = resolve_candidate_manifest(freeze)
    if not candidate_path.is_file():
        raise RuntimeError(f"CandidateManifestMissing:{candidate_path}")
    candidate = load_json(candidate_path)
    if candidate.get("schema") != CANDIDATE_SCHEMA:
        raise RuntimeError("CandidateSchemaMismatch")
    if candidate.get("status") != "frozen_unique_v22_certification_input":
        raise RuntimeError("CandidateStatusMismatch")
    if candidate.get("editorCandidateRank") != EXPECTED_RANK:
        raise RuntimeError("CandidateRankMismatch")
    for field, expected_value in expected.items():
        if normalized_hash(candidate.get(field), f"candidate.{field}") != expected_value:
            raise RuntimeError(f"CandidateIdentityMismatch:{field}")
        if normalized_hash(candidate.get(field), f"candidate.{field}") != normalized_hash(
            freeze.get(field), f"freeze.{field}"
        ):
            raise RuntimeError(f"FreezeCandidateDisagreement:{field}")
    acceptance = candidate.get("acceptance")
    if not isinstance(acceptance, dict):
        raise RuntimeError("CandidateAcceptanceMissing")
    certification_state = acceptance.get("fullInputDomainCertification")
    replayable_states = {
        "in_progress",
        "early_stopped_half_step_non_unique_f4_and_early_target_hit",
    }
    if certification_state not in replayable_states:
        raise RuntimeError("CandidateCertificationStateMismatch")

    domain = freeze.get("domain")
    stages = freeze.get("discoveryStages")
    if not isinstance(domain, dict) or not isinstance(stages, list) or len(stages) != 2:
        raise RuntimeError("FreezeScanContractMissing")
    if [stage.get("name") for stage in stages] != ["base", "half_step"]:
        raise RuntimeError("FreezeStageOrderMismatch")
    return freeze, candidate_path, candidate


def run_process(command: list[str], stdout_path: Path, stderr_path: Path) -> int:
    started = time.perf_counter()
    result = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        shell=False,
    )
    elapsed = time.perf_counter() - started
    atomic_write_text(
        stdout_path,
        result.stdout
        + f"\n[orchestrator] returnCode={result.returncode}"
        + f" elapsedSeconds={elapsed:.6f}\n",
    )
    atomic_write_text(stderr_path, result.stderr)
    return result.returncode


def stage_definition(freeze: dict[str, Any], name: str) -> dict[str, Any]:
    for stage in freeze["discoveryStages"]:
        if stage.get("name") == name:
            return stage
    raise RuntimeError(f"StageNotFrozen:{name}")


def domain_arguments(freeze: dict[str, Any], stage: dict[str, Any]) -> list[str]:
    domain = freeze["domain"]
    yaw = domain["yawDegrees"]
    pitch = domain["pitchDegrees"]
    power = domain["power"]
    steps = stage["steps"]
    return [
        "--min-yaw", str(yaw[0]), "--max-yaw", str(yaw[1]),
        "--min-pitch", str(pitch[0]), "--max-pitch", str(pitch[1]),
        "--min-power", str(power[0]), "--max-power", str(power[1]),
        "--yaw-step", str(steps[0]),
        "--pitch-step", str(steps[1]),
        "--power-step", str(steps[2]),
    ]


def canonical_plan(args: argparse.Namespace, freeze_path: Path, freeze: dict[str, Any], candidate_path: Path, executable: Path) -> dict[str, Any]:
    source_manifest_path = (
        repository_root()
        / "Intermediate"
        / "M11CoreStandalone"
        / "cmake"
        / "m11_core_source_manifest.json"
    )
    source_manifest = load_json(source_manifest_path) if source_manifest_path.is_file() else {}
    return {
        "schema": PLAN_SCHEMA,
        "authority": "ABTSM11V22CertificationCLI",
        "pythonRole": "process_scheduling_resume_and_early_stop_only",
        "candidateRank": EXPECTED_RANK,
        "candidateSourceHash": EXPECTED_SOURCE_HASH,
        "nominalRequestHash": EXPECTED_REQUEST_HASH,
        "nominalResultHash": EXPECTED_RESULT_HASH,
        "freezeManifest": str(freeze_path),
        "freezeManifestSha256": sha256_file(freeze_path),
        "candidateManifest": str(candidate_path),
        "candidateManifestSha256": sha256_file(candidate_path),
        "executable": str(executable),
        "executableSha256": sha256_file(executable),
        "searchSourceHashSha256": source_manifest.get("searchSourceHashSha256"),
        "productionCoreSourceHashSha256": source_manifest.get("productionCoreSourceHashSha256"),
        "shardCount": args.shards,
        "threadsPerShard": args.threads_per_shard,
        "checkpointEvery": args.checkpoint_every,
        "logicalCpuCount": os.cpu_count() or 1,
        "pythonVersion": sys.version.split()[0],
        "discoveryStages": freeze["discoveryStages"],
        "domain": freeze["domain"],
    }


def validate_or_create_plan(root: Path, requested: dict[str, Any], resume: bool) -> dict[str, Any]:
    plan_path = root / "plan.json"
    if plan_path.is_file():
        existing = load_json(plan_path)
        comparable = dict(existing)
        comparable.pop("createdUtc", None)
        if comparable != requested:
            raise RuntimeError("ImmutableCertificationPlanMismatch")
        if not resume:
            raise RuntimeError("CertificationPlanAlreadyExistsUseResume")
        return existing
    if resume:
        raise RuntimeError("ResumeRequestedWithoutCertificationPlan")
    plan = dict(requested)
    plan["createdUtc"] = dt.datetime.now(dt.timezone.utc).isoformat()
    atomic_write_text(plan_path, json.dumps(plan, indent=2, sort_keys=True) + "\n")
    return plan


def verify_screen_aim(root: Path, executable: Path) -> dict[str, Any]:
    output = root / "identity" / "screen_aim"
    summary_path = output / "screen_aim_summary.json"
    if not summary_path.is_file():
        output.mkdir(parents=True, exist_ok=True)
        logs = root / "logs"
        logs.mkdir(parents=True, exist_ok=True)
        code = run_process(
            [str(executable), "screen-aim", "--rank", str(EXPECTED_RANK),
             "--output", str(output), "--screen-aim-samples", "5000"],
            logs / "identity_screen_aim.stdout.log",
            logs / "identity_screen_aim.stderr.log",
        )
        if code != 0:
            raise RuntimeError(f"Rank11ScreenAimFailed:{code}")
    summary = load_json(summary_path)
    if normalized_hash(summary.get("variantSourceHash"), "screenAim.variantSourceHash") != EXPECTED_SOURCE_HASH:
        raise RuntimeError("Rank11ScreenAimSourceHashMismatch")
    if normalized_hash(summary.get("nominalRequestHash"), "screenAim.nominalRequestHash") != EXPECTED_REQUEST_HASH:
        raise RuntimeError("Rank11ScreenAimRequestHashMismatch")
    if normalized_hash(summary.get("nominalResultHash"), "screenAim.nominalResultHash") != EXPECTED_RESULT_HASH:
        raise RuntimeError("Rank11ScreenAimResultHashMismatch")
    if summary.get("prefixCounts") != [616, 137, 21, 6]:
        raise RuntimeError("Rank11ScreenAimEvidenceDrift")
    return summary


def verify_nominal_f4(root: Path, freeze: dict[str, Any], executable: Path) -> dict[str, Any]:
    nominal = freeze.get("nominalInput")
    if nominal != [-1.75, 26.25, 1.0]:
        raise RuntimeError("FrozenNominalInputMismatch")
    stage_root = root / "identity" / "nominal_f4"
    shard = stage_root / "shards" / "shard_0000"
    merged = stage_root / "merged"
    summary_path = merged / "summary.json"
    logs = root / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    grid = [
        "--min-yaw", str(nominal[0]), "--max-yaw", str(nominal[0]),
        "--min-pitch", str(nominal[1]), "--max-pitch", str(nominal[1]),
        "--min-power", str(nominal[2]), "--max-power", str(nominal[2]),
        "--yaw-step", "1", "--pitch-step", "1", "--power-step", "1",
    ]
    if not summary_path.is_file():
        shard.mkdir(parents=True, exist_ok=True)
        code = run_process(
            [str(executable), "preflight", "--rank", str(EXPECTED_RANK),
             "--output", str(shard), "--threads", "1",
             "--shard-index", "0", "--shard-count", "1", *grid],
            logs / "identity_nominal_shard.stdout.log",
            logs / "identity_nominal_shard.stderr.log",
        )
        if code != 0:
            raise RuntimeError(f"Rank11NominalPreflightFailed:{code}")
        code = run_process(
            [str(executable), "merge", "--rank", str(EXPECTED_RANK),
             "--input-root", str(stage_root / "shards"),
             "--output", str(merged), "--shard-count", "1", *grid],
            logs / "identity_nominal_merge.stdout.log",
            logs / "identity_nominal_merge.stderr.log",
        )
        if code != 0:
            raise RuntimeError(f"Rank11NominalMergeFailed:{code}")
    summary = load_json(summary_path)
    if summary.get("passed") is not True:
        raise RuntimeError("Rank11NominalF4Rejected")
    if summary.get("prefixCounts") != [1, 1, 1, 1]:
        raise RuntimeError("Rank11NominalPrefixMismatch")
    if summary.get("componentCounts") != [1, 1, 1, 1]:
        raise RuntimeError("Rank11NominalComponentMismatch")
    if summary.get("nominalF4") is not True:
        raise RuntimeError("Rank11NominalIdentityMismatch")
    return summary


def shard_directory(root: Path, stage_name: str, shard_index: int) -> Path:
    return root / stage_name / "shards" / f"shard_{shard_index:04d}"


def run_shard(plan: dict[str, Any], freeze: dict[str, Any], root: Path, stage: dict[str, Any], shard_index: int, resume: bool, assist_mask: int = 7, expect_no_f4: bool = False) -> tuple[int, int, str]:
    stage_name = stage["name"]
    output = shard_directory(root, stage_name, shard_index)
    output.mkdir(parents=True, exist_ok=True)
    summary_path = output / "summary.json"
    if summary_path.is_file():
        summary = load_json(summary_path)
        if summary.get("complete") is True:
            return shard_index, 0, "already-complete"
    samples_path = output / "samples.tsv"
    if samples_path.exists() and not resume:
        raise RuntimeError(f"ShardStateAlreadyExists:{stage_name}:{shard_index}")
    command = [
        plan["executable"], "preflight", "--rank", str(EXPECTED_RANK),
        "--output", str(output),
        "--threads", str(plan["threadsPerShard"]),
        "--shard-index", str(shard_index),
        "--shard-count", str(plan["shardCount"]),
        "--checkpoint-every", str(plan["checkpointEvery"]),
        "--assist-mask", str(assist_mask),
        "--allow-off-grid-nominal",
        *domain_arguments(freeze, stage),
    ]
    if expect_no_f4:
        command.append("--expect-no-f4")
    if samples_path.exists():
        command.append("--resume")
    logs = root / "logs"
    code = run_process(
        command,
        logs / f"{stage_name}_shard_{shard_index:04d}.stdout.log",
        logs / f"{stage_name}_shard_{shard_index:04d}.stderr.log",
    )
    return shard_index, code, ""


def merge_stage(plan: dict[str, Any], freeze: dict[str, Any], root: Path, stage: dict[str, Any], assist_mask: int = 7, expect_no_f4: bool = False) -> dict[str, Any]:
    stage_name = stage["name"]
    merged = root / stage_name / "merged"
    summary_path = merged / "summary.json"
    if summary_path.is_file():
        return load_json(summary_path)
    command = [
        plan["executable"], "merge", "--rank", str(EXPECTED_RANK),
        "--input-root", str(root / stage_name / "shards"),
        "--output", str(merged),
        "--shard-count", str(plan["shardCount"]),
        "--assist-mask", str(assist_mask),
        "--allow-off-grid-nominal",
        *domain_arguments(freeze, stage),
    ]
    if expect_no_f4:
        command.append("--expect-no-f4")
    logs = root / "logs"
    code = run_process(
        command,
        logs / f"{stage_name}_merge.stdout.log",
        logs / f"{stage_name}_merge.stderr.log",
    )
    if code not in (0, 2) or not summary_path.is_file():
        raise RuntimeError(f"AuthoritativeMergeFailed:{stage_name}:{code}")
    summary = load_json(summary_path)
    if normalized_hash(summary.get("candidateSourceHash"), f"{stage_name}.candidateSourceHash") != EXPECTED_SOURCE_HASH:
        raise RuntimeError(f"StageSourceHashMismatch:{stage_name}")
    if summary.get("sampleCount") != stage.get("expectedSampleCount"):
        raise RuntimeError(f"StageSampleCountMismatch:{stage_name}")
    return summary


def run_stage(plan: dict[str, Any], freeze: dict[str, Any], root: Path, stage: dict[str, Any], resume: bool) -> dict[str, Any]:
    failures: list[str] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=plan["shardCount"]) as executor:
        futures = [
            executor.submit(run_shard, plan, freeze, root, stage, index, resume)
            for index in range(plan["shardCount"])
        ]
        for future in concurrent.futures.as_completed(futures):
            index, code, diagnostic = future.result()
            print(
                f"[M11-B-v2.2][Rank{EXPECTED_RANK}][{stage['name']}] "
                f"shard={index + 1}/{plan['shardCount']} returnCode={code}",
                flush=True,
            )
            if code != 0:
                failures.append(f"shard={index}:{code}:{diagnostic}")
    if failures:
        raise RuntimeError("CertificationShardFailures:" + " | ".join(failures))
    return merge_stage(plan, freeze, root, stage)


def early_stop_reasons(summary: dict[str, Any], require_unique_f4: bool) -> list[str]:
    reasons: list[str] = []
    prefix = summary.get("prefixCounts", [0, 0, 0, 0])
    components = summary.get("componentCounts", [0, 0, 0, 0])
    if len(prefix) != 4 or prefix[3] == 0:
        reasons.append("f4_empty")
    if require_unique_f4 and (len(components) != 4 or components[3] != 1):
        reasons.append("f4_component_count_not_one")
    if summary.get("nestingViolations") != 0:
        reasons.append("prefix_nesting_violation")
    if summary.get("earlyTargetHitCount") != 0:
        reasons.append("early_target_hit")
    if summary.get("bypassTargetHitCount") != 0:
        reasons.append("bypass_target_hit")
    return reasons


def write_status(root: Path, state: str, stage: str, reasons: list[str], summaries: dict[str, Any]) -> None:
    status = {
        "schema": STATUS_SCHEMA,
        "state": state,
        "stage": stage,
        "candidateRank": EXPECTED_RANK,
        "candidateSourceHash": EXPECTED_SOURCE_HASH,
        "reasons": reasons,
        "summaries": summaries,
        "updatedUtc": dt.datetime.now(dt.timezone.utc).isoformat(),
    }
    atomic_write_text(
        root / "certification_status.json",
        json.dumps(status, indent=2, sort_keys=True) + "\n",
    )


def command_validate(args: argparse.Namespace) -> int:
    freeze_path = args.freeze.resolve()
    freeze, candidate_path, _ = validate_freeze(freeze_path)
    result = {
        "schema": "abts.m11b.v2_2.rank11_freeze_validation.v1",
        "passed": True,
        "candidateRank": EXPECTED_RANK,
        "candidateSourceHash": EXPECTED_SOURCE_HASH,
        "nominalRequestHash": EXPECTED_REQUEST_HASH,
        "nominalResultHash": EXPECTED_RESULT_HASH,
        "freezeManifest": str(freeze_path),
        "candidateManifest": str(candidate_path),
        "discoveryStages": freeze["discoveryStages"],
    }
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


def command_run(args: argparse.Namespace) -> int:
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    freeze_path = args.freeze.resolve()
    freeze, candidate_path, _ = validate_freeze(freeze_path)
    executable = args.executable.resolve()
    if not executable.is_file():
        raise RuntimeError(f"CertificationExecutableMissing:{executable}")
    logical_cpu = os.cpu_count() or 1
    requested_threads = args.shards * args.threads_per_shard
    if requested_threads > logical_cpu and not args.allow_oversubscribe:
        raise RuntimeError(f"OversubscriptionRejected:{requested_threads}>{logical_cpu}")
    requested = canonical_plan(args, freeze_path, freeze, candidate_path, executable)
    plan = validate_or_create_plan(root, requested, args.resume)
    verify_screen_aim(root, executable)
    verify_nominal_f4(root, freeze, executable)

    summaries: dict[str, Any] = {}
    base = stage_definition(freeze, "base")
    summaries["base"] = run_stage(plan, freeze, root, base, args.resume)
    reasons = early_stop_reasons(summaries["base"], require_unique_f4=False)
    if reasons:
        write_status(root, "early_stopped", "base", reasons, summaries)
        print(json.dumps({"state": "early_stopped", "stage": "base", "reasons": reasons}, indent=2))
        return 2

    half_step = stage_definition(freeze, "half_step")
    summaries["half_step"] = run_stage(plan, freeze, root, half_step, args.resume)
    reasons = early_stop_reasons(summaries["half_step"], require_unique_f4=True)
    if reasons:
        write_status(root, "early_stopped", "half_step", reasons, summaries)
        print(json.dumps({"state": "early_stopped", "stage": "half_step", "reasons": reasons}, indent=2))
        return 2

    write_status(
        root,
        "discovery_passed_requires_boundary_and_ablation",
        "half_step",
        [],
        summaries,
    )
    print(json.dumps({
        "state": "discovery_passed_requires_boundary_and_ablation",
        "stage": "half_step",
    }, indent=2))
    return 3


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Schedule the authoritative C++ v2.2 scan for the uniquely frozen "
            "Rank 11 input; no rank or diagnostic-layout override is exposed."
        )
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    validate = subparsers.add_parser("validate")
    validate.add_argument("--freeze", type=Path, default=default_freeze_manifest())
    validate.set_defaults(handler=command_validate)

    run = subparsers.add_parser("run")
    run.add_argument("--output", type=Path, required=True)
    run.add_argument("--freeze", type=Path, default=default_freeze_manifest())
    run.add_argument("--executable", type=Path, default=default_executable())
    run.add_argument("--shards", type=int, default=4)
    run.add_argument("--threads-per-shard", type=int, default=2)
    run.add_argument("--checkpoint-every", type=int, default=256)
    run.add_argument("--resume", action="store_true")
    run.add_argument("--allow-oversubscribe", action="store_true")
    run.set_defaults(handler=command_run)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return int(args.handler(args))
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"m11_v22_certify_rank11.py: error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
