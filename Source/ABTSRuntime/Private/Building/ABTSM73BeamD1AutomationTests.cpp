// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ABTSM73BeamD1BrickCompiler.h"

#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

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

#endif
