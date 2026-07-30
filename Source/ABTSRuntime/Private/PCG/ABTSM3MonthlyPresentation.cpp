// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3MonthlyPresentation.h"

#include "ABTSRuntime.h"
#include "Algo/Sort.h"
#include "Planet/ABTSM2Planet.h"

namespace ABTSM3R5PresentationPrivate
{
constexpr uint64 FnvOffset64 = 14695981039346656037ull;
constexpr uint64 FnvPrime64 = 1099511628211ull;
constexpr int32 FlowQuantization = 1000000;

class FStableHash64
{
public:
	void AddUInt64(const uint64 Input)
	{
		for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
		{
			Value ^= static_cast<uint8>(
				(Input >> (ByteIndex * 8)) & 0xffull);
			Value *= FnvPrime64;
		}
	}

	void AddInt64(const int64 Input)
	{
		AddUInt64(static_cast<uint64>(Input));
	}

	void AddInt32(const int32 Input)
	{
		AddUInt64(static_cast<uint32>(Input));
	}

	void AddBool(const bool bInput)
	{
		AddUInt64(bInput ? 1ull : 0ull);
	}

	uint64 Get() const
	{
		return Value;
	}

private:
	uint64 Value = FnvOffset64;
};

uint64 Mix64(uint64 Value)
{
	Value += 0x9E3779B97F4A7C15ull;
	Value = (Value ^ (Value >> 30)) * 0xBF58476D1CE4E5B9ull;
	Value = (Value ^ (Value >> 27)) * 0x94D049BB133111EBull;
	return Value ^ (Value >> 31);
}

EABTSM3TerrainType TerrainForBiome(
	const EABTSM3BiomeArchetype Archetype)
{
	switch (Archetype)
	{
	case EABTSM3BiomeArchetype::Forest:
		return EABTSM3TerrainType::Forest;
	case EABTSM3BiomeArchetype::Highland:
		return EABTSM3TerrainType::Highland;
	case EABTSM3BiomeArchetype::Mountain:
		return EABTSM3TerrainType::Mountain;
	case EABTSM3BiomeArchetype::Water:
		return EABTSM3TerrainType::Water;
	case EABTSM3BiomeArchetype::Plain:
	case EABTSM3BiomeArchetype::Background:
	default:
		return EABTSM3TerrainType::Plain;
	}
}

bool IsValidBiome(const EABTSM3BiomeArchetype Archetype)
{
	return static_cast<uint8>(Archetype)
		<= static_cast<uint8>(
			EABTSM3BiomeArchetype::Background);
}

int32 ThemeIdForBiome(const EABTSM3BiomeArchetype Archetype)
{
	return 1000 + static_cast<int32>(Archetype);
}

int32 DecorationMaskForBiome(
	const EABTSM3BiomeArchetype Archetype)
{
	switch (Archetype)
	{
	case EABTSM3BiomeArchetype::Forest:
		return static_cast<int32>(
			EABTSM3MonthlyDecorationKind::Forest);
	case EABTSM3BiomeArchetype::Mountain:
		return static_cast<int32>(
			EABTSM3MonthlyDecorationKind::Rock);
	default:
		return 0;
	}
}

int32 ProgressFromFlowQ(
	const int32 FlowQ,
	const int32 RouteLengthCM)
{
	return static_cast<int32>(
		(FMath::Clamp<int64>(FlowQ, 0, FlowQuantization)
			* static_cast<int64>(RouteLengthCM)
			+ FlowQuantization / 2)
		/ FlowQuantization);
}

bool ValidateConfig(
	const FABTSM3MonthlyPresentationConfig& Config,
	FString& OutFailure)
{
	if (!Config.bBuildPresentation)
	{
		OutFailure = TEXT("Disabled");
		return false;
	}
	if (Config.MinVisualBeatLengthCM < 100
		|| Config.TargetVisualBeatLengthCM
			< Config.MinVisualBeatLengthCM
		|| Config.MaxVisualBeatLengthCM
			< Config.TargetVisualBeatLengthCM
		|| Config.MaxVisualBeatLengthCM > 60000)
	{
		OutFailure = TEXT("VisualBeatRange");
		return false;
	}
	if (Config.MinBiomeArchetypeCount < 1
		|| Config.MinBiomeArchetypeCount > 6
		|| Config.MinActiveRoleCoveragePermille < 0
		|| Config.MinActiveRoleCoveragePermille > 1000
		|| Config.MaxDeepWildPermille < 0
		|| Config.MaxDeepWildPermille > 1000
		|| Config.MinVisualBiomeComponentCells < 2
		|| Config.MinVisualBiomeComponentCells > 64
		|| Config.MaxVisualBiomeBoundaryPermille < 0
		|| Config.MaxVisualBiomeBoundaryPermille > 1000)
	{
		OutFailure = TEXT("CoverageDomain");
		return false;
	}
	if (Config.MaxDecorInstancesPerCell < 0
		|| Config.MaxDecorInstancesPerCell > 8
		|| Config.MaxDecorInstancesPerCandidate < 0
		|| Config.MaxDecorInstancesPerCandidate > 10000
		|| ((Config.MaxDecorInstancesPerCell == 0)
			!= (Config.MaxDecorInstancesPerCandidate == 0)))
	{
		OutFailure = TEXT("DecorationBudget");
		return false;
	}
	return true;
}

bool BuildVisualBeats(
	const int32 WorldSeed,
	const FABTSM3MonthlyPresentationConfig& Config,
	const FABTSM3MonthlySpatialCandidate& Source,
	FABTSM3MonthlyCandidatePresentation& OutCandidate,
	FString& OutFailure)
{
	const int32 RouteLengthCM =
		Source.RecomputedRoute.Metrics.RouteLengthCM;
	if (RouteLengthCM < Config.MinVisualBeatLengthCM)
	{
		OutFailure = TEXT("RouteShorterThanVisualBeat");
		return false;
	}
	const int32 MinBeatCount =
		(RouteLengthCM
			+ Config.MaxVisualBeatLengthCM - 1)
		/ Config.MaxVisualBeatLengthCM;
	const int32 MaxBeatCount =
		RouteLengthCM / Config.MinVisualBeatLengthCM;
	if (MinBeatCount <= 0 || MaxBeatCount < MinBeatCount)
	{
		OutFailure = TEXT("VisualBeatNoFeasiblePartition");
		return false;
	}
	const int32 TargetBeatCount = FMath::Max(
		1,
		FMath::RoundToInt(
			static_cast<double>(RouteLengthCM)
			/ Config.TargetVisualBeatLengthCM));
	const int32 BeatCount = FMath::Clamp(
		TargetBeatCount,
		MinBeatCount,
		MaxBeatCount);
	OutCandidate.VisualBeats.Reset(BeatCount);
	OutCandidate.MinVisualBeatLengthCM = MAX_int32;
	OutCandidate.MaxVisualBeatLengthCM = 0;
	for (int32 BeatOrdinal = 0;
		BeatOrdinal < BeatCount;
		++BeatOrdinal)
	{
		FABTSM3MonthlyVisualBeat& Beat =
			OutCandidate.VisualBeats.AddDefaulted_GetRef();
		Beat.VisualBeatId = BeatOrdinal + 1;
		Beat.BeatOrdinal = BeatOrdinal;
		Beat.StartProgressCM = static_cast<int32>(
			static_cast<int64>(RouteLengthCM) * BeatOrdinal
			/ BeatCount);
		Beat.EndProgressCM = static_cast<int32>(
			static_cast<int64>(RouteLengthCM)
			* (BeatOrdinal + 1)
			/ BeatCount);
		const int32 LengthCM =
			Beat.EndProgressCM - Beat.StartProgressCM;
		if (LengthCM < Config.MinVisualBeatLengthCM
			|| LengthCM > Config.MaxVisualBeatLengthCM)
		{
			OutFailure = FString::Printf(
				TEXT("VisualBeatLength:%d:%d"),
				BeatOrdinal,
				LengthCM);
			return false;
		}
		const uint64 AccentKey = Mix64(
			static_cast<uint64>(
				static_cast<uint32>(WorldSeed))
			^ static_cast<uint64>(
				Source.SpatialCandidateHash)
			^ (static_cast<uint64>(BeatOrdinal) << 32));
		Beat.AccentVariantId =
			static_cast<int32>(AccentKey & 1ull);
		OutCandidate.MinVisualBeatLengthCM = FMath::Min(
			OutCandidate.MinVisualBeatLengthCM,
			LengthCM);
		OutCandidate.MaxVisualBeatLengthCM = FMath::Max(
			OutCandidate.MaxVisualBeatLengthCM,
			LengthCM);
	}
	return true;
}

int32 FindVisualBeatId(
	const TArray<FABTSM3MonthlyVisualBeat>& Beats,
	const int32 ProgressCM)
{
	for (const FABTSM3MonthlyVisualBeat& Beat : Beats)
	{
		if (ProgressCM < Beat.EndProgressCM
			|| Beat.BeatOrdinal == Beats.Num() - 1)
		{
			return Beat.VisualBeatId;
		}
	}
	return INDEX_NONE;
}

bool CountBiomeComponents(
	const TArray<FABTSM2Cell>& LogicalCells,
	const TArray<FABTSM3MonthlyPresentationCell>& Cells,
	const bool bUseDisplayBiome,
	int32& OutComponentCount,
	int32& OutSingletonCount,
	TArray<int32>* OutSingletonCellIds,
	FString& OutFailure)
{
	OutComponentCount = 0;
	OutSingletonCount = 0;
	if (OutSingletonCellIds != nullptr)
	{
		OutSingletonCellIds->Reset();
	}
	if (LogicalCells.Num() != Cells.Num())
	{
		OutFailure = TEXT("BiomeComponentCellCount");
		return false;
	}
	TBitArray<> Visited(false, Cells.Num());
	TArray<int32> Queue;
	Queue.Reserve(Cells.Num());
	for (int32 StartCellId = 0;
		StartCellId < Cells.Num();
		++StartCellId)
	{
		if (Visited[StartCellId])
		{
			continue;
		}
		const int32 ComponentKey = bUseDisplayBiome
			? static_cast<int32>(
				Cells[StartCellId].DisplayBiomeArchetype)
			: Cells[StartCellId].BiomeDistrictId;
		if (ComponentKey == INDEX_NONE)
		{
			OutFailure = FString::Printf(
				TEXT("BiomeComponentUnassigned:%d"),
				StartCellId);
			return false;
		}
		Queue.Reset();
		Queue.Add(StartCellId);
		Visited[StartCellId] = true;
		int32 ReadIndex = 0;
		while (ReadIndex < Queue.Num())
		{
			const int32 CellId = Queue[ReadIndex++];
			for (const int32 NeighborId :
				LogicalCells[CellId].NeighborCellIds)
			{
				if (!Cells.IsValidIndex(NeighborId)
					|| Visited[NeighborId]
					|| (bUseDisplayBiome
							? static_cast<int32>(
								Cells[NeighborId]
									.DisplayBiomeArchetype)
							: Cells[NeighborId]
								.BiomeDistrictId)
						!= ComponentKey)
				{
					continue;
				}
				Visited[NeighborId] = true;
				Queue.Add(NeighborId);
			}
		}
		++OutComponentCount;
		OutSingletonCount += Queue.Num() == 1 ? 1 : 0;
		if (Queue.Num() == 1
			&& OutSingletonCellIds != nullptr)
		{
			OutSingletonCellIds->Add(StartCellId);
		}
	}
	return true;
}

bool ApplyDisplayBiome(
	FABTSM3MonthlyCandidatePresentation& Candidate,
	const int32 CellId,
	const EABTSM3BiomeArchetype DisplayBiome,
	FString& OutFailure)
{
	if (!Candidate.Cells.IsValidIndex(CellId)
		|| !IsValidBiome(DisplayBiome))
	{
		OutFailure = FString::Printf(
			TEXT("VisualMergeApply:%d:%d"),
			CellId,
			static_cast<int32>(DisplayBiome));
		return false;
	}
	FABTSM3MonthlyPresentationCell& Cell =
		Candidate.Cells[CellId];
	Cell.DisplayBiomeArchetype = DisplayBiome;
	Cell.VisualTerrainType = Cell.bWater
		? EABTSM3TerrainType::Water
		: TerrainForBiome(DisplayBiome);
	Cell.DecorationKindMask =
		Cell.bDecorationProtected
		? 0
		: DecorationMaskForBiome(DisplayBiome);
	const int32 VariantCount =
		DisplayBiome == EABTSM3BiomeArchetype::Background
		? 1
		: 2;
	const uint64 VariantKey = Mix64(
		static_cast<uint64>(
			Candidate.SourceSpatialCandidateHash)
		^ (static_cast<uint64>(
			static_cast<uint32>(CellId)) << 32)
		^ static_cast<uint64>(
			static_cast<uint32>(DisplayBiome)));
	Cell.ThemeVariantId = static_cast<int32>(
		VariantKey
		% static_cast<uint64>(VariantCount));
	return true;
}

bool MergeLogicalSingletonsForDisplay(
	const TArray<FABTSM2Cell>& LogicalCells,
	const TArray<int32>& LogicalSingletonCellIds,
	FABTSM3MonthlyCandidatePresentation& Candidate,
	FString& OutFailure)
{
	for (const int32 CellId : LogicalSingletonCellIds)
	{
		if (!LogicalCells.IsValidIndex(CellId)
			|| !Candidate.Cells.IsValidIndex(CellId))
		{
			OutFailure = FString::Printf(
				TEXT("VisualMergeCell:%d"),
				CellId);
			return false;
		}
		TMap<int32, int32> NeighborCountByArchetype;
		for (const int32 NeighborId :
			LogicalCells[CellId].NeighborCellIds)
		{
			if (!Candidate.Cells.IsValidIndex(NeighborId))
			{
				continue;
			}
			const int32 Archetype = static_cast<int32>(
				Candidate.Cells[NeighborId]
					.DisplayBiomeArchetype);
			++NeighborCountByArchetype.FindOrAdd(Archetype);
		}
		int32 BestArchetype = INDEX_NONE;
		int32 BestCount = -1;
		for (const TPair<int32, int32>& Pair :
			NeighborCountByArchetype)
		{
			if (Pair.Value > BestCount
				|| (Pair.Value == BestCount
					&& Pair.Key < BestArchetype))
			{
				BestArchetype = Pair.Key;
				BestCount = Pair.Value;
			}
		}
		if (BestArchetype == INDEX_NONE
			|| !IsValidBiome(
				static_cast<EABTSM3BiomeArchetype>(
					BestArchetype)))
		{
			OutFailure = FString::Printf(
				TEXT("VisualMergeNeighbor:%d"),
				CellId);
			return false;
		}
		if (!ApplyDisplayBiome(
				Candidate,
				CellId,
				static_cast<EABTSM3BiomeArchetype>(
					BestArchetype),
				OutFailure))
		{
			return false;
		}
	}
	Candidate.MergedLogicalSingletonCellCount =
		LogicalSingletonCellIds.Num();
	return true;
}

bool GatherSmallDisplayBiomeComponents(
	const TArray<FABTSM2Cell>& LogicalCells,
	const FABTSM3MonthlyCandidatePresentation& Candidate,
	const int32 MinimumComponentCells,
	TArray<TArray<int32>>& OutSmallComponents,
	int32& OutMinimumComponentCells,
	FString& OutFailure)
{
	OutSmallComponents.Reset();
	OutMinimumComponentCells = MAX_int32;
	if (LogicalCells.Num() != Candidate.Cells.Num()
		|| MinimumComponentCells < 2)
	{
		OutFailure = TEXT("VisualFragmentInput");
		return false;
	}
	TBitArray<> Visited(false, Candidate.Cells.Num());
	TArray<int32> Queue;
	Queue.Reserve(Candidate.Cells.Num());
	for (int32 StartCellId = 0;
		StartCellId < Candidate.Cells.Num();
		++StartCellId)
	{
		if (Visited[StartCellId])
		{
			continue;
		}
		const EABTSM3BiomeArchetype ComponentBiome =
			Candidate.Cells[StartCellId]
				.DisplayBiomeArchetype;
		Queue.Reset();
		Queue.Add(StartCellId);
		Visited[StartCellId] = true;
		int32 ReadIndex = 0;
		while (ReadIndex < Queue.Num())
		{
			const int32 CellId = Queue[ReadIndex++];
			for (const int32 NeighborId :
				LogicalCells[CellId].NeighborCellIds)
			{
				if (!Candidate.Cells.IsValidIndex(NeighborId)
					|| Visited[NeighborId]
					|| Candidate.Cells[NeighborId]
							.DisplayBiomeArchetype
						!= ComponentBiome)
				{
					continue;
				}
				Visited[NeighborId] = true;
				Queue.Add(NeighborId);
			}
		}
		Queue.Sort();
		OutMinimumComponentCells = FMath::Min(
			OutMinimumComponentCells,
			Queue.Num());
		if (Queue.Num() < MinimumComponentCells)
		{
			OutSmallComponents.Add(Queue);
		}
	}
	OutSmallComponents.Sort([](
		const TArray<int32>& A,
		const TArray<int32>& B)
	{
		return !A.IsEmpty()
			&& !B.IsEmpty()
			&& A[0] < B[0];
	});
	return OutMinimumComponentCells != MAX_int32;
}

bool MergeSmallDisplayBiomeComponents(
	const TArray<FABTSM2Cell>& LogicalCells,
	const int32 MinimumComponentCells,
	FABTSM3MonthlyCandidatePresentation& Candidate,
	FString& OutFailure)
{
	Candidate.MergedSmallVisualFragmentCellCount = 0;
	constexpr int32 MaxRepairPasses = 16;
	for (int32 Pass = 0; Pass < MaxRepairPasses; ++Pass)
	{
		TArray<TArray<int32>> SmallComponents;
		int32 MinimumObservedCells = 0;
		if (!GatherSmallDisplayBiomeComponents(
				LogicalCells,
				Candidate,
				MinimumComponentCells,
				SmallComponents,
				MinimumObservedCells,
				OutFailure))
		{
			return false;
		}
		if (SmallComponents.IsEmpty())
		{
			Candidate.MinVisualBiomeComponentCellCount =
				MinimumObservedCells;
			return true;
		}
		for (const TArray<int32>& Component :
			SmallComponents)
		{
			TSet<int32> ComponentCells;
			ComponentCells.Reserve(Component.Num());
			for (const int32 CellId : Component)
			{
				ComponentCells.Add(CellId);
			}
			TMap<int32, int32> NeighborCountByArchetype;
			for (const int32 CellId : Component)
			{
				for (const int32 NeighborId :
					LogicalCells[CellId].NeighborCellIds)
				{
					if (!Candidate.Cells.IsValidIndex(
							NeighborId)
						|| ComponentCells.Contains(NeighborId))
					{
						continue;
					}
					const int32 Archetype =
						static_cast<int32>(
							Candidate.Cells[NeighborId]
								.DisplayBiomeArchetype);
					++NeighborCountByArchetype.FindOrAdd(
						Archetype);
				}
			}
			int32 BestArchetype = INDEX_NONE;
			int32 BestCount = -1;
			for (const TPair<int32, int32>& Pair :
				NeighborCountByArchetype)
			{
				if (Pair.Value > BestCount
					|| (Pair.Value == BestCount
						&& Pair.Key < BestArchetype))
				{
					BestArchetype = Pair.Key;
					BestCount = Pair.Value;
				}
			}
			if (BestArchetype == INDEX_NONE
				|| !IsValidBiome(
					static_cast<EABTSM3BiomeArchetype>(
						BestArchetype)))
			{
				OutFailure = FString::Printf(
					TEXT("SmallVisualMergeNeighbor:%d"),
					Component.IsEmpty()
						? INDEX_NONE
						: Component[0]);
				return false;
			}
			for (const int32 CellId : Component)
			{
				if (!ApplyDisplayBiome(
						Candidate,
						CellId,
						static_cast<EABTSM3BiomeArchetype>(
							BestArchetype),
						OutFailure))
				{
					return false;
				}
			}
			Candidate.MergedSmallVisualFragmentCellCount +=
				Component.Num();
		}
	}
	OutFailure = TEXT("SmallVisualMergePassLimit");
	return false;
}

bool ComputeVisualBiomeBoundaryPermille(
	const TArray<FABTSM2Cell>& LogicalCells,
	const FABTSM3MonthlyCandidatePresentation& Candidate,
	int32& OutBoundaryPermille,
	FString& OutFailure)
{
	OutBoundaryPermille = 0;
	if (LogicalCells.Num() != Candidate.Cells.Num())
	{
		OutFailure = TEXT("VisualBoundaryInput");
		return false;
	}
	int32 TotalEdges = 0;
	int32 BoundaryEdges = 0;
	for (int32 CellId = 0;
		CellId < LogicalCells.Num();
		++CellId)
	{
		for (const int32 NeighborId :
			LogicalCells[CellId].NeighborCellIds)
		{
			if (NeighborId <= CellId
				|| !Candidate.Cells.IsValidIndex(NeighborId))
			{
				continue;
			}
			++TotalEdges;
			BoundaryEdges +=
				Candidate.Cells[CellId]
					.DisplayBiomeArchetype
					!= Candidate.Cells[NeighborId]
						.DisplayBiomeArchetype
				? 1
				: 0;
		}
	}
	if (TotalEdges <= 0)
	{
		OutFailure = TEXT("VisualBoundaryNoEdges");
		return false;
	}
	OutBoundaryPermille =
		BoundaryEdges * 1000 / TotalEdges;
	return true;
}

void ApplyDecorationBudget(
	const int32 WorldSeed,
	const FABTSM3MonthlyPresentationConfig& Config,
	FABTSM3MonthlyCandidatePresentation& Candidate)
{
	struct FEligibleCell
	{
		int32 CellId = INDEX_NONE;
		uint64 Priority = 0;
	};
	TArray<FEligibleCell> Eligible;
	Eligible.Reserve(Candidate.Cells.Num());
	for (FABTSM3MonthlyPresentationCell& Cell : Candidate.Cells)
	{
		Cell.MaxDecorationInstances = 0;
		if (Cell.DecorationKindMask == 0
			|| Cell.bDecorationProtected)
		{
			continue;
		}
		const uint64 Key =
			static_cast<uint64>(static_cast<uint32>(WorldSeed))
			^ static_cast<uint64>(
				Candidate.SourceSpatialCandidateHash)
			^ (static_cast<uint64>(
				static_cast<uint32>(Cell.CellId)) << 1);
		Eligible.Add({Cell.CellId, Mix64(Key)});
	}
	Eligible.Sort([](
		const FEligibleCell& A,
		const FEligibleCell& B)
	{
		if (A.Priority != B.Priority)
		{
			return A.Priority < B.Priority;
		}
		return A.CellId < B.CellId;
	});
	int32 Remaining =
		Config.MaxDecorInstancesPerCandidate;
	for (int32 Pass = 0;
		Pass < Config.MaxDecorInstancesPerCell
			&& Remaining > 0;
		++Pass)
	{
		for (const FEligibleCell& Entry : Eligible)
		{
			if (Remaining <= 0)
			{
				break;
			}
			++Candidate.Cells[Entry.CellId]
				.MaxDecorationInstances;
			--Remaining;
		}
	}
	Candidate.PlannedDecorationInstanceCount =
		Config.MaxDecorInstancesPerCandidate - Remaining;
}

bool BuildCandidate(
	const int32 WorldSeed,
	const FABTSM3MonthlyPresentationConfig& Config,
	const TArray<FABTSM2Cell>& LogicalCells,
	const FABTSM3MonthlySpatialCandidate& Source,
	FABTSM3MonthlyCandidatePresentation& OutCandidate,
	EABTSM3MonthlyPresentationRejectReason& OutReason,
	FString& OutFailure)
{
	OutCandidate = FABTSM3MonthlyCandidatePresentation();
	OutCandidate.SourceRouteCandidateId =
		Source.SourceRouteCandidateId;
	OutCandidate.SourceRouteCandidateHash =
		Source.SourceRouteCandidateHash;
	OutCandidate.SourceRecomputedRouteCandidateHash =
		Source.RecomputedRoute.CandidateHash;
	OutCandidate.SourceSpatialCandidateHash =
		Source.SpatialCandidateHash;
	OutCandidate.PresentationConfigHash =
		static_cast<int64>(
			FABTSM3MonthlyPresentationBuilder::
				ComputeConfigHash(Config));
	OutCandidate.RouteLengthCM =
		Source.RecomputedRoute.Metrics.RouteLengthCM;

	if (!Source.bHardPass
		|| Source.SpatialCandidateHash == 0
		|| static_cast<uint64>(Source.SpatialCandidateHash)
			!= FABTSM3MonthlyEncounterBuilder::
				ComputeCandidateHash(Source)
		|| Source.Cells.Num() != LogicalCells.Num())
	{
		OutReason =
			EABTSM3MonthlyPresentationRejectReason::
				SourceCandidateInvalid;
		OutFailure = TEXT("SourceCandidateIdentity");
		return false;
	}

	TMap<int32, EABTSM3BiomeArchetype> BiomeByDistrict;
	TSet<int32> ThemeArchetypes;
	OutCandidate.DistrictStyles.Reserve(
		Source.BiomeDistricts.Num());
	for (const FABTSM3BiomeDistrict& District :
		Source.BiomeDistricts)
	{
		if (District.BiomeDistrictId == INDEX_NONE
			|| !IsValidBiome(District.Archetype)
			|| BiomeByDistrict.Contains(
				District.BiomeDistrictId))
		{
			OutReason =
				EABTSM3MonthlyPresentationRejectReason::
					BiomeCoverageFailed;
			OutFailure = TEXT("DistrictCatalog");
			return false;
		}
		BiomeByDistrict.Add(
			District.BiomeDistrictId,
			District.Archetype);
		FABTSM3MonthlyDistrictStyle& Style =
			OutCandidate.DistrictStyles
				.AddDefaulted_GetRef();
		Style.BiomeDistrictId =
			District.BiomeDistrictId;
		Style.Archetype = District.Archetype;
		Style.ThemeId =
			ThemeIdForBiome(District.Archetype);
		Style.ThemeVariantCount =
			District.bBackground ? 1 : 2;
		Style.bBackground = District.bBackground;
		if (!District.bBackground)
		{
			ThemeArchetypes.Add(
				static_cast<int32>(District.Archetype));
		}
	}
	OutCandidate.DistrictStyles.Sort([](
		const FABTSM3MonthlyDistrictStyle& A,
		const FABTSM3MonthlyDistrictStyle& B)
	{
		return A.BiomeDistrictId < B.BiomeDistrictId;
	});
	OutCandidate.BiomeArchetypeCount =
		ThemeArchetypes.Num();
	if (OutCandidate.BiomeArchetypeCount
		< Config.MinBiomeArchetypeCount)
	{
		OutReason =
			EABTSM3MonthlyPresentationRejectReason::
				BiomeCoverageFailed;
		OutFailure = TEXT("BiomeDiversity");
		return false;
	}

	TArray<TArray<int32>> EnvelopeIdsByCell;
	EnvelopeIdsByCell.SetNum(LogicalCells.Num());
	TSet<int32> EnvelopeIdSet;
	OutCandidate.Envelopes.Reserve(
		Source.PlayableEnvelopes.Num());
	for (const FABTSM3PlayableEnvelope& Envelope :
		Source.PlayableEnvelopes)
	{
		if (Envelope.EnvelopeId == INDEX_NONE
			|| EnvelopeIdSet.Contains(Envelope.EnvelopeId)
			|| Envelope.Resolution
				!= EABTSM3SchemaResolution::Finalized)
		{
			OutReason =
				EABTSM3MonthlyPresentationRejectReason::
					EnvelopeCoverageFailed;
			OutFailure = TEXT("EnvelopeCatalog");
			return false;
		}
		EnvelopeIdSet.Add(Envelope.EnvelopeId);
		FABTSM3MonthlyEnvelopePresentation& Header =
			OutCandidate.Envelopes.AddDefaulted_GetRef();
		Header.EnvelopeId = Envelope.EnvelopeId;
		Header.RouteBeatId = Envelope.RouteBeatId;
		Header.EncounterId = Envelope.EncounterId;
		Header.MinProgressCM = FMath::RoundToInt(
			Envelope.MinProgressDistanceCM);
		Header.MaxProgressCM = FMath::RoundToInt(
			Envelope.MaxProgressDistanceCM);
		if (Header.MinProgressCM < 0
			|| Header.MaxProgressCM
				< Header.MinProgressCM)
		{
			OutReason =
				EABTSM3MonthlyPresentationRejectReason::
					EnvelopeCoverageFailed;
			OutFailure = FString::Printf(
				TEXT("EnvelopeProgress:%d"),
				Envelope.EnvelopeId);
			return false;
		}
		for (const FABTSM3PlayableCellRole& Role :
			Envelope.Cells)
		{
			if (!EnvelopeIdsByCell.IsValidIndex(
					Role.CellId))
			{
				OutReason =
					EABTSM3MonthlyPresentationRejectReason::
						EnvelopeCoverageFailed;
				OutFailure = FString::Printf(
					TEXT("EnvelopeCell:%d:%d"),
					Envelope.EnvelopeId,
					Role.CellId);
				return false;
			}
			EnvelopeIdsByCell[Role.CellId].Add(
				Envelope.EnvelopeId);
		}
	}
	OutCandidate.Envelopes.Sort([](
		const FABTSM3MonthlyEnvelopePresentation& A,
		const FABTSM3MonthlyEnvelopePresentation& B)
	{
		return A.EnvelopeId < B.EnvelopeId;
	});
	for (TArray<int32>& Membership :
		EnvelopeIdsByCell)
	{
		Membership.Sort();
		if (Membership.Num() > 1)
		{
			for (int32 Index = 1;
				Index < Membership.Num();
				++Index)
			{
				if (Membership[Index]
					== Membership[Index - 1])
				{
					OutReason =
						EABTSM3MonthlyPresentationRejectReason::
							EnvelopeCoverageFailed;
					OutFailure = TEXT(
						"DuplicateEnvelopeMembership");
					return false;
				}
			}
		}
	}

	if (!BuildVisualBeats(
			WorldSeed,
			Config,
			Source,
			OutCandidate,
			OutFailure))
	{
		OutReason =
			EABTSM3MonthlyPresentationRejectReason::
				VisualRhythmFailed;
		return false;
	}

	OutCandidate.Cells.SetNum(LogicalCells.Num());
	for (int32 CellId = 0;
		CellId < LogicalCells.Num();
		++CellId)
	{
		const FABTSM3MonthlySpatialCell& SourceCell =
			Source.Cells[CellId];
		if (SourceCell.CellId != CellId)
		{
			OutReason =
				EABTSM3MonthlyPresentationRejectReason::
					SourceCandidateInvalid;
			OutFailure = FString::Printf(
				TEXT("DenseCellIdentity:%d:%d"),
				CellId,
				SourceCell.CellId);
			return false;
		}
		const EABTSM3BiomeArchetype* Archetype =
			BiomeByDistrict.Find(
				SourceCell.BiomeDistrictId);
		if (Archetype == nullptr)
		{
			OutReason =
				EABTSM3MonthlyPresentationRejectReason::
					BiomeCoverageFailed;
			OutFailure = FString::Printf(
				TEXT("CellDistrict:%d:%d"),
				CellId,
				SourceCell.BiomeDistrictId);
			return false;
		}
		const TArray<int32>& Membership =
			EnvelopeIdsByCell[CellId];
		if ((SourceCell.PrimaryEnvelopeId == INDEX_NONE)
				!= Membership.IsEmpty()
			|| (SourceCell.PrimaryEnvelopeId != INDEX_NONE
				&& !Membership.Contains(
					SourceCell.PrimaryEnvelopeId)))
		{
			OutReason =
				EABTSM3MonthlyPresentationRejectReason::
					EnvelopeCoverageFailed;
			OutFailure = FString::Printf(
				TEXT("PrimaryEnvelope:%d:%d"),
				CellId,
				SourceCell.PrimaryEnvelopeId);
			return false;
		}
		FABTSM3MonthlyPresentationCell& Cell =
			OutCandidate.Cells[CellId];
		Cell.CellId = CellId;
		Cell.BiomeDistrictId =
			SourceCell.BiomeDistrictId;
		Cell.BiomeArchetype = *Archetype;
		Cell.DisplayBiomeArchetype = *Archetype;
		Cell.VisualTerrainType =
			SourceCell.bWater
			? EABTSM3TerrainType::Water
			: TerrainForBiome(*Archetype);
		Cell.PrimaryEnvelopeId =
			SourceCell.PrimaryEnvelopeId;
		Cell.EnvelopeIds = Membership;
		Cell.ActiveRoleMask =
			SourceCell.ActiveRoleMask;
		Cell.bPlayable = !Membership.IsEmpty();
		Cell.bApprovedTransition =
			SourceCell.bApprovedTransition;
		Cell.bDeepWild = Cell.bPlayable
			&& Cell.ActiveRoleMask == 0
			&& !Cell.bApprovedTransition;
		Cell.bWater = SourceCell.bWater;
		Cell.bTargetFootprint =
			SourceCell.bTargetFootprint;
		Cell.bNoRoad = SourceCell.bNoRoad;
		Cell.bAttackCorridor =
			SourceCell.bAttackCorridor;
		Cell.bDecorationProtected =
			Cell.bWater
			|| Cell.bTargetFootprint
			|| Cell.bNoRoad
			|| Cell.bAttackCorridor
			|| (Config.bSuppressDecorOnActiveRoles
				&& Cell.ActiveRoleMask != 0);
		Cell.DecorationKindMask =
			Cell.bDecorationProtected
			? 0
			: DecorationMaskForBiome(*Archetype);
		const int32 ProgressCM = ProgressFromFlowQ(
			SourceCell.FlowQ,
			OutCandidate.RouteLengthCM);
		Cell.VisualBeatId = FindVisualBeatId(
			OutCandidate.VisualBeats,
			ProgressCM);
		if (Cell.VisualBeatId == INDEX_NONE)
		{
			OutReason =
				EABTSM3MonthlyPresentationRejectReason::
					VisualRhythmFailed;
			OutFailure = FString::Printf(
				TEXT("CellVisualBeat:%d"),
				CellId);
			return false;
		}
		const uint64 VariantKey = Mix64(
			static_cast<uint64>(
				static_cast<uint32>(WorldSeed))
			^ static_cast<uint64>(
				Source.SpatialCandidateHash)
			^ (static_cast<uint64>(
				static_cast<uint32>(
					Cell.BiomeDistrictId)) << 16)
			^ static_cast<uint64>(
				static_cast<uint32>(
					Cell.VisualBeatId)));
		const FABTSM3MonthlyDistrictStyle* Style =
			OutCandidate.DistrictStyles
				.FindByPredicate(
					[&Cell](
						const FABTSM3MonthlyDistrictStyle&
							Item)
					{
						return Item.BiomeDistrictId
							== Cell.BiomeDistrictId;
					});
		Cell.ThemeVariantId =
			Style != nullptr
				&& Style->ThemeVariantCount > 0
			? static_cast<int32>(
				VariantKey
				% static_cast<uint64>(
					Style->ThemeVariantCount))
			: 0;
		OutCandidate.PlayableCellCount +=
			Cell.bPlayable ? 1 : 0;
		OutCandidate.ActiveRoleCellCount +=
			Cell.bPlayable
				&& Cell.ActiveRoleMask != 0
			? 1 : 0;
		OutCandidate.DeepWildCellCount +=
			Cell.bDeepWild ? 1 : 0;
		OutCandidate.DecorationProtectedCellCount +=
			Cell.bDecorationProtected ? 1 : 0;
	}
	OutCandidate.ActiveRoleCoveragePermille =
		OutCandidate.PlayableCellCount > 0
		? OutCandidate.ActiveRoleCellCount * 1000
			/ OutCandidate.PlayableCellCount
		: 0;
	OutCandidate.DeepWildPermille =
		OutCandidate.PlayableCellCount > 0
		? OutCandidate.DeepWildCellCount * 1000
			/ OutCandidate.PlayableCellCount
		: 1000;
	if (OutCandidate.PlayableCellCount
			!= Source.PlayableCellCount
		|| OutCandidate.ActiveRoleCellCount
			!= Source.ActiveRoleCellCount
		|| OutCandidate.DeepWildCellCount
			!= Source.DeepWildCellCount
		|| OutCandidate.ActiveRoleCoveragePermille
			!= Source.ActiveRoleCoveragePermille
		|| OutCandidate.DeepWildPermille
			!= Source.DeepWildPermille
		|| OutCandidate.ActiveRoleCoveragePermille
			< Config.MinActiveRoleCoveragePermille
		|| OutCandidate.DeepWildPermille
			> Config.MaxDeepWildPermille)
	{
		OutReason =
			EABTSM3MonthlyPresentationRejectReason::
				EnvelopeCoverageFailed;
		OutFailure = TEXT("CoverageMetrics");
		return false;
	}

	TArray<int32> LogicalSingletonCellIds;
	if (!CountBiomeComponents(
			LogicalCells,
			OutCandidate.Cells,
			false,
			OutCandidate.BiomeComponentCount,
			OutCandidate.SingleCellBiomeComponentCount,
			&LogicalSingletonCellIds,
			OutFailure))
	{
		OutReason =
			EABTSM3MonthlyPresentationRejectReason::
				BiomeCoverageFailed;
		return false;
	}
	if (!MergeLogicalSingletonsForDisplay(
			LogicalCells,
			LogicalSingletonCellIds,
			OutCandidate,
			OutFailure))
	{
		OutReason =
			EABTSM3MonthlyPresentationRejectReason::
				BiomeFragmented;
		return false;
	}
	if (!MergeSmallDisplayBiomeComponents(
			LogicalCells,
			Config.MinVisualBiomeComponentCells,
			OutCandidate,
			OutFailure))
	{
		OutReason =
			EABTSM3MonthlyPresentationRejectReason::
				BiomeFragmented;
		return false;
	}
	if (!CountBiomeComponents(
			LogicalCells,
			OutCandidate.Cells,
			true,
			OutCandidate.BiomeComponentCount,
			OutCandidate.SingleCellBiomeComponentCount,
			nullptr,
			OutFailure))
	{
		OutReason =
			EABTSM3MonthlyPresentationRejectReason::
				BiomeCoverageFailed;
		return false;
	}
	if (OutCandidate.SingleCellBiomeComponentCount > 0)
	{
		OutReason =
			EABTSM3MonthlyPresentationRejectReason::
				BiomeFragmented;
		OutFailure = FString::Printf(
			TEXT("VisualSingletonBiomeComponents:%d"),
			OutCandidate.SingleCellBiomeComponentCount);
		return false;
	}
	if (OutCandidate.MinVisualBiomeComponentCellCount
			< Config.MinVisualBiomeComponentCells
		|| !ComputeVisualBiomeBoundaryPermille(
			LogicalCells,
			OutCandidate,
			OutCandidate.VisualBiomeBoundaryPermille,
			OutFailure)
		|| OutCandidate.VisualBiomeBoundaryPermille
			> Config.MaxVisualBiomeBoundaryPermille)
	{
		OutReason =
			EABTSM3MonthlyPresentationRejectReason::
				BiomeFragmented;
		if (OutFailure.IsEmpty())
		{
			OutFailure = FString::Printf(
				TEXT("VisualFragmentMetrics:%d:%d"),
				OutCandidate
					.MinVisualBiomeComponentCellCount,
				OutCandidate
					.VisualBiomeBoundaryPermille);
		}
		return false;
	}

	ApplyDecorationBudget(
		WorldSeed,
		Config,
		OutCandidate);
	if (OutCandidate.PlannedDecorationInstanceCount
		> Config.MaxDecorInstancesPerCandidate)
	{
		OutReason =
			EABTSM3MonthlyPresentationRejectReason::
				DecorationBudgetFailed;
		OutFailure = TEXT("CandidateDecorationBudget");
		return false;
	}
	OutCandidate.CandidatePresentationHash =
		static_cast<int64>(
			FABTSM3MonthlyPresentationBuilder::
				ComputeCandidateHash(OutCandidate));
	return true;
}

void SetRejectedResult(
	FABTSM3MonthlyPresentationResult& Result,
	const EABTSM3MonthlyPresentationRejectReason Reason)
{
	Result.bPresentationValid = false;
	Result.bMonthlyWorldAccepted = false;
	Result.RejectReason = Reason;
	Result.CandidatePresentations.Reset();
	Result.PresentationResultHash =
		static_cast<int64>(
			FABTSM3MonthlyPresentationBuilder::
				ComputeResultHash(Result));
}
}

