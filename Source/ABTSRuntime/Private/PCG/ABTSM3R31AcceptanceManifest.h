// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EABTSM3R31AcceptanceLayer : uint8
{
	Automation = 0,
	FreshRuntime = 1,
	IntegrationAutomation = 2,
	VisiblePIE = 3
};

struct FABTSM3R31AcceptanceEntry
{
	const TCHAR* EntryId = TEXT("");
	EABTSM3R31AcceptanceLayer Layer =
		EABTSM3R31AcceptanceLayer::Automation;
	const TCHAR* Target = TEXT("");
	int32 ExpectedCaseCount = 0;
	int32 ExpectedTerminalCount = 0;
};

/**
 * Frozen acceptance identity for the additive M3R-3.1 slot-field layer.
 *
 * M3LocalAccepted covers deterministic data production. Physical DirtHole
 * spawning, exact runtime cord length and stake/cord obstruction remain
 * Integration-owned entries and are not implied by a valid local manifest.
 */
class FABTSM3R31AcceptanceManifest final
{
public:
	static constexpr int32 ManifestSchemaVersion = 1;
	static constexpr int32 SlotFieldSchemaVersion = 1;
	static constexpr int32 GeneratorVersion = 3;
	static constexpr int32 MonthlyLayoutPolicyVersion = 2;
	static constexpr int32 DisplaySeed = 312503;
	static constexpr int32 SweepSeedCount = 100;
	static constexpr int32 DefaultAdditionalSlotsPerField = 5;
	static constexpr int32 DefaultAdditionalRoadFieldCount = 2;
	static constexpr int32 DefaultMaxCordLengthCM = 1200;
	static constexpr int32 DisplayFieldsPerCandidate = 8;
	static constexpr int32 DisplaySlotsPerCandidate = 56;
	static constexpr uint64 RequiredR3ManifestHash =
		0xE71AA286BB4B273Aull;
	static constexpr uint64 FrozenSweepSeedManifestHash =
		0x5610DCBA0A03D9CBull;
	static constexpr uint64 FrozenDisplayConfigHash =
		0xD08CCF16744A2B7Bull;
	static constexpr uint64 FrozenDisplayResultHash =
		0xE17A5F2FF30221E6ull;
	static constexpr uint64 FrozenDisplayCandidateHash =
		0xCD79141DA5C277C0ull;
	static constexpr uint64 FrozenSweepOracleHash =
		0x8071E747415A20F2ull;

	// Calibrated from BuildCanonicalPayload after all identities above freeze.
	static constexpr uint64 FrozenManifestHash =
		0xA7783FACECF3FE4Aull;

	static TConstArrayView<int32> GetSweepSeeds();
	static TConstArrayView<FABTSM3R31AcceptanceEntry> GetEntries();
	static const TCHAR* GetLayerName(
		EABTSM3R31AcceptanceLayer Layer);
	static uint64 ComputeSweepSeedManifestHash();
	static FString BuildCanonicalPayload();
	static uint64 ComputeManifestHash();
	static bool Validate(FString& OutFailure);
};
