// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3R0AcceptanceManifest.h"

#include "Containers/StringConv.h"

namespace
{
constexpr uint64 Fnv1a64OffsetBasis = 14695981039346656037ull;
constexpr uint64 Fnv1a64Prime = 1099511628211ull;

const int32 WeekOneSeeds[] = {
	312503,
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
	10, 11, 12, 13, 14, 15, 16, 17, 18, 19
};

const int32 DeterminismSeeds[] = {0, 7, 19, 312503};

const TArray<int32>& GetM110SeedStorage()
{
	static const TArray<int32> Seeds = []
	{
		TArray<int32> Result;
		Result.Reserve(103);
		for (int32 Seed = 0; Seed < 100; ++Seed)
		{
			Result.Add(Seed);
		}
		Result.Add(312503);
		Result.Add(20260727);
		Result.Add(8675309);
		return Result;
	}();
	return Seeds;
}

const FABTSM3R0AcceptanceEntry AcceptanceEntries[] = {
	{
		TEXT("Automation.WeekOne"),
		EABTSM3R0AcceptanceLayer::Automation,
		TEXT("ABTS.M3.WeekOne"),
		2,
		1
	},
	{
		TEXT("Automation.WorldGenerationContract"),
		EABTSM3R0AcceptanceLayer::Automation,
		TEXT("ABTS.Contracts.WorldGeneration"),
		2,
		1
	},
	{
		TEXT("Automation.M110FinaleSeparation"),
		EABTSM3R0AcceptanceLayer::Automation,
		TEXT("ABTS.M110.TaskGraphFinaleSeparation"),
		1,
		1
	},
	{
		TEXT("Runtime.LABTSM3"),
		EABTSM3R0AcceptanceLayer::FreshRuntime,
		TEXT("/Game/Maps/L_ABTS_M3"),
		1,
		1
	},
	{
		TEXT("VisiblePIE.LABTSM10"),
		EABTSM3R0AcceptanceLayer::VisiblePIE,
		TEXT("/Game/Maps/L_ABTS_M10"),
		1,
		1
	}
};

// BuildBuildingSpawnSites scans CellState in ascending CellId order.  The
// launch reservation is intentionally part of this four-site sequence but is
// not an ordinary M7 building.
const FABTSM3R0ExpectedBuildingSite DisplayBuildingSites[] = {
	{6, 7683},
	{1, 8864},
	{3, 9168},
	{5, 9763}
};

constexpr TCHAR VisiblePIEChecklist[] =
	TEXT("B1ReadableAtStart;B2HiddenAtStart;B3HiddenAtStart;")
	TEXT("NoCoordinateOrPermanentLabelLeak;B2RevealReachable;")
	TEXT("B3RevealReachable;ThreeM7IdleAccepted");

uint64 HashUtf8FNV1a64(const FString& Value)
{
	const FTCHARToUTF8 Utf8(*Value);
	uint64 Hash = Fnv1a64OffsetBasis;
	for (int32 Index = 0; Index < Utf8.Length(); ++Index)
	{
		Hash ^= static_cast<uint8>(Utf8.Get()[Index]);
		Hash *= Fnv1a64Prime;
	}
	return Hash;
}

FString BuildCanonicalSeedList(const TConstArrayView<int32> Seeds)
{
	FString Result;
	for (int32 Index = 0; Index < Seeds.Num(); ++Index)
	{
		if (Index > 0)
		{
			Result.AppendChar(TEXT(','));
		}
		Result.Append(FString::FromInt(Seeds[Index]));
	}
	return Result;
}
}

TConstArrayView<int32> FABTSM3R0AcceptanceManifest::GetWeekOneSeeds()
{
	return MakeArrayView(WeekOneSeeds);
}

TConstArrayView<int32> FABTSM3R0AcceptanceManifest::GetDeterminismSeeds()
{
	return MakeArrayView(DeterminismSeeds);
}

TConstArrayView<int32> FABTSM3R0AcceptanceManifest::GetM110Seeds()
{
	return GetM110SeedStorage();
}

TConstArrayView<FABTSM3R0AcceptanceEntry>
FABTSM3R0AcceptanceManifest::GetEntries()
{
	return MakeArrayView(AcceptanceEntries);
}

TConstArrayView<FABTSM3R0ExpectedBuildingSite>
FABTSM3R0AcceptanceManifest::GetDisplayBuildingSites()
{
	return MakeArrayView(DisplayBuildingSites);
}

const TCHAR* FABTSM3R0AcceptanceManifest::GetVisiblePIEChecklist()
{
	return VisiblePIEChecklist;
}

