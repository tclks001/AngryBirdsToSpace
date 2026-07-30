// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3R4AcceptanceManifest.h"

#include "Containers/StringConv.h"
#include "PCG/ABTSM3MonthlyWitness.h"
#include "PCG/ABTSM3R31AcceptanceManifest.h"

namespace ABTSM3R4ManifestPrivate
{
constexpr uint64 FnvOffset = 14695981039346656037ull;
constexpr uint64 FnvPrime = 1099511628211ull;

uint64 HashUtf8(const FString& Text)
{
	const FTCHARToUTF8 Utf8(*Text);
	uint64 Hash = FnvOffset;
	for (int32 Index = 0; Index < Utf8.Length(); ++Index)
	{
		Hash ^= static_cast<uint8>(Utf8.Get()[Index]);
		Hash *= FnvPrime;
	}
	return Hash;
}

const TArray<int32>& GetSweepSeedStorage()
{
	static const TArray<int32> Seeds = []
	{
		TArray<int32> Result;
		Result.Reserve(FABTSM3R4AcceptanceManifest::SweepSeedCount);
		Result.Add(FABTSM3R4AcceptanceManifest::DisplaySeed);
		for (int32 Seed = 0; Seed <= 98; ++Seed)
		{
			Result.Add(Seed);
		}
		return Result;
	}();
	return Seeds;
}

FString BuildSeedList(const TConstArrayView<int32> Seeds)
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

const FABTSM3R4AcceptanceEntry AcceptanceEntries[] = {
	{
		TEXT("Automation.EncounterWitness"),
		EABTSM3R4AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.EncounterWitness.0"),
		8,
		1
	},
	{
		TEXT("Automation.EncounterWitnessFailure"),
		EABTSM3R4AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.EncounterWitnessFailure"),
		8,
		1
	},
	{
		TEXT("Automation.SlotField"),
		EABTSM3R4AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.SlotField.0"),
		7,
		1
	},
	{
		TEXT("Automation.SlotFieldFailure"),
		EABTSM3R4AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.SlotFieldFailure"),
		2,
		1
	},
	{
		TEXT("Automation.EncounterSpatial"),
		EABTSM3R4AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.EncounterSpatial.0"),
		8,
		1
	},
	{
		TEXT("Automation.EncounterSpatialFailure"),
		EABTSM3R4AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.EncounterSpatialFailure"),
		2,
		1
	},
	{
		TEXT("Automation.RouteCore"),
		EABTSM3R4AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.RouteCore"),
		7,
		1
	},
	{
		TEXT("Automation.RouteFailure"),
		EABTSM3R4AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.RouteFailure"),
		1,
		1
	},
	{
		TEXT("Automation.MonthlySchema"),
		EABTSM3R4AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.Schema"),
		8,
		1
	},
	{
		TEXT("Automation.WeekOne"),
		EABTSM3R4AcceptanceLayer::Automation,
		TEXT("ABTS.M3.WeekOne"),
		2,
		1
	},
	{
		TEXT("Automation.WorldGenerationContract"),
		EABTSM3R4AcceptanceLayer::Automation,
		TEXT("ABTS.Contracts.WorldGeneration"),
		2,
		1
	},
	{
		TEXT("Automation.M110FinaleSeparation"),
		EABTSM3R4AcceptanceLayer::Automation,
		TEXT("ABTS.M110.TaskGraphFinaleSeparation"),
		1,
		1
	},
	{
		TEXT("Runtime.LABTSM3"),
		EABTSM3R4AcceptanceLayer::FreshRuntime,
		TEXT("/Game/Maps/L_ABTS_M3?-ABTSM3R4Smoke"),
		1,
		1
	},
	{
		TEXT("Integration.M51SlingshotGeometry"),
		EABTSM3R4AcceptanceLayer::IntegrationAutomation,
		TEXT("ABTS.M51.SlingshotAssembly.Geometry"),
		1,
		1
	},
	{
		TEXT("Integration.M51SlingshotRuntime"),
		EABTSM3R4AcceptanceLayer::IntegrationAutomation,
		TEXT("ABTS.M51.SlingshotAssembly.Runtime"),
		1,
		1
	},
	{
		TEXT("Integration.M6TrajectoryProvider"),
		EABTSM3R4AcceptanceLayer::IntegrationAutomation,
		TEXT("ABTS.M6.MonthlyWitness.Provider"),
		1,
		1
	},
	{
		TEXT("Integration.M9GravitySnapshot"),
		EABTSM3R4AcceptanceLayer::IntegrationAutomation,
		TEXT("ABTS.M9.MonthlyWitness.Gravity"),
		1,
		1
	},
	{
		TEXT("Integration.M7ProfileCatalog"),
		EABTSM3R4AcceptanceLayer::IntegrationAutomation,
		TEXT("ABTS.M7.MonthlyWitness.ProfileCatalog"),
		1,
		1
	},
	{
		TEXT("Integration.ProgressionBridge"),
		EABTSM3R4AcceptanceLayer::IntegrationAutomation,
		TEXT("ABTS.M3.MonthlyWitness.ProgressionBridge"),
		1,
		1
	},
	{
		TEXT("Integration.M3R6BuildingContract"),
		EABTSM3R4AcceptanceLayer::IntegrationAutomation,
		TEXT("ABTS.M3.Monthly.R6BuildingContract"),
		1,
		1
	},
	{
		TEXT("VisiblePIE.CanonicalM10"),
		EABTSM3R4AcceptanceLayer::VisiblePIE,
		TEXT("/Game/Maps/L_ABTS_M10"),
		3,
		1
	}
};
}

