#!/usr/bin/env python3
"""Regression tests for M11 camera observation schema compatibility."""

from __future__ import annotations

import csv
import importlib.util
import pathlib
import sys
import tempfile
import unittest


MODULE_PATH = pathlib.Path(__file__).with_name("analyze_camera_observations.py")
SPEC = importlib.util.spec_from_file_location("m11_camera_analysis", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"unable to load {MODULE_PATH}")
ANALYSIS = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = ANALYSIS
SPEC.loader.exec_module(ANALYSIS)

CSV_COLUMNS = (
    "schemaVersion",
    "frameIndex",
    "captureSeconds",
    "playbackSeconds",
    "interactionState",
    "stage",
    "currentTarget",
    "framingTarget",
    "stageReason",
    "endpointAuthority",
    "stageProgress",
    "stageDurationSeconds",
    "shotPhase",
    "shotReason",
    "shotProgress",
    "shotDurationSeconds",
    "shotEndSlope",
    "directorMode",
    "directorM2FrozenEnabled",
    "directorM3FrozenEnabled",
    "directorBlendAlpha",
    "birdWorldX",
    "birdWorldY",
    "birdWorldZ",
    "birdScreenX",
    "birdScreenY",
    "birdDepthCM",
    "birdPixelRadius",
    "birdVisibleRatio",
    "targetWorldX",
    "targetWorldY",
    "targetWorldZ",
    "targetScreenX",
    "targetScreenY",
    "targetDepthCM",
    "targetPixelRadius",
    "targetVisibleRatio",
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
    "cameraWorldX",
    "cameraWorldY",
    "cameraWorldZ",
    "cameraPitch",
    "cameraYaw",
    "cameraRoll",
    "cameraToBirdCM",
    "cameraToTargetCM",
    "fovDegrees",
    "cameraPositionDeltaCM",
    "cameraRotationDeltaDegrees",
    "fovDeltaDegrees",
)


def _row(frame_index: int, **overrides: object) -> dict[str, str]:
    row = {column: "0" for column in CSV_COLUMNS}
    row.update(
        {
            "schemaVersion": "7",
            "frameIndex": str(frame_index),
            "captureSeconds": f"{frame_index / 30.0:.9f}",
            "playbackSeconds": f"{frame_index / 30.0:.9f}",
            "interactionState": "Launched",
            "stage": "Periapsis",
            "currentTarget": "Assist3",
            "framingTarget": "Assist3",
            "stageReason": "Assist3ClosestApproach",
            "endpointAuthority": "PhysicalContact",
            "shotPhase": "Authority",
            "shotReason": "LucyAuthority",
            "directorMode": "M3MultiAssist",
            "directorM3FrozenEnabled": "1",
            "directorBlendAlpha": "1",
            "birdWorldX": "100",
            "birdWorldY": "200",
            "birdWorldZ": "300",
            "birdScreenX": "500",
            "birdScreenY": "400",
            "birdDepthCM": "1000",
            "birdPixelRadius": "12",
            "birdVisibleRatio": "1",
            "targetWorldX": "800",
            "targetWorldY": "900",
            "targetWorldZ": "1000",
            "targetScreenX": "640",
            "targetScreenY": "360",
            "targetDepthCM": "2000",
            "targetPixelRadius": "40",
            "targetVisibleRatio": "1",
            "bridgeOutgoingTarget": "None",
            "bridgeIncomingTarget": "None",
            "cameraWorldX": "10",
            "cameraWorldY": "20",
            "cameraWorldZ": "30",
            "cameraToBirdCM": "1000",
            "cameraToTargetCM": "2000",
            "fovDegrees": "55",
        }
    )
    row.update({key: str(value) for key, value in overrides.items()})
    return row


