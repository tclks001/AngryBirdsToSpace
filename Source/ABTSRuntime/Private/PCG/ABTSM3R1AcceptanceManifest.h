// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCG/ABTSM3TaskGraphTypes.h"

enum class EABTSM3R1AcceptanceLayer : uint8
{
	Automation = 0,
	FreshRuntime = 1
};

struct FABTSM3R1AcceptanceEntry
{
	const TCHAR* EntryId = TEXT("");
	EABTSM3R1AcceptanceLayer Layer =
		EABTSM3R1AcceptanceLayer::Automation;
	const TCHAR* Target = TEXT("");
	int32 ExpectedCaseCount = 0;
	int32 ExpectedTerminalCount = 0;
};

struct FABTSM3R1CompatibilityOracle
{
	int32 Seed = 0;
	int64 ConfigHash = 0;
	int64 LayoutHash = 0;
	int32 AttemptIndex = INDEX_NONE;
	uint64 SnapshotHash = 0;
};

/**
 * Frozen M3R-1 acceptance identity. This manifest verifies the new schema and
 * the Gen3/Policy1 compatibility seam; it never participates in generation.
 */
class FABTSM3R1AcceptanceManifest final
{
public:
	static constexpr int32 ManifestSchemaVersion = 2;
	static constexpr int32 MonthlySchemaVersion = 1;
	static constexpr int32 CanonicalHashAlgorithmVersion = 2;
	static constexpr int32 QuantizationVersion = 1;
	static constexpr int32 GeneratorVersion = 3;
	static constexpr int32 CompatibilityLayoutPolicyVersion = 1;
	static constexpr int32 MonthlyLayoutPolicyVersion = 2;
	static constexpr int32 DisplaySeed = 312503;
	static constexpr uint64 RequiredR0ManifestHash =
		0xF3CC08FCEB6D6FC8ull;
	static constexpr uint64 FrozenCompatibilitySeedManifestHash =
		0x3DE06FCA1D76EF0Full;
	static constexpr uint64 FrozenSchemaFixtureSeedManifestHash =
		0x919267FB996F6A2Cull;
	static constexpr uint64 FrozenCompatibilityOracleHash =
		0x31D0B260C04BEB4Full;

	static constexpr uint64 FrozenDisplaySchemaConfigHash =
		0x1FC60A49D5354A32ull;
	static constexpr uint64 FrozenDisplaySchemaLayoutHash =
		0x28AC8C67CCB595CDull;
	static constexpr uint64 FrozenManifestHash =
		0x57AB73741D5B0629ull;

	static constexpr int32 DisplayRouteBeatCount = 9;
	static constexpr int32 DisplayEncounterCount = 3;
	static constexpr int32 DisplayPocketCount = 21;
	static constexpr int32 DisplayBiomeDistrictCount = 5;
	static constexpr int32 DisplayPlayableEnvelopeCount = 3;

	static TConstArrayView<int32> GetCompatibilitySeeds();
	static TConstArrayView<int32> GetSchemaFixtureSeeds();
	static TConstArrayView<FABTSM3R1CompatibilityOracle>
		GetCompatibilityOracles();
	static TConstArrayView<FABTSM3R1AcceptanceEntry> GetEntries();

	static const TCHAR* GetLayerName(EABTSM3R1AcceptanceLayer Layer);
	static uint64 ComputeCompatibilitySeedManifestHash();
	static uint64 ComputeSchemaFixtureSeedManifestHash();
	static uint64 ComputeCompatibilityOracleHash();
	static uint64 ComputeCompatibilitySnapshotHash(
		const TArray<FABTSM3TaskNode>& Tasks,
		const TArray<FABTSM3TaskLink>& Links,
		const TArray<FABTSM3CellState>& CellStates,
		const TArray<FABTSM3CellEdgeState>& EdgeStates,
		const FABTSM3PCGSummary& Summary);
	static uint64 ComputeManifestHash();
	static FString BuildCanonicalPayload();
	static bool Validate(FString& OutFailure);
};
