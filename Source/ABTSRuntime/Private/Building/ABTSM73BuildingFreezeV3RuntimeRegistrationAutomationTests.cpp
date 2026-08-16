// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Building/ABTSM73BuildingFreezeV3RuntimeRegistration.h"

#include "Building/ABTSM73StableBuildingActor.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/ABTSM7GameMode.h"
#include "GameFramework/GameModeBase.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

namespace ABTSM73BuildingFreezeV3RuntimeRegistrationTests
{
	class FRuntimeRegistrationTestWorld final : public FTestWorldWrapper
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
			TestWorld = UWorld::CreateWorld(
				EWorldType::Game, false,
				TEXT("ABTSM73BuildingFreezeV3RuntimeRegistrationWorld"),
				nullptr, true, ERHIFeatureLevel::Num, &Values);
			if (TestWorld == nullptr)
			{
				ReportFailure(TEXT("Failed to create V3 runtime test world"));
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

	TArray<FABTSM73BuildingFreezeV3RuntimePlacement> MakeFixturePlacements()
	{
		const EABTSM73BeamDemoBuilding ComplexityOrder[] = {
			EABTSM73BeamDemoBuilding::E2DropTrigger,
			EABTSM73BeamDemoBuilding::E3SlideRelease,
			EABTSM73BeamDemoBuilding::E4TipOver,
			EABTSM73BeamDemoBuilding::E5SeamRelease,
			EABTSM73BeamDemoBuilding::E1ColumnBreak,
			EABTSM73BeamDemoBuilding::E6TipOver};
		TArray<FABTSM73BuildingFreezeV3RuntimePlacement> Placements;
		Placements.Reserve(UE_ARRAY_COUNT(ComplexityOrder));
		for (int32 EncounterSlot = 0;
			EncounterSlot < UE_ARRAY_COUNT(ComplexityOrder); ++EncounterSlot)
		{
			FABTSM73BuildingFreezeV3RuntimePlacement& Placement =
				Placements.AddDefaulted_GetRef();
			Placement.ComplexityId = ComplexityOrder[EncounterSlot];
			Placement.EncounterSlot = EncounterSlot;
			Placement.WorldTransform = FTransform(
				FQuat(FVector::UpVector,
					FMath::DegreesToRadians(EncounterSlot * 11.0)),
				FVector(EncounterSlot * 6000.0, EncounterSlot * 250.0, 0.0));
		}
		return Placements;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BuildingFreezeV3RuntimeFixturePlanTest,
	"ABTS.M73DAG.BuildingFreezeV3.RuntimeFixture.Plan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BuildingFreezeV3RuntimeFixturePlanTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BuildingFreezeV3RuntimeRegistrationTests;
	const EABTSM73BeamDemoBuilding ExpectedComplexities[] = {
		EABTSM73BeamDemoBuilding::E2DropTrigger,
		EABTSM73BeamDemoBuilding::E3SlideRelease,
		EABTSM73BeamDemoBuilding::E4TipOver,
		EABTSM73BeamDemoBuilding::E5SeamRelease,
		EABTSM73BeamDemoBuilding::E1ColumnBreak,
		EABTSM73BeamDemoBuilding::E6TipOver};
	const int32 ExpectedComplexityIndices[] = {1, 2, 3, 4, 0, 5};
	const EABTSM7BuildingMaterial ExpectedPrimaryMaterials[] = {
		EABTSM7BuildingMaterial::Wood,
		EABTSM7BuildingMaterial::Wood,
		EABTSM7BuildingMaterial::Stone,
		EABTSM7BuildingMaterial::Iron,
		EABTSM7BuildingMaterial::Stone,
		EABTSM7BuildingMaterial::Iron};

	TArray<FABTSM73BuildingFreezeV3RuntimePlacement> Placements =
		MakeFixturePlacements();
	FABTSM73BuildingFreezeV3RuntimePlan Plan;
	FString Error;
	if (!FABTSM73BuildingFreezeV3RuntimeRegistration::BuildFixturePlan(
		Placements, Plan, Error))
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("V3 runtime fixture plan is usable"), Plan.IsUsable());
	TestEqual(TEXT("Fixture authority is explicit"), Plan.Authority,
		FABTSM73BuildingFreezeV3RuntimeRegistration::FixtureAuthority);
	TestEqual(TEXT("Fixture cannot claim an M3 LayoutHash"),
		Plan.SourceLayoutHash, 0ull);
	TestEqual(TEXT("Runtime plan contains six buildings"),
		Plan.Entries.Num(), FABTSM73BuildingFreezeV3::ExpectedEntryCount);
	for (int32 EncounterSlot = 0;
		EncounterSlot < Plan.Entries.Num(); ++EncounterSlot)
	{
		const FABTSM73BuildingFreezeV3RuntimeEntry& Entry =
			Plan.Entries[EncounterSlot];
		TestEqual(*FString::Printf(
			TEXT("Encounter %d keeps its independent complexity id"),
			EncounterSlot), Entry.ComplexityId,
			ExpectedComplexities[EncounterSlot]);
		TestEqual(*FString::Printf(
			TEXT("Encounter %d keeps its complexity index"), EncounterSlot),
			Entry.ComplexityIndex, ExpectedComplexityIndices[EncounterSlot]);
		TestEqual(*FString::Printf(
			TEXT("Encounter %d keeps play order only in EncounterSlot"),
			EncounterSlot), Entry.EncounterSlot, EncounterSlot);
		TestEqual(*FString::Printf(
			TEXT("Encounter %d applies the frozen primary material"),
			EncounterSlot), Entry.PrimaryMaterial,
			ExpectedPrimaryMaterials[EncounterSlot]);
	}
	TestEqual(TEXT("E1 remains the lowest-complexity building"),
		Plan.Entries[4].ComplexityIndex, 0);
	TestEqual(TEXT("E1 independently appears at encounter slot four"),
		Plan.Entries[4].EncounterSlot, 4);
	TestEqual(TEXT("E1 has one Crystal cap"),
		Plan.Entries[4].Caps.Num(), 1);

	FABTSM73BuildingFreezeV3RuntimePlan RepeatedPlan;
	if (!FABTSM73BuildingFreezeV3RuntimeRegistration::BuildFixturePlan(
		Placements, RepeatedPlan, Error))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("Fixture placement hash is deterministic"),
		RepeatedPlan.RuntimePlacementHash, Plan.RuntimePlacementHash);
	TestEqual(TEXT("Runtime registration hash is deterministic"),
		RepeatedPlan.RegistrationResultHash, Plan.RegistrationResultHash);

