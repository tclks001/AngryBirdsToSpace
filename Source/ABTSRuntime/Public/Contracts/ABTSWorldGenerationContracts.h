// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "World/ABTSM110FinaleTypes.h"

/**
 * Stable, consumer-facing purpose of a generated construction site.
 *
 * This enum deliberately does not expose M3 TaskGraph task types. M3 may evolve
 * its mission grammar without forcing M7 to include or reinterpret the complete
 * TaskGraph schema.
 */
enum class EABTSGeneratedBuildingPurpose : uint8
{
	Unsupported = 0,
	Workshop,
	DestructibleTarget,
	FurnaceRuins,
	FinaleLaunchReserved,
	Count
};

/**
 * Versioned identity shared by all generated-world snapshots.
 *
 * Snapshots are immutable value copies. They are not UObject references, are
 * not serialized into maps, and never grant a consumer write access to M3.
 */
struct ABTSRUNTIME_API FABTSGeneratedWorldIdentity
{
	static constexpr int32 CurrentContractVersion = 1;

	int32 ContractVersion = CurrentContractVersion;
	int32 WorldSeed = 0;
	int32 GeneratorVersion = 0;
	int32 GenerationAttempt = INDEX_NONE;
	bool bSourceWorldAccepted = false;

	bool IsUsable() const;
};

/**
 * One stable M7 construction input exported from M3.
 *
 * The currently unused encounter fields are intentionally part of version 1 so
 * M3 can add route progression, difficulty and presentation metadata without
 * changing the binary contract used by the parallel M7 worktree.
 */
struct ABTSRUNTIME_API FABTSGeneratedBuildingSite
{
	/** Opaque stable identity. MAX_uint64 is reserved as the invalid sentinel. */
	uint64 SiteId = MAX_uint64;
	int32 TaskId = INDEX_NONE;
	int32 CellId = INDEX_NONE;
	int32 SourceTaskTypeValue = 0;
	EABTSGeneratedBuildingPurpose Purpose =
		EABTSGeneratedBuildingPurpose::Unsupported;

	int32 EncounterIndex = INDEX_NONE;
	int32 DifficultyTier = 0;
	float NormalizedRouteProgress = -1.0f;
	FName LayoutArchetypeId = NAME_None;
	FName VisualThemeId = NAME_None;

	/** Exact deterministic seed formerly reconstructed by M7 from M3 fields. */
	int32 DeterministicSeed = 0;

	FTransform WorldTransform = FTransform::Identity;
	float MaxSlopeDegrees = 0.0f;
	FVector AnchorDirection = FVector::UpVector;
	FVector TangentForward = FVector::ForwardVector;
	FVector TangentRight = FVector::RightVector;
	FVector2D PadHalfExtentCM = FVector2D::ZeroVector;
	float PadEdgeBlendWidthCM = 0.0f;
	float PadTargetRadiusCM = 0.0f;
	bool bTerrainPadApplied = false;

	bool IsUsable(double Tolerance = 1.0e-3) const;
};

/**
 * Additive Fixed-Six V2 facts derived from the sealed M7 Stage 5/5.5 producer.
 *
 * V1 sites leave this structure empty. V2 keeps the static placement bounds
 * separate from the dynamic effect corridor so M3 can reserve world clearance
 * without inflating the building pad or changing the Stage 4 placement pivot.
 */
struct ABTSRUNTIME_API FABTSJuryDemoFixedSixV2Envelope
{
	uint64 StaticGeometryHash = 0;
	uint64 ProductionIdentityHash = 0;
	uint64 DeviceAssemblyHash = 0;
	FBox PhysicalBounds = FBox(EForceInit::ForceInit);
	FBox EffectBounds = FBox(EForceInit::ForceInit);
	bool bDynamicEnvelopeRequired = false;

	bool IsEmpty() const;
};

/** Stable support surface identity carried by a Fixed-Six V3 value snapshot. */
enum class EABTSJuryDemoFixedSixSurfaceKind : uint8
{
	Unknown = 0,
	PrimaryPlanet,
	Satellite,
	Count
};

/**
 * Additive Fixed-Six V3 building and placement facts.
 *
 * All values are immutable data. In particular, GravityAuthorityId names the
 * producer authority but this DTO never retains a Planet/Satellite UObject.
 */
struct ABTSRUNTIME_API FABTSJuryDemoFixedSixV3Envelope
{
	uint64 StaticGeometryHash = 0;
	uint64 ProductionIdentityHash = 0;
	uint64 DeviceAssemblyHash = 0;
	FBox SiteLocalBounds = FBox(EForceInit::ForceInit);
	FBox PadBounds = FBox(EForceInit::ForceInit);
	FBox EffectBounds = FBox(EForceInit::ForceInit);
	EABTSJuryDemoFixedSixSurfaceKind SurfaceKind =
		EABTSJuryDemoFixedSixSurfaceKind::Unknown;
	FVector SupportCenterWorldCM = FVector::ZeroVector;
	double SupportRadiusCM = 0.0;
	FName GravityAuthorityId = NAME_None;
	uint64 GravityIdentityHash = 0;
	uint64 PlacementHash = 0;

	bool IsEmpty() const;
};

