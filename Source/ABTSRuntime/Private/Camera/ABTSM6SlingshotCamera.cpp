// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/ABTSM6SlingshotCamera.h"

#include "ABTSRuntime.h"
#include "Camera/CameraComponent.h"
#include "Planet/ABTSM2Planet.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Slingshot/ABTSM6Types.h"
#include "World/ABTSM9Satellite.h"

namespace ABTSM9SatelliteCameraPrivate
{
	float ComputeGroundFollowBirdDistanceCM(
		const float FlightDistanceCM,
		const float FlightHeightCM)
	{
		return FVector(FlightDistanceCM, 0.0f, FlightHeightCM).Size();
	}

	FVector KeepSatelliteLimbVisible(
		const FVector& CameraLocation,
		const FVector& PrimaryFocus,
		const FVector& SatelliteCenter,
		const float SatelliteRadiusCM,
		const float HorizontalFovDegrees,
		const float AspectRatio)
	{
		const FVector PrimaryDirection =
			(PrimaryFocus - CameraLocation).GetSafeNormal();
		const FVector SatelliteOffset = SatelliteCenter - CameraLocation;
		const float SatelliteDistanceCM = SatelliteOffset.Size();
		const FVector SatelliteDirection = SatelliteOffset.GetSafeNormal();
		if (PrimaryDirection.IsNearlyZero()
			|| SatelliteDirection.IsNearlyZero()
			|| SatelliteDistanceCM <= KINDA_SMALL_NUMBER)
		{
			return PrimaryDirection;
		}

		const float SeparationRadians = FMath::Acos(FMath::Clamp(
			FVector::DotProduct(PrimaryDirection, SatelliteDirection),
			-1.0f,
			1.0f));
		if (SeparationRadians <= KINDA_SMALL_NUMBER) return PrimaryDirection;
		const float AngularRadiusRadians = FMath::Asin(FMath::Clamp(
			SatelliteRadiusCM / SatelliteDistanceCM,
			0.0f,
			0.999f));
		// Reserve a small strip of visible lunar surface instead of merely touching
		// the exact screen edge. Only the minimum necessary correction is applied,
		// so the bird remains the dominant target rather than becoming a finale-wide
		// two-body composition.
		const float HorizontalHalfFovRadians = FMath::DegreesToRadians(
			FMath::Clamp(HorizontalFovDegrees, 10.0f, 170.0f) * 0.5f);
		const float HalfFovRadians = FMath::Atan(
			FMath::Tan(HorizontalHalfFovRadians)
			/ FMath::Max(0.1f, AspectRatio));
		const float VisibleLimbMarginRadians = FMath::DegreesToRadians(3.0f);
		const float MaximumMoonCenterOffsetRadians = FMath::Max(
			HalfFovRadians * 0.92f,
			HalfFovRadians + AngularRadiusRadians - VisibleLimbMarginRadians);
		const float RequiredCorrectionRadians = FMath::Max(
			0.0f,
			SeparationRadians - MaximumMoonCenterOffsetRadians);
		if (RequiredCorrectionRadians <= KINDA_SMALL_NUMBER)
		{
			return PrimaryDirection;
		}
		// Keep the bird inside the central 70% of the vertical field even in the
		// closest pass. If both envelopes cannot fit, bird readability wins.
		const float MaximumBirdOffsetRadians = HalfFovRadians * 0.82f;
		const float CorrectionRadians = FMath::Min(
			RequiredCorrectionRadians,
			MaximumBirdOffsetRadians);
		FVector RotationAxis = FVector::CrossProduct(
			PrimaryDirection,
			SatelliteDirection).GetSafeNormal();
		if (RotationAxis.IsNearlyZero()) return PrimaryDirection;
		return FQuat(RotationAxis, CorrectionRadians)
			.RotateVector(PrimaryDirection).GetSafeNormal();
	}
}

FVector AABTSM6SlingshotCamera::ConstrainCameraToBirdDistance(
	const FVector& CandidateLocation,
	const FVector& BirdLocation,
	const float DistanceCM,
	const FVector& FallbackDirection)
{
	FVector Direction = (CandidateLocation - BirdLocation).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		Direction = FallbackDirection.GetSafeNormal();
	}
	if (Direction.IsNearlyZero()) Direction = -FVector::ForwardVector;
	return BirdLocation + Direction * FMath::Max(1.0f, DistanceCM);
}

FVector AABTSM6SlingshotCamera::ConstrainFixedDistanceCameraForSatelliteVisibility(
	const FVector& CandidateLocation,
	const FVector& BirdLocation,
	const FVector& SatelliteCenter,
	const float SatelliteRadiusCM,
	const float DistanceCM,
	const float HorizontalFovDegrees,
	const float AspectRatio)
{
	const float RadiusCM = FMath::Max(1.0f, DistanceCM);
	FVector CandidateDirection = (CandidateLocation - BirdLocation).GetSafeNormal();
	const FVector BirdToSatellite = SatelliteCenter - BirdLocation;
	const FVector IdealDirection = -BirdToSatellite.GetSafeNormal();
	if (CandidateDirection.IsNearlyZero()) CandidateDirection = IdealDirection;
	if (CandidateDirection.IsNearlyZero()) return CandidateLocation;
	if (IdealDirection.IsNearlyZero())
	{
		return BirdLocation + CandidateDirection * RadiusCM;
	}

	auto FitsView = [&](const FVector& CameraDirection)
	{
		const FVector CameraLocation = BirdLocation + CameraDirection * RadiusCM;
		const FVector BirdDirection = (BirdLocation - CameraLocation).GetSafeNormal();
		const FVector CameraToSatellite = SatelliteCenter - CameraLocation;
		const float SatelliteDistanceCM = CameraToSatellite.Size();
		if (BirdDirection.IsNearlyZero() || SatelliteDistanceCM <= KINDA_SMALL_NUMBER)
		{
			return false;
		}
		const FVector SatelliteDirection = CameraToSatellite / SatelliteDistanceCM;
		const float SeparationRadians = FMath::Acos(FMath::Clamp(
			FVector::DotProduct(BirdDirection, SatelliteDirection), -1.0f, 1.0f));
		const float AngularRadiusRadians = FMath::Asin(FMath::Clamp(
			SatelliteRadiusCM / SatelliteDistanceCM, 0.0f, 0.999f));
		const float HorizontalHalfFovRadians = FMath::DegreesToRadians(
			FMath::Clamp(HorizontalFovDegrees, 10.0f, 170.0f) * 0.5f);
		const float VerticalHalfFovRadians = FMath::Atan(
			FMath::Tan(HorizontalHalfFovRadians) / FMath::Max(0.1f, AspectRatio));
		const float VisibleLimbMarginRadians = FMath::DegreesToRadians(3.0f);
		const float MaximumMoonCenterOffsetRadians = FMath::Max(
			VerticalHalfFovRadians * 0.92f,
			VerticalHalfFovRadians + AngularRadiusRadians - VisibleLimbMarginRadians);
		const float MaximumBirdOffsetRadians = VerticalHalfFovRadians * 0.82f;
		return SeparationRadians
			<= MaximumMoonCenterOffsetRadians + MaximumBirdOffsetRadians;
	};

	if (FitsView(CandidateDirection))
	{
		return BirdLocation + CandidateDirection * RadiusCM;
	}
	FVector PreferredTangent = FVector::CrossProduct(
		CandidateDirection,
		IdealDirection).GetSafeNormal();
	if (PreferredTangent.IsNearlyZero()) PreferredTangent = FVector::UpVector;
	float LowerAlpha = 0.0f;
	float UpperAlpha = 1.0f;
	for (int32 Iteration = 0; Iteration < 10; ++Iteration)
	{
		const float Alpha = (LowerAlpha + UpperAlpha) * 0.5f;
		const FVector Direction = BlendSurfaceUpStable(
			CandidateDirection,
			IdealDirection,
			PreferredTangent,
			Alpha);
		if (FitsView(Direction)) UpperAlpha = Alpha;
		else LowerAlpha = Alpha;
	}
	const FVector ConstrainedDirection = BlendSurfaceUpStable(
		CandidateDirection,
		IdealDirection,
		PreferredTangent,
		UpperAlpha);
	return BirdLocation + ConstrainedDirection * RadiusCM;
}

AABTSM6SlingshotCamera::AABTSM6SlingshotCamera()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	GetCameraComponent()->SetFieldOfView(50.0f);
}

