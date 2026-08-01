// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM3GameMode.h"

#include "ABTSRuntime.h"
#include "Components/CapsuleComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/ABTSM1PlayerController.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "PCG/ABTSM3R0AcceptanceManifest.h"
#include "PCG/ABTSM3R1AcceptanceManifest.h"
#include "PCG/ABTSM3R2AcceptanceManifest.h"
#include "PCG/ABTSM3R31AcceptanceManifest.h"
#include "PCG/ABTSM3R3AcceptanceManifest.h"
#include "PCG/ABTSM3R4AcceptanceManifest.h"
#include "PCG/ABTSM3R5AcceptanceManifest.h"
#include "Terrain/ABTSM3Planet.h"
#include "TimerManager.h"
#include "UI/ABTSM1HUD.h"
#include "World/ABTSCollisionChannels.h"

namespace ABTSM3R5GameModePrivate
{
class FSurfaceHash64
{
public:
	void Add(const uint64 Input)
	{
		for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
		{
			Value ^= static_cast<uint8>(
				(Input >> (ByteIndex * 8)) & 0xffull);
			Value *= 1099511628211ull;
		}
	}

	uint64 Get() const
	{
		return Value;
	}

private:
	uint64 Value = 14695981039346656037ull;
};

uint64 ComputeSurfaceQueryHash(const AABTSM3Planet& Planet)
{
	static const FVector Directions[] = {
		FVector::ForwardVector,
		-FVector::ForwardVector,
		FVector::RightVector,
		-FVector::RightVector,
		FVector::UpVector,
		-FVector::UpVector,
		FVector(1.0, 1.0, 1.0).GetSafeNormal(),
		FVector(-1.0, 1.0, -1.0).GetSafeNormal()
	};
	FSurfaceHash64 Hash;
	for (const FVector& Direction : Directions)
	{
		FVector WorldPosition = FVector::ZeroVector;
		FVector WorldNormal = FVector::ZeroVector;
		float Radius = 0.0f;
		int32 CellId = INDEX_NONE;
		const bool bHit = Planet.QuerySurface(
			Direction,
			WorldPosition,
			WorldNormal,
			Radius,
			CellId);
		Hash.Add(bHit ? 1ull : 0ull);
		Hash.Add(static_cast<uint32>(CellId));
		Hash.Add(static_cast<uint32>(
			FMath::RoundToInt(Radius * 100.0f)));
		Hash.Add(static_cast<uint32>(
			FMath::RoundToInt(WorldNormal.X * 1000000.0)));
		Hash.Add(static_cast<uint32>(
			FMath::RoundToInt(WorldNormal.Y * 1000000.0)));
		Hash.Add(static_cast<uint32>(
			FMath::RoundToInt(WorldNormal.Z * 1000000.0)));
	}
	return Hash.Get();
}
}

AABTSM3GameMode::AABTSM3GameMode()
{
	DefaultPawnClass = AABTSM25BirdCharacter::StaticClass();
	PlayerControllerClass = AABTSM1PlayerController::StaticClass();
	HUDClass = AABTSM1HUD::StaticClass();
#if WITH_EDITOR
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
#endif
}

void AABTSM3GameMode::BeginPlay()
{
	Super::BeginPlay();
#if WITH_EDITOR
	bMonthlyLogicRegionDebugEnabled = FParse::Param(
		FCommandLine::Get(),
		TEXT("ABTSM3R5LogicRegions"));
	bMonthlyLogicRegionDebugReadyLogged = false;
	MonthlyLogicRegionDebugRefreshRemaining = 0.0f;
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M3R5][LogicRegionDebug] Shortcut=F7 StartupEnabled=%d TargetColor=Red AttackCorridorColor=Orange PreviewCandidateRequired=1"),
		bMonthlyLogicRegionDebugEnabled ? 1 : 0);
