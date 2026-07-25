// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73DAGTypes.h"
#include "Building/ABTSM7BuildingTypes.h"
#include "ABTSM73BuildingTypes.generated.h"

UENUM(BlueprintType)
enum class EABTSM73GroundMode : uint8
{
	Auto,
	PlanarTestStage,
	SphericalCellTopo
};

/** Keeps the already accepted fixed-layer generator intact while DAG-2 is introduced in parallel. */
UENUM(BlueprintType)
enum class EABTSM73GenerationAlgorithm : uint8
{
	LegacyLayeredAB2,
	RecursiveSupportDAG
};

UENUM(BlueprintType)
enum class EABTSM73Silhouette : uint8
{
	SingleTower,
	Gatehouse,
	TwinTowerBridge
};

/** Device-free M7.3-B2 structural weakness geometry. */
UENUM(BlueprintType)
enum class EABTSM73StructuralWeaknessPattern : uint8
{
	Auto,
	CriticalCorner,
	AsymmetricDualSupport,
	OffsetSeam
};

UENUM(BlueprintType)
enum class EABTSM73PredictedCollapseMode : uint8
{
	None,
	Tip,
	SlideAndTip
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
	EABTSM73GenerationAlgorithm GenerationAlgorithm = EABTSM73GenerationAlgorithm::LegacyLayeredAB2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
	EABTSM73Silhouette Silhouette = EABTSM73Silhouette::SingleTower;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
	EABTSM7BuildingMaterial PrimaryMaterial = EABTSM7BuildingMaterial::Wood;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (ClampMin = "1", ClampMax = "6"))
	int32 Levels = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (ClampMin = "5", ClampMax = "100"))
	int32 MaxBrickCount = 50;

	/** Adds one authored top weakness segment before support edges are finalized. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weakness Geometry")
	bool bGenerateStructuralWeakness = true;

	/** Auto maps the three current silhouettes to different B2 templates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weakness Geometry", meta = (EditCondition = "bGenerateStructuralWeakness"))
	EABTSM73StructuralWeaknessPattern StructuralWeaknessPattern = EABTSM73StructuralWeaknessPattern::Auto;

	/** -1 chooses one tower deterministically from BuildingSeed; SingleTower always uses bay 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weakness Geometry", meta = (ClampMin = "-1", ClampMax = "1", EditCondition = "bGenerateStructuralWeakness"))
	int32 StructuralWeaknessBayIndex = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weakness Geometry", meta = (ClampMin = "0.40", ClampMax = "0.85", EditCondition = "bGenerateStructuralWeakness"))
	float WeaknessFootprintRatio = 0.62f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weakness Geometry", meta = (ClampMin = "40.0", UIMax = "240.0", EditCondition = "bGenerateStructuralWeakness"))
	float WeaknessSupportHeightCM = 110.0f;

	/** Bias of the carrier/load COM toward the breakable support, relative to support span. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weakness Geometry", meta = (ClampMin = "0.10", ClampMax = "0.80", EditCondition = "bGenerateStructuralWeakness"))
	float WeaknessBiasRatio = 0.72f;

	/** Absolute COM reserve along the intended failure direction, after proportional bias. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weakness Geometry", meta = (ClampMin = "0.0", UIMax = "20.0", EditCondition = "bGenerateStructuralWeakness"))
	float WeaknessTipReserveCM = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weakness Geometry", meta = (ClampMin = "40.0", UIMax = "300.0", EditCondition = "bGenerateStructuralWeakness"))
	float WeaknessPayloadHeightCM = 130.0f;

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

	/** Minimum hidden Chaos observation before a quiet window may complete. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0.1", UIMax = "5.0"))
	float IdleValidationSeconds = 1.25f;

	/** Continuous low-motion time required before the hidden pre-settle is frozen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0.05", UIMax = "2.0"))
	float IdleStableHoldSeconds = 0.45f;

	/** Hard observation limit. A timeout still rejects meaningful displacement, settlement or rotation; bounded Chaos contact jitter is frozen and accepted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0.5", UIMax = "10.0"))
	float IdleValidationMaxSeconds = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0.0", UIMax = "50.0"))
	float IdleLinearSpeedThresholdCMPerSec = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ClampMin = "0.0", UIMax = "10.0"))
	float IdleAngularSpeedThresholdDegPerSec = 1.5f;

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
	float MinWeakCollapseRatio = 0.02f;

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

	/** Prevents the old aligned-floor proxy from being accepted as a physical weak point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "B2 Failure Validation")
	bool bRequireAuthoredStructuralWeakness = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "B2 Failure Validation", meta = (ClampMin = "0.0", UIMax = "50.0"))
	float MinInitialSupportMarginCM = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "B2 Failure Validation", meta = (ClampMin = "0.0", UIMax = "100.0"))
	float MinTipMarginCM = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "B2 Failure Validation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxReseatRisk = 0.35f;

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-2")
	EABTSM73GenerationAlgorithm GenerationAlgorithm = EABTSM73GenerationAlgorithm::LegacyLayeredAB2;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-2")
	int32 DAGMacroNodeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-2")
	int32 DAGSelectedSupportCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-2")
	int32 DAGMissingRequiredContactCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-2")
	int32 DAGUnexpectedBypassCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-2")
	int64 DAGTopologyHash = 0;

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "B2 Failure")
	EABTSM73StructuralWeaknessPattern StructuralWeaknessPattern = EABTSM73StructuralWeaknessPattern::Auto;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "B2 Failure")
	EABTSM73PredictedCollapseMode PredictedCollapseMode = EABTSM73PredictedCollapseMode::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "B2 Failure")
	float PrimaryTipMarginCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "B2 Failure")
	float PrimaryReseatRisk = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	FString RejectReason;
};
