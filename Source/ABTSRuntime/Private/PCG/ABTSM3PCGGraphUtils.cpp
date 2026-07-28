// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSM3PCGInternal.h"

#include "Planet/ABTSM2Planet.h"

namespace ABTSM3PCG
{
const TArray<FTaskSpec>& GetTaskSpecs()
{
	static const TArray<FTaskSpec> Specs = {
		{EABTSM3TaskType::Start, EABTSM3TerrainType::Plain, 0.08f, 0.16f, 230, false},
		{EABTSM3TaskType::Workshop, EABTSM3TerrainType::Forest, 0.16f, 0.30f, 300, true},
		{EABTSM3TaskType::SlingshotRange, EABTSM3TerrainType::Plain, 0.10f, 0.23f, 270, false},
		{EABTSM3TaskType::TargetBuilding, EABTSM3TerrainType::Highland, 0.38f, 0.62f, 310, true},
		{EABTSM3TaskType::BridgeGate, EABTSM3TerrainType::Plain, 0.12f, 0.28f, 250, false},
		{EABTSM3TaskType::FurnaceRuins, EABTSM3TerrainType::Mountain, 0.66f, 0.90f, 330, true},
		{EABTSM3TaskType::LaunchSite, EABTSM3TerrainType::Highland, 0.44f, 0.66f, 270, true},
		{EABTSM3TaskType::Scout, EABTSM3TerrainType::Forest, 0.18f, 0.38f, 230, false},
		{EABTSM3TaskType::SatelliteWindow, EABTSM3TerrainType::Highland, 0.48f, 0.72f, 270, false},
	};
	return Specs;
}

uint32 MakeStageSeed(const int32 WorldSeed, const TCHAR* StageTag, const int32 AttemptIndex)
{
	uint32 Hash = HashCombineFast(GetTypeHash(WorldSeed), GetTypeHash(FString(StageTag)));
	Hash = HashCombineFast(Hash, GetTypeHash(AttemptIndex));
	Hash = HashCombineFast(Hash, GetTypeHash(GeneratorVersion));
	return HashCombineFast(Hash, GetTypeHash(LayoutPolicyVersion));
}

int32 FindTaskIndexById(const TArray<FABTSM3TaskNode>& Tasks, const int32 TaskId)
{
	return Tasks.IndexOfByPredicate([TaskId](const FABTSM3TaskNode& Task) { return Task.TaskId == TaskId; });
}

int32 FindTaskIndexByType(const TArray<FABTSM3TaskNode>& Tasks, const EABTSM3TaskType Type)
{
	return Tasks.IndexOfByPredicate([Type](const FABTSM3TaskNode& Task) { return Task.Type == Type; });
}

int32 FindEdgeStateIndex(const TArray<FABTSM3CellEdgeState>& Edges, const FABTSM3CellEdgeKey& Key)
{
	return Edges.IndexOfByPredicate([&Key](const FABTSM3CellEdgeState& State) { return State.Key == Key; });
}

FABTSM3CellEdgeState& FindOrAddEdgeState(TArray<FABTSM3CellEdgeState>& Edges, const FABTSM3CellEdgeKey& Key)
{
	const int32 Existing = FindEdgeStateIndex(Edges, Key);
	if (Existing != INDEX_NONE) return Edges[Existing];
	FABTSM3CellEdgeState& State = Edges.AddDefaulted_GetRef();
	State.Key = Key;
	return State;
}

TArray<int32> FindUnweightedPath(
	const TArray<FABTSM2Cell>& Cells,
	const int32 StartCellId,
	const int32 GoalCellId,
	const TSet<int32>* BlockedCells)
{
	TArray<int32> Path;
	if (!Cells.IsValidIndex(StartCellId) || !Cells.IsValidIndex(GoalCellId)) return Path;
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
			if (Parent[Neighbor] != INDEX_NONE || (BlockedCells && BlockedCells->Contains(Neighbor) && Neighbor != GoalCellId)) continue;
			Parent[Neighbor] = Current;
			Queue.Enqueue(Neighbor);
		}
	}
	if (Parent[GoalCellId] == INDEX_NONE) return Path;
	for (int32 CellId = GoalCellId; CellId != StartCellId; CellId = Parent[CellId]) Path.Add(CellId);
	Path.Add(StartCellId);
	Algo::Reverse(Path);
	return Path;
}

void BuildDistanceField(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<int32>& Sources,
	TArray<int32>& OutDistance,
	const TSet<int32>* BlockedCells)
{
	OutDistance.Init(MAX_int32, Cells.Num());
	TQueue<int32> Queue;
	for (const int32 Source : Sources)
	{
		if (!Cells.IsValidIndex(Source) || (BlockedCells && BlockedCells->Contains(Source))) continue;
		if (OutDistance[Source] == 0) continue;
		OutDistance[Source] = 0;
		Queue.Enqueue(Source);
	}
	int32 Current = INDEX_NONE;
	while (Queue.Dequeue(Current))
	{
		for (const int32 Neighbor : Cells[Current].NeighborCellIds)
		{
			if (BlockedCells && BlockedCells->Contains(Neighbor)) continue;
			if (OutDistance[Neighbor] <= OutDistance[Current] + 1) continue;
			OutDistance[Neighbor] = OutDistance[Current] + 1;
			Queue.Enqueue(Neighbor);
		}
	}
}
}
