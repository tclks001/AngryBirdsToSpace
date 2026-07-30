// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Crafting/ABTSCraftingSystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Inventory/ABTSInventoryComponent.h"
#include "Misc/AutomationTest.h"
#include "World/ABTSM51OrdinarySlingshotSlotSnapshot.h"
#include "World/ABTSM51WorldActors.h"
#include "World/ABTSM51WorldSystem.h"

namespace
{
FABTSM51OrdinarySlingshotSlotSnapshot MakeValidSlotSnapshot()
{
	FABTSM51OrdinarySlingshotSlotSnapshot Snapshot;
	Snapshot.LayoutHash = 0x1100ull;
	Snapshot.CandidateHash = 0x2200ull;
	Snapshot.MaxCordLengthCM = 1200;
	FABTSM51OrdinarySlingshotSlotGroup& First =
		Snapshot.SlotGroups.AddDefaulted_GetRef();
	First.SlotCellIds = {1, 2, 3};
	FABTSM51OrdinarySlingshotSlotGroup& Second =
		Snapshot.SlotGroups.AddDefaulted_GetRef();
	Second.SlotCellIds = {4, 5};
	return Snapshot;
}

class FScopedM51SlingshotAssemblyWorld
{
public:
	FScopedM51SlingshotAssemblyWorld()
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
			TEXT("ABTSM51SlingshotAssemblyAutomationWorld"),
			nullptr,
			true,
			ERHIFeatureLevel::Num,
			&Values);
	}

	~FScopedM51SlingshotAssemblyWorld()
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

template <typename TActor>
TActor* SpawnTestActor(UWorld& World, const FVector& Location)
{
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World.SpawnActor<TActor>(
		TActor::StaticClass(),
		FTransform(FQuat::Identity, Location),
		Parameters);
}

AABTSM51SlingshotStake* SpawnTestStake(
	UWorld& World,
	const FVector& Location,
	const EABTSItemId StakeItem,
	const int32 CellId)
{
	AABTSM51SlingshotStake* Stake =
		SpawnTestActor<AABTSM51SlingshotStake>(World, Location);
	if (Stake != nullptr)
	{
		Stake->InitializeStake(
			StakeItem,
			CellId,
			FVector::UpVector);
	}
	return Stake;
}

int32 CountLiveCordActors(UWorld& World)
{
	int32 Count = 0;
	for (TActorIterator<AABTSM51SlingshotCord> It(&World); It; ++It)
	{
		if (!It->IsActorBeingDestroyed())
		{
			++Count;
		}
	}
	return Count;
}

