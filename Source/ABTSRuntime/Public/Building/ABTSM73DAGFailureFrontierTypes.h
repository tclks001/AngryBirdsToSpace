// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/ABTSM7BuildingTypes.h"
#include "CoreMinimal.h"
#include "ABTSM73DAGFailureFrontierTypes.generated.h"

/** Physical removal primitive used to prove a directed Ground-to-load cut. */
UENUM(BlueprintType)
enum class EABTSM73DAGFailureCandidateKind : uint8
{
	DirectedNodeCut,
	SupportInterfaceCutSet,
	DirectedEdgeCut,
	BoundedSmallNodeCut
};

/** Canonical physical support-edge identity used by generalized DAG3-C cuts. */
USTRUCT(BlueprintType)
struct FABTSM73DAGFailureEdgeRef
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-3C|Cut")
	int32 LowerNodeId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-3C|Cut")
	int32 UpperNodeId = INDEX_NONE;

	bool operator==(const FABTSM73DAGFailureEdgeRef& Other) const
	{
		return LowerNodeId == Other.LowerNodeId
			&& UpperNodeId == Other.UpperNodeId;
	}
};

/** Geometry/topology rewrite materialized around one accepted DAG3-A interface. */
UENUM(BlueprintType)
enum class EABTSM73DAGFailurePattern : uint8
{
	Auto,
	InternalSingleSupport,
	InternalAsymmetricDualSupport,
	InternalOffsetSeam
};

/** Static failure motion expected from the rewritten interface. */
UENUM(BlueprintType)
enum class EABTSM73DAGFailureMotion : uint8
{
	None,
	Drop,
	Tip,
	SlideThenTip
};

/**
 * DAG3-A pure-data frontier discovery settings.
 *
 * The production default is deliberately disabled until DAG3-B geometry rewrites
 * and the DAG-4 settled/attack gates are complete.
 */
USTRUCT(BlueprintType)
struct FABTSM73DAGFailureFrontierSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3|Activation")
	bool bEnableAnalysis = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3|Search", meta = (ClampMin = "1", ClampMax = "4"))
	int32 MaxCutSetSize = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3|Search", meta = (ClampMin = "1", ClampMax = "256"))
	int32 MaxCandidateCount = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3|Search")
	bool bRequireCompleteDirectedCut = true;

	/**
	 * Adds bounded physical edge/vertex min-cut discovery for DAG3-C.
	 * Disabled preserves the accepted DAG3-A/B candidate set and hashes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3C|Search")
	bool bEnableGeneralizedSmallCutSearch = false;

	/** Deterministic Edmonds-Karp operation ceiling, never a wall-clock timeout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3C|Search",
		meta = (ClampMin = "64", ClampMax = "65536"))
	int32 MaxFlowOperationCount = 8192;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3|Frontier", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinNormalizedHeight = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3|Frontier", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxNormalizedHeight = 0.60f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3|Main Body", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinMainBodyAffectedMassRatio = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3|Main Body", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TargetMainBodyAffectedMassRatio = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3|Main Body", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxMainBodyAffectedMassRatio = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3|Main Body", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinAffectedHeightSpanNormalized = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3|Main Body", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MinAffectedMacroNodeCount = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3|Contact", meta = (ClampMin = "0", ClampMax = "16"))
	int32 MaxBypassSupportEdgeCount = 0;
};

/**
 * DAG3-B transaction settings.
 *
 * Production stays disabled until DAG3-C attack/material routing and DAG-4
 * settled/Chaos counterfactuals are complete.
 */
USTRUCT(BlueprintType)
struct FABTSM73DAGFailurePatternSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3B|Activation")
	bool bEnableGeometryRewrite = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3B|Pattern")
	EABTSM73DAGFailurePattern Pattern = EABTSM73DAGFailurePattern::Auto;

	/** Hard bound over (frontier, pattern) transactions. Exhaustion rejects the candidate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3B|Budget", meta = (ClampMin = "1", ClampMax = "128"))
	int32 MaxRewriteAttemptCount = 48;

	/** Required column area is multiplied by this value before fitting rewritten supports. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3B|Contact", meta = (ClampMin = "1.01", ClampMax = "2.0"))
	float ContactAreaSafetyFactor = 1.10f;

	/** OffsetSeam translates the complete affected closure by this fraction of the interface long axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3B|Offset Seam", meta = (ClampMin = "0.05", ClampMax = "0.35"))
	float OffsetSeamShiftRatio = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3B|Offset Seam", meta = (ClampMin = "0.0", UIMax = "160.0"))
	float MinOffsetSeamShiftCM = 36.0f;
};

/**
 * DAG3-C static playability certification.
 *
 * The default is deliberately disabled. Enabling it requires accepted DAG3-A,
 * an applied DAG3-B pattern and generalized bounded cut search.
 */
