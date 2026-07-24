// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FABTSM73GenerationSettings;
struct FABTSM73StructureData;

/** Cheap deterministic rejection pass before any Chaos body is spawned. */
class FABTSM73StabilityValidator
{
public:
	bool Validate(const FABTSM73GenerationSettings& Settings, const FABTSM73StructureData& Data, FString& OutError) const;

private:
	static bool HasGroundPath(int32 NodeId, const FABTSM73StructureData& Data, TSet<int32>& Visiting, TMap<int32, bool>& Cache);
};

