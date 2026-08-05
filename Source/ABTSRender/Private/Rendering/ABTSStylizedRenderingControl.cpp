// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSStylizedRenderingControl.h"

#include "HAL/IConsoleManager.h"

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

int32 FABTSStylizedRenderingControl::GetImplementationVersion()
{
	return 4;
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
		Parameters.Color = FVector3f(0.030f, 0.045f, 0.075f);
		break;
	case EABTSStylizedRenderProfile::FinaleSpace:
		Parameters.WidthPixels = 1.40f;
		Parameters.DepthThreshold = 0.009f;
		Parameters.DepthSoftness = 0.015f;
		Parameters.NormalThreshold = 0.13f;
		Parameters.NormalSoftness = 0.16f;
		Parameters.Strength = 0.86f;
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
		&& Color.GetMin() >= 0.0f
		&& Color.GetMax() <= 1.0f;
}
