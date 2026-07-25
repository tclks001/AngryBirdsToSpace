// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FABTSM73DAGGenerationSettings;
struct FABTSM73DAGLayoutSettings;
struct FABTSM73GenerationSettings;
struct FABTSM73StructureData;

/** Small orchestration boundary between DAG-1 topology and DAG-2 physical BrickNode lowering. */
class FABTSM73DAGBuildingPipeline
{
public:
	bool Build(const FABTSM73DAGGenerationSettings& DAGSettings, const FABTSM73DAGLayoutSettings& LayoutSettings,
		const FABTSM73GenerationSettings& BuildingSettings, FABTSM73StructureData& OutData, FString& OutError) const;
};
