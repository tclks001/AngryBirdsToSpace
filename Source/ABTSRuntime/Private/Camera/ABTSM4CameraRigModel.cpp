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
	(void)bEscapingWithAlternateCandidate;
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

		// When this optional solver is enabled, never rate-limit a user-requested
		// zoom-out or a safe alternate candidate.
		DistanceCM = HardSafeDistance;
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
		}
		DistanceCM = HardSafeDistance;
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

FVector ABTSM4CameraRigModel::UpdateSphericalPivot(
	const FVector& CurrentPivot,
	const FVector& TargetPivot,
	const FVector& PlanetCenter,
	const float DeltaSeconds,
	const float FollowSpeed,
	const float MaxLagCM,
	const float GroundedTangentialDeadZoneCM,
	const bool bApplyGroundedTangentialDeadZone)
{
	const FVector CurrentOffset = CurrentPivot - PlanetCenter;
	const FVector TargetOffset = TargetPivot - PlanetCenter;
	const float CurrentRadius = CurrentOffset.Size();
	const float TargetRadius = TargetOffset.Size();
	if (CurrentRadius <= SMALL_NUMBER || TargetRadius <= SMALL_NUMBER)
	{
		return TargetPivot;
	}

	const float SafeDeltaSeconds = FMath::Max(0.0f, DeltaSeconds);
	const float SmoothingAlpha = 1.0f - FMath::Exp(-FMath::Max(0.0f, FollowSpeed) * SafeDeltaSeconds);
	const FVector CurrentDirection = CurrentOffset / CurrentRadius;
	const FVector TargetDirection = TargetOffset / TargetRadius;
	const float ArcAngleRadians = FMath::Acos(FMath::Clamp(
		FVector::DotProduct(CurrentDirection, TargetDirection),
		-1.0f,
		1.0f));
	const float TangentialErrorCM = ArcAngleRadians * TargetRadius;
	const bool bFollowTangentially = !bApplyGroundedTangentialDeadZone
		|| TangentialErrorCM > FMath::Max(0.0f, GroundedTangentialDeadZoneCM);

	FVector SmoothedDirection = CurrentDirection;
	if (bFollowTangentially && SmoothingAlpha > 0.0f)
	{
		const FQuat ArcRotation = FQuat::FindBetweenNormals(CurrentDirection, TargetDirection);
		SmoothedDirection = FQuat::Slerp(FQuat::Identity, ArcRotation, SmoothingAlpha)
			.RotateVector(CurrentDirection)
			.GetSafeNormal();
	}

	// Radius deliberately has no dead zone. Applying the old full-vector dead
	// zone and then copying TargetRadius produced one visible jump per threshold.
	const float SmoothedRadius = FMath::Lerp(CurrentRadius, TargetRadius, SmoothingAlpha);
	FVector UpdatedPivot = PlanetCenter + SmoothedDirection * SmoothedRadius;
	const FVector RemainingLag = TargetPivot - UpdatedPivot;
	if (MaxLagCM > 0.0f && RemainingLag.SizeSquared() > FMath::Square(MaxLagCM))
	{
		UpdatedPivot = TargetPivot - RemainingLag.GetSafeNormal() * MaxLagCM;
	}
	return UpdatedPivot;
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