void AABTSM6SlingshotCamera::SetAimFrame(const FVector& InCenter, const FVector& InForward, const FVector& InUp)
{
	AimCenter = InCenter;
	AimUp = InUp.GetSafeNormal();
	AimForward = FVector::VectorPlaneProject(InForward, AimUp).GetSafeNormal();
	if (AimForward.IsNearlyZero()) AimForward = FVector::ForwardVector;
	bFollowBird = false;
	SatelliteFlightIntent = EABTSM9SatelliteFlightCameraIntent::None;
	bSatelliteSubtleAssistInsideEnvelope = false;
	SatelliteSubtleAssistAlpha = 0.0f;
	PredictedPeriapsisWorld = FVector::ZeroVector;
	PredictedPeriapsisVelocity = FVector::ZeroVector;
	UpdateAim(0.0f);
}

bool AABTSM6SlingshotCamera::CopyAimFraming(
	float& OutDistanceCM,
	float& OutPitchDegrees,
	float& OutTargetForwardDistanceCM,
	float& OutTargetHeightCM) const
{
	OutDistanceCM = AimDistanceCM;
	OutPitchDegrees = AimPitchDegrees;
	OutTargetForwardDistanceCM = AimTargetForwardDistanceCM;
	OutTargetHeightCM = AimTargetHeightCM;
	return FMath::IsFinite(OutDistanceCM)
		&& FMath::IsFinite(OutPitchDegrees)
		&& FMath::IsFinite(OutTargetForwardDistanceCM)
		&& FMath::IsFinite(OutTargetHeightCM)
		&& OutDistanceCM >= 100.0f
		&& OutPitchDegrees >= -10.0f
		&& OutPitchDegrees <= 75.0f
		&& OutTargetForwardDistanceCM >= 0.0f;
}

bool AABTSM6SlingshotCamera::BuildAimView(
	const FVector& InCenter,
	const FVector& InForward,
	const FVector& InUp,
	FVector& OutLocation,
	FVector& OutLook,
	FVector& OutScreenUp) const
{
	const FVector SafeUp = InUp.GetSafeNormal();
	const FVector SafeForward =
		FVector::VectorPlaneProject(
			InForward,
			SafeUp).GetSafeNormal();
	if (SafeUp.IsNearlyZero() || SafeForward.IsNearlyZero())
	{
		return false;
	}
	const float PitchRadians = FMath::DegreesToRadians(AimPitchDegrees);
	const FVector BackAndUp =
		(-SafeForward * FMath::Cos(PitchRadians)
			+ SafeUp * FMath::Sin(PitchRadians)).GetSafeNormal();
	OutLocation = InCenter + BackAndUp * AimDistanceCM;
	const FVector Target =
		InCenter
		+ SafeForward * AimTargetForwardDistanceCM
		+ SafeUp * AimTargetHeightCM;
	OutLook = (Target - OutLocation).GetSafeNormal();
	OutScreenUp =
		FVector::VectorPlaneProject(SafeUp, OutLook).GetSafeNormal();
	return !OutLook.IsNearlyZero() && !OutScreenUp.IsNearlyZero();
}

bool AABTSM6SlingshotCamera::BuildAimInputPlaneBasis(
	const FVector& InCenter,
	const FVector& InForward,
	const FVector& InUp,
	FVector& OutPlaneNormal,
	FVector& OutInPlaneAxis,
	FVector& OutOutOfPlaneAxis) const
{
	OutPlaneNormal = FVector::ZeroVector;
	OutInPlaneAxis = FVector::ZeroVector;
	OutOutOfPlaneAxis = FVector::ZeroVector;
	FVector CameraLocation;
	if (!BuildAimView(
		InCenter,
		InForward,
		InUp,
		CameraLocation,
		OutPlaneNormal,
		OutInPlaneAxis))
	{
		return false;
	}
	OutOutOfPlaneAxis =
		FVector::CrossProduct(
			OutInPlaneAxis,
			OutPlaneNormal).GetSafeNormal();
	const FVector PreferredRight =
		FVector::CrossProduct(
			InUp.GetSafeNormal(),
			InForward.GetSafeNormal()).GetSafeNormal();
	if (FVector::DotProduct(
		OutOutOfPlaneAxis,
		PreferredRight) < 0.0f)
	{
		OutOutOfPlaneAxis *= -1.0f;
	}
	return !OutOutOfPlaneAxis.IsNearlyZero();
}

void AABTSM6SlingshotCamera::FollowBird(AABTSM25BirdCharacter* InBird, AABTSM2Planet* InPlanet)
{
	Bird = InBird;
	Planet = InPlanet;
	bPlanarFollow = false;
	bFollowBird = true;
	bSatelliteE5Hit = false;
	bSatelliteSurfaceContact = false;
	bSatelliteSurfaceFrameLatched = false;
	bSatelliteSurfaceFrameCommitted = false;
	bSatelliteE5ApproachLatched = false;
	SatelliteSurfaceFrameAlpha = 0.0f;
	SatelliteApproachCompositionAlpha = 0.0f;
	SatelliteE5CompositionAlpha = 0.0f;
	bSatelliteSubtleAssistInsideEnvelope = false;
	SatelliteSubtleAssistAlpha = 0.0f;
	StableSatellitePresentationUp = FVector::ZeroVector;
	StableFollowForward = FVector::ZeroVector;
	bForcePrimaryFrameUntilNextFollow = false;
	SatelliteOrbitViewNormal = FVector::ZeroVector;
	SetSatelliteFlightPhase(
		EABTSM9SatelliteFlightCameraPhase::PrimaryFollow);
}

void AABTSM6SlingshotCamera::FollowBirdPlanar(AABTSM25BirdCharacter* InBird, const FVector& InPlanarUp)
{
	Bird = InBird;
	Planet.Reset();
	PlanarFollowUp = InPlanarUp.GetSafeNormal();
	if (PlanarFollowUp.IsNearlyZero()) PlanarFollowUp = FVector::UpVector;
	bPlanarFollow = true;
	bFollowBird = true;
}

bool AABTSM6SlingshotCamera::SnapToPrimaryFollowForSatelliteCapture()
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("ABTSM9CameraCapture"))
		|| !Bird.IsValid())
	{
		return false;
	}
	FVector Location;
	FQuat Rotation;
	if (!BuildPrimaryFollowPose(*Bird.Get(), Location, Rotation)) return false;
	SetActorLocationAndRotation(Location, Rotation);
	return true;
}

void AABTSM6SlingshotCamera::ConfigureSatelliteFlightPresentation(
	AABTSM9Satellite* InSatellite,
	AActor* InE5Target)
{
	Satellite = InSatellite;
	E5Target = InE5Target;
	bSatelliteE5Hit = false;
	bSatelliteSurfaceContact = false;
	bSatelliteSurfaceFrameLatched = false;
	bSatelliteSurfaceFrameCommitted = false;
	bSatelliteE5ApproachLatched = false;
	SatelliteSurfaceFrameAlpha = 0.0f;
	SatelliteApproachCompositionAlpha = 0.0f;
	SatelliteE5CompositionAlpha = 0.0f;
	bSatelliteSubtleAssistInsideEnvelope = false;
	SatelliteSubtleAssistAlpha = 0.0f;
	StableSatellitePresentationUp = FVector::ZeroVector;
	StableFollowForward = FVector::ZeroVector;
	bForcePrimaryFrameUntilNextFollow = false;
	SatelliteOrbitViewNormal = FVector::ZeroVector;
	SetSatelliteFlightPhase(
		EABTSM9SatelliteFlightCameraPhase::PrimaryFollow);
}

void AABTSM6SlingshotCamera::LockSatelliteFlightIntent(
	const FABTSM6TrajectoryPreview& Preview)
{
	SatelliteFlightIntent = ClassifySatelliteFlightIntent(Preview);
	PredictedPeriapsisWorld = FVector::ZeroVector;
	PredictedPeriapsisVelocity = FVector::ZeroVector;
	SatelliteOrbitViewNormal = FVector::ZeroVector;
	if (SatelliteFlightIntent == EABTSM9SatelliteFlightCameraIntent::None
		|| !Satellite.IsValid()
		|| !E5Target.IsValid()
		|| Preview.EncounterSatelliteRadiusCM <= 0.0f
		|| Preview.WorldPoints.Num() < 2)
	{
		return;
	}
	int32 ClosestIndex = 0;
	double ClosestDistanceSquared = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < Preview.WorldPoints.Num(); ++Index)
	{
		const double DistanceSquared = FVector::DistSquared(
			Preview.WorldPoints[Index],
			Preview.EncounterSatelliteCenterWorld);
		if (DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			ClosestIndex = Index;
		}
	}
	PredictedPeriapsisWorld = Preview.WorldPoints[ClosestIndex];
	const int32 PreviousIndex = FMath::Max(0, ClosestIndex - 1);
	const int32 NextIndex = FMath::Min(Preview.WorldPoints.Num() - 1, ClosestIndex + 1);
	PredictedPeriapsisVelocity =
		(Preview.WorldPoints[NextIndex] - Preview.WorldPoints[PreviousIndex])
		.GetSafeNormal();
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M9][FlightCamera] IntentLocked=%s Terminal=%s Periapsis=%s"),
		*UEnum::GetValueAsString(SatelliteFlightIntent),
		*UEnum::GetValueAsString(Preview.TerminalType),
		*PredictedPeriapsisWorld.ToCompactString());
}

