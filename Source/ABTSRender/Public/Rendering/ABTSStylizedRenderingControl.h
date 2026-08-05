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

	static int32 GetImplementationVersion();
	static bool IsProfileValid(EABTSStylizedRenderProfile Profile);
};
