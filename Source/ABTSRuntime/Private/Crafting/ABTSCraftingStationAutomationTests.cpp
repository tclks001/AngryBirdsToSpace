// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Crafting/ABTSCraftingStation.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"

namespace
{
class FScopedCraftingStationTestWorld
{
public:
	FScopedCraftingStationTestWorld()
	{
		const UWorld::InitializationValues Values =
			UWorld::InitializationValues()
				.InitializeScenes(false)
				.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(false)
				.CreateNavigation(false)
				.CreateAISystem(false)
				.ShouldSimulatePhysics(false)
				.EnableTraceCollision(false)
				.SetTransactional(false)
				.CreateFXSystem(false);
		World = UWorld::CreateWorld(
			EWorldType::Game,
			false,
			TEXT("ABTSCraftingStationRuntimeVisualWorld"),
			nullptr,
			true,
			ERHIFeatureLevel::Num,
			&Values);
	}

	~FScopedCraftingStationTestWorld()
	{
		if (World != nullptr)
		{
			World->DestroyWorld(false);
			World->RemoveFromRoot();
		}
	}

	UWorld* Get() const { return World; }

private:
	UWorld* World = nullptr;
};

bool AssetPathEndsWith(const UObject* Asset, const TCHAR* ExpectedSuffix)
{
	return Asset != nullptr && Asset->GetPathName().EndsWith(ExpectedSuffix);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSCraftingStationRuntimeVisualSelectionTest,
	"ABTS.M51.CraftingStation.RuntimeVisualSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSCraftingStationRuntimeVisualSelectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FScopedCraftingStationTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("Crafting-station test World is created"), World);
	if (World == nullptr) return false;

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSCraftingStation* Station = World->SpawnActor<AABTSCraftingStation>(
		AABTSCraftingStation::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	TestNotNull(TEXT("Crafting station spawns"), Station);
	if (Station == nullptr) return false;

	UStaticMeshComponent* Visual =
		Station->FindComponentByClass<UStaticMeshComponent>();
	TestNotNull(TEXT("Crafting station exposes its visual component"), Visual);
	if (Visual == nullptr) return false;

	TestTrue(
		TEXT("The constructor assigns the workbench mesh"),
		AssetPathEndsWith(
			Visual->GetStaticMesh(),
			TEXT("/SM_Workbench.SM_Workbench")));
	TestTrue(
		TEXT("The constructor assigns the workbench material"),
		AssetPathEndsWith(
			Visual->GetMaterial(0),
			TEXT("/MI_Workbench.MI_Workbench")));

	// This call intentionally runs after actor construction. ConstructorHelpers
	// here caused the original PIE fatal when the player placed a FurnaceKit.
	Station->SetStationType(EABTSCraftingStationType::Furnace);
	TestEqual(
		TEXT("Runtime selection records the furnace station type"),
		static_cast<uint8>(Station->GetStationType()),
		static_cast<uint8>(EABTSCraftingStationType::Furnace));
	TestTrue(
		TEXT("Runtime selection assigns the furnace mesh"),
		AssetPathEndsWith(
			Visual->GetStaticMesh(),
			TEXT("/SM_Furnace.SM_Furnace")));
	TestTrue(
		TEXT("Runtime selection assigns the furnace material"),
		AssetPathEndsWith(
			Visual->GetMaterial(0),
			TEXT("/MI_Furnace.MI_Furnace")));

	Station->SetStationType(EABTSCraftingStationType::Workbench);
	TestTrue(
		TEXT("Runtime selection can restore the workbench mesh"),
		AssetPathEndsWith(
			Visual->GetStaticMesh(),
			TEXT("/SM_Workbench.SM_Workbench")));
	TestTrue(
		TEXT("Runtime selection can restore the workbench material"),
		AssetPathEndsWith(
			Visual->GetMaterial(0),
			TEXT("/MI_Workbench.MI_Workbench")));
	return true;
}

#endif
