// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/ABTSStylizedMaterialOverrideRegistry.h"

class AABTSM11FinaleSystem;
class UMaterialInterface;

/**
 * Borrowed M11-owned material interfaces for one binding collection pass.
 *
 * Assist entries are semantic Assist1/2/3 identities from the certified
 * layout, not Actor traversal positions. Null entries deliberately publish no
 * binding so the accepted pre-T3 material remains active.
 */
struct ABTSRUNTIME_API FABTSM11StylizedMaterialSet
{
	UMaterialInterface* Assist1PlanetMaterial = nullptr;
	UMaterialInterface* Assist2PlanetMaterial = nullptr;
	UMaterialInterface* Assist3PlanetMaterial = nullptr;
	UMaterialInterface* UFOMaterial = nullptr;

	UMaterialInterface* GetAssistPlanetMaterial(int32 AssistIndex) const;
};

/**
 * Read-only T3-A3 bridge from the committed M11 3+1 presentation to the
 * Integration-owned reversible material registry.
 *
 * This adapter neither owns a registry nor mutates components. Integration
 * may call CollectBindings and submit the returned declarations to its single
 * world registry.
 */
class ABTSRUNTIME_API FABTSM11StylizedMaterialAdapter final
{
public:
	/** Resolves the M11-owned default assets and fails soft per missing entry. */
	static void CollectBindings(
		const AABTSM11FinaleSystem& FinaleSystem,
		TArray<FABTSStylizedMaterialSlotBinding>& OutBindings);

	/** Injectable overload used by automation and explicit Integration wiring. */
	static void CollectBindings(
		const AABTSM11FinaleSystem& FinaleSystem,
		const FABTSM11StylizedMaterialSet& Materials,
		TArray<FABTSStylizedMaterialSlotBinding>& OutBindings);
};
