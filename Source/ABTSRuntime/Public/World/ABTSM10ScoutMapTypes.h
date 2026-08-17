// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ABTSM10ScoutMapTypes.generated.h"

class UTexture2D;

UENUM()
enum class EABTSM10ScoutMarkerType : uint8
{
	Tree,
	Stone,
	Building
};

/** Editor-facing M10 rendering and sampling settings shared by the world system and HUD. */
USTRUCT(BlueprintType)
struct FABTSM10ScoutMapSettings
{
	GENERATED_BODY()

	/** Arc distance divided by primary-planet radius. 0.25 means radius / 4. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coverage", meta = (ClampMin = "0.01", ClampMax = "3.0"))
	float ScoutRadiusPrimaryRatio = 0.25f;

	/** Optional explicit arc radius in cm. Zero keeps ScoutRadiusPrimaryRatio authoritative. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coverage", meta = (ClampMin = "0.0", UIMax = "50000.0", Units = "cm"))
	float ScoutRadiusOverrideCM = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "64", ClampMax = "512"))
	int32 TerrainTextureResolution = 192;

	/** Environment icons update at this cadence; bird portraits remain frame-accurate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Markers", meta = (ClampMin = "0.02", ClampMax = "2.0", Units = "s"))
	float EnvironmentRefreshIntervalSeconds = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Markers", meta = (ClampMin = "64", ClampMax = "10000"))
	int32 MaximumEnvironmentMarkerCount = 1024;

	/** Extra radial-height allowance used by the HISM spherical-cap broadphase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Markers", meta = (ClampMin = "0.0", UIMax = "5000.0", Units = "cm"))
	float EnvironmentBroadphasePaddingCM = 1400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "120.0", ClampMax = "700.0"))
	float MapDiameterPx = 310.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0.0", ClampMax = "200.0"))
	float TopLeftMarginPx = 28.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	TObjectPtr<UTexture2D> TreeIconTexture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons", meta = (ClampMin = "2.0", ClampMax = "96.0"))
	float TreeIconSizePx = 11.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	TObjectPtr<UTexture2D> StoneIconTexture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons", meta = (ClampMin = "2.0", ClampMax = "96.0"))
	float StoneIconSizePx = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	TObjectPtr<UTexture2D> BuildingIconTexture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons", meta = (ClampMin = "4.0", ClampMax = "128.0"))
	float BuildingIconSizePx = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons", meta = (ClampMin = "6.0", ClampMax = "128.0"))
	float BirdIconSizePx = 28.0f;

	/** Shows the active slingshot prediction when its landing lies inside this scout snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Preview")
	bool bShowReinforcedTrajectoryPreview = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Preview", meta = (ClampMin = "1.0", ClampMax = "40.0"))
	float TrajectoryDashLengthPx = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Preview", meta = (ClampMin = "0.0", ClampMax = "40.0"))
	float TrajectoryGapLengthPx = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Preview", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float TrajectoryLineThicknessPx = 1.8f;

	/** Full width and height of the red predicted-landing X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Preview", meta = (ClampMin = "4.0", ClampMax = "64.0"))
	float PredictedLandingCrossSizePx = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Preview", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float PredictedLandingCrossThicknessPx = 2.5f;

	/** Enables the M10.1-B SceneCapture only for an eligible reinforced Pulling trajectory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landing Preview")
	bool bShowReinforcedLandingPreview = true;

	/**
	 * Calibration-only extension: reuse the landing capture only when M6's
	 * authoritative prediction actually terminates on the practice satellite.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landing Preview|Satellite E5")
	bool bShowSatelliteE5LandingPreview = true;

	/**
	 * Legacy serialized compatibility only. Satellite landing preview
	 * eligibility is now determined exclusively by SatelliteBody/SatelliteE5
	 * terminal types, so proximity alone can never open the lunar preview.
	 */
	UPROPERTY()
	float SatelliteE5PreviewPathProximityRadiusMultiplier = 4.5f;

