// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM7GameMode.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM73StableBuildingActor.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Terrain/ABTSM3Planet.h"

namespace
{
	void GetLatitudeLongitudeDegrees(const FVector& WorldLocation, const FVector& PlanetCenter, float& OutLatitudeDegrees, float& OutLongitudeDegrees)
	{
		const FVector Direction = (WorldLocation - PlanetCenter).GetSafeNormal();
		OutLatitudeDegrees = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Direction.Z, -1.0f, 1.0f)));
		OutLongitudeDegrees = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
	}
}

AABTSM7GameMode::AABTSM7GameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	BuildingMaterialSystemClass = AABTSM7BuildingMaterialSystem::StaticClass();
	StableBuildingClass = AABTSM73StableBuildingActor::StaticClass();
	const auto AddDefaultProfile = [this](
		const EABTSM3TaskType TaskType,
		const EABTSM7BuildingMaterial Material,
		const EABTSM73Silhouette Silhouette,
		const int32 Levels)
	{
		FABTSM7TaskGraphBuildingProfile& Profile = TaskGraphBuildingProfiles.AddDefaulted_GetRef();
		Profile.TaskType = TaskType;
		Profile.GenerationSettings.GenerationAlgorithm = EABTSM73GenerationAlgorithm::LegacyLayeredAB2;
		Profile.GenerationSettings.Silhouette = Silhouette;
		Profile.GenerationSettings.PrimaryMaterial = Material;
		Profile.GenerationSettings.Levels = Levels;
		Profile.GenerationSettings.MaxSinglePlatformAngularSpanDegrees = 7.0f;
		// M7 closure favors a known stable base construction. The DAG settings remain
		// exposed here so later profiles can opt in without changing the PCG bridge.
		Profile.DAGGenerationSettings.ExpansionStepBudget = 0;
		Profile.DAGGenerationSettings.MaxExpansionDepth = 1;
	};
	AddDefaultProfile(EABTSM3TaskType::Workshop, EABTSM7BuildingMaterial::Wood, EABTSM73Silhouette::SingleTower, 2);
	AddDefaultProfile(EABTSM3TaskType::TargetBuilding, EABTSM7BuildingMaterial::Stone, EABTSM73Silhouette::Gatehouse, 3);
	AddDefaultProfile(EABTSM3TaskType::FurnaceRuins, EABTSM7BuildingMaterial::Iron, EABTSM73Silhouette::SingleTower, 2);
	AddDefaultProfile(EABTSM3TaskType::LaunchSite, EABTSM7BuildingMaterial::Glass, EABTSM73Silhouette::TwinTowerBridge, 2);
}

void AABTSM7GameMode::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bShowTaskGraphPositionDebug)
	{
		DrawTaskGraphPositionDebug();
	}
}

const FABTSM7TaskGraphBuildingProfile* AABTSM7GameMode::FindTaskGraphBuildingProfile(const EABTSM3TaskType TaskType) const
{
	return TaskGraphBuildingProfiles.FindByPredicate([TaskType](const FABTSM7TaskGraphBuildingProfile& Profile)
	{
		return Profile.bSpawnBuilding && Profile.TaskType == TaskType;
	});
}

int32 AABTSM7GameMode::SpawnTaskGraphBuildings(AABTSM3Planet& Planet, AABTSM7BuildingMaterialSystem& MaterialSystem)
{
	if (!StableBuildingClass || MaxTaskGraphBuildings <= 0) return 0;
	TaskGraphBuildingDebugEntries.Reset();
	int32 SpawnedCount = 0;
	for (const FABTSM3BuildingSpawnSite& Site : Planet.GetBuildingSpawnSites())
	{
		if (SpawnedCount >= MaxTaskGraphBuildings) break;
		const FABTSM7TaskGraphBuildingProfile* Profile = FindTaskGraphBuildingProfile(Site.TaskType);
		if (Profile == nullptr) continue;

		const uint32 SeedHash = HashCombineFast(
			GetTypeHash(Planet.WorldSeed), HashCombineFast(GetTypeHash(Site.TaskId), GetTypeHash(Site.CellId)));
		FABTSM73GenerationSettings GenerationSettings = Profile->GenerationSettings;
		GenerationSettings.BuildingSeed = static_cast<int32>(SeedHash & MAX_int32);
		if (Planet.BuildingPadSettings.bEnableTerrainFlattening)
		{
			GenerationSettings.MaxSinglePlatformAngularSpanDegrees = FMath::Max(
				GenerationSettings.MaxSinglePlatformAngularSpanDegrees,
				MaxTaskGraphBuildingAngularSpanDegrees);
		}
		FABTSM73DAGGenerationSettings DAGGenerationSettings = Profile->DAGGenerationSettings;
		DAGGenerationSettings.BuildingSeed = GenerationSettings.BuildingSeed;

		AABTSM73StableBuildingActor* Building = GetWorld()->SpawnActorDeferred<AABTSM73StableBuildingActor>(
			StableBuildingClass, Site.WorldTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Building == nullptr)
		{
			UE_LOG(LogABTSRuntime, Error, TEXT("[ABTS][M7][TaskGraphBuilding] SpawnDeferredFailed Task=%d Cell=%d"), Site.TaskId, Site.CellId);
			continue;
		}
		Building->ConfigureTaskGraphGeneration(GenerationSettings, DAGGenerationSettings, Profile->DAGLayoutSettings, Profile->DifficultySettings);
		Building->ConfigureSphericalAnchor(&Planet, Site.CellId, Site.WorldTransform);
		UGameplayStatics::FinishSpawningActor(Building, Site.WorldTransform);
		Building->InitializeRuntimeBuilding(&MaterialSystem);
		FABTSM7TaskGraphBuildingDebugEntry& DebugEntry = TaskGraphBuildingDebugEntries.AddDefaulted_GetRef();
		DebugEntry.Building = Building;
		DebugEntry.TaskId = Site.TaskId;
		DebugEntry.TaskType = Site.TaskType;
		DebugEntry.CellId = Site.CellId;
		++SpawnedCount;
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7][TaskGraphBuilding] Task=%d Type=%d Cell=%d Material=%d Seed=%d Pad=%d Spawned=%s"),
			Site.TaskId, static_cast<int32>(Site.TaskType), Site.CellId,
			static_cast<int32>(GenerationSettings.PrimaryMaterial), GenerationSettings.BuildingSeed,
			Site.bTerrainPadApplied ? 1 : 0, *Building->GetName());
	}
	return SpawnedCount;
}

