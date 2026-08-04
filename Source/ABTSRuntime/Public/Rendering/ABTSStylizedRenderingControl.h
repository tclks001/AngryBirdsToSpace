// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/ABTSToonVisualCaptureTypes.h"

/**
 * Stable Integration-owned switch seam for T0 capture and future stylized
 * rendering. T0 deliberately has implementation version zero: changing this
 * state records an identity but does not alter pixels until T1 consumes it.
 */
class ABTSRUNTIME_API FABTSStylizedRenderingControl
{
public:
	static bool IsEnabled();
	static void SetEnabled(bool bEnabled);

	static EABTSStylizedRenderProfile GetProfile();
	static void SetProfile(EABTSStylizedRenderProfile Profile);

	static int32 GetImplementationVersion();
	static bool IsProfileValid(EABTSStylizedRenderProfile Profile);
};
