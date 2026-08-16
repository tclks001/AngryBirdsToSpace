// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Movement/ABTSSatelliteGravityMovementPolicy.h"
#include "Planet/ABTSM2Planet.h"
#include "Planet/ABTSPrimaryPlanetMovementAuthority.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "World/ABTSM9Satellite.h"

namespace
{
class FScopedPrimaryPlanetAuthorityWorld
{
public:
	FScopedPrimaryPlanetAuthorityWorld()
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
			TEXT("ABTSPrimaryPlanetAuthorityWorld"),
			nullptr,
			true,
			ERHIFeatureLevel::Num,
			&Values);
	}

	~FScopedPrimaryPlanetAuthorityWorld()
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
	FABTSSlingshotDeveloperObstacleCollisionPolicyTest,
	"ABTS.M6.Collision.DeveloperWalkSlingshotOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSSlingshotDeveloperObstacleCollisionPolicyTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TestEqual(
		TEXT("Normal developer walking may ignore developer obstacles"),
		AABTSM25BirdCharacter::ResolveDeveloperObstacleCollisionResponse(
			true, false),
		ECR_Ignore);
	TestEqual(
		TEXT("Slingshot flight blocks buildings even with developer walk enabled"),
		AABTSM25BirdCharacter::ResolveDeveloperObstacleCollisionResponse(
			true, true),
		ECR_Block);
	TestEqual(
		TEXT("Normal gameplay always blocks developer obstacles"),
		AABTSM25BirdCharacter::ResolveDeveloperObstacleCollisionResponse(
			false, false),
		ECR_Block);
	TestEqual(
		TEXT("Normal gameplay walking is blocked by river air walls"),
		AABTSM25BirdCharacter::ResolveWalkBarrierCollisionResponse(
			false, false),
		ECR_Block);
	TestEqual(
		TEXT("Slingshot flight ignores river air walls"),
		AABTSM25BirdCharacter::ResolveWalkBarrierCollisionResponse(
			false, true),
		ECR_Ignore);
	TestEqual(
		TEXT("Developer walking may ignore river air walls"),
		AABTSM25BirdCharacter::ResolveWalkBarrierCollisionResponse(
			true, false),
		ECR_Ignore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSGroundLocomotionSatelliteGravityGateTest,
	"ABTS.M4.GroundLocomotion.SatelliteGravityStateGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSGroundLocomotionSatelliteGravityGateTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FVector RawSatelliteAcceleration(123.25, -456.5, 789.75);
	TestTrue(
		TEXT("Chaos and Force ground locomotion consume no satellite acceleration"),
		FABTSSatelliteGravityMovementPolicy::ResolveAcceleration(
			false,
			RawSatelliteAcceleration) == FVector::ZeroVector);
	TestTrue(
		TEXT("Chaos and Force ballistic flight preserve the exact satellite acceleration"),
		FABTSSatelliteGravityMovementPolicy::ResolveAcceleration(
			true,
			RawSatelliteAcceleration) == RawSatelliteAcceleration);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSGroundLocomotionPrimaryPlanetAuthorityTest,
	"ABTS.M4.GroundLocomotion.PrimaryPlanetAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSGroundLocomotionPrimaryPlanetAuthorityTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FScopedPrimaryPlanetAuthorityWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("Primary-planet authority test World is created"), World);
	if (World == nullptr)
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM9Satellite* Satellite = World->SpawnActor<AABTSM9Satellite>(
		AABTSM9Satellite::StaticClass(), FTransform::Identity, SpawnParameters);
	AABTSM2Planet* Primary = World->SpawnActor<AABTSM2Planet>(
		AABTSM2Planet::StaticClass(), FTransform::Identity, SpawnParameters);
	TestNotNull(TEXT("Satellite is spawned before the primary"), Satellite);
	TestNotNull(TEXT("Primary is spawned"), Primary);
	if (Satellite == nullptr || Primary == nullptr)
	{
		return false;
	}

	Satellite->LogicalSubdivision = 1;
	Satellite->SurfaceSubdivision = 1;
	Satellite->PlanetRadiusCM = 1250.0f;
	Primary->LogicalSubdivision = 1;
	Primary->SurfaceSubdivision = 1;
	Primary->PlanetRadiusCM = 10000.0f;
	TestTrue(TEXT("Satellite fixture is ready"), Satellite->RebuildPlanet());
	TestTrue(TEXT("Primary fixture is ready"), Primary->RebuildPlanet());

	TWeakObjectPtr<AABTSM2Planet> PoisonedCache = Satellite;
	AABTSM2Planet* Resolved =
		ABTSPrimaryPlanetMovementAuthority::Resolve(World, PoisonedCache);
	TestFalse(
		TEXT("M9 satellite is never a ground-movement primary candidate"),
		ABTSPrimaryPlanetMovementAuthority::IsPrimaryCandidate(Satellite));
	TestTrue(
		TEXT("A ready non-satellite planet is a primary candidate"),
		ABTSPrimaryPlanetMovementAuthority::IsPrimaryCandidate(Primary));
	TestTrue(
		TEXT("A satellite-first/stale satellite cache resolves to the production primary"),
		Resolved == Primary && PoisonedCache.Get() == Primary);
	return true;
}

#endif
