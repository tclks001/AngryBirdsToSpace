// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11FinalePostHitCinematicTypes.h"

namespace
{
	float Ease(const float Alpha)
	{
		return FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(Alpha, 0.0f, 1.0f));
	}

	FVector ImpactPosition(const EABTSM11FinalePostHitBird Bird)
	{
		switch (Bird)
		{
		case EABTSM11FinalePostHitBird::Red:
			return FVector(-80.0f, -42.0f, 0.0f);
		case EABTSM11FinalePostHitBird::Blue:
			return FVector(-80.0f, 14.0f, 0.0f);
		case EABTSM11FinalePostHitBird::Yellow:
			return FVector(-80.0f, 42.0f, 0.0f);
		case EABTSM11FinalePostHitBird::Black:
			return FVector(-80.0f, -14.0f, 0.0f);
		default:
			return FVector(0.0f, 0.0f, 120.0f);
		}
	}

	FVector ImpactSeparationPosition(const EABTSM11FinalePostHitBird Bird)
	{
		switch (Bird)
		{
		case EABTSM11FinalePostHitBird::Red:
			return FVector(80.0f, -210.0f, 100.0f);
		case EABTSM11FinalePostHitBird::Blue:
			return FVector(-20.0f, -150.0f, -100.0f);
		case EABTSM11FinalePostHitBird::Yellow:
			return FVector(80.0f, 210.0f, 100.0f);
		case EABTSM11FinalePostHitBird::Black:
			return FVector(-20.0f, 150.0f, -100.0f);
		default:
			return FVector(0.0f, 0.0f, 120.0f);
		}
	}

	FVector RescueFormationPosition(const EABTSM11FinalePostHitBird Bird)
	{
		switch (Bird)
		{
		case EABTSM11FinalePostHitBird::Red:
			return FVector(430.0f, -180.0f, 100.0f);
		case EABTSM11FinalePostHitBird::Blue:
			return FVector(300.0f, -140.0f, -80.0f);
		case EABTSM11FinalePostHitBird::Yellow:
			return FVector(430.0f, 180.0f, 100.0f);
		case EABTSM11FinalePostHitBird::Black:
			return FVector(300.0f, 140.0f, -80.0f);
		default:
			return FVector(650.0f, 0.0f, 160.0f);
		}
	}

	float OrbitPhaseRadians(const EABTSM11FinalePostHitBird Bird)
	{
		constexpr float PhaseStep = 2.0f * UE_PI / 5.0f;
		switch (Bird)
		{
		case EABTSM11FinalePostHitBird::White:
			return 0.0f;
		case EABTSM11FinalePostHitBird::Red:
			return PhaseStep;
		case EABTSM11FinalePostHitBird::Blue:
			return PhaseStep * 2.0f;
		case EABTSM11FinalePostHitBird::Yellow:
			return PhaseStep * 3.0f;
		case EABTSM11FinalePostHitBird::Black:
			return PhaseStep * 4.0f;
		default:
			return 0.0f;
		}
	}

	FABTSM11FinalePostHitBirdPose EvaluateOrbitBird(
		const float TimeSeconds,
		const EABTSM11FinalePostHitBird Bird)
	{
		FABTSM11FinalePostHitBirdPose Pose;
		const float OrbitSeconds = FMath::Max(
			0.0f,
			TimeSeconds
				- FABTSM11FinalePostHitCinematicEvaluator::ReformationEndSeconds);
		const float Phase = OrbitPhaseRadians(Bird)
			+ OrbitSeconds
				* FABTSM11FinalePostHitCinematicEvaluator::FiveBirdOrbitRadiansPerSecond;
		float Radius = FABTSM11FinalePostHitCinematicEvaluator::FiveBirdOrbitRadiusCM;
		if (Bird == EABTSM11FinalePostHitBird::White
			&& TimeSeconds
				>= FABTSM11FinalePostHitCinematicEvaluator::OrbitEndSeconds)
		{
			const float EndingAlpha = Ease(
				(TimeSeconds
					- FABTSM11FinalePostHitCinematicEvaluator::OrbitEndSeconds)
				/ (FABTSM11FinalePostHitCinematicEvaluator::DurationSeconds
					- FABTSM11FinalePostHitCinematicEvaluator::OrbitEndSeconds));
			Radius *= 1.0f + 0.12f * FMath::Sin(EndingAlpha * 2.0f * UE_PI);
		}
		Pose.LocalPosition =
			FABTSM11FinalePostHitCinematicEvaluator::GetOrbitCenter()
			+ FVector(
				18.0f * FMath::Sin(Phase * 2.0f),
				FMath::Cos(Phase) * Radius,
				FMath::Sin(Phase) * Radius);
		Pose.LocalFacing = FVector(
			0.35f,
			-FMath::Sin(Phase),
			FMath::Cos(Phase)).GetSafeNormal();
		Pose.AnimationCue = EABTSM11FinalePostHitAnimationCue::Fly;
		return Pose;
	}

	bool Crossed(
		const float PreviousTimeSeconds,
		const float CurrentTimeSeconds,
		const float EventTimeSeconds)
	{
		return PreviousTimeSeconds < EventTimeSeconds
			&& CurrentTimeSeconds >= EventTimeSeconds;
	}
}

