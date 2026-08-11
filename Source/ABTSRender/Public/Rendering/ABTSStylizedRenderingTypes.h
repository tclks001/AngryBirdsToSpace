// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/ABTSStylizedRenderProfile.h"

/** Semantic object identity supplied by feature modules; it is not a raw stencil value. */
enum class EABTSStylizedObjectClass : uint8
{
	None = 0,
	WorldSurface,
	BackgroundProp,
	PlayerBird,
	Slingshot,
	BuildingBody,
	BuildingWeakPoint,
	SatelliteTarget,
	FinalePlanet,
	FinaleUFO
};

/** Semantic view identity. Scene captures must declare one of the preview classes explicitly. */
enum class EABTSStylizedViewClass : uint8
{
	MainWorld = 0,
	GroundLandingPreview,
	SatelliteLandingPreview,
	FinaleRemotePreview,
	/** Main-world-equivalent offscreen view used only by the M11 AVI recorder. */
	FinaleCinematicCapture
};

/** Read-only rendering policy resolved by Integration from a semantic view class. */
struct ABTSRENDER_API FABTSStylizedViewPolicy
{
	EABTSStylizedRenderProfile Profile = EABTSStylizedRenderProfile::GroundDay;
	bool bApplyTone = true;
	bool bApplyOutline = true;
	bool bAllowSelectiveStencil = false;

	bool IsValid() const;
};

/**
 * Integration-owned rendering contract.
 *
 * Feature modules publish semantic classes only. They must not copy, persist, or assign
 * the raw stencil values returned by ResolveStencilValueForRenderer().
 */
class ABTSRENDER_API FABTSStylizedRenderingContract
{
public:
	static bool IsObjectClassValid(EABTSStylizedObjectClass ObjectClass);
	static bool IsViewClassValid(EABTSStylizedViewClass ViewClass);
	static bool RequiresSelectiveStencil(EABTSStylizedObjectClass ObjectClass);
	static uint8 ResolveStencilValueForRenderer(EABTSStylizedObjectClass ObjectClass);

	/**
	 * Integration-only composite stencil outside the 1..7 selective gameplay
	 * allocation. All logical clouds share this outline class, while generation,
	 * LOD and diagnostics keep their logical identity entirely CPU-side.
	 */
	static uint8 ResolveCloudCompositeStencilValueForRenderer();
	static bool IsCloudCompositeStencilValueForRenderer(uint8 StencilValue);
	static bool ShouldSuppressInternalOutlineBetweenStencilValues(
		uint8 CenterStencilValue,
		uint8 SampleStencilValue);

	static FABTSStylizedViewPolicy ResolveViewPolicy(
		EABTSStylizedViewClass ViewClass,
		EABTSStylizedRenderProfile MainWorldProfile =
			EABTSStylizedRenderProfile::GroundDay);

	/** T2-A deliberately renders only the final main view; previews are wired in T2-B. */
	static bool IsViewClassImplemented(EABTSStylizedViewClass ViewClass);
};
