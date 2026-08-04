// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/ABTSM73BeamD1Types.h"

struct FABTSM73BeamD0ResolvedProfile;
struct FABTSM73BeamBGenerationResult;
struct FABTSM73BeamCGenerationResult;

struct FABTSM73BeamD1GenerationResult
{
	FABTSM73BeamD1Summary Summary;
	TArray<FABTSM73BeamD1BrickBinding> Bricks;
};

/** Pure-data Beam-D1 profile-to-real-Brick compiler. */
class FABTSM73BeamD1BrickCompiler
{
public:
	bool Generate(
		const FABTSM73BeamD1Settings& Settings,
		FABTSM73BeamD1GenerationResult& OutResult,
		FString& OutError) const;

	bool CompileResolved(
		const FABTSM73BeamD0ResolvedProfile& Profile,
		const FABTSM73BeamBGenerationResult& BeamB,
		const FABTSM73BeamCGenerationResult& BeamC,
		FABTSM73BeamD1GenerationResult& OutResult,
		FString& OutError) const;
};
