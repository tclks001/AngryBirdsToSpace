// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3MonthlyEncounter.h"

#include "ABTSRuntime.h"
#include "Algo/BinarySearch.h"
#include "Algo/Reverse.h"
#include "Algo/Sort.h"
#include "Algo/Unique.h"
#include "Containers/StringConv.h"
#include "PCG/ABTSM3R2AcceptanceManifest.h"
#include "Planet/ABTSM2Planet.h"

namespace ABTSM3R3EncounterPrivate
{
constexpr uint64 Fnv1a64OffsetBasis = 14695981039346656037ull;
constexpr uint64 Fnv1a64Prime = 1099511628211ull;
constexpr int32 FlowQuantization =
	FABTSM3MonthlyRouteBuilder::FlowQuantization;
constexpr int32 EncounterIdBase = 310000;
constexpr int32 BeatIdBase = 320000;
constexpr int32 PocketIdBase = 330000;
constexpr int32 BiomeIdBase = 340000;
constexpr int32 EnvelopeIdBase = 350000;
constexpr int32 PocketStride = 16;
constexpr int32 BackgroundBiomeOrder = 6;

struct FCanonicalHash64
{
	uint64 Value = Fnv1a64OffsetBasis;

	void AddByte(const uint8 Byte)
	{
		Value ^= Byte;
		Value *= Fnv1a64Prime;
	}

	void AddBool(const bool bValue)
	{
		AddByte(bValue ? 1 : 0);
	}

	void AddInt32(const int32 Int)
	{
		const uint32 Bits = static_cast<uint32>(Int);
		for (int32 Shift = 0; Shift < 32; Shift += 8)
		{
			AddByte(static_cast<uint8>((Bits >> Shift) & 0xffu));
		}
	}

	void AddInt64(const int64 Int)
	{
		const uint64 Bits = static_cast<uint64>(Int);
		for (int32 Shift = 0; Shift < 64; Shift += 8)
		{
			AddByte(static_cast<uint8>((Bits >> Shift) & 0xffull));
		}
	}

	void AddUInt64(const uint64 Int)
	{
		AddInt64(static_cast<int64>(Int));
	}

	void AddName(const FName Name)
	{
		const FTCHARToUTF8 Utf8(*Name.ToString());
		AddInt32(Utf8.Length());
		for (int32 Index = 0; Index < Utf8.Length(); ++Index)
		{
			AddByte(static_cast<uint8>(Utf8.Get()[Index]));
		}
	}

	void AddVectorCM(const FVector& Vector)
	{
		AddInt32(FMath::RoundToInt(Vector.X * 10.0));
		AddInt32(FMath::RoundToInt(Vector.Y * 10.0));
		AddInt32(FMath::RoundToInt(Vector.Z * 10.0));
	}

	void AddIntArray(const TArray<int32>& Values)
	{
		AddInt32(Values.Num());
		for (const int32 Item : Values)
		{
			AddInt32(Item);
		}
	}

	uint64 Get() const
	{
		return Value;
	}
};

uint64 Mix64(uint64 Value)
{
	Value += 0x9e3779b97f4a7c15ull;
	Value = (Value ^ (Value >> 30)) * 0xbf58476d1ce4e5b9ull;
	Value = (Value ^ (Value >> 27)) * 0x94d049bb133111ebull;
	return Value ^ (Value >> 31);
}

bool IsFiniteVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X)
		&& FMath::IsFinite(Value.Y)
		&& FMath::IsFinite(Value.Z);
}

int32 EncounterId(const int32 Order)
{
	return EncounterIdBase + Order;
}

int32 BeatId(const int32 Order)
{
	return BeatIdBase + Order;
}

int32 PocketId(
	const int32 EncounterOrder,
	const EABTSM3PocketRole Role)
{
	return PocketIdBase + EncounterOrder * PocketStride
		+ static_cast<int32>(Role);
}

int32 BiomeId(const int32 Order)
{
	return BiomeIdBase + Order;
}

int32 EnvelopeId(const int32 Order)
{
	return EnvelopeIdBase + Order;
}

int32 RoleMask(const EABTSM3ActiveRole Role)
{
	return static_cast<int32>(Role);
}

bool IsValidBiome(const EABTSM3BiomeArchetype Biome)
{
	return static_cast<uint8>(Biome)
		<= static_cast<uint8>(EABTSM3BiomeArchetype::Background);
}

bool IsValidRevealPolicy(const EABTSM3MonthlyRevealPolicy Policy)
{
	return static_cast<uint8>(Policy)
		<= static_cast<uint8>(
			EABTSM3MonthlyRevealPolicy::ScoutRequired);
}

uint64 ComputeDescriptorHash(
	const FABTSM3MonthlyProfileDescriptorFixture& Descriptor)
{
	FCanonicalHash64 Hash;
	Hash.AddName(Descriptor.ProfileId);
	Hash.AddVectorCM(Descriptor.BoundsExtentCM);
	Hash.AddInt32(Descriptor.SilhouetteFamilyId);
	Hash.AddInt32(Descriptor.MaterialProfileId);
	Hash.AddInt32(Descriptor.WeaknessProfileId);
	return Hash.Get();
}

const TArray<FABTSM3MonthlyProfileDescriptorFixture>&
GetFixtureProfileCatalogStorage()
{
	static const TArray<FABTSM3MonthlyProfileDescriptorFixture> Catalog = []
	{
		const FName Names[] = {
			TEXT("M3R3_Fixture_LowWarehouse"),
			TEXT("M3R3_Fixture_OffsetTower"),
			TEXT("M3R3_Fixture_TwinPier"),
			TEXT("M3R3_Fixture_SteppedKeep"),
			TEXT("M3R3_Fixture_GravityArch"),
			TEXT("M3R3_Fixture_RelayFort")
		};
		const FVector Bounds[] = {
			FVector(250.0, 220.0, 220.0),
			FVector(240.0, 290.0, 330.0),
			FVector(330.0, 220.0, 260.0),
			FVector(260.0, 250.0, 410.0),
			FVector(290.0, 250.0, 360.0),
			FVector(350.0, 290.0, 470.0)
		};
		const int32 Silhouettes[] = { 0, 1, 2, 3, 1, 4 };
		const int32 Materials[] = { 0, 0, 1, 2, 3, 2 };
		const int32 Weaknesses[] = { 0, 1, 2, 1, 3, 4 };
		TArray<FABTSM3MonthlyProfileDescriptorFixture> Result;
		Result.Reserve(6);
		for (int32 Index = 0; Index < 6; ++Index)
		{
			FABTSM3MonthlyProfileDescriptorFixture Descriptor;
			Descriptor.ProfileId = Names[Index];
			Descriptor.BoundsExtentCM = Bounds[Index];
			Descriptor.SilhouetteFamilyId = Silhouettes[Index];
			Descriptor.MaterialProfileId = Materials[Index];
			Descriptor.WeaknessProfileId = Weaknesses[Index];
			Descriptor.DescriptorHash = static_cast<int64>(
				ComputeDescriptorHash(Descriptor));
			Result.Add(Descriptor);
		}
		return Result;
	}();
	return Catalog;
}

uint64 ComputeCatalogHash()
{
	FCanonicalHash64 Hash;
	const TArray<FABTSM3MonthlyProfileDescriptorFixture>& Catalog =
		GetFixtureProfileCatalogStorage();
	Hash.AddInt32(1);
	Hash.AddInt32(Catalog.Num());
	for (const FABTSM3MonthlyProfileDescriptorFixture& Descriptor :
		Catalog)
	{
		Hash.AddInt64(Descriptor.DescriptorHash);
	}
	return Hash.Get();
}

bool ValidateConfig(
	const FABTSM3MonthlyEncounterSpatialConfig& Config,
	FString& OutFailure)
{
	OutFailure.Reset();
	const int32 Count = Config.DestructibleEncounterCount;
	if (Count != 6
		|| Config.EncounterFlowQ.Num() != Count
		|| Config.TargetRoadDistanceWindowsCells.Num() != Count
		|| Config.DifficultyBands.Num() != Count
		|| Config.RevealPolicies.Num() != Count
		|| Config.EncounterBiomeArchetypes.Num() != Count
		|| Config.MinAdjacentEncounterProgressCM <= 0
		|| Config.MaxAdjacentEncounterProgressCM
			< Config.MinAdjacentEncounterProgressCM
		|| Config.MaxPlannedProgressDeviationCM <= 0
		|| Config.MaxPlannedProgressDeviationCM
			> Config.MinAdjacentEncounterProgressCM
		|| Config.TargetEnvelopeRadiusCells < 0
		|| Config.TargetEnvelopeRadiusCells > 4
		|| Config.TargetFootprintRadiusCells < 0
		|| Config.TargetFootprintRadiusCells > 4
		|| Config.TargetNoRoadRadiusCells < 1
		|| Config.TargetNoRoadRadiusCells > 5
		|| Config.TargetNoRoadRadiusCells
			<= Config.TargetFootprintRadiusCells
		|| Config.TargetEnvelopeRadiusCells
			< Config.TargetFootprintRadiusCells
		|| Config.PocketRadiusCells < 0
		|| Config.PocketRadiusCells > 4
		|| Config.RoadHalfWidthCM < 0
		|| Config.PadRoadBlendWidthCM < 0
		|| Config.FootprintSafetyMarginCM < 0
		|| Config.PlayableRouteRadiusCells <= 0
		|| Config.PlayableRouteRadiusCells > 6
		|| Config.PlayablePocketRadiusCells <= 0
		|| Config.PlayablePocketRadiusCells > 6
		|| Config.ActivePocketRadiusCells < 0
		|| Config.ActivePocketRadiusCells > 6
		|| Config.ActivePocketRadiusCells
			>= Config.PlayablePocketRadiusCells
		|| Config.PreRevealLeadCM < 100
		|| Config.ExitLeadCM < 100
		|| Config.MaxAnchorCandidatesPerEncounter < 8
		|| Config.MaxAnchorCandidatesPerEncounter > 2048
		|| Config.MaxSpatialBacktracksPerCandidate < 0
		|| Config.MaxSpatialBacktracksPerCandidate > 512
		|| Config.BaseTerrainCost < 0
		|| Config.HeightCostScale < 0
		|| Config.SlopeCostScale < 0
		|| Config.ScratchHeightScaleCM < 0
		|| Config.ExistingRoadReuseBias > 0
		|| Config.BackgroundWaterPermille < 0
		|| Config.BackgroundWaterPermille > 300
		|| Config.LegalCrossingHalfWidthCells <= 0
		|| Config.LegalCrossingHalfWidthCells > 8
		|| Config.MaxOptimizedPVSRaysPerWorld <= 0
		|| Config.MaxOptimizedPVSRaysPerWorld > 4096
		|| Config.OptimizedTraceSamples < 4
		|| Config.OptimizedTraceSamples > 128
		|| Config.ReferenceTraceSamples < 8
		|| Config.ReferenceTraceSamples > 256
		|| Config.ReferenceTraceSamples
			< Config.OptimizedTraceSamples
		|| Config.DefaultOrbitDistanceCM <= 0
		|| Config.MaxOrbitDistanceCM
			<= Config.DefaultOrbitDistanceCM
		|| Config.CameraElevationDegrees <= 0
		|| Config.CameraElevationDegrees >= 90
		|| Config.ObserverCharacterCenterHeightCM < 0
		|| Config.ObserverLookAtHeightCM < 0
		|| Config.TargetCenterHeightCM < 0
		|| Config.VisibilityOcclusionEpsilonCM < 0
		|| Config.AttackReadableMaxDistanceCM < 100
		|| Config.LandmarkMaxDistanceCM < 100
		|| Config.LandmarkMaxDistanceCM
			< Config.AttackReadableMaxDistanceCM
		|| Config.ScoutDetectionRadiusCM < 100
		|| Config.MinActiveRoleCoveragePermille < 0
		|| Config.MinActiveRoleCoveragePermille > 1000
		|| Config.MaxDeepWildPermille < 0
		|| Config.MaxDeepWildPermille > 1000
		|| Config.MinEncounterBiomeArchetypes < 1
		|| Config.MinEncounterBiomeArchetypes > Count
		|| Config.ReservationPlannerVersion != 1
		|| Config.TerrainScratchVersion != 1
		|| Config.HydrologyScratchVersion != 1
		|| Config.BiomePlannerVersion != 1
		|| Config.CameraSampleSetVersion != 2
		|| Config.SpatialScoreVersion != 1)
	{
		OutFailure = TEXT("SpatialConfigDomain");
		return false;
	}

	int32 PreviousFlow = INDEX_NONE;
	int32 PreviousDifficulty = INDEX_NONE;
	int32 StrictDifficultyRises = 0;
	TSet<uint8> EncounterBiomes;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const int32 FlowQ = Config.EncounterFlowQ[Index];
		const FIntPoint Window =
			Config.TargetRoadDistanceWindowsCells[Index];
		const int32 Difficulty = Config.DifficultyBands[Index];
		if (FlowQ <= 0
			|| FlowQ >= FlowQuantization
			|| (PreviousFlow != INDEX_NONE
				&& FlowQ <= PreviousFlow)
			|| Window.X < 3
			|| Window.Y < Window.X
			|| Difficulty < 0
			|| (PreviousDifficulty != INDEX_NONE
				&& Difficulty < PreviousDifficulty)
			|| !IsValidRevealPolicy(Config.RevealPolicies[Index])
			|| !IsValidBiome(Config.EncounterBiomeArchetypes[Index])
			|| Config.EncounterBiomeArchetypes[Index]
				== EABTSM3BiomeArchetype::Background)
		{
			OutFailure = FString::Printf(
				TEXT("SpatialCatalog:%d"),
				Index);
			return false;
		}
		if (PreviousDifficulty != INDEX_NONE
			&& Difficulty > PreviousDifficulty)
		{
			++StrictDifficultyRises;
		}
		PreviousFlow = FlowQ;
		PreviousDifficulty = Difficulty;
		EncounterBiomes.Add(static_cast<uint8>(
			Config.EncounterBiomeArchetypes[Index]));
	}
	if (StrictDifficultyRises < 3
		|| EncounterBiomes.Num()
			< Config.MinEncounterBiomeArchetypes)
	{
		OutFailure = TEXT("SpatialCatalogDiversity");
		return false;
	}
	return true;
}

void ExpandRing(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<int32>& SeedCells,
	const int32 Radius,
	TArray<int32>& OutCells)
{
	OutCells.Reset();
	if (Radius < 0 || Cells.IsEmpty())
	{
		return;
	}
	TBitArray<> Visited(false, Cells.Num());
	TArray<int32> Queue;
	TArray<int32> QueueDistance;
	for (const int32 Seed : SeedCells)
	{
		if (Cells.IsValidIndex(Seed) && !Visited[Seed])
		{
			Visited[Seed] = true;
			Queue.Add(Seed);
			QueueDistance.Add(0);
		}
	}
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 CellId = Queue[Head];
		const int32 Distance = QueueDistance[Head];
		if (Distance >= Radius)
		{
			continue;
		}
		for (const int32 NeighborId :
			Cells[CellId].NeighborCellIds)
		{
			if (!Visited[NeighborId])
			{
				Visited[NeighborId] = true;
				Queue.Add(NeighborId);
				QueueDistance.Add(Distance + 1);
			}
		}
	}
	OutCells = MoveTemp(Queue);
	OutCells.Sort();
}

void ExpandRing(
	const TArray<FABTSM2Cell>& Cells,
	const int32 SeedCell,
	const int32 Radius,
	TArray<int32>& OutCells)
{
	TArray<int32> Seeds;
	Seeds.Add(SeedCell);
	ExpandRing(Cells, Seeds, Radius, OutCells);
}

bool IsStrictlySortedUnique(const TArray<int32>& Values)
{
	for (int32 Index = 0; Index < Values.Num(); ++Index)
	{
		if (Values[Index] < 0
			|| (Index > 0 && Values[Index] <= Values[Index - 1]))
		{
			return false;
		}
	}
	return true;
}

int32 FindRouteIndexAtProgress(
	const FABTSM3MonthlyRouteCandidate& Route,
	const int32 ProgressCM)
{
	if (Route.ProgressDistanceCM.IsEmpty())
	{
		return INDEX_NONE;
	}
	const int32 Clamped = FMath::Clamp(
		ProgressCM,
		0,
		Route.ProgressDistanceCM.Last());
	const int32 Lower = Algo::LowerBound(
		Route.ProgressDistanceCM,
		Clamped);
	if (Lower <= 0)
	{
		return 0;
	}
	if (Lower >= Route.ProgressDistanceCM.Num())
	{
		return Route.ProgressDistanceCM.Num() - 1;
	}
	const int32 Previous = Lower - 1;
	return FMath::Abs(Route.ProgressDistanceCM[Previous] - Clamped)
		<= FMath::Abs(Route.ProgressDistanceCM[Lower] - Clamped)
		? Previous
		: Lower;
}

float SurfaceArcDistanceCM(
	const TArray<FABTSM2Cell>& Cells,
	const int32 CellA,
	const int32 CellB,
	const float PlanetRadiusCM)
{
	if (!Cells.IsValidIndex(CellA)
		|| !Cells.IsValidIndex(CellB)
		|| !FMath::IsFinite(PlanetRadiusCM)
		|| PlanetRadiusCM <= 0.0f)
	{
		return TNumericLimits<float>::Max();
	}
	return FMath::Acos(FMath::Clamp(
		FVector::DotProduct(
			Cells[CellA].UnitCenter,
			Cells[CellB].UnitCenter),
		-1.0,
		1.0)) * PlanetRadiusCM;
}

int32 FindSemanticPreRevealRouteIndex(
	const FABTSM3MonthlyRouteCandidate& Route,
	const TArray<FABTSM2Cell>& Cells,
	const int32 TargetCellId,
	const int32 ArrivalProgressCM,
	const int32 PreRevealLeadCM,
	const int32 AttackReadableMaxDistanceCM,
	const float PlanetRadiusCM)
{
	if (!Cells.IsValidIndex(TargetCellId)
		|| Route.OrderedRoadCellIds.Num()
			!= Route.ProgressDistanceCM.Num())
	{
		return INDEX_NONE;
	}
	const int32 MaximumProgressCM =
		ArrivalProgressCM - PreRevealLeadCM;
	if (MaximumProgressCM < 0)
	{
		return INDEX_NONE;
	}
	int32 RouteIndex = FindRouteIndexAtProgress(
		Route,
		MaximumProgressCM);
	while (Route.ProgressDistanceCM.IsValidIndex(RouteIndex)
		&& Route.ProgressDistanceCM[RouteIndex]
			> MaximumProgressCM)
	{
		--RouteIndex;
	}
	for (; RouteIndex >= 0; --RouteIndex)
	{
		const int32 ObserverCellId =
			Route.OrderedRoadCellIds[RouteIndex];
		if (SurfaceArcDistanceCM(
				Cells,
				ObserverCellId,
				TargetCellId,
				PlanetRadiusCM)
			> AttackReadableMaxDistanceCM)
		{
			return RouteIndex;
		}
	}
	return INDEX_NONE;
}

int32 FindSemanticExitRouteIndex(
	const FABTSM3MonthlyRouteCandidate& Route,
	const int32 ArrivalRouteIndex,
	const int32 ArrivalProgressCM,
	const int32 ExitLeadCM)
{
	if (!Route.OrderedRoadCellIds.IsValidIndex(
			ArrivalRouteIndex))
	{
		return INDEX_NONE;
	}
	int32 RouteIndex = FindRouteIndexAtProgress(
		Route,
		FMath::Min(
			Route.Metrics.RouteLengthCM,
			ArrivalProgressCM + ExitLeadCM));
	RouteIndex = FMath::Max(
		RouteIndex,
		ArrivalRouteIndex + 1);
	const int32 ArrivalCellId =
		Route.OrderedRoadCellIds[ArrivalRouteIndex];
	while (Route.OrderedRoadCellIds.IsValidIndex(RouteIndex)
		&& Route.OrderedRoadCellIds[RouteIndex]
			== ArrivalCellId)
	{
		++RouteIndex;
	}
	return Route.OrderedRoadCellIds.IsValidIndex(RouteIndex)
		? RouteIndex
		: INDEX_NONE;
}

bool BuildNearestRouteFields(
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlyRouteCandidate& Route,
	TArray<int32>& OutDistance,
	TArray<int32>& OutNearestRouteIndex)
{
	OutDistance.Init(MAX_int32, Cells.Num());
	OutNearestRouteIndex.Init(INDEX_NONE, Cells.Num());
	TArray<int32> Queue;
	Queue.Reserve(Cells.Num());
	for (int32 RouteIndex = 0;
		RouteIndex < Route.OrderedRoadCellIds.Num();
		++RouteIndex)
	{
		const int32 CellId = Route.OrderedRoadCellIds[RouteIndex];
		if (!Cells.IsValidIndex(CellId))
		{
			return false;
		}
		if (OutDistance[CellId] == MAX_int32)
		{
			OutDistance[CellId] = 0;
			OutNearestRouteIndex[CellId] = RouteIndex;
			Queue.Add(CellId);
		}
		else
		{
			OutNearestRouteIndex[CellId] = FMath::Min(
				OutNearestRouteIndex[CellId],
				RouteIndex);
		}
	}
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 CellId = Queue[Head];
		for (const int32 NeighborId :
			Cells[CellId].NeighborCellIds)
		{
			const int32 NextDistance = OutDistance[CellId] + 1;
			if (NextDistance < OutDistance[NeighborId])
			{
				OutDistance[NeighborId] = NextDistance;
				OutNearestRouteIndex[NeighborId] =
					OutNearestRouteIndex[CellId];
				Queue.Add(NeighborId);
			}
			else if (NextDistance == OutDistance[NeighborId]
				&& OutNearestRouteIndex[CellId]
					< OutNearestRouteIndex[NeighborId])
			{
				OutNearestRouteIndex[NeighborId] =
					OutNearestRouteIndex[CellId];
			}
		}
	}
	return true;
}

TArray<int32> BuildPathToRoad(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<int32>& RoadDistance,
	const int32 TargetCellId)
{
	TArray<int32> Path;
	if (!Cells.IsValidIndex(TargetCellId)
		|| !RoadDistance.IsValidIndex(TargetCellId)
		|| RoadDistance[TargetCellId] == MAX_int32)
	{
		return Path;
	}
	int32 Current = TargetCellId;
	Path.Add(Current);
	while (RoadDistance[Current] > 0)
	{
		int32 Next = INDEX_NONE;
		for (const int32 NeighborId :
			Cells[Current].NeighborCellIds)
		{
			if (RoadDistance[NeighborId]
					== RoadDistance[Current] - 1
				&& (Next == INDEX_NONE || NeighborId < Next))
			{
				Next = NeighborId;
			}
		}
		if (Next == INDEX_NONE)
		{
			Path.Reset();
			return Path;
		}
		Current = Next;
		Path.Add(Current);
	}
	return Path;
}

bool BuildFinalPathToPlannedRoute(
	const FABTSM3MonthlyEncounterSpatialConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlySpatialCandidate& Candidate,
	const FABTSM3MonthlySpatialEncounter& Encounter,
	const TBitArray<>& ReservedCells,
	const TArray<int32>& RouteIndexByCell,
	const int32 DesiredProgressCM,
	const int32 PreviousProgressCM,
	TArray<int32>& OutPath,
	int32& OutArrivalRouteIndex)
{
	OutPath.Reset();
	OutArrivalRouteIndex = INDEX_NONE;
	const int32 TargetCellId = Encounter.TargetAnchorCellId;
	if (!Cells.IsValidIndex(TargetCellId)
		|| Candidate.Cells.Num() != Cells.Num()
		|| ReservedCells.Num() != Cells.Num()
		|| RouteIndexByCell.Num() != Cells.Num()
		|| !ReservedCells[TargetCellId])
	{
		return false;
	}

	TArray<int32> ParentCell;
	ParentCell.Init(INDEX_NONE, Cells.Num());
	TArray<int32> PathDistance;
	PathDistance.Init(MAX_int32, Cells.Num());
	TArray<int32> Queue;
	ParentCell[TargetCellId] = TargetCellId;
	PathDistance[TargetCellId] = 0;
	Queue.Add(TargetCellId);
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 CellId = Queue[Head];
		for (const int32 NeighborId :
			Cells[CellId].NeighborCellIds)
		{
			if (!Cells.IsValidIndex(NeighborId)
				|| ParentCell[NeighborId] != INDEX_NONE
				|| !ReservedCells[NeighborId]
				|| Candidate.Cells[NeighborId].bWater
				|| (Candidate.Cells[NeighborId].
						bTargetFootprint
					&& !Encounter.TargetFootprintCellIds.
						Contains(NeighborId))
				|| (Candidate.Cells[NeighborId].bNoRoad
					&& !Encounter.TargetNoRoadCellIds.
						Contains(NeighborId)))
			{
				continue;
			}
			ParentCell[NeighborId] = CellId;
			PathDistance[NeighborId] =
				PathDistance[CellId] + 1;
			if (RouteIndexByCell[NeighborId] == INDEX_NONE)
			{
				Queue.Add(NeighborId);
			}
		}
	}

	int64 BestScore = MAX_int64;
	for (int32 RouteIndex = 0;
		RouteIndex
			< Candidate.RecomputedRoute.OrderedRoadCellIds.Num();
		++RouteIndex)
	{
		const int32 RouteCellId =
			Candidate.RecomputedRoute.OrderedRoadCellIds[
				RouteIndex];
		if (!ParentCell.IsValidIndex(RouteCellId)
			|| ParentCell[RouteCellId] == INDEX_NONE
			|| PathDistance[RouteCellId] < 3
			|| !Candidate.RecomputedRoute.ProgressDistanceCM.
				IsValidIndex(RouteIndex))
		{
			continue;
		}
		const int32 ProgressCM =
			Candidate.RecomputedRoute.ProgressDistanceCM[
				RouteIndex];
		const int32 PlannedDeviationCM =
			FMath::Abs(ProgressCM - DesiredProgressCM);
		if (PlannedDeviationCM
			> Config.MaxPlannedProgressDeviationCM)
		{
			continue;
		}
		if (PreviousProgressCM != INDEX_NONE)
		{
			const int32 Gap = ProgressCM - PreviousProgressCM;
			if (Gap < Config.MinAdjacentEncounterProgressCM
				|| Gap > Config.MaxAdjacentEncounterProgressCM)
			{
				continue;
			}
		}
		const int64 Score =
			static_cast<int64>(PlannedDeviationCM)
				* 100000
			+ static_cast<int64>(PathDistance[RouteCellId])
				* 100
			+ RouteIndex;
		if (Score < BestScore)
		{
			BestScore = Score;
			OutArrivalRouteIndex = RouteIndex;
		}
	}
	if (!Candidate.RecomputedRoute.OrderedRoadCellIds.
			IsValidIndex(OutArrivalRouteIndex))
	{
		return false;
	}

	int32 Current =
		Candidate.RecomputedRoute.OrderedRoadCellIds[
			OutArrivalRouteIndex];
	while (Current != TargetCellId)
	{
		OutPath.Add(Current);
		Current = ParentCell.IsValidIndex(Current)
			? ParentCell[Current]
			: INDEX_NONE;
		if (!Cells.IsValidIndex(Current))
		{
			OutPath.Reset();
			OutArrivalRouteIndex = INDEX_NONE;
			return false;
		}
	}
	OutPath.Add(TargetCellId);
	Algo::Reverse(OutPath);
	return OutPath.Num() >= 4;
}

float AverageNeighborArcCM(
	const TArray<FABTSM2Cell>& Cells,
	const int32 CellId,
	const float PlanetRadiusCM)
{
	if (!Cells.IsValidIndex(CellId)
		|| Cells[CellId].NeighborCellIds.IsEmpty())
	{
		return 0.0f;
	}
	double Sum = 0.0;
	for (const int32 NeighborId :
		Cells[CellId].NeighborCellIds)
	{
		Sum += FMath::Acos(FMath::Clamp(
			FVector::DotProduct(
				Cells[CellId].UnitCenter,
				Cells[NeighborId].UnitCenter),
			-1.0,
			1.0)) * PlanetRadiusCM;
	}
	return static_cast<float>(
		Sum / Cells[CellId].NeighborCellIds.Num());
}