void AABTSM7GameMode::DrawTaskGraphPositionDebug()
{
	AABTSM3Planet* Planet = TaskGraphDebugPlanet.Get();
	ACharacter* Player = TaskGraphDebugPlayer.Get();
	if (Planet == nullptr || Player == nullptr || !Planet->IsPlanetReady() || GEngine == nullptr) return;

	const FVector PlanetCenter = Planet->GetPlanetCenterWorld();
	float PlayerLatitude = 0.0f;
	float PlayerLongitude = 0.0f;
	GetLatitudeLongitudeDegrees(Player->GetActorLocation(), PlanetCenter, PlayerLatitude, PlayerLongitude);
	GEngine->AddOnScreenDebugMessage(7100, 0.0f, FColor::Cyan,
		FString::Printf(TEXT("[TaskGraph 位置] 玩家  纬度 %+07.2f°  经度 %+08.2f°"), PlayerLatitude, PlayerLongitude),
		true, FVector2D(TaskGraphPositionDebugTextScale));

	int32 DebugIndex = 0;
	for (const FABTSM7TaskGraphBuildingDebugEntry& Entry : TaskGraphBuildingDebugEntries)
	{
		AABTSM73StableBuildingActor* Building = Entry.Building.Get();
		if (Building == nullptr) continue;
		float Latitude = 0.0f;
		float Longitude = 0.0f;
		GetLatitudeLongitudeDegrees(Building->GetActorLocation(), PlanetCenter, Latitude, Longitude);
		const FString Text = FString::Printf(
			TEXT("建筑%d  Task=%d Cell=%d  纬度 %+07.2f°  经度 %+08.2f°"),
			DebugIndex, Entry.TaskId, Entry.CellId, Latitude, Longitude);
		GEngine->AddOnScreenDebugMessage(7101 + DebugIndex, 0.0f, FColor::Yellow, Text, true, FVector2D(TaskGraphPositionDebugTextScale));
		if (bDrawTaskGraphBuildingWorldLabels)
		{
			const FVector RadialUp = (Building->GetActorLocation() - PlanetCenter).GetSafeNormal();
			DrawDebugString(GetWorld(), Building->GetActorLocation() + RadialUp * TaskGraphBuildingWorldLabelHeightCM,
				FString::Printf(TEXT("B%d  Task=%d\\nLat %+0.2f  Lon %+0.2f"), DebugIndex, Entry.TaskId, Latitude, Longitude),
				nullptr, FColor::Yellow, 0.0f, true, TaskGraphPositionDebugTextScale);
		}
		++DebugIndex;
	}
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
	AABTSM3Planet* Planet = nullptr;
	for (TActorIterator<AABTSM3Planet> It(GetWorld()); It; ++It) { if (It->IsPlanetReady()) { Planet = *It; break; } }
	TaskGraphDebugPlayer = &Character;
	TaskGraphDebugPlanet = Planet;
	const int32 TaskGraphBuildingCount = System && Planet && bSpawnTaskGraphBuildings
		? SpawnTaskGraphBuildings(*Planet, *System)
		: 0;

	AABTSM73StableBuildingActor* StableBuilding = nullptr;
	if (System && bSpawnStableBuildingAtFirstAnchor && StableBuildingClass && TaskGraphBuildingCount == 0)
	{
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
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M7] Entry ready=%d StartCell=%d TestSet=%d TaskGraphBuildings=%d LegacyM73A=%d"),
		System ? 1 : 0, SpawnCellId, bSpawnBuildingMaterialTestSet ? 1 : 0, TaskGraphBuildingCount, StableBuilding ? 1 : 0);
}
