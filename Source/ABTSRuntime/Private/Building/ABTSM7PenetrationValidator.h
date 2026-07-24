// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AABTSM7BuildingModule;
class UWorld;

struct FABTSM7PenetrationValidationStats
{
	int32 PendingModuleCount = 0;
	int32 DetectedPairCount = 0;
	int32 RepairCount = 0;
	int32 LargeErrorPairCount = 0;
	int32 RemainingSmallPairCount = 0;
	float MaximumDetectedDepthCM = 0.0f;
};

/** Pre-Chaos validation for blocking penetration between M7 modules and the world. */
class FABTSM7PenetrationValidator final
{
public:
	static FABTSM7PenetrationValidationStats ValidateAndRepair(
		UWorld& World,
		const TArray<AABTSM7BuildingModule*>& PendingModules,
		float RepairToleranceCM,
		int32 MaximumRepairPasses);
};
