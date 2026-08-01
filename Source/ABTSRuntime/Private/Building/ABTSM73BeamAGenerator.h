// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/ABTSM73BeamAPreviewTypes.h"

struct FABTSM73DAG5BV2GenerationResult;

struct FABTSM73BeamAGenerationResult
{
	FABTSM73BeamAPreviewSummary Summary;
	TArray<FABTSM73BeamABay> Bays;
	TArray<FABTSM73BeamAJoint> Joints;
	TArray<FABTSM73BeamAMember> Members;
	TArray<FABTSM73BeamABearingContact> BearingContacts;
	TArray<FABTSM73BeamAAssembly> Assemblies;
};

namespace ABTSM73BeamA
{
	/** Resolve Z-support stations for two aligned parallel beam lanes. */
	bool BuildAlignedParallelSupportOffsets(
		double LowerLane,
		double UpperLane,
		double OverlapMinimum,
		double OverlapMaximum,
		const FABTSM73BeamAPreviewSettings& Settings,
		TArray<double>& OutOffsets);

	/**
	 * Rebuild and close an already populated Beam-A-compatible assembly.
	 * Beam-B uses this exact entry point after compiling Motif plans back to
	 * Joint/Member/Assembly IR, so both stages share one penetration and
	 * ground-reachability contract.
	 */
	bool CloseGeneratedAssembly(
		const FABTSM73BeamAPreviewSettings& Settings,
		FABTSM73BeamAGenerationResult& InOutResult,
		FString& OutError);
}

/** Pure-data Beam-A compiler from accepted DAG5-B v2 silhouette volumes. */
class FABTSM73BeamAGenerator
{
public:
	bool Generate(
		const FABTSM73BeamAPreviewSettings& Settings,
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		FABTSM73BeamAGenerationResult& OutResult,
		FString& OutError) const;
};
