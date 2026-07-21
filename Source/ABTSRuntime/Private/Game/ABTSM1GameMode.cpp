// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM1GameMode.h"

#include "ABTSRuntime.h"
#include "Player/ABTSM1BirdCharacter.h"
#include "Player/ABTSM1PlayerController.h"
#include "UI/ABTSM1HUD.h"

AABTSM1GameMode::AABTSM1GameMode()
{
	DefaultPawnClass = AABTSM1BirdCharacter::StaticClass();
	PlayerControllerClass = AABTSM1PlayerController::StaticClass();
	HUDClass = AABTSM1HUD::StaticClass();
}

void AABTSM1GameMode::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M1] Independent entry ready. No turn, faction, Cell-click, or NPC gameplay is loaded."));
}