EABTSM9SatelliteFlightCameraIntent
AABTSM6SlingshotCamera::ClassifySatelliteFlightIntent(
	const FABTSM6TrajectoryPreview& Preview)
{
	if (!Preview.bHasSatelliteEncounter
		|| Preview.EncounterSatelliteRadiusCM <= 0.0f
		|| Preview.WorldPoints.Num() < 2)
	{
		return EABTSM9SatelliteFlightCameraIntent::None;
	}
	if (Preview.TerminalType == EABTSM6TrajectoryTerminalType::SatelliteBody)
	{
		return EABTSM9SatelliteFlightCameraIntent::SurfaceLanding;
	}
	return Preview.TerminalType == EABTSM6TrajectoryTerminalType::SatelliteE5
		? EABTSM9SatelliteFlightCameraIntent::CinematicE5
		: EABTSM9SatelliteFlightCameraIntent::SubtleAssist;
}

float AABTSM6SlingshotCamera::ComputeSatelliteSurfaceFrameTarget(
	const EABTSM9SatelliteFlightCameraIntent Intent,
	const float SurfaceAltitudeCM,
	const float SatelliteRadiusCM,
	const float PredictedContactSeconds,
	const bool bAuthoritativeContact,
	const float StartAltitudeMultiplier,
	const float LeadSeconds)
{
	if (bAuthoritativeContact) return 1.0f;
	if (Intent != EABTSM9SatelliteFlightCameraIntent::CinematicE5
		&& Intent != EABTSM9SatelliteFlightCameraIntent::SurfaceLanding)
	{
		return 0.0f;
	}
	if (!FMath::IsFinite(PredictedContactSeconds)
		|| PredictedContactSeconds < 0.0f)
	{
		return 0.0f;
	}
	const float MaximumAltitudeCM = FMath::Max(
		1.0f,
		SatelliteRadiusCM * FMath::Max(0.25f, StartAltitudeMultiplier));
	if (SurfaceAltitudeCM > MaximumAltitudeCM) return 0.0f;
	const float AltitudeAlpha = 1.0f - FMath::Clamp(
		SurfaceAltitudeCM / MaximumAltitudeCM, 0.0f, 1.0f);
	const float TimeAlpha = 1.0f - FMath::Clamp(
		PredictedContactSeconds / FMath::Max(0.1f, LeadSeconds), 0.0f, 1.0f);
	return FMath::Clamp(FMath::Max(AltitudeAlpha, TimeAlpha), 0.0f, 1.0f);
}

float AABTSM6SlingshotCamera::ComputeSatelliteSubtleAssistDistanceWeight(
	const float SatelliteDistanceCM,
	const float FullInfluenceDistanceCM,
	const float ZeroInfluenceDistanceCM)
{
	if (!FMath::IsFinite(SatelliteDistanceCM)
		|| !FMath::IsFinite(FullInfluenceDistanceCM)
		|| !FMath::IsFinite(ZeroInfluenceDistanceCM)
		|| ZeroInfluenceDistanceCM <= FullInfluenceDistanceCM)
	{
		return 0.0f;
	}
	const float NormalizedDistance = FMath::Clamp(
		(SatelliteDistanceCM - FullInfluenceDistanceCM)
			/ (ZeroInfluenceDistanceCM - FullInfluenceDistanceCM),
		0.0f,
		1.0f);
	// Cubic smoothstep has zero slope at both ends, so crossing either camera
	// envelope cannot create a visible angular-velocity step.
	const float SmoothDistance = NormalizedDistance * NormalizedDistance
		* (3.0f - 2.0f * NormalizedDistance);
	return 1.0f - SmoothDistance;
}

FVector AABTSM6SlingshotCamera::BlendSurfaceUpStable(
	const FVector& PrimaryUp,
	const FVector& SatelliteUp,
	const FVector& PreferredTangent,
	const float Alpha)
{
	const FVector From = PrimaryUp.GetSafeNormal();
	const FVector To = SatelliteUp.GetSafeNormal();
	if (From.IsNearlyZero()) return To.IsNearlyZero() ? FVector::UpVector : To;
	if (To.IsNearlyZero()) return From;
	const float Dot = FMath::Clamp(FVector::DotProduct(From, To), -1.0f, 1.0f);
	FVector Axis = FVector::CrossProduct(From, To).GetSafeNormal();
	if (Axis.IsNearlyZero())
	{
		Axis = FVector::VectorPlaneProject(PreferredTangent, From).GetSafeNormal();
		if (Axis.IsNearlyZero())
		{
			const FVector Reference = FMath::Abs(From.Z) < 0.9f
				? FVector::UpVector
				: FVector::ForwardVector;
			Axis = FVector::CrossProduct(From, Reference).GetSafeNormal();
		}
	}
	const float Angle = FMath::Acos(Dot) * FMath::Clamp(Alpha, 0.0f, 1.0f);
	return FQuat(Axis, Angle).RotateVector(From).GetSafeNormal();
}

FVector AABTSM6SlingshotCamera::LimitSurfaceUpAngularStep(
	const FVector& CurrentUp,
	const FVector& DesiredUp,
	const FVector& PreferredTangent,
	const float MaximumStepDegrees)
{
	const FVector From = CurrentUp.GetSafeNormal();
	const FVector To = DesiredUp.GetSafeNormal();
	if (From.IsNearlyZero()) return To.IsNearlyZero() ? FVector::UpVector : To;
	if (To.IsNearlyZero()) return From;
	const float AngleRadians = FMath::Acos(FMath::Clamp(
		FVector::DotProduct(From, To), -1.0f, 1.0f));
	const float MaximumStepRadians = FMath::DegreesToRadians(
		FMath::Max(0.0f, MaximumStepDegrees));
	if (MaximumStepRadians <= 0.0f || AngleRadians <= MaximumStepRadians)
	{
		return To;
	}
	return BlendSurfaceUpStable(
		From,
		To,
		PreferredTangent,
		MaximumStepRadians / FMath::Max(KINDA_SMALL_NUMBER, AngleRadians));
}

FQuat AABTSM6SlingshotCamera::LimitCameraRotationAngularStep(
	const FQuat& CurrentRotation,
	const FQuat& DesiredRotation,
	const float MaximumStepDegrees)
{
	const FQuat From = CurrentRotation.GetNormalized();
	const FQuat To = DesiredRotation.GetNormalized();
	const float AngleDegrees = FMath::RadiansToDegrees(From.AngularDistance(To));
	const float StepDegrees = FMath::Max(0.0f, MaximumStepDegrees);
	if (!FMath::IsFinite(AngleDegrees)
		|| AngleDegrees <= StepDegrees
		|| AngleDegrees <= KINDA_SMALL_NUMBER)
	{
		return To;
	}
	return FQuat::Slerp(
		From,
		To,
		FMath::Clamp(StepDegrees / AngleDegrees, 0.0f, 1.0f)).GetNormalized();
}

void AABTSM6SlingshotCamera::ClearSatelliteFlightPresentation()
{
	Satellite.Reset();
	E5Target.Reset();
	bSatelliteE5Hit = false;
	bSatelliteSurfaceContact = false;
	bSatelliteSurfaceFrameLatched = false;
	bSatelliteSurfaceFrameCommitted = false;
	bSatelliteE5ApproachLatched = false;
	SatelliteSurfaceFrameAlpha = 0.0f;
	SatelliteApproachCompositionAlpha = 0.0f;
	SatelliteE5CompositionAlpha = 0.0f;
	bSatelliteSubtleAssistInsideEnvelope = false;
	SatelliteSubtleAssistAlpha = 0.0f;
	StableSatellitePresentationUp = FVector::ZeroVector;
	if (Bird.IsValid()) Bird->ClearSlingshotPresentationUp();
	bForcePrimaryFrameUntilNextFollow = false;
	SatelliteOrbitViewNormal = FVector::ZeroVector;
	SatelliteFlightIntent = EABTSM9SatelliteFlightCameraIntent::None;
	PredictedPeriapsisWorld = FVector::ZeroVector;
	PredictedPeriapsisVelocity = FVector::ZeroVector;
	SetSatelliteFlightPhase(
		EABTSM9SatelliteFlightCameraPhase::PrimaryFollow);
}