EABTSM11FinalePostHitPhase
FABTSM11FinalePostHitCinematicEvaluator::ResolvePhase(const float TimeSeconds)
{
	if (TimeSeconds < ImpactEndSeconds)
	{
		return EABTSM11FinalePostHitPhase::Impact;
	}
	if (TimeSeconds < RescueEndSeconds)
	{
		return EABTSM11FinalePostHitPhase::Rescue;
	}
	if (TimeSeconds < ReformationEndSeconds)
	{
		return EABTSM11FinalePostHitPhase::Reformation;
	}
	if (TimeSeconds < OrbitEndSeconds)
	{
		return EABTSM11FinalePostHitPhase::FiveBirdOrbit;
	}
	if (TimeSeconds < DurationSeconds)
	{
		return EABTSM11FinalePostHitPhase::Ending;
	}
	return EABTSM11FinalePostHitPhase::Complete;
}

FABTSM11FinalePostHitBirdPose
FABTSM11FinalePostHitCinematicEvaluator::EvaluateBird(
	const float TimeSeconds,
	const EABTSM11FinalePostHitBird Bird)
{
	const float Time = FMath::Clamp(TimeSeconds, 0.0f, DurationSeconds);
	FABTSM11FinalePostHitBirdPose Pose;
	Pose.LocalPosition = ImpactPosition(Bird);
	Pose.LocalFacing = FVector::ForwardVector;
	Pose.AnimationCue = Bird == EABTSM11FinalePostHitBird::White
		? EABTSM11FinalePostHitAnimationCue::Damage
		: EABTSM11FinalePostHitAnimationCue::Impact;

	if (Time < ImpactEndSeconds)
	{
		if (Bird == EABTSM11FinalePostHitBird::White)
		{
			const float ShakeAlpha = FMath::Max(
				0.0f,
				1.0f - Time / ImpactEndSeconds);
			Pose.LocalPosition.Y +=
				FMath::Sin(Time * 42.0f) * 9.0f * ShakeAlpha;
			return Pose;
		}
		const float SeparationAlpha = Ease(
			(Time - ImpactBreakCueSeconds)
			/ (ImpactEndSeconds - ImpactBreakCueSeconds));
		Pose.LocalPosition = FMath::Lerp(
			ImpactPosition(Bird),
			ImpactSeparationPosition(Bird),
			SeparationAlpha);
		Pose.LocalFacing = (Pose.LocalPosition - FVector::ZeroVector)
			.GetSafeNormal();
		return Pose;
	}

	Pose.AnimationCue = EABTSM11FinalePostHitAnimationCue::Fly;
	if (Time < RescueEndSeconds)
	{
		const float RescueAlpha = Ease(
			(Time - ImpactEndSeconds)
			/ (RescueEndSeconds - ImpactEndSeconds));
		const FVector Start = Bird == EABTSM11FinalePostHitBird::White
			? FVector(0.0f, 0.0f, 120.0f)
			: ImpactSeparationPosition(Bird);
		Pose.LocalPosition = FMath::Lerp(
			Start,
			RescueFormationPosition(Bird),
			RescueAlpha);
		Pose.LocalFacing = FVector::ForwardVector;
		return Pose;
	}

	const FABTSM11FinalePostHitBirdPose OrbitPose = EvaluateOrbitBird(
		Time,
		Bird);
	if (Time < ReformationEndSeconds)
	{
		const float ReformationAlpha = Ease(
			(Time - RescueEndSeconds)
			/ (ReformationEndSeconds - RescueEndSeconds));
		Pose.LocalPosition = FMath::Lerp(
			RescueFormationPosition(Bird),
			EvaluateOrbitBird(ReformationEndSeconds, Bird).LocalPosition,
			ReformationAlpha);
		Pose.LocalFacing = FMath::Lerp(
			FVector::ForwardVector,
			EvaluateOrbitBird(ReformationEndSeconds, Bird).LocalFacing,
			ReformationAlpha).GetSafeNormal();
		return Pose;
	}
	return OrbitPose;
}

