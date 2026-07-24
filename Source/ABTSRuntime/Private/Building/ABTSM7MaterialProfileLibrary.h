// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM7BuildingTypes.h"

/** Shared source of the canonical M7 material profiles used by runtime and PCG analysis. */
class FABTSM7MaterialProfileLibrary
{
public:
	static TArray<FABTSM7MaterialProfile> MakeDefaultProfiles();
	static const FABTSM7MaterialProfile* FindProfile(
		TConstArrayView<FABTSM7MaterialProfile> Profiles,
		EABTSM7BuildingMaterial Material);
	static float ComputeBreakEffort(const FABTSM7MaterialProfile& Profile);
};
