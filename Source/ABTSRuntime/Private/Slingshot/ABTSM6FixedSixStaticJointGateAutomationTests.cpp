// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Slingshot/ABTSM6SlingshotSystem.h"

#include "Building/ABTSM73JuryDemoFixedSixRegistration.h"
#include "Building/ABTSM73StableBuildingActor.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Terrain/ABTSM3Planet.h"
#include "Tests/AutomationCommon.h"

namespace ABTSM6FixedSixStaticJointGateTests
{
class FFixedSixStaticJointTestWorld final : public FTestWorldWrapper
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
			.CreatePhysicsScene(false)
			.ShouldSimulatePhysics(false)
			.EnableTraceCollision(false)
			.CreateNavigation(false)
			.CreateAISystem(false)
			.CreateFXSystem(false);
		TestWorld = UWorld::CreateWorld(
			EWorldType::Game,
			false,
			TEXT("ABTSM6FixedSixStaticJointGateWorld"),
			nullptr,
			true,
			ERHIFeatureLevel::Num,
			&Values);
		if (TestWorld == nullptr)
		{
			ReportFailure(TEXT("Failed to create static-joint test world"));
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

void RollbackFixedSixStaticActors(
	const TArray<TWeakObjectPtr<AABTSM73StableBuildingActor>>& Actors,
	const TCHAR* Reason)
{
	for (const TWeakObjectPtr<AABTSM73StableBuildingActor>& WeakActor : Actors)
	{
		if (AABTSM73StableBuildingActor* Actor = WeakActor.Get())
		{
			Actor->RollbackJuryDemoFixedSixStaticRegistration(Reason);
		}
	}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM6FixedSixStaticJointGateTest,
	"ABTS.Integration.JuryDemoFixedSix.StaticJointGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM6FixedSixStaticJointGateTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM6FixedSixStaticJointGateTests;
	(void)Parameters;
	static constexpr uint64 FrozenRegistrationResultHash =
		FABTSM73JuryDemoFixedSixRegistration::FrozenV3RegistrationResultHash;
	static constexpr int32 FrozenStaticModuleCount =
		FABTSM73JuryDemoFixedSixRegistration::FrozenV3StaticModuleCount;

	FFixedSixStaticJointTestWorld WorldWrapper;
	if (!WorldWrapper.Create())
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	if (!TestNotNull(TEXT("Static-joint automation world"), World))
	{
		return false;
	}

	AABTSM3Planet* Planet = World->SpawnActor<AABTSM3Planet>();
	if (!TestNotNull(TEXT("M3 fixed-six producer"), Planet))
	{
		return false;
	}
	Planet->WorldSeed = FABTSJuryDemoFixedSixContract::FrozenWorldSeed;
	Planet->SurfaceSubdivision = 1;
	Planet->InstancesPerCell = 0;
	if (!TestTrue(TEXT("M3 fixed-six world rebuilds"), Planet->RebuildPlanet()))
	{
		return false;
	}

	FABTSBuildingGenerationContract Contract;
	if (!TestTrue(TEXT("M3 exports the frozen V3 snapshot"),
		Planet->TryExportBuildingGenerationContract(Contract)))
	{
		return false;
	}
	FABTSM73JuryDemoFixedSixStaticPlan Plan;
	FString Error;
	if (!TestTrue(TEXT("M7 resolves the exact six-building static plan"),
		FABTSM73JuryDemoFixedSixRegistration::BuildStaticPlan(
			Contract, Plan, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("M7 publishes the frozen registration result hash"),
		Plan.RegistrationResultHash, FrozenRegistrationResultHash);
	FABTSM73JuryDemoFixedSixStaticPlan WrongHashPlan = Plan;
	WrongHashPlan.RegistrationResultHash ^= 1ull;
	for (FABTSM73JuryDemoFixedSixStaticEntry& Entry : WrongHashPlan.Entries)
	{
		Entry.RegistrationResultHash = WrongHashPlan.RegistrationResultHash;
	}

	AABTSM7BuildingMaterialSystem* MaterialSystem =
		World->SpawnActor<AABTSM7BuildingMaterialSystem>();
	if (!TestNotNull(TEXT("M7 material system"), MaterialSystem))
	{
		return false;
	}
	TArray<TWeakObjectPtr<AABTSM73StableBuildingActor>> Actors;
	if (!TestTrue(TEXT("M7 atomically registers six static Actors"),
		FABTSM73JuryDemoFixedSixRegistration::SpawnStaticActors(
			*World,
			*MaterialSystem,
			AABTSM73StableBuildingActor::StaticClass(),
			MoveTemp(Plan),
			Actors,
			Error)))
	{
		AddError(Error);
		return false;
	}

	AABTSM6SlingshotSystem* Slingshot =
		World->SpawnActor<AABTSM6SlingshotSystem>();
	if (!TestNotNull(TEXT("M6 shared startup gate"), Slingshot))
	{
		return false;
	}
	Slingshot->BeginRequiredBuildingContract(
		FABTSJuryDemoFixedSixContract::ExpectedSiteCount);
	for (const TWeakObjectPtr<AABTSM73StableBuildingActor>& WeakActor : Actors)
	{
		if (AABTSM73StableBuildingActor* Actor = WeakActor.Get())
		{
			Slingshot->RegisterRequiredBuilding(*Actor);
		}
	}
	Slingshot->SealRequiredBuildingContract(false);
	uint64 ResultHash = 0;
	int32 RegisteredBuildings = 0;
	int32 StaticModules = 0;
	TestTrue(TEXT("M6 accepts the exact Fixed-Six static joint gate"),
		Slingshot->CopyFixedSixStaticJointGateResult(
			ResultHash, RegisteredBuildings, StaticModules));
	TestEqual(TEXT("Joint gate preserves the registration result hash"),
		ResultHash, FrozenRegistrationResultHash);
	TestEqual(TEXT("Joint gate requires exactly six buildings"),
		RegisteredBuildings,
		FABTSJuryDemoFixedSixContract::ExpectedSiteCount);
	TestEqual(TEXT("Joint gate freezes all brick instances and devices"),
		StaticModules, FrozenStaticModuleCount);
	AABTSM6SlingshotSystem* WrongOrderGate =
		World->SpawnActor<AABTSM6SlingshotSystem>();
	TestNotNull(TEXT("Wrong-order M6 gate"), WrongOrderGate);
	if (WrongOrderGate != nullptr)
	{
		WrongOrderGate->BeginRequiredBuildingContract(
			FABTSJuryDemoFixedSixContract::ExpectedSiteCount);
		WrongOrderGate->RegisterRequiredBuilding(*Actors[1].Get());
		WrongOrderGate->RegisterRequiredBuilding(*Actors[0].Get());
		for (int32 Index = 2; Index < Actors.Num(); ++Index)
		{
			WrongOrderGate->RegisterRequiredBuilding(*Actors[Index].Get());
		}
		AddExpectedError(
			TEXT("FixedSixStaticJointGateRejected"),
			EAutomationExpectedErrorFlags::Contains,
			1);
		AddExpectedError(
			TEXT("BuildingContractSealed"),
			EAutomationExpectedErrorFlags::Contains,
			1);
		WrongOrderGate->SealRequiredBuildingContract(false);
		TestFalse(TEXT("Out-of-order static batch fails closed"),
			WrongOrderGate->CopyFixedSixStaticJointGateResult(
				ResultHash, RegisteredBuildings, StaticModules));
	}

	TArray<TWeakObjectPtr<AABTSM73StableBuildingActor>> WrongHashActors;
	if (!TestTrue(TEXT("Self-consistent wrong-hash Actors can reach M6 boundary"),
		FABTSM73JuryDemoFixedSixRegistration::SpawnStaticActors(
			*World,
			*MaterialSystem,
			AABTSM73StableBuildingActor::StaticClass(),
			MoveTemp(WrongHashPlan),
			WrongHashActors,
			Error)))
	{
		AddError(Error);
		return false;
	}
	AABTSM6SlingshotSystem* WrongHashGate =
		World->SpawnActor<AABTSM6SlingshotSystem>();
	TestNotNull(TEXT("Wrong-hash M6 gate"), WrongHashGate);
	if (WrongHashGate != nullptr)
	{
		WrongHashGate->BeginRequiredBuildingContract(
			FABTSJuryDemoFixedSixContract::ExpectedSiteCount);
		for (const TWeakObjectPtr<AABTSM73StableBuildingActor>& WeakActor
			: WrongHashActors)
		{
			WrongHashGate->RegisterRequiredBuilding(*WeakActor.Get());
		}
		AddExpectedError(
			TEXT("FixedSixStaticJointGateRejected"),
			EAutomationExpectedErrorFlags::Contains,
			1);
		AddExpectedError(
			TEXT("BuildingContractSealed"),
			EAutomationExpectedErrorFlags::Contains,
			1);
		WrongHashGate->SealRequiredBuildingContract(false);
		TestFalse(TEXT("Wrong registration result hash fails closed"),
			WrongHashGate->CopyFixedSixStaticJointGateResult(
				ResultHash, RegisteredBuildings, StaticModules));
	}

	AddInfo(FString::Printf(
		TEXT("FixedSixStaticJointGate Buildings=%d Modules=%d")
		TEXT(" Layout=%llu ResultHash=%llu")
		TEXT(" Authority=StaticRegistration Chaos=NotEvaluated Gate=Accepted"),
		FABTSJuryDemoFixedSixContract::ExpectedSiteCount,
		FrozenStaticModuleCount,
		FABTSJuryDemoFixedSixContract::FrozenV3LayoutHash,
		FrozenRegistrationResultHash));

	RollbackFixedSixStaticActors(Actors, TEXT("StaticJointAutomationCleanup"));
	RollbackFixedSixStaticActors(
		WrongHashActors, TEXT("StaticJointAutomationCleanup"));
	MaterialSystem->Destroy();
	Planet->Destroy();
	return true;
}

#endif
