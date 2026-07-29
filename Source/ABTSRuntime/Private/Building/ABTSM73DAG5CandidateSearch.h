// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73DAG5Types.h"

struct FABTSM73DAGGenerationSettings;
struct FABTSM73DAGLayoutSettings;
struct FABTSM73GenerationSettings;
struct FABTSM73StructureData;

/** Pure-data, deterministic and bounded feasibility search around the current DAG candidate pipeline. */
class FABTSM73DAG5CandidateSearch
{
public:
	using FCandidateBuilder = TFunctionRef<bool(
		const FABTSM73DAGGenerationSettings&,
		FABTSM73StructureData&,
		FString&)>;

	bool Build(
		const FABTSM73DAG5ASettings& SearchSettings,
		const FABTSM73DAGGenerationSettings& BaseDAGSettings,
		const FABTSM73DAGLayoutSettings& LayoutSettings,
		const FABTSM73GenerationSettings& BuildingSettings,
		FCandidateBuilder CandidateBuilder,
		FABTSM73DAGGenerationSettings& OutSelectedDAGSettings,
		FABTSM73StructureData& OutData,
		FABTSM73DAG5AResult& OutResult,
		FString& OutError) const;
};
