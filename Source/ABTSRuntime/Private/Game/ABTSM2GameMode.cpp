// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM2GameMode.h"

#include "ABTSRuntime.h"
#include "Player/ABTSM2BirdCharacter.h"
#include "Player/ABTSM1PlayerController.h"
#include "UI/ABTSM1HUD.h"

AABTSM2GameMode::AABTSM2GameMode()
{
	DefaultPawnClass = AABTSM2BirdCharacter::StaticClass();
	PlayerControllerClass = AABTSM1PlayerController::StaticClass();
	HUDClass = AABTSM1HUD::StaticClass();
}

void AABTSM2GameMode::BeginPlay()
{
	Super::BeginPlay();
	if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
	{
		APawn* ExistingPawn = PlayerController->GetPawn();
		if (ExistingPawn != nullptr && !ExistingPawn->IsA<AABTSM2BirdCharacter>())
		{
			const FTransform SpawnTransform = ExistingPawn->GetActorTransform();
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			if (AABTSM2BirdCharacter* M2Pawn = GetWorld()->SpawnActor<AABTSM2BirdCharacter>(AABTSM2BirdCharacter::StaticClass(), SpawnTransform, SpawnParameters))
			{
				PlayerController->Possess(M2Pawn);
				ExistingPawn->Destroy();
				UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M2] Replaced obsolete M1 pawn serialized by the M2 GameMode Blueprint."));
			}
		}
	}
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M2] Dedicated planet entry ready. CellTopo is logic; continuous surface is collision/presentation only."));
}
