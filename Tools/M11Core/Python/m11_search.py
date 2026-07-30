#!/usr/bin/env python3
"""Standard-library orchestration for the authoritative M11-B v2.1 C++ search.

Python owns process scheduling, immutable plans, logs, and resume dispatch only.
Every trajectory, acceptance decision, hash, global ranking, and candidate
manifest is produced by ABTSM11SearchCLI.
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


PLAN_SCHEMA = "abts.m11b21.orchestration_plan.v3"
DEFAULT_SEED = 0x11B21001


def atomic_write_text(path: Path, text: str) -> None:
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(text, encoding="utf-8", newline="\n")
    temporary.replace(path)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def default_executable() -> Path:
    repository = Path(__file__).resolve().parents[3]
    suffix = ".exe" if os.name == "nt" else ""
    return (
        repository
        / "Intermediate"
        / "M11CoreStandalone"
        / "bin"
        / f"ABTSM11SearchCLI{suffix}"
    )


def describe_contract(executable: Path, seed: int) -> dict[str, Any]:
    result = subprocess.run(
        [
            str(executable),
            "--describe-contract",
            "--seed",
            str(seed),
            "--json",
        ],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        shell=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            "ContractDescriptorFailed:"
            + (result.stderr.strip() or result.stdout.strip())
        )
    try:
        descriptor = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError("ContractDescriptorInvalidJson") from error
    if (
        descriptor.get("schema")
        != "abts.m11b21.contract_descriptor.v1"
        or descriptor.get("authority") != "ABTSM11SearchCLI"
        or not descriptor.get("contractHash")
        or not descriptor.get("searchSourceHashSha256")
    ):
        raise RuntimeError("ContractDescriptorIdentityInvalid")
    return descriptor


def canonical_plan(args: argparse.Namespace, executable: Path) -> dict[str, Any]:
    descriptor = describe_contract(executable, args.seed)
    return {
        "schema": PLAN_SCHEMA,
        "executable": str(executable),
        "executableSha256": sha256_file(executable),
        "workItems": args.work_items,
        "shardCount": args.shards,
        "threadsPerShard": args.threads_per_shard,
        "topK": args.top_k,
        "seed": args.seed,
        "checkpointEvery": args.checkpoint_every,
        "logicalCpuCount": os.cpu_count() or 1,
        "pythonVersion": sys.version.split()[0],
        "authority": "ABTSM11SearchCLI",
        "pythonRole": "process-orchestration-only",
        "contractDescriptor": descriptor,
        "candidateAnalysis": {
            "screenAimSamples": 5000,
            "screenAimDimensions": ["yaw", "pitch"],
            "screenAimPower": "nominalInputPower",
            "screenAimAuthority": "prefix-ratio-hull-and-ux-score",
            "fullLaunchDomainSamples": 5000,
            "fullLaunchDomainDimensions": ["yaw", "pitch", "power"],
            "fullLaunchDomainAuthority": "diagnostic-only",
            "sampling": "fixed-seed-halton-low-discrepancy",
            "prefixSets": ["S1", "S2", "S3", "S4"],
            "prefixRatioGateBeforeHull": True,
            "conditionalEvidenceMergedIntoUnbiasedSets": False,
            "ranking": "cpp-hard-ratio-and-hull-gates-then-soft-score",
            "exhaustiveCertification": False,
        },
    }


def load_plan(root: Path) -> dict[str, Any]:
    plan_path = root / "plan.json"
    if not plan_path.is_file():
        raise RuntimeError(f"PlanMissing:{plan_path}")
    plan = json.loads(plan_path.read_text(encoding="utf-8"))
    if plan.get("schema") != PLAN_SCHEMA:
        raise RuntimeError("PlanSchemaMismatch")
    return plan


def validate_or_create_plan(
    root: Path,
    requested: dict[str, Any],
    resume: bool,
) -> dict[str, Any]:
    plan_path = root / "plan.json"
    if plan_path.exists():
        existing = load_plan(root)
        comparable = dict(existing)
        comparable.pop("createdUtc", None)
        if comparable != requested:
            raise RuntimeError("ImmutablePlanMismatch")
        if not resume:
            raise RuntimeError("PlanAlreadyExistsUseResume")
        return existing
    if resume:
        raise RuntimeError("ResumeRequestedWithoutPlan")
    plan = dict(requested)
    plan["createdUtc"] = dt.datetime.now(dt.timezone.utc).isoformat()
    atomic_write_text(
        plan_path,
        json.dumps(plan, indent=2, sort_keys=True) + "\n",
    )
    return plan


def run_checked(
    command: list[str],
    stdout_path: Path,
    stderr_path: Path,
) -> subprocess.CompletedProcess[str]:
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
    return result


def shard_directory(root: Path, shard_index: int) -> Path:
    return root / "shards" / f"shard_{shard_index:04d}"


def shard_command(
    plan: dict[str, Any],
    root: Path,
    shard_index: int,
    resume: bool,
) -> list[str]:
    output = shard_directory(root, shard_index)
    output.mkdir(parents=True, exist_ok=True)
    checkpoint = output / "checkpoint.json"
    state_files = [
        output / "state.tsv",
        output / "evaluations.jsonl",
        checkpoint,
        output / "summary.json",
    ]
    if not resume and any(path.exists() for path in state_files):
        raise RuntimeError(f"ShardStateAlreadyExists:{shard_index}")
    command = [
        plan["executable"],
        "search",
        "--output",
        str(output),
        "--work-items",
        str(plan["workItems"]),
        "--shard-index",
        str(shard_index),
        "--shard-count",
        str(plan["shardCount"]),
        "--threads",
        str(plan["threadsPerShard"]),
        "--top-k",
        str(plan["topK"]),
        "--seed",
        str(plan["seed"]),
        "--checkpoint-every",
        str(plan["checkpointEvery"]),
        "--json",
    ]
    if resume:
        if checkpoint.is_file():
            command.append("--resume")
        elif any(path.exists() for path in state_files):
            raise RuntimeError(f"IncompleteShardWithoutCheckpoint:{shard_index}")
    return command


def run_shard(
    plan: dict[str, Any],
    root: Path,
    shard_index: int,
    resume: bool,
) -> tuple[int, int, str]:
    logs = root / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    command = shard_command(plan, root, shard_index, resume)
    result = run_checked(
        command,
        logs / f"shard_{shard_index:04d}.stdout.log",
        logs / f"shard_{shard_index:04d}.stderr.log",
    )
    return shard_index, result.returncode, result.stderr.strip()


def merge_shards(plan: dict[str, Any], root: Path) -> dict[str, Any]:
    merged = root / "merged"
    summary_path = merged / "summary.json"
    if summary_path.is_file():
        return json.loads(summary_path.read_text(encoding="utf-8"))
    if merged.exists() and any(merged.iterdir()):
        raise RuntimeError("MergeOutputExistsWithoutSummary")
    logs = root / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    command = [
        plan["executable"],
        "merge",
        "--input-root",
        str(root / "shards"),
        "--output",
        str(merged),
        "--work-items",
        str(plan["workItems"]),
        "--shard-count",
        str(plan["shardCount"]),
        "--top-k",
        str(plan["topK"]),
        "--seed",
        str(plan["seed"]),
        "--json",
    ]
    result = run_checked(
        command,
        logs / "merge.stdout.log",
        logs / "merge.stderr.log",
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"AuthoritativeMergeFailed:{result.stderr.strip()}"
        )
    if not summary_path.is_file():
        raise RuntimeError("AuthoritativeMergeDidNotWriteSummary")
    return json.loads(summary_path.read_text(encoding="utf-8"))


def command_run(args: argparse.Namespace) -> int:
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    executable = args.executable.resolve()
    if not executable.is_file():
        raise RuntimeError(f"SearchExecutableMissing:{executable}")
    if args.shards > args.work_items:
        raise RuntimeError("ShardCountCannotExceedWorkItems")
    logical_cpu = os.cpu_count() or 1
    requested_threads = args.shards * args.threads_per_shard
    if requested_threads > logical_cpu and not args.allow_oversubscribe:
        raise RuntimeError(
            "OversubscriptionRejected:"
            f"{requested_threads}>{logical_cpu}"
        )
    requested = canonical_plan(args, executable)
    plan = validate_or_create_plan(root, requested, args.resume)

    logs = root / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    self_test = run_checked(
        [str(executable), "--self-test", "--json"],
        logs / "self_test.stdout.log",
        logs / "self_test.stderr.log",
    )
    if self_test.returncode != 0:
        raise RuntimeError(
            f"AuthoritativeSelfTestFailed:{self_test.stderr.strip()}"
        )

    failures: list[str] = []
    with concurrent.futures.ThreadPoolExecutor(
        max_workers=args.shards
    ) as executor:
        futures = [
            executor.submit(run_shard, plan, root, index, args.resume)
            for index in range(args.shards)
        ]
        for future in concurrent.futures.as_completed(futures):
            shard_index, return_code, diagnostic = future.result()
            print(
                f"[M11-B-v2.1] shard {shard_index + 1}/{args.shards}"
                f" returnCode={return_code}",
                flush=True,
            )
            if return_code != 0:
                failures.append(
                    f"shard={shard_index} diagnostic={diagnostic}"
                )
    if failures:
        raise RuntimeError("ShardFailures:" + " | ".join(failures))
    summary = merge_shards(plan, root)
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


def command_status(args: argparse.Namespace) -> int:
    root = args.output.resolve()
    plan = load_plan(root)
    shards: list[dict[str, Any]] = []
    for index in range(int(plan["shardCount"])):
        directory = shard_directory(root, index)
        summary_path = directory / "summary.json"
        checkpoint_path = directory / "checkpoint.json"
        entry: dict[str, Any] = {"shardIndex": index}
        if summary_path.is_file():
            summary = json.loads(summary_path.read_text(encoding="utf-8"))
            entry.update(
                {
                    "state": "complete",
                    "evaluatedCount": summary.get("evaluatedCount", 0),
                    "acceptedCount": summary.get("acceptedCount", 0),
                }
            )
        elif checkpoint_path.is_file():
            checkpoint = json.loads(
                checkpoint_path.read_text(encoding="utf-8")
            )
            entry.update(
                {
                    "state": "partial",
                    "nextLocalOffset": checkpoint.get(
                        "nextLocalOffset", 0
                    ),
                }
            )
        else:
            entry["state"] = "not-started"
        shards.append(entry)
    status = {
        "schema": "abts.m11b21.orchestration_status.v3",
        "plan": plan,
        "shards": shards,
        "merged": (root / "merged" / "summary.json").is_file(),
    }
    print(json.dumps(status, indent=2, sort_keys=True))
    return 0


def command_merge(args: argparse.Namespace) -> int:
    root = args.output.resolve()
    plan = load_plan(root)
    summary = merge_shards(plan, root)
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Schedule the authoritative standard C++ M11-B v2.1 search; "
            "Python never integrates or classifies trajectories."
        )
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    run = subparsers.add_parser("run")
    run.add_argument("--output", type=Path, required=True)
    run.add_argument("--executable", type=Path, default=default_executable())
    run.add_argument("--work-items", type=int, required=True)
    run.add_argument("--shards", type=int, default=4)
    run.add_argument("--threads-per-shard", type=int, default=2)
    run.add_argument("--top-k", type=int, default=5)
    run.add_argument("--seed", type=int, default=DEFAULT_SEED)
    run.add_argument("--checkpoint-every", type=int, default=8)
    run.add_argument("--resume", action="store_true")
    run.add_argument("--allow-oversubscribe", action="store_true")
    run.set_defaults(handler=command_run)

    status = subparsers.add_parser("status")
    status.add_argument("--output", type=Path, required=True)
    status.set_defaults(handler=command_status)

    merge = subparsers.add_parser("merge")
    merge.add_argument("--output", type=Path, required=True)
    merge.set_defaults(handler=command_merge)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return int(args.handler(args))
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"m11_search.py: error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
