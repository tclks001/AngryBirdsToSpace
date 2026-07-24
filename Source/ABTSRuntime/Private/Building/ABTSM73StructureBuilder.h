// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM7BuildingTypes.h"

struct FABTSM73GenerationSettings;
struct FABTSM73StructureData;

/** Deterministic M7.3-A brick grammar. It owns no UObjects or world state. */
class FABTSM73StructureBuilder
{
public:
	bool Build(const FABTSM73GenerationSettings& Settings, FABTSM73StructureData& OutData, FString& OutError) const;

private:
	static void AddBrick(FABTSM73StructureData& Data, const FVector& Center, const FVector& Dimensions, EABTSM7BuildingMaterial Material);
	static void AddFourColumnStorey(FABTSM73StructureData& Data, float CenterY, float Width, float Depth, float BottomZ,
		float LevelHeight, float ColumnWidth, float BeamHeight, EABTSM7BuildingMaterial Material, bool bAddRoof);
	static void FinalizeBoundsAndSupports(FABTSM73StructureData& Data);
};
