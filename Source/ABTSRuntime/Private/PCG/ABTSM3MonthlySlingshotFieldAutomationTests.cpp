// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ABTSRuntime.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "PCG/ABTSM3MonthlySlingshotField.h"
#include "PCG/ABTSM3R31AcceptanceManifest.h"
#include "PCG/ABTSM3R3AcceptanceManifest.h"
#include "Planet/ABTSM2Planet.h"

namespace ABTSM3R31SlingshotFieldTests
{
constexpr float ReferencePlanetRadiusCM = 10000.0f;
constexpr int32 DisplaySeed = 312503;
constexpr int32 SweepSeedCount = 100;
constexpr int32 DefaultFieldCount = 8;
constexpr int32 DefaultSlotsPerField = 7;
constexpr int32 FlowQuantization = 1000000;
constexpr int32 RoadEndClearanceCM = 1200;
constexpr int32 RoadFieldProgressClearanceCM = 900;

TArray<FABTSM2Cell> BuildLogicalCells()
{
	AABTSM2Planet::FUnitSphereMesh Mesh;
	AABTSM2Planet::BuildUnitIcosphere(5, Mesh);
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
	static const TArray<FABTSM2Cell> Cells =
		BuildLogicalCells();
	return Cells;
}

FABTSM3MonthlyRouteConfig MakeRouteConfig()
{
	FABTSM3MonthlyRouteConfig Config;
	Config.bEmitRouteLogs = false;
	return Config;
}

FABTSM3MonthlyEncounterSpatialConfig MakeSpatialConfig()
{
	FABTSM3MonthlyEncounterSpatialConfig Config;
	Config.bEmitSpatialLogs = false;
	return Config;
}

FABTSM3MonthlySlingshotFieldConfig MakeFieldConfig()
{
	FABTSM3MonthlySlingshotFieldConfig Config;
	Config.bEmitSlingshotFieldLogs = false;
	return Config;
}

bool BuildSpatialFixture(
	const int32 Seed,
	FABTSM3MonthlyRoutePool& OutRoutePool,
	FABTSM3MonthlySpatialResult& OutSpatialResult,
	FString& OutFailure)
{
	const FABTSM3MonthlyRouteConfig RouteConfig =
		MakeRouteConfig();
	if (!FABTSM3MonthlyRouteBuilder::Build(
			Seed,
			RouteConfig,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			FABTSM3MonthlyRoadContext(),
			OutRoutePool,
			OutFailure))
	{
		return false;
	}
	return FABTSM3MonthlyEncounterBuilder::Build(
		Seed,
		MakeSpatialConfig(),
		RouteConfig,
		GetLogicalCells(),
		ReferencePlanetRadiusCM,
		OutRoutePool,
		FABTSM3MonthlySpatialFaultInjection(),
		OutSpatialResult,
		OutFailure);
}

bool BuildFieldFixture(
	const int32 Seed,
	const FABTSM3MonthlySlingshotFieldConfig& FieldConfig,
	FABTSM3MonthlyRoutePool& OutRoutePool,
	FABTSM3MonthlySpatialResult& OutSpatialResult,
	FABTSM3MonthlySlingshotFieldResult& OutFieldResult,
	FString& OutFailure)
{
	if (!BuildSpatialFixture(
			Seed,
			OutRoutePool,
			OutSpatialResult,
			OutFailure))
	{
		return false;
	}
	return FABTSM3MonthlySlingshotFieldBuilder::Build(
		Seed,
		FieldConfig,
		GetLogicalCells(),
		ReferencePlanetRadiusCM,
		OutSpatialResult,
		OutFieldResult,
		OutFailure);
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

float ChordDistanceCM(
	const int32 CellA,
	const int32 CellB)
{
	const TArray<FABTSM2Cell>& Cells = GetLogicalCells();
	return FVector::Distance(
		Cells[CellA].UnitCenter,
		Cells[CellB].UnitCenter)
		* ReferencePlanetRadiusCM;
}

bool ResultsEqual(
	const FABTSM3MonthlySlingshotFieldResult& A,
	const FABTSM3MonthlySlingshotFieldResult& B)
{
	return FABTSM3MonthlySlingshotFieldResult::StaticStruct()
		->CompareScriptStruct(&A, &B, PPF_None);
}

uint64 MixOracle(uint64 Hash, const uint64 Value)
{
	for (int32 Shift = 0; Shift < 64; Shift += 8)
	{
		Hash ^= static_cast<uint8>((Value >> Shift) & 0xffull);
		Hash *= 1099511628211ull;
	}
	return Hash;
}

bool ValidateCanonicalCandidate(
	FAutomationTestBase& Test,
	const FABTSM3MonthlySlingshotFieldConfig& Config,
	const FABTSM3MonthlySpatialCandidate& SpatialCandidate,
	const FABTSM3MonthlySlingshotFieldCandidate& FieldCandidate)
{
	const int32 ExpectedFieldCount =
		FABTSM3MonthlySlingshotFieldBuilder::
			RequiredEncounterFieldCount
		+ Config.AdditionalRoadFieldCount;
	const int32 ExpectedSlotsPerField =
		FABTSM3MonthlySlingshotFieldBuilder::
			BaseSlotsPerOrdinaryField
		+ Config.AdditionalSlotsPerOrdinaryField;
	bool bValid = Test.TestEqual(
		TEXT("Field count"),
		FieldCandidate.Fields.Num(),
		ExpectedFieldCount);
	bValid &= Test.TestEqual(
		TEXT("Slot count"),
		FieldCandidate.TotalSlotCount,
		ExpectedFieldCount * ExpectedSlotsPerField);
	bValid &= Test.TestEqual(
		TEXT("Source spatial identity"),
		FieldCandidate.SourceSpatialCandidateHash,
		SpatialCandidate.SpatialCandidateHash);
	bValid &= Test.TestEqual(
		TEXT("Candidate hash"),
		static_cast<uint64>(FieldCandidate.CandidateHash),
		FABTSM3MonthlySlingshotFieldBuilder::
			ComputeCandidateHash(FieldCandidate));

	TSet<int32> UsedCells;
	TSet<int32> RoadCells;
	TArray<int32> ReservedProgressCM;
	int32 LastRoadFieldProgressCM = INDEX_NONE;
	for (const int32 CellId :
		SpatialCandidate.RecomputedRoute.OrderedRoadCellIds)
	{
		RoadCells.Add(CellId);
	}
	for (int32 FieldIndex = 0;
		FieldIndex < FieldCandidate.Fields.Num();
		++FieldIndex)
	{
		const FABTSM3MonthlySlingshotField& Field =
			FieldCandidate.Fields[FieldIndex];
		bValid &= Test.TestEqual(
			TEXT("Per-field slot count"),
			Field.SlotCellIds.Num(),
			ExpectedSlotsPerField);
		bValid &= Test.TestEqual(
			TEXT("Anchor-first ordering"),
			Field.SlotCellIds[0],
			Field.AnchorCellId);
		bValid &= Test.TestEqual(
			TEXT("Field hash"),
			static_cast<uint64>(Field.FieldHash),
			FABTSM3MonthlySlingshotFieldBuilder::
				ComputeFieldHash(Field));
		bValid &= Test.TestTrue(
			TEXT("Field has a distance-reachable star"),
			Field.DistanceReachablePairCount
				>= Field.SlotCellIds.Num() - 1);
		if (FieldIndex
			< FABTSM3MonthlySlingshotFieldBuilder::
				RequiredEncounterFieldCount)
		{
			bValid &= Test.TestEqual(
				TEXT("Required field kind"),
				Field.Kind,
				EABTSM3MonthlySlingshotFieldKind::
					EncounterRequired);
			const FABTSM3MonthlySpatialEncounter&
				Encounter =
					SpatialCandidate.Encounters[FieldIndex];
			const FABTSM3PocketContract* Pocket =
				FindPocket(
					SpatialCandidate,
					Encounter.Contract.SlingshotPocketId);
			bValid &= Test.TestNotNull(
				TEXT("Required pocket"), Pocket);
			if (Pocket != nullptr)
			{
				bValid &= Test.TestEqual(
					TEXT("Required search anchor preserved"),
					Field.SourcePocketAnchorCellId,
					Pocket->AnchorCellId);
			}
			bValid &= Test.TestEqual(
				TEXT("Required encounter identity"),
				Field.EncounterId,
				Encounter.Contract.EncounterId);
			const int32 AnchorRouteIndex =
				SpatialCandidate.Cells[Field.AnchorCellId].
					NearestRoadOrderIndex;
			bValid &= Test.TestTrue(
				TEXT("Required anchor has road progress"),
				SpatialCandidate.RecomputedRoute.
					ProgressDistanceCM.IsValidIndex(
						AnchorRouteIndex));
			if (SpatialCandidate.RecomputedRoute.
					ProgressDistanceCM.IsValidIndex(
						AnchorRouteIndex))
			{
				ReservedProgressCM.Add(
					SpatialCandidate.RecomputedRoute.
						ProgressDistanceCM[AnchorRouteIndex]);
			}
		}
		else
		{
			bValid &= Test.TestEqual(
				TEXT("Road field kind"),
				Field.Kind,
				EABTSM3MonthlySlingshotFieldKind::
					RoadAuxiliary);
			bValid &= Test.TestEqual(
				TEXT("Road field has no encounter owner"),
				Field.EncounterId,
				INDEX_NONE);
			bValid &= Test.TestEqual(
				TEXT("Road field has no pocket anchor"),
				Field.SourcePocketAnchorCellId,
				INDEX_NONE);
			const int32 AnchorRouteIndex =
				SpatialCandidate.Cells[Field.AnchorCellId].
					NearestRoadOrderIndex;
			const bool bAnchorProgressValid =
				SpatialCandidate.RecomputedRoute.
					ProgressDistanceCM.IsValidIndex(
						AnchorRouteIndex);
			bValid &= Test.TestTrue(
				TEXT("Road anchor has road progress"),
				bAnchorProgressValid);
			if (bAnchorProgressValid)
			{
				const int32 ActualProgressCM =
					SpatialCandidate.RecomputedRoute.
						ProgressDistanceCM[AnchorRouteIndex];
				bValid &= Test.TestEqual(
					TEXT("Road FlowQ uses final anchor progress"),
					Field.FlowQ,
					static_cast<int32>(
						static_cast<int64>(ActualProgressCM)
							* FlowQuantization
							/ SpatialCandidate.RecomputedRoute.
								Metrics.RouteLengthCM));
				bValid &= Test.TestTrue(
					TEXT("Road anchor keeps endpoint clearance"),
					ActualProgressCM >= RoadEndClearanceCM
					&& ActualProgressCM
						<= SpatialCandidate.RecomputedRoute.
								Metrics.RouteLengthCM
							- RoadEndClearanceCM);
				for (const int32 ReservedCM :
					ReservedProgressCM)
				{
					bValid &= Test.TestTrue(
						TEXT("Road anchor keeps field progress clearance"),
						FMath::Abs(
							ActualProgressCM - ReservedCM)
							>= RoadFieldProgressClearanceCM);
				}
				if (LastRoadFieldProgressCM != INDEX_NONE)
				{
					bValid &= Test.TestTrue(
						TEXT("Road fields follow increasing actual progress"),
						ActualProgressCM
							> LastRoadFieldProgressCM
								+ RoadFieldProgressClearanceCM);
				}
				ReservedProgressCM.Add(ActualProgressCM);
				LastRoadFieldProgressCM = ActualProgressCM;
			}
		}
		for (const int32 CellId : Field.SlotCellIds)
		{
			bValid &= Test.TestTrue(
				TEXT("Slot cell unique"),
				!UsedCells.Contains(CellId));
			UsedCells.Add(CellId);
			bValid &= Test.TestTrue(
				TEXT("Slot does not occupy road"),
				!RoadCells.Contains(CellId));
			bValid &= Test.TestTrue(
				TEXT("Slot cell identity valid"),
				SpatialCandidate.Cells.IsValidIndex(CellId)
				&& SpatialCandidate.Cells[CellId].CellId
					== CellId);
			if (SpatialCandidate.Cells.IsValidIndex(CellId))
			{
				const FABTSM3MonthlySpatialCell& Cell =
					SpatialCandidate.Cells[CellId];
				bValid &= Test.TestTrue(
					TEXT("Slot is dry and outside target exclusion"),
					!Cell.bWater
					&& !Cell.bTargetFootprint
					&& (Field.Kind
							== EABTSM3MonthlySlingshotFieldKind::
								EncounterRequired
						|| !Cell.bNoRoad));
			}
			bValid &= Test.TestTrue(
				TEXT("Every slot can reach its anchor"),
				ChordDistanceCM(
					Field.AnchorCellId,
					CellId)
				<= Config.MaxCordLengthCM + 0.01f);
		}
	}
	return bValid;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R31SlingshotFieldDefaultsAndDisplayTest,
	"ABTS.M3.Monthly.SlotField.01DefaultsAndDisplay",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R31SlingshotFieldDefaultsAndDisplayTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R31SlingshotFieldTests;
	FString R31ManifestFailure;
	const bool bR31ManifestValid =
		FABTSM3R31AcceptanceManifest::Validate(
			R31ManifestFailure);
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M3R3.1][ManifestCalibration] Valid=%d Failure=%s Manifest=%016llX SeedManifest=%016llX"),
		bR31ManifestValid ? 1 : 0,
		*R31ManifestFailure,
		static_cast<unsigned long long>(
			FABTSM3R31AcceptanceManifest::
				ComputeManifestHash()),
		static_cast<unsigned long long>(
			FABTSM3R31AcceptanceManifest::
				ComputeSweepSeedManifestHash()));
	TestTrue(
		TEXT("R3.1 acceptance manifest valid"),
		bR31ManifestValid);
	FString ManifestFailure;
	TestTrue(
		TEXT("Frozen R3 manifest remains valid"),
		FABTSM3R3AcceptanceManifest::Validate(ManifestFailure));
	TestEqual(
		TEXT("Frozen R3 manifest identity is unchanged"),
		FABTSM3R3AcceptanceManifest::ComputeManifestHash(),
		FABTSM3R3AcceptanceManifest::FrozenManifestHash);
	const FABTSM3MonthlySlingshotFieldConfig Config =
		MakeFieldConfig();
	TestEqual(
		TEXT("Default fields"),
		Config.AdditionalRoadFieldCount,
		2);
	TestEqual(
		TEXT("Default slots per field"),
		FABTSM3MonthlySlingshotFieldBuilder::
			BaseSlotsPerOrdinaryField
			+ Config.AdditionalSlotsPerOrdinaryField,
		DefaultSlotsPerField);
	TestEqual(
		TEXT("Default maximum cord length"),
		Config.MaxCordLengthCM,
		1200);

