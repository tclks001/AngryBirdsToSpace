// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ABTSM73BeamD0ProfileCatalog.h"

#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"

namespace ABTSM73BeamD0Tests
{
	const TArray<FName>& ProfileIds()
	{
		static const TArray<FName> Ids = {
			TEXT("ColumnBreak"),
			TEXT("SeamRelease"),
			TEXT("TipOver"),
			TEXT("DropTrigger"),
			TEXT("SlideRelease")
		};
		return Ids;
	}

	bool HardGatesEqual(
		const FABTSM73BeamCPreviewSettings& A,
		const FABTSM73BeamCPreviewSettings& B)
	{
		return A.BeamB.BeamA.Silhouette.MaxVolumeCount
				== B.BeamB.BeamA.Silhouette.MaxVolumeCount
			&& A.BeamB.BeamA.Silhouette.MaxWFCPropagationOperations
				== B.BeamB.BeamA.Silhouette.MaxWFCPropagationOperations
			&& A.BeamB.BeamA.Silhouette.MaxWFCBacktrackSteps
				== B.BeamB.BeamA.Silhouette.MaxWFCBacktrackSteps
			&& A.BeamB.BeamA.MaxBayCount == B.BeamB.BeamA.MaxBayCount
			&& A.BeamB.BeamA.MaxJointCount == B.BeamB.BeamA.MaxJointCount
			&& A.BeamB.BeamA.MaxMemberCount == B.BeamB.BeamA.MaxMemberCount
			&& A.BeamB.BeamA.MaxBearingContactCount
				== B.BeamB.BeamA.MaxBearingContactCount
			&& A.BeamB.BeamA.MaxBearingPairChecks
				== B.BeamB.BeamA.MaxBearingPairChecks
			&& A.BeamB.MaxWFCPropagationOperations
				== B.BeamB.MaxWFCPropagationOperations
			&& A.BeamB.MaxWFCBacktrackSteps == B.BeamB.MaxWFCBacktrackSteps
			&& A.BeamB.MaxGrammarStepCount == B.BeamB.MaxGrammarStepCount
			&& A.BeamB.MaxPlannedMemberCount == B.BeamB.MaxPlannedMemberCount
			&& A.MinimumBearingAreaRatio == B.MinimumBearingAreaRatio
			&& A.MaximumUnsupportedSpanCM == B.MaximumUnsupportedSpanCM
			&& A.MaximumSpanUtilization == B.MaximumSpanUtilization
			&& A.MaximumCantileverRatio == B.MaximumCantileverRatio
			&& A.MaximumColumnSlenderness == B.MaximumColumnSlenderness
			&& A.bRequireBidirectionalLateralTies
				== B.bRequireBidirectionalLateralTies
			&& A.MaximumLoadNodeCount == B.MaximumLoadNodeCount
			&& A.MaximumLoadEdgeCount == B.MaximumLoadEdgeCount
			&& A.MaximumTopologyOperationCount == B.MaximumTopologyOperationCount;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamD0CatalogValidationTest,
	"ABTS.M73DAG.BeamD0.CatalogValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamD0CatalogValidationTest::RunTest(const FString& Parameters)
{
	const FABTSM73BeamD0ProfileCatalog& Catalog =
		FABTSM73BeamD0ProfileCatalog::GetDefault();
	FString Error;
	TestTrue(TEXT("Default catalog validates"), Catalog.Validate(Error));
	TestEqual(TEXT("Default catalog exposes five semantic families"),
		Catalog.GetDefinitions().Num(), 5);
	TestTrue(TEXT("Catalog hash is non-zero"), Catalog.GetCatalogHash() != 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamD0ProfileTierMatrixTest,
	"ABTS.M73DAG.BeamD0.ProfileTierMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamD0ProfileTierMatrixTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamD0Tests;
	const FABTSM73BeamD0ProfileCatalog& Catalog =
		FABTSM73BeamD0ProfileCatalog::GetDefault();
	TSet<FName> ExactIds;
	for (const FName ProfileId : ProfileIds())
	{
		FABTSM73BeamD0DifficultyMetrics Previous;
		FABTSM73BeamD0VisualComplexityRecipe PreviousVisual;
		bool bHasPrevious = false;
		for (int32 Tier = 0; Tier <= 5; ++Tier)
		{
			FABTSM73BeamD0ResolvedProfile Resolved;
			FString Error;
			TestTrue(*FString::Printf(TEXT("%s tier %d resolves"),
				*ProfileId.ToString(), Tier),
				Catalog.Resolve(ProfileId, Tier, 735201, Resolved, Error));
			TestTrue(TEXT("Resolved profile is accepted"), Resolved.bAccepted);
			TestEqual(TEXT("Solution family remains single-step"),
				Resolved.Difficulty.SolutionSteps, 1);
			TestEqual(TEXT("Visual milestone matches exact tier"),
				Resolved.VisualComplexity.MilestoneTier, Tier);
			if (Tier <= 1)
			{
				TestTrue(TEXT("Low tier requires one terminal roof"),
					Resolved.VisualComplexity.bRequireSingleTerminalRoof);
				TestFalse(TEXT("Low tier does not require full primitive variety"),
					Resolved.VisualComplexity.bRequirePrimitiveVariety);
				TestTrue(TEXT("Low tier keeps a non-Box roof domain"),
					Resolved.BeamSettings.BeamB.BeamA.Silhouette.PrismWeight
						+ Resolved.BeamSettings.BeamB.BeamA.Silhouette.PyramidWeight
						> 0.0f);
				const int32 RequiredRoofCourses = Tier == 0 ? 8 : 10;
				TestEqual(TEXT("Low tier resolves the required roof course count"),
					Resolved.VisualComplexity.SingleTerminalRoofCourseCount,
					RequiredRoofCourses);
				TestEqual(TEXT("Roof quantization follows the real Brick section"),
					Resolved.BeamSettings.BeamB.BeamA.Silhouette
						.RoofCourseHeightCM,
					Resolved.BeamSettings.BeamB.BeamA.BlockCrossSectionCM);
				TestEqual(TEXT("Roof minimum course floor follows the tier recipe"),
					Resolved.BeamSettings.BeamB.BeamA.Silhouette
						.MinimumRoofCourseCount,
					RequiredRoofCourses);
				TestEqual(TEXT("Roof envelope height matches full courses"),
					Resolved.BeamSettings.BeamB.BeamA.Silhouette
						.SingleTerminalRoofHeightCM,
					Resolved.BeamSettings.BeamB.BeamA.BlockCrossSectionCM
						* RequiredRoofCourses);
			}
			else
			{
				TestFalse(TEXT("Higher tier leaves single-roof policy"),
					Resolved.VisualComplexity.bRequireSingleTerminalRoof);
			}
			TestFalse(TEXT("Exact resolved id is unique in the matrix"),
				ExactIds.Contains(Resolved.ResolvedM7ProfileId));
			ExactIds.Add(Resolved.ResolvedM7ProfileId);
			if (bHasPrevious)
			{
				TestTrue(TEXT("Damage cost is monotonic"),
					Resolved.Difficulty.TargetDamageCost
						>= Previous.TargetDamageCost);
				TestTrue(TEXT("Weakness exposure is monotonic"),
					Resolved.Difficulty.WeaknessExposureRatio
						<= Previous.WeaknessExposureRatio);
				TestTrue(TEXT("Aim tolerance is monotonic"),
					Resolved.Difficulty.AimToleranceDegrees
						<= Previous.AimToleranceDegrees);
				TestTrue(TEXT("Support redundancy is monotonic"),
					Resolved.Difficulty.SupportRedundancy
						>= Previous.SupportRedundancy);
				TestTrue(TEXT("Weakness reward is monotonic"),
					Resolved.Difficulty.WeaknessRewardMultiplier
						>= Previous.WeaknessRewardMultiplier);
				TestTrue(TEXT("Adjacent visual Brick windows do not overlap"),
					Resolved.VisualComplexity.MinimumBrickCount
						> PreviousVisual.MaximumBrickCount);
				TestTrue(TEXT("Every adjacent tier advances the macro recipe"),
					Resolved.VisualComplexity.ShapeGrammarDepth
						> PreviousVisual.ShapeGrammarDepth
					|| Resolved.VisualComplexity.TargetShapeVolumeCount
						> PreviousVisual.TargetShapeVolumeCount
					|| Resolved.VisualComplexity.MaximumBaysPerVolume
						> PreviousVisual.MaximumBaysPerVolume
					|| Resolved.VisualComplexity.MaximumParallelBlocksPerCourse
						> PreviousVisual.MaximumParallelBlocksPerCourse);
			}
			Previous = Resolved.Difficulty;
			PreviousVisual = Resolved.VisualComplexity;
			bHasPrevious = true;
		}
	}
	TestEqual(TEXT("Five profiles times six tiers have exact identities"),
		ExactIds.Num(), 30);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamD0DeterminismTest,
	"ABTS.M73DAG.BeamD0.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamD0DeterminismTest::RunTest(const FString& Parameters)
{
	const FABTSM73BeamD0ProfileCatalog& Catalog =
		FABTSM73BeamD0ProfileCatalog::GetDefault();
	FABTSM73BeamD0ResolvedProfile A;
	FABTSM73BeamD0ResolvedProfile B;
	FABTSM73BeamD0ResolvedProfile OtherSeed;
	FString Error;
	TestTrue(TEXT("First resolution succeeds"),
		Catalog.Resolve(TEXT("SeamRelease"), 4, 940633, A, Error));
	TestTrue(TEXT("Second resolution succeeds"),
		Catalog.Resolve(TEXT("SeamRelease"), 4, 940633, B, Error));
	TestEqual(TEXT("Exact ids match"), A.ResolvedM7ProfileId, B.ResolvedM7ProfileId);
	TestEqual(TEXT("Catalog hashes match"), A.ProfileCatalogHash, B.ProfileCatalogHash);
	TestEqual(TEXT("Resolved settings hashes match"),
		A.ResolvedSettingsHash, B.ResolvedSettingsHash);
	TestEqual(TEXT("Resolved width matches"),
		A.BeamSettings.BeamB.BeamA.Silhouette.TargetWidthCM,
		B.BeamSettings.BeamB.BeamA.Silhouette.TargetWidthCM);
	TestTrue(TEXT("Another seed resolves"),
		Catalog.Resolve(TEXT("SeamRelease"), 4, 940634, OtherSeed, Error));
	TestEqual(TEXT("Seed does not reselect exact profile"),
		A.ResolvedM7ProfileId, OtherSeed.ResolvedM7ProfileId);
	TestEqual(TEXT("Seed does not change catalog identity"),
		A.ProfileCatalogHash, OtherSeed.ProfileCatalogHash);
	TestNotEqual(TEXT("Seed changes resolved instance identity"),
		A.ResolvedSettingsHash, OtherSeed.ResolvedSettingsHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamD0FailClosedTest,
	"ABTS.M73DAG.BeamD0.FailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamD0FailClosedTest::RunTest(const FString& Parameters)
{
	const FABTSM73BeamD0ProfileCatalog& Catalog =
		FABTSM73BeamD0ProfileCatalog::GetDefault();
	FABTSM73BeamD0ResolvedProfile Result;
	FString Error;
	TestFalse(TEXT("Unknown profile is rejected"),
		Catalog.Resolve(TEXT("Unknown"), 2, 1, Result, Error));
	TestEqual(TEXT("Unknown profile reason is stable"), Error,
		FString(TEXT("BeamD0UnknownGameplayProfileId")));
	TestFalse(TEXT("Tier below domain is rejected"),
		Catalog.Resolve(TEXT("ColumnBreak"), -1, 1, Result, Error));
	TestEqual(TEXT("Low tier reason is stable"), Error,
		FString(TEXT("BeamD0DifficultyTierOutOfRange")));
	TestFalse(TEXT("Tier above domain is rejected"),
		Catalog.Resolve(TEXT("ColumnBreak"), 6, 1, Result, Error));
	TestEqual(TEXT("High tier reason is stable"), Error,
		FString(TEXT("BeamD0DifficultyTierOutOfRange")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamD0HardGateIsolationTest,
	"ABTS.M73DAG.BeamD0.HardGateIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamD0HardGateIsolationTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamD0Tests;
	const FABTSM73BeamD0ProfileCatalog& Catalog =
		FABTSM73BeamD0ProfileCatalog::GetDefault();
	FABTSM73BeamD0ResolvedProfile Easy;
	FABTSM73BeamD0ResolvedProfile Hard;
	FString Error;
	TestTrue(TEXT("Easy tier resolves"),
		Catalog.Resolve(TEXT("TipOver"), 0, 940844, Easy, Error));
	TestTrue(TEXT("Hard tier resolves"),
		Catalog.Resolve(TEXT("TipOver"), 5, 940844, Hard, Error));
	TestTrue(TEXT("Difficulty changes gameplay metrics"),
		Hard.Difficulty.TargetDamageCost > Easy.Difficulty.TargetDamageCost);
	TestTrue(TEXT("Difficulty changes internal generation policy"),
		Hard.BeamSettings.BeamB.BeamA.Silhouette.TargetHeightCM
			> Easy.BeamSettings.BeamB.BeamA.Silhouette.TargetHeightCM);
	TestTrue(TEXT("Validation budgets and hard gates stay project-owned"),
		HardGatesEqual(Easy.BeamSettings, Hard.BeamSettings));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamD0CatalogHashCoverageTest,
	"ABTS.M73DAG.BeamD0.CatalogHashCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamD0CatalogHashCoverageTest::RunTest(const FString& Parameters)
{
	const FABTSM73BeamD0ProfileCatalog& DefaultCatalog =
		FABTSM73BeamD0ProfileCatalog::GetDefault();
	TArray<FABTSM73BeamD0ProfileDefinition> Reordered =
		DefaultCatalog.GetDefinitions();
	Algo::Reverse(Reordered);
	const FABTSM73BeamD0ProfileCatalog ReorderedCatalogV2(
		Reordered, DefaultCatalog.GetCatalogVersion());
	TestEqual(TEXT("Definition order does not change catalog identity"),
		ReorderedCatalogV2.GetCatalogHash(), DefaultCatalog.GetCatalogHash());

	TArray<FABTSM73BeamD0ProfileDefinition> Mutated =
		DefaultCatalog.GetDefinitions();
	Mutated[0].BaseWidthCM += 1.0f;
	const FABTSM73BeamD0ProfileCatalog MutatedCatalog(
		Mutated, DefaultCatalog.GetCatalogVersion());
	TestNotEqual(TEXT("Behavior-affecting data changes catalog identity"),
		MutatedCatalog.GetCatalogHash(), DefaultCatalog.GetCatalogHash());

	TArray<FABTSM73BeamD0ProfileDefinition> Duplicated =
		DefaultCatalog.GetDefinitions();
	const FABTSM73BeamD0ProfileDefinition Duplicate = Duplicated[0];
	Duplicated.Add(Duplicate);
	const FABTSM73BeamD0ProfileCatalog DuplicateCatalog(
		Duplicated, DefaultCatalog.GetCatalogVersion());
	FString Error;
	TestFalse(TEXT("Duplicate gameplay profile ids fail closed"),
		DuplicateCatalog.Validate(Error));
	TestEqual(TEXT("Duplicate reason is stable"), Error,
		FString(TEXT("BeamD0DuplicateGameplayProfileId")));
	return true;
}

#endif
