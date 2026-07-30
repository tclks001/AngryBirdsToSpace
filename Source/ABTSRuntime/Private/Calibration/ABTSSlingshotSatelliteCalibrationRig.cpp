// Copyright Epic Games, Inc. All Rights Reserved.

#include "Calibration/ABTSSlingshotSatelliteCalibrationRig.h"

#include "ABTSRuntime.h"
#include "Calibration/ABTSCalibrationTargetProxy.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Physics/ABTSSweptCollision.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "Terrain/ABTSM3Planet.h"
#include "World/ABTSM9Satellite.h"

namespace ABTSSlingshotCalibrationRigPrivate
{
	TAutoConsoleVariable<int32> CVarSatelliteGravity(
		TEXT("abts.Calibration.SatelliteGravity"),
		-1,
		TEXT("-1 uses the practice preset, 0 disables M9 gravity, 1 enables it."),
		ECVF_Cheat);

	FLinearColor TierColor(const EABTSSlingshotTier Tier, const bool bMaximum)
	{
		FLinearColor Color = FLinearColor::White;
		switch (Tier)
		{
		case EABTSSlingshotTier::Twig:
			Color = FLinearColor(0.28f, 0.95f, 0.35f, 1.0f);
			break;
		case EABTSSlingshotTier::Simple:
			Color = FLinearColor(0.25f, 0.65f, 1.0f, 1.0f);
			break;
		case EABTSSlingshotTier::Reinforced:
			Color = FLinearColor(1.0f, 0.65f, 0.12f, 1.0f);
			break;
		default:
			break;
		}
		if (!bMaximum) Color *= 0.62f;
		Color.A = 1.0f;
		return Color;
	}

	const TCHAR* TierLabel(const EABTSSlingshotTier Tier)
	{
		switch (Tier)
		{
		case EABTSSlingshotTier::Twig: return TEXT("Twig");
		case EABTSSlingshotTier::Simple: return TEXT("Simple");
		case EABTSSlingshotTier::Reinforced: return TEXT("Reinforced");
		default: return TEXT("Unknown");
		}
	}
}

AABTSSlingshotSatelliteCalibrationRig::AABTSSlingshotSatelliteCalibrationRig()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
}

void AABTSSlingshotSatelliteCalibrationRig::Configure(
	AABTSM3Planet& InPrimaryPlanet,
	AABTSM9Satellite& InSatellite,
	AABTSM6SlingshotSystem& InSlingshotSystem,
	const FVector& InCalibrationOriginWorld,
	const FVector& InStartUnitDirection,
	const FVector& InCalibrationForward,
	const FABTSSatellitePracticePreset& InPreset)
{
	PrimaryPlanet = &InPrimaryPlanet;
	Satellite = &InSatellite;
	SlingshotSystem = &InSlingshotSystem;
	CalibrationOriginWorld = InCalibrationOriginWorld;
	StartUnitDirection = InStartUnitDirection.GetSafeNormal();
	CalibrationForward = FVector::VectorPlaneProject(
		InCalibrationForward, StartUnitDirection).GetSafeNormal();
	Preset = InPreset;
}

