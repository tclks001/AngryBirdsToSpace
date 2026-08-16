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

/** CPU-side counterpart of the M3 line-SDF terrain presentation query. */
struct FABTSM3SurfaceSDFSample
{
	int32 CellId = INDEX_NONE;
	EABTSM3TerrainType PrimaryTerrain = EABTSM3TerrainType::Plain;
	EABTSM3TerrainType SecondaryTerrain = EABTSM3TerrainType::Plain;
	/** Weight of PrimaryTerrain after the same line-feature interpolation used by the terrain color field. */
	float PrimaryTerrainWeight = 1.0f;
	/** 0..1 masks reconstructed from the same CPU line segments that feed the material LUT. */
	float RoadWeight = 0.0f;
	float RiverWeight = 0.0f;
	FVector SurfaceNormal = FVector::UpVector;
};

/** Read-only aggregate used by deterministic continuous-surface generation. */
struct FABTSM3ContinuousSurfaceSample
{
	int32 CellId = INDEX_NONE;
	float SurfaceRadiusCM = 0.0f;
	FVector SurfaceNormal = FVector::UpVector;
	FLinearColor TerrainColor = FLinearColor::Gray;
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
		float InTrailRoadHalfWidthCM,
		float InMainRoadHalfWidthCM,
		float InStreamHalfWidthCM,
		float InShallowRiverHalfWidthCM,
		float InDeepRiverHalfWidthCM);

	bool IsReady() const { return Cells != nullptr && CellStates != nullptr && BoundarySegmentsByCell.Num() == Cells->Num(); }
	/** Adds CellTopo-derived local tangent construction pads after the base field is initialized. */
	void SetBuildingPads(const TArray<FABTSM3BuildingSpawnSite>& InSites);
	/** Used by HISM placement to keep decoration out of the construction footprint. */
	bool IsInsideBuildingPad(const FVector& UnitDirection) const;
	int32 FindNearestCell(const FVector& UnitDirection, int32 StartCellHint = 0) const;
	/** Base CellTopo surface before any construction pad is applied. Used by M3 grading and diagnostics only. */
	float GetUnpaddedSurfaceRadius(const FVector& UnitDirection) const;
	/** Surface after ordinary TaskGraph pads but before terrain-only fixed-six grading. */
	float GetCompatibilityPaddedSurfaceRadius(const FVector& UnitDirection) const;
	/** Signed distance from the unpadded surface sample to one rectangular pad footprint. */
	float GetBuildingPadSignedDistanceCM(const FVector& UnitDirection, float RadiusCM, const FABTSM3BuildingSpawnSite& Pad) const;
	float GetSurfaceRadius(const FVector& UnitDirection) const;
	FVector GetSurfaceNormal(const FVector& UnitDirection) const;
	/** Central-Cell-reusing geometry query for production surface consumers. */
	bool QuerySurfaceGeometry(
		const FVector& UnitDirection,
		int32 StartCellHint,
		int32& OutCellId,
		float& OutSurfaceRadiusCM,
		FVector& OutSurfaceNormal) const;
	/** Central Cell/radius/color only; caller may resolve canonical normal serially. */
	bool QueryContinuousSurfaceBaseSample(
		const FVector& UnitDirection,
		int32 StartCellHint,
		FABTSM3ContinuousSurfaceSample& OutSample) const;
	/**
	 * Resolves one central Cell once so radius and color reuse it directly.
	 * Normal probes retain the legacy Cell-0 canonical path. Production mesh
	 * generation resolves them serially because the exact oracle rejected
	 * worker-thread low-bit drift. This method itself remains read-only.
	 */
	bool QueryContinuousSurfaceSample(
		const FVector& UnitDirection,
		int32 StartCellHint,
		FABTSM3ContinuousSurfaceSample& OutSample) const;
	/** Samples terrain, road and river line-SDF weights on CPU; never reads GPU material pixels. */
	bool QuerySurfaceSDF(const FVector& UnitDirection, float PhysicsBlendWidthCM, FABTSM3SurfaceSDFSample& OutSample) const;
	FLinearColor GetDebugTerrainColor(const FVector& UnitDirection) const;
	/** Land-only color used by the material LUT; rivers are rendered from edge segments. */
	FLinearColor GetDebugLandColor(const FVector& UnitDirection) const;
	/** Fixed base color for one effective land TerrainType; no monthly presentation variation. */
	static FLinearColor GetTerrainBaseColor(EABTSM3TerrainType TerrainType);
	/** Fixed base color for the Cell's effective land type; does not sample or blend boundaries. */
	FLinearColor GetCellBaseLandColor(int32 CellId) const;
	/** M10-only color query: one nearest-cell walk, no height or surface-normal work. */
	bool QueryScoutMapColor(
		const FVector& UnitDirection,
		const FLinearColor& RoadColor,
		const FLinearColor& RiverColor,
		int32 StartCellHint,
		int32& OutCellId,
		FLinearColor& OutColor) const;
	const TArray<FABTSM3BoundarySegment>& GetBoundarySegments(int32 CellId) const;

private:
	void BuildBoundarySegments();
	void BuildRiverSegments(const TArray<FABTSM3CellEdgeState>& EdgeStates, float StreamHalfWidthCM, float ShallowRiverHalfWidthCM, float DeepRiverHalfWidthCM);
	void BuildRoadSegments(const TArray<FABTSM3CellEdgeState>& EdgeStates);
	float GetCellHeightCM(int32 CellId) const;
	float GetInterpolatedHeightCM(const FVector& UnitDirection, int32 NearestCellId) const;
	float GetUnpaddedSurfaceRadiusForCell(const FVector& UnitDirection, int32 CellId) const;
	float GetSurfaceRadiusWithHint(const FVector& UnitDirection, int32 StartCellHint, int32& OutCellId) const;
	float ApplyCompatibilityBuildingPadRadius(const FVector& UnitDirection, float UnpaddedRadiusCM) const;
	float ApplyBuildingPadRadius(const FVector& UnitDirection, float UnpaddedRadiusCM) const;
	float GetDistanceToSegmentCM(const FVector& UnitDirection, const FABTSM3BoundarySegment& Segment) const;
	void FindTwoNearestTerrainFeatures(const FVector& UnitDirection, int32 CellId, const FABTSM3BoundarySegment*& OutBest, float& OutBestDistanceCM, const FABTSM3BoundarySegment*& OutSecond, float& OutSecondDistanceCM) const;
	FLinearColor GetCellColor(int32 CellId) const;
	FLinearColor GetDebugTerrainColorForCell(const FVector& UnitDirection, int32 CellId) const;

	float BaseRadiusCM = 10000.0f;
	float HeightScaleCM = 900.0f;
	float WaterDepthCM = 80.0f;
	float HeightBlendWidthCM = 160.0f;
	float ColorBlendWidthCM = 240.0f;
	float NormalSmoothingDistanceCM = 160.0f;
	float TrailRoadHalfWidthCM = 80.0f;
	float MainRoadHalfWidthCM = 180.0f;
	const TArray<FABTSM2Cell>* Cells = nullptr;
	const TArray<FABTSM3CellState>* CellStates = nullptr;
	TArray<TArray<FABTSM3BoundarySegment>> BoundarySegmentsByCell;
	TArray<TArray<FABTSM3RiverVisualSegment>> RiverSegmentsByCell;
	TArray<TArray<FABTSM3RiverVisualSegment>> RoadSegmentsByCell;
	TArray<FABTSM3BuildingSpawnSite> BuildingPads;
};
