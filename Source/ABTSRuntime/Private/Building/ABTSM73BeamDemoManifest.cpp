// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BeamDemoManifest.h"

#include "Misc/Crc.h"

namespace ABTSM73BeamDemoManifestPrivate
{
	FABTSM73BeamDemoManifestEntry MakeDemoEntry(
		const EABTSM73BeamDemoBuilding Id,
		const TCHAR* StableId,
		const TCHAR* ProfileId,
		const int32 Tier,
		const int32 Seed)
	{
		FABTSM73BeamDemoManifestEntry Entry;
		Entry.Id = Id;
		Entry.StableId = FName(StableId);
		Entry.Settings.GameplayProfileId = FName(ProfileId);
		Entry.Settings.DifficultyTier = Tier;
		Entry.Settings.BuildingSeed = Seed;
		return Entry;
	}
}

const TArray<FABTSM73BeamDemoManifestEntry>&
FABTSM73BeamDemoManifest::GetEntries()
{
	using namespace ABTSM73BeamDemoManifestPrivate;
	static const TArray<FABTSM73BeamDemoManifestEntry> Entries = {
		MakeDemoEntry(EABTSM73BeamDemoBuilding::E1ColumnBreak,
			TEXT("DemoE1ColumnBreak"), TEXT("ColumnBreak"), 0, 710000),
		MakeDemoEntry(EABTSM73BeamDemoBuilding::E2DropTrigger,
			TEXT("DemoE2DropTrigger"), TEXT("DropTrigger"), 1, 740000),
		MakeDemoEntry(EABTSM73BeamDemoBuilding::E3SlideRelease,
			TEXT("DemoE3SlideRelease"), TEXT("SlideRelease"), 2, 750137),
		MakeDemoEntry(EABTSM73BeamDemoBuilding::E4TipOver,
			TEXT("DemoE4TipOver"), TEXT("TipOver"), 3, 730000),
		MakeDemoEntry(EABTSM73BeamDemoBuilding::E5SeamRelease,
			TEXT("DemoE5SeamRelease"), TEXT("SeamRelease"), 4, 720000),
		MakeDemoEntry(EABTSM73BeamDemoBuilding::E6TipOver,
			TEXT("DemoE6TipOver"), TEXT("TipOver"), 5, 750000)};
	return Entries;
}

bool FABTSM73BeamDemoManifest::Resolve(
	const EABTSM73BeamDemoBuilding Id,
	FABTSM73BeamDemoManifestEntry& OutEntry,
	FString& OutError)
{
	OutEntry = FABTSM73BeamDemoManifestEntry();
	OutError.Reset();
	if (Id == EABTSM73BeamDemoBuilding::Custom)
	{
		OutError = TEXT("BeamDemoManifestCustomHasNoFrozenEntry");
		return false;
	}
	for (const FABTSM73BeamDemoManifestEntry& Entry : GetEntries())
	{
		if (Entry.Id == Id)
		{
			OutEntry = Entry;
			return true;
		}
	}
	OutError = FString::Printf(
		TEXT("BeamDemoManifestUnknownEntry:Id=%d:Version=%d"),
		static_cast<int32>(Id), Version);
	return false;
}

int64 FABTSM73BeamDemoManifest::CalculateHash()
{
	FString Canonical = FString::Printf(TEXT("BeamDemoManifestVersion=%d"), Version);
	for (const FABTSM73BeamDemoManifestEntry& Entry : GetEntries())
	{
		Canonical += FString::Printf(
			TEXT("|%d:%s:%s:E%d:%d"), static_cast<int32>(Entry.Id),
			*Entry.StableId.ToString(), *Entry.Settings.GameplayProfileId.ToString(),
			Entry.Settings.DifficultyTier + 1, Entry.Settings.BuildingSeed);
	}
	return static_cast<int64>(FCrc::StrCrc32(*Canonical));
}
