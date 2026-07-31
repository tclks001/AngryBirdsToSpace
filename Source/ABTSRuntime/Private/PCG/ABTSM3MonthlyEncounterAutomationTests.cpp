// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ABTSRuntime.h"
#include "Algo/Sort.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "PCG/ABTSM3MonthlyEncounter.h"
#include "PCG/ABTSM3R3AcceptanceManifest.h"
#include "Planet/ABTSM2Planet.h"

namespace ABTSM3R3EncounterSpatialTests
{
constexpr float ReferencePlanetRadiusCM = 10000.0f;
constexpr int32 DisplaySeed = 312503;
constexpr int32 ExpectedEncounterCount = 6;
constexpr int32 ExpectedPocketsPerEncounter = 7;
constexpr int32 ExpectedBiomeDistrictCount = 7;
constexpr int32 SweepSeedCount = 100;
constexpr double P95BudgetMS = 750.0;
constexpr double MaxBudgetMS = 2000.0;

TArray<FABTSM2Cell> BuildLogicalCells(const int32 Subdivision = 5)
{
	AABTSM2Planet::FUnitSphereMesh Mesh;
	AABTSM2Planet::BuildUnitIcosphere(Subdivision, Mesh);
	TArray<FABTSM2Cell> Cells;
	Cells.SetNum(Mesh.Vertices.Num());
	for (int32 CellId = 0; CellId < Mesh.Vertices.Num(); ++CellId)
	{
		Cells[CellId].UnitCenter = Mesh.Vertices[CellId];
	}
	const auto AddNeighbor = [&Cells](
		const int32 CellA,
		const int32 CellB)
	{
		Cells[CellA].NeighborCellIds.AddUnique(CellB);
		Cells[CellB].NeighborCellIds.AddUnique(CellA);
	};
	for (const FIntVector& Triangle : Mesh.Triangles)
	{
		AddNeighbor(Triangle.X, Triangle.Y);
		AddNeighbor(Triangle.Y, Triangle.Z);
		AddNeighbor(Triangle.Z, Triangle.X);
	}
	for (FABTSM2Cell& Cell : Cells)
	{
		Cell.NeighborCellIds.Sort();
		Cell.bIsPentagon = Cell.NeighborCellIds.Num() == 5;
	}
	return Cells;
}

const TArray<FABTSM2Cell>& GetLogicalCells()
{
	static const TArray<FABTSM2Cell> Cells = BuildLogicalCells();
	return Cells;
}

FABTSM3MonthlyRouteConfig MakeQuietRouteConfig()
{
	FABTSM3MonthlyRouteConfig Config;
	Config.bEmitRouteLogs = false;
	return Config;
}

FABTSM3MonthlyEncounterSpatialConfig MakeQuietSpatialConfig()
{
	FABTSM3MonthlyEncounterSpatialConfig Config;
	Config.bEmitSpatialLogs = false;
	return Config;
}

bool BuildRoutePool(
	const int32 Seed,
	const FABTSM3MonthlyRouteConfig& RouteConfig,
	FABTSM3MonthlyRoutePool& OutPool,
	FString& OutFailure)
{
	return FABTSM3MonthlyRouteBuilder::Build(
		Seed,
		RouteConfig,
		GetLogicalCells(),
		ReferencePlanetRadiusCM,
		FABTSM3MonthlyRoadContext(),
		OutPool,
		OutFailure);
}

bool BuildSpatialResult(
	const int32 Seed,
	const FABTSM3MonthlyEncounterSpatialConfig& SpatialConfig,
	const FABTSM3MonthlySpatialFaultInjection& FaultInjection,
	FABTSM3MonthlyRoutePool& OutRoutePool,
	FABTSM3MonthlySpatialResult& OutSpatialResult,
	FString& OutFailure)
{
	const FABTSM3MonthlyRouteConfig RouteConfig =
		MakeQuietRouteConfig();
	if (!BuildRoutePool(
			Seed,
			RouteConfig,
			OutRoutePool,
			OutFailure))
	{
		return false;
	}
	return FABTSM3MonthlyEncounterBuilder::Build(
		Seed,
		SpatialConfig,
		RouteConfig,
		GetLogicalCells(),
		ReferencePlanetRadiusCM,
		OutRoutePool,
		FaultInjection,
		OutSpatialResult,
		OutFailure);
}

bool BuildSpatialResult(
	const int32 Seed,
	FABTSM3MonthlyRoutePool& OutRoutePool,
	FABTSM3MonthlySpatialResult& OutSpatialResult,
	FString& OutFailure)
{
	return BuildSpatialResult(
		Seed,
		MakeQuietSpatialConfig(),
		FABTSM3MonthlySpatialFaultInjection(),
		OutRoutePool,
		OutSpatialResult,
		OutFailure);
}

bool RoutePoolEqual(
	const FABTSM3MonthlyRoutePool& A,
	const FABTSM3MonthlyRoutePool& B)
{
	return FABTSM3MonthlyRoutePool::StaticStruct()
		->CompareScriptStruct(&A, &B, PPF_None);
}

bool SpatialResultEqual(
	const FABTSM3MonthlySpatialResult& A,
	const FABTSM3MonthlySpatialResult& B)
{
	return FABTSM3MonthlySpatialResult::StaticStruct()
		->CompareScriptStruct(&A, &B, PPF_None);
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

const FABTSM3MonthlyVisibilityEntry* FindVisibility(
	const FABTSM3MonthlySpatialCandidate& Candidate,
	const EABTSM3MonthlyObserverRole ObserverRole,
	const int32 ObserverEncounterId,
	const int32 TargetEncounterId)
{
	return Candidate.VisibilityEntries.FindByPredicate(
		[=](const FABTSM3MonthlyVisibilityEntry& Entry)
		{
			return Entry.ObserverRole == ObserverRole
				&& Entry.ObserverEncounterId
					== ObserverEncounterId
				&& Entry.TargetEncounterId == TargetEncounterId;
		});
}

float SurfaceArcDistanceCM(
	const int32 CellA,
	const int32 CellB)
{
	const TArray<FABTSM2Cell>& Cells = GetLogicalCells();
	if (!Cells.IsValidIndex(CellA)
		|| !Cells.IsValidIndex(CellB))
	{
		return TNumericLimits<float>::Max();
	}
	return FMath::Acos(FMath::Clamp(
		FVector::DotProduct(
			Cells[CellA].UnitCenter,
			Cells[CellB].UnitCenter),
		-1.0,
		1.0)) * ReferencePlanetRadiusCM;
}

uint64 MixOracleHash(uint64 Hash, const uint64 Value)
{
	for (int32 Shift = 0; Shift < 64; Shift += 8)
	{
		Hash ^= static_cast<uint8>((Value >> Shift) & 0xffull);
		Hash *= 1099511628211ull;
	}
	return Hash;
}

TArray<int32> BuildSweepSeeds()
{
	TArray<int32> Seeds;
	Seeds.Reserve(SweepSeedCount);
	Seeds.Add(DisplaySeed);
	for (int32 Seed = 0; Seed < SweepSeedCount - 1; ++Seed)
	{
		Seeds.Add(Seed);
	}
	return Seeds;
}

bool IsStrictlyAscending(const TArray<int32>& Values)
{
	for (int32 Index = 1; Index < Values.Num(); ++Index)
	{
		if (Values[Index] <= Values[Index - 1])
		{
			return false;
		}
	}
	return true;
}

bool CandidateHasCanonicalShape(
	const FABTSM3MonthlySpatialCandidate& Candidate)
{
	return Candidate.bHardPass
		&& Candidate.RejectReason
			== EABTSM3MonthlySpatialRejectReason::None
		&& Candidate.Encounters.Num() == ExpectedEncounterCount
		&& Candidate.Pockets.Num()
			== FABTSM3MonthlyEncounterBuilder::RequiredPocketCount
		&& Candidate.VisibilityEntries.Num()
			== FABTSM3MonthlyEncounterBuilder::
				RequiredVisibilityEntryCount
		&& Candidate.BiomeDistricts.Num()
			== ExpectedBiomeDistrictCount
		&& Candidate.PlayableEnvelopes.Num()
			== ExpectedEncounterCount
		&& Candidate.Cells.Num() == GetLogicalCells().Num()
		&& Candidate.OptimizedPVSRays > 0
		&& Candidate.SpatialCandidateHash != 0;
}

uint64 ObserverIdentity(
	const EABTSM3MonthlyObserverRole Role,
	const int32 EncounterId)
{
	return (static_cast<uint64>(static_cast<uint8>(Role)) << 32)
		| static_cast<uint32>(EncounterId + 1);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R3EncounterSpatialDefaultsAndDisplayTest,
	"ABTS.M3.Monthly.EncounterSpatial.01DefaultsAndDisplay",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R3EncounterSpatialDefaultsAndDisplayTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R3EncounterSpatialTests;
	FString ManifestFailure;
	TestTrue(TEXT("R3 acceptance manifest is self-valid"),
		FABTSM3R3AcceptanceManifest::Validate(ManifestFailure));
	TestEqual(TEXT("R3 acceptance manifest identity is frozen"),
		FABTSM3R3AcceptanceManifest::ComputeManifestHash(),
		FABTSM3R3AcceptanceManifest::FrozenManifestHash);
	const FABTSM3MonthlyEncounterSpatialConfig Config =
		MakeQuietSpatialConfig();
	TestTrue(TEXT("Spatial observation defaults on"),
		Config.bBuildSpatialObservation);
	TestEqual(TEXT("Exactly six destructible encounters"),
		Config.DestructibleEncounterCount,
		ExpectedEncounterCount);
	TestEqual(TEXT("Six flow targets"),
		Config.EncounterFlowQ.Num(),
		ExpectedEncounterCount);
	TestEqual(TEXT("Six distance windows"),
		Config.TargetRoadDistanceWindowsCells.Num(),
		ExpectedEncounterCount);
	TestEqual(TEXT("Six difficulty bands"),
		Config.DifficultyBands.Num(),
		ExpectedEncounterCount);
	TestEqual(TEXT("Six reveal policies"),
		Config.RevealPolicies.Num(),
		ExpectedEncounterCount);
	TestEqual(TEXT("Six biome archetypes"),
		Config.EncounterBiomeArchetypes.Num(),
		ExpectedEncounterCount);
	TestEqual(TEXT("Six calibrated slingshot tiers"),
		Config.EncounterSlingshotTiers.Num(),
		ExpectedEncounterCount);
	for (int32 EncounterIndex = 0;
		EncounterIndex < ExpectedEncounterCount;
		++EncounterIndex)
	{
		const EABTSSlingshotTier ExpectedTier =
			EncounterIndex < 3
				? EABTSSlingshotTier::Simple
				: EABTSSlingshotTier::Reinforced;
		TestEqual(
			FString::Printf(
				TEXT("Encounter %d uses its frozen progression tier"),
				EncounterIndex),
			Config.EncounterSlingshotTiers[EncounterIndex],
			ExpectedTier);
	}
	TestEqual(TEXT("Minimum encounter progress gap"),
		Config.MinAdjacentEncounterProgressCM,
		3500);
	TestEqual(TEXT("Maximum encounter progress gap"),
		Config.MaxAdjacentEncounterProgressCM,
		6000);
	TestEqual(TEXT("Maximum planned progress deviation"),
		Config.MaxPlannedProgressDeviationCM,
		1200);
	TestTrue(TEXT("Pocket role core is smaller than its playable envelope"),
		Config.ActivePocketRadiusCells
			< Config.PlayablePocketRadiusCells);
	TestEqual(TEXT("Observer count contract"),
		FABTSM3MonthlyEncounterBuilder::RequiredObserverCount,
		13);
	TestEqual(TEXT("PVS count contract"),
		FABTSM3MonthlyEncounterBuilder::
			RequiredVisibilityEntryCount,
		78);
	TestEqual(TEXT("Camera sample count contract"),
		FABTSM3MonthlyEncounterBuilder::
			RequiredCameraSampleCount,
		2);
	TestEqual(TEXT("Default orbit distance matches M4"),
		Config.DefaultOrbitDistanceCM,
		850);
	TestEqual(TEXT("Maximum orbit distance matches M4"),
		Config.MaxOrbitDistanceCM,
		1300);
	TestEqual(TEXT("Camera elevation matches M4"),
		Config.CameraElevationDegrees,
		60);
	TestEqual(TEXT("Observer character-center height matches M1"),
		Config.ObserverCharacterCenterHeightCM,
		60);
	TestEqual(TEXT("Observer look-at height matches M4"),
		Config.ObserverLookAtHeightCM,
		30);
	TestEqual(TEXT("Camera sample set version"),
		Config.CameraSampleSetVersion,
		2);
	TestEqual(TEXT("Pocket count contract"),
		FABTSM3MonthlyEncounterBuilder::RequiredPocketCount,
		42);
	TestEqual(TEXT("Fixture catalog count"),
		FABTSM3MonthlyEncounterBuilder::
			GetFixtureProfileCatalog().Num(),
		ExpectedEncounterCount);
	TestNotEqual(TEXT("Fixture catalog identity is nonzero"),
		FABTSM3MonthlyEncounterBuilder::
			ComputeFixtureProfileCatalogHash(),
		0ull);

	FABTSM3MonthlyRoutePool RoutePool;
	FABTSM3MonthlySpatialResult Result;
	FString Failure;
	TestTrue(TEXT("Display spatial world builds"),
		BuildSpatialResult(
			DisplaySeed,
			RoutePool,
			Result,
			Failure));
	if (!Result.bSpatialResultValid
		|| Result.RetainedCandidates.IsEmpty())
	{
		AddError(FString::Printf(
			TEXT("Display result invalid: Reason=%s Failure=%s"),
			FABTSM3MonthlyEncounterBuilder::GetRejectReasonName(
				Result.RejectReason),
			*Failure));
		return false;
	}
	TestTrue(TEXT("R3 spatial result is valid"),
		Result.bSpatialResultValid);
	TestFalse(TEXT("R3 cannot publish a monthly world"),
		Result.bMonthlyWorldAccepted);
	TestFalse(TEXT("Display source does not use route fallback"),
		Result.bUsedRouteFallback);
	TestEqual(TEXT("Result generator remains Gen3"),
		Result.GeneratorVersion,
		FABTSM3MonthlyEncounterBuilder::GeneratorVersion);
	TestEqual(TEXT("Result uses monthly policy 2"),
		Result.LayoutPolicyVersion,
		FABTSM3MonthlyEncounterBuilder::
			MonthlyLayoutPolicyVersion);
	TestNotEqual(TEXT("Spatial result identity is nonzero"),
		Result.SpatialResultHash,
		static_cast<int64>(0));
	TestTrue(TEXT("At least one spatial candidate passes"),
		Result.SpatialHardPassCount > 0);
	FABTSM3FrozenCalibrationBatch ExpectedCalibrationBatch;
	FString CalibrationFailure;
	TestTrue(TEXT("Frozen calibration batch rebuilds from public factories"),
		FABTSM3MonthlyEncounterBuilder::BuildFrozenCalibrationBatchV0(
			ReferencePlanetRadiusCM,
			ExpectedCalibrationBatch,
			CalibrationFailure));
	TestTrue(TEXT("R3 persists the exact frozen calibration batch"),
		FABTSM3FrozenCalibrationBatch::StaticStruct()
			->CompareScriptStruct(
				&ExpectedCalibrationBatch,
				&Result.FrozenCalibrationBatch,
				PPF_None));
	TestEqual(TEXT("Frozen calibration schema is explicit"),
		Result.FrozenCalibrationBatch.SchemaVersion,
		FABTSM3MonthlyEncounterBuilder::
			FrozenCalibrationSchemaVersion);
	TestNotEqual(TEXT("Frozen launch-profile identity is nonzero"),
		Result.FrozenCalibrationBatch.LaunchProfileHash,
		static_cast<int64>(0));
	TestNotEqual(TEXT("Frozen satellite-preset identity is nonzero"),
		Result.FrozenCalibrationBatch.
			SatellitePracticePresetHash,
		static_cast<int64>(0));
	TestEqual(TEXT("Frozen batch exposes all three reach envelopes"),
		Result.FrozenCalibrationBatch.ReachEnvelopes.Num(),
		3);
	float PreviousComfortableReachCM = 0.0f;
	float PreviousMaximumReachCM = 0.0f;
	for (const FABTSM6ReachEnvelope& Envelope :
		Result.FrozenCalibrationBatch.ReachEnvelopes)
	{
		TestTrue(TEXT("Comfortable reach is positive"),
			Envelope.ComfortableReachCM > 0.0f);
		TestTrue(TEXT("Maximum reach contains comfortable reach"),
			Envelope.MaximumReachCM
				>= Envelope.ComfortableReachCM);
		TestTrue(TEXT("Tier comfortable reach is strictly increasing"),
			Envelope.ComfortableReachCM
				> PreviousComfortableReachCM);
		TestTrue(TEXT("Tier maximum reach is strictly increasing"),
			Envelope.MaximumReachCM
				> PreviousMaximumReachCM);
		PreviousComfortableReachCM =
			Envelope.ComfortableReachCM;
		PreviousMaximumReachCM = Envelope.MaximumReachCM;
	}
	TestEqual(TEXT("Frozen batch identity recomputes"),
		static_cast<uint64>(
			Result.FrozenCalibrationBatch.BatchHash),
		FABTSM3MonthlyEncounterBuilder::
			ComputeFrozenCalibrationBatchHash(
				Result.FrozenCalibrationBatch));

	const FABTSM3MonthlySpatialCandidate& Candidate =
		Result.RetainedCandidates[0];
	const uint64 ResultSnapshotHash =
		FABTSM3MonthlyEncounterBuilder::
			ComputeResultSnapshotHash(Result);
	TestTrue(TEXT("Display candidate has canonical R3 shape"),
		CandidateHasCanonicalShape(Candidate));
	TestEqual(TEXT("Display result identity is frozen"),
		static_cast<uint64>(Result.SpatialResultHash),
		FABTSM3R3AcceptanceManifest::FrozenDisplayResultHash);
	TestEqual(TEXT("Display snapshot identity is frozen"),
		ResultSnapshotHash,
		FABTSM3R3AcceptanceManifest::FrozenDisplaySnapshotHash);
	TestEqual(TEXT("Display candidate identity is frozen"),
		static_cast<uint64>(Candidate.SpatialCandidateHash),
		FABTSM3R3AcceptanceManifest::FrozenDisplayCandidateHash);
	TestEqual(TEXT("Display attempted-route count is frozen"),
		Result.AttemptedRouteCandidateCount,
		FABTSM3R3AcceptanceManifest::
			DisplayAttemptedRouteCandidates);
	TestEqual(TEXT("Display spatial hard-pass count is frozen"),
		Result.SpatialHardPassCount,
		FABTSM3R3AcceptanceManifest::
			DisplaySpatialHardPassCount);
	TestEqual(TEXT("Display retained count is frozen"),
		Result.RetainedCandidates.Num(),
		FABTSM3R3AcceptanceManifest::DisplayRetainedCandidates);
	TestEqual(TEXT("Display recomputed route length is frozen"),
		Candidate.RecomputedRoute.Metrics.RouteLengthCM,
		FABTSM3R3AcceptanceManifest::
			DisplayRecomputedRouteLengthCM);
	TestEqual(TEXT("Display biome district count is frozen"),
		Candidate.BiomeDistricts.Num(),
		FABTSM3R3AcceptanceManifest::DisplayBiomeDistrictCount);
	TestEqual(TEXT("Display playable-cell count is frozen"),
		Candidate.PlayableCellCount,
		FABTSM3R3AcceptanceManifest::DisplayPlayableCellCount);
	TestEqual(TEXT("Display transition-cell count is frozen"),
		Candidate.ApprovedTransitionCellCount,
		FABTSM3R3AcceptanceManifest::
			DisplayApprovedTransitionCellCount);
	TestEqual(TEXT("Display coverage is frozen"),
		Candidate.ActiveRoleCoveragePermille,
		FABTSM3R3AcceptanceManifest::
			DisplayActiveCoveragePermille);
	TestEqual(TEXT("Display deep-wild ratio is frozen"),
		Candidate.DeepWildPermille,
		FABTSM3R3AcceptanceManifest::DisplayDeepWildPermille);
	TestEqual(TEXT("Display optimized PVS ray count is frozen"),
		Candidate.OptimizedPVSRays,
		FABTSM3R3AcceptanceManifest::DisplayOptimizedPVSRays);
	TestEqual(TEXT("Display has six encounters"),
		Candidate.Encounters.Num(),
		ExpectedEncounterCount);
	TestEqual(TEXT("Display has forty-two pockets"),
		Candidate.Pockets.Num(),
		ExpectedEncounterCount
			* ExpectedPocketsPerEncounter);
	TestEqual(TEXT("Display has seventy-eight PVS entries"),
		Candidate.VisibilityEntries.Num(),
		FABTSM3MonthlyEncounterBuilder::
			RequiredVisibilityEntryCount);
	TestTrue(TEXT("PVS respects the ray budget"),
		Candidate.OptimizedPVSRays
			<= Config.MaxOptimizedPVSRaysPerWorld);

	TSet<int32> EncounterIds;
	TSet<int32> PocketIds;
	TSet<uint64> ObserverIds;
	for (const FABTSM3MonthlySpatialEncounter& Encounter :
		Candidate.Encounters)
	{
		EncounterIds.Add(Encounter.Contract.EncounterId);
		TestEqual(TEXT("Encounter uses the frozen profile catalog"),
			Encounter.ProfileCatalogHash,
			Result.ProfileCatalogHash);
	}
	for (const FABTSM3PocketContract& Pocket :
		Candidate.Pockets)
	{
		PocketIds.Add(Pocket.PocketId);
	}
	for (const FABTSM3MonthlyVisibilityEntry& Entry :
		Candidate.VisibilityEntries)
	{
		ObserverIds.Add(ObserverIdentity(
			Entry.ObserverRole,
			Entry.ObserverEncounterId));
	}
	TestEqual(TEXT("Encounter identities are unique"),
		EncounterIds.Num(),
		ExpectedEncounterCount);
	TestEqual(TEXT("Pocket identities are unique"),
		PocketIds.Num(),
		FABTSM3MonthlyEncounterBuilder::RequiredPocketCount);
	TestEqual(TEXT("PVS contains exactly thirteen observers"),
		ObserverIds.Num(),
		FABTSM3MonthlyEncounterBuilder::RequiredObserverCount);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R3][DisplayOracle] Seed=%d SourcePool=%016llX Config=%016llX Catalog=%016llX Result=%016llX Snapshot=%016llX Best=%016llX Attempts=%d HardPass=%d Retained=%d RouteCM=%d Encounters=%d Pockets=%d Biomes=%d Playable=%d Transitions=%d Coverage=%d DeepWild=%d PVS=%d CameraSamples=%d Rays=%d Backtracks=%d ComputedManifest=%016llX"),
		DisplaySeed,
		static_cast<unsigned long long>(
			static_cast<uint64>(Result.SourceRoutePoolHash)),
		static_cast<unsigned long long>(
			static_cast<uint64>(Result.SpatialConfigHash)),
		static_cast<unsigned long long>(
			static_cast<uint64>(Result.ProfileCatalogHash)),
		static_cast<unsigned long long>(
			static_cast<uint64>(Result.SpatialResultHash)),
		static_cast<unsigned long long>(
			ResultSnapshotHash),
		static_cast<unsigned long long>(
			static_cast<uint64>(
				Candidate.SpatialCandidateHash)),
		Result.AttemptedRouteCandidateCount,
		Result.SpatialHardPassCount,
		Result.RetainedCandidates.Num(),
		Candidate.RecomputedRoute.Metrics.RouteLengthCM,
		Candidate.Encounters.Num(),
		Candidate.Pockets.Num(),
		Candidate.BiomeDistricts.Num(),
		Candidate.PlayableCellCount,
		Candidate.ApprovedTransitionCellCount,
		Candidate.ActiveRoleCoveragePermille,
		Candidate.DeepWildPermille,
		Candidate.VisibilityEntries.Num(),
		FABTSM3MonthlyEncounterBuilder::
			RequiredCameraSampleCount,
		Candidate.OptimizedPVSRays,
		Candidate.Backtracks,
		static_cast<unsigned long long>(
			FABTSM3R3AcceptanceManifest::
				ComputeManifestHash()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R3EncounterSpatialReservationTest,
	"ABTS.M3.Monthly.EncounterSpatial.02ReservationAndStrictRoad",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R3EncounterSpatialReservationTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R3EncounterSpatialTests;
	const FABTSM3MonthlyEncounterSpatialConfig Config =
		MakeQuietSpatialConfig();
	FABTSM3MonthlyRoutePool RoutePool;
	FABTSM3MonthlySpatialResult Result;
	FString Failure;
	TestTrue(TEXT("Reservation fixture builds"),
		BuildSpatialResult(
			DisplaySeed,
			RoutePool,
			Result,
			Failure));
	if (Result.RetainedCandidates.IsEmpty())
	{
		AddError(FString::Printf(
			TEXT("Reservation fixture has no candidate: %s"),
			*Failure));
		return false;
	}
	const FABTSM3MonthlySpatialCandidate& Candidate =
		Result.RetainedCandidates[0];
	const FABTSM3MonthlyRouteCandidate* SourceRoute =
		RoutePool.RetainedCandidates.FindByPredicate(
			[&Candidate](
				const FABTSM3MonthlyRouteCandidate& Route)
			{
				return Route.CandidateId
					== Candidate.SourceRouteCandidateId;
			});
	TestNotNull(TEXT("Selected candidate retains source identity"),
		SourceRoute);
	if (SourceRoute != nullptr)
	{
		TestTrue(TEXT("Strict rebuild keeps control cells"),
			SourceRoute->ControlCellIds
				== Candidate.RecomputedRoute.ControlCellIds);
		TestEqual(TEXT("Source route hash is retained"),
			Candidate.SourceRouteCandidateHash,
			SourceRoute->CandidateHash);
	}
	TestNotEqual(TEXT("Formal RoadContext identity is nonzero"),
		Candidate.RoadContextHash,
		static_cast<int64>(0));
	TestEqual(TEXT("Formal RoadContext covers all cells"),
		Candidate.RoadContext.Cells.Num(),
		GetLogicalCells().Num());

	TSet<int32> SeenPocketIds;
	TSet<int32> SeenTargetNoRoadCells;
	int32 PreviousProgressCM = INDEX_NONE;
	for (int32 Order = 0;
		Order < Candidate.Encounters.Num();
		++Order)
	{
		const FABTSM3MonthlySpatialEncounter& Encounter =
			Candidate.Encounters[Order];
		TestEqual(TEXT("Encounter order is canonical"),
			Encounter.Contract.OrderIndex,
			Order);
		const int32 ProgressCM = FMath::RoundToInt(
			Encounter.Contract.ProgressDistanceCM);
		const int32 PlannedProgressCM = static_cast<int32>(
			static_cast<int64>(
				Candidate.RecomputedRoute.Metrics.RouteLengthCM)
				* Config.EncounterFlowQ[Order]
				/ FABTSM3MonthlyRouteBuilder::
					FlowQuantization);
		TestTrue(TEXT("Road arrival remains near planned flow"),
			FMath::Abs(ProgressCM - PlannedProgressCM)
				<= Config.MaxPlannedProgressDeviationCM);
		if (Order > 0)
		{
			const int32 GapCM = ProgressCM - PreviousProgressCM;
			TestTrue(TEXT("Adjacent encounter gap is in range"),
				GapCM >= Config.MinAdjacentEncounterProgressCM
					&& GapCM
						<= Config.MaxAdjacentEncounterProgressCM);
		}
		PreviousProgressCM = ProgressCM;
		TestTrue(TEXT("Target road distance uses configured window"),
			Config.TargetRoadDistanceWindowsCells.IsValidIndex(Order)
				&& Encounter.MainRoadDistanceCells
					>= Config.TargetRoadDistanceWindowsCells[Order].X
				&& Encounter.MainRoadDistanceCells
					<= Config.TargetRoadDistanceWindowsCells[Order].Y);
		TestTrue(TEXT("Target footprint is explicit"),
			!Encounter.TargetFootprintCellIds.IsEmpty());
		TestTrue(TEXT("Target no-road set is explicit"),
			!Encounter.TargetNoRoadCellIds.IsEmpty());
		TestTrue(TEXT("Target anchor belongs to footprint"),
			Encounter.TargetFootprintCellIds.Contains(
				Encounter.TargetAnchorCellId));
		TestTrue(TEXT("Target anchor belongs to no-road set"),
			Encounter.TargetNoRoadCellIds.Contains(
				Encounter.TargetAnchorCellId));
		for (const int32 CellId :
			Encounter.TargetNoRoadCellIds)
		{
			TestFalse(TEXT("Encounter no-road sets do not overlap"),
				SeenTargetNoRoadCells.Contains(CellId));
			SeenTargetNoRoadCells.Add(CellId);
		}

		const struct
		{
			int32 PocketId;
			EABTSM3PocketRole Role;
		} ExpectedPockets[] = {
			{ Encounter.Contract.RoadArrivalPocketId,
				EABTSM3PocketRole::RoadArrival },
			{ Encounter.Contract.ScoutRevealPocketId,
				EABTSM3PocketRole::ScoutReveal },
			{ Encounter.Contract.SlingshotPocketId,
				EABTSM3PocketRole::Slingshot },
			{ Encounter.Contract.TargetEnvelopePocketId,
				EABTSM3PocketRole::TargetEnvelope },
			{ Encounter.Contract.TargetAnchorPocketId,
				EABTSM3PocketRole::TargetAnchor },
			{ Encounter.Contract.RewardPocketId,
				EABTSM3PocketRole::Reward },
			{ Encounter.Contract.ExitPocketId,
				EABTSM3PocketRole::Exit }
		};
		for (const auto& Expected : ExpectedPockets)
		{
			const FABTSM3PocketContract* Pocket =
				FindPocket(Candidate, Expected.PocketId);
			TestNotNull(TEXT("Encounter pocket reference resolves"),
				Pocket);
			if (Pocket != nullptr)
			{
				TestEqual(TEXT("Pocket role is canonical"),
					Pocket->Role,
					Expected.Role);
				TestEqual(TEXT("Pocket belongs to encounter"),
					Pocket->EncounterId,
					Encounter.Contract.EncounterId);
				TestTrue(TEXT("Pocket owns cells"),
					!Pocket->CellIds.IsEmpty());
				TestTrue(TEXT("Pocket cells are ordered"),
					IsStrictlyAscending(Pocket->CellIds)
						|| Pocket->CellIds.Num() == 1);
				TestFalse(TEXT("Pocket identity is not reused"),
					SeenPocketIds.Contains(Pocket->PocketId));
				SeenPocketIds.Add(Pocket->PocketId);
			}
		}
		const FABTSM3PocketContract* RoadArrival =
			FindPocket(
				Candidate,
				Encounter.Contract.RoadArrivalPocketId);
		const FABTSM3PocketContract* Exit =
			FindPocket(
				Candidate,
				Encounter.Contract.ExitPocketId);
		if (RoadArrival != nullptr && Exit != nullptr)
		{
			const int32 ArrivalRouteIndex =
				Candidate.RecomputedRoute.OrderedRoadCellIds.
					IndexOfByKey(RoadArrival->AnchorCellId);
			const int32 ExitRouteIndex =
				Candidate.RecomputedRoute.OrderedRoadCellIds.
					IndexOfByKey(Exit->AnchorCellId);
			TestNotEqual(TEXT("Exit anchor differs from road arrival"),
				Exit->AnchorCellId,
				RoadArrival->AnchorCellId);
			TestTrue(TEXT("Exit follows road arrival"),
				ArrivalRouteIndex != INDEX_NONE
					&& ExitRouteIndex > ArrivalRouteIndex);
		}
	}
	TestEqual(TEXT("All forty-two pockets are referenced once"),
		SeenPocketIds.Num(),
		FABTSM3MonthlyEncounterBuilder::RequiredPocketCount);
	for (const int32 RoadCellId :
		Candidate.RecomputedRoute.OrderedRoadCellIds)
	{
		TestTrue(TEXT("Road cell index is valid"),
			Candidate.Cells.IsValidIndex(RoadCellId));
		if (Candidate.Cells.IsValidIndex(RoadCellId))
		{
			TestFalse(TEXT("Strict rebuilt road never crosses no-road"),
				Candidate.Cells[RoadCellId].bNoRoad);
		}
	}
	FABTSM3MonthlySpatialFaultInjection BacktrackFault;
	BacktrackFault.ForcedReservationFailures = 2;
	FABTSM3MonthlyRoutePool BacktrackRoutePool;
	FABTSM3MonthlySpatialResult BacktrackResult;
	TestTrue(TEXT("Candidate-local reservation retries succeed"),
		BuildSpatialResult(
			DisplaySeed,
			Config,
			BacktrackFault,
			BacktrackRoutePool,
			BacktrackResult,
			Failure));
	TestTrue(TEXT("Backtrack fixture retains a candidate"),
		!BacktrackResult.RetainedCandidates.IsEmpty());
	if (!BacktrackResult.RetainedCandidates.IsEmpty())
	{
		TestEqual(TEXT("Backtrack fixture succeeds on variant two"),
			BacktrackResult.RetainedCandidates[0].Backtracks,
			2);
	}
	for (const FABTSM3MonthlySpatialAttemptReport& Report :
		BacktrackResult.AttemptReports)
	{
		if (Report.bHardPass)
		{
			TestEqual(TEXT("Successful attempt freezes local backtrack count"),
				Report.Backtracks,
				2);
		}
		else
		{
			TestTrue(TEXT("Rejected attempt remains inside local backtrack budget"),
				Report.Backtracks >= 2
					&& Report.Backtracks
						<= Config.
							MaxSpatialBacktracksPerCandidate);
		}
	}

	FABTSM3MonthlyEncounterSpatialConfig RerouteConfig = Config;
	RerouteConfig.MaxSpatialBacktracksPerCandidate = 2;
	FABTSM3MonthlySpatialFaultInjection RerouteFault;
	RerouteFault.BlockedSourceRoadOrderIndex = 5;
	FABTSM3MonthlyRoutePool RerouteRoutePool;
	FABTSM3MonthlySpatialResult RerouteResult;
	TestTrue(TEXT("Partial source-road block preserves a valid spatial pool"),
		BuildSpatialResult(
			DisplaySeed,
			RerouteConfig,
			RerouteFault,
			RerouteRoutePool,
			RerouteResult,
			Failure));
	bool bObservedStrictReroute = false;
	for (const FABTSM3MonthlySpatialCandidate& Rerouted :
		RerouteResult.RetainedCandidates)
	{
		const FABTSM3MonthlyRouteCandidate* Source =
			RerouteRoutePool.RetainedCandidates.
				FindByPredicate(
					[&Rerouted](
						const FABTSM3MonthlyRouteCandidate&
							Route)
					{
						return Route.CandidateId
							== Rerouted.
								SourceRouteCandidateId;
					});
		bObservedStrictReroute |= Source != nullptr
			&& Source->OrderedRoadCellIds
				!= Rerouted.RecomputedRoute.
					OrderedRoadCellIds;
	}
	TestTrue(TEXT("Strict rebuild demonstrably changes road cells"),
		bObservedStrictReroute);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R3EncounterSpatialBiomeEnvelopeTest,
	"ABTS.M3.Monthly.EncounterSpatial.03BiomeAndPlayableEnvelope",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R3EncounterSpatialBiomeEnvelopeTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R3EncounterSpatialTests;
	const FABTSM3MonthlyEncounterSpatialConfig Config =
		MakeQuietSpatialConfig();
	FABTSM3MonthlyRoutePool RoutePool;
	FABTSM3MonthlySpatialResult Result;
	FString Failure;
	TestTrue(TEXT("Biome fixture builds"),
		BuildSpatialResult(
			DisplaySeed,
			RoutePool,
			Result,
			Failure));
	if (Result.RetainedCandidates.IsEmpty())
	{
		AddError(FString::Printf(
			TEXT("Biome fixture has no candidate: %s"),
			*Failure));
		return false;
	}
	const FABTSM3MonthlySpatialCandidate& Candidate =
		Result.RetainedCandidates[0];
	TestEqual(TEXT("Six encounter districts plus background"),
		Candidate.BiomeDistricts.Num(),
		ExpectedBiomeDistrictCount);
	TestEqual(TEXT("One playable envelope per encounter"),
		Candidate.PlayableEnvelopes.Num(),
		ExpectedEncounterCount);
	TSet<int32> AssignedCells;
	TSet<int32> DistrictIds;
	TSet<uint8> EncounterArchetypes;
	int32 BackgroundCount = 0;
	for (const FABTSM3BiomeDistrict& District :
		Candidate.BiomeDistricts)
	{
		TestFalse(TEXT("Biome district identity is unique"),
			DistrictIds.Contains(District.BiomeDistrictId));
		DistrictIds.Add(District.BiomeDistrictId);
		BackgroundCount += District.bBackground ? 1 : 0;
		if (!District.bBackground)
		{
			EncounterArchetypes.Add(
				static_cast<uint8>(District.Archetype));
		}
		TestTrue(TEXT("Biome district owns cells"),
			!District.CellIds.IsEmpty());
		TestTrue(TEXT("Biome district cells are ordered"),
			IsStrictlyAscending(District.CellIds)
				|| District.CellIds.Num() == 1);
		for (const int32 CellId : District.CellIds)
		{
			TestFalse(TEXT("Biome districts do not overlap"),
				AssignedCells.Contains(CellId));
			AssignedCells.Add(CellId);
		}
	}
	TestEqual(TEXT("Exactly one background biome"),
		BackgroundCount,
		1);
	TestEqual(TEXT("Biome allocation covers every topology cell"),
		AssignedCells.Num(),
		GetLogicalCells().Num());
	TestTrue(TEXT("At least four encounter biome themes"),
		EncounterArchetypes.Num()
			>= Config.MinEncounterBiomeArchetypes);

	TSet<int32> PlayableCells;
	for (const FABTSM3PlayableEnvelope& Envelope :
		Candidate.PlayableEnvelopes)
	{
		TestTrue(TEXT("Playable envelope owns cells"),
			!Envelope.Cells.IsEmpty());
		int32 PreviousCellId = INDEX_NONE;
		for (const FABTSM3PlayableCellRole& Role : Envelope.Cells)
		{
			TestTrue(TEXT("Envelope CellIds are strictly ordered"),
				PreviousCellId == INDEX_NONE
					|| Role.CellId > PreviousCellId);
			PreviousCellId = Role.CellId;
			TestTrue(TEXT("Envelope cell has a biome"),
				Candidate.Cells.IsValidIndex(Role.CellId)
					&& Candidate.Cells[Role.CellId].
						BiomeDistrictId != INDEX_NONE);
			PlayableCells.Add(Role.CellId);
		}
	}
	TestEqual(TEXT("Playable union count is canonical"),
		PlayableCells.Num(),
		Candidate.PlayableCellCount);
	TestTrue(TEXT("Active-role coverage meets the gate"),
		Candidate.ActiveRoleCoveragePermille
			>= Config.MinActiveRoleCoveragePermille);
	TestTrue(TEXT("Deep wild stays under the gate"),
		Candidate.DeepWildPermille
			<= Config.MaxDeepWildPermille);
	TestTrue(TEXT("Playable envelope contains intentional transition cells"),
		Candidate.ApprovedTransitionCellCount > 0);
	TestTrue(TEXT("Coverage is no longer a tautological one hundred percent"),
		Candidate.ActiveRoleCoveragePermille < 1000);
	TestEqual(TEXT("Playable cells partition into active, transition and deep wild"),
		Candidate.ActiveRoleCellCount
			+ Candidate.ApprovedTransitionCellCount
			+ Candidate.DeepWildCellCount,
		Candidate.PlayableCellCount);
	const int32 RouteRoleMask =
		static_cast<int32>(EABTSM3ActiveRole::Route);
	for (const int32 RoadCellId :
		Candidate.RecomputedRoute.OrderedRoadCellIds)
	{
		TestTrue(TEXT("Every strict-rebuild road cell remains inside the route envelope"),
			Candidate.Cells.IsValidIndex(RoadCellId)
				&& Candidate.Cells[RoadCellId].
					PrimaryEnvelopeId != INDEX_NONE
				&& (Candidate.Cells[RoadCellId].
					ActiveRoleMask & RouteRoleMask) != 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R3EncounterSpatialVisibilityContractTest,
	"ABTS.M3.Monthly.EncounterSpatial.04VisibilityContract",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R3EncounterSpatialVisibilityContractTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R3EncounterSpatialTests;
	const FABTSM3MonthlyEncounterSpatialConfig Config =
		MakeQuietSpatialConfig();
	FABTSM3MonthlyRoutePool RoutePool;
	FABTSM3MonthlySpatialResult Result;
	FString Failure;
	TestTrue(TEXT("Visibility fixture builds"),
		BuildSpatialResult(
			DisplaySeed,
			RoutePool,
			Result,
			Failure));
	if (Result.RetainedCandidates.IsEmpty())
	{
		AddError(FString::Printf(
			TEXT("Visibility fixture has no candidate: %s"),
			*Failure));
		return false;
	}
	const FABTSM3MonthlySpatialCandidate& Candidate =
		Result.RetainedCandidates[0];
	for (const FABTSM3MonthlyVisibilityEntry& Entry :
		Candidate.VisibilityEntries)
	{
		TestTrue(TEXT("Every PVS entry evaluates"),
			Entry.bEvaluationValid);
		TestEqual(TEXT("Every PVS entry uses two camera samples"),
			Entry.CameraSampleCount,
			FABTSM3MonthlyEncounterBuilder::
				RequiredCameraSampleCount);
		TestEqual(TEXT("Every PVS entry uses six rays"),
			Entry.RayCount,
			FABTSM3MonthlyEncounterBuilder::
				RequiredCameraSampleCount * 3);
	}
	for (int32 TargetOrder = 0;
		TargetOrder < Candidate.Encounters.Num();
		++TargetOrder)
	{
		const FABTSM3MonthlySpatialEncounter& Target =
			Candidate.Encounters[TargetOrder];
		const int32 EncounterId =
			Target.Contract.EncounterId;
		const FABTSM3MonthlyVisibilityEntry* Start =
			FindVisibility(
				Candidate,
				EABTSM3MonthlyObserverRole::Start,
				INDEX_NONE,
				EncounterId);
		const FABTSM3MonthlyVisibilityEntry* PreReveal =
			FindVisibility(
				Candidate,
				EABTSM3MonthlyObserverRole::PreReveal,
				EncounterId,
				EncounterId);
		const FABTSM3MonthlyVisibilityEntry* Reveal =
			FindVisibility(
				Candidate,
				EABTSM3MonthlyObserverRole::Reveal,
				EncounterId,
				EncounterId);
		TestNotNull(TEXT("Start PVS entry exists"), Start);
		TestNotNull(TEXT("Pre-reveal PVS entry exists"), PreReveal);
		TestNotNull(TEXT("Reveal PVS entry exists"), Reveal);
		if (Start == nullptr
			|| PreReveal == nullptr
			|| Reveal == nullptr)
		{
			continue;
		}
		if (TargetOrder == 0)
		{
			TestEqual(TEXT("Only E1 is attack-readable at start"),
				Start->VisibilityClass,
				EABTSM3MonthlyVisibilityClass::AttackReadable);
			TestEqual(TEXT("E1 is readable at both orbit distances"),
				Start->AttackReadableCameraSampleCount,
				FABTSM3MonthlyEncounterBuilder::
					RequiredCameraSampleCount);
			TestTrue(TEXT("E1 start distance permits attack readability"),
				SurfaceArcDistanceCM(
					Start->ObserverCellId,
					Target.TargetAnchorCellId)
					<= Config.AttackReadableMaxDistanceCM);
		}
		else
		{
			TestNotEqual(TEXT("Later encounters are not attack-readable at start"),
				Start->VisibilityClass,
				EABTSM3MonthlyVisibilityClass::AttackReadable);
			TestEqual(TEXT("Later encounters are unreadable at every start sample"),
				Start->AttackReadableCameraSampleCount,
				0);
			TestNotEqual(TEXT("Target remains unreadable before reveal"),
				PreReveal->VisibilityClass,
				EABTSM3MonthlyVisibilityClass::AttackReadable);
			TestEqual(TEXT("Target is unreadable at every pre-reveal sample"),
				PreReveal->AttackReadableCameraSampleCount,
				0);
			TestTrue(TEXT("Later target starts outside attack-readable distance"),
				SurfaceArcDistanceCM(
					Start->ObserverCellId,
					Target.TargetAnchorCellId)
					> Config.AttackReadableMaxDistanceCM);
			TestTrue(TEXT("Semantic pre-reveal is outside attack-readable distance"),
				SurfaceArcDistanceCM(
					PreReveal->ObserverCellId,
					Target.TargetAnchorCellId)
					> Config.AttackReadableMaxDistanceCM);
		}
		if (Target.RevealPolicy
			== EABTSM3MonthlyRevealPolicy::DirectVisual)
		{
			TestEqual(TEXT("Direct reveal becomes attack-readable"),
				Reveal->VisibilityClass,
				EABTSM3MonthlyVisibilityClass::AttackReadable);
			TestEqual(TEXT("Direct reveal is readable at both orbit distances"),
				Reveal->AttackReadableCameraSampleCount,
				FABTSM3MonthlyEncounterBuilder::
					RequiredCameraSampleCount);
		}
		else
		{
			TestTrue(TEXT("Scout reveal becomes detectable"),
				Reveal->bScoutDetectable);
		}
		for (int32 FutureOrder = TargetOrder + 2;
			FutureOrder < Candidate.Encounters.Num();
			++FutureOrder)
		{
			const FABTSM3MonthlyVisibilityEntry* Future =
				FindVisibility(
					Candidate,
					EABTSM3MonthlyObserverRole::Reveal,
					EncounterId,
					Candidate.Encounters[FutureOrder].
						Contract.EncounterId);
			TestNotNull(TEXT("Future PVS entry exists"), Future);
			if (Future != nullptr)
			{
				TestNotEqual(TEXT("Future target is not attack-readable"),
					Future->VisibilityClass,
					EABTSM3MonthlyVisibilityClass::
						AttackReadable);
				TestEqual(TEXT("Future target is unreadable at every camera sample"),
					Future->AttackReadableCameraSampleCount,
					0);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R3EncounterSpatialDeterminismTamperTest,
	"ABTS.M3.Monthly.EncounterSpatial.05DeterminismAndHashTamper",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R3EncounterSpatialDeterminismTamperTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R3EncounterSpatialTests;
	const FABTSM3MonthlyEncounterSpatialConfig SpatialConfig =
		MakeQuietSpatialConfig();
	const FABTSM3MonthlyRouteConfig RouteConfig =
		MakeQuietRouteConfig();
	FABTSM3MonthlyRoutePool FirstRoute;
	FABTSM3MonthlyRoutePool SecondRoute;
	FABTSM3MonthlySpatialResult First;
	FABTSM3MonthlySpatialResult Second;
	FString Failure;
	TestTrue(TEXT("First deterministic R3 build"),
		BuildSpatialResult(
			DisplaySeed,
			SpatialConfig,
			FABTSM3MonthlySpatialFaultInjection(),
			FirstRoute,
			First,
			Failure));
	TestTrue(TEXT("Second deterministic R3 build"),
		BuildSpatialResult(
			DisplaySeed,
			SpatialConfig,
			FABTSM3MonthlySpatialFaultInjection(),
			SecondRoute,
			Second,
			Failure));
	TestTrue(TEXT("Source route pools are deeply equal"),
		RoutePoolEqual(FirstRoute, SecondRoute));
	TestTrue(TEXT("Spatial results are deeply equal"),
		SpatialResultEqual(First, Second));
	TestEqual(TEXT("Spatial result hash is deterministic"),
		First.SpatialResultHash,
		Second.SpatialResultHash);
	TestEqual(TEXT("Spatial snapshot is deterministic"),
		FABTSM3MonthlyEncounterBuilder::
			ComputeResultSnapshotHash(First),
		FABTSM3MonthlyEncounterBuilder::
			ComputeResultSnapshotHash(Second));
	EABTSM3MonthlySpatialRejectReason ValidationReason =
		EABTSM3MonthlySpatialRejectReason::None;
	TestTrue(TEXT("Canonical result validates"),
		FABTSM3MonthlyEncounterBuilder::Validate(
			SpatialConfig,
			RouteConfig,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			FirstRoute,
			FABTSM3MonthlySpatialFaultInjection(),
			First,
			ValidationReason,
			Failure));

	FABTSM3MonthlyEncounterSpatialConfig MutatedConfig =
		SpatialConfig;
	++MutatedConfig.RoadHalfWidthCM;
	TestNotEqual(TEXT("Config mutation changes config identity"),
		FABTSM3MonthlyEncounterBuilder::ComputeConfigHash(
			SpatialConfig,
			RouteConfig,
			ReferencePlanetRadiusCM,
			FABTSM3MonthlyRouteBuilder::ComputeTopologyHash(
				GetLogicalCells())),
		FABTSM3MonthlyEncounterBuilder::ComputeConfigHash(
			MutatedConfig,
			RouteConfig,
			ReferencePlanetRadiusCM,
			FABTSM3MonthlyRouteBuilder::ComputeTopologyHash(
				GetLogicalCells())));
	FABTSM3MonthlyEncounterSpatialConfig
		MutatedPlannedDeviationConfig = SpatialConfig;
	++MutatedPlannedDeviationConfig.
		MaxPlannedProgressDeviationCM;
	TestNotEqual(
		TEXT("Planned progress deviation changes config identity"),
		FABTSM3MonthlyEncounterBuilder::ComputeConfigHash(
			SpatialConfig,
			RouteConfig,
			ReferencePlanetRadiusCM,
			FABTSM3MonthlyRouteBuilder::ComputeTopologyHash(
				GetLogicalCells())),
		FABTSM3MonthlyEncounterBuilder::ComputeConfigHash(
			MutatedPlannedDeviationConfig,
			RouteConfig,
			ReferencePlanetRadiusCM,
			FABTSM3MonthlyRouteBuilder::ComputeTopologyHash(
				GetLogicalCells())));

	if (First.RetainedCandidates.IsEmpty()
		|| First.RetainedCandidates[0].Encounters.IsEmpty())
	{
		AddError(TEXT("Tamper fixture has no encounter."));
		return false;
	}
	FABTSM3MonthlySpatialResult Tampered = First;
	FABTSM3MonthlySpatialCandidate& TamperedCandidate =
		Tampered.RetainedCandidates[0];
	FABTSM3MonthlySpatialEncounter& TamperedEncounter =
		TamperedCandidate.Encounters[0];
	TamperedEncounter.TargetAnchorCellId =
		(TamperedEncounter.TargetAnchorCellId + 1)
			% GetLogicalCells().Num();
	TamperedCandidate.SpatialCandidateHash =
		static_cast<int64>(
			FABTSM3MonthlyEncounterBuilder::
				ComputeCandidateHash(TamperedCandidate));
	Tampered.SpatialResultHash = static_cast<int64>(
		FABTSM3MonthlyEncounterBuilder::
			ComputeResultHash(Tampered));
	TestFalse(TEXT("Outer re-sign cannot hide semantic tampering"),
		FABTSM3MonthlyEncounterBuilder::Validate(
			SpatialConfig,
			RouteConfig,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			FirstRoute,
			FABTSM3MonthlySpatialFaultInjection(),
			Tampered,
			ValidationReason,
			Failure));
	TestEqual(TEXT("Semantic tamper reports hash mismatch"),
		ValidationReason,
		EABTSM3MonthlySpatialRejectReason::HashMismatch);

	FABTSM3MonthlySpatialResult CalibrationTampered = First;
	++CalibrationTampered.FrozenCalibrationBatch.
		LaunchProfileHash;
	CalibrationTampered.SpatialResultHash = static_cast<int64>(
		FABTSM3MonthlyEncounterBuilder::ComputeResultHash(
			CalibrationTampered));
	TestFalse(TEXT("Outer re-sign cannot hide frozen calibration tampering"),
		FABTSM3MonthlyEncounterBuilder::Validate(
			SpatialConfig,
			RouteConfig,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			FirstRoute,
			FABTSM3MonthlySpatialFaultInjection(),
			CalibrationTampered,
			ValidationReason,
			Failure));
	TestEqual(TEXT("Frozen calibration tamper reports hash mismatch"),
		ValidationReason,
		EABTSM3MonthlySpatialRejectReason::HashMismatch);

	const auto ResignOuterIdentity =
		[](FABTSM3MonthlySpatialResult& Result)
	{
		Result.RetainedCandidates[0].SpatialCandidateHash =
			static_cast<int64>(
				FABTSM3MonthlyEncounterBuilder::
					ComputeCandidateHash(
						Result.RetainedCandidates[0]));
		Result.SpatialResultHash = static_cast<int64>(
			FABTSM3MonthlyEncounterBuilder::
				ComputeResultHash(Result));
	};
	FABTSM3MonthlySpatialResult RouteTampered = First;
	++RouteTampered.RetainedCandidates[0].
		RecomputedRoute.Metrics.RouteLengthCM;
	RouteTampered.RetainedCandidates[0].
		RecomputedRoute.CandidateHash = static_cast<int64>(
			FABTSM3MonthlyRouteBuilder::ComputeCandidateHash(
				RouteTampered.RetainedCandidates[0].
					RecomputedRoute));
	ResignOuterIdentity(RouteTampered);
	TestFalse(TEXT("Re-signed derived route tamper is rejected"),
		FABTSM3MonthlyEncounterBuilder::Validate(
			SpatialConfig,
			RouteConfig,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			FirstRoute,
			FABTSM3MonthlySpatialFaultInjection(),
			RouteTampered,
			ValidationReason,
			Failure));

	FABTSM3MonthlySpatialResult PVSTampered = First;
	PVSTampered.RetainedCandidates[0].
		VisibilityEntries[0].bIdealSphereBlocked =
			!PVSTampered.RetainedCandidates[0].
				VisibilityEntries[0].bIdealSphereBlocked;
	ResignOuterIdentity(PVSTampered);
	TestFalse(TEXT("Re-signed PVS semantic tamper is rejected"),
		FABTSM3MonthlyEncounterBuilder::Validate(
			SpatialConfig,
			RouteConfig,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			FirstRoute,
			FABTSM3MonthlySpatialFaultInjection(),
			PVSTampered,
			ValidationReason,
			Failure));

	FABTSM3MonthlySpatialResult BiomeTampered = First;
	BiomeTampered.RetainedCandidates[0].
		BiomeDistricts[0].ObservedTerrainType =
			EABTSM3TerrainType::Water;
	ResignOuterIdentity(BiomeTampered);
	TestFalse(TEXT("Re-signed biome semantic tamper is rejected"),
		FABTSM3MonthlyEncounterBuilder::Validate(
			SpatialConfig,
			RouteConfig,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			FirstRoute,
			FABTSM3MonthlySpatialFaultInjection(),
			BiomeTampered,
			ValidationReason,
			Failure));

	FABTSM3MonthlySpatialResult ScratchTampered = First;
	FABTSM3MonthlySpatialCandidate& ScratchCandidate =
		ScratchTampered.RetainedCandidates[0];
	const int32 ScratchCellId =
		ScratchCandidate.Cells.IndexOfByPredicate(
			[](const FABTSM3MonthlySpatialCell& Cell)
			{
				return Cell.HeightQ
					< FABTSM3MonthlyRouteBuilder::
						FlowQuantization;
			});
	if (ScratchCellId == INDEX_NONE)
	{
		AddError(TEXT("Scratch tamper fixture has no mutable cell."));
		return false;
	}
	++ScratchCandidate.Cells[ScratchCellId].HeightQ;
	ResignOuterIdentity(ScratchTampered);
	TestFalse(TEXT("Re-signed scratch-field tamper is rejected by full rebuild"),
		FABTSM3MonthlyEncounterBuilder::Validate(
			SpatialConfig,
			RouteConfig,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			FirstRoute,
			FABTSM3MonthlySpatialFaultInjection(),
			ScratchTampered,
			ValidationReason,
			Failure));

	FABTSM3MonthlySpatialFaultInjection PartialFailureFault;
	PartialFailureFault.RejectedSourceCandidateId =
		FirstRoute.RetainedCandidates[0].CandidateId;
	FABTSM3MonthlyRoutePool PartialRoute;
	FABTSM3MonthlySpatialResult PartialResult;
	TestTrue(TEXT("One failed source candidate preserves a valid partial pool"),
		BuildSpatialResult(
			DisplaySeed,
			SpatialConfig,
			PartialFailureFault,
			PartialRoute,
			PartialResult,
			Failure));
	TestEqual(TEXT("Partial pool reports one failed source"),
		PartialResult.SpatialHardPassCount,
		PartialRoute.RetainedCandidates.Num() - 1);
	TestTrue(TEXT("Canonical partial-failure report validates"),
		FABTSM3MonthlyEncounterBuilder::Validate(
			SpatialConfig,
			RouteConfig,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			PartialRoute,
			PartialFailureFault,
			PartialResult,
			ValidationReason,
			Failure));
	const int32 FailedReportIndex =
		PartialResult.AttemptReports.IndexOfByPredicate(
			[](const FABTSM3MonthlySpatialAttemptReport& Report)
			{
				return !Report.bHardPass;
			});
	if (FailedReportIndex == INDEX_NONE)
	{
		AddError(TEXT("Partial-failure fixture has no failed report."));
		return false;
	}
	FABTSM3MonthlySpatialResult PartialReportTampered =
		PartialResult;
	PartialReportTampered.AttemptReports[
		FailedReportIndex].FailureCode =
			TEXT("ReSignedFailureCode");
	PartialReportTampered.SpatialResultHash =
		static_cast<int64>(
			FABTSM3MonthlyEncounterBuilder::
				ComputeResultHash(PartialReportTampered));
	TestFalse(TEXT("Re-signed failed-attempt report tamper is rejected"),
		FABTSM3MonthlyEncounterBuilder::Validate(
			SpatialConfig,
			RouteConfig,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			PartialRoute,
			PartialFailureFault,
			PartialReportTampered,
			ValidationReason,
			Failure));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R3EncounterSpatialReferencePVSTest,
	"ABTS.M3.Monthly.EncounterSpatial.06ReferencePVS",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R3EncounterSpatialReferencePVSTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R3EncounterSpatialTests;
	const FABTSM3MonthlyEncounterSpatialConfig SpatialConfig =
		MakeQuietSpatialConfig();
	const TConstArrayView<int32> ReferenceSeeds =
		FABTSM3R3AcceptanceManifest::GetReferenceSeeds();
	uint64 OracleHash = 14695981039346656037ull;
	uint64 BoundaryOracleHash = 14695981039346656037ull;
	int32 Passed = 0;
	int32 BoundaryPassed = 0;
	for (const int32 Seed : ReferenceSeeds)
	{
		FABTSM3MonthlyRoutePool RoutePool;
		FABTSM3MonthlySpatialResult Result;
		FString Failure;
		if (!BuildSpatialResult(
				Seed,
				RoutePool,
				Result,
				Failure)
			|| Result.RetainedCandidates.IsEmpty())
		{
			AddError(FString::Printf(
				TEXT("Reference PVS build failed Seed=%d Failure=%s"),
				Seed,
				*Failure));
			continue;
		}
		int32 MismatchCount = INDEX_NONE;
		uint64 ReferenceHash = 0;
		const bool bReferencePassed =
			FABTSM3MonthlyEncounterBuilder::
				ValidateVisibilityAgainstReference(
					SpatialConfig,
					GetLogicalCells(),
					ReferencePlanetRadiusCM,
					Result.RetainedCandidates[0],
					MismatchCount,
					ReferenceHash,
					Failure);
		if (!bReferencePassed)
		{
			AddError(FString::Printf(
				TEXT("Reference PVS mismatch Seed=%d Mismatches=%d Failure=%s"),
				Seed,
				MismatchCount,
				*Failure));
			continue;
		}
		TestEqual(TEXT("Reference PVS has no mismatches"),
			MismatchCount,
			0);
		TestNotEqual(TEXT("Reference PVS identity is nonzero"),
			ReferenceHash,
			0ull);
		int32 HorizonBoundaryRelations = 0;
		int32 HeightBoundaryRelations = 0;
		int32 OrbitTransitionRelations = 0;
		for (const FABTSM3MonthlyVisibilityEntry& Entry :
			Result.RetainedCandidates[0].VisibilityEntries)
		{
			HorizonBoundaryRelations +=
				Entry.bIdealSphereBlocked
				&& Entry.VisibleCameraSampleCount > 0
					? 1
					: 0;
			HeightBoundaryRelations +=
				Entry.bTerrainBlocked
				&& !Entry.bIdealSphereBlocked
				&& Entry.VisibleCameraSampleCount > 0
					? 1
					: 0;
			OrbitTransitionRelations +=
				Entry.AttackReadableCameraSampleCount > 0
				&& Entry.AttackReadableCameraSampleCount
					< Entry.CameraSampleCount
					? 1
					: 0;
		}
		const bool bBoundarySeed =
			HorizonBoundaryRelations > 0
			|| HeightBoundaryRelations > 0
			|| OrbitTransitionRelations > 0;
		if (Seed != DisplaySeed)
		{
			TestTrue(
				TEXT("Frozen reference seed has measured horizon, height or orbit-distance boundary evidence"),
				bBoundarySeed);
			BoundaryPassed += bBoundarySeed ? 1 : 0;
		}
		BoundaryOracleHash = MixOracleHash(
			BoundaryOracleHash,
			static_cast<uint32>(Seed));
		BoundaryOracleHash = MixOracleHash(
			BoundaryOracleHash,
			static_cast<uint32>(HorizonBoundaryRelations));
		BoundaryOracleHash = MixOracleHash(
			BoundaryOracleHash,
			static_cast<uint32>(HeightBoundaryRelations));
		BoundaryOracleHash = MixOracleHash(
			BoundaryOracleHash,
			static_cast<uint32>(OrbitTransitionRelations));
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M3R3][ReferenceBoundary] Seed=%d Horizon=%d Height=%d Orbit=%d Qualified=%d"),
			Seed,
			HorizonBoundaryRelations,
			HeightBoundaryRelations,
			OrbitTransitionRelations,
			bBoundarySeed ? 1 : 0);
		OracleHash = MixOracleHash(
			OracleHash,
			static_cast<uint32>(Seed));
		OracleHash = MixOracleHash(
			OracleHash,
			ReferenceHash);
		++Passed;
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R3][ReferencePVSOracle] Terminal=%d Passed=%d Failed=%d BoundaryPassed=%d BoundaryOracleHash=%016llX OracleHash=%016llX"),
		ReferenceSeeds.Num(),
		Passed,
		ReferenceSeeds.Num() - Passed,
		BoundaryPassed,
		static_cast<unsigned long long>(BoundaryOracleHash),
		static_cast<unsigned long long>(OracleHash));
	TestEqual(TEXT("All reference PVS seeds pass"),
		Passed,
		ReferenceSeeds.Num());
	TestEqual(TEXT("All ten non-display references are measured boundary seeds"),
		BoundaryPassed,
		ReferenceSeeds.Num() - 1);
	TestEqual(TEXT("Reference PVS oracle is frozen"),
		OracleHash,
		FABTSM3R3AcceptanceManifest::
			FrozenReferencePVSOracleHash);
	TestEqual(TEXT("Reference boundary evidence oracle is frozen"),
		BoundaryOracleHash,
		FABTSM3R3AcceptanceManifest::
			FrozenReferenceBoundaryOracleHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R3EncounterSpatialCompatibilityBoundaryTest,
	"ABTS.M3.Monthly.EncounterSpatial.07CompatibilityBoundary",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R3EncounterSpatialCompatibilityBoundaryTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R3EncounterSpatialTests;
	const FABTSM3MonthlyRouteConfig RouteConfig =
		MakeQuietRouteConfig();
	FABTSM3MonthlyRoutePool RoutePool;
	FString Failure;
	TestTrue(TEXT("Source route pool builds"),
		BuildRoutePool(
			DisplaySeed,
			RouteConfig,
			RoutePool,
			Failure));
	const FABTSM3MonthlyRoutePool FrozenRoutePool = RoutePool;
	FABTSM3MonthlyEncounterSpatialConfig DisabledConfig =
		MakeQuietSpatialConfig();
	DisabledConfig.bBuildSpatialObservation = false;
	FABTSM3MonthlySpatialResult DisabledResult;
	TestTrue(TEXT("Disabled R3 observation is a valid no-op"),
		FABTSM3MonthlyEncounterBuilder::Build(
			DisplaySeed,
			DisabledConfig,
			RouteConfig,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			RoutePool,
			FABTSM3MonthlySpatialFaultInjection(),
			DisabledResult,
			Failure));
	TestTrue(TEXT("R3 never mutates the source R2 route pool"),
		RoutePoolEqual(RoutePool, FrozenRoutePool));
	TestTrue(TEXT("Disabled observation result is valid"),
		DisabledResult.bSpatialResultValid);
	TestFalse(TEXT("Disabled observation cannot accept monthly world"),
		DisabledResult.bMonthlyWorldAccepted);
	TestEqual(TEXT("Disabled observation is explicitly not evaluated"),
		DisabledResult.RejectReason,
		EABTSM3MonthlySpatialRejectReason::NotEvaluated);
	TestTrue(TEXT("Disabled observation retains no candidates"),
		DisabledResult.RetainedCandidates.IsEmpty());
	TestTrue(TEXT("Disabled observation emits no attempts"),
		DisabledResult.AttemptReports.IsEmpty());
	TestFalse(TEXT("R2 source remains publication-ineligible"),
		RoutePool.bMonthlyWorldAccepted);
	EABTSM3MonthlySpatialRejectReason ValidationReason =
		EABTSM3MonthlySpatialRejectReason::None;
	TestTrue(TEXT("Disabled observation validates"),
		FABTSM3MonthlyEncounterBuilder::Validate(
			DisabledConfig,
			RouteConfig,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			RoutePool,
			FABTSM3MonthlySpatialFaultInjection(),
			DisabledResult,
			ValidationReason,
			Failure));

	FABTSM3MonthlySpatialResult TamperedAttemptCount =
		DisabledResult;
	TamperedAttemptCount.AttemptedRouteCandidateCount = 1;
	TamperedAttemptCount.SpatialResultHash = static_cast<int64>(
		FABTSM3MonthlyEncounterBuilder::ComputeResultHash(
			TamperedAttemptCount));
	TestFalse(TEXT("Disabled observation rejects re-signed attempts"),
		FABTSM3MonthlyEncounterBuilder::Validate(
			DisabledConfig,
			RouteConfig,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			RoutePool,
			FABTSM3MonthlySpatialFaultInjection(),
			TamperedAttemptCount,
			ValidationReason,
			Failure));

	FABTSM3MonthlySpatialResult TamperedHardPassCount =
		DisabledResult;
	TamperedHardPassCount.SpatialHardPassCount = 1;
	TamperedHardPassCount.SpatialResultHash = static_cast<int64>(
		FABTSM3MonthlyEncounterBuilder::ComputeResultHash(
			TamperedHardPassCount));
	TestFalse(TEXT("Disabled observation rejects re-signed hard passes"),
		FABTSM3MonthlyEncounterBuilder::Validate(
			DisabledConfig,
			RouteConfig,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			RoutePool,
			FABTSM3MonthlySpatialFaultInjection(),
			TamperedHardPassCount,
			ValidationReason,
			Failure));

	FABTSM3MonthlySpatialResult TamperedFallback =
		DisabledResult;
	TamperedFallback.bUsedRouteFallback = true;
	TamperedFallback.SpatialResultHash = static_cast<int64>(
		FABTSM3MonthlyEncounterBuilder::ComputeResultHash(
			TamperedFallback));
	TestFalse(TEXT("Disabled observation rejects re-signed fallback"),
		FABTSM3MonthlyEncounterBuilder::Validate(
			DisabledConfig,
			RouteConfig,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			RoutePool,
			FABTSM3MonthlySpatialFaultInjection(),
			TamperedFallback,
			ValidationReason,
			Failure));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R3EncounterSpatialSweep100Test,
	"ABTS.M3.Monthly.EncounterSpatial.08SpatialSweep100",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R3EncounterSpatialSweep100Test::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R3EncounterSpatialTests;
	const FABTSM3MonthlyRouteConfig RouteConfig =
		MakeQuietRouteConfig();
	const FABTSM3MonthlyEncounterSpatialConfig SpatialConfig =
		MakeQuietSpatialConfig();
	const FABTSM3MonthlySpatialFaultInjection FaultInjection;
	FABTSM3MonthlyRoutePool WarmupRoute;
	FString Failure;
	TestTrue(TEXT("Spatial timing route warm-up"),
		BuildRoutePool(
			DisplaySeed,
			RouteConfig,
			WarmupRoute,
			Failure));
	FABTSM3MonthlySpatialResult WarmupResult;
	TestTrue(TEXT("Spatial timing R3 warm-up"),
		FABTSM3MonthlyEncounterBuilder::Build(
			DisplaySeed,
			SpatialConfig,
			RouteConfig,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			WarmupRoute,
			FaultInjection,
			WarmupResult,
			Failure));

	const TArray<int32> Seeds = BuildSweepSeeds();
	TArray<double> DurationsMS;
	DurationsMS.Reserve(Seeds.Num());
	int32 Accepted = 0;
	int32 Rejected = 0;
	int32 RouteFallback = 0;
	int32 MaxRays = 0;
	int32 MaxBacktracks = 0;
	uint64 OracleHash = 14695981039346656037ull;
	for (const int32 Seed : Seeds)
	{
		FABTSM3MonthlyRoutePool RoutePool;
		if (!BuildRoutePool(
				Seed,
				RouteConfig,
				RoutePool,
				Failure))
		{
			++Rejected;
			AddError(FString::Printf(
				TEXT("Sweep source route failed Seed=%d Failure=%s"),
				Seed,
				*Failure));
			continue;
		}
		FABTSM3MonthlySpatialResult Result;
		const double StartSeconds = FPlatformTime::Seconds();
		const bool bBuilt =
			FABTSM3MonthlyEncounterBuilder::Build(
				Seed,
				SpatialConfig,
				RouteConfig,
				GetLogicalCells(),
				ReferencePlanetRadiusCM,
				RoutePool,
				FaultInjection,
				Result,
				Failure);
		DurationsMS.Add(
			(FPlatformTime::Seconds() - StartSeconds) * 1000.0);
		if (!bBuilt
			|| !Result.bSpatialResultValid
			|| Result.RetainedCandidates.IsEmpty()
			|| Result.bUsedRouteFallback
			|| !CandidateHasCanonicalShape(
				Result.RetainedCandidates[0]))
		{
			++Rejected;
			RouteFallback += Result.bUsedRouteFallback ? 1 : 0;
			FString AttemptReasons;
			for (const FABTSM3MonthlySpatialAttemptReport& Report :
				Result.AttemptReports)
			{
				if (!AttemptReasons.IsEmpty())
				{
					AttemptReasons.AppendChar(TEXT(','));
				}
				AttemptReasons.Append(FString::Printf(
					TEXT("%d:%s:%s"),
					Report.SourceRouteCandidateId,
					FABTSM3MonthlyEncounterBuilder::
						GetRejectReasonName(
							Report.RejectReason),
					*Report.FailureCode.ToString()));
			}
			AddError(FString::Printf(
				TEXT("Spatial sweep rejected Seed=%d Reason=%s Failure=%s Attempts=%s"),
				Seed,
				FABTSM3MonthlyEncounterBuilder::
					GetRejectReasonName(Result.RejectReason),
				*Failure,
				*AttemptReasons));
			continue;
		}
		++Accepted;
		const FABTSM3MonthlySpatialCandidate& Candidate =
			Result.RetainedCandidates[0];
		MaxRays = FMath::Max(
			MaxRays,
			Candidate.OptimizedPVSRays);
		MaxBacktracks = FMath::Max(
			MaxBacktracks,
			Candidate.Backtracks);
		OracleHash = MixOracleHash(
			OracleHash,
			static_cast<uint32>(Seed));
		OracleHash = MixOracleHash(
			OracleHash,
			static_cast<uint64>(Result.SpatialResultHash));
		OracleHash = MixOracleHash(
			OracleHash,
			FABTSM3MonthlyEncounterBuilder::
				ComputeResultSnapshotHash(Result));
		OracleHash = MixOracleHash(
			OracleHash,
			static_cast<uint32>(
				Result.RetainedCandidates.Num()));
	}
	DurationsMS.Sort();
	const int32 P95Index = FMath::Clamp(
		FMath::CeilToInt(DurationsMS.Num() * 0.95) - 1,
		0,
		FMath::Max(0, DurationsMS.Num() - 1));
	const double P95MS = DurationsMS.IsValidIndex(P95Index)
		? DurationsMS[P95Index]
		: 0.0;
	const double MaximumMS = DurationsMS.IsEmpty()
		? 0.0
		: DurationsMS.Last();
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R3][SpatialSweep] Terminal=%d Accepted=%d Rejected=%d RouteFallback=%d P95MS=%.3f MaxMS=%.3f MaxRays=%d MaxBacktracks=%d OracleHash=%016llX"),
		Seeds.Num(),
		Accepted,
		Rejected,
		RouteFallback,
		P95MS,
		MaximumMS,
		MaxRays,
		MaxBacktracks,
		static_cast<unsigned long long>(OracleHash));
	TestEqual(TEXT("Sweep terminal count"),
		Seeds.Num(),
		SweepSeedCount);
	TestEqual(TEXT("All sweep seeds spatially accepted"),
		Accepted,
		SweepSeedCount);
	TestEqual(TEXT("No sweep seed rejected"), Rejected, 0);
	TestEqual(TEXT("No sweep route fallback"), RouteFallback, 0);
	TestTrue(TEXT("Spatial P95 budget"),
		P95MS <= P95BudgetMS);
	TestTrue(TEXT("Spatial maximum budget"),
		MaximumMS <= MaxBudgetMS);
	TestTrue(TEXT("Optimized PVS ray hard cap"),
		MaxRays <= SpatialConfig.MaxOptimizedPVSRaysPerWorld);
	TestTrue(TEXT("Spatial backtrack hard cap"),
		MaxBacktracks
			<= SpatialConfig.MaxSpatialBacktracksPerCandidate);
	TestEqual(TEXT("Spatial sweep oracle is frozen"),
		OracleHash,
		FABTSM3R3AcceptanceManifest::FrozenSweepOracleHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R3EncounterSpatialAllSourceRoadBlockedTest,
	"ABTS.M3.Monthly.EncounterSpatialFailure.01AllSourceRoadHardBlocked",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R3EncounterSpatialAllSourceRoadBlockedTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R3EncounterSpatialTests;
	FABTSM3MonthlyEncounterSpatialConfig InvalidZeroConfig =
		MakeQuietSpatialConfig();
	InvalidZeroConfig.MaxPlannedProgressDeviationCM = 0;
	FABTSM3MonthlyRoutePool InvalidZeroRoute;
	FABTSM3MonthlySpatialResult InvalidZeroResult;
	FString InvalidFailure;
	TestFalse(TEXT("Zero planned progress deviation fails closed"),
		BuildSpatialResult(
			DisplaySeed,
			InvalidZeroConfig,
			FABTSM3MonthlySpatialFaultInjection(),
			InvalidZeroRoute,
			InvalidZeroResult,
			InvalidFailure));
	TestEqual(TEXT("Zero planned progress deviation is invalid config"),
		InvalidZeroResult.RejectReason,
		EABTSM3MonthlySpatialRejectReason::InvalidConfig);

	FABTSM3MonthlyEncounterSpatialConfig InvalidRangeConfig =
		MakeQuietSpatialConfig();
	InvalidRangeConfig.MaxPlannedProgressDeviationCM =
		InvalidRangeConfig.MinAdjacentEncounterProgressCM + 1;
	FABTSM3MonthlyRoutePool InvalidRangeRoute;
	FABTSM3MonthlySpatialResult InvalidRangeResult;
	TestFalse(TEXT("Oversized planned progress deviation fails closed"),
		BuildSpatialResult(
			DisplaySeed,
			InvalidRangeConfig,
			FABTSM3MonthlySpatialFaultInjection(),
			InvalidRangeRoute,
			InvalidRangeResult,
			InvalidFailure));
	TestEqual(
		TEXT("Oversized planned progress deviation is invalid config"),
		InvalidRangeResult.RejectReason,
		EABTSM3MonthlySpatialRejectReason::InvalidConfig);

	FABTSM3MonthlyEncounterSpatialConfig InvalidBacktrackConfig =
		MakeQuietSpatialConfig();
	InvalidBacktrackConfig.MaxSpatialBacktracksPerCandidate = 513;
	FABTSM3MonthlyRoutePool InvalidBacktrackRoute;
	FABTSM3MonthlySpatialResult InvalidBacktrackResult;
	TestFalse(TEXT("Oversized spatial backtrack cap fails closed"),
		BuildSpatialResult(
			DisplaySeed,
			InvalidBacktrackConfig,
			FABTSM3MonthlySpatialFaultInjection(),
			InvalidBacktrackRoute,
			InvalidBacktrackResult,
			InvalidFailure));
	TestEqual(TEXT("Oversized spatial backtrack cap is invalid config"),
		InvalidBacktrackResult.RejectReason,
		EABTSM3MonthlySpatialRejectReason::InvalidConfig);

	FABTSM3MonthlyEncounterSpatialConfig InvalidTraceConfig =
		MakeQuietSpatialConfig();
	InvalidTraceConfig.OptimizedTraceSamples = 129;
	InvalidTraceConfig.ReferenceTraceSamples = 129;
	FABTSM3MonthlyRoutePool InvalidTraceRoute;
	FABTSM3MonthlySpatialResult InvalidTraceResult;
	TestFalse(TEXT("Oversized optimized trace cap fails closed"),
		BuildSpatialResult(
			DisplaySeed,
			InvalidTraceConfig,
			FABTSM3MonthlySpatialFaultInjection(),
			InvalidTraceRoute,
			InvalidTraceResult,
			InvalidFailure));
	TestEqual(TEXT("Oversized optimized trace cap is invalid config"),
		InvalidTraceResult.RejectReason,
		EABTSM3MonthlySpatialRejectReason::InvalidConfig);

	FABTSM3MonthlySpatialFaultInjection FaultInjection;
	FaultInjection.bBlockEverySourceRoadCell = true;
	FABTSM3MonthlyRoutePool FirstRoute;
	FABTSM3MonthlyRoutePool SecondRoute;
	FABTSM3MonthlySpatialResult First;
	FABTSM3MonthlySpatialResult Second;
	FString Failure;
	const bool bFirstBuilt = BuildSpatialResult(
		DisplaySeed,
		MakeQuietSpatialConfig(),
		FaultInjection,
		FirstRoute,
		First,
		Failure);
	const bool bSecondBuilt = BuildSpatialResult(
		DisplaySeed,
		MakeQuietSpatialConfig(),
		FaultInjection,
		SecondRoute,
		Second,
		Failure);
	TestFalse(TEXT("All source-road hard blocks fail closed"),
		bFirstBuilt);
	TestFalse(TEXT("Repeated hard-block injection fails closed"),
		bSecondBuilt);
	TestTrue(TEXT("Failure source pools are identical"),
		RoutePoolEqual(FirstRoute, SecondRoute));
	TestTrue(TEXT("Failure results are deterministic"),
		SpatialResultEqual(First, Second));
	TestFalse(TEXT("Failure result is not valid"),
		First.bSpatialResultValid);
	TestFalse(TEXT("Failure result cannot publish monthly world"),
		First.bMonthlyWorldAccepted);
	TestEqual(TEXT("Every source candidate was attempted"),
		First.AttemptedRouteCandidateCount,
		FirstRoute.RetainedCandidates.Num());
	TestEqual(TEXT("No hard-blocked candidate passes"),
		First.SpatialHardPassCount,
		0);
	TestTrue(TEXT("No hard-blocked candidate is retained"),
		First.RetainedCandidates.IsEmpty());
	TestEqual(TEXT("Terminal rejection is all candidates failed"),
		First.RejectReason,
		EABTSM3MonthlySpatialRejectReason::
			AllRouteCandidatesFailed);
	int32 FailedReports = 0;
	int32 RoadRebuildRejects = 0;
	for (const FABTSM3MonthlySpatialAttemptReport& Report :
		First.AttemptReports)
	{
		FailedReports += !Report.bHardPass ? 1 : 0;
		RoadRebuildRejects += Report.RejectReason
				== EABTSM3MonthlySpatialRejectReason::
					RoadRebuildFailed
			? 1
			: 0;
	}
	TestEqual(TEXT("Every attempt report fails"),
		FailedReports,
		First.AttemptReports.Num());
	TestEqual(TEXT("Every injected failure reaches strict road rebuild"),
		RoadRebuildRejects,
		First.AttemptReports.Num());
	TestNotEqual(TEXT("Failure identity is nonzero"),
		First.SpatialResultHash,
		static_cast<int64>(0));
	const uint64 FailureSnapshotHash =
		FABTSM3MonthlyEncounterBuilder::
			ComputeResultSnapshotHash(First);
	TestEqual(TEXT("Hard-block result identity is frozen"),
		static_cast<uint64>(First.SpatialResultHash),
		FABTSM3R3AcceptanceManifest::
			FrozenBlockedRoadResultHash);
	TestEqual(TEXT("Hard-block snapshot identity is frozen"),
		FailureSnapshotHash,
		FABTSM3R3AcceptanceManifest::
			FrozenBlockedRoadSnapshotHash);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R3][HardBlockFailureOracle] Attempts=%d RoadRebuildRejects=%d Result=%016llX Snapshot=%016llX"),
		First.AttemptReports.Num(),
		RoadRebuildRejects,
		static_cast<unsigned long long>(
			static_cast<uint64>(First.SpatialResultHash)),
		static_cast<unsigned long long>(
			FailureSnapshotHash));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R3EncounterSpatialPVSFailureTest,
	"ABTS.M3.Monthly.EncounterSpatialFailure.02PVSInvalidAndRayBudget",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R3EncounterSpatialPVSFailureTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R3EncounterSpatialTests;
	FABTSM3MonthlySpatialFaultInjection InvalidObserverFault;
	InvalidObserverFault.bInvalidateStartObserver = true;
	FABTSM3MonthlyRoutePool InvalidRoute;
	FABTSM3MonthlySpatialResult InvalidResult;
	FString Failure;
	TestFalse(TEXT("Invalid start observer fails closed"),
		BuildSpatialResult(
			DisplaySeed,
			MakeQuietSpatialConfig(),
			InvalidObserverFault,
			InvalidRoute,
			InvalidResult,
			Failure));
	TestFalse(TEXT("Invalid PVS result is not valid"),
		InvalidResult.bSpatialResultValid);
	TestEqual(TEXT("Invalid PVS terminal rejection"),
		InvalidResult.RejectReason,
		EABTSM3MonthlySpatialRejectReason::
			AllRouteCandidatesFailed);
	int32 PVSInvalidRejects = 0;
	for (const FABTSM3MonthlySpatialAttemptReport& Report :
		InvalidResult.AttemptReports)
	{
		PVSInvalidRejects += Report.RejectReason
				== EABTSM3MonthlySpatialRejectReason::PVSInvalid
			? 1
			: 0;
	}
	TestTrue(TEXT("Invalid observer reaches and rejects the PVS gate"),
		PVSInvalidRejects > 0);
	TestEqual(TEXT("Invalid-observer pool retains no candidate"),
		InvalidResult.RetainedCandidates.Num(),
		0);

	FABTSM3MonthlyEncounterSpatialConfig RayBudgetConfig =
		MakeQuietSpatialConfig();
	RayBudgetConfig.MaxOptimizedPVSRaysPerWorld = 1;
	FABTSM3MonthlyRoutePool RayRoute;
	FABTSM3MonthlySpatialResult RayResult;
	TestFalse(TEXT("Insufficient PVS ray budget fails closed"),
		BuildSpatialResult(
			DisplaySeed,
			RayBudgetConfig,
			FABTSM3MonthlySpatialFaultInjection(),
			RayRoute,
			RayResult,
			Failure));
	TestFalse(TEXT("Ray-budget result is not valid"),
		RayResult.bSpatialResultValid);
	TestEqual(TEXT("Ray-budget terminal rejection"),
		RayResult.RejectReason,
		EABTSM3MonthlySpatialRejectReason::
			AllRouteCandidatesFailed);
	int32 RayBudgetRejects = 0;
	for (const FABTSM3MonthlySpatialAttemptReport& Report :
		RayResult.AttemptReports)
	{
		RayBudgetRejects += Report.RejectReason
				== EABTSM3MonthlySpatialRejectReason::
					RayBudgetExceeded
			? 1
			: 0;
	}
	TestTrue(TEXT("Ray-budget fixture reaches and rejects the ray gate"),
		RayBudgetRejects > 0);
	TestEqual(TEXT("Ray-budget pool retains no candidate"),
		RayResult.RetainedCandidates.Num(),
		0);
	TestNotEqual(TEXT("Fault identities distinguish failure modes"),
		InvalidResult.FaultInjectionHash,
		RayResult.FaultInjectionHash);
	TestNotEqual(TEXT("Config identities distinguish failure modes"),
		InvalidResult.SpatialConfigHash,
		RayResult.SpatialConfigHash);
	const uint64 InvalidSnapshotHash =
		FABTSM3MonthlyEncounterBuilder::
			ComputeResultSnapshotHash(InvalidResult);
	const uint64 RaySnapshotHash =
		FABTSM3MonthlyEncounterBuilder::
			ComputeResultSnapshotHash(RayResult);
	TestEqual(TEXT("Invalid-PVS result identity is frozen"),
		static_cast<uint64>(InvalidResult.SpatialResultHash),
		FABTSM3R3AcceptanceManifest::
			FrozenInvalidPVSResultHash);
	TestEqual(TEXT("Invalid-PVS snapshot identity is frozen"),
		InvalidSnapshotHash,
		FABTSM3R3AcceptanceManifest::
			FrozenInvalidPVSSnapshotHash);
	TestEqual(TEXT("Ray-budget result identity is frozen"),
		static_cast<uint64>(RayResult.SpatialResultHash),
		FABTSM3R3AcceptanceManifest::
			FrozenRayBudgetResultHash);
	TestEqual(TEXT("Ray-budget snapshot identity is frozen"),
		RaySnapshotHash,
		FABTSM3R3AcceptanceManifest::
			FrozenRayBudgetSnapshotHash);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R3][PVSFailureOracle] InvalidAttempts=%d InvalidRejects=%d InvalidHash=%016llX InvalidSnapshot=%016llX RayAttempts=%d RayRejects=%d RayHash=%016llX RaySnapshot=%016llX"),
		InvalidResult.AttemptReports.Num(),
		PVSInvalidRejects,
		static_cast<unsigned long long>(
			static_cast<uint64>(
				InvalidResult.SpatialResultHash)),
		static_cast<unsigned long long>(
			InvalidSnapshotHash),
		RayResult.AttemptReports.Num(),
		RayBudgetRejects,
		static_cast<unsigned long long>(
			static_cast<uint64>(RayResult.SpatialResultHash)),
		static_cast<unsigned long long>(
			RaySnapshotHash));
	return true;
}

#endif
