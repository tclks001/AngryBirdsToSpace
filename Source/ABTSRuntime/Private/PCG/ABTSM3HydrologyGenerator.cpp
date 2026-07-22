// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSM3PCGInternal.h"

#include "Planet/ABTSM2Planet.h"

namespace ABTSM3PCG
{
namespace
{
void MarkWaterCell(TArray<FABTSM3CellState>& CellStates, const int32 CellId)
{
	if (!CellStates.IsValidIndex(CellId)) return;
	CellStates[CellId].bWater = true;
	CellStates[CellId].TerrainType = EABTSM3TerrainType::Water;
	CellStates[CellId].bBuildable = false;
}
}

bool FHydrologyGenerator::Generate(
	const int32 WorldSeed,
	const int32 AttemptIndex,
	const int32 StreamThreshold,
	const float BarrierHalfWidthCells,
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3TaskNode>& Tasks,
	TArray<FABTSM3CellState>& CellStates,
	TArray<FABTSM3CellEdgeState>& EdgeStates,
	FABTSM3CellEdgeKey& OutBridgeEdge) const
{
	EdgeStates.Reset();
	OutBridgeEdge = FABTSM3CellEdgeKey();
	if (Cells.Num() != CellStates.Num()) return false;
	TMap<FABTSM3CellEdgeKey, int32> EdgeIndexByKey;
	EdgeIndexByKey.Reserve(Cells.Num());
	auto FindOrAddEdge = [&EdgeStates, &EdgeIndexByKey](const FABTSM3CellEdgeKey& Key) -> FABTSM3CellEdgeState&
	{
		if (const int32* Existing = EdgeIndexByKey.Find(Key)) return EdgeStates[*Existing];
		const int32 NewIndex = EdgeStates.AddDefaulted();
		EdgeStates[NewIndex].Key = Key;
		EdgeIndexByKey.Add(Key, NewIndex);
		return EdgeStates[NewIndex];
	};

	TArray<int32> Downstream;
	Downstream.Init(INDEX_NONE, Cells.Num());
	TArray<float> Flow;
	Flow.Init(1.0f, Cells.Num());
	TArray<int32> Order;
	Order.Reserve(Cells.Num());
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		Order.Add(CellId);
		float BestHeight = CellStates[CellId].LogicalHeight01;
		uint32 BestTie = MAX_uint32;
		for (const int32 Neighbor : Cells[CellId].NeighborCellIds)
		{
			const float CandidateHeight = CellStates[Neighbor].LogicalHeight01;
			const uint32 Tie = GetTypeHash(FABTSM3CellEdgeKey(CellId, Neighbor));
			if (CandidateHeight < BestHeight - KINDA_SMALL_NUMBER || (FMath::IsNearlyEqual(CandidateHeight, BestHeight) && Tie < BestTie))
			{
				BestHeight = CandidateHeight;
				BestTie = Tie;
				Downstream[CellId] = Neighbor;
			}
		}
	}
	Order.Sort([&](const int32 A, const int32 B) { return CellStates[A].LogicalHeight01 > CellStates[B].LogicalHeight01; });
	for (const int32 CellId : Order)
	{
		if (Downstream[CellId] != INDEX_NONE) Flow[Downstream[CellId]] += Flow[CellId];
	}

	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		const int32 Next = Downstream[CellId];
		if (Next == INDEX_NONE || Flow[CellId] < StreamThreshold) continue;
		FABTSM3CellEdgeState& Edge = FindOrAddEdge(FABTSM3CellEdgeKey(CellId, Next));
		Edge.DownstreamCellId = Next;
		Edge.FlowAccumulation = Flow[CellId];
		Edge.Water = Flow[CellId] >= StreamThreshold * 3 ? EABTSM3WaterEdgeType::ShallowRiver : EABTSM3WaterEdgeType::Stream;
		MarkWaterCell(CellStates, CellId);
		MarkWaterCell(CellStates, Next);
	}

	const int32 BridgeTaskIndex = FindTaskIndexByType(Tasks, EABTSM3TaskType::BridgeGate);
	const int32 FurnaceTaskIndex = FindTaskIndexByType(Tasks, EABTSM3TaskType::FurnaceRuins);
	if (BridgeTaskIndex == INDEX_NONE || FurnaceTaskIndex == INDEX_NONE) return false;
	const int32 BridgeCell = Tasks[BridgeTaskIndex].SeedCellId;
	const FVector GateUp = Cells[BridgeCell].UnitCenter;
	FVector RouteDirection = FVector::VectorPlaneProject(Cells[Tasks[FurnaceTaskIndex].SeedCellId].UnitCenter - GateUp, GateUp).GetSafeNormal();
	if (RouteDirection.IsNearlyZero()) return false;
	// A great-circle cut with RouteDirection as its plane normal is perpendicular
	// to the main route at GateUp. Cells before and after the gate therefore lie
	// on opposite sides, while the river itself runs across the route.
	const FVector BarrierNormal = RouteDirection;

	// The sign band is a closed great-circle cut. It guarantees that bridge-front
	// and bridge-back regions cannot walk around the ends of a cosmetic river.
	TArray<FABTSM3CellEdgeKey> BarrierEdges;
	for (int32 CellA = 0; CellA < Cells.Num(); ++CellA)
	{
		const float SideA = FVector::DotProduct(Cells[CellA].UnitCenter, BarrierNormal);
		for (const int32 CellB : Cells[CellA].NeighborCellIds)
		{
			if (CellB <= CellA) continue;
			const float SideB = FVector::DotProduct(Cells[CellB].UnitCenter, BarrierNormal);
			if (SideA * SideB > 0.0f) continue;
			const float GateDistance = 1.0f - FVector::DotProduct((Cells[CellA].UnitCenter + Cells[CellB].UnitCenter).GetSafeNormal(), GateUp);
			FABTSM3CellEdgeState& Edge = FindOrAddEdge(FABTSM3CellEdgeKey(CellA, CellB));
			Edge.Water = GateDistance < 0.12f + BarrierHalfWidthCells * 0.02f ? EABTSM3WaterEdgeType::ShallowRiver : EABTSM3WaterEdgeType::DeepRiver;
			Edge.bBlocksOnFoot = true;
			BarrierEdges.Add(Edge.Key);
			MarkWaterCell(CellStates, CellA);
			MarkWaterCell(CellStates, CellB);
		}
	}
	if (BarrierEdges.IsEmpty()) return false;

	float BestBridgeScore = -TNumericLimits<float>::Max();
	for (const FABTSM3CellEdgeKey& Key : BarrierEdges)
	{
		const FVector Mid = (Cells[Key.CellA].UnitCenter + Cells[Key.CellB].UnitCenter).GetSafeNormal();
		const float NearGate = FVector::DotProduct(Mid, GateUp);
		const float LowSlope = 1.0f - FMath::Clamp((CellStates[Key.CellA].LogicalSlopeDegrees + CellStates[Key.CellB].LogicalSlopeDegrees) / 60.0f, 0.0f, 1.0f);
		const float Score = NearGate * 3.0f + LowSlope;
		if (Score > BestBridgeScore)
		{
			BestBridgeScore = Score;
			OutBridgeEdge = Key;
		}
	}
	FABTSM3CellEdgeState& Bridge = FindOrAddEdge(OutBridgeEdge);
	Bridge.Water = EABTSM3WaterEdgeType::ShallowRiver;
	Bridge.Crossing = EABTSM3CrossingType::BridgeSite;
	Bridge.RequiredKey = EABTSM3ProgressKey::BridgeBuilt;
	Bridge.bBlocksOnFoot = true;
	return OutBridgeEdge.CellA != INDEX_NONE;
}
}
