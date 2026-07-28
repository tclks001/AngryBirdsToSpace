// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSM3PCGInternal.h"

#include "ABTSRuntime.h"
#include "PCG/ABTSM3TaskGraphGenerator.h"
#include "Planet/ABTSM2Planet.h"

namespace ABTSM3PCG
{
namespace
{
bool IsBuildingTask(const EABTSM3TaskType Type)
{
	return GetTaskSpecs().ContainsByPredicate([Type](const FTaskSpec& Spec)
	{
		return Spec.Type == Type && Spec.bBuilding;
	});
}

bool IsOrdinaryBuildingTask(const EABTSM3TaskType Type)
{
	return Type == EABTSM3TaskType::Workshop
		|| Type == EABTSM3TaskType::TargetBuilding
		|| Type == EABTSM3TaskType::FurnaceRuins;
}

int32 GetConfiguredMinimumRoadDistance(
	const EABTSM3TaskType Type,
	const FABTSM3PCGConfig& Config)
{
	switch (Type)
	{
	case EABTSM3TaskType::Workshop:
		return Config.WorkshopMinMainRoadDistanceCells;
	case EABTSM3TaskType::TargetBuilding:
		return Config.TargetBuildingMinMainRoadDistanceCells;
	case EABTSM3TaskType::FurnaceRuins:
		return Config.FurnaceMinMainRoadDistanceCells;
	default:
		return 0;
	}
}

bool ResolveRouteTangent(
	const FABTSM3TaskNode& Task,
	const TArray<FABTSM3TaskNode>& Tasks,
	const TArray<FABTSM2Cell>& Cells,
	FVector& OutForward)
{
	EABTSM3TaskType PreviousType = EABTSM3TaskType::Unassigned;
	EABTSM3TaskType NextType = EABTSM3TaskType::Unassigned;
	switch (Task.Type)
	{
	case EABTSM3TaskType::Workshop:
		PreviousType = EABTSM3TaskType::Start;
		NextType = EABTSM3TaskType::SlingshotRange;
		break;
	case EABTSM3TaskType::TargetBuilding:
		PreviousType = EABTSM3TaskType::SlingshotRange;
		NextType = EABTSM3TaskType::BridgeGate;
		break;
	case EABTSM3TaskType::FurnaceRuins:
		PreviousType = EABTSM3TaskType::BridgeGate;
		NextType = EABTSM3TaskType::LaunchSite;
		break;
	default:
		return false;
	}

	const int32 PreviousIndex = FindTaskIndexByType(Tasks, PreviousType);
	const int32 NextIndex = FindTaskIndexByType(Tasks, NextType);
	if (PreviousIndex == INDEX_NONE
		|| NextIndex == INDEX_NONE
		|| !Cells.IsValidIndex(Task.SeedCellId)
		|| !Cells.IsValidIndex(Tasks[PreviousIndex].SeedCellId)
		|| !Cells.IsValidIndex(Tasks[NextIndex].SeedCellId))
	{
		return false;
	}

	const FVector Up = Cells[Task.SeedCellId].UnitCenter.GetSafeNormal();
	OutForward = FVector::VectorPlaneProject(
		Cells[Tasks[NextIndex].SeedCellId].UnitCenter
			- Cells[Tasks[PreviousIndex].SeedCellId].UnitCenter,
		Up).GetSafeNormal();
	return !OutForward.IsNearlyZero();
}

bool IsClearanceInsideTask(
	const int32 CenterCellId,
	const int32 RingCount,
	const int32 TaskId,
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3CellState>& States)
{
	if (!Cells.IsValidIndex(CenterCellId) || !States.IsValidIndex(CenterCellId)) return false;
	TSet<int32> Visited;
	TArray<TPair<int32, int32>> Queue;
	Visited.Add(CenterCellId);
	Queue.Add({CenterCellId, 0});
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 CellId = Queue[Head].Key;
		const int32 Depth = Queue[Head].Value;
		if (!States.IsValidIndex(CellId) || States[CellId].TaskId != TaskId) return false;
		if (Depth >= RingCount) continue;
		for (const int32 NeighborId : Cells[CellId].NeighborCellIds)
		{
			if (!Cells.IsValidIndex(NeighborId)) return false;
			if (Visited.Contains(NeighborId)) continue;
			Visited.Add(NeighborId);
			Queue.Add({NeighborId, Depth + 1});
		}
	}
	return true;
}

void BuildTaskDistanceField(
	const FABTSM3TaskNode& Task,
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3CellState>& States,
	const int32 MaximumDistance,
	TArray<int32>& OutDistance)
{
	OutDistance.Init(MAX_int32, Cells.Num());
	if (!Cells.IsValidIndex(Task.SeedCellId) || !States.IsValidIndex(Task.SeedCellId)) return;
	TQueue<int32> Queue;
	OutDistance[Task.SeedCellId] = 0;
	Queue.Enqueue(Task.SeedCellId);
	int32 Current = INDEX_NONE;
	while (Queue.Dequeue(Current))
	{
		const int32 NextDistance = OutDistance[Current] + 1;
		if (NextDistance > MaximumDistance) continue;
		for (const int32 NeighborId : Cells[Current].NeighborCellIds)
		{
			if (!Cells.IsValidIndex(NeighborId)
				|| !States.IsValidIndex(NeighborId)
				|| States[NeighborId].TaskId != Task.TaskId
				|| OutDistance[NeighborId] <= NextDistance)
			{
				continue;
			}
			OutDistance[NeighborId] = NextDistance;
			Queue.Enqueue(NeighborId);
		}
	}
}

void MarkRoadExclusion(
	const int32 CenterCellId,
	const int32 MinimumRoadDistance,
	const TArray<FABTSM2Cell>& Cells,
	TArray<FABTSM3CellState>& States)
{
	if (!Cells.IsValidIndex(CenterCellId) || !States.IsValidIndex(CenterCellId)
		|| MinimumRoadDistance <= 0)
	{
		return;
	}
	TSet<int32> Visited;
	TArray<TPair<int32, int32>> Queue;
	Visited.Add(CenterCellId);
	Queue.Add({CenterCellId, 0});
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 CellId = Queue[Head].Key;
		const int32 Depth = Queue[Head].Value;
		if (Depth >= MinimumRoadDistance) continue;
		States[CellId].bBuildingRoadExclusion = true;
		for (const int32 NeighborId : Cells[CellId].NeighborCellIds)
		{
			if (!Cells.IsValidIndex(NeighborId) || Visited.Contains(NeighborId)) continue;
			Visited.Add(NeighborId);
			Queue.Add({NeighborId, Depth + 1});
		}
	}
}

