// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/ABTSM4CameraRigModel.h"

void FABTSM4CameraObstructionFilter::Reset(const float InDistanceCM)
{
	DistanceCM = FMath::Max(1.0f, InDistanceCM);
	ObstructionSeconds = 0.0f;
	ClearSeconds = 0.0f;
	Phase = EABTSM4CameraObstructionPhase::Clear;
}

float FABTSM4CameraObstructionFilter::Update(
	const bool bDirectArmObstructed,
	const float SafeDistanceCM,
	const float DesiredDistanceCM,
	const bool bEscapingWithAlternateCandidate,
	const float DeltaSeconds,
	const FABTSM4CameraObstructionFilterSettings& Settings)
{
	const float SafeDesiredDistance = FMath::Max(1.0f, DesiredDistanceCM);
	const float HardSafeDistance = FMath::Clamp(SafeDistanceCM, 1.0f, SafeDesiredDistance);
	const float SafeDeltaSeconds = FMath::Max(0.0f, DeltaSeconds);

	if (bDirectArmObstructed)
	{
		ClearSeconds = 0.0f;
		ObstructionSeconds += SafeDeltaSeconds;
		Phase = ObstructionSeconds >= FMath::Max(0.0f, Settings.EnterDelaySeconds)
			? EABTSM4CameraObstructionPhase::Obstructed
			: EABTSM4CameraObstructionPhase::EnterPending;

		// A hard constraint is never interpolated through. An alternate candidate
		// may expand only as its swept transition becomes safe.
		if (HardSafeDistance < DistanceCM)
		{
			DistanceCM = HardSafeDistance;
		}
		else if (bEscapingWithAlternateCandidate)
		{
			DistanceCM = FMath::FInterpConstantTo(
				DistanceCM,
				HardSafeDistance,
				SafeDeltaSeconds,
				FMath::Max(0.0f, Settings.EscapeExpansionSpeedCMPerSecond));
		}
	}
	else
	{
		ObstructionSeconds = 0.0f;
		ClearSeconds += SafeDeltaSeconds;
		const float ExitDelay = FMath::Max(0.0f, Settings.ExitDelaySeconds);
		if (Phase != EABTSM4CameraObstructionPhase::Clear && ClearSeconds < ExitDelay)
		{
			Phase = EABTSM4CameraObstructionPhase::ExitPending;
		}
		else
		{
			Phase = EABTSM4CameraObstructionPhase::Clear;
			DistanceCM = FMath::FInterpConstantTo(
				DistanceCM,
				HardSafeDistance,
				SafeDeltaSeconds,
				FMath::Max(0.0f, Settings.RestoreSpeedCMPerSecond));
		}
	}

	DistanceCM = FMath::Clamp(DistanceCM, 1.0f, HardSafeDistance);
	return DistanceCM;
}

float ABTSM4CameraRigModel::ApplyGamepadResponse(
	const float RawValue,
	const float DeadZone,
	const float Exponent)
{
	const float SafeDeadZone = FMath::Clamp(DeadZone, 0.0f, 0.99f);
	const float Magnitude = FMath::Clamp(FMath::Abs(RawValue), 0.0f, 1.0f);
	if (Magnitude <= SafeDeadZone) return 0.0f;

	const float Normalized = (Magnitude - SafeDeadZone) / (1.0f - SafeDeadZone);
	return FMath::Sign(RawValue) * FMath::Pow(Normalized, FMath::Max(0.01f, Exponent));
}

float ABTSM4CameraRigModel::ComputeSafeSweepDistance(
	const float DesiredDistanceCM,
	const bool bBlockingHit,
	const bool bStartPenetrating,
	const float HitDistanceCM,
	const float CollisionSafetyMarginCM)
{
	const float SafeDesiredDistance = FMath::Max(1.0f, DesiredDistanceCM);
	if (!bBlockingHit) return SafeDesiredDistance;
	if (bStartPenetrating) return 1.0f;
	return FMath::Clamp(
		HitDistanceCM - FMath::Max(0.0f, CollisionSafetyMarginCM),
		1.0f,
		SafeDesiredDistance);
}

const TCHAR* ABTSM4CameraRigModel::LexToString(const EABTSM4CameraObstructionPhase Phase)
{
	switch (Phase)
	{
	case EABTSM4CameraObstructionPhase::Clear: return TEXT("Clear");
	case EABTSM4CameraObstructionPhase::EnterPending: return TEXT("EnterPending");
	case EABTSM4CameraObstructionPhase::Obstructed: return TEXT("Obstructed");
	case EABTSM4CameraObstructionPhase::ExitPending: return TEXT("ExitPending");
	default: return TEXT("Unknown");
	}
}