const TCHAR* FABTSM3R0AcceptanceManifest::GetLayerName(
	const EABTSM3R0AcceptanceLayer Layer)
{
	switch (Layer)
	{
	case EABTSM3R0AcceptanceLayer::Automation:
		return TEXT("Automation");
	case EABTSM3R0AcceptanceLayer::FreshRuntime:
		return TEXT("FreshRuntime");
	case EABTSM3R0AcceptanceLayer::VisiblePIE:
		return TEXT("VisiblePIE");
	default:
		return TEXT("Invalid");
	}
}

uint64 FABTSM3R0AcceptanceManifest::ComputeWeekOneSeedManifestHash()
{
	return HashUtf8FNV1a64(BuildCanonicalSeedList(GetWeekOneSeeds()));
}

uint64 FABTSM3R0AcceptanceManifest::ComputeM110SeedManifestHash()
{
	return HashUtf8FNV1a64(BuildCanonicalSeedList(GetM110Seeds()));
}

FString FABTSM3R0AcceptanceManifest::BuildCanonicalPayload()
{
	FString Payload;
	Payload.Reserve(2048);
	Payload.Append(TEXT("M3R0AcceptanceManifest\n"));
	Payload.Append(FString::Printf(
		TEXT("SchemaVersion=%d\n"),
		SchemaVersion));
	Payload.Append(FString::Printf(
		TEXT("CompatibilityOracle=Gen%d/Policy%d\n"),
		GeneratorVersion,
		LayoutPolicyVersion));
	Payload.Append(FString::Printf(
		TEXT("GeneratorVersion=%d\n"),
		GeneratorVersion));
	Payload.Append(FString::Printf(
		TEXT("LayoutPolicyVersion=%d\n"),
		LayoutPolicyVersion));
	Payload.Append(FString::Printf(
		TEXT("DisplaySeed=%d\n"),
		DisplaySeed));
	Payload.Append(FString::Printf(
		TEXT("DisplayConfigHash=%lld\n"),
		static_cast<long long>(DisplayConfigHash)));
	Payload.Append(FString::Printf(
		TEXT("DisplayLayoutHash=%lld\n"),
		static_cast<long long>(DisplayLayoutHash)));
	Payload.Append(FString::Printf(
		TEXT("DisplayMainRouteCM=%d.%d\n"),
		DisplayMainRouteDeciCM / 10,
		DisplayMainRouteDeciCM % 10));
	Payload.Append(FString::Printf(
		TEXT("DisplayBuildingGapCM=%d.%d\n"),
		DisplayBuildingGapDeciCM / 10,
		DisplayBuildingGapDeciCM % 10));
	Payload.Append(FString::Printf(
		TEXT("DisplaySatelliteLaunchSepDeg=%d.%02d\n"),
		DisplaySatelliteLaunchSeparationCentiDegrees / 100,
		DisplaySatelliteLaunchSeparationCentiDegrees % 100));
	Payload.Append(FString::Printf(
		TEXT("DisplayVisibility=%d%d/%d%d/%d%d\n"),
		(DisplayVisibilityMask >> 0) & 1,
		(DisplayVisibilityMask >> 1) & 1,
		(DisplayVisibilityMask >> 2) & 1,
		(DisplayVisibilityMask >> 3) & 1,
		(DisplayVisibilityMask >> 4) & 1,
		(DisplayVisibilityMask >> 5) & 1));
	Payload.Append(FString::Printf(
		TEXT("WeekOneSeedManifestHash=%016llX\n"),
		static_cast<unsigned long long>(ComputeWeekOneSeedManifestHash())));
	Payload.Append(TEXT("WeekOneSeeds="));
	Payload.Append(BuildCanonicalSeedList(GetWeekOneSeeds()));
	Payload.AppendChar(TEXT('\n'));
	Payload.Append(TEXT("DeterminismSeeds="));
	Payload.Append(BuildCanonicalSeedList(GetDeterminismSeeds()));
	Payload.AppendChar(TEXT('\n'));
	Payload.Append(FString::Printf(
		TEXT("M110SeedManifestHash=%016llX\n"),
		static_cast<unsigned long long>(ComputeM110SeedManifestHash())));
	Payload.Append(TEXT("M110Seeds="));
	Payload.Append(BuildCanonicalSeedList(GetM110Seeds()));
	Payload.AppendChar(TEXT('\n'));
	Payload.Append(TEXT("VisiblePIEChecklist="));
	Payload.Append(VisiblePIEChecklist);
	Payload.AppendChar(TEXT('\n'));
	for (const FABTSM3R0ExpectedBuildingSite& Site : DisplayBuildingSites)
	{
		Payload.Append(FString::Printf(
			TEXT("DisplayBuildingSite=%d|%d\n"),
			Site.TaskId,
			Site.CellId));
	}
	for (const FABTSM3R0AcceptanceEntry& Entry : AcceptanceEntries)
	{
		Payload.Append(FString::Printf(
			TEXT("Entry=%s|%s|%s|%d|%d\n"),
			Entry.EntryId,
			GetLayerName(Entry.Layer),
			Entry.Target,
			Entry.ExpectedCaseCount,
			Entry.ExpectedTerminalCount));
	}
	return Payload;
}

