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

/** Frame-rate-independent settings for the obstruction distance filter. */
struct ABTSRUNTIME_API FABTSM4CameraObstructionFilterSettings
{
	float EnterDelaySeconds = 0.04f;
	float ExitDelaySeconds = 0.16f;
	float RestoreSpeedCMPerSecond = 520.0f;
	float EscapeExpansionSpeedCMPerSecond = 900.0f;
};

/**
 * Spring-arm style distance state. Hard collision contraction is immediate;
 * expansion is monotonic and delayed so edge contacts cannot pump the camera.
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

namespace ABTSM4CameraRigModel
{
	/** Converts a stick sample to a signed normalized response after dead zone and exponent. */
	ABTSRUNTIME_API float ApplyGamepadResponse(float RawValue, float DeadZone, float Exponent);

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

	ABTSRUNTIME_API const TCHAR* LexToString(EABTSM4CameraObstructionPhase Phase);
}
