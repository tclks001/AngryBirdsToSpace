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

	/** Shows the reinforced-slingshot prediction only when its landing lies inside this scout snapshot. */
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
};

/** One already-projected, fixed-frame environment icon consumed by the HUD. */
USTRUCT()
struct FABTSM10ScoutMapMarker
{
	GENERATED_BODY()

	EABTSM10ScoutMarkerType Type = EABTSM10ScoutMarkerType::Tree;
	FVector2D NormalizedMapPosition = FVector2D::ZeroVector;
};
