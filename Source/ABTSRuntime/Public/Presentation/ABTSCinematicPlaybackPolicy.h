// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** One release-facing debug policy shared by the opening and post-hit finale. */
struct ABTSRUNTIME_API FABTSCinematicPlaybackPolicy
{
	static bool ShouldSkipCinematics();
	static bool IsShippingPlaybackHardLocked();

	/** Pure contract seam used by automation without mutating the process CVar. */
	static bool ResolveSkipRequest(bool bDebugSkipRequested, bool bShippingBuild);
};
