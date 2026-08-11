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

/** Pure-data Beam-D1 profile-to-real-Brick compiler. */
class FABTSM73BeamD1BrickCompiler
{
public:
	bool Generate(
		const FABTSM73BeamD1Settings& Settings,
		FABTSM73BeamD1GenerationResult& OutResult,
		FString& OutError) const;

	/** Runs a real Stage 0/1 early stop for editor diagnosis; later stages fail closed. */
	bool GenerateStagePreview(
		const FABTSM73BeamD1Settings& Settings,
		EABTSM73BeamC3GenerationStage StopStage,
		FABTSM73BeamD1StagePreviewResult& OutResult,
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
