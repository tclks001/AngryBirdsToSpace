// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/ABTSM1PlayerController.h"

#include "ABTSRuntime.h"

void AABTSM1PlayerController::BeginPlay()
{
	Super::BeginPlay();

	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M1] Player controller initialized."));
}