bool HasCertifiedClearance(
	const int32 CenterCellId,
	const int32 RingCount,
	const int32 TaskId,
	const bool bRequireNoRoad,
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3CellState>& States)
{
	if (!Cells.IsValidIndex(CenterCellId) || !States.IsValidIndex(CenterCellId)) return false;
	TSet<int32> Visited;
	TArray<TPair<int32, int32>> Queue;
	Visited.Add(CenterCellId);
	Queue.Add({CenterCellId, 0});
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 CellId = Queue[Head].Key;
		const int32 Depth = Queue[Head].Value;
		const FABTSM3CellState& State = States[CellId];
		if (State.TaskId != TaskId || !State.bBuildable || State.bWater
			|| (bRequireNoRoad && State.bRoad))
		{
			return false;
		}
		if (Depth >= RingCount) continue;
		for (const int32 NeighborId : Cells[CellId].NeighborCellIds)
		{
			if (!Cells.IsValidIndex(NeighborId) || Visited.Contains(NeighborId)) continue;
			Visited.Add(NeighborId);
			Queue.Add({NeighborId, Depth + 1});
		}
	}
	return true;
}

bool SegmentIntersectsExpandedPad(
	const FVector2D& SegmentStart,
	const FVector2D& SegmentEnd,
	const FVector2D& ExpandedHalfExtent)
{
	const FVector2D Delta = SegmentEnd - SegmentStart;
	float MinimumT = 0.0f;
	float MaximumT = 1.0f;
	const auto ClipAxis = [&MinimumT, &MaximumT](
		const float Start,
		const float Direction,
		const float HalfExtent)
	{
		if (FMath::Abs(Direction) <= UE_SMALL_NUMBER)
		{
			return FMath::Abs(Start) <= HalfExtent;
		}
		float EntryT = (-HalfExtent - Start) / Direction;
		float ExitT = (HalfExtent - Start) / Direction;
		if (EntryT > ExitT) Swap(EntryT, ExitT);
		MinimumT = FMath::Max(MinimumT, EntryT);
		MaximumT = FMath::Min(MaximumT, ExitT);
		return MinimumT <= MaximumT;
	};
	return ClipAxis(SegmentStart.X, Delta.X, ExpandedHalfExtent.X)
		&& ClipAxis(SegmentStart.Y, Delta.Y, ExpandedHalfExtent.Y);
}

