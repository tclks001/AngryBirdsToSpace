// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3MonthlyFinaleAnchor.h"

#include "ABTSRuntime.h"
#include "Planet/ABTSM2Planet.h"

namespace ABTSM3R52FinaleAnchorPrivate
{
constexpr uint64 Fnv1a64OffsetBasis = 14695981039346656037ull;
constexpr uint64 Fnv1a64Prime = 1099511628211ull;
constexpr double VectorQuantization = 1000.0;

class FCanonicalHash64
{
public:
	void AddUInt64(const uint64 Value)
	{
		for (int32 Shift = 0; Shift < 64; Shift += 8)
		{
			Hash ^= static_cast<uint8>((Value >> Shift) & 0xffull);
			Hash *= Fnv1a64Prime;
		}
	}

	void AddInt64(const int64 Value) { AddUInt64(static_cast<uint64>(Value)); }
	void AddInt32(const int32 Value) { AddUInt64(static_cast<uint32>(Value)); }
	void AddBool(const bool bValue) { AddUInt64(bValue ? 1ull : 0ull); }
	void AddFloat(const float Value)
	{
		AddInt64(FMath::RoundToInt64(
			static_cast<double>(Value) * VectorQuantization));
	}
	void AddVector(const FVector& Value)
	{
		AddFloat(Value.X);
		AddFloat(Value.Y);
		AddFloat(Value.Z);
	}
	void AddIntArray(const TArray<int32>& Values)
	{
		AddInt32(Values.Num());
		for (const int32 Value : Values)
		{
			AddInt32(Value);
		}
	}
	uint64 Get() const { return Hash; }

private:
	uint64 Hash = Fnv1a64OffsetBasis;
};

bool IsFiniteFinaleVector(const FVector& Value)
{
	return !Value.ContainsNaN()
		&& FMath::IsFinite(Value.X)
		&& FMath::IsFinite(Value.Y)
		&& FMath::IsFinite(Value.Z);
}

bool ValidateConfig(
	const FABTSM3MonthlyFinaleAnchorConfig& Config,
	FString& OutFailure)
{
	if (Config.TerminalSearchWindowCells < 3
		|| Config.TerminalSearchWindowCells > 32
		|| Config.MinimumTerminalCandidateCount < 1
		|| Config.MinimumTerminalCandidateCount > 16
		|| Config.MinimumTerminalCandidateCount
			> Config.TerminalSearchWindowCells
		|| Config.TangentFitWindowCells < 2
		|| Config.TangentFitWindowCells > 12
		|| Config.ClearanceRings < 0
		|| Config.ClearanceRings > 4
		|| !FMath::IsFinite(Config.SlotSeparationCM)
		|| Config.SlotSeparationCM < 100.0f
		|| Config.SlotSeparationCM > 600.0f
		|| !FMath::IsFinite(Config.SurfaceOffsetCM)
		|| Config.SurfaceOffsetCM < 0.0f
		|| Config.SurfaceOffsetCM > 40.0f
		|| !FMath::IsFinite(Config.MaxSurfaceSlopeDegrees)
		|| Config.MaxSurfaceSlopeDegrees < 0.0f
		|| Config.MaxSurfaceSlopeDegrees > 60.0f
		|| Config.PlannerVersion != 1
		|| Config.SurfaceResolutionVersion != 1)
	{
		OutFailure = TEXT("ConfigRangeOrVersion");
		return false;
	}
	return true;
}

bool IsTopologyUsable(const TArray<FABTSM2Cell>& Cells)
{
	if (Cells.IsEmpty())
	{
		return false;
	}
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		const FABTSM2Cell& Cell = Cells[CellId];
		if (!IsFiniteFinaleVector(Cell.UnitCenter)
			|| !Cell.UnitCenter.IsNormalized()
			|| Cell.NeighborCellIds.IsEmpty())
		{
			return false;
		}
		for (const int32 NeighborId : Cell.NeighborCellIds)
		{
			if (!Cells.IsValidIndex(NeighborId)
				|| NeighborId == CellId)
			{
				return false;
			}
		}
	}
	return true;
}

bool IsTerminalCellEligible(
	const FABTSM3MonthlySpatialCandidate& Candidate,
	const int32 CellId)
{
	if (!Candidate.Cells.IsValidIndex(CellId)
		|| Candidate.Cells[CellId].CellId != CellId)
	{
		return false;
	}
	const FABTSM3MonthlySpatialCell& Cell = Candidate.Cells[CellId];
	const int32 RouteMask = static_cast<int32>(EABTSM3ActiveRole::Route);
	return (Cell.ActiveRoleMask & RouteMask) != 0
		&& !Cell.bWater
		&& !Cell.bTargetFootprint
		&& !Cell.bNoRoad
		&& !Cell.bAttackCorridor;
}

