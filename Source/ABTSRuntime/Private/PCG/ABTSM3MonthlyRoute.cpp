// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3MonthlyRoute.h"

#include "ABTSRuntime.h"
#include "Algo/BinarySearch.h"
#include "Algo/Reverse.h"
#include "Algo/Sort.h"
#include "Planet/ABTSM2Planet.h"

#include <limits>

namespace ABTSM3R2RoutePrivate
{
constexpr uint64 FnvOffset64 = 14695981039346656037ull;
constexpr uint64 FnvPrime64 = 1099511628211ull;
constexpr int32 StateStride = 7;
constexpr int32 FallbackCandidateBase = 1000000;
constexpr int32 RouteBeatIdBase = 2000000;
constexpr int32 QuantizedUnitScale = 1000000;
constexpr int32 QuantizedAngleScale = 100;

class FCanonicalHash64
{
public:
	void AddByte(const uint8 Value)
	{
		ValueHash ^= Value;
		ValueHash *= FnvPrime64;
	}

	void AddBool(const bool Value)
	{
		AddByte(Value ? 1 : 0);
	}

	void AddInt32(const int32 Value)
	{
		AddUInt32(static_cast<uint32>(Value));
	}

	void AddUInt32(const uint32 Value)
	{
		for (int32 Shift = 0; Shift < 32; Shift += 8)
		{
			AddByte(static_cast<uint8>((Value >> Shift) & 0xffu));
		}
	}

	void AddInt64(const int64 Value)
	{
		AddUInt64(static_cast<uint64>(Value));
	}

	void AddUInt64(const uint64 Value)
	{
		for (int32 Shift = 0; Shift < 64; Shift += 8)
		{
			AddByte(static_cast<uint8>((Value >> Shift) & 0xffull));
		}
	}

	void AddIntArray(const TArray<int32>& Values)
	{
		AddInt32(Values.Num());
		for (const int32 Value : Values)
		{
			AddInt32(Value);
		}
	}

	uint64 Get() const
	{
		return ValueHash;
	}

private:
	uint64 ValueHash = FnvOffset64;
};

uint64 Mix64(uint64 Value)
{
	Value += 0x9e3779b97f4a7c15ull;
	Value = (Value ^ (Value >> 30)) * 0xbf58476d1ce4e5b9ull;
	Value = (Value ^ (Value >> 27)) * 0x94d049bb133111ebull;
	return Value ^ (Value >> 31);
}

uint64 MakeCandidateSeed(
	const int32 WorldSeed,
	const int32 CandidateId,
	const bool bFallback)
{
	uint64 Value = Mix64(static_cast<uint32>(WorldSeed));
	Value = Mix64(Value ^ static_cast<uint32>(CandidateId));
	Value = Mix64(Value ^ (bFallback
		? 0x4d33523246414c4cull
		: 0x4d3352324e4f524dull));
	Value = Mix64(Value ^ 3ull);
	Value = Mix64(Value ^ 2ull);
	return Value;
}

double UnitRandom01(uint64& State)
{
	State = Mix64(State);
	return static_cast<double>(State >> 11)
		* (1.0 / 9007199254740992.0);
}

double UnitRandomSigned(uint64& State)
{
	return UnitRandom01(State) * 2.0 - 1.0;
}

bool IsFiniteVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X)
		&& FMath::IsFinite(Value.Y)
		&& FMath::IsFinite(Value.Z);
}

int32 QuantizeUnit(const double Value)
{
	return FMath::RoundToInt(Value * QuantizedUnitScale);
}

int32 QuantizeDot(const double Value)
{
	return FMath::RoundToInt(
		FMath::Clamp(Value, -1.0, 1.0) * QuantizedUnitScale);
}

int32 QuantizeAngleCentidegrees(const double Radians)
{
	return FMath::RoundToInt(
		FMath::RadiansToDegrees(Radians) * QuantizedAngleScale);
}

FVector CanonicalUnit(const FVector& Value)
{
	return FVector(
		static_cast<double>(QuantizeUnit(Value.X))
			/ QuantizedUnitScale,
		static_cast<double>(QuantizeUnit(Value.Y))
			/ QuantizedUnitScale,
		static_cast<double>(QuantizeUnit(Value.Z))
			/ QuantizedUnitScale)
		.GetSafeNormal();
}

int32 CellArcLengthCM(
	const TArray<FABTSM2Cell>& Cells,
	const int32 CellA,
	const int32 CellB,
	const float PlanetRadiusCM)
{
	if (!Cells.IsValidIndex(CellA) || !Cells.IsValidIndex(CellB))
	{
		return 0;
	}
	const double Dot = FMath::Clamp(
		static_cast<double>(FVector::DotProduct(
			CanonicalUnit(Cells[CellA].UnitCenter),
			CanonicalUnit(Cells[CellB].UnitCenter))),
		-1.0,
		1.0);
	return FMath::Max(
		1,
		FMath::RoundToInt(
			static_cast<double>(FMath::RoundToInt(PlanetRadiusCM))
				* FMath::Acos(Dot)));
}

int32 FindNearestCell(
	const TArray<FABTSM2Cell>& Cells,
	const FVector& UnitDirection)
{
	int32 BestCellId = INDEX_NONE;
	int32 BestDotQ = MIN_int32;
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		const int32 DotQ = QuantizeDot(FVector::DotProduct(
			CanonicalUnit(Cells[CellId].UnitCenter),
			UnitDirection));
		if (DotQ > BestDotQ
			|| (DotQ == BestDotQ
				&& (BestCellId == INDEX_NONE || CellId < BestCellId)))
		{
			BestDotQ = DotQ;
			BestCellId = CellId;
		}
	}
	return BestCellId;
}

bool FindUnweightedPath(
	const TArray<FABTSM2Cell>& Cells,
	const int32 StartCellId,
	const int32 GoalCellId,
	TArray<int32>& OutPath)
{
	OutPath.Reset();
	if (!Cells.IsValidIndex(StartCellId)
		|| !Cells.IsValidIndex(GoalCellId))
	{
		return false;
	}
	TArray<int32> Parent;
	Parent.Init(INDEX_NONE, Cells.Num());
	TArray<int32> Queue;
	Queue.Reserve(Cells.Num());
	Queue.Add(StartCellId);
	Parent[StartCellId] = StartCellId;
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 CellId = Queue[Head];
		if (CellId == GoalCellId)
		{
			break;
		}
		for (const int32 NeighborId : Cells[CellId].NeighborCellIds)
		{
			if (!Cells.IsValidIndex(NeighborId)
				|| Parent[NeighborId] != INDEX_NONE)
			{
				continue;
			}
			Parent[NeighborId] = CellId;
			Queue.Add(NeighborId);
		}
	}
	if (Parent[GoalCellId] == INDEX_NONE)
	{
		return false;
	}
	for (int32 CellId = GoalCellId;
		CellId != StartCellId;
		CellId = Parent[CellId])
	{
		OutPath.Add(CellId);
	}
	OutPath.Add(StartCellId);
	Algo::Reverse(OutPath);
	return true;
}

void BuildCorridorRings(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<int32>& Centerline,
	const int32 MaxRadius,
	TArray<int32>& OutRings)
{
	OutRings.Init(INDEX_NONE, Cells.Num());
	TArray<int32> Queue;
	Queue.Reserve(FMath::Min(Cells.Num(), Centerline.Num() * 64));
	for (const int32 CellId : Centerline)
	{
		if (!Cells.IsValidIndex(CellId) || OutRings[CellId] == 0)
		{
			continue;
		}
		OutRings[CellId] = 0;
		Queue.Add(CellId);
	}
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 CellId = Queue[Head];
		const int32 Ring = OutRings[CellId];
		if (Ring >= MaxRadius)
		{
			continue;
		}
		for (const int32 NeighborId : Cells[CellId].NeighborCellIds)
		{
			if (!Cells.IsValidIndex(NeighborId)
				|| OutRings[NeighborId] != INDEX_NONE)
			{
				continue;
			}
			OutRings[NeighborId] = Ring + 1;
			Queue.Add(NeighborId);
		}
	}
}

const FABTSM3MonthlyRouteCellContext& ResolveCellContext(
	const FABTSM3MonthlyRoadContext& Context,
	const int32 CellId)
{
	static const FABTSM3MonthlyRouteCellContext Neutral;
	return Context.Cells.IsValidIndex(CellId)
		? Context.Cells[CellId]
		: Neutral;
}

bool IsCellTraversable(
	const FABTSM3MonthlyRoadContext& Context,
	const int32 CellId)
{
	const FABTSM3MonthlyRouteCellContext& Cell =
		ResolveCellContext(Context, CellId);
	return !Cell.bHardBlocked
		&& (!Cell.bWater || Cell.bLegalWaterCrossing);
}

int32 ResolveRawReuseBonus(
	const FABTSM3MonthlyRouteCellContext& Cell,
	const FABTSM3MonthlyRouteConfig& Config)
{
	return static_cast<int32>(FMath::Clamp<int64>(
		-static_cast<int64>(Cell.ReuseBias),
		0,
		Config.Costs.MaxReuseBonusPerStep));
}

int32 FindIncomingSlot(
	const TArray<FABTSM2Cell>& Cells,
	const int32 CellId,
	const int32 PreviousCellId)
{
	if (PreviousCellId == INDEX_NONE)
	{
		return 0;
	}
	if (!Cells.IsValidIndex(CellId))
	{
		return INDEX_NONE;
	}
	const int32 NeighborIndex =
		Cells[CellId].NeighborCellIds.IndexOfByKey(PreviousCellId);
	return NeighborIndex == INDEX_NONE ? INDEX_NONE : NeighborIndex + 1;
}

int32 ResolvePreviousCell(
	const TArray<FABTSM2Cell>& Cells,
	const int32 CellId,
	const int32 IncomingSlot)
{
	if (IncomingSlot <= 0 || !Cells.IsValidIndex(CellId))
	{
		return INDEX_NONE;
	}
	const int32 NeighborIndex = IncomingSlot - 1;
	return Cells[CellId].NeighborCellIds.IsValidIndex(NeighborIndex)
		? Cells[CellId].NeighborCellIds[NeighborIndex]
		: INDEX_NONE;
}

int32 ComputeTurnCentidegrees(
	const TArray<FABTSM2Cell>& Cells,
	const int32 PreviousCellId,
	const int32 CurrentCellId,
	const int32 NextCellId)
{
	if (!Cells.IsValidIndex(PreviousCellId)
		|| !Cells.IsValidIndex(CurrentCellId)
		|| !Cells.IsValidIndex(NextCellId))
	{
		return 0;
	}
	const FVector Previous =
		CanonicalUnit(Cells[PreviousCellId].UnitCenter);
	const FVector Current =
		CanonicalUnit(Cells[CurrentCellId].UnitCenter);
	const FVector Next =
		CanonicalUnit(Cells[NextCellId].UnitCenter);
	const FVector Up = Current;
	const FVector Incoming = FVector::VectorPlaneProject(
		Current - Previous,
		Up).GetSafeNormal();
	const FVector Outgoing = FVector::VectorPlaneProject(
		Next - Current,
		Up).GetSafeNormal();
	if (Incoming.IsNearlyZero() || Outgoing.IsNearlyZero())
	{
		return 0;
	}
	const double Dot = FMath::Clamp(
		static_cast<double>(FVector::DotProduct(Incoming, Outgoing)),
		-1.0,
		1.0);
	return QuantizeAngleCentidegrees(FMath::Acos(Dot));
}

struct FOpenNode
{
	int64 Cost = 0;
	int32 CellId = INDEX_NONE;
	int32 IncomingSlot = 0;
	int32 StateIndex = INDEX_NONE;

	bool operator<(const FOpenNode& Other) const
	{
		if (Cost != Other.Cost)
		{
			return Cost < Other.Cost;
		}
		if (CellId != Other.CellId)
		{
			return CellId < Other.CellId;
		}
		return IncomingSlot < Other.IncomingSlot;
	}
};

struct FSegmentSolveResult
{
	TArray<int32> Path;
	int64 Cost = 0;
	int32 ExpandedStates = 0;
	int32 Relaxations = 0;
	int32 ShoulderEdges = 0;
	int32 UTurns = 0;
	int32 AppliedReuseBonus = 0;
	EABTSM3MonthlyRouteRejectReason RejectReason =
		EABTSM3MonthlyRouteRejectReason::None;
};

