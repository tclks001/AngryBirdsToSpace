// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSRuntime.h"
#include "Building/ABTSM73StableBuildingActor.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM7BuildingModule.h"
#include "ABTSM7PenetrationValidator.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/ABTSM7GameMode.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Tests/AutomationCommon.h"
#include "TestStage/ABTSM71TestStageActors.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	class FABTSM73PhysicsTestWorld final : public FTestWorldWrapper
	{
	public:
		bool CreatePhysicsWorld()
		{
			if (TestWorld != nullptr)
			{
				ReportFailure(TEXT("Physics test world already exists"));
				return false;
			}
			if (GEngine == nullptr)
			{
				ReportFailure(TEXT("GEngine is unavailable"));
				return false;
			}

			UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
			UWorld::InitializationValues InitializationValues;
			InitializationValues
				.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(true)
				.ShouldSimulatePhysics(true)
				.EnableTraceCollision(true)
				.CreateNavigation(false)
				.CreateAISystem(false)
				.CreateFXSystem(false)
				.SetDefaultGameMode(AGameModeBase::StaticClass());
			TestWorld = UWorld::CreateWorld(
				EWorldType::Game,
				false,
				TEXT("ABTSM73ArchPhysicsTestWorld"),
				nullptr,
				true,
				ERHIFeatureLevel::Num,
				&InitializationValues);
			if (TestWorld == nullptr)
			{
				ReportFailure(TEXT("Failed to create a physics-enabled world"));
				return false;
			}

			TestWorld->SetShouldTick(false);
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.OwningGameInstance = GameInstance;
			WorldContext.SetCurrentWorld(TestWorld);
			TestWorld->SetGameInstance(GameInstance);
			GameInstance->Init();
			return true;
		}
	};

	AABTSM73StableBuildingActor* SpawnArch(
		UWorld& World,
		const FTransform& Transform,
		const FABTSM73GenerationSettings& GenerationSettings)
	{
		AABTSM73StableBuildingActor* Building =
			World.SpawnActorDeferred<AABTSM73StableBuildingActor>(
				AABTSM73StableBuildingActor::StaticClass(),
				Transform,
				nullptr,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Building == nullptr)
		{
			return nullptr;
		}

		FABTSM73DAGGenerationSettings DAGSettings;
		DAGSettings.BuildingSeed = GenerationSettings.BuildingSeed;
		DAGSettings.Preset = EABTSM73DAGPreset::Arch;
		FABTSM73DAGLayoutSettings LayoutSettings;
		FABTSM73DAGFailureFrontierSettings FrontierSettings;
		FABTSM73DAGFailurePatternSettings PatternSettings;
		FABTSM73DifficultySettings DifficultySettings;
		Building->ConfigureTaskGraphGeneration(
			GenerationSettings,
			DAGSettings,
			LayoutSettings,
			FrontierSettings,
			PatternSettings,
			DifficultySettings);
		UGameplayStatics::FinishSpawningActor(Building, Transform);
		return Building;
	}

	AABTSM73StableBuildingActor* SpawnDAG3BWorkshop(
		UWorld& World,
		const FTransform& Transform,
		const EABTSM73DAGFailurePattern Pattern)
	{
		AABTSM73StableBuildingActor* Building =
			World.SpawnActorDeferred<AABTSM73StableBuildingActor>(
				AABTSM73StableBuildingActor::StaticClass(),
				Transform,
				nullptr,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Building == nullptr)
		{
			return nullptr;
		}

		FABTSM7TaskGraphBuildingProfile Profile =
			FABTSM7TaskGraphDAG23ProfileResolver::MakeDefaultProfile(
				EABTSM3TaskType::Workshop,
				EABTSM7BuildingMaterial::Wood);
		Profile.GenerationSettings.BuildingSeed = 1034266606;
		Profile.DAGGenerationSettings.BuildingSeed = 1034266606;
		Profile.DAGFailureFrontierSettings.bEnableAnalysis = true;
		Profile.DAGFailurePatternSettings.bEnableGeometryRewrite = true;
		Profile.DAGFailurePatternSettings.Pattern = Pattern;
		Building->ConfigureTaskGraphGeneration(
			Profile.GenerationSettings,
			Profile.DAGGenerationSettings,
			Profile.DAGLayoutSettings,
			Profile.DAGFailureFrontierSettings,
			Profile.DAGFailurePatternSettings,
			Profile.DifficultySettings);
		UGameplayStatics::FinishSpawningActor(Building, Transform);
		return Building;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73ArchChaosPlanarIdleTest,
	"ABTS.M73DAG.Chaos.Arch7301PlanarIdle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73ArchChaosPlanarIdleTest::RunTest(const FString& Parameters)
{
	constexpr int32 BuildingCount = 3;
	constexpr int32 ExpectedModulesPerBuilding = 33;
	constexpr float FixedDeltaSeconds = 1.0f / 30.0f;
	constexpr int32 MaximumTicks = 210;

	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7.3-ChaosTest][Begin] Buildings=%d FPS=30 Seed=7301 PositionIterations=32 VelocityIterations=8"),
		BuildingCount);

	FABTSM73PhysicsTestWorld WorldWrapper;
	if (!WorldWrapper.CreatePhysicsWorld())
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	if (!TestNotNull(TEXT("Physics test world"), World))
	{
		return false;
	}
	TestNotNull(TEXT("Physics scene exists"), World->GetPhysicsScene());

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM71PhysicsTestStage* Stage =
		World->SpawnActorDeferred<AABTSM71PhysicsTestStage>(
			AABTSM71PhysicsTestStage::StaticClass(),
			FTransform::Identity,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Stage != nullptr)
	{
		UGameplayStatics::FinishSpawningActor(Stage, FTransform::Identity);
	}
	AABTSM7BuildingMaterialSystem* MaterialSystem =
		World->SpawnActor<AABTSM7BuildingMaterialSystem>(
			AABTSM7BuildingMaterialSystem::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	TestNotNull(TEXT("Planar physics stage"), Stage);
	if (!TestNotNull(TEXT("M7 material system"), MaterialSystem))
	{
		return false;
	}

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	AStaticMeshActor* GravityProbe = World->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(),
		FTransform(FVector(4800.0f, 4800.0f, 1500.0f)),
		SpawnParameters);
	if (!TestNotNull(TEXT("Gravity probe"), GravityProbe)
		|| !TestNotNull(TEXT("Gravity probe mesh"), CubeMesh))
	{
		return false;
	}
	UStaticMeshComponent* ProbeMesh = GravityProbe->GetStaticMeshComponent();
	ProbeMesh->SetMobility(EComponentMobility::Movable);
	ProbeMesh->SetStaticMesh(CubeMesh);
	ProbeMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ProbeMesh->SetSimulatePhysics(true);
	ProbeMesh->SetEnableGravity(true);

	FABTSM73GenerationSettings GenerationSettings;
	GenerationSettings.BuildingSeed = 7301;
	GenerationSettings.GenerationAlgorithm =
		EABTSM73GenerationAlgorithm::RecursiveSupportDAG;
	GenerationSettings.bGenerateStructuralWeakness = false;
	GenerationSettings.ChaosPositionSolverIterationCount = 32;
	GenerationSettings.ChaosVelocitySolverIterationCount = 8;

	TArray<AABTSM73StableBuildingActor*> Buildings;
	for (int32 Index = 0; Index < BuildingCount; ++Index)
	{
		const FTransform Transform(FVector(
			0.0f,
			(static_cast<float>(Index) - 1.0f) * 1600.0f,
			0.0f));
		Buildings.Add(SpawnArch(*World, Transform, GenerationSettings));
	}
	if (Buildings.Contains(nullptr))
	{
		AddError(TEXT("Failed to spawn all Arch stress buildings"));
		return false;
	}
	if (!WorldWrapper.BeginPlayInTestWorld())
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}
	for (AABTSM73StableBuildingActor* Building : Buildings)
	{
		Building->InitializeRuntimeBuilding(MaterialSystem);
	}

	for (AABTSM73StableBuildingActor* Building : Buildings)
	{
		const FABTSM73GenerationSummary& Summary =
			Building->GetGenerationSummary();
		TestTrue(TEXT("Arch runtime generation accepted"), Summary.bAccepted);
		TestTrue(TEXT("Arch runtime ground is planar"), Summary.bPlanar);
		TestEqual(TEXT("Arch brick count"), Summary.BrickCount, 33);
		TestEqual(TEXT("Arch support edge count"), Summary.SupportEdgeCount, 48);
		TestEqual(TEXT("Arch ground-node count"), Summary.GroundNodeCount, 3);
		TestEqual(TEXT("Arch macro count"), Summary.DAGMacroNodeCount, 9);
		TestEqual(TEXT("Arch sparse-support count"), Summary.DAGSelectedSupportCount, 8);
		TestEqual(
			TEXT("Arch topology hash"),
			Summary.DAGTopologyHash,
			static_cast<int64>(2113728967u));
		TestEqual(
			TEXT("Arch starts the dynamic idle gate"),
			Building->GetIdleValidationState(),
			EABTSM73IdleValidationState::Running);
	}

	TArray<AABTSM7BuildingModule*> RuntimeModules;
	for (TActorIterator<AABTSM7BuildingModule> It(World); It; ++It)
	{
		RuntimeModules.Add(*It);
	}
	TestEqual(
		TEXT("All Arch runtime modules spawned"),
		RuntimeModules.Num(),
		BuildingCount * ExpectedModulesPerBuilding);
	for (AABTSM7BuildingModule* Module : RuntimeModules)
	{
		const FBodyInstance* BodyInstance =
			Module->GetMeshComponent()->GetBodyInstance();
		if (!TestNotNull(TEXT("Runtime module BodyInstance"), BodyInstance))
		{
			continue;
		}
		TestEqual(
			TEXT("Per-body position iterations"),
			BodyInstance->GetPositionSolverIterationCount(),
			GenerationSettings.ChaosPositionSolverIterationCount);
		TestEqual(
			TEXT("Per-body velocity iterations"),
			BodyInstance->GetVelocitySolverIterationCount(),
			GenerationSettings.ChaosVelocitySolverIterationCount);
		TestTrue(
			TEXT("Runtime module is simulating during idle validation"),
			Module->GetMeshComponent()->IsSimulatingPhysics());
	}

	const float InitialProbeZ = GravityProbe->GetActorLocation().Z;
	for (int32 TickIndex = 0; TickIndex < 8; ++TickIndex)
	{
		if (!WorldWrapper.TickTestWorld(FixedDeltaSeconds))
		{
			WorldWrapper.ForwardErrorMessages(this);
			return false;
		}
	}
	const float ProbeDropCM =
		InitialProbeZ - GravityProbe->GetActorLocation().Z;
	TestTrue(
		FString::Printf(
			TEXT("Physics scene advances; probe drop %.2f cm"),
			ProbeDropCM),
		ProbeDropCM > 10.0f);
	TestTrue(
		TEXT("Gravity probe has downward velocity"),
		ProbeMesh->GetPhysicsLinearVelocity().Z < -50.0f);

	int32 AcceptedCount = 0;
	int32 RejectedCount = 0;
	for (int32 TickIndex = 8; TickIndex < MaximumTicks; ++TickIndex)
	{
		if (!WorldWrapper.TickTestWorld(FixedDeltaSeconds))
		{
			WorldWrapper.ForwardErrorMessages(this);
			return false;
		}
		AcceptedCount = 0;
		RejectedCount = 0;
		for (const AABTSM73StableBuildingActor* Building : Buildings)
		{
			if (Building->GetIdleValidationState()
				== EABTSM73IdleValidationState::Accepted)
			{
				++AcceptedCount;
			}
			else if (Building->GetIdleValidationState()
				== EABTSM73IdleValidationState::Rejected)
			{
				++RejectedCount;
			}
		}
		if (AcceptedCount == BuildingCount || RejectedCount > 0)
		{
			break;
		}
	}

	TestEqual(TEXT("No Arch idle gate rejected"), RejectedCount, 0);
	TestEqual(
		TEXT("Every Arch idle gate accepted at 30 FPS"),
		AcceptedCount,
		BuildingCount);
	int32 RemainingModuleCount = 0;
	for (TActorIterator<AABTSM7BuildingModule> It(World); It; ++It)
	{
		++RemainingModuleCount;
	}
	TestEqual(
		TEXT("Accepted Arch modules remain present"),
		RemainingModuleCount,
		BuildingCount * ExpectedModulesPerBuilding);

	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7.3-ChaosTest][Complete] Accepted=%d Rejected=%d Modules=%d ProbeDrop=%.2f"),
		AcceptedCount,
		RejectedCount,
		RemainingModuleCount,
		ProbeDropCM);
	WorldWrapper.ForwardErrorMessages(this);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3BPatternMatrixPlanarIdleTest,
	"ABTS.M73DAG3.Chaos.PatternMatrixPlanarIdle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3BPatternMatrixPlanarIdleTest::RunTest(
	const FString& Parameters)
{
	struct FPatternExpectation
	{
		EABTSM73DAGFailurePattern Pattern;
		EABTSM73DAGFailureMotion Motion;
		int32 PivotCount;
	};
	const FPatternExpectation Expectations[] = {
		{
			EABTSM73DAGFailurePattern::InternalSingleSupport,
			EABTSM73DAGFailureMotion::Drop,
			0
		},
		{
			EABTSM73DAGFailurePattern::InternalAsymmetricDualSupport,
			EABTSM73DAGFailureMotion::Tip,
			1
		},
		{
			EABTSM73DAGFailurePattern::InternalOffsetSeam,
			EABTSM73DAGFailureMotion::SlideThenTip,
			1
		}
	};
	constexpr float FixedDeltaSeconds = 1.0f / 30.0f;
	constexpr int32 MaximumTicks = 210;

	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7.3-DAG3B][ChaosMatrix][Begin] Buildings=%d FPS=30 Seed=1034266606 Fixture=Workshop"),
		static_cast<int32>(UE_ARRAY_COUNT(Expectations)));

	FABTSM73PhysicsTestWorld WorldWrapper;
	if (!WorldWrapper.CreatePhysicsWorld())
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	if (!TestNotNull(TEXT("DAG3-B physics test world"), World))
	{
		return false;
	}
	TestNotNull(TEXT("DAG3-B physics scene exists"), World->GetPhysicsScene());

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM71PhysicsTestStage* Stage =
		World->SpawnActorDeferred<AABTSM71PhysicsTestStage>(
			AABTSM71PhysicsTestStage::StaticClass(),
			FTransform::Identity,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Stage != nullptr)
	{
		UGameplayStatics::FinishSpawningActor(Stage, FTransform::Identity);
	}
	AABTSM7BuildingMaterialSystem* MaterialSystem =
		World->SpawnActor<AABTSM7BuildingMaterialSystem>(
			AABTSM7BuildingMaterialSystem::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	TestNotNull(TEXT("DAG3-B planar physics stage"), Stage);
	if (!TestNotNull(TEXT("DAG3-B material system"), MaterialSystem))
	{
		return false;
	}

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	AStaticMeshActor* GravityProbe = World->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(),
		FTransform(FVector(4800.0f, 4800.0f, 1500.0f)),
		SpawnParameters);
	if (!TestNotNull(TEXT("DAG3-B gravity probe"), GravityProbe)
		|| !TestNotNull(TEXT("DAG3-B gravity probe mesh"), CubeMesh))
	{
		return false;
	}
	UStaticMeshComponent* ProbeMesh = GravityProbe->GetStaticMeshComponent();
	ProbeMesh->SetMobility(EComponentMobility::Movable);
	ProbeMesh->SetStaticMesh(CubeMesh);
	ProbeMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ProbeMesh->SetSimulatePhysics(true);
	ProbeMesh->SetEnableGravity(true);

	TArray<AABTSM73StableBuildingActor*> Buildings;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Expectations); ++Index)
	{
		const FTransform Transform(FVector(
			0.0f,
			(static_cast<float>(Index) - 1.0f) * 1600.0f,
			0.0f));
		Buildings.Add(SpawnDAG3BWorkshop(
			*World,
			Transform,
			Expectations[Index].Pattern));
	}
	if (Buildings.Contains(nullptr))
	{
		AddError(TEXT("Failed to spawn all DAG3-B Workshop fixtures"));
		return false;
	}
	if (!WorldWrapper.BeginPlayInTestWorld())
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}
	for (AABTSM73StableBuildingActor* Building : Buildings)
	{
		Building->InitializeRuntimeBuilding(MaterialSystem);
	}

	TSet<int64> RealizedPatternHashes;
	int32 ExpectedRuntimeModuleCount = 0;
	for (int32 Index = 0; Index < Buildings.Num(); ++Index)
	{
		AABTSM73StableBuildingActor* Building = Buildings[Index];
		const FPatternExpectation& Expected = Expectations[Index];
		const FABTSM73GenerationSummary& Summary =
			Building->GetGenerationSummary();
		const FABTSM73DAGFailurePatternResult& PatternResult =
			Building->GetDAGFailurePatternResultForValidation();

		TestTrue(
			FString::Printf(TEXT("Pattern %d runtime generation accepted"),
				static_cast<int32>(Expected.Pattern)),
			Summary.bAccepted);
		TestTrue(
			FString::Printf(TEXT("Pattern %d runtime ground is planar"),
				static_cast<int32>(Expected.Pattern)),
			Summary.bPlanar);
		TestTrue(
			FString::Printf(TEXT("Pattern %d enables DAG3-A"),
				static_cast<int32>(Expected.Pattern)),
			Summary.bDAGFailureFrontierAnalysisEnabled);
		TestTrue(
			FString::Printf(TEXT("Pattern %d accepts a DAG3-A frontier"),
				static_cast<int32>(Expected.Pattern)),
			Summary.bDAGFailureFrontierAccepted);
		TestTrue(
			FString::Printf(TEXT("Pattern %d enables DAG3-B"),
				static_cast<int32>(Expected.Pattern)),
			Summary.bDAGFailurePatternEnabled);
		TestTrue(
			FString::Printf(TEXT("Pattern %d applies DAG3-B"),
				static_cast<int32>(Expected.Pattern)),
			Summary.bDAGFailurePatternApplied);
		TestEqual(
			FString::Printf(TEXT("Pattern %d preserves its explicit identity"),
				static_cast<int32>(Expected.Pattern)),
			static_cast<int32>(Summary.DAGFailurePattern),
			static_cast<int32>(Expected.Pattern));
		TestTrue(
			FString::Printf(TEXT("Pattern %d has a non-zero realized hash"),
				static_cast<int32>(Expected.Pattern)),
			Summary.DAGRealizedPatternHash != 0);
		TestTrue(
			FString::Printf(TEXT("Pattern %d has runtime modules"),
				static_cast<int32>(Expected.Pattern)),
			Summary.BrickCount > 0);
		TestEqual(
			FString::Printf(TEXT("Pattern %d does not create legacy WeakPoints"),
				static_cast<int32>(Expected.Pattern)),
			Summary.WeakPointCount,
			0);

		TestTrue(
			FString::Printf(TEXT("Pattern %d result is enabled"),
				static_cast<int32>(Expected.Pattern)),
			PatternResult.bEnabled);
		TestTrue(
			FString::Printf(TEXT("Pattern %d result is applied"),
				static_cast<int32>(Expected.Pattern)),
			PatternResult.bApplied);
		TestEqual(
			FString::Printf(TEXT("Pattern %d result identity"),
				static_cast<int32>(Expected.Pattern)),
			static_cast<int32>(PatternResult.Pattern),
			static_cast<int32>(Expected.Pattern));
		TestEqual(
			FString::Printf(TEXT("Pattern %d expected motion"),
				static_cast<int32>(Expected.Pattern)),
			static_cast<int32>(PatternResult.ExpectedMotion),
			static_cast<int32>(Expected.Motion));
		TestTrue(
			FString::Printf(TEXT("Pattern %d source hash is non-zero"),
				static_cast<int32>(Expected.Pattern)),
			PatternResult.SourceFrontierHash != 0);
		TestEqual(
			FString::Printf(TEXT("Pattern %d summary and result hashes agree"),
				static_cast<int32>(Expected.Pattern)),
			Summary.DAGRealizedPatternHash,
			static_cast<int64>(PatternResult.RealizedPatternHash));
		TestTrue(
			FString::Printf(TEXT("Pattern %d source and realized hashes differ"),
				static_cast<int32>(Expected.Pattern)),
			PatternResult.SourceFrontierHash
				!= PatternResult.RealizedPatternHash);
		TestEqual(
			FString::Printf(TEXT("Pattern %d has one weak support"),
				static_cast<int32>(Expected.Pattern)),
			PatternResult.WeakNodeIds.Num(),
			1);
		TestEqual(
			FString::Printf(TEXT("Pattern %d has the expected pivot count"),
				static_cast<int32>(Expected.Pattern)),
			PatternResult.RemainingSupportNodeIds.Num(),
			Expected.PivotCount);
		TestTrue(
			FString::Printf(TEXT("Pattern %d affects main-body nodes"),
				static_cast<int32>(Expected.Pattern)),
			!PatternResult.AffectedMainBodyNodeIds.IsEmpty());
		TestFalse(
			FString::Printf(TEXT("Pattern %d has a failure direction"),
				static_cast<int32>(Expected.Pattern)),
			PatternResult.ExpectedFailureDirectionLocal.IsNearlyZero());

		TestEqual(
			FString::Printf(TEXT("Pattern %d has one weak debug overlay"),
				static_cast<int32>(Expected.Pattern)),
			Building->GetDAG3BWeakDebugInstanceCount(),
			1);
		TestEqual(
			FString::Printf(TEXT("Pattern %d has the expected pivot overlays"),
				static_cast<int32>(Expected.Pattern)),
			Building->GetDAG3BPivotDebugInstanceCount(),
			Expected.PivotCount);
		TestTrue(
			FString::Printf(TEXT("Pattern %d exposes affected-body overlays"),
				static_cast<int32>(Expected.Pattern)),
			Building->GetDAG3BAffectedDebugInstanceCount() > 0);
		TestEqual(
			FString::Printf(TEXT("Pattern %d has a three-piece direction arrow"),
				static_cast<int32>(Expected.Pattern)),
			Building->GetDAG3BDirectionDebugInstanceCount(),
			3);
		TestEqual(
			FString::Printf(TEXT("Pattern %d starts the dynamic idle gate"),
				static_cast<int32>(Expected.Pattern)),
			Building->GetIdleValidationState(),
			EABTSM73IdleValidationState::Running);

		ExpectedRuntimeModuleCount += Summary.BrickCount;
		RealizedPatternHashes.Add(Summary.DAGRealizedPatternHash);
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7.3-DAG3B][ChaosMatrix][Fixture] Actor=%s Pattern=%d Motion=%d Bricks=%d Weak=%d Pivot=%d Affected=%d Direction=%d SourceHash=%u RealizedHash=%u"),
			*Building->GetName(),
			static_cast<int32>(PatternResult.Pattern),
			static_cast<int32>(PatternResult.ExpectedMotion),
			Summary.BrickCount,
			Building->GetDAG3BWeakDebugInstanceCount(),
			Building->GetDAG3BPivotDebugInstanceCount(),
			Building->GetDAG3BAffectedDebugInstanceCount(),
			Building->GetDAG3BDirectionDebugInstanceCount(),
			PatternResult.SourceFrontierHash,
			PatternResult.RealizedPatternHash);
	}
	TestEqual(
		TEXT("All three DAG3-B patterns have distinct realized hashes"),
		RealizedPatternHashes.Num(),
		static_cast<int32>(UE_ARRAY_COUNT(Expectations)));

	TArray<AABTSM7BuildingModule*> RuntimeModules;
	for (TActorIterator<AABTSM7BuildingModule> It(World); It; ++It)
	{
		RuntimeModules.Add(*It);
	}
	TestEqual(
		TEXT("Every DAG3-B runtime module spawned"),
		RuntimeModules.Num(),
		ExpectedRuntimeModuleCount);
	for (AABTSM7BuildingModule* Module : RuntimeModules)
	{
		if (!TestNotNull(TEXT("DAG3-B runtime module"), Module)
			|| !TestNotNull(
				TEXT("DAG3-B runtime module mesh"),
				Module->GetMeshComponent()))
		{
			continue;
		}
		const FBodyInstance* BodyInstance =
			Module->GetMeshComponent()->GetBodyInstance();
		if (!TestNotNull(
				TEXT("DAG3-B runtime module BodyInstance"),
				BodyInstance))
		{
			continue;
		}
		TestEqual(
			TEXT("DAG3-B per-body position iterations"),
			BodyInstance->GetPositionSolverIterationCount(),
			32);
		TestEqual(
			TEXT("DAG3-B per-body velocity iterations"),
			BodyInstance->GetVelocitySolverIterationCount(),
			8);
		TestTrue(
			TEXT("DAG3-B runtime module simulates during idle validation"),
			Module->GetMeshComponent()->IsSimulatingPhysics());
	}
	const FABTSM7PenetrationValidationStats Penetration =
		MaterialSystem->ValidateAndRepairPendingModules(RuntimeModules);
	TestEqual(
		TEXT("DAG3-B fixtures start without detected penetration"),
		Penetration.DetectedPairCount,
		0);
	TestEqual(
		TEXT("DAG3-B fixtures need no penetration repair"),
		Penetration.RepairCount,
		0);
	TestEqual(
		TEXT("DAG3-B fixtures have no large penetration errors"),
		Penetration.LargeErrorPairCount,
		0);
	TestEqual(
		TEXT("DAG3-B fixtures leave no small penetration errors"),
		Penetration.RemainingSmallPairCount,
		0);

	const float InitialProbeZ = GravityProbe->GetActorLocation().Z;
	for (int32 TickIndex = 0; TickIndex < 8; ++TickIndex)
	{
		if (!WorldWrapper.TickTestWorld(FixedDeltaSeconds))
		{
			WorldWrapper.ForwardErrorMessages(this);
			return false;
		}
	}
	const float ProbeDropCM =
		InitialProbeZ - GravityProbe->GetActorLocation().Z;
	TestTrue(
		FString::Printf(
			TEXT("DAG3-B physics scene advances; probe drop %.2f cm"),
			ProbeDropCM),
		ProbeDropCM > 10.0f);
	TestTrue(
		TEXT("DAG3-B gravity probe has downward velocity"),
		ProbeMesh->GetPhysicsLinearVelocity().Z < -50.0f);

	int32 AcceptedCount = 0;
	int32 RejectedCount = 0;
	for (int32 TickIndex = 8; TickIndex < MaximumTicks; ++TickIndex)
	{
		if (!WorldWrapper.TickTestWorld(FixedDeltaSeconds))
		{
			WorldWrapper.ForwardErrorMessages(this);
			return false;
		}
		AcceptedCount = 0;
		RejectedCount = 0;
		for (const AABTSM73StableBuildingActor* Building : Buildings)
		{
			if (Building->GetIdleValidationState()
				== EABTSM73IdleValidationState::Accepted)
			{
				++AcceptedCount;
			}
			else if (Building->GetIdleValidationState()
				== EABTSM73IdleValidationState::Rejected)
			{
				++RejectedCount;
			}
		}
		if (AcceptedCount == Buildings.Num() || RejectedCount > 0)
		{
			break;
		}
	}

	TestEqual(TEXT("No DAG3-B idle gate rejected"), RejectedCount, 0);
	TestEqual(
		TEXT("Every DAG3-B idle gate accepted at 30 FPS"),
		AcceptedCount,
		Buildings.Num());
	int32 RemainingModuleCount = 0;
	for (TActorIterator<AABTSM7BuildingModule> It(World); It; ++It)
	{
		++RemainingModuleCount;
	}
	TestEqual(
		TEXT("Accepted DAG3-B modules remain present"),
		RemainingModuleCount,
		ExpectedRuntimeModuleCount);

	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7.3-DAG3B][ChaosMatrix][Complete] Accepted=%d Rejected=%d Modules=%d PenetrationPairs=%d Repairs=%d RemainingSmall=%d ProbeDrop=%.2f"),
		AcceptedCount,
		RejectedCount,
		RemainingModuleCount,
		Penetration.DetectedPairCount,
		Penetration.RepairCount,
		Penetration.RemainingSmallPairCount,
		ProbeDropCM);
	WorldWrapper.ForwardErrorMessages(this);
	return !HasAnyErrors();
}

#endif
