// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ABTSM73BeamD1BrickCompiler.h"

#include "ABTSM73BeamD0ProfileCatalog.h"
#include "Building/ABTSM73BeamD1PreviewActor.h"
#include "Building/ABTSM73BeamDemoManifest.h"
#include "Building/ABTSM73StableBuildingActor.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UnrealType.h"

namespace ABTSM73BeamD1Tests
{
	const TArray<FName>& ProfileIds()
	{
		static const TArray<FName> Ids = {
			TEXT("ColumnBreak"), TEXT("SeamRelease"), TEXT("TipOver"),
			TEXT("DropTrigger"), TEXT("SlideRelease")};
		return Ids;
	}

	int32 AcceptedFixtureSeed(const FName ProfileId)
	{
		if (ProfileId == TEXT("ColumnBreak")) return 710000;
		if (ProfileId == TEXT("SeamRelease")) return 720000;
		if (ProfileId == TEXT("TipOver")) return 730000;
		if (ProfileId == TEXT("DropTrigger")) return 740000;
		return 750137;
	}

	FABTSM73BeamD1Settings MakeSettings(
		const FName ProfileId,
		const int32 Seed,
		const int32 Tier = 0)
	{
		FABTSM73BeamD1Settings Settings;
		Settings.GameplayProfileId = ProfileId;
		Settings.DifficultyTier = Tier;
		Settings.BuildingSeed = Seed;
		return Settings;
	}

	class FBeamD1TestWorld final : public FTestWorldWrapper
	{
	public:
		bool Create()
		{
			if (GEngine == nullptr)
			{
				ReportFailure(TEXT("GEngine unavailable"));
				return false;
			}
			UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(true)
				.ShouldSimulatePhysics(false)
				.EnableTraceCollision(true)
				.CreateNavigation(false)
				.CreateAISystem(false)
				.CreateFXSystem(false)
				.SetDefaultGameMode(AGameModeBase::StaticClass());
			TestWorld = UWorld::CreateWorld(EWorldType::Game, false,
				TEXT("ABTSM73BeamD1RuntimeBrickWorld"), nullptr, true,
				ERHIFeatureLevel::Num, &Values);
			if (TestWorld == nullptr)
			{
				ReportFailure(TEXT("Failed to create Beam-D1 test world"));
				return false;
			}
			FWorldContext& Context =
				GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.OwningGameInstance = GameInstance;
			Context.SetCurrentWorld(TestWorld);
			TestWorld->SetGameInstance(GameInstance);
			GameInstance->Init();
			return true;
		}
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FABTSM73BeamD15VisualComplexityLadderTest,
	"ABTS.M73DAG.BeamD15.VisualComplexityLadder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FABTSM73BeamD15VisualComplexityLadderTest::GetTests(
	TArray<FString>& OutBeautifiedNames,
	TArray<FString>& OutTestCommands) const
{
	using namespace ABTSM73BeamD1Tests;
	for (const FName ProfileId : ProfileIds())
	{
		for (int32 Tier = 0; Tier <= 5; ++Tier)
		{
			OutBeautifiedNames.Add(FString::Printf(
				TEXT("%s.E%d"), *ProfileId.ToString(), Tier + 1));
			OutTestCommands.Add(FString::Printf(
				TEXT("%s|%d"), *ProfileId.ToString(), Tier));
		}
	}
}

bool FABTSM73BeamD15VisualComplexityLadderTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamD1Tests;
	FString ProfileText;
	FString TierText;
	if (!Parameters.Split(TEXT("|"), &ProfileText, &TierText))
	{
		AddError(FString::Printf(
			TEXT("Invalid Beam-D1.5 case command: %s"), *Parameters));
		return false;
	}
	const FName ProfileId(*ProfileText);
	const int32 Tier = FCString::Atoi(*TierText);
	if (!ProfileIds().Contains(ProfileId) || Tier < 0 || Tier > 5)
	{
		AddError(FString::Printf(
			TEXT("Unknown Beam-D1.5 case: %s"), *Parameters));
		return false;
	}