FABTSM3PocketContract MakePocket(
	const int32 EncounterOrder,
	const EABTSM3PocketRole Role,
	const int32 AnchorCellId,
	const TArray<int32>& CellIds)
{
	FABTSM3PocketContract Pocket;
	Pocket.PocketId = PocketId(EncounterOrder, Role);
	Pocket.EncounterId = EncounterId(EncounterOrder);
	Pocket.RouteBeatId = BeatId(EncounterOrder);
	Pocket.Role = Role;
	Pocket.AnchorCellId = AnchorCellId;
	Pocket.CellIds = CellIds;
	Pocket.Resolution = EABTSM3SchemaResolution::Reserved;
	return Pocket;
}

FABTSM3PocketContract* FindPocket(
	TArray<FABTSM3PocketContract>& Pockets,
	const int32 Id)
{
	return Pockets.FindByPredicate(
		[Id](const FABTSM3PocketContract& Pocket)
		{
			return Pocket.PocketId == Id;
		});
}

const FABTSM3PocketContract* FindPocket(
	const TArray<FABTSM3PocketContract>& Pockets,
	const int32 Id)
{
	return Pockets.FindByPredicate(
		[Id](const FABTSM3PocketContract& Pocket)
		{
			return Pocket.PocketId == Id;
		});
}

int32 PocketRoleMask(const EABTSM3PocketRole Role)
{
	switch (Role)
	{
	case EABTSM3PocketRole::RoadArrival:
		return RoleMask(EABTSM3ActiveRole::RoadArrival);
	case EABTSM3PocketRole::ScoutReveal:
		return RoleMask(EABTSM3ActiveRole::Reveal);
	case EABTSM3PocketRole::Slingshot:
		return RoleMask(EABTSM3ActiveRole::Slingshot);
	case EABTSM3PocketRole::TargetEnvelope:
	case EABTSM3PocketRole::TargetAnchor:
		return RoleMask(EABTSM3ActiveRole::Target);
	case EABTSM3PocketRole::Reward:
		return RoleMask(EABTSM3ActiveRole::Reward);
	case EABTSM3PocketRole::Exit:
		return RoleMask(EABTSM3ActiveRole::Exit);
	default:
		return 0;
	}
}

void AddRole(
	FABTSM3MonthlySpatialCell& Cell,
	const int32 Mask)
{
	Cell.ActiveRoleMask |= Mask;
}

bool IntersectsAny(
	const TArray<int32>& SortedCells,
	const TSet<int32>& UsedCells)
{
	for (const int32 CellId : SortedCells)
	{
		if (UsedCells.Contains(CellId))
		{
			return true;
		}
	}
	return false;
}

EABTSM3TerrainType TerrainForBiome(
	const EABTSM3BiomeArchetype Biome)
{
	switch (Biome)
	{
	case EABTSM3BiomeArchetype::Forest:
		return EABTSM3TerrainType::Forest;
	case EABTSM3BiomeArchetype::Highland:
		return EABTSM3TerrainType::Highland;
	case EABTSM3BiomeArchetype::Mountain:
		return EABTSM3TerrainType::Mountain;
	case EABTSM3BiomeArchetype::Water:
		return EABTSM3TerrainType::Water;
	default:
		return EABTSM3TerrainType::Plain;
	}
}

int32 BiomeBaseHeightQ(const EABTSM3BiomeArchetype Biome)
{
	switch (Biome)
	{
	case EABTSM3BiomeArchetype::Plain:
		return 300000;
	case EABTSM3BiomeArchetype::Forest:
		return 390000;
	case EABTSM3BiomeArchetype::Highland:
		return 570000;
	case EABTSM3BiomeArchetype::Mountain:
		return 720000;
	case EABTSM3BiomeArchetype::Water:
		return 180000;
	case EABTSM3BiomeArchetype::Background:
	default:
		return 260000;
	}
}

int32 FindEncounterBiomeOrder(
	const FABTSM3MonthlyEncounterSpatialConfig& Config,
	const int32 FlowQ)
{
	int32 BestOrder = 0;
	int32 BestDifference = MAX_int32;
	for (int32 Order = 0;
		Order < Config.EncounterFlowQ.Num();
		++Order)
	{
		const int32 Difference =
			FMath::Abs(Config.EncounterFlowQ[Order] - FlowQ);
		if (Difference < BestDifference)
		{
			BestDifference = Difference;
			BestOrder = Order;
		}
	}
	return BestOrder;
}

uint64 ComputeReservationHash(
	const FABTSM3MonthlySpatialCandidate& Candidate)
{
	FCanonicalHash64 Hash;
	Hash.AddInt32(3);
	Hash.AddInt32(Candidate.SourceRouteCandidateId);
	Hash.AddInt64(Candidate.SourceRouteCandidateHash);
	Hash.AddInt32(Candidate.Encounters.Num());
	for (const FABTSM3MonthlySpatialEncounter& Encounter :
		Candidate.Encounters)
	{
		Hash.AddInt32(Encounter.Contract.EncounterId);
		Hash.AddInt32(Encounter.Contract.OrderIndex);
		Hash.AddInt32(static_cast<int32>(Encounter.RevealPolicy));
		Hash.AddInt32(Encounter.TargetAnchorCellId);
		Hash.AddInt32(Encounter.RequiredRoadClearanceCells);
		Hash.AddIntArray(Encounter.TargetFootprintCellIds);
		Hash.AddIntArray(Encounter.TargetNoRoadCellIds);
		Hash.AddName(Encounter.ResolvedFixtureProfileId);
		Hash.AddInt64(Encounter.ProfileCatalogHash);
	}
	Hash.AddIntArray(
		Candidate.PreRoadReservedPlayableCellIds);
	return Hash.Get();
}

bool ReserveEncounters(
	const int32 WorldSeed,
	const int32 ReservationVariant,
	const FABTSM3MonthlyEncounterSpatialConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	const FABTSM3MonthlyRouteCandidate& SourceRoute,
	const uint64 ProfileCatalogHash,
	FABTSM3MonthlySpatialCandidate& OutCandidate,
	EABTSM3MonthlySpatialRejectReason& OutReason,
	FString& OutFailure)
{
	OutCandidate = FABTSM3MonthlySpatialCandidate();
	OutCandidate.SourceRouteCandidateId = SourceRoute.CandidateId;
	OutCandidate.SourceRouteCandidateHash =
		SourceRoute.CandidateHash;
	OutReason = EABTSM3MonthlySpatialRejectReason::None;
	OutFailure.Reset();

	TArray<int32> RoadDistance;
	TArray<int32> NearestRouteIndex;
	if (!BuildNearestRouteFields(
			Cells,
			SourceRoute,
			RoadDistance,
			NearestRouteIndex))
	{
		OutReason =
			EABTSM3MonthlySpatialRejectReason::ReservationFailed;
		OutFailure = TEXT("NearestRouteFields");
		return false;
	}
	OutCandidate.Cells.SetNum(Cells.Num());
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		FABTSM3MonthlySpatialCell& Cell =
			OutCandidate.Cells[CellId];
		Cell.CellId = CellId;
		Cell.MainRoadDistanceCells = RoadDistance[CellId];
		Cell.NearestRoadOrderIndex = NearestRouteIndex[CellId];
		if (SourceRoute.ProgressDistanceCM.IsValidIndex(
				NearestRouteIndex[CellId]))
		{
			Cell.FlowQ = SourceRoute.Metrics.RouteLengthCM > 0
				? FMath::Clamp(
					static_cast<int32>(
						static_cast<int64>(
							SourceRoute.ProgressDistanceCM[
								NearestRouteIndex[CellId]])
							* FlowQuantization
							/ SourceRoute.Metrics.RouteLengthCM),
					0,
					FlowQuantization)
				: 0;
		}
	}

	const TArray<FABTSM3MonthlyProfileDescriptorFixture>& Catalog =
		GetFixtureProfileCatalogStorage();
	TSet<int32> UsedConstructionCells;
	for (int32 Order = 0;
		Order < Config.DestructibleEncounterCount;
		++Order)
	{
		const int32 DesiredProgressCM = static_cast<int32>(
			static_cast<int64>(SourceRoute.Metrics.RouteLengthCM)
			* Config.EncounterFlowQ[Order]
			/ FlowQuantization);
		const int32 DesiredRouteIndex = FindRouteIndexAtProgress(
			SourceRoute,
			DesiredProgressCM);
		if (!SourceRoute.OrderedRoadCellIds.IsValidIndex(
			DesiredRouteIndex))
		{
			OutReason =
				EABTSM3MonthlySpatialRejectReason::ReservationFailed;
			OutFailure = FString::Printf(
				TEXT("DesiredRoute:%d"),
				Order);
			return false;
		}
		const FABTSM3MonthlyProfileDescriptorFixture& Descriptor =
			Catalog[Order];
		const FIntPoint Window =
			Config.TargetRoadDistanceWindowsCells[Order];

		int32 BestCellId = INDEX_NONE;
		int32 BestRequiredClearanceCells = 0;
		int64 BestScore = MAX_int64;
		int32 EvaluatedCandidates = 0;
		TArray<int32> BestEnvelope;
		TArray<int32> BestFootprint;
		TArray<int32> BestNoRoad;
		for (int32 CellId = 0;
			CellId < Cells.Num()
				&& EvaluatedCandidates
					< Config.MaxAnchorCandidatesPerEncounter;
			++CellId)
		{
			if (!NearestRouteIndex.IsValidIndex(CellId)
				|| !SourceRoute.ProgressDistanceCM.IsValidIndex(
					NearestRouteIndex[CellId]))
			{
				continue;
			}
			const int32 ProgressDifference = FMath::Abs(
				SourceRoute.ProgressDistanceCM[
					NearestRouteIndex[CellId]]
				- DesiredProgressCM);
			if (ProgressDifference > 1800)
			{
				continue;
			}
			const int32 CandidateArrivalRouteIndex =
				NearestRouteIndex[CellId];
			const int32 CandidateArrivalProgressCM =
				SourceRoute.ProgressDistanceCM[
					CandidateArrivalRouteIndex];
			const float StartToTargetArcCM =
				SurfaceArcDistanceCM(
					Cells,
					SourceRoute.OrderedRoadCellIds[0],
					CellId,
					PlanetRadiusCM);
			if ((Order == 0
					&& StartToTargetArcCM
						> Config.AttackReadableMaxDistanceCM)
				|| (Order > 0
					&& StartToTargetArcCM
						<= Config.AttackReadableMaxDistanceCM)
				|| (Order > 0
					&& FindSemanticPreRevealRouteIndex(
							SourceRoute,
							Cells,
							CellId,
							CandidateArrivalProgressCM,
							Config.PreRevealLeadCM,
							Config.AttackReadableMaxDistanceCM,
							PlanetRadiusCM)
						== INDEX_NONE)
				|| FindSemanticExitRouteIndex(
						SourceRoute,
						CandidateArrivalRouteIndex,
						CandidateArrivalProgressCM,
						Config.ExitLeadCM)
					== INDEX_NONE)
			{
				continue;
			}
			const float CellArcCM = AverageNeighborArcCM(
				Cells,
				CellId,
				PlanetRadiusCM);
			if (CellArcCM <= 1.0f)
			{
				continue;
			}
			const float HorizontalExtent = FMath::Max(
				Descriptor.BoundsExtentCM.X,
				Descriptor.BoundsExtentCM.Y);
			const int32 ContinuousClearanceCells = FMath::CeilToInt(
				(HorizontalExtent
					+ Config.RoadHalfWidthCM
					+ Config.PadRoadBlendWidthCM
					+ Config.FootprintSafetyMarginCM)
				/ CellArcCM);
			const int32 MinDistance = FMath::Max(
				Window.X,
				ContinuousClearanceCells);
			if (RoadDistance[CellId] < MinDistance
				|| RoadDistance[CellId] > Window.Y)
			{
				continue;
			}
			++EvaluatedCandidates;
			const int32 PreferredDistance =
				MinDistance
				+ static_cast<int32>(
					Mix64(
						static_cast<uint32>(WorldSeed)
						^ static_cast<uint64>(Order * 7919)
						^ static_cast<uint64>(
							ReservationVariant + 1)
							* 0x9e3779b97f4a7c15ull)
					% static_cast<uint64>(
						Window.Y - MinDistance + 1));
			const int64 Tie = static_cast<int64>(
				Mix64(
					static_cast<uint32>(WorldSeed)
					^ static_cast<uint64>(
						SourceRoute.CandidateId * 104729)
					^ static_cast<uint64>(Order * 8191)
					^ static_cast<uint64>(CellId)
					^ static_cast<uint64>(
						ReservationVariant + 1)
						* 0xbf58476d1ce4e5b9ull)
				& 0xffffull);
			const int64 Score =
				static_cast<int64>(ProgressDifference) * 1000
				+ static_cast<int64>(
					FMath::Abs(
						RoadDistance[CellId]
						- PreferredDistance)) * 100000
				+ Tie;
			if (Score >= BestScore)
			{
				continue;
			}
			TArray<int32> NoRoadCells;
			ExpandRing(
				Cells,
				CellId,
				Config.TargetNoRoadRadiusCells,
				NoRoadCells);
			bool bTouchesRoad = false;
			for (const int32 NoRoadCellId : NoRoadCells)
			{
				if (RoadDistance[NoRoadCellId] == 0)
				{
					bTouchesRoad = true;
					break;
				}
			}
			if (bTouchesRoad
				|| IntersectsAny(
					NoRoadCells,
					UsedConstructionCells))
			{
				continue;
			}
			TArray<int32> EnvelopeCells;
			TArray<int32> FootprintCells;
			ExpandRing(
				Cells,
				CellId,
				Config.TargetEnvelopeRadiusCells,
				EnvelopeCells);
			ExpandRing(
				Cells,
				CellId,
				Config.TargetFootprintRadiusCells,
				FootprintCells);
			BestScore = Score;
			BestCellId = CellId;
			BestRequiredClearanceCells = MinDistance;
			BestEnvelope = MoveTemp(EnvelopeCells);
			BestFootprint = MoveTemp(FootprintCells);
			BestNoRoad = MoveTemp(NoRoadCells);
		}
		if (BestCellId == INDEX_NONE)
		{
			OutReason =
				EABTSM3MonthlySpatialRejectReason::ReservationFailed;
			OutFailure = FString::Printf(
				TEXT("TargetEnvelope:%d"),
				Order);
			return false;
		}
		for (const int32 CellId : BestNoRoad)
		{
			UsedConstructionCells.Add(CellId);
			OutCandidate.Cells[CellId].bNoRoad = true;
		}
		for (const int32 CellId : BestFootprint)
		{
			OutCandidate.Cells[CellId].bTargetFootprint = true;
		}

		const TArray<int32> SidePath = BuildPathToRoad(
			Cells,
			RoadDistance,
			BestCellId);
		if (SidePath.Num() < 4)
		{
			OutReason =
				EABTSM3MonthlySpatialRejectReason::ReservationFailed;
			OutFailure = FString::Printf(
				TEXT("SidePath:%d"),
				Order);
			return false;
		}
		const int32 RoadArrivalCellId = SidePath.Last();
		const int32 RevealCellId =
			SidePath[SidePath.Num() - 2];
		const int32 SlingshotCellId =
			SidePath[SidePath.Num() - 3];
		const int32 ArrivalRouteIndex =
			NearestRouteIndex[RoadArrivalCellId];
		const int32 ArrivalProgressCM =
			SourceRoute.ProgressDistanceCM.IsValidIndex(
				ArrivalRouteIndex)
			? SourceRoute.ProgressDistanceCM[ArrivalRouteIndex]
			: DesiredProgressCM;
		const float StartToTargetArcCM =
			SurfaceArcDistanceCM(
				Cells,
				SourceRoute.OrderedRoadCellIds[0],
				BestCellId,
				PlanetRadiusCM);
		if ((Order == 0
				&& StartToTargetArcCM
					> Config.AttackReadableMaxDistanceCM)
			|| (Order > 0
				&& StartToTargetArcCM
					<= Config.AttackReadableMaxDistanceCM))
		{
			OutReason =
				EABTSM3MonthlySpatialRejectReason::ReservationFailed;
			OutFailure = FString::Printf(
				TEXT("StartSemantic:%d"),
				Order);
			return false;
		}
		const int32 PreRevealRouteIndex = Order == 0
			? FindRouteIndexAtProgress(
				SourceRoute,
				FMath::Max(
					0,
					ArrivalProgressCM
						- Config.PreRevealLeadCM))
			: FindSemanticPreRevealRouteIndex(
				SourceRoute,
				Cells,
				BestCellId,
				ArrivalProgressCM,
				Config.PreRevealLeadCM,
				Config.AttackReadableMaxDistanceCM,
				PlanetRadiusCM);
		const int32 ExitRouteIndex =
			FindSemanticExitRouteIndex(
			SourceRoute,
			ArrivalRouteIndex,
			ArrivalProgressCM,
			Config.ExitLeadCM);
		if (!SourceRoute.OrderedRoadCellIds.IsValidIndex(
				PreRevealRouteIndex)
			|| !SourceRoute.OrderedRoadCellIds.IsValidIndex(
				ExitRouteIndex))
		{
			OutReason =
				EABTSM3MonthlySpatialRejectReason::ReservationFailed;
			OutFailure = FString::Printf(
				TEXT("ObserverRoute:%d"),
				Order);
			return false;
		}
		const int32 ExitCellId =
			SourceRoute.OrderedRoadCellIds[ExitRouteIndex];
		int32 RewardCellId = INDEX_NONE;
		for (const int32 NeighborId :
			Cells[BestCellId].NeighborCellIds)
		{
			if (NeighborId != SlingshotCellId
				&& NeighborId != RevealCellId
				&& NeighborId != RoadArrivalCellId)
			{
				RewardCellId = NeighborId;
				break;
			}
		}
		if (RewardCellId == INDEX_NONE
			|| ExitRouteIndex <= ArrivalRouteIndex
			|| ExitCellId == RoadArrivalCellId)
		{
			OutReason =
				EABTSM3MonthlySpatialRejectReason::ReservationFailed;
			OutFailure = FString::Printf(
				TEXT("PocketAnchors:%d"),
				Order);
			return false;
		}

		TArray<int32> OneCell;
		TArray<int32> RoadArrivalCells;
		TArray<int32> RevealCells;
		TArray<int32> SlingshotCells;
		TArray<int32> RewardCells;
		TArray<int32> ExitCells;
		ExpandRing(
			Cells,
			RoadArrivalCellId,
			Config.PocketRadiusCells,
			RoadArrivalCells);
		ExpandRing(
			Cells,
			RevealCellId,
			Config.PocketRadiusCells,
			RevealCells);
		ExpandRing(
			Cells,
			SlingshotCellId,
			Config.PocketRadiusCells,
			SlingshotCells);
		ExpandRing(
			Cells,
			RewardCellId,
			Config.PocketRadiusCells,
			RewardCells);
		ExpandRing(
			Cells,
			ExitCellId,
			Config.PocketRadiusCells,
			ExitCells);
		OutCandidate.Pockets.Add(MakePocket(
			Order,
			EABTSM3PocketRole::RoadArrival,
			RoadArrivalCellId,
			RoadArrivalCells));
		OutCandidate.Pockets.Add(MakePocket(
			Order,
			EABTSM3PocketRole::ScoutReveal,
			RevealCellId,
			RevealCells));
		OutCandidate.Pockets.Add(MakePocket(
			Order,
			EABTSM3PocketRole::Slingshot,
			SlingshotCellId,
			SlingshotCells));
		OutCandidate.Pockets.Add(MakePocket(
			Order,
			EABTSM3PocketRole::TargetEnvelope,
			BestCellId,
			BestEnvelope));
		OneCell.Reset();
		OneCell.Add(BestCellId);
		OutCandidate.Pockets.Add(MakePocket(
			Order,
			EABTSM3PocketRole::TargetAnchor,
			BestCellId,
			OneCell));
		OutCandidate.Pockets.Add(MakePocket(
			Order,
			EABTSM3PocketRole::Reward,
			RewardCellId,
			RewardCells));
		OutCandidate.Pockets.Add(MakePocket(
			Order,
			EABTSM3PocketRole::Exit,
			ExitCellId,
			ExitCells));

		FABTSM3MonthlySpatialEncounter Encounter;
		Encounter.Contract.EncounterId = EncounterId(Order);
		Encounter.Contract.OrderIndex = Order;
		Encounter.Contract.MissionTaskId = INDEX_NONE;
		Encounter.Contract.RouteBeatId = BeatId(Order);
		Encounter.Contract.Role =
			EABTSM3EncounterRole::DestructibleTarget;
		Encounter.Contract.BuildingPurpose =
			Order == 4
				? EABTSM3BuildingPurpose::GravityTraining
				: (Order == 0
					? EABTSM3BuildingPurpose::ProgressionTarget
					: EABTSM3BuildingPurpose::ResourceTarget);
		Encounter.Contract.DifficultyBand =
			Config.DifficultyBands[Order];
		Encounter.Contract.ProgressDistanceCM =
			static_cast<float>(
				SourceRoute.ProgressDistanceCM[
					NearestRouteIndex[BestCellId]]);
		Encounter.Contract.FlowS =
			static_cast<float>(Config.EncounterFlowQ[Order])
				/ FlowQuantization;
		Encounter.Contract.RoadArrivalPocketId = PocketId(
			Order,
			EABTSM3PocketRole::RoadArrival);
		Encounter.Contract.ScoutRevealPocketId = PocketId(
			Order,
			EABTSM3PocketRole::ScoutReveal);
		Encounter.Contract.SlingshotPocketId = PocketId(
			Order,
			EABTSM3PocketRole::Slingshot);
		Encounter.Contract.TargetEnvelopePocketId = PocketId(
			Order,
			EABTSM3PocketRole::TargetEnvelope);
		Encounter.Contract.TargetAnchorPocketId = PocketId(
			Order,
			EABTSM3PocketRole::TargetAnchor);
		Encounter.Contract.RewardPocketId = PocketId(
			Order,
			EABTSM3PocketRole::Reward);
		Encounter.Contract.ExitPocketId = PocketId(
			Order,
			EABTSM3PocketRole::Exit);
		Encounter.Contract.BallisticWitnessId = INDEX_NONE;
		Encounter.Contract.BiomeDistrictId = BiomeId(Order);
		Encounter.Contract.ResolvedM7ProfileId =
			Descriptor.ProfileId;
		Encounter.Contract.ProfileCatalogHash =
			static_cast<int64>(ProfileCatalogHash);
		Encounter.Contract.Resolution =
			EABTSM3SchemaResolution::Reserved;
		Encounter.RevealPolicy = Config.RevealPolicies[Order];
		Encounter.FlowQ = Config.EncounterFlowQ[Order];
		Encounter.PreRevealCellId =
			SourceRoute.OrderedRoadCellIds[PreRevealRouteIndex];
		Encounter.TargetAnchorCellId = BestCellId;
		Encounter.MainRoadDistanceCells =
			RoadDistance[BestCellId];
		Encounter.RequiredRoadClearanceCells =
			BestRequiredClearanceCells;
		Encounter.TargetFootprintCellIds = BestFootprint;
		Encounter.TargetNoRoadCellIds = BestNoRoad;
		Encounter.ResolvedFixtureProfileId =
			Descriptor.ProfileId;
		Encounter.ProfileCatalogHash =
			static_cast<int64>(ProfileCatalogHash);
		Encounter.ProfileBoundsExtentCM =
			Descriptor.BoundsExtentCM;
		const FVector Up =
			Cells[BestCellId].UnitCenter.GetSafeNormal();
		Encounter.AttackFaceDirection =
			FVector::VectorPlaneProject(
				Cells[SlingshotCellId].UnitCenter - Up,
				Up).GetSafeNormal();
		FCanonicalHash64 VisualHash;
		VisualHash.AddName(Descriptor.ProfileId);
		VisualHash.AddInt32(Descriptor.SilhouetteFamilyId);
		VisualHash.AddInt32(Descriptor.MaterialProfileId);
		VisualHash.AddInt32(Descriptor.WeaknessProfileId);
		VisualHash.AddInt32(static_cast<int32>(
			Config.EncounterBiomeArchetypes[Order]));
		VisualHash.AddInt32(Config.DifficultyBands[Order]);
		Encounter.VisualSignature =
			static_cast<int64>(VisualHash.Get());
		OutCandidate.Encounters.Add(MoveTemp(Encounter));

		for (const int32 AttackCellId : SidePath)
		{
			OutCandidate.Cells[AttackCellId].bAttackCorridor = true;
		}
	}
	return true;
}