bool HasGeometricRoadClearance(
	const FABTSM3TaskNode& Task,
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3CellEdgeState>& EdgeStates,
	const FABTSM3PCGGeometryContext& Geometry,
	FABTSM3CellEdgeKey& OutIntersectingEdge)
{
	OutIntersectingEdge = FABTSM3CellEdgeKey();
	if (!Cells.IsValidIndex(Task.BuildingAnchorCellId)
		|| !Cells.IsValidIndex(Task.RoadPortalCellId))
	{
		return false;
	}
	const FVector PadUp = Cells[Task.BuildingAnchorCellId].UnitCenter.GetSafeNormal();
	const FVector PadForward = FVector::VectorPlaneProject(
		PadUp - Cells[Task.RoadPortalCellId].UnitCenter,
		PadUp).GetSafeNormal();
	if (PadForward.IsNearlyZero()) return false;
	const FVector PadRight = FVector::CrossProduct(PadUp, PadForward).GetSafeNormal();
	if (PadRight.IsNearlyZero()) return false;

	const float SafePlanetRadiusCM = FMath::Max(1.0f, Geometry.PlanetRadiusCM);
	const float PadBlendCM = FMath::Max(0.0f, Geometry.BuildingPadEdgeBlendWidthCM);
	const float SafetyMarginCM = FMath::Max(0.0f, Geometry.RoadPadSafetyMarginCM);
	const FVector2D PadHalfExtent(
		FMath::Max(0.0f, Geometry.BuildingPadHalfExtentCM.X),
		FMath::Max(0.0f, Geometry.BuildingPadHalfExtentCM.Y));
	// TerrainVisualField only applies a pad while the surface direction remains
	// inside this front-facing cap. The edge-specific broad phase below also
	// includes one complete CellTopo edge, so a segment cannot cross the
	// expanded rectangle while both of its endpoints are skipped.
	const float PadApplicabilityAngleRadians = FMath::Acos(0.25f);

	for (const FABTSM3CellEdgeState& Edge : EdgeStates)
	{
		if (Edge.Transport == EABTSM3TransportType::None
			|| !Cells.IsValidIndex(Edge.Key.CellA)
			|| !Cells.IsValidIndex(Edge.Key.CellB))
		{
			continue;
		}
		const FVector UnitA = Cells[Edge.Key.CellA].UnitCenter;
		const FVector UnitB = Cells[Edge.Key.CellB].UnitCenter;
		const float RoadHalfWidthCM =
			Edge.Transport == EABTSM3TransportType::MainRoad
				? FMath::Max(0.0f, Geometry.MainRoadHalfWidthCM)
				: FMath::Max(0.0f, Geometry.TrailHalfWidthCM);
		const FVector2D ExpandedHalfExtent =
			PadHalfExtent
			+ FVector2D(PadBlendCM + RoadHalfWidthCM + SafetyMarginCM);
		const float MaximumProjectedRadiusCM = ExpandedHalfExtent.Size();
		const float MaximumPadAngleRadians = FMath::Min(
			PadApplicabilityAngleRadians,
			FMath::Asin(FMath::Clamp(
				MaximumProjectedRadiusCM / SafePlanetRadiusCM,
				0.0f,
				1.0f)));
		const float EdgeAngleRadians = FMath::Acos(FMath::Clamp(
			FVector::DotProduct(UnitA, UnitB),
			-1.0f,
			1.0f));
		const float SearchAngleRadians = FMath::Min(
			PI - UE_SMALL_NUMBER,
			MaximumPadAngleRadians + EdgeAngleRadians);
		const float NearbyDotThreshold = FMath::Cos(SearchAngleRadians);
		if (FMath::Max(
				FVector::DotProduct(PadUp, UnitA),
				FVector::DotProduct(PadUp, UnitB)) < NearbyDotThreshold)
		{
			continue;
		}
		const FVector DeltaA = (UnitA - PadUp) * SafePlanetRadiusCM;
		const FVector DeltaB = (UnitB - PadUp) * SafePlanetRadiusCM;
		const FVector2D SegmentStart(
			FVector::DotProduct(DeltaA, PadForward),
			FVector::DotProduct(DeltaA, PadRight));
		const FVector2D SegmentEnd(
			FVector::DotProduct(DeltaB, PadForward),
			FVector::DotProduct(DeltaB, PadRight));
		if (SegmentIntersectsExpandedPad(
			SegmentStart,
			SegmentEnd,
			ExpandedHalfExtent))
		{
			OutIntersectingEdge = Edge.Key;
			return false;
		}
	}
	return true;
}
}

