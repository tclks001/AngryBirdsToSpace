// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM3GameMode.h"

#include "ABTSRuntime.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/ABTSM1PlayerController.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "PCG/ABTSM3R0AcceptanceManifest.h"
#include "Terrain/ABTSM3Planet.h"
#include "TimerManager.h"
#include "UI/ABTSM1HUD.h"

AABTSM3GameMode::AABTSM3GameMode()
{
	DefaultPawnClass = AABTSM25BirdCharacter::StaticClass();
	PlayerControllerClass = AABTSM1PlayerController::StaticClass();
	HUDClass = AABTSM1HUD::StaticClass();
}

void AABTSM3GameMode::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M3] TaskGraph terrain presentation entry ready."));
	FString ManifestFailure;
	if (FABTSM3R0AcceptanceManifest::Validate(ManifestFailure))
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M3R0][AcceptanceManifest] SelfValid=1 Schema=%d CompatibilityOracle=Gen3/Policy1 ManifestHash=%016llX SeedManifestHash=%016llX M110SeedManifestHash=%016llX Entries=%d ExpectedWeekOneCases=2 ExpectedContractCases=2 ExpectedM110Cases=1 ExpectedM110Seeds=103 ExpectedRuntimeCases=1 ExpectedVisiblePIECases=1 DisplaySeed=%d"),
			FABTSM3R0AcceptanceManifest::SchemaVersion,
			static_cast<unsigned long long>(
				FABTSM3R0AcceptanceManifest::ComputeManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R0AcceptanceManifest::ComputeWeekOneSeedManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R0AcceptanceManifest::ComputeM110SeedManifestHash()),
			FABTSM3R0AcceptanceManifest::GetEntries().Num(),
			FABTSM3R0AcceptanceManifest::DisplaySeed);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R0][AcceptanceManifest] SelfValid=0 Failure=%s ComputedManifestHash=%016llX ComputedSeedManifestHash=%016llX ComputedM110SeedManifestHash=%016llX"),
			*ManifestFailure,
			static_cast<unsigned long long>(
				FABTSM3R0AcceptanceManifest::ComputeManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R0AcceptanceManifest::ComputeWeekOneSeedManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R0AcceptanceManifest::ComputeM110SeedManifestHash()));
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("ABTSM3R0Smoke")))
	{
		M3R0SmokeStartSeconds = FPlatformTime::Seconds();
		GetWorldTimerManager().SetTimer(
			M3R0SmokeTimer,
			this,
			&AABTSM3GameMode::TryCompleteM3R0Smoke,
			0.25f,
			true,
			0.25f);
	}
	TryPlacePlayerAtInitialRoad();
}

void AABTSM3GameMode::TryPlacePlayerAtInitialRoad()
{
	constexpr int32 MaxAttempts = 30;
	constexpr float RetryIntervalSeconds = 0.1f;
	++InitialRoadSpawnAttempts;

	AABTSM3Planet* Planet = nullptr;
	for (TActorIterator<AABTSM3Planet> It(GetWorld()); It; ++It)
	{
		if (It->IsM3PresentationReady())
		{
			Planet = *It;
			break;
		}
	}

	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	ACharacter* Character = PlayerController ? Cast<ACharacter>(PlayerController->GetPawn()) : nullptr;
	if (Planet != nullptr && Character != nullptr)
	{
		const float CapsuleHalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		FTransform SpawnTransform;
		int32 SpawnCellId = INDEX_NONE;
		if (Planet->GetInitialRoadSpawnTransform(CapsuleHalfHeight, SpawnTransform, SpawnCellId))
		{
			if (AABTSM25BirdCharacter* BirdCharacter = Cast<AABTSM25BirdCharacter>(Character))
			{
				BirdCharacter->ResetRadialMovementState();
			}
			Character->SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
			PlayerController->SetControlRotation(SpawnTransform.Rotator());
			bInitialPlayerPlaced = true;
			OnInitialPlayerPlaced(*Character, SpawnTransform, SpawnCellId);
			GetWorldTimerManager().ClearTimer(InitialRoadSpawnTimer);
			UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M3][Spawn] Player placed at Start road. Cell=%d Location=(%.1f,%.1f,%.1f) Attempts=%d"),
				SpawnCellId,
				SpawnTransform.GetLocation().X,
				SpawnTransform.GetLocation().Y,
				SpawnTransform.GetLocation().Z,
				InitialRoadSpawnAttempts);
			return;
		}
	}

	if (InitialRoadSpawnAttempts < MaxAttempts)
	{
		GetWorldTimerManager().SetTimer(InitialRoadSpawnTimer, this, &AABTSM3GameMode::TryPlacePlayerAtInitialRoad, RetryIntervalSeconds, false);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error, TEXT("[ABTS][M3][Spawn] Failed after %d attempts. PlanetReady=%d PawnReady=%d"),
			InitialRoadSpawnAttempts,
			Planet ? 1 : 0,
			Character ? 1 : 0);
	}
}

