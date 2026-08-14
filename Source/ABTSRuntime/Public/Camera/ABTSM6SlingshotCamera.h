// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "ABTSM6SlingshotCamera.generated.h"

class AABTSM25BirdCharacter;
class AABTSM2Planet;
class AABTSM9Satellite;
struct FABTSM6TrajectoryPreview;

UENUM(BlueprintType)
enum class EABTSM9SatelliteFlightCameraIntent : uint8
{
	None,
	SubtleAssist,
	CinematicE5,
	/** Predicted or authoritative contact with the satellite surface, without E5 framing. */
	SurfaceLanding
};

UENUM(BlueprintType)
enum class EABTSM9SatelliteFlightCameraPhase : uint8
{
	PrimaryFollow,
	SatelliteApproach,
	SatelliteOrbit,
	E5Approach,
	E5Impact
};

/** Roll-locked aim and projectile-follow camera used only during one M6 launch. */
UCLASS()
class ABTSRUNTIME_API AABTSM6SlingshotCamera : public ACameraActor
{
	GENERATED_BODY()

public:
	AABTSM6SlingshotCamera();
	virtual void Tick(float DeltaSeconds) override;
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
	void SetAimFrame(const FVector& InCenter, const FVector& InForward, const FVector& InUp);
	/** Copies the authored camera-class defaults used by player input and calibration certification. */
	bool CopyAimFraming(
		float& OutDistanceCM,
		float& OutPitchDegrees,
		float& OutTargetForwardDistanceCM,
		float& OutTargetHeightCM) const;
	/** Returns the exact plane/basis consumed by UpdateAimFromCursor. */
	bool BuildAimInputPlaneBasis(
		const FVector& InCenter,
		const FVector& InForward,
		const FVector& InUp,
		FVector& OutPlaneNormal,
		FVector& OutInPlaneAxis,
		FVector& OutOutOfPlaneAxis) const;
	void FollowBird(AABTSM25BirdCharacter* InBird, AABTSM2Planet* InPlanet);
	void FollowBirdPlanar(AABTSM25BirdCharacter* InBird, const FVector& InPlanarUp);
	/** Preview/Test recorder seam; removes fixture teleport history before capture. */
	bool SnapToPrimaryFollowForSatelliteCapture();
	void ConfigureSatelliteFlightPresentation(
		AABTSM9Satellite* InSatellite,
		AActor* InE5Target);
	/** Freezes camera eligibility from the same immutable prediction drawn while aiming. */
	void LockSatelliteFlightIntent(const FABTSM6TrajectoryPreview& Preview);
	static EABTSM9SatelliteFlightCameraIntent ClassifySatelliteFlightIntent(
		const FABTSM6TrajectoryPreview& Preview);
	/** Pure contact-lead policy shared by runtime and automation. */
	static float ComputeSatelliteSurfaceFrameTarget(
		EABTSM9SatelliteFlightCameraIntent Intent,
		float SurfaceAltitudeCM,
		float SatelliteRadiusCM,
		float PredictedContactSeconds,
		bool bAuthoritativeContact,
		float StartAltitudeMultiplier = 1.0f,
		float LeadSeconds = 1.25f);
	/** Smooth distance envelope used only by a non-landing lunar assist. */
	static float ComputeSatelliteSubtleAssistDistanceWeight(
		float SatelliteDistanceCM,
		float FullInfluenceDistanceCM,
		float ZeroInfluenceDistanceCM);
	/** Stable unit-vector hand-off, including the near-antipodal primary/moon case. */
	static FVector BlendSurfaceUpStable(
		const FVector& PrimaryUp,
		const FVector& SatelliteUp,
		const FVector& PreferredTangent,
		float Alpha);
	/** Caps one presentation-frame step without changing either endpoint. */
	static FVector LimitSurfaceUpAngularStep(
		const FVector& CurrentUp,
		const FVector& DesiredUp,
		const FVector& PreferredTangent,
		float MaximumStepDegrees);
	/** Caps a camera-orientation step so phase changes cannot become one-frame cuts. */
	static FQuat LimitCameraRotationAngularStep(
		const FQuat& CurrentRotation,
		const FQuat& DesiredRotation,
		float MaximumStepDegrees);
	/** Experimental M9 composition: preserves candidate direction while enforcing exact bird distance. */
	static FVector ConstrainCameraToBirdDistance(
		const FVector& CandidateLocation,
		const FVector& BirdLocation,
		float DistanceCM,
		const FVector& FallbackDirection = -FVector::ForwardVector);
	/** Moves a fixed-radius camera only as far around the bird as needed to include the lunar limb. */
	static FVector ConstrainFixedDistanceCameraForSatelliteVisibility(
		const FVector& CandidateLocation,
		const FVector& BirdLocation,
		const FVector& SatelliteCenter,
		float SatelliteRadiusCM,
		float DistanceCM,
		float HorizontalFovDegrees,
		float AspectRatio);
	void ClearSatelliteFlightPresentation();
	void NotifySatelliteE5Hit();
	/** Real collision is stronger than prediction and guarantees a moon-frame hand-off. */
	void NotifySatelliteSurfaceContact();
	/** Locks the last trustworthy tangent briefly so restitution cannot flip the shot 180 degrees. */
	void NotifyBirdImpact();
	void BeginReturnToPrimaryFrame();
	EABTSM9SatelliteFlightCameraPhase GetSatelliteFlightPhase() const
	{
		return SatelliteFlightPhase;
	}
	EABTSM9SatelliteFlightCameraIntent GetSatelliteFlightIntent() const
	{
		return SatelliteFlightIntent;
	}
	float GetSatelliteSurfaceFrameAlpha() const
	{
		return SatelliteSurfaceFrameAlpha;
	}
	bool IsSatelliteSurfaceFrameCommitted() const
	{
		return bSatelliteSurfaceFrameCommitted;
	}

private:
	void UpdateAim(float DeltaSeconds);
	void UpdateFollow(float DeltaSeconds);
	bool UpdateSatelliteFollow(
		AABTSM25BirdCharacter& TargetBird,
		float DeltaSeconds);
	bool BuildPrimaryFollowPose(
		AABTSM25BirdCharacter& TargetBird,
		FVector& OutLocation,
		FQuat& OutRotation);
	FVector ResolveStableFollowForward(
		AABTSM25BirdCharacter& TargetBird,
		const FVector& Up,
		const FVector& CandidateVelocity,
		bool bLockReversal);
	void SetSatelliteFlightPhase(
		EABTSM9SatelliteFlightCameraPhase NewPhase);
	bool BuildAimView(
		const FVector& InCenter,
		const FVector& InForward,
		const FVector& InUp,
		FVector& OutLocation,
		FVector& OutLook,
		FVector& OutScreenUp) const;
	TWeakObjectPtr<AABTSM25BirdCharacter> Bird;
	TWeakObjectPtr<AABTSM2Planet> Planet;
	FVector AimCenter = FVector::ZeroVector;
	FVector AimForward = FVector::ForwardVector;
	FVector AimUp = FVector::UpVector;
	FVector PlanarFollowUp = FVector::UpVector;
	bool bPlanarFollow = false;
	bool bFollowBird = false;
	TWeakObjectPtr<AABTSM9Satellite> Satellite;
	TWeakObjectPtr<AActor> E5Target;
	EABTSM9SatelliteFlightCameraPhase SatelliteFlightPhase =
		EABTSM9SatelliteFlightCameraPhase::PrimaryFollow;
	EABTSM9SatelliteFlightCameraIntent SatelliteFlightIntent =
		EABTSM9SatelliteFlightCameraIntent::None;
	FVector PredictedPeriapsisWorld = FVector::ZeroVector;
	FVector PredictedPeriapsisVelocity = FVector::ZeroVector;
	FVector SatelliteOrbitViewNormal = FVector::ZeroVector;
	bool bSatelliteE5Hit = false;
	bool bSatelliteSurfaceContact = false;
	bool bSatelliteSurfaceFrameLatched = false;
	/** Once true, presentation Up follows the live moon radial without primary-frame lag. */
	bool bSatelliteSurfaceFrameCommitted = false;
	bool bSatelliteE5ApproachLatched = false;
	float SatelliteSurfaceFrameAlpha = 0.0f;
	/** Continuous composition weights prevent camera grammar changes from becoming one-frame cuts. */
	float SatelliteApproachCompositionAlpha = 0.0f;
	float SatelliteE5CompositionAlpha = 0.0f;
	/** Hysteretic near-moon assist state; it must decay back to the primary frame. */
	bool bSatelliteSubtleAssistInsideEnvelope = false;
	float SatelliteSubtleAssistAlpha = 0.0f;
	FVector StableSatellitePresentationUp = FVector::ZeroVector;
	FVector StableFollowForward = FVector::ZeroVector;
	float FollowFacingLockRemainingSeconds = 0.0f;
	/** Return flight must remain in the primary frame until the next launch. */
	bool bForcePrimaryFrameUntilNextFollow = false;

