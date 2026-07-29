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
#include "PCG/ABTSM3R1AcceptanceManifest.h"
#include "PCG/ABTSM3R2AcceptanceManifest.h"
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
	ManifestFailure.Reset();
	if (FABTSM3R1AcceptanceManifest::Validate(ManifestFailure))
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M3R1][AcceptanceManifest] SelfValid=1 Schema=%d CompatibilityOracle=Gen3/Policy1 MonthlyPolicy=%d ManifestHash=%016llX SeedManifestHash=%016llX OracleHash=%016llX Entries=%d ExpectedSchemaCases=8 ExpectedWeekOneCases=2 ExpectedContractCases=2 ExpectedM110Cases=1 ExpectedRuntimeCases=1 DisplaySeed=%d"),
			FABTSM3R1AcceptanceManifest::MonthlySchemaVersion,
			FABTSM3R1AcceptanceManifest::MonthlyLayoutPolicyVersion,
			static_cast<unsigned long long>(
				FABTSM3R1AcceptanceManifest::ComputeManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R1AcceptanceManifest::
					ComputeCompatibilitySeedManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R1AcceptanceManifest::
					ComputeCompatibilityOracleHash()),
			FABTSM3R1AcceptanceManifest::GetEntries().Num(),
			FABTSM3R1AcceptanceManifest::DisplaySeed);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R1][AcceptanceManifest] SelfValid=0 Failure=%s ComputedManifestHash=%016llX ComputedSeedManifestHash=%016llX ComputedOracleHash=%016llX"),
			*ManifestFailure,
			static_cast<unsigned long long>(
				FABTSM3R1AcceptanceManifest::ComputeManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R1AcceptanceManifest::
					ComputeCompatibilitySeedManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R1AcceptanceManifest::
					ComputeCompatibilityOracleHash()));
	}
	ManifestFailure.Reset();
	if (FABTSM3R2AcceptanceManifest::Validate(ManifestFailure))
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M3R2][AcceptanceManifest] SelfValid=1 RoutePoolSchema=%d CompatibilityOracle=Gen3/Policy1 MonthlyPolicy=%d ManifestHash=%016llX SeedManifestHash=%016llX ProfileHash=%016llX RouteOracleHash=%016llX Entries=%d ExpectedRouteCoreCases=7 ExpectedRouteFailureCases=1 ExpectedRuntimeCases=1 DisplaySeed=%d"),
			FABTSM3R2AcceptanceManifest::RoutePoolSchemaVersion,
			FABTSM3R2AcceptanceManifest::
				MonthlyLayoutPolicyVersion,
			static_cast<unsigned long long>(
				FABTSM3R2AcceptanceManifest::
					ComputeManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R2AcceptanceManifest::
					ComputeSweepSeedManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R2AcceptanceManifest::
					ComputeAcceptanceProfileHash()),
			static_cast<unsigned long long>(
				FABTSM3R2AcceptanceManifest::
					FrozenRouteOracleHash),
			FABTSM3R2AcceptanceManifest::GetEntries().Num(),
			FABTSM3R2AcceptanceManifest::DisplaySeed);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R2][AcceptanceManifest] SelfValid=0 Failure=%s ComputedManifestHash=%016llX ComputedSeedManifestHash=%016llX ComputedProfileHash=%016llX"),
			*ManifestFailure,
			static_cast<unsigned long long>(
				FABTSM3R2AcceptanceManifest::
					ComputeManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R2AcceptanceManifest::
					ComputeSweepSeedManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R2AcceptanceManifest::
					ComputeAcceptanceProfileHash()));
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
	if (FParse::Param(FCommandLine::Get(), TEXT("ABTSM3R1Smoke")))
	{
		M3R1SmokeStartSeconds = FPlatformTime::Seconds();
		GetWorldTimerManager().SetTimer(
			M3R1SmokeTimer,
			this,
			&AABTSM3GameMode::TryCompleteM3R1Smoke,
			0.25f,
			true,
			0.25f);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("ABTSM3R2Smoke")))
	{
		M3R2SmokeStartSeconds = FPlatformTime::Seconds();
		GetWorldTimerManager().SetTimer(
			M3R2SmokeTimer,
			this,
			&AABTSM3GameMode::TryCompleteM3R2Smoke,
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

void AABTSM3GameMode::TryCompleteM3R1Smoke()
{
	constexpr double MaxWaitSeconds = 20.0;
	const double ElapsedSeconds =
		FPlatformTime::Seconds() - M3R1SmokeStartSeconds;

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
		FinishM3R1Smoke(false, TEXT("CertificationExceeded20Seconds"));
		return;
	}
	if ((Planet == nullptr || !bInitialPlayerPlaced)
		&& ElapsedSeconds < MaxWaitSeconds)
	{
		return;
	}
	if (Planet == nullptr)
	{
		FinishM3R1Smoke(false, TEXT("PlanetNotReadyWithin20Seconds"));
		return;
	}
	if (!bInitialPlayerPlaced)
	{
		FinishM3R1Smoke(
			false,
			TEXT("InitialPlayerNotPlacedWithin20Seconds"));
		return;
	}

	FString ManifestFailure;
	if (!FABTSM3R1AcceptanceManifest::Validate(ManifestFailure))
	{
		FinishM3R1Smoke(
			false,
			FString::Printf(TEXT("Manifest:%s"), *ManifestFailure));
		return;
	}

	const FABTSM3PCGSummary& Summary = Planet->PCGSummary;
	const TConstArrayView<FABTSM3R1CompatibilityOracle> Oracles =
		FABTSM3R1AcceptanceManifest::GetCompatibilityOracles();
	const FABTSM3R1CompatibilityOracle& DisplayOracle = Oracles[0];
	if (Planet->WorldSeed != DisplayOracle.Seed
		|| !Summary.bAccepted
		|| Summary.GeneratorVersion
			!= FABTSM3R1AcceptanceManifest::GeneratorVersion
		|| Summary.LayoutPolicyVersion
			!= FABTSM3R1AcceptanceManifest::
				CompatibilityLayoutPolicyVersion
		|| Summary.ConfigHash != DisplayOracle.ConfigHash
		|| Summary.LayoutHash != DisplayOracle.LayoutHash
		|| Summary.AttemptIndex != DisplayOracle.AttemptIndex)
	{
		FinishM3R1Smoke(
			false,
			TEXT("CompatibilityOracleIdentityMismatch"));
		return;
	}

	const FABTSM3MonthlyWorldSchema& Schema =
		Planet->GetMonthlyWorldSchema();
	EABTSM3SchemaRejectReason ValidationReason =
		EABTSM3SchemaRejectReason::None;
	FString SchemaFailure;
	if (!FABTSM3MonthlySchemaBuilder::Validate(
			Schema,
			ValidationReason,
			SchemaFailure))
	{
		FinishM3R1Smoke(
			false,
			FString::Printf(
				TEXT("Schema:%s:%s"),
				FABTSM3MonthlySchemaBuilder::GetRejectReasonName(
					ValidationReason),
				*SchemaFailure));
		return;
	}
	if (Schema.Identity.Mode
			!= EABTSM3GenerationMode::CompatibilityOracle
		|| Schema.Identity.LayoutPolicyVersion
			!= FABTSM3R1AcceptanceManifest::
				CompatibilityLayoutPolicyVersion
		|| static_cast<uint64>(Schema.Identity.SchemaConfigHash)
			!= FABTSM3R1AcceptanceManifest::
				FrozenDisplaySchemaConfigHash
		|| static_cast<uint64>(Schema.Identity.SchemaLayoutHash)
			!= FABTSM3R1AcceptanceManifest::
				FrozenDisplaySchemaLayoutHash
		|| !Schema.Quality.bSchemaValid
		|| Schema.Quality.bMonthlyWorldAccepted
		|| Schema.Quality.RejectReason
			!= EABTSM3SchemaRejectReason::NotEvaluated)
	{
		FinishM3R1Smoke(false, TEXT("SchemaIdentityMismatch"));
		return;
	}
	if (Schema.RouteBeats.Num()
			!= FABTSM3R1AcceptanceManifest::DisplayRouteBeatCount
		|| Schema.Encounters.Num()
			!= FABTSM3R1AcceptanceManifest::DisplayEncounterCount
		|| Schema.Pockets.Num()
			!= FABTSM3R1AcceptanceManifest::DisplayPocketCount
		|| Schema.BiomeDistricts.Num()
			!= FABTSM3R1AcceptanceManifest::DisplayBiomeDistrictCount
		|| Schema.PlayableEnvelopes.Num()
			!= FABTSM3R1AcceptanceManifest::
				DisplayPlayableEnvelopeCount)
	{
		FinishM3R1Smoke(false, TEXT("SchemaLayerCountMismatch"));
		return;
	}

	const TArray<FABTSM3BuildingSpawnSite>& Sites =
		Planet->GetBuildingSpawnSites();
	const TConstArrayView<FABTSM3R0ExpectedBuildingSite> ExpectedSites =
		FABTSM3R0AcceptanceManifest::GetDisplayBuildingSites();
	if (Sites.Num() != ExpectedSites.Num())
	{
		FinishM3R1Smoke(false, TEXT("BuildingSiteCountMismatch"));
		return;
	}
	for (int32 SiteIndex = 0; SiteIndex < ExpectedSites.Num(); ++SiteIndex)
	{
		if (Sites[SiteIndex].TaskId != ExpectedSites[SiteIndex].TaskId
			|| Sites[SiteIndex].CellId != ExpectedSites[SiteIndex].CellId)
		{
			FinishM3R1Smoke(
				false,
				FString::Printf(
					TEXT("BuildingSiteMismatch:%d"),
					SiteIndex));
			return;
		}
	}
	FinishM3R1Smoke(true, FString());
}

void AABTSM3GameMode::FinishM3R1Smoke(
	const bool bPassed,
	const FString& Failure)
{
	GetWorldTimerManager().ClearTimer(M3R1SmokeTimer);
	const double ElapsedSeconds = FMath::Max(
		0.0,
		FPlatformTime::Seconds() - M3R1SmokeStartSeconds);
	if (bPassed)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M3R1][RuntimeCertification] ManifestHash=%016llX Terminal=1 Passed=1 Failed=0 ElapsedSeconds=%.3f"),
			static_cast<unsigned long long>(
				FABTSM3R1AcceptanceManifest::ComputeManifestHash()),
			ElapsedSeconds);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R1][RuntimeCertification] ManifestHash=%016llX Terminal=1 Passed=0 Failed=1 Failure=%s ElapsedSeconds=%.3f"),
			static_cast<unsigned long long>(
				FABTSM3R1AcceptanceManifest::ComputeManifestHash()),
			*Failure,
			ElapsedSeconds);
	}
	FPlatformMisc::RequestExitWithStatus(
		false,
		bPassed ? 0 : 1,
		TEXT("AABTSM3GameMode::FinishM3R1Smoke"));
}

