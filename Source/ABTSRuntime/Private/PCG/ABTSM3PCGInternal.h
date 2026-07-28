// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCG/ABTSM3TaskGraphTypes.h"

struct FABTSM2Cell;

namespace ABTSM3PCG
{
constexpr int32 GeneratorVersion = 3;

struct FTaskSpec
{
	EABTSM3TaskType Type;
	EABTSM3TerrainType Terrain;
	float HeightMin;
	float HeightMax;
	int32 TargetCells;
	bool bBuilding;
};

const TArray<FTaskSpec>& GetTaskSpecs();
uint32 MakeStageSeed(int32 WorldSeed, const TCHAR* StageTag, int32 AttemptIndex);
int32 FindTaskIndexById(const TArray<FABTSM3TaskNode>& Tasks, int32 TaskId);
int32 FindTaskIndexByType(const TArray<FABTSM3TaskNode>& Tasks, EABTSM3TaskType Type);
int32 FindEdgeStateIndex(const TArray<FABTSM3CellEdgeState>& Edges, const FABTSM3CellEdgeKey& Key);
FABTSM3CellEdgeState& FindOrAddEdgeState(TArray<FABTSM3CellEdgeState>& Edges, const FABTSM3CellEdgeKey& Key);
TArray<int32> FindUnweightedPath(const TArray<FABTSM2Cell>& Cells, int32 StartCellId, int32 GoalCellId, const TSet<int32>* BlockedCells = nullptr);
void BuildDistanceField(const TArray<FABTSM2Cell>& Cells, const TArray<int32>& Sources, TArray<int32>& OutDistance, const TSet<int32>* BlockedCells = nullptr);

class FMissionBuilder
{
public:
	bool Build(int32 WorldSeed, int32 AttemptIndex, int32 TargetCells, TArray<FABTSM3TaskNode>& OutTasks, TArray<FABTSM3TaskLink>& OutLinks) const;
};

class FSpatialBuilder
{
public:
	bool PlaceTaskSeeds(int32 WorldSeed, int32 AttemptIndex, float MinSatelliteLaunchAngularSeparationDegrees,
		const TArray<FABTSM2Cell>& Cells, TArray<FABTSM3TaskNode>& Tasks) const;
	bool GrowTaskRegions(int32 WorldSeed, int32 AttemptIndex, int32 TargetCells, const TArray<FABTSM2Cell>& Cells, TArray<FABTSM3TaskNode>& Tasks, TArray<FABTSM3CellState>& CellStates) const;
};

class FHeightFieldGenerator
{
public:
	void Generate(int32 WorldSeed, int32 AttemptIndex, const TArray<FABTSM2Cell>& Cells,
		const TArray<FABTSM3TaskNode>& Tasks, float MaxBuildSlopeDegrees,
		int32 BuildingPadClearanceRingCells, TArray<FABTSM3CellState>& CellStates) const;
};

class FHydrologyGenerator
{
public:
	bool Generate(int32 WorldSeed, int32 AttemptIndex, int32 StreamThreshold, float BarrierHalfWidthCells,
		const TArray<FABTSM2Cell>& Cells, const TArray<FABTSM3TaskNode>& Tasks, TArray<FABTSM3CellState>& CellStates,
		TArray<FABTSM3CellEdgeState>& EdgeStates, FABTSM3CellEdgeKey& OutBridgeEdge) const;
};

class FRoadPlanner
{
public:
	bool Build(const TArray<FABTSM2Cell>& Cells, const TArray<FABTSM3TaskNode>& Tasks, TArray<FABTSM3TaskLink>& Links,
		TArray<FABTSM3CellState>& CellStates, TArray<FABTSM3CellEdgeState>& EdgeStates, const FABTSM3CellEdgeKey& BridgeEdge) const;
};

/** Picks one CellTopo anchor per building task only after water/road generation can certify its full footprint. */
class FBuildingPadPlanner
{
public:
	bool Place(const TArray<FABTSM2Cell>& Cells, const TArray<FABTSM3TaskNode>& Tasks,
		int32 ClearanceRingCells, TArray<FABTSM3CellState>& CellStates, FString& OutFailure) const;
};

class FWorldValidator
{
public:
	bool Validate(const TArray<FABTSM2Cell>& Cells, const TArray<FABTSM3TaskNode>& Tasks, const TArray<FABTSM3TaskLink>& Links,
		const TArray<FABTSM3CellState>& CellStates, const TArray<FABTSM3CellEdgeState>& EdgeStates,
		const FABTSM3CellEdgeKey& BridgeEdge, float MinSatelliteLaunchAngularSeparationDegrees,
		FABTSM3PCGSummary& Summary, FString& OutFailure) const;
};
}
