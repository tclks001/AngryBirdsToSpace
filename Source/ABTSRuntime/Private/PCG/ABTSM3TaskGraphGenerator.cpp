// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3TaskGraphGenerator.h"

#include "ABTSM3PCGInternal.h"
#include "ABTSRuntime.h"
#include "Planet/ABTSM2Planet.h"

bool FABTSM3TaskGraphGenerator::Generate(
	const int32 WorldSeed,
	const FABTSM3PCGConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	TArray<FABTSM3TaskNode>& OutTasks,
	TArray<FABTSM3TaskLink>& OutTaskLinks,
	TArray<FABTSM3CellState>& OutCellStates,
	TArray<FABTSM3CellEdgeState>& OutEdgeStates,
	FABTSM3PCGSummary& OutSummary) const
{
	using namespace ABTSM3PCG;
	OutTasks.Reset();
	OutTaskLinks.Reset();
	OutCellStates.Reset();
	OutEdgeStates.Reset();
	OutSummary = FABTSM3PCGSummary();
	OutSummary.GeneratorVersion = GeneratorVersion;
	if (Cells.Num() < GetTaskSpecs().Num()) return false;

	const int32 AttemptCount = FMath::Clamp(Config.MaxAttempts, 1, 16);
	for (int32 Attempt = 0; Attempt < AttemptCount; ++Attempt)
	{
		const double AttemptStartSeconds = FPlatformTime::Seconds();
		TArray<FABTSM3TaskNode> CandidateTasks;
		TArray<FABTSM3TaskLink> CandidateLinks;
		TArray<FABTSM3CellState> CandidateCells;
		TArray<FABTSM3CellEdgeState> CandidateEdges;
		FABTSM3PCGSummary CandidateSummary;
		CandidateSummary.GeneratorVersion = GeneratorVersion;
		CandidateSummary.AttemptIndex = Attempt;
		FString Failure;

		FMissionBuilder Mission;
		FSpatialBuilder Spatial;
		FHeightFieldGenerator Height;
		FHydrologyGenerator Hydrology;
		FRoadPlanner Roads;
		FBuildingPadPlanner BuildingPads;
		FWorldValidator Validator;
		FABTSM3CellEdgeKey BridgeEdge;

		bool bSuccess = Mission.Build(WorldSeed, Attempt, Config.TaskTargetCells, CandidateTasks, CandidateLinks);
		UE_LOG(LogABTSRuntime, Verbose, TEXT("[ABTS][PCG][Stage] Attempt=%d Mission=%d TimeMS=%.2f"), Attempt, bSuccess ? 1 : 0, (FPlatformTime::Seconds() - AttemptStartSeconds) * 1000.0);
		if (bSuccess)
		{
			bSuccess = Spatial.PlaceTaskSeeds(
				WorldSeed,
				Attempt,
				Config.MinSatelliteLaunchAngularSeparationDegrees,
				Cells,
				CandidateTasks);
		}
		if (bSuccess) bSuccess = Spatial.GrowTaskRegions(WorldSeed, Attempt, Config.TaskTargetCells, Cells, CandidateTasks, CandidateCells);
		UE_LOG(LogABTSRuntime, Verbose, TEXT("[ABTS][PCG][Stage] Attempt=%d Spatial=%d TimeMS=%.2f"), Attempt, bSuccess ? 1 : 0, (FPlatformTime::Seconds() - AttemptStartSeconds) * 1000.0);
		if (bSuccess)
		{
			Height.Generate(
				WorldSeed,
				Attempt,
				Cells,
				CandidateTasks,
				Config.MaxBuildSlopeDegrees,
				Config.BuildingPadClearanceRingCells,
				CandidateCells);
			UE_LOG(LogABTSRuntime, Verbose, TEXT("[ABTS][PCG][Stage] Attempt=%d Height=1 TimeMS=%.2f"), Attempt, (FPlatformTime::Seconds() - AttemptStartSeconds) * 1000.0);
			bSuccess = Hydrology.Generate(WorldSeed, Attempt, Config.StreamFlowThreshold, Config.WaterBarrierHalfWidthCells,
				Cells, CandidateTasks, CandidateCells, CandidateEdges, BridgeEdge);
			UE_LOG(LogABTSRuntime, Verbose, TEXT("[ABTS][PCG][Stage] Attempt=%d Hydrology=%d Edges=%d TimeMS=%.2f"), Attempt, bSuccess ? 1 : 0, CandidateEdges.Num(), (FPlatformTime::Seconds() - AttemptStartSeconds) * 1000.0);
		}
		if (bSuccess)
		{
			bSuccess = Roads.Build(Cells, CandidateTasks, CandidateLinks, CandidateCells, CandidateEdges, BridgeEdge);
			UE_LOG(LogABTSRuntime, Verbose, TEXT("[ABTS][PCG][Stage] Attempt=%d Roads=%d TimeMS=%.2f"), Attempt, bSuccess ? 1 : 0, (FPlatformTime::Seconds() - AttemptStartSeconds) * 1000.0);
		}
		if (bSuccess)
		{
			bSuccess = BuildingPads.Place(Cells, CandidateTasks, Config.BuildingPadClearanceRingCells, CandidateCells, Failure);
			UE_LOG(LogABTSRuntime, Verbose, TEXT("[ABTS][PCG][Stage] Attempt=%d BuildingPads=%d TimeMS=%.2f"), Attempt, bSuccess ? 1 : 0, (FPlatformTime::Seconds() - AttemptStartSeconds) * 1000.0);
		}
		if (bSuccess)
		{
			bSuccess = Validator.Validate(
				Cells,
				CandidateTasks,
				CandidateLinks,
				CandidateCells,
				CandidateEdges,
				BridgeEdge,
				Config.MinSatelliteLaunchAngularSeparationDegrees,
				CandidateSummary,
				Failure);
			UE_LOG(LogABTSRuntime, Verbose, TEXT("[ABTS][PCG][Stage] Attempt=%d Validate=%d TimeMS=%.2f"), Attempt, bSuccess ? 1 : 0, (FPlatformTime::Seconds() - AttemptStartSeconds) * 1000.0);
		}
		if (!bSuccess)
		{
			if (Failure.IsEmpty()) Failure = TEXT("StageFailure");
			UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][PCG][Reject] Seed=%d Version=%d Attempt=%d Reason=%s"), WorldSeed, GeneratorVersion, Attempt, *Failure);
			continue;
		}

		CandidateSummary.BridgeEdge = BridgeEdge;
		for (const FABTSM3CellState& State : CandidateCells) CandidateSummary.AssignedTaskCells += State.TaskId != INDEX_NONE ? 1 : 0;
		for (const FABTSM3CellEdgeState& Edge : CandidateEdges)
		{
			CandidateSummary.RiverEdges += Edge.Water != EABTSM3WaterEdgeType::None ? 1 : 0;
			CandidateSummary.RoadEdges += Edge.Transport != EABTSM3TransportType::None ? 1 : 0;
		}
		OutTasks = MoveTemp(CandidateTasks);
		OutTaskLinks = MoveTemp(CandidateLinks);
		OutCellStates = MoveTemp(CandidateCells);
		OutEdgeStates = MoveTemp(CandidateEdges);
		OutSummary = CandidateSummary;
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][PCG][Accepted] Seed=%d Version=%d Attempt=%d Tasks=%d Links=%d Assigned=%d Wilderness=%d RiverEdges=%d RoadEdges=%d Bridge=(%d,%d) LockedBefore=%d ReachableAfter=%d SatelliteLaunchSepDeg=%.2f"),
			WorldSeed, GeneratorVersion, Attempt, OutTasks.Num(), OutTaskLinks.Num(), OutSummary.AssignedTaskCells,
			Cells.Num() - OutSummary.AssignedTaskCells, OutSummary.RiverEdges, OutSummary.RoadEdges,
			BridgeEdge.CellA, BridgeEdge.CellB, OutSummary.bBridgeLockedBeforeBuild ? 1 : 0, OutSummary.bMainPathReachableAfterBridge ? 1 : 0,
			OutSummary.SatelliteLaunchAngularSeparationDegrees);
		return true;
	}

	UE_LOG(LogABTSRuntime, Error, TEXT("[ABTS][PCG] All attempts failed. Seed=%d Attempts=%d"), WorldSeed, AttemptCount);
	return false;
}