bool SolveSegment(
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlyRouteConfig& Config,
	const FABTSM3MonthlyRoadContext& Context,
	const TArray<int32>& CorridorRings,
	const float PlanetRadiusCM,
	const int32 StartCellId,
	const int32 GoalCellId,
	const int32 PreviousCellId,
	const int32 RemainingExpansionBudget,
	const int32 RemainingRelaxationBudget,
	const int32 RemainingReuseBonusBudget,
	FSegmentSolveResult& OutResult)
{
	OutResult = FSegmentSolveResult();
	if (!Cells.IsValidIndex(StartCellId)
		|| !Cells.IsValidIndex(GoalCellId)
		|| !CorridorRings.IsValidIndex(StartCellId)
		|| !CorridorRings.IsValidIndex(GoalCellId)
		|| CorridorRings[StartCellId] == INDEX_NONE
		|| CorridorRings[GoalCellId] == INDEX_NONE)
	{
		OutResult.RejectReason =
			EABTSM3MonthlyRouteRejectReason::CorridorDisconnected;
		return false;
	}
	if (!IsCellTraversable(Context, StartCellId)
		|| !IsCellTraversable(Context, GoalCellId))
	{
		OutResult.RejectReason =
			EABTSM3MonthlyRouteRejectReason::HardBlocked;
		return false;
	}
	TArray<int32> EffectiveReuseBonuses;
	EffectiveReuseBonuses.Init(0, Cells.Num());
	int64 TotalAvailableReuseBonus = 0;
	if (RemainingReuseBonusBudget > 0
		&& !Context.Cells.IsEmpty())
	{
		for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
		{
			if (CellId == StartCellId
				|| !CorridorRings.IsValidIndex(CellId)
				|| CorridorRings[CellId] == INDEX_NONE
				|| CorridorRings[CellId]
					> Config.CorridorAllowedRadiusCells
				|| !IsCellTraversable(Context, CellId))
			{
				continue;
			}
			TotalAvailableReuseBonus += ResolveRawReuseBonus(
				ResolveCellContext(Context, CellId),
				Config);
		}
		for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
		{
			const int32 RawBonus = ResolveRawReuseBonus(
				ResolveCellContext(Context, CellId),
				Config);
			if (RawBonus <= 0
				|| TotalAvailableReuseBonus <= 0)
			{
				continue;
			}
			EffectiveReuseBonuses[CellId] =
				TotalAvailableReuseBonus
					<= RemainingReuseBonusBudget
				? RawBonus
				: static_cast<int32>(
					static_cast<int64>(RawBonus)
						* RemainingReuseBonusBudget
						/ TotalAvailableReuseBonus);
		}
	}
	const int32 StartSlot =
		FindIncomingSlot(Cells, StartCellId, PreviousCellId);
	if (StartSlot == INDEX_NONE)
	{
		OutResult.RejectReason =
			EABTSM3MonthlyRouteRejectReason::NonAdjacentPath;
		return false;
	}
	const int32 StateCount = Cells.Num() * StateStride;
	TArray<int64> Distances;
	Distances.Init(std::numeric_limits<int64>::max(), StateCount);
	TArray<int32> Parents;
	Parents.Init(INDEX_NONE, StateCount);
	TArray<int32> AppliedReuseTotals;
	AppliedReuseTotals.Init(MAX_int32, StateCount);
	TArray<FOpenNode> Open;
	Open.Reserve(FMath::Min(StateCount, 4096));
	const int32 StartState = StartCellId * StateStride + StartSlot;
	Distances[StartState] = 0;
	AppliedReuseTotals[StartState] = 0;
	Open.HeapPush({0, StartCellId, StartSlot, StartState});

	int32 GoalState = INDEX_NONE;
	int64 GoalCost = std::numeric_limits<int64>::max();
	while (!Open.IsEmpty())
	{
		FOpenNode Node;
		Open.HeapPop(Node, EAllowShrinking::No);
		if (Node.Cost != Distances[Node.StateIndex])
		{
			continue;
		}
		if (GoalState != INDEX_NONE && Node.Cost > GoalCost)
		{
			break;
		}
		if (OutResult.ExpandedStates >= RemainingExpansionBudget)
		{
			OutResult.RejectReason =
				EABTSM3MonthlyRouteRejectReason::SearchBudgetExceeded;
			return false;
		}
		++OutResult.ExpandedStates;
		if (Node.CellId == GoalCellId)
		{
			if (GoalState == INDEX_NONE
				|| Node.Cost < GoalCost
				|| (Node.Cost == GoalCost
					&& (AppliedReuseTotals[Node.StateIndex]
							< AppliedReuseTotals[GoalState]
						|| (AppliedReuseTotals[Node.StateIndex]
								== AppliedReuseTotals[GoalState]
							&& Node.StateIndex < GoalState))))
			{
				GoalState = Node.StateIndex;
				GoalCost = Node.Cost;
			}
			continue;
		}
		const int32 IncomingCellId = ResolvePreviousCell(
			Cells,
			Node.CellId,
			Node.IncomingSlot);
		for (const int32 NeighborId :
			Cells[Node.CellId].NeighborCellIds)
		{
			if (OutResult.Relaxations
				>= RemainingRelaxationBudget)
			{
				OutResult.RejectReason =
					EABTSM3MonthlyRouteRejectReason::
						SearchBudgetExceeded;
				return false;
			}
			++OutResult.Relaxations;
			if (!CorridorRings.IsValidIndex(NeighborId)
				|| CorridorRings[NeighborId] == INDEX_NONE
				|| CorridorRings[NeighborId]
					> Config.CorridorAllowedRadiusCells)
			{
				continue;
			}
			const FABTSM3MonthlyRouteCellContext& CellContext =
				ResolveCellContext(Context, NeighborId);
			if (!IsCellTraversable(Context, NeighborId))
			{
				continue;
			}
			const int32 NextIncomingSlot =
				FindIncomingSlot(Cells, NeighborId, Node.CellId);
			if (NextIncomingSlot == INDEX_NONE
				|| NextIncomingSlot >= StateStride)
			{
				continue;
			}
			const int32 TurnCentidegrees = ComputeTurnCentidegrees(
				Cells,
				IncomingCellId,
				Node.CellId,
				NeighborId);
			if (TurnCentidegrees
				>= Config.Costs.UTurnRejectDegrees
					* QuantizedAngleScale)
			{
				continue;
			}
			int64 StepCost = CellArcLengthCM(
				Cells,
				Node.CellId,
				NeighborId,
				PlanetRadiusCM);
			const int32 Ring = CorridorRings[NeighborId];
			if (Ring > Config.CorridorCoreRadiusCells)
			{
				const int32 ShoulderOffset =
					Ring - Config.CorridorCoreRadiusCells;
				StepCost += ShoulderOffset == 1
					? Config.Costs.CorridorRing3Penalty
					: Config.Costs.CorridorRing4Penalty
						* (ShoulderOffset - 1);
			}
			const int32 SharpThreshold =
				Config.Costs.SharpTurnStartDegrees
				* QuantizedAngleScale;
			if (TurnCentidegrees > SharpThreshold)
			{
				StepCost += static_cast<int64>(
					TurnCentidegrees - SharpThreshold)
					* Config.Costs.SharpTurnCostPerDegree
					/ QuantizedAngleScale;
			}
			const int32 UTurnThreshold =
				Config.Costs.UTurnPenaltyStartDegrees
				* QuantizedAngleScale;
			if (TurnCentidegrees > UTurnThreshold)
			{
				StepCost += static_cast<int64>(
					TurnCentidegrees - UTurnThreshold)
					* Config.Costs.UTurnCostPerDegree
					/ QuantizedAngleScale;
			}
			StepCost += CellContext.TerrainCost;
			StepCost += CellContext.SlopeCost;
			if (CellContext.bWater)
			{
				StepCost += Config.Costs.LegalWaterCrossingCost;
			}
			if (CellContext.bSoftEncounterReserved)
			{
				StepCost +=
					Config.Costs.SoftEncounterReservationCost;
			}
			const int32 AppliedReuseBonus = static_cast<int32>(
				FMath::Min<int64>(
					EffectiveReuseBonuses[NeighborId],
					FMath::Max<int64>(0, StepCost - 1)));
			StepCost -= AppliedReuseBonus;

			const int32 NextState =
				NeighborId * StateStride + NextIncomingSlot;
			const int64 NewCost = Node.Cost + StepCost;
			const int32 NewAppliedReuseTotal =
				AppliedReuseTotals[Node.StateIndex]
				+ AppliedReuseBonus;
			const int32 ExistingParent = Parents[NextState];
			if (NewCost > Distances[NextState]
				|| (NewCost == Distances[NextState]
					&& (NewAppliedReuseTotal
							> AppliedReuseTotals[NextState]
						|| (NewAppliedReuseTotal
								== AppliedReuseTotals[NextState]
							&& ExistingParent != INDEX_NONE
							&& Node.StateIndex >= ExistingParent))))
			{
				continue;
			}
			Distances[NextState] = NewCost;
			Parents[NextState] = Node.StateIndex;
			AppliedReuseTotals[NextState] =
				NewAppliedReuseTotal;
			Open.HeapPush({
				NewCost,
				NeighborId,
				NextIncomingSlot,
				NextState});
		}
	}
	if (GoalState == INDEX_NONE)
	{
		OutResult.RejectReason =
			EABTSM3MonthlyRouteRejectReason::CorridorDisconnected;
		return false;
	}

	TArray<int32> ReverseStates;
	for (int32 State = GoalState;
		State != INDEX_NONE;
		State = Parents[State])
	{
		ReverseStates.Add(State);
		if (State == StartState)
		{
			break;
		}
	}
	if (ReverseStates.IsEmpty() || ReverseStates.Last() != StartState)
	{
		OutResult.RejectReason =
			EABTSM3MonthlyRouteRejectReason::CorridorDisconnected;
		return false;
	}
	Algo::Reverse(ReverseStates);
	for (const int32 State : ReverseStates)
	{
		OutResult.Path.Add(State / StateStride);
	}
	OutResult.Cost = Distances[GoalState];
	OutResult.AppliedReuseBonus =
		AppliedReuseTotals[GoalState];
	for (int32 Index = 1; Index < OutResult.Path.Num(); ++Index)
	{
		const int32 CellId = OutResult.Path[Index];
		if (CorridorRings[CellId] > Config.CorridorCoreRadiusCells)
		{
			++OutResult.ShoulderEdges;
		}
		if (Index + 1 < OutResult.Path.Num()
			&& ComputeTurnCentidegrees(
				Cells,
				OutResult.Path[Index - 1],
				CellId,
				OutResult.Path[Index + 1])
				>= Config.Costs.UTurnPenaltyStartDegrees
					* QuantizedAngleScale)
		{
			++OutResult.UTurns;
		}
	}
	return true;
}

FVector SlerpUnit(
	const FVector& Start,
	const FVector& End,
	const double Alpha)
{
	const double Dot = FMath::Clamp(
		static_cast<double>(FVector::DotProduct(Start, End)),
		-1.0,
		1.0);
	const double Angle = FMath::Acos(Dot);
	if (Angle < 1.0e-8)
	{
		return FMath::Lerp(Start, End, Alpha).GetSafeNormal();
	}
	const double SinAngle = FMath::Sin(Angle);
	if (FMath::Abs(SinAngle) < 1.0e-8)
	{
		return FMath::Lerp(Start, End, Alpha).GetSafeNormal();
	}
	return (Start * (FMath::Sin((1.0 - Alpha) * Angle) / SinAngle)
		+ End * (FMath::Sin(Alpha * Angle) / SinAngle))
		.GetSafeNormal();
}

FVector SampleRouteDirection(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<int32>& Route,
	const TArray<int32>& Progress,
	const int32 DistanceCM)
{
	if (Route.IsEmpty() || Progress.Num() != Route.Num())
	{
		return FVector::ZeroVector;
	}
	const int32 ClampedDistance = FMath::Clamp(
		DistanceCM,
		0,
		Progress.Last());
	int32 UpperIndex = Algo::LowerBound(Progress, ClampedDistance);
	if (UpperIndex <= 0)
	{
		return CanonicalUnit(Cells[Route[0]].UnitCenter);
	}
	if (UpperIndex >= Progress.Num())
	{
		return CanonicalUnit(Cells[Route.Last()].UnitCenter);
	}
	const int32 LowerIndex = UpperIndex - 1;
	const int32 SegmentLength =
		Progress[UpperIndex] - Progress[LowerIndex];
	const double Alpha = SegmentLength > 0
		? static_cast<double>(
			ClampedDistance - Progress[LowerIndex])
			/ static_cast<double>(SegmentLength)
		: 0.0;
	return SlerpUnit(
		CanonicalUnit(Cells[Route[LowerIndex]].UnitCenter),
		CanonicalUnit(Cells[Route[UpperIndex]].UnitCenter),
		Alpha);
}