uint64 FABTSM3R0AcceptanceManifest::ComputeManifestHash()
{
	return HashUtf8FNV1a64(BuildCanonicalPayload());
}

bool FABTSM3R0AcceptanceManifest::Validate(FString& OutFailure)
{
	OutFailure.Reset();
	const TConstArrayView<int32> Seeds = GetWeekOneSeeds();
	if (Seeds.Num() != 21 || Seeds[0] != DisplaySeed)
	{
		OutFailure = TEXT("WeekOneSeedCountOrDisplaySeed");
		return false;
	}

	TSet<int32> UniqueSeeds;
	for (const int32 Seed : Seeds)
	{
		if (UniqueSeeds.Contains(Seed))
		{
			OutFailure = FString::Printf(TEXT("DuplicateWeekOneSeed:%d"), Seed);
			return false;
		}
		UniqueSeeds.Add(Seed);
	}

	const TConstArrayView<int32> DeterminismSeedList =
		GetDeterminismSeeds();
	if (DeterminismSeedList.Num() != 4)
	{
		OutFailure = TEXT("DeterminismSeedCount");
		return false;
	}
	TSet<int32> UniqueDeterminismSeeds;
	for (const int32 Seed : DeterminismSeedList)
	{
		if (!UniqueSeeds.Contains(Seed)
			|| UniqueDeterminismSeeds.Contains(Seed))
		{
			OutFailure =
				FString::Printf(TEXT("InvalidDeterminismSeed:%d"), Seed);
			return false;
		}
		UniqueDeterminismSeeds.Add(Seed);
	}

	const TConstArrayView<int32> M110Seeds = GetM110Seeds();
	if (M110Seeds.Num() != 103)
	{
		OutFailure = TEXT("M110SeedCount");
		return false;
	}
	TSet<int32> UniqueM110Seeds;
	for (const int32 Seed : M110Seeds)
	{
		if (UniqueM110Seeds.Contains(Seed))
		{
			OutFailure = FString::Printf(TEXT("DuplicateM110Seed:%d"), Seed);
			return false;
		}
		UniqueM110Seeds.Add(Seed);
	}

	const TConstArrayView<FABTSM3R0AcceptanceEntry> Entries = GetEntries();
	if (Entries.Num() != 5)
	{
		OutFailure = TEXT("AcceptanceEntryCount");
		return false;
	}

	TSet<FString> UniqueEntryIds;
	for (const FABTSM3R0AcceptanceEntry& Entry : Entries)
	{
		if (Entry.EntryId == nullptr
			|| Entry.Target == nullptr
			|| Entry.EntryId[0] == TEXT('\0')
			|| Entry.Target[0] == TEXT('\0')
			|| Entry.ExpectedCaseCount <= 0
			|| Entry.ExpectedTerminalCount != 1)
		{
			OutFailure = TEXT("InvalidAcceptanceEntry");
			return false;
		}
		if (UniqueEntryIds.Contains(Entry.EntryId))
		{
			OutFailure = FString::Printf(
				TEXT("DuplicateAcceptanceEntry:%s"),
				Entry.EntryId);
			return false;
		}
		UniqueEntryIds.Add(Entry.EntryId);
	}

	const TConstArrayView<FABTSM3R0ExpectedBuildingSite> Sites =
		GetDisplayBuildingSites();
	if (Sites.Num() != 4)
	{
		OutFailure = TEXT("DisplayBuildingSiteCount");
		return false;
	}
	int32 PreviousCellId = INDEX_NONE;
	TSet<int32> UniqueTaskIds;
	for (const FABTSM3R0ExpectedBuildingSite& Site : Sites)
	{
		if (Site.TaskId < 0
			|| Site.CellId <= PreviousCellId
			|| UniqueTaskIds.Contains(Site.TaskId))
		{
			OutFailure = TEXT("InvalidDisplayBuildingSite");
			return false;
		}
		PreviousCellId = Site.CellId;
		UniqueTaskIds.Add(Site.TaskId);
	}

	if (ComputeWeekOneSeedManifestHash() != FrozenWeekOneSeedManifestHash)
	{
		OutFailure = TEXT("WeekOneSeedManifestHashDrift");
		return false;
	}
	if (ComputeM110SeedManifestHash() != FrozenM110SeedManifestHash)
	{
		OutFailure = TEXT("M110SeedManifestHashDrift");
		return false;
	}
	if (ComputeManifestHash() != FrozenManifestHash)
	{
		OutFailure = TEXT("AcceptanceManifestHashDrift");
		return false;
	}
	return true;
}
