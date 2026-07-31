#!/usr/bin/env python3
"""Render a deterministic, read-only report from M11-B v2.1 manifests.

This tool never invokes the solver, integrates a trajectory, changes a stored
status, or re-evaluates an acceptance gate.  It only formats values already
recorded by ABTSM11SearchCLI.  Candidate ordering uses the stored soft-score
total, with deterministic tie-breaks.
"""

from __future__ import annotations

import argparse
import copy
import csv
import hashlib
import html
import io
import json
import math
from pathlib import Path, PurePosixPath
import re
import sys
from typing import Any, Iterable, Sequence


REPORT_SCHEMA = "abts.m11b21.particle_beam_read_only_report.v1"
REPORT_TOOL_VERSION = 2
SERIES = (
    ("S1", "#0072B2", (0, 114, 178, 52)),
    ("S2", "#E69F00", (230, 159, 0, 62)),
    ("S3", "#009E73", (0, 158, 115, 72)),
    ("S4", "#CC79A7", (204, 121, 167, 82)),
)
SVG_BACKGROUND = "#F7F9FC"
SVG_FOREGROUND = "#172033"
SVG_MUTED = "#596579"
SVG_GRID = "#D9DFE9"
SVG_BORDER = "#8A96A8"
ZERO_HASH64 = "0x0000000000000000"
HASH64_PATTERN = re.compile(r"^0x[0-9a-f]{16}$")
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
SELECTED_MANIFEST_HASH_SCHEMA = "fnv1a64-exact-manifest-bytes-v1"


def atomic_write_text(path: Path, value: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(value, encoding="utf-8", newline="\n")
    temporary.replace(path)


def atomic_write_bytes(path: Path, value: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(value)
    temporary.replace(path)


def canonical_json(value: Any) -> str:
    return json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    )


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"InvalidJson:{path}:{error}") from error
    if not isinstance(value, dict):
        raise RuntimeError(f"JsonRootMustBeObject:{path}")
    return value


def repository_root() -> Path:
    return Path(__file__).resolve().parents[3]


def display_path(path: Path, root: Path) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(root.resolve()).as_posix()
    except ValueError:
        return resolved.as_posix()


