#!/usr/bin/env python3
"""Offline M11 camera-observation criteria and orthogonality comparison.

This tool consumes observation schema v1-v9 CSV from the M11 capture runner.
It never reads pixels and never changes a candidate, trajectory, camera, or UE
asset. A criteria failure is a useful M1 result, so the default exit status is
zero for a structurally valid report. Use --require-pass to gate later stages.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import pathlib
import sys
from dataclasses import dataclass
from typing import Iterable


SUPPORTED_SCHEMA_VERSIONS = {1, 2, 3, 4, 5, 6, 7, 8, 9}
M3_APPROACH_BACKWARD_JUMP_PX_MAX = 20.0
M3_FIRST_BODY_BLEND_SECONDS_MAX = 0.10
M3_FIRST_BODY_VISIBLE_SECONDS_MAX = 0.75
M3_FIRST_BODY_FULLY_VISIBLE_SECONDS_MAX = 1.00
REQUIRED_COLUMNS = {
    "schemaVersion",
    "frameIndex",
    "captureSeconds",
    "playbackSeconds",
    "interactionState",
    "stage",
    "currentTarget",
    "stageReason",
    "birdVisibleRatio",
    "targetPixelRadius",
    "targetVisibleRatio",
    "cameraPositionDeltaCM",
    "cameraRotationDeltaDegrees",
    "fovDeltaDegrees",
}
ENVIRONMENT_COLUMNS = {
    "environmentStage",
    "environmentProfile",
}
OBSERVATION_FINGERPRINT_COLUMNS = (
    "playbackSeconds",
    "interactionState",
    "stage",
    "currentTarget",
    "stageReason",
    "birdWorldX",
    "birdWorldY",
    "birdWorldZ",
    "targetWorldX",
    "targetWorldY",
    "targetWorldZ",
    "cameraWorldX",
    "cameraWorldY",
    "cameraWorldZ",
    "cameraPitch",
    "cameraYaw",
    "cameraRoll",
    "fovDegrees",
)
DECISION_FINGERPRINT_COLUMNS = (
    "playbackSeconds",
    "interactionState",
    "stage",
    "currentTarget",
    "framingTarget",
    "stageProgress",
    "stageDurationSeconds",
    "stageReason",
    "shotPhase",
    "shotReason",
    "shotProgress",
    "shotDurationSeconds",
    "shotEndSlope",
)
DIRECTOR_COLUMNS = {
    "directorMode",
    "directorM2FrozenEnabled",
    "directorBlendAlpha",
}
M3_DIRECTOR_COLUMNS = {
    "framingTarget",
    "directorM3FrozenEnabled",
    "stageProgress",
    "stageDurationSeconds",
}
M3_SHOT_COLUMNS = {
    "shotPhase",
    "shotReason",
    "shotProgress",
    "shotDurationSeconds",
    "shotEndSlope",
}
M3_BRIDGE_COLUMNS = {
    "bridgeOutgoingTarget",
    "bridgeOutgoingScreenX",
    "bridgeOutgoingScreenY",
    "bridgeOutgoingPixelRadius",
    "bridgeOutgoingVisibleRatio",
    "bridgeIncomingTarget",
    "bridgeIncomingScreenX",
    "bridgeIncomingScreenY",
    "bridgeIncomingPixelRadius",
    "bridgeIncomingVisibleRatio",
}
M4_TERMINAL_COLUMNS = {"endpointAuthority"}
M4_ENDPOINT_AUTHORITIES = {
    "None",
    "CandidateQualified",
    "PhysicalContact",
}
M4_TERMINAL_STAGES = {"FinalApproach", "Terminal"}
M4_TERMINAL_SHOT_PHASES = {"TerminalAcquire", "TerminalTrack"}
M6_FORMATION_COLUMNS = {
    "formationExpectedSpacingCM",
    "formationSpacing01CM",
    "formationSpacing12CM",
    "formationSpacing23CM",
    "formationOrderStable",
    "formationPrimaryAnchored",
    "formationFullyDeployed",
} | {
    f"formation{index}{suffix}"
    for index in range(4)
    for suffix in (
        "BirdId", "Actor", "WorldX", "WorldY", "WorldZ",
        "ScreenX", "ScreenY", "DepthCM", "PixelRadius", "VisibleRatio",
    )
}


@dataclass(frozen=True)
class Thresholds:
    bird_visible_ratio: float = 0.5
    target_visible_ratio: float = 0.01
    target_pixel_radius: float = 4.0
    camera_position_delta_cm: float = 5000.0
    camera_rotation_delta_degrees: float = 15.0
    fov_delta_degrees: float = 2.0


def _finite(row: dict[str, str], key: str) -> float:
    try:
        value = float(row[key])
    except (KeyError, ValueError) as exc:
        raise ValueError(f"invalid numeric field {key!r}: {row.get(key)!r}") from exc
    if not math.isfinite(value):
        raise ValueError(f"non-finite numeric field {key!r}: {value!r}")
    return value


def _longest_run(values: Iterable[bool]) -> int:
    longest = 0
    current = 0
    for value in values:
        current = current + 1 if value else 0
        longest = max(longest, current)
    return longest


def _mean(values: list[float]) -> float:
    return sum(values) / len(values) if values else 0.0


def _median(values: list[float]) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    midpoint = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[midpoint]
    return 0.5 * (ordered[midpoint - 1] + ordered[midpoint])


def _edge_mean(values: list[float], from_end: bool) -> float:
    if not values:
        return 0.0
    count = max(1, len(values) // 4)
    selected = values[-count:] if from_end else values[:count]
    return _mean(selected)


def _subjects_observable(
    row: dict[str, str], thresholds: Thresholds
) -> bool:
    return (
        _finite(row, "birdVisibleRatio") >= thresholds.bird_visible_ratio
        and _finite(row, "targetVisibleRatio") > thresholds.target_visible_ratio
        and _finite(row, "targetPixelRadius") >= thresholds.target_pixel_radius
    )


def _derivative_maxima(
    rows: list[dict[str, str]], key: str
) -> tuple[float, float, float]:
    samples = [
        (_finite(row, "captureSeconds"), _finite(row, key)) for row in rows
    ]
    maxima: list[float] = []
    for _ in range(3):
        derivatives: list[tuple[float, float]] = []
        for previous, current in zip(samples, samples[1:]):
            delta_seconds = current[0] - previous[0]
            if delta_seconds <= 1.0e-9:
                continue
            derivatives.append(
                (
                    0.5 * (previous[0] + current[0]),
                    (current[1] - previous[1]) / delta_seconds,
                )
            )
        maxima.append(max((abs(value) for _, value in derivatives), default=0.0))
        samples = derivatives
    return maxima[0], maxima[1], maxima[2]


def _fingerprint(
    rows: list[dict[str, str]], columns: tuple[str, ...]
) -> str:
    digest = hashlib.sha256()
    for row in rows:
        canonical: list[str] = []
        for key in columns:
            value = row[key]
            if key not in {
                "interactionState",
                "stage",
                "currentTarget",
                "framingTarget",
                "stageReason",
                "shotPhase",
                "shotReason",
                "endpointAuthority",
            }:
                value = f"{float(value):.6f}"
            canonical.append(value)
        digest.update(("|".join(canonical) + "\n").encode("utf-8"))
    return digest.hexdigest().upper()


def _criteria_fingerprint(
    rows: list[dict[str, str]],
    bird_lost: list[bool],
    target_lost: list[bool],
    position_jump: list[bool],
    rotation_jump: list[bool],
    fov_jump: list[bool],
    m4_acquire_no_target: list[bool] | None = None,
    m4_endpoint_missing: list[bool] | None = None,
) -> str:
    digest = hashlib.sha256()
    for index, row in enumerate(rows):
        canonical = (
            row["playbackSeconds"],
            row["stage"],
            row["currentTarget"],
            row["framingTarget"],
            "1" if bird_lost[index] else "0",
            "1" if target_lost[index] else "0",
            "1" if position_jump[index] else "0",
            "1" if rotation_jump[index] else "0",
            "1" if fov_jump[index] else "0",
        )
        if m4_acquire_no_target is not None and m4_endpoint_missing is not None:
            canonical += (
                "1" if m4_acquire_no_target[index] else "0",
                "1" if m4_endpoint_missing[index] else "0",
            )
        digest.update(("|".join(canonical) + "\n").encode("utf-8"))
    return digest.hexdigest().upper()


def read_rows(path: pathlib.Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        missing = REQUIRED_COLUMNS.difference(reader.fieldnames or ())
        if missing:
            raise ValueError(f"{path}: missing columns: {sorted(missing)}")
        rows = list(reader)
    if not rows:
        raise ValueError(f"{path}: no observation rows")
    expected_schema: int | None = None
    for expected_index, row in enumerate(rows):
        schema = int(_finite(row, "schemaVersion"))
        frame_index = int(_finite(row, "frameIndex"))
        if schema not in SUPPORTED_SCHEMA_VERSIONS:
            raise ValueError(
                f"{path}: unsupported schema {schema}; "
                f"expected one of {sorted(SUPPORTED_SCHEMA_VERSIONS)}"
            )
        if expected_schema is None:
            expected_schema = schema
        elif schema != expected_schema:
            raise ValueError(f"{path}: mixed observation schemas")
        if schema >= 2:
            missing_director = DIRECTOR_COLUMNS.difference(row)
            if missing_director:
                raise ValueError(
                    f"{path}: schema {schema} missing director columns: "
                    f"{sorted(missing_director)}"
                )
            _finite(row, "directorM2FrozenEnabled")
            _finite(row, "directorBlendAlpha")
        if schema >= 3:
            missing_m3 = M3_DIRECTOR_COLUMNS.difference(row)
            if missing_m3:
                raise ValueError(
                    f"{path}: schema {schema} missing M3 columns: "
                    f"{sorted(missing_m3)}"
                )
            _finite(row, "directorM3FrozenEnabled")
        if schema >= 4:
            missing_shot = M3_SHOT_COLUMNS.difference(row)
            if missing_shot:
                raise ValueError(
                    f"{path}: schema {schema} missing shot columns: "
                    f"{sorted(missing_shot)}"
                )
            _finite(row, "shotProgress")
            _finite(row, "shotDurationSeconds")
            _finite(row, "shotEndSlope")
        if schema >= 6:
            missing_bridge = M3_BRIDGE_COLUMNS.difference(row)
            if missing_bridge:
                raise ValueError(
                    f"{path}: schema {schema} missing bridge columns: "
                    f"{sorted(missing_bridge)}"
                )
            for key in M3_BRIDGE_COLUMNS.difference(
                {"bridgeOutgoingTarget", "bridgeIncomingTarget"}
            ):
                _finite(row, key)
        if schema >= 7:
            missing_terminal = M4_TERMINAL_COLUMNS.difference(row)
            if missing_terminal:
                raise ValueError(
                    f"{path}: schema {schema} missing M4 columns: "
                    f"{sorted(missing_terminal)}"
                )
            if row["endpointAuthority"] not in M4_ENDPOINT_AUTHORITIES:
                raise ValueError(
                    f"{path}: invalid endpointAuthority "
                    f"{row['endpointAuthority']!r}"
                )
        if schema >= 8:
            missing_formation = M6_FORMATION_COLUMNS.difference(row)
            if missing_formation:
                raise ValueError(
                    f"{path}: schema {schema} missing M6 columns: "
                    f"{sorted(missing_formation)}"
                )
            for key in M6_FORMATION_COLUMNS:
                if key.endswith("Actor"):
                    if (
                        row["interactionState"] in {"Launched", "TargetHit"}
                        and not row[key]
                    ):
                        raise ValueError(f"{path}: empty M6 actor identity {key}")
                else:
                    _finite(row, key)
        if schema >= 9:
            missing_environment = ENVIRONMENT_COLUMNS.difference(row)
            if missing_environment:
                raise ValueError(
                    f"{path}: schema {schema} missing environment columns: "
                    f"{sorted(missing_environment)}"
                )
            if not row["environmentStage"] or not row["environmentProfile"]:
                raise ValueError(f"{path}: empty environment stage/profile")
        row.setdefault("framingTarget", row["currentTarget"])
        row.setdefault("directorM3FrozenEnabled", "0")
        row.setdefault("stageProgress", "0")
        row.setdefault("stageDurationSeconds", "0")
        row.setdefault("shotPhase", "Authority")
        row.setdefault("shotReason", "LegacyAuthorityStage")
        row.setdefault("shotProgress", "0")
        row.setdefault("shotDurationSeconds", "0")
        row.setdefault("shotEndSlope", "0")
        row.setdefault("endpointAuthority", "None")
        if frame_index != expected_index:
            raise ValueError(
                f"{path}: non-contiguous frame {frame_index}; expected {expected_index}"
            )
        for key in OBSERVATION_FINGERPRINT_COLUMNS:
            if key not in row:
                raise ValueError(f"{path}: missing fingerprint column {key!r}")
            if key not in {
                "interactionState",
                "stage",
                "currentTarget",
                "framingTarget",
                "stageReason",
                "shotPhase",
                "shotReason",
            }:
                _finite(row, key)
    return rows


def analyze(path: pathlib.Path, thresholds: Thresholds) -> dict[str, object]:
    rows = read_rows(path)
    flight_rows = [
        row
        for row in rows
        if row["interactionState"] in {"Launched", "TargetHit"}
    ]
    if not flight_rows:
        raise ValueError(f"{path}: no Launched/TargetHit observation rows")
    observation_schema = int(_finite(flight_rows[0], "schemaVersion"))
    has_m4_terminal_schema = observation_schema >= 7
    has_m6_formation_schema = observation_schema >= 8

    bird_lost = [
        _finite(row, "birdVisibleRatio") < thresholds.bird_visible_ratio
        for row in flight_rows
    ]
    target_lost = [
        _finite(row, "targetVisibleRatio") <= thresholds.target_visible_ratio
        or _finite(row, "targetPixelRadius") < thresholds.target_pixel_radius
        for row in flight_rows
    ]
    empty = [bird and target for bird, target in zip(bird_lost, target_lost)]
    position_jump = [
        _finite(row, "cameraPositionDeltaCM")
        > thresholds.camera_position_delta_cm
        for row in flight_rows
    ]
    rotation_jump = [
        _finite(row, "cameraRotationDeltaDegrees")
        > thresholds.camera_rotation_delta_degrees
        for row in flight_rows
    ]
    fov_jump = [
        _finite(row, "fovDeltaDegrees") > thresholds.fov_delta_degrees
        for row in flight_rows
    ]

    has_director_schema = "directorBlendAlpha" in flight_rows[0]
    director_blends = [
        _finite(row, "directorBlendAlpha") if has_director_schema else 0.0
        for row in flight_rows
    ]
    m2_window = [
        row["currentTarget"] == "Assist1"
        and row["stage"] in {"CruiseToBody", "Approach", "Periapsis"}
        for row in flight_rows
    ]
    m3_window = [
        row["framingTarget"].startswith("Assist")
        and row["stage"]
        in {"CruiseToBody", "Handoff", "Approach", "Periapsis"}
        and row["shotPhase"] not in M4_TERMINAL_SHOT_PHASES
        for row in flight_rows
    ]
    m4_director_window = [
        has_m4_terminal_schema
        and (
            row["stage"] in M4_TERMINAL_STAGES
            or row["shotPhase"] in M4_TERMINAL_SHOT_PHASES
        )
        for row in flight_rows
    ]
    director_blended = [blend > 1.0e-9 for blend in director_blends]
    director_leak = [
        blended
        and not (
            (m3 or m4)
            if row["directorMode"] == "M3MultiAssist"
            else m2
        )
        for blended, m2, m3, m4, row in zip(
            director_blended,
            m2_window,
            m3_window,
            m4_director_window,
            flight_rows,
        )
    ]
    m2_indices = [index for index, value in enumerate(m2_window) if value]
    m3_indices = [index for index, value in enumerate(m3_window) if value]
    has_m3_shot_schema = observation_schema >= 4
    m3_shot_phase_counts = {
        phase: sum(row["shotPhase"] == phase for row in flight_rows)
        for phase in (
            "Authority",
            "OutgoingHold",
            "DualBodyBridge",
            "IncomingReveal",
            "IncomingTrack",
            "IncomingEntryMatch",
        )
    }
    m3_handoff_indices = [
        index
        for index, row in enumerate(flight_rows)
        if row["stage"] == "Handoff"
    ]
    bridge_indices = [
        index
        for index, row in enumerate(flight_rows)
        if row["shotPhase"] == "DualBodyBridge"
    ]
    bridge_both_visible = [
        _finite(flight_rows[index], "bridgeOutgoingVisibleRatio") > 0.01
        and _finite(flight_rows[index], "bridgeIncomingVisibleRatio") > 0.01
        for index in bridge_indices
    ] if observation_schema >= 6 else []
    cruise_indices = [
        index
        for index, row in enumerate(flight_rows)
        if row["currentTarget"] == "Assist1" and row["stage"] == "CruiseToBody"
    ]
    first_cruise_blend_index = next(
        (index for index in cruise_indices if director_blended[index]), None
    )
    first_cruise_visible_index = next(
        (index for index in cruise_indices if not target_lost[index]), None
    )
    first_cruise_fully_visible_index = next(
        (
            index
            for index in cruise_indices
            if _finite(flight_rows[index], "targetVisibleRatio") >= 0.99
            and _finite(flight_rows[index], "targetPixelRadius")
            >= thresholds.target_pixel_radius
        ),
        None,
    )
    approach_rows = [
        row
        for row in flight_rows
        if row["currentTarget"] == "Assist1" and row["stage"] == "Approach"
    ]
    periapsis_rows = [
        row
        for row in flight_rows
        if row["currentTarget"] == "Assist1" and row["stage"] == "Periapsis"
    ]
    approach_observable_rows = [
        row for row in approach_rows if _subjects_observable(row, thresholds)
    ]
    periapsis_observable_rows = [
        row for row in periapsis_rows if _subjects_observable(row, thresholds)
    ]
    approach_radii = [
        _finite(row, "targetPixelRadius") for row in approach_observable_rows
    ]
    periapsis_radii = [
        _finite(row, "targetPixelRadius") for row in periapsis_observable_rows
    ]
    approach_relative_x = [
        _finite(row, "birdScreenX") - _finite(row, "targetScreenX")
        for row in approach_observable_rows
    ]
    periapsis_relative_x = [
        _finite(row, "birdScreenX") - _finite(row, "targetScreenX")
        for row in periapsis_observable_rows
    ]
    transit_observable_rows = approach_observable_rows + periapsis_observable_rows
    transit_center_separation_radii = []
    transit_silhouette_overlap = []
    transit_bird_center_inside = []
    transit_foreground = []
    for row in transit_observable_rows:
        relative_x = _finite(row, "birdScreenX") - _finite(row, "targetScreenX")
        relative_y = _finite(row, "birdScreenY") - _finite(row, "targetScreenY")
        center_separation = math.hypot(relative_x, relative_y)
        target_radius = _finite(row, "targetPixelRadius")
        bird_radius = _finite(row, "birdPixelRadius")
        transit_center_separation_radii.append(center_separation / target_radius)
        transit_silhouette_overlap.append(
            center_separation < target_radius + bird_radius
        )
        transit_bird_center_inside.append(center_separation < target_radius)
        bird_depth = _finite(row, "birdDepthCM")
        target_depth = _finite(row, "targetDepthCM")
        transit_foreground.append(
            bird_depth > 0.0 and target_depth > bird_depth
        )
    inbound_outside_speeds: list[float] = []
    near_planet_speeds: list[float] = []
    for previous, current in zip(flight_rows, flight_rows[1:]):
        if (
            previous["currentTarget"] != "Assist1"
            or current["currentTarget"] != "Assist1"
            or not _subjects_observable(previous, thresholds)
            or not _subjects_observable(current, thresholds)
        ):
            continue
        delta_seconds = (
            _finite(current, "captureSeconds")
            - _finite(previous, "captureSeconds")
        )
        if delta_seconds <= 1.0e-9:
            continue
        previous_relative_x = (
            _finite(previous, "birdScreenX")
            - _finite(previous, "targetScreenX")
        )
        current_relative_x = (
            _finite(current, "birdScreenX")
            - _finite(current, "targetScreenX")
        )
        speed = abs(current_relative_x - previous_relative_x) / delta_seconds
        separations = []
        for row in (previous, current):
            relative_x = _finite(row, "birdScreenX") - _finite(
                row, "targetScreenX"
            )
            relative_y = _finite(row, "birdScreenY") - _finite(
                row, "targetScreenY"
            )
            separations.append(
                math.hypot(relative_x, relative_y)
                / _finite(row, "targetPixelRadius")
            )
        separation_radii = _mean(separations)
        relative_x = 0.5 * (previous_relative_x + current_relative_x)
        if separation_radii <= 1.0:
            near_planet_speeds.append(speed)
        if (
            previous["stage"] in {"CruiseToBody", "Approach"}
            and current["stage"] in {"CruiseToBody", "Approach"}
            and relative_x < 0.0
            and separation_radii >= 1.35
            and (
                not has_director_schema
                or (
                    _finite(previous, "directorBlendAlpha") >= 0.99
                    and _finite(current, "directorBlendAlpha") >= 0.99
                )
            )
        ):
            inbound_outside_speeds.append(speed)
    inbound_outside_median_speed = _median(inbound_outside_speeds)
    near_planet_median_speed = _median(near_planet_speeds)
    near_planet_speedup_ratio = (
        near_planet_median_speed / inbound_outside_median_speed
        if inbound_outside_median_speed > 1.0e-9 else 0.0
    )
    near_planet_speedup_observed = (
        len(inbound_outside_speeds) >= 3
        and len(near_planet_speeds) >= 3
        and near_planet_speedup_ratio >= 1.25
    )
    assist1_observable_rows = [
        row
        for row in flight_rows
        if row["currentTarget"] == "Assist1"
        and row["stage"] in {"CruiseToBody", "Approach", "Periapsis"}
        and _subjects_observable(row, thresholds)
    ]
    assist1_target_radii = [
        _finite(row, "targetPixelRadius") for row in assist1_observable_rows
    ]
    assist1_fovs = [_finite(row, "fovDegrees") for row in assist1_observable_rows]
    assist1_peak_target_radius = max(assist1_target_radii, default=0.0)
    approach_start_radius = _edge_mean(approach_radii, False)
    closest_scale_gain = (
        assist1_peak_target_radius / approach_start_radius
        if approach_start_radius > 1.0e-9 else 0.0
    )
    closest_lens_zoom_observed = (
        closest_scale_gain >= 1.75
        and min(assist1_fovs, default=180.0) <= 35.0
    )

    m3_assists: dict[str, object] = {}
    for assist_index in range(1, 4):
        label = f"Assist{assist_index}"
        assist_indices = [
            index
            for index, row in enumerate(flight_rows)
            if row["framingTarget"] == label
            and row["stage"]
            in {"CruiseToBody", "Handoff", "Approach", "Periapsis"}
            and row["shotPhase"] not in M4_TERMINAL_SHOT_PHASES
        ]
        directed_indices = [
            index
            for index in assist_indices
            if director_blended[index]
        ]
        assist_approach = [
            row
            for row in flight_rows
            if row["framingTarget"] == label
            and row["stage"] == "Approach"
            and _subjects_observable(row, thresholds)
        ]
        assist_periapsis = [
            row
            for row in flight_rows
            if row["framingTarget"] == label
            and row["stage"] == "Periapsis"
            and row["shotPhase"] not in M4_TERMINAL_SHOT_PHASES
            and _subjects_observable(row, thresholds)
        ]
        approach_x = [
            _finite(row, "birdScreenX") - _finite(row, "targetScreenX")
            for row in assist_approach
        ]
        approach_x_steps = [
            current - previous
            for previous, current in zip(approach_x, approach_x[1:])
        ]
        approach_backward_jumps = [
            step
            for step in approach_x_steps
            if step < -M3_APPROACH_BACKWARD_JUMP_PX_MAX
        ]
        running_maximum = -math.inf
        approach_backward_excursions = []
        for relative_x in approach_x:
            running_maximum = max(running_maximum, relative_x)
            approach_backward_excursions.append(running_maximum - relative_x)
        maximum_backward_excursion = max(
            approach_backward_excursions,
            default=0.0,
        )
        periapsis_x = [
            _finite(row, "birdScreenX") - _finite(row, "targetScreenX")
            for row in assist_periapsis
        ]
        left_mean = _edge_mean(approach_x, False)
        right_mean = _edge_mean(periapsis_x, True)
        m3_assists[label] = {
            "frameCount": len(assist_indices),
            "firstFramedFrame": (
                int(flight_rows[assist_indices[0]]["frameIndex"])
                if assist_indices else None
            ),
            "directorBlendFrames": len(directed_indices),
            "incomingShotFrames": sum(
                flight_rows[index]["shotPhase"]
                in {"IncomingReveal", "IncomingTrack", "IncomingEntryMatch"}
                for index in assist_indices
            ),
            "birdLostFrames": sum(bird_lost[index] for index in assist_indices),
            "targetLostFrames": sum(
                target_lost[index] for index in assist_indices
            ),
            "approachObservableFrames": len(assist_approach),
            "periapsisObservableFrames": len(assist_periapsis),
            "approachBirdRelativeXStartMean": left_mean,
            "periapsisBirdRelativeXEndMean": right_mean,
            "approachBackwardJumpFrames": len(approach_backward_jumps),
            "maximumApproachBackwardJumpPixels": abs(
                min(approach_backward_jumps, default=0.0)
            ),
            "maximumApproachBackwardExcursionPixels": (
                maximum_backward_excursion
            ),
            "leftToRightObserved": (
                bool(approach_x)
                and bool(periapsis_x)
                and left_mean < 0.0
                and right_mean > 0.0
            ),
        }

    m3_switches: list[dict[str, object]] = []
    for previous, current in zip(flight_rows, flight_rows[1:]):
        previous_target = previous["currentTarget"]
        current_target = current["currentTarget"]
        if (
            previous_target != current_target
            and previous_target.startswith("Assist")
            and current_target.startswith("Assist")
        ):
            m3_switches.append(
                {
                    "frame": int(current["frameIndex"]),
                    "from": previous_target,
                    "to": current_target,
                    "stage": current["stage"],
                    "framingTarget": current["framingTarget"],
                }
            )
    m3_handoff_target_lost = [
        target_lost[index] for index in m3_handoff_indices
    ]
    m3_handoff_preframe_count = sum(
        flight_rows[index]["currentTarget"]
        != flight_rows[index]["framingTarget"]
        for index in m3_handoff_indices
    )
    m3_all_assists_directed = all(
        int(m3_assists[f"Assist{assist_index}"]["directorBlendFrames"]) > 0
        for assist_index in range(1, 4)
    )
    m3_no_approach_reversal = all(
        float(
            m3_assists[f"Assist{assist_index}"][
                "maximumApproachBackwardExcursionPixels"
            ]
        )
        <= M3_APPROACH_BACKWARD_JUMP_PX_MAX
        for assist_index in range(1, 4)
    )
    first_cruise_blend_seconds = (
        _finite(flight_rows[first_cruise_blend_index], "playbackSeconds")
        if first_cruise_blend_index is not None
        else math.inf
    )
    first_cruise_visible_seconds = (
        _finite(flight_rows[first_cruise_visible_index], "playbackSeconds")
        if first_cruise_visible_index is not None
        else math.inf
    )
    first_cruise_fully_visible_seconds = (
        _finite(flight_rows[first_cruise_fully_visible_index], "playbackSeconds")
        if first_cruise_fully_visible_index is not None
        else math.inf
    )
    has_m3_launch_acquire_schema = observation_schema >= 5
    m3_first_body_acquisition_passed = (
        not has_m3_launch_acquire_schema
        or (
            m3_shot_phase_counts["IncomingReveal"] > 0
            and m3_shot_phase_counts["IncomingTrack"] > 0
            and first_cruise_blend_seconds <= M3_FIRST_BODY_BLEND_SECONDS_MAX
            and first_cruise_visible_seconds <= M3_FIRST_BODY_VISIBLE_SECONDS_MAX
            and first_cruise_fully_visible_seconds
            <= M3_FIRST_BODY_FULLY_VISIBLE_SECONDS_MAX
            and sum(bird_lost[index] for index in cruise_indices) == 0
        )
    )
    m3_shot_plan_passed = (
        not has_m3_shot_schema
        or (
            m3_shot_phase_counts["OutgoingHold"] > 0
            and m3_shot_phase_counts["IncomingReveal"] > 0
            and (
                observation_schema < 6
                or m3_shot_phase_counts["DualBodyBridge"] > 0
            )
            and (
                not has_m3_launch_acquire_schema
                or m3_shot_phase_counts["IncomingTrack"] > 0
            )
            and m3_shot_phase_counts["IncomingEntryMatch"] > 0
            and all(
                int(m3_assists[f"Assist{assist_index}"]["incomingShotFrames"])
                > 0
                for assist_index in range(
                    1 if has_m3_launch_acquire_schema else 2,
                    4,
                )
            )
            and m3_first_body_acquisition_passed
        )
    )
    bridge_transition_indices = [
        index
        for index, row in enumerate(flight_rows)
        if observation_schema >= 6
        and row["bridgeOutgoingTarget"].startswith("Assist")
        and row["bridgeIncomingTarget"].startswith("Assist")
    ]
    bridge_zero_planet_frames = sum(
        _finite(flight_rows[index], "bridgeOutgoingVisibleRatio") <= 0.01
        and _finite(flight_rows[index], "bridgeIncomingVisibleRatio") <= 0.01
        for index in bridge_transition_indices
    )
    m3_dual_body_bridge_passed = (
        observation_schema < 6
        or (
            len(bridge_indices) > 0
            and all(bridge_both_visible)
            and bridge_zero_planet_frames == 0
            and all(not bird_lost[index] for index in bridge_transition_indices)
            and min(
                (_finite(flight_rows[index], "birdPixelRadius")
                 for index in bridge_indices),
                default=0.0,
            ) >= 2.0
        )
    )
    m3_switches_only_in_handoff = (
        len(m3_switches) == 2
        and all(switch["stage"] == "Handoff" for switch in m3_switches)
    )
    m3_handoff_passed = (
        m3_all_assists_directed
        and m3_switches_only_in_handoff
        and m3_no_approach_reversal
        and m3_shot_plan_passed
        and sum(m3_handoff_target_lost) == 0
        and sum(bird_lost[index] for index in m3_indices) == 0
        and sum(position_jump[index] for index in m3_indices) == 0
        and sum(rotation_jump[index] for index in m3_indices) == 0
        and sum(fov_jump[index] for index in m3_indices) == 0
    )
    m4_terminal_indices = [
        index
        for index, row in enumerate(flight_rows)
        if has_m4_terminal_schema and row["stage"] in M4_TERMINAL_STAGES
    ]
    m4_acquire_indices = [
        index
        for index, row in enumerate(flight_rows)
        if has_m4_terminal_schema and row["shotPhase"] == "TerminalAcquire"
    ]
    m4_terminal_index_set = set(m4_terminal_indices)
    m4_acquire_no_target = [False] * len(flight_rows)
    for index in m4_acquire_indices:
        row = flight_rows[index]
        outgoing_visible = (
            _finite(row, "bridgeOutgoingVisibleRatio") > 0.01
            and _finite(row, "bridgeOutgoingPixelRadius")
            >= thresholds.target_pixel_radius
        )
        incoming_visible = (
            _finite(row, "bridgeIncomingVisibleRatio") > 0.01
            and _finite(row, "bridgeIncomingPixelRadius")
            >= thresholds.target_pixel_radius
        )
        m4_acquire_no_target[index] = not (
            outgoing_visible or incoming_visible
        )
    m4_bird_lost = [
        index in m4_terminal_index_set
        and (bird_lost[index] or _finite(row, "birdPixelRadius") < 1.0)
        for index, row in enumerate(flight_rows)
    ]
    m4_target_lost = [
        index in m4_terminal_index_set and target_lost[index]
        for index in range(len(flight_rows))
    ]
    m4_endpoint_missing = [
        index in m4_terminal_index_set and row["endpointAuthority"] == "None"
        for index, row in enumerate(flight_rows)
    ]
    m4_position_jump = [
        index in m4_terminal_index_set and position_jump[index]
        for index in range(len(flight_rows))
    ]
    m4_rotation_jump = [
        index in m4_terminal_index_set and rotation_jump[index]
        for index in range(len(flight_rows))
    ]
    m4_fov_jump = [
        index in m4_terminal_index_set and fov_jump[index]
        for index in range(len(flight_rows))
    ]
    m4_authorities = sorted(
        {
            flight_rows[index]["endpointAuthority"]
            for index in m4_terminal_indices
            if flight_rows[index]["endpointAuthority"] != "None"
        }
    )
    m4_offline_camera_closure_passed = (
        not has_m4_terminal_schema
        or (
            len(m4_terminal_indices) > 0
            and len(m4_acquire_indices) > 0
            and sum(m4_acquire_no_target) == 0
            and sum(m4_bird_lost) == 0
            and sum(m4_target_lost) == 0
            and sum(m4_endpoint_missing) == 0
            and sum(m4_position_jump) == 0
            and sum(m4_rotation_jump) == 0
            and sum(m4_fov_jump) == 0
        )
    )
    m6_order_mismatch = [
        has_m6_formation_schema
        and int(_finite(row, "formationOrderStable")) == 0
        for row in flight_rows
    ]
    m6_primary_mismatch = [
        has_m6_formation_schema
        and int(_finite(row, "formationPrimaryAnchored")) == 0
        for row in flight_rows
    ]
    m6_lost = [
        has_m6_formation_schema
        and any(
            _finite(row, f"formation{index}VisibleRatio")
            < thresholds.bird_visible_ratio
            for index in range(4)
        )
        for row in flight_rows
    ]
    m6_deployed_rows = [
        row for row in flight_rows
        if has_m6_formation_schema
        and int(_finite(row, "formationFullyDeployed")) != 0
    ]
    m6_spacing_mismatch_count = sum(
        spacing + 1.0e-3 < _finite(row, "formationExpectedSpacingCM") * 0.95
        for row in m6_deployed_rows
        for spacing in (
            _finite(row, "formationSpacing01CM"),
            _finite(row, "formationSpacing12CM"),
            _finite(row, "formationSpacing23CM"),
        )
    )
    m6_formation_passed = (
        not has_m6_formation_schema
        or (
            bool(m6_deployed_rows)
            and sum(m6_order_mismatch) == 0
            and sum(m6_primary_mismatch) == 0
            and sum(m6_lost) == 0
            and m6_spacing_mismatch_count == 0
        )
    )
    approach_left_mean = _edge_mean(approach_relative_x, False)
    periapsis_right_mean = _edge_mean(periapsis_relative_x, True)
    bird_motion_maxima = _derivative_maxima(
        approach_rows + periapsis_rows, "birdScreenX"
    )
    target_scale_maxima = _derivative_maxima(
        approach_rows + periapsis_rows, "targetPixelRadius"
    )
    # Thirty samples on either side of the stage boundary form a two-second
    # closest-approach window at the capture contract's 30 fps. Unlike the
    # whole encounter maxima, this window is not polluted when the target
    # deliberately leaves frame near Assist1 Exit.
    closest_rows = (approach_rows[-30:] + periapsis_rows[:30])
    closest_bird_motion_maxima = _derivative_maxima(
        closest_rows, "birdScreenX"
    )
    closest_target_scale_maxima = _derivative_maxima(
        closest_rows, "targetPixelRadius"
    )

    windows: list[dict[str, object]] = []
    start = 0
    for index in range(1, len(flight_rows) + 1):
        boundary = index == len(flight_rows)
        if not boundary:
            previous = flight_rows[start]
            current = flight_rows[index]
            boundary = (
                previous["stage"] != current["stage"]
                or previous["currentTarget"] != current["currentTarget"]
            )
        if boundary:
            group = flight_rows[start:index]
            target_radii = [_finite(row, "targetPixelRadius") for row in group]
            windows.append(
                {
                    "stage": group[0]["stage"],
                    "target": group[0]["currentTarget"],
                    "reason": group[0]["stageReason"],
                    "firstFrame": int(group[0]["frameIndex"]),
                    "lastFrame": int(group[-1]["frameIndex"]),
                    "frameCount": len(group),
                    "targetPixelRadiusMin": min(target_radii),
                    "targetPixelRadiusMax": max(target_radii),
                    "birdVisibleRatioMin": min(
                        _finite(row, "birdVisibleRatio") for row in group
                    ),
                    "targetVisibleRatioMin": min(
                        _finite(row, "targetVisibleRatio") for row in group
                    ),
                }
            )
            start = index

    failures = {
        "birdLostFrames": sum(bird_lost),
        "targetLostFrames": sum(target_lost),
        "emptyCompositionFrames": sum(empty),
        "cameraPositionJumpFrames": sum(position_jump),
        "cameraRotationJumpFrames": sum(rotation_jump),
        "fovJumpFrames": sum(fov_jump),
    }
    passed = (
        all(value == 0 for value in failures.values())
        and m4_offline_camera_closure_passed
        and m6_formation_passed
    )
    decision_fingerprint_columns = DECISION_FINGERPRINT_COLUMNS
    if has_m4_terminal_schema:
        decision_fingerprint_columns += ("endpointAuthority",)
    return {
        "schemaVersion": 1,
        "observationSchemaVersion": int(_finite(rows[0], "schemaVersion")),
        "sourceCsv": str(path.resolve()),
        "criteriaPassed": passed,
        "assessment": "Pass" if passed else "BaselineFailureObserved",
        "frameCount": len(rows),
        "flightFrameCount": len(flight_rows),
        # Orthogonality is assessed only over authoritative flight samples.
        # Fresh processes may spend a different number of frames waiting before
        # release, and render-thread timing may perturb presentation floats. The
        # raw observation fingerprint keeps those diagnostics visible without
        # letting them masquerade as a stage-decision change.
        "decisionFingerprintSha256": _fingerprint(
            flight_rows, decision_fingerprint_columns
        ),
        "criteriaFingerprintSha256": _criteria_fingerprint(
            flight_rows,
            bird_lost,
            target_lost,
            position_jump,
            rotation_jump,
            fov_jump,
            m4_acquire_no_target if has_m4_terminal_schema else None,
            m4_endpoint_missing if has_m4_terminal_schema else None,
        ),
        "observationFingerprintSha256": _fingerprint(
            flight_rows, OBSERVATION_FINGERPRINT_COLUMNS
        ),
        "thresholds": {
            "birdVisibleRatioMin": thresholds.bird_visible_ratio,
            "targetVisibleRatioMinExclusive": thresholds.target_visible_ratio,
            "targetPixelRadiusMin": thresholds.target_pixel_radius,
            "cameraPositionDeltaCMMax": thresholds.camera_position_delta_cm,
            "cameraRotationDeltaDegreesMax": thresholds.camera_rotation_delta_degrees,
            "fovDeltaDegreesMax": thresholds.fov_delta_degrees,
        },
        **failures,
        "longestBirdLostRun": _longest_run(bird_lost),
        "longestTargetLostRun": _longest_run(target_lost),
        "longestEmptyCompositionRun": _longest_run(empty),
        "maximumCameraPositionDeltaCM": max(
            _finite(row, "cameraPositionDeltaCM") for row in flight_rows
        ),
        "maximumCameraRotationDeltaDegrees": max(
            _finite(row, "cameraRotationDeltaDegrees") for row in flight_rows
        ),
        "maximumFovDeltaDegrees": max(
            _finite(row, "fovDeltaDegrees") for row in flight_rows
        ),
        "m4Terminal": {
            "schemaAvailable": has_m4_terminal_schema,
            "terminalFrames": len(m4_terminal_indices),
            "acquireFrames": len(m4_acquire_indices),
            "acquireNoTargetFrames": sum(m4_acquire_no_target),
            "birdLostFrames": sum(m4_bird_lost),
            "ufoLostFrames": sum(m4_target_lost),
            "endpointMissingFrames": sum(m4_endpoint_missing),
            "positionJumpFrames": sum(m4_position_jump),
            "rotationJumpFrames": sum(m4_rotation_jump),
            "fovJumpFrames": sum(m4_fov_jump),
            "endpointAuthorities": m4_authorities,
            "offlineCameraClosurePassed": m4_offline_camera_closure_passed,
            "physicalContactAssessment": (
                "ManifestAuthorityRequired"
                if has_m4_terminal_schema
                else "SchemaUnavailable"
            ),
        },
        "m6Formation": {
            "schemaAvailable": has_m6_formation_schema,
            "fullyDeployedFrames": len(m6_deployed_rows),
            "lostFrames": sum(m6_lost),
            "orderMismatchFrames": sum(m6_order_mismatch),
            "primaryMismatchFrames": sum(m6_primary_mismatch),
            "spacingMismatchCount": m6_spacing_mismatch_count,
            "minimumAdjacentSpacingCM": min(
                (
                    spacing
                    for row in m6_deployed_rows
                    for spacing in (
                        _finite(row, "formationSpacing01CM"),
                        _finite(row, "formationSpacing12CM"),
                        _finite(row, "formationSpacing23CM"),
                    )
                ),
                default=0.0,
            ),
            "minimumPixelRadius": min(
                (
                    _finite(row, f"formation{index}PixelRadius")
                    for row in flight_rows
                    for index in range(4)
                ),
                default=0.0,
            ),
            "passed": m6_formation_passed,
        },
        "director": {
            "schemaAvailable": has_director_schema,
            "m2FrozenEnabledFrames": sum(
                int(_finite(row, "directorM2FrozenEnabled")) != 0
                for row in flight_rows
            ) if has_director_schema else 0,
            "m3FrozenEnabledFrames": sum(
                int(_finite(row, "directorM3FrozenEnabled")) != 0
                for row in flight_rows
            ) if has_director_schema else 0,
            "blendFrames": sum(director_blended),
            "leakFrames": sum(director_leak),
            "maximumBlendAlpha": max(director_blends),
            "m2Assist1Frames": len(m2_indices),
            "m2Assist1BirdLostFrames": sum(bird_lost[index] for index in m2_indices),
            "m2Assist1TargetLostFrames": sum(
                target_lost[index] for index in m2_indices
            ),
            "m3WindowFrames": len(m3_indices),
            "m3WindowBirdLostFrames": sum(
                bird_lost[index] for index in m3_indices
            ),
            "m3WindowTargetLostFrames": sum(
                target_lost[index] for index in m3_indices
            ),
            "m3HandoffFrames": len(m3_handoff_indices),
            "m3HandoffPreframeFrames": m3_handoff_preframe_count,
            "m3HandoffTargetLostFrames": sum(m3_handoff_target_lost),
            "m3HandoffLongestTargetLostRun": _longest_run(
                m3_handoff_target_lost
            ),
            "m3AllAssistsDirected": m3_all_assists_directed,
            "m3AssistSwitchesOnlyInHandoff": m3_switches_only_in_handoff,
            "m3NoApproachReversal": m3_no_approach_reversal,
            "m3ShotSchemaAvailable": has_m3_shot_schema,
            "m3LaunchAcquireSchemaAvailable": has_m3_launch_acquire_schema,
            "m3ShotPhaseCounts": m3_shot_phase_counts,
            "m3FirstBodyAcquisitionPassed": m3_first_body_acquisition_passed,
            "m3ShotPlanPassed": m3_shot_plan_passed,
            "m3DualBodyBridgeSchemaAvailable": observation_schema >= 6,
            "m3DualBodyBridgeFrames": len(bridge_indices),
            "m3DualBodyBridgeBothVisibleFrames": sum(bridge_both_visible),
            "m3BridgeZeroPlanetFrames": bridge_zero_planet_frames,
            "m3DualBodyBridgePassed": m3_dual_body_bridge_passed,
            "m3AssistSwitches": m3_switches,
            "m3AssistMetrics": m3_assists,
            "m3HandoffPassed": m3_handoff_passed,
            "cruiseBlendFrames": sum(
                director_blended[index] for index in cruise_indices
            ),
            "cruiseTargetLostFrames": sum(
                target_lost[index] for index in cruise_indices
            ),
            "firstCruiseBlendFrame": (
                int(flight_rows[first_cruise_blend_index]["frameIndex"])
                if first_cruise_blend_index is not None else None
            ),
            "firstCruiseTargetVisibleFrame": (
                int(flight_rows[first_cruise_visible_index]["frameIndex"])
                if first_cruise_visible_index is not None else None
            ),
            "firstCruiseTargetFullyVisibleFrame": (
                int(flight_rows[first_cruise_fully_visible_index]["frameIndex"])
                if first_cruise_fully_visible_index is not None else None
            ),
            "firstCruiseBlendPlaybackSeconds": (
                first_cruise_blend_seconds
                if math.isfinite(first_cruise_blend_seconds) else None
            ),
            "firstCruiseTargetVisiblePlaybackSeconds": (
                first_cruise_visible_seconds
                if math.isfinite(first_cruise_visible_seconds) else None
            ),
            "firstCruiseTargetFullyVisiblePlaybackSeconds": (
                first_cruise_fully_visible_seconds
                if math.isfinite(first_cruise_fully_visible_seconds) else None
            ),
            "approachTargetRadiusStartMean": _edge_mean(
                approach_radii, False
            ),
            "approachTargetRadiusEndMean": _edge_mean(approach_radii, True),
            "periapsisTargetRadiusStartMean": _edge_mean(
                periapsis_radii, False
            ),
            "periapsisTargetRadiusEndMean": _edge_mean(
                periapsis_radii, True
            ),
            "approachBirdRelativeXStartMean": approach_left_mean,
            "periapsisBirdRelativeXEndMean": periapsis_right_mean,
            "approachObservableFrames": len(approach_observable_rows),
            "periapsisObservableFrames": len(periapsis_observable_rows),
            "assist1LeftToRightNetPixels": (
                periapsis_right_mean - approach_left_mean
            ),
            "assist1LeftToRightObserved": (
                bool(approach_relative_x)
                and bool(periapsis_relative_x)
                and approach_left_mean < 0.0
                and periapsis_right_mean > 0.0
            ),
            "foregroundTransitObservableFrames": len(transit_observable_rows),
            "foregroundTransitBirdCenterInsideFrames": sum(
                transit_bird_center_inside
            ),
            "foregroundTransitSilhouetteOverlapFrames": sum(
                transit_silhouette_overlap
            ),
            "foregroundTransitLongestSilhouetteOverlapRun": _longest_run(
                transit_silhouette_overlap
            ),
            "foregroundTransitBirdInFrontFrames": sum(transit_foreground),
            "foregroundTransitMinimumCenterSeparationRadii": (
                min(transit_center_separation_radii)
                if transit_center_separation_radii else 0.0
            ),
            "foregroundTransitObserved": (
                bool(approach_relative_x)
                and bool(periapsis_relative_x)
                and approach_left_mean < 0.0
                and periapsis_right_mean > 0.0
                and sum(transit_bird_center_inside) >= 3
                and _longest_run(transit_silhouette_overlap) >= 3
                and all(transit_foreground)
            ),
            "inboundOutsideSpeedSampleCount": len(inbound_outside_speeds),
            "inboundOutsideMedianBirdRelativeXSpeedPxPerSec": inbound_outside_median_speed,
            "nearPlanetSpeedSampleCount": len(near_planet_speeds),
            "nearPlanetMedianBirdRelativeXSpeedPxPerSec": near_planet_median_speed,
            "nearPlanetScreenSpeedupRatio": near_planet_speedup_ratio,
            "nearPlanetScreenSpeedupObserved": near_planet_speedup_observed,
            "assist1PeakTargetPixelRadius": assist1_peak_target_radius,
            "assist1MinimumFovDegrees": min(assist1_fovs, default=0.0),
            "assist1MaximumFovDegrees": max(assist1_fovs, default=0.0),
            "closestScaleGainFromApproachStart": closest_scale_gain,
            "closestLensZoomObserved": closest_lens_zoom_observed,
            "lucyPacingObserved": (
                near_planet_speedup_observed and closest_lens_zoom_observed
            ),
            "maximumBirdScreenXVelocityPxPerSec": bird_motion_maxima[0],
            "maximumBirdScreenXAccelerationPxPerSec2": bird_motion_maxima[1],
            "maximumBirdScreenXJerkPxPerSec3": bird_motion_maxima[2],
            "maximumTargetRadiusVelocityPxPerSec": target_scale_maxima[0],
            "maximumTargetRadiusAccelerationPxPerSec2": target_scale_maxima[1],
            "maximumTargetRadiusJerkPxPerSec3": target_scale_maxima[2],
            "closestWindowFrameCount": len(closest_rows),
            "closestMaximumBirdScreenXVelocityPxPerSec": closest_bird_motion_maxima[0],
            "closestMaximumBirdScreenXAccelerationPxPerSec2": closest_bird_motion_maxima[1],
            "closestMaximumBirdScreenXJerkPxPerSec3": closest_bird_motion_maxima[2],
            "closestMaximumTargetRadiusVelocityPxPerSec": closest_target_scale_maxima[0],
            "closestMaximumTargetRadiusAccelerationPxPerSec2": closest_target_scale_maxima[1],
            "closestMaximumTargetRadiusJerkPxPerSec3": closest_target_scale_maxima[2],
        },
        "stageWindows": windows,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--require-pass", action="store_true")
    parser.add_argument(
        "--comparison-mode",
        choices=("orthogonality", "director-ab"),
        default="orthogonality",
    )
    parser.add_argument("--bird-visible-ratio", type=float, default=0.5)
    parser.add_argument("--target-visible-ratio", type=float, default=0.01)
    parser.add_argument("--target-pixel-radius", type=float, default=4.0)
    parser.add_argument("--camera-position-delta-cm", type=float, default=5000.0)
    parser.add_argument("--camera-rotation-delta-degrees", type=float, default=15.0)
    parser.add_argument("--fov-delta-degrees", type=float, default=2.0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    thresholds = Thresholds(
        bird_visible_ratio=args.bird_visible_ratio,
        target_visible_ratio=args.target_visible_ratio,
        target_pixel_radius=args.target_pixel_radius,
        camera_position_delta_cm=args.camera_position_delta_cm,
        camera_rotation_delta_degrees=args.camera_rotation_delta_degrees,
        fov_delta_degrees=args.fov_delta_degrees,
    )
    try:
        reports = [analyze(path, thresholds) for path in args.inputs]
    except (OSError, ValueError) as exc:
        print(f"M11 camera observation analysis failed: {exc}", file=sys.stderr)
        return 1

    decision_fingerprints = [
        report["decisionFingerprintSha256"] for report in reports
    ]
    criteria_fingerprints = [
        report["criteriaFingerprintSha256"] for report in reports
    ]
    decisions_equal = len(set(decision_fingerprints)) == 1
    criteria_equal = len(set(criteria_fingerprints)) == 1
    orthogonality = {
        "inputCount": len(reports),
        "allDecisionFingerprintsEqual": decisions_equal,
        "allCriteriaFingerprintsEqual": criteria_equal,
        "allIdentityFingerprintsEqual": decisions_equal and criteria_equal,
        "decisionFingerprints": decision_fingerprints,
        "criteriaFingerprints": criteria_fingerprints,
        "observationFingerprints": [
            report["observationFingerprintSha256"] for report in reports
        ],
    }
    director_ab: dict[str, object] | None = None
    if args.comparison_mode == "director-ab":
        if len(reports) != 2:
            print(
                "M11 director A/B comparison requires exactly two inputs",
                file=sys.stderr,
            )
            return 1
        baseline = reports[0]
        directed = reports[1]
        baseline_director = baseline["director"]
        directed_director = directed["director"]
        director_ab = {
            "decisionFingerprintEqual": decisions_equal,
            "baselineBlendFramesZero": baseline_director["blendFrames"] == 0,
            "directedBlendFramesPositive": directed_director["blendFrames"] > 0,
            "directedLeakFramesZero": directed_director["leakFrames"] == 0,
            "directedBirdLostFramesZero": (
                directed_director["m2Assist1BirdLostFrames"] == 0
            ),
            "targetLostFramesImproved": (
                directed_director["m2Assist1TargetLostFrames"]
                < baseline_director["m2Assist1TargetLostFrames"]
            ),
            "baselineM2TargetLostFrames": (
                baseline_director["m2Assist1TargetLostFrames"]
            ),
            "directedM2TargetLostFrames": (
                directed_director["m2Assist1TargetLostFrames"]
            ),
            "directedCameraJumpFrames": (
                directed["cameraPositionJumpFrames"]
                + directed["cameraRotationJumpFrames"]
                + directed["fovJumpFrames"]
            ),
        }
        director_ab["passed"] = all(
            value
            for key, value in director_ab.items()
            if key
            not in {
                "baselineM2TargetLostFrames",
                "directedM2TargetLostFrames",
                "directedCameraJumpFrames",
            }
        ) and director_ab["directedCameraJumpFrames"] == 0
    output: dict[str, object] = {
        "schemaVersion": 1,
        "reports": reports,
        "comparisonMode": args.comparison_mode,
        "orthogonalityComparison": orthogonality,
    }
    if director_ab is not None:
        output["directorABComparison"] = director_ab
    output_path = args.output
    if output_path is None:
        first = args.inputs[0]
        output_path = first.with_name(first.stem + ".camera-report.json")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(output, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(output_path.resolve())
    comparison_passed = (
        director_ab["passed"]
        if director_ab is not None
        else (
            all(report["criteriaPassed"] for report in reports)
            and orthogonality["allIdentityFingerprintsEqual"]
        )
    )
    if args.require_pass and not comparison_passed:
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
