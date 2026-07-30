// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EABTSM3R5AcceptanceLayer : uint8
{
	Automation = 0,
	FreshRuntime = 1,
	IntegrationAutomation = 2,
	VisiblePIE = 3
};

struct FABTSM3R5AcceptanceEntry
{
	const TCHAR* EntryId = TEXT("");
	EABTSM3R5AcceptanceLayer Layer =
		EABTSM3R5AcceptanceLayer::Automation;
	const TCHAR* Target = TEXT("");
	int32 ExpectedCaseCount = 0;
	int32 ExpectedTerminalCount = 0;
};

/**
 * Frozen M3-local R-5 acceptance identity.
 *
 * It certifies candidate-bound presentation planning and an explicit preview
 * authority. It never certifies a selected or accepted monthly world.
 */
class FABTSM3R5AcceptanceManifest final
{
public:
	static constexpr int32 ManifestSchemaVersion = 1;
	static constexpr int32 PresentationSchemaVersion = 1;
	static constexpr int32 PlannerVersion = 1;
	static constexpr int32 GeneratorVersion = 3;
	static constexpr int32 MonthlyLayoutPolicyVersion = 2;
	static constexpr int32 DisplaySeed = 312503;
	static constexpr int32 DisplayPreviewSourceCandidateId = 4;
	static constexpr int32 SweepSeedCount = 100;
	static constexpr int32 SweepCandidatePlanCount = 300;
	static constexpr int32 SweepMergedLogicalSingletonCount = 2;
	static constexpr int32 SweepMergedSmallVisualFragmentCellCount = 2;

	static constexpr int32 DefaultMinVisualBeatLengthCM = 2000;
	static constexpr int32 DefaultTargetVisualBeatLengthCM = 3000;
	static constexpr int32 DefaultMaxVisualBeatLengthCM = 4500;
	static constexpr int32 DefaultMinBiomeArchetypeCount = 4;
	static constexpr int32 DefaultMinActiveRoleCoveragePermille = 750;
	static constexpr int32 DefaultMaxDeepWildPermille = 200;
	static constexpr int32 DefaultMaxDecorInstancesPerCell = 2;
	static constexpr int32 DefaultMaxDecorInstancesPerCandidate = 1024;
	static constexpr int32 PlannerP95BudgetMS = 250;
	static constexpr int32 PlannerMaxBudgetMS = 1000;
	static constexpr int32 FullRebuildBudgetMS = 8000;
	static constexpr int32 RequiredSurfaceSubdivision = 7;
	static constexpr int32 BaselinePeakPhysicalMB = 2250;
	static constexpr int32 MaxPeakPhysicalPermille = 1150;
	static constexpr int32 MinVisualBiomeComponentCells = 3;
	static constexpr int32 MaxVisualBiomeBoundaryPermille = 250;

	static constexpr uint64 RequiredR3ManifestHash =
		0x4F1A236CDF81B80Dull;
	static constexpr uint64 FrozenSweepSeedManifestHash =
		0x5610DCBA0A03D9CBull;
	static constexpr uint64 FrozenDisplayConfigHash =
		0x9BB9CF98FB4127F9ull;
	static constexpr uint64 FrozenDisplaySourceSpatialHash =
		0x550F7B095B788C49ull;
	static constexpr uint64 FrozenDisplayResultHash =
		0xEC87F999625CBE44ull;
	static constexpr uint64 FrozenDisplayPreviewCandidateHash =
		0x9BE1F04A45277AEFull;
	static constexpr uint64 FrozenSweepOracleHash =
		0x6751B93DA5E4C778ull;
	static constexpr uint64 FrozenManifestHash =
		0x9E5A2FE0E563A7C4ull;

	static TConstArrayView<int32> GetSweepSeeds();
	static TConstArrayView<FABTSM3R5AcceptanceEntry> GetEntries();
	static const TCHAR* GetLayerName(EABTSM3R5AcceptanceLayer Layer);
	static uint64 ComputeSweepSeedManifestHash();
	static FString BuildCanonicalPayload();
	static uint64 ComputeManifestHash();
	static bool Validate(FString& OutFailure);
};
