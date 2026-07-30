// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3MonthlySlingshotField.h"

#include "ABTSRuntime.h"
#include "Planet/ABTSM2Planet.h"

namespace ABTSM3R31SlingshotFieldPrivate
{
constexpr uint64 Fnv1a64OffsetBasis = 14695981039346656037ull;
constexpr uint64 Fnv1a64Prime = 1099511628211ull;
constexpr int32 FlowQuantization = 1000000;
constexpr int32 FieldSearchRadiusCells = 3;
constexpr int32 RoadEndClearanceCM = 1200;
constexpr int32 RoadFieldProgressClearanceCM = 900;
constexpr int32 EncounterFieldIdBase = 360000;
constexpr int32 RoadFieldIdBase = 370000;

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

	void AddInt64(const int64 Value)
	{
		AddUInt64(static_cast<uint64>(Value));
	}

	void AddInt32(const int32 Value)
	{
		AddUInt64(static_cast<uint32>(Value));
	}

	void AddBool(const bool bValue)
	{
		AddUInt64(bValue ? 1ull : 0ull);
	}

	uint64 Get() const
	{
		return Hash;
	}

private:
	uint64 Hash = Fnv1a64OffsetBasis;
};

struct FCellRank
{
	int32 CellId = INDEX_NONE;
	int32 GraphDistance = MAX_int32;
	int32 ChordDistanceMM = MAX_int32;
	uint64 StableRank = 0;
};

struct FRouteIndexRank
{
	int32 RouteIndex = INDEX_NONE;
	int32 TargetDeltaCM = MAX_int32;
	int32 CellId = INDEX_NONE;
};

bool ValidateConfig(
	const FABTSM3MonthlySlingshotFieldConfig& Config,
	FString& OutFailure)
{
	if (Config.AdditionalSlotsPerOrdinaryField < 0
		|| Config.AdditionalSlotsPerOrdinaryField > 10)
	{
		OutFailure = TEXT("AdditionalSlotsPerOrdinaryField");
		return false;
	}
	if (Config.AdditionalRoadFieldCount < 0
		|| Config.AdditionalRoadFieldCount > 12)
	{
		OutFailure = TEXT("AdditionalRoadFieldCount");
		return false;
	}
	if (Config.MaxCordLengthCM < 100
		|| Config.MaxCordLengthCM > 4000)
	{
		OutFailure = TEXT("MaxCordLengthCM");
		return false;
	}
	if (Config.FieldPlannerVersion != 1
		|| Config.SlotSelectionVersion != 1)
	{
		OutFailure = TEXT("PlannerVersion");
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
		if (Cell.UnitCenter.ContainsNaN()
			|| !Cell.UnitCenter.IsNormalized()
			|| Cell.NeighborCellIds.Num() < 3)
		{
			return false;
		}
		int32 PreviousNeighbor = INDEX_NONE;
		for (const int32 NeighborId : Cell.NeighborCellIds)
		{
			if (!Cells.IsValidIndex(NeighborId)
				|| NeighborId == CellId
				|| NeighborId <= PreviousNeighbor
				|| !Cells[NeighborId].NeighborCellIds.Contains(CellId))
			{
				return false;
			}
			PreviousNeighbor = NeighborId;
		}
	}
	return true;
}

uint64 StableCellRank(
	const int32 WorldSeed,
	const int64 SourceCandidateHash,
	const int32 FieldId,
	const int32 CellId)
{
	FCanonicalHash64 Hash;
	Hash.AddInt32(WorldSeed);
	Hash.AddInt64(SourceCandidateHash);
	Hash.AddInt32(FieldId);
	Hash.AddInt32(CellId);
	return Hash.Get();
}

float ChordDistanceCM(
	const TArray<FABTSM2Cell>& Cells,
	const int32 CellA,
	const int32 CellB,
	const float PlanetRadiusCM)
{
	if (!Cells.IsValidIndex(CellA) || !Cells.IsValidIndex(CellB))
	{
		return TNumericLimits<float>::Max();
	}
	return FVector::Distance(
		Cells[CellA].UnitCenter,
		Cells[CellB].UnitCenter) * PlanetRadiusCM;
}

void BuildGraphDistances(
	const TArray<FABTSM2Cell>& Cells,
	const int32 SeedCellId,
	const int32 MaxDistance,
	TArray<int32>& OutDistances)
{
	OutDistances.Init(INDEX_NONE, Cells.Num());
	if (!Cells.IsValidIndex(SeedCellId) || MaxDistance < 0)
	{
		return;
	}
	TArray<int32> Queue;
	Queue.Reserve(64);
	Queue.Add(SeedCellId);
	OutDistances[SeedCellId] = 0;
	for (int32 ReadIndex = 0; ReadIndex < Queue.Num(); ++ReadIndex)
	{
		const int32 CellId = Queue[ReadIndex];
		const int32 Distance = OutDistances[CellId];
		if (Distance >= MaxDistance)
		{
			continue;
		}
		for (const int32 NeighborId : Cells[CellId].NeighborCellIds)
		{
			if (OutDistances[NeighborId] == INDEX_NONE)
			{
				OutDistances[NeighborId] = Distance + 1;
				Queue.Add(NeighborId);
			}
		}
	}
}

