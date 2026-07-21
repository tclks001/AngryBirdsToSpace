// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCG/ABTSM3TaskGraphTypes.h"

struct FABTSM2Cell;

/** Deterministic logical generator. It never reads render vertices, materials, collision, or HISM instances. */
class ABTSRUNTIME_API FABTSM3TaskGraphGenerator
{
public:
	bool Generate(
		int32 WorldSeed,
		const TArray<FABTSM2Cell>& Cells,
		TArray<FABTSM3TaskNode>& OutTasks,
		TArray<FABTSM3CellState>& OutCellStates) const;

private:
	static TArray<int32> FindPath(const TArray<FABTSM2Cell>& Cells, int32 StartCellId, int32 GoalCellId);
	static void AssignTaskRegions(const TArray<FABTSM2Cell>& Cells, TArray<FABTSM3TaskNode>& Tasks, TArray<FABTSM3CellState>& CellStates);
	static void BuildRoads(const TArray<FABTSM2Cell>& Cells, const TArray<FABTSM3TaskNode>& Tasks, TArray<FABTSM3CellState>& CellStates);
};

