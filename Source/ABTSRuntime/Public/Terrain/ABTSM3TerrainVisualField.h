// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCG/ABTSM3TaskGraphTypes.h"

struct FABTSM2Cell;

struct FABTSM3BoundarySegment
{
	FVector StartUnit = FVector::ZeroVector;
	FVector EndUnit = FVector::ZeroVector;
	int32 OtherCellId = INDEX_NONE;
};

/** Pure presentation field derived from immutable CellTopo PCG output. */
class ABTSRUNTIME_API FABTSM3TerrainVisualField
{
public:
	void Initialize(
		float InBaseRadiusCM,
		float InHeightScaleCM,
		float InWaterDepthCM,
		float InBlendWidthCM,
		const TArray<FABTSM2Cell>& InCells,
		const TArray<FABTSM3CellState>& InCellStates);

	bool IsReady() const { return Cells != nullptr && CellStates != nullptr && BoundarySegmentsByCell.Num() == Cells->Num(); }
	int32 FindNearestCell(const FVector& UnitDirection, int32 StartCellHint = 0) const;
	float GetSurfaceRadius(const FVector& UnitDirection) const;
	FVector GetSurfaceNormal(const FVector& UnitDirection) const;
	FLinearColor GetDebugTerrainColor(const FVector& UnitDirection) const;
	const TArray<FABTSM3BoundarySegment>& GetBoundarySegments(int32 CellId) const;

private:
	void BuildBoundarySegments();
	float GetCellHeightCM(int32 CellId) const;
	float GetDistanceToSegmentCM(const FVector& UnitDirection, const FABTSM3BoundarySegment& Segment) const;
	FLinearColor GetCellColor(int32 CellId) const;

	float BaseRadiusCM = 10000.0f;
	float HeightScaleCM = 900.0f;
	float WaterDepthCM = 80.0f;
	float BlendWidthCM = 240.0f;
	const TArray<FABTSM2Cell>* Cells = nullptr;
	const TArray<FABTSM3CellState>* CellStates = nullptr;
	TArray<TArray<FABTSM3BoundarySegment>> BoundarySegmentsByCell;
};

