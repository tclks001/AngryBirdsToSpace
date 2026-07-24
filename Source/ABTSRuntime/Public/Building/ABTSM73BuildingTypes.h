// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM7BuildingTypes.h"
#include "ABTSM73BuildingTypes.generated.h"

UENUM(BlueprintType)
enum class EABTSM73GroundMode : uint8
{
	Auto,
	PlanarTestStage,
	SphericalCellTopo
};

UENUM(BlueprintType)
enum class EABTSM73Silhouette : uint8
{
	SingleTower,
	Gatehouse,
	TwinTowerBridge
};

/** Editor-facing M7.3-A generation and validation preset. */
USTRUCT(BlueprintType)
struct FABTSM73GenerationSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
	int32 BuildingSeed = 7301;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
	EABTSM73Silhouette Silhouette = EABTSM73Silhouette::SingleTower;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
	EABTSM7BuildingMaterial PrimaryMaterial = EABTSM7BuildingMaterial::Wood;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (ClampMin = "1", ClampMax = "6"))
	int32 Levels = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (ClampMin = "5", ClampMax = "100"))
	int32 MaxBrickCount = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dimensions", meta = (ClampMin = "80.0", UIMax = "800.0"))
	float BayWidthCM = 360.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dimensions", meta = (ClampMin = "80.0", UIMax = "800.0"))
	float BuildingDepthCM = 260.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dimensions", meta = (ClampMin = "60.0", UIMax = "500.0"))
	float LevelHeightCM = 190.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dimensions", meta = (ClampMin = "20.0", UIMax = "200.0"))
	float ColumnWidthCM = 74.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dimensions", meta = (ClampMin = "20.0", UIMax = "160.0"))
	float BeamHeightCM = 58.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "20.0", UIMax = "400.0"))
	float FoundationMarginCM = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "10.0", UIMax = "150.0"))
	float FoundationCapThicknessCM = 36.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "20.0", UIMax = "180.0"))
	float FoundationFootSizeCM = 72.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "10.0", UIMax = "400.0"))
	float FootprintSampleSpacingCM = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "0.0", UIMax = "25.0"))
	float MaxBuildingPadSlopeDegrees = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "0.0", UIMax = "1000.0"))
	float MaxTerrainDeltaCM = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "1.0", UIMax = "500.0"))
	float MaxFoundationDepthCM = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "0.0", UIMax = "20.0"))
	float FoundationEmbedDepthCM = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "0.0", UIMax = "50.0"))
	float FoundationTopClearanceCM = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "0.25", UIMax = "15.0"))
	float MaxSinglePlatformAngularSpanDegrees = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float MinContactAreaRatio = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0.1", UIMax = "5.0"))
	float IdleValidationSeconds = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0.0", UIMax = "50.0"))
	float MaxIdleDisplacementCM = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0.0", UIMax = "30.0"))
	float MaxIdleRotationDegrees = 2.0f;
};

/** Last deterministic generation/validation result exposed in Details and logs. */
USTRUCT(BlueprintType)
struct FABTSM73GenerationSummary
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	bool bAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	bool bPlanar = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 BrickCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 SupportEdgeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 GroundNodeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 FoundationFootCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	float FootprintTerrainDeltaCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	float CurvatureDropCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	float MaxSlopeDegrees = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	float MaxFoundationDepthCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	FString RejectReason;
};