bool BuildEnvelopesAndBiomes(
	const FABTSM3MonthlyEncounterSpatialConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlyRouteCandidate& SourceRoute,
	FABTSM3MonthlySpatialCandidate& Candidate,
	FString& OutFailure)
{
	Candidate.PlayableEnvelopes.Reset();
	for (int32 Order = 0;
		Order < Config.DestructibleEncounterCount;
		++Order)
	{
		const int32 MinFlow = Order == 0
			? 0
			: (Config.EncounterFlowQ[Order - 1]
				+ Config.EncounterFlowQ[Order]) / 2;
		const int32 MaxFlow =
			Order + 1 == Config.DestructibleEncounterCount
			? FlowQuantization
			: (Config.EncounterFlowQ[Order]
				+ Config.EncounterFlowQ[Order + 1]) / 2;
		TArray<int32> RouteSeeds;
		for (int32 RouteIndex = 0;
			RouteIndex < SourceRoute.OrderedRoadCellIds.Num();
			++RouteIndex)
		{
			const int32 FlowQ =
				SourceRoute.Metrics.RouteLengthCM > 0
				? static_cast<int32>(
					static_cast<int64>(
						SourceRoute.ProgressDistanceCM[RouteIndex])
					* FlowQuantization
					/ SourceRoute.Metrics.RouteLengthCM)
				: 0;
			if (FlowQ >= MinFlow && FlowQ <= MaxFlow)
			{
				RouteSeeds.Add(
					SourceRoute.OrderedRoadCellIds[RouteIndex]);
			}
		}
		TArray<int32> RouteEnvelopeCells;
		ExpandRing(
			Cells,
			RouteSeeds,
			Config.PlayableRouteRadiusCells,
			RouteEnvelopeCells);
		TMap<int32, int32> CellRoles;
		for (const int32 CellId : RouteEnvelopeCells)
		{
			CellRoles.Add(
				CellId,
				RoleMask(EABTSM3ActiveRole::Route));
		}
		const FABTSM3MonthlySpatialEncounter& Encounter =
			Candidate.Encounters[Order];
		const int32 PocketIds[] = {
			Encounter.Contract.RoadArrivalPocketId,
			Encounter.Contract.ScoutRevealPocketId,
			Encounter.Contract.SlingshotPocketId,
			Encounter.Contract.TargetEnvelopePocketId,
			Encounter.Contract.TargetAnchorPocketId,
			Encounter.Contract.RewardPocketId,
			Encounter.Contract.ExitPocketId
		};
		for (const int32 Id : PocketIds)
		{
			const FABTSM3PocketContract* Pocket =
				FindPocket(Candidate.Pockets, Id);
			if (Pocket == nullptr)
			{
				OutFailure = FString::Printf(
					TEXT("EnvelopePocket:%d"),
					Id);
				return false;
			}
			TArray<int32> EnvelopeInfluenceCells;
			ExpandRing(
				Cells,
				Pocket->CellIds,
				Config.PlayablePocketRadiusCells,
				EnvelopeInfluenceCells);
			for (const int32 CellId : EnvelopeInfluenceCells)
			{
				CellRoles.FindOrAdd(CellId);
			}
			TArray<int32> ActiveInfluenceCells;
			ExpandRing(
				Cells,
				Pocket->CellIds,
				Config.ActivePocketRadiusCells,
				ActiveInfluenceCells);
			const int32 Mask = PocketRoleMask(Pocket->Role);
			for (const int32 CellId : ActiveInfluenceCells)
			{
				CellRoles.FindOrAdd(CellId) |= Mask;
			}
		}
		TArray<int32> SortedCellIds;
		CellRoles.GetKeys(SortedCellIds);
		SortedCellIds.Sort();
		FABTSM3PlayableEnvelope Envelope;
		Envelope.EnvelopeId = EnvelopeId(Order);
		Envelope.RouteBeatId = BeatId(Order);
		Envelope.EncounterId = EncounterId(Order);
		Envelope.MinProgressDistanceCM =
			static_cast<float>(
				static_cast<int64>(
					SourceRoute.Metrics.RouteLengthCM) * MinFlow
				/ FlowQuantization);
		Envelope.MaxProgressDistanceCM =
			static_cast<float>(
				static_cast<int64>(
					SourceRoute.Metrics.RouteLengthCM) * MaxFlow
				/ FlowQuantization);
		for (const int32 CellId : SortedCellIds)
		{
			FABTSM3PlayableCellRole Role;
			Role.CellId = CellId;
			Role.ActiveRoleMask = CellRoles[CellId];
			Envelope.Cells.Add(Role);
			FABTSM3MonthlySpatialCell& SpatialCell =
				Candidate.Cells[CellId];
			SpatialCell.ActiveRoleMask |= Role.ActiveRoleMask;
			if (SpatialCell.PrimaryEnvelopeId == INDEX_NONE)
			{
				SpatialCell.PrimaryEnvelopeId =
					Envelope.EnvelopeId;
			}
		}
		Envelope.Resolution = EABTSM3SchemaResolution::Finalized;
		Candidate.PlayableEnvelopes.Add(MoveTemp(Envelope));
	}
	for (FABTSM3MonthlySpatialCell& Cell : Candidate.Cells)
	{
		Cell.bApprovedTransition =
			Cell.PrimaryEnvelopeId != INDEX_NONE
			&& Cell.ActiveRoleMask == 0;
	}
	for (FABTSM3PlayableEnvelope& Envelope :
		Candidate.PlayableEnvelopes)
	{
		for (FABTSM3PlayableCellRole& Role : Envelope.Cells)
		{
			Role.ActiveRoleMask =
				Candidate.Cells[Role.CellId].ActiveRoleMask;
		}
	}

	Candidate.BiomeDistricts.Reset();
	for (int32 Order = 0;
		Order <= BackgroundBiomeOrder;
		++Order)
	{
		FABTSM3BiomeDistrict District;
		District.BiomeDistrictId = BiomeId(Order);
		District.Archetype = Order == BackgroundBiomeOrder
			? EABTSM3BiomeArchetype::Background
			: Config.EncounterBiomeArchetypes[Order];
		District.ObservedTerrainType =
			TerrainForBiome(District.Archetype);
		District.MinProgressDistanceCM = MAX_flt;
		District.MaxProgressDistanceCM = 0.0f;
		District.MinFlowS = 1.0f;
		District.MaxFlowS = 0.0f;
		District.bBackground = Order == BackgroundBiomeOrder;
		District.Resolution =
			EABTSM3SchemaResolution::Finalized;
		Candidate.BiomeDistricts.Add(MoveTemp(District));
	}
	for (FABTSM3MonthlySpatialCell& Cell : Candidate.Cells)
	{
		int32 Order = BackgroundBiomeOrder;
		if (Cell.PrimaryEnvelopeId != INDEX_NONE)
		{
			Order = Cell.PrimaryEnvelopeId - EnvelopeIdBase;
		}
		else if (Cell.MainRoadDistanceCells <= 8)
		{
			Order = FindEncounterBiomeOrder(Config, Cell.FlowQ);
		}
		if (Order < 0 || Order > BackgroundBiomeOrder)
		{
			OutFailure = FString::Printf(
				TEXT("BiomeOrder:%d"),
				Cell.CellId);
			return false;
		}
		Cell.BiomeDistrictId = BiomeId(Order);
		FABTSM3BiomeDistrict& District =
			Candidate.BiomeDistricts[Order];
		District.CellIds.Add(Cell.CellId);
		const float FlowS =
			static_cast<float>(Cell.FlowQ) / FlowQuantization;
		const float ProgressCM =
			SourceRoute.Metrics.RouteLengthCM * FlowS;
		District.MinProgressDistanceCM = FMath::Min(
			District.MinProgressDistanceCM,
			ProgressCM);
		District.MaxProgressDistanceCM = FMath::Max(
			District.MaxProgressDistanceCM,
			ProgressCM);
		District.MinFlowS = FMath::Min(District.MinFlowS, FlowS);
		District.MaxFlowS = FMath::Max(District.MaxFlowS, FlowS);
	}
	for (FABTSM3BiomeDistrict& District :
		Candidate.BiomeDistricts)
	{
		if (District.CellIds.IsEmpty())
		{
			OutFailure = FString::Printf(
				TEXT("EmptyBiome:%d"),
				District.BiomeDistrictId);
			return false;
		}
	}
	for (int32 Order = 0;
		Order < Candidate.Encounters.Num();
		++Order)
	{
		Candidate.Encounters[Order].Contract.BiomeDistrictId =
			BiomeId(Order);
	}
	for (FABTSM3PocketContract& Pocket : Candidate.Pockets)
	{
		const int32 Order =
			Pocket.EncounterId - EncounterIdBase;
		Pocket.BiomeDistrictId = BiomeId(Order);
	}
	for (FABTSM3PlayableEnvelope& Envelope :
		Candidate.PlayableEnvelopes)
	{
		for (FABTSM3PlayableCellRole& Role : Envelope.Cells)
		{
			Role.BiomeDistrictId =
				Candidate.Cells[Role.CellId].BiomeDistrictId;
		}
	}
	TSet<int32> ReservedPlayableCells;
	for (const FABTSM3PlayableEnvelope& Envelope :
		Candidate.PlayableEnvelopes)
	{
		for (const FABTSM3PlayableCellRole& Role :
			Envelope.Cells)
		{
			ReservedPlayableCells.Add(Role.CellId);
		}
	}
	TArray<int32> ReservedStrictCorridorCells;
	ExpandRing(
		Cells,
		SourceRoute.CorridorCellIds,
		Config.PlayableRouteRadiusCells,
		ReservedStrictCorridorCells);
	for (const int32 CellId : ReservedStrictCorridorCells)
	{
		ReservedPlayableCells.Add(CellId);
	}
	Candidate.PreRoadReservedPlayableCellIds.Reset();
	Candidate.PreRoadReservedPlayableCellIds.Reserve(
		ReservedPlayableCells.Num());
	for (const int32 CellId : ReservedPlayableCells)
	{
		Candidate.PreRoadReservedPlayableCellIds.Add(CellId);
	}
	Candidate.PreRoadReservedPlayableCellIds.Sort();
	Candidate.ReservationHash = static_cast<int64>(
		ComputeReservationHash(Candidate));
	return true;
}

void BuildScratchContext(
	const int32 WorldSeed,
	const FABTSM3MonthlyEncounterSpatialConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlyRouteCandidate& SourceRoute,
	const FABTSM3MonthlySpatialFaultInjection& FaultInjection,
	FABTSM3MonthlySpatialCandidate& Candidate)
{
	TSet<int32> SourceRoadCells;
	for (const int32 CellId : SourceRoute.OrderedRoadCellIds)
	{
		SourceRoadCells.Add(CellId);
	}
	const FABTSM3MonthlySpatialEncounter& WaterEncounter =
		Candidate.Encounters[2];
	const FABTSM3PocketContract* WaterArrival =
		FindPocket(
			Candidate.Pockets,
			WaterEncounter.Contract.RoadArrivalPocketId);
	const int32 WaterRouteIndex =
		WaterArrival != nullptr
		&& Candidate.Cells.IsValidIndex(
			WaterArrival->AnchorCellId)
		? Candidate.Cells[
			WaterArrival->AnchorCellId].NearestRoadOrderIndex
		: INDEX_NONE;

	for (FABTSM3MonthlySpatialCell& Cell : Candidate.Cells)
	{
		const int32 DistrictOrder =
			Cell.BiomeDistrictId - BiomeIdBase;
		const EABTSM3BiomeArchetype Biome =
			Candidate.BiomeDistricts.IsValidIndex(DistrictOrder)
			? Candidate.BiomeDistricts[DistrictOrder].Archetype
			: EABTSM3BiomeArchetype::Background;
		const uint64 NoiseHash = Mix64(
			static_cast<uint32>(WorldSeed)
			^ static_cast<uint64>(
				SourceRoute.CandidateId * 65537)
			^ static_cast<uint64>(Cell.CellId * 104729)
			^ 0x4d33523348454947ull);
		const int32 NoiseQ =
			static_cast<int32>(NoiseHash % 180001ull) - 90000;
		Cell.HeightQ = FMath::Clamp(
			BiomeBaseHeightQ(Biome) + NoiseQ,
			0,
			FlowQuantization);
		const bool bProtected =
			Cell.ActiveRoleMask != 0
			|| Cell.bNoRoad
			|| Cell.bTargetFootprint
			|| Cell.bAttackCorridor;
		const uint64 WaterHash = Mix64(
			NoiseHash ^ 0x4d33523357415452ull);
		Cell.bWater = !bProtected
			&& static_cast<int32>(WaterHash % 1000ull)
				< Config.BackgroundWaterPermille;
		Cell.bLegalWaterCrossing = false;
		const bool bPureRoute =
			Cell.ActiveRoleMask
				== RoleMask(EABTSM3ActiveRole::Route);
		if (WaterRouteIndex != INDEX_NONE
			&& Cell.NearestRoadOrderIndex != INDEX_NONE
			&& FMath::Abs(
				Cell.NearestRoadOrderIndex - WaterRouteIndex)
				<= Config.LegalCrossingHalfWidthCells
			&& Cell.MainRoadDistanceCells <= 1
			&& bPureRoute
			&& !Cell.bNoRoad)
		{
			Cell.bWater = true;
			Cell.bLegalWaterCrossing = true;
		}
		if (Cell.bTargetFootprint || Cell.bAttackCorridor)
		{
			Cell.bWater = false;
			Cell.bLegalWaterCrossing = false;
		}
	}

	for (const FABTSM3MonthlySpatialEncounter& Encounter :
		Candidate.Encounters)
	{
		const int32 AnchorHeightQ =
			Candidate.Cells[Encounter.TargetAnchorCellId].HeightQ;
		for (const int32 CellId :
			Encounter.TargetFootprintCellIds)
		{
			Candidate.Cells[CellId].HeightQ = AnchorHeightQ;
		}
	}

	Candidate.RoadContext.Cells.SetNum(Cells.Num());
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		const FABTSM3MonthlySpatialCell& Cell =
			Candidate.Cells[CellId];
		int32 MaxNeighborDeltaQ = 0;
		for (const int32 NeighborId :
			Cells[CellId].NeighborCellIds)
		{
			MaxNeighborDeltaQ = FMath::Max(
				MaxNeighborDeltaQ,
				FMath::Abs(
					Cell.HeightQ
					- Candidate.Cells[NeighborId].HeightQ));
		}
		FABTSM3MonthlyRouteCellContext& Context =
			Candidate.RoadContext.Cells[CellId];
		Context.TerrainCost =
			Config.BaseTerrainCost
			+ static_cast<int32>(
				static_cast<int64>(Cell.HeightQ)
				* Config.HeightCostScale
				/ FlowQuantization);
		Context.SlopeCost = static_cast<int32>(
			static_cast<int64>(MaxNeighborDeltaQ)
			* Config.SlopeCostScale
			/ FlowQuantization);
		Context.bWater = Cell.bWater;
		Context.bLegalWaterCrossing =
			Cell.bLegalWaterCrossing;
		Context.bSoftEncounterReserved =
			Cell.ActiveRoleMask != 0
			&& (Cell.ActiveRoleMask
				& RoleMask(EABTSM3ActiveRole::Route)) == 0;
		Context.bHardBlocked = Cell.bNoRoad;
		Context.ReuseBias = SourceRoadCells.Contains(CellId)
			? Config.ExistingRoadReuseBias
			: 0;
		if (FaultInjection.bBlockEverySourceRoadCell
			&& SourceRoadCells.Contains(CellId))
		{
			Context.bHardBlocked = true;
		}
		if (SourceRoute.OrderedRoadCellIds.IsValidIndex(
				FaultInjection.BlockedSourceRoadOrderIndex)
			&& CellId
				== SourceRoute.OrderedRoadCellIds[
					FaultInjection.
						BlockedSourceRoadOrderIndex])
		{
			Context.bHardBlocked = true;
		}
	}
	Candidate.RoadContextHash = static_cast<int64>(
		FABTSM3MonthlyRouteBuilder::ComputeRoadContextHash(
			Candidate.RoadContext));
}

bool RebuildFinalPlayableEnvelopes(
	const FABTSM3MonthlyEncounterSpatialConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlyRouteCandidate& FinalRoute,
	FABTSM3MonthlySpatialCandidate& Candidate,
	FString& OutFailure)
{
	if (Candidate.PreRoadReservedPlayableCellIds.IsEmpty()
		|| !IsStrictlySortedUnique(
			Candidate.PreRoadReservedPlayableCellIds))
	{
		OutFailure = TEXT("FinalReservationIdentity");
		return false;
	}
	TSet<int32> ReservedPlayableCells;
	for (const int32 CellId :
		Candidate.PreRoadReservedPlayableCellIds)
	{
		ReservedPlayableCells.Add(CellId);
	}
	for (FABTSM3MonthlySpatialCell& Cell : Candidate.Cells)
	{
		Cell.PrimaryEnvelopeId = INDEX_NONE;
		Cell.ActiveRoleMask = 0;
		Cell.bApprovedTransition = false;
	}
	Candidate.PlayableEnvelopes.Reset();
	for (int32 Order = 0;
		Order < Config.DestructibleEncounterCount;
		++Order)
	{
		const int32 MinFlowQ = Order == 0
			? 0
			: (Config.EncounterFlowQ[Order - 1]
				+ Config.EncounterFlowQ[Order]) / 2;
		const int32 MaxFlowQ =
			Order + 1 == Config.DestructibleEncounterCount
			? FlowQuantization
			: (Config.EncounterFlowQ[Order]
				+ Config.EncounterFlowQ[Order + 1]) / 2;
		TArray<int32> RouteSeeds;
		for (int32 RouteIndex = 0;
			RouteIndex < FinalRoute.OrderedRoadCellIds.Num();
			++RouteIndex)
		{
			const int32 FlowQ =
				FinalRoute.Metrics.RouteLengthCM > 0
				? static_cast<int32>(
					static_cast<int64>(
						FinalRoute.ProgressDistanceCM[RouteIndex])
					* FlowQuantization
					/ FinalRoute.Metrics.RouteLengthCM)
				: 0;
			if (FlowQ >= MinFlowQ && FlowQ <= MaxFlowQ)
			{
				RouteSeeds.Add(
					FinalRoute.OrderedRoadCellIds[RouteIndex]);
			}
		}
		if (RouteSeeds.IsEmpty())
		{
			OutFailure = FString::Printf(
				TEXT("FinalEnvelopeRouteSeeds:%d"),
				Order);
			return false;
		}
		TArray<int32> RouteEnvelopeCells;
		ExpandRing(
			Cells,
			RouteSeeds,
			Config.PlayableRouteRadiusCells,
			RouteEnvelopeCells);
		TMap<int32, int32> CellRoles;
		for (const int32 CellId : RouteEnvelopeCells)
		{
			CellRoles.Add(
				CellId,
				RoleMask(EABTSM3ActiveRole::Route));
		}
		const FABTSM3MonthlySpatialEncounter& Encounter =
			Candidate.Encounters[Order];
		const int32 PocketIds[] = {
			Encounter.Contract.RoadArrivalPocketId,
			Encounter.Contract.ScoutRevealPocketId,
			Encounter.Contract.SlingshotPocketId,
			Encounter.Contract.TargetEnvelopePocketId,
			Encounter.Contract.TargetAnchorPocketId,
			Encounter.Contract.RewardPocketId,
			Encounter.Contract.ExitPocketId
		};
		for (const int32 PocketContractId : PocketIds)
		{
			const FABTSM3PocketContract* Pocket =
				FindPocket(
					Candidate.Pockets,
					PocketContractId);
			if (Pocket == nullptr)
			{
				OutFailure = FString::Printf(
					TEXT("FinalEnvelopePocket:%d"),
					PocketContractId);
				return false;
			}
			TArray<int32> PlayableInfluenceCells;
			ExpandRing(
				Cells,
				Pocket->CellIds,
				Config.PlayablePocketRadiusCells,
				PlayableInfluenceCells);
			for (const int32 CellId :
				PlayableInfluenceCells)
			{
				CellRoles.FindOrAdd(CellId);
			}
			TArray<int32> ActiveInfluenceCells;
			ExpandRing(
				Cells,
				Pocket->CellIds,
				Config.ActivePocketRadiusCells,
				ActiveInfluenceCells);
			const int32 Mask = PocketRoleMask(Pocket->Role);
			for (const int32 CellId : ActiveInfluenceCells)
			{
				CellRoles.FindOrAdd(CellId) |= Mask;
			}
		}
		TArray<int32> SortedCellIds;
		CellRoles.GetKeys(SortedCellIds);
		SortedCellIds.Sort();
		for (const int32 CellId : SortedCellIds)
		{
			if (!ReservedPlayableCells.Contains(CellId))
			{
				OutFailure = FString::Printf(
					TEXT("FinalEnvelopeEscapedReservation:%d:%d"),
					Order,
					CellId);
				return false;
			}
		}
		FABTSM3PlayableEnvelope Envelope;
		Envelope.EnvelopeId = EnvelopeId(Order);
		Envelope.RouteBeatId = BeatId(Order);
		Envelope.EncounterId = EncounterId(Order);
		Envelope.MinProgressDistanceCM =
			static_cast<float>(
				static_cast<int64>(
					FinalRoute.Metrics.RouteLengthCM)
				* MinFlowQ
				/ FlowQuantization);
		Envelope.MaxProgressDistanceCM =
			static_cast<float>(
				static_cast<int64>(
					FinalRoute.Metrics.RouteLengthCM)
				* MaxFlowQ
				/ FlowQuantization);
		for (const int32 CellId : SortedCellIds)
		{
			FABTSM3MonthlySpatialCell& SpatialCell =
				Candidate.Cells[CellId];
			SpatialCell.ActiveRoleMask |= CellRoles[CellId];
			if (SpatialCell.PrimaryEnvelopeId == INDEX_NONE)
			{
				SpatialCell.PrimaryEnvelopeId =
					Envelope.EnvelopeId;
			}
			FABTSM3PlayableCellRole Role;
			Role.CellId = CellId;
			Envelope.Cells.Add(Role);
		}
		Envelope.Resolution =
			EABTSM3SchemaResolution::Finalized;
		Candidate.PlayableEnvelopes.Add(MoveTemp(Envelope));
	}
	for (FABTSM3MonthlySpatialCell& Cell : Candidate.Cells)
	{
		Cell.bApprovedTransition =
			Cell.PrimaryEnvelopeId != INDEX_NONE
			&& Cell.ActiveRoleMask == 0;
	}
	for (FABTSM3PlayableEnvelope& Envelope :
		Candidate.PlayableEnvelopes)
	{
		for (FABTSM3PlayableCellRole& Role : Envelope.Cells)
		{
			Role.ActiveRoleMask =
				Candidate.Cells[Role.CellId].ActiveRoleMask;
			Role.BiomeDistrictId =
				Candidate.Cells[Role.CellId].BiomeDistrictId;
		}
	}
	return true;
}

bool UpdateFinalRouteFields(
	const FABTSM3MonthlyEncounterSpatialConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	FABTSM3MonthlySpatialCandidate& Candidate,
	FString& OutFailure)
{
	TArray<int32> RoadDistance;
	TArray<int32> NearestRouteIndex;
	if (!BuildNearestRouteFields(
			Cells,
			Candidate.RecomputedRoute,
			RoadDistance,
			NearestRouteIndex))
	{
		OutFailure = TEXT("FinalNearestRoute");
		return false;
	}
	for (FABTSM3MonthlySpatialCell& Cell : Candidate.Cells)
	{
		Cell.MainRoadDistanceCells = RoadDistance[Cell.CellId];
		Cell.NearestRoadOrderIndex =
			NearestRouteIndex[Cell.CellId];
		Cell.FlowQ =
			Candidate.RecomputedRoute.Metrics.RouteLengthCM > 0
			&& Candidate.RecomputedRoute.ProgressDistanceCM.IsValidIndex(
				NearestRouteIndex[Cell.CellId])
			? static_cast<int32>(
				static_cast<int64>(
					Candidate.RecomputedRoute.ProgressDistanceCM[
						NearestRouteIndex[Cell.CellId]])
				* FlowQuantization
				/ Candidate.RecomputedRoute.Metrics.RouteLengthCM)
			: 0;
	}
	for (FABTSM3MonthlySpatialCell& Cell : Candidate.Cells)
	{
		Cell.bAttackCorridor = false;
	}
	TSet<int32> RouteCells;
	for (const int32 CellId :
		Candidate.RecomputedRoute.OrderedRoadCellIds)
	{
		RouteCells.Add(CellId);
		if (!Candidate.RoadContext.Cells.IsValidIndex(CellId)
			|| Candidate.RoadContext.Cells[CellId].bHardBlocked
			|| (Candidate.RoadContext.Cells[CellId].bWater
				&& !Candidate.RoadContext.Cells[
					CellId].bLegalWaterCrossing))
		{
			OutFailure = FString::Printf(
				TEXT("FinalRoadContext:%d"),
				CellId);
			return false;
		}
	}

	int32 PreviousProgressCM = INDEX_NONE;
	TBitArray<> ReservedCells(false, Cells.Num());
	for (const int32 CellId :
		Candidate.PreRoadReservedPlayableCellIds)
	{
		if (Cells.IsValidIndex(CellId))
		{
			ReservedCells[CellId] = true;
		}
	}
	TArray<int32> RouteIndexByCell;
	RouteIndexByCell.Init(INDEX_NONE, Cells.Num());
	for (int32 RouteIndex = 0;
		RouteIndex
			< Candidate.RecomputedRoute.OrderedRoadCellIds.Num();
		++RouteIndex)
	{
		const int32 CellId =
			Candidate.RecomputedRoute.OrderedRoadCellIds[
				RouteIndex];
		if (Cells.IsValidIndex(CellId))
		{
			RouteIndexByCell[CellId] = RouteIndex;
		}
	}
	for (int32 Order = 0;
		Order < Candidate.Encounters.Num();
		++Order)
	{
		FABTSM3MonthlySpatialEncounter& Encounter =
			Candidate.Encounters[Order];
		const int32 TargetCellId = Encounter.TargetAnchorCellId;
		const FIntPoint Window =
			Config.TargetRoadDistanceWindowsCells[Order];
		if (!RoadDistance.IsValidIndex(TargetCellId)
			|| RoadDistance[TargetCellId]
				< FMath::Max(
					Window.X,
					Encounter.RequiredRoadClearanceCells)
			|| RoadDistance[TargetCellId] > Window.Y)
		{
			OutFailure = FString::Printf(
				TEXT("FinalTargetDistance:%d:%d"),
				Order,
				RoadDistance.IsValidIndex(TargetCellId)
					? RoadDistance[TargetCellId]
					: INDEX_NONE);
			return false;
		}
		for (const int32 CellId : Encounter.TargetNoRoadCellIds)
		{
			if (RouteCells.Contains(CellId)
				|| Candidate.Cells[CellId].bWater)
			{
				OutFailure = FString::Printf(
					TEXT("FinalNoRoadOverlap:%d:%d"),
					Order,
					CellId);
				return false;
			}
		}
		const int32 DesiredProgressCM = static_cast<int32>(
			static_cast<int64>(
				Candidate.RecomputedRoute.Metrics.RouteLengthCM)
			* Config.EncounterFlowQ[Order]
			/ FlowQuantization);
		TArray<int32> FinalSidePath;
		int32 ArrivalRouteIndex = INDEX_NONE;
		if (!BuildFinalPathToPlannedRoute(
				Config,
				Cells,
				Candidate,
				Encounter,
				ReservedCells,
				RouteIndexByCell,
				DesiredProgressCM,
				PreviousProgressCM,
				FinalSidePath,
				ArrivalRouteIndex))
		{
			OutFailure = FString::Printf(
				TEXT("FinalSidePath:%d"),
				Order);
			return false;
		}
		const int32 RoadArrivalCellId =
			FinalSidePath.Last();
		const int32 RevealCellId =
			FinalSidePath[FinalSidePath.Num() - 2];
		const int32 SlingshotCellId =
			FinalSidePath[FinalSidePath.Num() - 3];
		if (!Candidate.RecomputedRoute.OrderedRoadCellIds.
				IsValidIndex(ArrivalRouteIndex)
			|| Candidate.RecomputedRoute.OrderedRoadCellIds[
					ArrivalRouteIndex]
				!= RoadArrivalCellId)
		{
			OutFailure = FString::Printf(
				TEXT("FinalArrival:%d"),
				Order);
			return false;
		}
		const int32 ProgressCM =
			Candidate.RecomputedRoute.ProgressDistanceCM[
				ArrivalRouteIndex];
		if (PreviousProgressCM != INDEX_NONE)
		{
			const int32 Gap = ProgressCM - PreviousProgressCM;
			if (Gap < Config.MinAdjacentEncounterProgressCM
				|| Gap > Config.MaxAdjacentEncounterProgressCM)
			{
				OutFailure = FString::Printf(
					TEXT("FinalSpacing:%d:%d"),
					Order,
					Gap);
				return false;
			}
		}
		PreviousProgressCM = ProgressCM;
		const float StartToTargetArcCM =
			SurfaceArcDistanceCM(
				Cells,
				Candidate.RecomputedRoute.OrderedRoadCellIds[0],
				TargetCellId,
				PlanetRadiusCM);
		if ((Order == 0
				&& StartToTargetArcCM
					> Config.AttackReadableMaxDistanceCM)
			|| (Order > 0
				&& StartToTargetArcCM
					<= Config.AttackReadableMaxDistanceCM))
		{
			OutFailure = FString::Printf(
				TEXT("FinalStartSemantic:%d"),
				Order);
			return false;
		}
		const int32 ExitRouteIndex =
			FindSemanticExitRouteIndex(
			Candidate.RecomputedRoute,
			ArrivalRouteIndex,
			ProgressCM,
			Config.ExitLeadCM);
		const int32 PreRevealRouteIndex = Order == 0
			? FindRouteIndexAtProgress(
				Candidate.RecomputedRoute,
				FMath::Max(
					0,
					ProgressCM - Config.PreRevealLeadCM))
			: FindSemanticPreRevealRouteIndex(
				Candidate.RecomputedRoute,
				Cells,
				TargetCellId,
				ProgressCM,
				Config.PreRevealLeadCM,
				Config.AttackReadableMaxDistanceCM,
				PlanetRadiusCM);
		FABTSM3PocketContract* RoadArrival = FindPocket(
			Candidate.Pockets,
			Encounter.Contract.RoadArrivalPocketId);
		FABTSM3PocketContract* Reveal = FindPocket(
			Candidate.Pockets,
			Encounter.Contract.ScoutRevealPocketId);
		FABTSM3PocketContract* Slingshot = FindPocket(
			Candidate.Pockets,
			Encounter.Contract.SlingshotPocketId);
		FABTSM3PocketContract* Reward = FindPocket(
			Candidate.Pockets,
			Encounter.Contract.RewardPocketId);
		FABTSM3PocketContract* Exit = FindPocket(
			Candidate.Pockets,
			Encounter.Contract.ExitPocketId);
		if (RoadArrival == nullptr
			|| Reveal == nullptr
			|| Slingshot == nullptr
			|| Reward == nullptr
			|| Exit == nullptr
			|| !Candidate.RecomputedRoute.OrderedRoadCellIds.IsValidIndex(
				ExitRouteIndex)
			|| !Candidate.RecomputedRoute.OrderedRoadCellIds.IsValidIndex(
				PreRevealRouteIndex))
		{
			OutFailure = FString::Printf(
				TEXT("FinalObserver:%d"),
				Order);
			return false;
		}
		int32 RewardCellId = INDEX_NONE;
		for (const int32 NeighborId :
			Cells[TargetCellId].NeighborCellIds)
		{
			if (NeighborId != SlingshotCellId
				&& NeighborId != RevealCellId
				&& NeighborId != RoadArrivalCellId
				&& (RewardCellId == INDEX_NONE
					|| NeighborId < RewardCellId))
			{
				RewardCellId = NeighborId;
			}
		}
		if (RewardCellId == INDEX_NONE)
		{
			OutFailure = FString::Printf(
				TEXT("FinalReward:%d"),
				Order);
			return false;
		}
		for (const int32 AttackCellId : FinalSidePath)
		{
			if (!Candidate.Cells.IsValidIndex(AttackCellId)
				|| Candidate.Cells[AttackCellId].bWater
				|| (Candidate.Cells[AttackCellId].
						bTargetFootprint
					&& !Encounter.TargetFootprintCellIds.
						Contains(AttackCellId))
				|| (Candidate.Cells[AttackCellId].bNoRoad
					&& !Encounter.TargetNoRoadCellIds.
						Contains(AttackCellId)))
			{
				OutFailure = FString::Printf(
					TEXT("FinalAttackCorridor:%d:%d"),
					Order,
					AttackCellId);
				return false;
			}
			Candidate.Cells[AttackCellId].
				bAttackCorridor = true;
		}
		const auto ResetPocketRing =
			[&Cells, &Config](
				FABTSM3PocketContract& Pocket,
				const int32 AnchorCellId)
			{
				Pocket.AnchorCellId = AnchorCellId;
				ExpandRing(
					Cells,
					AnchorCellId,
					Config.PocketRadiusCells,
					Pocket.CellIds);
			};
		ResetPocketRing(*RoadArrival, RoadArrivalCellId);
		ResetPocketRing(*Reveal, RevealCellId);
		ResetPocketRing(*Slingshot, SlingshotCellId);
		ResetPocketRing(*Reward, RewardCellId);
		Exit->AnchorCellId =
			Candidate.RecomputedRoute.OrderedRoadCellIds[
				ExitRouteIndex];
		if (ExitRouteIndex <= ArrivalRouteIndex
			|| Exit->AnchorCellId == RoadArrival->AnchorCellId)
		{
			OutFailure = FString::Printf(
				TEXT("FinalExit:%d"),
				Order);
			return false;
		}
		ResetPocketRing(*Exit, Exit->AnchorCellId);
		Encounter.PreRevealCellId =
			Candidate.RecomputedRoute.OrderedRoadCellIds[
				PreRevealRouteIndex];
		Encounter.MainRoadDistanceCells =
			RoadDistance[TargetCellId];
		Encounter.Contract.ProgressDistanceCM =
			static_cast<float>(ProgressCM);
		Encounter.FlowQ = static_cast<int32>(
			static_cast<int64>(ProgressCM)
			* FlowQuantization
			/ Candidate.RecomputedRoute.Metrics.RouteLengthCM);
		Encounter.Contract.FlowS =
			static_cast<float>(Encounter.FlowQ)
			/ FlowQuantization;
		const FVector Up =
			Cells[TargetCellId].UnitCenter.GetSafeNormal();
		Encounter.AttackFaceDirection =
			FVector::VectorPlaneProject(
				Cells[SlingshotCellId].UnitCenter - Up,
				Up).GetSafeNormal();
		Encounter.Contract.Resolution =
			EABTSM3SchemaResolution::Finalized;
		for (FABTSM3PocketContract& Pocket : Candidate.Pockets)
		{
			if (Pocket.EncounterId
				== Encounter.Contract.EncounterId)
			{
				Pocket.Resolution =
					EABTSM3SchemaResolution::Finalized;
			}
		}
	}
	if (!RebuildFinalPlayableEnvelopes(
			Config,
			Cells,
			Candidate.RecomputedRoute,
			Candidate,
			OutFailure))
	{
		return false;
	}
	for (const int32 CellId :
		Candidate.RecomputedRoute.OrderedRoadCellIds)
	{
		if (!Candidate.Cells.IsValidIndex(CellId)
			|| (Candidate.Cells[CellId].ActiveRoleMask
				& RoleMask(EABTSM3ActiveRole::Route)) == 0
			|| Candidate.Cells[CellId].PrimaryEnvelopeId
				== INDEX_NONE)
		{
			OutFailure = FString::Printf(
				TEXT("FinalRouteEnvelope:%d"),
				CellId);
			return false;
		}
	}
	for (int32 Order = 0;
		Order < Candidate.PlayableEnvelopes.Num();
		++Order)
	{
		const int32 MinFlowQ = Order == 0
			? 0
			: (Config.EncounterFlowQ[Order - 1]
				+ Config.EncounterFlowQ[Order]) / 2;
		const int32 MaxFlowQ =
			Order + 1 == Config.DestructibleEncounterCount
			? FlowQuantization
			: (Config.EncounterFlowQ[Order]
				+ Config.EncounterFlowQ[Order + 1]) / 2;
		FABTSM3PlayableEnvelope& Envelope =
			Candidate.PlayableEnvelopes[Order];
		Envelope.MinProgressDistanceCM =
			static_cast<float>(
				static_cast<int64>(
					Candidate.RecomputedRoute.Metrics.RouteLengthCM)
				* MinFlowQ
				/ FlowQuantization);
		Envelope.MaxProgressDistanceCM =
			static_cast<float>(
				static_cast<int64>(
					Candidate.RecomputedRoute.Metrics.RouteLengthCM)
				* MaxFlowQ
				/ FlowQuantization);
	}
	for (FABTSM3BiomeDistrict& District :
		Candidate.BiomeDistricts)
	{
		District.MinProgressDistanceCM = MAX_flt;
		District.MaxProgressDistanceCM = 0.0f;
		District.MinFlowS = 1.0f;
		District.MaxFlowS = 0.0f;
	}
	for (const FABTSM3MonthlySpatialCell& Cell : Candidate.Cells)
	{
		const int32 DistrictOrder =
			Cell.BiomeDistrictId - BiomeIdBase;
		if (!Candidate.BiomeDistricts.IsValidIndex(
				DistrictOrder))
		{
			OutFailure = FString::Printf(
				TEXT("FinalBiomeDistrict:%d"),
				Cell.CellId);
			return false;
		}
		FABTSM3BiomeDistrict& District =
			Candidate.BiomeDistricts[DistrictOrder];
		const float FlowS =
			static_cast<float>(Cell.FlowQ) / FlowQuantization;
		const float ProgressCM =
			Candidate.RecomputedRoute.Metrics.RouteLengthCM
				* FlowS;
		District.MinProgressDistanceCM = FMath::Min(
			District.MinProgressDistanceCM,
			ProgressCM);
		District.MaxProgressDistanceCM = FMath::Max(
			District.MaxProgressDistanceCM,
			ProgressCM);
		District.MinFlowS = FMath::Min(
			District.MinFlowS,
			FlowS);
		District.MaxFlowS = FMath::Max(
			District.MaxFlowS,
			FlowS);
	}
	return true;
}