void AABTSSlingshotSatelliteCalibrationRig::BeginPlay()
{
	Super::BeginPlay();
	if (!PrimaryPlanet.IsValid()
		|| !Satellite.IsValid()
		|| !SlingshotSystem.IsValid()
		|| CalibrationForward.IsNearlyZero()
		|| Preset.TargetBody != TEXT("PracticeSatellite")
		|| !SlingshotSystem->CopyCalibrationCatalog(
			LaunchProfileCatalog, LaunchProfileHash)
		|| !SlingshotSystem->CopyReinforcedCalibrationLaunchFrame(
			ReinforcedLaunchFrame))
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][Calibration][Rig] Rejected: unresolved dependencies, target body, launch catalog or Reinforced frame."));
		return;
	}
	float ResolvedBirdCollisionRadiusCM = 0.0f;
	if (!SlingshotSystem->CopyCalibrationBirdCollisionRadius(
		ResolvedBirdCollisionRadiusCM))
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][Calibration][Rig] Rejected: actual bird collision radius is unavailable."));
		return;
	}
	Preset.BirdCollisionRadiusCM = ResolvedBirdCollisionRadiusCM;
	AddTickPrerequisiteActor(SlingshotSystem.Get());
	SlingshotSystem->BuildCalibrationReachEnvelopes(ReachEnvelopes);
	SlingshotSystem->OnCalibrationLaunchRecorded().AddUObject(
		this,
		&AABTSSlingshotSatelliteCalibrationRig::HandleLaunchRecorded);
	GravitySnapshot.PrimaryCenterWorld = PrimaryPlanet->GetPlanetCenterWorld();
	GravitySnapshot.PrimaryRadiusCM = PrimaryPlanet->GetPlanetRadiusCM();
	GravitySnapshot.PrimarySurfaceGravityCMPerSec2 = 980.0f;
	GravitySnapshot.SatelliteCenterWorld = Satellite->GetPlanetCenterWorld();
	GravitySnapshot.SatelliteRadiusCM = Satellite->GetPlanetRadiusCM();
	GravitySnapshot.SatelliteSurfaceGravityCMPerSec2 =
		Satellite->GetSurfaceGravityAccelerationCMPerSec2();
	GravitySnapshot.FlightAirDragPerSecond =
		LaunchProfileCatalog.FlightAirDragPerSecond;
	GravitySnapshot.bSatelliteGravityEnabled = Satellite->bGravityEnabled;
	GravitySnapshotHash =
		FABTSSlingshotSatelliteCalibrationModel::ComputeGravitySnapshotHash(
			GravitySnapshot);
	SatellitePracticePresetHash =
		FABTSSlingshotSatelliteCalibrationModel::ComputeSatellitePracticePresetHash(
			Preset);
	const bool bReachTargetsReady = SpawnReachTargets();
	const bool bSatelliteTargetReady = SpawnSatelliteTarget();
	if (bSatelliteTargetReady)
	{
		RunSweep();
	}
	bReady = ReachEnvelopes.Num() == 3
		&& bReachTargetsReady
		&& bSatelliteTargetReady
		&& TargetProxies.Num() == 7
		&& LaunchProfileHash != 0
		&& GravitySnapshotHash != 0
		&& SatellitePracticePresetHash != 0;
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][Calibration][Ready] Ready=%d ReinforcedFrame=1 Targets=%d BirdCollisionRadius=%.1f LaunchProfileHash=%llu BaselineGravitySnapshotHash=%llu SatellitePracticePresetHash=%llu SweepPassed=%d"),
		bReady ? 1 : 0,
		TargetProxies.Num(),
		Preset.BirdCollisionRadiusCM,
		LaunchProfileHash,
		GravitySnapshotHash,
		SatellitePracticePresetHash,
		SweepSummary.bPassed ? 1 : 0);
}

void AABTSSlingshotSatelliteCalibrationRig::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Satellite.IsValid()) return;
	const int32 GravityOverride =
		ABTSSlingshotCalibrationRigPrivate::CVarSatelliteGravity.GetValueOnGameThread();
	Satellite->bGravityEnabled = GravityOverride < 0
		? GravitySnapshot.bSatelliteGravityEnabled
		: GravityOverride != 0;
	UpdateActualLaunchTargetSweep();
}