void BuildClearanceCells(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<int32>& SeedCellIds,
	const int32 ClearanceRings,
	TArray<int32>& OutClearanceCellIds)
{
	TArray<int32> Distances;
	Distances.Init(INDEX_NONE, Cells.Num());
	TArray<int32> Queue;
	Queue.Reserve(SeedCellIds.Num() * 8);
	for (const int32 SeedCellId : SeedCellIds)
	{
		if (Cells.IsValidIndex(SeedCellId)
			&& Distances[SeedCellId] == INDEX_NONE)
		{
			Distances[SeedCellId] = 0;
			Queue.Add(SeedCellId);
		}
	}
	for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
	{
		const int32 CellId = Queue[QueueIndex];
		const int32 Distance = Distances[CellId];
		if (Distance >= ClearanceRings)
		{
			continue;
		}
		for (const int32 NeighborId : Cells[CellId].NeighborCellIds)
		{
			if (Distances[NeighborId] == INDEX_NONE)
			{
				Distances[NeighborId] = Distance + 1;
				Queue.Add(NeighborId);
			}
		}
	}
	OutClearanceCellIds.Reset();
	for (int32 CellId = 0; CellId < Distances.Num(); ++CellId)
	{
		if (Distances[CellId] != INDEX_NONE)
		{
			OutClearanceCellIds.Add(CellId);
		}
	}
}

bool BuildCandidate(
	const FABTSM3MonthlyFinaleAnchorConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlySpatialCandidate& Source,
	FABTSM3MonthlyFinaleAnchorPlanCandidate& OutCandidate,
	FString& OutFailure)
{
	OutCandidate = FABTSM3MonthlyFinaleAnchorPlanCandidate();
	OutCandidate.SourceRouteCandidateId = Source.SourceRouteCandidateId;
	OutCandidate.SourceSpatialCandidateHash = Source.SpatialCandidateHash;
	OutCandidate.SourceRecomputedRouteCandidateHash =
		Source.RecomputedRoute.CandidateHash;
	const FABTSM3MonthlyRouteCandidate& Route = Source.RecomputedRoute;
	if (!Source.bHardPass
		|| Source.RejectReason != EABTSM3MonthlySpatialRejectReason::None
		|| Source.Cells.Num() != Cells.Num()
		|| static_cast<uint64>(Source.SpatialCandidateHash)
			!= FABTSM3MonthlyEncounterBuilder::ComputeCandidateHash(Source)
		|| Route.OrderedRoadCellIds.Num() < 2
		|| Route.OrderedRoadCellIds.Num()
			!= Route.ProgressDistanceCM.Num()
		|| static_cast<uint64>(Route.CandidateHash)
			!= FABTSM3MonthlyRouteBuilder::ComputeCandidateHash(Route))
	{
		OutFailure = TEXT("SourceCandidateIdentity");
		return false;
	}

	OutCandidate.RoadTerminalCellId = Route.OrderedRoadCellIds.Last();
	const int32 FirstRouteIndex = FMath::Max(
		0,
		Route.OrderedRoadCellIds.Num()
			- Config.TerminalSearchWindowCells);
	for (int32 RouteIndex = Route.OrderedRoadCellIds.Num() - 1;
		RouteIndex >= FirstRouteIndex;
		--RouteIndex)
	{
		const int32 CellId = Route.OrderedRoadCellIds[RouteIndex];
		if (IsTerminalCellEligible(Source, CellId))
		{
			OutCandidate.TerminalCandidateCellIds.Add(CellId);
		}
	}
	if (OutCandidate.TerminalCandidateCellIds.Num()
		< Config.MinimumTerminalCandidateCount)
	{
		OutFailure = FString::Printf(
			TEXT("TerminalCandidates:%d/%d"),
			OutCandidate.TerminalCandidateCellIds.Num(),
			Config.MinimumTerminalCandidateCount);
		return false;
	}
	BuildClearanceCells(
		Cells,
		OutCandidate.TerminalCandidateCellIds,
		Config.ClearanceRings,
		OutCandidate.ClearanceCellIds);
	if (OutCandidate.ClearanceCellIds.IsEmpty())
	{
		OutFailure = TEXT("ClearanceEmpty");
		return false;
	}
	OutCandidate.CandidateHash = static_cast<int64>(
		FABTSM3MonthlyFinaleAnchorBuilder::ComputeCandidateHash(
			OutCandidate));
	return true;
}

