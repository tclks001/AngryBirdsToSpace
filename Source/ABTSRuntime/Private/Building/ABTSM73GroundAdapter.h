// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73BuildingTypes.h"

class AActor;
struct FABTSM73GenerationSettings;
struct FABTSM73GroundContext;
struct FABTSM73GroundSample;
struct FABTSM73StructureData;

/** Resolves the same local building contract against M7.1 Floor or M3 CellTopo terrain. */
class FABTSM73GroundAdapter
{
public:
	bool Resolve(AActor& Host, EABTSM73GroundMode RequestedMode, int32 RequestedAnchorCellId, bool bSnapPlanarAnchorToStage,
		FABTSM73GroundContext& OutContext, FString& OutError) const;
	bool AnalyzeFootprint(const FABTSM73GenerationSettings& Settings, const FABTSM73GroundContext& Context,
		FABTSM73StructureData& InOutData, FString& OutError) const;

private:
	static bool QueryGround(const FABTSM73GroundContext& Context, const FVector2D& LocalXY, FABTSM73GroundSample& OutSample);
};