/**
 * One immutable JuryDemoFixedSix V1/V2/V3 placement exported by M3 for exact M7
 * Manifest resolution.
 *
 * This DTO deliberately carries no weakness, attack-face or profile-search
 * data. M7 must resolve ManifestEntryId exactly and may not substitute another
 * entry or seed when any identity check fails.
 */
struct ABTSRUNTIME_API FABTSJuryDemoFixedSixBuildingSite
{
	FName ManifestEntryId = NAME_None;
	int32 EncounterIndex = INDEX_NONE;
	FTransform WorldTransform = FTransform::Identity;
	FVector2D PadHalfExtentCM = FVector2D::ZeroVector;
	FBox LocalBounds = FBox(EForceInit::ForceInit);
	int32 DifficultyTier = INDEX_NONE;
	int32 DeterministicSeed = 0;
	uint64 DescriptorHash = 0;
	FABTSJuryDemoFixedSixV2Envelope V2Envelope;
	FABTSJuryDemoFixedSixV3Envelope V3Envelope;

	bool IsUsable(double Tolerance = 1.0e-3) const;
	bool IsUsableForContractVersion(
		int32 ContractVersion,
		double Tolerance = 1.0e-3) const;
};

/**
 * Optional, additive vNext identity for the DDL-scoped fixed-six jury map.
 *
 * ContractVersion == 0 and otherwise-default fields mean that an existing v1
 * building snapshot does not publish this profile. Once any fixed-six state is
 * present, the complete frozen identity and all six ordered sites are required.
 */
struct ABTSRUNTIME_API FABTSJuryDemoFixedSixContract
{
	/** V1 remains the compatibility version; fixed-six production publishes V2. */
	static constexpr int32 CurrentContractVersion = 1;
	static constexpr int32 SupportedV2ContractVersion = 2;
	/** V3 is a handoff schema until Map Freeze publishes an approved Layout. */
	static constexpr int32 SupportedV3ContractVersion = 3;
	static constexpr int32 ProductionContractVersion =
		SupportedV2ContractVersion;
	static constexpr int32 ExpectedSiteCount = 6;
	static constexpr int32 FrozenPlacementSchemaVersion = 1;
	static constexpr int32 FrozenV3PlacementSchemaVersion = 3;
	static constexpr int32 FrozenDemoManifestVersion = 1;
	static constexpr uint64 FrozenDemoManifestHash = 2324068295ull;
	/** V1 alias retained for existing producers and tests. */
	static constexpr uint64 FrozenPlacementCatalogHash =
		13889440156022460967ull;
	static constexpr uint64 FrozenV2PlacementCatalogHash =
		11501529584318250152ull;
	static constexpr uint64 FrozenV3PlacementCatalogHash =
		8960617043786800590ull;
	static constexpr int32 FrozenWorldSeed = 312503;
	static constexpr int32 FrozenCandidateId = 4;
	/** Frozen V1 identity retained for backward-compatible readers and tests. */
	static constexpr uint64 FrozenLayoutHash = 0x8AB8D7E4F094072Dull;
	/** Exact M3 V2 result published after dynamic-envelope reservation. */
	static constexpr uint64 FrozenV2LayoutHash = 0x7029074579FDC52Eull;

	/** Zero means this additive snapshot is absent. */
	int32 ContractVersion = 0;
	int32 PlacementSchemaVersion = 0;
	int32 DemoManifestVersion = 0;
	uint64 DemoManifestHash = 0;
	uint64 PlacementCatalogHash = 0;
	int32 WorldSeed = 0;
	int32 CandidateId = INDEX_NONE;
	uint64 LayoutHash = 0;
	TArray<FABTSJuryDemoFixedSixBuildingSite> Sites;

	bool IsEmpty() const;
	/**
	 * Validates a complete V3 handoff without granting production authority.
	 * LayoutHash and PlacementHash values must be non-zero but are not approved
	 * until Integration freezes the M3 map identity in a later change.
	 */
	bool IsStructurallyUsableV3(double Tolerance = 1.0e-3) const;
	/** Accepts only production-approved V1/V2 snapshots at this stage. */
	bool IsUsable(double Tolerance = 1.0e-3) const;
};

/** Complete read-only input consumed by the M7 building-generation boundary. */
struct ABTSRUNTIME_API FABTSBuildingGenerationContract
{
	FABTSGeneratedWorldIdentity Identity;
	TArray<FABTSGeneratedBuildingSite> Sites;

	/**
	 * Additive exact-placement profile. Existing generic consumers may ignore an
	 * empty value; fixed-six consumers must require IsUsable().
	 */
	FABTSJuryDemoFixedSixContract JuryDemoFixedSix;

	bool IsUsable(double Tolerance = 1.0e-3) const;
};

/**
 * Complete read-only input consumed by the M11 finale boundary.
 *
 * M9's practice satellite and all M3 TaskGraph arrays are absent by design.
 */
struct ABTSRUNTIME_API FABTSFinaleWorldContract
{
	FABTSGeneratedWorldIdentity Identity;
	double PrimaryRadiusCM = 0.0;
	FABTSM110FinaleLocalFrame LaunchFrame;

	bool IsUsable(double Tolerance = 1.0e-3) const;
};