bool FBuildingPadPlanner::Reserve(
	const int32 WorldSeed,
	const int32 AttemptIndex,
	const TArray<FABTSM2Cell>& Cells,
	TArray<FABTSM3TaskNode>& Tasks,
	const FABTSM3PCGConfig& Config,
	TArray<FABTSM3CellState>& CellStates,
	FString& OutFailure) const
{
	OutFailure.Reset();
	if (Cells.Num() != CellStates.Num())
	{
		OutFailure = TEXT("BuildingPadInvalidCellStateCount");
		return false;
	}
	for (FABTSM3CellState& State : CellStates)
	{
		State.bBuildingAnchor = false;
		State.bBuildingRoadExclusion = false;
	}
	for (FABTSM3TaskNode& Task : Tasks)
	{
		Task.BuildingAnchorCellId = INDEX_NONE;
		if (!IsBuildingTask(Task.Type)) continue;
		Task.RoadPortalCellId = Task.SeedCellId;
		if (!Cells.IsValidIndex(Task.SeedCellId) || Task.CellIds.IsEmpty())
		{
			OutFailure = FString::Printf(TEXT("BuildingPadInvalidTask:%d"), Task.TaskId);
			return false;
		}
		const int32 SafeRings = FMath::Clamp(Config.BuildingPadClearanceRingCells, 1, 4);

		if (Task.Type == EABTSM3TaskType::LaunchSite)
		{
			if (!IsClearanceInsideTask(
				Task.SeedCellId,
				SafeRings + 1,
				Task.TaskId,
				Cells,
				CellStates))
			{
				OutFailure = FString::Printf(
					TEXT("BuildingPadFinaleGuardOutsideTask:%d:Rings=%d"),
					Task.TaskId,
					SafeRings + 1);
				return false;
			}
			Task.BuildingAnchorCellId = Task.SeedCellId;
			CellStates[Task.BuildingAnchorCellId].bBuildingAnchor = true;
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][PCG][BuildingPadReserve] Task=%d Type=%d Portal=%d Anchor=%d Finale=1 GuardRings=%d"),
				Task.TaskId, static_cast<int32>(Task.Type), Task.RoadPortalCellId,
				Task.BuildingAnchorCellId, SafeRings + 1);
			continue;
		}
		if (!IsOrdinaryBuildingTask(Task.Type))
		{
			OutFailure = FString::Printf(
				TEXT("BuildingPadUnsupportedTask:%d:Type=%d"),
				Task.TaskId, static_cast<int32>(Task.Type));
			return false;
		}

		const int32 MinimumRoadDistance = FMath::Max(
			GetConfiguredMinimumRoadDistance(Task.Type, Config),
			SafeRings + 1);
		const int32 MaximumSearchDistance = MinimumRoadDistance
			+ FMath::Clamp(Config.BuildingAnchorSearchSlackCells, 0, 6);
		FVector RouteForward = FVector::ZeroVector;
		if (!ResolveRouteTangent(Task, Tasks, Cells, RouteForward))
		{
			OutFailure = FString::Printf(TEXT("BuildingPadRouteTangentInvalid:%d"), Task.TaskId);
			return false;
		}
		const FVector Up = Cells[Task.SeedCellId].UnitCenter.GetSafeNormal();
		FVector PreferredSide = FVector::CrossProduct(Up, RouteForward).GetSafeNormal();
		if (PreferredSide.IsNearlyZero())
		{
			OutFailure = FString::Printf(TEXT("BuildingPadRouteSideInvalid:%d"), Task.TaskId);
			return false;
		}
		const uint32 SideHash = HashCombineFast(
			MakeStageSeed(WorldSeed, TEXT("BuildingPadSide"), AttemptIndex),
			GetTypeHash(Task.TaskId));
		const int32 SideSign = (SideHash & 1u) == 0u ? 1 : -1;
		PreferredSide *= static_cast<float>(SideSign);

		TArray<int32> DistanceFromPortal;
		BuildTaskDistanceField(
			Task,
			Cells,
			CellStates,
			MaximumSearchDistance,
			DistanceFromPortal);

		struct FCandidate
		{
			int32 CellId = INDEX_NONE;
			int32 Distance = MAX_int32;
			int32 SideScore = MIN_int32;
			int32 AlongScore = MAX_int32;
			float SideAlignment = -1.0f;
			float AlongAlignment = 1.0f;
		};
		TArray<FCandidate> Candidates;
		for (const int32 CandidateCellId : Task.CellIds)
		{
			if (!Cells.IsValidIndex(CandidateCellId)
				|| !DistanceFromPortal.IsValidIndex(CandidateCellId))
			{
				continue;
			}
			const int32 Distance = DistanceFromPortal[CandidateCellId];
			if (Distance < MinimumRoadDistance || Distance > MaximumSearchDistance
				|| !IsClearanceInsideTask(
					CandidateCellId,
					SafeRings + 1,
					Task.TaskId,
					Cells,
					CellStates))
			{
				continue;
			}
			const FVector OffsetDirection = FVector::VectorPlaneProject(
				Cells[CandidateCellId].UnitCenter,
				Up).GetSafeNormal();
			if (OffsetDirection.IsNearlyZero()) continue;
			const float SideAlignment = FVector::DotProduct(OffsetDirection, PreferredSide);
			const float AlongAlignment = FMath::Abs(
				FVector::DotProduct(OffsetDirection, RouteForward));
			// Keep the construction pocket on the selected route side. A candidate
			// whose longitudinal component dominates would merely move the beat
			// forward/backward instead of creating an off-road attack pocket.
			if (SideAlignment < 0.55f || AlongAlignment > 0.80f) continue;
			Candidates.Add({
				CandidateCellId,
				Distance,
				FMath::RoundToInt(SideAlignment * 1000000.0f),
				FMath::RoundToInt(AlongAlignment * 1000000.0f),
				SideAlignment,
				AlongAlignment});
		}
		Candidates.Sort([](const FCandidate& A, const FCandidate& B)
		{
			if (A.SideScore != B.SideScore) return A.SideScore > B.SideScore;
			if (A.AlongScore != B.AlongScore) return A.AlongScore < B.AlongScore;
			if (A.Distance != B.Distance) return A.Distance < B.Distance;
			return A.CellId < B.CellId;
		});
		if (Candidates.IsEmpty())
		{
			OutFailure = FString::Printf(
				TEXT("BuildingPadNoReservedFootprint:%d:Min=%d:Max=%d:Rings=%d:Side=%d"),
				Task.TaskId, MinimumRoadDistance, MaximumSearchDistance, SafeRings, SideSign);
			return false;
		}

		Task.BuildingAnchorCellId = Candidates[0].CellId;
		CellStates[Task.BuildingAnchorCellId].bBuildingAnchor = true;
		MarkRoadExclusion(
			Task.BuildingAnchorCellId,
			MinimumRoadDistance,
			Cells,
			CellStates);
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][PCG][BuildingPadReserve] Task=%d Type=%d Portal=%d Anchor=%d PortalDistance=%d MinRoad=%d MaxSearch=%d Rings=%d Side=%d SideAlign=%.3f AlongAlign=%.3f"),
			Task.TaskId, static_cast<int32>(Task.Type), Task.RoadPortalCellId,
			Task.BuildingAnchorCellId, Candidates[0].Distance, MinimumRoadDistance,
			MaximumSearchDistance, SafeRings, SideSign, Candidates[0].SideAlignment,
			Candidates[0].AlongAlignment);
	}
	return true;
}

