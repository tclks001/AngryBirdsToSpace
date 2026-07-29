// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FABTSM7MaterialProfile;
struct FABTSM73DAGFailureFrontierAnalysis;
struct FABTSM73DAGFailureFrontierSettings;
struct FABTSM73StructureData;

/**
 * DAG3-A: discovers directed internal Failure Frontiers from the realized
 * support graph. It is pure data and never mutates Brick geometry or materials.
 */
class FABTSM73DAGFailureFrontierAnalyzer
{
public:
	bool Analyze(
		const FABTSM73DAGFailureFrontierSettings& Settings,
		TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles,
		const FABTSM73StructureData& Data,
		FABTSM73DAGFailureFrontierAnalysis& OutAnalysis,
		FString& OutError) const;
};
