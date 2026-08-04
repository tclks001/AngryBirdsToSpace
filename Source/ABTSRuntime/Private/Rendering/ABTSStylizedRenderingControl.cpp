// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSStylizedRenderingControl.h"

#include "HAL/IConsoleManager.h"

namespace ABTSStylizedRenderingControl
{
	TAutoConsoleVariable<int32> CVarEnabled(
		TEXT("abts.Rendering.Stylized.Enabled"),
		0,
		TEXT("Integration-owned stylized rendering switch. T0 records identity only; T1 consumes it."),
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
	return 0;
}

bool FABTSStylizedRenderingControl::IsProfileValid(
	EABTSStylizedRenderProfile Profile)
{
	return Profile >= EABTSStylizedRenderProfile::GroundDay
		&& Profile <= EABTSStylizedRenderProfile::FinaleSpace;
}