uint64 FABTSM3MonthlyPresentationBuilder::ComputeConfigHash(
	const FABTSM3MonthlyPresentationConfig& Config)
{
	using namespace ABTSM3R5PresentationPrivate;
	FStableHash64 Hash;
	Hash.AddInt32(PresentationSchemaVersion);
	Hash.AddInt32(PlannerVersion);
	Hash.AddBool(Config.bBuildPresentation);
	Hash.AddInt32(Config.MinVisualBeatLengthCM);
	Hash.AddInt32(Config.TargetVisualBeatLengthCM);
	Hash.AddInt32(Config.MaxVisualBeatLengthCM);
	Hash.AddInt32(Config.MinBiomeArchetypeCount);
	Hash.AddInt32(
		Config.MinActiveRoleCoveragePermille);
	Hash.AddInt32(Config.MaxDeepWildPermille);
	Hash.AddInt32(
		Config.MinVisualBiomeComponentCells);
	Hash.AddInt32(
		Config.MaxVisualBiomeBoundaryPermille);
	Hash.AddInt32(Config.MaxDecorInstancesPerCell);
	Hash.AddInt32(
		Config.MaxDecorInstancesPerCandidate);
	Hash.AddBool(Config.bSuppressDecorOnActiveRoles);
	return Hash.Get();
}