void AABTSM3GameMode::TryCompleteM3R2Smoke()
{
	constexpr double MaxWaitSeconds = 20.0;
	const double ElapsedSeconds =
		FPlatformTime::Seconds() - M3R2SmokeStartSeconds;

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
		FinishM3R2Smoke(
			false,
			TEXT("CertificationExceeded20Seconds"));
		return;
	}
	if ((Planet == nullptr || !bInitialPlayerPlaced)
		&& ElapsedSeconds < MaxWaitSeconds)
	{
		return;
	}
	if (Planet == nullptr)
	{
		FinishM3R2Smoke(
			false,
			TEXT("PlanetNotReadyWithin20Seconds"));
		return;
	}
	if (!bInitialPlayerPlaced)
	{
		FinishM3R2Smoke(
			false,
			TEXT("InitialPlayerNotPlacedWithin20Seconds"));
		return;
	}

	FString Failure;
	if (!FABTSM3R2AcceptanceManifest::Validate(Failure))
	{
		FinishM3R2Smoke(
			false,
			FString::Printf(TEXT("Manifest:%s"), *Failure));
		return;
	}

	const FABTSM3PCGSummary& Summary = Planet->PCGSummary;
	const FABTSM3R1CompatibilityOracle& DisplayOracle =
		FABTSM3R1AcceptanceManifest::
			GetCompatibilityOracles()[0];
	const uint64 CompatibilitySnapshot =
		FABTSM3R1AcceptanceManifest::
			ComputeCompatibilitySnapshotHash(
				Planet->GetGeneratedTasks(),
				Planet->GetGeneratedTaskLinks(),
				Planet->GetGeneratedCellStates(),
				Planet->GetGeneratedEdgeStates(),
				Summary);
	if (Planet->WorldSeed
			!= FABTSM3R2AcceptanceManifest::DisplaySeed
		|| !Summary.bAccepted
		|| Summary.GeneratorVersion
			!= FABTSM3R2AcceptanceManifest::GeneratorVersion
		|| Summary.LayoutPolicyVersion
			!= FABTSM3R1AcceptanceManifest::
				CompatibilityLayoutPolicyVersion
		|| CompatibilitySnapshot != DisplayOracle.SnapshotHash)
	{
		FinishM3R2Smoke(
			false,
			TEXT("CompatibilityOracleIdentityMismatch"));
		return;
	}

	const FABTSM3MonthlyWorldSchema& Schema =
		Planet->GetMonthlyWorldSchema();
	if (static_cast<uint64>(Schema.Identity.SchemaConfigHash)
			!= FABTSM3R1AcceptanceManifest::
				FrozenDisplaySchemaConfigHash
		|| static_cast<uint64>(Schema.Identity.SchemaLayoutHash)
			!= FABTSM3R1AcceptanceManifest::
				FrozenDisplaySchemaLayoutHash
		|| Schema.Quality.bMonthlyWorldAccepted)
	{
		FinishM3R2Smoke(false, TEXT("R1SchemaIdentityMismatch"));
		return;
	}

	if (!Planet->ValidateMonthlyRoutePool(Failure))
	{
		FinishM3R2Smoke(
			false,
			FString::Printf(TEXT("RoutePool:%s"), *Failure));
		return;
	}
	const FABTSM3MonthlyRoutePool& Pool =
		Planet->GetMonthlyRoutePool();
	if (!Pool.bRoutePoolValid
		|| Pool.bMonthlyWorldAccepted
		|| Pool.bUsedRouteFallback
		|| Pool.RejectReason
			!= EABTSM3MonthlyRouteRejectReason::None
		|| Pool.SchemaVersion
			!= FABTSM3R2AcceptanceManifest::
				RoutePoolSchemaVersion
		|| Pool.GeneratorVersion
			!= FABTSM3R2AcceptanceManifest::GeneratorVersion
		|| Pool.LayoutPolicyVersion
			!= FABTSM3R2AcceptanceManifest::
				MonthlyLayoutPolicyVersion
		|| Pool.WorldSeed
			!= FABTSM3R2AcceptanceManifest::DisplaySeed
		|| Pool.AttemptedCandidateCount
			!= FABTSM3R2AcceptanceManifest::
				DisplayAttemptedCandidates
		|| Pool.NormalHardPassCount
			!= FABTSM3R2AcceptanceManifest::
				DisplayNormalHardPassCount
		|| Pool.RetainedCandidates.Num()
			!= FABTSM3R2AcceptanceManifest::
				DisplayRetainedCandidates
		|| static_cast<uint64>(Pool.RouteCandidatePoolHash)
			!= FABTSM3R2AcceptanceManifest::
				FrozenDisplayPoolHash
		|| FABTSM3MonthlyRouteBuilder::
				ComputePoolSnapshotHash(Pool)
			!= FABTSM3R2AcceptanceManifest::
				FrozenDisplaySnapshotHash)
	{
		FinishM3R2Smoke(false, TEXT("RoutePoolIdentityMismatch"));
		return;
	}

	const FABTSM3MonthlyRouteCandidate& Best =
		Pool.RetainedCandidates[0];
	if (Best.Metrics.RouteLengthCM
			!= FABTSM3R2AcceptanceManifest::
				DisplayBestRouteLengthCM
		|| Best.Metrics.ScenicBendCount
			!= FABTSM3R2AcceptanceManifest::
				DisplayBestScenicBendCount
		|| Best.Metrics.MaxStraightCM
			!= FABTSM3R2AcceptanceManifest::
				DisplayBestMaxStraightCM
		|| Best.Metrics.MinSelfApproachCells
			!= FABTSM3R2AcceptanceManifest::
				DisplayBestSelfApproachCells
		|| Best.RouteScore
			!= FABTSM3R2AcceptanceManifest::DisplayBestScore
		|| Planet->GetBuildingSpawnSites().Num() != 4)
	{
		FinishM3R2Smoke(false, TEXT("RoutePoolMetricsMismatch"));
		return;
	}
	FinishM3R2Smoke(true, FString());
}

