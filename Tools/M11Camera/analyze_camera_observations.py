#!/usr/bin/env python3
"""Offline M11 camera-observation criteria and orthogonality comparison.

This tool consumes only the contract-v4 CSV emitted by the M11 capture runner.
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


SUPPORTED_SCHEMA_VERSIONS = {1, 2}
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
    "stageReason",
)
DIRECTOR_COLUMNS = {
    "directorMode",
    "directorM2FrozenEnabled",
    "directorBlendAlpha",
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
                "stageReason",
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
) -> str:
    digest = hashlib.sha256()
    for index, row in enumerate(rows):
        canonical = (
            row["playbackSeconds"],
            row["stage"],
            row["currentTarget"],
            "1" if bird_lost[index] else "0",
            "1" if target_lost[index] else "0",
            "1" if position_jump[index] else "0",
            "1" if rotation_jump[index] else "0",
            "1" if fov_jump[index] else "0",
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
                "stageReason",
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
    director_blended = [blend > 1.0e-9 for blend in director_blends]
    director_leak = [
        blended and not window
        for blended, window in zip(director_blended, m2_window)
    ]
    m2_indices = [index for index, value in enumerate(m2_window) if value]
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
    passed = all(value == 0 for value in failures.values())
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
            flight_rows, DECISION_FINGERPRINT_COLUMNS
        ),
        "criteriaFingerprintSha256": _criteria_fingerprint(
            flight_rows,
            bird_lost,
            target_lost,
            position_jump,
            rotation_jump,
            fov_jump,
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
        "director": {
            "schemaAvailable": has_director_schema,
            "m2FrozenEnabledFrames": sum(
                int(_finite(row, "directorM2FrozenEnabled")) != 0
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