uint64 FABTSM3MonthlyPresentationBuilder::
	ComputeFaultInjectionHash(
		const FABTSM3MonthlyPresentationFaultInjection&
			FaultInjection)
{
	using namespace ABTSM3R5PresentationPrivate;
	FStableHash64 Hash;
	Hash.AddInt32(1);
	Hash.AddInt32(
		FaultInjection.RejectedSourceCandidateId);
	return Hash.Get();
}

uint64 FABTSM3MonthlyPresentationBuilder::
	ComputeCandidateHash(
		const FABTSM3MonthlyCandidatePresentation& Candidate)
{
	using namespace ABTSM3R5PresentationPrivate;
	FStableHash64 Hash;
	Hash.AddInt32(PresentationSchemaVersion);
	Hash.AddInt32(PlannerVersion);
	Hash.AddInt32(Candidate.SourceRouteCandidateId);
	Hash.AddInt64(Candidate.SourceRouteCandidateHash);
	Hash.AddInt64(
		Candidate.SourceRecomputedRouteCandidateHash);
	Hash.AddInt64(Candidate.SourceSpatialCandidateHash);
	Hash.AddInt64(Candidate.PresentationConfigHash);
	Hash.AddInt32(Candidate.RouteLengthCM);
	Hash.AddInt32(Candidate.DistrictStyles.Num());
	for (const FABTSM3MonthlyDistrictStyle& Style :
		Candidate.DistrictStyles)
	{
		Hash.AddInt32(Style.BiomeDistrictId);
		Hash.AddInt32(
			static_cast<int32>(Style.Archetype));
		Hash.AddInt32(Style.ThemeId);
		Hash.AddInt32(Style.ThemeVariantCount);
		Hash.AddBool(Style.bBackground);
	}
	Hash.AddInt32(Candidate.Envelopes.Num());
	for (const FABTSM3MonthlyEnvelopePresentation& Envelope :
		Candidate.Envelopes)
	{
		Hash.AddInt32(Envelope.EnvelopeId);
		Hash.AddInt32(Envelope.RouteBeatId);
		Hash.AddInt32(Envelope.EncounterId);
		Hash.AddInt32(Envelope.MinProgressCM);
		Hash.AddInt32(Envelope.MaxProgressCM);
	}
	Hash.AddInt32(Candidate.VisualBeats.Num());
	for (const FABTSM3MonthlyVisualBeat& Beat :
		Candidate.VisualBeats)
	{
		Hash.AddInt32(Beat.VisualBeatId);
		Hash.AddInt32(Beat.BeatOrdinal);
		Hash.AddInt32(Beat.StartProgressCM);
		Hash.AddInt32(Beat.EndProgressCM);
		Hash.AddInt32(Beat.AccentVariantId);
	}
	Hash.AddInt32(Candidate.Cells.Num());
	for (const FABTSM3MonthlyPresentationCell& Cell :
		Candidate.Cells)
	{
		Hash.AddInt32(Cell.CellId);
		Hash.AddInt32(Cell.BiomeDistrictId);
		Hash.AddInt32(
			static_cast<int32>(Cell.BiomeArchetype));
		Hash.AddInt32(
			static_cast<int32>(
				Cell.DisplayBiomeArchetype));
		Hash.AddInt32(
			static_cast<int32>(Cell.VisualTerrainType));
		Hash.AddInt32(Cell.PrimaryEnvelopeId);
		Hash.AddInt32(Cell.EnvelopeIds.Num());
		for (const int32 EnvelopeId : Cell.EnvelopeIds)
		{
			Hash.AddInt32(EnvelopeId);
		}
		Hash.AddInt32(Cell.ActiveRoleMask);
		Hash.AddInt32(Cell.VisualBeatId);
		Hash.AddInt32(Cell.ThemeVariantId);
		Hash.AddInt32(Cell.DecorationKindMask);
		Hash.AddInt32(Cell.MaxDecorationInstances);
		Hash.AddBool(Cell.bPlayable);
		Hash.AddBool(Cell.bDeepWild);
		Hash.AddBool(Cell.bApprovedTransition);
		Hash.AddBool(Cell.bWater);
		Hash.AddBool(Cell.bTargetFootprint);
		Hash.AddBool(Cell.bNoRoad);
		Hash.AddBool(Cell.bAttackCorridor);
		Hash.AddBool(Cell.bDecorationProtected);
	}
	Hash.AddInt32(Candidate.PlayableCellCount);
	Hash.AddInt32(Candidate.ActiveRoleCellCount);
	Hash.AddInt32(Candidate.DeepWildCellCount);
	Hash.AddInt32(
		Candidate.ActiveRoleCoveragePermille);
	Hash.AddInt32(Candidate.DeepWildPermille);
	Hash.AddInt32(Candidate.BiomeArchetypeCount);
	Hash.AddInt32(Candidate.BiomeComponentCount);
	Hash.AddInt32(
		Candidate.MergedLogicalSingletonCellCount);
	Hash.AddInt32(
		Candidate.MergedSmallVisualFragmentCellCount);
	Hash.AddInt32(
		Candidate.SingleCellBiomeComponentCount);
	Hash.AddInt32(
		Candidate.MinVisualBiomeComponentCellCount);
	Hash.AddInt32(
		Candidate.VisualBiomeBoundaryPermille);
	Hash.AddInt32(Candidate.MinVisualBeatLengthCM);
	Hash.AddInt32(Candidate.MaxVisualBeatLengthCM);
	Hash.AddInt32(
		Candidate.DecorationProtectedCellCount);
	Hash.AddInt32(
		Candidate.PlannedDecorationInstanceCount);
	return Hash.Get();
}