bool FBuildingPadPlanner::Certify(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3TaskNode>& Tasks,
	const TArray<FABTSM3CellEdgeState>& EdgeStates,
	const FABTSM3PCGConfig& Config,
	const FABTSM3PCGGeometryContext& GeometryContext,
	TArray<FABTSM3CellState>& CellStates,
	FString& OutFailure) const
{
	OutFailure.Reset();
	if (Cells.Num() != CellStates.Num())
	{
		OutFailure = TEXT("BuildingPadInvalidCellStateCount");
		return false;
	}
	const int32 SafeRings = FMath::Clamp(Config.BuildingPadClearanceRingCells, 1, 4);
	TSet<int32> ExpectedAnchors;
	for (const FABTSM3TaskNode& Task : Tasks)
	{
		if (!IsBuildingTask(Task.Type)) continue;
		if (!Cells.IsValidIndex(Task.RoadPortalCellId)
			|| Task.RoadPortalCellId != Task.SeedCellId
			|| !Cells.IsValidIndex(Task.BuildingAnchorCellId))
		{
			OutFailure = FString::Printf(TEXT("BuildingPadReservationInvalid:%d"), Task.TaskId);
			return false;
		}
		if (ExpectedAnchors.Contains(Task.BuildingAnchorCellId)
			|| !CellStates[Task.BuildingAnchorCellId].bBuildingAnchor
			|| CellStates[Task.BuildingAnchorCellId].TaskId != Task.TaskId)
		{
			OutFailure = FString::Printf(
				TEXT("BuildingPadAnchorMismatch:%d:%d"),
				Task.TaskId, Task.BuildingAnchorCellId);
			return false;
		}
		ExpectedAnchors.Add(Task.BuildingAnchorCellId);

		const bool bOrdinary = IsOrdinaryBuildingTask(Task.Type);
		if (!HasCertifiedClearance(
			Task.BuildingAnchorCellId,
			SafeRings,
			Task.TaskId,
			bOrdinary,
			Cells,
			CellStates))
		{
			OutFailure = FString::Printf(
				TEXT("BuildingPadNoCertifiedFootprint:%d:Anchor=%d:Rings=%d"),
				Task.TaskId, Task.BuildingAnchorCellId, SafeRings);
			return false;
		}
		if (bOrdinary)
		{
			const int32 MinimumRoadDistance = FMath::Max(
				GetConfiguredMinimumRoadDistance(Task.Type, Config),
				SafeRings + 1);
			const FABTSM3CellState& AnchorState = CellStates[Task.BuildingAnchorCellId];
			if (AnchorState.bRoad
				|| AnchorState.MainRoadDistance == MAX_int32
				|| AnchorState.MainRoadDistance < MinimumRoadDistance)
			{
				OutFailure = FString::Printf(
					TEXT("BuildingPadRoadDistanceInvalid:%d:Anchor=%d:Actual=%d:Required=%d"),
					Task.TaskId, Task.BuildingAnchorCellId,
					AnchorState.MainRoadDistance, MinimumRoadDistance);
				return false;
			}
			FABTSM3CellEdgeKey IntersectingEdge;
			if (!HasGeometricRoadClearance(
				Task,
				Cells,
				EdgeStates,
				GeometryContext,
				IntersectingEdge))
			{
				OutFailure = FString::Printf(
					TEXT("BuildingPadGeometricRoadOverlap:%d:Anchor=%d:Edge=%d-%d"),
					Task.TaskId,
					Task.BuildingAnchorCellId,
					IntersectingEdge.CellA,
					IntersectingEdge.CellB);
				return false;
			}
		}

		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][PCG][BuildingPad] Task=%d Type=%d Seed=%d RoadPortal=%d Anchor=%d Rings=%d Shifted=%d MainRoadDistance=%d GeometricRoadClearance=%d"),
			Task.TaskId, static_cast<int32>(Task.Type), Task.SeedCellId,
			Task.RoadPortalCellId, Task.BuildingAnchorCellId, SafeRings,
			Task.BuildingAnchorCellId != Task.SeedCellId ? 1 : 0,
			CellStates[Task.BuildingAnchorCellId].MainRoadDistance,
			bOrdinary ? 1 : 0);
	}

	for (int32 CellId = 0; CellId < CellStates.Num(); ++CellId)
	{
		const FABTSM3CellState& State = CellStates[CellId];
		if (State.bBuildingAnchor && !ExpectedAnchors.Contains(CellId))
		{
			OutFailure = FString::Printf(TEXT("BuildingPadUnexpectedAnchor:%d"), CellId);
			return false;
		}
		if (State.bBuildingRoadExclusion && State.bRoad)
		{
			OutFailure = FString::Printf(TEXT("BuildingPadRoadEnteredExclusion:%d"), CellId);
			return false;
		}
	}
	return true;
}
}
