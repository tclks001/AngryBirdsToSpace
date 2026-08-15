// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73BeamD1Types.h"

/** Site-local OBB whose axes retain the generator content-axis identity. */
struct ABTSRUNTIME_API FABTSM73BuildingFreezeV3OBB
{
	FVector Center = FVector::ZeroVector;
	FVector HalfExtent = FVector::ZeroVector;
	FVector ContentXAxisInSite = -FVector::RightVector;
	FVector ContentYAxisInSite = FVector::ForwardVector;
	FVector ContentZAxisInSite = FVector::UpVector;
};

/** The single E1 gameplay cap. It is deliberately outside the Beam load DAG. */
struct ABTSRUNTIME_API FABTSM73BuildingFreezeV3CapBinding
{
	FABTSM7BrickSpec BrickSpec;
	FTransform SiteLocalTransform = FTransform::Identity;
	FBox SiteLocalBounds = FBox(EForceInit::ForceInit);
	bool bLoadBearing = false;
	bool bWeaknessCandidate = false;
	EABTSM73BeamD1DeviceRole DeviceRole = EABTSM73BeamD1DeviceRole::None;
};

struct ABTSRUNTIME_API FABTSM73BuildingFreezeV3MaterialHistogram
{
	int32 Wood = 0;
	int32 Stone = 0;
	int32 Iron = 0;
	int32 Glass = 0;
	int32 Crystal = 0;

	int32 Total() const { return Wood + Stone + Iron + Glass + Crystal; }
	int32 Count(EABTSM7BuildingMaterial Material) const;
};

/** M7-owned V3 payload. Shared Integration/M3 DTO adaptation is intentionally separate. */
struct ABTSRUNTIME_API FABTSM73BuildingFreezeV3Descriptor
{
	int32 SchemaVersion = 0;
	int32 SourceManifestVersion = 0;
	int64 SourceManifestHash = 0;
	EABTSM73BeamDemoBuilding ManifestEntryId = EABTSM73BeamDemoBuilding::Custom;
	FName StableId;
	FName GameplayProfileId;
	int32 DifficultyTier = INDEX_NONE;
	int32 BuildingSeed = 0;
	int32 EncounterSlot = INDEX_NONE;
	EABTSM7BuildingMaterial PrimaryMaterial = EABTSM7BuildingMaterial::Wood;

	/** Body bricks and devices are already converted from content local to site local. */
	TArray<FABTSM73BeamD1BrickBinding> Bricks;
	TArray<FABTSM73BeamD1DeviceBinding> Devices;
	TArray<FABTSM73BuildingFreezeV3CapBinding> Caps;
	FABTSM73BuildingFreezeV3MaterialHistogram MaterialHistogram;

	FTransform ContentToSite = FTransform::Identity;
	FBox GeneratorLocalBounds = FBox(EForceInit::ForceInit);
	FBox SiteLocalBounds = FBox(EForceInit::ForceInit);
	FABTSM73BuildingFreezeV3OBB SiteLocalOBB;
	FBox PadBounds = FBox(EForceInit::ForceInit);
	FBox EffectBounds = FBox(EForceInit::ForceInit);

	uint64 SourceStage5ProductionHash = 0;
	uint64 SourceDeviceAssemblyHash = 0;
	uint64 StaticGeometryHash = 0;
	uint64 DescriptorHash = 0;
	uint64 ProductionHash = 0;
};

/** Compact committed snapshot used to fail closed on any V3 identity drift. */
struct ABTSRUNTIME_API FABTSM73BuildingFreezeV3FrozenIdentity
{
	EABTSM73BeamDemoBuilding ManifestEntryId = EABTSM73BeamDemoBuilding::Custom;
	int32 EncounterSlot = INDEX_NONE;
	EABTSM7BuildingMaterial PrimaryMaterial = EABTSM7BuildingMaterial::Wood;
	int32 DifficultyTier = INDEX_NONE;
	int32 BuildingSeed = 0;
	int32 BrickCount = 0;
	int32 DeviceCount = 0;
	int32 CapCount = 0;
	FABTSM73BuildingFreezeV3MaterialHistogram MaterialHistogram;
	FBox GeneratorLocalBounds = FBox(EForceInit::ForceInit);
	FBox SiteLocalBounds = FBox(EForceInit::ForceInit);
	FBox PadBounds = FBox(EForceInit::ForceInit);
	FBox EffectBounds = FBox(EForceInit::ForceInit);
	uint64 SourceStage5ProductionHash = 0;
	uint64 SourceDeviceAssemblyHash = 0;
	uint64 StaticGeometryHash = 0;
	uint64 ProductionHash = 0;
	uint64 DescriptorHash = 0;
};

/** Derives the fixed-six V3 building identity without mutating the legacy V2 contract. */
class ABTSRUNTIME_API FABTSM73BuildingFreezeV3
{
public:
	static constexpr int32 SchemaVersion = 3;
	static constexpr int32 ExpectedEntryCount = 6;
	static constexpr double PadSafetyMarginCM = 36.0;
	static constexpr double CrystalCapExtentCM = 72.0;
	static constexpr int32 FrozenSourceManifestVersion = 1;
	static constexpr int64 FrozenSourceManifestHash = 2324068295;
	static constexpr uint64 FrozenCatalogHash = 8960617043786800590ull;

	static const TArray<FABTSM73BuildingFreezeV3FrozenIdentity>&
		GetFrozenIdentities();

	static bool ResolvePrimaryMaterial(
		EABTSM73BeamDemoBuilding Id,
		EABTSM7BuildingMaterial& OutMaterial,
		FString& OutError);
	static bool ResolveEncounterSlot(
		EABTSM73BeamDemoBuilding Id,
		int32& OutEncounterSlot,
		FString& OutError);
	static bool DeriveAndValidate(
		EABTSM73BeamDemoBuilding Id,
		FABTSM73BuildingFreezeV3Descriptor& OutDescriptor,
		FString& OutError);
	/** Returns descriptors in encounter order: E2, E3, E4, E5, E1, E6. */
	static bool DeriveAndValidateCatalog(
		TArray<FABTSM73BuildingFreezeV3Descriptor>& OutDescriptors,
		uint64& OutCatalogHash,
		FString& OutError);
};
