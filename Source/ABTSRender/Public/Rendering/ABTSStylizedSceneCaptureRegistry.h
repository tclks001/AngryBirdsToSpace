// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/ABTSStylizedRenderingTypes.h"

class USceneCaptureComponent2D;

/**
 * Integration-owned bridge from an explicit SceneCapture component to one
 * frozen stylized view class.  Unknown captures remain completely untouched.
 */
class ABTSRENDER_API FABTSStylizedSceneCaptureRegistry
{
public:
	/** Registers or atomically replaces the component-local view extension. */
	static bool Register(
		USceneCaptureComponent2D& Capture,
		EABTSStylizedViewClass ViewClass);

	/** Removes only the extension installed by this registry. */
	static void Unregister(USceneCaptureComponent2D& Capture);

	/** Read-only diagnostic seam used by runtime ownership and automation. */
	static bool TryGetViewClass(
		const USceneCaptureComponent2D& Capture,
		EABTSStylizedViewClass& OutViewClass);

	/** Module shutdown and fresh-process test cleanup. */
	static void Reset();
};
