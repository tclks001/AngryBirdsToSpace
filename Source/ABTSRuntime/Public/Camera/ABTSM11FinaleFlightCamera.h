// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Camera/ABTSM11FinaleCameraDirector.h"
#include "Camera/CameraActor.h"
#include "CoreMinimal.h"
#include "ABTSM11FinaleFlightCamera.generated.h"

/** One finite, roll-stable camera frame derived from an authority trajectory sample. */
struct ABTSRUNTIME_API FABTSM11FinaleFlightCameraFrame
{
	FVector TrajectoryForward = FVector::ForwardVector;
	FVector TransportedUp = FVector::UpVector;
	FTransform DesiredTransform = FTransform::Identity;

	bool IsUsable() const;
};

/** Tunable, renderer-independent envelope for the Assist1 single encounter. */
struct ABTSRUNTIME_API FABTSM11FinaleCameraM2Settings
{
	double MaximumRetreatCM = 4500.0;
	double CruiseLeadInStartFraction = 0.15;
	double CruiseLeadInBlendFraction = 0.35;
	double ApproachBrakeStartFraction = 0.65;
	double ClosestRetreatFraction = 0.10;
	double PeriapsisReleaseFraction = 0.80;
	double MinimumForegroundBirdDistanceCM = 4000.0;
	double TransitCruiseFarOffsetRadii = 3.00;
	double TransitEntryOffsetRadii = 1.35;
	double TransitClosestOffsetRadii = 0.55;
	double TransitExitOffsetRadii = 2.50;
	double TransitVerticalOffsetRadii = -0.28;
	double TransitExitProgressFraction = 1.00;
	double BaselineFovDegrees = 50.0;
	double ClosestFovDegrees = 30.0;
	double PeriapsisFovRestoreFraction = 0.80;

	bool IsUsable() const;
};

/** Per-frame values retained for observation without driving authority state. */
struct ABTSRUNTIME_API FABTSM11FinaleCameraM2Diagnostics
{
	double DirectorBlendAlpha = 0.0;
	double RetreatAlpha = 0.0;
	double TransitScreenXInTargetRadii = -3.00;
	double DirectedFovDegrees = 50.0;

	bool IsUsable() const;
};

namespace ABTSM11FinaleFlightCameraMath
{
	/**
	 * Builds a camera frame from the authority trajectory tangent.
	 *
	 * Up is initialized from PreferredUp, then parallel transported from the
	 * preceding trajectory frame. This keeps the view roll-stable without
	 * treating any World Actor or movement component as trajectory authority.
	 */
	ABTSRUNTIME_API bool BuildDesiredFrame(
		const FVector& TargetPosition,
		const FVector& TrajectoryTangent,
		const FVector& PreferredUp,
		const FVector& PreviousForward,
		const FVector& PreviousUp,
		bool bHasPreviousFrame,
		double FollowDistanceCM,
		double FollowHeightCM,
		double LookAheadDistanceCM,
		double LookTargetHeightCM,
		FABTSM11FinaleFlightCameraFrame& OutFrame);

	/** Builds the M2 Assist1 dual-subject frame without mutating authority data. */
	ABTSRUNTIME_API bool BuildM2Assist1Frame(
		const FABTSM11FinaleFlightCameraFrame& BaselineFrame,
		const FVector& BirdPosition,
		const FABTSM11FinaleCameraDirectorSample& DirectorSample,
		const FABTSM11FinaleCameraM2Settings& Settings,
		FTransform& OutDirectedTransform,
		FABTSM11FinaleCameraM2Diagnostics& OutDiagnostics);

	/** Keeps the planet anchored after the camera location has been smoothed. */
	ABTSRUNTIME_API bool BuildM2PlanetAnchoredRotation(
		const FVector& CameraLocation,
		const FVector& PreferredUp,
		const FVector& TargetCenter,
		FQuat& OutRotation);
}

