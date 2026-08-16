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

float ABTSM4CameraRigModel::ComputeUpwardFramingDistance(
	const float UserOrbitDistanceCM,
	const float ElevationDegrees,
	const float PullInStartElevationDegrees,
	const float FullPullInElevationDegrees,
	const float MinimumDistanceScale,
	float& OutPullInAlpha)
{
	const float SafeUserDistanceCM = FMath::Max(1.0f, UserOrbitDistanceCM);
	const float StartElevationDegrees = FMath::Max(
		PullInStartElevationDegrees,
		FullPullInElevationDegrees);
	const float EndElevationDegrees = FMath::Min(
		PullInStartElevationDegrees,
		FullPullInElevationDegrees);
	const float RangeDegrees = FMath::Max(KINDA_SMALL_NUMBER, StartElevationDegrees - EndElevationDegrees);
	const float LinearAlpha = FMath::Clamp(
		(StartElevationDegrees - ElevationDegrees) / RangeDegrees,
		0.0f,
		1.0f);
	OutPullInAlpha = FMath::SmoothStep(0.0f, 1.0f, LinearAlpha);
	const float SafeMinimumScale = FMath::Clamp(MinimumDistanceScale, 0.1f, 1.0f);
	return SafeUserDistanceCM * FMath::Lerp(1.0f, SafeMinimumScale, OutPullInAlpha);
}

bool ABTSM4CameraRigModel::BuildSurfaceSafeTranslatedPose(
	const FVector& DesiredCameraLocation,
	const FVector& DesiredFocusLocation,
	const FVector& SurfacePoint,
	const FVector& SurfaceOutwardNormal,
	const float MinimumCameraCenterClearanceCM,
	const float TransitionBandCM,
	FABTSM4SurfaceSafePose& OutPose)
{
	OutPose = FABTSM4SurfaceSafePose{};
	const FVector SafeNormal = SurfaceOutwardNormal.GetSafeNormal();
	if (DesiredCameraLocation.ContainsNaN()
		|| DesiredFocusLocation.ContainsNaN()
		|| SurfacePoint.ContainsNaN()
		|| SafeNormal.IsNearlyZero()
		|| !FMath::IsFinite(MinimumCameraCenterClearanceCM)
		|| !FMath::IsFinite(TransitionBandCM))
	{
		return false;
	}

	const float CurrentClearanceCM = FVector::DotProduct(
		DesiredCameraLocation - SurfacePoint,
		SafeNormal);
	const float RequiredClearanceCM = FMath::Max(0.0f, MinimumCameraCenterClearanceCM);
	const float RawPenetrationCM = RequiredClearanceCM - CurrentClearanceCM;
	const float SafeTransitionBandCM = FMath::Max(0.0f, TransitionBandCM);
	float AppliedLiftCM = FMath::Max(0.0f, RawPenetrationCM);
	float TransitionAlpha = RawPenetrationCM > 0.0f ? 1.0f : 0.0f;
	if (SafeTransitionBandCM > KINDA_SMALL_NUMBER)
	{
		if (RawPenetrationCM <= -SafeTransitionBandCM)
		{
			AppliedLiftCM = 0.0f;
			TransitionAlpha = 0.0f;
		}
		else if (RawPenetrationCM < SafeTransitionBandCM)
		{
			TransitionAlpha = (RawPenetrationCM + SafeTransitionBandCM)
				/ (2.0f * SafeTransitionBandCM);
			// C1 smooth maximum of RawPenetrationCM and zero. It begins before
			// contact, remains above the hard minimum, and joins the exact hard
			// correction with matching velocity at the far edge of the band.
			AppliedLiftCM = SafeTransitionBandCM * FMath::Square(TransitionAlpha);
		}
	}
	const FVector Translation = SafeNormal * AppliedLiftCM;
	OutPose.CameraLocation = DesiredCameraLocation + Translation;
	OutPose.FocusLocation = DesiredFocusLocation + Translation;
	OutPose.AppliedLiftCM = AppliedLiftCM;
	OutPose.RawPenetrationCM = RawPenetrationCM;
	OutPose.TransitionAlpha = TransitionAlpha;
	OutPose.bConstrained = AppliedLiftCM > KINDA_SMALL_NUMBER;
	return true;
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
