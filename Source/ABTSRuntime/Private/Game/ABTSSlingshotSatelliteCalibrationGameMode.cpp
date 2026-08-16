// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSSlingshotSatelliteCalibrationGameMode.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM73StableBuildingActor.h"
#include "Calibration/ABTSSlingshotSatelliteCalibrationRig.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "Terrain/ABTSM3Planet.h"
#include "TimerManager.h"
#include "UI/ABTSSlingshotSatelliteCalibrationHUD.h"
#include "World/ABTSM10ScoutMapSystem.h"
#include "World/ABTSM9Satellite.h"

AABTSSlingshotSatelliteCalibrationGameMode::
AABTSSlingshotSatelliteCalibrationGameMode()
{
	HUDClass = AABTSSlingshotSatelliteCalibrationHUD::StaticClass();
	SatelliteClass = AABTSM9Satellite::StaticClass();
	CalibrationRigClass = AABTSSlingshotSatelliteCalibrationRig::StaticClass();
	ScoutMapSystemClass = AABTSM10ScoutMapSystem::StaticClass();
	LaunchProfileCatalog =
		FABTSSlingshotSatelliteCalibrationModel::
			MakeFrozenLaunchProfileCatalogV0();
	PracticePreset =
		FABTSSlingshotSatelliteCalibrationModel::
			MakeFrozenSatellitePracticePresetV0();
	ScoutMapSettings.bShowOrbitalOverview = true;
	ScoutMapSettings.OrbitalDiagramMinPathLengthCM = 1000.0f;
	ScoutMapSettings.OrbitalDiagramPathLengthHysteresisCM = 250.0f;
}

