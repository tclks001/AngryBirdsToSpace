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

/** Pure-data Beam-C Load DAG extractor and static proxy validator. */
class FABTSM73BeamCGenerator
{
public:
	bool Generate(
		const FABTSM73BeamCPreviewSettings& Settings,
		const FABTSM73BeamAGenerationResult& ClosedAssembly,
		FABTSM73BeamCGenerationResult& OutResult,
		FString& OutError) const;
};
