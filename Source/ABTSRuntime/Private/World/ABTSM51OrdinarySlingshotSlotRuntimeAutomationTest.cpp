// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Crafting/ABTSCraftingSystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Terrain/ABTSM3Planet.h"
#include "World/ABTSM51OrdinarySlingshotSlotSnapshot.h"
#include "World/ABTSM51WorldActors.h"
#include "World/ABTSM51WorldSystem.h"

namespace
{
class FScopedM51OrdinarySlotWorld
{
public:
	explicit FScopedM51OrdinarySlotWorld(const TCHAR* WorldName)
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
			WorldName,
			nullptr,
			true,
			ERHIFeatureLevel::Num,
			&Values);
	}

	~FScopedM51OrdinarySlotWorld()
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
TActor* SpawnOrdinarySlotTestActor(UWorld& World)
{
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World.SpawnActor<TActor>(
		TActor::StaticClass(),
		FTransform::Identity,
		Parameters);
}

AABTSM3Planet* BuildSmallAcceptedPlanet(UWorld& World)
{
	AABTSM3Planet* Planet =
		SpawnOrdinarySlotTestActor<AABTSM3Planet>(World);
	if (Planet == nullptr)
	{
		return nullptr;
	}
	Planet->SurfaceSubdivision = 1;
	Planet->InstancesPerCell = 0;
	return Planet->RebuildPlanet() ? Planet : nullptr;
}

TArray<int32> FindUsableOrdinarySlotCells(
	const AABTSM3Planet& Planet,
	const int32 DesiredCount)
{
	TArray<int32> Result;
	const TArray<FABTSM3CellState>& States =
		Planet.GetGeneratedCellStates();
	const int32 FinaleAnchor =
		Planet.GetFinaleLaunchFrame().AnchorCellId;
	for (int32 CellId = 0;
		CellId < States.Num() && Result.Num() < DesiredCount;
		++CellId)
	{
		if (!States[CellId].bWater
			&& !States[CellId].bBuildingAnchor
			&& CellId != FinaleAnchor)
		{
			Result.Add(CellId);
		}
	}
	return Result;
}

FABTSM51OrdinarySlingshotSlotSnapshot MakeRuntimeSnapshot(
	const TArray<int32>& CellIds)
{
	FABTSM51OrdinarySlingshotSlotSnapshot Snapshot;
	Snapshot.LayoutHash = 0x51A0ull;
	Snapshot.CandidateHash = 0x51B0ull;
	Snapshot.MaxCordLengthCM = 987;
	Snapshot.SlotGroups.AddDefaulted_GetRef().SlotCellIds =
		CellIds;
	return Snapshot;
}

