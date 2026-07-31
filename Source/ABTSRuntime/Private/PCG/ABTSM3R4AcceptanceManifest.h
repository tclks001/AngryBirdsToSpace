// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EABTSM3R4AcceptanceLayer : uint8
{
	Automation = 0,
	FreshRuntime = 1,
	IntegrationAutomation = 2,
	VisiblePIE = 3
};

struct FABTSM3R4AcceptanceEntry
{
	const TCHAR* EntryId = TEXT("");
	EABTSM3R4AcceptanceLayer Layer =
		EABTSM3R4AcceptanceLayer::Automation;
	const TCHAR* Target = TEXT("");
	int32 ExpectedCaseCount = 0;
	int32 ExpectedTerminalCount = 0;
};

/**
 * Frozen M3-local acceptance identity for R4.
 *
 * Fixture authority certifies the deterministic finalizer only. The manifest
 * keeps real M6/M9/M7/bridge and Visible PIE gates explicitly pending.
 */
class FABTSM3R4AcceptanceManifest final
{
public:
	static constexpr int32 ManifestSchemaVersion = 1;
	static constexpr int32 WitnessSchemaVersion = 1;
	static constexpr int32 GeneratorVersion = 3;
	static constexpr int32 MonthlyLayoutPolicyVersion = 2;
	static constexpr int32 DisplaySeed = 312503;
	static constexpr int32 SweepSeedCount = 100;
	static constexpr int32 DefaultEvaluationBudget = 8192;
	static constexpr int32 DefaultPullSamples = 7;
	static constexpr int32 DefaultAimAxisSamples = 3;
	static constexpr uint64 RequiredR31ManifestHash =
		0xA7783FACECF3FE4Aull;
	static constexpr uint64 FrozenSweepSeedManifestHash =
		0x5610DCBA0A03D9CBull;

	static constexpr uint64 FrozenDisplayConfigHash =
		0xE7831808F41259DAull;
	static constexpr uint64 FrozenDisplayResultHash =
		0x3F148C763A8AB08Eull;
	static constexpr uint64 FrozenDisplayCandidateHash =
		0x2C9798D1B1BE3B14ull;
	static constexpr uint64 FrozenDisplayGameplayLayoutHash =
		0x919D8B8777E98DC5ull;
	static constexpr uint64 FrozenSweepOracleHash =
		0x73E737B64B33E3BFull;
	static constexpr uint64 FrozenManifestHash =
		0x735D1CEB18102607ull;

	static TConstArrayView<int32> GetSweepSeeds();
	static TConstArrayView<FABTSM3R4AcceptanceEntry> GetEntries();
	static const TCHAR* GetLayerName(EABTSM3R4AcceptanceLayer Layer);
	static uint64 ComputeSweepSeedManifestHash();
	static FString BuildCanonicalPayload();
	static uint64 ComputeManifestHash();
	static bool Validate(FString& OutFailure);
};