FABTSM11FinalePostHitUFOPose
FABTSM11FinalePostHitCinematicEvaluator::EvaluateUFO(const float TimeSeconds)
{
	const float Time = FMath::Clamp(TimeSeconds, 0.0f, DurationSeconds);
	FABTSM11FinalePostHitUFOPose Pose;
	Pose.bIntactVisible = Time < ImpactBreakCueSeconds;
	Pose.bBrokenVisible = Time >= ImpactBreakCueSeconds && Time < 3.7f;
	Pose.FlashAlpha = Time >= ImpactBreakCueSeconds && Time < 0.58f
		? 1.0f - Ease((Time - ImpactBreakCueSeconds) / 0.4f)
		: 0.0f;
	Pose.BrokenFadeAlpha = Time >= ImpactBreakCueSeconds
		? Ease((Time - ImpactBreakCueSeconds) / 2.5f)
		: 0.0f;
	return Pose;
}

FABTSM11FinalePostHitCameraPose
FABTSM11FinalePostHitCinematicEvaluator::EvaluateCamera(const float TimeSeconds)
{
	const float Time = FMath::Clamp(TimeSeconds, 0.0f, DurationSeconds);
	FABTSM11FinalePostHitCameraPose Pose;
	if (Time < ImpactEndSeconds)
	{
		const float Recoil = Ease(Time / ImpactEndSeconds);
		Pose.LocalPosition = FMath::Lerp(
			FVector(350.0f, -1300.0f, 320.0f),
			FVector(260.0f, -1420.0f, 350.0f),
			Recoil);
		Pose.LocalLookAt = FVector(20.0f, 0.0f, 55.0f);
		Pose.FieldOfViewDegrees = 62.0f;
		return Pose;
	}
	if (Time < RescueEndSeconds)
	{
		const float Alpha = Ease(
			(Time - ImpactEndSeconds)
			/ (RescueEndSeconds - ImpactEndSeconds));
		Pose.LocalPosition = FMath::Lerp(
			FVector(260.0f, -920.0f, 280.0f),
			FVector(320.0f, -620.0f, 220.0f),
			Alpha);
		Pose.LocalLookAt = FMath::Lerp(
			FVector(0.0f, 0.0f, 120.0f),
			FVector(650.0f, 0.0f, 160.0f),
			Alpha);
		Pose.FieldOfViewDegrees = 45.0f;
		return Pose;
	}

	const FVector Center = GetOrbitCenter();
	const FVector OrbitOffset(-1050.0f, -1250.0f, 800.0f);
	if (Time < ReformationEndSeconds)
	{
		const float Alpha = Ease(
			(Time - RescueEndSeconds)
			/ (ReformationEndSeconds - RescueEndSeconds));
		Pose.LocalPosition = FMath::Lerp(
			FVector(320.0f, -620.0f, 220.0f),
			Center + OrbitOffset,
			Alpha);
		Pose.LocalLookAt = FMath::Lerp(
			FVector(650.0f, 0.0f, 160.0f),
			Center,
			Alpha);
		Pose.FieldOfViewDegrees = FMath::Lerp(45.0f, 50.0f, Alpha);
		return Pose;
	}

	const float OrbitAlpha = FMath::Clamp(
		(Time - ReformationEndSeconds)
		/ (OrbitEndSeconds - ReformationEndSeconds),
		0.0f,
		1.0f);
	const float OrbitDegrees = 25.0f * Ease(OrbitAlpha);
	FVector CameraOffset = FRotator(0.0f, OrbitDegrees, 0.0f)
		.RotateVector(OrbitOffset);
	if (Time >= OrbitEndSeconds)
	{
		const float EndingAlpha = Ease(
			(Time - OrbitEndSeconds) / (DurationSeconds - OrbitEndSeconds));
		CameraOffset *= FMath::Lerp(1.0f, 2.2f, EndingAlpha);
		Pose.FieldOfViewDegrees = FMath::Lerp(50.0f, 42.0f, EndingAlpha);
		Pose.FadeToBlackAlpha = Ease((Time - 16.5f) / 1.5f);
	}
	else
	{
		Pose.FieldOfViewDegrees = 50.0f;
	}
	Pose.LocalPosition = Center + CameraOffset;
	Pose.LocalLookAt = Center;
	return Pose;
}