const FABTSM3PocketContract* FindPocket(
	const FABTSM3MonthlySpatialCandidate& Candidate,
	const int32 PocketId)
{
	return Candidate.Pockets.FindByPredicate(
		[PocketId](const FABTSM3PocketContract& Pocket)
		{
			return Pocket.PocketId == PocketId;
		});
}

void BuildEncounterEnvelopeCells(
	const FABTSM3MonthlySpatialCandidate& Candidate,
	const int32 EncounterId,
	TSet<int32>& OutCells)
{
	OutCells.Reset();
	for (const FABTSM3PlayableEnvelope& Envelope :
		Candidate.PlayableEnvelopes)
	{
		if (Envelope.EncounterId != EncounterId)
		{
			continue;
		}
		for (const FABTSM3PlayableCellRole& Role : Envelope.Cells)
		{
			OutCells.Add(Role.CellId);
		}
	}
}

bool IsEligibleSlotCell(
	const FABTSM3MonthlySpatialCandidate& Candidate,
	const int32 CellId,
	const TSet<int32>& RoadCellIds,
	const TSet<int32>& UsedCellIds,
	const TSet<int32>* AllowedEnvelopeCells,
	const bool bExcludeNoRoad,
	const bool bExcludeAttackCorridor)
{
	if (!Candidate.Cells.IsValidIndex(CellId)
		|| Candidate.Cells[CellId].CellId != CellId
		|| UsedCellIds.Contains(CellId)
		|| RoadCellIds.Contains(CellId)
		|| (AllowedEnvelopeCells != nullptr
			&& !AllowedEnvelopeCells->Contains(CellId)))
	{
		return false;
	}
	const FABTSM3MonthlySpatialCell& Cell = Candidate.Cells[CellId];
	return Cell.PrimaryEnvelopeId != INDEX_NONE
		&& Cell.MainRoadDistanceCells > 0
		&& !Cell.bWater
		&& !Cell.bTargetFootprint
		&& (!bExcludeNoRoad || !Cell.bNoRoad)
		&& (!bExcludeAttackCorridor || !Cell.bAttackCorridor);
}

int32 CountDistanceReachablePairs(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<int32>& SlotCellIds,
	const float PlanetRadiusCM,
	const int32 MaxCordLengthCM)
{
	int32 Count = 0;
	for (int32 A = 0; A < SlotCellIds.Num(); ++A)
	{
		for (int32 B = A + 1; B < SlotCellIds.Num(); ++B)
		{
			if (ChordDistanceCM(
					Cells,
					SlotCellIds[A],
					SlotCellIds[B],
					PlanetRadiusCM)
				<= static_cast<float>(MaxCordLengthCM))
			{
				++Count;
			}
		}
	}
	return Count;
}

