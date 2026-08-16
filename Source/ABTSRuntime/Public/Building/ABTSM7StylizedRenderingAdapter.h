// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM7BuildingTypes.h"
#include "Rendering/ABTSStylizedMaterialOverrideRegistry.h"
#include "Rendering/ABTSStylizedRenderingTypes.h"

class AABTSM7BuildingMaterialSystem;
class AABTSM7BuildingModule;
class AActor;
class UPrimitiveComponent;
class UMaterialInterface;

/** M7-owned T3-B candidates. Null deliberately means retain the accepted baseline. */
struct ABTSRUNTIME_API FABTSM7StylizedMaterialSet final
{
	UMaterialInterface* Wood = nullptr;
	UMaterialInterface* Stone = nullptr;
	UMaterialInterface* Steel = nullptr;
	UMaterialInterface* Glass = nullptr;

	UMaterialInterface* Get(EABTSM7BuildingMaterial Material) const;
};

/** Ephemeral semantic declaration for Integration's single world renderer. */
struct ABTSRUNTIME_API FABTSM7StylizedSemanticBinding final
{
	const UObject* SemanticAuthority = nullptr;
	const AActor* Actor = nullptr;
	const UPrimitiveComponent* Component = nullptr;
	EABTSStylizedObjectClass ObjectClass = EABTSStylizedObjectClass::None;
	EABTSM7BuildingMaterial Material = EABTSM7BuildingMaterial::Wood;
	bool bDynamic = false;

	bool IsValid() const;
	/** Resolved on demand; M7 never persists or assigns raw stencil values. */
	uint8 ResolveStencilValueForRenderer() const;
};

/**
 * Read-only M7 T3-B adapter. It owns neither the world registry nor any MID,
 * CustomDepth, material slot, collision, or destruction mutation.
 */
class ABTSRUNTIME_API FABTSM7StylizedRenderingAdapter final
{
public:
	static EABTSStylizedMaterialFamily ResolveMaterialFamily(
		EABTSM7BuildingMaterial Material);
	static EABTSStylizedObjectClass ResolveObjectClass(
		const AABTSM7BuildingModule& Module);

	static void GatherMaterialBindings(
		const AABTSM7BuildingMaterialSystem& MaterialSystem,
		const FABTSM7StylizedMaterialSet& Materials,
		TArray<FABTSStylizedMaterialSlotBinding>& OutBindings);
	static void GatherSemanticBindings(
		const AABTSM7BuildingMaterialSystem& MaterialSystem,
		TArray<FABTSM7StylizedSemanticBinding>& OutBindings);
};
