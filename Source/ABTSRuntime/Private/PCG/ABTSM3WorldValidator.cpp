// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSM3PCGInternal.h"

#include "ABTSRuntime.h"
#include "PCG/ABTSM3TaskGraphGenerator.h"
#include "Planet/ABTSM2Planet.h"

namespace ABTSM3PCG
{
namespace
{
bool IsReachable(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3CellEdgeState>& EdgeStates,
	const int32 Start,
	const int32 Goal,
	const bool bBridgeBuilt)
{
	if (!Cells.IsValidIndex(Start) || !Cells.IsValidIndex(Goal)) return false;
	TMap<FABTSM3CellEdgeKey, const FABTSM3CellEdgeState*> EdgeMap;
	for (const FABTSM3CellEdgeState& Edge : EdgeStates) EdgeMap.Add(Edge.Key, &Edge);
	TBitArray<> Visited(false, Cells.Num());
	TQueue<int32> Queue;
	Visited[Start] = true;
	Queue.Enqueue(Start);
	int32 Current = INDEX_NONE;
	while (Queue.Dequeue(Current))
	{
		if (Current == Goal) return true;
		for (const int32 Neighbor : Cells[Current].NeighborCellIds)
		{
			if (Visited[Neighbor]) continue;
			const FABTSM3CellEdgeState* const* EdgePtr = EdgeMap.Find(FABTSM3CellEdgeKey(Current, Neighbor));
			if (EdgePtr && (*EdgePtr)->bBlocksOnFoot)
			{
				const bool bOpenBridge = bBridgeBuilt
					&& ((*EdgePtr)->Crossing == EABTSM3CrossingType::BridgeSite
						|| (*EdgePtr)->Crossing == EABTSM3CrossingType::Bridge);
				const bool bOpenFord = (*EdgePtr)->Crossing == EABTSM3CrossingType::Ford
					|| (*EdgePtr)->Crossing == EABTSM3CrossingType::FallenLog;
				if (!bOpenBridge && !bOpenFord) continue;
			}
			Visited[Neighbor] = true;
			Queue.Enqueue(Neighbor);
		}
	}
	return false;
}

const FABTSM3TaskNode* FindTask(
	const TArray<FABTSM3TaskNode>& Tasks,
	const EABTSM3TaskType Type)
{
	return Tasks.FindByPredicate([Type](const FABTSM3TaskNode& Task)
	{
		return Task.Type == Type;
	});
}

int32 FindClosestCellByWalk(
	const TArray<FABTSM2Cell>& Cells,
	const FVector& Direction,
	const int32 InitialCell)
{
	if (Cells.IsEmpty()) return INDEX_NONE;
	int32 Current = Cells.IsValidIndex(InitialCell) ? InitialCell : 0;
	float CurrentDot = FVector::DotProduct(Cells[Current].UnitCenter, Direction);
	for (int32 Iteration = 0; Iteration < Cells.Num(); ++Iteration)
	{
		int32 BestNeighbor = Current;
		float BestDot = CurrentDot;
		for (const int32 Neighbor : Cells[Current].NeighborCellIds)
		{
			const float Dot = FVector::DotProduct(Cells[Neighbor].UnitCenter, Direction);
			if (Dot > BestDot + UE_SMALL_NUMBER
				|| (FMath::IsNearlyEqual(Dot, BestDot, UE_SMALL_NUMBER) && Neighbor < BestNeighbor))
			{
				BestNeighbor = Neighbor;
				BestDot = Dot;
			}
		}
		if (BestNeighbor == Current) break;
		Current = BestNeighbor;
		CurrentDot = BestDot;
	}
	return Current;
}

bool HasTerrainLineOfSight(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3CellState>& CellStates,
	const FVector& Camera,
	const FVector& Target,
	const float PlanetRadiusCM,
	const float TerrainHeightScaleCM,
	const int32 TraceSamples,
	const int32 InitialCell)
{
	const int32 SafeSamples = FMath::Clamp(TraceSamples, 16, 128);
	int32 CellHint = InitialCell;
	for (int32 SampleIndex = 1; SampleIndex < SafeSamples; ++SampleIndex)
	{
		const float Alpha = static_cast<float>(SampleIndex) / static_cast<float>(SafeSamples);
		const FVector Point = FMath::Lerp(Camera, Target, Alpha);
		const float PointRadius = Point.Size();
		if (PointRadius <= UE_SMALL_NUMBER) return false;
		CellHint = FindClosestCellByWalk(Cells, Point / PointRadius, CellHint);
		if (!CellStates.IsValidIndex(CellHint)) return false;
		const float SurfaceRadius = PlanetRadiusCM
			+ CellStates[CellHint].LogicalHeight01 * FMath::Max(0.0f, TerrainHeightScaleCM);
		// A small positive margin keeps the pure-data test conservative relative
		// to the rendered SDF surface and target bounds.
		if (PointRadius < SurfaceRadius + 8.0f) return false;
	}
	return true;
}

bool EvaluateStartVisibility(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3CellState>& CellStates,
	const FABTSM3TaskNode& StartTask,
	const FABTSM3TaskNode& TargetTask,
	const TArray<FABTSM3TaskLink>& Links,
	const FABTSM3PCGConfig& Config,
	const float PlanetRadiusCM,
	const float TerrainHeightScaleCM,
	const float OrbitDistanceCM)
{
	if (!Cells.IsValidIndex(StartTask.RoadPortalCellId)
		|| !Cells.IsValidIndex(TargetTask.BuildingAnchorCellId))
	{
		return false;
	}

	const FABTSM3TaskLink* FirstMainLink = Links.FindByPredicate([&StartTask](const FABTSM3TaskLink& Link)
	{
		return Link.TaskA == StartTask.TaskId
			&& (Link.Role == EABTSM3TaskLinkRole::MainPath
				|| Link.Role == EABTSM3TaskLinkRole::LockedGate)
			&& Link.CorridorCells.Num() >= 2;
	});
	if (FirstMainLink == nullptr) return false;

	const FVector StartUp = Cells[StartTask.RoadPortalCellId].UnitCenter.GetSafeNormal();
	FVector RouteForward = FVector::ZeroVector;
	for (const int32 CorridorCell : FirstMainLink->CorridorCells)
	{
		if (CorridorCell == StartTask.RoadPortalCellId || !Cells.IsValidIndex(CorridorCell)) continue;
		RouteForward = FVector::VectorPlaneProject(Cells[CorridorCell].UnitCenter, StartUp).GetSafeNormal();
		if (!RouteForward.IsNearlyZero()) break;
	}
	if (RouteForward.IsNearlyZero()) return false;

	const float StartSurfaceRadius = PlanetRadiusCM
		+ CellStates[StartTask.RoadPortalCellId].LogicalHeight01 * FMath::Max(0.0f, TerrainHeightScaleCM);
	const FVector Pivot = StartUp * (
		StartSurfaceRadius
		+ FMath::Max(0.0f, Config.VisibilityCharacterCenterHeightCM)
		+ Config.VisibilityLookAtHeightCM);
	const float ElevationRadians = FMath::DegreesToRadians(
		FMath::Clamp(Config.VisibilityElevationDegrees, 20.0f, 85.0f));
	const FVector CameraOffset = (
		StartUp * FMath::Sin(ElevationRadians)
		- RouteForward * FMath::Cos(ElevationRadians)).GetSafeNormal();
	const FVector Camera = Pivot + CameraOffset * FMath::Max(300.0f, OrbitDistanceCM);

	const FVector TargetUp = Cells[TargetTask.BuildingAnchorCellId].UnitCenter.GetSafeNormal();
	const float TargetSurfaceRadius = PlanetRadiusCM
		+ CellStates[TargetTask.BuildingAnchorCellId].LogicalHeight01 * FMath::Max(0.0f, TerrainHeightScaleCM);
	const FVector Target = TargetUp * (
		TargetSurfaceRadius + FMath::Max(100.0f, Config.VisibilityTargetHeightCM));
	return HasTerrainLineOfSight(
		Cells,
		CellStates,
		Camera,
		Target,
		PlanetRadiusCM,
		TerrainHeightScaleCM,
		Config.VisibilityTraceSamples,
		StartTask.RoadPortalCellId);
}

bool AccumulateOrderedMainRoute(
	const TArray<FABTSM3TaskNode>& Tasks,
	const TArray<FABTSM3TaskLink>& Links,
	const FABTSM3TaskNode& StartTask,
	const FABTSM3TaskNode& LaunchTask,
	float& OutLengthCM)
{
	OutLengthCM = 0.0f;
	TSet<int32> VisitedTasks;
	TSet<int32> VisitedRouteCells;
	int32 PreviousRouteCell = INDEX_NONE;
	int32 CurrentTaskId = StartTask.TaskId;
	for (int32 Guard = 0; Guard < Tasks.Num(); ++Guard)
	{
		if (CurrentTaskId == LaunchTask.TaskId) return true;
		if (VisitedTasks.Contains(CurrentTaskId)) return false;
		VisitedTasks.Add(CurrentTaskId);
		const FABTSM3TaskLink* NextLink = Links.FindByPredicate([CurrentTaskId](const FABTSM3TaskLink& Link)
		{
			return Link.TaskA == CurrentTaskId
				&& (Link.Role == EABTSM3TaskLinkRole::MainPath
					|| Link.Role == EABTSM3TaskLinkRole::LockedGate);
		});
		if (NextLink == nullptr || NextLink->CorridorLengthCM <= 0.0f) return false;
		for (int32 CellIndex = 0; CellIndex < NextLink->CorridorCells.Num(); ++CellIndex)
		{
			const int32 CellId = NextLink->CorridorCells[CellIndex];
			if (CellIndex == 0 && PreviousRouteCell != INDEX_NONE)
			{
				if (CellId != PreviousRouteCell) return false;
				continue;
			}
			if (VisitedRouteCells.Contains(CellId)) return false;
			VisitedRouteCells.Add(CellId);
			PreviousRouteCell = CellId;
		}
		OutLengthCM += NextLink->CorridorLengthCM;
		CurrentTaskId = NextLink->TaskB;
	}
	return CurrentTaskId == LaunchTask.TaskId;
}

int32 MinimumRoadDistanceForTask(
	const EABTSM3TaskType Type,
	const FABTSM3PCGConfig& Config)
{
	switch (Type)
	{
	case EABTSM3TaskType::Workshop:
		return FMath::Max(
			Config.BuildingPadClearanceRingCells + 1,
			Config.WorkshopMinMainRoadDistanceCells);
	case EABTSM3TaskType::TargetBuilding:
		return FMath::Max(
			Config.BuildingPadClearanceRingCells + 1,
			Config.TargetBuildingMinMainRoadDistanceCells);
	case EABTSM3TaskType::FurnaceRuins:
		return FMath::Max(
			Config.BuildingPadClearanceRingCells + 1,
			Config.FurnaceMinMainRoadDistanceCells);
	default:
		return 0;
	}
}
}

bool FWorldValidator::Validate(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3TaskNode>& Tasks,
	const TArray<FABTSM3TaskLink>& Links,
	const TArray<FABTSM3CellState>& CellStates,
	const TArray<FABTSM3CellEdgeState>& EdgeStates,
	const FABTSM3CellEdgeKey& BridgeEdge,
	const FABTSM3PCGConfig& Config,
	const FABTSM3PCGGeometryContext& GeometryContext,
	FABTSM3PCGSummary& Summary,
	FString& OutFailure) const
{
	OutFailure.Reset();
	if (Cells.Num() != CellStates.Num() || Tasks.Num() < 9 || Links.IsEmpty())
	{
		OutFailure = TEXT("InvalidResultSizes");
		return false;
	}
	for (const FABTSM3TaskNode& Task : Tasks)
	{
		if (!Cells.IsValidIndex(Task.SeedCellId)
			|| !Cells.IsValidIndex(Task.RoadPortalCellId)
			|| Task.CellIds.IsEmpty())
		{
			OutFailure = FString::Printf(TEXT("InvalidTask_%d"), Task.TaskId);
			return false;
		}
	}
	for (const FABTSM3TaskLink& Link : Links)
	{
		if (Link.CorridorCells.Num() < 2
			|| Link.CorridorEdges.Num() + 1 != Link.CorridorCells.Num()
			|| Link.CorridorLengthCM <= 0.0f)
		{
			OutFailure = FString::Printf(TEXT("InvalidCorridor_%d"), Link.LinkId);
			return false;
		}
	}

	const FABTSM3TaskNode* StartTask = FindTask(Tasks, EABTSM3TaskType::Start);
	const FABTSM3TaskNode* WorkshopTask = FindTask(Tasks, EABTSM3TaskType::Workshop);
	const FABTSM3TaskNode* TargetTask = FindTask(Tasks, EABTSM3TaskType::TargetBuilding);
	const FABTSM3TaskNode* FurnaceTask = FindTask(Tasks, EABTSM3TaskType::FurnaceRuins);
	const FABTSM3TaskNode* LaunchTask = FindTask(Tasks, EABTSM3TaskType::LaunchSite);
	const FABTSM3TaskNode* SatelliteWindowTask = FindTask(Tasks, EABTSM3TaskType::SatelliteWindow);
	if (StartTask == nullptr || WorkshopTask == nullptr || TargetTask == nullptr
		|| FurnaceTask == nullptr || LaunchTask == nullptr || SatelliteWindowTask == nullptr)
	{
		OutFailure = TEXT("MissingRequiredTask");
		return false;
	}

	if (!AccumulateOrderedMainRoute(Tasks, Links, *StartTask, *LaunchTask, Summary.MainRouteLengthCM))
	{
		OutFailure = TEXT("MainRouteDiscontinuous");
		return false;
	}
	if (Summary.MainRouteLengthCM + KINDA_SMALL_NUMBER < FMath::Max(0.0f, Config.MinMainRouteLengthCM))
	{
		OutFailure = FString::Printf(
			TEXT("MainRouteTooShort:Actual=%.1f:Required=%.1f"),
			Summary.MainRouteLengthCM,
			Config.MinMainRouteLengthCM);
		return false;
	}

	const float WorkshopToTargetCM =
		TargetTask->RouteProgressDistanceCM - WorkshopTask->RouteProgressDistanceCM;
	const float TargetToFurnaceCM =
		FurnaceTask->RouteProgressDistanceCM - TargetTask->RouteProgressDistanceCM;
	Summary.MinAdjacentBuildingProgressCM = FMath::Min(WorkshopToTargetCM, TargetToFurnaceCM);
	if (WorkshopToTargetCM <= 0.0f || TargetToFurnaceCM <= 0.0f
		|| Summary.MinAdjacentBuildingProgressCM + KINDA_SMALL_NUMBER
			< FMath::Max(0.0f, Config.MinAdjacentBuildingProgressCM))
	{
		OutFailure = FString::Printf(
			TEXT("BuildingProgressTooClose:WorkshopTarget=%.1f:TargetFurnace=%.1f:Required=%.1f"),
			WorkshopToTargetCM,
			TargetToFurnaceCM,
			Config.MinAdjacentBuildingProgressCM);
		return false;
	}

	if (!Cells.IsValidIndex(LaunchTask->BuildingAnchorCellId)
		|| !Cells.IsValidIndex(SatelliteWindowTask->SeedCellId))
	{
		OutFailure = TEXT("FinaleAnchorMissing");
		return false;
	}
	Summary.SatelliteLaunchAngularSeparationDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
		FVector::DotProduct(
			Cells[LaunchTask->BuildingAnchorCellId].UnitCenter,
			Cells[SatelliteWindowTask->SeedCellId].UnitCenter),
		-1.0f,
		1.0f)));
	if (Summary.SatelliteLaunchAngularSeparationDegrees + KINDA_SMALL_NUMBER
		< FMath::Clamp(Config.MinSatelliteLaunchAngularSeparationDegrees, 0.0f, 179.0f))
	{
		OutFailure = FString::Printf(
			TEXT("SatelliteLaunchSeparationTooSmall:Actual=%.2f:Required=%.2f"),
			Summary.SatelliteLaunchAngularSeparationDegrees,
			Config.MinSatelliteLaunchAngularSeparationDegrees);
		return false;
	}

	const int32 StartCell = StartTask->RoadPortalCellId;
	Summary.bBridgeLockedBeforeBuild = !IsReachable(
		Cells,
		EdgeStates,
		StartCell,
		FurnaceTask->RoadPortalCellId,
		false);
	Summary.bMainPathReachableAfterBridge = IsReachable(
		Cells,
		EdgeStates,
		StartCell,
		LaunchTask->RoadPortalCellId,
		true);
	if (!Summary.bBridgeLockedBeforeBuild)
	{
		OutFailure = TEXT("BridgeBypassExists");
		return false;
	}
	if (!Summary.bMainPathReachableAfterBridge)
	{
		OutFailure = TEXT("BridgeDoesNotUnlockMainPath");
		return false;
	}
	if (!IsReachable(Cells, EdgeStates, StartCell, WorkshopTask->RoadPortalCellId, false)
		|| !IsReachable(Cells, EdgeStates, StartCell, TargetTask->RoadPortalCellId, false))
	{
		OutFailure = TEXT("PreBridgeProgressionBlocked");
		return false;
	}
	if (FindEdgeStateIndex(EdgeStates, BridgeEdge) == INDEX_NONE)
	{
		OutFailure = TEXT("MissingBridgeEdge");
		return false;
	}

	TMap<int32, int32> BuildingAnchorCountByTask;
	for (int32 CellId = 0; CellId < CellStates.Num(); ++CellId)
	{
		const FABTSM3CellState& State = CellStates[CellId];
		if (State.bBuildingRoadExclusion && State.bRoad)
		{
			OutFailure = FString::Printf(TEXT("RoadEnteredBuildingExclusion:%d"), CellId);
			return false;
		}
		if (!State.bBuildingAnchor) continue;
		if (!State.bBuildable || State.bWater)
		{
			OutFailure = FString::Printf(TEXT("BuildingAnchorInvalid:%d"), CellId);
			return false;
		}
		const int32 TaskIndex = FindTaskIndexById(Tasks, State.TaskId);
		if (TaskIndex == INDEX_NONE || Tasks[TaskIndex].BuildingAnchorCellId != CellId)
		{
			OutFailure = FString::Printf(TEXT("BuildingAnchorTaskMismatch:%d"), CellId);
			return false;
		}
		const int32 RequiredRoadDistance = MinimumRoadDistanceForTask(Tasks[TaskIndex].Type, Config);
		if (RequiredRoadDistance > 0
			&& (State.bRoad || State.MainRoadDistance < RequiredRoadDistance))
		{
			OutFailure = FString::Printf(
				TEXT("BuildingRoadSetbackInvalid:Task=%d:Cell=%d:Actual=%d:Required=%d"),
				State.TaskId,
				CellId,
				State.MainRoadDistance,
				RequiredRoadDistance);
			return false;
		}
		BuildingAnchorCountByTask.FindOrAdd(State.TaskId)++;
	}
	for (const FABTSM3TaskNode& Task : Tasks)
	{
		const FTaskSpec* Spec = GetTaskSpecs().FindByPredicate([Type = Task.Type](const FTaskSpec& Candidate)
		{
			return Candidate.Type == Type;
		});
		if (Spec != nullptr && Spec->bBuilding
			&& BuildingAnchorCountByTask.FindRef(Task.TaskId) != 1)
		{
			OutFailure = FString::Printf(
				TEXT("BuildingAnchorCountInvalid:%d:%d"),
				Task.TaskId,
				BuildingAnchorCountByTask.FindRef(Task.TaskId));
			return false;
		}
	}

	Summary.bWorkshopVisibleAtDefaultOrbit = EvaluateStartVisibility(
		Cells,
		CellStates,
		*StartTask,
		*WorkshopTask,
		Links,
		Config,
		GeometryContext.PlanetRadiusCM,
		GeometryContext.TerrainHeightScaleCM,
		Config.VisibilityDefaultOrbitDistanceCM);
	Summary.bWorkshopVisibleAtMaxOrbit = EvaluateStartVisibility(
		Cells,
		CellStates,
		*StartTask,
		*WorkshopTask,
		Links,
		Config,
		GeometryContext.PlanetRadiusCM,
		GeometryContext.TerrainHeightScaleCM,
		Config.VisibilityMaxOrbitDistanceCM);
	Summary.bTargetBuildingVisibleAtDefaultOrbit = EvaluateStartVisibility(
		Cells,
		CellStates,
		*StartTask,
		*TargetTask,
		Links,
		Config,
		GeometryContext.PlanetRadiusCM,
		GeometryContext.TerrainHeightScaleCM,
		Config.VisibilityDefaultOrbitDistanceCM);
	Summary.bTargetBuildingVisibleAtMaxOrbit = EvaluateStartVisibility(
		Cells,
		CellStates,
		*StartTask,
		*TargetTask,
		Links,
		Config,
		GeometryContext.PlanetRadiusCM,
		GeometryContext.TerrainHeightScaleCM,
		Config.VisibilityMaxOrbitDistanceCM);
	Summary.bFurnaceVisibleAtDefaultOrbit = EvaluateStartVisibility(
		Cells,
		CellStates,
		*StartTask,
		*FurnaceTask,
		Links,
		Config,
		GeometryContext.PlanetRadiusCM,
		GeometryContext.TerrainHeightScaleCM,
		Config.VisibilityDefaultOrbitDistanceCM);
	Summary.bFurnaceVisibleAtMaxOrbit = EvaluateStartVisibility(
		Cells,
		CellStates,
		*StartTask,
		*FurnaceTask,
		Links,
		Config,
		GeometryContext.PlanetRadiusCM,
		GeometryContext.TerrainHeightScaleCM,
		Config.VisibilityMaxOrbitDistanceCM);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][PCG][Visibility] Workshop=%d/%d Target=%d/%d Furnace=%d/%d Orbit=%.0f/%.0f Elevation=%.1f"),
		Summary.bWorkshopVisibleAtDefaultOrbit ? 1 : 0,
		Summary.bWorkshopVisibleAtMaxOrbit ? 1 : 0,
		Summary.bTargetBuildingVisibleAtDefaultOrbit ? 1 : 0,
		Summary.bTargetBuildingVisibleAtMaxOrbit ? 1 : 0,
		Summary.bFurnaceVisibleAtDefaultOrbit ? 1 : 0,
		Summary.bFurnaceVisibleAtMaxOrbit ? 1 : 0,
		Config.VisibilityDefaultOrbitDistanceCM,
		Config.VisibilityMaxOrbitDistanceCM,
		Config.VisibilityElevationDegrees);

	if (Config.bRequireWeekOneVisibilityContract
		&& (!Summary.bWorkshopVisibleAtDefaultOrbit
			|| !Summary.bWorkshopVisibleAtMaxOrbit
			|| Summary.bTargetBuildingVisibleAtDefaultOrbit
			|| Summary.bTargetBuildingVisibleAtMaxOrbit
			|| Summary.bFurnaceVisibleAtDefaultOrbit
			|| Summary.bFurnaceVisibleAtMaxOrbit))
	{
		OutFailure = FString::Printf(
			TEXT("WeekOneVisibilityContract:Workshop=%d/%d:Target=%d/%d:Furnace=%d/%d"),
			Summary.bWorkshopVisibleAtDefaultOrbit ? 1 : 0,
			Summary.bWorkshopVisibleAtMaxOrbit ? 1 : 0,
			Summary.bTargetBuildingVisibleAtDefaultOrbit ? 1 : 0,
			Summary.bTargetBuildingVisibleAtMaxOrbit ? 1 : 0,
			Summary.bFurnaceVisibleAtDefaultOrbit ? 1 : 0,
			Summary.bFurnaceVisibleAtMaxOrbit ? 1 : 0);
		return false;
	}

	Summary.bAccepted = true;
	return true;
}
}
