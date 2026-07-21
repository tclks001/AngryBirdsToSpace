// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3TaskGraphGenerator.h"

#include "Planet/ABTSM2Planet.h"

namespace
{
	struct FTaskTemplateEntry
	{
		EABTSM3TaskType Type;
		float RouteAlpha;
		EABTSM3TerrainType Terrain;
		float Height01;
		bool bBuilding;
	};

	const FTaskTemplateEntry TaskTemplate[] =
	{
		{EABTSM3TaskType::Start, 0.00f, EABTSM3TerrainType::Plain, 0.08f, false},
		{EABTSM3TaskType::Workshop, 0.16f, EABTSM3TerrainType::Forest, 0.22f, true},
		{EABTSM3TaskType::SlingshotRange, 0.31f, EABTSM3TerrainType::Plain, 0.12f, false},
		{EABTSM3TaskType::TargetBuilding, 0.47f, EABTSM3TerrainType::Highland, 0.56f, true},
		{EABTSM3TaskType::BridgeGate, 0.62f, EABTSM3TerrainType::Plain, 0.10f, false},
		{EABTSM3TaskType::FurnaceRuins, 0.79f, EABTSM3TerrainType::Mountain, 0.86f, true},
		{EABTSM3TaskType::LaunchSite, 1.00f, EABTSM3TerrainType::Highland, 0.48f, true},
	};

	float TerrainHeightBias(const EABTSM3TerrainType Terrain)
	{
		switch (Terrain)
		{
		case EABTSM3TerrainType::Forest: return 0.12f;
		case EABTSM3TerrainType::Highland: return 0.42f;
		case EABTSM3TerrainType::Mountain: return 0.78f;
		case EABTSM3TerrainType::Water: return 0.0f;
		default: return 0.06f;
		}
	}
}

bool FABTSM3TaskGraphGenerator::Generate(
	const int32 WorldSeed,
	const TArray<FABTSM2Cell>& Cells,
	TArray<FABTSM3TaskNode>& OutTasks,
	TArray<FABTSM3CellState>& OutCellStates) const
{
	OutTasks.Reset();
	OutCellStates.SetNum(Cells.Num());
	if (Cells.Num() < UE_ARRAY_COUNT(TaskTemplate))
	{
		return false;
	}

	FRandomStream Stream(WorldSeed);
	const FVector RouteAxis = Stream.VRand().GetSafeNormal();
	const FVector StartDirection = (RouteAxis + FVector::UpVector * 0.35f).GetSafeNormal();
	const FVector EndDirection = (-RouteAxis + FVector::RightVector * 0.25f).GetSafeNormal();

	auto FindNearestCell = [&Cells](const FVector& Direction)
	{
		int32 BestCell = 0;
		float BestDot = -2.0f;
		for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
		{
			const float Dot = FVector::DotProduct(Cells[CellId].UnitCenter, Direction);
			if (Dot > BestDot)
			{
				BestDot = Dot;
				BestCell = CellId;
			}
		}
		return BestCell;
	};

	for (int32 TaskIndex = 0; TaskIndex < UE_ARRAY_COUNT(TaskTemplate); ++TaskIndex)
	{
		const FTaskTemplateEntry& Entry = TaskTemplate[TaskIndex];
		const FVector DesiredDirection = FMath::Lerp(StartDirection, EndDirection, Entry.RouteAlpha).GetSafeNormal();
		FABTSM3TaskNode& Task = OutTasks.AddDefaulted_GetRef();
		Task.TaskId = TaskIndex;
		Task.Type = Entry.Type;
		Task.SeedCellId = FindNearestCell(DesiredDirection);
		if (TaskIndex > 0)
		{
			Task.LinkedTaskIds.Add(TaskIndex - 1);
			OutTasks[TaskIndex - 1].LinkedTaskIds.Add(TaskIndex);
		}
	}

	AssignTaskRegions(Cells, OutTasks, OutCellStates);

	// Water is a TaskGraph crossing corridor, not a low-point classification of
	// the whole BridgeGate region. Reserve the gate anchor and a narrow pair of
	// adjacent Cells for M3's water presentation; later gameplay owns the edge
	// crossing state independently.
	const FABTSM3TaskNode& BridgeTask = OutTasks[4];
	TArray<int32, TInlineAllocator<3>> WaterCorridorCells;
	WaterCorridorCells.Add(BridgeTask.SeedCellId);
	for (const int32 Neighbor : Cells[BridgeTask.SeedCellId].NeighborCellIds)
	{
		if (WaterCorridorCells.Num() >= 3) break;
		WaterCorridorCells.Add(Neighbor);
	}
	for (const int32 CellId : WaterCorridorCells)
	{
		OutCellStates[CellId].TerrainType = EABTSM3TerrainType::Water;
		OutCellStates[CellId].LogicalHeight01 = 0.0f;
		OutCellStates[CellId].bWater = true;
	}
	BuildRoads(Cells, OutTasks, OutCellStates);
	return true;
}