bool ResolveSurfaceSample(
	const IABTSM3MonthlyFinaleAnchorSurface& Surface,
	const FVector& UnitDirection,
	FABTSM3MonthlyFinaleSurfaceSample& OutSample)
{
	return Surface.QuerySurface(UnitDirection, OutSample)
		&& IsFiniteFinaleVector(OutSample.WorldLocation)
		&& IsFiniteFinaleVector(OutSample.WorldNormal)
		&& OutSample.WorldNormal.Normalize();
}

float SurfaceSlopeDegrees(
	const FVector& PrimaryCenter,
	const FABTSM3MonthlyFinaleSurfaceSample& Sample)
{
	const FVector RadialUp =
		(Sample.WorldLocation - PrimaryCenter).GetSafeNormal();
	if (RadialUp.IsNearlyZero())
	{
		return 180.0f;
	}
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
		FVector::DotProduct(RadialUp, Sample.WorldNormal),
		-1.0f,
		1.0f)));
}

bool BuildFittedForward(
	const FABTSM3MonthlyFinaleAnchorConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlyRouteCandidate& Route,
	const int32 AnchorCellId,
	const FVector& Up,
	FVector& OutForward)
{
	const int32 AnchorRouteIndex =
		Route.OrderedRoadCellIds.IndexOfByKey(AnchorCellId);
	if (AnchorRouteIndex == INDEX_NONE)
	{
		return false;
	}
	const int32 EndRouteIndex = FMath::Min(
		Route.OrderedRoadCellIds.Num() - 1,
		AnchorRouteIndex + 1);
	const int32 StartRouteIndex = FMath::Max(
		0,
		EndRouteIndex - Config.TangentFitWindowCells + 1);
	if (StartRouteIndex >= EndRouteIndex)
	{
		return false;
	}
	const FVector RawForward =
		Cells[Route.OrderedRoadCellIds[EndRouteIndex]].UnitCenter
		- Cells[Route.OrderedRoadCellIds[StartRouteIndex]].UnitCenter;
	OutForward = FVector::VectorPlaneProject(RawForward, Up).GetSafeNormal();
	if (OutForward.IsNearlyZero())
	{
		return false;
	}
	if (AnchorRouteIndex < Route.OrderedRoadCellIds.Num() - 1)
	{
		const FVector ToTerminal = FVector::VectorPlaneProject(
			Cells[Route.OrderedRoadCellIds.Last()].UnitCenter - Up,
			Up).GetSafeNormal();
		if (!ToTerminal.IsNearlyZero()
			&& FVector::DotProduct(OutForward, ToTerminal) < 0.0f)
		{
			OutForward *= -1.0f;
		}
	}
	return true;
}

