// Copyright Epic Games, Inc. All Rights Reserved.

#include "Presentation/ABTSOpeningCinematicTypes.h"

namespace
{
	constexpr float PhaseDegrees[] = {-90.0f, -18.0f, 54.0f, 126.0f, 198.0f};

	float EaseInOut(const float Alpha)
	{
		return FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(Alpha, 0.0f, 1.0f));
	}

	float BirdAngularSpeed(const EABTSOpeningBird Bird)
	{
		return FABTSOpeningCinematicEvaluator::CircleAngularSpeedRadiansPerSecond
			* (Bird == EABTSOpeningBird::White ? 1.08f : 1.0f);
	}

	FVector CirclePosition(const EABTSOpeningBird Bird, const float RunningSeconds)
	{
		const int32 BirdIndex = static_cast<int32>(Bird);
		const float Phase = FMath::DegreesToRadians(PhaseDegrees[BirdIndex])
			+ BirdAngularSpeed(Bird) * RunningSeconds;
		return FVector(FMath::Cos(Phase), FMath::Sin(Phase), 0.0f)
			* FABTSOpeningCinematicEvaluator::CircleRadiusCM;
	}

	FVector CircleFacing(const EABTSOpeningBird Bird, const float RunningSeconds)
	{
		const int32 BirdIndex = static_cast<int32>(Bird);
		const float Phase = FMath::DegreesToRadians(PhaseDegrees[BirdIndex])
			+ BirdAngularSpeed(Bird) * RunningSeconds;
		return FVector(-FMath::Sin(Phase), FMath::Cos(Phase), 0.0f).GetSafeNormal();
	}

	FVector HandoffPosition(const EABTSOpeningBird Bird)
	{
		switch (Bird)
		{
		case EABTSOpeningBird::Red: return FVector(180.0f, 0.0f, 0.0f);
		case EABTSOpeningBird::Blue: return FVector(0.0f, -145.0f, 0.0f);
		case EABTSOpeningBird::Yellow: return FVector(0.0f, 145.0f, 0.0f);
		case EABTSOpeningBird::Black: return FVector(-180.0f, 0.0f, 0.0f);
		default: return FVector::ZeroVector;
		}
	}

	bool IsCelebrateWindow(
		const float Time,
		const EABTSOpeningBird Bird)
	{
		const float BirdOffset = static_cast<float>(static_cast<int32>(Bird));
		const float EstablishStart = 0.35f + BirdOffset * 0.58f;
		const float CircleStart = 4.75f + BirdOffset * 1.05f;
		return (Time >= EstablishStart && Time < EstablishStart + 1.55f)
			|| (Time >= CircleStart && Time < CircleStart + 1.55f);
	}
}

EABTSOpeningPhase FABTSOpeningCinematicEvaluator::ResolvePhase(const float TimeSeconds)
{
	if (TimeSeconds < 4.0f) return EABTSOpeningPhase::Establish;
	if (TimeSeconds < 12.0f) return EABTSOpeningPhase::CirclePlay;
	if (TimeSeconds < 16.0f) return EABTSOpeningPhase::WhiteBirdCloseUp;
	if (TimeSeconds < 21.0f) return EABTSOpeningPhase::ThreatReveal;
	if (TimeSeconds < 27.0f) return EABTSOpeningPhase::Capture;
	if (TimeSeconds < 35.0f) return EABTSOpeningPhase::Departure;
	if (TimeSeconds < DurationSeconds) return EABTSOpeningPhase::Handoff;
	return EABTSOpeningPhase::Complete;
}

FVector FABTSOpeningCinematicEvaluator::GetWhiteBirdCaptureBase()
{
	return CirclePosition(EABTSOpeningBird::White, 8.0f);
}

