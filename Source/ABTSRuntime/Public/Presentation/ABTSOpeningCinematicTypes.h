// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EABTSOpeningBird : uint8
{
	Red,
	Blue,
	Yellow,
	Black,
	White,
	Count
};

enum class EABTSOpeningAnimationCue : uint8
{
	Idle,
	Move,
	Fly
};

enum class EABTSOpeningPhase : uint8
{
	Establish,
	CirclePlay,
	WhiteBirdCloseUp,
	ThreatReveal,
	Capture,
	Departure,
	Handoff,
	Complete
};

struct FABTSOpeningBirdPose
{
	FVector LocalPosition = FVector::ZeroVector;
	FVector LocalFacing = FVector::ForwardVector;
	EABTSOpeningAnimationCue AnimationCue = EABTSOpeningAnimationCue::Idle;
	bool bVisible = true;
};

struct FABTSOpeningUFOPose
{
	FVector LocalPosition = FVector::ZeroVector;
	FRotator LocalRotation = FRotator::ZeroRotator;
	bool bVisible = false;
	bool bCaptureBeamVisible = false;
};

struct FABTSOpeningCameraPose
{
	FVector LocalPosition = FVector::ZeroVector;
	FVector LocalLookAt = FVector::ZeroVector;
	float FieldOfViewDegrees = 50.0f;
};

/** Deterministic, world-independent evaluation of the 42-second opening direction. */
struct ABTSRUNTIME_API FABTSOpeningCinematicEvaluator
{
	static constexpr float DurationSeconds = 42.0f;
	static constexpr float CircleRadiusCM = 300.0f;
	static constexpr float CircleAngularSpeedRadiansPerSecond = 1.05f;

	static EABTSOpeningPhase ResolvePhase(float TimeSeconds);
	static FABTSOpeningBirdPose EvaluateBird(float TimeSeconds, EABTSOpeningBird Bird);
	static FABTSOpeningUFOPose EvaluateUFO(float TimeSeconds);
	static FABTSOpeningCameraPose EvaluateCamera(float TimeSeconds);
	static FVector GetWhiteBirdCaptureBase();
};
