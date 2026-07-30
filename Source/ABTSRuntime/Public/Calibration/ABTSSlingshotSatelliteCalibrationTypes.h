// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Slingshot/ABTSSlingshotTypes.h"
#include "ABTSSlingshotSatelliteCalibrationTypes.generated.h"

/** One deterministic Pull -> launch-speed and input-feel profile. */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM6LaunchProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	EABTSSlingshotTier Tier = EABTSSlingshotTier::Simple;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "100.0", Units = "cm/s"))
	float MinimumSpeedCMPerSec = 900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "100.0", Units = "cm/s"))
	float MaximumSpeedCMPerSec = 2300.0f;

	/** Speed = lerp(Minimum, Maximum, pow(PullAlpha, PowerExponent)). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float PowerExponent = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pull", meta = (ClampMin = "10.0", Units = "cm"))
	float MinimumPullDistanceCM = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pull", meta = (ClampMin = "10.0", Units = "cm"))
	float MaximumPullDistanceCM = 430.0f;

	/** Pull value assigned when the bird first enters this tier's pouch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pull", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InitialPullAlpha = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pull", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float PullPowerWheelStep = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float AimSensitivityScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "20.0", Units = "cm"))
	float MaximumAimPlaneOffsetCM = 260.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reach", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ComfortablePullMinimum = 0.60f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reach", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ComfortablePullMaximum = 0.85f;
};

/** Versioned calibration-only catalog. Space intentionally remains on M11's legacy M6 contract. */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM6LaunchProfileCatalog
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FABTSM6LaunchProfile> Profiles;

	/** Shared flight drag is part of LaunchProfileHash because it changes every reach envelope. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float FlightAirDragPerSecond = 0.08f;

	/**
	 * Runtime snapshots copied from the spawned SlingshotCamera Blueprint.
	 * They stay in the hash so certification uses the real mouse projection
	 * plane, but are deliberately not a second authored parameter source.
	 */
	float AimCameraDistanceCM = 1150.0f;

	float AimCameraPitchDegrees = 18.0f;

	float AimTargetForwardDistanceCM = 900.0f;

	float AimTargetHeightCM = 245.0f;
};

/** Snapshot of the actual Reinforced M6 pouch frame spawned in the calibration world. */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM6CalibrationLaunchFrame
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FVector SlingCenterWorld = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector SlingUpWorld = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly)
	FVector SlingForwardWorld = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly)
	FVector SlingRightWorld = FVector::RightVector;

	/** Normal and orthonormal screen axes of the actual M6 mouse projection plane. */
	UPROPERTY(BlueprintReadOnly)
	FVector AimPlaneNormalWorld = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly)
	FVector AimInPlaneAxisWorld = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly)
	FVector AimOutOfPlaneAxisWorld = FVector::RightVector;

	UPROPERTY(BlueprintReadOnly)
	FVector RestPouchWorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	float BirdInPouchOffsetCM = 20.0f;
};

/** Ideal-sphere reach measurement used by the calibration HUD and future M3 consumers. */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM6ReachEnvelope
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	EABTSSlingshotTier Tier = EABTSSlingshotTier::Simple;

	UPROPERTY(BlueprintReadOnly)
	float ComfortableReachCM = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float MaximumReachCM = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float ComfortableReachPrimaryRadiusRatio = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float MaximumReachPrimaryRadiusRatio = 0.0f;
};

/** Complete measured telemetry for one real M6 launch. */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM6LaunchCalibrationTelemetry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 Sequence = 0;

	UPROPERTY(BlueprintReadOnly)
	EABTSSlingshotTier Tier = EABTSSlingshotTier::Simple;

	uint64 LaunchProfileHash = 0;

	UPROPERTY(BlueprintReadOnly)
	float PullAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	FVector AimPlaneOffsetCM = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector InitialWorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector InitialWorldVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	float InitialSpeedCMPerSec = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float ActualLandingArcLengthCM = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float ActualPathLengthCM = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float ApexAltitudeAbovePrimaryCM = 0.0f;

	/** Time until the first grounded transition, excluding the later settling hold and return. */
	UPROPERTY(BlueprintReadOnly)
	float FlightTimeSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	FVector LandingWorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FName HitTargetId = NAME_None;

	UPROPERTY(BlueprintReadOnly)
	bool bHitTarget = false;

	UPROPERTY(BlueprintReadOnly)
	bool bHitSatelliteBodyFirst = false;
};