FABTSOpeningBirdPose FABTSOpeningCinematicEvaluator::EvaluateBird(
	const float TimeSeconds,
	const EABTSOpeningBird Bird)
{
	const float Time = FMath::Clamp(TimeSeconds, 0.0f, DurationSeconds);
	FABTSOpeningBirdPose Pose;
	Pose.LocalPosition = CirclePosition(Bird, 0.0f);
	Pose.LocalFacing = CircleFacing(Bird, 0.0f);

	if (Time < 4.0f)
	{
		if (IsCelebrateWindow(Time, Bird))
		{
			Pose.AnimationCue = EABTSOpeningAnimationCue::Celebrate;
		}
		return Pose;
	}

	const float RunningSeconds = FMath::Min(Time - 4.0f, 8.0f);
	Pose.LocalPosition = CirclePosition(Bird, RunningSeconds);
	Pose.LocalFacing = CircleFacing(Bird, RunningSeconds);
	Pose.AnimationCue = EABTSOpeningAnimationCue::Move;
	if (IsCelebrateWindow(Time, Bird))
	{
		Pose.AnimationCue = EABTSOpeningAnimationCue::Celebrate;
	}
	if (Time < 12.0f) return Pose;

	if (Bird == EABTSOpeningBird::White)
	{
		const FVector CaptureBase = GetWhiteBirdCaptureBase();
		Pose.LocalPosition = CaptureBase;
		Pose.LocalFacing = FVector(0.35f, -1.0f, 0.0f).GetSafeNormal();
		Pose.AnimationCue = EABTSOpeningAnimationCue::Idle;
		if (Time >= 21.0f && Time < 27.0f)
		{
			const float Alpha = EaseInOut((Time - 21.0f) / 6.0f);
			Pose.LocalPosition = CaptureBase + FVector(0.0f, 0.0f, 475.0f * Alpha);
			Pose.AnimationCue = EABTSOpeningAnimationCue::Fly;
		}
		else if (Time >= 27.0f && Time < 35.0f)
		{
			const FABTSOpeningUFOPose UFO = EvaluateUFO(Time);
			Pose.LocalPosition = UFO.LocalPosition - FVector(0.0f, 0.0f, 65.0f);
			Pose.AnimationCue = EABTSOpeningAnimationCue::Fly;
			Pose.bVisible = UFO.bVisible;
		}
		else if (Time >= 35.0f)
		{
			Pose.bVisible = false;
		}
		return Pose;
	}

	if (Time < 16.0f) return Pose;

	Pose.AnimationCue = EABTSOpeningAnimationCue::Idle;
	Pose.LocalFacing = (GetWhiteBirdCaptureBase() - Pose.LocalPosition).GetSafeNormal();
	if (Time >= 21.0f && Time < 35.0f)
	{
		const float ApproachDistances[] = {220.0f, 160.0f, 180.0f, 120.0f};
		const FVector ToWhite = (GetWhiteBirdCaptureBase() - Pose.LocalPosition).GetSafeNormal();
		const float Alpha = EaseInOut(FMath::Min((Time - 21.0f) / 2.2f, 1.0f));
		Pose.LocalPosition += ToWhite * ApproachDistances[static_cast<int32>(Bird)] * Alpha;
		Pose.AnimationCue = Time < 23.2f ? EABTSOpeningAnimationCue::Move : EABTSOpeningAnimationCue::Idle;
	}
	else if (Time >= 35.0f)
	{
		const float Alpha = EaseInOut((Time - 35.0f) / 2.0f);
		Pose.LocalPosition = FMath::Lerp(Pose.LocalPosition, HandoffPosition(Bird), Alpha);
		Pose.LocalFacing = FVector::ForwardVector;
		Pose.AnimationCue = Alpha < 1.0f ? EABTSOpeningAnimationCue::Move : EABTSOpeningAnimationCue::Idle;
	}
	return Pose;
}

