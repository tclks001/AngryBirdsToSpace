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
