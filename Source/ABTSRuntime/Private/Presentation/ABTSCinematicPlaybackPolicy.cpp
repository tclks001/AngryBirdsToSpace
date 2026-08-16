// Copyright Epic Games, Inc. All Rights Reserved.

#include "Presentation/ABTSCinematicPlaybackPolicy.h"

#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarABTSDebugSkipCinematics(
		TEXT("abts.Debug.SkipCinematics"),
		0,
		TEXT("Development-only fast iteration switch. 1 skips opening and finale cinematics. "
			"RC9 defaults to 0; Shipping always ignores the request and plays cinematics."),
		ECVF_Default);
}

bool FABTSCinematicPlaybackPolicy::ShouldSkipCinematics()
{
	return ResolveSkipRequest(
		CVarABTSDebugSkipCinematics.GetValueOnAnyThread() != 0,
		UE_BUILD_SHIPPING != 0);
}

bool FABTSCinematicPlaybackPolicy::IsShippingPlaybackHardLocked()
{
	return UE_BUILD_SHIPPING != 0;
}

bool FABTSCinematicPlaybackPolicy::ResolveSkipRequest(
	const bool bDebugSkipRequested,
	const bool bShippingBuild)
{
	return bShippingBuild ? false : bDebugSkipRequested;
}
