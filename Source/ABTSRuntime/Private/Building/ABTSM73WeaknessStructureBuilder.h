// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FABTSM73GenerationSettings;
struct FABTSM73StructureData;

/** Adds one deterministic, device-free B2 weakness segment before support edges are finalized. */
class FABTSM73WeaknessStructureBuilder
{
public:
	bool Apply(
		const FABTSM73GenerationSettings& Settings,
		float ResolvedLevelHeightCM,
		float ResolvedColumnWidthCM,
		float ResolvedBeamHeightCM,
		FABTSM73StructureData& InOutData,
		FString& OutError) const;
};
