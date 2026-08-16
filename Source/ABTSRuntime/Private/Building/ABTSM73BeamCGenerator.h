// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/ABTSM73BeamAGenerator.h"
#include "Building/ABTSM73BeamCPreviewTypes.h"

struct FABTSM73BeamCGenerationResult
{
	FABTSM73BeamCPreviewSummary Summary;
	TArray<FABTSM73BeamCLoadNode> Nodes;
	TArray<FABTSM73BeamCLoadEdge> Edges;
	TArray<int32> TopologicalMemberOrder;
};

using FABTSM73BeamCMemberSelfLoadResolver =
	TFunction<double(const FABTSM73BeamAMember&)>;

namespace ABTSM73BeamC
{
	/** Register a failed DAG and reject non-adjacent hash cycles fail closed. */
	bool TryObserveStructuralClosureFailure(
		uint32 FailedAnalysisHash,
		const TOptional<uint32>& PreviousFailedAnalysisHash,
		TSet<uint32>& SeenHashes,
		bool& bOutImmediateRepeat,
		FString& OutError);

	/** A repeated DAG may force the prior-lane cap only after reclose proved twin lanes. */
	bool ShouldForceRootedGrillageRepair(
		bool bRepeatedFailedAnalysis,
		int32 PriorTwinAttemptCount);

	/** One physical rooted-grillage transaction is allowed per failed DAG. */
	bool TryBeginRootedGrillageRepair(
		uint32 FailedAnalysisHash,
		TSet<uint32>& AttemptedHashes,
		FString& OutError);

	/** Reject a failed DAG before repair if it already consumed its grillage token. */
	bool TryCheckRootedGrillageRepairAvailable(
		uint32 FailedAnalysisHash,
		const TSet<uint32>& AttemptedHashes,
		FString& OutError);

	/** Commit the failed-DAG token only when the repair helper added physical grillage. */
	bool TryCommitAddedRootedGrillageRepair(
		uint32 FailedAnalysisHash,
		bool bAddedRootedGrillage,
		TSet<uint32>& AttemptedHashes,
		FString& OutError);
}

/** Pure-data Beam-C Load DAG extractor and static proxy validator. */
class FABTSM73BeamCGenerator
{
public:
	bool Generate(
		const FABTSM73BeamCPreviewSettings& Settings,
		const FABTSM73BeamAGenerationResult& ClosedAssembly,
		FABTSM73BeamCGenerationResult& OutResult,
		FString& OutError,
		const FABTSM73BeamCMemberSelfLoadResolver* MemberSelfLoadResolver = nullptr) const;

	/**
	 * Production path: validate final Brick contacts, add bounded local Z
	 * supports for failed horizontal bearing footprints, then re-close and
	 * revalidate the authoritative assembly. Deferred core bracing may only be
	 * enabled by an orchestrator that immediately runs Beam-C3 again and rejects
	 * the candidate unless the final all-Z span audit succeeds.
	 */
	bool GenerateWithStructuralClosure(
		const FABTSM73BeamCPreviewSettings& Settings,
		FABTSM73BeamAGenerationResult& InOutClosedAssembly,
		FABTSM73BeamCGenerationResult& OutResult,
		FString& OutError,
		int32 MaximumFinalMemberCount = MAX_int32,
		bool bAllowDeferredCoreBracing = false,
		int32 PriorStructuralClosurePassCount = 0,
		int32 PriorAddedStructuralSupportPostCount = 0,
		const FABTSM73BeamCMemberSelfLoadResolver* MemberSelfLoadResolver = nullptr,
		bool bAllowPostRepairResultantAdvisory = true) const;
};