void AABTSM3GameMode::TryCompleteM3R0Smoke()
{
	constexpr double MaxWaitSeconds = 20.0;
	const double ElapsedSeconds =
		FPlatformTime::Seconds() - M3R0SmokeStartSeconds;

	AABTSM3Planet* Planet = nullptr;
	for (TActorIterator<AABTSM3Planet> It(GetWorld()); It; ++It)
	{
		if (It->IsM3PresentationReady())
		{
			Planet = *It;
			break;
		}
	}
	if (ElapsedSeconds > MaxWaitSeconds)
	{
		FinishM3R0Smoke(false, TEXT("CertificationExceeded20Seconds"));
		return;
	}
	if ((Planet == nullptr || !bInitialPlayerPlaced)
		&& ElapsedSeconds < MaxWaitSeconds)
	{
		return;
	}
	if (Planet == nullptr)
	{
		FinishM3R0Smoke(false, TEXT("PlanetNotReadyWithin20Seconds"));
		return;
	}
	if (!bInitialPlayerPlaced)
	{
		FinishM3R0Smoke(false, TEXT("InitialPlayerNotPlacedWithin20Seconds"));
		return;
	}

	FString ManifestFailure;
	if (!FABTSM3R0AcceptanceManifest::Validate(ManifestFailure))
	{
		FinishM3R0Smoke(
			false,
			FString::Printf(TEXT("Manifest:%s"), *ManifestFailure));
		return;
	}

	const FABTSM3PCGSummary& Summary = Planet->PCGSummary;
	if (Planet->WorldSeed != FABTSM3R0AcceptanceManifest::DisplaySeed
		|| !Summary.bAccepted
		|| Summary.GeneratorVersion != FABTSM3R0AcceptanceManifest::GeneratorVersion
		|| Summary.LayoutPolicyVersion != FABTSM3R0AcceptanceManifest::LayoutPolicyVersion
		|| Summary.ConfigHash != FABTSM3R0AcceptanceManifest::DisplayConfigHash
		|| Summary.LayoutHash != FABTSM3R0AcceptanceManifest::DisplayLayoutHash)
	{
		FinishM3R0Smoke(false, TEXT("CompatibilityOracleIdentityMismatch"));
		return;
	}
	const uint8 VisibilityMask = FABTSM3R0AcceptanceManifest::PackVisibility(
		Summary.bWorkshopVisibleAtDefaultOrbit,
		Summary.bWorkshopVisibleAtMaxOrbit,
		Summary.bTargetBuildingVisibleAtDefaultOrbit,
		Summary.bTargetBuildingVisibleAtMaxOrbit,
		Summary.bFurnaceVisibleAtDefaultOrbit,
		Summary.bFurnaceVisibleAtMaxOrbit);
	if (!FMath::IsNearlyEqual(
			Summary.MainRouteLengthCM,
			FABTSM3R0AcceptanceManifest::DisplayMainRouteLengthCM,
			1.0f)
		|| !FMath::IsNearlyEqual(
			Summary.MinAdjacentBuildingProgressCM,
			FABTSM3R0AcceptanceManifest::DisplayBuildingGapCM,
			1.0f)
		|| !FMath::IsNearlyEqual(
			Summary.SatelliteLaunchAngularSeparationDegrees,
			FABTSM3R0AcceptanceManifest::DisplaySatelliteLaunchSeparationDegrees,
			0.01f)
		|| VisibilityMask != FABTSM3R0AcceptanceManifest::DisplayVisibilityMask)
	{
		FinishM3R0Smoke(false, TEXT("CompatibilityOracleMetricsMismatch"));
		return;
	}

	const TArray<FABTSM3BuildingSpawnSite>& ActualSites =
		Planet->GetBuildingSpawnSites();
	const TConstArrayView<FABTSM3R0ExpectedBuildingSite> ExpectedSites =
		FABTSM3R0AcceptanceManifest::GetDisplayBuildingSites();
	if (ActualSites.Num() != ExpectedSites.Num())
	{
		FinishM3R0Smoke(false, TEXT("BuildingSiteCountMismatch"));
		return;
	}
	for (int32 SiteIndex = 0; SiteIndex < ExpectedSites.Num(); ++SiteIndex)
	{
		if (ActualSites[SiteIndex].TaskId != ExpectedSites[SiteIndex].TaskId
			|| ActualSites[SiteIndex].CellId != ExpectedSites[SiteIndex].CellId)
		{
			FinishM3R0Smoke(
				false,
				FString::Printf(TEXT("BuildingSiteMismatch:%d"), SiteIndex));
			return;
		}
	}

	const FABTSM110FinaleLocalFrame& FinaleFrame =
		Planet->GetFinaleLaunchFrame();
	if (!FinaleFrame.IsUsable()
		|| FinaleFrame.LaunchTaskId != ExpectedSites[0].TaskId
		|| FinaleFrame.AnchorCellId != ExpectedSites[0].CellId)
	{
		FinishM3R0Smoke(false, TEXT("FinaleFrameMismatch"));
		return;
	}

	FinishM3R0Smoke(true, FString());
}

void AABTSM3GameMode::FinishM3R0Smoke(
	const bool bPassed,
	const FString& Failure)
{
	GetWorldTimerManager().ClearTimer(M3R0SmokeTimer);
	const double ElapsedSeconds = FMath::Max(
		0.0,
		FPlatformTime::Seconds() - M3R0SmokeStartSeconds);
	if (bPassed)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M3R0][RuntimeCertification] ManifestHash=%016llX Terminal=1 Passed=1 Failed=0 ElapsedSeconds=%.3f"),
			static_cast<unsigned long long>(
				FABTSM3R0AcceptanceManifest::ComputeManifestHash()),
			ElapsedSeconds);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R0][RuntimeCertification] ManifestHash=%016llX Terminal=1 Passed=0 Failed=1 Failure=%s ElapsedSeconds=%.3f"),
			static_cast<unsigned long long>(
				FABTSM3R0AcceptanceManifest::ComputeManifestHash()),
			*Failure,
			ElapsedSeconds);
	}
	FPlatformMisc::RequestExitWithStatus(
		false,
		bPassed ? 0 : 1,
		TEXT("AABTSM3GameMode::FinishM3R0Smoke"));
}

void AABTSM3GameMode::OnInitialPlayerPlaced(ACharacter& Character, const FTransform& SpawnTransform, const int32 SpawnCellId)
{
}