void AABTSSlingshotSatelliteCalibrationRig::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	if (SlingshotSystem.IsValid())
	{
		SlingshotSystem->OnCalibrationLaunchRecorded().RemoveAll(this);
		// The Rig is the unique owner of this narrow presentation context. Clear
		// unconditionally so teardown order cannot leave a stale weak target.
		SlingshotSystem->ClearSatellitePracticeTarget(nullptr);
		RemoveTickPrerequisiteActor(SlingshotSystem.Get());
	}
	if (Satellite.IsValid())
	{
		Satellite->bGravityEnabled = GravitySnapshot.bSatelliteGravityEnabled;
	}
	for (AABTSCalibrationTargetProxy* Proxy : TargetProxies)
	{
		if (IsValid(Proxy) && !Proxy->IsActorBeingDestroyed())
		{
			Proxy->Destroy();
		}
	}
	TargetProxies.Reset();
	SatelliteTargetProxy = nullptr;
	Super::EndPlay(EndPlayReason);
}

bool AABTSSlingshotSatelliteCalibrationRig::IsSatelliteGravityEnabled() const
{
	return Satellite.IsValid() && Satellite->bGravityEnabled;
}

AABTSCalibrationTargetProxy*
AABTSSlingshotSatelliteCalibrationRig::SpawnTarget(
	const FName TargetId,
	const FVector& WorldLocation,
	const float RadiusCM,
	const FLinearColor& Color)
{
	if (GetWorld() == nullptr) return nullptr;
	AABTSCalibrationTargetProxy* Proxy =
		GetWorld()->SpawnActorDeferred<AABTSCalibrationTargetProxy>(
			AABTSCalibrationTargetProxy::StaticClass(),
			FTransform(FQuat::Identity, WorldLocation),
			this,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Proxy == nullptr) return nullptr;
	Proxy->Configure(TargetId, RadiusCM, Color);
	UGameplayStatics::FinishSpawningActor(
		Proxy,
		FTransform(FQuat::Identity, WorldLocation));
	TargetProxies.Add(Proxy);
	return Proxy;
}

AABTSCalibrationTargetProxy*
AABTSSlingshotSatelliteCalibrationRig::SpawnCubeTarget(
	const FName TargetId,
	const FTransform& WorldTransform,
	const float HalfExtentCM,
	const FLinearColor& Color)
{
	if (GetWorld() == nullptr) return nullptr;
	AABTSCalibrationTargetProxy* Proxy =
		GetWorld()->SpawnActorDeferred<AABTSCalibrationTargetProxy>(
			AABTSCalibrationTargetProxy::StaticClass(),
			WorldTransform,
			this,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Proxy == nullptr) return nullptr;
	Proxy->ConfigureCube(TargetId, HalfExtentCM, Color);
	UGameplayStatics::FinishSpawningActor(Proxy, WorldTransform);
	TargetProxies.Add(Proxy);
	return Proxy;
}

bool AABTSSlingshotSatelliteCalibrationRig::SpawnReachTargets()
{
	if (!PrimaryPlanet.IsValid() || ReachEnvelopes.Num() != 3) return false;
	const float PrimaryRadiusCM = PrimaryPlanet->GetPlanetRadiusCM();
	for (const FABTSM6ReachEnvelope& Envelope : ReachEnvelopes)
	{
		const float ReachValues[] =
		{
			Envelope.ComfortableReachCM,
			Envelope.MaximumReachCM
		};
		for (int32 MarkerIndex = 0; MarkerIndex < UE_ARRAY_COUNT(ReachValues); ++MarkerIndex)
		{
			const bool bMaximum = MarkerIndex == 1;
			const float ArcRadians = FMath::Clamp(
				ReachValues[MarkerIndex] / FMath::Max(PrimaryRadiusCM, 1.0f),
				0.0f,
				PI * 0.95f);
			const FVector TargetDirection =
				(StartUnitDirection * FMath::Cos(ArcRadians)
					+ CalibrationForward * FMath::Sin(ArcRadians)).GetSafeNormal();
			FVector SurfaceWorld;
			FVector SurfaceNormal;
			float SurfaceRadiusCM = 0.0f;
			int32 CellId = INDEX_NONE;
			if (!PrimaryPlanet->QuerySurface(
				TargetDirection,
				SurfaceWorld,
				SurfaceNormal,
				SurfaceRadiusCM,
				CellId))
			{
				return false;
			}
			const FString TargetName = FString::Printf(
				TEXT("Range.%s.%s"),
				ABTSSlingshotCalibrationRigPrivate::TierLabel(Envelope.Tier),
				bMaximum ? TEXT("Maximum") : TEXT("Comfortable"));
			if (SpawnTarget(
				FName(*TargetName),
				SurfaceWorld + SurfaceNormal
					* (Preset.RangeTargetProxyRadiusCM + 30.0f),
				Preset.RangeTargetProxyRadiusCM,
				ABTSSlingshotCalibrationRigPrivate::TierColor(
					Envelope.Tier, bMaximum)) == nullptr)
			{
				return false;
			}
		}
	}
	return true;
}

