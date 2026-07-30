// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EABTSM3R2AcceptanceLayer : uint8
{
	Automation = 0,
	FreshRuntime = 1
};

struct FABTSM3R2AcceptanceEntry
{
	const TCHAR* EntryId = TEXT("");
	EABTSM3R2AcceptanceLayer Layer =
		EABTSM3R2AcceptanceLayer::Automation;
	const TCHAR* Target = TEXT("");
	int32 ExpectedCaseCount = 0;
	int32 ExpectedTerminalCount = 0;
};

/**
 * Frozen M3R-2 route-only acceptance identity.
 *
 * This manifest certifies the parallel monthly route candidate pool. It does
 * not authorize a monthly world, replace Gen3/Policy1, or publish LayoutHash.
 */
class FABTSM3R2AcceptanceManifest final
{
public:
	static constexpr int32 ManifestSchemaVersion = 1;
	static constexpr int32 RoutePoolSchemaVersion = 1;
	static constexpr int32 CanonicalHashAlgorithmVersion = 1;
	static constexpr int32 GeneratorVersion = 3;
	static constexpr int32 MonthlyLayoutPolicyVersion = 2;
	static constexpr int32 DisplaySeed = 312503;
	static constexpr uint64 RequiredR1ManifestHash =
		0x57AB73741D5B0629ull;

	static constexpr uint64 FrozenSweepSeedManifestHash =
		0x588930CEC3A71BF2ull;
	static constexpr uint64 FrozenAcceptanceProfileHash =
		0x773EDEACA8B32025ull;
	static constexpr uint64 FrozenRouteOracleHash =
		0x059A0EE7C1C288FEull;
	static constexpr uint64 FrozenFailureFallbackHash =
		0x3E10F21BCB5E5700ull;
	static constexpr uint64 FrozenFailurePoolHash =
		0xA03845A65FEF0689ull;
	static constexpr uint64 FrozenFailureSnapshotHash =
		0x672BF5A0C3E91875ull;
	static constexpr uint64 FrozenDisplayPoolHash =
		0xE747FE054DD218F4ull;
	static constexpr uint64 FrozenDisplaySnapshotHash =
		0xC5FCCEA6089DBAC0ull;
	static constexpr uint64 FrozenManifestHash =
		0x3D33F37F4AD7A0E9ull;

	static constexpr int32 DisplayAttemptedCandidates = 8;
	static constexpr int32 DisplayNormalHardPassCount = 8;
	static constexpr int32 DisplayRetainedCandidates = 3;
	static constexpr int32 DisplayBestRouteLengthCM = 33537;
	static constexpr int32 DisplayBestScenicBendCount = 12;
	static constexpr int32 DisplayBestMaxStraightCM = 3500;
	static constexpr int32 DisplayBestSelfApproachCells = 7;
	static constexpr int32 DisplayBestScore = 999850;

	static TConstArrayView<int32> GetSweepSeeds();
	static TConstArrayView<FABTSM3R2AcceptanceEntry> GetEntries();

	static const TCHAR* GetLayerName(EABTSM3R2AcceptanceLayer Layer);
	static uint64 ComputeSweepSeedManifestHash();
	static uint64 ComputeAcceptanceProfileHash();
	static uint64 ComputeManifestHash();
	static FString BuildCanonicalPayload();
	static bool Validate(FString& OutFailure);
};