/** Local, map-independent V0 practice layout. No absolute world coordinate participates in its hash. */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSSatellitePracticePreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite", meta = (ClampMin = "0.02", ClampMax = "0.5"))
	float SatelliteRadiusPrimaryRatio = 0.125f;

	/** Primary-surface arc from the calibration origin to the satellite anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite", meta = (ClampMin = "1.0", ClampMax = "80.0", Units = "deg"))
	float SatelliteAnchorArcDegrees = 30.0f;

	/** Rotation of the satellite-anchor great-circle direction around the local radial up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite", meta = (ClampMin = "-180.0", ClampMax = "180.0", Units = "deg"))
	float SatelliteAnchorAzimuthDegrees = 0.0f;

	/** Distance from the primary surface to the satellite centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SatelliteCenterClearancePrimaryRatio = 0.125f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float SatelliteSurfaceGravityPrimaryRatio = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	FName TargetBody = TEXT("PracticeSatellite");

	/** Zero faces the calibration launch origin; values above 90 degrees are on the back side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target", meta = (ClampMin = "90.0", ClampMax = "179.0", Units = "deg"))
	float BacksideAngleDeg = 178.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target", meta = (ClampMin = "-180.0", ClampMax = "180.0", Units = "deg"))
	float TargetLocalAzimuthDeg = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target", meta = (ClampMin = "0.0", Units = "cm"))
	float TargetAltitudeAboveSurfaceCM = 560.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target", meta = (ClampMin = "20.0", Units = "cm"))
	float TargetProxyRadiusCM = 420.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target", meta = (ClampMin = "10.0", Units = "cm"))
	float BirdCollisionRadiusCM = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target", meta = (ClampMin = "0.0", Units = "cm"))
	float TargetSatelliteClearanceCM = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Range Targets", meta = (ClampMin = "20.0", Units = "cm"))
	float RangeTargetProxyRadiusCM = 150.0f;

	/** Actual M6 AimPlaneOffset component along SlingUp (orbital plane). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sweep", meta = (ClampMin = "-500.0", ClampMax = "0.0", Units = "cm"))
	float AimInPlaneMinimumCM = -260.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sweep", meta = (ClampMin = "0.0", ClampMax = "500.0", Units = "cm"))
	float AimInPlaneMaximumCM = 260.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sweep", meta = (ClampMin = "5", ClampMax = "161"))
	int32 AimInPlaneSampleCount = 41;

	/** Actual M6 AimPlaneOffset component along SlingRight (out of orbital plane). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sweep", meta = (ClampMin = "-500.0", ClampMax = "0.0", Units = "cm"))
	float AimOutOfPlaneMinimumCM = -80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sweep", meta = (ClampMin = "0.0", ClampMax = "500.0", Units = "cm"))
	float AimOutOfPlaneMaximumCM = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sweep", meta = (ClampMin = "1", ClampMax = "31"))
	int32 AimOutOfPlaneSampleCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sweep", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PullMinimum = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sweep", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PullMaximum = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sweep", meta = (ClampMin = "0.01", ClampMax = "0.2", Units = "s"))
	float IntegrationStepSeconds = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sweep", meta = (ClampMin = "2.0", ClampMax = "60.0", Units = "s"))
	float MaximumFlightSeconds = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sweep", meta = (ClampMin = "1", ClampMax = "100"))
	int32 MinimumSuccessIslandSamples = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sweep", meta = (ClampMin = "0.0", Units = "cm"))
	float GravityOffMinimumMissCM = 60.0f;
};

/** Immutable local two-body snapshot shared by the offline sweep and diagnostics. */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSCalibrationGravitySnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FVector PrimaryCenterWorld = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	float PrimaryRadiusCM = 10000.0f;

	UPROPERTY(BlueprintReadOnly)
	float PrimarySurfaceGravityCMPerSec2 = 980.0f;

	UPROPERTY(BlueprintReadOnly)
	FVector SatelliteCenterWorld = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	float SatelliteRadiusCM = 1250.0f;

	UPROPERTY(BlueprintReadOnly)
	float SatelliteSurfaceGravityCMPerSec2 = 294.0f;

	UPROPERTY(BlueprintReadOnly)
	float FlightAirDragPerSecond = 0.08f;

	UPROPERTY(BlueprintReadOnly)
	bool bSatelliteGravityEnabled = true;
};

UENUM(BlueprintType)
enum class EABTSCalibrationTrajectoryOutcome : uint8
{
	Timeout,
	TargetHit,
	SatelliteBodyHit,
	PrimaryBodyHit
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSCalibrationTrajectoryResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	EABTSCalibrationTrajectoryOutcome Outcome = EABTSCalibrationTrajectoryOutcome::Timeout;

	UPROPERTY(BlueprintReadOnly)
	float FlightTimeSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float PathLengthCM = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float ApexAltitudeAbovePrimaryCM = 0.0f;

	/** Minimum bird-centre clearance outside the expanded target sphere. */
	UPROPERTY(BlueprintReadOnly)
	float ClosestTargetClearanceCM = BIG_NUMBER;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSCalibrationSweepSummary
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 ReinforcedSampleCount = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 ReinforcedReachablePullSamples = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 ReinforcedCertifiedPullSamples = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 ReinforcedGravityOnHits = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 ReinforcedSatelliteBodyHits = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 ReinforcedPrimaryBodyHits = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 ReinforcedTimeouts = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 GravityDependentHits = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 LargestSuccessIslandSamples = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 SimpleFullPowerHits = 0;

