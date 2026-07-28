// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3TaskGraphGenerator.h"

#include "ABTSM3PCGInternal.h"
#include "ABTSRuntime.h"
#include "Planet/ABTSM2Planet.h"

namespace
{
uint32 ComputeM3ConfigHash(
	const FABTSM3PCGConfig& Config,
	const FABTSM3PCGGeometryContext& Geometry,
	const TArray<FABTSM2Cell>& Cells)
{
	uint32 Hash = GetTypeHash(ABTSM3PCG::LayoutPolicyVersion);
	const auto Add = [&Hash](const auto& Value)
	{
		Hash = HashCombineFast(Hash, GetTypeHash(Value));
	};
	Add(Config.MaxAttempts);
	Add(Config.TaskTargetCells);
	Add(Config.WaterBarrierHalfWidthCells);
	Add(Config.StreamFlowThreshold);
	Add(Config.MaxBuildSlopeDegrees);
	Add(Config.FirstWeekMainRouteAngularSpanDegrees);
	Add(Config.MinMainRouteLengthCM);
	Add(Config.MinAdjacentBuildingProgressCM);
	Add(Config.WorkshopMinMainRoadDistanceCells);
	Add(Config.TargetBuildingMinMainRoadDistanceCells);
	Add(Config.FurnaceMinMainRoadDistanceCells);
	Add(Config.BuildingAnchorSearchSlackCells);
	Add(Config.bRequireWeekOneVisibilityContract);
	Add(Config.VisibilityCharacterCenterHeightCM);
	Add(Config.VisibilityLookAtHeightCM);
	Add(Config.VisibilityDefaultOrbitDistanceCM);
	Add(Config.VisibilityMaxOrbitDistanceCM);
	Add(Config.VisibilityElevationDegrees);
	Add(Config.VisibilityTargetHeightCM);
	Add(Config.VisibilityTraceSamples);
	Add(Config.BuildingPadClearanceRingCells);
	Add(Config.MinSatelliteLaunchAngularSeparationDegrees);
	Add(Geometry.PlanetRadiusCM);
	Add(Geometry.TerrainHeightScaleCM);
	Add(Geometry.BuildingPadHalfExtentCM.X);
	Add(Geometry.BuildingPadHalfExtentCM.Y);
	Add(Geometry.BuildingPadEdgeBlendWidthCM);
	Add(Geometry.TrailHalfWidthCM);
	Add(Geometry.MainRoadHalfWidthCM);
	Add(Geometry.RoadPadSafetyMarginCM);
	Add(Cells.Num());
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		const FABTSM2Cell& Cell = Cells[CellId];
		Add(CellId);
		Add(FMath::RoundToInt(Cell.UnitCenter.X * 1000000.0));
		Add(FMath::RoundToInt(Cell.UnitCenter.Y * 1000000.0));
		Add(FMath::RoundToInt(Cell.UnitCenter.Z * 1000000.0));
		Add(Cell.bIsPentagon);
		Add(Cell.NeighborCellIds.Num());
		for (const int32 NeighborCellId : Cell.NeighborCellIds)
		{
			Add(NeighborCellId);
		}
	}
	return Hash;
}

uint32 ComputeM3LayoutHash(
	const TArray<FABTSM3TaskNode>& Tasks,
	const TArray<FABTSM3TaskLink>& Links)
{
	uint32 Hash = GetTypeHash(ABTSM3PCG::LayoutPolicyVersion);
	for (const FABTSM3TaskNode& Task : Tasks)
	{
		Hash = HashCombineFast(Hash, GetTypeHash(Task.TaskId));
		Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(Task.Type)));
		Hash = HashCombineFast(Hash, GetTypeHash(Task.SeedCellId));
		Hash = HashCombineFast(Hash, GetTypeHash(Task.RoadPortalCellId));
		Hash = HashCombineFast(Hash, GetTypeHash(Task.BuildingAnchorCellId));
		for (const int32 CellId : Task.CellIds)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(CellId));
		}
	}
	for (const FABTSM3TaskLink& Link : Links)
	{
		Hash = HashCombineFast(Hash, GetTypeHash(Link.LinkId));
		Hash = HashCombineFast(Hash, GetTypeHash(Link.TaskA));
		Hash = HashCombineFast(Hash, GetTypeHash(Link.TaskB));
		Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(Link.Role)));
		for (const int32 CellId : Link.CorridorCells)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(CellId));
		}
	}
	return Hash;
}
}

