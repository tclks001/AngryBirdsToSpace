// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Observable phases of the ground-camera obstruction resolver. */
enum class EABTSM4CameraObstructionPhase : uint8
{
	Clear,
	EnterPending,
	Obstructed,
	ExitPending
};

/** Timing settings for the optional obstruction state filter. */
struct ABTSRUNTIME_API FABTSM4CameraObstructionFilterSettings
{
	float EnterDelaySeconds = 0.04f;
	float ExitDelaySeconds = 0.16f;
};

/**
 * Optional spring-arm style distance state. Contraction and expansion are
 * immediate; timing remains observable only for obstruction diagnostics.
 */
struct ABTSRUNTIME_API FABTSM4CameraObstructionFilter
{
	void Reset(float InDistanceCM);

	float Update(
		bool bDirectArmObstructed,
		float SafeDistanceCM,
		float DesiredDistanceCM,
		bool bEscapingWithAlternateCandidate,
		float DeltaSeconds,
		const FABTSM4CameraObstructionFilterSettings& Settings);

	float GetDistanceCM() const { return DistanceCM; }
	EABTSM4CameraObstructionPhase GetPhase() const { return Phase; }
	float GetObstructionSeconds() const { return ObstructionSeconds; }
	float GetClearSeconds() const { return ClearSeconds; }

private:
	float DistanceCM = 0.0f;
	float ObstructionSeconds = 0.0f;
	float ClearSeconds = 0.0f;
	EABTSM4CameraObstructionPhase Phase = EABTSM4CameraObstructionPhase::Clear;
};

/** A rigidly translated orbit pose that clears the authoritative ground surface. */
struct ABTSRUNTIME_API FABTSM4SurfaceSafePose
{
	FVector CameraLocation = FVector::ZeroVector;
	FVector FocusLocation = FVector::ZeroVector;
	float AppliedLiftCM = 0.0f;
	float RawPenetrationCM = 0.0f;
	float TransitionAlpha = 0.0f;
	bool bConstrained = false;
};

namespace ABTSM4CameraRigModel
{
	/** Converts a stick sample to a signed normalized response after dead zone and exponent. */
	ABTSRUNTIME_API float ApplyGamepadResponse(float RawValue, float DeadZone, float Exponent);

	/**
	 * Smooths a planet-relative focus point without quantizing radial motion.
	 * Radius always follows continuously. The optional dead zone applies only to
	 * grounded tangential travel, so airborne ascent/descent cannot staircase.
	 */
	ABTSRUNTIME_API FVector UpdateSphericalPivot(
		const FVector& CurrentPivot,
		const FVector& TargetPivot,
		const FVector& PlanetCenter,
		float DeltaSeconds,
		float FollowSpeed,
		float MaxLagCM,
		float GroundedTangentialDeadZoneCM,
		bool bApplyGroundedTangentialDeadZone);

	/**
	 * Converts a swept sphere result to camera-center distance. The sweep already
	 * accounts for probe radius, so only the explicit safety margin is removed.
	 */
	ABTSRUNTIME_API float ComputeSafeSweepDistance(
		float DesiredDistanceCM,
		bool bBlockingHit,
		bool bStartPenetrating,
		float HitDistanceCM,
		float CollisionSafetyMarginCM);

	/** Pitch-authored composition distance, independent from collision response. */
	ABTSRUNTIME_API float ComputeUpwardFramingDistance(
		float UserOrbitDistanceCM,
		float ElevationDegrees,
		float PullInStartElevationDegrees,
		float FullPullInElevationDegrees,
		float MinimumDistanceScale,
		float& OutPullInAlpha);

	/**
	 * Keeps a camera center above a known surface without shortening or rotating
	 * the requested orbit arm. Camera and virtual focus receive the same lift.
	 */
	ABTSRUNTIME_API bool BuildSurfaceSafeTranslatedPose(
		const FVector& DesiredCameraLocation,
		const FVector& DesiredFocusLocation,
		const FVector& SurfacePoint,
		const FVector& SurfaceOutwardNormal,
		float MinimumCameraCenterClearanceCM,
		float TransitionBandCM,
		FABTSM4SurfaceSafePose& OutPose);

	ABTSRUNTIME_API const TCHAR* LexToString(EABTSM4CameraObstructionPhase Phase);
}