bool PrepareAssemblyRuntime(
	UWorld& World,
	const EABTSItemId CordItem,
	const int32 CordQuantity,
	AABTSM51WorldSystem*& OutSystem,
	UABTSInventoryComponent*& OutInventory)
{
	AABTSCraftingSystem* Crafting =
		SpawnTestActor<AABTSCraftingSystem>(
			World,
			FVector::ZeroVector);
	OutSystem =
		SpawnTestActor<AABTSM51WorldSystem>(
			World,
			FVector::ZeroVector);
	OutInventory = Crafting != nullptr
		? Crafting->GetInventory()
		: nullptr;
	return OutSystem != nullptr
		&& OutInventory != nullptr
		&& OutInventory->AddItem(CordItem, CordQuantity)
		&& OutInventory->SetHeldItem(CordItem);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM51SlingshotAssemblyRuntimeTest,
	"ABTS.M51.SlingshotAssembly.Runtime",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM51SlingshotAssemblyRuntimeTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;

	{
		FABTSM51OrdinarySlingshotSlotSnapshot Snapshot =
			MakeValidSlotSnapshot();
		TArray<int32> FlattenedCells;
		TestTrue(
			TEXT("Accepted-slot consumer DTO is structurally valid"),
			Snapshot.IsStructurallyUsable());
		TestTrue(
			TEXT("Accepted-slot DTO validates and flattens against CellTopo"),
			Snapshot.TryBuildCellList(6, FlattenedCells));
		TestEqual(
			TEXT("Every accepted slot appears exactly once"),
			FlattenedCells.Num(),
			5);

		FABTSM51OrdinarySlingshotSlotSnapshot Duplicate =
			Snapshot;
		Duplicate.SlotGroups[1].SlotCellIds[0] = 2;
		TestFalse(
			TEXT("Duplicate Cell identity fails closed"),
			Duplicate.IsStructurallyUsable());

		FABTSM51OrdinarySlingshotSlotSnapshot MissingIdentity =
			Snapshot;
		MissingIdentity.CandidateHash = 0;
		TestFalse(
			TEXT("Missing accepted candidate identity fails closed"),
			MissingIdentity.IsStructurallyUsable());

		FlattenedCells.Add(99);
		TestFalse(
			TEXT("A slot outside the active CellTopo fails closed"),
			Snapshot.TryBuildCellList(5, FlattenedCells));
		TestTrue(
			TEXT("Failed flattening retains no partial spawn plan"),
			FlattenedCells.IsEmpty());

		FScopedM51SlingshotAssemblyWorld ScopedWorld;
		UWorld* World = ScopedWorld.Get();
		AABTSM51WorldSystem* SnapshotConsumer =
			World != nullptr
			? SpawnTestActor<AABTSM51WorldSystem>(
				*World,
				FVector::ZeroVector)
			: nullptr;
		TestNotNull(
			TEXT("Snapshot consumer Actor spawns"),
			SnapshotConsumer);
		if (SnapshotConsumer != nullptr)
		{
			TestTrue(
				TEXT("A valid accepted snapshot can be injected before BeginPlay"),
				SnapshotConsumer->
					ConfigureAcceptedOrdinarySlingshotSlotSnapshot(
						Snapshot));
			TestEqual(
				TEXT("M6 reads the accepted snapshot maximum length"),
				SnapshotConsumer->
					GetActiveOrdinaryMaxCordLengthCM(),
				1200);
		}
	}

	{
		FScopedM51SlingshotAssemblyWorld ScopedWorld;
		UWorld* World = ScopedWorld.Get();
		AABTSM51WorldSystem* System = nullptr;
		UABTSInventoryComponent* Inventory = nullptr;
		TestTrue(
			TEXT("Simple success fixture is ready"),
			World != nullptr
				&& PrepareAssemblyRuntime(
					*World,
					EABTSItemId::SimpleCord,
					2,
					System,
					Inventory));
		if (World != nullptr && System != nullptr && Inventory != nullptr)
		{
			AABTSM51SlingshotStake* First =
				SpawnTestStake(
					*World,
					FVector(0.0, 0.0, 0.0),
					EABTSItemId::SimpleStake,
					1);
			AABTSM51SlingshotStake* Second =
				SpawnTestStake(
					*World,
					FVector(200.0, 0.0, 0.0),
					EABTSItemId::SimpleStake,
					2);
			const int32 InitialQuantity =
				Inventory->GetQuantity(EABTSItemId::SimpleCord);
			const int32 InitialCordCount =
				CountLiveCordActors(*World);
			TestTrue(
				TEXT("First click only selects a stake"),
				First != nullptr
					&& System->SelectStakeForHeldCord(*First));
			TestEqual(
				TEXT("First click consumes no inventory"),
				Inventory->GetQuantity(
					EABTSItemId::SimpleCord),
				InitialQuantity);
			TestEqual(
				TEXT("First click spawns no cord"),
				CountLiveCordActors(*World),
				InitialCordCount);
			TestTrue(
				TEXT("Second click commits a clear ordinary connection"),
				Second != nullptr
					&& System->SelectStakeForHeldCord(*Second));
			TestEqual(
				TEXT("Successful connection consumes exactly one cord"),
				Inventory->GetQuantity(
					EABTSItemId::SimpleCord),
				InitialQuantity - 1);
			TestEqual(
				TEXT("Successful connection spawns exactly one cord Actor"),
				CountLiveCordActors(*World),
				InitialCordCount + 1);
			TestTrue(
				TEXT("Successful connection commits both endpoint states"),
				First != nullptr
					&& Second != nullptr
					&& First->HasCord()
					&& Second->HasCord());
		}
	}

	{
		FScopedM51SlingshotAssemblyWorld ScopedWorld;
		UWorld* World = ScopedWorld.Get();
		AABTSM51WorldSystem* System = nullptr;
		UABTSInventoryComponent* Inventory = nullptr;
		if (World != nullptr
			&& PrepareAssemblyRuntime(
				*World,
				EABTSItemId::SimpleCord,
				2,
				System,
				Inventory))
		{
			AABTSM51SlingshotStake* First =
				SpawnTestStake(
					*World,
					FVector(0.0, 0.0, 0.0),
					EABTSItemId::SimpleStake,
					1);
			AABTSM51SlingshotStake* Second =
				SpawnTestStake(
					*World,
					FVector(1201.0, 0.0, 0.0),
					EABTSItemId::SimpleStake,
					2);
			const int32 QuantityBefore =
				Inventory->GetQuantity(EABTSItemId::SimpleCord);
			const int32 CordsBefore =
				CountLiveCordActors(*World);
			TestTrue(
				TEXT("Too-long fixture selects its first stake"),
				System->SelectStakeForHeldCord(*First));
			TestFalse(
				TEXT("A connection beyond 1200 cm is rejected"),
				System->SelectStakeForHeldCord(*Second));
			TestEqual(
				TEXT("Too-long rejection preserves inventory"),
				Inventory->GetQuantity(
					EABTSItemId::SimpleCord),
				QuantityBefore);
			TestEqual(
				TEXT("Too-long rejection spawns no cord"),
				CountLiveCordActors(*World),
				CordsBefore);
			TestTrue(
				TEXT("Too-long rejection leaves both endpoints uncommitted"),
				!First->HasCord() && !Second->HasCord());
		}
	}

	{
		FScopedM51SlingshotAssemblyWorld ScopedWorld;
		UWorld* World = ScopedWorld.Get();
		AABTSM51WorldSystem* System = nullptr;
		UABTSInventoryComponent* Inventory = nullptr;
		if (World != nullptr
			&& PrepareAssemblyRuntime(
				*World,
				EABTSItemId::SimpleCord,
				2,
				System,
				Inventory))
		{
			AABTSM51SlingshotStake* First =
				SpawnTestStake(
					*World,
					FVector(0.0, 0.0, 0.0),
					EABTSItemId::SimpleStake,
					1);
			AABTSM51SlingshotStake* Second =
				SpawnTestStake(
					*World,
					FVector(200.0, 0.0, 0.0),
					EABTSItemId::SimpleStake,
					2);
			SpawnTestStake(
				*World,
				FVector(100.0, 0.0, 0.0),
				EABTSItemId::SimpleStake,
				3);
			const int32 QuantityBefore =
				Inventory->GetQuantity(EABTSItemId::SimpleCord);
			const int32 CordsBefore =
				CountLiveCordActors(*World);
			System->SelectStakeForHeldCord(*First);
			TestFalse(
				TEXT("A third visible stake blocks the candidate cord"),
				System->SelectStakeForHeldCord(*Second));
			TestEqual(
				TEXT("Stake obstruction preserves inventory"),
				Inventory->GetQuantity(
					EABTSItemId::SimpleCord),
				QuantityBefore);
			TestEqual(
				TEXT("Stake obstruction spawns no cord"),
				CountLiveCordActors(*World),
				CordsBefore);
			TestTrue(
				TEXT("Stake obstruction leaves endpoint states unchanged"),
				!First->HasCord() && !Second->HasCord());
		}
	}

	{
		FScopedM51SlingshotAssemblyWorld ScopedWorld;
		UWorld* World = ScopedWorld.Get();
		AABTSM51WorldSystem* System = nullptr;
		UABTSInventoryComponent* Inventory = nullptr;
		if (World != nullptr
			&& PrepareAssemblyRuntime(
				*World,
				EABTSItemId::SimpleCord,
				2,
				System,
				Inventory))
		{
			AABTSM51SlingshotStake* First =
				SpawnTestStake(
					*World,
					FVector(0.0, 0.0, 0.0),
					EABTSItemId::SimpleStake,
					1);
			AABTSM51SlingshotStake* Second =
				SpawnTestStake(
					*World,
					FVector(200.0, 0.0, 0.0),
					EABTSItemId::SimpleStake,
					2);
			AABTSM51SlingshotCord* Existing =
				SpawnTestActor<AABTSM51SlingshotCord>(
					*World,
					FVector::ZeroVector);
			if (Existing != nullptr)
			{
				Existing->InitializeCordWithTier(
					nullptr,
					nullptr,
					FVector(100.0, -100.0, 110.0),
					FVector(100.0, 100.0, 110.0),
					EABTSSlingshotTier::Simple);
			}
			const int32 QuantityBefore =
				Inventory->GetQuantity(EABTSItemId::SimpleCord);
			const int32 CordsBefore =
				CountLiveCordActors(*World);
			System->SelectStakeForHeldCord(*First);
			TestFalse(
				TEXT("An existing crossing cord blocks the candidate"),
				System->SelectStakeForHeldCord(*Second));
			TestEqual(
				TEXT("Cord obstruction preserves inventory"),
				Inventory->GetQuantity(
					EABTSItemId::SimpleCord),
				QuantityBefore);
			TestEqual(
				TEXT("Cord obstruction creates no additional Actor"),
				CountLiveCordActors(*World),
				CordsBefore);
			TestTrue(
				TEXT("Cord obstruction leaves endpoint states unchanged"),
				!First->HasCord() && !Second->HasCord());
		}
	}

	{
		FScopedM51SlingshotAssemblyWorld ScopedWorld;
		UWorld* World = ScopedWorld.Get();
		AABTSM51WorldSystem* System = nullptr;
		UABTSInventoryComponent* Inventory = nullptr;
		if (World != nullptr
			&& PrepareAssemblyRuntime(
				*World,
				EABTSItemId::SpaceCord,
				2,
				System,
				Inventory))
		{
			AABTSM51SlingshotStake* Left =
				SpawnTestStake(
					*World,
					FVector(0.0, 0.0, 0.0),
					EABTSItemId::SpaceStake,
					10);
			AABTSM51SlingshotStake* Right =
				SpawnTestStake(
					*World,
					FVector(210.0, 0.0, 0.0),
					EABTSItemId::SpaceStake,
					11);
			Left->SetInstalledSlotIdentity(
				EABTSSlingshotSlotKind::FinaleSpace,
				77,
				EABTSSlingshotSlotSide::Left);
			Right->SetInstalledSlotIdentity(
				EABTSSlingshotSlotKind::FinaleSpace,
				77,
				EABTSSlingshotSlotSide::Right);
			System->SelectStakeForHeldCord(*Left);
			TestTrue(
				TEXT("The unique matching Finale pair still accepts SpaceCord"),
				System->SelectStakeForHeldCord(*Right));
			TestTrue(
				TEXT("Space success commits both Finale stakes"),
				Left->HasCord() && Right->HasCord());
		}
	}

	{
		FScopedM51SlingshotAssemblyWorld ScopedWorld;
		UWorld* World = ScopedWorld.Get();
		AABTSM51WorldSystem* System = nullptr;
		UABTSInventoryComponent* Inventory = nullptr;
		if (World != nullptr
			&& PrepareAssemblyRuntime(
				*World,
				EABTSItemId::SpaceCord,
				2,
				System,
				Inventory))
		{
			AABTSM51SlingshotStake* Left =
				SpawnTestStake(
					*World,
					FVector(0.0, 0.0, 0.0),
					EABTSItemId::SpaceStake,
					10);
			AABTSM51SlingshotStake* WrongRight =
				SpawnTestStake(
					*World,
					FVector(210.0, 0.0, 0.0),
					EABTSItemId::SpaceStake,
					11);
			Left->SetInstalledSlotIdentity(
				EABTSSlingshotSlotKind::FinaleSpace,
				77,
				EABTSSlingshotSlotSide::Left);
			WrongRight->SetInstalledSlotIdentity(
				EABTSSlingshotSlotKind::FinaleSpace,
				78,
				EABTSSlingshotSlotSide::Right);
			const int32 QuantityBefore =
				Inventory->GetQuantity(EABTSItemId::SpaceCord);
			const int32 CordsBefore =
				CountLiveCordActors(*World);
			System->SelectStakeForHeldCord(*Left);
			TestFalse(
				TEXT("SpaceCord cannot connect different Finale pair identities"),
				System->SelectStakeForHeldCord(*WrongRight));
			TestEqual(
				TEXT("Finale pair rejection preserves inventory"),
				Inventory->GetQuantity(EABTSItemId::SpaceCord),
				QuantityBefore);
			TestEqual(
				TEXT("Finale pair rejection spawns no cord"),
				CountLiveCordActors(*World),
				CordsBefore);
			TestTrue(
				TEXT("Finale pair rejection leaves both endpoint states unchanged"),
				!Left->HasCord()
					&& !WrongRight->HasCord());
		}
	}
	return true;
}

#endif
