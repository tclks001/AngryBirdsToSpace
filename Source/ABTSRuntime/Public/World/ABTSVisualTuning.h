// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Temporary PIE targets used to calibrate shared placed-object and pickup presentation. */
enum class EABTSVisualTuningTarget : uint8
{
	Workbench,
	Furnace,
	Bridge,
	StandardSlot,
	FinaleSlot,
	PickupBranch,
	PickupStone,
	PickupWood,
	PickupPlantFiber,
	Count
};

struct FABTSVisualTuningValue
{
	float ScaleMultiplier = 1.0f;
	float LocalZOffsetCM = 0.0f;
};

ABTSRUNTIME_API const FABTSVisualTuningValue& ABTSGetVisualTuning(
	EABTSVisualTuningTarget Target);

ABTSRUNTIME_API const TCHAR* ABTSGetVisualTuningTargetName(
	EABTSVisualTuningTarget Target);
