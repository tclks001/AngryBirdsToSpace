// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EABTSM7BuildingMaterial : uint8;
struct FABTSM7MaterialProfile;
struct FABTSM73DAGFailurePlayabilityResult;
struct FABTSM73DAGFailurePlayabilitySettings;
struct FABTSM73DifficultySettings;
struct FABTSM73StructureData;

/**
 * Pure-data DAG3-C certification and authoritative weak-point binding.
 *
 * The planner never changes material identity and never invokes Chaos. It works
 * on a copy of the structure, so every enabled failure is atomic.
 */
class FABTSM73DAGFailurePlayabilityPlanner
{
public:
	bool Plan(
		const FABTSM73DAGFailurePlayabilitySettings& Settings,
		const FABTSM73DifficultySettings& DifficultySettings,
		EABTSM7BuildingMaterial ExpectedBuildingMaterial,
		TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles,
		const FVector& LocalAttackDirection,
		FABTSM73StructureData& InOutData,
		FABTSM73DAGFailurePlayabilityResult& OutResult,
		FString& OutError) const;
};