FVector TangentAtDistance(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<int32>& Route,
	const TArray<int32>& Progress,
	const int32 DistanceCM,
	const int32 DeltaCM)
{
	const int32 Total = Progress.IsEmpty() ? 0 : Progress.Last();
	const FVector Center = SampleRouteDirection(
		Cells,
		Route,
		Progress,
		DistanceCM);
	const FVector Before = SampleRouteDirection(
		Cells,
		Route,
		Progress,
		FMath::Max(0, DistanceCM - DeltaCM));
	const FVector After = SampleRouteDirection(
		Cells,
		Route,
		Progress,
		FMath::Min(Total, DistanceCM + DeltaCM));
	return FVector::VectorPlaneProject(After - Before, Center)
		.GetSafeNormal();
}

FVector ParallelTransport(
	const FVector& Vector,
	const FVector& From,
	const FVector& To)
{
	const FVector Axis = FVector::CrossProduct(From, To);
	const double AxisSize = Axis.Length();
	const double Dot = FMath::Clamp(
		static_cast<double>(FVector::DotProduct(From, To)),
		-1.0,
		1.0);
	if (AxisSize < 1.0e-8)
	{
		return FVector::VectorPlaneProject(Vector, To).GetSafeNormal();
	}
	const double Angle = FMath::Atan2(AxisSize, Dot);
	const FQuat Rotation(Axis / AxisSize, Angle);
	return FVector::VectorPlaneProject(
		Rotation.RotateVector(Vector),
		To).GetSafeNormal();
}

struct FTurnSample
{
	int32 DistanceCM = 0;
	int32 SignedAngleCentidegrees = 0;
};

TArray<FTurnSample> BuildTurnSamples(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<int32>& Route,
	const TArray<int32>& Progress,
	const FABTSM3MonthlyAcceptanceProfileV1& Profile)
{
	TArray<FTurnSample> Samples;
	if (Progress.IsEmpty())
	{
		return Samples;
	}
	const int32 HalfWindow = Profile.BendWindowCM / 2;
	const int32 TangentDelta =
		FMath::Max(50, Profile.BendSampleSpacingCM / 2);
	for (int32 CenterDistance = HalfWindow;
		CenterDistance + HalfWindow <= Progress.Last();
		CenterDistance += Profile.BendSampleSpacingCM)
	{
		const int32 BeforeDistance = CenterDistance - HalfWindow;
		const int32 AfterDistance = CenterDistance + HalfWindow;
		const FVector Center = SampleRouteDirection(
			Cells,
			Route,
			Progress,
			CenterDistance);
		const FVector BeforePoint = SampleRouteDirection(
			Cells,
			Route,
			Progress,
			BeforeDistance);
		const FVector AfterPoint = SampleRouteDirection(
			Cells,
			Route,
			Progress,
			AfterDistance);
		const FVector BeforeTangent = ParallelTransport(
			TangentAtDistance(
				Cells,
				Route,
				Progress,
				BeforeDistance,
				TangentDelta),
			BeforePoint,
			Center);
		const FVector AfterTangent = ParallelTransport(
			TangentAtDistance(
				Cells,
				Route,
				Progress,
				AfterDistance,
				TangentDelta),
			AfterPoint,
			Center);
		if (BeforeTangent.IsNearlyZero()
			|| AfterTangent.IsNearlyZero())
		{
			continue;
		}
		const double Dot = FMath::Clamp(
			static_cast<double>(FVector::DotProduct(
				BeforeTangent,
				AfterTangent)),
			-1.0,
			1.0);
		const double Cross = FVector::DotProduct(
			Center,
			FVector::CrossProduct(BeforeTangent, AfterTangent));
		const double SignedAngle = FMath::Atan2(Cross, Dot);
		Samples.Add({
			CenterDistance,
			QuantizeAngleCentidegrees(SignedAngle)});
	}
	return Samples;
}

int32 CountScenicBends(
	const TArray<FTurnSample>& Samples,
	const FABTSM3MonthlyAcceptanceProfileV1& Profile)
{
	TArray<FTurnSample> Candidates;
	const int32 Threshold =
		Profile.MinBendAngleDegrees * QuantizedAngleScale;
	for (const FTurnSample& Sample : Samples)
	{
		if (FMath::Abs(Sample.SignedAngleCentidegrees)
			>= Threshold)
		{
			Candidates.Add(Sample);
		}
	}
	Candidates.Sort([](
		const FTurnSample& A,
		const FTurnSample& B)
	{
		const int32 AngleA = FMath::Abs(A.SignedAngleCentidegrees);
		const int32 AngleB = FMath::Abs(B.SignedAngleCentidegrees);
		return AngleA != AngleB
			? AngleA > AngleB
			: A.DistanceCM < B.DistanceCM;
	});
	TArray<int32> AcceptedDistances;
	for (const FTurnSample& Candidate : Candidates)
	{
		if (AcceptedDistances.ContainsByPredicate(
			[&Profile, &Candidate](const int32 ExistingDistance)
			{
				return FMath::Abs(
					ExistingDistance - Candidate.DistanceCM)
					< Profile.MinBendSeparationCM;
			}))
		{
			continue;
		}
		AcceptedDistances.Add(Candidate.DistanceCM);
	}
	return AcceptedDistances.Num();
}

int32 ComputeMaxStraightCM(
	const TArray<FTurnSample>& Samples,
	const int32 RouteLengthCM,
	const FABTSM3MonthlyAcceptanceProfileV1& Profile)
{
	if (Samples.IsEmpty())
	{
		return RouteLengthCM;
	}
	const int32 Threshold =
		Profile.StraightTurnThresholdDegrees
		* QuantizedAngleScale;
	const int32 HalfWindow = Profile.BendWindowCM / 2;
	int32 CurrentStart = INDEX_NONE;
	int32 CurrentEnd = INDEX_NONE;
	int32 Maximum = 0;
	for (const FTurnSample& Sample : Samples)
	{
		if (FMath::Abs(Sample.SignedAngleCentidegrees)
			>= Threshold)
		{
			if (CurrentStart != INDEX_NONE)
			{
				Maximum = FMath::Max(
					Maximum,
					CurrentEnd - CurrentStart);
			}
			CurrentStart = INDEX_NONE;
			CurrentEnd = INDEX_NONE;
			continue;
		}
		const int32 IntervalStart =
			FMath::Max(0, Sample.DistanceCM - HalfWindow);
		const int32 IntervalEnd =
			FMath::Min(
				RouteLengthCM,
				Sample.DistanceCM + HalfWindow);
		if (CurrentStart == INDEX_NONE)
		{
			CurrentStart = IntervalStart;
			CurrentEnd = IntervalEnd;
		}
		else if (IntervalStart
			<= CurrentEnd + Profile.BendSampleSpacingCM)
		{
			CurrentEnd = FMath::Max(CurrentEnd, IntervalEnd);
		}
		else
		{
			Maximum = FMath::Max(
				Maximum,
				CurrentEnd - CurrentStart);
			CurrentStart = IntervalStart;
			CurrentEnd = IntervalEnd;
		}
	}
	if (CurrentStart != INDEX_NONE)
	{
		Maximum = FMath::Max(Maximum, CurrentEnd - CurrentStart);
	}
	return Maximum;
}

int32 ComputeMinNonLocalSelfApproachCells(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<int32>& Route,
	const TArray<int32>& Progress,
	const FABTSM3MonthlyAcceptanceProfileV1& Profile)
{
	const int32 SearchDepth = Profile.MinSelfApproachCells + 3;
	int32 Minimum = SearchDepth;
	TArray<int32> RouteIndexByCell;
	RouteIndexByCell.Init(INDEX_NONE, Cells.Num());
	for (int32 RouteIndex = 0; RouteIndex < Route.Num(); ++RouteIndex)
	{
		if (Cells.IsValidIndex(Route[RouteIndex]))
		{
			RouteIndexByCell[Route[RouteIndex]] = RouteIndex;
		}
	}
	TArray<int32> Distance;
	Distance.SetNumUninitialized(Cells.Num());
	TArray<int32> Queue;
	for (int32 RouteIndex = 0; RouteIndex < Route.Num(); ++RouteIndex)
	{
		for (int32& Value : Distance)
		{
			Value = INDEX_NONE;
		}
		Queue.Reset();
		const int32 SourceCell = Route[RouteIndex];
		Distance[SourceCell] = 0;
		Queue.Add(SourceCell);
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const int32 CellId = Queue[Head];
			const int32 Hop = Distance[CellId];
			const int32 OtherRouteIndex =
				RouteIndexByCell[CellId];
			if (OtherRouteIndex != INDEX_NONE
				&& OtherRouteIndex != RouteIndex
				&& FMath::Abs(
					Progress[OtherRouteIndex]
						- Progress[RouteIndex])
					>= Profile.SelfApproachIgnoreAlongRouteCM)
			{
				Minimum = FMath::Min(Minimum, Hop);
			}
			if (Hop >= SearchDepth - 1 || Hop >= Minimum)
			{
				continue;
			}
			for (const int32 NeighborId :
				Cells[CellId].NeighborCellIds)
			{
				if (!Cells.IsValidIndex(NeighborId)
					|| Distance[NeighborId] != INDEX_NONE)
				{
					continue;
				}
				Distance[NeighborId] = Hop + 1;
				Queue.Add(NeighborId);
			}
		}
		if (Minimum == 0)
		{
			break;
		}
	}
	return Minimum;
}

int32 ComputeRouteScore(
	const FABTSM3MonthlyRouteConfig& Config,
	const FABTSM3MonthlyRouteCandidate& Candidate)
{
	const FABTSM3MonthlyRouteMetrics& Metrics = Candidate.Metrics;
	const int32 LengthScale = FMath::Max(
		1,
		Config.Acceptance.MaxRouteLengthCM
			- Config.Acceptance.TargetRouteLengthCM);
	const int32 LengthRisk = FMath::Clamp(
		FMath::Abs(
			Metrics.RouteLengthCM
				- Config.Acceptance.TargetRouteLengthCM)
			* 1000 / LengthScale,
		0,
		1000);
	const int32 StraightRisk = FMath::Clamp(
		(Metrics.MaxStraightCM - 3500) * 1000 / 2000,
		0,
		1000);
	const int32 SelfApproachRisk =
		Metrics.MinSelfApproachCells <=
			Config.Acceptance.MinSelfApproachCells
			? 1000
			: Metrics.MinSelfApproachCells
				== Config.Acceptance.MinSelfApproachCells + 1
				? 500
				: 0;
	const int32 ShoulderRisk = Metrics.TotalEdgeCount > 0
		? Metrics.ShoulderEdgeCount * 1000
			/ Metrics.TotalEdgeCount
		: 1000;
	const int64 BaseDistance =
		FMath::Max(1, Metrics.RouteLengthCM);
	const int32 CostInflation = FMath::Clamp<int64>(
		FMath::Max<int64>(0, Metrics.SolverCost - BaseDistance)
			* 1000 / BaseDistance,
		0,
		1000);
	const int32 Penalty =
		(300 * LengthRisk
			+ 150 * StraightRisk
			+ 200 * SelfApproachRisk
			+ 200 * ShoulderRisk
			+ 150 * CostInflation)
		/ 1000;
	return 1000000 - Penalty;
}

