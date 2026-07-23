// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM71PhysicsTestGameMode.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Party/ABTSBirdParty.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Player/ABTSM6PlayerController.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "TestStage/ABTSM71TestStageActors.h"
#include "UI/ABTSM4PartyHUD.h"

AABTSM71PhysicsTestGameMode::AABTSM71PhysicsTestGameMode()
{
	DefaultPawnClass = AABTSM25BirdCharacter::StaticClass();
	PlayerControllerClass = AABTSM6PlayerController::StaticClass();
	HUDClass = AABTSM4PartyHUD::StaticClass();
	BirdPartyClass = AABTSBirdParty::StaticClass();
	SlingshotSystemClass = AABTSM6SlingshotSystem::StaticClass();
	BuildingMaterialSystemClass = AABTSM7BuildingMaterialSystem::StaticClass();
}

void AABTSM71PhysicsTestGameMode::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();
	if (World == nullptr) return;

	AABTSM71PhysicsTestStage* Stage = nullptr;
	for (TActorIterator<AABTSM71PhysicsTestStage> It(World); It; ++It) { Stage = *It; break; }
	AABTSM71PlayerStart* Start = nullptr;
	for (TActorIterator<AABTSM71PlayerStart> It(World); It; ++It) { Start = *It; break; }
	APlayerController* Controller = World->GetFirstPlayerController();
	AABTSM25BirdCharacter* InitialBird = Controller ? Cast<AABTSM25BirdCharacter>(Controller->GetPawn()) : nullptr;
	if (Stage == nullptr || Start == nullptr || InitialBird == nullptr)
	{
		UE_LOG(LogABTSRuntime, Error, TEXT("[ABTS][M7.1] Startup failed Stage=%d PlayerStart=%d Bird=%d"), Stage ? 1 : 0, Start ? 1 : 0, InitialBird ? 1 : 0);
		return;
	}

	const FVector PlaneOrigin = Stage->GetPlaneOrigin();
	const FVector PlaneUp = Stage->GetPlaneUp();
	FTransform SpawnTransform = Start->GetActorTransform();
	FVector SpawnLocation = SpawnTransform.GetLocation();
	const float MinimumHeight = InitialBird->GetCapsuleComponent()->GetScaledCapsuleRadius() + 4.0f;
	const float CurrentHeight = FVector::DotProduct(SpawnLocation - PlaneOrigin, PlaneUp);
	if (CurrentHeight < MinimumHeight) SpawnLocation += PlaneUp * (MinimumHeight - CurrentHeight);
	FVector SpawnForward = FVector::VectorPlaneProject(Start->GetActorForwardVector(), PlaneUp).GetSafeNormal();
	if (SpawnForward.IsNearlyZero()) SpawnForward = FVector::VectorPlaneProject(FVector::ForwardVector, PlaneUp).GetSafeNormal();
	SpawnTransform.SetLocation(SpawnLocation);
	SpawnTransform.SetRotation(FRotationMatrix::MakeFromXZ(SpawnForward, PlaneUp).ToQuat());
	InitialBird->EnablePlanarChaosMovement(PlaneOrigin, PlaneUp);
	InitialBird->SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
	InitialBird->ResetRadialMovementState();
	Controller->SetControlRotation(SpawnTransform.Rotator());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSBirdParty* Party = World->SpawnActor<AABTSBirdParty>(BirdPartyClass, FTransform::Identity, Params);
	const bool bPartyReady = Party && Party->InitializePlanarParty(InitialBird, PlaneOrigin, PlaneUp);

	AABTSM6SlingshotSystem* SlingshotSystem = World->SpawnActorDeferred<AABTSM6SlingshotSystem>(
		SlingshotSystemClass, FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (SlingshotSystem)
	{
		SlingshotSystem->ConfigurePlanarTestMode(PlaneOrigin, PlaneUp);
		SlingshotSystem->FinishSpawning(FTransform::Identity);
	}
	AABTSM7BuildingMaterialSystem* MaterialSystem = World->SpawnActor<AABTSM7BuildingMaterialSystem>(
		BuildingMaterialSystemClass, FTransform::Identity, Params);

	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7.1] Planar test stage ready Party=%d Slingshot=%d Materials=%d Spawn=(%.1f,%.1f,%.1f) Up=(%.3f,%.3f,%.3f)"),
		bPartyReady ? 1 : 0, SlingshotSystem ? 1 : 0, MaterialSystem ? 1 : 0,
		SpawnLocation.X, SpawnLocation.Y, SpawnLocation.Z, PlaneUp.X, PlaneUp.Y, PlaneUp.Z);
}
