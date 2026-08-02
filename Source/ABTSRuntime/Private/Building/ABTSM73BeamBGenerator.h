// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/ABTSM73BeamAGenerator.h"
#include "Building/ABTSM73BeamBPreviewTypes.h"

struct FABTSM73DAG5BV2GenerationResult;

struct FABTSM73BeamBBridgeEndpoint
{
	int32 SpanVolumeId = INDEX_NONE;
	/** Upstream SupportedSpan contract identity; may be one part of a module. */
	int32 DeclaredSupportVolumeId = INDEX_NONE;
	/** Actual boundary Volume in the declared semantic module that owns the seat. */
	int32 SupportVolumeId = INDEX_NONE;
	int32 BridgeBayId = INDEX_NONE;
	int32 SupportBayId = INDEX_NONE;
	int32 SeatPlannedMemberId = INDEX_NONE;
	double BearingPlaneCM = 0.0;
	bool bNegativeEndpoint = false;
};

struct FABTSM73BeamBGenerationResult
{
	FABTSM73BeamBPreviewSummary Summary;
	TArray<FABTSM73BeamBPlacement> Placements;
	TArray<FABTSM73BeamBPlannedMember> PlannedMembers;
	TArray<FABTSM73BeamBGrammarStep> GrammarSteps;
	TArray<FABTSM73BeamBBridgeEndpoint> BridgeEndpoints;
	FABTSM73BeamAGenerationResult ClosedAssembly;
};

/** Pure-data Beam-B Motif WFC and bounded graph-grammar planner. */
class FABTSM73BeamBGenerator
{
public:
	bool Generate(
		const FABTSM73BeamBPreviewSettings& Settings,
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		const FABTSM73BeamAGenerationResult& BeamA,
		FABTSM73BeamBGenerationResult& OutResult,
		FString& OutError) const;
};