void FillProgressAndBeatPoints(
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlyRouteConfig& Config,
	const float PlanetRadiusCM,
	FABTSM3MonthlyRouteCandidate& Candidate)
{
	Candidate.ProgressDistanceCM.Reset();
	Candidate.ProgressDistanceCM.Reserve(
		Candidate.OrderedRoadCellIds.Num());
	int32 ProgressCM = 0;
	for (int32 Index = 0;
		Index < Candidate.OrderedRoadCellIds.Num();
		++Index)
	{
		if (Index > 0)
		{
			ProgressCM += CellArcLengthCM(
				Cells,
				Candidate.OrderedRoadCellIds[Index - 1],
				Candidate.OrderedRoadCellIds[Index],
				PlanetRadiusCM);
		}
		Candidate.ProgressDistanceCM.Add(ProgressCM);
	}
	Candidate.Metrics.RouteLengthCM = ProgressCM;

	Candidate.BeatPoints.Reset();
	const int32 BeatCount = FMath::Clamp(
		Config.RouteBeatPointCount,
		2,
		Candidate.ControlCellIds.Num());
	for (int32 BeatOrder = 0; BeatOrder < BeatCount; ++BeatOrder)
	{
		const int32 ControlIndex = FMath::RoundToInt(
			static_cast<double>(BeatOrder)
				* static_cast<double>(
					Candidate.ControlCellIds.Num() - 1)
				/ static_cast<double>(BeatCount - 1));
		const int32 CellId =
			Candidate.ControlCellIds[ControlIndex];
		const int32 RouteIndex =
			Candidate.OrderedRoadCellIds.IndexOfByKey(CellId);
		if (!Candidate.ProgressDistanceCM.IsValidIndex(RouteIndex))
		{
			continue;
		}
		FABTSM3MonthlyRouteBeatPoint& Beat =
			Candidate.BeatPoints.AddDefaulted_GetRef();
		Beat.BeatPointId =
			RouteBeatIdBase + Candidate.CandidateId * 100 + BeatOrder;
		Beat.OrderIndex = BeatOrder;
		Beat.CellId = CellId;
		Beat.ProgressDistanceCM =
			Candidate.ProgressDistanceCM[RouteIndex];
		Beat.FlowQ = ProgressCM > 0
			? FMath::Clamp(
				FMath::RoundToInt(
					static_cast<double>(Beat.ProgressDistanceCM)
						* FABTSM3MonthlyRouteBuilder::FlowQuantization
						/ static_cast<double>(ProgressCM)),
				0,
				FABTSM3MonthlyRouteBuilder::FlowQuantization)
			: 0;
		Beat.FlowS = static_cast<float>(Beat.FlowQ)
			/ static_cast<float>(
				FABTSM3MonthlyRouteBuilder::FlowQuantization);
	}
	if (!Candidate.BeatPoints.IsEmpty())
	{
		Candidate.BeatPoints[0].ProgressDistanceCM = 0;
		Candidate.BeatPoints[0].FlowQ = 0;
		Candidate.BeatPoints[0].FlowS = 0.0f;
		Candidate.BeatPoints.Last().ProgressDistanceCM =
			ProgressCM;
		Candidate.BeatPoints.Last().FlowQ =
			FABTSM3MonthlyRouteBuilder::FlowQuantization;
		Candidate.BeatPoints.Last().FlowS = 1.0f;
	}
}

EABTSM3MonthlyRouteRejectReason EvaluateCandidateGeometry(
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlyRouteConfig& Config,
	FABTSM3MonthlyRouteCandidate& Candidate)
{
	if (Candidate.OrderedRoadCellIds.Num() < 2
		|| Candidate.ProgressDistanceCM.Num()
			!= Candidate.OrderedRoadCellIds.Num())
	{
		return EABTSM3MonthlyRouteRejectReason::ProgressInvalid;
	}
	TSet<int32> Visited;
	for (int32 Index = 0;
		Index < Candidate.OrderedRoadCellIds.Num();
		++Index)
	{
		const int32 CellId = Candidate.OrderedRoadCellIds[Index];
		if (!Cells.IsValidIndex(CellId))
		{
			return EABTSM3MonthlyRouteRejectReason::InvalidTopology;
		}
		if (Visited.Contains(CellId))
		{
			return EABTSM3MonthlyRouteRejectReason::RepeatedCell;
		}
		Visited.Add(CellId);
		if (Index > 0)
		{
			const int32 PreviousCell =
				Candidate.OrderedRoadCellIds[Index - 1];
			if (!Cells[PreviousCell].NeighborCellIds.Contains(CellId))
			{
				return
					EABTSM3MonthlyRouteRejectReason::NonAdjacentPath;
			}
			if (Candidate.ProgressDistanceCM[Index]
				<= Candidate.ProgressDistanceCM[Index - 1])
			{
				return
					EABTSM3MonthlyRouteRejectReason::ProgressInvalid;
			}
		}
	}
	if (Candidate.Metrics.RouteLengthCM
			< Config.Acceptance.MinRouteLengthCM
		|| Candidate.Metrics.RouteLengthCM
			> Config.Acceptance.MaxRouteLengthCM)
	{
		return
			EABTSM3MonthlyRouteRejectReason::RouteLengthOutOfRange;
	}
	const TArray<FTurnSample> TurnSamples = BuildTurnSamples(
		Cells,
		Candidate.OrderedRoadCellIds,
		Candidate.ProgressDistanceCM,
		Config.Acceptance);
	Candidate.Metrics.ScenicBendCount = CountScenicBends(
		TurnSamples,
		Config.Acceptance);
	Candidate.Metrics.MaxStraightCM = ComputeMaxStraightCM(
		TurnSamples,
		Candidate.Metrics.RouteLengthCM,
		Config.Acceptance);
	Candidate.Metrics.MinSelfApproachCells =
		ComputeMinNonLocalSelfApproachCells(
			Cells,
			Candidate.OrderedRoadCellIds,
			Candidate.ProgressDistanceCM,
			Config.Acceptance);
	if (Candidate.Metrics.ScenicBendCount
		< Config.Acceptance.MinScenicBendCount)
	{
		return
			EABTSM3MonthlyRouteRejectReason::InsufficientScenicBends;
	}
	if (Candidate.Metrics.MaxStraightCM
		> Config.Acceptance.MaxStraightCM)
	{
		return
			EABTSM3MonthlyRouteRejectReason::StraightRunTooLong;
	}
	if (Candidate.Metrics.MinSelfApproachCells
		< Config.Acceptance.MinSelfApproachCells)
	{
		return
			EABTSM3MonthlyRouteRejectReason::NonLocalSelfApproach;
	}
	if (Candidate.BeatPoints.Num() != Config.RouteBeatPointCount
		|| Candidate.BeatPoints.IsEmpty()
		|| Candidate.BeatPoints[0].FlowQ != 0
		|| Candidate.BeatPoints.Last().FlowQ
			!= FABTSM3MonthlyRouteBuilder::FlowQuantization)
	{
		return EABTSM3MonthlyRouteRejectReason::ProgressInvalid;
	}
	for (int32 BeatIndex = 1;
		BeatIndex < Candidate.BeatPoints.Num();
		++BeatIndex)
	{
		if (Candidate.BeatPoints[BeatIndex].ProgressDistanceCM
				<= Candidate.BeatPoints[BeatIndex - 1].
					ProgressDistanceCM
			|| Candidate.BeatPoints[BeatIndex].FlowQ
				<= Candidate.BeatPoints[BeatIndex - 1].FlowQ)
		{
			return
				EABTSM3MonthlyRouteRejectReason::ProgressInvalid;
		}
	}
	return EABTSM3MonthlyRouteRejectReason::None;
}

bool BuildControlCells(
	const int32 WorldSeed,
	const int32 CandidateId,
	const bool bFallback,
	const FABTSM3MonthlyRouteConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	TArray<int32>& OutControlCellIds,
	EABTSM3MonthlyRouteRejectReason& OutReason)
{
	OutControlCellIds.Reset();
	OutReason = EABTSM3MonthlyRouteRejectReason::None;
	uint64 RandomState = MakeCandidateSeed(
		WorldSeed,
		CandidateId,
		bFallback);
	const int32 StartCellId = static_cast<int32>(
		Mix64(static_cast<uint32>(WorldSeed)
			^ 0x4d33523253544152ull)
		% static_cast<uint64>(Cells.Num()));
	if (!Cells.IsValidIndex(StartCellId)
		|| Cells[StartCellId].NeighborCellIds.IsEmpty())
	{
		OutReason = EABTSM3MonthlyRouteRejectReason::InvalidTopology;
		return false;
	}
	const FVector Start =
		CanonicalUnit(Cells[StartCellId].UnitCenter);
	const int32 BaseNeighborIndex = static_cast<int32>(
		Mix64(static_cast<uint32>(WorldSeed)
			^ 0x4d33523248454144ull)
		% static_cast<uint64>(
			Cells[StartCellId].NeighborCellIds.Num()));
	const FVector BaseNeighbor = CanonicalUnit(Cells[
		Cells[StartCellId].NeighborCellIds[BaseNeighborIndex]]
		.UnitCenter);
	FVector TangentX = FVector::VectorPlaneProject(
		BaseNeighbor - Start,
		Start).GetSafeNormal();
	if (TangentX.IsNearlyZero())
	{
		OutReason =
			EABTSM3MonthlyRouteRejectReason::SkeletonGenerationFailed;
		return false;
	}
	const double HeadingOffset = bFallback
		? 0.0
		: UnitRandomSigned(RandomState) * 0.32;
	TangentX = FQuat(Start, HeadingOffset)
		.RotateVector(TangentX).GetSafeNormal();
	FVector TangentY = FVector::CrossProduct(Start, TangentX)
		.GetSafeNormal();
	if (TangentY.IsNearlyZero())
	{
		OutReason =
			EABTSM3MonthlyRouteRejectReason::SkeletonGenerationFailed;
		return false;
	}
	const double SpanRadians = bFallback
		? 2.40
		: 2.34 + UnitRandomSigned(RandomState) * 0.10;
	const double AmplitudeRadians = bFallback
		? 0.34
		: 0.32 + UnitRandomSigned(RandomState) * 0.035;
	const double Mirror = ((CandidateId
		+ static_cast<int32>(Mix64(
			static_cast<uint32>(WorldSeed)) & 1ull)) & 1)
		? -1.0
		: 1.0;
	const int32 ControlCount = Config.SkeletonControlPointCount;
	OutControlCellIds.Reserve(ControlCount);
	for (int32 ControlIndex = 0;
		ControlIndex < ControlCount;
		++ControlIndex)
	{
		const double T = static_cast<double>(ControlIndex)
			/ static_cast<double>(ControlCount - 1);
		const double Longitude = SpanRadians * T;
		const double Latitude = Mirror
			* AmplitudeRadians
			* FMath::Sin(3.0 * UE_PI * T);
		const FVector EquatorDirection =
			Start * FMath::Cos(Longitude)
			+ TangentX * FMath::Sin(Longitude);
		const FVector Direction = (
			EquatorDirection * FMath::Cos(Latitude)
			+ TangentY * FMath::Sin(Latitude))
			.GetSafeNormal();
		if (!IsFiniteVector(Direction)
			|| (ControlIndex > 0
				&& FVector::DotProduct(
					CanonicalUnit(
						Cells[OutControlCellIds.Last()].UnitCenter),
					Direction)
					<= Config.AntipodalRejectDot))
		{
			OutReason =
				EABTSM3MonthlyRouteRejectReason::
					AntipodalInstability;
			return false;
		}
		const int32 CellId = ControlIndex == 0
			? StartCellId
			: FindNearestCell(Cells, Direction);
		if (!Cells.IsValidIndex(CellId)
			|| (!OutControlCellIds.IsEmpty()
				&& CellId == OutControlCellIds.Last()))
		{
			OutReason =
				EABTSM3MonthlyRouteRejectReason::
					SkeletonGenerationFailed;
			return false;
		}
		OutControlCellIds.Add(CellId);
	}
	return true;
}