#endif
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
	ManifestFailure.Reset();
	if (FABTSM3R3AcceptanceManifest::Validate(ManifestFailure))
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M3R3][AcceptanceManifest] SelfValid=1 SpatialSchema=%d CompatibilityOracle=Gen3/Policy1 MonthlyPolicy=%d ManifestHash=%016llX SweepSeedManifestHash=%016llX ReferenceSeedManifestHash=%016llX FixtureCatalogHash=%016llX Entries=%d ExpectedSpatialCases=8 ExpectedFailureCases=2 ExpectedRuntimeCases=1 DisplaySeed=%d"),
			FABTSM3R3AcceptanceManifest::SpatialSchemaVersion,
			FABTSM3R3AcceptanceManifest::
				MonthlyLayoutPolicyVersion,
			static_cast<unsigned long long>(
				FABTSM3R3AcceptanceManifest::
					ComputeManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R3AcceptanceManifest::
					ComputeSweepSeedManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R3AcceptanceManifest::
					ComputeReferenceSeedManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3MonthlyEncounterBuilder::
					ComputeFixtureProfileCatalogHash()),
			FABTSM3R3AcceptanceManifest::GetEntries().Num(),
			FABTSM3R3AcceptanceManifest::DisplaySeed);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R3][AcceptanceManifest] SelfValid=0 Failure=%s ComputedManifestHash=%016llX ComputedSweepSeedManifestHash=%016llX ComputedReferenceSeedManifestHash=%016llX ComputedFixtureCatalogHash=%016llX"),
			*ManifestFailure,
			static_cast<unsigned long long>(
				FABTSM3R3AcceptanceManifest::
					ComputeManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R3AcceptanceManifest::
					ComputeSweepSeedManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R3AcceptanceManifest::
					ComputeReferenceSeedManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3MonthlyEncounterBuilder::
					ComputeFixtureProfileCatalogHash()));
	}
	ManifestFailure.Reset();
	if (FABTSM3R31AcceptanceManifest::Validate(
			ManifestFailure))
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M3R3.1][AcceptanceManifest] SelfValid=1 Schema=%d MonthlyPolicy=%d ManifestHash=%016llX RequiredR3=%016llX SeedManifest=%016llX Entries=%d ExpectedSlotFieldCases=7 ExpectedFailureCases=2 ExpectedRuntimeCases=1 IntegrationPending=1 DisplaySeed=%d"),
			FABTSM3R31AcceptanceManifest::
				SlotFieldSchemaVersion,
			FABTSM3R31AcceptanceManifest::
				MonthlyLayoutPolicyVersion,
			static_cast<unsigned long long>(
				FABTSM3R31AcceptanceManifest::
					ComputeManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R31AcceptanceManifest::
					RequiredR3ManifestHash),
			static_cast<unsigned long long>(
				FABTSM3R31AcceptanceManifest::
					ComputeSweepSeedManifestHash()),
			FABTSM3R31AcceptanceManifest::GetEntries().Num(),
			FABTSM3R31AcceptanceManifest::DisplaySeed);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R3.1][AcceptanceManifest] SelfValid=0 Failure=%s ComputedManifestHash=%016llX ComputedSeedManifestHash=%016llX"),
			*ManifestFailure,
			static_cast<unsigned long long>(
				FABTSM3R31AcceptanceManifest::
					ComputeManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R31AcceptanceManifest::
					ComputeSweepSeedManifestHash()));
	}
	ManifestFailure.Reset();
	if (FABTSM3R4AcceptanceManifest::Validate(
			ManifestFailure))
	{
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M3R4][AcceptanceManifest] SelfValid=1 Schema=%d MonthlyPolicy=%d ManifestHash=%016llX RequiredR31=%016llX SeedManifest=%016llX Entries=%d ExpectedCoreCases=8 ExpectedFailureCases=8 ExpectedRuntimeCases=1 M3LocalAccepted=1 FixtureAuthority=1 IntegrationPending=1 DisplaySeed=%d"),
			FABTSM3R4AcceptanceManifest::
				WitnessSchemaVersion,
			FABTSM3R4AcceptanceManifest::
				MonthlyLayoutPolicyVersion,
			static_cast<unsigned long long>(
				FABTSM3R4AcceptanceManifest::
					ComputeManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R4AcceptanceManifest::
					RequiredR31ManifestHash),
			static_cast<unsigned long long>(
				FABTSM3R4AcceptanceManifest::
					ComputeSweepSeedManifestHash()),
			FABTSM3R4AcceptanceManifest::GetEntries().Num(),
			FABTSM3R4AcceptanceManifest::DisplaySeed);
	}
	else
	{
		UE_LOG(
			LogABTSRuntime,
			Error,
			TEXT("[ABTS][M3R4][AcceptanceManifest] SelfValid=0 Failure=%s ComputedManifestHash=%016llX ComputedSeedManifestHash=%016llX M3LocalAccepted=0 IntegrationPending=1"),
			*ManifestFailure,
			static_cast<unsigned long long>(
				FABTSM3R4AcceptanceManifest::
					ComputeManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R4AcceptanceManifest::
					ComputeSweepSeedManifestHash()));
	}
	ManifestFailure.Reset();
	if (FABTSM3R5AcceptanceManifest::Validate(
			ManifestFailure))
	{
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M3R5][AcceptanceManifest] SelfValid=1 Schema=%d Planner=%d MonthlyPolicy=%d ManifestHash=%016llX RequiredR3=%016llX SeedManifest=%016llX Entries=%d ExpectedBiomeTests=1 ExpectedFailureTests=1 ExpectedSweepTests=1 ExpectedSweepSeeds=100 ExpectedRuntimeCases=1 M3LocalAccepted=1 PreviewAuthority=1 IntegrationPending=1 MonthlyAccepted=0 DisplaySeed=%d PreviewCandidate=%d"),
			FABTSM3R5AcceptanceManifest::
				PresentationSchemaVersion,
			FABTSM3R5AcceptanceManifest::PlannerVersion,
			FABTSM3R5AcceptanceManifest::
				MonthlyLayoutPolicyVersion,
			static_cast<unsigned long long>(
				FABTSM3R5AcceptanceManifest::
					ComputeManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R5AcceptanceManifest::
					RequiredR3ManifestHash),
			static_cast<unsigned long long>(
				FABTSM3R5AcceptanceManifest::
					ComputeSweepSeedManifestHash()),
			FABTSM3R5AcceptanceManifest::GetEntries().Num(),
			FABTSM3R5AcceptanceManifest::DisplaySeed,
			FABTSM3R5AcceptanceManifest::
				DisplayPreviewSourceCandidateId);
	}
	else
	{
		UE_LOG(
			LogABTSRuntime,
			Error,
			TEXT("[ABTS][M3R5][AcceptanceManifest] SelfValid=0 Failure=%s ComputedManifestHash=%016llX ComputedSeedManifestHash=%016llX M3LocalAccepted=0 PreviewAuthority=0 IntegrationPending=1 MonthlyAccepted=0"),
			*ManifestFailure,
			static_cast<unsigned long long>(
				FABTSM3R5AcceptanceManifest::
					ComputeManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R5AcceptanceManifest::
					ComputeSweepSeedManifestHash()));
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
	if (FParse::Param(FCommandLine::Get(), TEXT("ABTSM3R3Smoke")))
	{
		M3R3SmokeStartSeconds = FPlatformTime::Seconds();
		GetWorldTimerManager().SetTimer(
			M3R3SmokeTimer,
			this,
			&AABTSM3GameMode::TryCompleteM3R3Smoke,
			0.25f,
			true,
			0.25f);
	}
	if (FParse::Param(
			FCommandLine::Get(),
			TEXT("ABTSM3R31Smoke")))
	{
		M3R31SmokeStartSeconds = FPlatformTime::Seconds();
		GetWorldTimerManager().SetTimer(
			M3R31SmokeTimer,
			this,
			&AABTSM3GameMode::TryCompleteM3R31Smoke,
			0.25f,
			true,
			0.25f);
	}
	if (FParse::Param(
			FCommandLine::Get(),
			TEXT("ABTSM3R4Smoke")))
	{
		M3R4SmokeStartSeconds = FPlatformTime::Seconds();
		GetWorldTimerManager().SetTimer(
			M3R4SmokeTimer,
			this,
			&AABTSM3GameMode::TryCompleteM3R4Smoke,
			0.25f,
			true,
			0.25f);
	}
	if (FParse::Param(
			FCommandLine::Get(),
			TEXT("ABTSM3R5Smoke")))
	{
		M3R5SmokeStartSeconds = FPlatformTime::Seconds();
		GetWorldTimerManager().SetTimer(
			M3R5SmokeTimer,
			this,
			&AABTSM3GameMode::TryCompleteM3R5Smoke,
			0.25f,
			true,
			0.25f);
	}
	TryPlacePlayerAtInitialRoad();
}

#if WITH_EDITOR
void AABTSM3GameMode::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APlayerController* PlayerController =
		GetWorld() != nullptr
		? GetWorld()->GetFirstPlayerController()
		: nullptr;
	if (PlayerController != nullptr
		&& PlayerController->WasInputKeyJustPressed(
			EKeys::F7))
	{
		ToggleMonthlyLogicRegionDebug();
	}
	if (bMonthlyLogicRegionDebugEnabled)
	{
		RefreshMonthlyLogicRegionDebug(DeltaSeconds);
	}
}

void AABTSM3GameMode::ToggleMonthlyLogicRegionDebug()
{
	bMonthlyLogicRegionDebugEnabled =
		!bMonthlyLogicRegionDebugEnabled;
	bMonthlyLogicRegionDebugReadyLogged = false;
	MonthlyLogicRegionDebugRefreshRemaining = 0.0f;
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M3R5][LogicRegionDebug] Enabled=%d Shortcut=F7"),
		bMonthlyLogicRegionDebugEnabled ? 1 : 0);
	if (!bMonthlyLogicRegionDebugEnabled
		&& GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(
			0x4D335235,
			2.0f,
			FColor::Silver,
			TEXT("M3R5 Logic Regions OFF (F7)"));
	}
}

