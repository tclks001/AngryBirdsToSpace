// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSM3PCGInternal.h"

#include "Planet/ABTSM2Planet.h"

namespace ABTSM3PCG
{
namespace
{
const FTaskSpec* FindSpec(const EABTSM3TaskType Type)
{
	return GetTaskSpecs().FindByPredicate([Type](const FTaskSpec& Spec) { return Spec.Type == Type; });
}

TPair<float, float> WildernessBand(const EABTSM3TerrainType Terrain)
{
	switch (Terrain)
	{
	case EABTSM3TerrainType::Forest: return {0.15f, 0.38f};
	case EABTSM3TerrainType::Highland: return {0.38f, 0.65f};
	case EABTSM3TerrainType::Mountain: return {0.62f, 0.94f};
	default: return {0.06f, 0.25f};
	}
}
}

void FHeightFieldGenerator::Generate(
	const int32 WorldSeed,
	const int32 AttemptIndex,
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3TaskNode>& Tasks,
	const float MaxBuildSlopeDegrees,
	TArray<FABTSM3CellState>& CellStates) const
{
	FRandomStream Stream(MakeStageSeed(WorldSeed, TEXT("Height"), AttemptIndex));
	TArray<FVector> FeatureDirections;
	TArray<float> FeatureWeights;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		FeatureDirections.Add(Stream.VRand().GetSafeNormal());
		FeatureWeights.Add(Stream.FRandRange(-0.12f, 0.16f));
	}

	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		FABTSM3CellState& State = CellStates[CellId];
		TPair<float, float> Band = WildernessBand(State.TerrainType);
		if (State.TaskId != INDEX_NONE)
		{
			const int32 TaskIndex = FindTaskIndexById(Tasks, State.TaskId);
			if (TaskIndex != INDEX_NONE)
			{
				if (const FTaskSpec* Spec = FindSpec(Tasks[TaskIndex].Type)) Band = {Spec->HeightMin, Spec->HeightMax};
			}
		}
		float Field = 0.5f;
		for (int32 Feature = 0; Feature < FeatureDirections.Num(); ++Feature)
		{
			const float Influence = FMath::Square(FMath::Max(0.0f, FVector::DotProduct(Cells[CellId].UnitCenter, FeatureDirections[Feature])));
			Field += Influence * FeatureWeights[Feature];
		}
		State.LogicalHeight01 = FMath::Lerp(Band.Key, Band.Value, FMath::Clamp(Field, 0.0f, 1.0f));
		State.Moisture01 = FMath::Clamp(0.52f - State.LogicalHeight01 * 0.35f + 0.20f * FMath::Sin(Cells[CellId].UnitCenter.Y * 11.0f), 0.0f, 1.0f);
	}

	// Preserve authored gameplay pads as flat logical anchors.
	for (const FABTSM3TaskNode& Task : Tasks)
	{
		const FTaskSpec* Spec = FindSpec(Task.Type);
		if (!Spec || !Spec->bBuilding || !Cells.IsValidIndex(Task.SeedCellId)) continue;
		const float AnchorHeight = CellStates[Task.SeedCellId].LogicalHeight01;
		for (const int32 Neighbor : Cells[Task.SeedCellId].NeighborCellIds)
		{
			CellStates[Neighbor].LogicalHeight01 = FMath::Lerp(CellStates[Neighbor].LogicalHeight01, AnchorHeight, 0.82f);
		}
		CellStates[Task.SeedCellId].bBuildingAnchor = true;
	}

	for (int32 Iteration = 0; Iteration < 3; ++Iteration)
	{
		TArray<float> Smoothed;
		Smoothed.SetNum(Cells.Num());
		for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
		{
			if (CellStates[CellId].bBuildingAnchor)
			{
				Smoothed[CellId] = CellStates[CellId].LogicalHeight01;
				continue;
			}
			float Sum = CellStates[CellId].LogicalHeight01;
			for (const int32 Neighbor : Cells[CellId].NeighborCellIds) Sum += CellStates[Neighbor].LogicalHeight01;
			Smoothed[CellId] = FMath::Lerp(CellStates[CellId].LogicalHeight01, Sum / (Cells[CellId].NeighborCellIds.Num() + 1), 0.42f);
		}
		for (int32 CellId = 0; CellId < Cells.Num(); ++CellId) CellStates[CellId].LogicalHeight01 = Smoothed[CellId];
	}
	// Re-apply gameplay pads after relaxation so the logical slope test sees the
	// flat footprint later consumed by presentation and construction.
	for (const FABTSM3TaskNode& Task : Tasks)
	{
		if (!Cells.IsValidIndex(Task.SeedCellId) || !CellStates[Task.SeedCellId].bBuildingAnchor) continue;
		const float AnchorHeight = CellStates[Task.SeedCellId].LogicalHeight01;
		for (const int32 Neighbor : Cells[Task.SeedCellId].NeighborCellIds) CellStates[Neighbor].LogicalHeight01 = AnchorHeight;
	}

	constexpr float ApproxCellArcRadians = 0.034f;
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		float MaxDelta = 0.0f;
		for (const int32 Neighbor : Cells[CellId].NeighborCellIds)
		{
			MaxDelta = FMath::Max(MaxDelta, FMath::Abs(CellStates[CellId].LogicalHeight01 - CellStates[Neighbor].LogicalHeight01));
		}
		CellStates[CellId].LogicalSlopeDegrees = FMath::RadiansToDegrees(FMath::Atan2(MaxDelta, ApproxCellArcRadians));
		CellStates[CellId].bBuildable = CellStates[CellId].LogicalSlopeDegrees <= MaxBuildSlopeDegrees && !CellStates[CellId].bWater;
	}
}
}