bool BuildCandidate(
	const int32 WorldSeed,
	const int32 CandidateId,
	const EABTSM3MonthlyRouteOrigin Origin,
	const FABTSM3MonthlyRouteConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	const FABTSM3MonthlyRoadContext& Context,
	FABTSM3MonthlyRouteCandidate& OutCandidate)
{
	OutCandidate = FABTSM3MonthlyRouteCandidate();
	OutCandidate.CandidateId = CandidateId;
	OutCandidate.Origin = Origin;
	EABTSM3MonthlyRouteRejectReason Reason =
		EABTSM3MonthlyRouteRejectReason::None;
	if (!BuildControlCells(
			WorldSeed,
			CandidateId,
			Origin == EABTSM3MonthlyRouteOrigin::
				MonthlyRouteFallback,
			Config,
			Cells,
			OutCandidate.ControlCellIds,
			Reason))
	{
		OutCandidate.RejectReason = Reason;
		OutCandidate.CandidateHash = static_cast<int64>(
			FABTSM3MonthlyRouteBuilder::ComputeCandidateHash(
				OutCandidate));
		return false;
	}

	TArray<bool> CorridorMembership;
	CorridorMembership.Init(false, Cells.Num());
	int32 PreviousCellId = INDEX_NONE;
	int32 RemainingReuseBonusBudget =
		Config.Costs.MaxReuseBonusPerCandidate;
	for (int32 SegmentIndex = 0;
		SegmentIndex + 1 < OutCandidate.ControlCellIds.Num();
		++SegmentIndex)
	{
		TArray<int32> Centerline;
		if (!FindUnweightedPath(
			Cells,
			OutCandidate.ControlCellIds[SegmentIndex],
			OutCandidate.ControlCellIds[SegmentIndex + 1],
			Centerline))
		{
			OutCandidate.RejectReason =
				EABTSM3MonthlyRouteRejectReason::
					SkeletonGenerationFailed;
			OutCandidate.CandidateHash = static_cast<int64>(
				FABTSM3MonthlyRouteBuilder::ComputeCandidateHash(
					OutCandidate));
			return false;
		}
		TArray<int32> CorridorRings;
		BuildCorridorRings(
			Cells,
			Centerline,
			Config.CorridorAllowedRadiusCells,
			CorridorRings);
		for (int32 CellId = 0; CellId < CorridorRings.Num(); ++CellId)
		{
			if (CorridorRings[CellId] != INDEX_NONE)
			{
				CorridorMembership[CellId] = true;
			}
		}
		FSegmentSolveResult Segment;
		if (!SolveSegment(
			Cells,
			Config,
			Context,
			CorridorRings,
			PlanetRadiusCM,
			OutCandidate.ControlCellIds[SegmentIndex],
			OutCandidate.ControlCellIds[SegmentIndex + 1],
			PreviousCellId,
			Config.MaxExpandedStatesPerCandidate
				- OutCandidate.ExpandedStates,
			Config.MaxRelaxationsPerCandidate
				- OutCandidate.Relaxations,
			RemainingReuseBonusBudget,
			Segment))
		{
			OutCandidate.ExpandedStates += Segment.ExpandedStates;
			OutCandidate.Relaxations += Segment.Relaxations;
			OutCandidate.RejectReason = Segment.RejectReason;
			OutCandidate.CandidateHash = static_cast<int64>(
				FABTSM3MonthlyRouteBuilder::ComputeCandidateHash(
					OutCandidate));
			return false;
		}
		OutCandidate.ExpandedStates += Segment.ExpandedStates;
		OutCandidate.Relaxations += Segment.Relaxations;
		OutCandidate.Metrics.SolverCost += Segment.Cost;
		OutCandidate.Metrics.AppliedReuseBonus +=
			Segment.AppliedReuseBonus;
		RemainingReuseBonusBudget = FMath::Max(
			0,
			RemainingReuseBonusBudget
				- Segment.AppliedReuseBonus);
		OutCandidate.Metrics.ShoulderEdgeCount +=
			Segment.ShoulderEdges;
		OutCandidate.Metrics.UTurnCount += Segment.UTurns;
		if (OutCandidate.OrderedRoadCellIds.IsEmpty())
		{
			OutCandidate.OrderedRoadCellIds = MoveTemp(Segment.Path);
		}
		else
		{
			if (Segment.Path.IsEmpty()
				|| Segment.Path[0]
					!= OutCandidate.OrderedRoadCellIds.Last())
			{
				OutCandidate.RejectReason =
					EABTSM3MonthlyRouteRejectReason::
						NonAdjacentPath;
				OutCandidate.CandidateHash = static_cast<int64>(
					FABTSM3MonthlyRouteBuilder::
						ComputeCandidateHash(OutCandidate));
				return false;
			}
			for (int32 PathIndex = 1;
				PathIndex < Segment.Path.Num();
				++PathIndex)
			{
				OutCandidate.OrderedRoadCellIds.Add(
					Segment.Path[PathIndex]);
			}
		}
		if (OutCandidate.OrderedRoadCellIds.Num() >= 2)
		{
			PreviousCellId = OutCandidate.OrderedRoadCellIds[
				OutCandidate.OrderedRoadCellIds.Num() - 2];
		}
	}
	for (int32 CellId = 0; CellId < CorridorMembership.Num(); ++CellId)
	{
		if (CorridorMembership[CellId])
		{
			OutCandidate.CorridorCellIds.Add(CellId);
		}
	}
	OutCandidate.Metrics.TotalEdgeCount =
		FMath::Max(0, OutCandidate.OrderedRoadCellIds.Num() - 1);
	FillProgressAndBeatPoints(
		Cells,
		Config,
		PlanetRadiusCM,
		OutCandidate);
	OutCandidate.RejectReason = EvaluateCandidateGeometry(
		Cells,
		Config,
		OutCandidate);
	OutCandidate.bHardPass =
		OutCandidate.RejectReason
			== EABTSM3MonthlyRouteRejectReason::None;
	if (OutCandidate.bHardPass)
	{
		OutCandidate.RouteScore =
			ComputeRouteScore(Config, OutCandidate);
	}
	OutCandidate.CandidateHash = static_cast<int64>(
		FABTSM3MonthlyRouteBuilder::ComputeCandidateHash(
			OutCandidate));
	return OutCandidate.bHardPass;
}

FABTSM3MonthlyRouteAttemptReport MakeAttemptReport(
	const FABTSM3MonthlyRouteCandidate& Candidate)
{
	FABTSM3MonthlyRouteAttemptReport Report;
	Report.CandidateId = Candidate.CandidateId;
	Report.Origin = Candidate.Origin;
	Report.bHardPass = Candidate.bHardPass;
	Report.RejectReason = Candidate.RejectReason;
	Report.RouteScore = Candidate.RouteScore;
	Report.CandidateHash = Candidate.CandidateHash;
	Report.ExpandedStates = Candidate.ExpandedStates;
	Report.Relaxations = Candidate.Relaxations;
	Report.Backtracks = Candidate.Backtracks;
	return Report;
}

bool CandidateLess(
	const FABTSM3MonthlyRouteCandidate& A,
	const FABTSM3MonthlyRouteCandidate& B,
	const FABTSM3MonthlyRouteConfig& Config)
{
	if (A.RouteScore != B.RouteScore)
	{
		return A.RouteScore > B.RouteScore;
	}
	const int32 LengthRiskA = FMath::Abs(
		A.Metrics.RouteLengthCM
			- Config.Acceptance.TargetRouteLengthCM);
	const int32 LengthRiskB = FMath::Abs(
		B.Metrics.RouteLengthCM
			- Config.Acceptance.TargetRouteLengthCM);
	if (LengthRiskA != LengthRiskB)
	{
		return LengthRiskA < LengthRiskB;
	}
	if (A.Metrics.MaxStraightCM != B.Metrics.MaxStraightCM)
	{
		return A.Metrics.MaxStraightCM < B.Metrics.MaxStraightCM;
	}
	if (A.Metrics.MinSelfApproachCells
		!= B.Metrics.MinSelfApproachCells)
	{
		return A.Metrics.MinSelfApproachCells
			> B.Metrics.MinSelfApproachCells;
	}
	if (A.Metrics.ShoulderEdgeCount
		!= B.Metrics.ShoulderEdgeCount)
	{
		return A.Metrics.ShoulderEdgeCount
			< B.Metrics.ShoulderEdgeCount;
	}
	if (A.OrderedRoadCellIds != B.OrderedRoadCellIds)
	{
		const int32 SharedCount = FMath::Min(
			A.OrderedRoadCellIds.Num(),
			B.OrderedRoadCellIds.Num());
		for (int32 Index = 0; Index < SharedCount; ++Index)
		{
			if (A.OrderedRoadCellIds[Index]
				!= B.OrderedRoadCellIds[Index])
			{
				return A.OrderedRoadCellIds[Index]
					< B.OrderedRoadCellIds[Index];
			}
		}
		return A.OrderedRoadCellIds.Num()
			< B.OrderedRoadCellIds.Num();
	}
	return A.CandidateId < B.CandidateId;
}

bool IsDuplicateRoute(
	const TArray<FABTSM3MonthlyRouteCandidate>& Candidates,
	const FABTSM3MonthlyRouteCandidate& Candidate)
{
	return Candidates.ContainsByPredicate(
		[&Candidate](const FABTSM3MonthlyRouteCandidate& Existing)
		{
			return Existing.OrderedRoadCellIds
				== Candidate.OrderedRoadCellIds;
		});
}

bool ValidateConfig(
	const FABTSM3MonthlyRouteConfig& Config,
	FString& OutFailure)
{
	const FABTSM3MonthlyAcceptanceProfileV1& A = Config.Acceptance;
	const FABTSM3MonthlyRouteCostProfileV1& C = Config.Costs;
	if (Config.NormalCandidateSlots < 4
		|| Config.NormalCandidateSlots > 8
		|| Config.MaxRetainedCandidates < 1
		|| Config.MaxRetainedCandidates > 3
		|| Config.SkeletonControlPointCount < 9
		|| Config.SkeletonControlPointCount > 33
		|| Config.RouteBeatPointCount < 2
		|| Config.RouteBeatPointCount
			> Config.SkeletonControlPointCount
		|| Config.CorridorCoreRadiusCells < 0
		|| Config.CorridorAllowedRadiusCells
			<= Config.CorridorCoreRadiusCells
		|| Config.MaxExpandedStatesPerCandidate < 1024
		|| Config.MaxRelaxationsPerCandidate < 4096
		|| Config.MaxCandidateBacktracksPerSeed < 0
		|| !FMath::IsFinite(Config.AntipodalRejectDot)
		|| Config.AntipodalRejectDot < -1.0f
		|| Config.AntipodalRejectDot > -0.5f
		|| Config.RoutePlannerVersion != 1
		|| Config.RouteMetricVersion != 1
		|| Config.RoadSolverVersion != 1
		|| Config.RouteScoreVersion != 1
		|| Config.RouteFallbackVersion != 1)
	{
		OutFailure = TEXT("RouteConfigDomain");
		return false;
	}
	if (A.MinRouteLengthCM <= 0
		|| A.TargetRouteLengthCM < A.MinRouteLengthCM
		|| A.MaxRouteLengthCM < A.TargetRouteLengthCM
		|| A.BendSampleSpacingCM <= 0
		|| A.BendWindowCM < 2
		|| (A.BendWindowCM & 1) != 0
		|| A.MinBendAngleDegrees <= 0
		|| A.MinBendAngleDegrees >= 180
		|| A.MinBendSeparationCM <= 0
		|| A.StraightTurnThresholdDegrees < 0
		|| A.StraightTurnThresholdDegrees >= 180
		|| A.MaxStraightCM <= 0
		|| A.SelfApproachIgnoreAlongRouteCM < 0
		|| A.MinSelfApproachCells <= 0
		|| A.MinScenicBendCount <= 0)
	{
		OutFailure = TEXT("AcceptanceProfileDomain");
		return false;
	}
	if (C.CorridorRing3Penalty < 0
		|| C.CorridorRing4Penalty < 0
		|| C.SharpTurnStartDegrees < 0
		|| C.SharpTurnStartDegrees >= 180
		|| C.SharpTurnCostPerDegree < 0
		|| C.UTurnPenaltyStartDegrees < 0
		|| C.UTurnPenaltyStartDegrees >= C.UTurnRejectDegrees
		|| C.UTurnRejectDegrees <= 0
		|| C.UTurnRejectDegrees >= 180
		|| C.UTurnCostPerDegree < 0
		|| C.LegalWaterCrossingCost < 0
		|| C.SoftEncounterReservationCost < 0
		|| C.MaxReuseBonusPerStep < 0
		|| C.MaxReuseBonusPerCandidate < 0)
	{
		OutFailure = TEXT("CostProfileDomain");
		return false;
	}
	return true;
}

bool ValidateRoadContext(
	const FABTSM3MonthlyRoadContext& Context,
	const int32 CellCount,
	FString& OutFailure)
{
	if (!Context.Cells.IsEmpty()
		&& Context.Cells.Num() != CellCount)
	{
		OutFailure = TEXT("RoadContextCellCount");
		return false;
	}
	for (int32 CellId = 0; CellId < Context.Cells.Num(); ++CellId)
	{
		const FABTSM3MonthlyRouteCellContext& Cell =
			Context.Cells[CellId];
		if (Cell.TerrainCost < 0 || Cell.SlopeCost < 0)
		{
			OutFailure = FString::Printf(
				TEXT("RoadContextCost:%d"),
				CellId);
			return false;
		}
	}
	return true;
}

