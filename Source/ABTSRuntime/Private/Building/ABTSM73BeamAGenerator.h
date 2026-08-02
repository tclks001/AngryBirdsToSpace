// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/ABTSM73BeamAPreviewTypes.h"
#include "Building/ABTSM73DAG5BShapePreviewTypes.h"

struct FABTSM73DAG5BV2GenerationResult;

struct FABTSM73BeamASupportVoid
{
	FBox Bounds = FBox(EForceInit::ForceInit);
	int32 SpanAxisIndex = INDEX_NONE;
};

struct FABTSM73BeamAGenerationResult
{
	FABTSM73BeamAPreviewSummary Summary;
	TArray<FABTSM73BeamABay> Bays;
	TArray<FABTSM73BeamAJoint> Joints;
	TArray<FABTSM73BeamAMember> Members;
	TArray<FABTSM73BeamABearingContact> BearingContacts;
	TArray<FABTSM73BeamAAssembly> Assemblies;
	/** Negative-space regions where global closure may not add Z supports. */
	TArray<FABTSM73BeamASupportVoid> ReservedSupportVoids;
};

struct FABTSM73BeamASemanticRoofMember
{
	FVector LocalStart = FVector::ZeroVector;
	FVector LocalEnd = FVector::ZeroVector;
	EABTSM73BeamAFrameAxis Axis = EABTSM73BeamAFrameAxis::X;
	EABTSM73BeamAMemberRole Role = EABTSM73BeamAMemberRole::RoofCourse;
};

namespace ABTSM73BeamA
{
	/**
	 * Beam-A's authoritative horizontal course envelope for a semantic roof.
	 * Alpha is the zero-based course index divided by the total course count.
	 */
	FBox SemanticRoofCourseBounds(
		const FBox& Bounds,
		EABTSM73DAG5BV2Primitive Primitive,
		double Alpha,
		double CrossSectionCM);

	/** Compile the pre-closure layered roof members used by Beam-A. */
	bool BuildSemanticRoofMembers(
		const FABTSM73BeamAPreviewSettings& Settings,
		const FABTSM73BeamAGenerationResult& Topology,
		const FABTSM73BeamABay& Bay,
		EABTSM73DAG5BV2Primitive Primitive,
		TArray<FABTSM73BeamASemanticRoofMember>& OutMembers);

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
