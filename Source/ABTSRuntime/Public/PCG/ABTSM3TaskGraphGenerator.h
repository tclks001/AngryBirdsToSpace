// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCG/ABTSM3TaskGraphTypes.h"

struct FABTSM2Cell;

/** Deterministic orchestrator for the gameplay-first spherical PCG pipeline. */
class ABTSRUNTIME_API FABTSM3TaskGraphGenerator
{
public:
	bool Generate(
		int32 WorldSeed,
		const FABTSM3PCGConfig& Config,
		const TArray<FABTSM2Cell>& Cells,
		TArray<FABTSM3TaskNode>& OutTasks,
		TArray<FABTSM3TaskLink>& OutTaskLinks,
		TArray<FABTSM3CellState>& OutCellStates,
		TArray<FABTSM3CellEdgeState>& OutEdgeStates,
		FABTSM3PCGSummary& OutSummary) const;
};