	/** Distance from the fixed slingshot-frame focus to the launch camera. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Aim", meta = (ClampMin = "100.0", UIMin = "300.0", UIMax = "3000.0"))
	float AimDistanceCM = 1500.0f;
	/** Fixed upward viewing pitch relative to the slingshot tangent plane. Does not follow pouch/aim direction. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Aim", meta = (ClampMin = "-10.0", ClampMax = "75.0", UIMin = "0.0", UIMax = "45.0"))
	float AimPitchDegrees = -3.0f;
	/** Fixed forward offset of the look target, measured along the slingshot launch normal. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Aim", meta = (ClampMin = "0.0", UIMin = "100.0", UIMax = "3000.0"))
	float AimTargetForwardDistanceCM = 900.0f;
	/** Fixed radial lift of the look target; changes framing without changing launch orientation. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Aim", meta = (UIMin = "-500.0", UIMax = "1000.0"))
	float AimTargetHeightCM = 245.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Aim", meta = (ClampMin = "0.0", UIMin = "1.0", UIMax = "20.0"))
	float AimCameraBlendSpeed = 10.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Flight")
	float FlightDistanceCM = 920.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Flight")
	float FlightHeightCM = 310.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Flight")
	float FollowSpeed = 7.0f;
	/** Below this tangential speed, collision jitter cannot redefine camera/bird facing. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Flight",
		meta = (ClampMin = "0.0", ClampMax = "1000.0", Units = "cm/s"))
	float FollowFacingMinimumSpeedCMPerSec = 120.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Flight",
		meta = (ClampMin = "0.0", ClampMax = "2.0", Units = "s"))
	float FollowFacingImpactLockSeconds = 0.55f;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "2.0", ClampMax = "10.0"))
	float SatelliteApproachEnterRadiusMultiplier = 5.5f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "2.0", ClampMax = "12.0"))
	float SatelliteApproachExitRadiusMultiplier = 6.3f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "1.1", ClampMax = "5.0"))
	float SatelliteOrbitEnterRadiusMultiplier = 2.3f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "1.1", ClampMax = "6.0"))
	float SatelliteOrbitExitRadiusMultiplier = 2.7f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "100.0", ClampMax = "10000.0", Units = "cm"))
	float SatelliteE5ApproachDistanceCM = 3400.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "100.0", ClampMax = "12000.0", Units = "cm"))
	float SatelliteE5ApproachExitDistanceCM = 3900.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "0.1", ClampMax = "20.0"))
	float SatelliteFollowBlendSpeed = 3.5f;
	/** Time used to move from the outgoing Earth-follow composition into the moon composition. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "0.1", ClampMax = "3.0", Units = "s"))
	float SatelliteApproachCompositionBlendSeconds = 0.9f;
	/** Time used to remove the orbit-side framing and introduce the E5 look-ahead. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "0.1", ClampMax = "3.0", Units = "s"))
	float SatelliteE5CompositionBlendSeconds = 0.65f;
	/** Moon-frame hand-off begins only inside this surface-altitude multiple. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "0.25", ClampMax = "2.0"))
	float SatelliteSurfaceFrameStartAltitudeMultiplier = 1.0f;
	/** A velocity-ray contact inside this time can lead the altitude hand-off. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "0.1", ClampMax = "3.0", Units = "s"))
	float SatelliteSurfaceFrameLeadSeconds = 1.25f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "0.1", ClampMax = "20.0"))
	float SatelliteSurfaceFrameBlendInSpeed = 3.5f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "0.1", ClampMax = "20.0"))
	float SatelliteSurfaceFrameBlendOutSpeed = 5.0f;
	/** Prevents a one-frame reference-frame cut when the satellite first enters view. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "10.0", ClampMax = "180.0", Units = "deg/s"))
	float SatelliteSurfaceUpMaxDegreesPerSecond = 90.0f;
	/** Camera phase transitions remain slower than the moving target and visually continuous. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "10.0", ClampMax = "180.0", Units = "deg/s"))
	float SatelliteTransitionRotationMaxDegreesPerSecond = 45.0f;
	/** Only the primary-to-satellite hand-off pulls farther than ground follow. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "1.0", ClampMax = "1.5"))
	float SatelliteTransitionPullbackMultiplier = 1.12f;
	/** Wider only during the Earth-to-moon hand-off so bird and lunar limb coexist without a finale-wide pullback. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "50.0", ClampMax = "80.0", Units = "deg"))
	float SatelliteTransitionFieldOfViewDegrees = 68.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "0.1", ClampMax = "20.0"))
	float SatelliteFieldOfViewBlendSpeed = 3.5f;
	/** Candidate experiment: keep bird angular size invariant during the cinematic lunar hand-off. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight")
	bool bSatelliteConstantBirdScaleExperiment = true;
	/** Fixed lens used with the constant bird-distance experiment. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "35.0", ClampMax = "70.0", Units = "deg"))
	float SatelliteConstantBirdScaleFieldOfViewDegrees = 50.0f;
	/** Small look-ahead preserves the ground strike grammar while revealing E5. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "0.0", ClampMax = "0.35"))
	float SatelliteE5LookAheadBias = 0.16f;
	/** Ordinary assists remain in the primary frame and cannot roll farther than this. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "0.0", ClampMax = "15.0"))
	float SatelliteSubtleMaximumTiltDegrees = 8.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "1.0", ClampMax = "2.0"))
	float SatelliteSubtlePullbackMultiplier = 1.18f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "0.05", ClampMax = "3.0", Units = "s"))
	float SatelliteSubtleBlendInSeconds = 0.35f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "0.1", ClampMax = "5.0", Units = "s"))
	float SatelliteSubtleBlendOutSeconds = 1.1f;
};