void AABTSM6SlingshotCamera::NotifySatelliteE5Hit()
{
	if (!Satellite.IsValid() || !E5Target.IsValid()) return;
	bSatelliteE5Hit = true;
	SetSatelliteFlightPhase(
		EABTSM9SatelliteFlightCameraPhase::E5Impact);
}

void AABTSM6SlingshotCamera::NotifySatelliteSurfaceContact()
{
	if (!Satellite.IsValid()) return;
	bSatelliteSurfaceContact = true;
	if (SatelliteFlightIntent == EABTSM9SatelliteFlightCameraIntent::None)
	{
		SatelliteFlightIntent = EABTSM9SatelliteFlightCameraIntent::SurfaceLanding;
	}
	// A real surface contact must strengthen the moon-frame authority, but it
	// must not demote an already qualified E5 strike back to generic orbit for
	// one frame. That phase bounce used to rotate the camera away precisely as
	// the bird reached the landing composition.
	if (!bSatelliteE5ApproachLatched
		&& SatelliteFlightPhase != EABTSM9SatelliteFlightCameraPhase::E5Approach
		&& SatelliteFlightPhase != EABTSM9SatelliteFlightCameraPhase::E5Impact)
	{
		SetSatelliteFlightPhase(
			EABTSM9SatelliteFlightCameraPhase::SatelliteOrbit);
	}
}

void AABTSM6SlingshotCamera::NotifyBirdImpact()
{
	FollowFacingLockRemainingSeconds = FMath::Max(
		FollowFacingLockRemainingSeconds,
		FMath::Max(0.0f, FollowFacingImpactLockSeconds));
}

void AABTSM6SlingshotCamera::BeginReturnToPrimaryFrame()
{
	bSatelliteE5Hit = false;
	bSatelliteSurfaceContact = false;
	bSatelliteSurfaceFrameLatched = false;
	bSatelliteSurfaceFrameCommitted = false;
	bSatelliteE5ApproachLatched = false;
	SatelliteSurfaceFrameAlpha = 0.0f;
	SatelliteApproachCompositionAlpha = 0.0f;
	SatelliteE5CompositionAlpha = 0.0f;
	bSatelliteSubtleAssistInsideEnvelope = false;
	SatelliteSubtleAssistAlpha = 0.0f;
	StableSatellitePresentationUp = FVector::ZeroVector;
	if (Bird.IsValid()) Bird->ClearSlingshotPresentationUp();
	bForcePrimaryFrameUntilNextFollow = true;
	SatelliteOrbitViewNormal = FVector::ZeroVector;
	SetSatelliteFlightPhase(
		EABTSM9SatelliteFlightCameraPhase::PrimaryFollow);
}

void AABTSM6SlingshotCamera::SetSatelliteFlightPhase(
	const EABTSM9SatelliteFlightCameraPhase NewPhase)
{
	if (SatelliteFlightPhase == NewPhase) return;
	const EABTSM9SatelliteFlightCameraPhase Previous =
		SatelliteFlightPhase;
	SatelliteFlightPhase = NewPhase;
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M9][FlightCamera] Phase=%s Previous=%s"),
		*UEnum::GetValueAsString(NewPhase),
		*UEnum::GetValueAsString(Previous));
}

void AABTSM6SlingshotCamera::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FollowFacingLockRemainingSeconds = FMath::Max(
		0.0f,
		FollowFacingLockRemainingSeconds - DeltaSeconds);
	if (bFollowBird) UpdateFollow(DeltaSeconds); else UpdateAim(DeltaSeconds);
}

void AABTSM6SlingshotCamera::UpdateAim(const float DeltaSeconds)
{
	// Use only the cord frame captured on launch-mode entry. Pulling the pouch
	// must not rotate or translate the camera around the slingshot.
	FVector DesiredLocation;
	FVector Look;
	FVector ScreenUp;
	if (!BuildAimView(
		AimCenter,
		AimForward,
		AimUp,
		DesiredLocation,
		Look,
		ScreenUp))
	{
		return;
	}
	const FQuat Rotation = FRotationMatrix::MakeFromXZ(Look, ScreenUp).ToQuat();
	SetActorLocationAndRotation(DeltaSeconds > 0.0f ? FMath::VInterpTo(GetActorLocation(), DesiredLocation, DeltaSeconds, AimCameraBlendSpeed) : DesiredLocation, Rotation);
}

void AABTSM6SlingshotCamera::UpdateFollow(const float DeltaSeconds)
{
	AABTSM25BirdCharacter* TargetBird = Bird.Get();
	AABTSM2Planet* TargetPlanet = Planet.Get();
	if (TargetBird == nullptr || (!bPlanarFollow && TargetPlanet == nullptr)) return;
	if (!bPlanarFollow
		&& UpdateSatelliteFollow(*TargetBird, DeltaSeconds))
	{
		return;
	}
	FVector DesiredLocation;
	FQuat DesiredRotation;
	if (!BuildPrimaryFollowPose(*TargetBird, DesiredLocation, DesiredRotation)) return;
	const FVector Location = FMath::VInterpTo(GetActorLocation(), DesiredLocation, DeltaSeconds, FollowSpeed);
	const FQuat Rotation = FMath::QInterpTo(GetActorQuat(), DesiredRotation, DeltaSeconds, FollowSpeed);
	SetActorLocationAndRotation(Location, Rotation);
}

void AABTSM6SlingshotCamera::CalcCamera(
	const float DeltaTime,
	FMinimalViewInfo& OutResult)
{
	Super::CalcCamera(DeltaTime, OutResult);
	if (!bSatelliteConstantBirdScaleExperiment
		|| (SatelliteFlightIntent
			!= EABTSM9SatelliteFlightCameraIntent::CinematicE5
			&& SatelliteFlightIntent
				!= EABTSM9SatelliteFlightCameraIntent::SurfaceLanding))
	{
		return;
	}
	AABTSM25BirdCharacter* TargetBird = Bird.Get();
	AABTSM9Satellite* TargetSatellite = Satellite.Get();
	if (TargetBird == nullptr || TargetSatellite == nullptr) return;
	const FVector BirdLocation = TargetBird->GetActorLocation();
	const float BirdDistanceCM =
		ABTSM9SatelliteCameraPrivate::ComputeGroundFollowBirdDistanceCM(
			FlightDistanceCM,
			FlightHeightCM);
	OutResult.Location = ConstrainCameraToBirdDistance(
		OutResult.Location,
		BirdLocation,
		BirdDistanceCM,
		GetActorLocation() - BirdLocation);
	OutResult.FOV = FMath::Clamp(
		SatelliteConstantBirdScaleFieldOfViewDegrees,
		35.0f,
		70.0f);
	OutResult.Location = ConstrainFixedDistanceCameraForSatelliteVisibility(
		OutResult.Location,
		BirdLocation,
		TargetSatellite->GetPlanetCenterWorld(),
		TargetSatellite->GetPlanetRadiusCM(),
		BirdDistanceCM,
		OutResult.FOV,
		OutResult.AspectRatio);

	FVector PresentationUp = StableSatellitePresentationUp.GetSafeNormal();
	if (PresentationUp.IsNearlyZero())
	{
		PresentationUp = (BirdLocation
			- TargetSatellite->GetPlanetCenterWorld()).GetSafeNormal();
	}
	if (PresentationUp.IsNearlyZero()) PresentationUp = FVector::UpVector;
	FVector Focus = BirdLocation + PresentationUp * 80.0f;
	if (E5Target.IsValid()
		&& (SatelliteFlightPhase == EABTSM9SatelliteFlightCameraPhase::E5Approach
			|| SatelliteFlightPhase == EABTSM9SatelliteFlightCameraPhase::E5Impact))
	{
		Focus = FMath::Lerp(
			Focus,
			E5Target->GetActorLocation(),
			FMath::Clamp(SatelliteE5LookAheadBias, 0.0f, 0.35f)
				* FMath::SmoothStep(0.0f, 1.0f, SatelliteE5CompositionAlpha));
	}
	const FVector Look = ABTSM9SatelliteCameraPrivate::KeepSatelliteLimbVisible(
		OutResult.Location,
		Focus,
		TargetSatellite->GetPlanetCenterWorld(),
		TargetSatellite->GetPlanetRadiusCM(),
		OutResult.FOV,
		OutResult.AspectRatio);
	const FVector ScreenUp = FVector::VectorPlaneProject(
		PresentationUp,
		Look).GetSafeNormal();
	if (!Look.IsNearlyZero() && !ScreenUp.IsNearlyZero())
	{
		OutResult.Rotation = FRotationMatrix::MakeFromXZ(
			Look,
			ScreenUp).Rotator();
	}
}

