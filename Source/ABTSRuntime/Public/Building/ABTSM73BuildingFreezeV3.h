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

/** The single E1 gameplay cap. It is not a bearing member, but its mass is an audited external load. */
struct ABTSRUNTIME_API FABTSM73BuildingFreezeV3CapBinding
{
	FABTSM7BrickSpec BrickSpec;
	FTransform SiteLocalTransform = FTransform::Identity;
	FBox SiteLocalBounds = FBox(EForceInit::ForceInit);
	bool bLoadBearing = false;
	bool bWeaknessCandidate = false;
	EABTSM73BeamD1DeviceRole DeviceRole = EABTSM73BeamD1DeviceRole::None;
	TArray<int32> SupportingMemberIds;
	bool bStaticExternalLoadCertified = false;
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

/** One deterministic compound Chaos body assembled from face-bearing bricks.
 * Presentation and per-brick damage identities remain one-to-one; only the
 * initial rigid-body/contact graph is coarsened. */
struct ABTSRUNTIME_API FABTSM73BuildingFreezeV3PhysicsCluster
{
	int32 ClusterId = INDEX_NONE;
	int32 RootBrickId = INDEX_NONE;
	TArray<int32> BrickIds;
	double StaticSelfLoadKG = 0.0;
	bool bDirectGroundSupport = false;
	int32 PositiveExternalSupportCount = 0;
	uint64 ClusterHash = 0;
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
	bool bStaticExternalLoadCertified = false;
	int32 StaticExternalLoadCount = 0;
	double StaticExternalMassKG = 0.0;
	double StaticDirectGroundMassKG = 0.0;
	int32 StaticSupportResultantAdvisoryCount = 0;
	uint64 StaticExternalLoadLedgerHash = 0;
	uint64 StaticExternalLoadDAGHash = 0;
	uint64 StaticExternalLoadCertificateHash = 0;
	/** E6-only atomic compound-body policy. Zero/empty means one body per module. */
	int32 PhysicsAssemblySchemaVersion = 0;
	int32 PhysicsBodyCount = 0;
	uint64 PhysicsAssemblyHash = 0;
	TArray<FABTSM73BuildingFreezeV3PhysicsCluster> PhysicsClusters;
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
	uint64 StaticExternalLoadCertificateHash = 0;
	int32 PhysicsBodyCount = 0;
	uint64 PhysicsAssemblyHash = 0;
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
	/** M7 atomic handoff published with the shared V3 seal by Integration. */
	static constexpr bool bE6CompoundV1Published = true;
	static constexpr uint64 PreE6CompoundV1CatalogHash =
		2428875568906321995ull;
	static constexpr uint64 E6CompoundV1CandidateCatalogHash =
		797455362285398432ull;
	static constexpr int32 E6CompoundV1PhysicsBodyCount = 809;
	static constexpr uint64 E6CompoundV1PhysicsAssemblyHash =
		5207773572942773531ull;
	static constexpr uint64 E6CompoundV1ProductionHash =
		9998077171702075971ull;
	static constexpr uint64 E6CompoundV1DescriptorHash =
		16759489927185121372ull;
	static constexpr uint64 FrozenCatalogHash = bE6CompoundV1Published
		? E6CompoundV1CandidateCatalogHash
		: PreE6CompoundV1CatalogHash;

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
		FString& OutError,
		bool bEnableE6CompoundV1Candidate = bE6CompoundV1Published);
	/** Returns descriptors in encounter order: E2, E3, E4, E5, E1, E6. */
	static bool DeriveAndValidateCatalog(
		TArray<FABTSM73BuildingFreezeV3Descriptor>& OutDescriptors,
		uint64& OutCatalogHash,
		FString& OutError);
	/** Explicit M7 atomic handoff; never substitutes for the published catalog. */
	static bool DeriveAndValidateE6CompoundV1CandidateCatalog(
		TArray<FABTSM73BuildingFreezeV3Descriptor>& OutDescriptors,
		uint64& OutCatalogHash,
		FString& OutError);
};