	FABTSM73BeamD1BrickCompiler Compiler;
	FABTSM73BeamD1GenerationResult Result;
	FString Error;
	const double StartSeconds = FPlatformTime::Seconds();
	const bool bGenerated = Compiler.Generate(
		MakeSettings(ProfileId, AcceptedFixtureSeed(ProfileId), Tier),
		Result,
		Error);
	const double ElapsedMilliseconds =
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	AddInfo(FString::Printf(
		TEXT("Beam-D1.5 Case=%s E%d Result=%s ElapsedMs=%.2f Error=%s"),
		*ProfileId.ToString(), Tier + 1,
		bGenerated ? TEXT("Success") : TEXT("Fail"),
		ElapsedMilliseconds, *Error));
	TestTrue(*FString::Printf(TEXT("%s E%d compiles: %s"),
		*ProfileId.ToString(), Tier + 1, *Error), bGenerated);
	if (!bGenerated)
	{
		return false;
	}
	AddInfo(FString::Printf(
		TEXT("Beam-D1.5 %s E%d Bricks=%d Target=%d-%d Attempt=%d Volumes=%d Box=%d Prism=%d Pyramid=%d RoofBricks=%d Motifs=%d Spans=%d"),
		*ProfileId.ToString(), Tier + 1, Result.Summary.BrickCount,
		Result.Summary.TargetMinimumBrickCount,
		Result.Summary.TargetMaximumBrickCount,
		Result.Summary.VisualCandidateAttempt,
		Result.Summary.SemanticVolumeCount,
		Result.Summary.SemanticBoxCount,
		Result.Summary.SemanticPrismCount,
		Result.Summary.SemanticPyramidCount,
		Result.Summary.RoofCourseBrickCount,
		Result.Summary.DistinctMotifCount,
		Result.Summary.SupportedSpanCount));
	TestTrue(TEXT("Visual complexity is certified"),
		Result.Summary.bVisualComplexityCertified);
	TestTrue(TEXT("Assembly axis/contact quality is certified"),
		Result.Summary.bAssemblyQualityCertified);
	TestTrue(TEXT("Skeleton-first static certificate is present"),
		Result.Summary.bSkeletonFirstCertified);
	TestEqual(TEXT("One candidate-wide building group is certified"),
		Result.Summary.SkeletonFirstBuildingGroupCount, 1);
	TestTrue(TEXT("Candidate-wide common shell is non-empty"),
		Result.Summary.SkeletonFirstCommonShellMemberCount > 0);
	TestEqual(TEXT("Common shell reaches every planned core"),
		Result.Summary.SkeletonFirstCommonShellConnectedCoreCount,
		Result.Summary.SkeletonFirstExplicitCoreCellCount);
	TestEqual(TEXT("Shared courses remain in their declared bands"),
		Result.Summary.SkeletonFirstSharedCourseBandViolationCount, 0);
	TestEqual(TEXT("Every shared rail replaces one slot in each endpoint core"),
		Result.Summary.SkeletonFirstSharedCourseReplacementSlotCount,
		Result.Summary.SkeletonFirstSharedCourseCount * 2);
	TestEqual(TEXT("Shared rails form two-rail course pairs"),
		Result.Summary.SkeletonFirstSharedCourseCount & 1, 0);
	TestTrue(TEXT("Every supported span owns at least one shared rail pair"),
		Result.Summary.SupportedSpanCount == 0
			? Result.Summary.SkeletonFirstSharedCourseCount == 0
			: Result.Summary.SkeletonFirstSharedCourseCount
				>= Result.Summary.SupportedSpanCount * 2);
	TestTrue(TEXT("Brick count reaches tier minimum"),
		Result.Summary.BrickCount
			>= Result.Summary.TargetMinimumBrickCount);
	TestTrue(TEXT("Brick count stays below tier maximum"),
		Result.Summary.BrickCount
			<= Result.Summary.TargetMaximumBrickCount);
	if (Tier <= 1)
	{
		TestTrue(TEXT("Low tier retains a Box body"),
			Result.Summary.SemanticBoxCount > 0);
		TestEqual(TEXT("Low tier has exactly one terminal roof"),
			Result.Summary.SemanticPrismCount
				+ Result.Summary.SemanticPyramidCount,
			1);
		TestTrue(TEXT("Low tier roof has enough stacked courses to read in 3D"),
			Result.Summary.RoofCourseBrickCount
				>= (Tier == 0 ? 8 : 10));
	}
	if (Tier == 2)
	{
		TestTrue(TEXT("E3 realizes a prism"),
			Result.Summary.SemanticPrismCount > 0);
		TestTrue(TEXT("E3 realizes a pyramid"),
			Result.Summary.SemanticPyramidCount > 0);
	}
	if (Tier > 0)
	{
		FABTSM73BeamD0ResolvedProfile PreviousProfile;
		FString PreviousError;
		const bool bPreviousResolved =
			FABTSM73BeamD0ProfileCatalog::GetDefault().Resolve(
				ProfileId, Tier - 1, AcceptedFixtureSeed(ProfileId),
				PreviousProfile, PreviousError);
		TestTrue(*FString::Printf(
			TEXT("Previous tier resolves: %s"), *PreviousError),
			bPreviousResolved);
		if (bPreviousResolved)
		{
			TestTrue(TEXT("Adjacent tier Brick windows do not overlap"),
				PreviousProfile.VisualComplexity.MaximumBrickCount
					< Result.Summary.TargetMinimumBrickCount);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamD1ReportedAxisBalanceRegressionTest,
	"ABTS.M73DAG.BeamD1.ReportedAxisBalanceRegression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamD1ReportedAxisBalanceRegressionTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamD1Tests;
	FABTSM73BeamD1BrickCompiler Compiler;
	FABTSM73BeamD1GenerationResult Result;
	FString Error;
	const bool bGenerated = Compiler.Generate(
		MakeSettings(TEXT("DropTrigger"), 669740, 4), Result, Error);
	TestTrue(*FString::Printf(
		TEXT("Reported DropTrigger T4 seed compiles: %s"), *Error),
		bGenerated);
	if (!bGenerated)
	{
		return false;
	}
	AddInfo(FString::Printf(
		TEXT("DropTrigger T4 Seed=669740 Bricks=%d Stations=%d/%d Density=%.3f ClosurePosts=%d ClosureRatio=%.3f Attempt=%d"),
		Result.Summary.BrickCount,
		Result.Summary.XColumnStationCount,
		Result.Summary.YColumnStationCount,
		Result.Summary.AxisStationDensityRatio,
		Result.Summary.AddedStructuralSupportPostCount,
		Result.Summary.StructuralClosurePostRatio,
		Result.Summary.VisualCandidateAttempt));
	TestTrue(TEXT("Reported seed passes assembly-quality gate"),
		Result.Summary.bAssemblyQualityCertified);
	TestTrue(TEXT("Normalized X/Y column density is balanced"),
		Result.Summary.AxisStationDensityRatio >= 0.20f);
	TestTrue(TEXT("Structural closure does not dominate the frame"),
		Result.Summary.StructuralClosurePostRatio <= 0.12f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamD15ColumnHighTierClosureTest,
	"ABTS.M73DAG.BeamD15.ColumnHighTierClosure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamD15ColumnHighTierClosureTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamD1Tests;
	FABTSM73BeamD1BrickCompiler Compiler;
	FABTSM73BeamD1GenerationResult TierResults[2];
	bool bTierGenerated[2] = {false, false};
	for (int32 Tier = 4; Tier <= 5; ++Tier)
	{
		FABTSM73BeamD1GenerationResult& Result = TierResults[Tier - 4];
		FString Error;
		const bool bGenerated = Compiler.Generate(
			MakeSettings(TEXT("ColumnBreak"),
				AcceptedFixtureSeed(TEXT("ColumnBreak")), Tier),
			Result, Error);
		bTierGenerated[Tier - 4] = bGenerated;
		TestTrue(*FString::Printf(
			TEXT("ColumnBreak E%d structurally closes: %s"),
			Tier + 1, *Error), bGenerated);
		if (bGenerated)
		{
			TestTrue(TEXT("High-tier result is accepted"),
				Result.Summary.bAccepted);
			TestTrue(TEXT("Visual milestone is certified"),
				Result.Summary.bVisualComplexityCertified);
			TestTrue(TEXT("Assembly quality is certified"),
				Result.Summary.bAssemblyQualityCertified);
			TestTrue(TEXT("Stability core is certified"),
				Result.Summary.bStabilityCoreCertified);
			TestTrue(TEXT("Brick count remains inside the resolved window"),
				Result.Summary.BrickCount >= Result.Summary.TargetMinimumBrickCount
				&& Result.Summary.BrickCount <= Result.Summary.TargetMaximumBrickCount);
			TestEqual(TEXT("Resolved lower Brick window is exact"),
				Result.Summary.TargetMinimumBrickCount, Tier == 4 ? 1600 : 2200);
			TestEqual(TEXT("Resolved upper Brick window is exact"),
				Result.Summary.TargetMaximumBrickCount, Tier == 4 ? 2199 : 3499);
			TestEqual(TEXT("Every emitted Brick is counted"),
				Result.Bricks.Num(), Result.Summary.BrickCount);
			TestEqual(TEXT("Every Member owns one Brick"),
				Result.Summary.BrickCount, Result.Summary.MemberCount);
			TestEqual(TEXT("Every Member reference is complete"),
				Result.Summary.CompleteReferenceCount, Result.Summary.MemberCount);
			TestEqual(TEXT("Real Brick AABBs do not penetrate"),
				Result.Summary.StrictPenetrationCount, 0);
			TestEqual(TEXT("No real-contact mismatch remains"),
				Result.Summary.RealContactMismatchCount, 0);
			TestEqual(TEXT("No blocking support violation remains"),
				Result.Summary.RemainingSupportViolationCount, 0);
			TestTrue(TEXT("Structural closure remains inside the high-tier pass ledger"),
				Result.Summary.StructuralClosurePassCount <= (Tier == 4 ? 6 : 1));
			TestTrue(TEXT("Structural closure remains inside the high-tier add ledger"),
				Result.Summary.AddedStructuralSupportPostCount
					<= (Tier == 4 ? 33 : 15));
			TestTrue(TEXT("Final all-Z span remains inside the hard 720 cm gate"),
				Result.Summary.MaximumUnbracedCorePostSpanAfterCM <= 720.01f);
			TestTrue(TEXT("At least one strict rooted course is certified"),
				Result.Summary.StabilityRootedExistingCourseCount > 0);
			TestTrue(TEXT("Candidate search terminates inside its fixed ledger"),
				Result.Summary.VisualCandidateAttempt >= 0
				&& Result.Summary.VisualCandidateAttempt
					< (Tier == 4 ? 10 : 12));
			TestTrue(TEXT("Resolved settings identity is non-zero"),
				Result.Summary.ResolvedSettingsHash != 0);
			TestTrue(TEXT("Upstream structural identity is non-zero"),
				Result.Summary.UpstreamBeamHash != 0);
			TestTrue(TEXT("Core plan identity is non-zero"),
				Result.Summary.StabilityCorePlanHash != 0);
			TestTrue(TEXT("Rooted evidence identity is non-zero"),
				Result.Summary.StabilityRootedEvidenceHash != 0);
			TestTrue(TEXT("Brick geometry identity is non-zero"),
				Result.Summary.BrickGeometryHash != 0);
			TestTrue(TEXT("Tier-specific semantic volume milestone is retained"),
				Result.Summary.SemanticVolumeCount >= (Tier == 4 ? 21 : 16));
		}
	}
	if (bTierGenerated[0] && bTierGenerated[1])
	{
		TestTrue(TEXT("E6 remains a visible Brick-count step above E5"),
			TierResults[1].Summary.BrickCount > TierResults[0].Summary.BrickCount);
	}
	for (int32 RepeatIndex = 0; RepeatIndex < 2; ++RepeatIndex)
	{
		if (!bTierGenerated[RepeatIndex])
		{
			continue;
		}
		const int32 Tier = RepeatIndex + 4;
		const FString Identity = FString::Printf(TEXT("E%d"), Tier + 1);
		FABTSM73BeamD1GenerationResult Repeat;
		FString RepeatError;
		const bool bRepeated = Compiler.Generate(
			MakeSettings(TEXT("ColumnBreak"),
				AcceptedFixtureSeed(TEXT("ColumnBreak")), Tier),
			Repeat, RepeatError);
		TestTrue(*FString::Printf(
			TEXT("ColumnBreak %s repeat closes deterministically: %s"),
			*Identity, *RepeatError), bRepeated);
		if (bRepeated)
		{
			const FABTSM73BeamD1Summary& Expected =
				TierResults[RepeatIndex].Summary;
			const FABTSM73BeamD1Summary& Actual = Repeat.Summary;
			TestEqual(*FString::Printf(TEXT("%s resolved settings hash is deterministic"),
				*Identity), Actual.ResolvedSettingsHash, Expected.ResolvedSettingsHash);
			TestEqual(*FString::Printf(TEXT("%s upstream structural hash is deterministic"),
				*Identity), Actual.UpstreamBeamHash, Expected.UpstreamBeamHash);
			TestEqual(*FString::Printf(TEXT("%s core plan hash is deterministic"),
				*Identity), Actual.StabilityCorePlanHash,
				Expected.StabilityCorePlanHash);
			TestEqual(*FString::Printf(TEXT("%s rooted evidence hash is deterministic"),
				*Identity), Actual.StabilityRootedEvidenceHash,
				Expected.StabilityRootedEvidenceHash);
			TestEqual(*FString::Printf(TEXT("%s Brick geometry hash is deterministic"),
				*Identity), Actual.BrickGeometryHash, Expected.BrickGeometryHash);
			TestEqual(*FString::Printf(TEXT("%s accepted attempt is deterministic"),
				*Identity), Actual.VisualCandidateAttempt,
				Expected.VisualCandidateAttempt);
			TestEqual(*FString::Printf(TEXT("%s Brick count is deterministic"),
				*Identity), Actual.BrickCount, Expected.BrickCount);
			TestEqual(*FString::Printf(TEXT("%s rooted course count is deterministic"),
				*Identity), Actual.StabilityRootedExistingCourseCount,
				Expected.StabilityRootedExistingCourseCount);
			TestEqual(*FString::Printf(TEXT("%s maximum all-Z span is deterministic"),
				*Identity), Actual.MaximumUnbracedCorePostSpanAfterCM,
				Expected.MaximumUnbracedCorePostSpanAfterCM);
			TestEqual(*FString::Printf(TEXT("%s closure pass ledger is deterministic"),
				*Identity), Actual.StructuralClosurePassCount,
				Expected.StructuralClosurePassCount);
			TestEqual(*FString::Printf(TEXT("%s closure add ledger is deterministic"),
				*Identity), Actual.AddedStructuralSupportPostCount,
				Expected.AddedStructuralSupportPostCount);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamD1FiveProfileCompilationTest,
	"ABTS.M73DAG.BeamD1.FiveProfileCompilation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamD1FiveProfileCompilationTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamD1Tests;
	FABTSM73BeamD1BrickCompiler Compiler;
	for (const FName ProfileId : ProfileIds())
	{
		FABTSM73BeamD1GenerationResult Result;
		FString Error;
		const FABTSM73BeamD1Settings Settings = MakeSettings(
			ProfileId, AcceptedFixtureSeed(ProfileId));
		const bool bGenerated = Compiler.Generate(Settings, Result, Error);
		TestTrue(*FString::Printf(TEXT("%s compiles: %s"),
			*ProfileId.ToString(), *Error), bGenerated);
		if (bGenerated)
		{
			TestTrue(TEXT("Result is accepted"), Result.Summary.bAccepted);
			TestTrue(TEXT("At least one real Brick is emitted"),
				!Result.Bricks.IsEmpty());
			TestEqual(TEXT("Every Member owns one Brick"),
				Result.Summary.BrickCount, Result.Summary.MemberCount);
			TestEqual(TEXT("Every Member reference is complete"),
				Result.Summary.CompleteReferenceCount,
				Result.Summary.MemberCount);
			TestEqual(TEXT("Exactly one weakness candidate is retained"),
				Result.Summary.WeaknessCandidateCount, 1);
			TestEqual(TEXT("Real Brick AABBs do not penetrate"),
				Result.Summary.StrictPenetrationCount, 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamD1DeterminismTest,
	"ABTS.M73DAG.BeamD1.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamD1DeterminismTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamD1Tests;
	FABTSM73BeamD1BrickCompiler Compiler;
	FABTSM73BeamD1GenerationResult A;
	FABTSM73BeamD1GenerationResult B;
	FABTSM73BeamD1GenerationResult OtherSeed;
	FString Error;
	TestTrue(TEXT("First deterministic compile succeeds"),
		Compiler.Generate(MakeSettings(TEXT("ColumnBreak"), 710000), A, Error));
	TestTrue(TEXT("Second deterministic compile succeeds"),
		Compiler.Generate(MakeSettings(TEXT("ColumnBreak"), 710000), B, Error));
	TestEqual(TEXT("Brick hashes match"),
		A.Summary.BrickGeometryHash, B.Summary.BrickGeometryHash);
	TestEqual(TEXT("Brick counts match"), A.Bricks.Num(), B.Bricks.Num());
	TestTrue(TEXT("Another seed compiles"),
		Compiler.Generate(MakeSettings(TEXT("ColumnBreak"), 940211),
			OtherSeed, Error));
	TestEqual(TEXT("Seed never reselects Profile identity"),
		A.Summary.ResolvedM7ProfileId, OtherSeed.Summary.ResolvedM7ProfileId);
	TestNotEqual(TEXT("Seed changes real Brick geometry identity"),
		A.Summary.BrickGeometryHash, OtherSeed.Summary.BrickGeometryHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamD1MaterialRoleTest,
	"ABTS.M73DAG.BeamD1.MaterialRoles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamD1MaterialRoleTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamD1Tests;
	FABTSM73BeamD1BrickCompiler Compiler;
	FABTSM73BeamD1GenerationResult Column;
	FABTSM73BeamD1GenerationResult Seam;
	FABTSM73BeamD1GenerationResult Tip;
	FABTSM73BeamD1GenerationResult Drop;
	FString Error;
	TestTrue(TEXT("Column palette compiles"), Compiler.Generate(
		MakeSettings(TEXT("ColumnBreak"), AcceptedFixtureSeed(TEXT("ColumnBreak"))), Column, Error));
	TestTrue(TEXT("Seam palette compiles"), Compiler.Generate(
		MakeSettings(TEXT("SeamRelease"), AcceptedFixtureSeed(TEXT("SeamRelease"))), Seam, Error));
	TestTrue(TEXT("Tip palette compiles"), Compiler.Generate(
		MakeSettings(TEXT("TipOver"), AcceptedFixtureSeed(TEXT("TipOver"))), Tip, Error));
	TestTrue(TEXT("Drop palette compiles"), Compiler.Generate(
		MakeSettings(TEXT("DropTrigger"), AcceptedFixtureSeed(TEXT("DropTrigger"))), Drop, Error));
	TestTrue(TEXT("Light frame uses Wood"), Column.Summary.WoodBrickCount > 0);
	TestEqual(TEXT("Light frame has a Glass weakness"),
		Column.Summary.GlassBrickCount, 1);
	TestTrue(TEXT("Masonry palette uses Stone"), Seam.Summary.StoneBrickCount > 0);
	TestTrue(TEXT("Masonry seam uses Wood"), Seam.Summary.WoodBrickCount > 0);
	TestTrue(TEXT("Iron frame palette uses Iron"), Tip.Summary.IronBrickCount > 0);
	TestEqual(TEXT("Iron frame has a Glass trigger"),
		Tip.Summary.GlassBrickCount, 1);
	TestTrue(TEXT("Suspended pod keeps a Stone payload"),
		Drop.Summary.StoneBrickCount > 0);
	TestEqual(TEXT("Hanging mass exposes one device role"),
		Drop.Summary.DeviceRoleCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamD15LowTierRoofBearingContinuityTest,
	"ABTS.M73DAG.BeamD15.LowTierRoofBearingContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamD15LowTierRoofBearingContinuityTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamD1Tests;
	FABTSM73BeamD1BrickCompiler Compiler;
	FABTSM73BeamD1GenerationResult Result;
	FString Error;
	const bool bGenerated = Compiler.Generate(
		MakeSettings(TEXT("DropTrigger"), 669740, 0), Result, Error);
	TestTrue(*FString::Printf(
		TEXT("Reported low-tier roof seed compiles: %s"), *Error),
		bGenerated);
	if (!bGenerated)
	{
		return false;
	}
	AddInfo(FString::Printf(
		TEXT("DropTrigger T0 Seed=669740 Bricks=%d RoofBricks=%d ClosurePass=%d AddedPosts=%d ContactMismatch=%d SupportViolations=%d"),
		Result.Summary.BrickCount,
		Result.Summary.RoofCourseBrickCount,
		Result.Summary.StructuralClosurePassCount,
		Result.Summary.AddedStructuralSupportPostCount,
		Result.Summary.RealContactMismatchCount,
		Result.Summary.RemainingSupportViolationCount));
	TestTrue(TEXT("Reported roof keeps its full low-tier physical output"),
		Result.Summary.RoofCourseBrickCount >= 8);
	TestEqual(TEXT("Reported roof needs no downstream structural closure pass"),
		Result.Summary.StructuralClosurePassCount, 0);
	TestEqual(TEXT("Reported roof needs no rescue support post"),
		Result.Summary.AddedStructuralSupportPostCount, 0);
	TestEqual(TEXT("Reported roof has no real-contact mismatch"),
		Result.Summary.RealContactMismatchCount, 0);
	TestEqual(TEXT("Reported roof has no remaining support violation"),
		Result.Summary.RemainingSupportViolationCount, 0);
	TestEqual(TEXT("Reported roof has no strict Brick penetration"),
		Result.Summary.StrictPenetrationCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamD1FailClosedTest,
	"ABTS.M73DAG.BeamD1.FailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamD1FailClosedTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamD1Tests;
	FABTSM73BeamD1BrickCompiler Compiler;
	FABTSM73BeamD1GenerationResult Result;
	FString Error;
	TestFalse(TEXT("Unknown Profile fails closed"), Compiler.Generate(
		MakeSettings(TEXT("UnknownProfile"), 1), Result, Error));
	TestTrue(TEXT("Failure preserves the D1 profile stage"),
		Error.StartsWith(TEXT("BeamD1Profile:")));
	TestFalse(TEXT("Rejected compile emits no Brick"), !Result.Bricks.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamD1RealModuleTest,
	"ABTS.M73DAG.BeamD1.RealModule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamD1RealModuleTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamD1Tests;
	FABTSM73BeamD1BrickCompiler Compiler;
	FABTSM73BeamD1GenerationResult Result;
	FString Error;
	if (!TestTrue(TEXT("Real Module source compile succeeds"),
		Compiler.Generate(MakeSettings(TEXT("ColumnBreak"), 940211), Result, Error))
		|| Result.Bricks.IsEmpty())
	{
		return false;
	}
	FBeamD1TestWorld WorldWrapper;
	if (!WorldWrapper.Create())
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM7BuildingMaterialSystem* MaterialSystem =
		World->SpawnActor<AABTSM7BuildingMaterialSystem>(
			AABTSM7BuildingMaterialSystem::StaticClass(),
			FTransform::Identity, Params);
	if (!TestNotNull(TEXT("Real M7 MaterialSystem"), MaterialSystem))
	{
		return false;
	}
	const FABTSM73BeamD1BrickBinding& Binding = Result.Bricks[0];
	AABTSM7BuildingModule* Module = MaterialSystem->SpawnBrickModule(
		Binding.BrickSpec, Binding.LocalTransform);
	if (!TestNotNull(TEXT("Beam Member becomes a real BuildingModule"), Module))
	{
		return false;
	}
	TestEqual(TEXT("Real Module retains material enum"),
		Module->GetBuildingMaterial(), Binding.BrickSpec.Material);
	TestEqual(TEXT("Real Module is a Brick"),
		Module->GetModuleKind(), EABTSM7ModuleKind::Brick);
	TestNotNull(TEXT("Real Module owns the shared Brick mesh"),
		Module->GetMeshComponent()->GetStaticMesh().Get());
	TestTrue(TEXT("Real Module dimensions are encoded in component scale"),
		Module->GetActorScale3D().Equals(
			Binding.BrickSpec.DimensionsCM / 100.0f, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamD1StaticCrystalCapTest,
	"ABTS.M73DAG.BuildingFreezeV3.StaticCrystalCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamD1StaticCrystalCapTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamD1Tests;
	FBeamD1TestWorld WorldWrapper;
	if (!WorldWrapper.Create())
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	AABTSM7BuildingMaterialSystem* MaterialSystem =
		World->SpawnActor<AABTSM7BuildingMaterialSystem>();
	if (!TestNotNull(TEXT("Crystal cap MaterialSystem"), MaterialSystem))
	{
		return false;
	}
	FABTSM7BrickSpec Spec;
	Spec.Material = EABTSM7BuildingMaterial::Crystal;
	Spec.DimensionsCM = FVector(72.0);
	AABTSM7BuildingModule* Cap = MaterialSystem->SpawnStaticBrickModule(
		Spec, FTransform(FVector(0.0, 0.0, 36.0)));
	if (!TestNotNull(TEXT("Static Crystal cap module"), Cap))
	{
		return false;
	}
	TestEqual(TEXT("Static cap retains Crystal identity"),
		Cap->GetBuildingMaterial(), EABTSM7BuildingMaterial::Crystal);
	TestNotNull(TEXT("Static cap has visible collision mesh"),
		Cap->GetMeshComponent()->GetStaticMesh().Get());
	TestNotNull(TEXT("Static cap has a Crystal material binding"),
		Cap->GetMeshComponent()->GetMaterial(0));
	TestTrue(TEXT("Static cap collision is enabled"),
		Cap->GetMeshComponent()->GetCollisionEnabled()
			== ECollisionEnabled::QueryAndPhysics);
	MaterialSystem->BeginLaunchPhysics(
		true, FVector::UpVector, 980.0f, 0.2f);
	TestFalse(TEXT("Static cap is excluded from global launch activation"),
		Cap->IsDynamic());

	int32 CrystalRecoveryQuantity = 0;
	MaterialSystem->OnMaterialRecovered.AddLambda(
		[&CrystalRecoveryQuantity](
			const EABTSM7BuildingMaterial Material, const int32 Quantity)
		{
			if (Material == EABTSM7BuildingMaterial::Crystal)
			{
				CrystalRecoveryQuantity += Quantity;
			}
		});
	UStaticMeshComponent* Collision = Cap->GetMeshComponent();
	TestTrue(TEXT("First Crystal impact is owned"),
		MaterialSystem->HandleBirdImpact(Collision, INDEX_NONE, 100000.0f,
			FVector(100000.0f, 0.0f, 0.0f), EABTSBirdId::Red));
	TestTrue(TEXT("First Crystal impact breaks the cap"), Cap->IsBroken());
	TestTrue(TEXT("Repeated impact remains an owned no-op during Destroy deferral"),
		MaterialSystem->HandleBirdImpact(Collision, INDEX_NONE, 100000.0f,
			FVector(100000.0f, 0.0f, 0.0f), EABTSBirdId::Red));
	TestEqual(TEXT("Crystal recovery is emitted exactly once"),
		CrystalRecoveryQuantity, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamD1DelayedMaterialSystemTest,
	"ABTS.M73DAG.BeamD1.DelayedMaterialSystem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamD1DelayedMaterialSystemTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamD1Tests;
	FBeamD1TestWorld WorldWrapper;
	if (!WorldWrapper.Create())
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	FTransform PreviewTransform = FTransform::Identity;
	AABTSM73BeamD1PreviewActor* Preview =
		World->SpawnActorDeferred<AABTSM73BeamD1PreviewActor>(
			AABTSM73BeamD1PreviewActor::StaticClass(), PreviewTransform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Delayed dependency PreviewActor"), Preview))
	{
		return false;
	}
	FBoolProperty* SpawnProperty = FindFProperty<FBoolProperty>(
		AABTSM73BeamD1PreviewActor::StaticClass(),
		TEXT("bSpawnRuntimeModulesInPIE"));
	if (!TestNotNull(TEXT("Runtime spawn property"), SpawnProperty))
	{
		return false;
	}
	SpawnProperty->SetPropertyValue_InContainer(Preview, true);
	UGameplayStatics::FinishSpawningActor(Preview, PreviewTransform);
	Preview->TryInitializeRuntimeBuilding();
	TestEqual(TEXT("No Module exists before delayed MaterialSystem"),
		Preview->GetRuntimeModuleCountForValidation(), 0);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM7BuildingMaterialSystem* MaterialSystem =
		World->SpawnActor<AABTSM7BuildingMaterialSystem>(
			AABTSM7BuildingMaterialSystem::StaticClass(),
			FTransform::Identity, Params);
	if (!TestNotNull(TEXT("Delayed M7 MaterialSystem"), MaterialSystem))
	{
		return false;
	}
	Preview->TryInitializeRuntimeBuilding();
	TestTrue(TEXT("Delayed Preview generation remains accepted"),
		Preview->GetSummaryForValidation().bAccepted);
	TestEqual(TEXT("Delayed MaterialSystem receives every compiled Brick"),
		Preview->GetRuntimeModuleCountForValidation(),
		Preview->GetSummaryForValidation().BrickCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamD1Stage5EditorPreviewRouteTest,
	"ABTS.M73DAG.BeamC3V3.Demo.Stage5Production.EditorPreviewE6",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamD1Stage5EditorPreviewRouteTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamD1Tests;
	FABTSM73BeamDemoManifestEntry Entry;
	FString Error;
	if (!TestTrue(TEXT("E6 manifest resolves"),
		FABTSM73BeamDemoManifest::Resolve(
			EABTSM73BeamDemoBuilding::E6TipOver, Entry, Error)))
	{
		AddError(Error);
		return false;
	}

	FABTSM73BeamD1Stage5Result DirectResult;
	if (!TestTrue(TEXT("Direct E6 Stage 5 producer accepts"),
		FABTSM73BeamD1BrickCompiler().GenerateStage5(
			Entry.Settings, DirectResult, Error)))
	{
		AddError(Error);
		return false;
	}

	FBeamD1TestWorld WorldWrapper;
	if (!WorldWrapper.Create())
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	const FTransform PreviewTransform = FTransform::Identity;
	AABTSM73BeamD1PreviewActor* Preview =
		World->SpawnActorDeferred<AABTSM73BeamD1PreviewActor>(
			AABTSM73BeamD1PreviewActor::StaticClass(), PreviewTransform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Stage 5 E6 PreviewActor"), Preview))
	{
		return false;
	}
	Preview->DemoBuilding = EABTSM73BeamDemoBuilding::E6TipOver;
	Preview->GenerationStopStage = EABTSM73BeamC3GenerationStage::StaticDAG;
	Preview->bShowEditorPreview = true;
	Preview->bSpawnRuntimeModulesInPIE = false;
	UGameplayStatics::FinishSpawningActor(Preview, PreviewTransform);

	const FABTSM73BeamD1Summary& Summary = Preview->GetSummaryForValidation();
	const int32 VisibleInstanceCount = Preview->WoodPreview->GetInstanceCount()
		+ Preview->StonePreview->GetInstanceCount()
		+ Preview->IronPreview->GetInstanceCount()
		+ Preview->GlassPreview->GetInstanceCount();
	TestTrue(TEXT("Editor Stage 5 route accepts"), Summary.bAccepted);
	TestEqual(TEXT("Editor route publishes every production brick"),
		Preview->GetCompiledBricksForValidation().Num(), DirectResult.Bricks.Num());
	TestEqual(TEXT("Editor route renders every production brick"),
		VisibleInstanceCount, DirectResult.Bricks.Num());
	TestEqual(TEXT("Editor route preserves the Stage 5 production hash"),
		Summary.BrickGeometryHash, DirectResult.Summary.BrickGeometryHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamD1Stage55DeviceAssemblyTest,
	"ABTS.M73DAG.BeamC3V3.Demo.Stage55DeviceAssembly.SixBuildings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamD1Stage55DeviceAssemblyTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamD1Tests;
	int32 BarrelCount = 0;
	int32 PistonCount = 0;
	for (const FABTSM73BeamDemoManifestEntry& Entry
		: FABTSM73BeamDemoManifest::GetEntries())
	{
		FABTSM73BeamD1Stage55Result Result;
		FString Error;
		if (!TestTrue(*FString::Printf(TEXT("%s Stage 5.5 accepts: %s"),
			*Entry.StableId.ToString(), *Error),
			FABTSM73BeamD1BrickCompiler().GenerateStage55DeviceAssembly(
				Entry.Settings, Result, Error)))
		{
			AddError(Error);
			return false;
		}
		TestTrue(TEXT("Stage 5.5 summary is accepted"), Result.Summary.bAccepted);
		TestTrue(TEXT("Device assembly was evaluated"),
			Result.Summary.bDeviceAssemblyEvaluated);
		TestEqual(TEXT("Exactly one bounded demo device is emitted"),
			Result.Devices.Num(), 1);
		TestEqual(TEXT("Frozen Stage 5 remains device-free"),
			Result.Stage5.Summary.DeviceAssemblyCount, 0);
		TestNotEqual(TEXT("Device slot hash is populated"),
			Result.DeviceSlotHash, 0ull);
		TestNotEqual(TEXT("Device load DAG hash is populated"),
			Result.DeviceLoadDAGHash, 0ull);
		TestNotEqual(TEXT("Device assembly hash is populated"),
			Result.DeviceAssemblyHash, 0ull);
		if (Result.Devices.IsEmpty())
		{
			return false;
		}
		const FABTSM73BeamD1DeviceBinding& Device = Result.Devices[0];
		BarrelCount += Device.Kind == EABTSM7ModuleKind::ExplosiveBarrel ? 1 : 0;
		PistonCount += Device.Kind == EABTSM7ModuleKind::SpringPiston ? 1 : 0;
		TestTrue(TEXT("Device uses at least two support cells"),
			Device.SupportContactCellCount >= 2);
		TestTrue(TEXT("Device has an explicit DAG support route"),
			Device.bDirectGroundSupport || !Device.SupportMemberIds.IsEmpty());
		for (const int32 SupportMemberId : Device.SupportMemberIds)
		{
			TestTrue(TEXT("Support member id exists in the Stage-5 DAG"),
				Result.Stage5.LoadDAG.Nodes.IsValidIndex(SupportMemberId));
			if (Result.Stage5.LoadDAG.Nodes.IsValidIndex(SupportMemberId))
			{
				TestTrue(TEXT("Every device support reaches ground"),
					Result.Stage5.LoadDAG.Nodes[SupportMemberId].bGroundReachable);
			}
		}
		for (const FABTSM73BeamD1BrickBinding& Brick : Result.Stage5.Bricks)
		{
			const FVector Overlap(
				FMath::Min(Device.LocalBounds.Max.X, Brick.LocalBounds.Max.X)
					- FMath::Max(Device.LocalBounds.Min.X, Brick.LocalBounds.Min.X),
				FMath::Min(Device.LocalBounds.Max.Y, Brick.LocalBounds.Max.Y)
					- FMath::Max(Device.LocalBounds.Min.Y, Brick.LocalBounds.Min.Y),
				FMath::Min(Device.LocalBounds.Max.Z, Brick.LocalBounds.Max.Z)
					- FMath::Max(Device.LocalBounds.Min.Z, Brick.LocalBounds.Min.Z));
			TestFalse(TEXT("Device has no positive-volume brick penetration"),
				Overlap.X > 0.01 && Overlap.Y > 0.01 && Overlap.Z > 0.01);
		}
		AddInfo(FString::Printf(
			TEXT("Stage55Demo Entry=%s Kind=%d Axis=%d Min=%s Extent=%s Supports=%s Slot=%llu Load=%llu Assembly=%llu"),
			*Entry.StableId.ToString(), static_cast<int32>(Device.Kind),
			static_cast<int32>(Device.Axis), *Device.VoxelMin.ToString(),
			*Device.VoxelExtent.ToString(),
			*FString::JoinBy(Device.SupportMemberIds, TEXT("."),
				[](const int32 Id) { return FString::FromInt(Id); }),
			Result.DeviceSlotHash, Result.DeviceLoadDAGHash,
			Result.DeviceAssemblyHash));
	}
	TestTrue(TEXT("Fixed six include at least one explosive barrel"), BarrelCount > 0);
	TestTrue(TEXT("Fixed six include at least one spring piston"), PistonCount > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamD1Stage55EditorPreviewRouteTest,
	"ABTS.M73DAG.BeamC3V3.Demo.Stage55DeviceAssembly.EditorPreviewE6",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamD1Stage55EditorPreviewRouteTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamD1Tests;
	FABTSM73BeamDemoManifestEntry Entry;
	FString Error;
	if (!TestTrue(TEXT("E6 manifest resolves"),
		FABTSM73BeamDemoManifest::Resolve(
			EABTSM73BeamDemoBuilding::E6TipOver, Entry, Error)))
	{
		AddError(Error);
		return false;
	}
	FABTSM73BeamD1Stage55Result DirectResult;
	if (!TestTrue(TEXT("Direct E6 Stage 5.5 producer accepts"),
		FABTSM73BeamD1BrickCompiler().GenerateStage55DeviceAssembly(
			Entry.Settings, DirectResult, Error)))
	{
		AddError(Error);
		return false;
	}

	FBeamD1TestWorld WorldWrapper;
	if (!WorldWrapper.Create())
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	AABTSM73BeamD1PreviewActor* Preview =
		World->SpawnActorDeferred<AABTSM73BeamD1PreviewActor>(
			AABTSM73BeamD1PreviewActor::StaticClass(), FTransform::Identity,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Stage 5.5 E6 PreviewActor"), Preview))
	{
		return false;
	}
	Preview->DemoBuilding = EABTSM73BeamDemoBuilding::E6TipOver;
	Preview->GenerationStopStage = EABTSM73BeamC3GenerationStage::DeviceAssembly;
	Preview->bShowEditorPreview = true;
	Preview->bSpawnRuntimeModulesInPIE = false;
	UGameplayStatics::FinishSpawningActor(Preview, FTransform::Identity);

	const int32 VisibleBrickCount = Preview->WoodPreview->GetInstanceCount()
		+ Preview->StonePreview->GetInstanceCount()
		+ Preview->IronPreview->GetInstanceCount()
		+ Preview->GlassPreview->GetInstanceCount();
	const int32 VisibleDeviceCount =
		Preview->ExplosiveDevicePreview->GetInstanceCount()
		+ Preview->PistonDevicePreview->GetInstanceCount();
	TestTrue(TEXT("Editor Stage 5.5 route accepts"),
		Preview->GetSummaryForValidation().bAccepted);
	TestEqual(TEXT("Editor route preserves every Stage-5 brick"),
		VisibleBrickCount, DirectResult.Stage5.Bricks.Num());
	TestEqual(TEXT("Editor route exposes the device ledger"),
		Preview->GetCompiledDevicesForValidation().Num(), DirectResult.Devices.Num());
	TestEqual(TEXT("Editor route renders every device asset"),
		VisibleDeviceCount, DirectResult.Devices.Num());
	TestEqual(TEXT("Editor route preserves device assembly identity"),
		Preview->GetSummaryForValidation().DeviceAssemblyHash,
		DirectResult.Summary.DeviceAssemblyHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73StableBuildingParticipationTest,
	"ABTS.M73A.StableBuildingParticipation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73StableBuildingParticipationTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamD1Tests;
	FBeamD1TestWorld WorldWrapper;
	if (!WorldWrapper.Create())
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	FActorSpawnParameters SystemParams;
	SystemParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM7BuildingMaterialSystem* MaterialSystem =
		World->SpawnActor<AABTSM7BuildingMaterialSystem>(
			AABTSM7BuildingMaterialSystem::StaticClass(),
			FTransform::Identity, SystemParams);
	if (!TestNotNull(TEXT("Participation MaterialSystem"), MaterialSystem))
	{
		return false;
	}

	const auto SpawnWithFlags = [World](
		const bool bPIERuntime,
		const bool bSlingshotGate)
	{
		FTransform Transform = FTransform::Identity;
		AABTSM73StableBuildingActor* Actor =
			World->SpawnActorDeferred<AABTSM73StableBuildingActor>(
				AABTSM73StableBuildingActor::StaticClass(), Transform,
				nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Actor == nullptr) return Actor;
		FBoolProperty* RuntimeProperty = FindFProperty<FBoolProperty>(
			AABTSM73StableBuildingActor::StaticClass(),
			TEXT("bParticipateInPIERuntime"));
		FBoolProperty* GateProperty = FindFProperty<FBoolProperty>(
			AABTSM73StableBuildingActor::StaticClass(),
			TEXT("bParticipateInSlingshotValidationGate"));
		if (RuntimeProperty != nullptr)
		{
			RuntimeProperty->SetPropertyValue_InContainer(Actor, bPIERuntime);
		}
		if (GateProperty != nullptr)
		{
			GateProperty->SetPropertyValue_InContainer(Actor, bSlingshotGate);
		}
		UGameplayStatics::FinishSpawningActor(Actor, Transform);
		return Actor;
	};

	AABTSM73StableBuildingActor* PreviewOnly = SpawnWithFlags(false, true);
	if (!TestNotNull(TEXT("Preview-only StableBuildingActor"), PreviewOnly))
	{
		return false;
	}
	PreviewOnly->InitializeRuntimeBuilding(MaterialSystem);
	TestFalse(TEXT("Preview-only fixture skips PIE runtime"),
		PreviewOnly->ShouldParticipateInPIERuntime());
	TestEqual(TEXT("Preview-only fixture is not required by startup physics"),
		PreviewOnly->GetIdleValidationState(),
		EABTSM73IdleValidationState::NotRequired);

	AABTSM73StableBuildingActor* GateExempt = SpawnWithFlags(true, false);
	if (!TestNotNull(TEXT("Gate-exempt StableBuildingActor"), GateExempt))
	{
		return false;
	}
	TestTrue(TEXT("Gate-exempt fixture can still participate in PIE runtime"),
		GateExempt->ShouldParticipateInPIERuntime());
	TestFalse(TEXT("Gate-exempt fixture opts out of slingshot validation"),
		GateExempt->ShouldParticipateInSlingshotValidationGate());
	TestEqual(TEXT("Gate exemption preserves the internal diagnostic state"),
		GateExempt->GetRawIdleValidationStateForValidation(),
		EABTSM73IdleValidationState::Pending);
	TestEqual(TEXT("Gate exemption maps its public state to NotRequired"),
		GateExempt->GetIdleValidationState(),
		EABTSM73IdleValidationState::NotRequired);
	return true;
}

#endif
