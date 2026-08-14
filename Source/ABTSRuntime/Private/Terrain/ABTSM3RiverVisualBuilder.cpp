// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/ABTSM3RiverVisualBuilder.h"

#include "Planet/ABTSM2Planet.h"

namespace
{
float ResolveHalfWidthCM(const EABTSM3WaterEdgeType Type, const float Stream, const float Shallow, const float Deep)
{
	if (Type == EABTSM3WaterEdgeType::ShallowRiver) return Shallow;
	if (Type == EABTSM3WaterEdgeType::DeepRiver || Type == EABTSM3WaterEdgeType::LakeShore) return Deep;
	return Stream;
}

bool ProjectABTSM3BarrierDualOntoGreatCircle(
	const FVector& BarrierPlaneNormal,
	FVector& InOutStartUnit,
	FVector& InOutEndUnit)
{
	const FVector UnitPlaneNormal = BarrierPlaneNormal.GetSafeNormal();
	if (UnitPlaneNormal.IsNearlyZero()) return false;
	const FVector ProjectedStart = FVector::VectorPlaneProject(
		InOutStartUnit,
		UnitPlaneNormal).GetSafeNormal();
	const FVector ProjectedEnd = FVector::VectorPlaneProject(
		InOutEndUnit,
		UnitPlaneNormal).GetSafeNormal();
	if (ProjectedStart.IsNearlyZero()
		|| ProjectedEnd.IsNearlyZero()
		|| ProjectedStart.Equals(ProjectedEnd, KINDA_SMALL_NUMBER))
	{
		return false;
	}
	InOutStartUnit = ProjectedStart;
	InOutEndUnit = ProjectedEnd;
	return true;
}
}

void FABTSM3RiverVisualBuilder::BuildSegments(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3CellEdgeState>& EdgeStates,
	const float StreamHalfWidthCM,
	const float ShallowRiverHalfWidthCM,
	const float DeepRiverHalfWidthCM,
	TArray<FABTSM3RiverVisualSegment>& OutSegments)
{
	OutSegments.Reset();
	for (const FABTSM3CellEdgeState& Edge : EdgeStates)
	{
		if (Edge.Water == EABTSM3WaterEdgeType::None || !Cells.IsValidIndex(Edge.Key.CellA) || !Cells.IsValidIndex(Edge.Key.CellB)) continue;

		FABTSM3RiverVisualSegment Segment;
		Segment.HalfWidthCM = ResolveHalfWidthCM(Edge.Water, StreamHalfWidthCM, ShallowRiverHalfWidthCM, DeepRiverHalfWidthCM);
		Segment.WaterType = Edge.Water;
		Segment.SourceEdgeKey = Edge.Key;

		if (Edge.DownstreamCellId != INDEX_NONE && !Edge.bBlocksOnFoot)
		{
			// A hydrology edge means water flows between Cell centers. Its Voronoi
			// dual is perpendicular to the flow and would render as a short stripe.
			Segment.StartUnit = Cells[Edge.Key.CellA].UnitCenter;
			Segment.EndUnit = Cells[Edge.Key.CellB].UnitCenter;
		}
		else
		{
			// Gameplay barrier edges form a graph cut. Their dual edges, not their
			// center-to-center crossings, join into the visible blocking river bank.
			TArray<int32, TInlineAllocator<2>> CommonNeighbors;
			for (const int32 Candidate : Cells[Edge.Key.CellA].NeighborCellIds)
			{
				if (Candidate != Edge.Key.CellB && Cells[Edge.Key.CellB].NeighborCellIds.Contains(Candidate)) CommonNeighbors.Add(Candidate);
			}
			if (CommonNeighbors.Num() != 2) continue;
			Segment.StartUnit = (Cells[Edge.Key.CellA].UnitCenter + Cells[Edge.Key.CellB].UnitCenter + Cells[CommonNeighbors[0]].UnitCenter).GetSafeNormal();
			Segment.EndUnit = (Cells[Edge.Key.CellA].UnitCenter + Cells[Edge.Key.CellB].UnitCenter + Cells[CommonNeighbors[1]].UnitCenter).GetSafeNormal();
			Segment.bBarrierCenterlineProjected =
				ProjectABTSM3BarrierDualOntoGreatCircle(
					Edge.WaterBarrierPlaneNormal,
					Segment.StartUnit,
					Segment.EndUnit);
		}
		OutSegments.Add(Segment);
	}
}

