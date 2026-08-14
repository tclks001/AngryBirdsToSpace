// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/ABTSStylizedMaterialOverrideRegistry.h"

class AABTSM3Planet;

/**
 * M3-owned read-only T3-A1 adapter.
 *
 * It publishes one deterministic component-slot binding per available HISM
 * material. Integration remains the sole owner of applying or restoring slots.
 */
class ABTSRUNTIME_API FABTSM3StylizedMaterialAdapter
{
public:
	static void GatherBackgroundPropMaterialBindings(
		const AABTSM3Planet& Planet,
		TArray<FABTSStylizedMaterialSlotBinding>& OutBindings);
};
