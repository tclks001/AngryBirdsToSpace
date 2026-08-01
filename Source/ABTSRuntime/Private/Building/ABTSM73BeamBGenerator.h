// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/ABTSM73BeamAGenerator.h"
#include "Building/ABTSM73BeamBPreviewTypes.h"

struct FABTSM73DAG5BV2GenerationResult;

struct FABTSM73BeamBGenerationResult
{
	FABTSM73BeamBPreviewSummary Summary;
	TArray<FABTSM73BeamBPlacement> Placements;
	TArray<FABTSM73BeamBPlannedMember> PlannedMembers;
	TArray<FABTSM73BeamBGrammarStep> GrammarSteps;
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