bool TryResolveCandidatePreview(
	const FABTSM3MonthlyFinaleAnchorConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlySpatialCandidate& Spatial,
	const FABTSM3MonthlyFinaleAnchorPlanCandidate& Plan,
	const FABTSM3MonthlyFinaleAnchorPlanResult& PlanResult,
	const IABTSM3MonthlyFinaleAnchorSurface& Surface,
	FABTSM3MonthlyFinaleAnchorPreview& OutPreview)
{
	const FVector PrimaryCenter = Surface.GetPrimaryCenterWorld();
	const float PrimaryRadiusCM = Surface.GetPrimaryRadiusCM();
	if (!IsFiniteFinaleVector(PrimaryCenter)
		|| !FMath::IsFinite(PrimaryRadiusCM)
		|| PrimaryRadiusCM <= 0.0f
		|| Config.SlotSeparationCM >= PrimaryRadiusCM)
	{
		return false;
	}

	for (const int32 AnchorCellId : Plan.TerminalCandidateCellIds)
	{
		if (!Cells.IsValidIndex(AnchorCellId))
		{
			continue;
		}
		FABTSM3MonthlyFinaleSurfaceSample Anchor;
		if (!ResolveSurfaceSample(
				Surface,
				Cells[AnchorCellId].UnitCenter,
				Anchor)
			|| Anchor.NearestCellId != AnchorCellId)
		{
			continue;
		}
		const float AnchorSlope = SurfaceSlopeDegrees(
			PrimaryCenter,
			Anchor);
		const FVector AnchorUp =
			(Anchor.WorldLocation - PrimaryCenter).GetSafeNormal();
		FVector IntendedForward;
		if (AnchorSlope > Config.MaxSurfaceSlopeDegrees
			|| AnchorUp.IsNearlyZero()
			|| !BuildFittedForward(
				Config,
				Cells,
				Spatial.RecomputedRoute,
				AnchorCellId,
				AnchorUp,
				IntendedForward))
		{
			continue;
		}
		const FVector IntendedRight = FVector::CrossProduct(
			AnchorUp,
			IntendedForward).GetSafeNormal();
		const float AnchorRadiusCM = FVector::Distance(
			Anchor.WorldLocation,
			PrimaryCenter);
		const float HalfSeparationCM = Config.SlotSeparationCM * 0.5f;
		if (IntendedRight.IsNearlyZero()
			|| AnchorRadiusCM <= HalfSeparationCM)
		{
			continue;
		}
		const FVector LeftDirection =
			(AnchorUp * AnchorRadiusCM
				- IntendedRight * HalfSeparationCM).GetSafeNormal();
		const FVector RightDirection =
			(AnchorUp * AnchorRadiusCM
				+ IntendedRight * HalfSeparationCM).GetSafeNormal();
		FABTSM3MonthlyFinaleSurfaceSample Left;
		FABTSM3MonthlyFinaleSurfaceSample Right;
		if (!ResolveSurfaceSample(Surface, LeftDirection, Left)
			|| !ResolveSurfaceSample(Surface, RightDirection, Right))
		{
			continue;
		}
		const float LeftSlope = SurfaceSlopeDegrees(PrimaryCenter, Left);
		const float RightSlope = SurfaceSlopeDegrees(PrimaryCenter, Right);
		const float MaxSlope = FMath::Max3(
			AnchorSlope,
			LeftSlope,
			RightSlope);
		if (MaxSlope > Config.MaxSurfaceSlopeDegrees)
		{
			continue;
		}

		FVector LeftWorld = Left.WorldLocation
			+ Left.WorldNormal * Config.SurfaceOffsetCM;
		FVector RightWorld = Right.WorldLocation
			+ Right.WorldNormal * Config.SurfaceOffsetCM;
		FVector FrameOrigin = (LeftWorld + RightWorld) * 0.5f;
		FVector FrameUp = (FrameOrigin - PrimaryCenter).GetSafeNormal();
		FVector FrameRight = FVector::VectorPlaneProject(
			RightWorld - LeftWorld,
			FrameUp).GetSafeNormal();
		FVector FrameForward = FVector::CrossProduct(
			FrameRight,
			FrameUp).GetSafeNormal();
		if (FrameUp.IsNearlyZero()
			|| FrameRight.IsNearlyZero()
			|| FrameForward.IsNearlyZero())
		{
			continue;
		}
		if (FVector::DotProduct(FrameForward, IntendedForward) < 0.0f)
		{
			Swap(Left, Right);
			Swap(LeftWorld, RightWorld);
			FrameRight *= -1.0f;
			FrameForward *= -1.0f;
		}
		const float ActualSeparationCM = FVector::Distance(
			LeftWorld,
			RightWorld);
		if (!FMath::IsFinite(ActualSeparationCM)
			|| ActualSeparationCM < Config.SlotSeparationCM * 0.6f
			|| ActualSeparationCM > Config.SlotSeparationCM * 1.4f)
		{
			continue;
		}

		OutPreview = FABTSM3MonthlyFinaleAnchorPreview();
		OutPreview.SourceRouteCandidateId =
			Plan.SourceRouteCandidateId;
		OutPreview.SourceSpatialCandidateHash =
			Plan.SourceSpatialCandidateHash;
		OutPreview.SourcePlanCandidateHash = Plan.CandidateHash;
		OutPreview.SourcePlanResultHash = PlanResult.ResultHash;
		OutPreview.RoadTerminalCellId = Plan.RoadTerminalCellId;
		OutPreview.AnchorCellId = AnchorCellId;
		OutPreview.LeftSlotNearestCellId = Left.NearestCellId;
		OutPreview.RightSlotNearestCellId = Right.NearestCellId;
		OutPreview.FrameOriginWorld = FrameOrigin;
		OutPreview.ForwardWorld = FrameForward;
		OutPreview.RightWorld = FrameRight;
		OutPreview.UpWorld = FrameUp;
		OutPreview.AnchorSurfaceWorld = Anchor.WorldLocation;
		OutPreview.LeftSlotSurfaceWorld = Left.WorldLocation;
		OutPreview.RightSlotSurfaceWorld = Right.WorldLocation;
		OutPreview.LeftSlotWorldLocation = LeftWorld;
		OutPreview.RightSlotWorldLocation = RightWorld;
		OutPreview.ActualSlotSeparationCM = ActualSeparationCM;
		OutPreview.MaxResolvedSurfaceSlopeDegrees = MaxSlope;
		OutPreview.bPreviewValid = true;
		OutPreview.bMonthlyWorldAccepted = false;
		OutPreview.PreviewHash = static_cast<int64>(
			FABTSM3MonthlyFinaleAnchorBuilder::ComputePreviewHash(
				OutPreview));
		return true;
	}
	return false;
}
}