float SurfaceRadiusForCell(
	const FABTSM3MonthlyEncounterSpatialConfig& Config,
	const float PlanetRadiusCM,
	const FABTSM3MonthlySpatialCell& Cell)
{
	return PlanetRadiusCM
		+ static_cast<float>(
			static_cast<int64>(Cell.HeightQ)
			* Config.ScratchHeightScaleCM
			/ FlowQuantization);
}

int32 FindNearestCellBrute(
	const TArray<FABTSM2Cell>& Cells,
	const FVector& Direction)
{
	int32 BestCellId = INDEX_NONE;
	double BestDot = -2.0;
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		const double Dot = FVector::DotProduct(
			Direction,
			Cells[CellId].UnitCenter);
		if (Dot > BestDot)
		{
			BestDot = Dot;
			BestCellId = CellId;
		}
	}
	return BestCellId;
}

int32 FindNearestCellHillClimb(
	const TArray<FABTSM2Cell>& Cells,
	const FVector& Direction,
	const int32 StartCellId)
{
	int32 Current = Cells.IsValidIndex(StartCellId)
		? StartCellId
		: 0;
	for (int32 Iteration = 0; Iteration < 256; ++Iteration)
	{
		int32 Best = Current;
		double BestDot = FVector::DotProduct(
			Direction,
			Cells[Current].UnitCenter);
		for (const int32 NeighborId :
			Cells[Current].NeighborCellIds)
		{
			const double Dot = FVector::DotProduct(
				Direction,
				Cells[NeighborId].UnitCenter);
			if (Dot > BestDot + 1.0e-12
				|| (FMath::IsNearlyEqual(Dot, BestDot, 1.0e-12)
					&& NeighborId < Best))
			{
				BestDot = Dot;
				Best = NeighborId;
			}
		}
		if (Best == Current)
		{
			return Current;
		}
		Current = Best;
	}
	return Current;
}

struct FVisibilityRayResult
{
	bool bVisible = false;
	bool bIdealSphereBlocked = false;
	bool bTerrainBlocked = false;
	bool bTraceValid = true;
};

double MinimumRadiusSquaredOnSegmentInterval(
	const FVector& Start,
	const FVector& Delta,
	const double MinT,
	const double MaxT)
{
	const double DeltaSquared = Delta.SizeSquared();
	const double ClosestT = DeltaSquared > UE_DOUBLE_SMALL_NUMBER
		? FMath::Clamp(
			-static_cast<double>(
				FVector::DotProduct(Start, Delta))
				/ DeltaSquared,
			MinT,
			MaxT)
		: MinT;
	return (Start + Delta * ClosestT).SizeSquared();
}

bool SegmentIntersectsSphereInterior(
	const FVector& Start,
	const FVector& End,
	const double Radius)
{
	const FVector Delta = End - Start;
	return MinimumRadiusSquaredOnSegmentInterval(
			Start,
			Delta,
			0.0,
			1.0)
		< Radius * Radius;
}

FVisibilityRayResult TraceVisibilityRay(
	const FABTSM3MonthlyEncounterSpatialConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	const FABTSM3MonthlySpatialCandidate& Candidate,
	const FVector& Start,
	const FVector& End,
	const int32 StartCellId,
	const int32 SampleCount,
	const bool bBruteReference)
{
	FVisibilityRayResult Result;
	(void)bBruteReference;
	const double IdealOcclusionRadius =
		PlanetRadiusCM
		+ Config.VisibilityOcclusionEpsilonCM;
	Result.bIdealSphereBlocked =
		SegmentIntersectsSphereInterior(
			Start,
			End,
			IdealOcclusionRadius);
	if (Result.bIdealSphereBlocked)
	{
		Result.bTerrainBlocked = true;
		return Result;
	}
	if (Cells.IsEmpty()
		|| Candidate.Cells.Num() != Cells.Num()
		|| SampleCount <= 0)
	{
		Result.bTraceValid = false;
		return Result;
	}
	const FVector Delta = End - Start;
	const FVector StartDirection = Start.GetSafeNormal();
	int32 ActiveCellId = FindNearestCellHillClimb(
		Cells,
		StartDirection,
		StartCellId);
	constexpr double ParameterTolerance = 1.0e-12;
	double CurrentT = 0.0;
	for (int32 SegmentIndex = 0;
		SegmentIndex < SampleCount;
		++SegmentIndex)
	{
		if (!Candidate.Cells.IsValidIndex(ActiveCellId))
		{
			Result.bTraceValid = false;
			return Result;
		}
		const double ActiveIntercept = FVector::DotProduct(
			Start,
			Cells[ActiveCellId].UnitCenter);
		const double ActiveSlope = FVector::DotProduct(
			Delta,
			Cells[ActiveCellId].UnitCenter);
		double NextT = 1.0;
		int32 NextCellId = INDEX_NONE;
		double NextSlope = -DBL_MAX;
		for (const int32 NeighborId :
			Cells[ActiveCellId].NeighborCellIds)
		{
			const double NeighborSlope = FVector::DotProduct(
				Delta,
				Cells[NeighborId].UnitCenter);
			const double SlopeDifference =
				NeighborSlope - ActiveSlope;
			if (SlopeDifference <= ParameterTolerance)
			{
				continue;
			}
			const double NeighborIntercept =
				FVector::DotProduct(
					Start,
					Cells[NeighborId].UnitCenter);
			const double CrossingT =
				(ActiveIntercept - NeighborIntercept)
				/ SlopeDifference;
			if (CrossingT
					<= CurrentT + ParameterTolerance
				|| CrossingT > 1.0)
			{
				continue;
			}
			if (CrossingT < NextT - ParameterTolerance
				|| (FMath::IsNearlyEqual(
						CrossingT,
						NextT,
						ParameterTolerance)
					&& (NeighborSlope
							> NextSlope
								+ ParameterTolerance
						|| (FMath::IsNearlyEqual(
								NeighborSlope,
								NextSlope,
								ParameterTolerance)
							&& NeighborId
								< NextCellId))))
			{
				NextT = CrossingT;
				NextCellId = NeighborId;
				NextSlope = NeighborSlope;
			}
		}
		const double SurfaceRadius =
			SurfaceRadiusForCell(
				Config,
				PlanetRadiusCM,
				Candidate.Cells[ActiveCellId])
			+ Config.VisibilityOcclusionEpsilonCM;
		if (MinimumRadiusSquaredOnSegmentInterval(
				Start,
				Delta,
				CurrentT,
				NextT)
			< SurfaceRadius * SurfaceRadius)
		{
			Result.bTerrainBlocked = true;
			return Result;
		}
		if (NextCellId == INDEX_NONE
			|| NextT >= 1.0 - ParameterTolerance)
		{
			Result.bVisible = true;
			return Result;
		}
		ActiveCellId = NextCellId;
		CurrentT = NextT;
	}
	Result.bTraceValid = false;
	return Result;
}

FVisibilityRayResult TraceVisibilityRayReferenceContinuous(
	const FABTSM3MonthlyEncounterSpatialConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	const FABTSM3MonthlySpatialCandidate& Candidate,
	const FVector& Start,
	const FVector& End)
{
	FVisibilityRayResult Result;
	if (Cells.IsEmpty()
		|| Candidate.Cells.Num() != Cells.Num()
		|| Config.ReferenceTraceSamples <= 0)
	{
		Result.bTraceValid = false;
		return Result;
	}
	const double IdealOcclusionRadius =
		PlanetRadiusCM
		+ Config.VisibilityOcclusionEpsilonCM;
	Result.bIdealSphereBlocked =
		SegmentIntersectsSphereInterior(
			Start,
			End,
			IdealOcclusionRadius);
	if (Result.bIdealSphereBlocked)
	{
		Result.bTerrainBlocked = true;
		return Result;
	}

	const FVector Delta = End - Start;
	TArray<double> Intercepts;
	TArray<double> Slopes;
	Intercepts.SetNumUninitialized(Cells.Num());
	Slopes.SetNumUninitialized(Cells.Num());
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		Intercepts[CellId] = FVector::DotProduct(
			Start,
			Cells[CellId].UnitCenter);
		Slopes[CellId] = FVector::DotProduct(
			Delta,
			Cells[CellId].UnitCenter);
	}

	constexpr double ParameterTolerance = 1.0e-12;
	double CurrentT = 0.0;
	for (int32 SegmentIndex = 0;
		SegmentIndex < Config.ReferenceTraceSamples;
		++SegmentIndex)
	{
		int32 ActiveCellId = INDEX_NONE;
		double ActiveValue = -DBL_MAX;
		double ActiveSlope = -DBL_MAX;
		for (int32 CellId = 0;
			CellId < Cells.Num();
			++CellId)
		{
			const double Value =
				Intercepts[CellId]
				+ Slopes[CellId] * CurrentT;
			if (Value > ActiveValue + ParameterTolerance
				|| (FMath::IsNearlyEqual(
						Value,
						ActiveValue,
						ParameterTolerance)
					&& (Slopes[CellId]
							> ActiveSlope
								+ ParameterTolerance
						|| (FMath::IsNearlyEqual(
								Slopes[CellId],
								ActiveSlope,
								ParameterTolerance)
							&& CellId < ActiveCellId))))
			{
				ActiveCellId = CellId;
				ActiveValue = Value;
				ActiveSlope = Slopes[CellId];
			}
		}
		if (!Candidate.Cells.IsValidIndex(ActiveCellId))
		{
			Result.bTraceValid = false;
			return Result;
		}

		double NextT = 1.0;
		for (int32 CellId = 0; CellId < Cells.Num();
			++CellId)
		{
			const double SlopeDifference =
				Slopes[CellId] - ActiveSlope;
			if (SlopeDifference <= ParameterTolerance)
			{
				continue;
			}
			const double CrossingT =
				(Intercepts[ActiveCellId]
					- Intercepts[CellId])
				/ SlopeDifference;
			if (CrossingT
					> CurrentT + ParameterTolerance
				&& CrossingT < NextT)
			{
				NextT = CrossingT;
			}
		}
		NextT = FMath::Clamp(NextT, CurrentT, 1.0);
		const double SurfaceRadius =
			SurfaceRadiusForCell(
				Config,
				PlanetRadiusCM,
				Candidate.Cells[ActiveCellId])
			+ Config.VisibilityOcclusionEpsilonCM;
		if (MinimumRadiusSquaredOnSegmentInterval(
				Start,
				Delta,
				CurrentT,
				NextT)
			< SurfaceRadius * SurfaceRadius)
		{
			Result.bTerrainBlocked = true;
			return Result;
		}
		if (NextT >= 1.0 - ParameterTolerance)
		{
			Result.bVisible = true;
			return Result;
		}
		CurrentT = NextT;
	}
	Result.bTraceValid = false;
	return Result;
}

FABTSM3MonthlyVisibilityEntry EvaluateVisibility(
	const FABTSM3MonthlyEncounterSpatialConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	const FABTSM3MonthlySpatialCandidate& Candidate,
	const EABTSM3MonthlyObserverRole ObserverRole,
	const int32 ObserverEncounterId,
	const int32 ObserverCellId,
	const FABTSM3MonthlySpatialEncounter& Target,
	const bool bBruteReference,
	const int32 TraceSamples)
{
	FABTSM3MonthlyVisibilityEntry Entry;
	Entry.ObserverRole = ObserverRole;
	Entry.ObserverEncounterId = ObserverEncounterId;
	Entry.ObserverCellId = ObserverCellId;
	Entry.TargetEncounterId = Target.Contract.EncounterId;
	if (!Cells.IsValidIndex(ObserverCellId)
		|| !Cells.IsValidIndex(Target.TargetAnchorCellId)
		|| !Candidate.Cells.IsValidIndex(ObserverCellId)
		|| !Candidate.Cells.IsValidIndex(Target.TargetAnchorCellId)
		|| !IsFiniteVector(Target.AttackFaceDirection)
		|| Target.AttackFaceDirection.IsNearlyZero())
	{
		return Entry;
	}
	const FVector ObserverUp =
		Cells[ObserverCellId].UnitCenter.GetSafeNormal();
	const FVector TargetUp =
		Cells[Target.TargetAnchorCellId].UnitCenter.GetSafeNormal();
	const int32 RouteIndex =
		Candidate.Cells[ObserverCellId].NearestRoadOrderIndex;
	const TArray<int32>& OrderedRoadCellIds =
		Candidate.RecomputedRoute.OrderedRoadCellIds;
	if (!OrderedRoadCellIds.IsValidIndex(RouteIndex)
		|| OrderedRoadCellIds.Num() < 2)
	{
		return Entry;
	}
	const int32 PreviousRouteIndex = FMath::Max(
		0,
		RouteIndex - 1);
	const int32 NextRouteIndex = FMath::Min(
		OrderedRoadCellIds.Num() - 1,
		RouteIndex + 1);
	if (PreviousRouteIndex == NextRouteIndex
		|| !Cells.IsValidIndex(
			OrderedRoadCellIds[PreviousRouteIndex])
		|| !Cells.IsValidIndex(
			OrderedRoadCellIds[NextRouteIndex]))
	{
		return Entry;
	}
	FVector ObserverForward = FVector::VectorPlaneProject(
		Cells[OrderedRoadCellIds[NextRouteIndex]].UnitCenter
			- Cells[OrderedRoadCellIds[PreviousRouteIndex]].
				UnitCenter,
		ObserverUp).GetSafeNormal();
	if (!IsFiniteVector(ObserverForward)
		|| ObserverForward.IsNearlyZero())
	{
		return Entry;
	}
	const float ObserverRadius = SurfaceRadiusForCell(
		Config,
		PlanetRadiusCM,
		Candidate.Cells[ObserverCellId]);
	const float TargetRadius = SurfaceRadiusForCell(
		Config,
		PlanetRadiusCM,
		Candidate.Cells[Target.TargetAnchorCellId]);
	const FVector ObserverPivot =
		ObserverUp
		* (ObserverRadius
			+ Config.ObserverCharacterCenterHeightCM
			+ Config.ObserverLookAtHeightCM);
	const float ElevationRadians = FMath::DegreesToRadians(
		static_cast<float>(Config.CameraElevationDegrees));
	const FVector CameraOffsetDirection = (
		ObserverUp * FMath::Sin(ElevationRadians)
		- ObserverForward * FMath::Cos(ElevationRadians)).
			GetSafeNormal();
	if (!IsFiniteVector(CameraOffsetDirection)
		|| CameraOffsetDirection.IsNearlyZero())
	{
		return Entry;
	}
	const FVector TargetCenter =
		TargetUp
		* (TargetRadius + Config.TargetCenterHeightCM);
	const FVector TargetTop =
		TargetUp
		* (TargetRadius
			+ FMath::Max(
				Config.TargetCenterHeightCM,
				static_cast<int32>(
					Target.ProfileBoundsExtentCM.Z * 2.0)));
	const FVector AttackSample =
		TargetCenter
		+ Target.AttackFaceDirection
			* Target.ProfileBoundsExtentCM.X;
	const FVector Samples[] = {
		TargetCenter,
		TargetTop,
		AttackSample
	};
	const int32 OrbitDistancesCM[] = {
		Config.DefaultOrbitDistanceCM,
		Config.MaxOrbitDistanceCM
	};
	const float ArcDistanceCM =
		FMath::Acos(FMath::Clamp(
			FVector::DotProduct(ObserverUp, TargetUp),
			-1.0,
			1.0)) * PlanetRadiusCM;
	for (const int32 OrbitDistanceCM : OrbitDistancesCM)
	{
		const FVector ObserverPosition =
			ObserverPivot
			+ CameraOffsetDirection * OrbitDistanceCM;
		bool bCenterVisible = false;
		bool bAttackFaceVisible = false;
		bool bAnyVisible = false;
		for (int32 TargetSampleIndex = 0;
			TargetSampleIndex < UE_ARRAY_COUNT(Samples);
			++TargetSampleIndex)
		{
			const FVisibilityRayResult Ray =
				TraceVisibilityRay(
					Config,
					Cells,
					PlanetRadiusCM,
					Candidate,
					ObserverPosition,
					Samples[TargetSampleIndex],
					ObserverCellId,
					TraceSamples,
					bBruteReference);
			if (!Ray.bTraceValid)
			{
				return Entry;
			}
			++Entry.RayCount;
			bAnyVisible |= Ray.bVisible;
			bCenterVisible |= TargetSampleIndex == 0
				&& Ray.bVisible;
			bAttackFaceVisible |= TargetSampleIndex == 2
				&& Ray.bVisible;
			Entry.bIdealSphereBlocked |=
				Ray.bIdealSphereBlocked;
			Entry.bTerrainBlocked |= Ray.bTerrainBlocked;
		}
		++Entry.CameraSampleCount;
		Entry.VisibleCameraSampleCount +=
			bAnyVisible
			&& ArcDistanceCM <= Config.LandmarkMaxDistanceCM
				? 1
				: 0;
		Entry.AttackReadableCameraSampleCount +=
			bCenterVisible
			&& bAttackFaceVisible
			&& ArcDistanceCM
				<= Config.AttackReadableMaxDistanceCM
				? 1
				: 0;
	}
	if (Entry.CameraSampleCount
			== FABTSM3MonthlyEncounterBuilder::
				RequiredCameraSampleCount
		&& Entry.AttackReadableCameraSampleCount
			== Entry.CameraSampleCount)
	{
		Entry.VisibilityClass =
			EABTSM3MonthlyVisibilityClass::AttackReadable;
	}
	else if (Entry.VisibleCameraSampleCount > 0)
	{
		Entry.VisibilityClass =
			EABTSM3MonthlyVisibilityClass::LandmarkOnly;
	}
	else
	{
		Entry.VisibilityClass =
			EABTSM3MonthlyVisibilityClass::Hidden;
	}
	Entry.bScoutDetectable =
		ArcDistanceCM <= Config.ScoutDetectionRadiusCM;
	Entry.bEvaluationValid = true;
	return Entry;
}

FABTSM3MonthlyVisibilityEntry
	EvaluateVisibilityReferenceContinuous(
		const FABTSM3MonthlyEncounterSpatialConfig& Config,
		const TArray<FABTSM2Cell>& Cells,
		const float PlanetRadiusCM,
		const FABTSM3MonthlySpatialCandidate& Candidate,
		const EABTSM3MonthlyObserverRole ObserverRole,
		const int32 ObserverEncounterId,
		const int32 ObserverCellId,
		const FABTSM3MonthlySpatialEncounter& Target)
{
	FABTSM3MonthlyVisibilityEntry Entry;
	Entry.ObserverRole = ObserverRole;
	Entry.ObserverEncounterId = ObserverEncounterId;
	Entry.ObserverCellId = ObserverCellId;
	Entry.TargetEncounterId = Target.Contract.EncounterId;
	if (!Cells.IsValidIndex(ObserverCellId)
		|| !Cells.IsValidIndex(Target.TargetAnchorCellId)
		|| !Candidate.Cells.IsValidIndex(ObserverCellId)
		|| !Candidate.Cells.IsValidIndex(
			Target.TargetAnchorCellId)
		|| !IsFiniteVector(Target.AttackFaceDirection)
		|| Target.AttackFaceDirection.IsNearlyZero())
	{
		return Entry;
	}
	const FVector ObserverUp =
		Cells[ObserverCellId].UnitCenter.GetSafeNormal();
	const FVector TargetUp =
		Cells[Target.TargetAnchorCellId].UnitCenter.
			GetSafeNormal();
	const int32 RouteIndex =
		Candidate.Cells[ObserverCellId].
			NearestRoadOrderIndex;
	const TArray<int32>& OrderedRoadCellIds =
		Candidate.RecomputedRoute.OrderedRoadCellIds;
	if (!OrderedRoadCellIds.IsValidIndex(RouteIndex)
		|| OrderedRoadCellIds.Num() < 2)
	{
		return Entry;
	}
	const int32 PreviousRouteIndex =
		FMath::Max(0, RouteIndex - 1);
	const int32 NextRouteIndex = FMath::Min(
		OrderedRoadCellIds.Num() - 1,
		RouteIndex + 1);
	if (PreviousRouteIndex == NextRouteIndex
		|| !Cells.IsValidIndex(
			OrderedRoadCellIds[PreviousRouteIndex])
		|| !Cells.IsValidIndex(
			OrderedRoadCellIds[NextRouteIndex]))
	{
		return Entry;
	}
	const FVector ObserverForward =
		FVector::VectorPlaneProject(
			Cells[OrderedRoadCellIds[NextRouteIndex]].
					UnitCenter
				- Cells[
					OrderedRoadCellIds[
						PreviousRouteIndex]].
						UnitCenter,
			ObserverUp).GetSafeNormal();
	if (!IsFiniteVector(ObserverForward)
		|| ObserverForward.IsNearlyZero())
	{
		return Entry;
	}
	const float ObserverRadius = SurfaceRadiusForCell(
		Config,
		PlanetRadiusCM,
		Candidate.Cells[ObserverCellId]);
	const float TargetRadius = SurfaceRadiusForCell(
		Config,
		PlanetRadiusCM,
		Candidate.Cells[Target.TargetAnchorCellId]);
	const FVector ObserverPivot =
		ObserverUp
		* (ObserverRadius
			+ Config.ObserverCharacterCenterHeightCM
			+ Config.ObserverLookAtHeightCM);
	const float ElevationRadians = FMath::DegreesToRadians(
		static_cast<float>(Config.CameraElevationDegrees));
	const FVector CameraOffsetDirection = (
		ObserverUp * FMath::Sin(ElevationRadians)
		- ObserverForward * FMath::Cos(ElevationRadians)).
			GetSafeNormal();
	if (!IsFiniteVector(CameraOffsetDirection)
		|| CameraOffsetDirection.IsNearlyZero())
	{
		return Entry;
	}
	const FVector TargetCenter =
		TargetUp
		* (TargetRadius + Config.TargetCenterHeightCM);
	const FVector TargetTop =
		TargetUp
		* (TargetRadius
			+ FMath::Max(
				Config.TargetCenterHeightCM,
				static_cast<int32>(
					Target.ProfileBoundsExtentCM.Z * 2.0)));
	const FVector AttackSample =
		TargetCenter
		+ Target.AttackFaceDirection
			* Target.ProfileBoundsExtentCM.X;
	const FVector TargetSamples[] = {
		TargetCenter,
		TargetTop,
		AttackSample
	};
	const int32 OrbitDistancesCM[] = {
		Config.DefaultOrbitDistanceCM,
		Config.MaxOrbitDistanceCM
	};
	const float ArcDistanceCM =
		FMath::Acos(FMath::Clamp(
			FVector::DotProduct(ObserverUp, TargetUp),
			-1.0,
			1.0)) * PlanetRadiusCM;
	for (const int32 OrbitDistanceCM : OrbitDistancesCM)
	{
		const FVector ObserverPosition =
			ObserverPivot
			+ CameraOffsetDirection * OrbitDistanceCM;
		bool bCenterVisible = false;
		bool bAttackFaceVisible = false;
		bool bAnyVisible = false;
		for (int32 TargetSampleIndex = 0;
			TargetSampleIndex
				< UE_ARRAY_COUNT(TargetSamples);
			++TargetSampleIndex)
		{
			const FVisibilityRayResult Ray =
				TraceVisibilityRayReferenceContinuous(
					Config,
					Cells,
					PlanetRadiusCM,
					Candidate,
					ObserverPosition,
					TargetSamples[TargetSampleIndex]);
			if (!Ray.bTraceValid)
			{
				return Entry;
			}
			++Entry.RayCount;
			bAnyVisible |= Ray.bVisible;
			bCenterVisible |= TargetSampleIndex == 0
				&& Ray.bVisible;
			bAttackFaceVisible |= TargetSampleIndex == 2
				&& Ray.bVisible;
			Entry.bIdealSphereBlocked |=
				Ray.bIdealSphereBlocked;
			Entry.bTerrainBlocked |=
				Ray.bTerrainBlocked;
		}
		++Entry.CameraSampleCount;
		Entry.VisibleCameraSampleCount +=
			bAnyVisible
			&& ArcDistanceCM <= Config.LandmarkMaxDistanceCM
				? 1
				: 0;
		Entry.AttackReadableCameraSampleCount +=
			bCenterVisible
			&& bAttackFaceVisible
			&& ArcDistanceCM
				<= Config.AttackReadableMaxDistanceCM
				? 1
				: 0;
	}
	if (Entry.CameraSampleCount
			== FABTSM3MonthlyEncounterBuilder::
				RequiredCameraSampleCount
		&& Entry.AttackReadableCameraSampleCount
			== Entry.CameraSampleCount)
	{
		Entry.VisibilityClass =
			EABTSM3MonthlyVisibilityClass::AttackReadable;
	}
	else if (Entry.VisibleCameraSampleCount > 0)
	{
		Entry.VisibilityClass =
			EABTSM3MonthlyVisibilityClass::LandmarkOnly;
	}
	else
	{
		Entry.VisibilityClass =
			EABTSM3MonthlyVisibilityClass::Hidden;
	}
	Entry.bScoutDetectable =
		ArcDistanceCM <= Config.ScoutDetectionRadiusCM;
	Entry.bEvaluationValid = true;
	return Entry;
}

