// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/ABTSStylizedRenderProfile.h"

struct ABTSRENDER_API FABTSStylizedToneProfileParameters
{
	float ShadowThreshold = 0.22f;
	float HighlightThreshold = 0.62f;
	float TransitionSoftness = 0.07f;
	float Strength = 0.72f;
	float ShadowLuminance = 0.14f;
	float MidLuminance = 0.44f;
	float HighlightLuminance = 0.82f;
	float Saturation = 1.04f;
	FVector3f ShadowTint = FVector3f(0.82f, 0.92f, 1.08f);
	FVector3f MidTint = FVector3f(1.0f, 0.98f, 0.94f);
	FVector3f HighlightTint = FVector3f(1.04f, 1.01f, 0.94f);

	bool IsValid() const;
};

struct ABTSRENDER_API FABTSStylizedOutlineProfileParameters
{
	float WidthPixels = 1.25f;
	float DepthThreshold = 0.012f;
	float DepthSoftness = 0.018f;
	float NormalThreshold = 0.16f;
	float NormalSoftness = 0.18f;
	float Strength = 0.78f;
	FVector3f Color = FVector3f(0.035f, 0.050f, 0.075f);

	bool IsValid() const;
};

/** Stable Integration-owned switch and profile seam for stylized rendering. */
class ABTSRENDER_API FABTSStylizedRenderingControl
{
public:
	static bool IsEnabled();
	static bool IsEnabledOnAnyThread();
	static void SetEnabled(bool bEnabled);

	static EABTSStylizedRenderProfile GetProfile();
	static EABTSStylizedRenderProfile GetProfileOnAnyThread();
	static void SetProfile(EABTSStylizedRenderProfile Profile);
	static FABTSStylizedToneProfileParameters GetToneProfileParameters(
		EABTSStylizedRenderProfile Profile);
	/**
	 * Scene captures do not retain the main view's temporal lighting history.
	 * Clamp tone normalization to this profile-specific floor so sub-visible
	 * dark noise cannot be expanded into bright chromatic speckles.
	 */
	static float GetSceneCaptureToneNormalizationFloor(
		EABTSStylizedRenderProfile Profile);
	static FABTSStylizedOutlineProfileParameters GetOutlineProfileParameters(
		EABTSStylizedRenderProfile Profile);

	static int32 GetImplementationVersion();
	static bool IsProfileValid(EABTSStylizedRenderProfile Profile);
};