bool BuildFieldSlots(
	const int32 WorldSeed,
	const FABTSM3MonthlySlingshotFieldConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	const FABTSM3MonthlySpatialCandidate& Candidate,
	const TSet<int32>& RoadCellIds,
	const int32 SearchSeedCellId,
	const int32 RequiredAnchorCellId,
	const TSet<int32>* AllowedEnvelopeCells,
	const bool bExcludeNoRoad,
	const bool bExcludeAttackCorridor,
	FABTSM3MonthlySlingshotField& InOutField,
	TSet<int32>& InOutUsedCellIds,
	FString& OutFailure)
{
	const int32 RequiredSlotCount =
		FABTSM3MonthlySlingshotFieldBuilder::BaseSlotsPerOrdinaryField
		+ Config.AdditionalSlotsPerOrdinaryField;
	TArray<int32> SeedDistances;
	BuildGraphDistances(
		Cells,
		SearchSeedCellId,
		FieldSearchRadiusCells,
		SeedDistances);
	TArray<FCellRank> AnchorCandidates;
	for (int32 CellId = 0; CellId < SeedDistances.Num(); ++CellId)
	{
		if (SeedDistances[CellId] == INDEX_NONE
			|| !IsEligibleSlotCell(
				Candidate,
				CellId,
				RoadCellIds,
				InOutUsedCellIds,
				AllowedEnvelopeCells,
				bExcludeNoRoad,
				bExcludeAttackCorridor))
		{
			continue;
		}
		FCellRank& Ranked = AnchorCandidates.AddDefaulted_GetRef();
		Ranked.CellId = CellId;
		Ranked.GraphDistance = SeedDistances[CellId];
		Ranked.StableRank = StableCellRank(
			WorldSeed,
			Candidate.SpatialCandidateHash,
			InOutField.FieldId,
			CellId);
	}
	AnchorCandidates.Sort(
		[](const FCellRank& A, const FCellRank& B)
		{
			if (A.GraphDistance != B.GraphDistance)
			{
				return A.GraphDistance < B.GraphDistance;
			}
			if (A.StableRank != B.StableRank)
			{
				return A.StableRank < B.StableRank;
			}
			return A.CellId < B.CellId;
		});

	int32 AnchorCellId = RequiredAnchorCellId;
	if (AnchorCellId == INDEX_NONE)
	{
		AnchorCellId = AnchorCandidates.IsEmpty()
			? INDEX_NONE
			: AnchorCandidates[0].CellId;
	}
	if (AnchorCellId == INDEX_NONE
		|| !IsEligibleSlotCell(
			Candidate,
			AnchorCellId,
			RoadCellIds,
			InOutUsedCellIds,
			AllowedEnvelopeCells,
			bExcludeNoRoad,
			bExcludeAttackCorridor))
	{
		const FABTSM3MonthlySpatialCell* AnchorState =
			Candidate.Cells.IsValidIndex(AnchorCellId)
				? &Candidate.Cells[AnchorCellId]
				: nullptr;
		OutFailure = FString::Printf(
			TEXT("FieldAnchor:%d:%d Cell=%d Used=%d Road=%d Envelope=%d Primary=%d RoadDistance=%d Water=%d Footprint=%d NoRoad=%d Attack=%d ExcludeNoRoad=%d ExcludeAttack=%d"),
			InOutField.FieldId,
			AnchorCellId,
			AnchorState != nullptr
				&& AnchorState->CellId == AnchorCellId
				? 1 : 0,
			InOutUsedCellIds.Contains(AnchorCellId) ? 1 : 0,
			RoadCellIds.Contains(AnchorCellId) ? 1 : 0,
			AllowedEnvelopeCells == nullptr
				|| AllowedEnvelopeCells->Contains(AnchorCellId)
				? 1 : 0,
			AnchorState != nullptr
				&& AnchorState->PrimaryEnvelopeId != INDEX_NONE
				? 1 : 0,
			AnchorState != nullptr
				? AnchorState->MainRoadDistanceCells
				: INDEX_NONE,
			AnchorState != nullptr && AnchorState->bWater ? 1 : 0,
			AnchorState != nullptr
				&& AnchorState->bTargetFootprint ? 1 : 0,
			AnchorState != nullptr && AnchorState->bNoRoad ? 1 : 0,
			AnchorState != nullptr
				&& AnchorState->bAttackCorridor ? 1 : 0,
			bExcludeNoRoad ? 1 : 0,
			bExcludeAttackCorridor ? 1 : 0);
		return false;
	}

	TArray<int32> AnchorDistances;
	BuildGraphDistances(
		Cells,
		AnchorCellId,
		FieldSearchRadiusCells,
		AnchorDistances);
	TArray<FCellRank> SlotCandidates;
	for (int32 CellId = 0; CellId < AnchorDistances.Num(); ++CellId)
	{
		if (CellId == AnchorCellId
			|| AnchorDistances[CellId] == INDEX_NONE
			|| !IsEligibleSlotCell(
				Candidate,
				CellId,
				RoadCellIds,
				InOutUsedCellIds,
				AllowedEnvelopeCells,
				bExcludeNoRoad,
				bExcludeAttackCorridor))
		{
			continue;
		}
		const float DistanceCM = ChordDistanceCM(
			Cells,
			AnchorCellId,
			CellId,
			PlanetRadiusCM);
		if (DistanceCM > static_cast<float>(Config.MaxCordLengthCM))
		{
			continue;
		}
		FCellRank& Ranked = SlotCandidates.AddDefaulted_GetRef();
		Ranked.CellId = CellId;
		Ranked.GraphDistance = AnchorDistances[CellId];
		Ranked.ChordDistanceMM =
			FMath::RoundToInt(DistanceCM * 10.0f);
		Ranked.StableRank = StableCellRank(
			WorldSeed,
			Candidate.SpatialCandidateHash,
			InOutField.FieldId,
			CellId);
	}
	if (SlotCandidates.Num() < RequiredSlotCount - 1)
	{
		OutFailure = FString::Printf(
			TEXT("FieldCapacity:%d:%d/%d"),
			InOutField.FieldId,
			SlotCandidates.Num() + 1,
			RequiredSlotCount);
		return false;
	}

	// The closest second slot preserves the old two-slot baseline. Remaining
	// slots use a seeded rank to create an irregular compact scatter.
	SlotCandidates.Sort(
		[](const FCellRank& A, const FCellRank& B)
		{
			if (A.ChordDistanceMM != B.ChordDistanceMM)
			{
				return A.ChordDistanceMM < B.ChordDistanceMM;
			}
			if (A.StableRank != B.StableRank)
			{
				return A.StableRank < B.StableRank;
			}
			return A.CellId < B.CellId;
		});
	const int32 BaselineSecondCellId = SlotCandidates[0].CellId;
	SlotCandidates.RemoveAt(0);
	SlotCandidates.Sort(
		[](const FCellRank& A, const FCellRank& B)
		{
			if (A.StableRank != B.StableRank)
			{
				return A.StableRank < B.StableRank;
			}
			if (A.GraphDistance != B.GraphDistance)
			{
				return A.GraphDistance < B.GraphDistance;
			}
			return A.CellId < B.CellId;
		});

	InOutField.AnchorCellId = AnchorCellId;
	InOutField.SlotCellIds.Reset(RequiredSlotCount);
	InOutField.SlotCellIds.Add(AnchorCellId);
	InOutField.SlotCellIds.Add(BaselineSecondCellId);
	for (const FCellRank& Ranked : SlotCandidates)
	{
		if (InOutField.SlotCellIds.Num() >= RequiredSlotCount)
		{
			break;
		}
		InOutField.SlotCellIds.Add(Ranked.CellId);
	}
	if (InOutField.SlotCellIds.Num() != RequiredSlotCount)
	{
		OutFailure = FString::Printf(
			TEXT("FieldSelection:%d:%d/%d"),
			InOutField.FieldId,
			InOutField.SlotCellIds.Num(),
			RequiredSlotCount);
		return false;
	}
	InOutField.DistanceReachablePairCount =
		CountDistanceReachablePairs(
			Cells,
			InOutField.SlotCellIds,
			PlanetRadiusCM,
			Config.MaxCordLengthCM);
	if (InOutField.DistanceReachablePairCount
		< RequiredSlotCount - 1)
	{
		OutFailure = FString::Printf(
			TEXT("FieldConnectivity:%d:%d"),
			InOutField.FieldId,
			InOutField.DistanceReachablePairCount);
		return false;
	}
	for (const int32 CellId : InOutField.SlotCellIds)
	{
		InOutUsedCellIds.Add(CellId);
	}
	InOutField.FieldHash = static_cast<int64>(
		FABTSM3MonthlySlingshotFieldBuilder::ComputeFieldHash(
			InOutField));
	return true;
}