void AABTSM3GameMode::RefreshMonthlyLogicRegionDebug(
	const float DeltaSeconds)
{
	MonthlyLogicRegionDebugRefreshRemaining -=
		FMath::Max(0.0f, DeltaSeconds);
	if (MonthlyLogicRegionDebugRefreshRemaining > 0.0f)
	{
		return;
	}
	constexpr float RefreshIntervalSeconds = 0.20f;
	constexpr float DrawLifeTimeSeconds = 0.35f;
	MonthlyLogicRegionDebugRefreshRemaining =
		RefreshIntervalSeconds;

	int32 TargetCellCount = 0;
	int32 AttackCorridorCellCount = 0;
	int32 SatelliteE5PreviewCount = 0;
	bool bDrewAnyPlanet = false;
	for (TActorIterator<AABTSM3Planet> It(GetWorld());
		It;
		++It)
	{
		int32 PlanetTargetCellCount = 0;
		int32 PlanetAttackCorridorCellCount = 0;
		bool bPlanetSatelliteE5PreviewDrawn = false;
		if (It->DrawMonthlyLogicRegionDebugOverlay(
				DrawLifeTimeSeconds,
				PlanetTargetCellCount,
				PlanetAttackCorridorCellCount,
				bPlanetSatelliteE5PreviewDrawn))
		{
			bDrewAnyPlanet = true;
			TargetCellCount += PlanetTargetCellCount;
			AttackCorridorCellCount +=
				PlanetAttackCorridorCellCount;
			SatelliteE5PreviewCount +=
				bPlanetSatelliteE5PreviewDrawn ? 1 : 0;
		}
	}
	if (GEngine == nullptr)
	{
		return;
	}
	if (bDrewAnyPlanet)
	{
		if (!bMonthlyLogicRegionDebugReadyLogged)
		{
			bMonthlyLogicRegionDebugReadyLogged = true;
			UE_LOG(
				LogABTSRuntime,
				Log,
				TEXT("[ABTS][M3R5.1][LogicRegionDebug] Ready=1 Enabled=1 Shortcut=F7 ExactPreviewCandidate=1 TargetFootprintCells=%d AttackCorridorCells=%d SatelliteE5Previews=%d"),
				TargetCellCount,
				AttackCorridorCellCount,
				SatelliteE5PreviewCount);
		}
		GEngine->AddOnScreenDebugMessage(
			0x4D335235,
			DrawLifeTimeSeconds + 0.1f,
			FColor::Yellow,
			FString::Printf(
				TEXT("M3R5.1 Logic Regions ON (F7)  RED=Target [%d]  ORANGE=Corridor [%d]  BLUE/MAGENTA=Satellite/E5 [%d]"),
				TargetCellCount,
				AttackCorridorCellCount,
				SatelliteE5PreviewCount));
	}
	else
	{
		GEngine->AddOnScreenDebugMessage(
			0x4D335235,
			DrawLifeTimeSeconds + 0.1f,
			FColor::Red,
			TEXT("M3R5 Logic Regions unavailable: launch with -ABTSM3R5Preview -ABTSM3R5PreviewCandidate=4"));
	}
}
#endif

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

void AABTSM3GameMode::TryCompleteM3R3Smoke()
{
	constexpr double MaxWaitSeconds = 20.0;
	const double ElapsedSeconds =
		FPlatformTime::Seconds() - M3R3SmokeStartSeconds;

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
		FinishM3R3Smoke(
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
		FinishM3R3Smoke(
			false,
			TEXT("PlanetNotReadyWithin20Seconds"));
		return;
	}
	if (!bInitialPlayerPlaced)
	{
		FinishM3R3Smoke(
			false,
			TEXT("InitialPlayerNotPlacedWithin20Seconds"));
		return;
	}

	FString Failure;
	if (!FABTSM3R3AcceptanceManifest::Validate(Failure))
	{
		FinishM3R3Smoke(
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
			!= FABTSM3R3AcceptanceManifest::DisplaySeed
		|| !Summary.bAccepted
		|| Summary.GeneratorVersion
			!= FABTSM3R3AcceptanceManifest::GeneratorVersion
		|| Summary.LayoutPolicyVersion
			!= FABTSM3R1AcceptanceManifest::
				CompatibilityLayoutPolicyVersion
		|| CompatibilitySnapshot != DisplayOracle.SnapshotHash
		|| Planet->GetMonthlyWorldSchema()
			.Quality.bMonthlyWorldAccepted)
	{
		FinishM3R3Smoke(
			false,
			TEXT("CompatibilityBoundaryMismatch"));
		return;
	}
	const TArray<FABTSM3BuildingSpawnSite>& CompatibilitySites =
		Planet->GetBuildingSpawnSites();
	const TConstArrayView<FABTSM3R0ExpectedBuildingSite>
		ExpectedCompatibilitySites =
			FABTSM3R0AcceptanceManifest::
				GetDisplayBuildingSites();
	if (CompatibilitySites.Num()
			!= ExpectedCompatibilitySites.Num())
	{
		FinishM3R3Smoke(
			false,
			TEXT("CompatibilityBuildingSiteCountMismatch"));
		return;
	}
	for (int32 SiteIndex = 0;
		SiteIndex < ExpectedCompatibilitySites.Num();
		++SiteIndex)
	{
		if (CompatibilitySites[SiteIndex].TaskId
				!= ExpectedCompatibilitySites[SiteIndex].TaskId
			|| CompatibilitySites[SiteIndex].CellId
				!= ExpectedCompatibilitySites[SiteIndex].CellId)
		{
			FinishM3R3Smoke(
				false,
				FString::Printf(
					TEXT("CompatibilityBuildingSiteMismatch:%d"),
					SiteIndex));
			return;
		}
	}
	if (!Planet->ValidateMonthlyRoutePool(Failure)
		|| static_cast<uint64>(
				Planet->GetMonthlyRoutePool().
					RouteCandidatePoolHash)
			!= FABTSM3R2AcceptanceManifest::
				FrozenDisplayPoolHash)
	{
		FinishM3R3Smoke(
			false,
			FString::Printf(TEXT("RoutePool:%s"), *Failure));
		return;
	}
	if (!Planet->ValidateMonthlySpatialResult(Failure))
	{
		FinishM3R3Smoke(
			false,
			FString::Printf(TEXT("Spatial:%s"), *Failure));
		return;
	}
	const FABTSM3MonthlySpatialResult& Result =
		Planet->GetMonthlySpatialResult();
	if (!Result.bSpatialResultValid
		|| Result.bMonthlyWorldAccepted
		|| Result.RejectReason
			!= EABTSM3MonthlySpatialRejectReason::None
		|| Result.SchemaVersion
			!= FABTSM3R3AcceptanceManifest::SpatialSchemaVersion
		|| Result.GeneratorVersion
			!= FABTSM3R3AcceptanceManifest::GeneratorVersion
		|| Result.LayoutPolicyVersion
			!= FABTSM3R3AcceptanceManifest::
				MonthlyLayoutPolicyVersion
		|| Result.AttemptedRouteCandidateCount
			!= FABTSM3R3AcceptanceManifest::
				DisplayAttemptedRouteCandidates
		|| Result.SpatialHardPassCount
			!= FABTSM3R3AcceptanceManifest::
				DisplaySpatialHardPassCount
		|| Result.RetainedCandidates.Num()
			!= FABTSM3R3AcceptanceManifest::
				DisplayRetainedCandidates
		|| static_cast<uint64>(Result.SpatialResultHash)
			!= FABTSM3R3AcceptanceManifest::
				FrozenDisplayResultHash
		|| FABTSM3MonthlyEncounterBuilder::
				ComputeResultSnapshotHash(Result)
			!= FABTSM3R3AcceptanceManifest::
				FrozenDisplaySnapshotHash)
	{
		FinishM3R3Smoke(
			false,
			TEXT("SpatialResultIdentityMismatch"));
		return;
	}
	const FABTSM3MonthlySpatialCandidate& Best =
		Result.RetainedCandidates[0];
	if (static_cast<uint64>(Best.SpatialCandidateHash)
			!= FABTSM3R3AcceptanceManifest::
				FrozenDisplayCandidateHash
		|| Best.RecomputedRoute.Metrics.RouteLengthCM
			!= FABTSM3R3AcceptanceManifest::
				DisplayRecomputedRouteLengthCM
		|| Best.Encounters.Num()
			!= FABTSM3R3AcceptanceManifest::DisplayEncounterCount
		|| Best.Pockets.Num()
			!= FABTSM3R3AcceptanceManifest::DisplayPocketCount
		|| Best.BiomeDistricts.Num()
			!= FABTSM3R3AcceptanceManifest::
				DisplayBiomeDistrictCount
		|| Best.PlayableCellCount
			!= FABTSM3R3AcceptanceManifest::
				DisplayPlayableCellCount
		|| Best.ApprovedTransitionCellCount
			!= FABTSM3R3AcceptanceManifest::
				DisplayApprovedTransitionCellCount
		|| Best.ActiveRoleCoveragePermille
			!= FABTSM3R3AcceptanceManifest::
				DisplayActiveCoveragePermille
		|| Best.DeepWildPermille
			!= FABTSM3R3AcceptanceManifest::
				DisplayDeepWildPermille
		|| Best.OptimizedPVSRays
			!= FABTSM3R3AcceptanceManifest::
				DisplayOptimizedPVSRays)
	{
		FinishM3R3Smoke(
			false,
			TEXT("SpatialCandidateMetricsMismatch"));
		return;
	}
	FinishM3R3Smoke(true, FString());
}

void AABTSM3GameMode::FinishM3R3Smoke(
	const bool bPassed,
	const FString& Failure)
{
	GetWorldTimerManager().ClearTimer(M3R3SmokeTimer);
	const double ElapsedSeconds = FMath::Max(
		0.0,
		FPlatformTime::Seconds() - M3R3SmokeStartSeconds);
	if (bPassed)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M3R3][RuntimeCertification] ManifestHash=%016llX Terminal=1 Passed=1 Failed=0 ElapsedSeconds=%.3f"),
			static_cast<unsigned long long>(
				FABTSM3R3AcceptanceManifest::
					ComputeManifestHash()),
			ElapsedSeconds);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R3][RuntimeCertification] ManifestHash=%016llX Terminal=1 Passed=0 Failed=1 Failure=%s ElapsedSeconds=%.3f"),
			static_cast<unsigned long long>(
				FABTSM3R3AcceptanceManifest::
					ComputeManifestHash()),
			*Failure,
			ElapsedSeconds);
	}
	FPlatformMisc::RequestExitWithStatus(
		false,
		bPassed ? 0 : 1,
		TEXT("AABTSM3GameMode::FinishM3R3Smoke"));
}

