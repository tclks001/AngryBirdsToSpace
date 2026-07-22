// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM51GameMode.h"

#include "ABTSRuntime.h"
#include "Player/ABTSM51PlayerController.h"
#include "World/ABTSM51WorldSystem.h"

AABTSM51GameMode::AABTSM51GameMode()
{
	PlayerControllerClass = AABTSM51PlayerController::StaticClass();
	WorldSystemClass = AABTSM51WorldSystem::StaticClass();
}

void AABTSM51GameMode::OnInitialPlayerPlaced(
	ACharacter& Character,
	const FTransform& SpawnTransform,
	const int32 SpawnCellId)
{
	Super::OnInitialPlayerPlaced(Character, SpawnTransform, SpawnCellId);
	if (GetWorld() == nullptr) return;
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM51WorldSystem* System = GetWorld()->SpawnActor<AABTSM51WorldSystem>(WorldSystemClass, FTransform::Identity, Parameters);
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M5.1] Entry ready=%d StartCell=%d"), System ? 1 : 0, SpawnCellId);
}