uint64 FABTSM3MonthlyPresentationBuilder::ComputeResultHash(
	const FABTSM3MonthlyPresentationResult& Result)
{
	using namespace ABTSM3R5PresentationPrivate;
	FStableHash64 Hash;
	Hash.AddInt32(Result.SchemaVersion);
	Hash.AddInt32(Result.PlannerVersion);
	Hash.AddInt32(Result.WorldSeed);
	Hash.AddInt64(Result.SourceTopologyHash);
	Hash.AddInt64(Result.SourceSpatialConfigHash);
	Hash.AddInt64(Result.SourceSpatialResultHash);
	Hash.AddInt64(Result.PresentationConfigHash);
	Hash.AddInt64(Result.FaultInjectionHash);
	Hash.AddBool(Result.bPresentationValid);
	Hash.AddBool(Result.bMonthlyWorldAccepted);
	Hash.AddInt32(
		static_cast<int32>(Result.RejectReason));
	Hash.AddInt32(Result.CandidatePresentations.Num());
	for (const FABTSM3MonthlyCandidatePresentation& Candidate :
		Result.CandidatePresentations)
	{
		Hash.AddInt64(Candidate.CandidatePresentationHash);
	}
	return Hash.Get();
}

bool FABTSM3MonthlyPresentationBuilder::Build(
	const int32 WorldSeed,
	const FABTSM3MonthlyPresentationConfig& Config,
	const FABTSM3MonthlyEncounterSpatialConfig&
		SpatialConfig,
	const FABTSM3MonthlyRouteConfig& RouteConfig,
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	const FABTSM3MonthlyRoutePool& SourceRoutePool,
	const FABTSM3MonthlySpatialResult&
		SourceSpatialResult,
	const FABTSM3MonthlyPresentationFaultInjection&
		FaultInjection,
	FABTSM3MonthlyPresentationResult& OutResult,
	FString& OutFailure)
{
	using namespace ABTSM3R5PresentationPrivate;
	OutFailure.Reset();
	OutResult = FABTSM3MonthlyPresentationResult();
	OutResult.SchemaVersion = PresentationSchemaVersion;
	OutResult.PlannerVersion = PlannerVersion;
	OutResult.WorldSeed = WorldSeed;
	OutResult.SourceTopologyHash =
		SourceSpatialResult.TopologyHash;
	OutResult.SourceSpatialConfigHash =
		SourceSpatialResult.SpatialConfigHash;
	OutResult.SourceSpatialResultHash =
		SourceSpatialResult.SpatialResultHash;
	OutResult.PresentationConfigHash =
		static_cast<int64>(ComputeConfigHash(Config));
	OutResult.FaultInjectionHash =
		static_cast<int64>(
			ComputeFaultInjectionHash(FaultInjection));

	FString ConfigFailure;
	if (!ValidateConfig(Config, ConfigFailure))
	{
		const EABTSM3MonthlyPresentationRejectReason Reason =
			Config.bBuildPresentation
			? EABTSM3MonthlyPresentationRejectReason::
				InvalidConfig
			: EABTSM3MonthlyPresentationRejectReason::
				Disabled;
		SetRejectedResult(OutResult, Reason);
		OutFailure = ConfigFailure;
		return false;
	}

	EABTSM3MonthlySpatialRejectReason SourceReason =
		EABTSM3MonthlySpatialRejectReason::None;
	FString SourceFailure;
	if (!FABTSM3MonthlyEncounterBuilder::Validate(
			SpatialConfig,
			RouteConfig,
			Cells,
			PlanetRadiusCM,
			SourceRoutePool,
			FABTSM3MonthlySpatialFaultInjection(),
			SourceSpatialResult,
			SourceReason,
			SourceFailure)
		|| SourceSpatialResult.SchemaVersion
			!= FABTSM3MonthlyEncounterBuilder::
				SpatialSchemaVersion
		|| SourceSpatialResult.GeneratorVersion
			!= FABTSM3MonthlyEncounterBuilder::
				GeneratorVersion
			|| SourceSpatialResult.LayoutPolicyVersion
			!= FABTSM3MonthlyEncounterBuilder::
				MonthlyLayoutPolicyVersion
		|| SourceSpatialResult.WorldSeed != WorldSeed
		|| SourceSpatialResult.bMonthlyWorldAccepted
		|| SourceSpatialResult.RetainedCandidates.IsEmpty())
	{
		SetRejectedResult(
			OutResult,
			EABTSM3MonthlyPresentationRejectReason::
				InvalidSourceSpatial);
		OutFailure = FString::Printf(
			TEXT("%s:%s"),
			FABTSM3MonthlyEncounterBuilder::
				GetRejectReasonName(SourceReason),
			*SourceFailure);
		return false;
	}

	TSet<int32> SourceCandidateIds;
	OutResult.CandidatePresentations.Reserve(
		SourceSpatialResult.RetainedCandidates.Num());
	for (const FABTSM3MonthlySpatialCandidate& Source :
		SourceSpatialResult.RetainedCandidates)
	{
		if (SourceCandidateIds.Contains(
				Source.SourceRouteCandidateId))
		{
			SetRejectedResult(
				OutResult,
				EABTSM3MonthlyPresentationRejectReason::
					CandidateIdentityMismatch);
			OutFailure = FString::Printf(
				TEXT("DuplicateSourceCandidate:%d"),
				Source.SourceRouteCandidateId);
			return false;
		}
		SourceCandidateIds.Add(
			Source.SourceRouteCandidateId);
		if (FaultInjection.RejectedSourceCandidateId
			== Source.SourceRouteCandidateId)
		{
			SetRejectedResult(
				OutResult,
				EABTSM3MonthlyPresentationRejectReason::
					FaultInjected);
			OutFailure = FString::Printf(
				TEXT("RejectedSourceCandidate:%d"),
				Source.SourceRouteCandidateId);
			return false;
		}
		FABTSM3MonthlyCandidatePresentation Candidate;
		EABTSM3MonthlyPresentationRejectReason Reason =
			EABTSM3MonthlyPresentationRejectReason::None;
		FString CandidateFailure;
		if (!BuildCandidate(
				WorldSeed,
				Config,
				Cells,
				Source,
				Candidate,
				Reason,
				CandidateFailure))
		{
			SetRejectedResult(OutResult, Reason);
			OutFailure = FString::Printf(
				TEXT("Candidate:%d:%s"),
				Source.SourceRouteCandidateId,
				*CandidateFailure);
			return false;
		}
		OutResult.CandidatePresentations.Add(
			MoveTemp(Candidate));
	}
	if (FaultInjection.RejectedSourceCandidateId
		!= INDEX_NONE)
	{
		SetRejectedResult(
			OutResult,
			EABTSM3MonthlyPresentationRejectReason::
				CandidateIdentityMismatch);
		OutFailure = FString::Printf(
			TEXT("UnknownFaultCandidate:%d"),
			FaultInjection.RejectedSourceCandidateId);
		return false;
	}
	OutResult.bPresentationValid = true;
	OutResult.bMonthlyWorldAccepted = false;
	OutResult.RejectReason =
		EABTSM3MonthlyPresentationRejectReason::None;
	OutResult.PresentationResultHash =
		static_cast<int64>(ComputeResultHash(OutResult));
	if (Config.bEmitPresentationLogs)
	{
		LogSummary(OutResult);
	}
	return true;
}