USTRUCT(BlueprintType)
struct FABTSM73DAGFailurePlayabilitySettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3C|Activation")
	bool bEnablePlayabilityRouting = false;

	/** Current representative bird capsule radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3C|Attack",
		meta = (ClampMin = "1.0", ClampMax = "200.0", Units = "cm"))
	float ProjectileRadiusCM = 42.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3C|Attack",
		meta = (ClampMin = "100.0", ClampMax = "5000.0", Units = "cm"))
	float AttackApproachDistanceCM = 900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3C|Attack",
		meta = (ClampMin = "1", ClampMax = "5"))
	int32 AttackFaceGridResolution = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3C|Attack",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinAttackExposure = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3C|Motion",
		meta = (ClampMin = "0.0", ClampMax = "300.0", Units = "cm"))
	float MinFreeDropDistanceCM = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3C|Motion",
		meta = (ClampMin = "0.0", ClampMax = "45.0", Units = "deg"))
	float MinFreeTipAngleDegrees = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3C|Motion",
		meta = (ClampMin = "0.0", ClampMax = "300.0", Units = "cm"))
	float MinFreeSlideDistanceCM = 24.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3C|Motion",
		meta = (ClampMin = "0.5", ClampMax = "50.0", Units = "cm"))
	float TranslationSweepStepCM = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3C|Motion",
		meta = (ClampMin = "0.25", ClampMax = "10.0", Units = "deg"))
	float TipSweepStepDegrees = 1.0f;

	/** Positive overlap below this tolerance is treated as authored face contact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3C|Motion",
		meta = (ClampMin = "0.0", ClampMax = "10.0", Units = "cm"))
	float CollisionToleranceCM = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3C|Budget",
		meta = (ClampMin = "8", ClampMax = "1024"))
	int32 MaxMotionSweepSampleCount = 256;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-3C|Attack")
	bool bRequireAllWeakNodesReachable = true;
};

/** One deterministic DAG3-A candidate and its static counterfactual metrics. */
struct FABTSM73DAGFailureFrontierCandidate
{
	bool bAccepted = false;
	bool bDirectedDominator = false;
	EABTSM73DAGFailureCandidateKind Kind = EABTSM73DAGFailureCandidateKind::DirectedNodeCut;
	TArray<int32> CandidateNodeIds;
	TArray<FABTSM73DAGFailureEdgeRef> CandidateEdges;
	TArray<int32> ProtectedRootNodeIds;
	TArray<int32> ExpectedAffectedNodeIds;
	TArray<int32> AffectedMainBodyNodeIds;
	TArray<int32> AffectedMacroNodeIds;
	float NormalizedHeight = 0.0f;
	float MainBodyAffectedMassRatio = 0.0f;
	float AffectedHeightSpanNormalized = 0.0f;
	int32 BypassSupportEdgeCount = 0;
	uint32 FrontierHash = 0;
	FString RejectReason;
};

/** Complete deterministic result of DAG3-A frontier discovery. */
struct FABTSM73DAGFailureFrontierAnalysis
{
	bool bEnabled = false;
	bool bAccepted = false;
	int32 AcceptedCandidateCount = 0;
	int32 SelectedCandidateIndex = INDEX_NONE;
	uint32 SelectedFrontierHash = 0;
	TArray<FABTSM73DAGFailureFrontierCandidate> Candidates;
	FString RejectReason;
};

/** Complete DAG3-B transaction result. Geometry is stored in FABTSM73StructureData. */
struct FABTSM73DAGFailurePatternResult
{
	bool bEnabled = false;
	bool bApplied = false;
	EABTSM73DAGFailurePattern Pattern = EABTSM73DAGFailurePattern::Auto;
	EABTSM73DAGFailureMotion ExpectedMotion = EABTSM73DAGFailureMotion::None;
	uint32 SourceFrontierHash = 0;
	uint32 RealizedPatternHash = 0;
	int32 SupportMacroNodeId = INDEX_NONE;
	int32 LoadMacroNodeId = INDEX_NONE;
	int32 SupportPlateNodeId = INDEX_NONE;
	int32 LoadPlateNodeId = INDEX_NONE;
	int32 RewriteAttemptCount = 0;
	int32 RemovedColumnCount = 0;
	TArray<int32> WeakNodeIds;
	TArray<int32> RemainingSupportNodeIds;
	TArray<int32> AffectedMainBodyNodeIds;
	FVector ExpectedFailureDirectionLocal = FVector::ZeroVector;
	float InitialSupportMarginCM = 0.0f;
	float PostFailureTipMarginCM = 0.0f;
	float ReseatRisk = 1.0f;
	float OffsetSeamShiftCM = 0.0f;
	int32 BypassSupportEdgeCount = 0;
	FString RejectReason;
};

/** Independent DAG3-C identity and metrics; B hashes remain geometry-only. */
struct FABTSM73DAGFailurePlayabilityResult
{
	bool bEnabled = false;
	bool bPlayable = false;
	bool bMaterialProfileValidated = false;
	EABTSM73DAGFailurePattern Pattern = EABTSM73DAGFailurePattern::Auto;
	EABTSM73DAGFailureMotion ExpectedMotion = EABTSM73DAGFailureMotion::None;
	EABTSM7BuildingMaterial Material = EABTSM7BuildingMaterial::Wood;
	TArray<int32> WeakNodeIds;
	TArray<int32> AffectedNodeIds;
	FVector AcceptedAttackDirectionLocal = FVector::ZeroVector;
	FVector AttackImpactPointLocal = FVector::ZeroVector;
	float AttackExposure = 0.0f;
	float MinAttackClearanceCM = 0.0f;
	float FreeDropDistanceCM = 0.0f;
	float FreeTipAngleDegrees = 0.0f;
	float FreeSlideDistanceCM = 0.0f;
	float MaterialKnockSpeedCMPerSec = 0.0f;
	float MaterialBreakSpeedCMPerSec = 0.0f;
	float MaterialDynamicFriction = 0.0f;
	float MaterialStaticFriction = 0.0f;
	float MaterialRestitution = 0.0f;
	float MaterialDensityGPerCubicCM = 0.0f;
	float MaterialDamageAtBreakSpeed = 0.0f;
	float MaterialBreakDamage = 0.0f;
	float MaterialPushVelocityTransfer = 0.0f;
	float LocalBreakEffort = 0.0f;
	int32 EstimatedHits = 0;
	int32 AttackSampleCount = 0;
	int32 MotionSweepSampleCount = 0;
	int32 BlockingNodeId = INDEX_NONE;
	uint32 PlayabilityHash = 0;
	FString RejectReason;
};