class CameraObservationSchemaTest(unittest.TestCase):
    def _write_csv(
        self,
        rows: list[dict[str, str]],
        columns: tuple[str, ...] = CSV_COLUMNS,
    ) -> pathlib.Path:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = pathlib.Path(directory.name) / "observations.csv"
        with path.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=columns)
            writer.writeheader()
            writer.writerows(
                {column: row[column] for column in columns} for row in rows
            )
        return path

    def test_schema_7_terminal_metrics_do_not_pollute_m3(self) -> None:
        rows = [
            _row(
                0,
                shotPhase="TerminalAcquire",
                shotReason="Assist3ToUFOAcquire",
                bridgeOutgoingTarget="Assist3",
                bridgeOutgoingPixelRadius="40",
                bridgeOutgoingVisibleRatio="1",
                bridgeIncomingTarget="UFO",
            ),
            _row(
                1,
                stage="FinalApproach",
                currentTarget="UFO",
                framingTarget="UFO",
                stageReason="PhysicalContactEndpoint",
                shotPhase="TerminalTrack",
                shotReason="UFOFinalApproach",
            ),
            _row(
                2,
                interactionState="TargetHit",
                stage="Terminal",
                currentTarget="UFO",
                framingTarget="UFO",
                stageReason="PhysicalContactEndpoint",
                shotPhase="TerminalTrack",
                shotReason="UFOContactHold",
            ),
        ]
        report = ANALYSIS.analyze(
            self._write_csv(rows), ANALYSIS.Thresholds()
        )

        self.assertEqual(report["observationSchemaVersion"], 7)
        self.assertEqual(report["director"]["leakFrames"], 0)
        self.assertEqual(report["director"]["m3BridgeZeroPlanetFrames"], 0)
        self.assertEqual(report["director"]["m3DualBodyBridgeFrames"], 0)
        self.assertEqual(report["m4Terminal"]["terminalFrames"], 2)
        self.assertEqual(report["m4Terminal"]["acquireFrames"], 1)
        self.assertTrue(
            report["m4Terminal"]["offlineCameraClosurePassed"]
        )
        self.assertTrue(report["criteriaPassed"])

    def test_schema_7_missing_endpoint_column_fails_closed(self) -> None:
        columns = tuple(
            column for column in CSV_COLUMNS if column != "endpointAuthority"
        )
        with self.assertRaisesRegex(ValueError, "missing M4 columns"):
            ANALYSIS.read_rows(self._write_csv([_row(0)], columns))

    def test_schema_7_invalid_endpoint_authority_fails_closed(self) -> None:
        path = self._write_csv([_row(0, endpointAuthority="QualifiedMaybe")])
        with self.assertRaisesRegex(ValueError, "invalid endpointAuthority"):
            ANALYSIS.read_rows(path)

    def test_schema_6_remains_compatible_without_endpoint_column(self) -> None:
        columns = tuple(
            column for column in CSV_COLUMNS if column != "endpointAuthority"
        )
        rows = [_row(0, schemaVersion="6")]
        path = self._write_csv(rows, columns)
        parsed = ANALYSIS.read_rows(path)
        report = ANALYSIS.analyze(path, ANALYSIS.Thresholds())

        self.assertEqual(parsed[0]["schemaVersion"], "6")
        self.assertEqual(parsed[0]["endpointAuthority"], "None")
        self.assertFalse(report["m4Terminal"]["schemaAvailable"])
        self.assertEqual(report["m4Terminal"]["terminalFrames"], 0)

    def test_schema_7_acquire_without_visible_subject_fails_criterion(self) -> None:
        rows = [
            _row(
                0,
                shotPhase="TerminalAcquire",
                bridgeOutgoingTarget="Assist3",
                bridgeIncomingTarget="UFO",
            ),
            _row(
                1,
                stage="FinalApproach",
                currentTarget="UFO",
                framingTarget="UFO",
                shotPhase="TerminalTrack",
            ),
        ]
        report = ANALYSIS.analyze(
            self._write_csv(rows), ANALYSIS.Thresholds()
        )

        self.assertEqual(report["m4Terminal"]["acquireNoTargetFrames"], 1)
        self.assertFalse(
            report["m4Terminal"]["offlineCameraClosurePassed"]
        )
        self.assertFalse(report["criteriaPassed"])


if __name__ == "__main__":
    unittest.main()