bool FABTSM3MonthlyPresentationBuilder::Validate(
	const FABTSM3MonthlyPresentationConfig& Config,
	const FABTSM3MonthlyEncounterSpatialConfig&
		SpatialConfig,
	const FABTSM3MonthlyRouteConfig& RouteConfig,
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	const FABTSM3MonthlyRoutePool& SourceRoutePool,
	const FABTSM3MonthlySpatialResult&
		SourceSpatialResult,
	const FABTSM3MonthlyPresentationFaultInjection&
		FaultInjection,
	const FABTSM3MonthlyPresentationResult& Result,
	EABTSM3MonthlyPresentationRejectReason& OutReason,
	FString& OutFailure)
{
	OutReason =
		EABTSM3MonthlyPresentationRejectReason::None;
	OutFailure.Reset();
	if (!Result.bPresentationValid
		|| Result.bMonthlyWorldAccepted
		|| Result.RejectReason
			!= EABTSM3MonthlyPresentationRejectReason::None
		|| Result.SchemaVersion != PresentationSchemaVersion
		|| Result.PlannerVersion != PlannerVersion
		|| static_cast<uint64>(Result.PresentationResultHash)
			!= ComputeResultHash(Result))
	{
		OutReason =
			EABTSM3MonthlyPresentationRejectReason::
				HashMismatch;
		OutFailure = TEXT("StoredResultIdentity");
		return false;
	}
	FABTSM3MonthlyPresentationConfig QuietConfig = Config;
	QuietConfig.bEmitPresentationLogs = false;
	FABTSM3MonthlyPresentationResult Expected;
	FString BuildFailure;
	if (!Build(
			Result.WorldSeed,
			QuietConfig,
			SpatialConfig,
			RouteConfig,
			Cells,
			PlanetRadiusCM,
			SourceRoutePool,
			SourceSpatialResult,
			FaultInjection,
			Expected,
			BuildFailure))
	{
		OutReason = Expected.RejectReason;
		OutFailure = FString::Printf(
			TEXT("CanonicalRebuild:%s"),
			*BuildFailure);
		return false;
	}
	if (!FABTSM3MonthlyPresentationResult::StaticStruct()
			->CompareScriptStruct(
				&Expected,
				&Result,
				PPF_None))
	{
		OutReason =
			EABTSM3MonthlyPresentationRejectReason::
				HashMismatch;
		OutFailure = TEXT("CanonicalResultMismatch");
		return false;
	}
	return true;
}

