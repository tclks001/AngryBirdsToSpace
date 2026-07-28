// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSM3PCGInternal.h"

#include "Planet/ABTSM2Planet.h"

namespace ABTSM3PCG
{
namespace
{
int32 FindNearestCell(const TArray<FABTSM2Cell>& Cells, const FVector& Direction)
{
	int32 BestCell = INDEX_NONE;
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
}

int32 FindNearestSeparatedCell(
	const TArray<FABTSM2Cell>& Cells,
	const FVector& DesiredDirection,
	const FVector& SeparationOriginDirection,
	const float MinimumSeparationDegrees,
	const TSet<int32>& ExcludedCells)
{
	const float MaximumOriginDot = FMath::Cos(FMath::DegreesToRadians(
		FMath::Clamp(MinimumSeparationDegrees, 0.0f, 179.0f)));
	int32 BestCell = INDEX_NONE;
	float BestDesiredDot = -2.0f;
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		if (ExcludedCells.Contains(CellId)
			|| FVector::DotProduct(Cells[CellId].UnitCenter, SeparationOriginDirection) > MaximumOriginDot)
		{
			continue;
		}
		const float DesiredDot = FVector::DotProduct(Cells[CellId].UnitCenter, DesiredDirection);
		if (DesiredDot > BestDesiredDot)
		{
			BestDesiredDot = DesiredDot;
			BestCell = CellId;
		}
	}
	return BestCell;
}

EABTSM3TerrainType TerrainForTask(const EABTSM3TaskType Type)
{
	for (const FTaskSpec& Spec : GetTaskSpecs()) if (Spec.Type == Type) return Spec.Terrain;
	return EABTSM3TerrainType::Plain;
}
}

bool FSpatialBuilder::PlaceTaskSeeds(
	const int32 WorldSeed,
	const int32 AttemptIndex,
	const float MainRouteAngularSpanDegrees,
	const float MinSatelliteLaunchAngularSeparationDegrees,
	const TArray<FABTSM2Cell>& Cells,
	TArray<FABTSM3TaskNode>& Tasks) const
{
	if (Cells.IsEmpty() || Tasks.Num() < 9) return false;
	FRandomStream Stream(MakeStageSeed(WorldSeed, TEXT("TaskSeeds"), AttemptIndex));
	const FVector Start = Stream.VRand().GetSafeNormal();
	FVector TangentX = FVector::VectorPlaneProject(Stream.VRand(), Start).GetSafeNormal();
	if (TangentX.IsNearlyZero()) TangentX = FVector::CrossProduct(Start, FVector::UpVector).GetSafeNormal();
	const FVector TangentY = FVector::CrossProduct(Start, TangentX).GetSafeNormal();
	const float Bend = Stream.FRandRange(-0.32f, 0.32f);

	const float MainRouteSpanRadians = FMath::DegreesToRadians(
		FMath::Clamp(MainRouteAngularSpanDegrees, 1.0f, 170.0f));
	static constexpr float MainRouteProgress[] = {
		0.0f,
		0.0952381f,
		0.3714286f,
		0.5476190f,
		0.6571429f,
		0.8476190f,
		1.0f
	};
	TSet<int32> Used;
	for (int32 Index = 0; Index < 7; ++Index)
	{
		const float Angle = MainRouteSpanRadians * MainRouteProgress[Index];
		const float Side = FMath::Sin(Angle * 2.1f + Bend) * 0.25f;
		const FVector Desired = (Start * FMath::Cos(Angle) + TangentX * FMath::Sin(Angle) + TangentY * Side).GetSafeNormal();
		Tasks[Index].SeedCellId = FindNearestCell(Cells, Desired);
		if (Used.Contains(Tasks[Index].SeedCellId)) return false;
		Used.Add(Tasks[Index].SeedCellId);
		Tasks[Index].RoadPortalCellId = Tasks[Index].SeedCellId;
	}

	const FVector BranchOrigin = Cells[Tasks[2].SeedCellId].UnitCenter;
	const FVector BranchSide = FVector::VectorPlaneProject(TangentY + TangentX * Stream.FRandRange(-0.25f, 0.25f), BranchOrigin).GetSafeNormal();
	const FVector ScoutDirection = (BranchOrigin * 0.92f + BranchSide * 0.42f).GetSafeNormal();
	const FVector SatelliteDirection = (ScoutDirection * 0.86f + BranchSide * 0.52f + TangentX * 0.12f).GetSafeNormal();
	Tasks[7].SeedCellId = FindNearestCell(Cells, ScoutDirection);
	if (Used.Contains(Tasks[7].SeedCellId)) return false;
	Used.Add(Tasks[7].SeedCellId);
	Tasks[7].RoadPortalCellId = Tasks[7].SeedCellId;
	Tasks[8].SeedCellId = FindNearestSeparatedCell(
		Cells,
		SatelliteDirection,
		Cells[Tasks[6].SeedCellId].UnitCenter,
		// The LaunchSite pad may move from its Task seed to a nearby certified
		// buildable Cell. Keep a small deterministic margin so the validator can
		// still certify the final anchor against the public minimum.
		FMath::Min(179.0f, MinSatelliteLaunchAngularSeparationDegrees + 5.0f),
		Used);
	if (!Cells.IsValidIndex(Tasks[8].SeedCellId)) return false;
	Tasks[8].RoadPortalCellId = Tasks[8].SeedCellId;
	return true;
}

