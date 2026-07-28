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

float CellArcLengthCM(
	const TArray<FABTSM2Cell>& Cells,
	const int32 CellA,
	const int32 CellB,
	const float PlanetRadiusCM)
{
	if (!Cells.IsValidIndex(CellA) || !Cells.IsValidIndex(CellB)) return 0.0f;
	const float Dot = FVector::DotProduct(Cells[CellA].UnitCenter, Cells[CellB].UnitCenter);
	return FMath::Max(1.0f, PlanetRadiusCM)
		* FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f));
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
	if (CellStates[Start].bBuildingRoadExclusion || CellStates[Goal].bBuildingRoadExclusion) return Empty;
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
			if (CellStates[Neighbor].bBuildingRoadExclusion) continue;
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
	TArray<FABTSM3TaskNode>& Tasks,
	TArray<FABTSM3TaskLink>& Links,
	TArray<FABTSM3CellState>& CellStates,
	TArray<FABTSM3CellEdgeState>& EdgeStates,
	const FABTSM3CellEdgeKey& BridgeEdge,
	const float PlanetRadiusCM) const
{
	for (FABTSM3CellState& State : CellStates)
	{
		State.bRoad = false;
		State.RoadDistance = MAX_int32;
		State.MainRoadDistance = MAX_int32;
		State.ProgressDistance = MAX_int32;
		State.ProgressDistanceCM = 0.0f;
		State.FlowS = 0.0f;
	}
	for (FABTSM3TaskNode& Task : Tasks)
	{
		if (!Cells.IsValidIndex(Task.RoadPortalCellId))
		{
			Task.RoadPortalCellId = Task.SeedCellId;
		}
		Task.RouteProgressDistanceCM = 0.0f;
		Task.FlowS = 0.0f;
	}
	for (FABTSM3TaskLink& Link : Links)
	{
		Link.CorridorCells.Reset();
		Link.CorridorEdges.Reset();
		Link.CorridorLengthCM = 0.0f;
		const int32 TaskAIndex = FindTaskIndexById(Tasks, Link.TaskA);
		const int32 TaskBIndex = FindTaskIndexById(Tasks, Link.TaskB);
		if (TaskAIndex == INDEX_NONE || TaskBIndex == INDEX_NONE) return false;
		int32 PathStart = Tasks[TaskAIndex].RoadPortalCellId;
		int32 PathGoal = Tasks[TaskBIndex].RoadPortalCellId;
		const int32 TargetIndex = FindTaskIndexByType(Tasks, EABTSM3TaskType::TargetBuilding);
		const FVector TargetDirection = TargetIndex != INDEX_NONE
			? Cells[Tasks[TargetIndex].RoadPortalCellId].UnitCenter
			: Cells[BridgeEdge.CellA].UnitCenter;
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
			const EABTSM3TransportType DesiredTransport =
				Link.Role == EABTSM3TaskLinkRole::Branch
				? EABTSM3TransportType::Trail
				: EABTSM3TransportType::MainRoad;
			if (DesiredTransport == EABTSM3TransportType::MainRoad
				|| Edge.Transport == EABTSM3TransportType::None)
			{
				Edge.Transport = DesiredTransport;
			}
			Link.CorridorEdges.Add(Key);
			Link.CorridorLengthCM += CellArcLengthCM(Cells, Path[Index], Path[Index + 1], PlanetRadiusCM);
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
	const int32 LaunchTaskIndex = FindTaskIndexByType(Tasks, EABTSM3TaskType::LaunchSite);
	if (StartTaskIndex == INDEX_NONE || LaunchTaskIndex == INDEX_NONE) return false;

	TArray<int32> OrderedMainRouteCells;
	TArray<float> OrderedMainRouteProgressCM;
	TSet<int32> MainTaskIds;
	TSet<int32> ConsumedMainLinkIds;
	int32 CurrentTaskId = Tasks[StartTaskIndex].TaskId;
	const int32 LaunchTaskId = Tasks[LaunchTaskIndex].TaskId;
	float MainRouteLengthCM = 0.0f;
	MainTaskIds.Add(CurrentTaskId);
	Tasks[StartTaskIndex].RouteProgressDistanceCM = 0.0f;

	for (int32 Guard = 0; CurrentTaskId != LaunchTaskId && Guard < Tasks.Num(); ++Guard)
	{
		const int32 LinkIndex = Links.IndexOfByPredicate([&](const FABTSM3TaskLink& Link)
		{
			const bool bMainRole =
				Link.Role == EABTSM3TaskLinkRole::MainPath
				|| Link.Role == EABTSM3TaskLinkRole::LockedGate;
			return bMainRole
				&& Link.TaskA == CurrentTaskId
				&& !ConsumedMainLinkIds.Contains(Link.LinkId);
		});
		if (LinkIndex == INDEX_NONE) return false;

		const FABTSM3TaskLink& Link = Links[LinkIndex];
		if (Link.CorridorCells.Num() < 2) return false;
		if (OrderedMainRouteCells.IsEmpty())
		{
			OrderedMainRouteCells.Add(Link.CorridorCells[0]);
			OrderedMainRouteProgressCM.Add(MainRouteLengthCM);
		}
		else if (OrderedMainRouteCells.Last() != Link.CorridorCells[0])
		{
			return false;
		}

		for (int32 CellIndex = 1; CellIndex < Link.CorridorCells.Num(); ++CellIndex)
		{
			const int32 PreviousCell = Link.CorridorCells[CellIndex - 1];
			const int32 CurrentCell = Link.CorridorCells[CellIndex];
			MainRouteLengthCM += CellArcLengthCM(Cells, PreviousCell, CurrentCell, PlanetRadiusCM);
			OrderedMainRouteCells.Add(CurrentCell);
			OrderedMainRouteProgressCM.Add(MainRouteLengthCM);
		}

		const int32 TaskBIndex = FindTaskIndexById(Tasks, Link.TaskB);
		if (TaskBIndex == INDEX_NONE) return false;
		Tasks[TaskBIndex].RouteProgressDistanceCM = MainRouteLengthCM;
		MainTaskIds.Add(Link.TaskB);
		ConsumedMainLinkIds.Add(Link.LinkId);
		CurrentTaskId = Link.TaskB;
	}
	if (CurrentTaskId != LaunchTaskId
		|| OrderedMainRouteCells.IsEmpty()
		|| MainRouteLengthCM <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	TArray<int32> ProjectionRouteOrdinal;
	ProjectionRouteOrdinal.Init(INDEX_NONE, Cells.Num());
	TArray<int32> ProjectionHopDistance;
	ProjectionHopDistance.Init(MAX_int32, Cells.Num());
	TQueue<int32> ProjectionQueue;
	for (int32 RouteOrdinal = 0; RouteOrdinal < OrderedMainRouteCells.Num(); ++RouteOrdinal)
	{
		const int32 CellId = OrderedMainRouteCells[RouteOrdinal];
		if (!Cells.IsValidIndex(CellId) || ProjectionRouteOrdinal[CellId] != INDEX_NONE) continue;
		ProjectionRouteOrdinal[CellId] = RouteOrdinal;
		ProjectionHopDistance[CellId] = 0;
		ProjectionQueue.Enqueue(CellId);
	}

	int32 ProjectionCell = INDEX_NONE;
	while (ProjectionQueue.Dequeue(ProjectionCell))
	{
		for (const int32 NeighborId : Cells[ProjectionCell].NeighborCellIds)
		{
			if (!Cells.IsValidIndex(NeighborId) || ProjectionRouteOrdinal[NeighborId] != INDEX_NONE) continue;
			ProjectionRouteOrdinal[NeighborId] = ProjectionRouteOrdinal[ProjectionCell];
			ProjectionHopDistance[NeighborId] = ProjectionHopDistance[ProjectionCell] + 1;
			ProjectionQueue.Enqueue(NeighborId);
		}
	}

	for (int32 CellId = 0; CellId < CellStates.Num(); ++CellId)
	{
		const int32 RouteOrdinal = ProjectionRouteOrdinal[CellId];
		if (!OrderedMainRouteProgressCM.IsValidIndex(RouteOrdinal)) return false;
		CellStates[CellId].ProgressDistance = RouteOrdinal;
		CellStates[CellId].ProgressDistanceCM = OrderedMainRouteProgressCM[RouteOrdinal];
		CellStates[CellId].FlowS = FMath::Clamp(
			CellStates[CellId].ProgressDistanceCM / MainRouteLengthCM,
			0.0f,
			1.0f);
	}

	for (FABTSM3TaskNode& Task : Tasks)
	{
		if (!MainTaskIds.Contains(Task.TaskId))
		{
			if (!CellStates.IsValidIndex(Task.RoadPortalCellId)) return false;
			Task.RouteProgressDistanceCM = CellStates[Task.RoadPortalCellId].ProgressDistanceCM;
		}
		Task.FlowS = FMath::Clamp(Task.RouteProgressDistanceCM / MainRouteLengthCM, 0.0f, 1.0f);
	}
	return true;
}
}
