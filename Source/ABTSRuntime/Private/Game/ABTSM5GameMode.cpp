// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM5GameMode.h"

#include "ABTSRuntime.h"
#include "Crafting/ABTSCraftingSystem.h"
#include "Player/ABTSM5PlayerController.h"
#include "UI/ABTSM5InventoryHUD.h"

AABTSM5GameMode::AABTSM5GameMode()
{
	PlayerControllerClass = AABTSM5PlayerController::StaticClass();
	HUDClass = AABTSM5InventoryHUD::StaticClass();
	CraftingSystemClass = AABTSCraftingSystem::StaticClass();
}

void AABTSM5GameMode::OnInitialPlayerPlaced(
	ACharacter& Character,
	const FTransform& SpawnTransform,
	const int32 SpawnCellId)
{
	Super::OnInitialPlayerPlaced(Character, SpawnTransform, SpawnCellId);
	if (GetWorld() == nullptr) return;
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSCraftingSystem* System = GetWorld()->SpawnActor<AABTSCraftingSystem>(CraftingSystemClass, FTransform::Identity, SpawnParameters);
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M5] Entry ready=%d StartCell=%d."), System ? 1 : 0, SpawnCellId);
}
