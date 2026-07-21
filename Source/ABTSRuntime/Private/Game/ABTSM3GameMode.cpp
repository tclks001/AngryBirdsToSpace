// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM3GameMode.h"

#include "ABTSRuntime.h"
#include "Player/ABTSM1PlayerController.h"
#include "Player/ABTSM25BirdCharacter.h"
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
}