void CountLiveSlots(
	UWorld& World,
	int32& OutOrdinaryCount,
	int32& OutFinaleCount)
{
	OutOrdinaryCount = 0;
	OutFinaleCount = 0;
	for (TActorIterator<AABTSM51SlingshotDirtHole> It(&World);
		It;
		++It)
	{
		if (It->IsActorBeingDestroyed())
		{
			continue;
		}
		if (It->IsFinaleSpaceSlot())
		{
			++OutFinaleCount;
		}
		else
		{
			++OutOrdinaryCount;
		}
	}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM51OrdinarySlingshotSlotRuntimeTest,
	"ABTS.M51.OrdinarySlots.Runtime",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM51OrdinarySlingshotSlotRuntimeTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;

	{
		FScopedM51OrdinarySlotWorld ScopedWorld(
			TEXT("ABTSM51AcceptedOrdinarySlotWorld"));
		UWorld* World = ScopedWorld.Get();
		TestNotNull(
			TEXT("Accepted-slot test World is created"),
			World);
		if (World == nullptr)
		{
			return false;
		}
		AABTSM3Planet* Planet =
			BuildSmallAcceptedPlanet(*World);
		TestNotNull(
			TEXT("Small accepted M3 world rebuilds"),
			Planet);
		if (Planet == nullptr)
		{
			return false;
		}

		const TArray<int32> Cells =
			FindUsableOrdinarySlotCells(*Planet, 2);
		TestEqual(
			TEXT("The small world exposes two usable ordinary cells"),
			Cells.Num(),
			2);
		if (Cells.Num() != 2)
		{
			return false;
		}

		SpawnOrdinarySlotTestActor<AABTSCraftingSystem>(*World);
		AABTSM51WorldSystem* System =
			SpawnOrdinarySlotTestActor<AABTSM51WorldSystem>(
				*World);
		TestNotNull(
			TEXT("Accepted-slot WorldSystem spawns"),
			System);
		if (System == nullptr)
		{
			return false;
		}
		const FABTSM51OrdinarySlingshotSlotSnapshot Snapshot =
			MakeRuntimeSnapshot(Cells);
		TestTrue(
			TEXT("Accepted snapshot configures before BeginPlay"),
			System->
				ConfigureAcceptedOrdinarySlingshotSlotSnapshot(
					Snapshot));
		System->DispatchBeginPlay();

		int32 OrdinaryCount = 0;
		int32 FinaleCount = 0;
		CountLiveSlots(
			*World,
			OrdinaryCount,
			FinaleCount);
		TestEqual(
			TEXT("Every accepted snapshot Cell spawns one ordinary DirtHole"),
			OrdinaryCount,
			2);
		TestEqual(
			TEXT("Accepted ordinary snapshot leaves the unique Space pair intact"),
			FinaleCount,
			2);
		TestEqual(
			TEXT("Accepted snapshot publishes its exact M6 length gate"),
			System->GetActiveOrdinaryMaxCordLengthCM(),
			987);
		AABTSM51SlingshotDirtHole* Left = nullptr;
		AABTSM51SlingshotDirtHole* Right = nullptr;
		TestTrue(
			TEXT("Finale pair identity remains usable"),
			System->GetFinaleSpaceSlots(Left, Right));

		System->Tick(0.1f);
		int32 RepeatedOrdinaryCount = 0;
		int32 RepeatedFinaleCount = 0;
		CountLiveSlots(
			*World,
			RepeatedOrdinaryCount,
			RepeatedFinaleCount);
		TestEqual(
			TEXT("A later initialization tick does not duplicate ordinary slots"),
			RepeatedOrdinaryCount,
			OrdinaryCount);
		TestEqual(
			TEXT("A later initialization tick does not duplicate Finale slots"),
			RepeatedFinaleCount,
			FinaleCount);
	}

	{
		FScopedM51OrdinarySlotWorld ScopedWorld(
			TEXT("ABTSM51RejectedOrdinarySlotWorld"));
		UWorld* World = ScopedWorld.Get();
		if (World == nullptr)
		{
			return false;
		}
		AABTSM3Planet* Planet =
			BuildSmallAcceptedPlanet(*World);
		if (Planet == nullptr)
		{
			return false;
		}
		const TArray<int32> ValidCells =
			FindUsableOrdinarySlotCells(*Planet, 1);
		if (ValidCells.Num() != 1)
		{
			return false;
		}

		FABTSM51OrdinarySlingshotSlotSnapshot InvalidTopology =
			MakeRuntimeSnapshot(
				{ValidCells[0], Planet->LogicalCells.Num()});
		TestTrue(
			TEXT("The invalid topology fixture is structurally valid"),
			InvalidTopology.IsStructurallyUsable());
		SpawnOrdinarySlotTestActor<AABTSCraftingSystem>(*World);
		AABTSM51WorldSystem* System =
			SpawnOrdinarySlotTestActor<AABTSM51WorldSystem>(
				*World);
		if (System == nullptr)
		{
			return false;
		}
		TestTrue(
			TEXT("Topology validation is intentionally deferred until planet consumption"),
			System->
				ConfigureAcceptedOrdinarySlingshotSlotSnapshot(
					InvalidTopology));
		AddExpectedErrorPlain(
			TEXT("[ABTS][M5.1][OrdinarySlots] Source=AcceptedSnapshot Accepted=0"),
			EAutomationExpectedErrorFlags::Contains,
			1);
		AddExpectedErrorPlain(
			TEXT("[ABTS][M5.1] World initialization rejected by ordinary slingshot slot gate."),
			EAutomationExpectedErrorFlags::Contains,
			1);
		System->DispatchBeginPlay();

		int32 OrdinaryCount = 0;
		int32 FinaleCount = 0;
		CountLiveSlots(
			*World,
			OrdinaryCount,
			FinaleCount);
		TestEqual(
			TEXT("An invalid monthly snapshot produces zero ordinary slots"),
			OrdinaryCount,
			0);
		TestEqual(
			TEXT("An invalid ordinary snapshot never changes the independent Space pair"),
			FinaleCount,
			2);
	}
	return true;
}

#endif
