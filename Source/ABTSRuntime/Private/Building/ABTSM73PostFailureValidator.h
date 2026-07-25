// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FABTSM7MaterialProfile;
struct FABTSM73DifficultySettings;
struct FABTSM73FailureProbeResult;
struct FABTSM73StructuralWeaknessIntent;
struct FABTSM73StructureData;

/** B2 static failure proxy: load COM, support hull, tip margin and vertical reseat risk. */
class FABTSM73PostFailureValidator
{
public:
	bool EvaluateAuthoredIntent(
		const FABTSM73DifficultySettings& Settings,
		TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles,
		const FABTSM73StructureData& Data,
		const FABTSM73StructuralWeaknessIntent& Intent,
		FABTSM73FailureProbeResult& OutResult,
		FString& OutError) const;

	float EstimateVerticalReseatRisk(
		TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles,
		const FABTSM73StructureData& Data,
		int32 RemovedNodeId,
		TConstArrayView<int32> FallingNodeIds) const;
};
