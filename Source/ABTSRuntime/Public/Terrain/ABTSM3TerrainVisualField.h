// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCG/ABTSM3TaskGraphTypes.h"
#include "Terrain/ABTSM3RiverVisualBuilder.h"
#include "Terrain/ABTSM3TerrainFeatureVisualBuilder.h"

struct FABTSM2Cell;

struct FABTSM3BoundarySegment
{
	FVector StartUnit = FVector::ZeroVector;
	FVector EndUnit = FVector::ZeroVector;
	EABTSM3TerrainType TerrainType = EABTSM3TerrainType::Plain;
	int32 SourceCellAId = INDEX_NONE;
	int32 SourceCellBId = INDEX_NONE;
};

/** Pure presentation field derived from immutable CellTopo PCG output. */
class ABTSRUNTIME_API FABTSM3TerrainVisualField
{
public:
	void Initialize(
		float InBaseRadiusCM,
		float InHeightScaleCM,
		float InWaterDepthCM,
		float InHeightBlendWidthCM,
		float InColorBlendWidthCM,
		float InNormalSmoothingDistanceCM,
		const TArray<FABTSM2Cell>& InCells,
		const TArray<FABTSM3CellState>& InCellStates,
		const TArray<FABTSM3CellEdgeState>& InEdgeStates,
		float InStreamHalfWidthCM,
		float InShallowRiverHalfWidthCM,
		float InDeepRiverHalfWidthCM);

	bool IsReady() const { return Cells != nullptr && CellStates != nullptr && BoundarySegmentsByCell.Num() == Cells->Num(); }
	int32 FindNearestCell(const FVector& UnitDirection, int32 StartCellHint = 0) const;
	float GetSurfaceRadius(const FVector& UnitDirection) const;
	FVector GetSurfaceNormal(const FVector& UnitDirection) const;
	FLinearColor GetDebugTerrainColor(const FVector& UnitDirection) const;
	/** Land-only color used by the material LUT; rivers are rendered from edge segments. */
	FLinearColor GetDebugLandColor(const FVector& UnitDirection) const;
	const TArray<FABTSM3BoundarySegment>& GetBoundarySegments(int32 CellId) const;

private:
	void BuildBoundarySegments();
	void BuildRiverSegments(const TArray<FABTSM3CellEdgeState>& EdgeStates, float StreamHalfWidthCM, float ShallowRiverHalfWidthCM, float DeepRiverHalfWidthCM);
	float GetCellHeightCM(int32 CellId) const;
	float GetInterpolatedHeightCM(const FVector& UnitDirection, int32 NearestCellId) const;
	float GetDistanceToSegmentCM(const FVector& UnitDirection, const FABTSM3BoundarySegment& Segment) const;
	void FindTwoNearestTerrainFeatures(const FVector& UnitDirection, int32 CellId, const FABTSM3BoundarySegment*& OutBest, float& OutBestDistanceCM, const FABTSM3BoundarySegment*& OutSecond, float& OutSecondDistanceCM) const;
	FLinearColor GetCellColor(int32 CellId) const;
	FLinearColor GetCellLandColor(int32 CellId) const;

	float BaseRadiusCM = 10000.0f;
	float HeightScaleCM = 900.0f;
	float WaterDepthCM = 80.0f;
	float HeightBlendWidthCM = 160.0f;
	float ColorBlendWidthCM = 240.0f;
	float NormalSmoothingDistanceCM = 160.0f;
	const TArray<FABTSM2Cell>* Cells = nullptr;
	const TArray<FABTSM3CellState>* CellStates = nullptr;
	TArray<TArray<FABTSM3BoundarySegment>> BoundarySegmentsByCell;
	TArray<TArray<FABTSM3RiverVisualSegment>> RiverSegmentsByCell;
};
