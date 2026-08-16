// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PCG/ABTSM3JuryFixedSixLayout.h"
#include "Planet/ABTSM2Planet.h"

namespace ABTSM3JuryFixedSixTests
{
void BuildSyntheticJurySource(
	TArray<FABTSM2Cell>& OutCells,
	FABTSM3MonthlySpatialResult& OutSpatial)
{
	constexpr int32 Count = FABTSM3JuryFixedSixLayoutBuilder::ExpectedEncounterCount;
	OutCells.SetNum(Count * 2);
	OutSpatial = FABTSM3MonthlySpatialResult();
	OutSpatial.WorldSeed = FABTSM3JuryFixedSixLayoutBuilder::FrozenWorldSeed;
	OutSpatial.SpatialResultHash = static_cast<int64>(
		FABTSM3JuryFixedSixLayoutBuilder::FrozenSourceSpatialResultHash);
	OutSpatial.bSpatialResultValid = true;

	FABTSM3MonthlySpatialCandidate Candidate;
	Candidate.SourceRouteCandidateId =
		FABTSM3JuryFixedSixLayoutBuilder::FrozenSourceCandidateId;
	Candidate.SpatialCandidateHash = static_cast<int64>(
		FABTSM3JuryFixedSixLayoutBuilder::FrozenSourceSpatialCandidateHash);
	Candidate.bHardPass = true;
	Candidate.RejectReason = EABTSM3MonthlySpatialRejectReason::None;
	Candidate.Cells.SetNum(OutCells.Num());

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const double Angle = 2.0 * PI * Index / Count;
		const double SlingshotAngle = Angle + FMath::DegreesToRadians(8.0);
		OutCells[Index].UnitCenter = FVector(
			FMath::Cos(Angle),
			FMath::Sin(Angle),
			0.0);
		OutCells[Count + Index].UnitCenter = FVector(
			FMath::Cos(SlingshotAngle),
			FMath::Sin(SlingshotAngle),
			0.0);

		Candidate.Cells[Index].CellId = Index;
		Candidate.Cells[Index].bNoRoad = true;
		Candidate.Cells[Count + Index].CellId = Count + Index;

		FABTSM3PocketContract SlingshotPocket;
		SlingshotPocket.PocketId = 7000 + Index;
		SlingshotPocket.EncounterId = 7100 + Index;
		SlingshotPocket.Role = EABTSM3PocketRole::Slingshot;
		SlingshotPocket.AnchorCellId = Count + Index;
		Candidate.Pockets.Add(SlingshotPocket);

		FABTSM3MonthlySpatialEncounter Encounter;
		Encounter.Contract.EncounterId = 7100 + Index;
		Encounter.Contract.SlingshotPocketId = SlingshotPocket.PocketId;
		Encounter.TargetAnchorCellId = Index;
		Encounter.TargetFootprintCellIds.Add(Index);
		Encounter.TargetNoRoadCellIds.Add(Index);
		Candidate.Encounters.Add(Encounter);
	}
	OutSpatial.RetainedCandidates.Add(MoveTemp(Candidate));
}