bool AABTSM6SlingshotCamera::BuildPrimaryFollowPose(
	AABTSM25BirdCharacter& TargetBird,
	FVector& OutLocation,
	FQuat& OutRotation)
{
	AABTSM2Planet* TargetPlanet = Planet.Get();
	if (!bPlanarFollow && TargetPlanet == nullptr) return false;
	const FVector Up = bPlanarFollow
		? PlanarFollowUp
		: TargetPlanet->GetRadialUpAtWorldLocation(TargetBird.GetActorLocation());
	const FVector Forward = ResolveStableFollowForward(
		TargetBird,
		Up,
		TargetBird.GetSlingshotVelocity(),
		FollowFacingLockRemainingSeconds > 0.0f);
	if (Forward.IsNearlyZero()) return false;
	OutLocation = TargetBird.GetActorLocation()
		- Forward * FlightDistanceCM
		+ Up * FlightHeightCM;
	const FVector Look =
		(TargetBird.GetActorLocation() + Up * 80.0f - OutLocation).GetSafeNormal();
	FVector ScreenUp = FVector::VectorPlaneProject(Up, Look).GetSafeNormal();
	if (ScreenUp.IsNearlyZero()) ScreenUp = Up;
	OutRotation = FRotationMatrix::MakeFromXZ(Look, ScreenUp).ToQuat();
	return true;
}

FVector AABTSM6SlingshotCamera::ResolveStableFollowForward(
	AABTSM25BirdCharacter& TargetBird,
	const FVector& Up,
	const FVector& CandidateVelocity,
	const bool bLockReversal)
{
	FVector Previous = FVector::VectorPlaneProject(
		StableFollowForward,
		Up).GetSafeNormal();
	const FVector TangentVelocity = FVector::VectorPlaneProject(
		CandidateVelocity,
		Up);
	FVector Candidate = TangentVelocity.GetSafeNormal();
	const bool bTrustVelocity = TangentVelocity.Size()
		>= FMath::Max(0.0f, FollowFacingMinimumSpeedCMPerSec)
		&& !(bLockReversal
			&& !Previous.IsNearlyZero()
			&& FVector::DotProduct(Candidate, Previous) < 0.0f);
	if (!bTrustVelocity) Candidate = Previous;
	if (Candidate.IsNearlyZero())
	{
		Candidate = FVector::VectorPlaneProject(
			TargetBird.GetActorForwardVector(),
			Up).GetSafeNormal();
	}
	if (Candidate.IsNearlyZero())
	{
		const FVector Reference = FMath::Abs(Up.Z) < 0.9f
			? FVector::UpVector
			: FVector::ForwardVector;
		Candidate = FVector::CrossProduct(Reference, Up).GetSafeNormal();
	}
	StableFollowForward = Candidate;
	return Candidate;
}

