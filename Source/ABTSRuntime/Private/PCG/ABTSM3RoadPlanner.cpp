// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSM3PCGInternal.h"

#include "Planet/ABTSM2Planet.h"

namespace ABTSM3PCG
{
namespace
{
struct FOpenNode
{
	int32 CellId = INDEX_NONE;
	float Cost = 0.0f;
	bool operator<(const FOpenNode& Other) const { return Cost < Other.Cost; }
};

float TerrainCost(const FABTSM3CellState& State)
{
	switch (State.TerrainType)
	{
	case EABTSM3TerrainType::Forest: return 0.65f;
	case EABTSM3TerrainType::Highland: return 0.85f;
	case EABTSM3TerrainType::Mountain: return 2.4f;
	case EABTSM3TerrainType::Water: return 1.2f;
	default: return 0.0f;
	}
}

TArray<int32> FindWeightedPath(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3CellState>& CellStates,
	const TArray<FABTSM3CellEdgeState>& EdgeStates,
	const FABTSM3TaskLink& Link,
	const int32 Start,
	const int32 Goal)
{
	TArray<int32> Empty;
	if (!Cells.IsValidIndex(Start) || !Cells.IsValidIndex(Goal)) return Empty;
	TMap<FABTSM3CellEdgeKey, const FABTSM3CellEdgeState*> EdgeMap;
	for (const FABTSM3CellEdgeState& Edge : EdgeStates) EdgeMap.Add(Edge.Key, &Edge);
	TArray<float> Distance;
	Distance.Init(TNumericLimits<float>::Max(), Cells.Num());
	TArray<int32> Parent;
	Parent.Init(INDEX_NONE, Cells.Num());
	TArray<FOpenNode> Open;
	Open.Reserve(Cells.Num());
	Distance[Start] = 0.0f;
	Open.HeapPush({Start, 0.0f});
	int32 PopCount = 0;

	while (!Open.IsEmpty())
	{
		++PopCount;
		if (PopCount > Cells.Num() * 64)
		{
			UE_LOG(LogTemp, Error, TEXT("[ABTS][PCG][RoadLink] Search overflow Start=%d Goal=%d Open=%d"), Start, Goal, Open.Num());
			return Empty;
		}
		FOpenNode Node;
		Open.HeapPop(Node, EAllowShrinking::No);
		if (Node.Cost > Distance[Node.CellId] + KINDA_SMALL_NUMBER) continue;
		if (Node.CellId == Goal) break;
		for (const int32 Neighbor : Cells[Node.CellId].NeighborCellIds)
		{
			const FABTSM3CellEdgeKey Key(Node.CellId, Neighbor);
			const FABTSM3CellEdgeState* const* EdgePtr = EdgeMap.Find(Key);
			if (EdgePtr && (*EdgePtr)->bBlocksOnFoot)
			{
				if ((*EdgePtr)->Crossing != EABTSM3CrossingType::BridgeSite) continue;
			}
			const float SlopeDelta = FMath::Abs(CellStates[Node.CellId].LogicalHeight01 - CellStates[Neighbor].LogicalHeight01);
			float StepCost = 1.0f + TerrainCost(CellStates[Neighbor]) + SlopeDelta * 18.0f;
			if (CellStates[Neighbor].bBuildingAnchor && Neighbor != Goal) StepCost += 40.0f;
			if (CellStates[Neighbor].bRoad) StepCost -= 0.35f;
			const float NewCost = Distance[Node.CellId] + FMath::Max(0.2f, StepCost);
			if (NewCost >= Distance[Neighbor]) continue;
			Distance[Neighbor] = NewCost;
			Parent[Neighbor] = Node.CellId;
			Open.HeapPush({Neighbor, NewCost});
		}
	}
	if (Parent[Goal] == INDEX_NONE && Start != Goal) return Empty;
	TArray<int32> Path;
	for (int32 CellId = Goal; CellId != Start; CellId = Parent[CellId]) Path.Add(CellId);
	Path.Add(Start);
	Algo::Reverse(Path);
	return Path;
}
}

bool FRoadPlanner::Build(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3TaskNode>& Tasks,
	TArray<FABTSM3TaskLink>& Links,
	TArray<FABTSM3CellState>& CellStates,
	TArray<FABTSM3CellEdgeState>& EdgeStates,
	const FABTSM3CellEdgeKey& BridgeEdge) const
{
	for (FABTSM3CellState& State : CellStates)
	{
		State.bRoad = false;
		State.RoadDistance = MAX_int32;
		State.MainRoadDistance = MAX_int32;
		State.ProgressDistance = MAX_int32;
	}
	for (FABTSM3TaskLink& Link : Links)
	{
		Link.CorridorCells.Reset();
		Link.CorridorEdges.Reset();
		const int32 TaskAIndex = FindTaskIndexById(Tasks, Link.TaskA);
		const int32 TaskBIndex = FindTaskIndexById(Tasks, Link.TaskB);
		if (TaskAIndex == INDEX_NONE || TaskBIndex == INDEX_NONE) return false;
		int32 PathStart = Tasks[TaskAIndex].SeedCellId;
		int32 PathGoal = Tasks[TaskBIndex].SeedCellId;
		const int32 TargetIndex = FindTaskIndexByType(Tasks, EABTSM3TaskType::TargetBuilding);
		const FVector TargetDirection = TargetIndex != INDEX_NONE ? Cells[Tasks[TargetIndex].SeedCellId].UnitCenter : Cells[BridgeEdge.CellA].UnitCenter;
		const bool bAIsPreBridge = FVector::DotProduct(Cells[BridgeEdge.CellA].UnitCenter, TargetDirection)
			>= FVector::DotProduct(Cells[BridgeEdge.CellB].UnitCenter, TargetDirection);
		const int32 PreBridgeCell = bAIsPreBridge ? BridgeEdge.CellA : BridgeEdge.CellB;
		const int32 PostBridgeCell = bAIsPreBridge ? BridgeEdge.CellB : BridgeEdge.CellA;
		const bool bTaskAIsBridge = Tasks[TaskAIndex].Type == EABTSM3TaskType::BridgeGate;
		const bool bTaskBIsBridge = Tasks[TaskBIndex].Type == EABTSM3TaskType::BridgeGate;
		if (bTaskBIsBridge) PathGoal = PreBridgeCell;
		if (bTaskAIsBridge) PathStart = Link.Role == EABTSM3TaskLinkRole::LockedGate ? PostBridgeCell : PreBridgeCell;
		TArray<int32> Path = FindWeightedPath(Cells, CellStates, EdgeStates, Link, PathStart, PathGoal);
		if (Path.Num() < 2) return false;
		if (Link.Role == EABTSM3TaskLinkRole::LockedGate)
		{
			if (PathStart != PostBridgeCell) return false;
			Path.Insert(PreBridgeCell, 0);
		}
		Link.CorridorCells = Path;
		for (int32 Index = 0; Index + 1 < Path.Num(); ++Index)
		{
			const FABTSM3CellEdgeKey Key(Path[Index], Path[Index + 1]);
			FABTSM3CellEdgeState& Edge = FindOrAddEdgeState(EdgeStates, Key);
			Edge.Transport = Link.Role == EABTSM3TaskLinkRole::Branch ? EABTSM3TransportType::Trail : EABTSM3TransportType::MainRoad;
			Link.CorridorEdges.Add(Key);
			CellStates[Path[Index]].bRoad = true;
		}
		CellStates[Path.Last()].bRoad = true;
	}

	if (!Links.ContainsByPredicate([&](const FABTSM3TaskLink& Link) { return Link.CorridorEdges.Contains(BridgeEdge); })) return false;

	TArray<int32> AllRoadCells;
	TArray<int32> MainRoadCells;
	for (int32 CellId = 0; CellId < CellStates.Num(); ++CellId)
	{
		if (CellStates[CellId].bRoad) AllRoadCells.Add(CellId);
	}
	for (const FABTSM3TaskLink& Link : Links)
	{
		if (Link.Role == EABTSM3TaskLinkRole::MainPath || Link.Role == EABTSM3TaskLinkRole::LockedGate)
		{
			MainRoadCells.Append(Link.CorridorCells);
		}
	}
	TArray<int32> NearestDistance;
	TArray<int32> MainDistance;
	BuildDistanceField(Cells, AllRoadCells, NearestDistance);
	BuildDistanceField(Cells, MainRoadCells, MainDistance);
	for (int32 CellId = 0; CellId < CellStates.Num(); ++CellId)
	{
		CellStates[CellId].RoadDistance = NearestDistance[CellId];
		CellStates[CellId].MainRoadDistance = MainDistance[CellId];
	}

	const int32 StartTaskIndex = FindTaskIndexByType(Tasks, EABTSM3TaskType::Start);
	if (StartTaskIndex == INDEX_NONE) return false;
	TArray<int32> StartSource = {Tasks[StartTaskIndex].SeedCellId};
	BuildDistanceField(Cells, StartSource, NearestDistance);
	for (int32 CellId = 0; CellId < CellStates.Num(); ++CellId) CellStates[CellId].ProgressDistance = NearestDistance[CellId];
	return true;
}
}