def require_mapping(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise RuntimeError(f"MissingObject:{label}")
    return value


def require_list(value: Any, label: str) -> list[Any]:
    if not isinstance(value, list):
        raise RuntimeError(f"MissingArray:{label}")
    return value


def as_float(value: Any, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise RuntimeError(f"MissingNumber:{label}")
    result = float(value)
    if not math.isfinite(result):
        raise RuntimeError(f"NonFiniteNumber:{label}")
    return result


def as_int(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise RuntimeError(f"MissingInteger:{label}")
    return value


def as_string(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value:
        raise RuntimeError(f"MissingString:{label}")
    return value


def as_bool(value: Any, label: str) -> bool:
    if not isinstance(value, bool):
        raise RuntimeError(f"MissingBoolean:{label}")
    return value


def as_hash64(value: Any, label: str) -> str:
    result = as_string(value, label)
    if HASH64_PATTERN.fullmatch(result) is None:
        raise RuntimeError(f"InvalidHash64:{label}")
    return result


def as_sha256(value: Any, label: str) -> str:
    result = as_string(value, label)
    if SHA256_PATTERN.fullmatch(result) is None:
        raise RuntimeError(f"InvalidSha256:{label}")
    return result


def require_equal(actual: Any, expected: Any, label: str) -> None:
    if canonical_json(actual) != canonical_json(expected):
        raise RuntimeError(f"ManifestLinkMismatch:{label}")


def sha256_canonical(value: Any) -> str:
    return hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def fnv1a64_bytes(value: bytes) -> str:
    result = 14695981039346656037
    for byte in value:
        result ^= byte
        result = (result * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"0x{result:016x}"


def selected_manifest_relative_path(value: Any, label: str) -> str:
    text = as_string(value, label)
    if "\\" in text:
        raise RuntimeError(f"ManifestPathMustUseForwardSlashes:{label}")
    path = PurePosixPath(text)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        raise RuntimeError(f"UnsafeManifestRelativePath:{label}")
    if len(path.parts) != 2 or path.parts[0] != "candidates":
        raise RuntimeError(f"ManifestPathOutsideCandidateDirectory:{label}")
    return path.as_posix()


def non_seed_contract_identity(
    descriptor: dict[str, Any], construction_contract: dict[str, Any]
) -> dict[str, Any]:
    contract = copy.deepcopy(construction_contract)
    for field in ("constructionSeed", "explorationSeed", "holdoutSeed"):
        if field not in contract:
            raise RuntimeError(f"MissingInteger:constructionContract.{field}")
        del contract[field]
    return {
        "descriptorSchema": as_string(
            descriptor.get("schema"), "contractDescriptor.schema"
        ),
        "authority": as_string(
            descriptor.get("authority"), "contractDescriptor.authority"
        ),
        "algorithm": as_string(
            descriptor.get("algorithm"), "contractDescriptor.algorithm"
        ),
        "constructionContractWithoutRunSeeds": contract,
    }


def input_set_map(value: Any, label: str) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for index, item in enumerate(require_list(value, label)):
        mapping = require_mapping(item, f"{label}[{index}]")
        set_name = as_string(mapping.get("set"), f"{label}[{index}].set")
        if set_name in result:
            raise RuntimeError(f"DuplicateInputSet:{label}:{set_name}")
        result[set_name] = mapping
    for set_name, _color, _rgba in SERIES:
        if set_name not in result:
            raise RuntimeError(f"MissingInputSet:{label}:{set_name}")
    return result


def hull_points(value: Any, label: str) -> list[list[float]]:
    result: list[list[float]] = []
    for index, point in enumerate(require_list(value, label)):
        pair = require_list(point, f"{label}[{index}]")
        if len(pair) != 2:
            raise RuntimeError(f"HullPointMustHaveTwoCoordinates:{label}[{index}]")
        result.append(
            [
                as_float(pair[0], f"{label}[{index}][0]"),
                as_float(pair[1], f"{label}[{index}][1]"),
            ]
        )
    return result


def validate_summary(
    path: Path,
    candidate_paths: Sequence[Path],
    repo_root: Path,
) -> tuple[dict[str, Any], dict[str, Any]]:
    value = load_json(path)
    if value.get("schema") != "abts.m11b21.particle_beam_summary.v1":
        raise RuntimeError(f"UnsupportedSummarySchema:{path}:{value.get('schema')}")
    if not as_bool(value.get("passed"), "summary.passed"):
        raise RuntimeError(f"SummaryNotPassed:{path}")

    descriptor = require_mapping(
        value.get("contractDescriptor"), "summary.contractDescriptor"
    )
    if descriptor.get("schema") != "abts.m11b21.particle_beam.contract_descriptor.v1":
        raise RuntimeError(
            f"UnsupportedContractDescriptorSchema:{path}:{descriptor.get('schema')}"
        )
    if descriptor.get("authority") != "ABTSM11SearchCLI":
        raise RuntimeError(f"UnexpectedContractAuthority:{path}")
    if descriptor.get("algorithm") != "conditional-particle-beam-v4":
        raise RuntimeError(f"UnexpectedContractAlgorithm:{path}")
    contract_hash = as_hash64(value.get("contractHash"), "summary.contractHash")
    descriptor_contract_hash = as_hash64(
        descriptor.get("contractHash"), "summary.contractDescriptor.contractHash"
    )
    if contract_hash != descriptor_contract_hash:
        raise RuntimeError(f"SummaryContractHashMismatch:{path}")

    tool_identity = require_mapping(value.get("toolIdentity"), "summary.toolIdentity")
    search_source_hash = as_sha256(
        tool_identity.get("searchSourceHashSha256"),
        "summary.toolIdentity.searchSourceHashSha256",
    )
    production_source_hash = as_sha256(
        tool_identity.get("productionCoreSourceHashSha256"),
        "summary.toolIdentity.productionCoreSourceHashSha256",
    )
    if (
        as_sha256(
            descriptor.get("searchSourceHashSha256"),
            "summary.contractDescriptor.searchSourceHashSha256",
        )
        != search_source_hash
    ):
        raise RuntimeError(f"SummarySearchSourceIdentityMismatch:{path}")
    if (
        as_sha256(
            descriptor.get("productionCoreSourceHashSha256"),
            "summary.contractDescriptor.productionCoreSourceHashSha256",
        )
        != production_source_hash
    ):
        raise RuntimeError(f"SummaryProductionSourceIdentityMismatch:{path}")

    construction_contract = require_mapping(
        descriptor.get("constructionContract"),
        "summary.contractDescriptor.constructionContract",
    )
    evaluation_contract = require_mapping(
        construction_contract.get("evaluationContract"),
        "summary.contractDescriptor.constructionContract.evaluationContract",
    )
    construction_aggregate_hash = as_hash64(
        value.get("constructionAggregateHash"), "summary.constructionAggregateHash"
    )
    candidate_aggregate_hash = as_hash64(
        value.get("candidateAggregateHash"), "summary.candidateAggregateHash"
    )

    evaluations = require_list(value.get("evaluations"), "summary.evaluations")
    audited_count = as_int(value.get("auditedCount"), "summary.auditedCount")
    accepted_count = as_int(value.get("acceptedCount"), "summary.acceptedCount")
    selected_count = as_int(
        value.get("selectedCandidateCount"), "summary.selectedCandidateCount"
    )
    diagnostic = as_string(value.get("diagnostic"), "summary.diagnostic")
    if audited_count != len(evaluations):
        raise RuntimeError(
            f"SummaryAuditedCountMismatch:{path}:{audited_count}:{len(evaluations)}"
        )
    if selected_count != len(candidate_paths):
        raise RuntimeError(
            f"SummaryCandidateFileCountMismatch:{path}:{selected_count}:"
            f"{len(candidate_paths)}"
        )
    if selected_count > accepted_count:
        raise RuntimeError(f"SummarySelectedExceedsAccepted:{path}")
    if selected_count > 0 and diagnostic not in (
        "Completed",
        "CompletedInsufficientCandidates",
    ):
        raise RuntimeError(
            f"SelectedCandidateDiagnosticMismatch:{path}:{diagnostic}"
        )

    if (
        as_int(
            value.get("selectedTopCandidatesSchemaVersion"),
            "summary.selectedTopCandidatesSchemaVersion",
        )
        != 1
    ):
        raise RuntimeError(f"UnsupportedSelectedTopCandidatesSchema:{path}")
    if value.get("selectedTopCandidatesAuthority") != "ordered-final-selection":
        raise RuntimeError(f"SelectedTopCandidatesAuthorityMismatch:{path}")
    if (
        value.get("selectedManifestRecordHashSchema")
        != SELECTED_MANIFEST_HASH_SCHEMA
    ):
        raise RuntimeError(f"SelectedManifestRecordHashSchemaMismatch:{path}")
    selected_values = require_list(
        value.get("selectedTopCandidates"), "summary.selectedTopCandidates"
    )
    if len(selected_values) != selected_count:
        raise RuntimeError(
            f"SelectedTopCandidatesCountMismatch:{path}:{selected_count}:"
            f"{len(selected_values)}"
        )
    root = path.parent.resolve()
    actual_relative_paths: list[str] = []
    for candidate_path in candidate_paths:
        resolved_candidate = candidate_path.resolve()
        try:
            relative_candidate = resolved_candidate.relative_to(root).as_posix()
        except ValueError as error:
            raise RuntimeError(
                f"CandidatePathEscapesSearchRoot:{candidate_path}"
            ) from error
        actual_relative_paths.append(relative_candidate)

    selected_records: list[dict[str, Any]] = []
    selected_by_relative_path: dict[str, dict[str, Any]] = {}
    selected_by_source: dict[str, dict[str, Any]] = {}
    for index, item in enumerate(selected_values):
        record = require_mapping(item, f"summary.selectedTopCandidates[{index}]")
        rank = as_int(
            record.get("rank"), f"summary.selectedTopCandidates[{index}].rank"
        )
        if rank != index + 1:
            raise RuntimeError(
                f"SelectedTopCandidateRankOrderMismatch:{path}:{rank}:{index + 1}"
            )
        source_hash = as_hash64(
            record.get("candidateSourceHash"),
            f"summary.selectedTopCandidates[{index}].candidateSourceHash",
        )
        relative_path = selected_manifest_relative_path(
            record.get("manifestRelativePath"),
            f"summary.selectedTopCandidates[{index}].manifestRelativePath",
        )
        expected_relative_path = (
            f"candidates/candidate_rank_{rank}_"
            f"{source_hash.removeprefix('0x')}.json"
        )
        if relative_path != expected_relative_path:
            raise RuntimeError(
                f"SelectedTopCandidatePathLinkMismatch:{path}:"
                f"{relative_path}:{expected_relative_path}"
            )
        if relative_path in selected_by_relative_path:
            raise RuntimeError(
                f"DuplicateSelectedManifestPath:{path}:{relative_path}"
            )
        if source_hash in selected_by_source:
            raise RuntimeError(
                f"DuplicateSelectedCandidateSource:{path}:{source_hash}"
            )
        manifest_byte_count = as_int(
            record.get("manifestByteCount"),
            f"summary.selectedTopCandidates[{index}].manifestByteCount",
        )
        if manifest_byte_count <= 0:
            raise RuntimeError(f"SelectedManifestByteCountInvalid:{path}:{rank}")
        manifest_record_hash = as_hash64(
            record.get("manifestRecordHash"),
            f"summary.selectedTopCandidates[{index}].manifestRecordHash",
        )
        selected_record = {
            "rank": rank,
            "candidateSourceHash": source_hash,
            "nominalRequestHash": as_hash64(
                record.get("nominalRequestHash"),
                f"summary.selectedTopCandidates[{index}].nominalRequestHash",
            ),
            "nominalResultHash": as_hash64(
                record.get("nominalResultHash"),
                f"summary.selectedTopCandidates[{index}].nominalResultHash",
            ),
            "scoreHash": as_hash64(
                record.get("scoreHash"),
                f"summary.selectedTopCandidates[{index}].scoreHash",
            ),
            "constructionHash": as_hash64(
                record.get("constructionHash"),
                f"summary.selectedTopCandidates[{index}].constructionHash",
            ),
            "manifestRelativePath": relative_path,
            "manifestByteCount": manifest_byte_count,
            "manifestRecordHash": manifest_record_hash,
        }
        manifest_path = root / PurePosixPath(relative_path)
        if not manifest_path.is_file():
            raise RuntimeError(f"SelectedManifestMissing:{manifest_path}")
        manifest_bytes = manifest_path.read_bytes()
        if len(manifest_bytes) != manifest_byte_count:
            raise RuntimeError(
                f"SelectedManifestByteCountMismatch:{manifest_path}:"
                f"{manifest_byte_count}:{len(manifest_bytes)}"
            )
        if fnv1a64_bytes(manifest_bytes) != manifest_record_hash:
            raise RuntimeError(f"SelectedManifestRecordHashMismatch:{manifest_path}")
        selected_records.append(selected_record)
        selected_by_relative_path[relative_path] = selected_record
        selected_by_source[source_hash] = selected_record
    expected_relative_paths = [
        record["manifestRelativePath"] for record in selected_records
    ]
    if (
        len(set(actual_relative_paths)) != len(actual_relative_paths)
        or set(actual_relative_paths) != set(expected_relative_paths)
    ):
        raise RuntimeError(
            f"CandidateDirectoryDoesNotMatchOrderedSelection:{path}"
        )

    accepted_by_source: dict[str, dict[str, Any]] = {}
    computed_accepted_count = 0
    for index, item in enumerate(evaluations):
        evaluation = require_mapping(item, f"summary.evaluations[{index}]")
        status = as_string(
            evaluation.get("status"), f"summary.evaluations[{index}].status"
        )
        if status != "Accepted":
            continue
        computed_accepted_count += 1
        source_hash = as_hash64(
            evaluation.get("candidateSourceHash"),
            f"summary.evaluations[{index}].candidateSourceHash",
        )
        if source_hash in accepted_by_source:
            raise RuntimeError(f"DuplicateAcceptedCandidateSource:{path}:{source_hash}")
        accepted_by_source[source_hash] = {
            "status": status,
            "candidateSourceHash": source_hash,
            "constructionHash": as_hash64(
                evaluation.get("constructionHash"),
                f"summary.evaluations[{index}].constructionHash",
            ),
            "scoreHash": as_hash64(
                evaluation.get("scoreHash"),
                f"summary.evaluations[{index}].scoreHash",
            ),
        }
    if computed_accepted_count != accepted_count:
        raise RuntimeError(
            f"SummaryAcceptedCountMismatch:{path}:{accepted_count}:"
            f"{computed_accepted_count}"
        )
    for selected_record in selected_records:
        accepted = accepted_by_source.get(selected_record["candidateSourceHash"])
        if accepted is None:
            raise RuntimeError(
                f"SelectedCandidateMissingAcceptedEvaluation:{path}:"
                f"{selected_record['candidateSourceHash']}"
            )
        if accepted["constructionHash"] != selected_record["constructionHash"]:
            raise RuntimeError(
                f"SelectedCandidateConstructionHashMismatch:{path}:"
                f"{selected_record['rank']}"
            )
        if accepted["scoreHash"] != selected_record["scoreHash"]:
            raise RuntimeError(
                f"SelectedCandidateScoreHashMismatch:{path}:"
                f"{selected_record['rank']}"
            )

    baseline = require_mapping(value.get("v3Baseline"), "summary.v3Baseline")
    construction_metrics = require_mapping(
        value.get("constructionMetrics"), "summary.constructionMetrics"
    )
    solver_invocation_count = as_int(
        value.get("solverInvocationCount"), "summary.solverInvocationCount"
    )
    if solver_invocation_count < 0:
        raise RuntimeError(f"NegativeSolverInvocationCount:{path}")
    construction_ledger_total = as_int(
        construction_metrics.get("initialParticleSolves"),
        "summary.constructionMetrics.initialParticleSolves",
    )
    if construction_ledger_total < 0:
        raise RuntimeError(f"NegativeConstructionLedgerEntry:{path}")
    for field_name in (
        "nominalProposalSolveCounts",
        "coarseParticleSolveCounts",
        "refinementParticleSolveCounts",
    ):
        entries = require_list(
            construction_metrics.get(field_name),
            f"summary.constructionMetrics.{field_name}",
        )
        if len(entries) != 3:
            raise RuntimeError(
                f"ConstructionLedgerStageCountMismatch:{path}:{field_name}:"
                f"{len(entries)}"
            )
        for index, entry_value in enumerate(entries):
            entry = as_int(
                entry_value,
                f"summary.constructionMetrics.{field_name}[{index}]",
            )
            if entry < 0:
                raise RuntimeError(
                    f"NegativeConstructionLedgerEntry:{path}:{field_name}:{index}"
                )
            construction_ledger_total += entry
    for field_name in ("holdoutSolveCount", "finalAuditSolveCount"):
        entry = as_int(
            construction_metrics.get(field_name),
            f"summary.constructionMetrics.{field_name}",
        )
        if entry < 0:
            raise RuntimeError(
                f"NegativeConstructionLedgerEntry:{path}:{field_name}"
            )
        construction_ledger_total += entry
    if solver_invocation_count != construction_ledger_total:
        raise RuntimeError(
            f"SolverInvocationLedgerMismatch:{path}:{solver_invocation_count}:"
            f"{construction_ledger_total}"
        )
    accepted_per_million = as_float(
        value.get("acceptedPerMillionSolverInvocations"),
        "summary.acceptedPerMillionSolverInvocations",
    )
    expected_accepted_per_million = (
        accepted_count * 1000000.0 / solver_invocation_count
        if solver_invocation_count > 0
        else 0.0
    )
    if not math.isclose(
        accepted_per_million,
        expected_accepted_per_million,
        rel_tol=1.0e-12,
        abs_tol=1.0e-12,
    ):
        raise RuntimeError(
            f"AcceptedPerMillionMismatch:{path}:{accepted_per_million}:"
            f"{expected_accepted_per_million}"
        )
    baseline_per_million = as_float(
        baseline.get("acceptedPerMillionSolverInvocations"),
        "summary.v3Baseline.acceptedPerMillionSolverInvocations",
    )
    identity = non_seed_contract_identity(descriptor, construction_contract)
    identity_hash = sha256_canonical(identity)
    summary_report = {
        "path": display_path(path, repo_root),
        "passed": True,
        "diagnostic": diagnostic,
        "contractHash": contract_hash,
        "nonSeedContractIdentitySha256": identity_hash,
        "searchSourceHashSha256": search_source_hash,
        "productionCoreSourceHashSha256": production_source_hash,
        "auditedCount": audited_count,
        "acceptedCount": accepted_count,
        "selectedCandidateCount": selected_count,
        "selectedTopCandidates": selected_records,
        "validatedCandidateFileCount": len(candidate_paths),
        "solverInvocationCount": solver_invocation_count,
        "wallClockSeconds": as_float(
            value.get("wallClockSeconds"), "summary.wallClockSeconds"
        ),
        "acceptedPerMillionSolverInvocations": accepted_per_million,
        "v3BaselineAcceptedPerMillionSolverInvocations": baseline_per_million,
        "efficiencyMultiplierVsV3": (
            accepted_per_million / baseline_per_million
            if baseline_per_million > 0.0
            else None
        ),
    }
    context = {
        "path": path,
        "contractHash": contract_hash,
        "constructionAggregateHash": construction_aggregate_hash,
        "candidateAggregateHash": candidate_aggregate_hash,
        "selectedCandidateCount": selected_count,
        "selectedRecords": selected_records,
        "selectedByRelativePath": selected_by_relative_path,
        "selectedBySource": selected_by_source,
        "searchSourceHashSha256": search_source_hash,
        "productionCoreSourceHashSha256": production_source_hash,
        "constructionContract": construction_contract,
        "evaluationContract": evaluation_contract,
        "acceptedBySource": accepted_by_source,
        "nonSeedContractIdentity": identity,
        "nonSeedContractIdentitySha256": identity_hash,
        "evaluationDescriptorIdentity": None,
    }
    return summary_report, context


def read_candidate(
    path: Path,
    source_root: Path,
    repo_root: Path,
    summary_context: dict[str, Any],
) -> dict[str, Any]:
    outer = load_json(path)
    try:
        candidate_relative_path = path.resolve().relative_to(
            source_root.resolve()
        ).as_posix()
    except ValueError as error:
        raise RuntimeError(f"CandidatePathEscapesSearchRoot:{path}") from error
    selected_record = summary_context["selectedByRelativePath"].get(
        candidate_relative_path
    )
    if selected_record is None:
        raise RuntimeError(f"CandidateNotInOrderedSelection:{path}")
    if outer.get("schema") != "abts.m11b21.particle_beam_candidate.v1":
        raise RuntimeError(f"UnsupportedCandidateSchema:{path}:{outer.get('schema')}")
    if outer.get("status") != "Candidate / NOT CERTIFIED":
        raise RuntimeError(f"CandidateOuterStatusMismatch:{path}:{outer.get('status')}")
    if (
        as_int(
            outer.get("particleBeamManifestVersion"), "particleBeamManifestVersion"
        )
        != 1
    ):
        raise RuntimeError(f"CandidateParticleManifestVersionMismatch:{path}")
    if (
        as_hash64(
            outer.get("constructionContractHash"), "candidate.constructionContractHash"
        )
        != summary_context["contractHash"]
    ):
        raise RuntimeError(f"CandidateConstructionContractHashMismatch:{path}")
    require_equal(
        require_mapping(
            outer.get("constructionContract"), "candidate.constructionContract"
        ),
        summary_context["constructionContract"],
        f"candidate.constructionContract:{path}",
    )
    evaluated = require_mapping(outer.get("evaluatedCandidate"), "evaluatedCandidate")
    if evaluated.get("schema") != "abts.m11b21.candidate.v3":
        raise RuntimeError(
            f"UnsupportedEvaluatedCandidateSchema:{path}:{evaluated.get('schema')}"
        )
    if evaluated.get("status") != "Candidate":
        raise RuntimeError(f"EvaluatedCandidateStatusMismatch:{path}")
    if (
        as_int(
            evaluated.get("searchContractVersion"),
            "evaluatedCandidate.searchContractVersion",
        )
        != as_int(
            summary_context["evaluationContract"].get("contractVersion"),
            "summary.evaluationContract.contractVersion",
        )
    ):
        raise RuntimeError(f"EvaluatedCandidateContractVersionMismatch:{path}")
    if (
        as_int(
            evaluated.get("searchAlgorithmVersion"),
            "evaluatedCandidate.searchAlgorithmVersion",
        )
        != as_int(
            summary_context["evaluationContract"].get("algorithmVersion"),
            "summary.evaluationContract.algorithmVersion",
        )
    ):
        raise RuntimeError(f"EvaluatedCandidateAlgorithmVersionMismatch:{path}")
    if (
        as_int(outer.get("particleBeamContractVersion"), "particleBeamContractVersion")
        != as_int(
            summary_context["constructionContract"].get("contractVersion"),
            "summary.constructionContract.contractVersion",
        )
    ):
        raise RuntimeError(f"CandidateParticleContractVersionMismatch:{path}")
    if (
        as_int(
            outer.get("particleBeamAlgorithmVersion"), "particleBeamAlgorithmVersion"
        )
        != as_int(
            summary_context["constructionContract"].get("algorithmVersion"),
            "summary.constructionContract.algorithmVersion",
        )
    ):
        raise RuntimeError(f"CandidateParticleAlgorithmVersionMismatch:{path}")

    evaluated_tool_identity = require_mapping(
        evaluated.get("toolIdentity"), "evaluatedCandidate.toolIdentity"
    )
    if (
        as_sha256(
            evaluated_tool_identity.get("searchSourceHashSha256"),
            "evaluatedCandidate.toolIdentity.searchSourceHashSha256",
        )
        != summary_context["searchSourceHashSha256"]
    ):
        raise RuntimeError(f"CandidateSearchSourceIdentityMismatch:{path}")
    if (
        as_sha256(
            evaluated_tool_identity.get("productionCoreSourceHashSha256"),
            "evaluatedCandidate.toolIdentity.productionCoreSourceHashSha256",
        )
        != summary_context["productionCoreSourceHashSha256"]
    ):
        raise RuntimeError(f"CandidateProductionSourceIdentityMismatch:{path}")

    evaluated_descriptor = require_mapping(
        evaluated.get("contractDescriptor"),
        "evaluatedCandidate.contractDescriptor",
    )
    if (
        as_sha256(
            evaluated_descriptor.get("searchSourceHashSha256"),
            "evaluatedCandidate.contractDescriptor.searchSourceHashSha256",
        )
        != summary_context["searchSourceHashSha256"]
    ):
        raise RuntimeError(f"CandidateDescriptorSearchSourceMismatch:{path}")
    descriptor_search_contract = require_mapping(
        evaluated_descriptor.get("searchContract"),
        "evaluatedCandidate.contractDescriptor.searchContract",
    )
    require_equal(
        descriptor_search_contract,
        summary_context["evaluationContract"],
        f"candidate.evaluationContract:{path}",
    )
    descriptor_identity = {
        "schema": as_string(
            evaluated_descriptor.get("schema"),
            "evaluatedCandidate.contractDescriptor.schema",
        ),
        "authority": as_string(
            evaluated_descriptor.get("authority"),
            "evaluatedCandidate.contractDescriptor.authority",
        ),
        "contractHash": as_hash64(
            evaluated_descriptor.get("contractHash"),
            "evaluatedCandidate.contractDescriptor.contractHash",
        ),
        "searchSourceHashSha256": summary_context["searchSourceHashSha256"],
        "samplingSemantics": require_mapping(
            evaluated_descriptor.get("samplingSemantics"),
            "evaluatedCandidate.contractDescriptor.samplingSemantics",
        ),
        "searchContract": descriptor_search_contract,
    }
    previous_descriptor_identity = summary_context["evaluationDescriptorIdentity"]
    if previous_descriptor_identity is None:
        summary_context["evaluationDescriptorIdentity"] = descriptor_identity
    else:
        require_equal(
            descriptor_identity,
            previous_descriptor_identity,
            f"candidate.evaluationDescriptorIdentity:{path}",
        )

    certification = require_mapping(
        evaluated.get("certification"), "evaluatedCandidate.certification"
    )
    if certification.get("status") != "not-certified":
        raise RuntimeError(f"CandidateCertificationStatusMismatch:{path}")
    if (
        as_hash64(
            certification.get("certificationHash"),
            "evaluatedCandidate.certification.certificationHash",
        )
        != ZERO_HASH64
    ):
        raise RuntimeError(f"CandidateCertificationHashNonZero:{path}")
    if (
        as_hash64(
            certification.get("certifiedBundleHash"),
            "evaluatedCandidate.certification.certifiedBundleHash",
        )
        != ZERO_HASH64
    ):
        raise RuntimeError(f"CandidateCertifiedBundleHashNonZero:{path}")

    candidate_hash = as_hash64(
        evaluated.get("candidateSourceHash"),
        "evaluatedCandidate.candidateSourceHash",
    )
    nominal_request_hash = as_hash64(
        evaluated.get("nominalRequestHash"),
        "evaluatedCandidate.nominalRequestHash",
    )
    nominal_result_hash = as_hash64(
        evaluated.get("nominalResultHash"),
        "evaluatedCandidate.nominalResultHash",
    )
    stored_rank = as_int(outer.get("rank"), "candidate.rank")
    expected_candidate_id = f"m11b21-{candidate_hash.removeprefix('0x')}"
    if (
        as_string(evaluated.get("candidateId"), "evaluatedCandidate.candidateId")
        != expected_candidate_id
    ):
        raise RuntimeError(f"CandidateIdLinkMismatch:{path}")
    accepted_evaluation = summary_context["acceptedBySource"].get(candidate_hash)
    if accepted_evaluation is None:
        raise RuntimeError(f"CandidateMissingAcceptedEvaluation:{path}:{candidate_hash}")
    if accepted_evaluation["status"] != "Accepted":
        raise RuntimeError(f"CandidateEvaluationStatusMismatch:{path}")
    construction_hash = as_hash64(
        outer.get("constructionHash"), "candidate.constructionHash"
    )
    score_hash = as_hash64(evaluated.get("scoreHash"), "evaluatedCandidate.scoreHash")
    if construction_hash != accepted_evaluation["constructionHash"]:
        raise RuntimeError(f"CandidateEvaluationConstructionHashMismatch:{path}")
    if score_hash != accepted_evaluation["scoreHash"]:
        raise RuntimeError(f"CandidateEvaluationScoreHashMismatch:{path}")
    if selected_record["rank"] != stored_rank:
        raise RuntimeError(f"CandidateOrderedSelectionRankMismatch:{path}")
    if selected_record["candidateSourceHash"] != candidate_hash:
        raise RuntimeError(f"CandidateOrderedSelectionSourceHashMismatch:{path}")
    if selected_record["constructionHash"] != construction_hash:
        raise RuntimeError(
            f"CandidateOrderedSelectionConstructionHashMismatch:{path}"
        )
    if selected_record["nominalRequestHash"] != nominal_request_hash:
        raise RuntimeError(f"CandidateOrderedSelectionRequestHashMismatch:{path}")
    if selected_record["nominalResultHash"] != nominal_result_hash:
        raise RuntimeError(f"CandidateOrderedSelectionResultHashMismatch:{path}")
    if selected_record["scoreHash"] != score_hash:
        raise RuntimeError(f"CandidateOrderedSelectionScoreHashMismatch:{path}")

    selection = require_mapping(
        evaluated.get("selection"), "evaluatedCandidate.selection"
    )
    if as_int(selection.get("rank"), "selection.rank") != stored_rank:
        raise RuntimeError(f"CandidateSelectionRankMismatch:{path}")
    if selection.get("scope") != "particle-beam-v4":
        raise RuntimeError(f"CandidateSelectionScopeMismatch:{path}")
    if (
        as_int(selection.get("workItems"), "selection.workItems")
        != as_int(
            summary_context["constructionContract"].get("rootParameterCount"),
            "summary.constructionContract.rootParameterCount",
        )
    ):
        raise RuntimeError(f"CandidateSelectionWorkItemsMismatch:{path}")
    if (
        as_int(selection.get("shardIndex"), "selection.shardIndex") != 0
        or as_int(selection.get("shardCount"), "selection.shardCount") != 1
    ):
        raise RuntimeError(f"CandidateSelectionShardMismatch:{path}")
    if (
        as_int(selection.get("selectedCount"), "selection.selectedCount")
        != summary_context["selectedCandidateCount"]
    ):
        raise RuntimeError(f"CandidateSelectionCountMismatch:{path}")
    if (
        as_hash64(
            selection.get("evaluationAggregateHash"),
            "selection.evaluationAggregateHash",
        )
        != summary_context["constructionAggregateHash"]
    ):
        raise RuntimeError(f"CandidateSelectionEvaluationAggregateMismatch:{path}")
    if (
        as_hash64(
            selection.get("candidateAggregateHash"),
            "selection.candidateAggregateHash",
        )
        != summary_context["candidateAggregateHash"]
    ):
        raise RuntimeError(f"CandidateSelectionCandidateAggregateMismatch:{path}")
    expected_filename = (
        f"candidate_rank_{stored_rank}_{candidate_hash.removeprefix('0x')}.json"
    )
    if path.name != expected_filename:
        raise RuntimeError(
            f"CandidateFilenameLinkMismatch:{path.name}:{expected_filename}"
        )

    metrics = require_mapping(evaluated.get("metrics"), "evaluatedCandidate.metrics")
    layout = require_mapping(evaluated.get("layout"), "evaluatedCandidate.layout")
    launch = require_mapping(layout.get("launch"), "evaluatedCandidate.layout.launch")
    nominal = require_mapping(
        layout.get("nominalInput"), "evaluatedCandidate.layout.nominalInput"
    )
    domain = require_mapping(
        metrics.get("candidateDomainAnalysis"),
        "evaluatedCandidate.metrics.candidateDomainAnalysis",
    )
    screen_sets = input_set_map(
        domain.get("inputSets"),
        "evaluatedCandidate.metrics.candidateDomainAnalysis.inputSets",
    )
    holdout = require_mapping(outer.get("independentHoldout"), "independentHoldout")
    holdout_sets = input_set_map(
        holdout.get("inputSets"), "independentHoldout.inputSets"
    )
    assists: list[dict[str, Any]] = []
    for index, item in enumerate(require_list(metrics.get("assists"), "metrics.assists")):
        assist = require_mapping(item, f"metrics.assists[{index}]")
        assists.append(
            {
                "assistIndex": as_int(
                    assist.get("assistIndex"), f"metrics.assists[{index}].assistIndex"
                ),
                "enterTimeSeconds": as_float(
                    assist.get("enterTimeSeconds"),
                    f"metrics.assists[{index}].enterTimeSeconds",
                ),
                "closestTimeSeconds": as_float(
                    assist.get("closestTimeSeconds"),
                    f"metrics.assists[{index}].closestTimeSeconds",
                ),
                "exitTimeSeconds": as_float(
                    assist.get("exitTimeSeconds"),
                    f"metrics.assists[{index}].exitTimeSeconds",
                ),
                "coastBeforeEnterSeconds": as_float(
                    assist.get("coastBeforeEnterSeconds"),
                    f"metrics.assists[{index}].coastBeforeEnterSeconds",
                ),
                "influenceDurationSeconds": as_float(
                    assist.get("influenceDurationSeconds"),
                    f"metrics.assists[{index}].influenceDurationSeconds",
                ),
                "actualDeflectionRadians": as_float(
                    assist.get("actualDeflectionRadians"),
                    f"metrics.assists[{index}].actualDeflectionRadians",
                ),
                "actualDeflectionDegrees": math.degrees(
                    as_float(
                        assist.get("actualDeflectionRadians"),
                        f"metrics.assists[{index}].actualDeflectionRadians",
                    )
                ),
                "signedLateralTurnRadians": as_float(
                    assist.get("signedLateralTurnRadians"),
                    f"metrics.assists[{index}].signedLateralTurnRadians",
                ),
                "lateralTurnAxisProjection": as_float(
                    assist.get("lateralTurnAxisProjection"),
                    f"metrics.assists[{index}].lateralTurnAxisProjection",
                ),
                "entrySpeedCMPerSec": as_float(
                    assist.get("entrySpeedCMPerSec"),
                    f"metrics.assists[{index}].entrySpeedCMPerSec",
                ),
                "exitSpeedCMPerSec": as_float(
                    assist.get("exitSpeedCMPerSec"),
                    f"metrics.assists[{index}].exitSpeedCMPerSec",
                ),
            }
        )
    if len(assists) != 3:
        raise RuntimeError(f"ExpectedThreeAssists:{path}:{len(assists)}")

    screen: dict[str, dict[str, Any]] = {}
    holdout_report: dict[str, dict[str, Any]] = {}
    for set_name, _color, _rgba in SERIES:
        item = screen_sets[set_name]
        screen[set_name] = {
            "count": as_int(item.get("screenAimCount"), f"{set_name}.screenAimCount"),
            "retentionRatio": as_float(
                item.get("screenAimRetentionRatio"),
                f"{set_name}.screenAimRetentionRatio",
            ),
            "hullEvidencePointCount": as_int(
                item.get("screenAimHullEvidencePointCount"),
                f"{set_name}.screenAimHullEvidencePointCount",
            ),
            "hullAreaSquareDegrees": as_float(
                item.get("screenAimHullAreaSquareDegrees"),
                f"{set_name}.screenAimHullAreaSquareDegrees",
            ),
            "hullYawSpanDegrees": as_float(
                item.get("screenAimHullYawSpanDegrees"),
                f"{set_name}.screenAimHullYawSpanDegrees",
            ),
            "hullPitchSpanDegrees": as_float(
                item.get("screenAimHullPitchSpanDegrees"),
                f"{set_name}.screenAimHullPitchSpanDegrees",
            ),
            "hullNormalizedArea": as_float(
                item.get("screenAimHullNormalizedArea"),
                f"{set_name}.screenAimHullNormalizedArea",
            ),
            "hullCompactness": as_float(
                item.get("screenAimHullCompactness"),
                f"{set_name}.screenAimHullCompactness",
            ),
            "hullContainsNominal": as_bool(
                item.get("screenAimHullContainsNominal"),
                f"{set_name}.screenAimHullContainsNominal",
            ),
            "hullCompliant": as_bool(
                item.get("screenAimHullCompliant"),
                f"{set_name}.screenAimHullCompliant",
            ),
            "hullYawPitch": hull_points(
                item.get("screenAimHullYawPitch"),
                f"{set_name}.screenAimHullYawPitch",
            ),
        }
        holdout_item = holdout_sets[set_name]
        holdout_report[set_name] = {
            "count": as_int(
                holdout_item.get("screenAimCount"),
                f"holdout.{set_name}.screenAimCount",
            ),
            "retentionRatio": as_float(
                holdout_item.get("screenAimRetentionRatio"),
                f"holdout.{set_name}.screenAimRetentionRatio",
            ),
            "hullAreaSquareDegrees": as_float(
                holdout_item.get("screenAimHullAreaSquareDegrees"),
                f"holdout.{set_name}.screenAimHullAreaSquareDegrees",
            ),
            "hullCompactness": as_float(
                holdout_item.get("screenAimHullCompactness"),
                f"holdout.{set_name}.screenAimHullCompactness",
            ),
            "hullContainsNominal": as_bool(
                holdout_item.get("screenAimHullContainsNominal"),
                f"holdout.{set_name}.screenAimHullContainsNominal",
            ),
        }

    soft_scores = require_mapping(metrics.get("softScores"), "metrics.softScores")
    physics_evidence_payload = {
        "layout": layout,
        "metrics": metrics,
        "independentHoldout": holdout,
        "nominalRequestHash": nominal_request_hash,
        "nominalResultHash": nominal_result_hash,
    }
    physics_evidence_payload_sha256 = sha256_canonical(physics_evidence_payload)
    return {
        "candidateSourceHash": candidate_hash,
        "candidateId": as_string(evaluated.get("candidateId"), "candidateId"),
        "recordedStatus": as_string(outer.get("status"), "status"),
        "certificationStatus": as_string(
            certification.get("status"), "certification.status"
        ),
        "storedRank": stored_rank,
        "storedSoftScore": as_float(soft_scores.get("total"), "softScores.total"),
        "sourceRoot": display_path(source_root, repo_root),
        "manifestPath": display_path(path, repo_root),
        "selectedManifestRelativePath": candidate_relative_path,
        "selectedManifestRecordHash": selected_record["manifestRecordHash"],
        "physicsEvidencePayloadSha256": physics_evidence_payload_sha256,
        "constructionSeed": as_int(
            require_mapping(
                outer.get("constructionContract"), "constructionContract"
            ).get("constructionSeed"),
            "constructionContract.constructionSeed",
        ),
        "constructionHash": construction_hash,
        "scoreHash": score_hash,
        "launchDomain": {
            "minimumYawDegrees": as_float(
                launch.get("minimumYawDegrees"), "launch.minimumYawDegrees"
            ),
            "maximumYawDegrees": as_float(
                launch.get("maximumYawDegrees"), "launch.maximumYawDegrees"
            ),
            "minimumPitchDegrees": as_float(
                launch.get("minimumPitchDegrees"), "launch.minimumPitchDegrees"
            ),
            "maximumPitchDegrees": as_float(
                launch.get("maximumPitchDegrees"), "launch.maximumPitchDegrees"
            ),
        },
        "nominalInput": {
            "yawDegrees": as_float(nominal.get("yawDegrees"), "nominal.yawDegrees"),
            "pitchDegrees": as_float(
                nominal.get("pitchDegrees"), "nominal.pitchDegrees"
            ),
            "power": as_float(nominal.get("power"), "nominal.power"),
        },
        "totalFlightTimeSeconds": as_float(
            metrics.get("totalFlightTimeSeconds"), "metrics.totalFlightTimeSeconds"
        ),
        "finalCoastSeconds": as_float(
            metrics.get("finalCoastSeconds"), "metrics.finalCoastSeconds"
        ),
        "maximumCoastSeconds": as_float(
            metrics.get("maximumCoastSeconds"), "metrics.maximumCoastSeconds"
        ),
        "totalInfluenceDurationSeconds": as_float(
            metrics.get("totalInfluenceDurationSeconds"),
            "metrics.totalInfluenceDurationSeconds",
        ),
        "minimumLayoutTurnRadians": as_float(
            metrics.get("minimumLayoutTurnRadians"),
            "metrics.minimumLayoutTurnRadians",
        ),
        "minimumLayoutTurnDegrees": math.degrees(
            as_float(
                metrics.get("minimumLayoutTurnRadians"),
                "metrics.minimumLayoutTurnRadians",
            )
        ),
        "robustSurvivorCount": as_int(
            metrics.get("robustSurvivorCount"), "metrics.robustSurvivorCount"
        ),
        "alternatingLateralTurnCount": as_int(
            metrics.get("alternatingLateralTurnCount"),
            "metrics.alternatingLateralTurnCount",
        ),
        "assists": assists,
        "screenAim": screen,
        "independentHoldout": {
            "sampleCount": as_int(holdout.get("sampleCount"), "holdout.sampleCount"),
            "sets": holdout_report,
        },
        "occurrences": [],
    }


def collect_candidates(
    roots: Sequence[Path], repo_root: Path
) -> tuple[list[dict[str, Any]], list[dict[str, Any]], dict[str, Any]]:
    summaries: list[dict[str, Any]] = []
    occurrences: list[dict[str, Any]] = []
    validation_roots: list[dict[str, Any]] = []
    contexts: list[dict[str, Any]] = []
    resolved_roots = sorted(
        (path.resolve() for path in roots), key=lambda path: path.as_posix()
    )
    if len(set(resolved_roots)) != len(resolved_roots):
        raise RuntimeError("DuplicateInputRoot")
    for root in resolved_roots:
        summary_path = root / "summary.json"
        if not summary_path.is_file():
            raise RuntimeError(f"MissingSummary:{summary_path}")
        candidate_directory = root / "candidates"
        if not candidate_directory.is_dir():
            raise RuntimeError(f"MissingCandidateDirectory:{candidate_directory}")
        candidate_entries = sorted(
            candidate_directory.iterdir(), key=lambda path: path.as_posix()
        )
        if any(not entry.is_file() for entry in candidate_entries):
            raise RuntimeError(
                f"CandidateDirectoryContainsNonFile:{candidate_directory}"
            )
        candidate_paths = candidate_entries
        summary_report, summary_context = validate_summary(
            summary_path, candidate_paths, repo_root
        )
        summaries.append(summary_report)
        contexts.append(summary_context)
        root_candidates: list[dict[str, Any]] = []
        ordered_candidate_paths = [
            root / PurePosixPath(record["manifestRelativePath"])
            for record in summary_context["selectedRecords"]
        ]
        for candidate_path in ordered_candidate_paths:
            candidate = read_candidate(
                candidate_path, root, repo_root, summary_context
            )
            root_candidates.append(candidate)
            occurrences.append(candidate)
        ranks = sorted(candidate["storedRank"] for candidate in root_candidates)
        expected_ranks = list(range(1, len(root_candidates) + 1))
        if ranks != expected_ranks:
            raise RuntimeError(
                f"CandidateRankSequenceMismatch:{summary_path}:{ranks}:"
                f"{expected_ranks}"
            )
        source_hashes = [
            candidate["candidateSourceHash"] for candidate in root_candidates
        ]
        if len(set(source_hashes)) != len(source_hashes):
            raise RuntimeError(f"DuplicateCandidateSourceWithinRoot:{summary_path}")
        evaluation_descriptor_identity = summary_context[
            "evaluationDescriptorIdentity"
        ]
        if root_candidates and evaluation_descriptor_identity is None:
            raise RuntimeError(
                f"MissingEvaluationDescriptorIdentity:{summary_path}"
            )
        validation_roots.append(
            {
                "summaryPath": display_path(summary_path, repo_root),
                "contractHash": summary_context["contractHash"],
                "nonSeedContractIdentitySha256": summary_context[
                    "nonSeedContractIdentitySha256"
                ],
                "evaluationDescriptorIdentitySha256": (
                    sha256_canonical(evaluation_descriptor_identity)
                    if evaluation_descriptor_identity is not None
                    else None
                ),
                "validatedCandidateCount": len(root_candidates),
                "candidateSourceHashes": sorted(source_hashes),
            }
        )

    if not contexts:
        raise RuntimeError("NoInputRoots")
    cross_root_identity = {
        "searchSourceHashSha256": contexts[0]["searchSourceHashSha256"],
        "productionCoreSourceHashSha256": contexts[0][
            "productionCoreSourceHashSha256"
        ],
        "nonSeedContractIdentity": contexts[0]["nonSeedContractIdentity"],
        "evaluationDescriptorIdentity": contexts[0][
            "evaluationDescriptorIdentity"
        ],
    }
    for context in contexts[1:]:
        if (
            context["searchSourceHashSha256"]
            != cross_root_identity["searchSourceHashSha256"]
        ):
            raise RuntimeError("CrossRootSearchSourceIdentityMismatch")
        if (
            context["productionCoreSourceHashSha256"]
            != cross_root_identity["productionCoreSourceHashSha256"]
        ):
            raise RuntimeError("CrossRootProductionSourceIdentityMismatch")
        require_equal(
            context["nonSeedContractIdentity"],
            cross_root_identity["nonSeedContractIdentity"],
            "crossRoot.nonSeedContractIdentity",
        )
        require_equal(
            context["evaluationDescriptorIdentity"],
            cross_root_identity["evaluationDescriptorIdentity"],
            "crossRoot.evaluationDescriptorIdentity",
        )

    by_hash: dict[str, list[dict[str, Any]]] = {}
    for candidate in occurrences:
        by_hash.setdefault(candidate["candidateSourceHash"], []).append(candidate)

    representatives: list[dict[str, Any]] = []
    for candidate_hash in sorted(by_hash):
        group = by_hash[candidate_hash]
        payload_hashes = {
            candidate["physicsEvidencePayloadSha256"] for candidate in group
        }
        if len(payload_hashes) != 1:
            raise RuntimeError(
                f"DuplicateCandidatePhysicsEvidenceMismatch:{candidate_hash}"
            )
        group.sort(
            key=lambda candidate: (
                -candidate["storedSoftScore"],
                candidate["totalFlightTimeSeconds"],
                candidate["manifestPath"],
            )
        )
        representative = dict(group[0])
        representative["occurrences"] = [
            {
                "sourceRoot": item["sourceRoot"],
                "manifestPath": item["manifestPath"],
                "storedRank": item["storedRank"],
                "constructionSeed": item["constructionSeed"],
                "constructionHash": item["constructionHash"],
                "scoreHash": item["scoreHash"],
                "storedSoftScore": item["storedSoftScore"],
                "selectedManifestRelativePath": item[
                    "selectedManifestRelativePath"
                ],
                "selectedManifestRecordHash": item[
                    "selectedManifestRecordHash"
                ],
                "physicsEvidencePayloadSha256": item[
                    "physicsEvidencePayloadSha256"
                ],
            }
            for item in sorted(group, key=lambda item: item["manifestPath"])
        ]
        representatives.append(representative)
    representatives.sort(
        key=lambda candidate: (
            -candidate["storedSoftScore"],
            candidate["totalFlightTimeSeconds"],
            candidate["candidateSourceHash"],
        )
    )
    validation = {
        "mode": "fail-closed-manifest-linkage-v1",
        "validatedRootCount": len(contexts),
        "validatedCandidateOccurrenceCount": len(occurrences),
        "crossRootIdentitySha256": sha256_canonical(cross_root_identity),
        "searchSourceHashSha256": cross_root_identity[
            "searchSourceHashSha256"
        ],
        "productionCoreSourceHashSha256": cross_root_identity[
            "productionCoreSourceHashSha256"
        ],
        "nonSeedContractIdentitySha256": sha256_canonical(
            cross_root_identity["nonSeedContractIdentity"]
        ),
        "evaluationDescriptorIdentitySha256": sha256_canonical(
            cross_root_identity["evaluationDescriptorIdentity"]
        ),
        "roots": validation_roots,
    }
    return summaries, representatives, validation


def fmt(value: float, digits: int = 3) -> str:
    return f"{value:.{digits}f}"


def pct(value: float, digits: int = 1) -> str:
    return f"{value * 100.0:.{digits}f}%"


def candidate_label(rank: int, candidate: dict[str, Any]) -> str:
    return f"#{rank} {candidate['candidateSourceHash']}"


def render_hull_panel_svg(
    candidate: dict[str, Any],
    rank: int,
    x: float,
    y: float,
    width: float,
    height: float,
) -> str:
    domain = candidate["launchDomain"]
    yaw_min = domain["minimumYawDegrees"]
    yaw_max = domain["maximumYawDegrees"]
    pitch_min = domain["minimumPitchDegrees"]
    pitch_max = domain["maximumPitchDegrees"]
    plot_left = x + 62
    plot_top = y + 70
    plot_right = x + width - 25
    plot_bottom = y + height - 86
    plot_width = plot_right - plot_left
    plot_height = plot_bottom - plot_top

    def project(point: Sequence[float]) -> tuple[float, float]:
        yaw, pitch = point
        px = plot_left + (yaw - yaw_min) / (yaw_max - yaw_min) * plot_width
        py = plot_bottom - (pitch - pitch_min) / (pitch_max - pitch_min) * plot_height
        return px, py

    parts = [
        f'<g aria-label="{html.escape(candidate_label(rank, candidate))}">',
        f'<rect x="{x:.1f}" y="{y:.1f}" width="{width:.1f}" height="{height:.1f}" '
        f'fill="{SVG_BACKGROUND}" stroke="{SVG_BORDER}"/>',
        f'<text x="{x + 18:.1f}" y="{y + 28:.1f}" fill="{SVG_FOREGROUND}" '
        f'font-size="15" font-weight="600">{html.escape(candidate_label(rank, candidate))}</text>',
        f'<text x="{x + 18:.1f}" y="{y + 49:.1f}" fill="{SVG_MUTED}" font-size="12">'
        f"score {candidate['storedSoftScore']:.2f} | flight {candidate['totalFlightTimeSeconds']:.2f}s"
        "</text>",
    ]
    yaw_ticks = [yaw_min, 0.0, yaw_max]
    pitch_tick_count = 6
    pitch_ticks = [
        pitch_min + (pitch_max - pitch_min) * index / pitch_tick_count
        for index in range(pitch_tick_count + 1)
    ]
    for tick in yaw_ticks:
        px, _py = project((tick, pitch_min))
        parts.append(
            f'<line x1="{px:.2f}" y1="{plot_top:.2f}" x2="{px:.2f}" '
            f'y2="{plot_bottom:.2f}" stroke="{SVG_GRID}" stroke-width="1"/>'
        )
        parts.append(
            f'<text x="{px:.2f}" y="{plot_bottom + 20:.2f}" text-anchor="middle" '
            f'fill="{SVG_MUTED}" font-size="11">{tick:g}</text>'
        )
    for tick in pitch_ticks:
        _px, py = project((yaw_min, tick))
        parts.append(
            f'<line x1="{plot_left:.2f}" y1="{py:.2f}" x2="{plot_right:.2f}" '
            f'y2="{py:.2f}" stroke="{SVG_GRID}" stroke-width="1"/>'
        )
        parts.append(
            f'<text x="{plot_left - 9:.2f}" y="{py + 4:.2f}" text-anchor="end" '
            f'fill="{SVG_MUTED}" font-size="11">{tick:g}</text>'
        )
    parts.extend(
        [
            f'<line x1="{plot_left:.2f}" y1="{plot_bottom:.2f}" x2="{plot_right:.2f}" '
            f'y2="{plot_bottom:.2f}" stroke="{SVG_FOREGROUND}" stroke-width="1.2"/>',
            f'<line x1="{plot_left:.2f}" y1="{plot_top:.2f}" x2="{plot_left:.2f}" '
            f'y2="{plot_bottom:.2f}" stroke="{SVG_FOREGROUND}" stroke-width="1.2"/>',
        ]
    )
    for set_name, color, _rgba in SERIES:
        points = candidate["screenAim"][set_name]["hullYawPitch"]
        projected = [project(point) for point in points]
        if len(projected) >= 3:
            point_text = " ".join(f"{px:.2f},{py:.2f}" for px, py in projected)
            parts.append(
                f'<polygon points="{point_text}" fill="{color}" fill-opacity="0.14" '
                f'stroke="{color}" stroke-width="2"/>'
            )
        elif len(projected) == 2:
            parts.append(
                f'<line x1="{projected[0][0]:.2f}" y1="{projected[0][1]:.2f}" '
                f'x2="{projected[1][0]:.2f}" y2="{projected[1][1]:.2f}" '
                f'stroke="{color}" stroke-width="2"/>'
            )
        elif len(projected) == 1:
            parts.append(
                f'<circle cx="{projected[0][0]:.2f}" cy="{projected[0][1]:.2f}" '
                f'r="3" fill="{color}"/>'
            )
    nominal_x, nominal_y = project(
        (
            candidate["nominalInput"]["yawDegrees"],
            candidate["nominalInput"]["pitchDegrees"],
        )
    )
    parts.extend(
        [
            f'<line x1="{nominal_x - 6:.2f}" y1="{nominal_y:.2f}" '
            f'x2="{nominal_x + 6:.2f}" y2="{nominal_y:.2f}" '
            f'stroke="{SVG_FOREGROUND}" stroke-width="2"/>',
            f'<line x1="{nominal_x:.2f}" y1="{nominal_y - 6:.2f}" '
            f'x2="{nominal_x:.2f}" y2="{nominal_y + 6:.2f}" '
            f'stroke="{SVG_FOREGROUND}" stroke-width="2"/>',
            f'<text x="{(plot_left + plot_right) / 2:.2f}" y="{y + height - 44:.2f}" '
            f'text-anchor="middle" fill="{SVG_FOREGROUND}" font-size="12">Yaw (deg)</text>',
            f'<text x="{x + 15:.2f}" y="{(plot_top + plot_bottom) / 2:.2f}" '
            f'transform="rotate(-90 {x + 15:.2f} {(plot_top + plot_bottom) / 2:.2f})" '
            f'text-anchor="middle" fill="{SVG_FOREGROUND}" font-size="12">Pitch (deg)</text>',
        ]
    )
    legend_y = y + height - 18
    legend_x = x + 22
    for set_name, color, _rgba in SERIES:
        values = candidate["screenAim"][set_name]
        parts.extend(
            [
                f'<rect x="{legend_x:.1f}" y="{legend_y - 10:.1f}" width="11" height="11" '
                f'fill="{color}" fill-opacity="0.5" stroke="{color}"/>',
                f'<text x="{legend_x + 16:.1f}" y="{legend_y:.1f}" fill="{SVG_FOREGROUND}" '
                f'font-size="11">{set_name} {pct(values["retentionRatio"])} '
                f'({values["count"]})</text>',
            ]
        )
        legend_x += 105
    parts.append("</g>")
    return "\n".join(parts)


def render_combined_hulls_svg(candidates: Sequence[dict[str, Any]]) -> str:
    columns = 2
    panel_width = 670
    panel_height = 500
    rows = math.ceil(len(candidates) / columns)
    width = columns * panel_width + 30
    height = rows * panel_height + 30
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}" role="img" '
        f'aria-label="Top M11-B particle-beam candidate ScreenAim convex hulls">',
        "<title>Top candidate ScreenAim convex hulls</title>",
        "<desc>S1 through S4 yaw and pitch hull envelopes for each candidate.</desc>",
        f'<rect width="{width}" height="{height}" fill="{SVG_BACKGROUND}"/>',
    ]
    for index, candidate in enumerate(candidates):
        row = index // columns
        column = index % columns
        parts.append(
            render_hull_panel_svg(
                candidate,
                index + 1,
                15 + column * panel_width,
                15 + row * panel_height,
                panel_width - 15,
                panel_height - 15,
            )
        )
    parts.append("</svg>")
    return "\n".join(parts) + "\n"


def render_single_hull_svg(candidate: dict[str, Any], rank: int) -> str:
    width = 940
    height = 680
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}" role="img" '
        f'aria-label="{html.escape(candidate_label(rank, candidate))} ScreenAim convex hulls">\n'
        f"<title>{html.escape(candidate_label(rank, candidate))} ScreenAim convex hulls</title>\n"
        "<desc>S1 through S4 yaw and pitch convex-hull envelopes; cross marks the nominal input.</desc>\n"
        f'<rect width="{width}" height="{height}" fill="{SVG_BACKGROUND}"/>\n'
        + render_hull_panel_svg(candidate, rank, 15, 15, width - 30, height - 30)
        + "\n</svg>\n"
    )


def render_timeline_svg(candidates: Sequence[dict[str, Any]]) -> str:
    width = 1380
    left = 220
    right = 45
    top = 64
    lane_height = 118
    height = top + len(candidates) * lane_height + 72
    maximum_time = max(candidate["totalFlightTimeSeconds"] for candidate in candidates)
    plot_width = width - left - right

    def tx(seconds: float) -> float:
        return left + seconds / maximum_time * plot_width

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}" role="img" '
        f'aria-label="M11-B candidate flight timelines and recorded deflection angles">',
        "<title>Candidate flight timelines and deflections</title>",
        "<desc>Coast and three assist intervals rendered from stored candidate metrics.</desc>",
        f'<rect width="{width}" height="{height}" fill="{SVG_BACKGROUND}"/>',
    ]
    tick_max = math.ceil(maximum_time / 5.0) * 5
    for tick in range(0, tick_max + 1, 5):
        x = tx(min(float(tick), maximum_time))
        if tick > maximum_time + 1e-9:
            continue
        parts.extend(
            [
                f'<line x1="{x:.2f}" y1="{top - 14}" x2="{x:.2f}" '
                f'y2="{height - 48}" stroke="{SVG_GRID}" stroke-width="1"/>',
                f'<text x="{x:.2f}" y="{height - 24}" text-anchor="middle" '
                f'fill="{SVG_MUTED}" font-size="12">{tick}s</text>',
            ]
        )
    for index, candidate in enumerate(candidates):
        y = top + index * lane_height
        bar_y = y + 28
        bar_height = 30
        parts.extend(
            [
                f'<text x="20" y="{y + 16}" fill="{SVG_FOREGROUND}" font-size="14" '
                f'font-weight="600">{html.escape(candidate_label(index + 1, candidate))}</text>',
                f'<text x="20" y="{y + 38}" fill="{SVG_MUTED}" font-size="12">'
                f"score {candidate['storedSoftScore']:.2f} | "
                f"total {candidate['totalFlightTimeSeconds']:.2f}s</text>",
                f'<rect x="{tx(0):.2f}" y="{bar_y}" '
                f'width="{tx(candidate["totalFlightTimeSeconds"]) - tx(0):.2f}" '
                f'height="{bar_height}" fill="#E2E7EF" stroke="{SVG_BORDER}"/>',
            ]
        )
        for assist_index, assist in enumerate(candidate["assists"]):
            _set_name, color, _rgba = SERIES[assist_index]
            x0 = tx(assist["enterTimeSeconds"])
            x1 = tx(assist["exitTimeSeconds"])
            xc = tx(assist["closestTimeSeconds"])
            parts.extend(
                [
                    f'<rect x="{x0:.2f}" y="{bar_y}" width="{x1 - x0:.2f}" '
                    f'height="{bar_height}" fill="{color}" fill-opacity="0.72" '
                    f'stroke="{color}"/>',
                    f'<line x1="{xc:.2f}" y1="{bar_y - 4}" x2="{xc:.2f}" '
                    f'y2="{bar_y + bar_height + 4}" stroke="{SVG_FOREGROUND}" '
                    f'stroke-width="1.2"/>',
                    f'<text x="{(x0 + x1) / 2:.2f}" y="{bar_y + 20}" '
                    f'text-anchor="middle" fill="{SVG_FOREGROUND}" font-size="11">'
                    f"A{assist_index + 1} {assist['actualDeflectionDegrees']:.1f} deg"
                    "</text>",
                    f'<text x="{(x0 + x1) / 2:.2f}" y="{bar_y + 49}" '
                    f'text-anchor="middle" fill="{SVG_MUTED}" font-size="11">'
                    f"{assist['influenceDurationSeconds']:.2f}s influence"
                    "</text>",
                ]
            )
        parts.append(
            f'<text x="{tx(candidate["totalFlightTimeSeconds"]) + 6:.2f}" '
            f'y="{bar_y + 20}" fill="{SVG_FOREGROUND}" font-size="11">'
            f"{candidate['totalFlightTimeSeconds']:.2f}s</text>"
        )
    parts.append(
        f'<text x="{left + plot_width / 2:.2f}" y="{height - 5}" text-anchor="middle" '
        f'fill="{SVG_FOREGROUND}" font-size="12">Flight time (seconds)</text>'
    )
    parts.append("</svg>")
    return "\n".join(parts) + "\n"


