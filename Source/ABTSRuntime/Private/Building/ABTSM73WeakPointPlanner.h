// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FABTSM7MaterialProfile;
struct FABTSM73DifficultySettings;
struct FABTSM73StructureData;

/** Pure-data M7.3-B counterfactual support-graph probe and material planner. */
class FABTSM73WeakPointPlanner
{
public:
	bool Plan(
		const FABTSM73DifficultySettings& Settings,
		TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles,
		const FVector& LocalAttackDirection,
		int32 BuildingSeed,
		FABTSM73StructureData& InOutData,
		FString& OutError) const;
};