TConstArrayView<int32> FABTSM3R4AcceptanceManifest::GetSweepSeeds()
{
	return MakeArrayView(
		ABTSM3R4ManifestPrivate::GetSweepSeedStorage());
}

TConstArrayView<FABTSM3R4AcceptanceEntry>
FABTSM3R4AcceptanceManifest::GetEntries()
{
	return MakeArrayView(
		ABTSM3R4ManifestPrivate::AcceptanceEntries);
}

const TCHAR* FABTSM3R4AcceptanceManifest::GetLayerName(
	const EABTSM3R4AcceptanceLayer Layer)
{
	switch (Layer)
	{
	case EABTSM3R4AcceptanceLayer::Automation:
		return TEXT("Automation");
	case EABTSM3R4AcceptanceLayer::FreshRuntime:
		return TEXT("FreshRuntime");
	case EABTSM3R4AcceptanceLayer::IntegrationAutomation:
		return TEXT("IntegrationAutomation");
	case EABTSM3R4AcceptanceLayer::VisiblePIE:
		return TEXT("VisiblePIE");
	default:
		return TEXT("Unknown");
	}
}

uint64 FABTSM3R4AcceptanceManifest::ComputeSweepSeedManifestHash()
{
	return ABTSM3R4ManifestPrivate::HashUtf8(
		ABTSM3R4ManifestPrivate::BuildSeedList(GetSweepSeeds()));
}

FString FABTSM3R4AcceptanceManifest::BuildCanonicalPayload()
{
	FString Payload;
	Payload.Append(TEXT("M3R4AcceptanceManifest\n"));
	Payload.Appendf(
		TEXT("ManifestSchema=%d\nWitnessSchema=%d\nGenerator=%d\nMonthlyPolicy=%d\n"),
		ManifestSchemaVersion,
		WitnessSchemaVersion,
		GeneratorVersion,
		MonthlyLayoutPolicyVersion);
	Payload.Appendf(
		TEXT("DisplaySeed=%d\nSweepSeeds=%s\n"),
		DisplaySeed,
		*ABTSM3R4ManifestPrivate::BuildSeedList(GetSweepSeeds()));
	Payload.Appendf(
		TEXT("RequiredR31=%016llX\nSeedManifest=%016llX\n"),
		static_cast<unsigned long long>(RequiredR31ManifestHash),
		static_cast<unsigned long long>(
			FrozenSweepSeedManifestHash));
	Payload.Appendf(
		TEXT("Defaults=Budget:%d,Pull:%d,AimAxis:%d\n"),
		DefaultEvaluationBudget,
		DefaultPullSamples,
		DefaultAimAxisSamples);
	Payload.Appendf(
		TEXT("Display=Config:%016llX,Result:%016llX,Candidate:%016llX,GameplayLayout:%016llX\n"),
		static_cast<unsigned long long>(FrozenDisplayConfigHash),
		static_cast<unsigned long long>(FrozenDisplayResultHash),
		static_cast<unsigned long long>(
			FrozenDisplayCandidateHash),
		static_cast<unsigned long long>(
			FrozenDisplayGameplayLayoutHash));
	Payload.Appendf(
		TEXT("SweepOracle=%016llX\n"),
		static_cast<unsigned long long>(FrozenSweepOracleHash));
	Payload.Append(
		TEXT("Boundary=M3LocalAccepted:FixtureAuthority;IntegrationPending:M5.1+M6+M9+M7+ProgressionBridge+R6+PIE;ExternalCertified=0;MonthlyAccepted=0\n"));
	for (const FABTSM3R4AcceptanceEntry& Entry : GetEntries())
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

uint64 FABTSM3R4AcceptanceManifest::ComputeManifestHash()
{
	return ABTSM3R4ManifestPrivate::HashUtf8(
		BuildCanonicalPayload());
}

bool FABTSM3R4AcceptanceManifest::Validate(FString& OutFailure)
{
	OutFailure.Reset();
	FString ParentFailure;
	if (!FABTSM3R31AcceptanceManifest::Validate(ParentFailure)
		|| FABTSM3R31AcceptanceManifest::ComputeManifestHash()
			!= RequiredR31ManifestHash)
	{
		OutFailure = FString::Printf(
			TEXT("RequiredR31:%s"),
			*ParentFailure);
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
	const FABTSM3MonthlyWitnessConfig Defaults;
	if (Defaults.bBuildGameplayFinalize
		|| Defaults.MaxWitnessEvaluationsPerEncounter
			!= DefaultEvaluationBudget
		|| Defaults.PullAlphaSampleCount != DefaultPullSamples
		|| Defaults.AimAxisSampleCount != DefaultAimAxisSamples
		|| FABTSM3MonthlyWitnessBuilder::SchemaVersion
			!= WitnessSchemaVersion
		|| FABTSM3MonthlyWitnessBuilder::GeneratorVersion
			!= GeneratorVersion
		|| FABTSM3MonthlyWitnessBuilder::
				MonthlyLayoutPolicyVersion
			!= MonthlyLayoutPolicyVersion)
	{
		OutFailure = TEXT("DefaultContract");
		return false;
	}
	TSet<FString> EntryIds;
	TSet<FString> Targets;
	if (GetEntries().Num() != 21)
	{
		OutFailure = TEXT("AcceptanceEntryCount");
		return false;
	}
	for (const FABTSM3R4AcceptanceEntry& Entry : GetEntries())
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
		|| FrozenDisplayGameplayLayoutHash == 0
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