class PngCanvas:
    def __init__(self, width: int, height: int, scale: int = 2) -> None:
        try:
            from PIL import Image, ImageDraw
        except ImportError as error:
            raise RuntimeError("PillowUnavailable") from error
        self.Image = Image
        self.ImageDraw = ImageDraw
        self.scale = scale
        self.image = Image.new(
            "RGB", (width * scale, height * scale), (247, 249, 252)
        )
        self.draw = ImageDraw.Draw(self.image, "RGBA")

    def s(self, value: float) -> int:
        return round(value * self.scale)

    def box(self, values: Sequence[float]) -> tuple[int, int, int, int]:
        return tuple(self.s(value) for value in values)  # type: ignore[return-value]

    def font(self, size: int, bold: bool = False) -> Any:
        from PIL import ImageFont

        windows_font = Path("C:/Windows/Fonts") / (
            "segoeuib.ttf" if bold else "segoeui.ttf"
        )
        try:
            return ImageFont.truetype(str(windows_font), size * self.scale)
        except OSError:
            return ImageFont.load_default()

    def text(
        self,
        xy: tuple[float, float],
        value: str,
        size: int = 12,
        color: tuple[int, int, int, int] = (23, 32, 51, 255),
        anchor: str | None = None,
        bold: bool = False,
    ) -> None:
        self.draw.text(
            (self.s(xy[0]), self.s(xy[1])),
            value,
            fill=color,
            font=self.font(size, bold),
            anchor=anchor,
        )

    def save(self, path: Path) -> None:
        result = self.image.resize(
            (self.image.width // self.scale, self.image.height // self.scale),
            self.Image.Resampling.LANCZOS,
        )
        output = io.BytesIO()
        result.save(output, format="PNG", optimize=False, compress_level=9)
        atomic_write_bytes(path, output.getvalue())


def draw_hull_panel_png(
    canvas: PngCanvas,
    candidate: dict[str, Any],
    rank: int,
    x: float,
    y: float,
    width: float,
    height: float,
) -> None:
    domain = candidate["launchDomain"]
    yaw_min = domain["minimumYawDegrees"]
    yaw_max = domain["maximumYawDegrees"]
    pitch_min = domain["minimumPitchDegrees"]
    pitch_max = domain["maximumPitchDegrees"]
    plot_left = x + 62
    plot_top = y + 70
    plot_right = x + width - 25
    plot_bottom = y + height - 86
    plot_width = plot_right - plot_left
    plot_height = plot_bottom - plot_top

    def project(point: Sequence[float]) -> tuple[int, int]:
        yaw, pitch = point
        px = plot_left + (yaw - yaw_min) / (yaw_max - yaw_min) * plot_width
        py = plot_bottom - (pitch - pitch_min) / (pitch_max - pitch_min) * plot_height
        return canvas.s(px), canvas.s(py)

    canvas.draw.rectangle(
        canvas.box((x, y, x + width, y + height)),
        fill=(247, 249, 252, 255),
        outline=(138, 150, 168, 255),
        width=canvas.s(1),
    )
    canvas.text((x + 18, y + 18), candidate_label(rank, candidate), 15, bold=True)
    canvas.text(
        (x + 18, y + 42),
        f"score {candidate['storedSoftScore']:.2f} | "
        f"flight {candidate['totalFlightTimeSeconds']:.2f}s",
        12,
        (89, 101, 121, 255),
    )
    canvas.text(
        (plot_left, plot_top - 17),
        "Pitch (deg)",
        10,
        (89, 101, 121, 255),
    )
    for tick in [yaw_min, 0.0, yaw_max]:
        px, _ = project((tick, pitch_min))
        canvas.draw.line(
            (px, canvas.s(plot_top), px, canvas.s(plot_bottom)),
            fill=(217, 223, 233, 255),
            width=canvas.s(1),
        )
        canvas.text(
            (px / canvas.scale, plot_bottom + 10),
            f"{tick:g}",
            10,
            (89, 101, 121, 255),
            "ma",
        )
    pitch_tick_count = 6
    for index in range(pitch_tick_count + 1):
        tick = pitch_min + (pitch_max - pitch_min) * index / pitch_tick_count
        _, py = project((yaw_min, tick))
        canvas.draw.line(
            (canvas.s(plot_left), py, canvas.s(plot_right), py),
            fill=(217, 223, 233, 255),
            width=canvas.s(1),
        )
        canvas.text(
            (plot_left - 8, py / canvas.scale),
            f"{tick:g}",
            10,
            (89, 101, 121, 255),
            "rm",
        )
    canvas.draw.line(
        canvas.box((plot_left, plot_bottom, plot_right, plot_bottom)),
        fill=(23, 32, 51, 255),
        width=canvas.s(1.2),
    )
    canvas.draw.line(
        canvas.box((plot_left, plot_top, plot_left, plot_bottom)),
        fill=(23, 32, 51, 255),
        width=canvas.s(1.2),
    )
    for set_name, color_hex, rgba in SERIES:
        points = [
            project(point)
            for point in candidate["screenAim"][set_name]["hullYawPitch"]
        ]
        if len(points) >= 3:
            canvas.draw.polygon(points, fill=rgba, outline=rgba[:3] + (255,))
            canvas.draw.line(
                points + [points[0]], fill=rgba[:3] + (255,), width=canvas.s(2)
            )
        elif len(points) == 2:
            canvas.draw.line(points, fill=rgba[:3] + (255,), width=canvas.s(2))
        elif len(points) == 1:
            px, py = points[0]
            radius = canvas.s(3)
            canvas.draw.ellipse(
                (px - radius, py - radius, px + radius, py + radius),
                fill=rgba[:3] + (255,),
            )
    nominal_x, nominal_y = project(
        (
            candidate["nominalInput"]["yawDegrees"],
            candidate["nominalInput"]["pitchDegrees"],
        )
    )
    canvas.draw.line(
        (
            nominal_x - canvas.s(6),
            nominal_y,
            nominal_x + canvas.s(6),
            nominal_y,
        ),
        fill=(23, 32, 51, 255),
        width=canvas.s(2),
    )
    canvas.draw.line(
        (
            nominal_x,
            nominal_y - canvas.s(6),
            nominal_x,
            nominal_y + canvas.s(6),
        ),
        fill=(23, 32, 51, 255),
        width=canvas.s(2),
    )
    canvas.text(
        ((plot_left + plot_right) / 2, y + height - 48),
        "Yaw (deg)",
        11,
        anchor="ma",
    )
    legend_y = y + height - 27
    legend_x = x + 22
    for set_name, _color, rgba in SERIES:
        canvas.draw.rectangle(
            canvas.box((legend_x, legend_y, legend_x + 10, legend_y + 10)),
            fill=rgba[:3] + (150,),
            outline=rgba[:3] + (255,),
            width=canvas.s(1),
        )
        values = candidate["screenAim"][set_name]
        canvas.text(
            (legend_x + 15, legend_y - 2),
            f"{set_name} {pct(values['retentionRatio'])} ({values['count']})",
            10,
        )
        legend_x += 105


def render_combined_hulls_png(
    path: Path, candidates: Sequence[dict[str, Any]]
) -> None:
    columns = 2
    panel_width = 670
    panel_height = 500
    rows = math.ceil(len(candidates) / columns)
    canvas = PngCanvas(columns * panel_width + 30, rows * panel_height + 30)
    for index, candidate in enumerate(candidates):
        row = index // columns
        column = index % columns
        draw_hull_panel_png(
            canvas,
            candidate,
            index + 1,
            15 + column * panel_width,
            15 + row * panel_height,
            panel_width - 15,
            panel_height - 15,
        )
    canvas.save(path)


def render_single_hull_png(path: Path, candidate: dict[str, Any], rank: int) -> None:
    canvas = PngCanvas(940, 680)
    draw_hull_panel_png(canvas, candidate, rank, 15, 15, 910, 650)
    canvas.save(path)


def render_timeline_png(path: Path, candidates: Sequence[dict[str, Any]]) -> None:
    width = 1380
    left = 220
    right = 45
    top = 64
    lane_height = 118
    height = top + len(candidates) * lane_height + 72
    maximum_time = max(candidate["totalFlightTimeSeconds"] for candidate in candidates)
    plot_width = width - left - right
    canvas = PngCanvas(width, height)

    def tx(seconds: float) -> float:
        return left + seconds / maximum_time * plot_width

    tick_max = math.ceil(maximum_time / 5.0) * 5
    for tick in range(0, tick_max + 1, 5):
        if tick > maximum_time + 1e-9:
            continue
        x = tx(float(tick))
        canvas.draw.line(
            canvas.box((x, top - 14, x, height - 48)),
            fill=(217, 223, 233, 255),
            width=canvas.s(1),
        )
        canvas.text((x, height - 27), f"{tick}s", 11, (89, 101, 121, 255), "ma")
    for index, candidate in enumerate(candidates):
        y = top + index * lane_height
        bar_y = y + 28
        bar_height = 30
        canvas.text(
            (20, y),
            candidate_label(index + 1, candidate),
            13,
            bold=True,
        )
        canvas.text(
            (20, y + 23),
            f"score {candidate['storedSoftScore']:.2f} | "
            f"total {candidate['totalFlightTimeSeconds']:.2f}s",
            11,
            (89, 101, 121, 255),
        )
        canvas.draw.rectangle(
            canvas.box(
                (
                    tx(0.0),
                    bar_y,
                    tx(candidate["totalFlightTimeSeconds"]),
                    bar_y + bar_height,
                )
            ),
            fill=(226, 231, 239, 255),
            outline=(138, 150, 168, 255),
            width=canvas.s(1),
        )
        for assist_index, assist in enumerate(candidate["assists"]):
            _set_name, _color, rgba = SERIES[assist_index]
            x0 = tx(assist["enterTimeSeconds"])
            x1 = tx(assist["exitTimeSeconds"])
            xc = tx(assist["closestTimeSeconds"])
            canvas.draw.rectangle(
                canvas.box((x0, bar_y, x1, bar_y + bar_height)),
                fill=rgba[:3] + (190,),
                outline=rgba[:3] + (255,),
                width=canvas.s(1),
            )
            canvas.draw.line(
                canvas.box((xc, bar_y - 4, xc, bar_y + bar_height + 4)),
                fill=(23, 32, 51, 255),
                width=canvas.s(1),
            )
            canvas.text(
                ((x0 + x1) / 2, bar_y + bar_height / 2),
                f"A{assist_index + 1} {assist['actualDeflectionDegrees']:.1f} deg",
                10,
                anchor="mm",
            )
            canvas.text(
                ((x0 + x1) / 2, bar_y + 43),
                f"{assist['influenceDurationSeconds']:.2f}s influence",
                10,
                (89, 101, 121, 255),
                "ma",
            )
        canvas.text(
            (tx(candidate["totalFlightTimeSeconds"]) + 6, bar_y + bar_height / 2),
            f"{candidate['totalFlightTimeSeconds']:.2f}s",
            10,
            anchor="lm",
        )
    canvas.text(
        (left + plot_width / 2, height - 7),
        "Flight time (seconds)",
        11,
        anchor="ma",
    )
    canvas.save(path)


def candidate_csv_row(rank: int, candidate: dict[str, Any]) -> dict[str, Any]:
    row: dict[str, Any] = {
        "report_rank": rank,
        "candidate_source_hash": candidate["candidateSourceHash"],
        "recorded_status": candidate["recordedStatus"],
        "certification_status": candidate["certificationStatus"],
        "stored_soft_score": fmt(candidate["storedSoftScore"], 9),
        "total_flight_seconds": fmt(candidate["totalFlightTimeSeconds"], 9),
        "maximum_coast_seconds": fmt(candidate["maximumCoastSeconds"], 9),
        "final_coast_seconds": fmt(candidate["finalCoastSeconds"], 9),
        "total_influence_seconds": fmt(
            candidate["totalInfluenceDurationSeconds"], 9
        ),
        "minimum_layout_turn_degrees": fmt(
            candidate["minimumLayoutTurnDegrees"], 9
        ),
        "robust_survivor_count": candidate["robustSurvivorCount"],
        "alternating_lateral_turn_count": candidate[
            "alternatingLateralTurnCount"
        ],
        "source_roots": "|".join(
            occurrence["sourceRoot"] for occurrence in candidate["occurrences"]
        ),
        "manifest_paths": "|".join(
            occurrence["manifestPath"] for occurrence in candidate["occurrences"]
        ),
        "selected_manifest_relative_paths": "|".join(
            occurrence["selectedManifestRelativePath"]
            for occurrence in candidate["occurrences"]
        ),
        "selected_manifest_record_hashes": "|".join(
            occurrence["selectedManifestRecordHash"]
            for occurrence in candidate["occurrences"]
        ),
        "physics_evidence_payload_sha256": candidate[
            "physicsEvidencePayloadSha256"
        ],
    }
    for index, assist in enumerate(candidate["assists"], start=1):
        row[f"assist{index}_deflection_degrees"] = fmt(
            assist["actualDeflectionDegrees"], 9
        )
        row[f"assist{index}_influence_seconds"] = fmt(
            assist["influenceDurationSeconds"], 9
        )
        row[f"assist{index}_coast_before_seconds"] = fmt(
            assist["coastBeforeEnterSeconds"], 9
        )
        row[f"assist{index}_enter_seconds"] = fmt(assist["enterTimeSeconds"], 9)
        row[f"assist{index}_exit_seconds"] = fmt(assist["exitTimeSeconds"], 9)
    for set_name, _color, _rgba in SERIES:
        screen = candidate["screenAim"][set_name]
        holdout = candidate["independentHoldout"]["sets"][set_name]
        prefix = set_name.lower()
        row[f"{prefix}_screen_count"] = screen["count"]
        row[f"{prefix}_screen_retention_ratio"] = fmt(
            screen["retentionRatio"], 12
        )
        row[f"{prefix}_holdout_count"] = holdout["count"]
        row[f"{prefix}_holdout_retention_ratio"] = fmt(
            holdout["retentionRatio"], 12
        )
        row[f"{prefix}_hull_area_square_degrees"] = fmt(
            screen["hullAreaSquareDegrees"], 9
        )
        row[f"{prefix}_hull_yaw_span_degrees"] = fmt(
            screen["hullYawSpanDegrees"], 9
        )
        row[f"{prefix}_hull_pitch_span_degrees"] = fmt(
            screen["hullPitchSpanDegrees"], 9
        )
        row[f"{prefix}_hull_compactness"] = fmt(screen["hullCompactness"], 12)
        row[f"{prefix}_hull_contains_nominal"] = (
            "true" if screen["hullContainsNominal"] else "false"
        )
    return row


def render_csv(candidates: Sequence[dict[str, Any]]) -> str:
    rows = [
        candidate_csv_row(index + 1, candidate)
        for index, candidate in enumerate(candidates)
    ]
    output = io.StringIO(newline="")
    writer = csv.DictWriter(output, fieldnames=list(rows[0].keys()), lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)
    return output.getvalue()


def markdown_table(candidates: Sequence[dict[str, Any]]) -> str:
    lines = [
        "| Rank | Candidate | Stored score | Deflections | Flight | Max coast | ScreenAim S1→S4 | Holdout S1→S4 |",
        "| ---: | --- | ---: | --- | ---: | ---: | --- | --- |",
    ]
    for index, candidate in enumerate(candidates):
        deflections = " / ".join(
            f"{assist['actualDeflectionDegrees']:.1f}°"
            for assist in candidate["assists"]
        )
        screen = " / ".join(
            pct(candidate["screenAim"][set_name]["retentionRatio"])
            for set_name, _color, _rgba in SERIES
        )
        holdout = " / ".join(
            pct(
                candidate["independentHoldout"]["sets"][set_name][
                    "retentionRatio"
                ]
            )
            for set_name, _color, _rgba in SERIES
        )
        lines.append(
            f"| {index + 1} | `{candidate['candidateSourceHash']}` | "
            f"{candidate['storedSoftScore']:.2f} | {deflections} | "
            f"{candidate['totalFlightTimeSeconds']:.2f}s | "
            f"{candidate['maximumCoastSeconds']:.2f}s | {screen} | {holdout} |"
        )
    return "\n".join(lines)


def render_markdown(
    roots: Sequence[Path],
    summaries: Sequence[dict[str, Any]],
    candidates: Sequence[dict[str, Any]],
    validation: dict[str, Any],
    report_tool_provenance: dict[str, Any],
    report_hash: str,
    repo_root: Path,
) -> str:
    best = candidates[0]
    strongest_deflections = max(
        candidates,
        key=lambda candidate: sum(
            assist["actualDeflectionDegrees"] for assist in candidate["assists"]
        ),
    )
    best_efficiency = max(
        summaries,
        key=lambda summary: summary["acceptedPerMillionSolverInvocations"],
    )
    best_hull_areas = [
        best["screenAim"][set_name]["hullAreaSquareDegrees"]
        for set_name, _color, _rgba in SERIES
    ]
    best_hull_ratios = [
        best_hull_areas[index] / best_hull_areas[index - 1]
        for index in range(1, len(best_hull_areas))
    ]
    lines = [
        "# M11-B v2.1 particle-beam candidate report",
        "",
        f"- Report hash: `{report_hash}`",
        f"- Unique candidates shown: {len(candidates)}",
        "- Authority: recorded CLI manifests only; no trajectory was integrated or reclassified.",
        "- Certification: every listed layout keeps its recorded `Candidate / NOT CERTIFIED` status.",
        f"- Validation: `{validation['mode']}`; cross-root identity "
        f"`{validation['crossRootIdentitySha256']}`.",
        f"- Report tool: `{report_tool_provenance['relativePath']}` at "
        f"`{report_tool_provenance['sha256']}` "
        "(report-only identity; intentionally outside C++ SearchSourceHash).",
        "",
        "## Candidate comparison",
        "",
        markdown_table(candidates),
        "",
        "ScreenAim S1 is a fraction of the full yaw/pitch sample; S2-S4 are conditional",
        "retention fractions relative to the previous prefix. Holdout uses the same",
        "interpretation on the independent 512-sample set.",
        "",
        "## Recorded search efficiency",
        "",
        "| Summary | Solves | Accepted | Accepted / M solves | vs v3 | Wall time |",
        "| --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for summary in summaries:
        multiplier = summary["efficiencyMultiplierVsV3"]
        lines.append(
            f"| `{summary['path']}` | {summary['solverInvocationCount']} | "
            f"{summary['acceptedCount']} | "
            f"{summary['acceptedPerMillionSolverInvocations']:.3f} | "
            f"{multiplier:.2f}× | {summary['wallClockSeconds']:.2f}s |"
        )
    lines.extend(
        [
            "",
            "## Data observations",
            "",
            f"- Highest stored soft score: `{best['candidateSourceHash']}` "
            f"({best['storedSoftScore']:.2f}), with "
            f"{best['totalFlightTimeSeconds']:.2f}s total flight time.",
            f"- Largest recorded three-assist deflection sum: "
            f"`{strongest_deflections['candidateSourceHash']}` "
            f"({sum(assist['actualDeflectionDegrees'] for assist in strongest_deflections['assists']):.1f}°).",
            f"- Top-candidate ScreenAim hull areas: "
            f"{' → '.join(f'{area:.2f}' for area in best_hull_areas)} deg²; "
            f"successive envelope ratios are "
            f"{' / '.join(pct(ratio) for ratio in best_hull_ratios)}.",
            f"- Most solve-efficient input run: `{best_efficiency['path']}` at "
            f"{best_efficiency['acceptedPerMillionSolverInvocations']:.3f} accepted candidates "
            f"per million solver calls ({best_efficiency['efficiencyMultiplierVsV3']:.2f}× the recorded v3 baseline).",
            "- Convex-hull images are envelopes of sampled yaw/pitch evidence. They do not",
            "  prove connectivity, uniqueness, or exhaustive input-domain certification.",
            "- These numbers are useful hand-feel proxies (bend strength, pacing, and island",
            "  progression); actual input feel still requires the planned M11-C PIE pass.",
            "",
            "## Inputs",
            "",
        ]
    )
    lines.extend(
        f"- `{display_path(root, repo_root)}`" for root in sorted(roots)
    )
    return "\n".join(lines) + "\n"


def report_candidate(candidate: dict[str, Any], rank: int) -> dict[str, Any]:
    result = dict(candidate)
    result["reportRank"] = rank
    return result


def write_report(
    roots: Sequence[Path],
    output: Path,
    top_count: int,
    repo_root: Path,
) -> dict[str, Any]:
    summaries, unique_candidates, validation = collect_candidates(roots, repo_root)
    if not unique_candidates:
        raise RuntimeError("NoCandidateManifests")
    selected = unique_candidates[: min(top_count, len(unique_candidates))]
    candidate_records = [
        report_candidate(candidate, index + 1)
        for index, candidate in enumerate(selected)
    ]
    report_tool_path = Path(__file__).resolve()
    report_tool_provenance = {
        "schema": "abts.m11b21.particle_beam_report_tool_provenance.v1",
        "relativePath": display_path(report_tool_path, repo_root),
        "sha256": sha256_file(report_tool_path),
        "byteCount": report_tool_path.stat().st_size,
        "identityScope": "read-only-reporting-tool-only",
        "includedInCppSearchSourceHash": False,
    }
    report_without_hash = {
        "schema": REPORT_SCHEMA,
        "reportToolVersion": REPORT_TOOL_VERSION,
        "authority": "read-only-recorded-manifest-values",
        "ordering": (
            "storedSoftScores.total descending, then totalFlightTimeSeconds "
            "ascending, then candidateSourceHash"
        ),
        "validation": validation,
        "reportToolProvenance": report_tool_provenance,
        "inputRoots": [
            display_path(root, repo_root) for root in sorted(path.resolve() for path in roots)
        ],
        "inputSummaries": summaries,
        "candidateOccurrenceCount": sum(
            len(candidate["occurrences"]) for candidate in candidate_records
        ),
        "uniqueCandidateCount": len(unique_candidates),
        "selectedCandidateCount": len(candidate_records),
        "certificationWarning": (
            "Candidate / NOT CERTIFIED; hulls are sampled envelopes, not "
            "connectivity, uniqueness, or exhaustive certification proofs."
        ),
        "candidates": candidate_records,
    }
    report_hash = hashlib.sha256(
        canonical_json(report_without_hash).encode("utf-8")
    ).hexdigest()
    report = dict(report_without_hash)
    report["reportHashSha256"] = report_hash

    output.mkdir(parents=True, exist_ok=True)
    atomic_write_text(
        output / "report.json",
        json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False) + "\n",
    )
    atomic_write_text(output / "candidates.csv", render_csv(candidate_records))
    atomic_write_text(
        output / "summary.md",
        render_markdown(
            roots,
            summaries,
            candidate_records,
            validation,
            report_tool_provenance,
            report_hash,
            repo_root,
        ),
    )
    atomic_write_text(
        output / "top_candidates_hulls.svg",
        render_combined_hulls_svg(candidate_records),
    )
    atomic_write_text(
        output / "top_candidates_timeline.svg",
        render_timeline_svg(candidate_records),
    )
    for index, candidate in enumerate(candidate_records):
        stem = (
            f"candidate_rank_{index + 1:02d}_"
            f"{candidate['candidateSourceHash'].removeprefix('0x')}_hulls"
        )
        atomic_write_text(
            output / f"{stem}.svg",
            render_single_hull_svg(candidate, index + 1),
        )

    png_paths: list[str] = []
    try:
        combined_png = output / "top_candidates_hulls.png"
        timeline_png = output / "top_candidates_timeline.png"
        render_combined_hulls_png(combined_png, candidate_records)
        render_timeline_png(timeline_png, candidate_records)
        png_paths.extend(
            [
                display_path(combined_png, repo_root),
                display_path(timeline_png, repo_root),
            ]
        )
        for index, candidate in enumerate(candidate_records):
            stem = (
                f"candidate_rank_{index + 1:02d}_"
                f"{candidate['candidateSourceHash'].removeprefix('0x')}_hulls"
            )
            png_path = output / f"{stem}.png"
            render_single_hull_png(png_path, candidate, index + 1)
            png_paths.append(display_path(png_path, repo_root))
    except RuntimeError as error:
        if str(error) != "PillowUnavailable":
            raise

    return {
        "schema": "abts.m11b21.particle_beam_read_only_report.result.v1",
        "reportHashSha256": report_hash,
        "output": display_path(output, repo_root),
        "candidateCount": len(candidate_records),
        "validationMode": validation["mode"],
        "crossRootIdentitySha256": validation["crossRootIdentitySha256"],
        "reportToolSha256": report_tool_provenance["sha256"],
        "candidateSourceHashes": [
            candidate["candidateSourceHash"] for candidate in candidate_records
        ],
        "pngFiles": png_paths,
    }


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Create a deterministic, read-only M11-B v2.1 candidate report "
            "from ABTSM11SearchCLI manifests."
        )
    )
    parser.add_argument(
        "--input-root",
        action="append",
        required=True,
        type=Path,
        help="Search output root containing summary.json and candidates/.",
    )
    parser.add_argument(
        "--output",
        required=True,
        type=Path,
        help="Destination directory for JSON, CSV, Markdown, SVG, and PNG.",
    )
    parser.add_argument(
        "--top",
        type=int,
        default=4,
        help="Maximum number of unique candidates to include (default: 4).",
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    arguments = parse_arguments(argv)
    if arguments.top <= 0:
        print("InvalidTopCount", file=sys.stderr)
        return 2
    repo_root = repository_root()
    roots = [
        path if path.is_absolute() else (repo_root / path)
        for path in arguments.input_root
    ]
    output = (
        arguments.output
        if arguments.output.is_absolute()
        else repo_root / arguments.output
    )
    try:
        result = write_report(roots, output, arguments.top, repo_root)
    except RuntimeError as error:
        print(str(error), file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
