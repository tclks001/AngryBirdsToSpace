// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FABTSM7MaterialProfile;
struct FABTSM73DAGFailureFrontierSettings;
struct FABTSM73DAGFailurePatternSettings;
struct FABTSM73DAGFailurePlayabilitySettings;
struct FABTSM73DAGGenerationSettings;
struct FABTSM73DAGLayoutSettings;
struct FABTSM73DifficultySettings;
struct FABTSM73GenerationSettings;
struct FABTSM73StructureData;

/** Small orchestration boundary between DAG-1 topology and DAG-2 physical BrickNode lowering. */
class FABTSM73DAGBuildingPipeline
{
public:
	bool Build(const FABTSM73DAGGenerationSettings& DAGSettings, const FABTSM73DAGLayoutSettings& LayoutSettings,
		const FABTSM73GenerationSettings& BuildingSettings, FABTSM73StructureData& OutData, FString& OutError) const;

	/**
	 * DAG3-A/B transaction. Disabled DAG3-B remains byte-for-byte on the
	 * baseline DAG2.3 geometry path while optional DAG3-A analysis is retained.
	 */
	bool BuildWithFailurePattern(
		const FABTSM73DAGGenerationSettings& DAGSettings,
		const FABTSM73DAGLayoutSettings& LayoutSettings,
		const FABTSM73GenerationSettings& BuildingSettings,
		const FABTSM73DAGFailureFrontierSettings& FrontierSettings,
		const FABTSM73DAGFailurePatternSettings& PatternSettings,
		const FABTSM73DifficultySettings& DifficultySettings,
		TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles,
		FABTSM73StructureData& OutData,
		FString& OutError) const;

	/** DAG3-C opt-in overload. Disabled C is identical to the legacy overload. */
	bool BuildWithFailurePattern(
		const FABTSM73DAGGenerationSettings& DAGSettings,
		const FABTSM73DAGLayoutSettings& LayoutSettings,
		const FABTSM73GenerationSettings& BuildingSettings,
		const FABTSM73DAGFailureFrontierSettings& FrontierSettings,
		const FABTSM73DAGFailurePatternSettings& PatternSettings,
		const FABTSM73DAGFailurePlayabilitySettings& PlayabilitySettings,
		const FABTSM73DifficultySettings& DifficultySettings,
		TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles,
		const FVector& LocalAttackDirection,
		FABTSM73StructureData& OutData,
		FString& OutError) const;
};
