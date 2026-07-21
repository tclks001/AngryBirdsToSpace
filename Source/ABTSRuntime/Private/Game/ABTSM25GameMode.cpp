// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM25GameMode.h"

#include "ABTSRuntime.h"
#include "Player/ABTSM1PlayerController.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "UI/ABTSM1HUD.h"

AABTSM25GameMode::AABTSM25GameMode()
{
	DefaultPawnClass = AABTSM25BirdCharacter::StaticClass();
	PlayerControllerClass = AABTSM1PlayerController::StaticClass();
	HUDClass = AABTSM1HUD::StaticClass();
}

void AABTSM25GameMode::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M2.5] Radial gravity, sweep collision, and grounded jump entry ready."));
}