bool FABTSM3MonthlyFinaleAnchorBuilder::Build(
	const int32 WorldSeed,
	const FABTSM3MonthlyFinaleAnchorConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlySpatialResult& SpatialResult,
	FABTSM3MonthlyFinaleAnchorPlanResult& OutResult,
	FString& OutFailure)
{
	using namespace ABTSM3R52FinaleAnchorPrivate;
	OutResult = FABTSM3MonthlyFinaleAnchorPlanResult();
	OutFailure.Reset();
	OutResult.SchemaVersion = SchemaVersion;
	OutResult.GeneratorVersion = GeneratorVersion;
	OutResult.LayoutPolicyVersion = MonthlyLayoutPolicyVersion;
	OutResult.WorldSeed = WorldSeed;
	if (!ValidateConfig(Config, OutFailure))
	{
		OutResult.RejectReason =
			EABTSM3MonthlyFinaleAnchorRejectReason::InvalidConfig;
		OutResult.ResultHash = static_cast<int64>(ComputeResultHash(OutResult));
		return false;
	}
	if (!IsTopologyUsable(Cells))
	{
		OutResult.RejectReason =
			EABTSM3MonthlyFinaleAnchorRejectReason::InvalidTopology;
		OutFailure = TEXT("CellTopo");
		OutResult.ResultHash = static_cast<int64>(ComputeResultHash(OutResult));
		return false;
	}
	const uint64 TopologyHash =
		FABTSM3MonthlyRouteBuilder::ComputeTopologyHash(Cells);
	OutResult.TopologyHash = static_cast<int64>(TopologyHash);
	OutResult.SourceSpatialResultHash = SpatialResult.SpatialResultHash;
	OutResult.ConfigHash = static_cast<int64>(
		ComputeConfigHash(Config, TopologyHash));
	if (SpatialResult.WorldSeed != WorldSeed
		|| SpatialResult.TopologyHash != OutResult.TopologyHash
		|| !SpatialResult.bSpatialResultValid
		|| SpatialResult.bMonthlyWorldAccepted
		|| SpatialResult.RejectReason
			!= EABTSM3MonthlySpatialRejectReason::None
		|| SpatialResult.RetainedCandidates.IsEmpty()
		|| static_cast<uint64>(SpatialResult.SpatialResultHash)
			!= FABTSM3MonthlyEncounterBuilder::ComputeResultHash(
				SpatialResult))
	{
		OutResult.RejectReason =
			EABTSM3MonthlyFinaleAnchorRejectReason::InvalidSpatialResult;
		OutFailure = TEXT("SourceSpatialResult");
		OutResult.ResultHash = static_cast<int64>(ComputeResultHash(OutResult));
		return false;
	}
	if (!Config.bBuildFinaleAnchorPlans)
	{
		OutResult.bPlanResultValid = true;
		OutResult.RejectReason =
			EABTSM3MonthlyFinaleAnchorRejectReason::NotEvaluated;
		OutResult.ResultHash = static_cast<int64>(ComputeResultHash(OutResult));
		return true;
	}

	for (const FABTSM3MonthlySpatialCandidate& Source :
		SpatialResult.RetainedCandidates)
	{
		FABTSM3MonthlyFinaleAnchorPlanCandidate Candidate;
		if (!BuildCandidate(
				Config,
				Cells,
				Source,
				Candidate,
				OutFailure))
		{
			OutResult.RetainedCandidates.Reset();
			OutResult.RejectReason =
				EABTSM3MonthlyFinaleAnchorRejectReason::
					TerminalWindowUnavailable;
			OutResult.ResultHash = static_cast<int64>(
				ComputeResultHash(OutResult));
			return false;
		}
		OutResult.RetainedCandidates.Add(MoveTemp(Candidate));
	}
	if (OutResult.RetainedCandidates.Num()
		!= SpatialResult.RetainedCandidates.Num())
	{
		OutResult.RetainedCandidates.Reset();
		OutResult.RejectReason =
			EABTSM3MonthlyFinaleAnchorRejectReason::
				CandidateJoinMismatch;
		OutFailure = TEXT("CandidateCount");
		OutResult.ResultHash = static_cast<int64>(ComputeResultHash(OutResult));
		return false;
	}
	OutResult.bPlanResultValid = true;
	OutResult.bMonthlyWorldAccepted = false;
	OutResult.RejectReason = EABTSM3MonthlyFinaleAnchorRejectReason::None;
	OutResult.ResultHash = static_cast<int64>(ComputeResultHash(OutResult));
	if (Config.bEmitFinaleAnchorLogs)
	{
		LogSummary(OutResult);
	}
	return true;
}