void AABTSM3GameMode::TryCompleteM3R31Smoke()
{
	constexpr double MaxWaitSeconds = 20.0;
	const double ElapsedSeconds =
		FPlatformTime::Seconds() - M3R31SmokeStartSeconds;
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
		FinishM3R31Smoke(
			false,
			TEXT("CertificationExceeded20Seconds"));
		return;
	}
	if ((Planet == nullptr || !bInitialPlayerPlaced)
		&& ElapsedSeconds < MaxWaitSeconds)
	{
		return;
	}
	if (Planet == nullptr || !bInitialPlayerPlaced)
	{
		FinishM3R31Smoke(
			false,
			Planet == nullptr
				? TEXT("PlanetNotReadyWithin20Seconds")
				: TEXT("InitialPlayerNotPlacedWithin20Seconds"));
		return;
	}

	FString Failure;
	if (!FABTSM3R31AcceptanceManifest::Validate(Failure))
	{
		FinishM3R31Smoke(
			false,
			FString::Printf(TEXT("Manifest:%s"), *Failure));
		return;
	}
	if (Planet->WorldSeed
			!= FABTSM3R31AcceptanceManifest::DisplaySeed
		|| !Planet->PCGSummary.bAccepted
		|| Planet->GetMonthlyWorldSchema()
			.Quality.bMonthlyWorldAccepted
		|| !Planet->GetFinaleLaunchFrame().IsUsable()
		|| Planet->GetBuildingSpawnSites().Num()
			!= FABTSM3R0AcceptanceManifest::
				GetDisplayBuildingSites().Num())
	{
		FinishM3R31Smoke(
			false,
			TEXT("CompatibilityBoundaryMismatch"));
		return;
	}
	if (!Planet->ValidateMonthlyRoutePool(Failure)
		|| !Planet->ValidateMonthlySpatialResult(Failure)
		|| !Planet->ValidateMonthlySlingshotFieldResult(
			Failure))
	{
		FinishM3R31Smoke(
			false,
			FString::Printf(
				TEXT("PlanetValidation:%s"),
				*Failure));
		return;
	}
	const FABTSM3MonthlySpatialResult& SpatialResult =
		Planet->GetMonthlySpatialResult();
	if (static_cast<uint64>(
			SpatialResult.SpatialResultHash)
			!= FABTSM3R3AcceptanceManifest::
				FrozenDisplayResultHash
		|| SpatialResult.RetainedCandidates.IsEmpty()
		|| static_cast<uint64>(
				SpatialResult.RetainedCandidates[0].
					SpatialCandidateHash)
			!= FABTSM3R3AcceptanceManifest::
				FrozenDisplayCandidateHash)
	{
		FinishM3R31Smoke(
			false,
			TEXT("RequiredR3IdentityMismatch"));
		return;
	}
	const FABTSM3MonthlySlingshotFieldResult& Result =
		Planet->GetMonthlySlingshotFieldResult();
	if (!Result.bSlingshotFieldResultValid
		|| Result.bMonthlyWorldAccepted
		|| Result.RejectReason
			!= EABTSM3MonthlySlingshotFieldRejectReason::None
		|| Result.SchemaVersion
			!= FABTSM3R31AcceptanceManifest::
				SlotFieldSchemaVersion
		|| Result.MaxCordLengthCM
			!= FABTSM3R31AcceptanceManifest::
				DefaultMaxCordLengthCM
		|| Result.FieldsPerCandidate
			!= FABTSM3R31AcceptanceManifest::
				DisplayFieldsPerCandidate
		|| Result.SlotsPerCandidate
			!= FABTSM3R31AcceptanceManifest::
				DisplaySlotsPerCandidate
		|| Result.RetainedCandidates.Num()
			!= SpatialResult.RetainedCandidates.Num()
		|| Result.RetainedCandidates.IsEmpty()
		|| static_cast<uint64>(Result.ConfigHash)
			!= FABTSM3R31AcceptanceManifest::
				FrozenDisplayConfigHash
		|| static_cast<uint64>(Result.ResultHash)
			!= FABTSM3R31AcceptanceManifest::
				FrozenDisplayResultHash
		|| static_cast<uint64>(
				Result.RetainedCandidates[0].CandidateHash)
			!= FABTSM3R31AcceptanceManifest::
				FrozenDisplayCandidateHash)
	{
		FinishM3R31Smoke(
			false,
			TEXT("SlotFieldIdentityMismatch"));
		return;
	}
	FinishM3R31Smoke(true, FString());
}