	Swap(Placements[0].ComplexityId, Placements[4].ComplexityId);
	FABTSM73BuildingFreezeV3RuntimePlan RejectedPlan;
	TestFalse(TEXT("Complexity/encounter conflation fails closed"),
		FABTSM73BuildingFreezeV3RuntimeRegistration::BuildFixturePlan(
			Placements, RejectedPlan, Error));
	TestTrue(TEXT("Rejected fixture publishes no partial entries"),
		RejectedPlan.Entries.IsEmpty());
	AddInfo(FString::Printf(
		TEXT("BuildingFreezeV3RuntimePlan Buildings=%d Placement=%llu")
		TEXT(" ResultHash=%llu E1Complexity=0 E1Encounter=4")
		TEXT(" Authority=M7V3RuntimeFixture Chaos=NotEvaluated"),
		Plan.Entries.Num(), Plan.RuntimePlacementHash,
		Plan.RegistrationResultHash));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BuildingFreezeV3AtomicRuntimeRegistrationTest,
	"ABTS.M73DAG.BuildingFreezeV3.RuntimeFixture.AtomicRegistration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BuildingFreezeV3AtomicRuntimeRegistrationTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BuildingFreezeV3RuntimeRegistrationTests;
	TArray<FABTSM73BuildingFreezeV3RuntimePlacement> Placements =
		MakeFixturePlacements();
	FABTSM73BuildingFreezeV3RuntimePlan Plan;
	FString Error;
	if (!FABTSM73BuildingFreezeV3RuntimeRegistration::BuildFixturePlan(
		Placements, Plan, Error))
	{
		AddError(Error);
		return false;
	}
	const uint64 ExpectedResultHash = Plan.RegistrationResultHash;
	TArray<int32> ExpectedBrickCounts;
	TArray<int32> ExpectedModuleCounts;
	TArray<EABTSM7BuildingMaterial> ExpectedPrimaryMaterials;
	int32 ExpectedTotalModuleCount = 0;
	for (const FABTSM73BuildingFreezeV3RuntimeEntry& Entry : Plan.Entries)
	{
		ExpectedBrickCounts.Add(Entry.Bricks.Num());
		const int32 EntryModuleCount =
			Entry.Bricks.Num() + Entry.Devices.Num() + Entry.Caps.Num();
		ExpectedModuleCounts.Add(EntryModuleCount);
		ExpectedPrimaryMaterials.Add(Entry.PrimaryMaterial);
		ExpectedTotalModuleCount += EntryModuleCount;
	}