/**
 * M11-only deterministic-flight camera.
 *
 * The interaction system feeds this Actor the sampled authority position and
 * velocity each frame. It never reads Chaos or bird movement velocity.
 */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM11FinaleFlightCamera : public ACameraActor
{
	GENERATED_BODY()

public:
	AABTSM11FinaleFlightCamera();

	bool BeginAuthorityFollow(
		const FVector& TargetPosition,
		const FVector& TrajectoryTangent,
		const FVector& PreferredUp,
		const FTransform& InitialViewTransform);
	bool UpdateAuthoritySample(
		const FVector& TargetPosition,
		const FVector& TrajectoryTangent,
		const FVector& PreferredUp,
		const FABTSM11FinaleCameraDirectorSample* DirectorSample,
		float DeltaSeconds);
	void ResetAuthorityFollow();

	bool IsAuthorityFollowActive() const
	{
		return bAuthorityFollowActive;
	}
	const FVector& GetLastAuthorityForward() const
	{
		return LastAuthorityForward;
	}
	const FVector& GetLastTransportedUp() const
	{
		return LastTransportedUp;
	}
	bool IsM2DirectorFrozenEnabled() const
	{
		return bM2DirectorFrozenEnabled;
	}
	double GetLastM2BlendAlpha() const { return LastM2BlendAlpha; }
	double GetLastM2RetreatAlpha() const { return LastM2RetreatAlpha; }
	double GetLastM2TransitScreenXInTargetRadii() const
	{
		return LastM2TransitScreenXInTargetRadii;
	}
	EABTSM11FinaleCameraStage GetLastDirectorStage() const
	{
		return LastDirectorStage;
	}

private:
	bool BuildAuthorityFrame(
		const FVector& TargetPosition,
		const FVector& TrajectoryTangent,
		const FVector& PreferredUp,
		FABTSM11FinaleFlightCameraFrame& OutFrame) const;

	/** Distance behind the authority sample, measured along its trajectory tangent. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera",
		meta = (ClampMin = "100.0", UIMin = "300.0", UIMax = "4000.0", Units = "cm"))
	double FollowDistanceCM = 920.0;

	/** Offset along the transported Up vector. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2000.0", Units = "cm"))
	double FollowHeightCM = 310.0;

	/** Look-ahead along the authority tangent. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "3000.0", Units = "cm"))
	double LookAheadDistanceCM = 80.0;

	/** Target lift along transported Up. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera",
		meta = (UIMin = "-500.0", UIMax = "1000.0", Units = "cm"))
	double LookTargetHeightCM = 80.0;

	/** Exponential location and rotation response in supplied presentation time. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera",
		meta = (ClampMin = "0.0", UIMin = "1.0", UIMax = "20.0"))
	double FollowLagSpeed = 7.0;

	/** Legacy and non-M2 field of view. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera",
		meta = (ClampMin = "20.0", ClampMax = "120.0", UIMin = "35.0", UIMax = "70.0"))
	double BaselineFovDegrees = 50.0;

	/** Maximum radial pull-out across the late-Approach/Periapsis envelope. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera|M2",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "30000.0", Units = "cm"))
	double M2MaximumRetreatCM = 4500.0;

	/** Assist1 Cruise fraction at which the planet reveal begins. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera|M2",
		meta = (ClampMin = "0.0", ClampMax = "0.9", UIMin = "0.0", UIMax = "0.6"))
	double M2CruiseLeadInStartFraction = 0.15;

	/** Cruise fraction used to blend from legacy chase to dual-subject framing. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera|M2",
		meta = (ClampMin = "0.05", ClampMax = "0.9", UIMin = "0.1", UIMax = "0.6"))
	double M2CruiseLeadInBlendFraction = 0.35;

	/** Approach fraction at which the pull-out begins braking into Closest. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera|M2",
		meta = (ClampMin = "0.0", ClampMax = "0.95", UIMin = "0.4", UIMax = "0.9"))
	double M2ApproachBrakeStartFraction = 0.65;

	/** Fraction of maximum pull-out already established at Closest. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera|M2",
		meta = (ClampMin = "0.0", ClampMax = "0.5", UIMin = "0.0", UIMax = "0.25"))
	double M2ClosestRetreatFraction = 0.10;

	/** Periapsis fraction over which the remaining pull-out is released. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera|M2",
		meta = (ClampMin = "0.1", ClampMax = "1.0", UIMin = "0.4", UIMax = "1.0"))
	double M2PeriapsisReleaseFraction = 0.80;

	/** Minimum camera-to-bird distance during the foreground transit. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera|M2",
		meta = (ClampMin = "500.0", UIMin = "1000.0", UIMax = "12000.0", Units = "cm"))
	double M2MinimumForegroundBirdDistanceCM = 4000.0;

	/** Full-director Cruise begins this many target radii outside screen-left. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera|M2",
		meta = (ClampMin = "1.5", ClampMax = "5.0", UIMin = "2.0", UIMax = "4.0"))
	double M2TransitCruiseFarOffsetRadii = 3.00;

	/** Approach starts this many target radii screen-left of the planet. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera|M2",
		meta = (ClampMin = "1.0", ClampMax = "3.0", UIMin = "1.0", UIMax = "2.0"))
	double M2TransitEntryOffsetRadii = 1.35;

	/** Bird offset at physical Closest; positive places it past planet centre. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera|M2",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "0.8"))
	double M2TransitClosestOffsetRadii = 0.55;

	/** Periapsis settles this many target radii screen-right of the planet. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera|M2",
		meta = (ClampMin = "1.0", ClampMax = "3.0", UIMin = "1.0", UIMax = "2.0"))
	double M2TransitExitOffsetRadii = 2.50;

	/** Vertical foreground track in target radii; negative is screen-lower. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera|M2",
		meta = (ClampMin = "-0.8", ClampMax = "0.8", UIMin = "-0.5", UIMax = "0.5"))
	double M2TransitVerticalOffsetRadii = -0.28;

	/** Periapsis fraction by which the foreground transit reaches screen-right. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera|M2",
		meta = (ClampMin = "0.5", ClampMax = "1.0", UIMin = "0.6", UIMax = "1.0"))
	double M2TransitExitProgressFraction = 1.00;

	/** Narrowest field of view at physical Closest. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera|M2",
		meta = (ClampMin = "20.0", ClampMax = "50.0", UIMin = "25.0", UIMax = "45.0"))
	double M2ClosestFovDegrees = 30.0;

	/** Periapsis fraction over which the lens restores the baseline FOV. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera|M2",
		meta = (ClampMin = "0.25", ClampMax = "1.0", UIMin = "0.5", UIMax = "1.0"))
	double M2PeriapsisFovRestoreFraction = 0.80;

	/** Presentation response while the M2 blend is non-zero. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera|M2",
		meta = (ClampMin = "0.0", UIMin = "15.0", UIMax = "90.0"))
	double M2FollowLagSpeed = 60.0;

	FVector LastAuthorityForward = FVector::ForwardVector;
	FVector LastTransportedUp = FVector::UpVector;
	double LastM2BlendAlpha = 0.0;
	double LastM2RetreatAlpha = 0.0;
	double LastM2TransitScreenXInTargetRadii = -3.00;
	EABTSM11FinaleCameraStage LastDirectorStage =
		EABTSM11FinaleCameraStage::PreLaunch;
	bool bAuthorityFollowActive = false;
	bool bM2DirectorFrozenEnabled = false;
};