FABTSOpeningUFOPose FABTSOpeningCinematicEvaluator::EvaluateUFO(const float TimeSeconds)
{
	FABTSOpeningUFOPose Pose;
	const float Time = FMath::Clamp(TimeSeconds, 0.0f, DurationSeconds);
	const FVector CaptureBase = GetWhiteBirdCaptureBase();
	if (Time < 16.0f || Time >= 35.0f) return Pose;
	Pose.bVisible = true;
	if (Time < 21.0f)
	{
		const float Alpha = EaseInOut((Time - 16.0f) / 5.0f);
		Pose.LocalPosition = FMath::Lerp(FVector(1100.0f, 350.0f, 900.0f),
			CaptureBase + FVector(0.0f, 0.0f, 540.0f), Alpha);
	}
	else if (Time < 27.0f)
	{
		Pose.LocalPosition = CaptureBase + FVector(0.0f, 0.0f, 540.0f);
		Pose.bCaptureBeamVisible = true;
	}
	else if (Time < 31.0f)
	{
		const float Alpha = EaseInOut((Time - 27.0f) / 4.0f);
		Pose.LocalPosition = FMath::Lerp(CaptureBase + FVector(0.0f, 0.0f, 540.0f),
			FVector(900.0f, 0.0f, 1300.0f), Alpha);
	}
	else
	{
		const float Alpha = EaseInOut((Time - 31.0f) / 4.0f);
		Pose.LocalPosition = FMath::Lerp(FVector(900.0f, 0.0f, 1300.0f),
			FVector(5000.0f, 0.0f, 5000.0f), Alpha);
	}
	Pose.LocalRotation = FRotator(
		1.8f * FMath::Sin(Time * 3.1f),
		Time * 8.0f,
		2.4f * FMath::Sin(Time * 2.3f));
	return Pose;
}

FABTSOpeningCameraPose FABTSOpeningCinematicEvaluator::EvaluateCamera(const float TimeSeconds)
{
	const float Time = FMath::Clamp(TimeSeconds, 0.0f, DurationSeconds);
	FABTSOpeningCameraPose Pose;
	if (Time < 4.0f)
	{
		const float Alpha = EaseInOut(Time / 4.0f);
		Pose.LocalPosition = FMath::Lerp(FVector(-900.0f, -1060.0f, 670.0f), FVector(-820.0f, -980.0f, 620.0f), Alpha);
		Pose.LocalLookAt = FVector(0.0f, 0.0f, 45.0f);
		Pose.FieldOfViewDegrees = 50.0f;
	}
	else if (Time < 12.0f)
	{
		const float Alpha = (Time - 4.0f) / 8.0f;
		const float Azimuth = FMath::DegreesToRadians(-132.0f - 35.0f * Alpha);
		Pose.LocalPosition = FVector(FMath::Cos(Azimuth) * 920.0f, FMath::Sin(Azimuth) * 920.0f, 530.0f);
		Pose.LocalLookAt = FVector(0.0f, 0.0f, 55.0f);
		Pose.FieldOfViewDegrees = 50.0f;
	}
	else if (Time < 16.0f)
	{
		Pose.LocalPosition = FVector(260.0f, -510.0f, 220.0f);
		Pose.LocalLookAt = GetWhiteBirdCaptureBase() + FVector(0.0f, 0.0f, 45.0f);
		Pose.FieldOfViewDegrees = 42.0f;
	}
	else if (Time < 21.0f)
	{
		Pose.LocalPosition = FVector(-180.0f, -760.0f, 560.0f);
		Pose.LocalLookAt = FMath::Lerp(GetWhiteBirdCaptureBase(), EvaluateUFO(Time).LocalPosition, 0.55f);
		Pose.FieldOfViewDegrees = 48.0f;
	}
	else if (Time < 27.0f)
	{
		Pose.LocalPosition = FVector(-480.0f, -660.0f, 150.0f);
		Pose.LocalLookAt = GetWhiteBirdCaptureBase() + FVector(0.0f, 0.0f, 280.0f);
		Pose.FieldOfViewDegrees = 55.0f;
	}
	else if (Time < 35.0f)
	{
		Pose.LocalPosition = FVector(-1200.0f, -800.0f, 880.0f);
		Pose.LocalLookAt = EvaluateUFO(Time).LocalPosition;
		Pose.FieldOfViewDegrees = 52.0f;
	}
	else
	{
		Pose.LocalPosition = FVector(-720.0f, -850.0f, 520.0f);
		Pose.LocalLookAt = FVector(0.0f, 0.0f, 55.0f);
		Pose.FieldOfViewDegrees = 50.0f;
	}
	return Pose;
}