const FABTSM3MonthlyCandidatePresentation*
FABTSM3MonthlyPresentationBuilder::FindCandidatePresentation(
	const FABTSM3MonthlyPresentationResult& Result,
	const int32 SourceRouteCandidateId)
{
	return Result.CandidatePresentations.FindByPredicate(
		[SourceRouteCandidateId](
			const FABTSM3MonthlyCandidatePresentation&
				Candidate)
		{
			return Candidate.SourceRouteCandidateId
				== SourceRouteCandidateId;
		});
}

void FABTSM3MonthlyPresentationBuilder::BuildDebugData(
	const FABTSM3MonthlyPresentationResult& Result,
	const int32 SourceRouteCandidateId,
	FABTSM3MonthlyPresentationDebugData& OutDebugData)
{
	OutDebugData =
		FABTSM3MonthlyPresentationDebugData();
	OutDebugData.SourceRouteCandidateId =
		SourceRouteCandidateId;
	const FABTSM3MonthlyCandidatePresentation* Candidate =
		FindCandidatePresentation(
			Result,
			SourceRouteCandidateId);
	if (Candidate == nullptr)
	{
		return;
	}
	for (const FABTSM3MonthlyPresentationCell& Cell :
		Candidate->Cells)
	{
		if (Cell.bPlayable)
		{
			OutDebugData.PlayableCellIds.Add(Cell.CellId);
		}
		if (Cell.ActiveRoleMask != 0)
		{
			OutDebugData.ActiveRoleCellIds.Add(
				Cell.CellId);
		}
		if (Cell.bDeepWild)
		{
			OutDebugData.DeepWildCellIds.Add(
				Cell.CellId);
		}
		if (Cell.BiomeArchetype
			== EABTSM3BiomeArchetype::Background)
		{
			OutDebugData.BackgroundCellIds.Add(
				Cell.CellId);
		}
		if (Cell.bDecorationProtected)
		{
			OutDebugData.DecorationProtectedCellIds.Add(
				Cell.CellId);
		}
		if (Cell.bTargetFootprint)
		{
			OutDebugData.TargetFootprintCellIds.Add(
				Cell.CellId);
		}
		if (Cell.bAttackCorridor)
		{
			OutDebugData.AttackCorridorCellIds.Add(
				Cell.CellId);
		}
	}
}