bool FABTSM3MonthlyFinaleAnchorBuilder::Validate(
	const FABTSM3MonthlyFinaleAnchorConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlySpatialResult& SpatialResult,
	const FABTSM3MonthlyFinaleAnchorPlanResult& Result,
	EABTSM3MonthlyFinaleAnchorRejectReason& OutReason,
	FString& OutFailure)
{
	OutReason = EABTSM3MonthlyFinaleAnchorRejectReason::None;
	OutFailure.Reset();
	FABTSM3MonthlyFinaleAnchorConfig QuietConfig = Config;
	QuietConfig.bEmitFinaleAnchorLogs = false;
	FABTSM3MonthlyFinaleAnchorPlanResult Expected;
	FString ExpectedFailure;
	if (!Build(
			SpatialResult.WorldSeed,
			QuietConfig,
			Cells,
			SpatialResult,
			Expected,
			ExpectedFailure))
	{
		OutReason = Expected.RejectReason;
		OutFailure = FString::Printf(TEXT("Rebuild:%s"), *ExpectedFailure);
		return false;
	}
	if (!FABTSM3MonthlyFinaleAnchorPlanResult::StaticStruct()
			->CompareScriptStruct(&Expected, &Result, PPF_None))
	{
		OutReason = EABTSM3MonthlyFinaleAnchorRejectReason::HashMismatch;
		OutFailure = TEXT("WholeStruct");
		return false;
	}
	return true;
}

bool FABTSM3MonthlyFinaleAnchorBuilder::BuildPreview(
	const int32 SourceRouteCandidateId,
	const FABTSM3MonthlyFinaleAnchorConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlySpatialResult& SpatialResult,
	const FABTSM3MonthlyFinaleAnchorPlanResult& PlanResult,
	const IABTSM3MonthlyFinaleAnchorSurface& Surface,
	FABTSM3MonthlyFinaleAnchorPreview& OutPreview,
	FString& OutFailure)
{
	using namespace ABTSM3R52FinaleAnchorPrivate;
	OutPreview = FABTSM3MonthlyFinaleAnchorPreview();
	OutFailure.Reset();
	if (SourceRouteCandidateId == INDEX_NONE)
	{
		OutFailure = TEXT("ExplicitCandidateRequired");
		return false;
	}
	EABTSM3MonthlyFinaleAnchorRejectReason PlanReason =
		EABTSM3MonthlyFinaleAnchorRejectReason::None;
	if (!Validate(
			Config,
			Cells,
			SpatialResult,
			PlanResult,
			PlanReason,
			OutFailure)
		|| !PlanResult.bPlanResultValid
		|| PlanResult.bMonthlyWorldAccepted
		|| PlanResult.RejectReason
			!= EABTSM3MonthlyFinaleAnchorRejectReason::None)
	{
		OutFailure = FString::Printf(
			TEXT("Plan:%s:%s"),
			GetRejectReasonName(PlanReason),
			*OutFailure);
		return false;
	}
	const FABTSM3MonthlyFinaleAnchorPlanCandidate* Plan =
		FindCandidate(PlanResult, SourceRouteCandidateId);
	const FABTSM3MonthlySpatialCandidate* Spatial =
		SpatialResult.RetainedCandidates.FindByPredicate(
			[SourceRouteCandidateId](
				const FABTSM3MonthlySpatialCandidate& Candidate)
			{
				return Candidate.SourceRouteCandidateId
					== SourceRouteCandidateId;
			});
	if (Plan == nullptr
		|| Spatial == nullptr
		|| Plan->SourceSpatialCandidateHash
			!= Spatial->SpatialCandidateHash
		|| Plan->SourceRecomputedRouteCandidateHash
			!= Spatial->RecomputedRoute.CandidateHash
		|| static_cast<uint64>(Plan->CandidateHash)
			!= ComputeCandidateHash(*Plan))
	{
		OutFailure = TEXT("CandidateJoinMismatch");
		return false;
	}
	if (!TryResolveCandidatePreview(
			Config,
			Cells,
			*Spatial,
			*Plan,
			PlanResult,
			Surface,
			OutPreview))
	{
		OutFailure = TEXT("NoSurfaceCandidate");
		return false;
	}
	return true;
}

