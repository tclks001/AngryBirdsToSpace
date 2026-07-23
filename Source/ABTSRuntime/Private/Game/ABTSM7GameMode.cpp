// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM7GameMode.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Kismet/GameplayStatics.h"

AABTSM7GameMode::AABTSM7GameMode()
{
	BuildingMaterialSystemClass = AABTSM7BuildingMaterialSystem::StaticClass();
}

void AABTSM7GameMode::OnInitialPlayerPlaced(ACharacter& Character, const FTransform& SpawnTransform, const int32 SpawnCellId)
{
	Super::OnInitialPlayerPlaced(Character, SpawnTransform, SpawnCellId);
	AABTSM7BuildingMaterialSystem* System = GetWorld()->SpawnActorDeferred<AABTSM7BuildingMaterialSystem>(
		BuildingMaterialSystemClass, FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (System)
	{
		System->ConfigureTestSet(bSpawnBuildingMaterialTestSet, SpawnTransform);
		UGameplayStatics::FinishSpawningActor(System, FTransform::Identity);
	}
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M7] Entry ready=%d StartCell=%d TestSet=%d"), System ? 1 : 0, SpawnCellId, bSpawnBuildingMaterialTestSet ? 1 : 0);
}