void AABTSM3GameMode::FinishM3R31Smoke(
	const bool bPassed,
	const FString& Failure)
{
	GetWorldTimerManager().ClearTimer(M3R31SmokeTimer);
	const double ElapsedSeconds = FMath::Max(
		0.0,
		FPlatformTime::Seconds() - M3R31SmokeStartSeconds);
	if (bPassed)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M3R3.1][RuntimeCertification] ManifestHash=%016llX Terminal=1 Passed=1 Failed=0 M3LocalAccepted=1 IntegrationAccepted=0 ElapsedSeconds=%.3f"),
			static_cast<unsigned long long>(
				FABTSM3R31AcceptanceManifest::
					ComputeManifestHash()),
			ElapsedSeconds);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R3.1][RuntimeCertification] ManifestHash=%016llX Terminal=1 Passed=0 Failed=1 M3LocalAccepted=0 IntegrationAccepted=0 Failure=%s ElapsedSeconds=%.3f"),
			static_cast<unsigned long long>(
				FABTSM3R31AcceptanceManifest::
					ComputeManifestHash()),
			*Failure,
			ElapsedSeconds);
	}
	FPlatformMisc::RequestExitWithStatus(
		false,
		bPassed ? 0 : 1,
		TEXT("AABTSM3GameMode::FinishM3R31Smoke"));
}

void AABTSM3GameMode::TryCompleteM3R4Smoke()
{
	constexpr double MaxWaitSeconds = 20.0;
	const double ElapsedSeconds =
		FPlatformTime::Seconds() - M3R4SmokeStartSeconds;
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
		FinishM3R4Smoke(
			false,
			TEXT("CertificationExceeded20Seconds"));
		return;
	}
	if ((Planet == nullptr || !bInitialPlayerPlaced)
		&& ElapsedSeconds < MaxWaitSeconds)
	{
		return;
	}
	if (Planet == nullptr || !bInitialPlayerPlaced)
	{
		FinishM3R4Smoke(
			false,
			Planet == nullptr
				? TEXT("PlanetNotReadyWithin20Seconds")
				: TEXT("InitialPlayerNotPlacedWithin20Seconds"));
		return;
	}

	FString Failure;
	if (!FABTSM3R4AcceptanceManifest::Validate(Failure))
	{
		FinishM3R4Smoke(
			false,
			FString::Printf(
				TEXT("R4AcceptanceManifest:%s"),
				*Failure));
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
			!= FABTSM3R31AcceptanceManifest::DisplaySeed
		|| !Summary.bAccepted
		|| Summary.GeneratorVersion
			!= FABTSM3R1AcceptanceManifest::GeneratorVersion
		|| Summary.LayoutPolicyVersion
			!= FABTSM3R1AcceptanceManifest::
				CompatibilityLayoutPolicyVersion
		|| CompatibilitySnapshot != DisplayOracle.SnapshotHash
		|| Planet->GetMonthlyWorldSchema()
			.Quality.bMonthlyWorldAccepted
		|| !Planet->GetFinaleLaunchFrame().IsUsable()
		|| Planet->GetBuildingSpawnSites().Num()
			!= FABTSM3R0AcceptanceManifest::
				GetDisplayBuildingSites().Num())
	{
		FinishM3R4Smoke(
			false,
			TEXT("CompatibilityBoundaryMismatch"));
		return;
	}

	const FABTSM3MonthlyWorldSchema& Schema =
		Planet->GetMonthlyWorldSchema();
	EABTSM3SchemaRejectReason SchemaReason =
		EABTSM3SchemaRejectReason::None;
	if (!FABTSM3MonthlySchemaBuilder::Validate(
			Schema,
			SchemaReason,
			Failure)
		|| static_cast<uint64>(
				Schema.Identity.SchemaConfigHash)
			!= FABTSM3R1AcceptanceManifest::
				FrozenDisplaySchemaConfigHash
		|| static_cast<uint64>(
				Schema.Identity.SchemaLayoutHash)
			!= FABTSM3R1AcceptanceManifest::
				FrozenDisplaySchemaLayoutHash)
	{
		FinishM3R4Smoke(
			false,
			FString::Printf(
				TEXT("R1SchemaIdentity:%s:%s"),
				FABTSM3MonthlySchemaBuilder::
					GetRejectReasonName(SchemaReason),
				*Failure));
		return;
	}
	if (!Planet->ValidateMonthlyRoutePool(Failure))
	{
		FinishM3R4Smoke(
			false,
			FString::Printf(
				TEXT("R2RouteValidation:%s"),
				*Failure));
		return;
	}
	const FABTSM3MonthlyRoutePool& RoutePool =
		Planet->GetMonthlyRoutePool();
	if (static_cast<uint64>(
			RoutePool.RouteCandidatePoolHash)
			!= FABTSM3R2AcceptanceManifest::
				FrozenDisplayPoolHash
		|| FABTSM3MonthlyRouteBuilder::
				ComputePoolSnapshotHash(RoutePool)
			!= FABTSM3R2AcceptanceManifest::
				FrozenDisplaySnapshotHash)
	{
		FinishM3R4Smoke(
			false,
			TEXT("R2RouteIdentityMismatch"));
		return;
	}
	if (!Planet->ValidateMonthlySpatialResult(Failure))
	{
		FinishM3R4Smoke(
			false,
			FString::Printf(
				TEXT("R3SpatialValidation:%s"),
				*Failure));
		return;
	}
	const FABTSM3MonthlySpatialResult& SpatialResult =
		Planet->GetMonthlySpatialResult();
	if (static_cast<uint64>(
			SpatialResult.SpatialResultHash)
			!= FABTSM3R3AcceptanceManifest::
				FrozenDisplayResultHash
		|| FABTSM3MonthlyEncounterBuilder::
				ComputeResultSnapshotHash(SpatialResult)
			!= FABTSM3R3AcceptanceManifest::
				FrozenDisplaySnapshotHash
		|| SpatialResult.RetainedCandidates.IsEmpty()
		|| static_cast<uint64>(
			SpatialResult.RetainedCandidates[0].
				SpatialCandidateHash)
			!= FABTSM3R3AcceptanceManifest::
				FrozenDisplayCandidateHash)
	{
		FinishM3R4Smoke(
			false,
			TEXT("R3SpatialIdentityMismatch"));
		return;
	}
	if (!Planet->ValidateMonthlySlingshotFieldResult(
			Failure))
	{
		FinishM3R4Smoke(
			false,
			FString::Printf(
				TEXT("R31SlotFieldValidation:%s"),
				*Failure));
		return;
	}
	const FABTSM3MonthlySlingshotFieldResult& FieldResult =
		Planet->GetMonthlySlingshotFieldResult();
	if (static_cast<uint64>(FieldResult.ConfigHash)
			!= FABTSM3R31AcceptanceManifest::
				FrozenDisplayConfigHash
		|| static_cast<uint64>(FieldResult.ResultHash)
			!= FABTSM3R31AcceptanceManifest::
				FrozenDisplayResultHash
		|| FieldResult.RetainedCandidates.IsEmpty()
		|| static_cast<uint64>(
				FieldResult.RetainedCandidates[0].
					CandidateHash)
			!= FABTSM3R31AcceptanceManifest::
				FrozenDisplayCandidateHash)
	{
		FinishM3R4Smoke(
			false,
			TEXT("R31SlotFieldIdentityMismatch"));
		return;
	}

	if (!Planet->ValidateMonthlyWitnessResult(Failure))
	{
		FinishM3R4Smoke(
			false,
			FString::Printf(
				TEXT("R4WitnessValidation:%s"),
				*Failure));
		return;
	}
	const FABTSM3MonthlyWitnessResult& WitnessResult =
		Planet->GetMonthlyWitnessResult();
	if (WitnessResult.RejectReason
			!= EABTSM3MonthlyWitnessRejectReason::NotEvaluated
		|| WitnessResult.bGameplayFinalizeValid
		|| WitnessResult.bExternalInputsCertified
		|| WitnessResult.bMonthlyWorldAccepted
		|| WitnessResult.Authority
			!= EABTSM3WitnessAuthority::None
		|| !WitnessResult.RetainedCandidates.IsEmpty()
		|| WitnessResult.SelectedCandidateId != INDEX_NONE
		|| WitnessResult.GameplayLayoutHash != 0)
	{
		FinishM3R4Smoke(
			false,
			TEXT("R4DefaultPendingIdentityMismatch"));
		return;
	}
	FinishM3R4Smoke(true, FString());
}

