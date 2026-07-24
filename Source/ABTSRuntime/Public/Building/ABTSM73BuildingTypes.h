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

/** Structural role assigned by the M7.3-B counterfactual weak-point pass. */
UENUM(BlueprintType)
enum class EABTSM73WeakPointRole : uint8
{
	None,
	GroundSupport,
	VerticalSupport,
	LoadBearingDeck,
	BridgeConnector
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

	/** Maximum drift along the construction plane during the hidden idle simulation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0.0", UIMax = "50.0"))
	float MaxIdleDisplacementCM = 4.0f;

	/** Small normal-axis contact settling is separate from a real storey losing support. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0.0", UIMax = "50.0"))
	float MaxIdleSettlementCM = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0.0", UIMax = "30.0"))
	float MaxIdleRotationDegrees = 2.0f;
};

/** Editor-facing M7.3-B weak-point and graph-probe difficulty window. */
USTRUCT(BlueprintType)
struct FABTSM73DifficultySettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weak Points")
	bool bEnableWeakPointPlanning = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weak Points", meta = (ClampMin = "1", ClampMax = "3"))
	int32 WeakPointCount = 1;

	/** Selects a material tier from the real M7 profile ordering instead of relying on enum order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weak Points")
	bool bAutoSelectWeakPointMaterial = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weak Points", meta = (EditCondition = "!bAutoSelectWeakPointMaterial"))
	EABTSM7BuildingMaterial WeakPointMaterial = EABTSM7BuildingMaterial::Glass;

	/** Relative effort tier: 1 is the easiest configured material and 4 is the hardest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty", meta = (ClampMin = "1", ClampMax = "4"))
	int32 TargetBirdHits = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinWeakCollapseRatio = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TargetWeakCollapseRatio = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxSingleWeakCollapseRatio = 0.70f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinWeakPointExposure = 0.35f;

	/** Maximum predicted unsupported-mass ratio per material-effort tier for a non-weak hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxNonWeakEffect = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weak Points", meta = (ClampMin = "0.0", UIMax = "1000.0"))
	float MinWeakPointSeparationCM = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weak Points", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxWeakPointAffectedOverlap = 0.60f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reinforcement")
	bool bReinforceNonWeakCriticalNodes = true;

	/** Stone is the default to add resistance without the extreme mass jump of an iron floor slab. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reinforcement")
	EABTSM7BuildingMaterial ReinforcementMaterial = EABTSM7BuildingMaterial::Stone;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reinforcement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReinforcementImpactThreshold = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reinforcement", meta = (ClampMin = "0", ClampMax = "16"))
	int32 MaxReinforcedNodeCount = 4;

	/** Weak-point structural loss per effort tier must exceed the best ordinary hit by this factor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "1.0", UIMax = "5.0"))
	float MinWeakPointAdvantage = 1.50f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation")
	bool bRejectOutsideDifficultyWindow = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowWeakPointDebug = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "1.0", ClampMax = "1.25"))
	float WeakPointDebugScale = 1.04f;
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weak Points")
	int32 WeakPointCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weak Points")
	int32 ReinforcedNodeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weak Points")
	int32 PrimaryWeakPointNodeId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weak Points")
	float BestWeakPointScore = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Difficulty")
	float PredictedWeakCollapseRatio = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Difficulty")
	float PredictedNonWeakEffect = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Difficulty")
	int32 EstimatedWeakPointHits = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Difficulty")
	float DifficultyScore = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	FString RejectReason;
};
