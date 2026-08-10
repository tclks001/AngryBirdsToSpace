// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/ABTSStylizedMaterialOverrideRegistry.h"

class AActor;
class UMaterialInterface;
class UPrimitiveComponent;

/**
 * Integration-owned T3-A2 adapter for shared CuteBird and slingshot visuals.
 *
 * Semantic identity is resolved from an explicit accepted source-material
 * catalog. Slot numbers never decide whether a surface is body, face, organic
 * or metal, and no gameplay, collision, physical-material or calibration state
 * is read or changed.
 */
class ABTSRUNTIME_API FABTSSharedStylizedMaterialAdapter final
{
public:
	static int32 GatherActorBindings(
		const AActor& Actor,
		TArray<FABTSStylizedMaterialSlotBinding>& OutBindings);
	static int32 GatherPrimitiveBindings(
		TConstArrayView<UPrimitiveComponent*> Primitives,
		TArray<FABTSStylizedMaterialSlotBinding>& OutBindings);

	static bool TryResolveMaterial(
		const UMaterialInterface& SourceMaterial,
		UMaterialInterface*& OutStylizedMaterial,
		EABTSStylizedMaterialFamily& OutFamily);

	/**
	 * Synchronously loads every shared candidate before first gameplay use.
	 * The caller owns the returned strong references for the world's lifetime.
	 */
	static int32 PreloadCatalogMaterials(
		TArray<UMaterialInterface*>& OutMaterials,
		int32& OutFailureCount);

	static int32 GetCatalogEntryCount();
	static uint32 GetCatalogHash();
};