const FABTSM3MonthlyFinaleAnchorPlanCandidate*
FABTSM3MonthlyFinaleAnchorBuilder::FindCandidate(
	const FABTSM3MonthlyFinaleAnchorPlanResult& Result,
	const int32 SourceRouteCandidateId)
{
	return Result.RetainedCandidates.FindByPredicate(
		[SourceRouteCandidateId](
			const FABTSM3MonthlyFinaleAnchorPlanCandidate& Candidate)
		{
			return Candidate.SourceRouteCandidateId
				== SourceRouteCandidateId;
		});
}

uint64 FABTSM3MonthlyFinaleAnchorBuilder::ComputeConfigHash(
	const FABTSM3MonthlyFinaleAnchorConfig& Config,
	const uint64 TopologyHash)
{
	using namespace ABTSM3R52FinaleAnchorPrivate;
	FCanonicalHash64 Hash;
	Hash.AddInt32(SchemaVersion);
	Hash.AddInt32(GeneratorVersion);
	Hash.AddInt32(MonthlyLayoutPolicyVersion);
	Hash.AddUInt64(TopologyHash);
	Hash.AddBool(Config.bBuildFinaleAnchorPlans);
	Hash.AddInt32(Config.TerminalSearchWindowCells);
	Hash.AddInt32(Config.MinimumTerminalCandidateCount);
	Hash.AddInt32(Config.TangentFitWindowCells);
	Hash.AddInt32(Config.ClearanceRings);
	Hash.AddFloat(Config.SlotSeparationCM);
	Hash.AddFloat(Config.SurfaceOffsetCM);
	Hash.AddFloat(Config.MaxSurfaceSlopeDegrees);
	Hash.AddInt32(Config.PlannerVersion);
	Hash.AddInt32(Config.SurfaceResolutionVersion);
	return Hash.Get();
}

uint64 FABTSM3MonthlyFinaleAnchorBuilder::ComputeCandidateHash(
	const FABTSM3MonthlyFinaleAnchorPlanCandidate& Candidate)
{
	using namespace ABTSM3R52FinaleAnchorPrivate;
	FCanonicalHash64 Hash;
	Hash.AddInt32(Candidate.SourceRouteCandidateId);
	Hash.AddInt64(Candidate.SourceSpatialCandidateHash);
	Hash.AddInt64(Candidate.SourceRecomputedRouteCandidateHash);
	Hash.AddInt32(Candidate.RoadTerminalCellId);
	Hash.AddIntArray(Candidate.TerminalCandidateCellIds);
	Hash.AddIntArray(Candidate.ClearanceCellIds);
	return Hash.Get();
}

uint64 FABTSM3MonthlyFinaleAnchorBuilder::ComputeResultHash(
	const FABTSM3MonthlyFinaleAnchorPlanResult& Result)
{
	using namespace ABTSM3R52FinaleAnchorPrivate;
	FCanonicalHash64 Hash;
	Hash.AddInt32(Result.SchemaVersion);
	Hash.AddInt32(Result.GeneratorVersion);
	Hash.AddInt32(Result.LayoutPolicyVersion);
	Hash.AddInt32(Result.WorldSeed);
	Hash.AddInt64(Result.TopologyHash);
	Hash.AddInt64(Result.SourceSpatialResultHash);
	Hash.AddInt64(Result.ConfigHash);
	Hash.AddBool(Result.bPlanResultValid);
	Hash.AddBool(Result.bMonthlyWorldAccepted);
	Hash.AddInt32(static_cast<int32>(Result.RejectReason));
	Hash.AddInt32(Result.RetainedCandidates.Num());
	for (const FABTSM3MonthlyFinaleAnchorPlanCandidate& Candidate :
		Result.RetainedCandidates)
	{
		Hash.AddInt64(Candidate.CandidateHash);
	}
	return Hash.Get();
}