	FRuntimeRegistrationTestWorld WorldWrapper;
	if (!WorldWrapper.Create())
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	if (!TestNotNull(TEXT("V3 runtime automation world"), World))
	{
		return false;
	}
	AABTSM7BuildingMaterialSystem* MaterialSystem =
		World->SpawnActor<AABTSM7BuildingMaterialSystem>();
	if (!TestNotNull(TEXT("V3 runtime material system"), MaterialSystem))
	{
		return false;
	}

	FABTSM73BuildingFreezeV3RuntimePlan InvalidPlan = Plan;
	InvalidPlan.Entries[2].Bricks.Reset();
	TArray<TWeakObjectPtr<AABTSM73StableBuildingActor>> Actors;
	TestFalse(TEXT("Invalid V3 batch fails before any Actor is published"),
		FABTSM73BuildingFreezeV3RuntimeRegistration::SpawnStaticActors(
			*World, *MaterialSystem,
			AABTSM73StableBuildingActor::StaticClass(),
			MoveTemp(InvalidPlan), Actors, Error));
	TestTrue(TEXT("Invalid V3 batch returns no Actors"), Actors.IsEmpty());

	if (!FABTSM73BuildingFreezeV3RuntimeRegistration::SpawnStaticActors(
		*World, *MaterialSystem,
		AABTSM73StableBuildingActor::StaticClass(),
		MoveTemp(Plan), Actors, Error))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("Exactly six V3 runtime Actors register atomically"),
		Actors.Num(), FABTSM73BuildingFreezeV3::ExpectedEntryCount);
	int32 ActualTotalModuleCount = 0;
	for (int32 EncounterSlot = 0;
		EncounterSlot < Actors.Num(); ++EncounterSlot)
	{
		AABTSM73StableBuildingActor* Actor = Actors[EncounterSlot].Get();
		if (!TestNotNull(*FString::Printf(
			TEXT("V3 Actor %d"), EncounterSlot), Actor))
		{
			continue;
		}
		TestTrue(*FString::Printf(
			TEXT("V3 Actor %d accepted"), EncounterSlot),
			Actor->IsBuildingFreezeV3RuntimeRegistrationAccepted());
		TestEqual(*FString::Printf(
			TEXT("V3 Actor %d keeps encounter slot"), EncounterSlot),
			Actor->GetBuildingFreezeV3EncounterSlot(), EncounterSlot);
		TestEqual(*FString::Printf(
			TEXT("V3 Actor %d keeps complexity index"), EncounterSlot),
			Actor->GetBuildingFreezeV3ComplexityIndex(),
			Placements[EncounterSlot].ComplexityId
				== EABTSM73BeamDemoBuilding::E1ColumnBreak
				? 0
				: static_cast<int32>(Placements[EncounterSlot].ComplexityId) - 1);
		TestEqual(*FString::Printf(
			TEXT("V3 Actor %d keeps registration result"), EncounterSlot),
			Actor->GetBuildingFreezeV3RegistrationResultHash(),
			ExpectedResultHash);
		TestEqual(*FString::Printf(
			TEXT("V3 Actor %d material recipe reaches its body HISM"),
			EncounterSlot),
			Actor->GetBuildingFreezeV3BodyInstanceCount(
				ExpectedPrimaryMaterials[EncounterSlot]),
			ExpectedBrickCounts[EncounterSlot]);
		TestEqual(*FString::Printf(
			TEXT("V3 Actor %d runtime module count"), EncounterSlot),
			Actor->GetBuildingFreezeV3RuntimeModuleCount(),
			ExpectedModuleCounts[EncounterSlot]);
		FVector PresentationAnchor;
		int32 LiveModules = 0;
		TestTrue(*FString::Printf(
			TEXT("V3 Actor %d presentation anchor"), EncounterSlot),
			Actor->QueryLivePresentationAnchor(
				PresentationAnchor, LiveModules));
		TestEqual(*FString::Printf(
			TEXT("V3 Actor %d presents every body/device/cap"), EncounterSlot),
			LiveModules, ExpectedModuleCounts[EncounterSlot]);
		ActualTotalModuleCount +=
			Actor->GetBuildingFreezeV3RuntimeModuleCount();
	}
	TestEqual(TEXT("All V3 body bricks, devices and cap registered"),
		ActualTotalModuleCount, ExpectedTotalModuleCount);

	int32 RuntimeActorModuleCount = 0;
	int32 CrystalCapCount = 0;
	for (TActorIterator<AABTSM7BuildingModule> It(World); It; ++It)
	{
		++RuntimeActorModuleCount;
		CrystalCapCount += It->GetBuildingMaterial()
			== EABTSM7BuildingMaterial::Crystal ? 1 : 0;
		TestFalse(TEXT("V3 device/cap starts static"), It->IsDynamic());
	}
	TestEqual(TEXT("Six devices plus one Crystal cap are Actor modules"),
		RuntimeActorModuleCount, 7);
	TestEqual(TEXT("Runtime assembly contains exactly one Crystal cap"),
		CrystalCapCount, 1);
	MaterialSystem->BeginLaunchPhysics(
		false, FVector::ZeroVector, 0.0f, 0.0f);
	int32 DynamicCountAfterGlobalLaunch = 0;
	for (TActorIterator<AABTSM7BuildingModule> It(World); It; ++It)
	{
		DynamicCountAfterGlobalLaunch += It->IsDynamic() ? 1 : 0;
	}
	TestEqual(TEXT("Fixture modules are isolated from global launch"),
		DynamicCountAfterGlobalLaunch, 0);
	AddInfo(FString::Printf(
		TEXT("BuildingFreezeV3AtomicRuntime Buildings=%d Modules=%d")
		TEXT(" CrystalCaps=%d ResultHash=%llu")
		TEXT(" Authority=M7V3RuntimeFixture Chaos=NotEvaluated"),
		Actors.Num(), ActualTotalModuleCount, CrystalCapCount,
		ExpectedResultHash));

	for (const TWeakObjectPtr<AABTSM73StableBuildingActor>& WeakActor : Actors)
	{
		if (AABTSM73StableBuildingActor* Actor = WeakActor.Get())
		{
			Actor->RollbackBuildingFreezeV3RuntimeRegistration(
				TEXT("AutomationCleanup"));
		}
	}
	MaterialSystem->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM7SatellitePracticeE1CrystalBindingLifecycleTest,
	"ABTS.M73DAG.BuildingFreezeV3.E1CrystalBindingLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM7SatellitePracticeE1CrystalBindingLifecycleTest::RunTest(
	const FString& Parameters)
{
	using FAction = EABTSM7SatellitePracticeE1CrystalBindingAction;
	using FState = EABTSM7SatellitePracticeE1CrystalBindingState;

	FABTSM7SatellitePracticeE1CrystalBindingLifecycle DelayedRuntime;
	TestTrue(TEXT("Lifecycle starts once"), DelayedRuntime.Start(0.0));
	TestFalse(TEXT("A duplicate schedule cannot reset the timeout"),
		DelayedRuntime.Start(0.01));
	FABTSM7SatellitePracticeE1CrystalBindingObservation Observation;
	Observation.AcceptedStaticBuildingCount =
		FABTSM7SatellitePracticeE1CrystalBindingLifecycle::
			ExpectedStaticBuildingCount;
	Observation.E1OrderedUnionCount = 1;
	FString Reason;
	TestTrue(TEXT("A missing runtime remains a non-terminal wait"),
		DelayedRuntime.Advance(0.1, Observation, Reason) == FAction::Wait);
	TestEqual(TEXT("Missing runtime wait is diagnostic"), Reason,
		FString(TEXT("SatelliteRuntimePending")));
	TestTrue(TEXT("Waiting remains active"),
		DelayedRuntime.GetState() == FState::Waiting);

	Observation.SatelliteRuntimeCount = 1;
	TestTrue(TEXT("An existing but unready runtime keeps waiting"),
		DelayedRuntime.Advance(0.2, Observation, Reason) == FAction::Wait);
	TestEqual(TEXT("Unready runtime wait is diagnostic"), Reason,
		FString(TEXT("SatelliteRuntimeNotReady")));
	Observation.bSatelliteRuntimeReady = true;
	TestTrue(TEXT("A late ready runtime produces exactly one bind request"),
		DelayedRuntime.Advance(0.3, Observation, Reason) == FAction::Bind);
	TestTrue(TEXT("Bind request enters the binding state"),
		DelayedRuntime.GetState() == FState::Binding);
	DelayedRuntime.MarkBound();
	TestTrue(TEXT("Successful binding becomes terminal"),
		DelayedRuntime.GetState() == FState::Bound);
	const int32 AttemptsAfterBind = DelayedRuntime.GetAttemptCount();
	TestTrue(TEXT("A repeated callback after success is idempotent"),
		DelayedRuntime.Advance(0.4, Observation, Reason) == FAction::None);
	TestEqual(TEXT("Idempotent callback does not add an attempt"),
		DelayedRuntime.GetAttemptCount(), AttemptsAfterBind);

	FABTSM7SatellitePracticeE1CrystalBindingLifecycle TimeoutLifecycle;
	TestTrue(TEXT("Timeout lifecycle starts"),
		TimeoutLifecycle.Start(5.0));
	Observation.SatelliteRuntimeCount = 0;
	Observation.bSatelliteRuntimeReady = false;
	TestTrue(TEXT("Missing runtime fails closed only after timeout"),
		TimeoutLifecycle.Advance(
			5.0 + FABTSM7SatellitePracticeE1CrystalBindingLifecycle::
				TimeoutSeconds + 0.01,
			Observation, Reason) == FAction::Reject);
	TestEqual(TEXT("Timeout publishes a precise reason"), Reason,
		FString(TEXT("SatelliteRuntimeTimeout")));

	FABTSM7SatellitePracticeE1CrystalBindingLifecycle MultipleRuntime;
	TestTrue(TEXT("Multiple-runtime lifecycle starts"),
		MultipleRuntime.Start(0.0));
	Observation.SatelliteRuntimeCount = 2;
	Observation.bSatelliteRuntimeReady = true;
	TestTrue(TEXT("Multiple runtimes fail closed immediately"),
		MultipleRuntime.Advance(0.1, Observation, Reason)
			== FAction::Reject);
	TestEqual(TEXT("Multiple-runtime reason is diagnostic"), Reason,
		FString(TEXT("MultipleSatelliteRuntimes")));

	FABTSM7SatellitePracticeE1CrystalBindingLifecycle MultipleUnion;
	TestTrue(TEXT("Multiple-union lifecycle starts"),
		MultipleUnion.Start(0.0));
	Observation.SatelliteRuntimeCount = 1;
	Observation.E1OrderedUnionCount = 2;
	TestTrue(TEXT("Multiple E1 ordered unions fail closed"),
		MultipleUnion.Advance(0.1, Observation, Reason)
			== FAction::Reject);
	TestEqual(TEXT("Multiple-union reason is diagnostic"), Reason,
		FString(TEXT("MultipleE1OrderedUnions")));

	using namespace ABTSM73BuildingFreezeV3RuntimeRegistrationTests;
	FRuntimeRegistrationTestWorld WorldWrapper;
	if (!WorldWrapper.Create())
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	AABTSM7GameMode* GameMode = World != nullptr
		? World->SpawnActor<AABTSM7GameMode>()
		: nullptr;
	if (!TestNotNull(TEXT("Lifecycle timer test GameMode"), GameMode))
	{
		return false;
	}
	GameMode->ScheduleSatellitePracticeE1CrystalTargetBinding();
	TestTrue(TEXT("Production retry timer is active while waiting"),
		World->GetTimerManager().TimerExists(
			GameMode->SatellitePracticeE1CrystalBindingTimerHandle));
	GameMode->EndPlay(EEndPlayReason::Quit);
	TestFalse(TEXT("EndPlay clears the retry timer"),
		World->GetTimerManager().TimerExists(
			GameMode->SatellitePracticeE1CrystalBindingTimerHandle));
	TestTrue(TEXT("Teardown cancels the lifecycle"),
		GameMode->SatellitePracticeE1CrystalBindingLifecycle.GetState()
			== FState::Cancelled);
	AddInfo(FString::Printf(
		TEXT("E1CrystalBindingLifecycle Attempts=%d Timeout=%.1fs")
		TEXT(" LateRuntime=Bound Duplicate=Idempotent")
		TEXT(" MultipleRuntime=Rejected Teardown=Cancelled"),
		AttemptsAfterBind,
		FABTSM7SatellitePracticeE1CrystalBindingLifecycle::TimeoutSeconds));
	return true;
}

#endif