bool AABTSSlingshotSatelliteCalibrationRig::SpawnSatelliteTarget()
{
	FTransform TargetWorldTransform;
	FString FailureReason;
	if (!FABTSSlingshotSatelliteCalibrationModel::BuildSatelliteTargetWorldTransform(
		ReinforcedLaunchFrame.RestPouchWorldLocation,
		GravitySnapshot,
		Preset,
		TargetWorldTransform,
		&FailureReason))
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][Calibration][SatelliteTarget] Rejected Reason=%s"),
			*FailureReason);
		return false;
	}
	SatelliteTargetProxy = SpawnCubeTarget(
		TEXT("Satellite.Backside"),
		TargetWorldTransform,
		Preset.TargetProxyRadiusCM,
		FLinearColor(1.0f, 0.12f, 0.72f, 1.0f));
	if (SatelliteTargetProxy == nullptr) return false;
	SatelliteTargetProxy->AttachToActor(
		Satellite.Get(),
		FAttachmentTransformRules::KeepWorldTransform);
	SlingshotSystem->ConfigureSatellitePracticeTarget(
		*Satellite.Get(),
		*SatelliteTargetProxy,
		SatelliteTargetProxy->GetTargetHalfExtentCM(),
		Preset.IntegrationStepSeconds,
		Preset.MaximumFlightSeconds);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][Calibration][E5] Spawned=1 Shape=Cube Center=%s HalfExtent=%.1f SurfaceGap=%.1f"),
		*TargetWorldTransform.GetLocation().ToCompactString(),
		Preset.TargetProxyRadiusCM,
		Preset.TargetSatelliteClearanceCM);
	return true;
}