uint64 FABTSM3MonthlyFinaleAnchorBuilder::ComputePreviewHash(
	const FABTSM3MonthlyFinaleAnchorPreview& Preview)
{
	using namespace ABTSM3R52FinaleAnchorPrivate;
	FCanonicalHash64 Hash;
	Hash.AddInt32(Preview.SourceRouteCandidateId);
	Hash.AddInt64(Preview.SourceSpatialCandidateHash);
	Hash.AddInt64(Preview.SourcePlanCandidateHash);
	Hash.AddInt64(Preview.SourcePlanResultHash);
	Hash.AddInt32(Preview.RoadTerminalCellId);
	Hash.AddInt32(Preview.AnchorCellId);
	Hash.AddInt32(Preview.LeftSlotNearestCellId);
	Hash.AddInt32(Preview.RightSlotNearestCellId);
	Hash.AddVector(Preview.FrameOriginWorld);
	Hash.AddVector(Preview.ForwardWorld);
	Hash.AddVector(Preview.RightWorld);
	Hash.AddVector(Preview.UpWorld);
	Hash.AddVector(Preview.AnchorSurfaceWorld);
	Hash.AddVector(Preview.LeftSlotSurfaceWorld);
	Hash.AddVector(Preview.RightSlotSurfaceWorld);
	Hash.AddVector(Preview.LeftSlotWorldLocation);
	Hash.AddVector(Preview.RightSlotWorldLocation);
	Hash.AddFloat(Preview.ActualSlotSeparationCM);
	Hash.AddFloat(Preview.MaxResolvedSurfaceSlopeDegrees);
	Hash.AddBool(Preview.bPreviewValid);
	Hash.AddBool(Preview.bMonthlyWorldAccepted);
	return Hash.Get();
}

void FABTSM3MonthlyFinaleAnchorBuilder::LogSummary(
	const FABTSM3MonthlyFinaleAnchorPlanResult& Result)
{
	int32 TotalTerminalCandidates = 0;
	int32 TotalClearanceCells = 0;
	for (const FABTSM3MonthlyFinaleAnchorPlanCandidate& Candidate :
		Result.RetainedCandidates)
	{
		TotalTerminalCandidates += Candidate.TerminalCandidateCellIds.Num();
		TotalClearanceCells += Candidate.ClearanceCellIds.Num();
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][PCG][FinaleAnchorPlan] Stage=M3R5.2 Seed=%d Valid=%d MonthlyAccepted=0 Candidates=%d TerminalCandidates=%d ClearanceCells=%d SourceSpatial=%016llX Config=%016llX Result=%016llX"),
		Result.WorldSeed,
		Result.bPlanResultValid ? 1 : 0,
		Result.RetainedCandidates.Num(),
		TotalTerminalCandidates,
		TotalClearanceCells,
		static_cast<unsigned long long>(
			static_cast<uint64>(Result.SourceSpatialResultHash)),
		static_cast<unsigned long long>(
			static_cast<uint64>(Result.ConfigHash)),
		static_cast<unsigned long long>(
			static_cast<uint64>(Result.ResultHash)));
}

const TCHAR* FABTSM3MonthlyFinaleAnchorBuilder::GetRejectReasonName(
	const EABTSM3MonthlyFinaleAnchorRejectReason Reason)
{
	switch (Reason)
	{
	case EABTSM3MonthlyFinaleAnchorRejectReason::None:
		return TEXT("None");
	case EABTSM3MonthlyFinaleAnchorRejectReason::NotEvaluated:
		return TEXT("NotEvaluated");
	case EABTSM3MonthlyFinaleAnchorRejectReason::InvalidConfig:
		return TEXT("InvalidConfig");
	case EABTSM3MonthlyFinaleAnchorRejectReason::InvalidTopology:
		return TEXT("InvalidTopology");
	case EABTSM3MonthlyFinaleAnchorRejectReason::InvalidSpatialResult:
		return TEXT("InvalidSpatialResult");
	case EABTSM3MonthlyFinaleAnchorRejectReason::TerminalWindowUnavailable:
		return TEXT("TerminalWindowUnavailable");
	case EABTSM3MonthlyFinaleAnchorRejectReason::CandidateJoinMismatch:
		return TEXT("CandidateJoinMismatch");
	case EABTSM3MonthlyFinaleAnchorRejectReason::SurfaceResolutionFailed:
		return TEXT("SurfaceResolutionFailed");
	case EABTSM3MonthlyFinaleAnchorRejectReason::HashMismatch:
		return TEXT("HashMismatch");
	default:
		return TEXT("Unknown");
	}
}