bool BuildCandidate(
	const int32 WorldSeed,
	const FABTSM3MonthlySlingshotFieldConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	const FABTSM3MonthlySpatialCandidate& Source,
	FABTSM3MonthlySlingshotFieldCandidate& OutCandidate,
	FString& OutFailure)
{
	OutCandidate = FABTSM3MonthlySlingshotFieldCandidate();
	OutCandidate.SourceRouteCandidateId =
		Source.SourceRouteCandidateId;
	OutCandidate.SourceSpatialCandidateHash =
		Source.SpatialCandidateHash;
	if (!Source.bHardPass
		|| Source.RejectReason
			!= EABTSM3MonthlySpatialRejectReason::None
		|| Source.Encounters.Num()
			!= FABTSM3MonthlySlingshotFieldBuilder::
				RequiredEncounterFieldCount
		|| Source.Cells.Num() != Cells.Num()
		|| static_cast<uint64>(Source.SpatialCandidateHash)
			!= FABTSM3MonthlyEncounterBuilder::ComputeCandidateHash(
				Source))
	{
		OutFailure = TEXT("SourceCandidateIdentity");
		return false;
	}

	TSet<int32> RoadCellIds;
	for (const int32 CellId :
		Source.RecomputedRoute.OrderedRoadCellIds)
	{
		RoadCellIds.Add(CellId);
	}
	TSet<int32> UsedCellIds;
	TArray<const FABTSM3MonthlySpatialEncounter*> Encounters;
	for (const FABTSM3MonthlySpatialEncounter& Encounter :
		Source.Encounters)
	{
		Encounters.Add(&Encounter);
	}
	Encounters.Sort(
		[](const FABTSM3MonthlySpatialEncounter& A,
			const FABTSM3MonthlySpatialEncounter& B)
		{
			if (A.Contract.OrderIndex != B.Contract.OrderIndex)
			{
				return A.Contract.OrderIndex < B.Contract.OrderIndex;
			}
			return A.Contract.EncounterId < B.Contract.EncounterId;
		});

	TArray<int32> ReservedProgressCM;
	for (const FABTSM3MonthlySpatialEncounter* Encounter :
		Encounters)
	{
		if (Encounter == nullptr)
		{
			OutFailure = TEXT("NullEncounter");
			return false;
		}
		const FABTSM3PocketContract* SlingshotPocket =
			FindPocket(
				Source,
				Encounter->Contract.SlingshotPocketId);
		if (SlingshotPocket == nullptr)
		{
			OutFailure = FString::Printf(
				TEXT("MissingSlingshotPocket:%d"),
				Encounter->Contract.EncounterId);
			return false;
		}
		TSet<int32> EncounterEnvelopeCells;
		BuildEncounterEnvelopeCells(
			Source,
			Encounter->Contract.EncounterId,
			EncounterEnvelopeCells);
		FABTSM3MonthlySlingshotField Field;
		Field.FieldId =
			EncounterFieldIdBase
			+ Encounter->Contract.OrderIndex;
		Field.Kind =
			EABTSM3MonthlySlingshotFieldKind::EncounterRequired;
		Field.EncounterId = Encounter->Contract.EncounterId;
		Field.SourcePocketAnchorCellId =
			SlingshotPocket->AnchorCellId;
		Field.FlowQ = Encounter->FlowQ;
		if (!BuildFieldSlots(
				WorldSeed,
				Config,
				Cells,
				PlanetRadiusCM,
				Source,
				RoadCellIds,
				SlingshotPocket->AnchorCellId,
				INDEX_NONE,
				&EncounterEnvelopeCells,
				false,
				false,
				Field,
				UsedCellIds,
				OutFailure))
		{
			return false;
		}
		const int32 RouteIndex =
			Source.Cells[Field.AnchorCellId].
				NearestRoadOrderIndex;
		if (Source.RecomputedRoute.ProgressDistanceCM.IsValidIndex(
				RouteIndex))
		{
			ReservedProgressCM.Add(
				Source.RecomputedRoute.ProgressDistanceCM[
					RouteIndex]);
		}
		OutCandidate.TotalSlotCount +=
			Field.SlotCellIds.Num();
		OutCandidate.Fields.Add(MoveTemp(Field));
	}

	const FABTSM3MonthlyRouteCandidate& Route =
		Source.RecomputedRoute;
	if (Config.AdditionalRoadFieldCount > 0
		&& (Route.OrderedRoadCellIds.IsEmpty()
			|| Route.OrderedRoadCellIds.Num()
				!= Route.ProgressDistanceCM.Num()
			|| Route.Metrics.RouteLengthCM
				<= RoadEndClearanceCM * 2))
	{
		OutFailure = TEXT("RoadIdentity");
		return false;
	}
	int32 LastRoadFieldProgressCM = INDEX_NONE;
	for (int32 RoadFieldIndex = 0;
		RoadFieldIndex < Config.AdditionalRoadFieldCount;
		++RoadFieldIndex)
	{
		const int32 TargetProgressCM =
			static_cast<int32>(
				static_cast<int64>(
					Route.Metrics.RouteLengthCM)
				* (RoadFieldIndex + 1)
				/ (Config.AdditionalRoadFieldCount + 1));
		TArray<FRouteIndexRank> RouteRanks;
		for (int32 RouteIndex = 0;
			RouteIndex < Route.OrderedRoadCellIds.Num();
			++RouteIndex)
		{
			const int32 ProgressCM =
				Route.ProgressDistanceCM[RouteIndex];
			if (ProgressCM < RoadEndClearanceCM
				|| ProgressCM
					> Route.Metrics.RouteLengthCM
						- RoadEndClearanceCM)
			{
				continue;
			}
			bool bProgressClear = true;
			for (const int32 ReservedCM : ReservedProgressCM)
			{
				if (FMath::Abs(ProgressCM - ReservedCM)
					< RoadFieldProgressClearanceCM)
				{
					bProgressClear = false;
					break;
				}
			}
			if (!bProgressClear)
			{
				continue;
			}
			FRouteIndexRank& Ranked =
				RouteRanks.AddDefaulted_GetRef();
			Ranked.RouteIndex = RouteIndex;
			Ranked.TargetDeltaCM =
				FMath::Abs(ProgressCM - TargetProgressCM);
			Ranked.CellId =
				Route.OrderedRoadCellIds[RouteIndex];
		}
		RouteRanks.Sort(
			[](const FRouteIndexRank& A,
				const FRouteIndexRank& B)
			{
				if (A.TargetDeltaCM != B.TargetDeltaCM)
				{
					return A.TargetDeltaCM < B.TargetDeltaCM;
				}
				if (A.CellId != B.CellId)
				{
					return A.CellId < B.CellId;
				}
				return A.RouteIndex < B.RouteIndex;
			});

		bool bBuiltRoadField = false;
		FString LastRoadFailure = TEXT("NoRoadCandidate");
		for (const FRouteIndexRank& Ranked : RouteRanks)
		{
			FABTSM3MonthlySlingshotField Field;
			Field.FieldId =
				RoadFieldIdBase + RoadFieldIndex;
			Field.Kind =
				EABTSM3MonthlySlingshotFieldKind::RoadAuxiliary;
			Field.EncounterId = INDEX_NONE;
			TSet<int32> TrialUsedCellIds = UsedCellIds;
			if (!BuildFieldSlots(
				WorldSeed,
				Config,
				Cells,
				PlanetRadiusCM,
				Source,
				RoadCellIds,
				Ranked.CellId,
				INDEX_NONE,
				nullptr,
				true,
				true,
				Field,
				TrialUsedCellIds,
				LastRoadFailure))
			{
				continue;
			}
			const int32 AnchorRouteIndex =
				Source.Cells[Field.AnchorCellId].
					NearestRoadOrderIndex;
			if (!Route.ProgressDistanceCM.IsValidIndex(
					AnchorRouteIndex))
			{
				LastRoadFailure = TEXT("AnchorRoadProgress");
				continue;
			}
			const int32 ActualProgressCM =
				Route.ProgressDistanceCM[AnchorRouteIndex];
			if (ActualProgressCM < RoadEndClearanceCM
				|| ActualProgressCM
					> Route.Metrics.RouteLengthCM
						- RoadEndClearanceCM
				|| (LastRoadFieldProgressCM != INDEX_NONE
					&& ActualProgressCM
						<= LastRoadFieldProgressCM
							+ RoadFieldProgressClearanceCM))
			{
				LastRoadFailure =
					TEXT("AnchorProgressClearance");
				continue;
			}
			bool bActualProgressClear = true;
			for (const int32 ReservedCM : ReservedProgressCM)
			{
				if (FMath::Abs(
						ActualProgressCM - ReservedCM)
					< RoadFieldProgressClearanceCM)
				{
					bActualProgressClear = false;
					break;
				}
			}
			if (!bActualProgressClear)
			{
				LastRoadFailure =
					TEXT("AnchorReservedProgress");
				continue;
			}
			Field.FlowQ = static_cast<int32>(
				static_cast<int64>(ActualProgressCM)
				* FlowQuantization
				/ Route.Metrics.RouteLengthCM);
			Field.FieldHash = static_cast<int64>(
				FABTSM3MonthlySlingshotFieldBuilder::
					ComputeFieldHash(Field));
			UsedCellIds = MoveTemp(TrialUsedCellIds);
			ReservedProgressCM.Add(ActualProgressCM);
			LastRoadFieldProgressCM = ActualProgressCM;
			OutCandidate.TotalSlotCount +=
				Field.SlotCellIds.Num();
			OutCandidate.Fields.Add(MoveTemp(Field));
			bBuiltRoadField = true;
			break;
		}
		if (!bBuiltRoadField)
		{
			OutFailure = FString::Printf(
				TEXT("RoadField:%d:%s"),
				RoadFieldIndex,
				*LastRoadFailure);
			return false;
		}
	}

	const int32 ExpectedFieldCount =
		FABTSM3MonthlySlingshotFieldBuilder::
			RequiredEncounterFieldCount
		+ Config.AdditionalRoadFieldCount;
	const int32 ExpectedSlotsPerField =
		FABTSM3MonthlySlingshotFieldBuilder::
			BaseSlotsPerOrdinaryField
		+ Config.AdditionalSlotsPerOrdinaryField;
	if (OutCandidate.Fields.Num() != ExpectedFieldCount
		|| OutCandidate.TotalSlotCount
			!= ExpectedFieldCount * ExpectedSlotsPerField
		|| UsedCellIds.Num() != OutCandidate.TotalSlotCount)
	{
		OutFailure = TEXT("CandidateCounts");
		return false;
	}
	OutCandidate.CandidateHash = static_cast<int64>(
		FABTSM3MonthlySlingshotFieldBuilder::
			ComputeCandidateHash(OutCandidate));
	return true;
}
}