	/** Hits at player-reachable pull notches outside the certified 75%-95% band. */
	UPROPERTY(BlueprintReadOnly)
	int32 ReinforcedOutsideCertifiedPullHits = 0;

	UPROPERTY(BlueprintReadOnly)
	float SuccessPullMinimum = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float SuccessPullMaximum = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float SuccessAimInPlaneMinimumCM = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float SuccessAimInPlaneMaximumCM = 0.0f;

	/** Minimum gravity-off miss among samples accepted into the gravity-dependent success set. */
	UPROPERTY(BlueprintReadOnly)
	float MinimumGravityOffMissCM = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float MinimumGravityOnTargetClearanceCM = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float BestGravityOnAimInPlaneCM = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float BestGravityOnAimOutOfPlaneCM = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float BestGravityOnPullAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	bool bIslandSpansAimNeighbors = false;

	UPROPERTY(BlueprintReadOnly)
	bool bIslandSpansPullNeighbors = false;

	UPROPERTY(BlueprintReadOnly)
	bool bPassed = false;

	uint64 ResultHash = 0;
};

/** World-space inputs to the deterministic calibration sweep. */
struct ABTSRUNTIME_API FABTSCalibrationScenario
{
	/** Per-sample bird centre. RunSuccessIslandSweep overwrites this from LaunchFrame. */
	FVector LaunchWorldLocation = FVector::ZeroVector;
	FABTSM6CalibrationLaunchFrame LaunchFrame;
	FVector TargetWorldLocation = FVector::ZeroVector;
	float TargetProxyRadiusCM = 420.0f;
	FABTSCalibrationGravitySnapshot Gravity;
};

/**
 * Pure calibration math. It never reads Actors, collision, PCG output or absolute-world identity.
 * Runtime actors resolve their values once, then pass POD snapshots through this boundary.
 */
struct ABTSRUNTIME_API FABTSSlingshotSatelliteCalibrationModel
{
	static FABTSM6LaunchProfileCatalog MakeCandidateCatalogV0();
	static FABTSSatellitePracticePreset MakeCandidatePracticePresetV0();
	static bool ResolveCatalog(
		const FABTSM6LaunchProfileCatalog& Source,
		FABTSM6LaunchProfileCatalog& OutResolved,
		FString* OutFailureReason = nullptr);
	static const FABTSM6LaunchProfile* FindProfile(
		const FABTSM6LaunchProfileCatalog& ResolvedCatalog,
		EABTSSlingshotTier Tier);
	static float EvaluateLaunchSpeed(const FABTSM6LaunchProfile& Profile, float PullAlpha);
	/**
	 * Converts player-enterable M6 state into the same pouch location and launch
	 * velocity used by UpdatePouchAndPreview/ComputeLaunchVelocity.
	 */
	static bool BuildM6LaunchSample(
		const FABTSM6CalibrationLaunchFrame& LaunchFrame,
		const FABTSM6LaunchProfile& Profile,
		float AimInPlaneOffsetCM,
		float AimOutOfPlaneOffsetCM,
		float PullAlpha,
		FVector& OutBirdWorldLocation,
		FVector& OutInitialWorldVelocity);
	static uint64 ComputeLaunchProfileHash(const FABTSM6LaunchProfileCatalog& ResolvedCatalog);
	static FABTSM6ReachEnvelope EstimateReachEnvelope(
		const FABTSM6LaunchProfile& Profile,
		float PrimaryRadiusCM,
		float PrimarySurfaceGravityCMPerSec2,
		float FlightAirDragPerSecond);

	static uint64 ComputeSatellitePracticePresetHash(const FABTSSatellitePracticePreset& Preset);
	static uint64 ComputeGravitySnapshotHash(const FABTSCalibrationGravitySnapshot& Snapshot);
	static bool BuildSatelliteTargetWorldLocation(
		const FVector& LaunchWorldLocation,
		const FABTSCalibrationGravitySnapshot& Snapshot,
		const FABTSSatellitePracticePreset& Preset,
		FVector& OutTargetWorldLocation,
		FString* OutFailureReason = nullptr);
	static FABTSCalibrationTrajectoryResult IntegrateTrajectory(
		const FABTSCalibrationScenario& Scenario,
		const FVector& InitialWorldVelocity,
		const FABTSSatellitePracticePreset& Preset,
		bool bSatelliteGravityEnabled);
	static FABTSCalibrationSweepSummary RunSuccessIslandSweep(
		const FABTSCalibrationScenario& Scenario,
		const FABTSM6LaunchProfileCatalog& ResolvedCatalog,
		const FABTSSatellitePracticePreset& Preset);
};