void AABTSM3GameMode::FinishM3R2Smoke(
	const bool bPassed,
	const FString& Failure)
{
	GetWorldTimerManager().ClearTimer(M3R2SmokeTimer);
	const double ElapsedSeconds = FMath::Max(
		0.0,
		FPlatformTime::Seconds() - M3R2SmokeStartSeconds);
	if (bPassed)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M3R2][RuntimeCertification] ManifestHash=%016llX Terminal=1 Passed=1 Failed=0 ElapsedSeconds=%.3f"),
			static_cast<unsigned long long>(
				FABTSM3R2AcceptanceManifest::
					ComputeManifestHash()),
			ElapsedSeconds);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R2][RuntimeCertification] ManifestHash=%016llX Terminal=1 Passed=0 Failed=1 Failure=%s ElapsedSeconds=%.3f"),
			static_cast<unsigned long long>(
				FABTSM3R2AcceptanceManifest::
					ComputeManifestHash()),
			*Failure,
			ElapsedSeconds);
	}
	FPlatformMisc::RequestExitWithStatus(
		false,
		bPassed ? 0 : 1,
		TEXT("AABTSM3GameMode::FinishM3R2Smoke"));
}

void AABTSM3GameMode::OnInitialPlayerPlaced(ACharacter& Character, const FTransform& SpawnTransform, const int32 SpawnCellId)
{
}
