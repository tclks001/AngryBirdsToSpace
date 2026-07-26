// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSM3PCGInternal.h"

#include "ABTSRuntime.h"
#include "Planet/ABTSM2Planet.h"

namespace ABTSM3PCG
{
namespace
{
	bool IsBuildingTask(const EABTSM3TaskType Type)
	{
		return GetTaskSpecs().ContainsByPredicate([Type](const FTaskSpec& Spec)
		{
			return Spec.Type == Type && Spec.bBuilding;
		});
	}

	bool HasCertifiedClearance(
		const int32 CenterCellId,
		const int32 RingCount,
		const TArray<FABTSM2Cell>& Cells,
		const TArray<FABTSM3CellState>& States)
	{
		if (!Cells.IsValidIndex(CenterCellId) || !States.IsValidIndex(CenterCellId)) return false;
		TSet<int32> Visited;
		TArray<TPair<int32, int32>> Queue;
		Visited.Add(CenterCellId);
		Queue.Add({CenterCellId, 0});
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const int32 CellId = Queue[Head].Key;
			const int32 Depth = Queue[Head].Value;
			const FABTSM3CellState& State = States[CellId];
			if (!State.bBuildable || State.bWater) return false;
			if (Depth >= RingCount) continue;
			for (const int32 NeighborId : Cells[CellId].NeighborCellIds)
			{
				if (!Cells.IsValidIndex(NeighborId) || Visited.Contains(NeighborId)) continue;
				Visited.Add(NeighborId);
				Queue.Add({NeighborId, Depth + 1});
			}
		}
		return true;
	}
}

bool FBuildingPadPlanner::Place(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3TaskNode>& Tasks,
	const int32 ClearanceRingCells,
	TArray<FABTSM3CellState>& CellStates,
	FString& OutFailure) const
{
	OutFailure.Reset();
	if (Cells.Num() != CellStates.Num())
	{
		OutFailure = TEXT("BuildingPadInvalidCellStateCount");
		return false;
	}
	for (FABTSM3CellState& State : CellStates) State.bBuildingAnchor = false;
	const int32 SafeRings = FMath::Clamp(ClearanceRingCells, 1, 4);
	for (const FABTSM3TaskNode& Task : Tasks)
	{
		if (!IsBuildingTask(Task.Type)) continue;
		if (!Cells.IsValidIndex(Task.SeedCellId) || Task.CellIds.IsEmpty())
		{
			OutFailure = FString::Printf(TEXT("BuildingPadInvalidTask:%d"), Task.TaskId);
			return false;
		}
		TArray<int32> Candidates = Task.CellIds;
		Candidates.Sort([&Cells, SeedCellId = Task.SeedCellId](const int32 A, const int32 B)
		{
			const float ScoreA = FVector::DotProduct(Cells[A].UnitCenter, Cells[SeedCellId].UnitCenter);
			const float ScoreB = FVector::DotProduct(Cells[B].UnitCenter, Cells[SeedCellId].UnitCenter);
			return !FMath::IsNearlyEqual(ScoreA, ScoreB) ? ScoreA > ScoreB : A < B;
		});
		const int32* Chosen = Candidates.FindByPredicate([&](const int32 Candidate)
		{
			return HasCertifiedClearance(Candidate, SafeRings, Cells, CellStates);
		});
		if (Chosen == nullptr)
		{
			OutFailure = FString::Printf(TEXT("BuildingPadNoCertifiedFootprint:%d:Rings=%d"), Task.TaskId, SafeRings);
			return false;
		}
		CellStates[*Chosen].bBuildingAnchor = true;
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][PCG][BuildingPad] Task=%d Type=%d Seed=%d Anchor=%d Rings=%d Shifted=%d"),
			Task.TaskId, static_cast<int32>(Task.Type), Task.SeedCellId, *Chosen, SafeRings,
			*Chosen != Task.SeedCellId ? 1 : 0);
	}
	return true;
}
}