void FABTSM3MonthlyPresentationBuilder::LogSummary(
	const FABTSM3MonthlyPresentationResult& Result)
{
	const FABTSM3MonthlyCandidatePresentation* Preview =
		Result.CandidatePresentations.IsEmpty()
		? nullptr
		: &Result.CandidatePresentations[0];
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][PCG][BiomePresentation] Stage=M3R5 Seed=%d Valid=%d MonthlyAccepted=%d CandidatePlans=%d PreviewAuthority=0 SourceCandidate=%d SourceSpatial=%016llX Config=%016llX Result=%016llX Cells=%d Districts=%d Envelopes=%d Themes=%d Beats=%d BeatMinCM=%d BeatMaxCM=%d ActiveCoveragePermille=%d DeepWildPermille=%d MergedLogicalSingletons=%d MergedSmallFragments=%d SingletonComponents=%d MinVisualComponentCells=%d VisualBoundaryPermille=%d ProtectedCells=%d PlannedInstances=%d"),
		Result.WorldSeed,
		Result.bPresentationValid ? 1 : 0,
		Result.bMonthlyWorldAccepted ? 1 : 0,
		Result.CandidatePresentations.Num(),
		Preview != nullptr
			? Preview->SourceRouteCandidateId
			: INDEX_NONE,
		static_cast<unsigned long long>(
			Result.SourceSpatialResultHash),
		static_cast<unsigned long long>(
			Result.PresentationConfigHash),
		static_cast<unsigned long long>(
			Result.PresentationResultHash),
		Preview != nullptr ? Preview->Cells.Num() : 0,
		Preview != nullptr
			? Preview->DistrictStyles.Num()
			: 0,
		Preview != nullptr
			? Preview->Envelopes.Num()
			: 0,
		Preview != nullptr
			? Preview->BiomeArchetypeCount
			: 0,
		Preview != nullptr
			? Preview->VisualBeats.Num()
			: 0,
		Preview != nullptr
			? Preview->MinVisualBeatLengthCM
			: 0,
		Preview != nullptr
			? Preview->MaxVisualBeatLengthCM
			: 0,
		Preview != nullptr
			? Preview->ActiveRoleCoveragePermille
			: 0,
		Preview != nullptr
			? Preview->DeepWildPermille
			: 0,
		Preview != nullptr
			? Preview->MergedLogicalSingletonCellCount
			: 0,
		Preview != nullptr
			? Preview->MergedSmallVisualFragmentCellCount
			: 0,
		Preview != nullptr
			? Preview->SingleCellBiomeComponentCount
			: 0,
		Preview != nullptr
			? Preview->MinVisualBiomeComponentCellCount
			: 0,
		Preview != nullptr
			? Preview->VisualBiomeBoundaryPermille
			: 0,
		Preview != nullptr
			? Preview->DecorationProtectedCellCount
			: 0,
		Preview != nullptr
			? Preview->PlannedDecorationInstanceCount
			: 0);
}

