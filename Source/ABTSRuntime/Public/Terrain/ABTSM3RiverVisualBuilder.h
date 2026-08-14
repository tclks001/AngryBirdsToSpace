// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCG/ABTSM3TaskGraphTypes.h"

struct FABTSM2Cell;

struct FABTSM3RiverVisualSegment
{
	FVector StartUnit = FVector::ZeroVector;
	FVector EndUnit = FVector::ZeroVector;
	float HalfWidthCM = 0.0f;
	EABTSM3WaterEdgeType WaterType = EABTSM3WaterEdgeType::None;
	EABTSM3TransportType TransportType = EABTSM3TransportType::None;
	FABTSM3CellEdgeKey SourceEdgeKey;
	bool bBarrierCenterlineProjected = false;
};

/** Converts logical water edges into presentation centerlines without changing gameplay data. */
class ABTSRUNTIME_API FABTSM3RiverVisualBuilder
{
public:
	static constexpr int32 BarrierSmoothingVersion = 1;

	static void BuildSegments(
		const TArray<FABTSM2Cell>& Cells,
		const TArray<FABTSM3CellEdgeState>& EdgeStates,
		float StreamHalfWidthCM,
		float ShallowRiverHalfWidthCM,
		float DeepRiverHalfWidthCM,
		TArray<FABTSM3RiverVisualSegment>& OutSegments);

	static void BuildRoadSegments(
		const TArray<FABTSM2Cell>& Cells,
		const TArray<FABTSM3CellEdgeState>& EdgeStates,
		float TrailHalfWidthCM,
		float MainRoadHalfWidthCM,
		TArray<FABTSM3RiverVisualSegment>& OutSegments);

	static float GetDistanceToSegmentCM(
		const FVector& UnitDirection,
		const FABTSM3RiverVisualSegment& Segment,
		float PlanetRadiusCM);

	static void BuildLocalSegmentIndices(
		const TArray<FABTSM2Cell>& Cells,
		const TArray<FABTSM3RiverVisualSegment>& Segments,
		float PlanetRadiusCM,
		float BlendWidthCM,
		int32 MaxSegmentsPerCell,
		TArray<TArray<int32>>& OutSegmentIndicesByCell,
		int32& OutDroppedReferences);
};