bool BuildVisibility(
	const FABTSM3MonthlyEncounterSpatialConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	const FABTSM3MonthlySpatialFaultInjection& FaultInjection,
	FABTSM3MonthlySpatialCandidate& Candidate,
	EABTSM3MonthlySpatialRejectReason& OutReason,
	FString& OutFailure)
{
	Candidate.VisibilityEntries.Reset();
	Candidate.OptimizedPVSRays = 0;
	const int32 StartCellId =
		FaultInjection.bInvalidateStartObserver
		? INDEX_NONE
		: Candidate.RecomputedRoute.OrderedRoadCellIds[0];
	const auto AddObserver = [&](
		const EABTSM3MonthlyObserverRole Role,
		const int32 ObserverEncounterId,
		const int32 ObserverCellId)
	{
		for (const FABTSM3MonthlySpatialEncounter& Target :
			Candidate.Encounters)
		{
			FABTSM3MonthlyVisibilityEntry Entry =
				EvaluateVisibility(
					Config,
					Cells,
					PlanetRadiusCM,
					Candidate,
					Role,
					ObserverEncounterId,
					ObserverCellId,
					Target,
					false,
					Config.OptimizedTraceSamples);
			Candidate.OptimizedPVSRays += Entry.RayCount;
			Candidate.VisibilityEntries.Add(MoveTemp(Entry));
		}
	};
	AddObserver(
		EABTSM3MonthlyObserverRole::Start,
		INDEX_NONE,
		StartCellId);
	for (const FABTSM3MonthlySpatialEncounter& Encounter :
		Candidate.Encounters)
	{
		AddObserver(
			EABTSM3MonthlyObserverRole::PreReveal,
			Encounter.Contract.EncounterId,
			Encounter.PreRevealCellId);
	}
	for (const FABTSM3MonthlySpatialEncounter& Encounter :
		Candidate.Encounters)
	{
		const FABTSM3PocketContract* Reveal = FindPocket(
			Candidate.Pockets,
			Encounter.Contract.ScoutRevealPocketId);
		AddObserver(
			EABTSM3MonthlyObserverRole::Reveal,
			Encounter.Contract.EncounterId,
			Reveal != nullptr ? Reveal->AnchorCellId : INDEX_NONE);
	}
	if (Candidate.OptimizedPVSRays
		> Config.MaxOptimizedPVSRaysPerWorld)
	{
		OutReason =
			EABTSM3MonthlySpatialRejectReason::RayBudgetExceeded;
		OutFailure = TEXT("OptimizedPVSRays");
		return false;
	}
	if (Candidate.VisibilityEntries.Num()
		!= FABTSM3MonthlyEncounterBuilder::
			RequiredVisibilityEntryCount)
	{
		OutReason =
			EABTSM3MonthlySpatialRejectReason::PVSInvalid;
		OutFailure = TEXT("PVSCount");
		return false;
	}
	for (const FABTSM3MonthlyVisibilityEntry& Entry :
		Candidate.VisibilityEntries)
	{
		if (!Entry.bEvaluationValid)
		{
			OutReason =
				EABTSM3MonthlySpatialRejectReason::PVSInvalid;
			OutFailure = TEXT("PVSEvaluation");
			return false;
		}
	}

	const auto FindEntry = [&Candidate](
		const EABTSM3MonthlyObserverRole Role,
		const int32 ObserverEncounterId,
		const int32 TargetEncounterId)
		-> const FABTSM3MonthlyVisibilityEntry*
	{
		return Candidate.VisibilityEntries.FindByPredicate(
			[=](const FABTSM3MonthlyVisibilityEntry& Entry)
			{
				return Entry.ObserverRole == Role
					&& Entry.ObserverEncounterId
						== ObserverEncounterId
					&& Entry.TargetEncounterId
						== TargetEncounterId;
			});
	};
	for (int32 TargetOrder = 0;
		TargetOrder < Candidate.Encounters.Num();
		++TargetOrder)
	{
		const FABTSM3MonthlySpatialEncounter& Target =
			Candidate.Encounters[TargetOrder];
		const FABTSM3MonthlyVisibilityEntry* StartEntry =
			FindEntry(
				EABTSM3MonthlyObserverRole::Start,
				INDEX_NONE,
				Target.Contract.EncounterId);
		if (StartEntry == nullptr
			|| (TargetOrder == 0
				&& StartEntry->VisibilityClass
					!= EABTSM3MonthlyVisibilityClass::
						AttackReadable)
			|| (TargetOrder > 0
				&& StartEntry->
					AttackReadableCameraSampleCount > 0))
		{
			OutReason =
				EABTSM3MonthlySpatialRejectReason::
					PVSContractFailed;
			OutFailure = FString::Printf(
				TEXT("PVSStart:%d"),
				TargetOrder);
			return false;
		}
		const FABTSM3MonthlyVisibilityEntry* PreReveal =
			FindEntry(
				EABTSM3MonthlyObserverRole::PreReveal,
				Target.Contract.EncounterId,
				Target.Contract.EncounterId);
		const FABTSM3MonthlyVisibilityEntry* Reveal =
			FindEntry(
				EABTSM3MonthlyObserverRole::Reveal,
				Target.Contract.EncounterId,
				Target.Contract.EncounterId);
		if (PreReveal == nullptr
			|| Reveal == nullptr
			|| (TargetOrder > 0
				&& PreReveal->
					AttackReadableCameraSampleCount > 0)
			|| (Target.RevealPolicy
					== EABTSM3MonthlyRevealPolicy::DirectVisual
				&& Reveal->VisibilityClass
					!= EABTSM3MonthlyVisibilityClass::
						AttackReadable)
			|| (Target.RevealPolicy
					== EABTSM3MonthlyRevealPolicy::ScoutRequired
				&& !Reveal->bScoutDetectable))
		{
			OutReason =
				EABTSM3MonthlySpatialRejectReason::
					PVSContractFailed;
			OutFailure = FString::Printf(
				TEXT("PVSReveal:%d"),
				TargetOrder);
			return false;
		}
		for (int32 FutureOrder = TargetOrder + 2;
			FutureOrder < Candidate.Encounters.Num();
			++FutureOrder)
		{
			const FABTSM3MonthlyVisibilityEntry* Future =
				FindEntry(
					EABTSM3MonthlyObserverRole::Reveal,
					Target.Contract.EncounterId,
					Candidate.Encounters[FutureOrder]
						.Contract.EncounterId);
			if (Future == nullptr
				|| Future->
					AttackReadableCameraSampleCount > 0)
			{
				OutReason =
					EABTSM3MonthlySpatialRejectReason::
						PVSContractFailed;
				OutFailure = FString::Printf(
					TEXT("PVSFuture:%d:%d"),
					TargetOrder,
					FutureOrder);
				return false;
			}
		}
	}
	return true;
}

void ComputeCoverage(FABTSM3MonthlySpatialCandidate& Candidate)
{
	TSet<int32> PlayableCells;
	for (const FABTSM3PlayableEnvelope& Envelope :
		Candidate.PlayableEnvelopes)
	{
		for (const FABTSM3PlayableCellRole& Role : Envelope.Cells)
		{
			PlayableCells.Add(Role.CellId);
		}
	}
	Candidate.PlayableCellCount = PlayableCells.Num();
	Candidate.ActiveRoleCellCount = 0;
	Candidate.ApprovedTransitionCellCount = 0;
	Candidate.DeepWildCellCount = 0;
	for (const int32 CellId : PlayableCells)
	{
		if (Candidate.Cells[CellId].ActiveRoleMask != 0)
		{
			++Candidate.ActiveRoleCellCount;
		}
		else if (Candidate.Cells[CellId].bApprovedTransition)
		{
			++Candidate.ApprovedTransitionCellCount;
		}
		else
		{
			++Candidate.DeepWildCellCount;
		}
	}
	Candidate.ActiveRoleCoveragePermille =
		Candidate.PlayableCellCount > 0
		? Candidate.ActiveRoleCellCount * 1000
			/ Candidate.PlayableCellCount
		: 0;
	Candidate.DeepWildPermille =
		Candidate.PlayableCellCount > 0
		? Candidate.DeepWildCellCount * 1000
			/ Candidate.PlayableCellCount
		: 1000;
}

uint64 ComputeEncounterHash(
	const FABTSM3MonthlySpatialEncounter& Encounter)
{
	FCanonicalHash64 Hash;
	Hash.AddInt32(2);
	const FABTSM3EncounterContract& Contract = Encounter.Contract;
	Hash.AddInt32(Contract.EncounterId);
	Hash.AddInt32(Contract.OrderIndex);
	Hash.AddInt32(Contract.MissionTaskId);
	Hash.AddInt32(Contract.RouteBeatId);
	Hash.AddInt32(static_cast<int32>(Contract.Role));
	Hash.AddInt32(static_cast<int32>(Contract.BuildingPurpose));
	Hash.AddInt32(Contract.DifficultyBand);
	Hash.AddInt32(FMath::RoundToInt(
		Contract.ProgressDistanceCM));
	Hash.AddInt32(FMath::RoundToInt(
		Contract.FlowS * FlowQuantization));
	Hash.AddInt32(Encounter.FlowQ);
	Hash.AddInt32(Contract.RequiredKeys.Num());
	for (const EABTSM3ProgressKey Key : Contract.RequiredKeys)
	{
		Hash.AddInt32(static_cast<int32>(Key));
	}
	Hash.AddInt32(Contract.GrantedKeys.Num());
	for (const EABTSM3ProgressKey Key : Contract.GrantedKeys)
	{
		Hash.AddInt32(static_cast<int32>(Key));
	}
	Hash.AddInt32(Contract.RoadArrivalPocketId);
	Hash.AddInt32(Contract.ScoutRevealPocketId);
	Hash.AddInt32(Contract.SlingshotPocketId);
	Hash.AddInt32(Contract.TargetEnvelopePocketId);
	Hash.AddInt32(Contract.TargetAnchorPocketId);
	Hash.AddInt32(Contract.RewardPocketId);
	Hash.AddInt32(Contract.ExitPocketId);
	Hash.AddInt32(Contract.BallisticWitnessId);
	Hash.AddInt32(Contract.BiomeDistrictId);
	Hash.AddName(Contract.ResolvedM7ProfileId);
	Hash.AddInt64(Contract.ProfileCatalogHash);
	Hash.AddInt32(static_cast<int32>(Contract.Resolution));
	Hash.AddInt32(static_cast<int32>(Encounter.RevealPolicy));
	Hash.AddInt32(Encounter.PreRevealCellId);
	Hash.AddInt32(Encounter.TargetAnchorCellId);
	Hash.AddInt32(Encounter.MainRoadDistanceCells);
	Hash.AddInt32(Encounter.RequiredRoadClearanceCells);
	Hash.AddIntArray(Encounter.TargetFootprintCellIds);
	Hash.AddIntArray(Encounter.TargetNoRoadCellIds);
	Hash.AddName(Encounter.ResolvedFixtureProfileId);
	Hash.AddInt64(Encounter.ProfileCatalogHash);
	Hash.AddVectorCM(Encounter.ProfileBoundsExtentCM);
	Hash.AddVectorCM(Encounter.AttackFaceDirection);
	Hash.AddInt64(Encounter.VisualSignature);
	return Hash.Get();
}

bool BuildSpatialCandidateAttempt(
	const int32 WorldSeed,
	const int32 ReservationVariant,
	const FABTSM3MonthlyEncounterSpatialConfig& Config,
	const FABTSM3MonthlyRouteConfig& RouteConfig,
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	const FABTSM3MonthlyRouteCandidate& SourceRoute,
	const FABTSM3MonthlySpatialFaultInjection& FaultInjection,
	const uint64 ProfileCatalogHash,
	FABTSM3MonthlySpatialCandidate& OutCandidate,
	EABTSM3MonthlySpatialRejectReason& OutReason,
	FString& OutFailure)
{
	if (FaultInjection.RejectedSourceCandidateId
			== SourceRoute.CandidateId)
	{
		OutCandidate = FABTSM3MonthlySpatialCandidate();
		OutCandidate.SourceRouteCandidateId =
			SourceRoute.CandidateId;
		OutCandidate.SourceRouteCandidateHash =
			SourceRoute.CandidateHash;
		OutCandidate.Backtracks = 0;
		OutReason =
			EABTSM3MonthlySpatialRejectReason::
				SearchBudgetExceeded;
		OutFailure = FString::Printf(
			TEXT("InjectedSourceCandidate:%d"),
			SourceRoute.CandidateId);
		return false;
	}
	if (ReservationVariant
		< FaultInjection.ForcedReservationFailures)
	{
		OutCandidate = FABTSM3MonthlySpatialCandidate();
		OutCandidate.SourceRouteCandidateId =
			SourceRoute.CandidateId;
		OutCandidate.SourceRouteCandidateHash =
			SourceRoute.CandidateHash;
		OutCandidate.Backtracks = ReservationVariant;
		OutReason =
			EABTSM3MonthlySpatialRejectReason::ReservationFailed;
		OutFailure = FString::Printf(
			TEXT("InjectedReservationVariant:%d"),
			ReservationVariant);
		return false;
	}
	if (!ReserveEncounters(
			WorldSeed,
			ReservationVariant,
			Config,
			Cells,
			PlanetRadiusCM,
			SourceRoute,
			ProfileCatalogHash,
			OutCandidate,
			OutReason,
			OutFailure))
	{
		return false;
	}
	OutCandidate.Backtracks = ReservationVariant;
	if (!BuildEnvelopesAndBiomes(
			Config,
			Cells,
			SourceRoute,
			OutCandidate,
			OutFailure))
	{
		OutReason =
			EABTSM3MonthlySpatialRejectReason::BiomeCoverageFailed;
		return false;
	}
	BuildScratchContext(
		WorldSeed,
		Config,
		Cells,
		SourceRoute,
		FaultInjection,
		OutCandidate);
	EABTSM3MonthlyRouteRejectReason RouteReason =
		EABTSM3MonthlyRouteRejectReason::None;
	if (!FABTSM3MonthlyRouteBuilder::RebuildCandidateStrict(
			WorldSeed,
			RouteConfig,
			Cells,
			PlanetRadiusCM,
			OutCandidate.RoadContext,
			SourceRoute,
			OutCandidate.RecomputedRoute,
			RouteReason,
			OutFailure))
	{
		OutReason =
			EABTSM3MonthlySpatialRejectReason::RoadRebuildFailed;
		return false;
	}
	if (!UpdateFinalRouteFields(
			Config,
			Cells,
			PlanetRadiusCM,
			OutCandidate,
			OutFailure))
	{
		if (OutFailure.StartsWith(TEXT("FinalSpacing")))
		{
			OutReason =
				EABTSM3MonthlySpatialRejectReason::
					EncounterSpacingFailed;
		}
		else if (OutFailure.StartsWith(
				TEXT("FinalStartSemantic"))
			|| OutFailure.StartsWith(TEXT("FinalObserver")))
		{
			OutReason =
				EABTSM3MonthlySpatialRejectReason::
					PVSContractFailed;
		}
		else if (OutFailure.StartsWith(TEXT("FinalExit")))
		{
			OutReason =
				EABTSM3MonthlySpatialRejectReason::
					PocketIdentityInvalid;
		}
		else if (OutFailure.StartsWith(
			TEXT("FinalRouteEnvelope")))
		{
			OutReason =
				EABTSM3MonthlySpatialRejectReason::
					RouteMetricsFailed;
		}
		else
		{
			OutReason =
				EABTSM3MonthlySpatialRejectReason::TargetOverlap;
		}
		return false;
	}
	ComputeCoverage(OutCandidate);
	if (OutCandidate.ActiveRoleCoveragePermille
			< Config.MinActiveRoleCoveragePermille
		|| OutCandidate.DeepWildPermille
			> Config.MaxDeepWildPermille)
	{
		OutReason =
			EABTSM3MonthlySpatialRejectReason::BiomeCoverageFailed;
		OutFailure = TEXT("EnvelopeCoverage");
		return false;
	}
	if (!BuildVisibility(
			Config,
			Cells,
			PlanetRadiusCM,
			FaultInjection,
			OutCandidate,
			OutReason,
			OutFailure))
	{
		return false;
	}
	for (FABTSM3MonthlySpatialEncounter& Encounter :
		OutCandidate.Encounters)
	{
		Encounter.EncounterHash = static_cast<int64>(
			ComputeEncounterHash(Encounter));
	}
	OutCandidate.SpatialScore =
		1000000
		+ OutCandidate.RecomputedRoute.RouteScore
		+ OutCandidate.ActiveRoleCoveragePermille * 10
		- OutCandidate.DeepWildPermille * 20
		- OutCandidate.OptimizedPVSRays
		- OutCandidate.Backtracks * 100;
	OutCandidate.bHardPass = true;
	OutCandidate.RejectReason =
		EABTSM3MonthlySpatialRejectReason::None;
	OutCandidate.SpatialCandidateHash = static_cast<int64>(
		FABTSM3MonthlyEncounterBuilder::ComputeCandidateHash(
			OutCandidate));
	return true;
}

bool IsRetryableSpatialFailure(
	const EABTSM3MonthlySpatialRejectReason Reason)
{
	switch (Reason)
	{
	case EABTSM3MonthlySpatialRejectReason::ReservationFailed:
	case EABTSM3MonthlySpatialRejectReason::RoadRebuildFailed:
	case EABTSM3MonthlySpatialRejectReason::RouteMetricsFailed:
	case EABTSM3MonthlySpatialRejectReason::EncounterSpacingFailed:
	case EABTSM3MonthlySpatialRejectReason::PocketIdentityInvalid:
	case EABTSM3MonthlySpatialRejectReason::TargetOverlap:
	case EABTSM3MonthlySpatialRejectReason::BiomeCoverageFailed:
	case EABTSM3MonthlySpatialRejectReason::PVSContractFailed:
		return true;
	default:
		return false;
	}
}

bool BuildSpatialCandidate(
	const int32 WorldSeed,
	const FABTSM3MonthlyEncounterSpatialConfig& Config,
	const FABTSM3MonthlyRouteConfig& RouteConfig,
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	const FABTSM3MonthlyRouteCandidate& SourceRoute,
	const FABTSM3MonthlySpatialFaultInjection& FaultInjection,
	const uint64 ProfileCatalogHash,
	FABTSM3MonthlySpatialCandidate& OutCandidate,
	EABTSM3MonthlySpatialRejectReason& OutReason,
	FString& OutFailure)
{
	const int32 MaximumVariant =
		FaultInjection.bBlockEverySourceRoadCell
			|| FaultInjection.bInvalidateStartObserver
		? 0
		: Config.MaxSpatialBacktracksPerCandidate;
	for (int32 ReservationVariant = 0;
		ReservationVariant <= MaximumVariant;
		++ReservationVariant)
	{
		FABTSM3MonthlySpatialCandidate Candidate;
		EABTSM3MonthlySpatialRejectReason Reason =
			EABTSM3MonthlySpatialRejectReason::None;
		FString Failure;
		if (BuildSpatialCandidateAttempt(
				WorldSeed,
				ReservationVariant,
				Config,
				RouteConfig,
				Cells,
				PlanetRadiusCM,
				SourceRoute,
				FaultInjection,
				ProfileCatalogHash,
				Candidate,
				Reason,
				Failure))
		{
			OutCandidate = MoveTemp(Candidate);
			OutReason =
				EABTSM3MonthlySpatialRejectReason::None;
			OutFailure.Reset();
			return true;
		}
		OutCandidate = MoveTemp(Candidate);
		OutCandidate.Backtracks = ReservationVariant;
		OutReason = Reason;
		OutFailure = MoveTemp(Failure);
		if (!IsRetryableSpatialFailure(Reason))
		{
			break;
		}
	}
	return false;
}

bool SpatialCandidateLess(
	const FABTSM3MonthlySpatialCandidate& A,
	const FABTSM3MonthlySpatialCandidate& B)
{
	if (A.SpatialScore != B.SpatialScore)
	{
		return A.SpatialScore > B.SpatialScore;
	}
	if (A.RecomputedRoute.RouteScore
		!= B.RecomputedRoute.RouteScore)
	{
		return A.RecomputedRoute.RouteScore
			> B.RecomputedRoute.RouteScore;
	}
	if (A.SourceRouteCandidateId
		!= B.SourceRouteCandidateId)
	{
		return A.SourceRouteCandidateId
			< B.SourceRouteCandidateId;
	}
	return static_cast<uint64>(A.SpatialCandidateHash)
		< static_cast<uint64>(B.SpatialCandidateHash);
}

FABTSM3MonthlySpatialAttemptReport MakeAttemptReport(
	const FABTSM3MonthlySpatialCandidate& Candidate,
	const EABTSM3MonthlySpatialRejectReason Reason,
	const FString& Failure)
{
	FABTSM3MonthlySpatialAttemptReport Report;
	Report.SourceRouteCandidateId =
		Candidate.SourceRouteCandidateId;
	Report.SourceRouteCandidateHash =
		Candidate.SourceRouteCandidateHash;
	Report.ReservationHash = Candidate.ReservationHash;
	Report.RoadContextHash = Candidate.RoadContextHash;
	Report.RecomputedRouteCandidateHash =
		Candidate.RecomputedRoute.CandidateHash;
	Report.bHardPass = Candidate.bHardPass;
	Report.RejectReason = Reason;
	Report.FailureCode = Failure.IsEmpty()
		? NAME_None
		: FName(*Failure);
	Report.SpatialScore = Candidate.SpatialScore;
	Report.Backtracks = Candidate.Backtracks;
	return Report;
}

bool ValidatePVSContract(
	const FABTSM3MonthlyEncounterSpatialConfig& Config,
	const FABTSM3MonthlySpatialCandidate& Candidate,
	FString& OutFailure)
{
	if (Candidate.VisibilityEntries.Num()
			!= FABTSM3MonthlyEncounterBuilder::
				RequiredVisibilityEntryCount
		|| Candidate.OptimizedPVSRays
			> Config.MaxOptimizedPVSRaysPerWorld)
	{
		OutFailure = TEXT("PVSCountOrBudget");
		return false;
	}
	int32 RaySum = 0;
	int32 PreviousObserverRole = INDEX_NONE;
	int32 PreviousObserverEncounter = MIN_int32;
	int32 PreviousTargetEncounter = MIN_int32;
	for (const FABTSM3MonthlyVisibilityEntry& Entry :
		Candidate.VisibilityEntries)
	{
		const int32 ObserverRole =
			static_cast<int32>(Entry.ObserverRole);
		const bool bVisibilityAggregationValid =
			Entry.CameraSampleCount
				== FABTSM3MonthlyEncounterBuilder::
					RequiredCameraSampleCount
			&& Entry.AttackReadableCameraSampleCount >= 0
			&& Entry.AttackReadableCameraSampleCount
				<= Entry.CameraSampleCount
			&& Entry.VisibleCameraSampleCount >= 0
			&& Entry.VisibleCameraSampleCount
				<= Entry.CameraSampleCount
			&& Entry.AttackReadableCameraSampleCount
				<= Entry.VisibleCameraSampleCount
			&& ((Entry.VisibilityClass
						== EABTSM3MonthlyVisibilityClass::
							AttackReadable
					&& Entry.AttackReadableCameraSampleCount
						== Entry.CameraSampleCount)
				|| (Entry.VisibilityClass
						== EABTSM3MonthlyVisibilityClass::
							LandmarkOnly
					&& Entry.VisibleCameraSampleCount > 0
					&& Entry.AttackReadableCameraSampleCount
						< Entry.CameraSampleCount)
				|| (Entry.VisibilityClass
						== EABTSM3MonthlyVisibilityClass::Hidden
					&& Entry.VisibleCameraSampleCount == 0));
		if (!Entry.bEvaluationValid
			|| !Candidate.Cells.IsValidIndex(Entry.ObserverCellId)
			|| !bVisibilityAggregationValid
			|| Entry.RayCount
				!= FABTSM3MonthlyEncounterBuilder::
					RequiredCameraSampleCount * 3
			|| ObserverRole < PreviousObserverRole
			|| (ObserverRole == PreviousObserverRole
				&& Entry.ObserverEncounterId
					< PreviousObserverEncounter)
			|| (ObserverRole == PreviousObserverRole
				&& Entry.ObserverEncounterId
					== PreviousObserverEncounter
				&& Entry.TargetEncounterId
					<= PreviousTargetEncounter))
		{
			OutFailure = TEXT("PVSCanonicalOrder");
			return false;
		}
		if (ObserverRole != PreviousObserverRole
			|| Entry.ObserverEncounterId
				!= PreviousObserverEncounter)
		{
			PreviousTargetEncounter = MIN_int32;
		}
		PreviousObserverRole = ObserverRole;
		PreviousObserverEncounter = Entry.ObserverEncounterId;
		PreviousTargetEncounter = Entry.TargetEncounterId;
		RaySum += Entry.RayCount;
	}
	if (RaySum != Candidate.OptimizedPVSRays)
	{
		OutFailure = TEXT("PVSRaySum");
		return false;
	}

	const auto FindEntry = [&Candidate](
		const EABTSM3MonthlyObserverRole Role,
		const int32 ObserverEncounterId,
		const int32 TargetEncounterId)
		-> const FABTSM3MonthlyVisibilityEntry*
	{
		return Candidate.VisibilityEntries.FindByPredicate(
			[=](const FABTSM3MonthlyVisibilityEntry& Entry)
			{
				return Entry.ObserverRole == Role
					&& Entry.ObserverEncounterId
						== ObserverEncounterId
					&& Entry.TargetEncounterId
						== TargetEncounterId;
			});
	};
	for (int32 Order = 0; Order < Candidate.Encounters.Num(); ++Order)
	{
		const FABTSM3MonthlySpatialEncounter& Encounter =
			Candidate.Encounters[Order];
		const FABTSM3MonthlyVisibilityEntry* Start = FindEntry(
			EABTSM3MonthlyObserverRole::Start,
			INDEX_NONE,
			Encounter.Contract.EncounterId);
		const FABTSM3MonthlyVisibilityEntry* PreReveal = FindEntry(
			EABTSM3MonthlyObserverRole::PreReveal,
			Encounter.Contract.EncounterId,
			Encounter.Contract.EncounterId);
		const FABTSM3MonthlyVisibilityEntry* Reveal = FindEntry(
			EABTSM3MonthlyObserverRole::Reveal,
			Encounter.Contract.EncounterId,
			Encounter.Contract.EncounterId);
		if (Start == nullptr
			|| PreReveal == nullptr
			|| Reveal == nullptr
			|| (Order == 0
				&& Start->VisibilityClass
					!= EABTSM3MonthlyVisibilityClass::
						AttackReadable)
			|| (Order > 0
				&& Start->AttackReadableCameraSampleCount > 0)
			|| (Order > 0
				&& PreReveal->
					AttackReadableCameraSampleCount > 0)
			|| (Encounter.RevealPolicy
					== EABTSM3MonthlyRevealPolicy::DirectVisual
				&& Reveal->VisibilityClass
					!= EABTSM3MonthlyVisibilityClass::
						AttackReadable)
			|| (Encounter.RevealPolicy
					== EABTSM3MonthlyRevealPolicy::ScoutRequired
				&& !Reveal->bScoutDetectable))
		{
			OutFailure = FString::Printf(
				TEXT("PVSMatrix:%d"),
				Order);
			return false;
		}
		for (int32 Future = Order + 2;
			Future < Candidate.Encounters.Num();
			++Future)
		{
			const FABTSM3MonthlyVisibilityEntry* FutureEntry =
				FindEntry(
					EABTSM3MonthlyObserverRole::Reveal,
					Encounter.Contract.EncounterId,
					Candidate.Encounters[Future]
						.Contract.EncounterId);
			if (FutureEntry == nullptr
				|| FutureEntry->
					AttackReadableCameraSampleCount > 0)
			{
				OutFailure = FString::Printf(
					TEXT("PVSFuture:%d:%d"),
					Order,
					Future);
				return false;
			}
		}
	}
	return true;
}

