// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM6GameMode.h"

#include "ABTSRuntime.h"
#include "Player/ABTSM6PlayerController.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "Kismet/GameplayStatics.h"

AABTSM6GameMode::AABTSM6GameMode()
{
	PlayerControllerClass = AABTSM6PlayerController::StaticClass();
	SlingshotSystemClass = AABTSM6SlingshotSystem::StaticClass();
}

void AABTSM6GameMode::OnInitialPlayerPlaced(ACharacter& Character, const FTransform& SpawnTransform, const int32 SpawnCellId)
{
	Super::OnInitialPlayerPlaced(Character, SpawnTransform, SpawnCellId);
	AABTSM6SlingshotSystem* System = GetWorld()->SpawnActorDeferred<AABTSM6SlingshotSystem>(
		SlingshotSystemClass,
		FTransform::Identity,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (System)
	{
		System->ConfigureDebugSlingshots(bSpawnDebugSlingshotsAtStart, SpawnCellId);
		UGameplayStatics::FinishSpawningActor(System, FTransform::Identity);
	}
	RuntimeSlingshotSystem = System;
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6] Entry ready=%d StartCell=%d"), System ? 1 : 0, SpawnCellId);
}
