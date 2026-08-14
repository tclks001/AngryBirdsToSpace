// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "World/ABTSVisualTuning.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Terrain/ABTSM3Planet.h"
#include "World/ABTSM51WorldActors.h"
#include "World/ABTSM51WorldSystem.h"
#include "World/ABTSM8BridgeActors.h"

namespace
{
class FScopedABTSVisualTuningWorld
{
public:
	FScopedABTSVisualTuningWorld()
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
			TEXT("ABTSVisualTuningRuntimeWorld"),
			nullptr,
			true,
			ERHIFeatureLevel::Num,
			&Values);
	}

	~FScopedABTSVisualTuningWorld()
	{
		if (World != nullptr)
		{
			if (GEngine != nullptr)
			{
				GEngine->Exec(World, TEXT("ABTS.Visual.ResetAll"));
			}
			World->DestroyWorld(false);
			World->RemoveFromRoot();
		}
	}

	UWorld* Get() const { return World; }

private:
	UWorld* World = nullptr;
};

template <typename TActor>
TActor* SpawnVisualTuningTestActor(UWorld& World)
{
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World.SpawnActor<TActor>(
		TActor::StaticClass(),
		FTransform::Identity,
		Parameters);
}

bool VisualAssetPathEndsWith(const UObject* Asset, const TCHAR* ExpectedSuffix)
{
	return Asset != nullptr && Asset->GetPathName().EndsWith(ExpectedSuffix);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSVisualTuningRuntimeTest,
	"ABTS.VisualTuning.Runtime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSVisualTuningRuntimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FScopedABTSVisualTuningWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("Visual-tuning test World is created"), World);
	if (World == nullptr || GEngine == nullptr) return false;
	GEngine->Exec(World, TEXT("ABTS.Visual.ResetAll"));

	static const TCHAR* RequiredCommands[] = {
		TEXT("ABTS.M51.Visual.Workbench"),
		TEXT("ABTS.M51.Visual.Furnace"),
		TEXT("ABTS.M8.Visual.Bridge"),
		TEXT("ABTS.M51.Visual.StandardSlot"),
		TEXT("ABTS.M51.Visual.FinaleSlot"),
		TEXT("ABTS.M51.Visual.Pickup.Branch"),
		TEXT("ABTS.M51.Visual.Pickup.Stone"),
		TEXT("ABTS.M51.Visual.Pickup.Wood"),
		TEXT("ABTS.M51.Visual.Pickup.PlantFiber"),
		TEXT("ABTS.M51.Pickup.SpawnShowcase"),
		TEXT("ABTS.Visual.Status"),
		TEXT("ABTS.Visual.ResetAll")};
	for (const TCHAR* CommandName : RequiredCommands)
	{
		TestNotNull(
			FString::Printf(TEXT("Console command is registered: %s"), CommandName),
			IConsoleManager::Get().FindConsoleObject(CommandName));
	}

	AABTSM51PickupItem* Pickup =
		SpawnVisualTuningTestActor<AABTSM51PickupItem>(*World);
	TestNotNull(TEXT("Pickup actor spawns"), Pickup);
	if (Pickup == nullptr) return false;
	const UStaticMeshComponent* PickupVisual = Pickup->GetVisualComponent();
	TestNotNull(TEXT("Pickup visual component exists"), PickupVisual);
	if (PickupVisual == nullptr) return false;

	struct FPickupExpectation
	{
		EABTSItemId ItemId;
		const TCHAR* MeshSuffix;
		const TCHAR* MaterialSuffix;
	};
	static const FPickupExpectation PickupExpectations[] = {
		{EABTSItemId::Branch,
			TEXT("/SM_Pickup_Branch.SM_Pickup_Branch"),
			TEXT("/MI_Pickup_Branch.MI_Pickup_Branch")},
		{EABTSItemId::Stone,
			TEXT("/SM_Pickup_Gravel.SM_Pickup_Gravel"),
			TEXT("/MI_Pickup_Gravel.MI_Pickup_Gravel")},
		{EABTSItemId::Wood,
			TEXT("/SM_Pickup_Wood.SM_Pickup_Wood"),
			TEXT("/MI_Pickup_Wood.MI_Pickup_Wood")},
		{EABTSItemId::PlantFiber,
			TEXT("/SM_Pickup_PlantFiber.SM_Pickup_PlantFiber"),
			TEXT("/MI_Pickup_PlantFiber.MI_Pickup_PlantFiber")}};
	for (const FPickupExpectation& Expectation : PickupExpectations)
	{
		Pickup->InitializePickup(Expectation.ItemId, 1, 7);
		TestTrue(
			FString::Printf(TEXT("Pickup mesh matches item %d"),
				static_cast<int32>(Expectation.ItemId)),
			VisualAssetPathEndsWith(
				PickupVisual->GetStaticMesh(),
				Expectation.MeshSuffix));
		TestTrue(
			FString::Printf(TEXT("Pickup material matches item %d"),
				static_cast<int32>(Expectation.ItemId)),
			VisualAssetPathEndsWith(
				PickupVisual->GetMaterial(0),
				Expectation.MaterialSuffix));
	}

	Pickup->InitializePickup(EABTSItemId::Branch, 1, 7);
	const FVector PickupAnchorBeforeTuning = Pickup->GetActorLocation();
	TestTrue(
		TEXT("Pickup tuning command executes"),
		GEngine->Exec(
			World,
			TEXT("ABTS.M51.Visual.Pickup.Branch 1.5 25")));
	TestEqual(
		TEXT("Pickup gameplay anchor is unchanged by visual Z tuning"),
		Pickup->GetActorLocation(),
		PickupAnchorBeforeTuning);
	TestEqual(
		TEXT("Pickup scale multiplier is applied"),
		PickupVisual->GetRelativeScale3D(),
		FVector(0.27f));
	TestEqual(
		TEXT("Pickup local Z offset is applied"),
		PickupVisual->GetRelativeLocation().Z,
		25.0);

	AABTSM51SlingshotDirtHole* Slot =
		SpawnVisualTuningTestActor<AABTSM51SlingshotDirtHole>(*World);
	TestNotNull(TEXT("Slingshot slot actor spawns"), Slot);
	if (Slot == nullptr) return false;
	const FVector SlotAnchorBeforeTuning = Slot->GetActorLocation();
	TestTrue(
		TEXT("Standard-slot tuning command executes"),
		GEngine->Exec(World, TEXT("ABTS.M51.Visual.StandardSlot 0.8 -12")));
	TestEqual(
		TEXT("Slot gameplay anchor is unchanged by visual Z tuning"),
		Slot->GetActorLocation(),
		SlotAnchorBeforeTuning);
	TestEqual(
		TEXT("Standard-slot local Z offset is applied"),
		Slot->GetVisualComponent()->GetRelativeLocation().Z,
		-12.0);

	AABTSM8BridgeActor* Bridge =
		SpawnVisualTuningTestActor<AABTSM8BridgeActor>(*World);
	TestNotNull(TEXT("Bridge actor spawns"), Bridge);
	if (Bridge == nullptr) return false;
	const FVector BridgeLocation(100.0f, 200.0f, 300.0f);
	Bridge->InitializeBridge(
		FABTSM3CellEdgeKey(),
		FTransform(BridgeLocation),
		FVector(400.0f, 200.0f, 40.0f));
	TestTrue(
		TEXT("Bridge uses the authored bridge mesh"),
		VisualAssetPathEndsWith(
			Bridge->GetDeckComponent()->GetStaticMesh(),
			TEXT("/SM_Bridge.SM_Bridge")));
	TestTrue(
		TEXT("Bridge uses the authored bridge material"),
		VisualAssetPathEndsWith(
			Bridge->GetDeckComponent()->GetMaterial(0),
			TEXT("/MI_Bridge.MI_Bridge")));
	TestEqual(
		TEXT("Bridge collision keeps the authoritative half extents"),
		Bridge->GetCollisionComponent()->GetUnscaledBoxExtent(),
		FVector(200.0f, 100.0f, 20.0f));
	TestTrue(
		TEXT("Bridge tuning command executes"),
		GEngine->Exec(World, TEXT("ABTS.M8.Visual.Bridge 1.25 30")));
	TestEqual(
		TEXT("Bridge gameplay anchor is unchanged by visual tuning"),
		Bridge->GetActorLocation(),
		BridgeLocation);
	TestEqual(
		TEXT("Bridge collision remains unchanged by visual tuning"),
		Bridge->GetCollisionComponent()->GetUnscaledBoxExtent(),
		FVector(200.0f, 100.0f, 20.0f));

	AABTSM3Planet* Planet = SpawnVisualTuningTestActor<AABTSM3Planet>(*World);
	TestNotNull(TEXT("Showcase test planet spawns"), Planet);
	if (Planet == nullptr) return false;
	Planet->SurfaceSubdivision = 1;
	Planet->InstancesPerCell = 0;
	TestTrue(TEXT("Showcase test planet rebuilds"), Planet->RebuildPlanet());
	if (!Planet->IsPlanetReady() || Planet->LogicalCells.IsEmpty()) return false;
	FVector SurfacePosition;
	FVector SurfaceNormal;
	float SurfaceRadius = 0.0f;
	int32 SurfaceCell = INDEX_NONE;
	TestTrue(
		TEXT("Showcase player surface position resolves"),
		Planet->QuerySurface(
			Planet->LogicalCells[0].UnitCenter,
			SurfacePosition,
			SurfaceNormal,
			SurfaceRadius,
			SurfaceCell));
	ACharacter* PlayerCharacter = SpawnVisualTuningTestActor<ACharacter>(*World);
	TestNotNull(TEXT("Showcase player character spawns"), PlayerCharacter);
	if (PlayerCharacter == nullptr) return false;
	PlayerCharacter->SetActorLocation(SurfacePosition + SurfaceNormal * 100.0f);
	AABTSM51WorldSystem* WorldSystem =
		SpawnVisualTuningTestActor<AABTSM51WorldSystem>(*World);
	TestNotNull(TEXT("Showcase WorldSystem spawns"), WorldSystem);
	if (WorldSystem == nullptr) return false;
	TestTrue(
		TEXT("Showcase atomically spawns four safe pickups"),
		WorldSystem->SpawnPickupShowcaseAroundPawn(*PlayerCharacter, 450.0f));
	TSet<EABTSItemId> ShowcaseItemIds;
	int32 LiveShowcaseCount = 0;
	for (TActorIterator<AABTSM51PickupItem> It(World); It; ++It)
	{
		if (*It == Pickup) continue;
		++LiveShowcaseCount;
		ShowcaseItemIds.Add(It->GetItemId());
		TestTrue(
			TEXT("Showcase pickup is outside the configured auto-pickup radius"),
			FVector::Distance(
				PlayerCharacter->GetActorLocation(),
				It->GetActorLocation()) > 195.0f);
	}
	TestEqual(TEXT("Showcase live pickup count"), LiveShowcaseCount, 4);
	TestEqual(TEXT("Showcase contains all four item ids"), ShowcaseItemIds.Num(), 4);
	return true;
}

#endif
