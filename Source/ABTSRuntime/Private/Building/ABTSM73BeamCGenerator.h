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

	/**
	 * Production path: validate final Brick contacts, add bounded local Z
	 * supports for failed horizontal bearing footprints, then re-close and
	 * revalidate the authoritative assembly.
	 */
	bool GenerateWithStructuralClosure(
		const FABTSM73BeamCPreviewSettings& Settings,
		FABTSM73BeamAGenerationResult& InOutClosedAssembly,
		FABTSM73BeamCGenerationResult& OutResult,
		FString& OutError) const;
};
