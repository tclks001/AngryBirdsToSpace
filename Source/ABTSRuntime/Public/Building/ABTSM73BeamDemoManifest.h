// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73BeamD1Types.h"

/** One immutable building in the bounded six-building jury demonstration. */
struct FABTSM73BeamDemoManifestEntry
{
	EABTSM73BeamDemoBuilding Id = EABTSM73BeamDemoBuilding::Custom;
	FName StableId;
	FABTSM73BeamD1Settings Settings;
};

/**
 * Single source of truth for the six-building jury demonstration.
 * This is deliberately not an arbitrary-seed search or a production fallback.
 */
class ABTSRUNTIME_API FABTSM73BeamDemoManifest
{
public:
	static constexpr int32 Version = 1;

	static const TArray<FABTSM73BeamDemoManifestEntry>& GetEntries();
	static bool Resolve(
		EABTSM73BeamDemoBuilding Id,
		FABTSM73BeamDemoManifestEntry& OutEntry,
		FString& OutError);
	static int64 CalculateHash();
};
