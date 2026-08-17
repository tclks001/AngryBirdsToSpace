// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EABTSM11FinalePostHitBird : uint8
{
	Red = 0,
	Blue,
	Yellow,
	Black,
	White,
	Count
};

enum class EABTSM11FinalePostHitPhase : uint8
{
	Impact = 0,
	Rescue,
	Reformation,
	FiveBirdOrbit,
	Ending,
	Complete
};

enum class EABTSM11FinalePostHitAnimationCue : uint8
{
	Fly = 0,
	Impact,
	Damage
};

enum class EABTSM11FinalePostHitAudioCue : uint8
{
	None = 0,
	ImpactBreak = 1 << 0,
	RescueRelease = 1 << 1,
	Reunion = 1 << 2,
	Completion = 1 << 3
};

ENUM_CLASS_FLAGS(EABTSM11FinalePostHitAudioCue);

struct FABTSM11FinalePostHitBirdPose
{
	FVector LocalPosition = FVector::ZeroVector;
	FVector LocalFacing = FVector::ForwardVector;
	EABTSM11FinalePostHitAnimationCue AnimationCue =
		EABTSM11FinalePostHitAnimationCue::Fly;
	float VisualScale = 1.0f;
	bool bVisible = true;
};

struct FABTSM11FinalePostHitUFOPose
{
	bool bIntactVisible = true;
	bool bBrokenVisible = false;
	float FlashAlpha = 0.0f;
	float BrokenFadeAlpha = 0.0f;
};

struct FABTSM11FinalePostHitCameraPose
{
	FVector LocalPosition = FVector::ZeroVector;
	FVector LocalLookAt = FVector::ZeroVector;
	float FieldOfViewDegrees = 50.0f;
	float FadeToBlackAlpha = 0.0f;
};

struct FABTSM11FinalePostHitLightingPose
{
	FVector KeyLocalPosition = FVector::ZeroVector;
	FVector FillLocalPosition = FVector::ZeroVector;
	FVector RimLocalPosition = FVector::ZeroVector;
	float KeyIntensity = 0.0f;
	float FillIntensity = 0.0f;
	float RimIntensity = 0.0f;
};

/**
 * World-independent M11-D direction for the first 18 seconds after a physical
 * UFO contact. It never reads Gameplay Actors, collision, solver state or the
 * released trajectory and therefore can be consumed by both isolated preview
 * and a later Integration-owned production binding.
 */
struct ABTSRUNTIME_API FABTSM11FinalePostHitCinematicEvaluator
{
	static constexpr float DurationSeconds = 18.0f;
	static constexpr float ImpactEndSeconds = 1.2f;
	static constexpr float RescueEndSeconds = 5.5f;
	static constexpr float ReformationEndSeconds = 7.0f;
	static constexpr float OrbitEndSeconds = 14.0f;
	static constexpr float ImpactBreakCueSeconds = 0.18f;
	static constexpr float RescueReleaseCueSeconds = 1.45f;
	static constexpr float ReunionCueSeconds = 6.0f;
	static constexpr float CompletionCueSeconds = 14.2f;
	static constexpr float FiveBirdOrbitRadiusCM = 260.0f;
	static constexpr float FiveBirdOrbitRadiansPerSecond = 0.55f;
	static constexpr float CinematicExposureBias = 0.45f;

	static EABTSM11FinalePostHitPhase ResolvePhase(float TimeSeconds);
	static FABTSM11FinalePostHitBirdPose EvaluateBird(
		float TimeSeconds,
		EABTSM11FinalePostHitBird Bird);
	static FABTSM11FinalePostHitUFOPose EvaluateUFO(float TimeSeconds);
	static FABTSM11FinalePostHitCameraPose EvaluateCamera(float TimeSeconds);
	static FABTSM11FinalePostHitLightingPose EvaluateLighting(float TimeSeconds);
	static EABTSM11FinalePostHitAudioCue ResolveCrossedAudioCues(
		float PreviousTimeSeconds,
		float CurrentTimeSeconds);
	static FVector GetOrbitCenter();
};
