// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ABTSM73BeamC3V3SkeletonFirstGenerator.h"
#include "ABTSM73BeamCGenerator.h"
#include "Building/ABTSM73DAG5BShapeGrammarV2.h"
#include "Building/ABTSM73BeamD1Types.h"

struct FABTSM73BeamD0ResolvedProfile;
struct FABTSM73BeamAGenerationResult;
struct FABTSM73BeamBGenerationResult;
struct FABTSM73BeamCGenerationResult;

struct FABTSM73BeamD1GenerationResult
{
	FABTSM73BeamD1Summary Summary;
	TArray<FABTSM73BeamD1BrickBinding> Bricks;
};

/** Editor acceptance payload. It is deliberately separate from production D1 bricks. */
struct FABTSM73BeamD1StagePreviewResult
{
	FABTSM73BeamD1Summary Summary;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	ABTSM73BeamC3V3::FGenerationResult Skeleton;
	FABTSM73BeamCGenerationResult StaticDAG;
};

/** Stage-5 production payload. The frozen Stage-4 plan remains an immutable
 * prefix; explicit production support members may be appended and are compiled
 * into ordinary visible bricks and load-DAG nodes. */
struct FABTSM73BeamD1Stage5Result
{
	FABTSM73BeamD1Summary Summary;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	ABTSM73BeamC3V3::FGenerationResult Stage4;
	FABTSM73BeamAGenerationResult CompactAssembly;
	FABTSM73BeamCGenerationResult LoadDAG;
	TArray<FABTSM73BeamD1BrickBinding> Bricks;

	/** Stage-4 planned member index -> compact Member/Brick id; suppressed rows are INDEX_NONE. */
	TArray<int32> Stage4ToCompactMember;
	int32 SuppressedStage4MemberCount = 0;
	int32 Stage4ActiveMemberCount = 0;
	/** Explicit Z members appended to connect floating Stage-4 top frames. */
	int32 ReachabilitySupportPostCount = 0;
	/** Additional visible members emitted by bounded Beam-C structural closure. */
	int32 StructuralClosureMemberCount = 0;
	uint64 ActiveGeometryHash = 0;
	uint64 BearingDAGHash = 0;
	uint64 ProductionIdentityHash = 0;
	bool bPhysicalStabilityEvaluated = false;
};

/** Derived device layer. Stage5 is retained byte-for-byte as its authority. */
struct FABTSM73BeamD1Stage55Result
{
	FABTSM73BeamD1Stage5Result Stage5;
	FABTSM73BeamD1Summary Summary;
	TArray<FABTSM73BeamD1DeviceBinding> Devices;
	uint64 DeviceSlotHash = 0;
	uint64 DeviceLoadDAGHash = 0;
	uint64 DeviceAssemblyHash = 0;
};

/** Optional production material policy used by the V3 fixed-six freeze publisher. */
struct FABTSM73BeamD1MaterialPolicy
{
	bool bOverrideOrdinaryBody = false;
	EABTSM7BuildingMaterial PrimaryMaterial = EABTSM7BuildingMaterial::Wood;
};

/** Pure-data Beam-D1 profile-to-real-Brick compiler. */
class FABTSM73BeamD1BrickCompiler
{
public:
	bool Generate(
		const FABTSM73BeamD1Settings& Settings,
		FABTSM73BeamD1GenerationResult& OutResult,
		FString& OutError) const;

	/** Runs a real Stage 0/1/2 early stop for editor diagnosis; later stages fail closed. */
	bool GenerateStagePreview(
		const FABTSM73BeamD1Settings& Settings,
		EABTSM73BeamC3GenerationStage StopStage,
		FABTSM73BeamD1StagePreviewResult& OutResult,
		FString& OutError) const;

	/** Compiles the accepted Stage-4 active plan into one brick per member and a real-contact load DAG. */
	bool GenerateStage5(
		const FABTSM73BeamD1Settings& Settings,
		FABTSM73BeamD1Stage5Result& OutResult,
		FString& OutError) const;
	bool GenerateStage5WithMaterialPolicy(
		const FABTSM73BeamD1Settings& Settings,
		const FABTSM73BeamD1MaterialPolicy& MaterialPolicy,
		FABTSM73BeamD1Stage5Result& OutResult,
		FString& OutError) const;

	/** Adds one deterministic voxelized demo device without mutating Stage 5. */
	bool GenerateStage55DeviceAssembly(
		const FABTSM73BeamD1Settings& Settings,
		FABTSM73BeamD1Stage55Result& OutResult,
		FString& OutError) const;
	bool GenerateStage55DeviceAssemblyWithMaterialPolicy(
		const FABTSM73BeamD1Settings& Settings,
		const FABTSM73BeamD1MaterialPolicy& MaterialPolicy,
		FABTSM73BeamD1Stage55Result& OutResult,
		FString& OutError) const;

	bool CompileResolved(
		const FABTSM73BeamD0ResolvedProfile& Profile,
		const FABTSM73BeamBGenerationResult& BeamB,
		const FABTSM73BeamCGenerationResult& BeamC,
		FABTSM73BeamD1GenerationResult& OutResult,
		FString& OutError) const;

	/** Compile a directly authored authoritative assembly without fabricating Beam-B evidence. */
	bool CompileResolvedAssembly(
		const FABTSM73BeamD0ResolvedProfile& Profile,
		const FABTSM73BeamAGenerationResult& Assembly,
		int64 UpstreamHash,
		const FABTSM73BeamCGenerationResult& BeamC,
		FABTSM73BeamD1GenerationResult& OutResult,
		FString& OutError) const;
};