void AABTSSlingshotSatelliteCalibrationRig::RunSweep()
{
	if (!PrimaryPlanet.IsValid() || SatelliteTargetProxy == nullptr) return;
	FABTSCalibrationScenario Scenario;
	Scenario.LaunchWorldLocation =
		ReinforcedLaunchFrame.RestPouchWorldLocation;
	Scenario.LaunchFrame = ReinforcedLaunchFrame;
	Scenario.TargetWorldLocation = SatelliteTargetProxy->GetActorLocation();
	Scenario.TargetWorldTransform = SatelliteTargetProxy->GetActorTransform();
	Scenario.TargetWorldTransform.SetScale3D(FVector::OneVector);
	Scenario.TargetHalfExtentCM =
		SatelliteTargetProxy->GetTargetHalfExtentCM();
	Scenario.TargetProxyRadiusCM = SatelliteTargetProxy->GetTargetRadiusCM();
	Scenario.Gravity = GravitySnapshot;
	SweepSummary =
		FABTSSlingshotSatelliteCalibrationModel::RunSuccessIslandSweep(
			Scenario,
			LaunchProfileCatalog,
			Preset);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][Calibration][Sweep] Tier=Reinforced Input=ActualM6Pouch Samples=%d ReachablePulls=%d CertifiedPulls=%d GravityOnHits=%d SatelliteBodyHits=%d PrimaryBodyHits=%d Timeouts=%d GravityDependentHits=%d LargestIsland=%d Pull=[%.3f,%.3f] AimInPlaneCM=[%.1f,%.1f] AimNeighbors=%d PullNeighbors=%d GravityOnBestClearance=%.1f BestAimCM=(%.1f,%.1f) BestPull=%.3f GravityOffMinMiss=%.1f SimpleFullPowerHits=%d OutsidePullHits=%d Passed=%d ResultHash=%llu LaunchRadius=%.1f SatelliteRadiusFromPrimary=%.1f TargetRadiusFromSatellite=%.1f"),
		SweepSummary.ReinforcedSampleCount,
		SweepSummary.ReinforcedReachablePullSamples,
		SweepSummary.ReinforcedCertifiedPullSamples,
		SweepSummary.ReinforcedGravityOnHits,
		SweepSummary.ReinforcedSatelliteBodyHits,
		SweepSummary.ReinforcedPrimaryBodyHits,
		SweepSummary.ReinforcedTimeouts,
		SweepSummary.GravityDependentHits,
		SweepSummary.LargestSuccessIslandSamples,
		SweepSummary.SuccessPullMinimum,
		SweepSummary.SuccessPullMaximum,
		SweepSummary.SuccessAimInPlaneMinimumCM,
		SweepSummary.SuccessAimInPlaneMaximumCM,
		SweepSummary.bIslandSpansAimNeighbors ? 1 : 0,
		SweepSummary.bIslandSpansPullNeighbors ? 1 : 0,
		SweepSummary.MinimumGravityOnTargetClearanceCM,
		SweepSummary.BestGravityOnAimInPlaneCM,
		SweepSummary.BestGravityOnAimOutOfPlaneCM,
		SweepSummary.BestGravityOnPullAlpha,
		SweepSummary.MinimumGravityOffMissCM,
		SweepSummary.SimpleFullPowerHits,
		SweepSummary.ReinforcedOutsideCertifiedPullHits,
		SweepSummary.bPassed ? 1 : 0,
		SweepSummary.ResultHash,
		FVector::Distance(
			Scenario.LaunchWorldLocation,
			Scenario.Gravity.PrimaryCenterWorld),
		FVector::Distance(
			Scenario.Gravity.SatelliteCenterWorld,
			Scenario.Gravity.PrimaryCenterWorld),
		FVector::Distance(
			Scenario.TargetWorldLocation,
			Scenario.Gravity.SatelliteCenterWorld));
}