uint64 FABTSM3MonthlySlingshotFieldBuilder::ComputeConfigHash(
	const FABTSM3MonthlySlingshotFieldConfig& Config,
	const float PlanetRadiusCM,
	const uint64 TopologyHash)
{
	using namespace ABTSM3R31SlingshotFieldPrivate;
	FCanonicalHash64 Hash;
	Hash.AddInt32(SchemaVersion);
	Hash.AddInt32(GeneratorVersion);
	Hash.AddInt32(MonthlyLayoutPolicyVersion);
	Hash.AddUInt64(TopologyHash);
	Hash.AddInt32(FMath::RoundToInt(PlanetRadiusCM * 1000.0f));
	Hash.AddBool(Config.bBuildSlingshotFields);
	Hash.AddInt32(Config.AdditionalSlotsPerOrdinaryField);
	Hash.AddInt32(Config.AdditionalRoadFieldCount);
	Hash.AddInt32(Config.MaxCordLengthCM);
	Hash.AddInt32(Config.FieldPlannerVersion);
	Hash.AddInt32(Config.SlotSelectionVersion);
	return Hash.Get();
}

uint64 FABTSM3MonthlySlingshotFieldBuilder::ComputeFieldHash(
	const FABTSM3MonthlySlingshotField& Field)
{
	using namespace ABTSM3R31SlingshotFieldPrivate;
	FCanonicalHash64 Hash;
	Hash.AddInt32(Field.FieldId);
	Hash.AddInt32(static_cast<int32>(Field.Kind));
	Hash.AddInt32(Field.EncounterId);
	Hash.AddInt32(Field.SourcePocketAnchorCellId);
	Hash.AddInt32(Field.AnchorCellId);
	Hash.AddInt32(Field.FlowQ);
	Hash.AddInt32(Field.SlotCellIds.Num());
	for (const int32 CellId : Field.SlotCellIds)
	{
		Hash.AddInt32(CellId);
	}
	Hash.AddInt32(Field.DistanceReachablePairCount);
	return Hash.Get();
}

