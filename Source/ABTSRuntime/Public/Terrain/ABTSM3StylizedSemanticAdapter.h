// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/ABTSStylizedRenderingTypes.h"

class AABTSM3MonthlySatellitePracticeRuntime;
class AABTSM3Planet;
class AABTSM9Satellite;
class AActor;
class UPrimitiveComponent;
struct FABTSM3MonthlySatellitePreviewCandidate;
struct FABTSM3MonthlySatellitePreviewResult;

/** Exact M3/M9 authority that produced one read-only stylized semantic. */
enum class EABTSM3StylizedSemanticSource : uint8
{
	None = 0,
	PrimaryContinuousSurface,
	ForestHISMBatch,
	RockHISMBatch,
	PracticeSatelliteActor,
	PracticeBacksideE5Actor
};

/** Integration registers actor semantics or one component batch; M3 never registers HISM instances. */
enum class EABTSM3StylizedSemanticGranularity : uint8
{
	None = 0,
	Actor,
	Component,
	ComponentBatch
};

/** Satellite-preview elements are explicit result roles, not inferred from transforms or names. */
enum class EABTSM3StylizedSatellitePreviewElement : uint8
{
	SatelliteSurface = 0,
	BacksideE5Target
};

/**
 * Ephemeral read-only binding for Integration. It carries semantic identity only:
 * no profile, custom-depth state, stencil value, generation authority or hash.
 */
struct ABTSRUNTIME_API FABTSM3StylizedSemanticBinding
{
	const UObject* SemanticAuthority = nullptr;
	const AActor* Actor = nullptr;
	const UPrimitiveComponent* Component = nullptr;
	EABTSStylizedObjectClass ObjectClass = EABTSStylizedObjectClass::None;
	EABTSM3StylizedSemanticSource Source =
		EABTSM3StylizedSemanticSource::None;
	EABTSM3StylizedSemanticGranularity Granularity =
		EABTSM3StylizedSemanticGranularity::None;
	int32 RepresentedInstanceCount = 0;

	bool IsValid() const;
};

/**
 * M3-owned, read-only semantic adapter for T2-B.
 *
 * Resolution uses exact authoritative Actor/component/result relationships.
 * Unknown objects fail closed and no method mutates rendering or gameplay state.
 */
class ABTSRUNTIME_API FABTSM3StylizedSemanticAdapter
{
public:
	static bool TryResolveActor(
		const UObject& SemanticAuthority,
		const AActor& Actor,
		FABTSM3StylizedSemanticBinding& OutBinding);

	static bool TryResolveComponent(
		const UObject& SemanticAuthority,
		const UPrimitiveComponent& Component,
		FABTSM3StylizedSemanticBinding& OutBinding);

	static void GatherPrimaryPlanetSemantics(
		const AABTSM3Planet& Planet,
		TArray<FABTSM3StylizedSemanticBinding>& OutBindings);

	static void GatherSatelliteSemantics(
		const AABTSM9Satellite& Satellite,
		TArray<FABTSM3StylizedSemanticBinding>& OutBindings);

	static void GatherMonthlyPracticeSemantics(
		const AABTSM3MonthlySatellitePracticeRuntime& Runtime,
		TArray<FABTSM3StylizedSemanticBinding>& OutBindings);

	static bool TryResolveMonthlySatellitePreviewElement(
		const FABTSM3MonthlySatellitePreviewResult& Result,
		const FABTSM3MonthlySatellitePreviewCandidate& Candidate,
		EABTSM3StylizedSatellitePreviewElement Element,
		EABTSStylizedObjectClass& OutObjectClass);
};
