// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCG/ABTSM3TaskGraphTypes.h"

struct FABTSM2Cell;

/**
 * Runtime geometry that affects pure-data certification but is owned by the
 * planet presentation actor rather than the mission policy.
 */
struct ABTSRUNTIME_API FABTSM3PCGGeometryContext
{
	float PlanetRadiusCM = 10000.0f;
	float TerrainHeightScaleCM = 900.0f;
	FVector2D BuildingPadHalfExtentCM = FVector2D(650.0f, 450.0f);
	float BuildingPadEdgeBlendWidthCM = 180.0f;
	float TrailHalfWidthCM = 80.0f;
	float MainRoadHalfWidthCM = 180.0f;
	float RoadPadSafetyMarginCM = 25.0f;
};

/** Deterministic orchestrator for the gameplay-first spherical PCG pipeline. */
class ABTSRUNTIME_API FABTSM3TaskGraphGenerator
{
public:
	bool Generate(
		int32 WorldSeed,
		const FABTSM3PCGConfig& Config,
		const TArray<FABTSM2Cell>& Cells,
		TArray<FABTSM3TaskNode>& OutTasks,
		TArray<FABTSM3TaskLink>& OutTaskLinks,
		TArray<FABTSM3CellState>& OutCellStates,
		TArray<FABTSM3CellEdgeState>& OutEdgeStates,
		FABTSM3PCGSummary& OutSummary,
		const FABTSM3PCGGeometryContext& GeometryContext =
			FABTSM3PCGGeometryContext()) const;
};
