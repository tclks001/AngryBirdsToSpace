// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM7GameMode.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM73StableBuildingActor.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Terrain/ABTSM3Planet.h"

AABTSM7GameMode::AABTSM7GameMode()
{
	BuildingMaterialSystemClass = AABTSM7BuildingMaterialSystem::StaticClass();
	StableBuildingClass = AABTSM73StableBuildingActor::StaticClass();
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
	AABTSM73StableBuildingActor* StableBuilding = nullptr;
	if (System && bSpawnStableBuildingAtFirstAnchor && StableBuildingClass)
	{
		AABTSM3Planet* Planet = nullptr;
		for (TActorIterator<AABTSM3Planet> It(GetWorld()); It; ++It) { if (It->IsPlanetReady()) { Planet = *It; break; } }
		if (Planet && !Planet->GetBuildingSpawnSites().IsEmpty())
		{
			const FABTSM3BuildingSpawnSite& Site = Planet->GetBuildingSpawnSites()[0];
			StableBuilding = GetWorld()->SpawnActorDeferred<AABTSM73StableBuildingActor>(
				StableBuildingClass, Site.WorldTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (StableBuilding)
			{
				StableBuilding->ConfigureSphericalAnchor(Planet, Site.CellId, Site.WorldTransform);
				UGameplayStatics::FinishSpawningActor(StableBuilding, Site.WorldTransform);
				StableBuilding->InitializeRuntimeBuilding(System);
			}
		}
	}
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M7] Entry ready=%d StartCell=%d TestSet=%d M73A=%d"),
		System ? 1 : 0, SpawnCellId, bSpawnBuildingMaterialTestSet ? 1 : 0, StableBuilding ? 1 : 0);
}
