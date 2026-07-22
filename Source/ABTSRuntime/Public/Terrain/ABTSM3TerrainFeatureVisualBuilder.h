// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCG/ABTSM3TaskGraphTypes.h"

struct FABTSM2Cell;

struct FABTSM3TerrainFeatureVisualSegment
{
	FVector StartUnit = FVector::ZeroVector;
	FVector EndUnit = FVector::ZeroVector;
	EABTSM3TerrainType TerrainType = EABTSM3TerrainType::Plain;
	int32 RepresentativeCellId = INDEX_NONE;
	int32 SourceCellAId = INDEX_NONE;
	int32 SourceCellBId = INDEX_NONE;
};

/** Builds a line-Voronoi presentation field from CellTopo terrain sources. */
class ABTSRUNTIME_API FABTSM3TerrainFeatureVisualBuilder
{
public:
	static EABTSM3TerrainType ResolveLandType(const FABTSM3CellState& State);

	static void BuildSegments(
		const TArray<FABTSM2Cell>& Cells,
		const TArray<FABTSM3CellState>& CellStates,
		TArray<FABTSM3TerrainFeatureVisualSegment>& OutSegments);

	static float GetDistanceToSegmentCM(
		const FVector& UnitDirection,
		const FABTSM3TerrainFeatureVisualSegment& Segment,
		float PlanetRadiusCM);

	static void BuildLocalSegmentIndices(
		const TArray<FABTSM2Cell>& Cells,
		const TArray<FABTSM3TerrainFeatureVisualSegment>& Segments,
		int32 NeighborhoodRings,
		int32 MaxSegmentsPerCell,
		float PlanetRadiusCM,
		TArray<TArray<int32>>& OutSegmentIndicesByCell,
		int32& OutDroppedReferences);
};
