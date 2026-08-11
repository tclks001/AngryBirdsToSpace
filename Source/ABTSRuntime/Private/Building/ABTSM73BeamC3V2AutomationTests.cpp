// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ABTSM73BeamC3V2MassiveXYCribPrototype.h"

#include "Misc/AutomationTest.h"

namespace ABTSM73BeamC3V2Tests
{
	constexpr uint32 ExpectedMassiveXYCribGeometryCrc32 = 3576735518u;

	struct FTier0BudgetExpectation
	{
		FName ProfileId;
		int32 ExpectedBodyHeightCM = 0;
		int32 ExpectedStandardCoreBrickCount = 0;
		int32 ExpectedMassiveCoreBrickCount = 0;
		int32 ExpectedMassiveRealizedBodyHeightCM = 0;
	};

	const FTier0BudgetExpectation Tier0BudgetExpectations[] = {
		{TEXT("ColumnBreak"), 962, 56, 20, 1080},
		{TEXT("SeamRelease"), 912, 52, 20, 1080},
		{TEXT("TipOver"), 1137, 64, 24, 1296},
		{TEXT("DropTrigger"), 804, 48, 16, 864},
		{TEXT("SlideRelease"), 837, 48, 16, 864}};

	float PositiveOverlap(const float MinimumA, const float MaximumA,
		const float MinimumB, const float MaximumB)
	{
		return FMath::Min(MaximumA, MaximumB)
			- FMath::Max(MinimumA, MinimumB);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V2MassiveXYCribStaticContractTest,
	"ABTS.M73DAG.BeamC3V2.MassiveXYCrib.StaticContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V2MassiveXYCribStaticContractTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V2;
	using namespace ABTSM73BeamC3V2Tests;

	FMassiveXYCribSettings Settings;
	FMassiveXYCribResult Result;
	FString Error;
	if (!TestTrue(TEXT("Worst-E1 quantized prototype builds"),
		FMassiveXYCribPrototype::Build(Settings, Result, Error)))
	{
		AddError(Error);
		return false;
	}

	TestTrue(TEXT("Prototype is accepted"), Result.bAccepted);
	TestEqual(TEXT("Six complete X/Y pairs"), Result.PairCount, 6);
	TestEqual(TEXT("Twelve alternating courses"), Result.CourseCount, 12);
	TestEqual(TEXT("Two rails per course"), Result.Bricks.Num(), 24);
	TestEqual(TEXT("Every adjacent course has four contact patches"),
		Result.AdjacentCourseContactCount, 44);
	TestEqual(TEXT("Realized body height is exactly six 216 cm pairs"),
		Result.RealizedBodyHeightCM, 1296.0f);
	TestEqual(TEXT("Realized geometry CRC32 is frozen"),
		Result.GeometryCrc32, ExpectedMassiveXYCribGeometryCrc32);

	int32 XBrickCount = 0;
	int32 YBrickCount = 0;
	int32 CountedAdjacentContacts = 0;
	FBox CombinedBounds(EForceInit::ForceInit);
	FBox GroundSupportBounds(EForceInit::ForceInit);
	for (int32 BrickIndex = 0; BrickIndex < Result.Bricks.Num(); ++BrickIndex)
	{
		const FMassiveXYCribBrick& Brick = Result.Bricks[BrickIndex];
		CombinedBounds += Brick.Bounds;
		if (Brick.CourseIndex == 0)
		{
			GroundSupportBounds += Brick.Bounds;
		}
		TestEqual(TEXT("Every log is horizontal and 108 cm thick in Z"),
			Brick.DimensionsCM.Z,
			static_cast<double>(FMassiveXYCribSettings::LogSectionCM));
		if (Brick.Axis == ECourseAxis::X)
		{
			++XBrickCount;
			TestEqual(TEXT("X log has the fixed 432 cm axial length"),
				Brick.DimensionsCM.X,
				static_cast<double>(FMassiveXYCribSettings::LogLengthCM));
			TestEqual(TEXT("X log has the fixed 108 cm transverse width"),
				Brick.DimensionsCM.Y,
				static_cast<double>(FMassiveXYCribSettings::LogSectionCM));
		}
		else
		{
			++YBrickCount;
			TestEqual(TEXT("Y log has the fixed 432 cm axial length"),
				Brick.DimensionsCM.Y,
				static_cast<double>(FMassiveXYCribSettings::LogLengthCM));
			TestEqual(TEXT("Y log has the fixed 108 cm transverse width"),
				Brick.DimensionsCM.X,
				static_cast<double>(FMassiveXYCribSettings::LogSectionCM));
		}

		for (int32 OtherIndex = BrickIndex + 1;
			OtherIndex < Result.Bricks.Num();
			++OtherIndex)
		{
			const FMassiveXYCribBrick& Other = Result.Bricks[OtherIndex];
			const float OverlapX = PositiveOverlap(
				Brick.Bounds.Min.X, Brick.Bounds.Max.X,
				Other.Bounds.Min.X, Other.Bounds.Max.X);
			const float OverlapY = PositiveOverlap(
				Brick.Bounds.Min.Y, Brick.Bounds.Max.Y,
				Other.Bounds.Min.Y, Other.Bounds.Max.Y);
			const float OverlapZ = PositiveOverlap(
				Brick.Bounds.Min.Z, Brick.Bounds.Max.Z,
				Other.Bounds.Min.Z, Other.Bounds.Max.Z);
			TestFalse(TEXT("No two prototype logs have positive-volume overlap"),
				OverlapX > KINDA_SMALL_NUMBER
				&& OverlapY > KINDA_SMALL_NUMBER
				&& OverlapZ > KINDA_SMALL_NUMBER);

			if (Other.CourseIndex == Brick.CourseIndex + 1
				&& FMath::IsNearlyEqual(
					Brick.Bounds.Max.Z, Other.Bounds.Min.Z, KINDA_SMALL_NUMBER)
				&& OverlapX > KINDA_SMALL_NUMBER
				&& OverlapY > KINDA_SMALL_NUMBER)
			{
				++CountedAdjacentContacts;
				TestEqual(TEXT("Every inter-course contact is 108 cm in X"),
					OverlapX, FMassiveXYCribSettings::LogSectionCM);
				TestEqual(TEXT("Every inter-course contact is 108 cm in Y"),
					OverlapY, FMassiveXYCribSettings::LogSectionCM);
			}
		}
	}

	TestEqual(TEXT("X and Y consume equal brick counts"), XBrickCount, 12);
	TestEqual(TEXT("X and Y consume equal brick counts"), YBrickCount, 12);
	TestEqual(TEXT("Static audit reconstructs all four-patch interfaces"),
		CountedAdjacentContacts, Result.AdjacentCourseContactCount);
	TestEqual(TEXT("First course sits exactly on the floor"),
		GroundSupportBounds.Min.Z, 0.0);
	TestEqual(TEXT("Alternating courses fill a 432 cm X envelope"),
		CombinedBounds.GetSize().X, 432.0);
	TestEqual(TEXT("Alternating courses fill a 432 cm Y envelope"),
		CombinedBounds.GetSize().Y, 432.0);
	TestEqual(TEXT("First-course bearing envelope is 432 cm axially"),
		GroundSupportBounds.GetSize().X, 432.0);
	TestEqual(TEXT("First-course bearing envelope is only 396 cm transversely"),
		GroundSupportBounds.GetSize().Y, 396.0);
	TestEqual(TEXT("Prototype reaches the quantized body height"),
		CombinedBounds.GetSize().Z, 1296.0);

	FMassiveXYCribResult Replay;
	TestTrue(TEXT("Exact replay builds"),
		FMassiveXYCribPrototype::Build(Settings, Replay, Error));
	TestEqual(TEXT("Exact replay preserves realized-geometry CRC32"),
		Replay.GeometryCrc32, Result.GeometryCrc32);
	TestEqual(TEXT("Exact replay preserves brick count"),
		Replay.Bricks.Num(), Result.Bricks.Num());
	FMassiveXYCribSettings SameRealizedGeometry = Settings;
	SameRealizedGeometry.TargetBodyHeightCM = 1295.0f;
	FMassiveXYCribResult SameRealizedResult;
	TestTrue(TEXT("A different request that realizes the same courses builds"),
		FMassiveXYCribPrototype::Build(
			SameRealizedGeometry, SameRealizedResult, Error));
	TestEqual(TEXT("Requested height cannot disguise identical realized geometry"),
		SameRealizedResult.GeometryCrc32, Result.GeometryCrc32);

	FMassiveXYCribSettings InsufficientBudget = Settings;
	InsufficientBudget.MaximumCoreBrickCount = 23;
	FMassiveXYCribResult Rejected;
	TestFalse(TEXT("One-brick-short budget fails closed"),
		FMassiveXYCribPrototype::Build(
			InsufficientBudget, Rejected, Error));
	TestEqual(TEXT("Rejected transaction publishes no bricks"),
		Rejected.Bricks.Num(), 0);
	TestTrue(TEXT("Rejected transaction reports the exact lower bound"),
		Error.Contains(TEXT("BeamC3V2CoreBudgetInsufficient:24>23")));
	TestEqual(TEXT("One exact XY pair consumes four Bricks"),
		FMassiveXYCribPrototype::ComputeMinimumBrickCount(216.0f, 108.0f),
		4);
	TestEqual(TEXT("Oversized finite input fails closed before integer overflow"),
		FMassiveXYCribPrototype::ComputeMinimumBrickCount(
			TNumericLimits<float>::Max(), 1.0f),
		INDEX_NONE);
	FMassiveXYCribSettings UnsupportedScale = Settings;
	UnsupportedScale.TargetBodyHeightCM = 300000.0f;
	UnsupportedScale.MaximumCoreBrickCount = TNumericLimits<int32>::Max();
	FMassiveXYCribResult UnsupportedScaleResult;
	TestFalse(TEXT("Stage-0 builder rejects allocation-scale inputs"),
		FMassiveXYCribPrototype::Build(
			UnsupportedScale, UnsupportedScaleResult, Error));
	TestEqual(TEXT("Allocation-scale rejection publishes no Bricks"),
		UnsupportedScaleResult.Bricks.Num(), 0);
	TestTrue(TEXT("Allocation-scale rejection has an exact reason"),
		Error.Contains(TEXT("BeamC3V2PrototypeScaleUnsupported")));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V2Tier0BudgetProofTest,
	"ABTS.M73DAG.BeamC3V2.MassiveXYCrib.Tier0BudgetProof",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V2Tier0BudgetProofTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V2;
	using namespace ABTSM73BeamC3V2Tests;

	// This is a frozen Stage-0 mathematical fixture for the retired 49-Brick
	// E1 window. It deliberately does not resolve the current V2 Catalog, whose
	// E1 authority is 20..99 and is covered by the production D0/D1.5 tests.
	constexpr int32 MinimumShellReserve = 12;
	constexpr int32 HistoricalRoofBrickFloor = 8;
	constexpr int32 HistoricalMaximumBrickCount = 49;
	bool bEvery72CMProfileFitsWithShell = true;
	bool bEvery108CMProfileFitsWithShell = true;
	bool bEveryThreeRail108CMProfileFitsWithShell = true;
	bool bEvery108CMProfileOverrunsTheExistingBodyWindow = true;

	for (const FTier0BudgetExpectation& Expected : Tier0BudgetExpectations)
	{
		const float BodyHeightCM = Expected.ExpectedBodyHeightCM;
		const int32 RoofBrickFloor = HistoricalRoofBrickFloor;
		const int32 MaximumBrickCount = HistoricalMaximumBrickCount;

		const int32 StandardCoreCount =
			FMassiveXYCribPrototype::ComputeMinimumBrickCount(
				BodyHeightCM, FMassiveXYCribSettings::StandardSectionCM);
		const int32 MassiveCoreCount =
			FMassiveXYCribPrototype::ComputeMinimumBrickCount(
				BodyHeightCM, FMassiveXYCribSettings::LogSectionCM);
		TestEqual(TEXT("36 cm no-Z core lower bound is exact"),
			StandardCoreCount, Expected.ExpectedStandardCoreBrickCount);
		TestEqual(TEXT("108 cm no-Z core lower bound is exact"),
			MassiveCoreCount, Expected.ExpectedMassiveCoreBrickCount);
		const int32 MassivePairCount = MassiveCoreCount / 4;
		const float MassiveRealizedBodyHeightCM = MassivePairCount * 2.0f
			* FMassiveXYCribSettings::LogSectionCM;
		TestEqual(TEXT("108 cm pair quantization height is explicit"),
			FMath::RoundToInt(MassiveRealizedBodyHeightCM),
			Expected.ExpectedMassiveRealizedBodyHeightCM);
		bEvery108CMProfileOverrunsTheExistingBodyWindow =
			bEvery108CMProfileOverrunsTheExistingBodyWindow
			&& MassiveRealizedBodyHeightCM > BodyHeightCM;
		TestTrue(TEXT("36 cm core plus mandatory roof was impossible in the historical E1 window"),
			StandardCoreCount + RoofBrickFloor > MaximumBrickCount);
		TestTrue(TEXT("108 cm core passed the historical Brick-count lower bound with a 12-Brick shell reserve"),
			MassiveCoreCount + RoofBrickFloor + MinimumShellReserve
				<= MaximumBrickCount);

		const int32 TwoRail72CMCount =
			FMassiveXYCribPrototype::ComputeMinimumBrickCount(
				BodyHeightCM, 72.0f);
		bEvery72CMProfileFitsWithShell =
			bEvery72CMProfileFitsWithShell
			&& TwoRail72CMCount + RoofBrickFloor + MinimumShellReserve
				<= MaximumBrickCount;
		bEvery108CMProfileFitsWithShell =
			bEvery108CMProfileFitsWithShell
			&& MassiveCoreCount + RoofBrickFloor + MinimumShellReserve
				<= MaximumBrickCount;

		const int32 ThreeRail108CMCount = MassivePairCount * 6;
		bEveryThreeRail108CMProfileFitsWithShell =
			bEveryThreeRail108CMProfileFitsWithShell
			&& ThreeRail108CMCount + RoofBrickFloor + MinimumShellReserve
				<= MaximumBrickCount;
	}

	TestFalse(TEXT("72 cm was not a unified historical E1 solution with shell reserve"),
		bEvery72CMProfileFitsWithShell);
	TestTrue(TEXT("108 cm was the first 36 cm quantum not excluded by the historical E1 Brick-count lower bound"),
		bEvery108CMProfileFitsWithShell);
	TestTrue(TEXT("Every 108 cm stack overran the historical body window and still needed a roof seam"),
		bEvery108CMProfileOverrunsTheExistingBodyWindow);
	TestFalse(TEXT("Three rails per course were not a historical E1-wide 49-Brick solution"),
		bEveryThreeRail108CMProfileFitsWithShell);
	return !HasAnyErrors();
}

#endif
