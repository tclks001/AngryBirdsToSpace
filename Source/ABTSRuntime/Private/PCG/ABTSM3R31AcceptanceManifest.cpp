// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3R31AcceptanceManifest.h"

#include "Containers/StringConv.h"
#include "PCG/ABTSM3MonthlySlingshotField.h"
#include "PCG/ABTSM3R3AcceptanceManifest.h"

namespace ABTSM3R31ManifestPrivate
{
constexpr uint64 Fnv1a64OffsetBasis = 14695981039346656037ull;
constexpr uint64 Fnv1a64Prime = 1099511628211ull;

uint64 HashUtf8FNV1a64(const FString& Text)
{
	const FTCHARToUTF8 Utf8(*Text);
	uint64 Hash = Fnv1a64OffsetBasis;
	for (int32 Index = 0; Index < Utf8.Length(); ++Index)
	{
		Hash ^= static_cast<uint8>(Utf8.Get()[Index]);
		Hash *= Fnv1a64Prime;
	}
	return Hash;
}

const TArray<int32>& GetSweepSeedStorage()
{
	static const TArray<int32> Seeds = []
	{
		TArray<int32> Result;
		Result.Reserve(
			FABTSM3R31AcceptanceManifest::SweepSeedCount);
		Result.Add(
			FABTSM3R31AcceptanceManifest::DisplaySeed);
		for (int32 Seed = 0; Seed <= 98; ++Seed)
		{
			Result.Add(Seed);
		}
		return Result;
	}();
	return Seeds;
}

FString BuildCanonicalSeedList(
	const TConstArrayView<int32> Seeds)
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

const FABTSM3R31AcceptanceEntry AcceptanceEntries[] = {
	{
		TEXT("Automation.SlotField"),
		EABTSM3R31AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.SlotField.0"),
		7,
		1
	},
	{
		TEXT("Automation.SlotFieldFailure"),
		EABTSM3R31AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.SlotFieldFailure"),
		2,
		1
	},
	{
		TEXT("Automation.EncounterSpatial"),
		EABTSM3R31AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.EncounterSpatial.0"),
		8,
		1
	},
	{
		TEXT("Automation.EncounterSpatialFailure"),
		EABTSM3R31AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.EncounterSpatialFailure"),
		2,
		1
	},
	{
		TEXT("Automation.RouteCore"),
		EABTSM3R31AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.RouteCore"),
		7,
		1
	},
	{
		TEXT("Automation.RouteFailure"),
		EABTSM3R31AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.RouteFailure"),
		1,
		1
	},
	{
		TEXT("Automation.MonthlySchema"),
		EABTSM3R31AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.Schema"),
		8,
		1
	},
	{
		TEXT("Automation.WeekOne"),
		EABTSM3R31AcceptanceLayer::Automation,
		TEXT("ABTS.M3.WeekOne"),
		2,
		1
	},
	{
		TEXT("Automation.WorldGenerationContract"),
		EABTSM3R31AcceptanceLayer::Automation,
		TEXT("ABTS.Contracts.WorldGeneration"),
		2,
		1
	},
	{
		TEXT("Automation.M110FinaleSeparation"),
		EABTSM3R31AcceptanceLayer::Automation,
		TEXT("ABTS.M110.TaskGraphFinaleSeparation"),
		1,
		1
	},
	{
		TEXT("Runtime.LABTSM3"),
		EABTSM3R31AcceptanceLayer::FreshRuntime,
		TEXT("/Game/Maps/L_ABTS_M3?-ABTSM3R31Smoke"),
		1,
		1
	},
	{
		TEXT("Integration.CordGeometry"),
		EABTSM3R31AcceptanceLayer::IntegrationAutomation,
		TEXT("ABTS.M51.SlingshotAssembly.Geometry"),
		1,
		1
	},
	{
		TEXT("Integration.CordRuntime"),
		EABTSM3R31AcceptanceLayer::IntegrationAutomation,
		TEXT("ABTS.M51.SlingshotAssembly.Runtime"),
		1,
		1
	},
	{
		TEXT("VisiblePIE.CanonicalM10"),
		EABTSM3R31AcceptanceLayer::VisiblePIE,
		TEXT("/Game/Maps/L_ABTS_M10"),
		1,
		1
	}
};
}

TConstArrayView<int32>
FABTSM3R31AcceptanceManifest::GetSweepSeeds()
{
	return MakeArrayView(
		ABTSM3R31ManifestPrivate::GetSweepSeedStorage());
}

TConstArrayView<FABTSM3R31AcceptanceEntry>
FABTSM3R31AcceptanceManifest::GetEntries()
{
	return MakeArrayView(
		ABTSM3R31ManifestPrivate::AcceptanceEntries);
}

const TCHAR* FABTSM3R31AcceptanceManifest::GetLayerName(
	const EABTSM3R31AcceptanceLayer Layer)
{
	switch (Layer)
	{
	case EABTSM3R31AcceptanceLayer::Automation:
		return TEXT("Automation");
	case EABTSM3R31AcceptanceLayer::FreshRuntime:
		return TEXT("FreshRuntime");
	case EABTSM3R31AcceptanceLayer::IntegrationAutomation:
		return TEXT("IntegrationAutomation");
	case EABTSM3R31AcceptanceLayer::VisiblePIE:
		return TEXT("VisiblePIE");
	default:
		return TEXT("Unknown");
	}
}

uint64 FABTSM3R31AcceptanceManifest::
	ComputeSweepSeedManifestHash()
{
	return ABTSM3R31ManifestPrivate::HashUtf8FNV1a64(
		ABTSM3R31ManifestPrivate::BuildCanonicalSeedList(
			GetSweepSeeds()));
}