void AABTSM3GameMode::FinishM3R4Smoke(
	const bool bPassed,
	const FString& Failure)
{
	GetWorldTimerManager().ClearTimer(M3R4SmokeTimer);
	const double ElapsedSeconds = FMath::Max(
		0.0,
		FPlatformTime::Seconds() - M3R4SmokeStartSeconds);
	if (bPassed)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M3R4][RuntimeCertification] ManifestHash=%016llX Entries=%d Terminal=1 Passed=1 Failed=0 M3LocalAccepted=1 FixtureAuthority=1 IntegrationPending=1 GameplayFinalizeValid=0 ExternalInputsCertified=0 MonthlyWorldAccepted=0 ElapsedSeconds=%.3f"),
			static_cast<unsigned long long>(
				FABTSM3R4AcceptanceManifest::
					ComputeManifestHash()),
			FABTSM3R4AcceptanceManifest::GetEntries().Num(),
			ElapsedSeconds);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R4][RuntimeCertification] ManifestHash=%016llX Entries=%d Terminal=1 Passed=0 Failed=1 M3LocalAccepted=0 FixtureAuthority=1 IntegrationPending=1 GameplayFinalizeValid=0 ExternalInputsCertified=0 MonthlyWorldAccepted=0 Failure=%s ElapsedSeconds=%.3f"),
			static_cast<unsigned long long>(
				FABTSM3R4AcceptanceManifest::
					ComputeManifestHash()),
			FABTSM3R4AcceptanceManifest::GetEntries().Num(),
			*Failure,
			ElapsedSeconds);
	}
	FPlatformMisc::RequestExitWithStatus(
		false,
		bPassed ? 0 : 1,
		TEXT("AABTSM3GameMode::FinishM3R4Smoke"));
}

