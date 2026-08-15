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
	TestTrue(TEXT("fixture catalog hash is non-zero"),
		FABTSM3JuryFixedSixLayoutBuilder::ComputeFixtureCatalogHash() != 0);

	TSet<FName> EntryIds;
	TSet<FName> StableIds;
	for (int32 Index = 0; Index < Fixtures.Num(); ++Index)
	{
		const FABTSM3JuryBuildingPlacementFixture& Fixture = Fixtures[Index];
		EntryIds.Add(Fixture.ManifestEntryId);
		StableIds.Add(Fixture.StableId);
		TestEqual(TEXT("difficulty follows encounter order"),
			Fixture.DifficultyTier, Index);
		TestTrue(TEXT("valid local bounds"), Fixture.LocalBounds.IsValid != 0);
		TestTrue(TEXT("positive pad X"), Fixture.RequiredPadHalfExtentCM.X > 0.0);
		TestTrue(TEXT("positive pad Y"), Fixture.RequiredPadHalfExtentCM.Y > 0.0);
		TestNotEqual(TEXT("descriptor hash"), Fixture.SourceDescriptorHash, int64(0));
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
	TestEqual(TEXT("six placements"), First.Placements.Num(), 6);
	TestNotEqual(TEXT("layout hash"), First.LayoutHash, int64(0));

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
	return true;
}

#endif

