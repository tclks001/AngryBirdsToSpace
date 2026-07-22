// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM3GameMode.h"

#include "ABTSRuntime.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Player/ABTSM1PlayerController.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Terrain/ABTSM3Planet.h"
#include "TimerManager.h"
#include "UI/ABTSM1HUD.h"

AABTSM3GameMode::AABTSM3GameMode()
{
	DefaultPawnClass = AABTSM25BirdCharacter::StaticClass();
	PlayerControllerClass = AABTSM1PlayerController::StaticClass();
	HUDClass = AABTSM1HUD::StaticClass();
}

void AABTSM3GameMode::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M3] TaskGraph terrain presentation entry ready."));
	TryPlacePlayerAtInitialRoad();
}

void AABTSM3GameMode::TryPlacePlayerAtInitialRoad()
{
	constexpr int32 MaxAttempts = 30;
	constexpr float RetryIntervalSeconds = 0.1f;
	++InitialRoadSpawnAttempts;

	AABTSM3Planet* Planet = nullptr;
	for (TActorIterator<AABTSM3Planet> It(GetWorld()); It; ++It)
	{
		if (It->IsPlanetReady())
		{
			Planet = *It;
			break;
		}
	}

	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	ACharacter* Character = PlayerController ? Cast<ACharacter>(PlayerController->GetPawn()) : nullptr;
	if (Planet != nullptr && Character != nullptr)
	{
		const float CapsuleHalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		FTransform SpawnTransform;
		int32 SpawnCellId = INDEX_NONE;
		if (Planet->GetInitialRoadSpawnTransform(CapsuleHalfHeight, SpawnTransform, SpawnCellId))
		{
			if (AABTSM25BirdCharacter* BirdCharacter = Cast<AABTSM25BirdCharacter>(Character))
			{
				BirdCharacter->ResetRadialMovementState();
			}
			Character->SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
			PlayerController->SetControlRotation(SpawnTransform.Rotator());
			OnInitialPlayerPlaced(*Character, SpawnTransform, SpawnCellId);
			GetWorldTimerManager().ClearTimer(InitialRoadSpawnTimer);
			UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M3][Spawn] Player placed at Start road. Cell=%d Location=(%.1f,%.1f,%.1f) Attempts=%d"),
				SpawnCellId,
				SpawnTransform.GetLocation().X,
				SpawnTransform.GetLocation().Y,
				SpawnTransform.GetLocation().Z,
				InitialRoadSpawnAttempts);
			return;
		}
	}

	if (InitialRoadSpawnAttempts < MaxAttempts)
	{
		GetWorldTimerManager().SetTimer(InitialRoadSpawnTimer, this, &AABTSM3GameMode::TryPlacePlayerAtInitialRoad, RetryIntervalSeconds, false);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error, TEXT("[ABTS][M3][Spawn] Failed after %d attempts. PlanetReady=%d PawnReady=%d"),
			InitialRoadSpawnAttempts,
			Planet ? 1 : 0,
			Character ? 1 : 0);
	}
}

void AABTSM3GameMode::OnInitialPlayerPlaced(ACharacter& Character, const FTransform& SpawnTransform, const int32 SpawnCellId)
{
}
