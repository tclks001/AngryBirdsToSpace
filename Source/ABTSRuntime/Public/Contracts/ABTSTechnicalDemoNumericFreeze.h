// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Crafting/ABTSCraftingTypes.h"

/** One recipe identity whose topology is frozen independently from ingredient costs. */
struct ABTSRUNTIME_API FABTSFrozenRecipeTopologyEntry
{
	FName RecipeId = NAME_None;
	EABTSItemId OutputItem = EABTSItemId::Branch;
	int32 OutputQuantity = 0;
	EABTSCraftingStationType RequiredStation = EABTSCraftingStationType::None;
};

/**
 * Integration-owned Phase-1 numeric freeze for the fixed technical demo.
 *
 * This manifest binds existing authoritative catalogs; it never replaces their
 * runtime implementations. Ingredient costs, resource rewards and destruction
 * thresholds deliberately remain outside v1 until the end-to-end no-softlock
 * proof is complete.
 */
class ABTSRUNTIME_API FABTSTechnicalDemoNumericFreeze final
{
public:
	static constexpr int32 ManifestVersion = 1;
	static constexpr uint64 FrozenManifestHash = 0x3B5CD2611E423715ull;

	static constexpr int32 LaunchCurveFormulaVersion = 1;
	static constexpr int32 CraftingTransactionFormulaVersion = 1;
	static constexpr int32 ResourceDemandFormulaVersion = 1;
	static constexpr int32 ImpactDamageFormulaVersion = 1;

	static constexpr int32 FrozenLaunchCatalogVersion = 1;
	static constexpr uint64 FrozenLaunchProfileHash = 0xC2B94139752AD846ull;

	static constexpr int32 FrozenFixedSixContractVersion = 2;
	static constexpr int32 FrozenFixedSixPlacementSchemaVersion = 1;
	static constexpr int32 FrozenFixedSixDemoManifestVersion = 1;
	static constexpr uint64 FrozenFixedSixDemoManifestHash = 2324068295ull;
	static constexpr uint64 FrozenFixedSixPlacementCatalogHash =
		0x9F9DA4381EEF7CA8ull;
	static constexpr int32 FrozenFixedSixWorldSeed = 312503;
	static constexpr int32 FrozenFixedSixCandidateId = 4;
	static constexpr uint64 FrozenFixedSixLayoutHash = 0x7029074579FDC52Eull;

	static constexpr int32 FrozenM7PlacementSchemaVersion = 1;
	static constexpr uint64 FrozenM7PlacementCatalogHash =
		0x9F9DA4381EEF7CA8ull;
	static constexpr int32 FrozenM7ActiveMemberCount = 5504;

	static constexpr int32 FrozenM11PresetVersion = 1;
	static constexpr uint64 FrozenM11PresetSourceHash = 0x7DBF1BA71F67768Eull;
	static constexpr uint64 FrozenM11PresetHash = 0x7DBF1BA71F67768Eull;
	static constexpr uint32 FrozenM11ScenarioHash = 0x62D86D29u;
	static constexpr uint64 FrozenM11ScanContractHash = 0x8A6D71CF21E552C9ull;
	static constexpr uint64 FrozenM11CertificationHash = 0x941684A72E11B27Dull;
	static constexpr uint64 FrozenM11NominalTrajectoryHash = 0x185D3B673C1D52AFull;
	static constexpr int32 FrozenM11PhysicalPlaybackContractVersion = 1;
	static constexpr uint64 FrozenM11PhysicalPlaybackTrajectoryHash =
		0xCAC902C4183084AFull;
	static constexpr uint64 FrozenM11CertifiedBundleHash = 0xA219D69CF3F92AF0ull;

	static constexpr int32 FrozenRecipeTopologyVersion = 1;
	static constexpr uint64 FrozenRecipeTopologyHash = 0x3C17FB587178014Bull;

	static TConstArrayView<FABTSFrozenRecipeTopologyEntry>
	GetFrozenRecipeTopologyV1();

	static uint64 ComputeRecipeTopologyHash(
		TConstArrayView<FABTSFrozenRecipeTopologyEntry> Entries);
	static bool ValidateRecipeTopologyV1(
		TConstArrayView<FABTSCraftingRecipe> Recipes,
		FString* OutFailure = nullptr);

	/** Hash of the expected v1 identities, not a snapshot of mutable runtime state. */
	static uint64 ComputeManifestHash();

	/** Validates every current authoritative producer against the v1 manifest. */
	static bool ValidateCurrentProject(FString* OutFailure = nullptr);
};