bool ValidateTopology(
	const TArray<FABTSM2Cell>& Cells,
	FString& OutFailure)
{
	if (Cells.Num() < 12)
	{
		OutFailure = TEXT("TopologyCellCount");
		return false;
	}
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		const FABTSM2Cell& Cell = Cells[CellId];
		if (!IsFiniteVector(Cell.UnitCenter)
			|| !FMath::IsNearlyEqual(
				Cell.UnitCenter.SquaredLength(),
				1.0,
				1.0e-3)
			|| Cell.NeighborCellIds.IsEmpty()
			|| Cell.NeighborCellIds.Num() >= StateStride)
		{
			OutFailure = FString::Printf(
				TEXT("TopologyCell:%d"),
				CellId);
			return false;
		}
		int32 PreviousNeighbor = INDEX_NONE;
		for (const int32 NeighborId : Cell.NeighborCellIds)
		{
			if (!Cells.IsValidIndex(NeighborId)
				|| NeighborId == CellId
				|| (PreviousNeighbor != INDEX_NONE
					&& NeighborId <= PreviousNeighbor)
				|| !Cells[NeighborId].NeighborCellIds.Contains(CellId))
			{
				OutFailure = FString::Printf(
					TEXT("TopologyNeighbor:%d:%d"),
					CellId,
					NeighborId);
				return false;
			}
			PreviousNeighbor = NeighborId;
		}
	}
	return true;
}

uint64 ComputeContextHash(
	const FABTSM3MonthlyRoadContext& Context)
{
	FCanonicalHash64 Hash;
	Hash.AddInt32(Context.Cells.Num());
	for (const FABTSM3MonthlyRouteCellContext& Cell : Context.Cells)
	{
		Hash.AddInt32(Cell.TerrainCost);
		Hash.AddInt32(Cell.SlopeCost);
		Hash.AddBool(Cell.bWater);
		Hash.AddBool(Cell.bLegalWaterCrossing);
		Hash.AddBool(Cell.bSoftEncounterReserved);
		Hash.AddBool(Cell.bHardBlocked);
		Hash.AddInt32(Cell.ReuseBias);
	}
	return Hash.Get();
}
}

uint64 FABTSM3MonthlyRouteBuilder::ComputeTopologyHash(
	const TArray<FABTSM2Cell>& Cells)
{
	using namespace ABTSM3R2RoutePrivate;
	FCanonicalHash64 Hash;
	Hash.AddInt32(Cells.Num());
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		const FABTSM2Cell& Cell = Cells[CellId];
		Hash.AddInt32(CellId);
		Hash.AddInt32(QuantizeUnit(Cell.UnitCenter.X));
		Hash.AddInt32(QuantizeUnit(Cell.UnitCenter.Y));
		Hash.AddInt32(QuantizeUnit(Cell.UnitCenter.Z));
		Hash.AddBool(Cell.bIsPentagon);
		Hash.AddIntArray(Cell.NeighborCellIds);
	}
	return Hash.Get();
}

uint64 FABTSM3MonthlyRouteBuilder::ComputeRoadContextHash(
	const FABTSM3MonthlyRoadContext& Context)
{
	return ABTSM3R2RoutePrivate::ComputeContextHash(Context);
}

uint64 FABTSM3MonthlyRouteBuilder::ComputeConfigHash(
	const FABTSM3MonthlyRouteConfig& Config,
	const float PlanetRadiusCM,
	const uint64 TopologyHash)
{
	using namespace ABTSM3R2RoutePrivate;
	FCanonicalHash64 Hash;
	Hash.AddInt32(GeneratorVersion);
	Hash.AddInt32(MonthlyLayoutPolicyVersion);
	Hash.AddInt32(Config.RoutePlannerVersion);
	Hash.AddInt32(Config.RouteMetricVersion);
	Hash.AddInt32(Config.RoadSolverVersion);
	Hash.AddInt32(Config.RouteScoreVersion);
	Hash.AddInt32(Config.RouteFallbackVersion);
	Hash.AddBool(Config.bBuildRouteObservation);
	Hash.AddInt32(Config.NormalCandidateSlots);
	Hash.AddInt32(Config.MaxRetainedCandidates);
	Hash.AddInt32(Config.SkeletonControlPointCount);
	Hash.AddInt32(Config.RouteBeatPointCount);
	Hash.AddInt32(Config.CorridorCoreRadiusCells);
	Hash.AddInt32(Config.CorridorAllowedRadiusCells);
	Hash.AddInt32(Config.MaxExpandedStatesPerCandidate);
	Hash.AddInt32(Config.MaxRelaxationsPerCandidate);
	Hash.AddInt32(Config.MaxCandidateBacktracksPerSeed);
	Hash.AddInt32(QuantizeUnit(Config.AntipodalRejectDot));
	const FABTSM3MonthlyAcceptanceProfileV1& A = Config.Acceptance;
	Hash.AddInt32(A.MinRouteLengthCM);
	Hash.AddInt32(A.TargetRouteLengthCM);
	Hash.AddInt32(A.MaxRouteLengthCM);
	Hash.AddInt32(A.BendSampleSpacingCM);
	Hash.AddInt32(A.BendWindowCM);
	Hash.AddInt32(A.MinBendAngleDegrees);
	Hash.AddInt32(A.MinBendSeparationCM);
	Hash.AddInt32(A.StraightTurnThresholdDegrees);
	Hash.AddInt32(A.MaxStraightCM);
	Hash.AddInt32(A.SelfApproachIgnoreAlongRouteCM);
	Hash.AddInt32(A.MinSelfApproachCells);
	Hash.AddInt32(A.MinScenicBendCount);
	const FABTSM3MonthlyRouteCostProfileV1& C = Config.Costs;
	Hash.AddInt32(C.CorridorRing3Penalty);
	Hash.AddInt32(C.CorridorRing4Penalty);
	Hash.AddInt32(C.SharpTurnStartDegrees);
	Hash.AddInt32(C.SharpTurnCostPerDegree);
	Hash.AddInt32(C.UTurnPenaltyStartDegrees);
	Hash.AddInt32(C.UTurnCostPerDegree);
	Hash.AddInt32(C.UTurnRejectDegrees);
	Hash.AddInt32(C.LegalWaterCrossingCost);
	Hash.AddInt32(C.SoftEncounterReservationCost);
	Hash.AddInt32(C.MaxReuseBonusPerStep);
	Hash.AddInt32(C.MaxReuseBonusPerCandidate);
	Hash.AddInt32(FMath::RoundToInt(PlanetRadiusCM));
	Hash.AddUInt64(TopologyHash);
	return Hash.Get();
}

uint64 FABTSM3MonthlyRouteBuilder::ComputeCandidateHash(
	const FABTSM3MonthlyRouteCandidate& Candidate)
{
	using namespace ABTSM3R2RoutePrivate;
	FCanonicalHash64 Hash;
	Hash.AddInt32(Candidate.CandidateId);
	Hash.AddByte(static_cast<uint8>(Candidate.Origin));
	Hash.AddBool(Candidate.bHardPass);
	Hash.AddByte(static_cast<uint8>(Candidate.RejectReason));
	Hash.AddIntArray(Candidate.ControlCellIds);
	Hash.AddIntArray(Candidate.CorridorCellIds);
	Hash.AddIntArray(Candidate.OrderedRoadCellIds);
	Hash.AddIntArray(Candidate.ProgressDistanceCM);
	Hash.AddInt32(Candidate.BeatPoints.Num());
	for (const FABTSM3MonthlyRouteBeatPoint& Beat :
		Candidate.BeatPoints)
	{
		Hash.AddInt32(Beat.BeatPointId);
		Hash.AddInt32(Beat.OrderIndex);
		Hash.AddInt32(Beat.CellId);
		Hash.AddInt32(Beat.ProgressDistanceCM);
		Hash.AddInt32(Beat.FlowQ);
	}
	const FABTSM3MonthlyRouteMetrics& M = Candidate.Metrics;
	Hash.AddInt32(M.RouteLengthCM);
	Hash.AddInt32(M.ScenicBendCount);
	Hash.AddInt32(M.MaxStraightCM);
	Hash.AddInt32(M.MinSelfApproachCells);
	Hash.AddInt32(M.ShoulderEdgeCount);
	Hash.AddInt32(M.TotalEdgeCount);
	Hash.AddInt32(M.UTurnCount);
	Hash.AddInt32(M.AppliedReuseBonus);
	Hash.AddInt64(M.SolverCost);
	Hash.AddInt32(Candidate.RouteScore);
	Hash.AddInt32(Candidate.ExpandedStates);
	Hash.AddInt32(Candidate.Relaxations);
	Hash.AddInt32(Candidate.Backtracks);
	return Hash.Get();
}

uint64 FABTSM3MonthlyRouteBuilder::ComputePoolHash(
	const FABTSM3MonthlyRoutePool& Pool)
{
	using namespace ABTSM3R2RoutePrivate;
	FCanonicalHash64 Hash;
	Hash.AddInt32(Pool.SchemaVersion);
	Hash.AddInt32(Pool.GeneratorVersion);
	Hash.AddInt32(Pool.LayoutPolicyVersion);
	Hash.AddInt32(Pool.WorldSeed);
	Hash.AddInt64(Pool.RouteConfigHash);
	Hash.AddInt64(Pool.TopologyHash);
	Hash.AddInt64(Pool.RoadContextHash);
	Hash.AddBool(Pool.bRoutePoolValid);
	Hash.AddBool(Pool.bMonthlyWorldAccepted);
	Hash.AddBool(Pool.bUsedRouteFallback);
	Hash.AddByte(static_cast<uint8>(Pool.RejectReason));
	Hash.AddInt32(Pool.AttemptedCandidateCount);
	Hash.AddInt32(Pool.NormalHardPassCount);
	Hash.AddInt32(Pool.AttemptReports.Num());
	for (const FABTSM3MonthlyRouteAttemptReport& Report :
		Pool.AttemptReports)
	{
		Hash.AddInt32(Report.CandidateId);
		Hash.AddByte(static_cast<uint8>(Report.Origin));
		Hash.AddBool(Report.bHardPass);
		Hash.AddByte(static_cast<uint8>(Report.RejectReason));
		Hash.AddInt32(Report.RouteScore);
		Hash.AddInt64(Report.CandidateHash);
		Hash.AddInt32(Report.ExpandedStates);
		Hash.AddInt32(Report.Relaxations);
		Hash.AddInt32(Report.Backtracks);
	}
	Hash.AddInt32(Pool.RetainedCandidates.Num());
	for (const FABTSM3MonthlyRouteCandidate& Candidate :
		Pool.RetainedCandidates)
	{
		Hash.AddInt64(Candidate.CandidateHash);
	}
	return Hash.Get();
}

uint64 FABTSM3MonthlyRouteBuilder::ComputePoolSnapshotHash(
	const FABTSM3MonthlyRoutePool& Pool)
{
	using namespace ABTSM3R2RoutePrivate;
	FCanonicalHash64 Hash;
	Hash.AddUInt64(ComputePoolHash(Pool));
	Hash.AddInt32(Pool.RetainedCandidates.Num());
	for (const FABTSM3MonthlyRouteCandidate& Candidate :
		Pool.RetainedCandidates)
	{
		Hash.AddInt32(Candidate.CandidateId);
		Hash.AddByte(static_cast<uint8>(Candidate.Origin));
		Hash.AddBool(Candidate.bHardPass);
		Hash.AddByte(static_cast<uint8>(Candidate.RejectReason));
		Hash.AddIntArray(Candidate.ControlCellIds);
		Hash.AddIntArray(Candidate.CorridorCellIds);
		Hash.AddIntArray(Candidate.OrderedRoadCellIds);
		Hash.AddIntArray(Candidate.ProgressDistanceCM);
		Hash.AddInt32(Candidate.RouteScore);
		Hash.AddInt64(Candidate.CandidateHash);
	}
	return Hash.Get();
}

