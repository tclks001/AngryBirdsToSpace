// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSStylizedRenderingControl.h"

#include "HAL/IConsoleManager.h"
#include "Misc/ScopeRWLock.h"

namespace ABTSStylizedRenderingControl
{
	TAutoConsoleVariable<int32> CVarEnabled(
		TEXT("abts.Rendering.Stylized.Enabled"),
		0,
		TEXT("Integration-owned stylized rendering switch. T2-B1 enables explicit main and preview view policies."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarProfile(
		TEXT("abts.Rendering.Stylized.Profile"),
		static_cast<int32>(EABTSStylizedRenderProfile::GroundDay),
		TEXT("0=GroundDay, 1=SatelliteGuide, 2=FinaleSpace."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarDiagnosticPassMask(
		TEXT("abts.Rendering.Stylized.DiagnosticPassMask"),
		static_cast<int32>(EABTSStylizedDiagnosticPassMask::ToneAndOutline),
		TEXT("Integration diagnostic seam. 0=None, 1=Tone, 2=Outline, 3=ToneAndOutline. Production default is 3."),
		ECVF_Default);

	FRWLock EnvironmentLock;
	FABTSStylizedEnvironmentParameters EnvironmentParameters;
	bool bEnvironmentParametersReady = false;

	EABTSStylizedDiagnosticPassMask SanitizeDiagnosticPassMask(const int32 Value)
	{
		return static_cast<EABTSStylizedDiagnosticPassMask>(
			FMath::Clamp(
				Value,
				static_cast<int32>(EABTSStylizedDiagnosticPassMask::None),
				static_cast<int32>(EABTSStylizedDiagnosticPassMask::ToneAndOutline)));
	}
}

bool FABTSStylizedRenderingControl::IsEnabled()
{
	return ABTSStylizedRenderingControl::CVarEnabled.GetValueOnGameThread() != 0;
}

bool FABTSStylizedRenderingControl::IsEnabledOnAnyThread()
{
	return ABTSStylizedRenderingControl::CVarEnabled.GetValueOnAnyThread() != 0;
}

void FABTSStylizedRenderingControl::SetEnabled(bool bEnabled)
{
	ABTSStylizedRenderingControl::CVarEnabled->Set(
		bEnabled ? 1 : 0,
		ECVF_SetByCode);
}

EABTSStylizedRenderProfile FABTSStylizedRenderingControl::GetProfile()
{
	const int32 Value = FMath::Clamp(
		ABTSStylizedRenderingControl::CVarProfile.GetValueOnGameThread(),
		static_cast<int32>(EABTSStylizedRenderProfile::GroundDay),
		static_cast<int32>(EABTSStylizedRenderProfile::FinaleSpace));
	return static_cast<EABTSStylizedRenderProfile>(Value);
}

EABTSStylizedRenderProfile FABTSStylizedRenderingControl::GetProfileOnAnyThread()
{
	const int32 Value = FMath::Clamp(
		ABTSStylizedRenderingControl::CVarProfile.GetValueOnAnyThread(),
		static_cast<int32>(EABTSStylizedRenderProfile::GroundDay),
		static_cast<int32>(EABTSStylizedRenderProfile::FinaleSpace));
	return static_cast<EABTSStylizedRenderProfile>(Value);
}

void FABTSStylizedRenderingControl::SetProfile(
	EABTSStylizedRenderProfile Profile)
{
	if (!IsProfileValid(Profile))
	{
		return;
	}
	ABTSStylizedRenderingControl::CVarProfile->Set(
		static_cast<int32>(Profile),
		ECVF_SetByCode);
}

EABTSStylizedDiagnosticPassMask
FABTSStylizedRenderingControl::GetDiagnosticPassMask()
{
	return ABTSStylizedRenderingControl::SanitizeDiagnosticPassMask(
		ABTSStylizedRenderingControl::CVarDiagnosticPassMask.GetValueOnGameThread());
}

EABTSStylizedDiagnosticPassMask
FABTSStylizedRenderingControl::GetDiagnosticPassMaskOnAnyThread()
{
	return ABTSStylizedRenderingControl::SanitizeDiagnosticPassMask(
		ABTSStylizedRenderingControl::CVarDiagnosticPassMask.GetValueOnAnyThread());
}

void FABTSStylizedRenderingControl::SetDiagnosticPassMask(
	const EABTSStylizedDiagnosticPassMask Mask)
{
	const int32 Value = static_cast<int32>(Mask);
	if (Value < static_cast<int32>(EABTSStylizedDiagnosticPassMask::None)
		|| Value > static_cast<int32>(
			EABTSStylizedDiagnosticPassMask::ToneAndOutline))
	{
		return;
	}
	ABTSStylizedRenderingControl::CVarDiagnosticPassMask->Set(
		Value,
		ECVF_SetByCode);
}

bool FABTSStylizedRenderingControl::IsTonePassEnabledOnAnyThread()
{
	return (static_cast<uint8>(GetDiagnosticPassMaskOnAnyThread())
		& static_cast<uint8>(EABTSStylizedDiagnosticPassMask::Tone)) != 0;
}

bool FABTSStylizedRenderingControl::IsOutlinePassEnabledOnAnyThread()
{
	return (static_cast<uint8>(GetDiagnosticPassMaskOnAnyThread())
		& static_cast<uint8>(EABTSStylizedDiagnosticPassMask::Outline)) != 0;
}

FABTSStylizedEnvironmentParameters
FABTSStylizedRenderingControl::BuildEnvironmentParameters(
	const FVector& PlanetCenterWorld,
	const double PlanetRadiusCM,
	const FVector& SunDirectionToSunWorld,
	const EABTSStylizedRenderProfile Profile)
{
	FABTSStylizedEnvironmentParameters Parameters;
	Parameters.PlanetCenterWorld = PlanetCenterWorld;
	Parameters.PlanetRadiusCM = static_cast<float>(PlanetRadiusCM);
	Parameters.AtmosphereHeightCM = static_cast<float>(FMath::Clamp(
		PlanetRadiusCM * 0.60,
		10000.0,
		8000000.0));
	// The practice satellite sits roughly 0.55 primary radii above the surface.
	// Finish the atmosphere-to-space transition just before that height while
	// retaining a broad spatial band that cannot read as a profile cut.
	Parameters.HighAltitudeTransitionStartCM = static_cast<float>(
		PlanetRadiusCM * 0.22);
	Parameters.HighAltitudeTransitionEndCM = static_cast<float>(
		PlanetRadiusCM * 0.52);
	Parameters.SunDirectionToSunWorld = FVector3f(
		SunDirectionToSunWorld.GetSafeNormal());
	Parameters.Profile = Profile;
	// Star positions are an art-direction identity, not a generated-world
	// identity. Keeping this seed fixed makes the sky stable across M3 seeds.
	Parameters.StarSeed = 0x00A8B751u;
	switch (Profile)
	{
	case EABTSStylizedRenderProfile::SatelliteGuide:
		Parameters.StarCellProbability = 0.014f;
		Parameters.StarHDRIntensity = 3.0f;
		break;
	case EABTSStylizedRenderProfile::FinaleSpace:
		Parameters.StarCellProbability = 0.016f;
		Parameters.StarHDRIntensity = 3.6f;
		break;
	case EABTSStylizedRenderProfile::GroundDay:
	default:
		// The project sky/ground lighting was authored against auto exposure.
		// Manual 0 EV underexposes the authored ground lighting. Keep a modest,
		// reproducible lift; sky scattering remains an independent environment
		// layer and must not be compensated by washing out object albedo.
		// UE's Earth-scale VolumetricCloud material becomes either empty or a
		// uniform grey veil at this gameplay planet scale. T4-A2R0 therefore uses
		// three deterministic, bounded low-poly cloud islands. These values define
		// their radial altitude envelope and remain part of capture identity.
		Parameters.bCloudsEnabled = 1u;
		Parameters.CloudBaseAltitudeCM = FMath::Clamp(
			Parameters.PlanetRadiusCM * 0.12f,
			900.0f,
			2400.0f);
		Parameters.CloudLayerHeightCM = FMath::Clamp(
			Parameters.PlanetRadiusCM * 0.085f,
			650.0f,
			1700.0f);
		Parameters.CloudGlobalScaleKM = 0.10f;
		Parameters.CloudCoverage = 0.48f;
		Parameters.CloudDensity = 0.84f;
		// Retained for schema compatibility; the R0 mesh route does not ray march.
		Parameters.CloudViewSampleCountScale = 1.0f;
		break;
	}
	Parameters.FixedExposureBias = GetFixedExposureBias(Profile);
	return Parameters;
}

void FABTSStylizedRenderingControl::SetEnvironmentParameters(
	const FABTSStylizedEnvironmentParameters& Parameters)
{
	if (!Parameters.IsValid())
	{
		return;
	}
	FWriteScopeLock ScopeLock(
		ABTSStylizedRenderingControl::EnvironmentLock);
	ABTSStylizedRenderingControl::EnvironmentParameters = Parameters;
	ABTSStylizedRenderingControl::bEnvironmentParametersReady = true;
}

void FABTSStylizedRenderingControl::ClearEnvironmentParameters()
{
	FWriteScopeLock ScopeLock(
		ABTSStylizedRenderingControl::EnvironmentLock);
	ABTSStylizedRenderingControl::EnvironmentParameters =
		FABTSStylizedEnvironmentParameters();
	ABTSStylizedRenderingControl::bEnvironmentParametersReady = false;
}

bool FABTSStylizedRenderingControl::TryGetEnvironmentParametersOnAnyThread(
	FABTSStylizedEnvironmentParameters& OutParameters)
{
	FReadScopeLock ScopeLock(
		ABTSStylizedRenderingControl::EnvironmentLock);
	OutParameters = ABTSStylizedRenderingControl::EnvironmentParameters;
	return ABTSStylizedRenderingControl::bEnvironmentParametersReady
		&& OutParameters.IsValid();
}

int32 FABTSStylizedRenderingControl::GetImplementationVersion()
{
	return 71;
}

FABTSStylizedEnvironmentProfilePolicy
FABTSStylizedRenderingControl::GetEnvironmentProfilePolicy(
	const EABTSStylizedRenderProfile Profile)
{
	FABTSStylizedEnvironmentProfilePolicy Policy;
	Policy.Profile = IsProfileValid(Profile)
		? Profile
		: EABTSStylizedRenderProfile::GroundDay;
	switch (Policy.Profile)
	{
	case EABTSStylizedRenderProfile::SatelliteGuide:
	case EABTSStylizedRenderProfile::FinaleSpace:
		Policy.bSkyAtmosphereVisible = false;
		Policy.bHeightFogVisible = false;
		Policy.bLowPolyCloudsVisible = false;
		break;
	case EABTSStylizedRenderProfile::GroundDay:
	default:
		Policy.bSkyAtmosphereVisible = true;
		// The authored fog is global-Z and remains incompatible with a
		// walkable sphere. GroundDay uses spherical atmosphere plus the
		// bounded low-poly cloud field instead.
		Policy.bHeightFogVisible = false;
		Policy.bLowPolyCloudsVisible = true;
		break;
	}
	return Policy;
}

float FABTSStylizedRenderingControl::ComputeHighAltitudeSpaceBlend(
	const float CameraAltitudeCM,
	const float TransitionStartCM,
	const float TransitionEndCM)
{
	if (!FMath::IsFinite(CameraAltitudeCM)
		|| !FMath::IsFinite(TransitionStartCM)
		|| !FMath::IsFinite(TransitionEndCM)
		|| TransitionEndCM <= TransitionStartCM)
	{
		return 0.0f;
	}
	return FMath::SmoothStep(
		TransitionStartCM,
		TransitionEndCM,
		FMath::Max(CameraAltitudeCM, 0.0f));
}

float FABTSStylizedRenderingControl::ComputeGroundStarNightFactor(
	const float ObserverSunHeight,
	const float ViewToSun)
{
	if (!FMath::IsFinite(ObserverSunHeight)
		|| !FMath::IsFinite(ViewToSun))
	{
		return 0.0f;
	}
	const float ClampedViewToSun = FMath::Clamp(ViewToSun, -1.0f, 1.0f);
	const float ObserverNight = 1.0f - FMath::SmoothStep(
		-0.18f, 0.06f, ObserverSunHeight);
	const float RayEffectiveSunHeight =
		ObserverSunHeight + ClampedViewToSun * 0.42f;
	const float RayNight = 1.0f - FMath::SmoothStep(
		-0.24f, 0.12f, RayEffectiveSunHeight);
	const float TerminatorInfluence = 1.0f - FMath::SmoothStep(
		0.20f, 0.65f, FMath::Abs(ObserverSunHeight));
	return FMath::Clamp(FMath::Lerp(
		ObserverNight,
		RayNight,
		0.82f * TerminatorInfluence), 0.0f, 1.0f);
}

float FABTSStylizedRenderingControl::ComputeGroundStarHorizonVisibility(
	const float ViewRadialDot)
{
	return FMath::IsFinite(ViewRadialDot)
		? FMath::SmoothStep(-0.10f, 0.08f, ViewRadialDot)
		: 0.0f;
}

float FABTSStylizedRenderingControl::ComputeGroundSkyRayPlanetClearance(
	const float CameraRadiusCM,
	const float PlanetRadiusCM,
	const float ViewToPlanetCenter)
{
	if (!FMath::IsFinite(CameraRadiusCM)
		|| !FMath::IsFinite(PlanetRadiusCM)
		|| !FMath::IsFinite(ViewToPlanetCenter)
		|| CameraRadiusCM <= 0.0f
		|| PlanetRadiusCM <= 0.0f)
	{
		return -1.0f;
	}
	const float SafeRadiusRatio = FMath::Clamp(
		PlanetRadiusCM / FMath::Max(CameraRadiusCM, PlanetRadiusCM),
		0.0f,
		1.0f);
	const float TangentCos = FMath::Sqrt(FMath::Max(
		1.0f - SafeRadiusRatio * SafeRadiusRatio,
		0.0f));
	return TangentCos - FMath::Clamp(
		ViewToPlanetCenter,
		-1.0f,
		1.0f);
}

float FABTSStylizedRenderingControl::GetFixedExposureBias(
	const EABTSStylizedRenderProfile Profile)
{
	switch (Profile)
	{
	case EABTSStylizedRenderProfile::SatelliteGuide:
		return -0.10f;
	case EABTSStylizedRenderProfile::FinaleSpace:
		return -0.20f;
	case EABTSStylizedRenderProfile::GroundDay:
	default:
		return 0.75f;
	}
}

bool FABTSStylizedRenderingControl::ShouldSuppressMotionBlur(
	EABTSStylizedRenderProfile Profile,
	bool bCloudsEnabled)
{
	return bCloudsEnabled
		&& Profile == EABTSStylizedRenderProfile::GroundDay;
}

FABTSStylizedToneProfileParameters
FABTSStylizedRenderingControl::GetToneProfileParameters(
	EABTSStylizedRenderProfile Profile)
{
	FABTSStylizedToneProfileParameters Parameters;
	switch (Profile)
	{
	case EABTSStylizedRenderProfile::SatelliteGuide:
		Parameters.ShadowThreshold = 0.18f;
		Parameters.HighlightThreshold = 0.56f;
		Parameters.TransitionSoftness = 0.08f;
		Parameters.Strength = 0.68f;
		Parameters.ShadowLuminance = 0.11f;
		Parameters.MidLuminance = 0.40f;
		Parameters.HighlightLuminance = 0.78f;
		Parameters.Saturation = 1.02f;
		Parameters.ShadowTint = FVector3f(0.76f, 0.88f, 1.12f);
		Parameters.MidTint = FVector3f(0.96f, 0.98f, 1.02f);
		Parameters.HighlightTint = FVector3f(1.02f, 1.01f, 0.98f);
		break;
	case EABTSStylizedRenderProfile::FinaleSpace:
		Parameters.ShadowThreshold = 0.15f;
		Parameters.HighlightThreshold = 0.48f;
		Parameters.TransitionSoftness = 0.075f;
		Parameters.Strength = 0.74f;
		Parameters.ShadowLuminance = 0.085f;
		Parameters.MidLuminance = 0.34f;
		Parameters.HighlightLuminance = 0.74f;
		Parameters.Saturation = 1.08f;
		Parameters.ShadowTint = FVector3f(0.72f, 0.82f, 1.14f);
		Parameters.MidTint = FVector3f(0.94f, 0.95f, 1.04f);
		Parameters.HighlightTint = FVector3f(1.06f, 0.98f, 1.0f);
		break;
	case EABTSStylizedRenderProfile::GroundDay:
	default:
		break;
	}
	return Parameters;
}

float FABTSStylizedRenderingControl::GetSceneCaptureToneNormalizationFloor(
	const EABTSStylizedRenderProfile Profile)
{
	const FABTSStylizedToneProfileParameters Parameters =
		GetToneProfileParameters(Profile);
	// ShadowLuminance is the first trustworthy output band for both main and
	// low-history capture views: values below it must not receive gain > 1.
	return FMath::Max(Parameters.ShadowLuminance, 1.0e-4f);
}

FABTSStylizedOutlineProfileParameters
FABTSStylizedRenderingControl::GetOutlineProfileParameters(
	EABTSStylizedRenderProfile Profile)
{
	FABTSStylizedOutlineProfileParameters Parameters;
	switch (Profile)
	{
	case EABTSStylizedRenderProfile::SatelliteGuide:
		Parameters.WidthPixels = 1.20f;
		Parameters.DepthThreshold = 0.010f;
		Parameters.DepthSoftness = 0.016f;
		Parameters.NormalThreshold = 0.14f;
		Parameters.NormalSoftness = 0.17f;
		Parameters.Strength = 0.82f;
		Parameters.OcclusionStrength = 0.68f;
		Parameters.NormalCreaseStrength = 0.28f;
		Parameters.Color = FVector3f(0.030f, 0.045f, 0.075f);
		break;
	case EABTSStylizedRenderProfile::FinaleSpace:
		Parameters.WidthPixels = 1.40f;
		Parameters.DepthThreshold = 0.009f;
		Parameters.DepthSoftness = 0.015f;
		Parameters.NormalThreshold = 0.13f;
		Parameters.NormalSoftness = 0.16f;
		Parameters.Strength = 0.86f;
		Parameters.OcclusionStrength = 0.72f;
		Parameters.NormalCreaseStrength = 0.30f;
		Parameters.Color = FVector3f(0.022f, 0.030f, 0.055f);
		break;
	case EABTSStylizedRenderProfile::GroundDay:
	default:
		break;
	}
	return Parameters;
}

bool FABTSStylizedRenderingControl::IsProfileValid(
	EABTSStylizedRenderProfile Profile)
{
	return Profile >= EABTSStylizedRenderProfile::GroundDay
		&& Profile <= EABTSStylizedRenderProfile::FinaleSpace;
}

bool FABTSStylizedToneProfileParameters::IsValid() const
{
	return ShadowThreshold > 0.0f
		&& HighlightThreshold > ShadowThreshold
		&& HighlightThreshold < 1.0f
		&& TransitionSoftness > 0.0f
		&& TransitionSoftness < 0.25f
		&& Strength > 0.0f
		&& Strength <= 1.0f
		&& ShadowLuminance >= 0.0f
		&& MidLuminance > ShadowLuminance
		&& HighlightLuminance > MidLuminance
		&& HighlightLuminance <= 1.0f
		&& Saturation > 0.0f
		&& ShadowTint.GetMin() > 0.0f
		&& MidTint.GetMin() > 0.0f
		&& HighlightTint.GetMin() > 0.0f;
}

bool FABTSStylizedOutlineProfileParameters::IsValid() const
{
	return WidthPixels > 0.0f
		&& WidthPixels <= 4.0f
		&& DepthThreshold > 0.0f
		&& DepthSoftness > 0.0f
		&& NormalThreshold > 0.0f
		&& NormalThreshold < 1.0f
		&& NormalSoftness > 0.0f
		&& Strength > 0.0f
		&& Strength <= 1.0f
		&& OcclusionStrength > 0.0f
		&& OcclusionStrength <= Strength
		&& NormalCreaseStrength > 0.0f
		&& NormalCreaseStrength <= OcclusionStrength
		&& Color.GetMin() >= 0.0f
		&& Color.GetMax() <= 1.0f;
}

bool FABTSStylizedEnvironmentParameters::IsValid() const
{
	return !PlanetCenterWorld.ContainsNaN()
		&& FMath::IsFinite(PlanetRadiusCM)
		&& PlanetRadiusCM > 0.0f
		&& FMath::IsFinite(AtmosphereHeightCM)
		&& AtmosphereHeightCM > 0.0f
		&& FMath::IsFinite(HighAltitudeTransitionStartCM)
		&& HighAltitudeTransitionStartCM > 0.0f
		&& FMath::IsFinite(HighAltitudeTransitionEndCM)
		&& HighAltitudeTransitionEndCM > HighAltitudeTransitionStartCM
		&& HighAltitudeTransitionEndCM <= AtmosphereHeightCM
		&& !SunDirectionToSunWorld.ContainsNaN()
		&& FMath::Abs(SunDirectionToSunWorld.SizeSquared() - 1.0f) <= 1.0e-3f
		&& FABTSStylizedRenderingControl::IsProfileValid(Profile)
		&& StarSeed != 0
		&& FMath::IsFinite(StarGridResolution)
		&& StarGridResolution >= 32.0f
		&& StarGridResolution <= 1024.0f
		&& FMath::IsFinite(StarCellProbability)
		&& StarCellProbability > 0.0f
		&& StarCellProbability < 1.0f
		&& FMath::IsFinite(StarAngularRadiusScale)
		&& StarAngularRadiusScale > 0.0f
		&& StarAngularRadiusScale <= 0.5f
		&& FMath::IsFinite(StarHDRIntensity)
		&& StarHDRIntensity > 0.0f
		&& FMath::IsFinite(FixedExposureBias)
		&& (bCloudsEnabled == 0u || bCloudsEnabled == 1u)
		&& (bCloudsEnabled == 0u
			|| (Profile == EABTSStylizedRenderProfile::GroundDay
				&& FMath::IsFinite(CloudBaseAltitudeCM)
				&& CloudBaseAltitudeCM > 0.0f
				&& FMath::IsFinite(CloudLayerHeightCM)
				&& CloudLayerHeightCM > 0.0f
				&& FMath::IsFinite(CloudGlobalScaleKM)
				&& CloudGlobalScaleKM > 0.0f
				&& FMath::IsFinite(CloudCoverage)
				&& FMath::IsFinite(CloudDensity)
				&& CloudDensity > 0.0f
				&& FMath::IsFinite(CloudViewSampleCountScale)
				&& CloudViewSampleCountScale >= 0.05f));
}

bool FABTSStylizedEnvironmentProfilePolicy::IsValid() const
{
	if (!FABTSStylizedRenderingControl::IsProfileValid(Profile)
		|| bHeightFogVisible)
	{
		return false;
	}
	return Profile == EABTSStylizedRenderProfile::GroundDay
		? bSkyAtmosphereVisible && bLowPolyCloudsVisible
		: !bSkyAtmosphereVisible && !bLowPolyCloudsVisible;
}