void FABTSM3RiverVisualBuilder::BuildRoadSegments(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3CellEdgeState>& EdgeStates,
	const float TrailHalfWidthCM,
	const float MainRoadHalfWidthCM,
	TArray<FABTSM3RiverVisualSegment>& OutSegments)
{
	OutSegments.Reset();
	for (const FABTSM3CellEdgeState& Edge : EdgeStates)
	{
		if (Edge.Transport == EABTSM3TransportType::None || !Cells.IsValidIndex(Edge.Key.CellA) || !Cells.IsValidIndex(Edge.Key.CellB)) continue;
		FABTSM3RiverVisualSegment& Segment = OutSegments.AddDefaulted_GetRef();
		Segment.SourceEdgeKey = Edge.Key;
		Segment.StartUnit = Cells[Edge.Key.CellA].UnitCenter;
		Segment.EndUnit = Cells[Edge.Key.CellB].UnitCenter;
		Segment.HalfWidthCM = Edge.Transport == EABTSM3TransportType::MainRoad ? MainRoadHalfWidthCM : TrailHalfWidthCM;
		Segment.TransportType = Edge.Transport;
	}
}

float FABTSM3RiverVisualBuilder::GetDistanceToSegmentCM(
	const FVector& UnitDirection,
	const FABTSM3RiverVisualSegment& Segment,
	const float PlanetRadiusCM)
{
	const FVector SegmentVector = Segment.EndUnit - Segment.StartUnit;
	const float LengthSquared = SegmentVector.SizeSquared();
	const float Projection = LengthSquared > SMALL_NUMBER
		? FMath::Clamp(FVector::DotProduct(UnitDirection - Segment.StartUnit, SegmentVector) / LengthSquared, 0.0f, 1.0f)
		: 0.0f;
	return FVector::Distance(UnitDirection, Segment.StartUnit + Projection * SegmentVector) * PlanetRadiusCM;
}

void FABTSM3RiverVisualBuilder::BuildLocalSegmentIndices(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3RiverVisualSegment>& Segments,
	const float PlanetRadiusCM,
	const float BlendWidthCM,
	const int32 MaxSegmentsPerCell,
	TArray<TArray<int32>>& OutSegmentIndicesByCell,
	int32& OutDroppedReferences)
{
	OutSegmentIndicesByCell.Reset();
	OutSegmentIndicesByCell.SetNum(Cells.Num());
	OutDroppedReferences = 0;
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		float CellRadiusCM = 0.0f;
		for (const int32 NeighborId : Cells[CellId].NeighborCellIds)
		{
			CellRadiusCM = FMath::Max(CellRadiusCM, FVector::Distance(Cells[CellId].UnitCenter, Cells[NeighborId].UnitCenter) * PlanetRadiusCM);
		}
		TArray<TPair<float, int32>> Candidates;
		for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
		{
			const float DistanceCM = GetDistanceToSegmentCM(Cells[CellId].UnitCenter, Segments[SegmentIndex], PlanetRadiusCM);
			if (DistanceCM <= Segments[SegmentIndex].HalfWidthCM + BlendWidthCM + CellRadiusCM)
			{
				Candidates.Emplace(DistanceCM, SegmentIndex);
			}
		}
		Candidates.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
		const int32 KeptCount = MaxSegmentsPerCell > 0 ? FMath::Min(MaxSegmentsPerCell, Candidates.Num()) : Candidates.Num();
		OutDroppedReferences += Candidates.Num() - KeptCount;
		OutSegmentIndicesByCell[CellId].Reserve(KeptCount);
		for (int32 Index = 0; Index < KeptCount; ++Index) OutSegmentIndicesByCell[CellId].Add(Candidates[Index].Value);
	}
}