	FABTSM3MonthlyRoutePool RoutePool;
	FABTSM3MonthlySpatialResult SpatialResult;
	FABTSM3MonthlySlingshotFieldResult FieldResult;
	FString Failure;
	const bool bBuilt = BuildFieldFixture(
		DisplaySeed,
		Config,
		RoutePool,
		SpatialResult,
		FieldResult,
		Failure);
	TestTrue(FString::Printf(
		TEXT("Display build: %s"), *Failure), bBuilt);
	if (!bBuilt)
	{
		return false;
	}
	TestEqual(
		TEXT("R3 display result identity is unchanged"),
		static_cast<uint64>(SpatialResult.SpatialResultHash),
		FABTSM3R3AcceptanceManifest::FrozenDisplayResultHash);
	TestTrue(
		TEXT("Field result valid"),
		FieldResult.bSlingshotFieldResultValid);
	TestFalse(
		TEXT("R3.1 cannot publish monthly world"),
		FieldResult.bMonthlyWorldAccepted);
	TestEqual(
		TEXT("Fields per candidate"),
		FieldResult.FieldsPerCandidate,
		DefaultFieldCount);
	TestEqual(
		TEXT("Slots per candidate"),
		FieldResult.SlotsPerCandidate,
		DefaultFieldCount * DefaultSlotsPerField);
	TestEqual(
		TEXT("Alternative count follows R3"),
		FieldResult.RetainedCandidates.Num(),
		SpatialResult.RetainedCandidates.Num());
	TestEqual(
		TEXT("Result hash"),
		static_cast<uint64>(FieldResult.ResultHash),
		FABTSM3MonthlySlingshotFieldBuilder::
			ComputeResultHash(FieldResult));
	TestEqual(
		TEXT("Frozen display config identity"),
		static_cast<uint64>(FieldResult.ConfigHash),
		FABTSM3R31AcceptanceManifest::
			FrozenDisplayConfigHash);
	TestEqual(
		TEXT("Frozen display result identity"),
		static_cast<uint64>(FieldResult.ResultHash),
		FABTSM3R31AcceptanceManifest::
			FrozenDisplayResultHash);
	TestEqual(
		TEXT("Frozen display candidate identity"),
		static_cast<uint64>(
			FieldResult.RetainedCandidates[0].CandidateHash),
		FABTSM3R31AcceptanceManifest::
			FrozenDisplayCandidateHash);
	EABTSM3MonthlySlingshotFieldRejectReason Reason =
		EABTSM3MonthlySlingshotFieldRejectReason::None;
	TestTrue(
		TEXT("Display result validates"),
		FABTSM3MonthlySlingshotFieldBuilder::Validate(
			Config,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			SpatialResult,
			FieldResult,
			Reason,
			Failure));
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M3R3.1][DisplayOracle] Seed=%d SourceR3=%016llX Config=%016llX Result=%016llX Candidate=%016llX Fields=%d Slots=%d"),
		DisplaySeed,
		static_cast<unsigned long long>(
			static_cast<uint64>(
				FieldResult.SourceSpatialResultHash)),
		static_cast<unsigned long long>(
			static_cast<uint64>(FieldResult.ConfigHash)),
		static_cast<unsigned long long>(
			static_cast<uint64>(FieldResult.ResultHash)),
		static_cast<unsigned long long>(
			static_cast<uint64>(
				FieldResult.RetainedCandidates[0].
					CandidateHash)),
		FieldResult.FieldsPerCandidate,
		FieldResult.SlotsPerCandidate);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R31SlingshotFieldEncounterContractsTest,
	"ABTS.M3.Monthly.SlotField.02EncounterContracts",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R31SlingshotFieldEncounterContractsTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R31SlingshotFieldTests;
	const FABTSM3MonthlySlingshotFieldConfig Config =
		MakeFieldConfig();
	FABTSM3MonthlyRoutePool RoutePool;
	FABTSM3MonthlySpatialResult SpatialResult;
	FABTSM3MonthlySlingshotFieldResult FieldResult;
	FString Failure;
	if (!BuildFieldFixture(
			DisplaySeed,
			Config,
			RoutePool,
			SpatialResult,
			FieldResult,
			Failure))
	{
		AddError(FString::Printf(
			TEXT("Fixture failed: %s"), *Failure));
		return false;
	}
	bool bValid = true;
	for (int32 CandidateIndex = 0;
		CandidateIndex < FieldResult.RetainedCandidates.Num();
		++CandidateIndex)
	{
		bValid &= ValidateCanonicalCandidate(
			*this,
			Config,
			SpatialResult.RetainedCandidates[CandidateIndex],
			FieldResult.RetainedCandidates[CandidateIndex]);
	}
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R31SlingshotFieldRoadCountParametersTest,
	"ABTS.M3.Monthly.SlotField.03RoadCountParameters",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R31SlingshotFieldRoadCountParametersTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R31SlingshotFieldTests;
	FABTSM3MonthlyRoutePool RoutePool;
	FABTSM3MonthlySpatialResult SpatialResult;
	FString Failure;
	if (!BuildSpatialFixture(
			DisplaySeed,
			RoutePool,
			SpatialResult,
			Failure))
	{
		AddError(Failure);
		return false;
	}
	for (const int32 RoadCount : {0, 1, 3})
	{
		FABTSM3MonthlySlingshotFieldConfig Config =
			MakeFieldConfig();
		Config.AdditionalRoadFieldCount = RoadCount;
		FABTSM3MonthlySlingshotFieldResult Result;
		const bool bBuilt =
			FABTSM3MonthlySlingshotFieldBuilder::Build(
				DisplaySeed,
				Config,
				GetLogicalCells(),
				ReferencePlanetRadiusCM,
				SpatialResult,
				Result,
				Failure);
		TestTrue(FString::Printf(
			TEXT("RoadCount=%d build: %s"),
			RoadCount,
			*Failure), bBuilt);
		if (bBuilt)
		{
			TestEqual(
				TEXT("Parameterized field count"),
				Result.FieldsPerCandidate,
				6 + RoadCount);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R31SlingshotFieldDistanceGeometryTest,
	"ABTS.M3.Monthly.SlotField.04DistanceGeometryAndConflicts",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R31SlingshotFieldDistanceGeometryTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R31SlingshotFieldTests;
	FABTSM3MonthlyRoutePool RoutePool;
	FABTSM3MonthlySpatialResult SpatialResult;
	FABTSM3MonthlySlingshotFieldResult Result;
	FString Failure;
	const FABTSM3MonthlySlingshotFieldConfig Config =
		MakeFieldConfig();
	if (!BuildFieldFixture(
			DisplaySeed,
			Config,
			RoutePool,
			SpatialResult,
			Result,
			Failure))
	{
		AddError(Failure);
		return false;
	}
	for (const FABTSM3MonthlySlingshotFieldCandidate& Candidate :
		Result.RetainedCandidates)
	{
		TSet<int32> Cells;
		TSet<int32> Fields;
		for (const FABTSM3MonthlySlingshotField& Field :
			Candidate.Fields)
		{
			TestTrue(
				TEXT("Field identity unique"),
				!Fields.Contains(Field.FieldId));
			Fields.Add(Field.FieldId);
			int32 PairCount = 0;
			for (int32 A = 0; A < Field.SlotCellIds.Num(); ++A)
			{
				TestTrue(
					TEXT("Slot identity unique across fields"),
					!Cells.Contains(Field.SlotCellIds[A]));
				Cells.Add(Field.SlotCellIds[A]);
				for (int32 B = A + 1;
					B < Field.SlotCellIds.Num();
					++B)
				{
					PairCount +=
						ChordDistanceCM(
							Field.SlotCellIds[A],
							Field.SlotCellIds[B])
							<= Config.MaxCordLengthCM
						? 1
						: 0;
				}
			}
			TestEqual(
				TEXT("Distance-only pair diagnostic"),
				Field.DistanceReachablePairCount,
				PairCount);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R31SlingshotFieldZeroAndBoundaryTest,
	"ABTS.M3.Monthly.SlotField.05ZeroAndBoundary",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R31SlingshotFieldZeroAndBoundaryTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R31SlingshotFieldTests;
	FABTSM3MonthlyRoutePool RoutePool;
	FABTSM3MonthlySpatialResult SpatialResult;
	FString Failure;
	if (!BuildSpatialFixture(
			DisplaySeed,
			RoutePool,
			SpatialResult,
			Failure))
	{
		AddError(Failure);
		return false;
	}
	FABTSM3MonthlySlingshotFieldConfig ZeroConfig =
		MakeFieldConfig();
	ZeroConfig.AdditionalSlotsPerOrdinaryField = 0;
	ZeroConfig.AdditionalRoadFieldCount = 0;
	FABTSM3MonthlySlingshotFieldResult ZeroResult;
	const bool bZeroBuilt = TestTrue(
		TEXT("Zero-additional configuration builds"),
		FABTSM3MonthlySlingshotFieldBuilder::Build(
			DisplaySeed,
			ZeroConfig,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			SpatialResult,
			ZeroResult,
			Failure));
	if (bZeroBuilt)
	{
		TestEqual(
			TEXT("Zero-additional keeps two base slots"),
			ZeroResult.SlotsPerCandidate,
			6 * FABTSM3MonthlySlingshotFieldBuilder::
				BaseSlotsPerOrdinaryField);
	}

	FABTSM3MonthlySlingshotFieldConfig MaxConfig =
		MakeFieldConfig();
	MaxConfig.AdditionalSlotsPerOrdinaryField = 10;
	MaxConfig.AdditionalRoadFieldCount = 12;
	MaxConfig.MaxCordLengthCM = 4000;
	FABTSM3MonthlySlingshotFieldResult MaxResult;
	const bool bMaxBuilt = TestTrue(
		TEXT("Inclusive maximum configuration builds"),
		FABTSM3MonthlySlingshotFieldBuilder::Build(
			DisplaySeed,
			MaxConfig,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			SpatialResult,
			MaxResult,
			Failure));
	if (bMaxBuilt)
	{
		TestEqual(
			TEXT("Maximum field count"),
			MaxResult.FieldsPerCandidate,
			6 + MaxConfig.AdditionalRoadFieldCount);
		TestEqual(
			TEXT("Maximum slot count"),
			MaxResult.SlotsPerCandidate,
			MaxResult.FieldsPerCandidate
				* (FABTSM3MonthlySlingshotFieldBuilder::
					BaseSlotsPerOrdinaryField
					+ MaxConfig.
						AdditionalSlotsPerOrdinaryField));
	}

	FABTSM3MonthlySlingshotFieldConfig DisabledConfig =
		MakeFieldConfig();
	DisabledConfig.bBuildSlingshotFields = false;
	FABTSM3MonthlySlingshotFieldResult DisabledResult;
	TestTrue(
		TEXT("Disabled observation is a valid no-op"),
		FABTSM3MonthlySlingshotFieldBuilder::Build(
			DisplaySeed,
			DisabledConfig,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			SpatialResult,
			DisabledResult,
			Failure));
	TestTrue(
		TEXT("Disabled result valid"),
		DisabledResult.bSlingshotFieldResultValid);
	TestEqual(
		TEXT("Disabled result not evaluated"),
		DisabledResult.RejectReason,
		EABTSM3MonthlySlingshotFieldRejectReason::
			NotEvaluated);
	TestTrue(
		TEXT("Disabled result has no candidates"),
		DisabledResult.RetainedCandidates.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R31SlingshotFieldDeterminismTamperTest,
	"ABTS.M3.Monthly.SlotField.06DeterminismAndTamper",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R31SlingshotFieldDeterminismTamperTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R31SlingshotFieldTests;
	FABTSM3MonthlyRoutePool RoutePool;
	FABTSM3MonthlySpatialResult SpatialResult;
	FString Failure;
	if (!BuildSpatialFixture(
			DisplaySeed,
			RoutePool,
			SpatialResult,
			Failure))
	{
		AddError(Failure);
		return false;
	}
	const FABTSM3MonthlySlingshotFieldConfig Config =
		MakeFieldConfig();
	FABTSM3MonthlySlingshotFieldResult First;
	FABTSM3MonthlySlingshotFieldResult Second;
	const bool bFirstBuilt = TestTrue(
		TEXT("First build"),
		FABTSM3MonthlySlingshotFieldBuilder::Build(
			DisplaySeed,
			Config,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			SpatialResult,
			First,
			Failure));
	const bool bSecondBuilt = TestTrue(
		TEXT("Second build"),
		FABTSM3MonthlySlingshotFieldBuilder::Build(
			DisplaySeed,
			Config,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			SpatialResult,
			Second,
			Failure));
	if (!bFirstBuilt || !bSecondBuilt)
	{
		return false;
	}
	TestTrue(
		TEXT("Whole result deterministic"),
		ResultsEqual(First, Second));

	FABTSM3MonthlySlingshotFieldResult Tampered = First;
	Tampered.RetainedCandidates[0].Fields[0].
		SlotCellIds[1] =
			Tampered.RetainedCandidates[0].Fields[1].
				SlotCellIds[1];
	Tampered.RetainedCandidates[0].Fields[0].FieldHash =
		static_cast<int64>(
			FABTSM3MonthlySlingshotFieldBuilder::
				ComputeFieldHash(
					Tampered.RetainedCandidates[0].
						Fields[0]));
	Tampered.RetainedCandidates[0].CandidateHash =
		static_cast<int64>(
			FABTSM3MonthlySlingshotFieldBuilder::
				ComputeCandidateHash(
					Tampered.RetainedCandidates[0]));
	Tampered.ResultHash = static_cast<int64>(
		FABTSM3MonthlySlingshotFieldBuilder::
			ComputeResultHash(Tampered));
	EABTSM3MonthlySlingshotFieldRejectReason Reason =
		EABTSM3MonthlySlingshotFieldRejectReason::None;
	TestFalse(
		TEXT("Re-signed structural tamper is rejected"),
		FABTSM3MonthlySlingshotFieldBuilder::Validate(
			Config,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			SpatialResult,
			Tampered,
			Reason,
			Failure));
	TestEqual(
		TEXT("Tamper reason"),
		Reason,
		EABTSM3MonthlySlingshotFieldRejectReason::
			HashMismatch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R31SlingshotFieldSweep100Test,
	"ABTS.M3.Monthly.SlotField.07Sweep100",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R31SlingshotFieldSweep100Test::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R31SlingshotFieldTests;
	const FABTSM3MonthlySlingshotFieldConfig Config =
		MakeFieldConfig();
	uint64 OracleHash = 14695981039346656037ull;
	int32 Accepted = 0;
	TArray<double> DurationsMS;
	for (int32 Index = 0; Index < SweepSeedCount; ++Index)
	{
		const int32 Seed = Index == 0 ? DisplaySeed : Index - 1;
		FABTSM3MonthlyRoutePool RoutePool;
		FABTSM3MonthlySpatialResult SpatialResult;
		FABTSM3MonthlySlingshotFieldResult Result;
		FString Failure;
		const double StartSeconds = FPlatformTime::Seconds();
		const bool bBuilt = BuildFieldFixture(
			Seed,
			Config,
			RoutePool,
			SpatialResult,
			Result,
			Failure);
		DurationsMS.Add(
			(FPlatformTime::Seconds() - StartSeconds) * 1000.0);
		if (!bBuilt)
		{
			AddError(FString::Printf(
				TEXT("Seed %d failed: %s"),
				Seed,
				*Failure));
			continue;
		}
		++Accepted;
		OracleHash = MixOracle(
			OracleHash,
			static_cast<uint32>(Seed));
		OracleHash = MixOracle(
			OracleHash,
			static_cast<uint64>(Result.ResultHash));
		OracleHash = MixOracle(
			OracleHash,
			static_cast<uint64>(
				Result.RetainedCandidates[0].
					CandidateHash));
		OracleHash = MixOracle(
			OracleHash,
			static_cast<uint32>(Result.FieldsPerCandidate));
		OracleHash = MixOracle(
			OracleHash,
			static_cast<uint32>(Result.SlotsPerCandidate));
	}
	DurationsMS.Sort();
	const double P95MS = DurationsMS[
		FMath::Clamp(
			FMath::CeilToInt(
				DurationsMS.Num() * 0.95) - 1,
			0,
			DurationsMS.Num() - 1)];
	const double MaxMS =
		DurationsMS.IsEmpty() ? 0.0 : DurationsMS.Last();
	TestEqual(
		TEXT("Sweep terminal count"),
		DurationsMS.Num(),
		SweepSeedCount);
	TestEqual(
		TEXT("Sweep accepted count"),
		Accepted,
		SweepSeedCount);
	TestEqual(
		TEXT("Frozen sweep oracle identity"),
		OracleHash,
		FABTSM3R31AcceptanceManifest::
			FrozenSweepOracleHash);
	TestTrue(TEXT("Sweep p95 under 1000ms"), P95MS < 1000.0);
	TestTrue(TEXT("Sweep max under 2500ms"), MaxMS < 2500.0);
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M3R3.1][SlotFieldSweep] Terminal=%d Accepted=%d Rejected=%d P95MS=%.3f MaxMS=%.3f OracleHash=%016llX"),
		DurationsMS.Num(),
		Accepted,
		DurationsMS.Num() - Accepted,
		P95MS,
		MaxMS,
		static_cast<unsigned long long>(OracleHash));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R31SlingshotFieldInvalidConfigTest,
	"ABTS.M3.Monthly.SlotFieldFailure.InvalidConfig",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R31SlingshotFieldInvalidConfigTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R31SlingshotFieldTests;
	FABTSM3MonthlyRoutePool RoutePool;
	FABTSM3MonthlySpatialResult SpatialResult;
	FString Failure;
	if (!BuildSpatialFixture(
			DisplaySeed,
			RoutePool,
			SpatialResult,
			Failure))
	{
		AddError(Failure);
		return false;
	}
	TArray<FABTSM3MonthlySlingshotFieldConfig> InvalidConfigs;
	FABTSM3MonthlySlingshotFieldConfig Invalid =
		MakeFieldConfig();
	Invalid.AdditionalSlotsPerOrdinaryField = -1;
	InvalidConfigs.Add(Invalid);
	Invalid = MakeFieldConfig();
	Invalid.AdditionalSlotsPerOrdinaryField = 11;
	InvalidConfigs.Add(Invalid);
	Invalid = MakeFieldConfig();
	Invalid.AdditionalRoadFieldCount = -1;
	InvalidConfigs.Add(Invalid);
	Invalid = MakeFieldConfig();
	Invalid.AdditionalRoadFieldCount = 13;
	InvalidConfigs.Add(Invalid);
	Invalid = MakeFieldConfig();
	Invalid.MaxCordLengthCM = 99;
	InvalidConfigs.Add(Invalid);
	Invalid = MakeFieldConfig();
	Invalid.MaxCordLengthCM = 4001;
	InvalidConfigs.Add(Invalid);
	for (const FABTSM3MonthlySlingshotFieldConfig& Config :
		InvalidConfigs)
	{
		FABTSM3MonthlySlingshotFieldResult Result;
		TestFalse(
			TEXT("Invalid config rejected"),
			FABTSM3MonthlySlingshotFieldBuilder::Build(
				DisplaySeed,
				Config,
				GetLogicalCells(),
				ReferencePlanetRadiusCM,
				SpatialResult,
				Result,
				Failure));
		TestEqual(
			TEXT("Invalid config reason"),
			Result.RejectReason,
			EABTSM3MonthlySlingshotFieldRejectReason::
				InvalidConfig);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R31SlingshotFieldCapacityFailureTest,
	"ABTS.M3.Monthly.SlotFieldFailure.Capacity",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R31SlingshotFieldCapacityFailureTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R31SlingshotFieldTests;
	FABTSM3MonthlyRoutePool RoutePool;
	FABTSM3MonthlySpatialResult SpatialResult;
	FString Failure;
	if (!BuildSpatialFixture(
			DisplaySeed,
			RoutePool,
			SpatialResult,
			Failure))
	{
		AddError(Failure);
		return false;
	}
	FABTSM3MonthlySlingshotFieldConfig Config =
		MakeFieldConfig();
	Config.MaxCordLengthCM = 100;
	FABTSM3MonthlySlingshotFieldResult Result;
	TestFalse(
		TEXT("Insufficient distance capacity fails closed"),
		FABTSM3MonthlySlingshotFieldBuilder::Build(
			DisplaySeed,
			Config,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			SpatialResult,
			Result,
			Failure));
	TestEqual(
		TEXT("Capacity failure reason"),
		Result.RejectReason,
		EABTSM3MonthlySlingshotFieldRejectReason::
			FieldGenerationFailed);
	TestTrue(
		TEXT("Rejected result publishes no partial candidates"),
		Result.RetainedCandidates.IsEmpty());
	return true;
}

#endif