bool ValidateCandidate(
	const FABTSM3MonthlyEncounterSpatialConfig& Config,
	const FABTSM3MonthlyRouteConfig& RouteConfig,
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	const FABTSM3MonthlyRoutePool& SourceRoutePool,
	const FABTSM3MonthlySpatialFaultInjection& FaultInjection,
	const FABTSM3MonthlySpatialCandidate& Candidate,
	const uint64 ProfileCatalogHash,
	const bool bRecomputeDerivedArtifacts,
	FString& OutFailure)
{
	const FABTSM3MonthlyRouteCandidate* SourceRoute =
		SourceRoutePool.RetainedCandidates.FindByPredicate(
			[&Candidate](
				const FABTSM3MonthlyRouteCandidate& Route)
			{
				return Route.CandidateId
						== Candidate.SourceRouteCandidateId
					&& Route.CandidateHash
						== Candidate.SourceRouteCandidateHash;
			});
	if (SourceRoute == nullptr
		|| !Candidate.bHardPass
		|| Candidate.RejectReason
			!= EABTSM3MonthlySpatialRejectReason::None
		|| Candidate.Cells.Num() != Cells.Num()
		|| Candidate.Encounters.Num()
			!= Config.DestructibleEncounterCount
		|| Candidate.Pockets.Num()
			!= FABTSM3MonthlyEncounterBuilder::RequiredPocketCount
		|| Candidate.PlayableEnvelopes.Num()
			!= Config.DestructibleEncounterCount
		|| Candidate.BiomeDistricts.Num()
			!= Config.DestructibleEncounterCount + 1
		|| Candidate.Backtracks < 0
		|| Candidate.Backtracks
			> Config.MaxSpatialBacktracksPerCandidate
		|| Candidate.PreRoadReservedPlayableCellIds.IsEmpty()
		|| !IsStrictlySortedUnique(
			Candidate.PreRoadReservedPlayableCellIds)
		|| Candidate.RoadContext.Cells.Num() != Cells.Num()
		|| Candidate.RecomputedRoute.ControlCellIds
			!= SourceRoute->ControlCellIds
		|| static_cast<uint64>(
				Candidate.RecomputedRoute.CandidateHash)
			!= FABTSM3MonthlyRouteBuilder::
				ComputeCandidateHash(Candidate.RecomputedRoute)
		|| static_cast<uint64>(Candidate.RoadContextHash)
			!= FABTSM3MonthlyRouteBuilder::
				ComputeRoadContextHash(Candidate.RoadContext)
		|| static_cast<uint64>(Candidate.ReservationHash)
			!= ComputeReservationHash(Candidate))
	{
		OutFailure = TEXT("CandidateIdentity");
		return false;
	}
	TSet<int32> PreRoadReservedPlayableCells;
	for (const int32 CellId :
		Candidate.PreRoadReservedPlayableCellIds)
	{
		if (!Cells.IsValidIndex(CellId))
		{
			OutFailure = FString::Printf(
				TEXT("PreRoadReservationCell:%d"),
				CellId);
			return false;
		}
		PreRoadReservedPlayableCells.Add(CellId);
	}
	if (bRecomputeDerivedArtifacts)
	{
		FABTSM3MonthlyRouteCandidate ExpectedRecomputedRoute;
		EABTSM3MonthlyRouteRejectReason RouteReason =
			EABTSM3MonthlyRouteRejectReason::None;
		FString RouteFailure;
		if (!FABTSM3MonthlyRouteBuilder::RebuildCandidateStrict(
				SourceRoutePool.WorldSeed,
				RouteConfig,
				Cells,
				PlanetRadiusCM,
				Candidate.RoadContext,
				*SourceRoute,
				ExpectedRecomputedRoute,
				RouteReason,
				RouteFailure)
			|| !FABTSM3MonthlyRouteCandidate::StaticStruct()
				->CompareScriptStruct(
					&ExpectedRecomputedRoute,
					&Candidate.RecomputedRoute,
					PPF_None))
		{
			OutFailure = FString::Printf(
				TEXT("RecomputedRoute:%s:%s"),
				FABTSM3MonthlyRouteBuilder::GetRejectReasonName(
					RouteReason),
				*RouteFailure);
			return false;
		}
	}
	for (int32 CellId = 0; CellId < Candidate.Cells.Num(); ++CellId)
	{
		const FABTSM3MonthlySpatialCell& Cell =
			Candidate.Cells[CellId];
		const bool bInjectedHardBlock =
			(FaultInjection.bBlockEverySourceRoadCell
				&& SourceRoute->OrderedRoadCellIds.
					Contains(CellId))
			|| (SourceRoute->OrderedRoadCellIds.IsValidIndex(
					FaultInjection.
						BlockedSourceRoadOrderIndex)
				&& CellId
					== SourceRoute->OrderedRoadCellIds[
						FaultInjection.
							BlockedSourceRoadOrderIndex]);
		if (Cell.CellId != CellId
			|| Cell.BiomeDistrictId == INDEX_NONE
			|| Cell.HeightQ < 0
			|| Cell.HeightQ > FlowQuantization
			|| (Cell.bTargetFootprint && Cell.bWater)
			|| (Cell.bAttackCorridor && Cell.bWater)
			|| Cell.bApprovedTransition
				!= (Cell.PrimaryEnvelopeId != INDEX_NONE
					&& Cell.ActiveRoleMask == 0)
			|| Candidate.RoadContext.Cells[CellId].bHardBlocked
				!= (Cell.bNoRoad || bInjectedHardBlock))
		{
			OutFailure = FString::Printf(
				TEXT("CandidateCell:%d"),
				CellId);
			return false;
		}
	}
	TSet<int32> SeenPocketIds;
	TSet<int32> SeenTargetConstruction;
	int32 PreviousProgressCM = INDEX_NONE;
	int32 PreviousDifficulty = INDEX_NONE;
	int32 StrictDifficultyRises = 0;
	TSet<int64> VisualSignatures;
	TSet<uint8> EncounterBiomes;
	TSet<int32> ExpectedAttackCorridorCells;
	TArray<int32> ValidatedFinalRoadDistance;
	TArray<int32> ValidatedFinalNearestRouteIndex;
	if (!BuildNearestRouteFields(
			Cells,
			Candidate.RecomputedRoute,
			ValidatedFinalRoadDistance,
			ValidatedFinalNearestRouteIndex))
	{
		OutFailure = TEXT("FinalRouteFields");
		return false;
	}
	TBitArray<> ReservedCells(false, Cells.Num());
	for (const int32 CellId :
		Candidate.PreRoadReservedPlayableCellIds)
	{
		ReservedCells[CellId] = true;
	}
	TArray<int32> RouteIndexByCell;
	RouteIndexByCell.Init(INDEX_NONE, Cells.Num());
	for (int32 RouteIndex = 0;
		RouteIndex
			< Candidate.RecomputedRoute.OrderedRoadCellIds.Num();
		++RouteIndex)
	{
		RouteIndexByCell[
			Candidate.RecomputedRoute.OrderedRoadCellIds[
				RouteIndex]] = RouteIndex;
	}
	for (int32 Order = 0;
		Order < Candidate.Encounters.Num();
		++Order)
	{
		const FABTSM3MonthlySpatialEncounter& Encounter =
			Candidate.Encounters[Order];
		const FABTSM3EncounterContract& Contract =
			Encounter.Contract;
		const FIntPoint Window =
			Config.TargetRoadDistanceWindowsCells[Order];
		const EABTSM3BuildingPurpose ExpectedPurpose =
			Order == 4
				? EABTSM3BuildingPurpose::GravityTraining
				: (Order == 0
					? EABTSM3BuildingPurpose::ProgressionTarget
					: EABTSM3BuildingPurpose::ResourceTarget);
		if (Contract.EncounterId != EncounterId(Order)
			|| Contract.OrderIndex != Order
			|| Contract.MissionTaskId != INDEX_NONE
			|| Contract.RouteBeatId != BeatId(Order)
			|| Contract.Role
				!= EABTSM3EncounterRole::DestructibleTarget
			|| Contract.BuildingPurpose != ExpectedPurpose
			|| Contract.DifficultyBand
				!= Config.DifficultyBands[Order]
			|| FMath::RoundToInt(
					Contract.FlowS * FlowQuantization)
				!= Encounter.FlowQ
			|| !Contract.RequiredKeys.IsEmpty()
			|| !Contract.GrantedKeys.IsEmpty()
			|| Contract.BallisticWitnessId != INDEX_NONE
			|| Contract.BiomeDistrictId != BiomeId(Order)
			|| Contract.Resolution
				!= EABTSM3SchemaResolution::Finalized
			|| Encounter.RevealPolicy
				!= Config.RevealPolicies[Order]
			|| Encounter.ProfileCatalogHash
				!= static_cast<int64>(ProfileCatalogHash)
			|| Contract.ProfileCatalogHash
				!= static_cast<int64>(ProfileCatalogHash)
			|| Encounter.ResolvedFixtureProfileId.IsNone()
			|| Contract.ResolvedM7ProfileId
				!= Encounter.ResolvedFixtureProfileId
			|| Encounter.MainRoadDistanceCells < Window.X
			|| Encounter.MainRoadDistanceCells
				< Encounter.RequiredRoadClearanceCells
			|| Encounter.RequiredRoadClearanceCells < Window.X
			|| Encounter.MainRoadDistanceCells > Window.Y
			|| !Cells.IsValidIndex(Encounter.TargetAnchorCellId)
			|| !IsStrictlySortedUnique(
				Encounter.TargetFootprintCellIds)
			|| !IsStrictlySortedUnique(
				Encounter.TargetNoRoadCellIds)
			|| static_cast<uint64>(Encounter.EncounterHash)
				!= ComputeEncounterHash(Encounter))
		{
			OutFailure = FString::Printf(
				TEXT("Encounter:%d"),
				Order);
			return false;
		}
		const int32 ProgressCM =
			FMath::RoundToInt(Contract.ProgressDistanceCM);
		const int32 ExpectedFlowQ =
			Candidate.RecomputedRoute.Metrics.RouteLengthCM > 0
			? static_cast<int32>(
				static_cast<int64>(ProgressCM)
				* FlowQuantization
				/ Candidate.RecomputedRoute.Metrics.RouteLengthCM)
			: INDEX_NONE;
		const float StartToTargetArcCM =
			SurfaceArcDistanceCM(
				Cells,
				Candidate.RecomputedRoute.OrderedRoadCellIds[0],
				Encounter.TargetAnchorCellId,
				PlanetRadiusCM);
		const int32 PreRevealRouteIndex =
			Candidate.RecomputedRoute.OrderedRoadCellIds.
				IndexOfByKey(Encounter.PreRevealCellId);
		if (Encounter.FlowQ != ExpectedFlowQ
			|| (Order == 0
				&& StartToTargetArcCM
					> Config.AttackReadableMaxDistanceCM)
			|| (Order > 0
				&& (StartToTargetArcCM
						<= Config.AttackReadableMaxDistanceCM
					|| !Candidate.RecomputedRoute.
						ProgressDistanceCM.IsValidIndex(
							PreRevealRouteIndex)
					|| ProgressCM
							- Candidate.RecomputedRoute.
								ProgressDistanceCM[
									PreRevealRouteIndex]
						< Config.PreRevealLeadCM
					|| SurfaceArcDistanceCM(
							Cells,
							Encounter.PreRevealCellId,
							Encounter.TargetAnchorCellId,
							PlanetRadiusCM)
						<= Config.AttackReadableMaxDistanceCM)))
		{
			OutFailure = FString::Printf(
				TEXT("EncounterSemantic:%d"),
				Order);
			return false;
		}
		const int32 PriorProgressCM = PreviousProgressCM;
		if (PriorProgressCM != INDEX_NONE)
		{
			const int32 Gap = ProgressCM - PriorProgressCM;
			if (Gap < Config.MinAdjacentEncounterProgressCM
				|| Gap > Config.MaxAdjacentEncounterProgressCM)
			{
				OutFailure = FString::Printf(
					TEXT("EncounterSpacing:%d"),
					Order);
				return false;
			}
		}
		if (PreviousDifficulty != INDEX_NONE
			&& Contract.DifficultyBand < PreviousDifficulty)
		{
			OutFailure = TEXT("DifficultyOrder");
			return false;
		}
		if (PreviousDifficulty != INDEX_NONE
			&& Contract.DifficultyBand > PreviousDifficulty)
		{
			++StrictDifficultyRises;
		}
		PreviousProgressCM = ProgressCM;
		PreviousDifficulty = Contract.DifficultyBand;
		VisualSignatures.Add(Encounter.VisualSignature);
		EncounterBiomes.Add(static_cast<uint8>(
			Config.EncounterBiomeArchetypes[Order]));
		for (const int32 CellId : Encounter.TargetNoRoadCellIds)
		{
			if (SeenTargetConstruction.Contains(CellId)
				|| Candidate.RecomputedRoute.OrderedRoadCellIds
					.Contains(CellId)
				|| Candidate.Cells[CellId].bWater)
			{
				OutFailure = FString::Printf(
					TEXT("TargetOverlap:%d:%d"),
					Order,
					CellId);
				return false;
			}
			SeenTargetConstruction.Add(CellId);
		}
		const int32 PocketIds[] = {
			Contract.RoadArrivalPocketId,
			Contract.ScoutRevealPocketId,
			Contract.SlingshotPocketId,
			Contract.TargetEnvelopePocketId,
			Contract.TargetAnchorPocketId,
			Contract.RewardPocketId,
			Contract.ExitPocketId
		};
		TSet<int32> AnchorIds;
		for (int32 RoleIndex = 0; RoleIndex < 7; ++RoleIndex)
		{
			const EABTSM3PocketRole Role =
				static_cast<EABTSM3PocketRole>(RoleIndex);
			const int32 Id = PocketIds[RoleIndex];
			const FABTSM3PocketContract* Pocket =
				FindPocket(Candidate.Pockets, Id);
			if (Pocket == nullptr
				|| SeenPocketIds.Contains(Id)
				|| Pocket->EncounterId != Contract.EncounterId
				|| Pocket->RouteBeatId != Contract.RouteBeatId
				|| Pocket->Role != Role
				|| Pocket->Resolution
					!= EABTSM3SchemaResolution::Finalized
				|| !Cells.IsValidIndex(Pocket->AnchorCellId)
				|| !IsStrictlySortedUnique(Pocket->CellIds)
				|| !Pocket->CellIds.Contains(
					Pocket->AnchorCellId)
				|| (Candidate.Cells[
						Pocket->AnchorCellId].
							ActiveRoleMask
					& PocketRoleMask(Role)) == 0
				|| (Role
						!= EABTSM3PocketRole::TargetAnchor
					&& AnchorIds.Contains(
						Pocket->AnchorCellId)))
			{
				OutFailure = FString::Printf(
					TEXT("Pocket:%d:%d"),
					Order,
					RoleIndex);
				return false;
			}
			SeenPocketIds.Add(Id);
			if (Role != EABTSM3PocketRole::TargetAnchor)
			{
				AnchorIds.Add(Pocket->AnchorCellId);
			}
		}
		const FABTSM3PocketContract* TargetEnvelope =
			FindPocket(
				Candidate.Pockets,
				Contract.TargetEnvelopePocketId);
		const FABTSM3PocketContract* TargetAnchor =
			FindPocket(
				Candidate.Pockets,
				Contract.TargetAnchorPocketId);
		const FABTSM3PocketContract* RoadArrival =
			FindPocket(
				Candidate.Pockets,
				Contract.RoadArrivalPocketId);
		const FABTSM3PocketContract* Reveal =
			FindPocket(
				Candidate.Pockets,
				Contract.ScoutRevealPocketId);
		const FABTSM3PocketContract* Slingshot =
			FindPocket(
				Candidate.Pockets,
				Contract.SlingshotPocketId);
		const FABTSM3PocketContract* Reward =
			FindPocket(
				Candidate.Pockets,
				Contract.RewardPocketId);
		const FABTSM3PocketContract* Exit =
			FindPocket(
				Candidate.Pockets,
				Contract.ExitPocketId);
		const int32 DesiredProgressCM = static_cast<int32>(
			static_cast<int64>(
				Candidate.RecomputedRoute.Metrics.RouteLengthCM)
			* Config.EncounterFlowQ[Order]
			/ FlowQuantization);
		TArray<int32> ExpectedSidePath;
		int32 ExpectedArrivalRouteIndex = INDEX_NONE;
		if (!BuildFinalPathToPlannedRoute(
				Config,
				Cells,
				Candidate,
				Encounter,
				ReservedCells,
				RouteIndexByCell,
				DesiredProgressCM,
				PriorProgressCM,
				ExpectedSidePath,
				ExpectedArrivalRouteIndex))
		{
			OutFailure = FString::Printf(
				TEXT("FinalExpectedSidePath:%d"),
				Order);
			return false;
		}
		const int32 ArrivalRouteIndex = RoadArrival != nullptr
			? Candidate.RecomputedRoute.OrderedRoadCellIds.
				IndexOfByKey(RoadArrival->AnchorCellId)
			: INDEX_NONE;
		const int32 ExitRouteIndex = Exit != nullptr
			? Candidate.RecomputedRoute.OrderedRoadCellIds.
				IndexOfByKey(Exit->AnchorCellId)
			: INDEX_NONE;
		if (TargetEnvelope == nullptr
			|| TargetAnchor == nullptr
			|| RoadArrival == nullptr
			|| Reveal == nullptr
			|| Slingshot == nullptr
			|| Reward == nullptr
			|| Exit == nullptr
			|| ExpectedSidePath.Num() < 4
			|| TargetAnchor->CellIds.Num() != 1
			|| TargetAnchor->AnchorCellId
				!= Encounter.TargetAnchorCellId
			|| !TargetEnvelope->CellIds.Contains(
				TargetAnchor->AnchorCellId)
			|| ArrivalRouteIndex == INDEX_NONE
			|| ArrivalRouteIndex != ExpectedArrivalRouteIndex
			|| ExitRouteIndex <= ArrivalRouteIndex
			|| Exit->AnchorCellId
				== RoadArrival->AnchorCellId
			|| RoadArrival->AnchorCellId
				!= ExpectedSidePath.Last()
			|| Reveal->AnchorCellId
				!= ExpectedSidePath[
					ExpectedSidePath.Num() - 2]
			|| Slingshot->AnchorCellId
				!= ExpectedSidePath[
					ExpectedSidePath.Num() - 3])
		{
			OutFailure = FString::Printf(
				TEXT("TargetAnchorEnvelope:%d"),
				Order);
			return false;
		}
		int32 ExpectedRewardCellId = INDEX_NONE;
		for (const int32 NeighborId :
			Cells[Encounter.TargetAnchorCellId].NeighborCellIds)
		{
			if (NeighborId != Slingshot->AnchorCellId
				&& NeighborId != Reveal->AnchorCellId
				&& NeighborId != RoadArrival->AnchorCellId
				&& (ExpectedRewardCellId == INDEX_NONE
					|| NeighborId < ExpectedRewardCellId))
			{
				ExpectedRewardCellId = NeighborId;
			}
		}
		if (Reward->AnchorCellId != ExpectedRewardCellId)
		{
			OutFailure = FString::Printf(
				TEXT("FinalRewardAnchor:%d"),
				Order);
			return false;
		}
		const FABTSM3PocketContract* RingPockets[] = {
			RoadArrival,
			Reveal,
			Slingshot,
			Reward,
			Exit
		};
		for (const FABTSM3PocketContract* Pocket :
			RingPockets)
		{
			TArray<int32> ExpectedPocketCells;
			ExpandRing(
				Cells,
				Pocket->AnchorCellId,
				Config.PocketRadiusCells,
				ExpectedPocketCells);
			if (Pocket->CellIds != ExpectedPocketCells)
			{
				OutFailure = FString::Printf(
					TEXT("FinalPocketRing:%d:%d"),
					Order,
					Pocket->PocketId);
				return false;
			}
		}
		for (const int32 AttackCellId : ExpectedSidePath)
		{
			ExpectedAttackCorridorCells.Add(AttackCellId);
			if (!Candidate.Cells[AttackCellId].
					bAttackCorridor)
			{
				OutFailure = FString::Printf(
					TEXT("FinalAttackPath:%d:%d"),
					Order,
					AttackCellId);
				return false;
			}
		}
		const FABTSM3PlayableEnvelope& Envelope =
			Candidate.PlayableEnvelopes[Order];
		const int32 ExpectedMinFlowQ = Order == 0
			? 0
			: (Config.EncounterFlowQ[Order - 1]
				+ Config.EncounterFlowQ[Order]) / 2;
		const int32 ExpectedMaxFlowQ =
			Order + 1 == Config.DestructibleEncounterCount
			? FlowQuantization
			: (Config.EncounterFlowQ[Order]
				+ Config.EncounterFlowQ[Order + 1]) / 2;
		const int32 ExpectedMinProgressCM =
			static_cast<int32>(
				static_cast<int64>(
					Candidate.RecomputedRoute.Metrics.RouteLengthCM)
				* ExpectedMinFlowQ
				/ FlowQuantization);
		const int32 ExpectedMaxProgressCM =
			static_cast<int32>(
				static_cast<int64>(
					Candidate.RecomputedRoute.Metrics.RouteLengthCM)
				* ExpectedMaxFlowQ
				/ FlowQuantization);
		if (Envelope.EnvelopeId != EnvelopeId(Order)
			|| Envelope.EncounterId != Contract.EncounterId
			|| Envelope.RouteBeatId != Contract.RouteBeatId
			|| FMath::RoundToInt(
					Envelope.MinProgressDistanceCM)
				!= ExpectedMinProgressCM
			|| FMath::RoundToInt(
					Envelope.MaxProgressDistanceCM)
				!= ExpectedMaxProgressCM
			|| Envelope.Resolution
				!= EABTSM3SchemaResolution::Finalized
			|| Envelope.Cells.IsEmpty())
		{
			OutFailure = FString::Printf(
				TEXT("Envelope:%d"),
				Order);
			return false;
		}
		int32 PreviousCellId = INDEX_NONE;
		for (const FABTSM3PlayableCellRole& Role : Envelope.Cells)
		{
			if (!Cells.IsValidIndex(Role.CellId)
				|| !PreRoadReservedPlayableCells.Contains(
					Role.CellId)
				|| (PreviousCellId != INDEX_NONE
					&& Role.CellId <= PreviousCellId)
				|| Role.ActiveRoleMask
					!= Candidate.Cells[
						Role.CellId].ActiveRoleMask
				|| Role.BiomeDistrictId
					!= Candidate.Cells[
						Role.CellId].BiomeDistrictId)
			{
				OutFailure = FString::Printf(
					TEXT("EnvelopeCell:%d"),
					Order);
				return false;
			}
			PreviousCellId = Role.CellId;
		}
		for (const int32 Id : PocketIds)
		{
			const FABTSM3PocketContract* Pocket =
				FindPocket(Candidate.Pockets, Id);
			if (Envelope.Cells.IndexOfByPredicate(
					[Pocket](
						const FABTSM3PlayableCellRole& CellRole)
					{
						return CellRole.CellId
							== Pocket->AnchorCellId;
					})
				== INDEX_NONE)
			{
				OutFailure = FString::Printf(
					TEXT("PocketOutsideEnvelope:%d:%d"),
					Order,
					Id);
				return false;
			}
		}
	}
	for (const FABTSM3MonthlySpatialCell& Cell : Candidate.Cells)
	{
		if (Cell.bAttackCorridor
			!= ExpectedAttackCorridorCells.Contains(Cell.CellId))
		{
			OutFailure = FString::Printf(
				TEXT("AttackCorridorExact:%d"),
				Cell.CellId);
			return false;
		}
	}
	for (const int32 CellId :
		Candidate.RecomputedRoute.OrderedRoadCellIds)
	{
		if ((Candidate.Cells[CellId].ActiveRoleMask
				& RoleMask(EABTSM3ActiveRole::Route)) == 0
			|| Candidate.Cells[CellId].PrimaryEnvelopeId
				== INDEX_NONE)
		{
			OutFailure = FString::Printf(
				TEXT("FinalRouteRole:%d"),
				CellId);
			return false;
		}
	}
	if (StrictDifficultyRises < 3
		|| VisualSignatures.Num()
			!= Config.DestructibleEncounterCount
		|| EncounterBiomes.Num()
			< Config.MinEncounterBiomeArchetypes)
	{
		OutFailure = TEXT("EncounterDiversity");
		return false;
	}

	TArray<int32> BiomeAssignmentCount;
	BiomeAssignmentCount.Init(0, Cells.Num());
	for (int32 Order = 0;
		Order < Candidate.BiomeDistricts.Num();
		++Order)
	{
		const FABTSM3BiomeDistrict& District =
			Candidate.BiomeDistricts[Order];
		const bool bExpectedBackground =
			Order == BackgroundBiomeOrder;
		const EABTSM3BiomeArchetype ExpectedArchetype =
			bExpectedBackground
			? EABTSM3BiomeArchetype::Background
			: Config.EncounterBiomeArchetypes[Order];
		if (District.BiomeDistrictId != BiomeId(Order)
			|| District.Archetype != ExpectedArchetype
			|| District.ObservedTerrainType
				!= TerrainForBiome(ExpectedArchetype)
			|| District.bBackground != bExpectedBackground
			|| District.Resolution
				!= EABTSM3SchemaResolution::Finalized
			|| District.CellIds.IsEmpty()
			|| !IsStrictlySortedUnique(District.CellIds))
		{
			OutFailure = FString::Printf(
				TEXT("BiomeDistrict:%d"),
				Order);
			return false;
		}
		float ExpectedMinProgressCM = MAX_flt;
		float ExpectedMaxProgressCM = 0.0f;
		int32 ExpectedMinFlowQ = FlowQuantization;
		int32 ExpectedMaxFlowQ = 0;
		for (const int32 CellId : District.CellIds)
		{
			if (!Cells.IsValidIndex(CellId)
				|| Candidate.Cells[CellId].BiomeDistrictId
					!= District.BiomeDistrictId)
			{
				OutFailure = FString::Printf(
					TEXT("BiomeCell:%d"),
					CellId);
				return false;
			}
			++BiomeAssignmentCount[CellId];
			const int32 FlowQ = Candidate.Cells[CellId].FlowQ;
			const float FlowS =
				static_cast<float>(FlowQ) / FlowQuantization;
			const float ProgressCM =
				Candidate.RecomputedRoute.Metrics.RouteLengthCM
					* FlowS;
			ExpectedMinProgressCM = FMath::Min(
				ExpectedMinProgressCM,
				ProgressCM);
			ExpectedMaxProgressCM = FMath::Max(
				ExpectedMaxProgressCM,
				ProgressCM);
			ExpectedMinFlowQ = FMath::Min(
				ExpectedMinFlowQ,
				FlowQ);
			ExpectedMaxFlowQ = FMath::Max(
				ExpectedMaxFlowQ,
				FlowQ);
		}
		if (FMath::RoundToInt(District.MinProgressDistanceCM)
				!= FMath::RoundToInt(ExpectedMinProgressCM)
			|| FMath::RoundToInt(District.MaxProgressDistanceCM)
				!= FMath::RoundToInt(ExpectedMaxProgressCM)
			|| FMath::RoundToInt(
					District.MinFlowS * FlowQuantization)
				!= ExpectedMinFlowQ
			|| FMath::RoundToInt(
					District.MaxFlowS * FlowQuantization)
				!= ExpectedMaxFlowQ)
		{
			OutFailure = FString::Printf(
				TEXT("BiomeRange:%d"),
				Order);
			return false;
		}
	}
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		if (BiomeAssignmentCount[CellId] != 1)
		{
			OutFailure = FString::Printf(
				TEXT("BiomeCoverage:%d"),
				CellId);
			return false;
		}
	}
	if (bRecomputeDerivedArtifacts)
	{
		FABTSM3MonthlySpatialCandidate ExpectedVisibility =
			Candidate;
		EABTSM3MonthlySpatialRejectReason VisibilityReason =
			EABTSM3MonthlySpatialRejectReason::None;
		FString VisibilityFailure;
		if (!BuildVisibility(
				Config,
				Cells,
				PlanetRadiusCM,
				FaultInjection,
				ExpectedVisibility,
				VisibilityReason,
				VisibilityFailure)
			|| ExpectedVisibility.OptimizedPVSRays
				!= Candidate.OptimizedPVSRays
			|| ExpectedVisibility.VisibilityEntries.Num()
				!= Candidate.VisibilityEntries.Num())
		{
			OutFailure = FString::Printf(
				TEXT("PVSRecompute:%s:%s"),
				FABTSM3MonthlyEncounterBuilder::
					GetRejectReasonName(VisibilityReason),
				*VisibilityFailure);
			return false;
		}
		for (int32 Index = 0;
			Index < Candidate.VisibilityEntries.Num();
			++Index)
		{
			if (!FABTSM3MonthlyVisibilityEntry::StaticStruct()
				->CompareScriptStruct(
					&ExpectedVisibility.VisibilityEntries[Index],
					&Candidate.VisibilityEntries[Index],
					PPF_None))
			{
				OutFailure = FString::Printf(
					TEXT("PVSRecomputeEntry:%d"),
					Index);
				return false;
			}
		}
	}
	const int32 ExpectedSpatialScore =
		1000000
		+ Candidate.RecomputedRoute.RouteScore
		+ Candidate.ActiveRoleCoveragePermille * 10
		- Candidate.DeepWildPermille * 20
		- Candidate.OptimizedPVSRays
		- Candidate.Backtracks * 100;
	if (Candidate.ActiveRoleCoveragePermille
			< Config.MinActiveRoleCoveragePermille
		|| Candidate.DeepWildPermille
			> Config.MaxDeepWildPermille
		|| Candidate.PlayableCellCount <= 0
		|| Candidate.ActiveRoleCellCount
				+ Candidate.ApprovedTransitionCellCount
				+ Candidate.DeepWildCellCount
			!= Candidate.PlayableCellCount
		|| Candidate.SpatialScore != ExpectedSpatialScore
		|| !ValidatePVSContract(
			Config,
			Candidate,
			OutFailure)
		|| static_cast<uint64>(Candidate.SpatialCandidateHash)
			!= FABTSM3MonthlyEncounterBuilder::
				ComputeCandidateHash(Candidate))
	{
		if (OutFailure.IsEmpty())
		{
			OutFailure = TEXT("CandidateSummaryOrHash");
		}
		return false;
	}
	return true;
}
}

