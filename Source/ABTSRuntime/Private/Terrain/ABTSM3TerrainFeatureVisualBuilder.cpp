// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/ABTSM3TerrainFeatureVisualBuilder.h"

#include "Planet/ABTSM2Planet.h"

EABTSM3TerrainType FABTSM3TerrainFeatureVisualBuilder::ResolveLandType(const FABTSM3CellState& State)
{
	if (State.TerrainType != EABTSM3TerrainType::Water) return State.TerrainType;
	if (State.LogicalHeight01 >= 0.62f) return EABTSM3TerrainType::Mountain;
	if (State.LogicalHeight01 >= 0.38f) return EABTSM3TerrainType::Highland;
	return State.Moisture01 >= 0.48f ? EABTSM3TerrainType::Forest : EABTSM3TerrainType::Plain;
}

void FABTSM3TerrainFeatureVisualBuilder::BuildSegments(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3CellState>& CellStates,
	TArray<FABTSM3TerrainFeatureVisualSegment>& OutSegments)
{
	OutSegments.Reset();
	if (Cells.Num() != CellStates.Num()) return;
	TArray<bool> HasSameTypeNeighbor;
	HasSameTypeNeighbor.Init(false, Cells.Num());
	for (int32 CellA = 0; CellA < Cells.Num(); ++CellA)
	{
		const EABTSM3TerrainType TypeA = ResolveLandType(CellStates[CellA]);
		for (const int32 CellB : Cells[CellA].NeighborCellIds)
		{
			if (CellB <= CellA || ResolveLandType(CellStates[CellB]) != TypeA) continue;
			FABTSM3TerrainFeatureVisualSegment& Segment = OutSegments.AddDefaulted_GetRef();
			Segment.StartUnit = Cells[CellA].UnitCenter;
			Segment.EndUnit = Cells[CellB].UnitCenter;
			Segment.TerrainType = TypeA;
			Segment.RepresentativeCellId = CellA;
			Segment.SourceCellAId = CellA;
			Segment.SourceCellBId = CellB;
			HasSameTypeNeighbor[CellA] = true;
			HasSameTypeNeighbor[CellB] = true;
		}
	}
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		if (HasSameTypeNeighbor[CellId]) continue;
		FABTSM3TerrainFeatureVisualSegment& PointFeature = OutSegments.AddDefaulted_GetRef();
		PointFeature.StartUnit = Cells[CellId].UnitCenter;
		PointFeature.EndUnit = Cells[CellId].UnitCenter;
		PointFeature.TerrainType = ResolveLandType(CellStates[CellId]);
		PointFeature.RepresentativeCellId = CellId;
		PointFeature.SourceCellAId = CellId;
		PointFeature.SourceCellBId = CellId;
	}
}

float FABTSM3TerrainFeatureVisualBuilder::GetDistanceToSegmentCM(
	const FVector& UnitDirection,
	const FABTSM3TerrainFeatureVisualSegment& Segment,
	const float PlanetRadiusCM)
{
	const FVector Delta = Segment.EndUnit - Segment.StartUnit;
	const float T = Delta.SizeSquared() > SMALL_NUMBER
		? FMath::Clamp(FVector::DotProduct(UnitDirection - Segment.StartUnit, Delta) / Delta.SizeSquared(), 0.0f, 1.0f)
		: 0.0f;
	return FVector::Distance(UnitDirection, Segment.StartUnit + T * Delta) * PlanetRadiusCM;
}

void FABTSM3TerrainFeatureVisualBuilder::BuildLocalSegmentIndices(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3TerrainFeatureVisualSegment>& Segments,
	const int32 NeighborhoodRings,
	const int32 MaxSegmentsPerCell,
	const float PlanetRadiusCM,
	TArray<TArray<int32>>& OutSegmentIndicesByCell,
	int32& OutDroppedReferences)
{
	OutSegmentIndicesByCell.Reset();
	OutSegmentIndicesByCell.SetNum(Cells.Num());
	OutDroppedReferences = 0;
	TArray<TArray<int32>> SegmentsBySourceCell;
	SegmentsBySourceCell.SetNum(Cells.Num());
	for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
	{
		const FABTSM3TerrainFeatureVisualSegment& Segment = Segments[SegmentIndex];
		if (SegmentsBySourceCell.IsValidIndex(Segment.SourceCellAId)) SegmentsBySourceCell[Segment.SourceCellAId].Add(SegmentIndex);
		if (Segment.SourceCellBId != Segment.SourceCellAId && SegmentsBySourceCell.IsValidIndex(Segment.SourceCellBId)) SegmentsBySourceCell[Segment.SourceCellBId].Add(SegmentIndex);
	}

	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		TSet<int32> VisitedCells = {CellId};
		TArray<int32> Frontier = {CellId};
		for (int32 Ring = 0; Ring < NeighborhoodRings; ++Ring)
		{
			TArray<int32> NextFrontier;
			for (const int32 Current : Frontier)
			{
				for (const int32 Neighbor : Cells[Current].NeighborCellIds)
				{
					if (!VisitedCells.Contains(Neighbor)) { VisitedCells.Add(Neighbor); NextFrontier.Add(Neighbor); }
				}
			}
			Frontier = MoveTemp(NextFrontier);
		}
		TSet<int32> CandidateSet;
		for (const int32 SourceCell : VisitedCells) CandidateSet.Append(SegmentsBySourceCell[SourceCell]);
		TArray<TPair<float, int32>> Candidates;
		for (const int32 SegmentIndex : CandidateSet)
		{
			Candidates.Emplace(GetDistanceToSegmentCM(Cells[CellId].UnitCenter, Segments[SegmentIndex], PlanetRadiusCM), SegmentIndex);
		}
		Candidates.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });

		TSet<EABTSM3TerrainType> KeptTypes;
		TSet<int32> KeptIndices;
		for (const TPair<float, int32>& Candidate : Candidates)
		{
			const EABTSM3TerrainType Type = Segments[Candidate.Value].TerrainType;
			if (!KeptTypes.Contains(Type)) { KeptTypes.Add(Type); KeptIndices.Add(Candidate.Value); }
		}
		for (const TPair<float, int32>& Candidate : Candidates)
		{
			if (KeptIndices.Num() >= MaxSegmentsPerCell) break;
			KeptIndices.Add(Candidate.Value);
		}
		TArray<int32>& Output = OutSegmentIndicesByCell[CellId];
		for (const TPair<float, int32>& Candidate : Candidates) if (KeptIndices.Contains(Candidate.Value)) Output.Add(Candidate.Value);
		OutDroppedReferences += FMath::Max(0, Candidates.Num() - Output.Num());
	}
}
