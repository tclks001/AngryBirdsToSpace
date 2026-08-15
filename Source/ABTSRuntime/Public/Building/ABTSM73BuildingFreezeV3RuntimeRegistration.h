// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/ABTSM73BuildingFreezeV3.h"
#include "CoreMinimal.h"

class AABTSM73StableBuildingActor;
class AABTSM7BuildingMaterialSystem;
class UWorld;

/**
 * M7-only placement input used before the shared Fixed-Six V3 DTO exists.
 * ComplexityId is the E1-E6 building identity; EncounterSlot is its separate
 * play-order slot and must never be inferred from the enum ordinal.
 */
struct ABTSRUNTIME_API FABTSM73BuildingFreezeV3RuntimePlacement
{
	EABTSM73BeamDemoBuilding ComplexityId =
		EABTSM73BeamDemoBuilding::Custom;
	int32 EncounterSlot = INDEX_NONE;
	FTransform WorldTransform = FTransform::Identity;
};

/** One verified, placement-bound V3 payload ready for a StableBuildingActor. */
struct ABTSRUNTIME_API FABTSM73BuildingFreezeV3RuntimeEntry
{
	EABTSM73BeamDemoBuilding ComplexityId =
		EABTSM73BeamDemoBuilding::Custom;
	FName StableId;
	FName GameplayProfileId;
	int32 ComplexityIndex = INDEX_NONE;
	int32 ComplexityTier = INDEX_NONE;
	int32 EncounterSlot = INDEX_NONE;
	int32 DeterministicSeed = 0;
	EABTSM7BuildingMaterial PrimaryMaterial =
		EABTSM7BuildingMaterial::Wood;
	FTransform WorldTransform = FTransform::Identity;
	FBox SiteLocalBounds = FBox(EForceInit::ForceInit);
	FBox PadBounds = FBox(EForceInit::ForceInit);
	FBox EffectBounds = FBox(EForceInit::ForceInit);
	FABTSM73BuildingFreezeV3MaterialHistogram MaterialHistogram;
	uint64 DescriptorHash = 0;
	uint64 StaticGeometryHash = 0;
	uint64 ProductionHash = 0;
	uint64 DeviceAssemblyHash = 0;
	uint64 RuntimePlacementHash = 0;
	uint64 RegistrationResultHash = 0;
	TArray<FABTSM73BeamD1BrickBinding> Bricks;
	TArray<FABTSM73BeamD1DeviceBinding> Devices;
	TArray<FABTSM73BuildingFreezeV3CapBinding> Caps;

	bool IsUsable(double Tolerance = 1.0e-3) const;
};

/**
 * Atomic fixture plan. It proves runtime assembly without claiming an M3
 * LayoutHash or production placement authority.
 */
struct ABTSRUNTIME_API FABTSM73BuildingFreezeV3RuntimePlan
{
	int32 SchemaVersion = 0;
	FName Authority;
	uint64 CatalogHash = 0;
	uint64 SourceLayoutHash = 0;
	uint64 RuntimePlacementHash = 0;
	uint64 RegistrationResultHash = 0;
	TArray<FABTSM73BuildingFreezeV3RuntimeEntry> Entries;

	bool IsUsable(double Tolerance = 1.0e-3) const;
};

/** M7-owned V3 fixture planner and transactional runtime registrar. */
class ABTSRUNTIME_API FABTSM73BuildingFreezeV3RuntimeRegistration
{
public:
	static const FName FixtureAuthority;

	/**
	 * Builds from the frozen M7 catalog and caller-supplied fixture transforms.
	 * Placements must be in encounter order, but each row carries its independent
	 * E1-E6 complexity identity.
	 */
	static bool BuildFixturePlan(
		TConstArrayView<FABTSM73BuildingFreezeV3RuntimePlacement> Placements,
		FABTSM73BuildingFreezeV3RuntimePlan& OutPlan,
		FString& OutError);

	static bool SpawnStaticActors(
		UWorld& World,
		AABTSM7BuildingMaterialSystem& MaterialSystem,
		TSubclassOf<AABTSM73StableBuildingActor> BuildingClass,
		FABTSM73BuildingFreezeV3RuntimePlan&& Plan,
		TArray<TWeakObjectPtr<AABTSM73StableBuildingActor>>& OutActors,
		FString& OutError);
};