FABTSM3MonthlyEncounterSpatialConfig::
	FABTSM3MonthlyEncounterSpatialConfig()
{
	EncounterFlowQ = {
		70000,
		210000,
		350000,
		490000,
		630000,
		770000
	};
	TargetRoadDistanceWindowsCells = {
		FIntPoint(3, 4),
		FIntPoint(3, 5),
		FIntPoint(4, 6),
		FIntPoint(4, 7),
		FIntPoint(5, 7),
		FIntPoint(6, 9)
	};
	DifficultyBands = { 0, 1, 1, 2, 2, 3 };
	RevealPolicies = {
		EABTSM3MonthlyRevealPolicy::DirectVisual,
		EABTSM3MonthlyRevealPolicy::ScoutRequired,
		EABTSM3MonthlyRevealPolicy::DirectVisual,
		EABTSM3MonthlyRevealPolicy::ScoutRequired,
		EABTSM3MonthlyRevealPolicy::ScoutRequired,
		EABTSM3MonthlyRevealPolicy::DirectVisual
	};
	EncounterBiomeArchetypes = {
		EABTSM3BiomeArchetype::Plain,
		EABTSM3BiomeArchetype::Forest,
		EABTSM3BiomeArchetype::Highland,
		EABTSM3BiomeArchetype::Mountain,
		EABTSM3BiomeArchetype::Forest,
		EABTSM3BiomeArchetype::Highland
	};
}

TConstArrayView<FABTSM3MonthlyProfileDescriptorFixture>
FABTSM3MonthlyEncounterBuilder::GetFixtureProfileCatalog()
{
	return MakeArrayView(
		ABTSM3R3EncounterPrivate::
			GetFixtureProfileCatalogStorage());
}

uint64 FABTSM3MonthlyEncounterBuilder::
	ComputeFixtureProfileCatalogHash()
{
	return ABTSM3R3EncounterPrivate::ComputeCatalogHash();
}

uint64 FABTSM3MonthlyEncounterBuilder::ComputeEncounterHash(
	const FABTSM3MonthlySpatialEncounter& Encounter)
{
	return ABTSM3R3EncounterPrivate::ComputeEncounterHash(
		Encounter);
}

uint64 FABTSM3MonthlyEncounterBuilder::ComputeFaultInjectionHash(
	const FABTSM3MonthlySpatialFaultInjection& FaultInjection)
{
	ABTSM3R3EncounterPrivate::FCanonicalHash64 Hash;
	Hash.AddInt32(3);
	Hash.AddBool(FaultInjection.bBlockEverySourceRoadCell);
	Hash.AddBool(FaultInjection.bInvalidateStartObserver);
	Hash.AddInt32(FaultInjection.ForcedReservationFailures);
	Hash.AddInt32(FaultInjection.RejectedSourceCandidateId);
	Hash.AddInt32(
		FaultInjection.BlockedSourceRoadOrderIndex);
	return Hash.Get();
}

uint64 FABTSM3MonthlyEncounterBuilder::ComputeConfigHash(
	const FABTSM3MonthlyEncounterSpatialConfig& Config,
	const FABTSM3MonthlyRouteConfig& RouteConfig,
	const float PlanetRadiusCM,
	const uint64 TopologyHash)
{
	using namespace ABTSM3R3EncounterPrivate;
	FCanonicalHash64 Hash;
	Hash.AddInt32(SpatialSchemaVersion);
	Hash.AddInt32(GeneratorVersion);
	Hash.AddInt32(MonthlyLayoutPolicyVersion);
	Hash.AddUInt64(TopologyHash);
	Hash.AddUInt64(FABTSM3MonthlyRouteBuilder::ComputeConfigHash(
		RouteConfig,
		PlanetRadiusCM,
		TopologyHash));
	Hash.AddInt32(FMath::RoundToInt(PlanetRadiusCM));
	Hash.AddBool(Config.bBuildSpatialObservation);
	Hash.AddInt32(Config.DestructibleEncounterCount);
	Hash.AddIntArray(Config.EncounterFlowQ);
	Hash.AddInt32(Config.TargetRoadDistanceWindowsCells.Num());
	for (const FIntPoint Window :
		Config.TargetRoadDistanceWindowsCells)
	{
		Hash.AddInt32(Window.X);
		Hash.AddInt32(Window.Y);
	}
	Hash.AddIntArray(Config.DifficultyBands);
	Hash.AddInt32(Config.RevealPolicies.Num());
	for (const EABTSM3MonthlyRevealPolicy Policy :
		Config.RevealPolicies)
	{
		Hash.AddInt32(static_cast<int32>(Policy));
	}
	Hash.AddInt32(Config.EncounterBiomeArchetypes.Num());
	for (const EABTSM3BiomeArchetype Biome :
		Config.EncounterBiomeArchetypes)
	{
		Hash.AddInt32(static_cast<int32>(Biome));
	}
	Hash.AddInt32(Config.MinAdjacentEncounterProgressCM);
	Hash.AddInt32(Config.MaxAdjacentEncounterProgressCM);
	Hash.AddInt32(Config.MaxPlannedProgressDeviationCM);
	Hash.AddInt32(Config.TargetEnvelopeRadiusCells);
	Hash.AddInt32(Config.TargetFootprintRadiusCells);
	Hash.AddInt32(Config.TargetNoRoadRadiusCells);
	Hash.AddInt32(Config.PocketRadiusCells);
	Hash.AddInt32(Config.RoadHalfWidthCM);
	Hash.AddInt32(Config.PadRoadBlendWidthCM);
	Hash.AddInt32(Config.FootprintSafetyMarginCM);
	Hash.AddInt32(Config.PlayableRouteRadiusCells);
	Hash.AddInt32(Config.PlayablePocketRadiusCells);
	Hash.AddInt32(Config.ActivePocketRadiusCells);
	Hash.AddInt32(Config.PreRevealLeadCM);
	Hash.AddInt32(Config.ExitLeadCM);
	Hash.AddInt32(Config.MaxAnchorCandidatesPerEncounter);
	Hash.AddInt32(Config.MaxSpatialBacktracksPerCandidate);
	Hash.AddInt32(Config.BaseTerrainCost);
	Hash.AddInt32(Config.HeightCostScale);
	Hash.AddInt32(Config.SlopeCostScale);
	Hash.AddInt32(Config.ScratchHeightScaleCM);
	Hash.AddInt32(Config.ExistingRoadReuseBias);
	Hash.AddInt32(Config.BackgroundWaterPermille);
	Hash.AddInt32(Config.LegalCrossingHalfWidthCells);
	Hash.AddInt32(Config.MaxOptimizedPVSRaysPerWorld);
	Hash.AddInt32(Config.OptimizedTraceSamples);
	Hash.AddInt32(Config.ReferenceTraceSamples);
	Hash.AddInt32(Config.DefaultOrbitDistanceCM);
	Hash.AddInt32(Config.MaxOrbitDistanceCM);
	Hash.AddInt32(Config.CameraElevationDegrees);
	Hash.AddInt32(Config.ObserverCharacterCenterHeightCM);
	Hash.AddInt32(Config.ObserverLookAtHeightCM);
	Hash.AddInt32(Config.TargetCenterHeightCM);
	Hash.AddInt32(Config.VisibilityOcclusionEpsilonCM);
	Hash.AddInt32(Config.AttackReadableMaxDistanceCM);
	Hash.AddInt32(Config.LandmarkMaxDistanceCM);
	Hash.AddInt32(Config.ScoutDetectionRadiusCM);
	Hash.AddInt32(Config.MinActiveRoleCoveragePermille);
	Hash.AddInt32(Config.MaxDeepWildPermille);
	Hash.AddInt32(Config.MinEncounterBiomeArchetypes);
	Hash.AddInt32(Config.ReservationPlannerVersion);
	Hash.AddInt32(Config.TerrainScratchVersion);
	Hash.AddInt32(Config.HydrologyScratchVersion);
	Hash.AddInt32(Config.BiomePlannerVersion);
	Hash.AddInt32(Config.CameraSampleSetVersion);
	Hash.AddInt32(Config.SpatialScoreVersion);
	Hash.AddUInt64(ComputeFixtureProfileCatalogHash());
	return Hash.Get();
}

uint64 FABTSM3MonthlyEncounterBuilder::ComputeCandidateHash(
	const FABTSM3MonthlySpatialCandidate& Candidate)
{
	using namespace ABTSM3R3EncounterPrivate;
	FCanonicalHash64 Hash;
	Hash.AddInt32(3);
	Hash.AddInt32(Candidate.SourceRouteCandidateId);
	Hash.AddInt64(Candidate.SourceRouteCandidateHash);
	Hash.AddInt64(Candidate.ReservationHash);
	Hash.AddIntArray(
		Candidate.PreRoadReservedPlayableCellIds);
	Hash.AddInt64(Candidate.RoadContextHash);
	Hash.AddInt64(Candidate.RecomputedRoute.CandidateHash);
	Hash.AddInt32(Candidate.RoadContext.Cells.Num());
	for (const FABTSM3MonthlyRouteCellContext& Cell :
		Candidate.RoadContext.Cells)
	{
		Hash.AddInt32(Cell.TerrainCost);
		Hash.AddInt32(Cell.SlopeCost);
		Hash.AddBool(Cell.bWater);
		Hash.AddBool(Cell.bLegalWaterCrossing);
		Hash.AddBool(Cell.bSoftEncounterReserved);
		Hash.AddBool(Cell.bHardBlocked);
		Hash.AddInt32(Cell.ReuseBias);
	}
	Hash.AddInt32(Candidate.Cells.Num());
	for (const FABTSM3MonthlySpatialCell& Cell : Candidate.Cells)
	{
		Hash.AddInt32(Cell.CellId);
		Hash.AddInt32(Cell.NearestRoadOrderIndex);
		Hash.AddInt32(Cell.MainRoadDistanceCells);
		Hash.AddInt32(Cell.FlowQ);
		Hash.AddInt32(Cell.BiomeDistrictId);
		Hash.AddInt32(Cell.PrimaryEnvelopeId);
		Hash.AddInt32(Cell.ActiveRoleMask);
		Hash.AddBool(Cell.bApprovedTransition);
		Hash.AddInt32(Cell.HeightQ);
		Hash.AddBool(Cell.bWater);
		Hash.AddBool(Cell.bLegalWaterCrossing);
		Hash.AddBool(Cell.bTargetFootprint);
		Hash.AddBool(Cell.bNoRoad);
		Hash.AddBool(Cell.bAttackCorridor);
	}
	Hash.AddInt32(Candidate.Encounters.Num());
	for (const FABTSM3MonthlySpatialEncounter& Encounter :
		Candidate.Encounters)
	{
		Hash.AddInt64(Encounter.EncounterHash);
	}
	Hash.AddInt32(Candidate.Pockets.Num());
	for (const FABTSM3PocketContract& Pocket : Candidate.Pockets)
	{
		Hash.AddInt32(Pocket.PocketId);
		Hash.AddInt32(Pocket.EncounterId);
		Hash.AddInt32(Pocket.RouteBeatId);
		Hash.AddInt32(static_cast<int32>(Pocket.Role));
		Hash.AddInt32(Pocket.AnchorCellId);
		Hash.AddIntArray(Pocket.CellIds);
		Hash.AddInt32(Pocket.BiomeDistrictId);
		Hash.AddInt32(static_cast<int32>(Pocket.Resolution));
	}
	Hash.AddInt32(Candidate.BiomeDistricts.Num());
	for (const FABTSM3BiomeDistrict& District :
		Candidate.BiomeDistricts)
	{
		Hash.AddInt32(District.BiomeDistrictId);
		Hash.AddInt32(static_cast<int32>(District.Archetype));
		Hash.AddInt32(static_cast<int32>(
			District.ObservedTerrainType));
		Hash.AddIntArray(District.CellIds);
		Hash.AddInt32(FMath::RoundToInt(
			District.MinProgressDistanceCM));
		Hash.AddInt32(FMath::RoundToInt(
			District.MaxProgressDistanceCM));
		Hash.AddInt32(FMath::RoundToInt(
			District.MinFlowS * FlowQuantization));
		Hash.AddInt32(FMath::RoundToInt(
			District.MaxFlowS * FlowQuantization));
		Hash.AddBool(District.bBackground);
		Hash.AddInt32(static_cast<int32>(District.Resolution));
	}
	Hash.AddInt32(Candidate.PlayableEnvelopes.Num());
	for (const FABTSM3PlayableEnvelope& Envelope :
		Candidate.PlayableEnvelopes)
	{
		Hash.AddInt32(Envelope.EnvelopeId);
		Hash.AddInt32(Envelope.RouteBeatId);
		Hash.AddInt32(Envelope.EncounterId);
		Hash.AddInt32(FMath::RoundToInt(
			Envelope.MinProgressDistanceCM));
		Hash.AddInt32(FMath::RoundToInt(
			Envelope.MaxProgressDistanceCM));
		Hash.AddInt32(Envelope.Cells.Num());
		for (const FABTSM3PlayableCellRole& Cell :
			Envelope.Cells)
		{
			Hash.AddInt32(Cell.CellId);
			Hash.AddInt32(Cell.ActiveRoleMask);
			Hash.AddInt32(Cell.BiomeDistrictId);
		}
		Hash.AddInt32(static_cast<int32>(Envelope.Resolution));
	}
	Hash.AddInt32(Candidate.VisibilityEntries.Num());
	for (const FABTSM3MonthlyVisibilityEntry& Entry :
		Candidate.VisibilityEntries)
	{
		Hash.AddInt32(static_cast<int32>(Entry.ObserverRole));
		Hash.AddInt32(Entry.ObserverEncounterId);
		Hash.AddInt32(Entry.ObserverCellId);
		Hash.AddInt32(Entry.TargetEncounterId);
		Hash.AddInt32(static_cast<int32>(
			Entry.VisibilityClass));
		Hash.AddBool(Entry.bScoutDetectable);
		Hash.AddBool(Entry.bEvaluationValid);
		Hash.AddBool(Entry.bIdealSphereBlocked);
		Hash.AddBool(Entry.bTerrainBlocked);
		Hash.AddInt32(Entry.CameraSampleCount);
		Hash.AddInt32(
			Entry.AttackReadableCameraSampleCount);
		Hash.AddInt32(Entry.VisibleCameraSampleCount);
		Hash.AddInt32(Entry.RayCount);
	}
	Hash.AddInt32(Candidate.OptimizedPVSRays);
	Hash.AddInt32(Candidate.PlayableCellCount);
	Hash.AddInt32(Candidate.ActiveRoleCellCount);
	Hash.AddInt32(Candidate.ApprovedTransitionCellCount);
	Hash.AddInt32(Candidate.DeepWildCellCount);
	Hash.AddInt32(Candidate.ActiveRoleCoveragePermille);
	Hash.AddInt32(Candidate.DeepWildPermille);
	Hash.AddInt32(Candidate.SpatialScore);
	Hash.AddInt32(Candidate.Backtracks);
	Hash.AddBool(Candidate.bHardPass);
	Hash.AddInt32(static_cast<int32>(Candidate.RejectReason));
	return Hash.Get();
}

uint64 FABTSM3MonthlyEncounterBuilder::ComputeResultHash(
	const FABTSM3MonthlySpatialResult& Result)
{
	using namespace ABTSM3R3EncounterPrivate;
	FCanonicalHash64 Hash;
	Hash.AddInt32(Result.SchemaVersion);
	Hash.AddInt32(Result.GeneratorVersion);
	Hash.AddInt32(Result.LayoutPolicyVersion);
	Hash.AddInt32(Result.WorldSeed);
	Hash.AddInt64(Result.TopologyHash);
	Hash.AddInt64(Result.SourceRoutePoolHash);
	Hash.AddInt64(Result.SpatialConfigHash);
	Hash.AddInt64(Result.ProfileCatalogHash);
	Hash.AddInt64(Result.FaultInjectionHash);
	Hash.AddBool(Result.bSpatialResultValid);
	Hash.AddBool(Result.bMonthlyWorldAccepted);
	Hash.AddBool(Result.bUsedRouteFallback);
	Hash.AddInt32(static_cast<int32>(Result.RejectReason));
	Hash.AddInt32(Result.AttemptedRouteCandidateCount);
	Hash.AddInt32(Result.SpatialHardPassCount);
	Hash.AddInt32(Result.AttemptReports.Num());
	for (const FABTSM3MonthlySpatialAttemptReport& Report :
		Result.AttemptReports)
	{
		Hash.AddInt32(Report.SourceRouteCandidateId);
		Hash.AddInt64(Report.SourceRouteCandidateHash);
		Hash.AddInt64(Report.ReservationHash);
		Hash.AddInt64(Report.RoadContextHash);
		Hash.AddInt64(Report.RecomputedRouteCandidateHash);
		Hash.AddBool(Report.bHardPass);
		Hash.AddInt32(static_cast<int32>(Report.RejectReason));
		Hash.AddName(Report.FailureCode);
		Hash.AddInt32(Report.SpatialScore);
		Hash.AddInt32(Report.Backtracks);
	}
	Hash.AddInt32(Result.RetainedCandidates.Num());
	for (const FABTSM3MonthlySpatialCandidate& Candidate :
		Result.RetainedCandidates)
	{
		Hash.AddInt64(Candidate.SpatialCandidateHash);
	}
	return Hash.Get();
}

uint64 FABTSM3MonthlyEncounterBuilder::ComputeResultSnapshotHash(
	const FABTSM3MonthlySpatialResult& Result)
{
	using namespace ABTSM3R3EncounterPrivate;
	FCanonicalHash64 Hash;
	Hash.AddInt32(Result.WorldSeed);
	Hash.AddInt64(Result.SpatialResultHash);
	Hash.AddInt32(Result.AttemptedRouteCandidateCount);
	Hash.AddInt32(Result.SpatialHardPassCount);
	Hash.AddBool(Result.bUsedRouteFallback);
	Hash.AddInt32(Result.RetainedCandidates.Num());
	for (const FABTSM3MonthlySpatialCandidate& Candidate :
		Result.RetainedCandidates)
	{
		Hash.AddInt32(Candidate.SourceRouteCandidateId);
		Hash.AddInt64(Candidate.SourceRouteCandidateHash);
		Hash.AddInt64(Candidate.ReservationHash);
		Hash.AddInt64(Candidate.RoadContextHash);
		Hash.AddInt64(
			Candidate.RecomputedRoute.CandidateHash);
		Hash.AddInt64(Candidate.SpatialCandidateHash);
		Hash.AddInt32(
			Candidate.RecomputedRoute.Metrics.RouteLengthCM);
		Hash.AddInt32(Candidate.Encounters.Num());
		Hash.AddInt32(Candidate.Pockets.Num());
		Hash.AddInt32(Candidate.BiomeDistricts.Num());
		Hash.AddInt32(Candidate.PlayableCellCount);
		Hash.AddInt32(Candidate.ActiveRoleCoveragePermille);
		Hash.AddInt32(Candidate.DeepWildPermille);
		Hash.AddInt32(Candidate.OptimizedPVSRays);
	}
	return Hash.Get();
}

