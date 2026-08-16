// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Building/ABTSM7BuildingModule.h"

#include "Components/ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/BodyInstance.h"
#include "UObject/StrongObjectPtr.h"
#include "World/ABTSCollisionChannels.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM7DeferredImpactCollisionTest,
	"ABTS.M7.DeferredImpactCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM7DeferredImpactCollisionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TStrongObjectPtr<UStaticMeshComponent> Module(
		NewObject<UStaticMeshComponent>(GetTransientPackage()));
	Module->SetCollisionProfileName(TEXT("BlockAll"));
	Module->SetCollisionObjectType(ABTSDeveloperObstacleChannel);
	Module->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TestFalse(TEXT("Prepared static module is not a simulated body"),
		Module->IsSimulatingPhysics());
	TestEqual(TEXT("Prepared static module keeps the M7 building channel"),
		Module->GetCollisionObjectType(), ABTSDeveloperObstacleChannel);
	TestEqual(TEXT("Prepared static module keeps physics-capable collision"),
		Module->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);

	FABTSM7DeferredImpactCollisionPolicy::ApplyTo(*Module.Get());
	TestTrue(TEXT("First-hit policy enables CCD before live promotion"),
		Module->BodyInstance.bUseCCD);
	TestTrue(TEXT("First-hit policy forbids zero initial depenetration velocity"),
		Module->GetMaxDepenetrationVelocity(NAME_None)
			>= FABTSM7DeferredImpactCollisionPolicy::
				MinimumInitialOverlapDepenetrationCMPerSec);
	FString PolicyError;
	TestFalse(TEXT("No live Chaos body fails closed rather than claiming promoted"),
		FABTSM7DeferredImpactCollisionPolicy::VerifyDynamic(
			*Module.Get(), PolicyError));
	TestFalse(TEXT("The pre-promotion rejection is diagnostic"),
		PolicyError.IsEmpty());

	TStrongObjectPtr<UProceduralMeshComponent> Terrain(
		NewObject<UProceduralMeshComponent>(GetTransientPackage()));
	Terrain->SetCollisionObjectType(ECC_WorldStatic);
	Terrain->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Terrain->SetCollisionResponseToAllChannels(ECR_Block);
	TestEqual(TEXT("Authority terrain blocks dynamic M7 modules after first hit"),
		Terrain->GetCollisionResponseToChannel(ABTSDeveloperObstacleChannel),
		ECR_Block);
	return true;
}

#endif