uint64 FABTSM3MonthlySlingshotFieldBuilder::ComputeCandidateHash(
	const FABTSM3MonthlySlingshotFieldCandidate& Candidate)
{
	using namespace ABTSM3R31SlingshotFieldPrivate;
	FCanonicalHash64 Hash;
	Hash.AddInt32(Candidate.SourceRouteCandidateId);
	Hash.AddInt64(Candidate.SourceSpatialCandidateHash);
	Hash.AddInt32(Candidate.Fields.Num());
	for (const FABTSM3MonthlySlingshotField& Field :
		Candidate.Fields)
	{
		Hash.AddInt64(Field.FieldHash);
	}
	Hash.AddInt32(Candidate.TotalSlotCount);
	return Hash.Get();
}

uint64 FABTSM3MonthlySlingshotFieldBuilder::ComputeResultHash(
	const FABTSM3MonthlySlingshotFieldResult& Result)
{
	using namespace ABTSM3R31SlingshotFieldPrivate;
	FCanonicalHash64 Hash;
	Hash.AddInt32(Result.SchemaVersion);
	Hash.AddInt32(Result.GeneratorVersion);
	Hash.AddInt32(Result.LayoutPolicyVersion);
	Hash.AddInt32(Result.WorldSeed);
	Hash.AddInt64(Result.TopologyHash);
	Hash.AddInt64(Result.SourceSpatialResultHash);
	Hash.AddInt64(Result.ConfigHash);
	Hash.AddInt32(Result.MaxCordLengthCM);
	Hash.AddInt32(Result.FieldsPerCandidate);
	Hash.AddInt32(Result.SlotsPerCandidate);
	Hash.AddBool(Result.bSlingshotFieldResultValid);
	Hash.AddBool(Result.bMonthlyWorldAccepted);
	Hash.AddInt32(static_cast<int32>(Result.RejectReason));
	Hash.AddInt32(Result.RetainedCandidates.Num());
	for (const FABTSM3MonthlySlingshotFieldCandidate& Candidate :
		Result.RetainedCandidates)
	{
		Hash.AddInt64(Candidate.CandidateHash);
	}
	return Hash.Get();
}

