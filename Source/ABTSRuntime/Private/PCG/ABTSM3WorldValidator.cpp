// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSM3PCGInternal.h"

#include "Planet/ABTSM2Planet.h"

namespace ABTSM3PCG
{
namespace
{
bool IsReachable(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3CellEdgeState>& EdgeStates,
	const int32 Start,
	const int32 Goal,
	const bool bBridgeBuilt)
{
	TMap<FABTSM3CellEdgeKey, const FABTSM3CellEdgeState*> EdgeMap;
	for (const FABTSM3CellEdgeState& Edge : EdgeStates) EdgeMap.Add(Edge.Key, &Edge);
	TBitArray<> Visited(false, Cells.Num());
	TQueue<int32> Queue;
	Visited[Start] = true;
	Queue.Enqueue(Start);
	int32 Current = INDEX_NONE;
	while (Queue.Dequeue(Current))
	{
		if (Current == Goal) return true;
		for (const int32 Neighbor : Cells[Current].NeighborCellIds)
		{
			if (Visited[Neighbor]) continue;
			const FABTSM3CellEdgeState* const* EdgePtr = EdgeMap.Find(FABTSM3CellEdgeKey(Current, Neighbor));
			if (EdgePtr && (*EdgePtr)->bBlocksOnFoot)
			{
				const bool bOpenBridge = bBridgeBuilt && ((*EdgePtr)->Crossing == EABTSM3CrossingType::BridgeSite || (*EdgePtr)->Crossing == EABTSM3CrossingType::Bridge);
				const bool bOpenFord = (*EdgePtr)->Crossing == EABTSM3CrossingType::Ford || (*EdgePtr)->Crossing == EABTSM3CrossingType::FallenLog;
				if (!bOpenBridge && !bOpenFord) continue;
			}
			Visited[Neighbor] = true;
			Queue.Enqueue(Neighbor);
		}
	}
	return false;
}
}

bool FWorldValidator::Validate(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3TaskNode>& Tasks,
	const TArray<FABTSM3TaskLink>& Links,
	const TArray<FABTSM3CellState>& CellStates,
	const TArray<FABTSM3CellEdgeState>& EdgeStates,
	const FABTSM3CellEdgeKey& BridgeEdge,
	FABTSM3PCGSummary& Summary,
	FString& OutFailure) const
{
	OutFailure.Reset();
	if (Cells.Num() != CellStates.Num() || Tasks.Num() < 9 || Links.IsEmpty())
	{
		OutFailure = TEXT("InvalidResultSizes");
		return false;
	}
	for (const FABTSM3TaskNode& Task : Tasks)
	{
		if (!Cells.IsValidIndex(Task.SeedCellId) || Task.CellIds.IsEmpty())
		{
			OutFailure = FString::Printf(TEXT("InvalidTask_%d"), Task.TaskId);
			return false;
		}
	}
	for (const FABTSM3TaskLink& Link : Links)
	{
		if (Link.CorridorCells.Num() < 2 || Link.CorridorEdges.Num() + 1 != Link.CorridorCells.Num())
		{
			OutFailure = FString::Printf(TEXT("InvalidCorridor_%d"), Link.LinkId);
			return false;
		}
	}
	const int32 StartIndex = FindTaskIndexByType(Tasks, EABTSM3TaskType::Start);
	const int32 WorkshopIndex = FindTaskIndexByType(Tasks, EABTSM3TaskType::Workshop);
	const int32 TargetIndex = FindTaskIndexByType(Tasks, EABTSM3TaskType::TargetBuilding);
	const int32 FurnaceIndex = FindTaskIndexByType(Tasks, EABTSM3TaskType::FurnaceRuins);
	const int32 LaunchIndex = FindTaskIndexByType(Tasks, EABTSM3TaskType::LaunchSite);
	if (StartIndex == INDEX_NONE || WorkshopIndex == INDEX_NONE || TargetIndex == INDEX_NONE || FurnaceIndex == INDEX_NONE || LaunchIndex == INDEX_NONE)
	{
		OutFailure = TEXT("MissingRequiredTask");
		return false;
	}
	const int32 StartCell = Tasks[StartIndex].SeedCellId;
	Summary.bBridgeLockedBeforeBuild = !IsReachable(Cells, EdgeStates, StartCell, Tasks[FurnaceIndex].SeedCellId, false);
	Summary.bMainPathReachableAfterBridge = IsReachable(Cells, EdgeStates, StartCell, Tasks[LaunchIndex].SeedCellId, true);
	if (!Summary.bBridgeLockedBeforeBuild)
	{
		OutFailure = TEXT("BridgeBypassExists");
		return false;
	}
	if (!Summary.bMainPathReachableAfterBridge)
	{
		OutFailure = TEXT("BridgeDoesNotUnlockMainPath");
		return false;
	}
	if (!IsReachable(Cells, EdgeStates, StartCell, Tasks[WorkshopIndex].SeedCellId, false)
		|| !IsReachable(Cells, EdgeStates, StartCell, Tasks[TargetIndex].SeedCellId, false))
	{
		OutFailure = TEXT("PreBridgeProgressionBlocked");
		return false;
	}
	if (FindEdgeStateIndex(EdgeStates, BridgeEdge) == INDEX_NONE)
	{
		OutFailure = TEXT("MissingBridgeEdge");
		return false;
	}
	TMap<int32, int32> BuildingAnchorCountByTask;
	for (int32 CellId = 0; CellId < CellStates.Num(); ++CellId)
	{
		const FABTSM3CellState& State = CellStates[CellId];
		if (!State.bBuildingAnchor) continue;
		if (!State.bBuildable || State.bWater)
		{
			OutFailure = FString::Printf(TEXT("BuildingAnchorInvalid:%d"), CellId);
			return false;
		}
		if (FindTaskIndexById(Tasks, State.TaskId) == INDEX_NONE)
		{
			OutFailure = FString::Printf(TEXT("BuildingAnchorTaskMissing:%d"), CellId);
			return false;
		}
		BuildingAnchorCountByTask.FindOrAdd(State.TaskId)++;
	}
	for (const FABTSM3TaskNode& Task : Tasks)
	{
		const FTaskSpec* Spec = GetTaskSpecs().FindByPredicate([Type = Task.Type](const FTaskSpec& Candidate) { return Candidate.Type == Type; });
		if (Spec != nullptr && Spec->bBuilding && BuildingAnchorCountByTask.FindRef(Task.TaskId) != 1)
		{
			OutFailure = FString::Printf(TEXT("BuildingAnchorCountInvalid:%d:%d"), Task.TaskId, BuildingAnchorCountByTask.FindRef(Task.TaskId));
			return false;
		}
	}
	Summary.bAccepted = true;
	return true;
}
}