int32 AddSyntheticDynamicEnvelopeCell(
	TArray<FABTSM2Cell>& Cells,
	FABTSM3MonthlySpatialResult& Spatial,
	const int32 EncounterIndex)
{
	constexpr float PlanetRadiusCM = 50000.0f;
	constexpr int32 Count =
		FABTSM3JuryFixedSixLayoutBuilder::ExpectedEncounterCount;
	const TConstArrayView<FABTSM3JuryBuildingPlacementFixture> Fixtures =
		FABTSM3JuryFixedSixLayoutBuilder::GetFrozenPlacementFixtures();
	check(Cells.IsValidIndex(EncounterIndex));
	check(Cells.IsValidIndex(Count + EncounterIndex));
	check(Fixtures.IsValidIndex(EncounterIndex));
	check(!Spatial.RetainedCandidates.IsEmpty());

	const FVector Up = Cells[EncounterIndex].UnitCenter.GetSafeNormal();
	FVector Forward = FVector::VectorPlaneProject(
		Cells[Count + EncounterIndex].UnitCenter,
		Up).GetSafeNormal();
	const FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
	Forward = FVector::CrossProduct(Right, Up).GetSafeNormal();
	const FBox& EffectBounds = Fixtures[EncounterIndex].EffectBounds;
	const FVector DynamicCornerDirection = (
		Up * PlanetRadiusCM
			+ Forward * EffectBounds.Min.X
			+ Right * EffectBounds.Min.Y).GetSafeNormal();

	FABTSM2Cell& AddedCell = Cells.AddDefaulted_GetRef();
	AddedCell.UnitCenter = DynamicCornerDirection;
	FABTSM3MonthlySpatialCell& AddedState =
		Spatial.RetainedCandidates[0].Cells.AddDefaulted_GetRef();
	AddedState.CellId = Cells.Num() - 1;
	return AddedState.CellId;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3JuryFixedSixDescriptorIdentityTest,
	"ABTS.M3.Monthly.JuryFixedSix.01FrozenDescriptorIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM3JuryFixedSixDescriptorIdentityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TConstArrayView<FABTSM3JuryBuildingPlacementFixture> Fixtures =
		FABTSM3JuryFixedSixLayoutBuilder::GetFrozenPlacementFixtures();
	TestEqual(TEXT("exact six descriptors"), Fixtures.Num(), 6);
	TestEqual(TEXT("source manifest version"),
		FABTSM3JuryFixedSixLayoutBuilder::M7SourceManifestVersion, 1);
	TestEqual(TEXT("source manifest hash"),
		FABTSM3JuryFixedSixLayoutBuilder::M7SourceManifestHash, 2324068295ll);
	TestEqual(TEXT("V2 fixed-six contract version"),
		FABTSM3JuryFixedSixLayoutBuilder::FixedSixContractVersion, 2);
	TestEqual(TEXT("V2 placement catalog hash"),
		FABTSM3JuryFixedSixLayoutBuilder::M7PlacementCatalogHash,
		11501529584318250152ull);
	TestTrue(TEXT("fixture catalog hash is non-zero"),
		FABTSM3JuryFixedSixLayoutBuilder::ComputeFixtureCatalogHash() != 0);

	TSet<FName> EntryIds;
	TSet<FName> StableIds;
	static constexpr uint64 ExpectedDescriptorHashes[] = {
		10113758205408230493ull, 1108134973396587699ull,
		17683520519518435068ull, 11089610541129920709ull,
		7322844578368466709ull, 3963542007450344969ull
	};
	static constexpr uint64 ExpectedStaticGeometryHashes[] = {
		10276011350224018878ull, 1243337162086650128ull,
		3075258440093988143ull, 4328116049969586954ull,
		461929562625370845ull, 6610608065286482828ull
	};
	static constexpr uint64 ExpectedProductionIdentityHashes[] = {
		6524532268529485689ull, 3864694895529971157ull,
		15118401498293854757ull, 3596567542130940914ull,
		12062404675177644267ull, 10510335516369342439ull
	};
	static constexpr uint64 ExpectedDeviceAssemblyHashes[] = {
		12560907909080588493ull, 1033929311817437759ull,
		6073774060920401162ull, 3035395675580472088ull,
		9042370151666144586ull, 1309116746468502251ull
	};
	for (int32 Index = 0; Index < Fixtures.Num(); ++Index)
	{
		const FABTSM3JuryBuildingPlacementFixture& Fixture = Fixtures[Index];
		EntryIds.Add(Fixture.ManifestEntryId);
		StableIds.Add(Fixture.StableId);
		TestEqual(TEXT("difficulty follows encounter order"),
			Fixture.DifficultyTier, Index);
		TestTrue(TEXT("valid local bounds"), Fixture.LocalBounds.IsValid != 0);
		TestTrue(TEXT("valid physical bounds"), Fixture.PhysicalBounds.IsValid != 0);
		TestTrue(TEXT("physical min matches local bounds"),
			Fixture.PhysicalBounds.Min.Equals(Fixture.LocalBounds.Min));
		TestTrue(TEXT("physical max matches local bounds"),
			Fixture.PhysicalBounds.Max.Equals(Fixture.LocalBounds.Max));
		TestTrue(TEXT("valid effect bounds"), Fixture.EffectBounds.IsValid != 0);
		TestTrue(TEXT("positive pad X"), Fixture.RequiredPadHalfExtentCM.X > 0.0);
		TestTrue(TEXT("positive pad Y"), Fixture.RequiredPadHalfExtentCM.Y > 0.0);
		TestEqual(TEXT("exact 36 cm physical pad margin X"),
			Fixture.RequiredPadHalfExtentCM.X
				- FMath::Max(FMath::Abs(Fixture.PhysicalBounds.Min.X),
					FMath::Abs(Fixture.PhysicalBounds.Max.X)),
			36.0);
		TestEqual(TEXT("exact 36 cm physical pad margin Y"),
			Fixture.RequiredPadHalfExtentCM.Y
				- FMath::Max(FMath::Abs(Fixture.PhysicalBounds.Min.Y),
					FMath::Abs(Fixture.PhysicalBounds.Max.Y)),
			36.0);
		TestNotEqual(TEXT("static geometry hash"), Fixture.StaticGeometryHash, int64(0));
		TestNotEqual(TEXT("descriptor hash"), Fixture.SourceDescriptorHash, int64(0));
		TestNotEqual(TEXT("production identity hash"),
			Fixture.ProductionIdentityHash, int64(0));
		TestNotEqual(TEXT("device assembly hash"),
			Fixture.DeviceAssemblyHash, int64(0));
		TestTrue(TEXT("dynamic envelope is required"),
			Fixture.bDynamicEnvelopeRequired);
		TestTrue(TEXT("effect envelope exceeds static pad"),
			Fixture.EffectBounds.Min.X < -Fixture.RequiredPadHalfExtentCM.X
				|| Fixture.EffectBounds.Max.X > Fixture.RequiredPadHalfExtentCM.X
				|| Fixture.EffectBounds.Min.Y < -Fixture.RequiredPadHalfExtentCM.Y
				|| Fixture.EffectBounds.Max.Y > Fixture.RequiredPadHalfExtentCM.Y);
		TestEqual(TEXT("V2 descriptor identity"),
			static_cast<uint64>(Fixture.SourceDescriptorHash),
			ExpectedDescriptorHashes[Index]);
		TestEqual(TEXT("V2 static geometry identity"),
			static_cast<uint64>(Fixture.StaticGeometryHash),
			ExpectedStaticGeometryHashes[Index]);
		TestEqual(TEXT("V2 production identity"),
			static_cast<uint64>(Fixture.ProductionIdentityHash),
			ExpectedProductionIdentityHashes[Index]);
		TestEqual(TEXT("V2 device assembly identity"),
			static_cast<uint64>(Fixture.DeviceAssemblyHash),
			ExpectedDeviceAssemblyHashes[Index]);
	}
	TestEqual(TEXT("unique manifest entries"), EntryIds.Num(), 6);
	TestEqual(TEXT("unique stable ids"), StableIds.Num(), 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3JuryFixedSixBuildTest,
	"ABTS.M3.Monthly.JuryFixedSix.02BuildAndFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM3JuryFixedSixBuildTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TArray<FABTSM2Cell> Cells;
	FABTSM3MonthlySpatialResult Source;
	ABTSM3JuryFixedSixTests::BuildSyntheticJurySource(Cells, Source);

	FABTSM3JuryFixedSixLayoutResult First;
	FString Failure;
	TestTrue(TEXT("fixed-six build succeeds"),
		FABTSM3JuryFixedSixLayoutBuilder::Build(
			Cells, 50000.0f, Source, First, Failure));
	TestEqual(TEXT("build failure is empty"), Failure, FString());
	TestTrue(TEXT("placement ready"), First.bPlacementReady);
	TestEqual(TEXT("result is V2"), First.FixedSixContractVersion, 2);
	TestEqual(TEXT("six placements"), First.Placements.Num(), 6);
	TestNotEqual(TEXT("layout hash"), First.LayoutHash, int64(0));
	for (const FABTSM3JuryBuildingPlacement& Placement : First.Placements)
	{
		TestFalse(TEXT("pad reservation is non-empty"),
			Placement.ReservedPadCellIds.IsEmpty());
		TestFalse(TEXT("dynamic envelope reservation is non-empty"),
			Placement.ReservedDynamicEnvelopeCellIds.IsEmpty());
		for (int32 CellIndex = 1;
			CellIndex < Placement.ReservedPadCellIds.Num();
			++CellIndex)
		{
			TestTrue(TEXT("pad reservation is sorted and unique"),
				Placement.ReservedPadCellIds[CellIndex - 1]
					< Placement.ReservedPadCellIds[CellIndex]);
		}
		for (int32 CellIndex = 1;
			CellIndex < Placement.ReservedDynamicEnvelopeCellIds.Num();
			++CellIndex)
		{
			TestTrue(TEXT("dynamic reservation is sorted and unique"),
				Placement.ReservedDynamicEnvelopeCellIds[CellIndex - 1]
					< Placement.ReservedDynamicEnvelopeCellIds[CellIndex]);
		}
	}
	FABTSM3JuryBuildingPlacement HashTampered = First.Placements[0];
	HashTampered.ReservedPadCellIds.Add(123456);
	HashTampered.ReservedPadCellIds.Sort();
	TestNotEqual(TEXT("reserved cells affect placement hash"),
		FABTSM3JuryFixedSixLayoutBuilder::ComputePlacementHash(HashTampered),
		static_cast<uint64>(First.Placements[0].PlacementHash));
	HashTampered = First.Placements[0];
	HashTampered.ReservedDynamicEnvelopeCellIds.Add(123456);
	HashTampered.ReservedDynamicEnvelopeCellIds.Sort();
	TestNotEqual(TEXT("dynamic reserved cells affect placement hash"),
		FABTSM3JuryFixedSixLayoutBuilder::ComputePlacementHash(HashTampered),
		static_cast<uint64>(First.Placements[0].PlacementHash));
	HashTampered = First.Placements[0];
	HashTampered.PhysicalBounds.Max.X += 1.0;
	TestNotEqual(TEXT("physical bounds affect placement hash"),
		FABTSM3JuryFixedSixLayoutBuilder::ComputePlacementHash(HashTampered),
		static_cast<uint64>(First.Placements[0].PlacementHash));
	HashTampered = First.Placements[0];
	HashTampered.EffectBounds.Min.Y -= 1.0;
	TestNotEqual(TEXT("effect bounds affect placement hash"),
		FABTSM3JuryFixedSixLayoutBuilder::ComputePlacementHash(HashTampered),
		static_cast<uint64>(First.Placements[0].PlacementHash));
	HashTampered = First.Placements[0];
	HashTampered.ProductionIdentityHash ^= 1;
	TestNotEqual(TEXT("production identity affects placement hash"),
		FABTSM3JuryFixedSixLayoutBuilder::ComputePlacementHash(HashTampered),
		static_cast<uint64>(First.Placements[0].PlacementHash));
	HashTampered = First.Placements[0];
	HashTampered.DeviceAssemblyHash ^= 1;
	TestNotEqual(TEXT("device assembly affects placement hash"),
		FABTSM3JuryFixedSixLayoutBuilder::ComputePlacementHash(HashTampered),
		static_cast<uint64>(First.Placements[0].PlacementHash));

	FABTSM3JuryFixedSixLayoutResult Second;
	TestTrue(TEXT("repeat build succeeds"),
		FABTSM3JuryFixedSixLayoutBuilder::Build(
			Cells, 50000.0f, Source, Second, Failure));
	TestEqual(TEXT("repeat layout hash"), Second.LayoutHash, First.LayoutHash);
	for (int32 Index = 0; Index < First.Placements.Num(); ++Index)
	{
		TestEqual(TEXT("entry order is stable"),
			Second.Placements[Index].ManifestEntryId,
			First.Placements[Index].ManifestEntryId);
		TestEqual(TEXT("placement hash is stable"),
			Second.Placements[Index].PlacementHash,
			First.Placements[Index].PlacementHash);
	}

	FABTSM3MonthlySpatialResult Tampered = Source;
	Tampered.WorldSeed += 1;
	FABTSM3JuryFixedSixLayoutResult Rejected;
	TestFalse(TEXT("wrong world seed fails closed"),
		FABTSM3JuryFixedSixLayoutBuilder::Build(
			Cells, 50000.0f, Tampered, Rejected, Failure));
	TestEqual(TEXT("identity reject reason"),
		Rejected.RejectReason,
		EABTSM3JuryFixedSixRejectReason::SourceIdentityMismatch);
	TestFalse(TEXT("rejected result is not placement ready"),
		Rejected.bPlacementReady);

	FABTSM3MonthlySpatialResult RoadBlocked = Source;
	RoadBlocked.RetainedCandidates[0].RecomputedRoute.OrderedRoadCellIds.Add(0);
	TestFalse(TEXT("pad over final road fails closed"),
		FABTSM3JuryFixedSixLayoutBuilder::Build(
			Cells, 50000.0f, RoadBlocked, Rejected, Failure));
	TestEqual(TEXT("road overlap reject reason"),
		Rejected.RejectReason,
		EABTSM3JuryFixedSixRejectReason::PadReservationFailed);

	FABTSM3MonthlySpatialResult WaterBlocked = Source;
	WaterBlocked.RetainedCandidates[0].Cells[0].bWater = true;
	TestFalse(TEXT("pad over water fails closed"),
		FABTSM3JuryFixedSixLayoutBuilder::Build(
			Cells, 50000.0f, WaterBlocked, Rejected, Failure));
	TestEqual(TEXT("water overlap reject reason"),
		Rejected.RejectReason,
		EABTSM3JuryFixedSixRejectReason::PadReservationFailed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3JuryFixedSixDynamicEnvelopeFailureTest,
	"ABTS.M3.Monthly.JuryFixedSix.03DynamicEnvelopeFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM3JuryFixedSixDynamicEnvelopeFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	constexpr float PlanetRadiusCM = 50000.0f;
	TArray<FABTSM2Cell> Cells;
	FABTSM3MonthlySpatialResult Source;
	ABTSM3JuryFixedSixTests::BuildSyntheticJurySource(Cells, Source);

	TArray<FABTSM2Cell> RoadCells = Cells;
	FABTSM3MonthlySpatialResult RoadBlocked = Source;
	const int32 RoadCellId =
		ABTSM3JuryFixedSixTests::AddSyntheticDynamicEnvelopeCell(
			RoadCells,
			RoadBlocked,
			0);
	RoadBlocked.RetainedCandidates[0]
		.RecomputedRoute.OrderedRoadCellIds.Add(RoadCellId);
	FABTSM3JuryFixedSixLayoutResult Rejected;
	FString Failure;
	TestFalse(TEXT("dynamic envelope over final road fails closed"),
		FABTSM3JuryFixedSixLayoutBuilder::Build(
			RoadCells,
			PlanetRadiusCM,
			RoadBlocked,
			Rejected,
			Failure));
	TestEqual(TEXT("dynamic road reject reason"),
		Rejected.RejectReason,
		EABTSM3JuryFixedSixRejectReason::DynamicEnvelopeReservationFailed);

	TArray<FABTSM2Cell> WaterCells = Cells;
	FABTSM3MonthlySpatialResult WaterBlocked = Source;
	const int32 WaterCellId =
		ABTSM3JuryFixedSixTests::AddSyntheticDynamicEnvelopeCell(
			WaterCells,
			WaterBlocked,
			0);
	WaterBlocked.RetainedCandidates[0].Cells[WaterCellId].bWater = true;
	TestFalse(TEXT("dynamic envelope over water fails closed"),
		FABTSM3JuryFixedSixLayoutBuilder::Build(
			WaterCells,
			PlanetRadiusCM,
			WaterBlocked,
			Rejected,
			Failure));
	TestEqual(TEXT("dynamic water reject reason"),
		Rejected.RejectReason,
		EABTSM3JuryFixedSixRejectReason::DynamicEnvelopeReservationFailed);

	TArray<FABTSM2Cell> SeparationCells = Cells;
	FABTSM3MonthlySpatialResult SeparationSource = Source;
	const double NearAngle = FMath::DegreesToRadians(1.0);
	const double NearSlingshotAngle = FMath::DegreesToRadians(9.0);
	SeparationCells[1].UnitCenter = FVector(
		FMath::Cos(NearAngle),
		FMath::Sin(NearAngle),
		0.0);
	SeparationCells[
		FABTSM3JuryFixedSixLayoutBuilder::ExpectedEncounterCount + 1]
		.UnitCenter = FVector(
			FMath::Cos(NearSlingshotAngle),
			FMath::Sin(NearSlingshotAngle),
			0.0);
	TestFalse(TEXT("overlapping dynamic envelopes fail closed"),
		FABTSM3JuryFixedSixLayoutBuilder::Build(
			SeparationCells,
			PlanetRadiusCM,
			SeparationSource,
			Rejected,
			Failure));
	TestEqual(TEXT("dynamic separation reject reason"),
		Rejected.RejectReason,
		EABTSM3JuryFixedSixRejectReason::DynamicEnvelopeSeparationFailed);
	return true;
}

#endif
