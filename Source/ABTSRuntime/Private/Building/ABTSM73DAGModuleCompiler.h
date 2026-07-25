// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73StructureData.h"

struct FABTSM73DAGGenerationResult;
struct FABTSM73DAGLayoutSettings;
struct FABTSM73DAGSpatialLayout;
struct FABTSM73GenerationSettings;
struct FABTSM73StructureData;

/** Lowers DAG-2 Macro plates plus selected sparse support edges into existing M7.3 BrickNode data. */
class FABTSM73DAGModuleCompiler
{
public:
	bool Compile(const FABTSM73GenerationSettings& BuildingSettings,
		const FABTSM73DAGGenerationResult& Graph, const FABTSM73DAGLayoutSettings& LayoutSettings,
		const FABTSM73DAGSpatialLayout& Layout, FABTSM73StructureData& OutData, FString& OutError) const;

private:
	static void AddBrick(FABTSM73StructureData& Data, int32 MacroNodeId, const FVector& Center,
		const FVector& Dimensions, EABTSM73BrickSemanticRole Role, int32 StructuralLevel,
		EABTSM7BuildingMaterial Material);
};