void FABTSM3TaskGraphGenerator::AssignTaskRegions(
	const TArray<FABTSM2Cell>& Cells,
	TArray<FABTSM3TaskNode>& Tasks,
	TArray<FABTSM3CellState>& CellStates)
{
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		int32 BestTaskIndex = 0;
		int32 BestDistance = MAX_int32;
		for (int32 TaskIndex = 0; TaskIndex < Tasks.Num(); ++TaskIndex)
		{
			const float Dot = FVector::DotProduct(Cells[CellId].UnitCenter, Cells[Tasks[TaskIndex].SeedCellId].UnitCenter);
			const int32 ApproxDistance = FMath::RoundToInt(FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f)) * 10000.0f);
			if (ApproxDistance < BestDistance)
			{
				BestDistance = ApproxDistance;
				BestTaskIndex = TaskIndex;
			}
		}

		FABTSM3CellState& State = CellStates[CellId];
		State.TaskId = Tasks[BestTaskIndex].TaskId;
		State.TerrainType = TaskTemplate[BestTaskIndex].Terrain;
		State.LogicalHeight01 = FMath::Max(TaskTemplate[BestTaskIndex].Height01, TerrainHeightBias(State.TerrainType));
		State.bWater = State.TerrainType == EABTSM3TerrainType::Water;
		State.bBuildingAnchor = CellId == Tasks[BestTaskIndex].SeedCellId && TaskTemplate[BestTaskIndex].bBuilding;
		Tasks[BestTaskIndex].CellIds.Add(CellId);
	}
}

TArray<int32> FABTSM3TaskGraphGenerator::FindPath(const TArray<FABTSM2Cell>& Cells, const int32 StartCellId, const int32 GoalCellId)
{
	TArray<int32> Parent;
	Parent.Init(INDEX_NONE, Cells.Num());
	TQueue<int32> Queue;
	Parent[StartCellId] = StartCellId;
	Queue.Enqueue(StartCellId);

	int32 Current = INDEX_NONE;
	while (Queue.Dequeue(Current))
	{
		if (Current == GoalCellId) break;
		for (const int32 Neighbor : Cells[Current].NeighborCellIds)
		{
			if (Parent[Neighbor] == INDEX_NONE)
			{
				Parent[Neighbor] = Current;
				Queue.Enqueue(Neighbor);
			}
		}
	}

	TArray<int32> Path;
	if (!Parent.IsValidIndex(GoalCellId) || Parent[GoalCellId] == INDEX_NONE) return Path;
	for (int32 CellId = GoalCellId; CellId != StartCellId; CellId = Parent[CellId]) Path.Add(CellId);
	Path.Add(StartCellId);
	Algo::Reverse(Path);
	return Path;
}

void FABTSM3TaskGraphGenerator::BuildRoads(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3TaskNode>& Tasks,
	TArray<FABTSM3CellState>& CellStates)
{
	TQueue<int32> DistanceQueue;
	for (int32 TaskIndex = 1; TaskIndex < Tasks.Num(); ++TaskIndex)
	{
		for (const int32 CellId : FindPath(Cells, Tasks[TaskIndex - 1].SeedCellId, Tasks[TaskIndex].SeedCellId))
		{
			if (!CellStates[CellId].bRoad)
			{
				CellStates[CellId].bRoad = true;
				CellStates[CellId].RoadDistance = 0;
				DistanceQueue.Enqueue(CellId);
			}
		}
	}

	int32 Current = INDEX_NONE;
	while (DistanceQueue.Dequeue(Current))
	{
		for (const int32 Neighbor : Cells[Current].NeighborCellIds)
		{
			if (CellStates[Neighbor].RoadDistance > CellStates[Current].RoadDistance + 1)
			{
				CellStates[Neighbor].RoadDistance = CellStates[Current].RoadDistance + 1;
				DistanceQueue.Enqueue(Neighbor);
			}
		}
	}
}