FString FABTSM3R31AcceptanceManifest::BuildCanonicalPayload()
{
	FString Payload;
	Payload.Append(TEXT("M3R31AcceptanceManifest\n"));
	Payload.Appendf(
		TEXT("ManifestSchema=%d\nSlotFieldSchema=%d\nGenerator=%d\nMonthlyPolicy=%d\n"),
		ManifestSchemaVersion,
		SlotFieldSchemaVersion,
		GeneratorVersion,
		MonthlyLayoutPolicyVersion);
	Payload.Appendf(
		TEXT("DisplaySeed=%d\nSweepSeeds=%s\n"),
		DisplaySeed,
		*ABTSM3R31ManifestPrivate::BuildCanonicalSeedList(
			GetSweepSeeds()));
	Payload.Appendf(
		TEXT("RequiredR3=%016llX\nSeedManifest=%016llX\n"),
		static_cast<unsigned long long>(
			RequiredR3ManifestHash),
		static_cast<unsigned long long>(
			FrozenSweepSeedManifestHash));
	Payload.Appendf(
		TEXT("Defaults=ExtraSlots:%d,RoadFields:%d,MaxCordCM:%d\n"),
		DefaultAdditionalSlotsPerField,
		DefaultAdditionalRoadFieldCount,
		DefaultMaxCordLengthCM);
	Payload.Appendf(
		TEXT("Display=Config:%016llX,Result:%016llX,Candidate:%016llX,Fields:%d,Slots:%d\n"),
		static_cast<unsigned long long>(
			FrozenDisplayConfigHash),
		static_cast<unsigned long long>(
			FrozenDisplayResultHash),
		static_cast<unsigned long long>(
			FrozenDisplayCandidateHash),
		DisplayFieldsPerCandidate,
		DisplaySlotsPerCandidate);
	Payload.Appendf(
		TEXT("SweepOracle=%016llX\n"),
		static_cast<unsigned long long>(
			FrozenSweepOracleHash));
	Payload.Append(
		TEXT("Boundary=M3LocalAccepted:Data;IntegrationAccepted:Holes+CordLength+StakeCordObstruction;FinalePairExcluded\n"));
	for (const FABTSM3R31AcceptanceEntry& Entry :
		GetEntries())
	{
		Payload.Appendf(
			TEXT("%s|%s|%s|Cases=%d|Terminal=%d\n"),
			Entry.EntryId,
			GetLayerName(Entry.Layer),
			Entry.Target,
			Entry.ExpectedCaseCount,
			Entry.ExpectedTerminalCount);
	}
	return Payload;
}

uint64 FABTSM3R31AcceptanceManifest::ComputeManifestHash()
{
	return ABTSM3R31ManifestPrivate::HashUtf8FNV1a64(
		BuildCanonicalPayload());
}

bool FABTSM3R31AcceptanceManifest::Validate(
	FString& OutFailure)
{
	OutFailure.Reset();
	FString R3Failure;
	if (!FABTSM3R3AcceptanceManifest::Validate(R3Failure)
		|| FABTSM3R3AcceptanceManifest::ComputeManifestHash()
			!= RequiredR3ManifestHash)
	{
		OutFailure = FString::Printf(
			TEXT("RequiredR3:%s"),
			*R3Failure);
		return false;
	}
	if (ComputeSweepSeedManifestHash()
			!= FrozenSweepSeedManifestHash
		|| GetSweepSeeds().Num() != SweepSeedCount
		|| GetSweepSeeds()[0] != DisplaySeed)
	{
		OutFailure = TEXT("SweepSeedManifest");
		return false;
	}
	const FABTSM3MonthlySlingshotFieldConfig Defaults;
	if (Defaults.AdditionalSlotsPerOrdinaryField
			!= DefaultAdditionalSlotsPerField
		|| Defaults.AdditionalRoadFieldCount
			!= DefaultAdditionalRoadFieldCount
		|| Defaults.MaxCordLengthCM
			!= DefaultMaxCordLengthCM
		|| FABTSM3MonthlySlingshotFieldBuilder::
				SchemaVersion
			!= SlotFieldSchemaVersion
		|| FABTSM3MonthlySlingshotFieldBuilder::
				GeneratorVersion
			!= GeneratorVersion
		|| FABTSM3MonthlySlingshotFieldBuilder::
				MonthlyLayoutPolicyVersion
			!= MonthlyLayoutPolicyVersion)
	{
		OutFailure = TEXT("DefaultContract");
		return false;
	}
	TSet<FString> EntryIds;
	TSet<FString> Targets;
	for (const FABTSM3R31AcceptanceEntry& Entry :
		GetEntries())
	{
		if (FCString::Strlen(Entry.EntryId) == 0
			|| FCString::Strlen(Entry.Target) == 0
			|| Entry.ExpectedCaseCount <= 0
			|| Entry.ExpectedTerminalCount != 1
			|| EntryIds.Contains(Entry.EntryId)
			|| Targets.Contains(Entry.Target))
		{
			OutFailure = TEXT("AcceptanceEntry");
			return false;
		}
		EntryIds.Add(Entry.EntryId);
		Targets.Add(Entry.Target);
	}
	if (FrozenDisplayConfigHash == 0
		|| FrozenDisplayResultHash == 0
		|| FrozenDisplayCandidateHash == 0
		|| FrozenSweepOracleHash == 0)
	{
		OutFailure = TEXT("FrozenIdentity");
		return false;
	}
	if (ComputeManifestHash() != FrozenManifestHash)
	{
		OutFailure = FString::Printf(
			TEXT("ManifestHash:%016llX"),
			static_cast<unsigned long long>(
				ComputeManifestHash()));
		return false;
	}
	return true;
}
