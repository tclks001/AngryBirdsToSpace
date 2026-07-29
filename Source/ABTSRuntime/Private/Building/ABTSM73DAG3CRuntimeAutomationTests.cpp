// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSRuntime.h"
#include "Building/ABTSM73StableBuildingActor.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/ABTSM7GameMode.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "TestStage/ABTSM71TestStageActors.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	class FABTSM73DAG3CRuntimeTestWorld final : public FTestWorldWrapper
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
				TEXT("ABTSM73DAG3CRuntimeDamageWorld"),
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
			FWorldContext& WorldContext =
				GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.OwningGameInstance = GameInstance;
			WorldContext.SetCurrentWorld(TestWorld);
			TestWorld->SetGameInstance(GameInstance);
			GameInstance->Init();
			return true;
		}
	};

	const FABTSM7MaterialProfile* FindMaterialProfile(
		const TConstArrayView<FABTSM7MaterialProfile> Profiles,
		const EABTSM7BuildingMaterial Material)
	{
		return Profiles.FindByPredicate(
			[Material](const FABTSM7MaterialProfile& Profile)
			{
				return Profile.Material == Material;
			});
	}

	bool DisableIdleRollout(AABTSM73StableBuildingActor& Building)
	{
		FBoolProperty* RunIdleProperty = FindFProperty<FBoolProperty>(
			AABTSM73StableBuildingActor::StaticClass(),
			TEXT("bRunIdleChaosValidation"));
		if (RunIdleProperty == nullptr)
		{
			return false;
		}
		RunIdleProperty->SetPropertyValue_InContainer(&Building, false);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3CWeakNodeDamageRoutingTest,
	"ABTS.M73DAG3.C.Runtime.WeakNodeDamageRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3CWeakNodeDamageRoutingTest::RunTest(
	const FString& Parameters)
{
	constexpr int32 BuildingSeed = 1034266606;
	constexpr float BelowDamageThresholdSpeedCMPerSec = 59.0f;

	FABTSM73DAG3CRuntimeTestWorld WorldWrapper;
	if (!WorldWrapper.CreatePhysicsWorld())
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	if (!TestNotNull(TEXT("DAG3-C physics test world"), World))
	{
		return false;
	}
	TestNotNull(TEXT("DAG3-C physics scene exists"), World->GetPhysicsScene());

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
	if (!TestNotNull(TEXT("DAG3-C planar physics stage"), Stage)
		|| !TestNotNull(TEXT("DAG3-C material system"), MaterialSystem))
	{
		return false;
	}

	FABTSM7TaskGraphBuildingProfile AuthoredProfile =
		FABTSM7TaskGraphDAG23ProfileResolver::MakeDefaultProfile(
			EABTSM3TaskType::Workshop,
			EABTSM7BuildingMaterial::Wood);
	AuthoredProfile.GenerationSettings.BuildingSeed = BuildingSeed;
	AuthoredProfile.DAGGenerationSettings.BuildingSeed = BuildingSeed;
	AuthoredProfile.DAGFailureFrontierSettings.bEnableAnalysis = true;
	AuthoredProfile.DAGFailureFrontierSettings
		.bEnableGeneralizedSmallCutSearch = true;
	AuthoredProfile.DAGFailurePatternSettings.bEnableGeometryRewrite = true;
	AuthoredProfile.DAGFailurePatternSettings.Pattern =
		EABTSM73DAGFailurePattern::InternalSingleSupport;
	AuthoredProfile.DAGFailurePlayabilitySettings
		.bEnablePlayabilityRouting = true;

	FABTSM7TaskGraphBuildingProfile RuntimeProfile;
	bool bMigratedLegacy = true;
	if (!TestTrue(
		TEXT("Explicit DAG3-C Workshop profile resolves"),
		FABTSM7TaskGraphDAG23ProfileResolver::ResolveRuntimeProfile(
			EABTSM3TaskType::Workshop,
			AuthoredProfile,
			RuntimeProfile,
			bMigratedLegacy)))
	{
		return false;
	}
	TestFalse(
		TEXT("Explicit DAG3-C profile is not a legacy migration"),
		bMigratedLegacy);

	AABTSM73StableBuildingActor* Building =
		World->SpawnActorDeferred<AABTSM73StableBuildingActor>(
			AABTSM73StableBuildingActor::StaticClass(),
			FTransform::Identity,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("DAG3-C runtime building"), Building))
	{
		return false;
	}
	Building->ConfigureTaskGraphGeneration(
		RuntimeProfile.GenerationSettings,
		RuntimeProfile.DAGGenerationSettings,
		RuntimeProfile.DAGLayoutSettings,
		RuntimeProfile.DAGFailureFrontierSettings,
		RuntimeProfile.DAGFailurePatternSettings,
		RuntimeProfile.DAGFailurePlayabilitySettings,
		RuntimeProfile.DifficultySettings);
	if (!TestTrue(
		TEXT("Runtime damage test disables idle Chaos rollout"),
		DisableIdleRollout(*Building)))
	{
		return false;
	}
	UGameplayStatics::FinishSpawningActor(Building, FTransform::Identity);

	if (!WorldWrapper.BeginPlayInTestWorld())
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}
	Building->InitializeRuntimeBuilding(MaterialSystem);

	const FABTSM73GenerationSummary& Summary =
		Building->GetGenerationSummary();
	const FABTSM73DAGFailurePlayabilityResult& Playability =
		Building->GetDAGFailurePlayabilityResultForValidation();
	TestTrue(
		FString::Printf(
			TEXT("DAG3-C runtime generation accepted: %s"),
			*Summary.RejectReason),
		Summary.bAccepted);
	TestTrue(TEXT("DAG3-C certification is enabled"),
		Summary.bDAGFailurePlayabilityEnabled);
	TestTrue(
		FString::Printf(
			TEXT("DAG3-C runtime candidate is playable: %s"),
			*Playability.RejectReason),
		Summary.bDAGFailurePlayable && Playability.bPlayable);
	TestEqual(TEXT("DAG3-C binds exactly one WeakPoint"),
		Summary.WeakPointCount, 1);
	TestEqual(TEXT("DAG3-C result contains one weak NodeId"),
		Playability.WeakNodeIds.Num(), 1);
	TestEqual(
		TEXT("Runtime test does not start the idle rollout"),
		Building->GetIdleValidationState(),
		EABTSM73IdleValidationState::NotRequired);
	if (!Summary.bAccepted
		|| !Playability.bPlayable
		|| Playability.WeakNodeIds.Num() != 1)
	{
		return false;
	}

	const int32 WeakNodeId = Playability.WeakNodeIds[0];
	TestEqual(TEXT("Generation summary names the certified weak NodeId"),
		Summary.PrimaryWeakPointNodeId, WeakNodeId);
	AABTSM7BuildingModule* WeakModule =
		Building->FindRuntimeModuleForNodeForValidation(WeakNodeId);
	if (!TestNotNull(TEXT("Weak NodeId maps to a real runtime module"),
		WeakModule))
	{
		return false;
	}
	UStaticMeshComponent* WeakMesh = WeakModule->GetMeshComponent();
	if (!TestNotNull(TEXT("Weak runtime module has a mesh"), WeakMesh))
	{
		return false;
	}

	TArray<FABTSM7MaterialProfile> CopiedProfiles;
	MaterialSystem->CopyMaterialProfiles(CopiedProfiles);
	const FABTSM7MaterialProfile* WoodProfile = FindMaterialProfile(
		CopiedProfiles,
		EABTSM7BuildingMaterial::Wood);
	if (!TestNotNull(TEXT("Runtime material system exposes its Wood profile"),
		WoodProfile))
	{
		return false;
	}
	TestEqual(TEXT("Weak module keeps the authored Wood material"),
		WeakModule->GetBuildingMaterial(), WoodProfile->Material);
	TestEqual(TEXT("Weak module BreakDamage uses the copied runtime profile"),
		WeakModule->GetBreakDamage(), WoodProfile->BreakDamage);
	TestEqual(TEXT("DAG3-C material identity uses the runtime profile"),
		Playability.Material, WoodProfile->Material);

	AABTSM7BuildingModule* NeighborModule = nullptr;
	float NeighborDistanceSquared = BIG_NUMBER;
	for (TActorIterator<AABTSM7BuildingModule> It(World); It; ++It)
	{
		AABTSM7BuildingModule* Candidate = *It;
		if (Candidate == WeakModule
			|| Candidate->GetOwner() != MaterialSystem)
		{
			continue;
		}
		const float DistanceSquared = FVector::DistSquared(
			Candidate->GetActorLocation(),
			WeakModule->GetActorLocation());
		if (DistanceSquared < NeighborDistanceSquared)
		{
			NeighborDistanceSquared = DistanceSquared;
			NeighborModule = Candidate;
		}
	}
	if (!TestNotNull(
		TEXT("Weak module has an untargeted adjacent runtime module"),
		NeighborModule))
	{
		return false;
	}
	TestEqual(TEXT("Adjacent module starts undamaged"),
		NeighborModule->GetCurrentDamage(), 0.0f);

	const FVector IncomingDirection =
		Playability.AcceptedAttackDirectionLocal.GetSafeNormal();
	TestFalse(TEXT("Certified attack direction is non-zero"),
		IncomingDirection.IsNearlyZero());
	TestTrue(
		TEXT("59 cm/s impact is routed to the weak runtime module"),
		MaterialSystem->HandleBirdImpact(
			WeakMesh,
			INDEX_NONE,
			BelowDamageThresholdSpeedCMPerSec,
			IncomingDirection * BelowDamageThresholdSpeedCMPerSec,
			EABTSBirdId::Red));
	TestEqual(TEXT("59 cm/s impact adds no weak-module damage"),
		WeakModule->GetCurrentDamage(), 0.0f);
	TestFalse(TEXT("59 cm/s impact does not destroy the weak module"),
		WeakModule->IsActorBeingDestroyed());
	TestEqual(TEXT("Untargeted adjacent module remains undamaged"),
		NeighborModule->GetCurrentDamage(), 0.0f);

	const int32 ExpectedHitCount = FMath::Max(
		1,
		FMath::CeilToInt(
			WoodProfile->BreakDamage
			/ WoodProfile->DamageAtBreakSpeed));
	TestEqual(TEXT("DAG3-C hit estimate uses the copied runtime profile"),
		Playability.EstimatedHits, ExpectedHitCount);
	TestEqual(TEXT("Generation summary keeps the DAG3-C hit estimate"),
		Summary.EstimatedWeakPointHits, ExpectedHitCount);
	for (int32 HitIndex = 1; HitIndex < ExpectedHitCount; ++HitIndex)
	{
		TestTrue(
			FString::Printf(
				TEXT("Break-speed hit %d routes to the weak module"),
				HitIndex),
			MaterialSystem->HandleBirdImpact(
				WeakMesh,
				INDEX_NONE,
				WoodProfile->BreakSpeedCMPerSec,
				IncomingDirection * WoodProfile->BreakSpeedCMPerSec,
				EABTSBirdId::Red));
		TestFalse(
			FString::Printf(
				TEXT("Weak module survives pre-final hit %d/%d"),
				HitIndex,
				ExpectedHitCount),
			WeakModule->IsActorBeingDestroyed());
	}
	TestFalse(TEXT("Weak module is alive immediately before the final hit"),
		WeakModule->IsActorBeingDestroyed());
	TestTrue(
		FString::Printf(
			TEXT("Final break-speed hit %d routes to the weak module"),
			ExpectedHitCount),
		MaterialSystem->HandleBirdImpact(
			WeakMesh,
			INDEX_NONE,
			WoodProfile->BreakSpeedCMPerSec,
			IncomingDirection * WoodProfile->BreakSpeedCMPerSec,
			EABTSBirdId::Red));
	TestTrue(
		FString::Printf(
			TEXT("Weak module is destroyed exactly on hit %d = ceil(%.1f/%.1f)"),
			ExpectedHitCount,
			WoodProfile->BreakDamage,
			WoodProfile->DamageAtBreakSpeed),
		WeakModule->IsActorBeingDestroyed());
	TestFalse(TEXT("Final weak-module hit does not destroy its neighbor"),
		NeighborModule->IsActorBeingDestroyed());
	TestEqual(TEXT("Final weak-module hit does not damage its neighbor"),
		NeighborModule->GetCurrentDamage(), 0.0f);

	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7.3-DAG3C][RuntimeDamage][Complete] WeakNode=%d Material=%d BreakDamage=%.1f DamageAtBreakSpeed=%.1f Hits=%d NeighborDamage=%.1f"),
		WeakNodeId,
		static_cast<int32>(WoodProfile->Material),
		WoodProfile->BreakDamage,
		WoodProfile->DamageAtBreakSpeed,
		ExpectedHitCount,
		NeighborModule->GetCurrentDamage());
	WorldWrapper.ForwardErrorMessages(this);
	return !HasAnyErrors();
}

#endif