FABTSM11FinalePostHitLightingPose
FABTSM11FinalePostHitCinematicEvaluator::EvaluateLighting(
	const float TimeSeconds)
{
	const float Time = FMath::Clamp(TimeSeconds, 0.0f, DurationSeconds);
	const FABTSM11FinalePostHitCameraPose Camera = EvaluateCamera(Time);
	FVector Forward = (Camera.LocalLookAt - Camera.LocalPosition).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}
	FVector Right = FVector::CrossProduct(FVector::UpVector, Forward)
		.GetSafeNormal();
	if (Right.IsNearlyZero())
	{
		Right = FVector::RightVector;
	}
	const FVector Up = FVector::CrossProduct(Forward, Right).GetSafeNormal();

	FABTSM11FinalePostHitLightingPose Pose;
	Pose.KeyLocalPosition = Camera.LocalLookAt
		- Forward * 520.0f - Right * 360.0f + Up * 480.0f;
	Pose.FillLocalPosition = Camera.LocalLookAt
		- Forward * 280.0f + Right * 540.0f + Up * 80.0f;
	Pose.RimLocalPosition = Camera.LocalLookAt
		+ Forward * 560.0f - Right * 180.0f + Up * 320.0f;

	const float RescueLift = Ease(
		(Time - ImpactEndSeconds) / 1.0f);
	const float ReunionDistance = FMath::Abs(Time - ReunionCueSeconds);
	const float ReunionPulse = ReunionDistance < 0.65f
		? Ease(1.0f - ReunionDistance / 0.65f)
		: 0.0f;
	const float EndingScale = Time >= OrbitEndSeconds
		? FMath::Lerp(
			1.0f,
			0.72f,
			Ease((Time - OrbitEndSeconds)
				/ (DurationSeconds - OrbitEndSeconds)))
		: 1.0f;
	Pose.KeyIntensity = 15000.0f * EndingScale;
	Pose.FillIntensity = (7600.0f + 2600.0f * RescueLift
		+ 2200.0f * ReunionPulse) * EndingScale;
	Pose.RimIntensity = (11800.0f + 5200.0f * ReunionPulse)
		* EndingScale;
	return Pose;
}

EABTSM11FinalePostHitAudioCue
FABTSM11FinalePostHitCinematicEvaluator::ResolveCrossedAudioCues(
	const float PreviousTimeSeconds,
	const float CurrentTimeSeconds)
{
	EABTSM11FinalePostHitAudioCue Cues =
		EABTSM11FinalePostHitAudioCue::None;
	if (Crossed(PreviousTimeSeconds, CurrentTimeSeconds, ImpactBreakCueSeconds))
	{
		Cues |= EABTSM11FinalePostHitAudioCue::ImpactBreak;
	}
	if (Crossed(PreviousTimeSeconds, CurrentTimeSeconds, RescueReleaseCueSeconds))
	{
		Cues |= EABTSM11FinalePostHitAudioCue::RescueRelease;
	}
	if (Crossed(PreviousTimeSeconds, CurrentTimeSeconds, ReunionCueSeconds))
	{
		Cues |= EABTSM11FinalePostHitAudioCue::Reunion;
	}
	if (Crossed(PreviousTimeSeconds, CurrentTimeSeconds, CompletionCueSeconds))
	{
		Cues |= EABTSM11FinalePostHitAudioCue::Completion;
	}
	return Cues;
}

FVector FABTSM11FinalePostHitCinematicEvaluator::GetOrbitCenter()
{
	return FVector(900.0f, 0.0f, 160.0f);
}
