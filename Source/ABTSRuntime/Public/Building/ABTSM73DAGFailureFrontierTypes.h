// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ABTSM73DAGFailureFrontierTypes.generated.h"

/** Physical removal primitive used to prove a directed Ground-to-load cut. */
UENUM(BlueprintType)
enum class EABTSM73DAGFailureCandidateKind : uint8
{
	DirectedNodeCut,
	SupportInterfaceCutSet
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

/** One deterministic DAG3-A candidate and its static counterfactual metrics. */
struct FABTSM73DAGFailureFrontierCandidate
{
	bool bAccepted = false;
	bool bDirectedDominator = false;
	EABTSM73DAGFailureCandidateKind Kind = EABTSM73DAGFailureCandidateKind::DirectedNodeCut;
	TArray<int32> CandidateNodeIds;
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