bool FABTSM3MonthlySlingshotFieldBuilder::Build(
	const int32 WorldSeed,
	const FABTSM3MonthlySlingshotFieldConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	const FABTSM3MonthlySpatialResult& SpatialResult,
	FABTSM3MonthlySlingshotFieldResult& OutResult,
	FString& OutFailure)
{
	using namespace ABTSM3R31SlingshotFieldPrivate;
	OutResult = FABTSM3MonthlySlingshotFieldResult();
	OutFailure.Reset();
	OutResult.WorldSeed = WorldSeed;
	OutResult.SchemaVersion = SchemaVersion;
	OutResult.GeneratorVersion = GeneratorVersion;
	OutResult.LayoutPolicyVersion = MonthlyLayoutPolicyVersion;
	OutResult.MaxCordLengthCM = Config.MaxCordLengthCM;
	if (!ValidateConfig(Config, OutFailure)
		|| !FMath::IsFinite(PlanetRadiusCM)
		|| PlanetRadiusCM <= 0.0f)
	{
		OutResult.RejectReason =
			EABTSM3MonthlySlingshotFieldRejectReason::InvalidConfig;
		OutResult.ResultHash = static_cast<int64>(
			ComputeResultHash(OutResult));
		return false;
	}
	if (!IsTopologyUsable(Cells))
	{
		OutResult.RejectReason =
			EABTSM3MonthlySlingshotFieldRejectReason::
				InvalidTopology;
		OutFailure = TEXT("CellTopo");
		OutResult.ResultHash = static_cast<int64>(
			ComputeResultHash(OutResult));
		return false;
	}
	const uint64 TopologyHash =
		FABTSM3MonthlyRouteBuilder::ComputeTopologyHash(Cells);
	OutResult.TopologyHash = static_cast<int64>(TopologyHash);
	OutResult.SourceSpatialResultHash =
		SpatialResult.SpatialResultHash;
	OutResult.ConfigHash = static_cast<int64>(
		ComputeConfigHash(Config, PlanetRadiusCM, TopologyHash));
	if (SpatialResult.WorldSeed != WorldSeed
		|| SpatialResult.TopologyHash != OutResult.TopologyHash
		|| !SpatialResult.bSpatialResultValid
		|| SpatialResult.bMonthlyWorldAccepted
		|| SpatialResult.RejectReason
			!= EABTSM3MonthlySpatialRejectReason::None
		|| SpatialResult.RetainedCandidates.IsEmpty()
		|| static_cast<uint64>(
				SpatialResult.SpatialResultHash)
			!= FABTSM3MonthlyEncounterBuilder::
				ComputeResultHash(SpatialResult))
	{
		OutResult.RejectReason =
			EABTSM3MonthlySlingshotFieldRejectReason::
				InvalidSpatialResult;
		OutFailure = TEXT("SourceSpatialResult");
		OutResult.ResultHash = static_cast<int64>(
			ComputeResultHash(OutResult));
		return false;
	}
	if (!Config.bBuildSlingshotFields)
	{
		OutResult.bSlingshotFieldResultValid = true;
		OutResult.RejectReason =
			EABTSM3MonthlySlingshotFieldRejectReason::
				NotEvaluated;
		OutResult.ResultHash = static_cast<int64>(
			ComputeResultHash(OutResult));
		return true;
	}

	OutResult.FieldsPerCandidate =
		RequiredEncounterFieldCount
		+ Config.AdditionalRoadFieldCount;
	OutResult.SlotsPerCandidate =
		OutResult.FieldsPerCandidate
		* (BaseSlotsPerOrdinaryField
			+ Config.AdditionalSlotsPerOrdinaryField);
	for (const FABTSM3MonthlySpatialCandidate& SourceCandidate :
		SpatialResult.RetainedCandidates)
	{
		FABTSM3MonthlySlingshotFieldCandidate Candidate;
		if (!BuildCandidate(
				WorldSeed,
				Config,
				Cells,
				PlanetRadiusCM,
				SourceCandidate,
				Candidate,
				OutFailure))
		{
			OutResult.RetainedCandidates.Reset();
			OutResult.FieldsPerCandidate = 0;
			OutResult.SlotsPerCandidate = 0;
			OutResult.RejectReason =
				EABTSM3MonthlySlingshotFieldRejectReason::
					FieldGenerationFailed;
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
		OutResult.FieldsPerCandidate = 0;
		OutResult.SlotsPerCandidate = 0;
		OutResult.RejectReason =
			EABTSM3MonthlySlingshotFieldRejectReason::
				FieldGenerationFailed;
		OutFailure = TEXT("CandidateCount");
		OutResult.ResultHash = static_cast<int64>(
			ComputeResultHash(OutResult));
		return false;
	}
	OutResult.bSlingshotFieldResultValid = true;
	OutResult.bMonthlyWorldAccepted = false;
	OutResult.RejectReason =
		EABTSM3MonthlySlingshotFieldRejectReason::None;
	OutResult.ResultHash = static_cast<int64>(
		ComputeResultHash(OutResult));
	if (Config.bEmitSlingshotFieldLogs)
	{
		LogSummary(OutResult);
	}
	return true;
}

bool FABTSM3MonthlySlingshotFieldBuilder::Validate(
	const FABTSM3MonthlySlingshotFieldConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	const FABTSM3MonthlySpatialResult& SpatialResult,
	const FABTSM3MonthlySlingshotFieldResult& Result,
	EABTSM3MonthlySlingshotFieldRejectReason& OutReason,
	FString& OutFailure)
{
	OutReason =
		EABTSM3MonthlySlingshotFieldRejectReason::None;
	OutFailure.Reset();
	FABTSM3MonthlySlingshotFieldConfig QuietConfig = Config;
	QuietConfig.bEmitSlingshotFieldLogs = false;
	FABTSM3MonthlySlingshotFieldResult Expected;
	FString ExpectedFailure;
	if (!Build(
			SpatialResult.WorldSeed,
			QuietConfig,
			Cells,
			PlanetRadiusCM,
			SpatialResult,
			Expected,
			ExpectedFailure))
	{
		OutReason = Expected.RejectReason;
		OutFailure = FString::Printf(
			TEXT("Rebuild:%s"),
			*ExpectedFailure);
		return false;
	}
	if (!FABTSM3MonthlySlingshotFieldResult::StaticStruct()
			->CompareScriptStruct(&Expected, &Result, PPF_None))
	{
		OutReason =
			EABTSM3MonthlySlingshotFieldRejectReason::
				HashMismatch;
		OutFailure = TEXT("WholeStruct");
		return false;
	}
	return true;
}

void FABTSM3MonthlySlingshotFieldBuilder::BuildDebugData(
	const FABTSM3MonthlySlingshotFieldResult& Result,
	FABTSM3MonthlySlingshotFieldDebugData& OutDebugData)
{
	OutDebugData = FABTSM3MonthlySlingshotFieldDebugData();
	if (!Result.bSlingshotFieldResultValid
		|| Result.RetainedCandidates.IsEmpty())
	{
		return;
	}
	for (const FABTSM3MonthlySlingshotField& Field :
		Result.RetainedCandidates[0].Fields)
	{
		OutDebugData.FieldAnchorCellIds.Add(Field.AnchorCellId);
		TArray<int32>& Destination =
			Field.Kind
				== EABTSM3MonthlySlingshotFieldKind::
					EncounterRequired
			? OutDebugData.EncounterSlotCellIds
			: OutDebugData.RoadSlotCellIds;
		Destination.Append(Field.SlotCellIds);
	}
	OutDebugData.FieldAnchorCellIds.Sort();
	OutDebugData.EncounterSlotCellIds.Sort();
	OutDebugData.RoadSlotCellIds.Sort();
}

void FABTSM3MonthlySlingshotFieldBuilder::LogSummary(
	const FABTSM3MonthlySlingshotFieldResult& Result)
{
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][PCG][SlingshotFields] Stage=M3R3.1 Seed=%d Valid=%d MonthlyAccepted=0 Candidates=%d FieldsPerCandidate=%d SlotsPerCandidate=%d MaxCordLengthCM=%d SourceSpatial=%016llX Config=%016llX Result=%016llX"),
		Result.WorldSeed,
		Result.bSlingshotFieldResultValid ? 1 : 0,
		Result.RetainedCandidates.Num(),
		Result.FieldsPerCandidate,
		Result.SlotsPerCandidate,
		Result.MaxCordLengthCM,
		static_cast<unsigned long long>(
			static_cast<uint64>(
				Result.SourceSpatialResultHash)),
		static_cast<unsigned long long>(
			static_cast<uint64>(Result.ConfigHash)),
		static_cast<unsigned long long>(
			static_cast<uint64>(Result.ResultHash)));
}

const TCHAR*
FABTSM3MonthlySlingshotFieldBuilder::GetRejectReasonName(
	const EABTSM3MonthlySlingshotFieldRejectReason Reason)
{
	switch (Reason)
	{
	case EABTSM3MonthlySlingshotFieldRejectReason::None:
		return TEXT("None");
	case EABTSM3MonthlySlingshotFieldRejectReason::NotEvaluated:
		return TEXT("NotEvaluated");
	case EABTSM3MonthlySlingshotFieldRejectReason::InvalidConfig:
		return TEXT("InvalidConfig");
	case EABTSM3MonthlySlingshotFieldRejectReason::InvalidTopology:
		return TEXT("InvalidTopology");
	case EABTSM3MonthlySlingshotFieldRejectReason::
		InvalidSpatialResult:
		return TEXT("InvalidSpatialResult");
	case EABTSM3MonthlySlingshotFieldRejectReason::
		FieldGenerationFailed:
		return TEXT("FieldGenerationFailed");
	case EABTSM3MonthlySlingshotFieldRejectReason::HashMismatch:
		return TEXT("HashMismatch");
	default:
		return TEXT("Unknown");
	}
}