void AABTSM3GameMode::TryCompleteM3R5Smoke()
{
	constexpr double MaxWaitSeconds = 20.0;
	constexpr double MaxPeakPhysicalMB =
		static_cast<double>(
			FABTSM3R5AcceptanceManifest::
				BaselinePeakPhysicalMB)
		* FABTSM3R5AcceptanceManifest::
			MaxPeakPhysicalPermille
		/ 1000.0;
	const double ElapsedSeconds =
		FPlatformTime::Seconds() - M3R5SmokeStartSeconds;
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
		FinishM3R5Smoke(
			false,
			TEXT("CertificationExceeded20Seconds"));
		return;
	}
	if ((Planet == nullptr || !bInitialPlayerPlaced)
		&& ElapsedSeconds < MaxWaitSeconds)
	{
		return;
	}
	if (Planet == nullptr || !bInitialPlayerPlaced)
	{
		FinishM3R5Smoke(
			false,
			Planet == nullptr
				? TEXT("PlanetNotReadyWithin20Seconds")
				: TEXT("InitialPlayerNotPlacedWithin20Seconds"));
		return;
	}

	FString Failure;
	if (!FABTSM3R5AcceptanceManifest::Validate(Failure))
	{
		FinishM3R5Smoke(
			false,
			FString::Printf(
				TEXT("R5AcceptanceManifest:%s"),
				*Failure));
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
			!= FABTSM3R5AcceptanceManifest::DisplaySeed
		|| Planet->SurfaceSubdivision
			!= FABTSM3R5AcceptanceManifest::
				RequiredSurfaceSubdivision
		|| !Summary.bAccepted
		|| CompatibilitySnapshot
			!= DisplayOracle.SnapshotHash
		|| Planet->GetMonthlyWorldSchema()
			.Quality.bMonthlyWorldAccepted
		|| !Planet->GetFinaleLaunchFrame().IsUsable()
		|| Planet->GetBuildingSpawnSites().Num()
			!= FABTSM3R0AcceptanceManifest::
				GetDisplayBuildingSites().Num())
	{
		FinishM3R5Smoke(
			false,
			TEXT("CompatibilityBoundaryMismatch"));
		return;
	}
	if (!Planet->ValidateMonthlySpatialResult(Failure))
	{
		FinishM3R5Smoke(
			false,
			FString::Printf(
				TEXT("R3SpatialValidation:%s"),
				*Failure));
		return;
	}
	const FABTSM3MonthlySpatialResult& SpatialResult =
		Planet->GetMonthlySpatialResult();
	if (static_cast<uint64>(
			SpatialResult.SpatialResultHash)
			!= FABTSM3R5AcceptanceManifest::
				FrozenDisplaySourceSpatialHash
		|| SpatialResult.bMonthlyWorldAccepted)
	{
		FinishM3R5Smoke(
			false,
			TEXT("R3SpatialIdentityMismatch"));
		return;
	}
	if (!Planet->ValidateMonthlyPresentationResult(
			Failure))
	{
		FinishM3R5Smoke(
			false,
			FString::Printf(
				TEXT("R5PresentationValidation:%s"),
				*Failure));
		return;
	}
	const FABTSM3MonthlyPresentationResult&
		PresentationResult =
			Planet->GetMonthlyPresentationResult();
	const FABTSM3MonthlyCandidatePresentation* Preview =
		FABTSM3MonthlyPresentationBuilder::
			FindCandidatePresentation(
				PresentationResult,
				FABTSM3R5AcceptanceManifest::
					DisplayPreviewSourceCandidateId);
	if (static_cast<uint64>(
			PresentationResult.PresentationConfigHash)
			!= FABTSM3R5AcceptanceManifest::
				FrozenDisplayConfigHash
		|| static_cast<uint64>(
			PresentationResult.PresentationResultHash)
			!= FABTSM3R5AcceptanceManifest::
				FrozenDisplayResultHash
		|| !PresentationResult.bPresentationValid
		|| PresentationResult.bMonthlyWorldAccepted
		|| PresentationResult.CandidatePresentations.Num()
			!= SpatialResult.RetainedCandidates.Num()
		|| Preview == nullptr
		|| static_cast<uint64>(
			Preview->CandidatePresentationHash)
			!= FABTSM3R5AcceptanceManifest::
				FrozenDisplayPreviewCandidateHash)
	{
		FinishM3R5Smoke(
			false,
			TEXT("R5PresentationIdentityMismatch"));
		return;
	}
	if (Preview->MinVisualBiomeComponentCellCount
			< FABTSM3R5AcceptanceManifest::
				MinVisualBiomeComponentCells
		|| Preview->VisualBiomeBoundaryPermille
			> FABTSM3R5AcceptanceManifest::
				MaxVisualBiomeBoundaryPermille)
	{
		FinishM3R5Smoke(
			false,
			TEXT("VisualFragmentationBudgetMismatch"));
		return;
	}
	if (!Planet->IsMonthlyPresentationPreviewActive()
		|| Planet->
				GetMonthlyPresentationPreviewCandidateId()
			!= FABTSM3R5AcceptanceManifest::
				DisplayPreviewSourceCandidateId
		|| static_cast<uint64>(
			Planet->
				GetMonthlyPresentationPreviewCandidateHash())
			!= FABTSM3R5AcceptanceManifest::
				FrozenDisplayPreviewCandidateHash)
	{
		FinishM3R5Smoke(
			false,
			TEXT("ExplicitPreviewAuthorityMissing"));
		return;
	}
	if (Planet->ForestHISM == nullptr
		|| Planet->RockHISM == nullptr
		|| Planet->ForestHISM->GetCollisionEnabled()
			!= ECollisionEnabled::QueryAndPhysics
		|| Planet->RockHISM->GetCollisionEnabled()
			!= ECollisionEnabled::QueryAndPhysics
		|| Planet->ForestHISM->GetCollisionObjectType()
			!= ABTSDeveloperObstacleChannel
		|| Planet->RockHISM->GetCollisionObjectType()
			!= ABTSDeveloperObstacleChannel
		|| Planet->ForestHISM->IsSimulatingPhysics()
		|| Planet->RockHISM->IsSimulatingPhysics())
	{
		FinishM3R5Smoke(
			false,
			TEXT("HISMCollisionContractMismatch"));
		return;
	}
	const int32 ForestInstances =
		Planet->ForestHISM->GetInstanceCount();
	const int32 RockInstances =
		Planet->RockHISM->GetInstanceCount();
	if (ForestInstances < 0
		|| RockInstances < 0
		|| ForestInstances + RockInstances
			> Preview->PlannedDecorationInstanceCount)
	{
		FinishM3R5Smoke(
			false,
			TEXT("HISMInstanceBudgetMismatch"));
		return;
	}
	if (!Planet->IsMonthlyMaterialRhythmApplied()
		|| Planet->GetMonthlyMaterialRhythmCellCount()
			!= Preview->Cells.Num())
	{
		FinishM3R5Smoke(
			false,
			TEXT("MaterialVisualRhythmNotConsumed"));
		return;
	}
	if (Planet->GetMonthlyDecorAccent0InstanceCount() <= 0
		|| Planet->GetMonthlyDecorAccent1InstanceCount()
			<= 0
		|| Planet->GetMonthlyDecorAccent0InstanceCount()
				+ Planet->
					GetMonthlyDecorAccent1InstanceCount()
			!= ForestInstances + RockInstances)
	{
		FinishM3R5Smoke(
			false,
			TEXT("HISMVisualRhythmNotConsumed"));
		return;
	}
	if (Planet->GetLastMonthlyPresentationBuildDurationMS()
		> FABTSM3R5AcceptanceManifest::
			PlannerMaxBudgetMS)
	{
		FinishM3R5Smoke(
			false,
			FString::Printf(
				TEXT("PresentationPlannerExceeded%dMS"),
				FABTSM3R5AcceptanceManifest::
					PlannerMaxBudgetMS));
		return;
	}
	if (Planet->GetLastM3RebuildDurationMS()
		> FABTSM3R5AcceptanceManifest::
			FullRebuildBudgetMS)
	{
		FinishM3R5Smoke(
			false,
			FString::Printf(
				TEXT("RebuildExceeded%dMS"),
				FABTSM3R5AcceptanceManifest::
					FullRebuildBudgetMS));
		return;
	}
	if (!Planet->ValidateMonthlySlingshotFieldResult(
			Failure)
		|| !Planet->ValidateMonthlyWitnessResult(Failure))
	{
		FinishM3R5Smoke(
			false,
			FString::Printf(
				TEXT("DownstreamObservation:%s"),
				*Failure));
		return;
	}
	const FABTSM3MonthlyWitnessResult& WitnessResult =
		Planet->GetMonthlyWitnessResult();
	if (WitnessResult.bMonthlyWorldAccepted
		|| WitnessResult.SelectedCandidateId != INDEX_NONE
		|| WitnessResult.GameplayLayoutHash != 0)
	{
		FinishM3R5Smoke(
			false,
			TEXT("R4PendingBoundaryMismatch"));
		return;
	}
	const uint64 SurfaceQueryHashA =
		ABTSM3R5GameModePrivate::
			ComputeSurfaceQueryHash(*Planet);
	const uint64 SurfaceQueryHashB =
		ABTSM3R5GameModePrivate::
			ComputeSurfaceQueryHash(*Planet);
	if (SurfaceQueryHashA == 0
		|| SurfaceQueryHashA != SurfaceQueryHashB)
	{
		FinishM3R5Smoke(
			false,
			TEXT("QuerySurfaceNondeterministic"));
		return;
	}
	const FPlatformMemoryStats MemoryStats =
		FPlatformMemory::GetStats();
	const double PeakPhysicalMB =
		static_cast<double>(MemoryStats.PeakUsedPhysical)
		/ (1024.0 * 1024.0);
	if (PeakPhysicalMB > MaxPeakPhysicalMB)
	{
		FinishM3R5Smoke(
			false,
			FString::Printf(
				TEXT("PeakPhysicalMB:%.1f"),
				PeakPhysicalMB));
		return;
	}
	FinishM3R5Smoke(true, FString());
}