bool AABTSM6SlingshotCamera::UpdateSatelliteFollow(
	AABTSM25BirdCharacter& TargetBird,
	const float DeltaSeconds)
{
	AABTSM9Satellite* TargetSatellite = Satellite.Get();
	AActor* TargetE5 = E5Target.Get();
	if (bForcePrimaryFrameUntilNextFollow)
	{
		TargetBird.ClearSlingshotPresentationUp();
		return false;
	}
	if (SatelliteFlightIntent == EABTSM9SatelliteFlightCameraIntent::None)
	{
		TargetBird.ClearSlingshotPresentationUp();
		return false;
	}
	if (TargetSatellite == nullptr || TargetE5 == nullptr)
	{
		if (SatelliteFlightPhase
			!= EABTSM9SatelliteFlightCameraPhase::PrimaryFollow)
		{
			SetSatelliteFlightPhase(
				EABTSM9SatelliteFlightCameraPhase::PrimaryFollow);
		}
		SatelliteSurfaceFrameAlpha = 0.0f;
		SatelliteApproachCompositionAlpha = 0.0f;
		SatelliteE5CompositionAlpha = 0.0f;
		TargetBird.ClearSlingshotPresentationUp();
		return false;
	}
	const FVector SatelliteCenter =
		TargetSatellite->GetPlanetCenterWorld();
	const float SatelliteRadiusCM =
		FMath::Max(1.0f, TargetSatellite->GetPlanetRadiusCM());
	const FVector BirdLocation = TargetBird.GetActorLocation();
	const FVector SatelliteToBird = BirdLocation - SatelliteCenter;
	const float SatelliteDistanceCM = SatelliteToBird.Size();
	const FVector BirdRadialUp = SatelliteToBird.GetSafeNormal();
	if (BirdRadialUp.IsNearlyZero()) return false;
	AABTSM2Planet* PrimaryPlanet = Planet.Get();
	const FVector PrimaryRadialUp = PrimaryPlanet
		? PrimaryPlanet->GetRadialUpAtWorldLocation(BirdLocation).GetSafeNormal()
		: FVector::UpVector;
	const FVector Velocity = TargetBird.GetSlingshotVelocity();
	const float ContactRadiusCM = SatelliteRadiusCM
		+ FMath::Max(0.0f, TargetBird.GetSlingshotTrajectoryCollisionRadiusCM());
	const float SurfaceAltitudeCM = SatelliteDistanceCM - ContactRadiusCM;
	float PredictedContactSeconds = -1.0f;
	const float SpeedSquared = Velocity.SizeSquared();
	const float RadialDotVelocity = FVector::DotProduct(SatelliteToBird, Velocity);
	const float Discriminant = FMath::Square(RadialDotVelocity)
		- SpeedSquared
			* (SatelliteToBird.SizeSquared() - FMath::Square(ContactRadiusCM));
	if (SpeedSquared > 1.0f
		&& RadialDotVelocity < 0.0f
		&& Discriminant >= 0.0f)
	{
		PredictedContactSeconds = FMath::Max(
			0.0f,
			(-RadialDotVelocity - FMath::Sqrt(Discriminant)) / SpeedSquared);
	}
	// The path is curved by satellite gravity, so a straight velocity ray can
	// miss even when the immutable launch preview proves a surface contact.
	// Use time-to-predicted-periapsis as the conservative lead clock, but only
	// while the bird is actually advancing toward that point.
	if (!PredictedPeriapsisWorld.IsNearlyZero() && SpeedSquared > 1.0f)
	{
		const FVector ToPredictedContact = PredictedPeriapsisWorld - BirdLocation;
		const float AlongVelocityCM = FVector::DotProduct(
			ToPredictedContact,
			Velocity.GetSafeNormal());
		if (AlongVelocityCM > 0.0f)
		{
			const float PreviewContactSeconds = AlongVelocityCM / FMath::Sqrt(SpeedSquared);
			if (PredictedContactSeconds < 0.0f
				|| PreviewContactSeconds < PredictedContactSeconds)
			{
				PredictedContactSeconds = PreviewContactSeconds;
			}
		}
	}
	const float ComputedSurfaceFrameTarget = ComputeSatelliteSurfaceFrameTarget(
		SatelliteFlightIntent,
		SurfaceAltitudeCM,
		SatelliteRadiusCM,
		PredictedContactSeconds,
		bSatelliteSurfaceContact || bSatelliteE5Hit,
		SatelliteSurfaceFrameStartAltitudeMultiplier,
		SatelliteSurfaceFrameLeadSeconds);
	// The original M9 composition adopted the moon radial as soon as the
	// cinematic approach began. Waiting for a near-contact ray leaves the
	// camera tilted through the entire visible orbit and only turns upright at
	// E5. Start the same hand-off before the moon reaches the frame edge, but keep
	// the transition rate-limited instead of cutting to the new frame.
	if (ComputedSurfaceFrameTarget > 0.01f
		|| ((SatelliteFlightIntent
				== EABTSM9SatelliteFlightCameraIntent::CinematicE5
				|| SatelliteFlightIntent
					== EABTSM9SatelliteFlightCameraIntent::SurfaceLanding)
			&& (SatelliteFlightPhase
					!= EABTSM9SatelliteFlightCameraPhase::PrimaryFollow
				|| SatelliteDistanceCM
					<= SatelliteRadiusCM * FMath::Max(
						2.0f,
						SatelliteApproachEnterRadiusMultiplier))))
	{
		bSatelliteSurfaceFrameLatched = true;
	}
	const float SurfaceFrameTarget = bSatelliteSurfaceFrameLatched
		? FMath::Max(ComputedSurfaceFrameTarget, 1.0f)
		: ComputedSurfaceFrameTarget;
	const float SurfaceFrameSpeed = SurfaceFrameTarget >= SatelliteSurfaceFrameAlpha
		? SatelliteSurfaceFrameBlendInSpeed
		: SatelliteSurfaceFrameBlendOutSpeed;
	SatelliteSurfaceFrameAlpha = FMath::FInterpTo(
		SatelliteSurfaceFrameAlpha,
		SurfaceFrameTarget,
		DeltaSeconds,
		FMath::Max(0.1f, SurfaceFrameSpeed));
	const FVector DesiredPresentationUp = BlendSurfaceUpStable(
		PrimaryRadialUp,
		BirdRadialUp,
		Velocity,
		SatelliteSurfaceFrameAlpha);
	if (StableSatellitePresentationUp.IsNearlyZero())
	{
		StableSatellitePresentationUp = PrimaryRadialUp;
	}
	FVector PresentationUp = bSatelliteSurfaceFrameCommitted
		? BirdRadialUp
		: LimitSurfaceUpAngularStep(
			StableSatellitePresentationUp,
			DesiredPresentationUp,
			Velocity,
			FMath::Max(0.0f, SatelliteSurfaceUpMaxDegreesPerSecond)
				* FMath::Max(0.0f, DeltaSeconds));
	if (!bSatelliteSurfaceFrameCommitted
		&& SatelliteSurfaceFrameAlpha >= 0.995f
		&& FVector::DotProduct(PresentationUp, BirdRadialUp)
			>= FMath::Cos(FMath::DegreesToRadians(1.0f)))
	{
		bSatelliteSurfaceFrameCommitted = true;
		PresentationUp = BirdRadialUp;
	}
	StableSatellitePresentationUp = PresentationUp;
	if (SatelliteFlightIntent
		!= EABTSM9SatelliteFlightCameraIntent::SubtleAssist)
	{
		TargetBird.SetSlingshotPresentationUp(
			PresentationUp,
			DeltaSeconds,
			bSatelliteSurfaceContact || bSatelliteE5Hit,
			BirdLocation - GetActorLocation());
	}
	else
	{
		// A gravity fly-by never transfers bird presentation authority to the
		// moon. Keeping this override active after the assist was the source of
		// the residual bird/camera roll reported after leaving lunar gravity.
		TargetBird.ClearSlingshotPresentationUp();
	}

	const float EnterDistanceCM =
		SatelliteRadiusCM
		* FMath::Max(
			2.0f,
			SatelliteApproachEnterRadiusMultiplier);
	const float ExitDistanceCM =
		SatelliteRadiusCM
		* FMath::Max(
			SatelliteApproachEnterRadiusMultiplier + 0.1f,
			SatelliteApproachExitRadiusMultiplier);
	const float OrbitDistanceCM =
		SatelliteRadiusCM
		* FMath::Clamp(
			SatelliteOrbitEnterRadiusMultiplier,
			1.1f,
			SatelliteApproachEnterRadiusMultiplier);
	const float OrbitExitDistanceCM =
		SatelliteRadiusCM
		* FMath::Max(
			SatelliteOrbitEnterRadiusMultiplier + 0.1f,
			SatelliteOrbitExitRadiusMultiplier);
	const bool bUseTransitionFieldOfView =
		SatelliteFlightIntent == EABTSM9SatelliteFlightCameraIntent::CinematicE5
		&& !bSatelliteE5ApproachLatched
		&& SatelliteDistanceCM > SatelliteRadiusCM * 1.45f;
	const float TargetFieldOfViewDegrees = bSatelliteConstantBirdScaleExperiment
		? FMath::Clamp(SatelliteConstantBirdScaleFieldOfViewDegrees, 35.0f, 70.0f)
		: (bUseTransitionFieldOfView
			? FMath::Max(50.0f, SatelliteTransitionFieldOfViewDegrees)
			: 50.0f);
	GetCameraComponent()->SetFieldOfView(FMath::FInterpTo(
		GetCameraComponent()->FieldOfView,
		TargetFieldOfViewDegrees,
		DeltaSeconds,
		FMath::Max(0.1f, SatelliteFieldOfViewBlendSpeed)));

	if (SatelliteFlightPhase
			== EABTSM9SatelliteFlightCameraPhase::PrimaryFollow
		&& SatelliteFlightIntent
			!= EABTSM9SatelliteFlightCameraIntent::SubtleAssist
		&& SatelliteDistanceCM > EnterDistanceCM)
	{
		// Keep the incoming fixed-radius state synchronized with the ordinary
		// ground-follow pose. The first SatelliteApproach frame can therefore
		// continue from the exact previous direction instead of snapping to a
		// freshly reconstructed moon-relative endpoint.
		return false;
	}
	if (SatelliteFlightIntent
		== EABTSM9SatelliteFlightCameraIntent::SubtleAssist)
	{
		if (!bSatelliteSubtleAssistInsideEnvelope
			&& SatelliteDistanceCM <= EnterDistanceCM)
		{
			bSatelliteSubtleAssistInsideEnvelope = true;
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][M9][FlightCamera] SubtleAssist=Enter Distance=%.1f Enter=%.1f Exit=%.1f"),
				SatelliteDistanceCM,
				EnterDistanceCM,
				ExitDistanceCM);
		}
		else if (bSatelliteSubtleAssistInsideEnvelope
			&& SatelliteDistanceCM >= ExitDistanceCM)
		{
			bSatelliteSubtleAssistInsideEnvelope = false;
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][M9][FlightCamera] SubtleAssist=Exit Distance=%.1f Enter=%.1f Exit=%.1f"),
				SatelliteDistanceCM,
				EnterDistanceCM,
				ExitDistanceCM);
		}
		const float DistanceWeight =
			bSatelliteSubtleAssistInsideEnvelope
				? ComputeSatelliteSubtleAssistDistanceWeight(
					SatelliteDistanceCM,
					OrbitExitDistanceCM,
					ExitDistanceCM)
				: 0.0f;
		const float AssistBlendSeconds =
			DistanceWeight >= SatelliteSubtleAssistAlpha
				? SatelliteSubtleBlendInSeconds
				: SatelliteSubtleBlendOutSeconds;
		SatelliteSubtleAssistAlpha = FMath::FInterpConstantTo(
			SatelliteSubtleAssistAlpha,
			DistanceWeight,
			DeltaSeconds,
			1.0f / FMath::Max(0.05f, AssistBlendSeconds));
		if (!bSatelliteSubtleAssistInsideEnvelope
			&& SatelliteSubtleAssistAlpha <= 0.0025f)
		{
			SatelliteSubtleAssistAlpha = 0.0f;
			StableSatellitePresentationUp = FVector::ZeroVector;
			return false;
		}
		FVector PrimaryLocation;
		FQuat PrimaryRotation;
		if (!BuildPrimaryFollowPose(TargetBird, PrimaryLocation, PrimaryRotation))
		{
			return false;
		}
		const FVector BirdToPrimaryCamera =
			PrimaryLocation - BirdLocation;
		const FVector AssistLocation = BirdLocation
			+ BirdToPrimaryCamera * FMath::Max(1.0f, SatelliteSubtlePullbackMultiplier);
		const FVector DesiredLocation = FMath::Lerp(
			PrimaryLocation,
			AssistLocation,
			SatelliteSubtleAssistAlpha);
		const FVector PredictedFocus = PredictedPeriapsisWorld.IsNearlyZero()
			? BirdLocation
			: FMath::Lerp(BirdLocation, PredictedPeriapsisWorld, 0.18f);
		const FVector AssistLook = (PredictedFocus - DesiredLocation).GetSafeNormal();
		FVector AssistUp = FVector::VectorPlaneProject(BirdRadialUp, AssistLook).GetSafeNormal();
		if (AssistLook.IsNearlyZero() || AssistUp.IsNearlyZero()) return false;
		const FQuat AssistRotation = FRotationMatrix::MakeFromXZ(
			AssistLook, AssistUp).ToQuat();
		const float AngularDifferenceDegrees = FMath::RadiansToDegrees(
			PrimaryRotation.AngularDistance(AssistRotation));
		const float MaximumTiltDegrees = FMath::Max(
			0.0f, SatelliteSubtleMaximumTiltDegrees);
		const float AssistAlpha = AngularDifferenceDegrees > KINDA_SMALL_NUMBER
			? FMath::Min(1.0f, MaximumTiltDegrees / AngularDifferenceDegrees)
			: 1.0f;
		const FQuat LimitedAssistRotation = FQuat::Slerp(
			PrimaryRotation, AssistRotation, AssistAlpha).GetNormalized();
		const FQuat DesiredRotation = FQuat::Slerp(
			PrimaryRotation,
			LimitedAssistRotation,
			SatelliteSubtleAssistAlpha).GetNormalized();
		const float BlendSpeed = FMath::Max(0.1f, SatelliteFollowBlendSpeed);
		SetActorLocationAndRotation(
			FMath::VInterpTo(GetActorLocation(), DesiredLocation, DeltaSeconds, BlendSpeed),
			FMath::QInterpTo(GetActorQuat(), DesiredRotation, DeltaSeconds, BlendSpeed));
		return true;
	}
	if (!bSatelliteE5Hit
		&& SatelliteFlightPhase
			!= EABTSM9SatelliteFlightCameraPhase::PrimaryFollow
		&& SatelliteDistanceCM >= ExitDistanceCM)
	{
		SatelliteOrbitViewNormal = FVector::ZeroVector;
		SatelliteSurfaceFrameAlpha = 0.0f;
		SatelliteApproachCompositionAlpha = 0.0f;
		SatelliteE5CompositionAlpha = 0.0f;
		bSatelliteSurfaceFrameLatched = false;
		bSatelliteSurfaceFrameCommitted = false;
		bSatelliteE5ApproachLatched = false;
		StableSatellitePresentationUp = FVector::ZeroVector;
		TargetBird.ClearSlingshotPresentationUp();
		SetSatelliteFlightPhase(
			EABTSM9SatelliteFlightCameraPhase::PrimaryFollow);
		return false;
	}

	if (SatelliteFlightPhase
		== EABTSM9SatelliteFlightCameraPhase::PrimaryFollow)
	{
		const FVector PredictedRadial =
			(PredictedPeriapsisWorld - SatelliteCenter).GetSafeNormal();
		FVector CandidateNormal = FVector::CrossProduct(
			PredictedRadial.IsNearlyZero() ? BirdRadialUp : PredictedRadial,
			PredictedPeriapsisVelocity.IsNearlyZero()
				? Velocity.GetSafeNormal()
				: PredictedPeriapsisVelocity).GetSafeNormal();
		if (CandidateNormal.IsNearlyZero())
		{
			CandidateNormal = FVector::VectorPlaneProject(
				GetActorLocation() - SatelliteCenter,
				BirdRadialUp).GetSafeNormal();
		}
		if (CandidateNormal.IsNearlyZero())
		{
			CandidateNormal = FVector::CrossProduct(
				BirdRadialUp,
				FMath::Abs(BirdRadialUp.Z) < 0.9f
					? FVector::UpVector
					: FVector::ForwardVector).GetSafeNormal();
		}
		if (FVector::DotProduct(
			CandidateNormal,
			GetActorLocation() - SatelliteCenter) < 0.0f)
		{
			CandidateNormal *= -1.0f;
		}
		SatelliteOrbitViewNormal = CandidateNormal;
		SetSatelliteFlightPhase(
			EABTSM9SatelliteFlightCameraPhase::SatelliteApproach);
	}
	if (bSatelliteE5Hit)
	{
		SetSatelliteFlightPhase(
			EABTSM9SatelliteFlightCameraPhase::E5Impact);
	}
	else
	{
		const FVector E5Location = TargetE5->GetActorLocation();
		const FVector E5Up =
			(E5Location - SatelliteCenter).GetSafeNormal();
		const bool bWasE5Approach =
			SatelliteFlightPhase
				== EABTSM9SatelliteFlightCameraPhase::E5Approach;
		const float E5DistanceThresholdCM = bWasE5Approach
			? FMath::Max(
				SatelliteE5ApproachDistanceCM + 100.0f,
				SatelliteE5ApproachExitDistanceCM)
			: FMath::Max(
				100.0f,
				SatelliteE5ApproachDistanceCM);
		const bool bNearE5 =
			SatelliteFlightIntent
				== EABTSM9SatelliteFlightCameraIntent::CinematicE5
			&& FVector::Distance(BirdLocation, E5Location)
				<= E5DistanceThresholdCM
			&& FVector::DotProduct(BirdRadialUp, E5Up)
				> (bWasE5Approach ? -0.08f : 0.0f);
		if (bNearE5)
		{
			bSatelliteE5ApproachLatched = true;
		}
		const bool bWasOrbitFraming =
			SatelliteFlightPhase
				== EABTSM9SatelliteFlightCameraPhase::SatelliteOrbit
			|| bWasE5Approach;
		if (bNearE5 || bSatelliteE5ApproachLatched)
		{
			SetSatelliteFlightPhase(
				EABTSM9SatelliteFlightCameraPhase::E5Approach);
		}
		else if (SatelliteDistanceCM
			<= (bWasOrbitFraming
				? OrbitExitDistanceCM
				: OrbitDistanceCM))
		{
			SetSatelliteFlightPhase(
				EABTSM9SatelliteFlightCameraPhase::SatelliteOrbit);
		}
		else
		{
			SetSatelliteFlightPhase(
				EABTSM9SatelliteFlightCameraPhase::SatelliteApproach);
		}
	}

	const FVector E5Location = TargetE5->GetActorLocation();
	const bool bE5Phase =
		SatelliteFlightPhase
			== EABTSM9SatelliteFlightCameraPhase::E5Approach
		|| SatelliteFlightPhase
			== EABTSM9SatelliteFlightCameraPhase::E5Impact;
	SatelliteApproachCompositionAlpha = FMath::FInterpConstantTo(
		SatelliteApproachCompositionAlpha,
		SatelliteFlightPhase == EABTSM9SatelliteFlightCameraPhase::PrimaryFollow
			? 0.0f
			: 1.0f,
		DeltaSeconds,
		1.0f / FMath::Max(0.1f, SatelliteApproachCompositionBlendSeconds));
	SatelliteE5CompositionAlpha = FMath::FInterpConstantTo(
		SatelliteE5CompositionAlpha,
		bE5Phase ? 1.0f : 0.0f,
		DeltaSeconds,
		1.0f / FMath::Max(0.1f, SatelliteE5CompositionBlendSeconds));
	const float ApproachCompositionBlend = FMath::SmoothStep(
		0.0f, 1.0f, SatelliteApproachCompositionAlpha);
	const float E5CompositionBlend = FMath::SmoothStep(
		0.0f, 1.0f, SatelliteE5CompositionAlpha);

	// M9 is a building strike, not a miniature finale. Once the hand-off is
	// complete, reuse the same bird-follow grammar as a ground launch and only
	// exchange the radial frame from the primary planet to the satellite.
	// This keeps the bird readable while allowing E5 to enter naturally in
	// front of it. The approach phase is the sole, mild pull-back transition.
	if (SatelliteFlightPhase
		== EABTSM9SatelliteFlightCameraPhase::SatelliteApproach)
	{
		// Approach and orbit must converge on the same spatial pose. The previous
		// approach still interpolated towards a primary-planet camera location,
		// then changed to a moon-ground pose at SatelliteOrbit. That target-position
		// discontinuity pushed the moon out of frame for ~25 frames even though the
		// rotation itself was rate-limited. Establish the moon-radial camera position
		// early; only its screen Up continues the smooth reference-frame hand-off.
		FVector MoonForward = FVector::VectorPlaneProject(
			Velocity,
			BirdRadialUp).GetSafeNormal();
		if (MoonForward.IsNearlyZero())
		{
			MoonForward = FVector::VectorPlaneProject(
				StableFollowForward,
				BirdRadialUp).GetSafeNormal();
		}
		if (MoonForward.IsNearlyZero())
		{
			MoonForward = FVector::VectorPlaneProject(
				TargetBird.GetActorForwardVector(),
				BirdRadialUp).GetSafeNormal();
		}
		if (MoonForward.IsNearlyZero()) return false;
		StableFollowForward = MoonForward;
		const float SideBlend = bSatelliteConstantBirdScaleExperiment
			? 0.0f
			: FMath::SmoothStep(
			0.0f,
			1.0f,
			FMath::Clamp(
				(EnterDistanceCM - SatelliteDistanceCM)
					/ FMath::Max(1.0f, EnterDistanceCM - OrbitDistanceCM),
				0.0f,
				1.0f));
		FVector GroundHorizontal = -MoonForward;
		FVector SideHorizontal = SatelliteOrbitViewNormal.GetSafeNormal();
		if (SideHorizontal.IsNearlyZero()) SideHorizontal = GroundHorizontal;
		if (FVector::DotProduct(SideHorizontal, GetActorLocation() - BirdLocation) < 0.0f)
		{
			SideHorizontal *= -1.0f;
		}
		FVector HorizontalDirection = FMath::Lerp(
			GroundHorizontal,
			SideHorizontal,
			SideBlend).GetSafeNormal();
		if (HorizontalDirection.IsNearlyZero()) HorizontalDirection = GroundHorizontal;
		const float SideDistanceCM = FMath::Max(
			FlightDistanceCM * FMath::Max(1.0f, SatelliteTransitionPullbackMultiplier),
			SatelliteRadiusCM * 2.6f);
		const float CameraDistanceCM = FMath::Lerp(
			FlightDistanceCM * FMath::Max(1.0f, SatelliteTransitionPullbackMultiplier),
			SideDistanceCM,
			SideBlend);
		const FVector MoonDesiredLocation = BirdLocation
			+ HorizontalDirection * CameraDistanceCM
			+ BirdRadialUp * FlightHeightCM;
		FVector PrimaryDesiredLocation;
		FQuat PrimaryDesiredRotation;
		if (!BuildPrimaryFollowPose(
			TargetBird, PrimaryDesiredLocation, PrimaryDesiredRotation))
		{
			return false;
		}
		FVector DesiredLocation = FMath::Lerp(
			PrimaryDesiredLocation,
			MoonDesiredLocation,
			ApproachCompositionBlend);
		if (bSatelliteConstantBirdScaleExperiment)
		{
			const float BirdDistanceCM =
				ABTSM9SatelliteCameraPrivate::ComputeGroundFollowBirdDistanceCM(
					FlightDistanceCM,
					FlightHeightCM);
			const FVector PrimaryDirection =
				(PrimaryDesiredLocation - BirdLocation).GetSafeNormal();
			const FVector MoonDirection =
				(MoonDesiredLocation - BirdLocation).GetSafeNormal();
			const FVector BlendedDirection = BlendSurfaceUpStable(
				PrimaryDirection,
				MoonDirection,
				PresentationUp,
				ApproachCompositionBlend);
			if (BlendedDirection.IsNearlyZero()) return false;
			// Both endpoints are directions on the same bird-centred sphere.
			// Interpolating their world positions and normalizing afterwards made
			// angular speed surge mid-transition. SmoothStep + spherical direction
			// interpolation has zero endpoint velocity and joins the orbit pose
			// without the visible 61-75 frame arc.
			DesiredLocation = BirdLocation + BlendedDirection * BirdDistanceCM;
		}
		const FVector TransitionFocus = BirdLocation + PresentationUp * 80.0f;
		const float BlendSpeed = FMath::Max(0.1f, SatelliteFollowBlendSpeed);
		FVector Location = FMath::VInterpTo(
			GetActorLocation(), DesiredLocation, DeltaSeconds, BlendSpeed);
		if (bSatelliteConstantBirdScaleExperiment)
		{
			const float BirdDistanceCM =
				ABTSM9SatelliteCameraPrivate::ComputeGroundFollowBirdDistanceCM(
					FlightDistanceCM,
					FlightHeightCM);
			Location = ConstrainCameraToBirdDistance(
				Location,
				BirdLocation,
				BirdDistanceCM,
				HorizontalDirection);
			Location = ConstrainFixedDistanceCameraForSatelliteVisibility(
				Location,
				BirdLocation,
				SatelliteCenter,
				SatelliteRadiusCM,
				BirdDistanceCM,
				SatelliteConstantBirdScaleFieldOfViewDegrees,
				GetCameraComponent()->AspectRatio);
		}
		const FVector BirdLook =
			(TransitionFocus - Location).GetSafeNormal();
		const FVector MoonConstrainedLook =
			ABTSM9SatelliteCameraPrivate::KeepSatelliteLimbVisible(
				Location,
				TransitionFocus,
				SatelliteCenter,
				SatelliteRadiusCM,
				GetCameraComponent()->FieldOfView,
				GetCameraComponent()->AspectRatio);
		const FVector TransitionLook = FMath::Lerp(
			BirdLook,
			MoonConstrainedLook,
			ApproachCompositionBlend).GetSafeNormal();
		FVector TransitionUp = FVector::VectorPlaneProject(
			PresentationUp, TransitionLook).GetSafeNormal();
		if (TransitionLook.IsNearlyZero() || TransitionUp.IsNearlyZero())
		{
			return false;
		}
		const FQuat TransitionRotation = FRotationMatrix::MakeFromXZ(
			TransitionLook, TransitionUp).ToQuat();
		// Both the outgoing and incoming poses focus the bird. Rebuilding rotation
		// from the interpolated location keeps that invariant; separately limiting
		// the quaternion lagged behind the moving bird and let it leave frame.
		SetActorLocationAndRotation(Location, TransitionRotation);
		return true;
	}

	FVector LocalForward = ResolveStableFollowForward(
		TargetBird,
		PresentationUp,
		Velocity,
		bSatelliteSurfaceContact || bSatelliteE5Hit);
	if (LocalForward.IsNearlyZero())
	{
		LocalForward = FVector::VectorPlaneProject(
			PredictedPeriapsisVelocity,
			PresentationUp).GetSafeNormal();
	}
	if (LocalForward.IsNearlyZero())
	{
		LocalForward = FVector::VectorPlaneProject(
			TargetBird.GetActorForwardVector(),
			PresentationUp).GetSafeNormal();
	}
	if (LocalForward.IsNearlyZero()) return false;
	const FVector GroundStyleFocus = BirdLocation + PresentationUp * 80.0f;
	const FVector Focus = FMath::Lerp(
		GroundStyleFocus,
		E5Location,
		FMath::Clamp(SatelliteE5LookAheadBias, 0.0f, 0.35f)
			* E5CompositionBlend);
	FVector GroundHorizontal = -LocalForward;
	FVector SideHorizontal = SatelliteOrbitViewNormal.GetSafeNormal();
	if (SideHorizontal.IsNearlyZero()) SideHorizontal = GroundHorizontal;
	if (FVector::DotProduct(SideHorizontal, GetActorLocation() - BirdLocation) < 0.0f)
	{
		SideHorizontal *= -1.0f;
	}
	const float BaseOrbitSideBlend = FMath::SmoothStep(
			0.0f,
			1.0f,
			FMath::Clamp(
				(SatelliteDistanceCM - SatelliteRadiusCM * 1.45f)
					/ FMath::Max(1.0f, OrbitDistanceCM - SatelliteRadiusCM * 1.45f),
				0.0f,
				1.0f));
	const float OrbitSideBlend = bSatelliteConstantBirdScaleExperiment
		? 0.0f
		: FMath::Lerp(
		BaseOrbitSideBlend,
		0.0f,
		E5CompositionBlend);
	FVector HorizontalDirection = FMath::Lerp(
		GroundHorizontal,
		SideHorizontal,
		OrbitSideBlend).GetSafeNormal();
	if (HorizontalDirection.IsNearlyZero()) HorizontalDirection = GroundHorizontal;
	const float SideDistanceCM = FMath::Max(
		FlightDistanceCM,
		SatelliteRadiusCM * 2.6f);
	const FVector DesiredLocation = BirdLocation
		+ HorizontalDirection
			* FMath::Lerp(FlightDistanceCM, SideDistanceCM, OrbitSideBlend)
		+ PresentationUp * FlightHeightCM;
	const float BlendSpeed =
		FMath::Max(0.1f, SatelliteFollowBlendSpeed);
	FVector Location = FMath::VInterpTo(
		GetActorLocation(),
		DesiredLocation,
		DeltaSeconds,
		BlendSpeed);
	if (bSatelliteConstantBirdScaleExperiment)
	{
		const float BirdDistanceCM =
			ABTSM9SatelliteCameraPrivate::ComputeGroundFollowBirdDistanceCM(
				FlightDistanceCM,
				FlightHeightCM);
		Location = ConstrainCameraToBirdDistance(
			Location,
			BirdLocation,
			BirdDistanceCM,
			HorizontalDirection);
		Location = ConstrainFixedDistanceCameraForSatelliteVisibility(
			Location,
			BirdLocation,
			SatelliteCenter,
			SatelliteRadiusCM,
			BirdDistanceCM,
			SatelliteConstantBirdScaleFieldOfViewDegrees,
			GetCameraComponent()->AspectRatio);
	}
	const FVector Look = ABTSM9SatelliteCameraPrivate::KeepSatelliteLimbVisible(
		Location,
		Focus,
		SatelliteCenter,
		SatelliteRadiusCM,
		GetCameraComponent()->FieldOfView,
		GetCameraComponent()->AspectRatio);
	FVector ScreenUp = FVector::VectorPlaneProject(
		PresentationUp, Look).GetSafeNormal();
	if (Look.IsNearlyZero()) return false;
	if (ScreenUp.IsNearlyZero()) return false;
	// Position may blend between the primary and satellite radial frames, but
	// the lens must keep the bird as its invariant target. Interpolating the
	// old and new quaternions independently lets the bird leave frame midway
	// through the hand-off even though both endpoint poses are valid.
	const FQuat Rotation = FRotationMatrix::MakeFromXZ(
		Look, ScreenUp).ToQuat();
	SetActorLocationAndRotation(Location, Rotation);
	return true;
}

