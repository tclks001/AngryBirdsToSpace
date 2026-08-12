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
	/** Geometry-to-background silhouette strength. */
	float Strength = 0.92f;
	/** Visible geometry-to-geometry depth discontinuity strength. */
	float OcclusionStrength = 0.64f;
	/** Same-depth normal crease strength; kept subordinate to silhouettes. */
	float NormalCreaseStrength = 0.22f;
	FVector3f Color = FVector3f(0.035f, 0.050f, 0.075f);

	bool IsValid() const;
};

/**
 * Immutable render-thread value snapshot for T4 spherical atmosphere and
 * deterministic HDR stars. It contains no UObject references.
 */
struct ABTSRENDER_API FABTSStylizedEnvironmentParameters
{
	FVector PlanetCenterWorld = FVector::ZeroVector;
	float PlanetRadiusCM = 0.0f;
	float AtmosphereHeightCM = 0.0f;
	FVector3f SunDirectionToSunWorld = FVector3f::ZeroVector;
	EABTSStylizedRenderProfile Profile =
		EABTSStylizedRenderProfile::GroundDay;
	uint32 StarSeed = 0;
	float StarGridResolution = 256.0f;
	float StarCellProbability = 0.012f;
	float StarAngularRadiusScale = 0.055f;
	float StarHDRIntensity = 1.8f;
	float FixedExposureBias = 0.0f;
	/** T4-A2 native radial cloud-shell route. Zero keeps non-ground profiles cloud-free. */
	uint32 bCloudsEnabled = 0;
	float CloudBaseAltitudeCM = 0.0f;
	float CloudLayerHeightCM = 0.0f;
	float CloudGlobalScaleKM = 0.0f;
	float CloudCoverage = 0.0f;
	float CloudDensity = 0.0f;
	float CloudViewSampleCountScale = 0.0f;
	bool IsValid() const;
};

/**
 * Integration-owned diagnostic mask.  Production uses ToneAndOutline; the
 * other values exist so the T4 capture runner can isolate rendering layers
 * without changing material families or gameplay state.
 */
enum class EABTSStylizedDiagnosticPassMask : uint8
{
	None = 0,
	Tone = 1 << 0,
	Outline = 1 << 1,
	ToneAndOutline = 3
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
	static EABTSStylizedDiagnosticPassMask GetDiagnosticPassMask();
	static EABTSStylizedDiagnosticPassMask GetDiagnosticPassMaskOnAnyThread();
	static void SetDiagnosticPassMask(EABTSStylizedDiagnosticPassMask Mask);
	static bool IsTonePassEnabledOnAnyThread();
	static bool IsOutlinePassEnabledOnAnyThread();
	static FABTSStylizedEnvironmentParameters BuildEnvironmentParameters(
		const FVector& PlanetCenterWorld,
		double PlanetRadiusCM,
		const FVector& SunDirectionToSunWorld,
		EABTSStylizedRenderProfile Profile);
	static void SetEnvironmentParameters(
		const FABTSStylizedEnvironmentParameters& Parameters);
	static void ClearEnvironmentParameters();
	static bool TryGetEnvironmentParametersOnAnyThread(
		FABTSStylizedEnvironmentParameters& OutParameters);
	static FABTSStylizedToneProfileParameters GetToneProfileParameters(
		EABTSStylizedRenderProfile Profile);
	static float GetFixedExposureBias(EABTSStylizedRenderProfile Profile);
	/**
	 * Scene captures do not retain the main view's temporal lighting history.
	 * Clamp tone normalization to this profile-specific floor so sub-visible
	 * dark noise cannot be expanded into bright chromatic speckles.
	 */
	static float GetSceneCaptureToneNormalizationFloor(
		EABTSStylizedRenderProfile Profile);
	static FABTSStylizedOutlineProfileParameters GetOutlineProfileParameters(
		EABTSStylizedRenderProfile Profile);
	/**
	 * Low-poly masked cloud silhouettes must remain temporally crisp in the
	 * ground traversal profile.  Full-screen motion blur blends the bright sky
	 * across the darker night-side cloud edge while the camera moves, so the
	 * ground cloud route explicitly suppresses it.  Satellite/finale profiles
	 * retain their own camera presentation policy.
	 */
	static bool ShouldSuppressMotionBlur(
		EABTSStylizedRenderProfile Profile,
		bool bCloudsEnabled);

	static int32 GetImplementationVersion();
	static bool IsProfileValid(EABTSStylizedRenderProfile Profile);
};