void AABTSSlingshotSatelliteCalibrationGameMode::OnInitialPlayerPlaced(
	ACharacter& Character,
	const FTransform& SpawnTransform,
	const int32 SpawnCellId)
{
#if UE_BUILD_SHIPPING
	bCalibrationSmokeRequested = false;
	bSatelliteCameraCaptureRequested = false;
#else
	bCalibrationSmokeRequested =
		FParse::Param(FCommandLine::Get(), TEXT("ABTSCalibrationSmoke"));
	bSatelliteCameraCaptureRequested =
		FParse::Param(FCommandLine::Get(), TEXT("ABTSM9CameraCapture"));
#endif
	if (bCalibrationSmokeRequested)
	{
		CalibrationSmokeStartSeconds = FPlatformTime::Seconds();
	}
	const auto RejectEntry = [this](const TCHAR* Reason)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][Calibration][Entry] Rejected: %s"),
			Reason);
		if (bCalibrationSmokeRequested)
		{
			FinishCalibrationSmoke(false, Reason);
		}
	};
	Super::OnInitialPlayerPlaced(Character, SpawnTransform, SpawnCellId);
	AABTSM6SlingshotSystem* SlingshotSystem = GetRuntimeSlingshotSystem();
	AABTSM3Planet* PrimaryPlanet = nullptr;
	for (TActorIterator<AABTSM3Planet> It(GetWorld()); It; ++It)
	{
		if (It->IsPlanetReady())
		{
			PrimaryPlanet = *It;
			break;
		}
	}
	if (SlingshotSystem == nullptr
		|| PrimaryPlanet == nullptr
		|| !PrimaryPlanet->LogicalCells.IsValidIndex(SpawnCellId)
		|| !SatelliteClass
		|| !CalibrationRigClass)
	{
		RejectEntry(TEXT("dependencies are invalid."));
		return;
	}
	if (!SlingshotSystem->ConfigureCalibrationLaunchProfiles(
		LaunchProfileCatalog))
	{
		RejectEntry(TEXT("launch profile catalog is invalid."));
		return;
	}

	const FABTSSatellitePracticePreset ResolvedPreset = PracticePreset;
	const FVector StartDirection =
		PrimaryPlanet->LogicalCells[SpawnCellId].UnitCenter.GetSafeNormal();
	FVector LocalForward = FVector::VectorPlaneProject(
		SpawnTransform.GetRotation().GetForwardVector(),
		StartDirection).GetSafeNormal();
	if (LocalForward.IsNearlyZero())
	{
		const FVector Reference = FMath::Abs(StartDirection.Z) < 0.9f
			? FVector::UpVector
			: FVector::ForwardVector;
		LocalForward = FVector::CrossProduct(
			Reference, StartDirection).GetSafeNormal();
	}
	LocalForward = FQuat(
		StartDirection,
		FMath::DegreesToRadians(ResolvedPreset.SatelliteAnchorAzimuthDegrees))
		.RotateVector(LocalForward).GetSafeNormal();
	const float AnchorArcRadians =
		FMath::DegreesToRadians(ResolvedPreset.SatelliteAnchorArcDegrees);
	const FVector SatelliteAnchorDirection =
		(StartDirection * FMath::Cos(AnchorArcRadians)
			+ LocalForward * FMath::Sin(AnchorArcRadians)).GetSafeNormal();
	const float PrimaryRadiusCM = PrimaryPlanet->GetPlanetRadiusCM();
	const float SatelliteRadiusCM = PrimaryRadiusCM
		* FMath::Clamp(
			ResolvedPreset.SatelliteRadiusPrimaryRatio,
			0.02f,
			0.5f);
	const float CenterClearanceCM = PrimaryRadiusCM
		* FMath::Clamp(
			ResolvedPreset.SatelliteCenterClearancePrimaryRatio,
			0.0f,
			1.0f);
	const float SurfaceGravityCMPerSec2 = 980.0f
		* FMath::Max(
			0.0f,
			ResolvedPreset.SatelliteSurfaceGravityPrimaryRatio);
	AABTSM9Satellite* PracticeSatellite =
		GetWorld()->SpawnActorDeferred<AABTSM9Satellite>(
			SatelliteClass,
			FTransform::Identity,
			this,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (PracticeSatellite == nullptr
		|| !PracticeSatellite->ConfigureFromPrimaryDirection(
			*PrimaryPlanet,
			SatelliteAnchorDirection,
			SatelliteRadiusCM,
			CenterClearanceCM,
			SurfaceGravityCMPerSec2))
	{
		if (PracticeSatellite) PracticeSatellite->Destroy();
		RejectEntry(TEXT("practice satellite configuration failed."));
		return;
	}
	PracticeSatellite->bGravityEnabled = true;
	// ConfigureFromPrimaryDirection moves the deferred native root away from the
	// original identity spawn transform. Passing the already-moved actor
	// transform here would make FinishSpawning compose that translation twice.
	UGameplayStatics::FinishSpawningActor(
		PracticeSatellite,
		FTransform::Identity);
	if (!PracticeSatellite->IsAtConfiguredCenter())
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][Calibration][Entry] Practice satellite transform changed during deferred finish. Actual=%s Expected=%s"),
			*PracticeSatellite->GetActorLocation().ToCompactString(),
			*PracticeSatellite->GetConfiguredCenterWorld().ToCompactString());
		PracticeSatellite->Destroy();
		if (bCalibrationSmokeRequested)
		{
			FinishCalibrationSmoke(
				false,
				TEXT("practice satellite deferred transform changed."));
		}
		return;
	}
	const int32 SlingshotCount =
		SlingshotSystem->SpawnCalibrationSlingshots(
			SpawnCellId,
			PracticeSatellite->GetPlanetCenterWorld());
	RuntimeCalibrationSlingshotCount = SlingshotCount;

	AABTSSlingshotSatelliteCalibrationRig* Rig =
		GetWorld()->SpawnActorDeferred<AABTSSlingshotSatelliteCalibrationRig>(
			CalibrationRigClass,
			FTransform::Identity,
			this,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Rig)
	{
		Rig->Configure(
			*PrimaryPlanet,
			*PracticeSatellite,
			*SlingshotSystem,
			SpawnTransform.GetLocation(),
			StartDirection,
			LocalForward,
			ResolvedPreset);
		UGameplayStatics::FinishSpawningActor(Rig, FTransform::Identity);
	}
	RuntimeCalibrationRig = Rig;

	AABTSM10ScoutMapSystem* ScoutMapSystem = nullptr;
	if (ScoutMapSystemClass)
	{
		ScoutMapSystem =
			GetWorld()->SpawnActorDeferred<AABTSM10ScoutMapSystem>(
				ScoutMapSystemClass,
				FTransform::Identity,
				this,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (ScoutMapSystem)
		{
			ScoutMapSystem->Configure(ScoutMapSettings);
			UGameplayStatics::FinishSpawningActor(
				ScoutMapSystem,
				FTransform::Identity);
			ScoutMapSystem->RevealForSlingshotCalibration(
				SpawnTransform.GetLocation());
		}
	}
	RuntimeCalibrationScoutMapSystem = ScoutMapSystem;

	UE_LOG(LogABTSRuntime,
		Log,
		TEXT("[ABTS][Calibration][Entry] Rig=%d Slingshots=%d SatelliteRadius=%.1f ArcDeg=%.2f Clearance=%.1f Gravity=%.1f ScoutMap=%d"),
		Rig ? 1 : 0,
		SlingshotCount,
		SatelliteRadiusCM,
		ResolvedPreset.SatelliteAnchorArcDegrees,
		CenterClearanceCM,
		SurfaceGravityCMPerSec2,
		ScoutMapSystem ? 1 : 0);

	if (bCalibrationSmokeRequested)
	{
		if (SlingshotCount != 3
			|| Rig == nullptr
			|| ScoutMapSystem == nullptr
			|| !ScoutMapSystem->IsScoutMapRevealed())
		{
			FinishCalibrationSmoke(
				false,
				FString::Printf(
					TEXT("Entry incomplete: Slingshots=%d Rig=%d ScoutMap=%d Revealed=%d"),
					SlingshotCount,
					Rig ? 1 : 0,
					ScoutMapSystem ? 1 : 0,
					ScoutMapSystem && ScoutMapSystem->IsScoutMapRevealed()
						? 1
						: 0));
			return;
		}
		GetWorldTimerManager().SetTimerForNextTick(
			this,
			&AABTSSlingshotSatelliteCalibrationGameMode::
				TryCompleteCalibrationSmoke);
	}
	if (bSatelliteCameraCaptureRequested)
	{
		GetWorldTimerManager().SetTimerForNextTick(
			this,
			&AABTSSlingshotSatelliteCalibrationGameMode::
				TryStartSatelliteCameraCapture);
	}
}