void AABTSM3GameMode::FinishM3R5Smoke(
	const bool bPassed,
	const FString& Failure)
{
	GetWorldTimerManager().ClearTimer(M3R5SmokeTimer);
	const double ElapsedSeconds = FMath::Max(
		0.0,
		FPlatformTime::Seconds() - M3R5SmokeStartSeconds);
	const AABTSM3Planet* Planet = nullptr;
	for (TActorIterator<AABTSM3Planet> It(GetWorld()); It; ++It)
	{
		Planet = *It;
		break;
	}
	const FABTSM3MonthlyPresentationResult* Result =
		Planet != nullptr
		? &Planet->GetMonthlyPresentationResult()
		: nullptr;
	const FABTSM3MonthlyCandidatePresentation* Preview =
		Result != nullptr
		? FABTSM3MonthlyPresentationBuilder::
			FindCandidatePresentation(
				*Result,
				FABTSM3R5AcceptanceManifest::
					DisplayPreviewSourceCandidateId)
		: nullptr;
	const int32 ForestInstances =
		Planet != nullptr && Planet->ForestHISM != nullptr
		? Planet->ForestHISM->GetInstanceCount()
		: 0;
	const int32 RockInstances =
		Planet != nullptr && Planet->RockHISM != nullptr
		? Planet->RockHISM->GetInstanceCount()
		: 0;
	const FPlatformMemoryStats MemoryStats =
		FPlatformMemory::GetStats();
	const double PeakPhysicalMB =
		static_cast<double>(MemoryStats.PeakUsedPhysical)
		/ (1024.0 * 1024.0);
	const uint64 SurfaceQueryHash =
		Planet != nullptr
		? ABTSM3R5GameModePrivate::
			ComputeSurfaceQueryHash(*Planet)
		: 0;
	if (bPassed)
	{
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M3R5][RuntimeCertification] ManifestHash=%016llX Entries=%d Terminal=1 Passed=1 Failed=0 M3LocalAccepted=1 PreviewAuthority=1 IntegrationPending=1 MonthlyWorldAccepted=0 SourceR3Frozen=1 SourceCandidates=%d PreviewCandidateId=%d SourceSpatialHash=%016llX PresentationConfigHash=%016llX PresentationResultHash=%016llX PreviewCandidateHash=%016llX Districts=%d CoveredCells=%d TotalCells=%d ActiveCoveragePermille=%d DeepWildPermille=%d EncounterThemes=%d VisualBeats=%d BeatMinCM=%d BeatMaxCM=%d MergedLogicalSingletons=%d MergedSmallFragments=%d SingletonComponents=%d MinVisualComponentCells=%d VisualBoundaryPermille=%d ProtectedCells=%d PlannedInstanceQuota=%d MaterialRhythmApplied=1 RhythmCells=%d ForestInstances=%d RockInstances=%d Accent0Instances=%d Accent1Instances=%d ForestCollision=QueryAndPhysics RockCollision=QueryAndPhysics ObstacleChannel=%d ForestSimulatePhysics=0 RockSimulatePhysics=0 QuerySurfaceHash=%016llX PlannerMS=%.3f RebuildMS=%.3f PeakPhysicalMB=%.1f ElapsedSeconds=%.3f"),
			static_cast<unsigned long long>(
				FABTSM3R5AcceptanceManifest::
					ComputeManifestHash()),
			FABTSM3R5AcceptanceManifest::GetEntries().Num(),
			Result != nullptr
				? Result->CandidatePresentations.Num()
				: 0,
			Planet != nullptr
				? Planet->
					GetMonthlyPresentationPreviewCandidateId()
				: INDEX_NONE,
			static_cast<unsigned long long>(
				Result != nullptr
					? Result->SourceSpatialResultHash
					: 0),
			static_cast<unsigned long long>(
				Result != nullptr
					? Result->PresentationConfigHash
					: 0),
			static_cast<unsigned long long>(
				Result != nullptr
					? Result->PresentationResultHash
					: 0),
			static_cast<unsigned long long>(
				Preview != nullptr
					? Preview->CandidatePresentationHash
					: 0),
			Preview != nullptr
				? Preview->DistrictStyles.Num()
				: 0,
			Preview != nullptr
				? Preview->PlayableCellCount
				: 0,
			Preview != nullptr ? Preview->Cells.Num() : 0,
			Preview != nullptr
				? Preview->ActiveRoleCoveragePermille
				: 0,
			Preview != nullptr
				? Preview->DeepWildPermille
				: 0,
			Preview != nullptr
				? Preview->BiomeArchetypeCount
				: 0,
			Preview != nullptr
				? Preview->VisualBeats.Num()
				: 0,
			Preview != nullptr
				? Preview->MinVisualBeatLengthCM
				: 0,
			Preview != nullptr
				? Preview->MaxVisualBeatLengthCM
				: 0,
			Preview != nullptr
				? Preview->MergedLogicalSingletonCellCount
				: 0,
			Preview != nullptr
				? Preview->
					MergedSmallVisualFragmentCellCount
				: 0,
			Preview != nullptr
				? Preview->SingleCellBiomeComponentCount
				: 0,
			Preview != nullptr
				? Preview->
					MinVisualBiomeComponentCellCount
				: 0,
			Preview != nullptr
				? Preview->VisualBiomeBoundaryPermille
				: 0,
			Preview != nullptr
				? Preview->DecorationProtectedCellCount
				: 0,
			Preview != nullptr
				? Preview->PlannedDecorationInstanceCount
				: 0,
			Planet != nullptr
				? Planet->
					GetMonthlyMaterialRhythmCellCount()
				: 0,
			ForestInstances,
			RockInstances,
			Planet != nullptr
				? Planet->
					GetMonthlyDecorAccent0InstanceCount()
				: 0,
			Planet != nullptr
				? Planet->
					GetMonthlyDecorAccent1InstanceCount()
				: 0,
			static_cast<int32>(ABTSDeveloperObstacleChannel),
			static_cast<unsigned long long>(
				SurfaceQueryHash),
			Planet != nullptr
				? Planet->
					GetLastMonthlyPresentationBuildDurationMS()
				: 0.0,
			Planet != nullptr
				? Planet->GetLastM3RebuildDurationMS()
				: 0.0,
			PeakPhysicalMB,
			ElapsedSeconds);
	}
	else
	{
		UE_LOG(
			LogABTSRuntime,
			Error,
			TEXT("[ABTS][M3R5][RuntimeCertification] ManifestHash=%016llX Entries=%d Terminal=1 Passed=0 Failed=1 M3LocalAccepted=0 PreviewAuthority=%d IntegrationPending=1 MonthlyWorldAccepted=0 Failure=%s PlannerMS=%.3f RebuildMS=%.3f PeakPhysicalMB=%.1f ElapsedSeconds=%.3f"),
			static_cast<unsigned long long>(
				FABTSM3R5AcceptanceManifest::
					ComputeManifestHash()),
			FABTSM3R5AcceptanceManifest::GetEntries().Num(),
			Planet != nullptr
				&& Planet->
					IsMonthlyPresentationPreviewActive()
				? 1 : 0,
			*Failure,
			Planet != nullptr
				? Planet->
					GetLastMonthlyPresentationBuildDurationMS()
				: 0.0,
			Planet != nullptr
				? Planet->GetLastM3RebuildDurationMS()
				: 0.0,
			PeakPhysicalMB,
			ElapsedSeconds);
	}
	FPlatformMisc::RequestExitWithStatus(
		false,
		bPassed ? 0 : 1,
		TEXT("AABTSM3GameMode::FinishM3R5Smoke"));
}

void AABTSM3GameMode::OnInitialPlayerPlaced(ACharacter& Character, const FTransform& SpawnTransform, const int32 SpawnCellId)
{
}