bool FABTSM3TaskGraphGenerator::Generate(
	const int32 WorldSeed,
	const FABTSM3PCGConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	TArray<FABTSM3TaskNode>& OutTasks,
	TArray<FABTSM3TaskLink>& OutTaskLinks,
	TArray<FABTSM3CellState>& OutCellStates,
	TArray<FABTSM3CellEdgeState>& OutEdgeStates,
	FABTSM3PCGSummary& OutSummary,
	const FABTSM3PCGGeometryContext& GeometryContext) const
{
	using namespace ABTSM3PCG;
	OutTasks.Reset();
	OutTaskLinks.Reset();
	OutCellStates.Reset();
	OutEdgeStates.Reset();
	OutSummary = FABTSM3PCGSummary();
	OutSummary.GeneratorVersion = GeneratorVersion;
	OutSummary.LayoutPolicyVersion = LayoutPolicyVersion;
	OutSummary.ConfigHash = ComputeM3ConfigHash(Config, GeometryContext, Cells);
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
		CandidateSummary.LayoutPolicyVersion = LayoutPolicyVersion;
		CandidateSummary.ConfigHash = OutSummary.ConfigHash;
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
				Config.FirstWeekMainRouteAngularSpanDegrees,
				Config.MinSatelliteLaunchAngularSeparationDegrees,
				Cells,
				CandidateTasks);
		}
		if (bSuccess) bSuccess = Spatial.GrowTaskRegions(WorldSeed, Attempt, Config.TaskTargetCells, Cells, CandidateTasks, CandidateCells);
		UE_LOG(LogABTSRuntime, Verbose, TEXT("[ABTS][PCG][Stage] Attempt=%d Spatial=%d TimeMS=%.2f"), Attempt, bSuccess ? 1 : 0, (FPlatformTime::Seconds() - AttemptStartSeconds) * 1000.0);
		if (bSuccess)
		{
			bSuccess = BuildingPads.Reserve(
				WorldSeed,
				Attempt,
				Cells,
				CandidateTasks,
				Config,
				CandidateCells,
				Failure);
			UE_LOG(LogABTSRuntime, Verbose,
				TEXT("[ABTS][PCG][Stage] Attempt=%d BuildingPadReserve=%d TimeMS=%.2f"),
				Attempt,
				bSuccess ? 1 : 0,
				(FPlatformTime::Seconds() - AttemptStartSeconds) * 1000.0);
		}
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
			bSuccess = Roads.Build(
				Cells,
				CandidateTasks,
				CandidateLinks,
				CandidateCells,
				CandidateEdges,
				BridgeEdge,
				GeometryContext.PlanetRadiusCM);
			UE_LOG(LogABTSRuntime, Verbose, TEXT("[ABTS][PCG][Stage] Attempt=%d Roads=%d TimeMS=%.2f"), Attempt, bSuccess ? 1 : 0, (FPlatformTime::Seconds() - AttemptStartSeconds) * 1000.0);
		}
		if (bSuccess)
		{
			bSuccess = BuildingPads.Certify(
				Cells,
				CandidateTasks,
				CandidateEdges,
				Config,
				GeometryContext,
				CandidateCells,
				Failure);
			UE_LOG(LogABTSRuntime, Verbose,
				TEXT("[ABTS][PCG][Stage] Attempt=%d BuildingPadCertify=%d TimeMS=%.2f"),
				Attempt,
				bSuccess ? 1 : 0,
				(FPlatformTime::Seconds() - AttemptStartSeconds) * 1000.0);
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
				Config,
				GeometryContext,
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
		CandidateSummary.LayoutHash = ComputeM3LayoutHash(CandidateTasks, CandidateLinks);
		OutTasks = MoveTemp(CandidateTasks);
		OutTaskLinks = MoveTemp(CandidateLinks);
		OutCellStates = MoveTemp(CandidateCells);
		OutEdgeStates = MoveTemp(CandidateEdges);
		OutSummary = CandidateSummary;
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][PCG][Accepted] Seed=%d Version=%d LayoutPolicy=%d ConfigHash=%lld LayoutHash=%lld Attempt=%d Tasks=%d Links=%d Assigned=%d Wilderness=%d RiverEdges=%d RoadEdges=%d Bridge=(%d,%d) LockedBefore=%d ReachableAfter=%d SatelliteLaunchSepDeg=%.2f MainRouteCM=%.1f BuildingGapCM=%.1f Visibility=%d%d/%d%d/%d%d"),
			WorldSeed, GeneratorVersion, LayoutPolicyVersion,
			static_cast<long long>(OutSummary.ConfigHash),
			static_cast<long long>(OutSummary.LayoutHash),
			Attempt, OutTasks.Num(), OutTaskLinks.Num(), OutSummary.AssignedTaskCells,
			Cells.Num() - OutSummary.AssignedTaskCells, OutSummary.RiverEdges, OutSummary.RoadEdges,
			BridgeEdge.CellA, BridgeEdge.CellB, OutSummary.bBridgeLockedBeforeBuild ? 1 : 0, OutSummary.bMainPathReachableAfterBridge ? 1 : 0,
			OutSummary.SatelliteLaunchAngularSeparationDegrees,
			OutSummary.MainRouteLengthCM,
			OutSummary.MinAdjacentBuildingProgressCM,
			OutSummary.bWorkshopVisibleAtDefaultOrbit ? 1 : 0,
			OutSummary.bWorkshopVisibleAtMaxOrbit ? 1 : 0,
			OutSummary.bTargetBuildingVisibleAtDefaultOrbit ? 1 : 0,
			OutSummary.bTargetBuildingVisibleAtMaxOrbit ? 1 : 0,
			OutSummary.bFurnaceVisibleAtDefaultOrbit ? 1 : 0,
			OutSummary.bFurnaceVisibleAtMaxOrbit ? 1 : 0);
		return true;
	}

	UE_LOG(LogABTSRuntime, Error, TEXT("[ABTS][PCG] All attempts failed. Seed=%d Attempts=%d"), WorldSeed, AttemptCount);
	return false;
}
