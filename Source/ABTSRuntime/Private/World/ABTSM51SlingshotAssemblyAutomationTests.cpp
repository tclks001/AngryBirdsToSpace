// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Crafting/ABTSCraftingSystem.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Inventory/ABTSInventoryComponent.h"
#include "Materials/MaterialInterface.h"
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
					FVector(ABTSFinaleSpaceStakeSpacingCM, 0.0, 0.0),
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
					FVector(ABTSFinaleSpaceStakeSpacingCM, 0.0, 0.0),
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM6SpaceSlingshotVisualPresetTest,
	"ABTS.M6.SlingshotVisual.SpaceFourBirdFrame",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM6SpaceSlingshotVisualPresetTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FABTSSlingshotVisualPreset Twig =
		ABTSMakeDefaultSlingshotVisualPreset(EABTSSlingshotTier::Twig);
	const FABTSSlingshotVisualPreset Simple =
		ABTSMakeDefaultSlingshotVisualPreset(EABTSSlingshotTier::Simple);
	const FABTSSlingshotVisualPreset Reinforced =
		ABTSMakeDefaultSlingshotVisualPreset(EABTSSlingshotTier::Reinforced);
	const FABTSSlingshotVisualPreset Space =
		ABTSMakeDefaultSlingshotVisualPreset(EABTSSlingshotTier::Space);

	TestEqual(TEXT("Visual preset contract is v2"),
		ABTSSlingshotVisualPresetContractVersion, 2);
	TestEqual(TEXT("Mounted bird orientation contract is v1"),
		ABTSSlingshotMountedBirdContractVersion, 1);
	const FVector TestLaunchForward = FVector(0.35, -0.82, 0.44).GetSafeNormal();
	const FVector TestRadialUp = FVector(0.18, 0.51, 0.84).GetSafeNormal();
	const FVector ExpectedMountedUp = FVector::VectorPlaneProject(
		TestRadialUp,
		TestLaunchForward).GetSafeNormal();
	const FQuat MountedRotation = ABTSMakeSlingshotMountedBirdRotation(
		TestLaunchForward,
		TestRadialUp);
	TestTrue(TEXT("Mounted bird actor +X faces the launch direction"),
		MountedRotation.GetAxisX().Equals(TestLaunchForward, 1.0e-4));
	TestTrue(TEXT("Mounted bird actor +Z uses the closest orthogonal radial up"),
		MountedRotation.GetAxisZ().Equals(ExpectedMountedUp, 1.0e-4));
	TestFalse(TEXT("Mounted bird orientation remains finite for degenerate input"),
		ABTSMakeSlingshotMountedBirdRotation(
			FVector::ZeroVector,
			FVector::ZeroVector).ContainsNaN());
	USceneComponent* MountedVisualFixture = NewObject<USceneComponent>();
	const FVector AuthoredVisualLocation(3.0f, -5.0f, 7.0f);
	const FQuat AuthoredVisualRotation = FRotator(0.0f, -90.0f, 0.0f).Quaternion();
	MountedVisualFixture->SetWorldLocationAndRotation(
		FVector(100.0f, 200.0f, 300.0f),
		FRotator(21.0f, 67.0f, -13.0f));
	ABTSRestoreSlingshotMountedBirdVisualFrame(
		*MountedVisualFixture,
		AuthoredVisualLocation,
		AuthoredVisualRotation);
	TestTrue(TEXT("Space formation repair restores authored visual location"),
		MountedVisualFixture->GetRelativeLocation().Equals(
			AuthoredVisualLocation,
			1.0e-4));
	TestTrue(TEXT("Space formation repair restores authored visual axis correction"),
		MountedVisualFixture->GetRelativeRotation().Quaternion().Equals(
			AuthoredVisualRotation,
			1.0e-4));
	TestEqual(TEXT("Twig geometry stays on the ordinary frame"),
		Twig.PouchSizeCM, Simple.PouchSizeCM);
	TestEqual(TEXT("Reinforced geometry stays on the ordinary frame"),
		Reinforced.PouchSizeCM, Simple.PouchSizeCM);
	TestEqual(TEXT("Ordinary pouch native size remains frozen"),
		Simple.PouchSizeCM, FVector(42.0f, 60.0f, 12.0f));
	TestEqual(TEXT("Space pouch is exactly twice the ordinary pouch"),
		Space.PouchSizeCM, Simple.PouchSizeCM * 2.0f);
	TestEqual(TEXT("Space stake spacing owns the finale frame"),
		Space.BaseStakeSpacingCM, ABTSFinaleSpaceStakeSpacingCM);
	TestTrue(TEXT("Space stakes are taller than ordinary stakes"),
		Space.StakeHeightCM > Simple.StakeHeightCM);
	TestTrue(TEXT("Space stakes are wider than ordinary stakes"),
		Space.StakeDiameterCM > Simple.StakeDiameterCM);
	TestTrue(TEXT("Space cords are thicker than ordinary cords"),
		Space.CordThicknessCM > Simple.CordThicknessCM);
	TestEqual(TEXT("Serialized v1 spacing migrates to the v2 finale frame"),
		ABTSResolveFinaleSpaceStakeSpacingCM(
			ABTSLegacyFinaleSpaceStakeSpacingCM),
		ABTSFinaleSpaceStakeSpacingCM);
	TestEqual(TEXT("Authored non-legacy spacing remains authoritative"),
		ABTSResolveFinaleSpaceStakeSpacingCM(360.0f), 360.0f);

	TestNotNull(TEXT("Space stake steel mesh is bound"), Space.StakeVisual.Mesh.Get());
	TestNotNull(TEXT("Space stake steel material is bound"), Space.StakeVisual.Material.Get());
	TestNotNull(TEXT("Space cord steel mesh is bound"), Space.CordVisual.Mesh.Get());
	TestNotNull(TEXT("Space cord steel material is bound"), Space.CordVisual.Material.Get());
	TestNotNull(TEXT("Space pouch steel mesh is bound"), Space.PouchVisual.Mesh.Get());
	TestNotNull(TEXT("Space pouch steel material is bound"), Space.PouchVisual.Material.Get());
	if (Space.PouchVisual.Mesh && Space.PouchVisual.Material)
	{
		TestEqual(TEXT("Space pouch consumes the Steel mesh"),
			Space.PouchVisual.Mesh->GetPathName(),
			FString(TEXT("/Game/StaticMesh/Pouch/Steel/SM_Pouch_Steel.SM_Pouch_Steel")));
		TestEqual(TEXT("Space pouch consumes the Steel material"),
			Space.PouchVisual.Material->GetPathName(),
			FString(TEXT("/Game/StaticMesh/Pouch/Steel/MI_Pouch_Steel.MI_Pouch_Steel")));
	}

	FScopedM51SlingshotAssemblyWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	AABTSM51SlingshotStake* Left = World != nullptr
		? SpawnTestStake(*World, FVector::ZeroVector,
			EABTSItemId::SpaceStake, 100)
		: nullptr;
	AABTSM51SlingshotStake* Right = World != nullptr
		? SpawnTestStake(*World, FVector(ABTSFinaleSpaceStakeSpacingCM, 0.0f, 0.0f),
			EABTSItemId::SpaceStake, 101)
		: nullptr;
	AABTSM51SlingshotCord* Cord = World != nullptr
		? SpawnTestActor<AABTSM51SlingshotCord>(*World, FVector::ZeroVector)
		: nullptr;
	TestNotNull(TEXT("Space left stake spawns"), Left);
	TestNotNull(TEXT("Space right stake spawns"), Right);
	TestNotNull(TEXT("Space cord spawns"), Cord);
	if (Left && Right && Cord)
	{
		Cord->InitializeCordWithTier(
			Left,
			Right,
			Left->GetVisualTopWorldLocation(),
			Right->GetVisualTopWorldLocation(),
			EABTSSlingshotTier::Space);
		TestEqual(TEXT("Runtime cord consumes the Space pouch size"),
			Cord->GetPouchSizeCM(), Space.PouchSizeCM);
		TestEqual(TEXT("Runtime cord consumes the Space thickness"),
			Cord->GetCordThicknessCM(), Space.CordThicknessCM);
		const UStaticMeshComponent* Pouch = Cord->GetPouchVisualComponent();
		TestNotNull(TEXT("Runtime Space pouch component exists"), Pouch);
		if (Pouch)
		{
			TestEqual(TEXT("Visible pouch is the click volume"),
				Pouch->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
			TestEqual(TEXT("Visible pouch blocks the click trace"),
				Pouch->GetCollisionResponseToChannel(ECC_Visibility), ECR_Block);
			TestTrue(TEXT("Runtime pouch mesh stays Steel"),
				Pouch->GetStaticMesh() == Space.PouchVisual.Mesh.Get());
			TestTrue(TEXT("Runtime pouch material stays Steel"),
				Pouch->GetMaterial(0) == Space.PouchVisual.Material.Get());
		}
		TestEqual(TEXT("Cord segment A remains presentation-only"),
			Cord->GetCordSegmentAComponent()->GetCollisionEnabled(),
			ECollisionEnabled::NoCollision);
		TestEqual(TEXT("Cord segment B remains presentation-only"),
			Cord->GetCordSegmentBComponent()->GetCollisionEnabled(),
			ECollisionEnabled::NoCollision);
	}
	return true;
}

#endif