const TCHAR*
FABTSM3MonthlyPresentationBuilder::GetRejectReasonName(
	const EABTSM3MonthlyPresentationRejectReason Reason)
{
	switch (Reason)
	{
	case EABTSM3MonthlyPresentationRejectReason::None:
		return TEXT("None");
	case EABTSM3MonthlyPresentationRejectReason::NotEvaluated:
		return TEXT("NotEvaluated");
	case EABTSM3MonthlyPresentationRejectReason::Disabled:
		return TEXT("Disabled");
	case EABTSM3MonthlyPresentationRejectReason::InvalidConfig:
		return TEXT("InvalidConfig");
	case EABTSM3MonthlyPresentationRejectReason::
		InvalidSourceSpatial:
		return TEXT("InvalidSourceSpatial");
	case EABTSM3MonthlyPresentationRejectReason::
		SourceCandidateInvalid:
		return TEXT("SourceCandidateInvalid");
	case EABTSM3MonthlyPresentationRejectReason::
		CandidateIdentityMismatch:
		return TEXT("CandidateIdentityMismatch");
	case EABTSM3MonthlyPresentationRejectReason::
		BiomeCoverageFailed:
		return TEXT("BiomeCoverageFailed");
	case EABTSM3MonthlyPresentationRejectReason::
		EnvelopeCoverageFailed:
		return TEXT("EnvelopeCoverageFailed");
	case EABTSM3MonthlyPresentationRejectReason::
		BiomeFragmented:
		return TEXT("BiomeFragmented");
	case EABTSM3MonthlyPresentationRejectReason::
		VisualRhythmFailed:
		return TEXT("VisualRhythmFailed");
	case EABTSM3MonthlyPresentationRejectReason::
		DecorationBudgetFailed:
		return TEXT("DecorationBudgetFailed");
	case EABTSM3MonthlyPresentationRejectReason::
		FaultInjected:
		return TEXT("FaultInjected");
	case EABTSM3MonthlyPresentationRejectReason::HashMismatch:
		return TEXT("HashMismatch");
	default:
		return TEXT("Unknown");
	}
}