	/**
	 * Downward angle from the satellite-local tangent plane toward the predicted
	 * lunar landing point. Screen Up remains constrained by satellite radial Up.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landing Preview|Satellite",
		meta = (ClampMin = "5.0", ClampMax = "85.0", UIMin = "15.0", UIMax = "75.0", Units = "deg"))
	float SatelliteLandingViewPitchDegrees = 45.0f;

	/** Width of the screen-top landing picture-in-picture, in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landing Preview|Layout", meta = (ClampMin = "180.0", ClampMax = "1200.0"))
	float LandingViewScreenWidthPx = 420.0f;

	/** Height of the screen-top landing picture-in-picture, in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landing Preview|Layout", meta = (ClampMin = "100.0", ClampMax = "700.0"))
	float LandingViewScreenHeightPx = 236.0f;

	/** Vertical distance from the top screen edge to the outer picture-in-picture frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landing Preview|Layout", meta = (ClampMin = "0.0", ClampMax = "400.0"))
	float LandingViewTopMarginPx = 24.0f;

	/** Runtime render-target width. Keep modest: this capture exists only while the pouch is pulled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landing Preview|Performance", meta = (ClampMin = "128", ClampMax = "2048"))
	int32 LandingViewRenderTargetWidth = 512;

	/** Runtime render-target height. Match the screen-frame aspect ratio where possible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landing Preview|Performance", meta = (ClampMin = "72", ClampMax = "2048"))
	int32 LandingViewRenderTargetHeight = 288;

	/** Manual SceneCapture cadence; zero is intentionally not allowed to prevent a per-frame accidental capture path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landing Preview|Performance", meta = (ClampMin = "1.0", ClampMax = "60.0", Units = "Hz"))
	float LandingViewCaptureHz = 20.0f;

	/** Fixed centimetre distance along the reverse extension of the predicted landing-incidence vector. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landing Preview|Camera", meta = (ClampMin = "100.0", ClampMax = "100000.0", Units = "cm"))
	float LandingViewCameraDistanceCM = 1200.0f;

	/** Horizontal field of view of the landing SceneCapture. The camera direction itself always follows the incidence vector. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landing Preview|Camera", meta = (ClampMin = "10.0", ClampMax = "120.0", Units = "deg"))
	float LandingViewFieldOfViewDegrees = 46.0f;

	/** Diameter of each sparse world-space prediction point rendered only into the landing SceneCapture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landing Preview|Trajectory", meta = (ClampMin = "1.0", ClampMax = "100.0", Units = "cm"))
	float LandingViewTrajectoryPointSizeCM = 8.0f;

	/** Use every Nth M6 prediction point to retain the existing dotted trajectory language. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landing Preview|Trajectory", meta = (ClampMin = "1", ClampMax = "16"))
	int32 LandingViewTrajectoryStride = 2;

	/** Upper bound for visible terminal trajectory points; older launch-side samples are intentionally omitted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landing Preview|Trajectory", meta = (ClampMin = "8", ClampMax = "128"))
	int32 LandingViewTrajectoryPointCount = 48;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landing Preview|Trajectory")
	FLinearColor LandingViewTrajectoryColor = FLinearColor(0.69f, 0.88f, 1.0f, 1.0f);

	/** Enables the M10.1-C fitted-plane orbital overview for long reinforced predictions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital Overview")
	bool bShowOrbitalOverview = true;

	/** First-show threshold, calibrated to appear just before the default launch camera loses the landing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital Overview|Visibility",
		meta = (ClampMin = "1000.0", ClampMax = "200000.0", Units = "cm"))
	float OrbitalDiagramMinPathLengthCM = 2500.0f;

	/** Once visible, the overview remains until path length falls this far below the first-show threshold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital Overview|Visibility",
		meta = (ClampMin = "0.0", ClampMax = "10000.0", Units = "cm"))
	float OrbitalDiagramPathLengthHysteresisCM = 800.0f;

	/** Main-view safe-frame inset used as a fallback so an off-screen landing can never precede the overview. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital Overview|Visibility",
		meta = (ClampMin = "0.0", ClampMax = "0.25"))
	float OrbitalDiagramViewportInsetRatio = 0.06f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital Overview|Layout",
		meta = (ClampMin = "120.0", ClampMax = "700.0"))
	float OrbitalDiagramDiameterPx = 250.0f;

	/** Vertical gap between the complete scout-map panel and orbital-overview panel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital Overview|Layout",
		meta = (ClampMin = "0.0", ClampMax = "200.0"))
	float OrbitalDiagramScoutMapGapPx = 32.0f;

	/** Bottom-screen space reserved for the M5 inventory hotbar and its help label. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital Overview|Layout",
		meta = (ClampMin = "80.0", ClampMax = "300.0"))
	float OrbitalDiagramBottomReservedPx = 116.0f;

	/** Screen-space padding between the complete projected trajectory and the circular frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital Overview|Layout",
		meta = (ClampMin = "4.0", ClampMax = "64.0"))
	float OrbitalDiagramContentPaddingPx = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital Overview|Grid",
		meta = (ClampMin = "15.0", ClampMax = "90.0", Units = "deg"))
	float OrbitalDiagramLatitudeStepDegrees = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital Overview|Grid",
		meta = (ClampMin = "15.0", ClampMax = "90.0", Units = "deg"))
	float OrbitalDiagramLongitudeStepDegrees = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital Overview|Trajectory",
		meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float OrbitalDiagramTrajectoryThicknessPx = 2.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital Overview|Trajectory",
		meta = (ClampMin = "1.0", ClampMax = "40.0"))
	float OrbitalDiagramOccludedDashLengthPx = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbital Overview|Trajectory",
		meta = (ClampMin = "0.0", ClampMax = "40.0"))
	float OrbitalDiagramOccludedGapLengthPx = 4.0f;
};

/** One already-projected, fixed-frame environment icon consumed by the HUD. */
USTRUCT()
struct FABTSM10ScoutMapMarker
{
	GENERATED_BODY()

	EABTSM10ScoutMarkerType Type = EABTSM10ScoutMarkerType::Tree;
	FVector2D NormalizedMapPosition = FVector2D::ZeroVector;
};
