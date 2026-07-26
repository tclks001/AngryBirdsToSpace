// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM9GameMode.h"

#include "ABTSRuntime.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Terrain/ABTSM3Planet.h"
#include "World/ABTSM51WorldSystem.h"
#include "World/ABTSM9Satellite.h"

AABTSM9GameMode::AABTSM9GameMode()
{
	SatelliteClass = AABTSM9Satellite::StaticClass();
}

void AABTSM9GameMode::OnInitialPlayerPlaced(ACharacter& Character, const FTransform& SpawnTransform, const int32 SpawnCellId)
{
	Super::OnInitialPlayerPlaced(Character, SpawnTransform, SpawnCellId);
	if (GetWorld() == nullptr) return;
	int32 DebugBirdCount = 0;
	for (TActorIterator<AABTSM25BirdCharacter> It(GetWorld()); It; ++It)
	{
		It->SetDeveloperWalkEnabled(bEnableDeveloperWalk, DeveloperWalkSpeedMultiplier);
		++DebugBirdCount;
	}
	for (TActorIterator<AABTSM51WorldSystem> It(GetWorld()); It; ++It)
	{
		It->SetDeveloperAnyCellStakePlacementEnabled(bAllowDeveloperAnyCellSlingshotStakePlacement);
	}
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M9][Debug] DeveloperWalk=%d Birds=%d SpeedMultiplier=%.1f AnyCellStake=%d"),
		bEnableDeveloperWalk ? 1 : 0, DebugBirdCount, FMath::Clamp(DeveloperWalkSpeedMultiplier, 1.0f, 10.0f),
		bAllowDeveloperAnyCellSlingshotStakePlacement ? 1 : 0);
	if (!SatelliteClass) return;
	AABTSM3Planet* PrimaryPlanet = nullptr;
	for (TActorIterator<AABTSM3Planet> It(GetWorld()); It; ++It)
	{
		if (It->IsPlanetReady()) { PrimaryPlanet = *It; break; }
	}
	if (PrimaryPlanet == nullptr) return;
	const FABTSM3TaskNode* FinalTask = PrimaryPlanet->GetGeneratedTasks().FindByPredicate(
		[this](const FABTSM3TaskNode& Task) { return Task.Type == FinalAnchorTaskType; });
	if (FinalTask == nullptr || !PrimaryPlanet->LogicalCells.IsValidIndex(FinalTask->SeedCellId))
	{
		UE_LOG(LogABTSRuntime, Error, TEXT("[ABTS][M9] Satellite rejected: final Task type %d has no valid CellTopo seed."), static_cast<int32>(FinalAnchorTaskType));
		return;
	}
	const float PrimaryRadiusCM = PrimaryPlanet->GetPlanetRadiusCM();
	const float SatelliteRadiusCM = PrimaryRadiusCM * FMath::Clamp(SatelliteRadiusPrimaryRatio, 0.02f, 0.5f);
	const float CenterClearanceCM = PrimaryRadiusCM * FMath::Clamp(SatelliteCenterClearancePrimaryRadiusRatio, 0.0f, 1.0f);
	const float SurfaceGravityCMPerSec2 = 980.0f * FMath::Max(0.0f, SatelliteSurfaceGravityPrimaryRatio);
	AABTSM9Satellite* Satellite = GetWorld()->SpawnActorDeferred<AABTSM9Satellite>(SatelliteClass, FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Satellite == nullptr) return;
	Satellite->ConfigureFromPrimaryPlanet(*PrimaryPlanet, FinalTask->SeedCellId, SatelliteRadiusCM, CenterClearanceCM, SurfaceGravityCMPerSec2);
	UGameplayStatics::FinishSpawningActor(Satellite, Satellite->GetActorTransform());
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M9] Satellite ready Task=%d Cell=%d Radius=%.1f Clearance=%.1f Gravity=%.1f LogicalSub=%d RenderSub=%d"),
		FinalTask->TaskId, FinalTask->SeedCellId, SatelliteRadiusCM, CenterClearanceCM, SurfaceGravityCMPerSec2,
		Satellite->LogicalSubdivision, Satellite->SurfaceSubdivision);
}
