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
	FinalAnchorTaskType = EABTSM3TaskType::SatelliteWindow;
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
	if (FinalAnchorTaskType != EABTSM3TaskType::SatelliteWindow)
	{
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M11.0][SatellitePlacement] Ignoring legacy anchor override Type=%d; SatelliteWindow is authoritative."),
			static_cast<int32>(FinalAnchorTaskType));
	}
	const FABTSM3TaskNode* SatelliteTask = PrimaryPlanet->GetGeneratedTasks().FindByPredicate(
		[](const FABTSM3TaskNode& Task) { return Task.Type == EABTSM3TaskType::SatelliteWindow; });
	const FABTSM110FinaleLocalFrame& FinaleFrame = PrimaryPlanet->GetFinaleLaunchFrame();
	if (SatelliteTask == nullptr
		|| !PrimaryPlanet->LogicalCells.IsValidIndex(SatelliteTask->SeedCellId)
		|| !FinaleFrame.IsUsable())
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M9] Satellite rejected: SatelliteWindow or finale frame is invalid."));
		return;
	}
	const FVector SatelliteAnchorDirection =
		PrimaryPlanet->LogicalCells[SatelliteTask->SeedCellId].UnitCenter.GetSafeNormal();
	const float AngularSeparationDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
		FVector::DotProduct(FinaleFrame.GetUp(), SatelliteAnchorDirection),
		-1.0f,
		1.0f)));
	if (AngularSeparationDegrees + KINDA_SMALL_NUMBER
		< PrimaryPlanet->PCGConfig.MinSatelliteLaunchAngularSeparationDegrees)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M11.0][SatellitePlacement] Rejected: angular separation %.2f is below %.2f degrees."),
			AngularSeparationDegrees,
			PrimaryPlanet->PCGConfig.MinSatelliteLaunchAngularSeparationDegrees);
		return;
	}
	const float PrimaryRadiusCM = PrimaryPlanet->GetPlanetRadiusCM();
	const float SatelliteRadiusCM = PrimaryRadiusCM * FMath::Clamp(SatelliteRadiusPrimaryRatio, 0.02f, 0.5f);
	const float CenterClearanceCM = PrimaryRadiusCM * FMath::Clamp(SatelliteCenterClearancePrimaryRadiusRatio, 0.0f, 1.0f);
	const float SurfaceGravityCMPerSec2 = 980.0f * FMath::Max(0.0f, SatelliteSurfaceGravityPrimaryRatio);
	AABTSM9Satellite* Satellite = GetWorld()->SpawnActorDeferred<AABTSM9Satellite>(SatelliteClass, FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Satellite == nullptr) return;
	Satellite->ConfigureFromPrimaryPlanet(*PrimaryPlanet, SatelliteTask->SeedCellId, SatelliteRadiusCM, CenterClearanceCM, SurfaceGravityCMPerSec2);
	// ConfigureFromPrimaryPlanet moves the deferred native root. Finish with the
	// original spawn transform so UE does not compose the configured translation
	// a second time.
	UGameplayStatics::FinishSpawningActor(Satellite, FTransform::Identity);
	if (!Satellite->IsAtConfiguredCenter())
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M9] Satellite rejected: deferred finish changed center. Actual=%s Expected=%s"),
			*Satellite->GetActorLocation().ToCompactString(),
			*Satellite->GetConfiguredCenterWorld().ToCompactString());
		Satellite->Destroy();
		return;
	}
	const FVector FinaleToSatellite = Satellite->GetPlanetCenterWorld() - FinaleFrame.GetOrigin();
	const float FinaleDistanceRatio = FinaleToSatellite.Size() / FMath::Max(PrimaryRadiusCM, 1.0f);
	const FVector TangentDirection =
		FVector::VectorPlaneProject(FinaleToSatellite, FinaleFrame.GetUp()).GetSafeNormal();
	const float LateralAlignmentDot =
		FVector::DotProduct(TangentDirection, FinaleFrame.GetRight());
	const bool bPlacementContractSatisfied =
		FinaleDistanceRatio + KINDA_SMALL_NUMBER >= MinFinaleSatelliteDistancePrimaryRadiusRatio
		&& LateralAlignmentDot + KINDA_SMALL_NUMBER >= MinFinaleSatelliteLateralAlignmentDot;
	if (!bPlacementContractSatisfied)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M11.0][SatellitePlacement] Rejected after spawn: DistanceRatio=%.3f Required=%.3f LateralDot=%.4f Required=%.4f"),
			FinaleDistanceRatio,
			MinFinaleSatelliteDistancePrimaryRadiusRatio,
			LateralAlignmentDot,
			MinFinaleSatelliteLateralAlignmentDot);
		Satellite->Destroy();
		return;
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M9] Satellite ready Task=%d Cell=%d Radius=%.1f Clearance=%.1f Gravity=%.1f LogicalSub=%d RenderSub=%d AngularSepDeg=%.2f FinaleDistanceRatio=%.3f LateralDot=%.4f FinaleGravitySource=%d"),
		SatelliteTask->TaskId,
		SatelliteTask->SeedCellId,
		SatelliteRadiusCM,
		CenterClearanceCM,
		SurfaceGravityCMPerSec2,
		Satellite->LogicalSubdivision,
		Satellite->SurfaceSubdivision,
		AngularSeparationDegrees,
		FinaleDistanceRatio,
		LateralAlignmentDot,
		Satellite->IsM11FinaleGravitySource() ? 1 : 0);
}
