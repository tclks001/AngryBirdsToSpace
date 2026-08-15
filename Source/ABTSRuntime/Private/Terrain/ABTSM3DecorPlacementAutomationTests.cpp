// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "PCG/ABTSM3R5AcceptanceManifest.h"
#include "Terrain/ABTSM3DecorPlacement.h"
#include "Terrain/ABTSM3Planet.h"

namespace
{
FABTSM3DecorCollisionShape MakeABTSM3SyntheticDecorShape(
	const FVector& HalfExtent,
	const FVector& Center = FVector::ZeroVector)
{
	FABTSM3DecorCollisionShape Shape;
	Shape.LocalBounds = FBox(Center - HalfExtent, Center + HalfExtent);
	for (int32 X = -1; X <= 1; X += 2)
	{
		for (int32 Y = -1; Y <= 1; Y += 2)
		{
			for (int32 Z = -1; Z <= 1; Z += 2)
			{
				Shape.LocalSurfaceSamples.Add(
					Center + FVector(X, Y, Z) * HalfExtent);
			}
		}
	}
	Shape.LocalSurfaceSamples.Add(Center - FVector(0.0f, 0.0f, HalfExtent.Z));
	return Shape;
}

class FScopedABTSM3DecorPlacementTestWorld
{
public:
	FScopedABTSM3DecorPlacementTestWorld()
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
			TEXT("ABTSM3DecorPlacementTestWorld"),
			nullptr,
			true,
			ERHIFeatureLevel::Num,
			&Values);
	}

	~FScopedABTSM3DecorPlacementTestWorld()
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3DecorCollisionSeatTest,
	"ABTS.M3.DecorPlacement.01CollisionSeatUsesMeshSupport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM3DecorCollisionSeatTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FABTSM3DecorCollisionShape RockShape =
		MakeABTSM3SyntheticDecorShape(
			FVector(30.0f, 30.0f, 40.0f));
	FTransform SeatedTransform;
	float CorrectionCM = 0.0f;
	float MinimumClearanceCM = 0.0f;
	constexpr float PlanetRadiusCM = 50000.0f;
	constexpr float RequiredClearanceCM = 2.0f;
	TestTrue(TEXT("collision support seats on the sphere"),
		FABTSM3DecorPlacementGeometry::TrySeatOnSurface(
			RockShape,
			FVector::UpVector,
			FQuat::Identity,
			1.0f,
			FVector(0.0f, 0.0f, PlanetRadiusCM - 8.0f),
			RequiredClearanceCM,
			[](const FVector& Point)
			{
				return Point.Size() - PlanetRadiusCM;
			},
			SeatedTransform,
			CorrectionCM,
			MinimumClearanceCM));
	TestTrue(TEXT("fixed minus-eight pivot required a material correction"),
		CorrectionCM > 45.0f);
	TestTrue(TEXT("all collision samples retain positive clearance"),
		MinimumClearanceCM + 0.01f >= RequiredClearanceCM);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3DecorCrossTypeSeparationTest,
	"ABTS.M3.DecorPlacement.02CrossTypeSpatialSeparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM3DecorCrossTypeSeparationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FABTSM3DecorCollisionShape TreeShape =
		MakeABTSM3SyntheticDecorShape(FVector(50.0f));
	const FABTSM3DecorCollisionShape RockShape =
		MakeABTSM3SyntheticDecorShape(FVector(50.0f));
	const FABTSM3DecorOrientedBounds TreeBounds =
		FABTSM3DecorPlacementGeometry::BuildOrientedBounds(
			TreeShape,
			FTransform(FQuat::Identity, FVector::ZeroVector));
	FABTSM3DecorSpatialHash SpatialHash(125.0f);
	SpatialHash.Add(TreeBounds);

	float AxisGapCM = 0.0f;
	const FABTSM3DecorOrientedBounds TooCloseRock =
		FABTSM3DecorPlacementGeometry::BuildOrientedBounds(
			RockShape,
			FTransform(FQuat::Identity, FVector(103.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("tree-rock pair inside four-centimeter margin is rejected"),
		SpatialHash.WouldOverlap(TooCloseRock, 4.0f, AxisGapCM));

	const FABTSM3DecorOrientedBounds ClearRock =
		FABTSM3DecorPlacementGeometry::BuildOrientedBounds(
			RockShape,
			FTransform(FQuat::Identity, FVector(105.0f, 0.0f, 0.0f)));
	TestFalse(TEXT("tree-rock pair beyond four-centimeter margin is accepted"),
		SpatialHash.WouldOverlap(ClearRock, 4.0f, AxisGapCM));
	TestTrue(TEXT("accepted pair reports at least the requested axis gap"),
		AxisGapCM + 0.01f >= 4.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3DecorAttemptSeedTest,
	"ABTS.M3.DecorPlacement.03IndependentAttemptSeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM3DecorAttemptSeedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TSet<uint32> Seeds;
	for (int32 WorldSeed = 7000; WorldSeed < 7103; ++WorldSeed)
	{
		for (int32 Slot = 0; Slot < 4; ++Slot)
		{
			for (int32 Attempt = 0; Attempt < 8; ++Attempt)
			{
				const uint32 First =
					FABTSM3DecorPlacementGeometry::MakeAttemptSeed(
						WorldSeed, 17, Slot, Attempt, 91u);
				const uint32 Second =
					FABTSM3DecorPlacementGeometry::MakeAttemptSeed(
						WorldSeed, 17, Slot, Attempt, 91u);
				TestEqual(TEXT("derived attempt seed is deterministic"),
					Second, First);
				Seeds.Add(First);
			}
		}
	}
	TestEqual(TEXT("103-seed attempt identities remain independent"),
		Seeds.Num(), 103 * 4 * 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3DecorProductionRebuildTest,
	"ABTS.M3.DecorPlacement.04ProductionMeshDeterministicRebuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM3DecorProductionRebuildTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FScopedABTSM3DecorPlacementTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("transient decor test World is created"), World);
	if (World == nullptr)
	{
		return false;
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM3Planet* Planet = World->SpawnActor<AABTSM3Planet>(
		AABTSM3Planet::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	TestNotNull(TEXT("native M3 decor producer spawns"), Planet);
	if (Planet == nullptr)
	{
		return false;
	}
	Planet->WorldSeed = 312503;
	Planet->SurfaceSubdivision = 2;
	Planet->InstancesPerCell = 2;
	Planet->DecorPlacementAttemptsPerSlot = 10;
	Planet->DecorGroundClearanceCM = 2.0f;
	Planet->DecorMaximumAdditionalSeatLiftCM = 25.0f;
	Planet->DecorInstanceSeparationMarginCM = 4.0f;
	TestTrue(TEXT("production M3 world rebuilds with collision-legal decor"),
		Planet->RebuildPlanet());
	TestTrue(TEXT("first collision-legal production rebuild stays in budget"),
		Planet->GetLastM3RebuildDurationMS()
			<= FABTSM3R5AcceptanceManifest::FullRebuildBudgetMS);
	const FABTSM3DecorPlacementSummary First = Planet->GetDecorPlacementSummary();
	TestTrue(TEXT("decor placement result is accepted"), First.bAccepted);
	TestTrue(TEXT("production mesh path requests decor"), First.RequestedSlots > 0);
	TestTrue(TEXT("production mesh path accepts decor"), First.AcceptedInstances > 0);
	TestEqual(TEXT("published HISM count matches accepted placement count"),
		Planet->ForestHISM->GetInstanceCount()
			+ Planet->RockHISM->GetInstanceCount(),
		First.AcceptedInstances);
	TestTrue(TEXT("production support samples retain ground clearance"),
		First.MinimumGroundClearanceCM + 0.02f
			>= Planet->DecorGroundClearanceCM);
	TestTrue(TEXT("production seating avoids excessive visual lift"),
		First.MaxSeatCorrectionCM
			<= 85.0f);
	if (First.AcceptedInstances > 1)
	{
		TestTrue(TEXT("production pairs retain the configured separation"),
			First.MinimumPairAxisGapCM + 0.02f
				>= Planet->DecorInstanceSeparationMarginCM);
	}
	TestNotEqual(TEXT("accepted production placement has a result hash"),
		First.PlacementResultHash,
		int64(0));

	TestTrue(TEXT("repeated production M3 rebuild succeeds"),
		Planet->RebuildPlanet());
	TestTrue(TEXT("repeated collision-legal production rebuild stays in budget"),
		Planet->GetLastM3RebuildDurationMS()
			<= FABTSM3R5AcceptanceManifest::FullRebuildBudgetMS);
	const FABTSM3DecorPlacementSummary Second = Planet->GetDecorPlacementSummary();
	TestTrue(TEXT("repeated decor placement remains accepted"), Second.bAccepted);
	TestEqual(TEXT("repeated accepted instance count is deterministic"),
		Second.AcceptedInstances,
		First.AcceptedInstances);
	TestEqual(TEXT("repeated placement result hash is deterministic"),
		Second.PlacementResultHash,
		First.PlacementResultHash);
	return true;
}

#endif