bool FSpatialBuilder::GrowTaskRegions(
	const int32 WorldSeed,
	const int32 AttemptIndex,
	const int32 TargetCells,
	const TArray<FABTSM2Cell>& Cells,
	TArray<FABTSM3TaskNode>& Tasks,
	TArray<FABTSM3CellState>& CellStates) const
{
	CellStates.Reset();
	CellStates.SetNum(Cells.Num());
	for (FABTSM3TaskNode& Task : Tasks) Task.CellIds.Reset();
	FRandomStream Stream(MakeStageSeed(WorldSeed, TEXT("Regions"), AttemptIndex));
	const TArray<FTaskSpec>& Specs = GetTaskSpecs();
	TArray<TQueue<int32>> Frontiers;
	Frontiers.SetNum(Tasks.Num());
	TArray<int32> Budgets;
	Budgets.SetNum(Tasks.Num());

	for (int32 TaskIndex = 0; TaskIndex < Tasks.Num(); ++TaskIndex)
	{
		if (!Cells.IsValidIndex(Tasks[TaskIndex].SeedCellId)) return false;
		const int32 SpecIndex = Specs.IndexOfByPredicate([&](const FTaskSpec& Spec) { return Spec.Type == Tasks[TaskIndex].Type; });
		const float SpecScale = SpecIndex == INDEX_NONE ? 1.0f : static_cast<float>(Specs[SpecIndex].TargetCells) / 280.0f;
		Budgets[TaskIndex] = FMath::Max(24, FMath::RoundToInt(TargetCells * SpecScale * Stream.FRandRange(0.88f, 1.12f)));
		CellStates[Tasks[TaskIndex].SeedCellId].TaskId = Tasks[TaskIndex].TaskId;
		Tasks[TaskIndex].CellIds.Add(Tasks[TaskIndex].SeedCellId);
		Frontiers[TaskIndex].Enqueue(Tasks[TaskIndex].SeedCellId);
	}

	bool bProgress = true;
	while (bProgress)
	{
		bProgress = false;
		for (int32 TaskIndex = 0; TaskIndex < Tasks.Num(); ++TaskIndex)
		{
			if (Tasks[TaskIndex].CellIds.Num() >= Budgets[TaskIndex]) continue;
			int32 Current = INDEX_NONE;
			if (!Frontiers[TaskIndex].Dequeue(Current)) continue;
			for (const int32 Neighbor : Cells[Current].NeighborCellIds)
			{
				if (CellStates[Neighbor].TaskId != INDEX_NONE) continue;
				CellStates[Neighbor].TaskId = Tasks[TaskIndex].TaskId;
				Tasks[TaskIndex].CellIds.Add(Neighbor);
				Frontiers[TaskIndex].Enqueue(Neighbor);
				bProgress = true;
				if (Tasks[TaskIndex].CellIds.Num() >= Budgets[TaskIndex]) break;
			}
		}
	}

	for (int32 CellId = 0; CellId < CellStates.Num(); ++CellId)
	{
		FABTSM3CellState& State = CellStates[CellId];
		if (State.TaskId != INDEX_NONE)
		{
			const int32 TaskIndex = FindTaskIndexById(Tasks, State.TaskId);
			State.TerrainType = TerrainForTask(Tasks[TaskIndex].Type);
			continue;
		}
		const float N0 = FMath::Sin(Cells[CellId].UnitCenter.X * 9.7f + Cells[CellId].UnitCenter.Z * 4.1f + static_cast<float>(WorldSeed) * 0.001f);
		const float N1 = FMath::Cos(Cells[CellId].UnitCenter.Y * 8.3f - Cells[CellId].UnitCenter.Z * 6.7f);
		const float Noise = 0.5f * (N0 + N1);
		State.TerrainType = Noise > 0.48f ? EABTSM3TerrainType::Mountain : Noise > 0.02f ? EABTSM3TerrainType::Forest : Noise < -0.58f ? EABTSM3TerrainType::Highland : EABTSM3TerrainType::Plain;
	}
	return Tasks.ContainsByPredicate([](const FABTSM3TaskNode& Task) { return Task.CellIds.IsEmpty(); }) == false;
}
}