bool FABTSM3MonthlyRouteBuilder::Build(
	const int32 WorldSeed,
	const FABTSM3MonthlyRouteConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	const FABTSM3MonthlyRoadContext& Context,
	FABTSM3MonthlyRoutePool& OutPool,
	FString& OutFailure)
{
	using namespace ABTSM3R2RoutePrivate;
	OutPool = FABTSM3MonthlyRoutePool();
	OutFailure.Reset();
	OutPool.WorldSeed = WorldSeed;
	OutPool.GeneratorVersion = GeneratorVersion;
	OutPool.LayoutPolicyVersion = MonthlyLayoutPolicyVersion;
	if (!ValidateConfig(Config, OutFailure)
		|| !FMath::IsFinite(PlanetRadiusCM)
		|| PlanetRadiusCM <= 0.0f)
	{
		OutPool.RejectReason =
			EABTSM3MonthlyRouteRejectReason::InvalidConfig;
		return false;
	}
	if (!ValidateTopology(Cells, OutFailure))
	{
		OutPool.RejectReason =
			EABTSM3MonthlyRouteRejectReason::InvalidTopology;
		return false;
	}
	if (!ValidateRoadContext(Context, Cells.Num(), OutFailure))
	{
		OutPool.RejectReason =
			EABTSM3MonthlyRouteRejectReason::InvalidContext;
		return false;
	}
	OutPool.TopologyHash = static_cast<int64>(
		ComputeTopologyHash(Cells));
	OutPool.RouteConfigHash = static_cast<int64>(
		ComputeConfigHash(
			Config,
			PlanetRadiusCM,
			static_cast<uint64>(OutPool.TopologyHash)));
	OutPool.RoadContextHash = static_cast<int64>(
		ComputeContextHash(Context));
	if (!Config.bBuildRouteObservation)
	{
		OutPool.bRoutePoolValid = true;
		OutPool.RejectReason =
			EABTSM3MonthlyRouteRejectReason::NotEvaluated;
		OutPool.RouteCandidatePoolHash = static_cast<int64>(
			ComputePoolHash(OutPool));
		return true;
	}

	TArray<FABTSM3MonthlyRouteCandidate> HardPassCandidates;
	for (int32 CandidateId = 0;
		CandidateId < Config.NormalCandidateSlots;
		++CandidateId)
	{
		FABTSM3MonthlyRouteCandidate Candidate;
		BuildCandidate(
			WorldSeed,
			CandidateId,
			EABTSM3MonthlyRouteOrigin::Normal,
			Config,
			Cells,
			PlanetRadiusCM,
			Context,
			Candidate);
		if (Config.bEmitRouteLogs && !Candidate.bHardPass)
		{
			UE_LOG(LogABTSRuntime, Verbose,
				TEXT("[ABTS][M3R2][RouteReject] Seed=%d Candidate=%d Origin=Normal Reason=%s LengthCM=%d Bends=%d StraightCM=%d SelfApproachCells=%d Cells=%d"),
				WorldSeed,
				Candidate.CandidateId,
				GetRejectReasonName(Candidate.RejectReason),
				Candidate.Metrics.RouteLengthCM,
				Candidate.Metrics.ScenicBendCount,
				Candidate.Metrics.MaxStraightCM,
				Candidate.Metrics.MinSelfApproachCells,
				Candidate.OrderedRoadCellIds.Num());
		}
		OutPool.AttemptReports.Add(MakeAttemptReport(Candidate));
		++OutPool.AttemptedCandidateCount;
		if (Candidate.bHardPass)
		{
			++OutPool.NormalHardPassCount;
			if (!IsDuplicateRoute(HardPassCandidates, Candidate))
			{
				HardPassCandidates.Add(MoveTemp(Candidate));
			}
		}
	}
	HardPassCandidates.Sort(
		[&Config](
			const FABTSM3MonthlyRouteCandidate& A,
			const FABTSM3MonthlyRouteCandidate& B)
		{
			return CandidateLess(A, B, Config);
		});
	for (int32 CandidateIndex = 0;
		CandidateIndex < HardPassCandidates.Num()
			&& CandidateIndex < Config.MaxRetainedCandidates;
		++CandidateIndex)
	{
		OutPool.RetainedCandidates.Add(
			MoveTemp(HardPassCandidates[CandidateIndex]));
	}

	if (OutPool.RetainedCandidates.IsEmpty())
	{
		OutPool.bUsedRouteFallback = true;
		FABTSM3MonthlyRoadContext NeutralFallbackContext;
		FABTSM3MonthlyRouteCandidate Fallback;
		bool bFallbackBuilt = false;
		for (int32 FallbackIndex = 0;
			FallbackIndex < FMath::Min(
				8,
				Config.MaxCandidateBacktracksPerSeed + 1)
				&& !bFallbackBuilt;
			++FallbackIndex)
		{
			const int32 FallbackCandidateId =
				FallbackCandidateBase + FallbackIndex;
			bFallbackBuilt = BuildCandidate(
				WorldSeed,
				FallbackCandidateId,
				EABTSM3MonthlyRouteOrigin::MonthlyRouteFallback,
				Config,
				Cells,
				PlanetRadiusCM,
				NeutralFallbackContext,
				Fallback);
			Fallback.Backtracks = FallbackIndex;
			Fallback.CandidateHash = static_cast<int64>(
				ComputeCandidateHash(Fallback));
			OutPool.AttemptReports.Add(
				MakeAttemptReport(Fallback));
			if (Config.bEmitRouteLogs && !bFallbackBuilt)
			{
				UE_LOG(LogABTSRuntime, Verbose,
					TEXT("[ABTS][M3R2][RouteReject] Seed=%d Candidate=%d Origin=Fallback Reason=%s LengthCM=%d Bends=%d StraightCM=%d SelfApproachCells=%d Cells=%d"),
					WorldSeed,
					Fallback.CandidateId,
					GetRejectReasonName(Fallback.RejectReason),
					Fallback.Metrics.RouteLengthCM,
					Fallback.Metrics.ScenicBendCount,
					Fallback.Metrics.MaxStraightCM,
					Fallback.Metrics.MinSelfApproachCells,
					Fallback.OrderedRoadCellIds.Num());
			}
		}
		if (!bFallbackBuilt)
		{
			OutPool.RejectReason =
				EABTSM3MonthlyRouteRejectReason::FallbackFailed;
			OutPool.RouteCandidatePoolHash = static_cast<int64>(
				ComputePoolHash(OutPool));
			OutFailure = FString::Printf(
				TEXT("Fallback:%s"),
				GetRejectReasonName(Fallback.RejectReason));
			return false;
		}
		OutPool.RetainedCandidates.Add(MoveTemp(Fallback));
	}

	OutPool.bRoutePoolValid = true;
	OutPool.bMonthlyWorldAccepted = false;
	OutPool.RejectReason =
		EABTSM3MonthlyRouteRejectReason::None;
	OutPool.RouteCandidatePoolHash = static_cast<int64>(
		ComputePoolHash(OutPool));
	EABTSM3MonthlyRouteRejectReason ValidationReason =
		EABTSM3MonthlyRouteRejectReason::None;
	if (!Validate(
		Config,
		Cells,
		PlanetRadiusCM,
		Context,
		OutPool,
		ValidationReason,
		OutFailure))
	{
		OutPool.bRoutePoolValid = false;
		OutPool.RejectReason = ValidationReason;
		OutPool.RouteCandidatePoolHash = static_cast<int64>(
			ComputePoolHash(OutPool));
		return false;
	}
	if (Config.bEmitRouteLogs)
	{
		LogSummary(OutPool);
	}
	return true;
}

bool FABTSM3MonthlyRouteBuilder::Validate(
	const FABTSM3MonthlyRouteConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	const FABTSM3MonthlyRoadContext& Context,
	const FABTSM3MonthlyRoutePool& Pool,
	EABTSM3MonthlyRouteRejectReason& OutReason,
	FString& OutFailure)
{
	using namespace ABTSM3R2RoutePrivate;
	OutReason = EABTSM3MonthlyRouteRejectReason::None;
	OutFailure.Reset();
	if (!ValidateConfig(Config, OutFailure)
		|| !FMath::IsFinite(PlanetRadiusCM)
		|| PlanetRadiusCM <= 0.0f)
	{
		OutReason =
			EABTSM3MonthlyRouteRejectReason::InvalidConfig;
		return false;
	}
	if (!ValidateTopology(Cells, OutFailure))
	{
		OutReason =
			EABTSM3MonthlyRouteRejectReason::InvalidTopology;
		return false;
	}
	if (!ValidateRoadContext(Context, Cells.Num(), OutFailure))
	{
		OutReason =
			EABTSM3MonthlyRouteRejectReason::InvalidContext;
		return false;
	}
	if (Pool.SchemaVersion != 1
		|| Pool.GeneratorVersion != GeneratorVersion
		|| Pool.LayoutPolicyVersion != MonthlyLayoutPolicyVersion
		|| Pool.bMonthlyWorldAccepted)
	{
		OutReason =
			EABTSM3MonthlyRouteRejectReason::InvalidConfig;
		OutFailure = TEXT("PoolIdentity");
		return false;
	}
	if (static_cast<uint64>(Pool.TopologyHash)
			!= ComputeTopologyHash(Cells)
		|| static_cast<uint64>(Pool.RouteConfigHash)
			!= ComputeConfigHash(
				Config,
				PlanetRadiusCM,
				static_cast<uint64>(Pool.TopologyHash))
		|| static_cast<uint64>(Pool.RoadContextHash)
			!= ComputeContextHash(Context))
	{
		OutReason =
			EABTSM3MonthlyRouteRejectReason::HashMismatch;
		OutFailure = TEXT("PoolInputHash");
		return false;
	}
	if (!Config.bBuildRouteObservation)
	{
		if (!Pool.bRoutePoolValid
			|| !Pool.RetainedCandidates.IsEmpty()
			|| !Pool.AttemptReports.IsEmpty()
			|| Pool.RejectReason
				!= EABTSM3MonthlyRouteRejectReason::NotEvaluated
			|| static_cast<uint64>(Pool.RouteCandidatePoolHash)
				!= ComputePoolHash(Pool))
		{
			OutReason =
				EABTSM3MonthlyRouteRejectReason::HashMismatch;
			OutFailure = TEXT("DisabledObservation");
			return false;
		}
		return true;
	}
	if (!Pool.bRoutePoolValid
		|| Pool.RejectReason
			!= EABTSM3MonthlyRouteRejectReason::None
		|| Pool.AttemptedCandidateCount
			!= Config.NormalCandidateSlots
		|| Pool.RetainedCandidates.IsEmpty()
		|| Pool.RetainedCandidates.Num()
			> Config.MaxRetainedCandidates
		|| (!Pool.bUsedRouteFallback
			&& Pool.NormalHardPassCount <= 0)
		|| (Pool.bUsedRouteFallback
			&& (Pool.RetainedCandidates.Num() != 1
				|| Pool.RetainedCandidates[0].Origin
					!= EABTSM3MonthlyRouteOrigin::
						MonthlyRouteFallback)))
	{
		OutReason =
			EABTSM3MonthlyRouteRejectReason::ProgressInvalid;
		OutFailure = TEXT("PoolCounts");
		return false;
	}
	int32 ReportedNormalHardPassCount = 0;
	if (Pool.AttemptReports.Num()
			< Config.NormalCandidateSlots
		|| (!Pool.bUsedRouteFallback
			&& Pool.AttemptReports.Num()
				!= Config.NormalCandidateSlots)
		|| (Pool.bUsedRouteFallback
			&& (Pool.AttemptReports.Num()
					<= Config.NormalCandidateSlots
				|| Pool.AttemptReports.Num()
					> Config.NormalCandidateSlots
						+ FMath::Min(
							8,
							Config.
								MaxCandidateBacktracksPerSeed
								+ 1))))
	{
		OutReason =
			EABTSM3MonthlyRouteRejectReason::ProgressInvalid;
		OutFailure = TEXT("AttemptReportCount");
		return false;
	}
	for (int32 ReportIndex = 0;
		ReportIndex < Pool.AttemptReports.Num();
		++ReportIndex)
	{
		const FABTSM3MonthlyRouteAttemptReport& Report =
			Pool.AttemptReports[ReportIndex];
		if (Report.ExpandedStates
				> Config.MaxExpandedStatesPerCandidate
			|| Report.Relaxations
				> Config.MaxRelaxationsPerCandidate
			|| Report.Backtracks
				> Config.MaxCandidateBacktracksPerSeed
			|| (ReportIndex < Config.NormalCandidateSlots
				&& (Report.CandidateId != ReportIndex
					|| Report.Origin
						!= EABTSM3MonthlyRouteOrigin::Normal))
			|| (ReportIndex >= Config.NormalCandidateSlots
				&& Report.Origin
					!= EABTSM3MonthlyRouteOrigin::
						MonthlyRouteFallback))
		{
			OutReason =
				EABTSM3MonthlyRouteRejectReason::ProgressInvalid;
			OutFailure = FString::Printf(
				TEXT("AttemptReport:%d"),
				ReportIndex);
			return false;
		}
		if (ReportIndex < Config.NormalCandidateSlots
			&& Report.bHardPass)
		{
			++ReportedNormalHardPassCount;
		}
	}
	if (ReportedNormalHardPassCount
			!= Pool.NormalHardPassCount
		|| (Pool.bUsedRouteFallback
			&& !Pool.AttemptReports.Last().bHardPass))
	{
		OutReason =
			EABTSM3MonthlyRouteRejectReason::ProgressInvalid;
		OutFailure = TEXT("AttemptReportSummary");
		return false;
	}
	for (int32 CandidateIndex = 0;
		CandidateIndex < Pool.RetainedCandidates.Num();
		++CandidateIndex)
	{
		FABTSM3MonthlyRouteCandidate Candidate =
			Pool.RetainedCandidates[CandidateIndex];
		int32 PreviousCorridorCell = INDEX_NONE;
		bool bCorridorValid =
			!Candidate.CorridorCellIds.IsEmpty();
		for (const int32 CorridorCellId :
			Candidate.CorridorCellIds)
		{
			if (!Cells.IsValidIndex(CorridorCellId)
				|| (PreviousCorridorCell != INDEX_NONE
					&& CorridorCellId <= PreviousCorridorCell))
			{
				bCorridorValid = false;
				break;
			}
			PreviousCorridorCell = CorridorCellId;
		}
		for (const int32 RouteCellId :
			Candidate.OrderedRoadCellIds)
		{
			if (Algo::BinarySearch(
					Candidate.CorridorCellIds,
					RouteCellId)
					== INDEX_NONE
				|| (Candidate.Origin
						== EABTSM3MonthlyRouteOrigin::Normal
					&& !IsCellTraversable(
						Context,
						RouteCellId)))
			{
				bCorridorValid = false;
				break;
			}
		}
		if (!Candidate.bHardPass
			|| Candidate.RejectReason
				!= EABTSM3MonthlyRouteRejectReason::None
			|| EvaluateCandidateGeometry(Cells, Config, Candidate)
				!= EABTSM3MonthlyRouteRejectReason::None
			|| Candidate.RouteScore
				!= ComputeRouteScore(Config, Candidate)
			|| Candidate.Metrics.AppliedReuseBonus < 0
			|| Candidate.Metrics.AppliedReuseBonus
				> Config.Costs.MaxReuseBonusPerCandidate
			|| !bCorridorValid
			|| static_cast<uint64>(Candidate.CandidateHash)
				!= ComputeCandidateHash(Candidate))
		{
			OutReason =
				EABTSM3MonthlyRouteRejectReason::HashMismatch;
			OutFailure = FString::Printf(
				TEXT("Candidate:%d"),
				CandidateIndex);
			return false;
		}
		if (CandidateIndex > 0
			&& CandidateLess(
				Pool.RetainedCandidates[CandidateIndex],
				Pool.RetainedCandidates[CandidateIndex - 1],
				Config))
		{
			OutReason =
				EABTSM3MonthlyRouteRejectReason::HashMismatch;
			OutFailure = TEXT("CandidateOrder");
			return false;
		}
	}
	if (static_cast<uint64>(Pool.RouteCandidatePoolHash)
		!= ComputePoolHash(Pool))
	{
		OutReason =
			EABTSM3MonthlyRouteRejectReason::HashMismatch;
		OutFailure = TEXT("PoolHash");
		return false;
	}
	return true;
}

