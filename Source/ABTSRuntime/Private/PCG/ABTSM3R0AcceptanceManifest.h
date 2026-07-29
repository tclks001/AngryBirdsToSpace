// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EABTSM3R0AcceptanceLayer : uint8
{
	Automation,
	FreshRuntime,
	VisiblePIE
};

struct FABTSM3R0AcceptanceEntry
{
	const TCHAR* EntryId = TEXT("");
	EABTSM3R0AcceptanceLayer Layer = EABTSM3R0AcceptanceLayer::Automation;
	const TCHAR* Target = TEXT("");
	int32 ExpectedCaseCount = 0;
	int32 ExpectedTerminalCount = 0;
};

struct FABTSM3R0ExpectedBuildingSite
{
	int32 TaskId = INDEX_NONE;
	int32 CellId = INDEX_NONE;
};

/**
 * Frozen acceptance identity for the first-week Gen3/Policy1 compatibility
 * oracle.  This is verification data only; it never participates in generation
 * or permits the monthly generator to fall back to the compatibility layout.
 */
class FABTSM3R0AcceptanceManifest final
{
public:
	static constexpr int32 SchemaVersion = 1;
	static constexpr int32 GeneratorVersion = 3;
	static constexpr int32 LayoutPolicyVersion = 1;
	static constexpr int32 DisplaySeed = 312503;
	static constexpr int64 DisplayConfigHash = 2795535429ll;
	static constexpr int64 DisplayLayoutHash = 2577447183ll;
	static constexpr int32 DisplayMainRouteDeciCM = 245834;
	static constexpr int32 DisplayBuildingGapDeciCM = 77368;
	static constexpr int32 DisplaySatelliteLaunchSeparationCentiDegrees = 9030;
	static constexpr float DisplayMainRouteLengthCM =
		DisplayMainRouteDeciCM / 10.0f;
	static constexpr float DisplayBuildingGapCM =
		DisplayBuildingGapDeciCM / 10.0f;
	static constexpr float DisplaySatelliteLaunchSeparationDegrees =
		DisplaySatelliteLaunchSeparationCentiDegrees / 100.0f;

	// Bit order: B1 default/max, B2 default/max, B3 default/max.
	static constexpr uint8 DisplayVisibilityMask = 0x03;
	static constexpr uint8 PackVisibility(
		const bool bB1Default,
		const bool bB1Max,
		const bool bB2Default,
		const bool bB2Max,
		const bool bB3Default,
		const bool bB3Max)
	{
		return (bB1Default ? 1u << 0 : 0u)
			| (bB1Max ? 1u << 1 : 0u)
			| (bB2Default ? 1u << 2 : 0u)
			| (bB2Max ? 1u << 3 : 0u)
			| (bB3Default ? 1u << 4 : 0u)
			| (bB3Max ? 1u << 5 : 0u);
	}

	// Changing any frozen hash requires an intentional manifest-version update and
	// fresh evidence from every entry.
	static constexpr uint64 FrozenWeekOneSeedManifestHash =
		0x3DE06FCA1D76EF0Full;
	static constexpr uint64 FrozenM110SeedManifestHash =
		0xD6D1C5BB00B49BB5ull;
	static constexpr uint64 FrozenManifestHash = 0xF3CC08FCEB6D6FC8ull;

	static TConstArrayView<int32> GetWeekOneSeeds();
	static TConstArrayView<int32> GetDeterminismSeeds();
	static TConstArrayView<int32> GetM110Seeds();
	static TConstArrayView<FABTSM3R0AcceptanceEntry> GetEntries();
	static TConstArrayView<FABTSM3R0ExpectedBuildingSite>
		GetDisplayBuildingSites();
	static const TCHAR* GetVisiblePIEChecklist();
	static const TCHAR* GetLayerName(EABTSM3R0AcceptanceLayer Layer);

	static uint64 ComputeWeekOneSeedManifestHash();
	static uint64 ComputeM110SeedManifestHash();
	static uint64 ComputeManifestHash();
	static FString BuildCanonicalPayload();
	static bool Validate(FString& OutFailure);
};
