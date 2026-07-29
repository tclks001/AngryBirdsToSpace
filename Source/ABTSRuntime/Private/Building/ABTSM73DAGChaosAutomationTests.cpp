// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSRuntime.h"
#include "Building/ABTSM73StableBuildingActor.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
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

#endif