bool FABTSM3MonthlyRouteBuilder::RebuildCandidateStrict(
	const int32 WorldSeed,
	const FABTSM3MonthlyRouteConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	const FABTSM3MonthlyRoadContext& Context,
	const FABTSM3MonthlyRouteCandidate& SourceCandidate,
	FABTSM3MonthlyRouteCandidate& OutCandidate,
	EABTSM3MonthlyRouteRejectReason& OutReason,
	FString& OutFailure)
{
	using namespace ABTSM3R2RoutePrivate;
	OutCandidate = FABTSM3MonthlyRouteCandidate();
	OutReason = EABTSM3MonthlyRouteRejectReason::None;
	OutFailure.Reset();
	if (!ValidateConfig(Config, OutFailure)
		|| !FMath::IsFinite(PlanetRadiusCM)
		|| PlanetRadiusCM <= 0.0f)
	{
		OutReason = EABTSM3MonthlyRouteRejectReason::InvalidConfig;
		return false;
	}
	if (!ValidateTopology(Cells, OutFailure))
	{
		OutReason = EABTSM3MonthlyRouteRejectReason::InvalidTopology;
		return false;
	}
	if (!ValidateRoadContext(Context, Cells.Num(), OutFailure))
	{
		OutReason = EABTSM3MonthlyRouteRejectReason::InvalidContext;
		return false;
	}
	if (!SourceCandidate.bHardPass
		|| SourceCandidate.RejectReason
			!= EABTSM3MonthlyRouteRejectReason::None
		|| SourceCandidate.CandidateId < 0
		|| SourceCandidate.ControlCellIds.Num()
			!= Config.SkeletonControlPointCount
		|| static_cast<uint64>(SourceCandidate.CandidateHash)
			!= ComputeCandidateHash(SourceCandidate))
	{
		OutReason = EABTSM3MonthlyRouteRejectReason::HashMismatch;
		OutFailure = TEXT("StrictSourceCandidate");
		return false;
	}

	const bool bBuilt = BuildCandidate(
		WorldSeed,
		SourceCandidate.CandidateId,
		SourceCandidate.Origin,
		Config,
		Cells,
		PlanetRadiusCM,
		Context,
		OutCandidate);
	if (OutCandidate.ControlCellIds
		!= SourceCandidate.ControlCellIds)
	{
		OutCandidate.bHardPass = false;
		OutCandidate.RejectReason =
			EABTSM3MonthlyRouteRejectReason::HashMismatch;
		OutCandidate.CandidateHash = static_cast<int64>(
			ComputeCandidateHash(OutCandidate));
		OutReason = EABTSM3MonthlyRouteRejectReason::HashMismatch;
		OutFailure = TEXT("StrictSkeletonIdentity");
		return false;
	}
	if (!bBuilt)
	{
		OutReason = OutCandidate.RejectReason;
		OutFailure = FString::Printf(
			TEXT("StrictRoad:%s"),
			GetRejectReasonName(OutReason));
		return false;
	}
	OutCandidate.Backtracks = SourceCandidate.Backtracks;
	OutCandidate.CandidateHash = static_cast<int64>(
		ComputeCandidateHash(OutCandidate));
	for (const int32 CellId : OutCandidate.OrderedRoadCellIds)
	{
		if (!IsCellTraversable(Context, CellId))
		{
			OutCandidate.bHardPass = false;
			OutCandidate.RejectReason =
				EABTSM3MonthlyRouteRejectReason::HardBlocked;
			OutCandidate.CandidateHash = static_cast<int64>(
				ComputeCandidateHash(OutCandidate));
			OutReason =
				EABTSM3MonthlyRouteRejectReason::HardBlocked;
			OutFailure = FString::Printf(
				TEXT("StrictContextCell:%d"),
				CellId);
			return false;
		}
	}
	return true;
}

void FABTSM3MonthlyRouteBuilder::BuildDebugData(
	const FABTSM3MonthlyRoutePool& Pool,
	FABTSM3MonthlyRouteDebugData& OutDebugData)
{
	OutDebugData = FABTSM3MonthlyRouteDebugData();
	if (Pool.RetainedCandidates.IsEmpty())
	{
		return;
	}
	const FABTSM3MonthlyRouteCandidate& Best =
		Pool.RetainedCandidates[0];
	OutDebugData.BestRouteCellIds = Best.OrderedRoadCellIds;
	OutDebugData.BestControlCellIds = Best.ControlCellIds;
	OutDebugData.BestCorridorCellIds = Best.CorridorCellIds;
	OutDebugData.BestScenicBendCount =
		Best.Metrics.ScenicBendCount;
	OutDebugData.BestRouteLengthCM = Best.Metrics.RouteLengthCM;
}

void FABTSM3MonthlyRouteBuilder::LogSummary(
	const FABTSM3MonthlyRoutePool& Pool)
{
	const FABTSM3MonthlyRouteCandidate* Best =
		Pool.RetainedCandidates.IsEmpty()
		? nullptr
		: &Pool.RetainedCandidates[0];
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][PCG][MonthlyRoute] Stage=M3R2 Policy=%d Seed=%d Attempted=%d NormalHardPass=%d Retained=%d RouteFallback=%d MonthlyWorldAccepted=%d BestLengthCM=%d BestBends=%d BestStraightCM=%d BestSelfApproachCells=%d BestScore=%d PoolHash=%016llX SnapshotHash=%016llX"),
		Pool.LayoutPolicyVersion,
		Pool.WorldSeed,
		Pool.AttemptedCandidateCount,
		Pool.NormalHardPassCount,
		Pool.RetainedCandidates.Num(),
		Pool.bUsedRouteFallback ? 1 : 0,
		Pool.bMonthlyWorldAccepted ? 1 : 0,
		Best ? Best->Metrics.RouteLengthCM : 0,
		Best ? Best->Metrics.ScenicBendCount : 0,
		Best ? Best->Metrics.MaxStraightCM : 0,
		Best ? Best->Metrics.MinSelfApproachCells : 0,
		Best ? Best->RouteScore : 0,
		static_cast<unsigned long long>(
			Pool.RouteCandidatePoolHash),
		static_cast<unsigned long long>(
			ComputePoolSnapshotHash(Pool)));
}

const TCHAR* FABTSM3MonthlyRouteBuilder::GetRejectReasonName(
	const EABTSM3MonthlyRouteRejectReason Reason)
{
	switch (Reason)
	{
	case EABTSM3MonthlyRouteRejectReason::None:
		return TEXT("None");
	case EABTSM3MonthlyRouteRejectReason::NotEvaluated:
		return TEXT("NotEvaluated");
	case EABTSM3MonthlyRouteRejectReason::InvalidConfig:
		return TEXT("InvalidConfig");
	case EABTSM3MonthlyRouteRejectReason::InvalidTopology:
		return TEXT("InvalidTopology");
	case EABTSM3MonthlyRouteRejectReason::InvalidContext:
		return TEXT("InvalidContext");
	case EABTSM3MonthlyRouteRejectReason::SkeletonGenerationFailed:
		return TEXT("SkeletonGenerationFailed");
	case EABTSM3MonthlyRouteRejectReason::AntipodalInstability:
		return TEXT("AntipodalInstability");
	case EABTSM3MonthlyRouteRejectReason::HardBlocked:
		return TEXT("HardBlocked");
	case EABTSM3MonthlyRouteRejectReason::CorridorDisconnected:
		return TEXT("CorridorDisconnected");
	case EABTSM3MonthlyRouteRejectReason::SearchBudgetExceeded:
		return TEXT("SearchBudgetExceeded");
	case EABTSM3MonthlyRouteRejectReason::NonAdjacentPath:
		return TEXT("NonAdjacentPath");
	case EABTSM3MonthlyRouteRejectReason::RepeatedCell:
		return TEXT("RepeatedCell");
	case EABTSM3MonthlyRouteRejectReason::RouteLengthOutOfRange:
		return TEXT("RouteLengthOutOfRange");
	case EABTSM3MonthlyRouteRejectReason::InsufficientScenicBends:
		return TEXT("InsufficientScenicBends");
	case EABTSM3MonthlyRouteRejectReason::StraightRunTooLong:
		return TEXT("StraightRunTooLong");
	case EABTSM3MonthlyRouteRejectReason::NonLocalSelfApproach:
		return TEXT("NonLocalSelfApproach");
	case EABTSM3MonthlyRouteRejectReason::ProgressInvalid:
		return TEXT("ProgressInvalid");
	case EABTSM3MonthlyRouteRejectReason::AllNormalCandidatesFailed:
		return TEXT("AllNormalCandidatesFailed");
	case EABTSM3MonthlyRouteRejectReason::FallbackFailed:
		return TEXT("FallbackFailed");
	case EABTSM3MonthlyRouteRejectReason::HashMismatch:
		return TEXT("HashMismatch");
	default:
		return TEXT("Unknown");
	}
}
