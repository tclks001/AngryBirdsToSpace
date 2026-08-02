// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EABTSM3R3AcceptanceLayer : uint8
{
	Automation = 0,
	FreshRuntime = 1
};

struct FABTSM3R3AcceptanceEntry
{
	const TCHAR* EntryId = TEXT("");
	EABTSM3R3AcceptanceLayer Layer =
		EABTSM3R3AcceptanceLayer::Automation;
	const TCHAR* Target = TEXT("");
	int32 ExpectedCaseCount = 0;
	int32 ExpectedTerminalCount = 0;
};

/**
 * Frozen M3R-3 acceptance identity.
 *
 * R-3 certifies only the M3-private six-Encounter spatial candidate pool.
 * It does not publish a monthly LayoutHash, instantiate M7 Actors, or replace
 * the frozen Gen3/Policy1 compatibility world and its four exported sites.
 */
class FABTSM3R3AcceptanceManifest final
{
public:
	static constexpr int32 ManifestSchemaVersion = 1;
	static constexpr int32 SpatialSchemaVersion = 1;
	static constexpr int32 CanonicalHashAlgorithmVersion = 1;
	static constexpr int32 GeneratorVersion = 3;
	static constexpr int32 MonthlyLayoutPolicyVersion = 2;
	static constexpr int32 DisplaySeed = 312503;
	static constexpr int32 SweepSeedCount = 100;
	static constexpr int32 ReferenceSeedCount = 11;
	static constexpr int32 MaxOptimizedPVSRaysPerWorld = 1024;
	static constexpr int32 EncounterSpatialP95BudgetMS = 750;
	static constexpr int32 EncounterSpatialMaxBudgetMS = 2000;
	static constexpr uint64 RequiredR2ManifestHash =
		0x3D33F37F4AD7A0E9ull;

	// Calibrated only from fresh-process R-3 automation.
	static constexpr uint64 FrozenSweepSeedManifestHash =
		0x5610DCBA0A03D9CBull;
	static constexpr uint64 FrozenReferenceSeedManifestHash =
		0xEA5240A493FAFF92ull;
	static constexpr uint64 FrozenFixtureProfileCatalogHash =
		0x0052B1916220B715ull;
	static constexpr uint64 FrozenSweepOracleHash =
		0xA5625BBBF4B16CB6ull;
	static constexpr uint64 FrozenReferencePVSOracleHash =
		0xF5C27D592F6E0A62ull;
	static constexpr uint64 FrozenReferenceBoundaryOracleHash =
		0x114A4C8172CFEE2Full;
	static constexpr uint64 FrozenDisplayResultHash =
		0x16A44AF72C58261Eull;
	static constexpr uint64 FrozenDisplaySnapshotHash =
		0x14EE4ECD0FB7B3D1ull;
	static constexpr uint64 FrozenDisplayCandidateHash =
		0x645E131BE34A5B3Eull;
	static constexpr uint64 FrozenBlockedRoadResultHash =
		0xE304EF2A07F9AE1Aull;
	static constexpr uint64 FrozenBlockedRoadSnapshotHash =
		0xBCCD863372C1C049ull;
	static constexpr uint64 FrozenInvalidPVSResultHash =
		0x791633917546E20Eull;
	static constexpr uint64 FrozenInvalidPVSSnapshotHash =
		0xC22841CE0783F607ull;
	static constexpr uint64 FrozenRayBudgetResultHash =
		0x085A78420F3DE440ull;
	static constexpr uint64 FrozenRayBudgetSnapshotHash =
		0xFEB1461B1CFBCD53ull;
	static constexpr uint64 FrozenManifestHash =
		0xD5E9EAF889A08018ull;

	static constexpr int32 DisplayAttemptedRouteCandidates = 3;
	static constexpr int32 DisplaySpatialHardPassCount = 3;
	static constexpr int32 DisplayRetainedCandidates = 3;
	static constexpr int32 DisplayRecomputedRouteLengthCM = 33537;
	static constexpr int32 DisplayEncounterCount = 6;
	static constexpr int32 DisplayPocketCount = 42;
	static constexpr int32 DisplayBiomeDistrictCount = 7;
	static constexpr int32 DisplayPlayableCellCount = 843;
	static constexpr int32 DisplayApprovedTransitionCellCount = 198;
	static constexpr int32 DisplayActiveCoveragePermille = 765;
	static constexpr int32 DisplayDeepWildPermille = 0;
	static constexpr int32 DisplayOptimizedPVSRays = 468;

	static TConstArrayView<int32> GetSweepSeeds();
	static TConstArrayView<int32> GetReferenceSeeds();
	static TConstArrayView<FABTSM3R3AcceptanceEntry> GetEntries();

	static const TCHAR* GetLayerName(EABTSM3R3AcceptanceLayer Layer);
	static uint64 ComputeSweepSeedManifestHash();
	static uint64 ComputeReferenceSeedManifestHash();
	static uint64 ComputeManifestHash();
	static FString BuildCanonicalPayload();
	static bool Validate(FString& OutFailure);
};