void AABTSSlingshotSatelliteCalibrationRig::UpdateActualLaunchTargetSweep()
{
	if (!SlingshotSystem.IsValid() || !Satellite.IsValid()) return;
	FABTSM6LaunchCalibrationTelemetry Telemetry;
	FVector BirdWorldLocation;
	bool bHasCurrentSatelliteBodyEvidence = false;
	bool bHasCurrentSatelliteE5Evidence = false;
	if (!SlingshotSystem->CopyActiveCalibrationLaunchSample(
		Telemetry,
		BirdWorldLocation,
		&bHasCurrentSatelliteBodyEvidence,
		&bHasCurrentSatelliteE5Evidence))
	{
		bHasPreviousActiveBirdLocation = false;
		ActiveSweepSequence = 0;
		return;
	}
	if (ActiveSweepSequence != Telemetry.Sequence)
	{
		ActiveSweepSequence = Telemetry.Sequence;
		PreviousActiveBirdLocation = BirdWorldLocation;
		bHasPreviousActiveBirdLocation = true;
		return;
	}
	if (!bHasPreviousActiveBirdLocation
		|| (Telemetry.bHitSatelliteBodyFirst
			&& !bHasCurrentSatelliteBodyEvidence)
		|| (Telemetry.HitTargetId == TEXT("Satellite.Backside")
			&& !bHasCurrentSatelliteE5Evidence))
	{
		if (Telemetry.HitTargetId == TEXT("Satellite.Backside")
			&& SatelliteTargetProxy != nullptr)
		{
			SatelliteTargetProxy->MarkHit();
		}
		PreviousActiveBirdLocation = BirdWorldLocation;
		bHasPreviousActiveBirdLocation = true;
		return;
	}

	float EarliestRangeAlpha = BIG_NUMBER;
	AABTSCalibrationTargetProxy* EarliestRangeTarget = nullptr;
	for (AABTSCalibrationTargetProxy* Proxy : TargetProxies)
	{
		if (Proxy == nullptr || Proxy == SatelliteTargetProxy) continue;
		float TargetAlpha = BIG_NUMBER;
		if (ABTSSweptCollision::SegmentSphereFirstAlpha(
			PreviousActiveBirdLocation,
			BirdWorldLocation,
			Proxy->GetActorLocation(),
			Proxy->GetTargetRadiusCM() + Preset.BirdCollisionRadiusCM,
			TargetAlpha)
			&& TargetAlpha < EarliestRangeAlpha)
		{
			EarliestRangeAlpha = TargetAlpha;
			EarliestRangeTarget = Proxy;
		}
	}
	float SatelliteTargetAlpha = BIG_NUMBER;
	const bool bSatelliteTargetHit =
		SatelliteTargetProxy != nullptr
		&& ABTSSweptCollision::SegmentExpandedOrientedBoxFirstAlpha(
			PreviousActiveBirdLocation,
			BirdWorldLocation,
			SatelliteTargetProxy->GetActorTransform(),
			SatelliteTargetProxy->GetTargetHalfExtentCM(),
			Preset.BirdCollisionRadiusCM,
			SatelliteTargetAlpha);
	float SatelliteAlpha = BIG_NUMBER;
	const bool bSatelliteBodyHit =
		ABTSSweptCollision::SegmentSphereFirstAlpha(
			PreviousActiveBirdLocation,
			BirdWorldLocation,
			Satellite->GetPlanetCenterWorld(),
			Satellite->GetPlanetRadiusCM() + Preset.BirdCollisionRadiusCM,
			SatelliteAlpha);
	const bool bSatelliteTargetWins =
		bSatelliteTargetHit
		&& (!bSatelliteBodyHit
			|| SatelliteTargetAlpha
				<= SatelliteAlpha + KINDA_SMALL_NUMBER);
	if (bSatelliteBodyHit && !bSatelliteTargetWins)
	{
		SlingshotSystem->NotifyCalibrationTargetEvent(
			TEXT("Satellite.Body"), true, true);
	}
	else if (bSatelliteTargetWins)
	{
		SatelliteTargetProxy->MarkHit();
		SlingshotSystem->NotifyCalibrationTargetEvent(
			SatelliteTargetProxy->GetTargetId(),
			false,
			true);
	}
	else if (bHasCurrentSatelliteE5Evidence)
	{
		// A Chaos solver may stop the body within contact tolerance just before
		// the analytical expanded boundary. Preserve the real blocking contact
		// as a deterministic fallback after the exact sweep had first refusal.
		SatelliteTargetProxy->MarkHit();
		SlingshotSystem->NotifyCalibrationTargetEvent(
			SatelliteTargetProxy->GetTargetId(),
			false,
			true);
	}
	else if (bHasCurrentSatelliteBodyEvidence)
	{
		SlingshotSystem->NotifyCalibrationTargetEvent(
			TEXT("Satellite.Body"),
			true,
			true);
	}
	else if (EarliestRangeTarget)
	{
		EarliestRangeTarget->MarkHit();
		SlingshotSystem->NotifyCalibrationTargetEvent(
			EarliestRangeTarget->GetTargetId(), false);
	}
	PreviousActiveBirdLocation = BirdWorldLocation;
	bHasPreviousActiveBirdLocation = true;
}

void AABTSSlingshotSatelliteCalibrationRig::HandleLaunchRecorded(
	const FABTSM6LaunchCalibrationTelemetry& Telemetry)
{
	LastLaunchTelemetry = Telemetry;
	bHasLastLaunchTelemetry = true;
}