bool FABTSM3MonthlyEncounterBuilder::Build(
	const int32 WorldSeed,
	const FABTSM3MonthlyEncounterSpatialConfig& Config,
	const FABTSM3MonthlyRouteConfig& RouteConfig,
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	const FABTSM3MonthlyRoutePool& SourceRoutePool,
	const FABTSM3MonthlySpatialFaultInjection& FaultInjection,
	FABTSM3MonthlySpatialResult& OutResult,
	FString& OutFailure)
{
	using namespace ABTSM3R3EncounterPrivate;
	OutResult = FABTSM3MonthlySpatialResult();
	OutFailure.Reset();
	OutResult.WorldSeed = WorldSeed;
	OutResult.GeneratorVersion = GeneratorVersion;
	OutResult.LayoutPolicyVersion = MonthlyLayoutPolicyVersion;
	if (!ValidateConfig(Config, OutFailure)
		|| !FMath::IsFinite(PlanetRadiusCM)
		|| PlanetRadiusCM <= 0.0f
		|| FaultInjection.ForcedReservationFailures < 0
		|| FaultInjection.ForcedReservationFailures > 64
		|| FaultInjection.ForcedReservationFailures
			> Config.MaxSpatialBacktracksPerCandidate
		|| FaultInjection.RejectedSourceCandidateId < INDEX_NONE
		|| FaultInjection.BlockedSourceRoadOrderIndex
			< INDEX_NONE)
	{
		OutResult.RejectReason =
			EABTSM3MonthlySpatialRejectReason::InvalidConfig;
		return false;
	}
	const uint64 TopologyHash =
		FABTSM3MonthlyRouteBuilder::ComputeTopologyHash(Cells);
	OutResult.TopologyHash = static_cast<int64>(TopologyHash);
	OutResult.SourceRoutePoolHash =
		SourceRoutePool.RouteCandidatePoolHash;
	OutResult.SpatialConfigHash = static_cast<int64>(
		ComputeConfigHash(
			Config,
			RouteConfig,
			PlanetRadiusCM,
			TopologyHash));
	OutResult.ProfileCatalogHash = static_cast<int64>(
		ComputeFixtureProfileCatalogHash());
	OutResult.FaultInjectionHash = static_cast<int64>(
		ComputeFaultInjectionHash(FaultInjection));
	EABTSM3MonthlyRouteRejectReason RouteValidationReason =
		EABTSM3MonthlyRouteRejectReason::None;
	FString RouteFailure;
	if (!FABTSM3R2AcceptanceManifest::Validate(RouteFailure)
		|| !FABTSM3MonthlyRouteBuilder::Validate(
			RouteConfig,
			Cells,
			PlanetRadiusCM,
			FABTSM3MonthlyRoadContext(),
			SourceRoutePool,
			RouteValidationReason,
			RouteFailure)
		|| SourceRoutePool.WorldSeed != WorldSeed
		|| !SourceRoutePool.bRoutePoolValid
		|| SourceRoutePool.RetainedCandidates.IsEmpty())
	{
		OutResult.RejectReason =
			EABTSM3MonthlySpatialRejectReason::InvalidRoutePool;
		OutFailure = FString::Printf(
			TEXT("SourceRoutePool:%s"),
			*RouteFailure);
		OutResult.SpatialResultHash = static_cast<int64>(
			ComputeResultHash(OutResult));
		return false;
	}
	if (ComputeFixtureProfileCatalogHash() == 0
		|| GetFixtureProfileCatalog().Num()
			!= Config.DestructibleEncounterCount)
	{
		OutResult.RejectReason =
			EABTSM3MonthlySpatialRejectReason::
				ProfileCatalogUnavailable;
		OutFailure = TEXT("FixtureProfileCatalog");
		OutResult.SpatialResultHash = static_cast<int64>(
			ComputeResultHash(OutResult));
		return false;
	}
	if (!Config.bBuildSpatialObservation)
	{
		OutResult.bSpatialResultValid = true;
		OutResult.RejectReason =
			EABTSM3MonthlySpatialRejectReason::NotEvaluated;
		OutResult.SpatialResultHash = static_cast<int64>(
			ComputeResultHash(OutResult));
		return true;
	}

	for (const FABTSM3MonthlyRouteCandidate& SourceRoute :
		SourceRoutePool.RetainedCandidates)
	{
		FABTSM3MonthlySpatialCandidate Candidate;
		EABTSM3MonthlySpatialRejectReason CandidateReason =
			EABTSM3MonthlySpatialRejectReason::None;
		FString CandidateFailure;
		const bool bBuilt = BuildSpatialCandidate(
			WorldSeed,
			Config,
			RouteConfig,
			Cells,
			PlanetRadiusCM,
			SourceRoute,
			FaultInjection,
			static_cast<uint64>(
				OutResult.ProfileCatalogHash),
			Candidate,
			CandidateReason,
			CandidateFailure);
		++OutResult.AttemptedRouteCandidateCount;
		if (SourceRoute.Origin
			== EABTSM3MonthlyRouteOrigin::
				MonthlyRouteFallback)
		{
			OutResult.bUsedRouteFallback = true;
		}
		if (!bBuilt)
		{
			Candidate.bHardPass = false;
			Candidate.RejectReason = CandidateReason;
			Candidate.SpatialCandidateHash =
				static_cast<int64>(
					ComputeCandidateHash(Candidate));
			if (Config.bEmitSpatialLogs)
			{
				UE_LOG(LogABTSRuntime, Verbose,
					TEXT("[ABTS][M3R3][SpatialReject] Seed=%d SourceCandidate=%d SourceHash=%016llX ReservationHash=%016llX ContextHash=%016llX RecomputedHash=%016llX Reason=%s Failure=%s"),
					WorldSeed,
					SourceRoute.CandidateId,
					static_cast<unsigned long long>(
						static_cast<uint64>(
							SourceRoute.CandidateHash)),
					static_cast<unsigned long long>(
						static_cast<uint64>(
							Candidate.ReservationHash)),
					static_cast<unsigned long long>(
						static_cast<uint64>(
							Candidate.RoadContextHash)),
					static_cast<unsigned long long>(
						static_cast<uint64>(
							Candidate.RecomputedRoute
								.CandidateHash)),
					GetRejectReasonName(CandidateReason),
					*CandidateFailure);
			}
		}
		else
		{
			++OutResult.SpatialHardPassCount;
			OutResult.RetainedCandidates.Add(Candidate);
		}
		OutResult.AttemptReports.Add(
			MakeAttemptReport(
				Candidate,
				CandidateReason,
				CandidateFailure));
	}
	OutResult.RetainedCandidates.Sort(
		[](const FABTSM3MonthlySpatialCandidate& A,
			const FABTSM3MonthlySpatialCandidate& B)
		{
			return SpatialCandidateLess(A, B);
		});
	if (OutResult.RetainedCandidates.IsEmpty())
	{
		OutResult.RejectReason =
			EABTSM3MonthlySpatialRejectReason::
				AllRouteCandidatesFailed;
		OutFailure = TEXT("AllRouteCandidatesFailed");
		OutResult.SpatialResultHash = static_cast<int64>(
			ComputeResultHash(OutResult));
		return false;
	}
	int32 ReportedHardPassCount = 0;
	bool bConstructedSummaryValid =
		OutResult.AttemptedRouteCandidateCount
			== SourceRoutePool.RetainedCandidates.Num()
		&& OutResult.AttemptReports.Num()
			== OutResult.AttemptedRouteCandidateCount
		&& OutResult.SpatialHardPassCount
			== OutResult.RetainedCandidates.Num();
	for (int32 Index = 0;
		bConstructedSummaryValid
			&& Index < OutResult.AttemptReports.Num();
		++Index)
	{
		const FABTSM3MonthlySpatialAttemptReport& Report =
			OutResult.AttemptReports[Index];
		const FABTSM3MonthlyRouteCandidate& Source =
			SourceRoutePool.RetainedCandidates[Index];
		const bool bReportStateValid =
			Report.SourceRouteCandidateId == Source.CandidateId
			&& Report.SourceRouteCandidateHash
				== Source.CandidateHash
			&& Report.Backtracks >= 0
			&& Report.Backtracks
				<= Config.MaxSpatialBacktracksPerCandidate
			&& (Report.bHardPass
				? Report.RejectReason
						== EABTSM3MonthlySpatialRejectReason::None
					&& Report.FailureCode.IsNone()
				: Report.RejectReason
						!= EABTSM3MonthlySpatialRejectReason::None
					&& !Report.FailureCode.IsNone());
		if (!bReportStateValid)
		{
			bConstructedSummaryValid = false;
			break;
		}
		if (Report.bHardPass)
		{
			++ReportedHardPassCount;
			bConstructedSummaryValid =
				OutResult.RetainedCandidates.ContainsByPredicate(
					[&Report](
						const FABTSM3MonthlySpatialCandidate&
							Candidate)
					{
						return Candidate.SourceRouteCandidateId
								== Report.SourceRouteCandidateId
							&& Candidate.SourceRouteCandidateHash
								== Report.SourceRouteCandidateHash
							&& Candidate.ReservationHash
								== Report.ReservationHash
							&& Candidate.RoadContextHash
								== Report.RoadContextHash
							&& Candidate.RecomputedRoute.
									CandidateHash
								== Report.
									RecomputedRouteCandidateHash;
					});
		}
	}
	bConstructedSummaryValid &=
		ReportedHardPassCount == OutResult.SpatialHardPassCount;
	if (!bConstructedSummaryValid)
	{
		OutResult.RejectReason =
			EABTSM3MonthlySpatialRejectReason::HashMismatch;
		OutFailure = TEXT("ConstructedResultSummary");
		OutResult.SpatialResultHash = static_cast<int64>(
			ComputeResultHash(OutResult));
		return false;
	}
	OutResult.bSpatialResultValid = true;
	OutResult.bMonthlyWorldAccepted = false;
	OutResult.RejectReason =
		EABTSM3MonthlySpatialRejectReason::None;
	OutResult.SpatialResultHash = static_cast<int64>(
		ComputeResultHash(OutResult));
	for (int32 Index = 0;
		Index < OutResult.RetainedCandidates.Num();
		++Index)
	{
		if (!ValidateCandidate(
				Config,
				RouteConfig,
				Cells,
				PlanetRadiusCM,
				SourceRoutePool,
				FaultInjection,
				OutResult.RetainedCandidates[Index],
				static_cast<uint64>(
					OutResult.ProfileCatalogHash),
				false,
				OutFailure))
		{
			OutResult.bSpatialResultValid = false;
			OutResult.RejectReason =
				EABTSM3MonthlySpatialRejectReason::HashMismatch;
			OutFailure = FString::Printf(
				TEXT("ConstructedCandidate:%d:%s"),
				Index,
				*OutFailure);
			OutResult.SpatialResultHash = static_cast<int64>(
				ComputeResultHash(OutResult));
			return false;
		}
	}
	if (Config.bEmitSpatialLogs)
	{
		LogSummary(OutResult);
	}
	return true;
}

bool FABTSM3MonthlyEncounterBuilder::Validate(
	const FABTSM3MonthlyEncounterSpatialConfig& Config,
	const FABTSM3MonthlyRouteConfig& RouteConfig,
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	const FABTSM3MonthlyRoutePool& SourceRoutePool,
	const FABTSM3MonthlySpatialFaultInjection& FaultInjection,
	const FABTSM3MonthlySpatialResult& Result,
	EABTSM3MonthlySpatialRejectReason& OutReason,
	FString& OutFailure)
{
	using namespace ABTSM3R3EncounterPrivate;
	OutReason = EABTSM3MonthlySpatialRejectReason::None;
	OutFailure.Reset();
	if (!ValidateConfig(Config, OutFailure)
		|| !FMath::IsFinite(PlanetRadiusCM)
		|| PlanetRadiusCM <= 0.0f
		|| FaultInjection.ForcedReservationFailures < 0
		|| FaultInjection.ForcedReservationFailures > 64
		|| FaultInjection.ForcedReservationFailures
			> Config.MaxSpatialBacktracksPerCandidate
		|| FaultInjection.RejectedSourceCandidateId < INDEX_NONE
		|| FaultInjection.BlockedSourceRoadOrderIndex
			< INDEX_NONE)
	{
		OutReason =
			EABTSM3MonthlySpatialRejectReason::InvalidConfig;
		return false;
	}
	EABTSM3MonthlyRouteRejectReason RouteValidationReason =
		EABTSM3MonthlyRouteRejectReason::None;
	FString RouteValidationFailure;
	if (!FABTSM3R2AcceptanceManifest::Validate(
			RouteValidationFailure)
		|| !FABTSM3MonthlyRouteBuilder::Validate(
			RouteConfig,
			Cells,
			PlanetRadiusCM,
			FABTSM3MonthlyRoadContext(),
			SourceRoutePool,
			RouteValidationReason,
			RouteValidationFailure))
	{
		OutReason =
			EABTSM3MonthlySpatialRejectReason::InvalidRoutePool;
		OutFailure = FString::Printf(
			TEXT("SourceRoutePool:%s:%s"),
			FABTSM3MonthlyRouteBuilder::GetRejectReasonName(
				RouteValidationReason),
			*RouteValidationFailure);
		return false;
	}
	const uint64 TopologyHash =
		FABTSM3MonthlyRouteBuilder::ComputeTopologyHash(Cells);
	if (Result.SchemaVersion != SpatialSchemaVersion
		|| Result.GeneratorVersion != GeneratorVersion
		|| Result.LayoutPolicyVersion
			!= MonthlyLayoutPolicyVersion
		|| Result.WorldSeed != SourceRoutePool.WorldSeed
		|| Result.bMonthlyWorldAccepted
		|| static_cast<uint64>(Result.TopologyHash)
			!= TopologyHash
		|| Result.SourceRoutePoolHash
			!= SourceRoutePool.RouteCandidatePoolHash
		|| static_cast<uint64>(Result.SpatialConfigHash)
			!= ComputeConfigHash(
				Config,
				RouteConfig,
				PlanetRadiusCM,
				TopologyHash)
		|| static_cast<uint64>(Result.ProfileCatalogHash)
			!= ComputeFixtureProfileCatalogHash()
		|| static_cast<uint64>(Result.FaultInjectionHash)
			!= ComputeFaultInjectionHash(FaultInjection))
	{
		OutReason =
			EABTSM3MonthlySpatialRejectReason::HashMismatch;
		OutFailure = TEXT("ResultIdentity");
		return false;
	}
	if (!Config.bBuildSpatialObservation)
	{
		if (!Result.bSpatialResultValid
			|| Result.RejectReason
				!= EABTSM3MonthlySpatialRejectReason::
					NotEvaluated
			|| Result.AttemptedRouteCandidateCount != 0
			|| Result.SpatialHardPassCount != 0
			|| Result.bUsedRouteFallback
			|| !Result.RetainedCandidates.IsEmpty()
			|| !Result.AttemptReports.IsEmpty()
			|| static_cast<uint64>(Result.SpatialResultHash)
				!= ComputeResultHash(Result))
		{
			OutReason =
				EABTSM3MonthlySpatialRejectReason::HashMismatch;
			OutFailure = TEXT("DisabledObservation");
			return false;
		}
		return true;
	}
	if (!Result.bSpatialResultValid
		|| Result.RejectReason
			!= EABTSM3MonthlySpatialRejectReason::None
		|| Result.AttemptedRouteCandidateCount
			!= SourceRoutePool.RetainedCandidates.Num()
		|| Result.AttemptReports.Num()
			!= Result.AttemptedRouteCandidateCount
		|| Result.SpatialHardPassCount
			!= Result.RetainedCandidates.Num()
		|| Result.RetainedCandidates.IsEmpty()
		|| Result.bUsedRouteFallback
			!= SourceRoutePool.bUsedRouteFallback)
	{
		OutReason =
			EABTSM3MonthlySpatialRejectReason::InvalidRoutePool;
		OutFailure = TEXT("ResultCounts");
		return false;
	}
	int32 ReportedPasses = 0;
	for (int32 Index = 0;
		Index < Result.AttemptReports.Num();
		++Index)
	{
		const FABTSM3MonthlySpatialAttemptReport& Report =
			Result.AttemptReports[Index];
		const FABTSM3MonthlyRouteCandidate& Source =
			SourceRoutePool.RetainedCandidates[Index];
		FABTSM3MonthlySpatialCandidate ExpectedCandidate;
		EABTSM3MonthlySpatialRejectReason ExpectedReason =
			EABTSM3MonthlySpatialRejectReason::None;
		FString ExpectedFailure;
		const bool bExpectedBuilt = BuildSpatialCandidate(
			SourceRoutePool.WorldSeed,
			Config,
			RouteConfig,
			Cells,
			PlanetRadiusCM,
			Source,
			FaultInjection,
			static_cast<uint64>(Result.ProfileCatalogHash),
			ExpectedCandidate,
			ExpectedReason,
			ExpectedFailure);
		const FABTSM3MonthlySpatialAttemptReport ExpectedReport =
			MakeAttemptReport(
				ExpectedCandidate,
				bExpectedBuilt
					? EABTSM3MonthlySpatialRejectReason::None
					: ExpectedReason,
				bExpectedBuilt ? FString() : ExpectedFailure);
		if (!FABTSM3MonthlySpatialAttemptReport::StaticStruct()
			->CompareScriptStruct(
				&ExpectedReport,
				&Report,
				PPF_None))
		{
			OutReason =
				EABTSM3MonthlySpatialRejectReason::HashMismatch;
			OutFailure = FString::Printf(
				TEXT("AttemptRebuild:%d:%s:%s"),
				Index,
				GetRejectReasonName(ExpectedReason),
				*ExpectedFailure);
			return false;
		}
		if (Report.SourceRouteCandidateId != Source.CandidateId
			|| Report.SourceRouteCandidateHash
				!= Source.CandidateHash
			|| (Report.bHardPass
				&& Report.RejectReason
					!= EABTSM3MonthlySpatialRejectReason::None)
			|| (!Report.bHardPass
				&& Report.RejectReason
					== EABTSM3MonthlySpatialRejectReason::None)
			|| (Report.bHardPass && !Report.FailureCode.IsNone())
			|| (!Report.bHardPass && Report.FailureCode.IsNone())
			|| Report.Backtracks < 0
			|| Report.Backtracks
				> Config.MaxSpatialBacktracksPerCandidate)
		{
			OutReason =
				EABTSM3MonthlySpatialRejectReason::HashMismatch;
			OutFailure = FString::Printf(
				TEXT("Attempt:%d"),
				Index);
			return false;
		}
		if (Report.bHardPass)
		{
			const FABTSM3MonthlySpatialCandidate* Retained =
				Result.RetainedCandidates.FindByPredicate(
					[&Report](
						const FABTSM3MonthlySpatialCandidate&
							Candidate)
					{
						return Candidate.SourceRouteCandidateId
								== Report.SourceRouteCandidateId
							&& Candidate.SourceRouteCandidateHash
								== Report.SourceRouteCandidateHash;
					});
			if (Retained == nullptr
				|| !bExpectedBuilt
				|| !FABTSM3MonthlySpatialCandidate::StaticStruct()
					->CompareScriptStruct(
						&ExpectedCandidate,
						Retained,
						PPF_None)
				|| Report.ReservationHash
					!= Retained->ReservationHash
				|| Report.RoadContextHash
					!= Retained->RoadContextHash
				|| Report.RecomputedRouteCandidateHash
					!= Retained->RecomputedRoute.CandidateHash
				|| Report.SpatialScore
					!= Retained->SpatialScore
				|| Report.Backtracks
					!= Retained->Backtracks)
			{
				OutReason =
					EABTSM3MonthlySpatialRejectReason::HashMismatch;
				OutFailure = FString::Printf(
					TEXT("AttemptRetained:%d"),
					Index);
				return false;
			}
		}
		ReportedPasses += Report.bHardPass ? 1 : 0;
	}
	if (ReportedPasses != Result.SpatialHardPassCount)
	{
		OutReason =
			EABTSM3MonthlySpatialRejectReason::HashMismatch;
		OutFailure = TEXT("AttemptPassCount");
		return false;
	}
	for (int32 Index = 0;
		Index < Result.RetainedCandidates.Num();
		++Index)
	{
		if (!ValidateCandidate(
				Config,
				RouteConfig,
				Cells,
				PlanetRadiusCM,
				SourceRoutePool,
				FaultInjection,
				Result.RetainedCandidates[Index],
				static_cast<uint64>(Result.ProfileCatalogHash),
				false,
				OutFailure))
		{
			OutReason =
				EABTSM3MonthlySpatialRejectReason::HashMismatch;
			return false;
		}
		if (Index > 0
			&& SpatialCandidateLess(
				Result.RetainedCandidates[Index],
				Result.RetainedCandidates[Index - 1]))
		{
			OutReason =
				EABTSM3MonthlySpatialRejectReason::HashMismatch;
			OutFailure = TEXT("CandidateOrder");
			return false;
		}
	}
	if (static_cast<uint64>(Result.SpatialResultHash)
		!= ComputeResultHash(Result))
	{
		OutReason =
			EABTSM3MonthlySpatialRejectReason::HashMismatch;
		OutFailure = TEXT("ResultHash");
		return false;
	}
	return true;
}

bool FABTSM3MonthlyEncounterBuilder::
	ValidateVisibilityAgainstReference(
		const FABTSM3MonthlyEncounterSpatialConfig& Config,
		const TArray<FABTSM2Cell>& Cells,
		const float PlanetRadiusCM,
		const FABTSM3MonthlySpatialCandidate& Candidate,
		int32& OutMismatchCount,
		uint64& OutReferenceHash,
		FString& OutFailure)
{
	using namespace ABTSM3R3EncounterPrivate;
	OutMismatchCount = 0;
	OutReferenceHash = 0;
	OutFailure.Reset();
	if (Candidate.VisibilityEntries.Num()
			!= RequiredVisibilityEntryCount
		|| Candidate.Encounters.Num() != 6)
	{
		OutFailure = TEXT("ReferenceInput");
		return false;
	}
	FCanonicalHash64 Hash;
	for (const FABTSM3MonthlyVisibilityEntry& Optimized :
		Candidate.VisibilityEntries)
	{
		const FABTSM3MonthlySpatialEncounter* Target =
			Candidate.Encounters.FindByPredicate(
				[&Optimized](
					const FABTSM3MonthlySpatialEncounter& Encounter)
				{
					return Encounter.Contract.EncounterId
						== Optimized.TargetEncounterId;
				});
		if (Target == nullptr)
		{
			OutFailure = TEXT("ReferenceTarget");
			return false;
		}
		const FABTSM3MonthlyVisibilityEntry Reference =
			EvaluateVisibilityReferenceContinuous(
				Config,
				Cells,
				PlanetRadiusCM,
				Candidate,
				Optimized.ObserverRole,
				Optimized.ObserverEncounterId,
				Optimized.ObserverCellId,
				*Target);
		if (!Reference.bEvaluationValid
			|| Reference.VisibilityClass
				!= Optimized.VisibilityClass
			|| Reference.bScoutDetectable
				!= Optimized.bScoutDetectable
			|| Reference.bIdealSphereBlocked
				!= Optimized.bIdealSphereBlocked
			|| Reference.bTerrainBlocked
				!= Optimized.bTerrainBlocked
			|| Reference.CameraSampleCount
				!= Optimized.CameraSampleCount
			|| Reference.AttackReadableCameraSampleCount
				!= Optimized.AttackReadableCameraSampleCount
			|| Reference.VisibleCameraSampleCount
				!= Optimized.VisibleCameraSampleCount
			|| Reference.RayCount != Optimized.RayCount)
		{
			++OutMismatchCount;
		}
		Hash.AddInt32(static_cast<int32>(
			Reference.ObserverRole));
		Hash.AddInt32(Reference.ObserverEncounterId);
		Hash.AddInt32(Reference.ObserverCellId);
		Hash.AddInt32(Reference.TargetEncounterId);
		Hash.AddInt32(static_cast<int32>(
			Reference.VisibilityClass));
		Hash.AddBool(Reference.bScoutDetectable);
		Hash.AddBool(Reference.bEvaluationValid);
		Hash.AddBool(Reference.bIdealSphereBlocked);
		Hash.AddBool(Reference.bTerrainBlocked);
		Hash.AddInt32(Reference.CameraSampleCount);
		Hash.AddInt32(
			Reference.AttackReadableCameraSampleCount);
		Hash.AddInt32(Reference.VisibleCameraSampleCount);
		Hash.AddInt32(Reference.RayCount);
	}
	OutReferenceHash = Hash.Get();
	if (OutMismatchCount > 0)
	{
		OutFailure = FString::Printf(
			TEXT("ReferenceMismatch:%d"),
			OutMismatchCount);
		return false;
	}
	return true;
}

void FABTSM3MonthlyEncounterBuilder::BuildDebugData(
	const FABTSM3MonthlySpatialResult& Result,
	FABTSM3MonthlySpatialDebugData& OutDebugData)
{
	OutDebugData = FABTSM3MonthlySpatialDebugData();
	if (Result.RetainedCandidates.IsEmpty())
	{
		return;
	}
	const FABTSM3MonthlySpatialCandidate& Candidate =
		Result.RetainedCandidates[0];
	for (const FABTSM3MonthlySpatialEncounter& Encounter :
		Candidate.Encounters)
	{
		const FABTSM3PocketContract* Arrival =
			ABTSM3R3EncounterPrivate::FindPocket(
			Candidate.Pockets,
			Encounter.Contract.RoadArrivalPocketId);
		const FABTSM3PocketContract* Reveal =
			ABTSM3R3EncounterPrivate::FindPocket(
			Candidate.Pockets,
			Encounter.Contract.ScoutRevealPocketId);
		const FABTSM3PocketContract* Slingshot =
			ABTSM3R3EncounterPrivate::FindPocket(
			Candidate.Pockets,
			Encounter.Contract.SlingshotPocketId);
		if (Arrival != nullptr)
		{
			OutDebugData.RoadArrivalCellIds.Add(
				Arrival->AnchorCellId);
		}
		if (Reveal != nullptr)
		{
			OutDebugData.RevealCellIds.Add(
				Reveal->AnchorCellId);
		}
		if (Slingshot != nullptr)
		{
			OutDebugData.SlingshotCellIds.Add(
				Slingshot->AnchorCellId);
		}
		OutDebugData.TargetAnchorCellIds.Add(
			Encounter.TargetAnchorCellId);
		OutDebugData.NoRoadCellIds.Append(
			Encounter.TargetNoRoadCellIds);
	}
	TSet<int32> PlayableCells;
	for (const FABTSM3PlayableEnvelope& Envelope :
		Candidate.PlayableEnvelopes)
	{
		for (const FABTSM3PlayableCellRole& Cell : Envelope.Cells)
		{
			PlayableCells.Add(Cell.CellId);
		}
	}
	OutDebugData.PlayableEnvelopeCellIds.Reserve(
		PlayableCells.Num());
	for (const int32 CellId : PlayableCells)
	{
		OutDebugData.PlayableEnvelopeCellIds.Add(CellId);
	}
	OutDebugData.NoRoadCellIds.Sort();
	OutDebugData.NoRoadCellIds.SetNum(Algo::Unique(
		OutDebugData.NoRoadCellIds));
	OutDebugData.PlayableEnvelopeCellIds.Sort();
}

void FABTSM3MonthlyEncounterBuilder::LogSummary(
	const FABTSM3MonthlySpatialResult& Result)
{
	if (Result.RetainedCandidates.IsEmpty())
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][PCG][EncounterSpatial] Stage=M3R3 Seed=%d Valid=%d MonthlyAccepted=0 Attempts=%d HardPass=%d Retained=0 UsedRouteFallback=%d Reason=%s ResultHash=%016llX SnapshotHash=%016llX"),
			Result.WorldSeed,
			Result.bSpatialResultValid ? 1 : 0,
			Result.AttemptedRouteCandidateCount,
			Result.SpatialHardPassCount,
			Result.bUsedRouteFallback ? 1 : 0,
			GetRejectReasonName(Result.RejectReason),
			static_cast<unsigned long long>(
				static_cast<uint64>(Result.SpatialResultHash)),
			static_cast<unsigned long long>(
				ComputeResultSnapshotHash(Result)));
		return;
	}
	const FABTSM3MonthlySpatialCandidate& Best =
		Result.RetainedCandidates[0];
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][PCG][EncounterSpatial] Stage=M3R3 Seed=%d Valid=%d MonthlyAccepted=0 Attempts=%d HardPass=%d Retained=%d SourceCandidate=%d SourceHash=%016llX ReservationHash=%016llX ContextHash=%016llX RecomputedHash=%016llX CandidateHash=%016llX RouteCM=%d Encounters=%d Pockets=%d Biomes=%d Playable=%d ActiveCoveragePermille=%d DeepWildPermille=%d PVSRays=%d ResultHash=%016llX SnapshotHash=%016llX"),
		Result.WorldSeed,
		Result.bSpatialResultValid ? 1 : 0,
		Result.AttemptedRouteCandidateCount,
		Result.SpatialHardPassCount,
		Result.RetainedCandidates.Num(),
		Best.SourceRouteCandidateId,
		static_cast<unsigned long long>(
			static_cast<uint64>(Best.SourceRouteCandidateHash)),
		static_cast<unsigned long long>(
			static_cast<uint64>(Best.ReservationHash)),
		static_cast<unsigned long long>(
			static_cast<uint64>(Best.RoadContextHash)),
		static_cast<unsigned long long>(
			static_cast<uint64>(
				Best.RecomputedRoute.CandidateHash)),
		static_cast<unsigned long long>(
			static_cast<uint64>(Best.SpatialCandidateHash)),
		Best.RecomputedRoute.Metrics.RouteLengthCM,
		Best.Encounters.Num(),
		Best.Pockets.Num(),
		Best.BiomeDistricts.Num(),
		Best.PlayableCellCount,
		Best.ActiveRoleCoveragePermille,
		Best.DeepWildPermille,
		Best.OptimizedPVSRays,
		static_cast<unsigned long long>(
			static_cast<uint64>(Result.SpatialResultHash)),
		static_cast<unsigned long long>(
			ComputeResultSnapshotHash(Result)));
}

const TCHAR* FABTSM3MonthlyEncounterBuilder::GetRejectReasonName(
	const EABTSM3MonthlySpatialRejectReason Reason)
{
	switch (Reason)
	{
	case EABTSM3MonthlySpatialRejectReason::None:
		return TEXT("None");
	case EABTSM3MonthlySpatialRejectReason::NotEvaluated:
		return TEXT("NotEvaluated");
	case EABTSM3MonthlySpatialRejectReason::InvalidConfig:
		return TEXT("InvalidConfig");
	case EABTSM3MonthlySpatialRejectReason::InvalidTopology:
		return TEXT("InvalidTopology");
	case EABTSM3MonthlySpatialRejectReason::InvalidRoutePool:
		return TEXT("InvalidRoutePool");
	case EABTSM3MonthlySpatialRejectReason::ProfileCatalogUnavailable:
		return TEXT("ProfileCatalogUnavailable");
	case EABTSM3MonthlySpatialRejectReason::ReservationFailed:
		return TEXT("ReservationFailed");
	case EABTSM3MonthlySpatialRejectReason::RoadRebuildFailed:
		return TEXT("RoadRebuildFailed");
	case EABTSM3MonthlySpatialRejectReason::RouteMetricsFailed:
		return TEXT("RouteMetricsFailed");
	case EABTSM3MonthlySpatialRejectReason::EncounterSpacingFailed:
		return TEXT("EncounterSpacingFailed");
	case EABTSM3MonthlySpatialRejectReason::PocketIdentityInvalid:
		return TEXT("PocketIdentityInvalid");
	case EABTSM3MonthlySpatialRejectReason::TargetOverlap:
		return TEXT("TargetOverlap");
	case EABTSM3MonthlySpatialRejectReason::BiomeCoverageFailed:
		return TEXT("BiomeCoverageFailed");
	case EABTSM3MonthlySpatialRejectReason::PVSInvalid:
		return TEXT("PVSInvalid");
	case EABTSM3MonthlySpatialRejectReason::PVSContractFailed:
		return TEXT("PVSContractFailed");
	case EABTSM3MonthlySpatialRejectReason::RayBudgetExceeded:
		return TEXT("RayBudgetExceeded");
	case EABTSM3MonthlySpatialRejectReason::SearchBudgetExceeded:
		return TEXT("SearchBudgetExceeded");
	case EABTSM3MonthlySpatialRejectReason::AllRouteCandidatesFailed:
		return TEXT("AllRouteCandidatesFailed");
	case EABTSM3MonthlySpatialRejectReason::HashMismatch:
		return TEXT("HashMismatch");
	default:
		return TEXT("Invalid");
	}
}
