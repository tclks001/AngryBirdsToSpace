// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM10GameMode.h"

#include "ABTSRuntime.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "UI/ABTSM10ScoutMapHUD.h"
#include "UObject/ConstructorHelpers.h"
#include "World/ABTSM10ScoutMapSystem.h"

AABTSM10GameMode::AABTSM10GameMode()
{
	HUDClass = AABTSM10ScoutMapHUD::StaticClass();
	ScoutMapSystemClass = AABTSM10ScoutMapSystem::StaticClass();

	static ConstructorHelpers::FObjectFinder<UTexture2D> TreeIcon(
		TEXT("/Game/Icons/Decorations/T_Icon_Tree.T_Icon_Tree"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> StoneIcon(
		TEXT("/Game/Icons/Decorations/T_Icon_Stone.T_Icon_Stone"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> BuildingIcon(
		TEXT("/Game/Icons/Decorations/T_Icon_Building.T_Icon_Building"));
	if (TreeIcon.Succeeded()) ScoutMapSettings.TreeIconTexture = TreeIcon.Object;
	if (StoneIcon.Succeeded()) ScoutMapSettings.StoneIconTexture = StoneIcon.Object;
	if (BuildingIcon.Succeeded()) ScoutMapSettings.BuildingIconTexture = BuildingIcon.Object;
}

void AABTSM10GameMode::OnInitialPlayerPlaced(
	ACharacter& Character,
	const FTransform& SpawnTransform,
	const int32 SpawnCellId)
{
	Super::OnInitialPlayerPlaced(Character, SpawnTransform, SpawnCellId);
	if (GetWorld() == nullptr || !ScoutMapSystemClass) return;

	AABTSM10ScoutMapSystem* System = GetWorld()->SpawnActorDeferred<AABTSM10ScoutMapSystem>(
		ScoutMapSystemClass, FTransform::Identity, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (System)
	{
		System->Configure(ScoutMapSettings);
		UGameplayStatics::FinishSpawningActor(System, FTransform::Identity);
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M10] Entry ready=%d StartCell=%d RadiusRatio=%.3f OverrideCM=%.1f Icons(Tree=%d Stone=%d Building=%d)"),
		System ? 1 : 0, SpawnCellId, ScoutMapSettings.ScoutRadiusPrimaryRatio,
		ScoutMapSettings.ScoutRadiusOverrideCM,
		ScoutMapSettings.TreeIconTexture ? 1 : 0,
		ScoutMapSettings.StoneIconTexture ? 1 : 0,
		ScoutMapSettings.BuildingIconTexture ? 1 : 0);
}