void AABTSSlingshotSatelliteCalibrationGameMode::
TryStartSatelliteCameraCapture()
{
	AABTSSlingshotSatelliteCalibrationRig* Rig = RuntimeCalibrationRig.Get();
	if (Rig && Rig->IsReady())
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M9][CameraCapture] CalibrationRigReady=1"));
		return;
	}
	FTimerHandle RetryHandle;
	GetWorldTimerManager().SetTimer(
		RetryHandle,
		this,
		&AABTSSlingshotSatelliteCalibrationGameMode::
			TryStartSatelliteCameraCapture,
		0.1f,
		false);
}

void AABTSSlingshotSatelliteCalibrationGameMode::
TryCompleteCalibrationSmoke()
{
	if (!bCalibrationSmokeRequested || bCalibrationSmokeFinished) return;
	const double ElapsedSeconds =
		FPlatformTime::Seconds() - CalibrationSmokeStartSeconds;
	AABTSSlingshotSatelliteCalibrationRig* Rig =
		RuntimeCalibrationRig.Get();
	if (Rig && Rig->IsReady())
	{
		int32 BuildingCount = 0;
		for (TActorIterator<AABTSM73StableBuildingActor> It(GetWorld()); It; ++It)
		{
			++BuildingCount;
		}
		const bool bPassed =
			RuntimeCalibrationSlingshotCount == 3
			&& Rig->GetTargetProxyCount() == 7
			&& Rig->GetReachEnvelopes().Num() == 3
			&& Rig->GetSweepSummary().bPassed
			&& Rig->GetSweepSummary().SimpleFullPowerHits == 0
			&& Rig->GetSweepSummary().ReinforcedOutsideCertifiedPullHits == 0
			&& Rig->GetLaunchProfileHash() != 0
			&& Rig->GetGravitySnapshotHash() != 0
			&& Rig->GetSatellitePracticePresetHash() != 0
			&& Rig->IsSatelliteGravityEnabled()
			&& RuntimeCalibrationScoutMapSystem.IsValid()
			&& RuntimeCalibrationScoutMapSystem->IsScoutMapRevealed()
			&& BuildingCount == 0;
		FinishCalibrationSmoke(
			bPassed,
			FString::Printf(
				TEXT("Ready=1 Slingshots=%d Targets=%d Envelopes=%d Sweep=%d SimpleHits=%d OutsidePullHits=%d Gravity=%d ScoutMap=%d Buildings=%d"),
				RuntimeCalibrationSlingshotCount,
				Rig->GetTargetProxyCount(),
				Rig->GetReachEnvelopes().Num(),
				Rig->GetSweepSummary().bPassed ? 1 : 0,
				Rig->GetSweepSummary().SimpleFullPowerHits,
				Rig->GetSweepSummary().ReinforcedOutsideCertifiedPullHits,
				Rig->IsSatelliteGravityEnabled() ? 1 : 0,
				RuntimeCalibrationScoutMapSystem.IsValid()
					&& RuntimeCalibrationScoutMapSystem->IsScoutMapRevealed()
						? 1
						: 0,
				BuildingCount));
		return;
	}
	if (ElapsedSeconds >= 30.0)
	{
		FinishCalibrationSmoke(false, TEXT("Timed out waiting for calibration rig."));
		return;
	}
	FTimerHandle RetryHandle;
	GetWorldTimerManager().SetTimer(
		RetryHandle,
		this,
		&AABTSSlingshotSatelliteCalibrationGameMode::
			TryCompleteCalibrationSmoke,
		0.25f,
		false);
}

void AABTSSlingshotSatelliteCalibrationGameMode::FinishCalibrationSmoke(
	const bool bPassed,
	const FString& Reason)
{
	if (bCalibrationSmokeFinished) return;
	bCalibrationSmokeFinished = true;
	if (bPassed)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][Calibration][RuntimeCertification] Terminal=1 Passed=1 Failed=0 Reason=%s"),
			*Reason);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][Calibration][RuntimeCertification] Terminal=1 Passed=0 Failed=1 Reason=%s"),
			*Reason);
	}
	FPlatformMisc::RequestExitWithStatus(
		false,
		bPassed ? 0 : 1,
		TEXT("AABTSSlingshotSatelliteCalibrationGameMode::FinishCalibrationSmoke"));
}
