// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3R3AcceptanceManifest.h"

#include "Containers/StringConv.h"
#include "PCG/ABTSM3MonthlyEncounter.h"
#include "PCG/ABTSM3R2AcceptanceManifest.h"

namespace ABTSM3R3ManifestPrivate
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
		Result.Reserve(FABTSM3R3AcceptanceManifest::SweepSeedCount);
		Result.Add(FABTSM3R3AcceptanceManifest::DisplaySeed);
		for (int32 Seed = 0; Seed <= 98; ++Seed)
		{
			Result.Add(Seed);
		}
		return Result;
	}();
	return Seeds;
}

const TArray<int32>& GetReferenceSeedStorage()
{
	static const TArray<int32> Seeds = {
		FABTSM3R3AcceptanceManifest::DisplaySeed,
		0,
		1,
		2,
		3,
		4,
		5,
		6,
		7,
		8,
		9
	};
	return Seeds;
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

const FABTSM3R3AcceptanceEntry AcceptanceEntries[] = {
	{
		TEXT("Automation.EncounterSpatial"),
		EABTSM3R3AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.EncounterSpatial.0"),
		8,
		1
	},
	{
		TEXT("Automation.EncounterSpatialFailure"),
		EABTSM3R3AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.EncounterSpatialFailure"),
		2,
		1
	},
	{
		TEXT("Automation.RouteCore"),
		EABTSM3R3AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.RouteCore"),
		7,
		1
	},
	{
		TEXT("Automation.RouteFailure"),
		EABTSM3R3AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.RouteFailure"),
		1,
		1
	},
	{
		TEXT("Automation.MonthlySchema"),
		EABTSM3R3AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.Schema"),
		8,
		1
	},
	{
		TEXT("Automation.WeekOne"),
		EABTSM3R3AcceptanceLayer::Automation,
		TEXT("ABTS.M3.WeekOne"),
		2,
		1
	},
	{
		TEXT("Automation.WorldGenerationContract"),
		EABTSM3R3AcceptanceLayer::Automation,
		TEXT("ABTS.Contracts.WorldGeneration"),
		2,
		1
	},
	{
		TEXT("Automation.M110FinaleSeparation"),
		EABTSM3R3AcceptanceLayer::Automation,
		TEXT("ABTS.M110.TaskGraphFinaleSeparation"),
		1,
		1
	},
	{
		TEXT("Runtime.LABTSM3"),
		EABTSM3R3AcceptanceLayer::FreshRuntime,
		TEXT("/Game/Maps/L_ABTS_M3?-ABTSM3R3Smoke"),
		1,
		1
	}
};
}

TConstArrayView<int32> FABTSM3R3AcceptanceManifest::GetSweepSeeds()
{
	return MakeArrayView(
		ABTSM3R3ManifestPrivate::GetSweepSeedStorage());
}

TConstArrayView<int32> FABTSM3R3AcceptanceManifest::GetReferenceSeeds()
{
	return MakeArrayView(
		ABTSM3R3ManifestPrivate::GetReferenceSeedStorage());
}

TConstArrayView<FABTSM3R3AcceptanceEntry>
FABTSM3R3AcceptanceManifest::GetEntries()
{
	return MakeArrayView(
		ABTSM3R3ManifestPrivate::AcceptanceEntries);
}

const TCHAR* FABTSM3R3AcceptanceManifest::GetLayerName(
	const EABTSM3R3AcceptanceLayer Layer)
{
	switch (Layer)
	{
	case EABTSM3R3AcceptanceLayer::Automation:
		return TEXT("Automation");
	case EABTSM3R3AcceptanceLayer::FreshRuntime:
		return TEXT("FreshRuntime");
	default:
		return TEXT("Invalid");
	}
}

uint64 FABTSM3R3AcceptanceManifest::ComputeSweepSeedManifestHash()
{
	return ABTSM3R3ManifestPrivate::HashUtf8FNV1a64(
		ABTSM3R3ManifestPrivate::BuildCanonicalSeedList(
			GetSweepSeeds()));
}

uint64 FABTSM3R3AcceptanceManifest::ComputeReferenceSeedManifestHash()
{
	return ABTSM3R3ManifestPrivate::HashUtf8FNV1a64(
		ABTSM3R3ManifestPrivate::BuildCanonicalSeedList(
			GetReferenceSeeds()));
}

FString FABTSM3R3AcceptanceManifest::BuildCanonicalPayload()
{
	FString Payload;
	Payload.Reserve(4096);
	Payload.Append(TEXT("M3R3AcceptanceManifest\n"));
	Payload.Append(FString::Printf(
		TEXT("ManifestSchemaVersion=%d\n"),
		ManifestSchemaVersion));
	Payload.Append(FString::Printf(
		TEXT("SpatialSchemaVersion=%d\n"),
		SpatialSchemaVersion));
	Payload.Append(FString::Printf(
		TEXT("CanonicalHashAlgorithmVersion=%d\n"),
		CanonicalHashAlgorithmVersion));
	Payload.Append(FString::Printf(
		TEXT("GeneratorVersion=%d\n"),
		GeneratorVersion));
	Payload.Append(FString::Printf(
		TEXT("MonthlyLayoutPolicyVersion=%d\n"),
		MonthlyLayoutPolicyVersion));
	Payload.Append(FString::Printf(
		TEXT("RequiredR2ManifestHash=%016llX\n"),
		static_cast<unsigned long long>(RequiredR2ManifestHash)));
	Payload.Append(FString::Printf(
		TEXT("SweepSeedManifestHash=%016llX\n"),
		static_cast<unsigned long long>(
			ComputeSweepSeedManifestHash())));
	Payload.Append(TEXT("SweepSeeds="));
	Payload.Append(
		ABTSM3R3ManifestPrivate::BuildCanonicalSeedList(
			GetSweepSeeds()));
	Payload.AppendChar(TEXT('\n'));
	Payload.Append(FString::Printf(
		TEXT("ReferenceSeedManifestHash=%016llX\n"),
		static_cast<unsigned long long>(
			ComputeReferenceSeedManifestHash())));
	Payload.Append(TEXT("ReferenceSeeds="));
	Payload.Append(
		ABTSM3R3ManifestPrivate::BuildCanonicalSeedList(
			GetReferenceSeeds()));
	Payload.AppendChar(TEXT('\n'));
	Payload.Append(FString::Printf(
		TEXT("FixtureProfileCatalogHash=%016llX\n"),
		static_cast<unsigned long long>(
			FrozenFixtureProfileCatalogHash)));
	Payload.Append(FString::Printf(
		TEXT("Budgets=%d|%d|%d\n"),
		MaxOptimizedPVSRaysPerWorld,
		EncounterSpatialP95BudgetMS,
		EncounterSpatialMaxBudgetMS));
	Payload.Append(FString::Printf(
		TEXT("SweepOracleHash=%016llX\n"),
		static_cast<unsigned long long>(FrozenSweepOracleHash)));
	Payload.Append(FString::Printf(
		TEXT("ReferencePVSOracleHash=%016llX\n"),
		static_cast<unsigned long long>(
			FrozenReferencePVSOracleHash)));
	Payload.Append(FString::Printf(
		TEXT("ReferenceBoundaryOracleHash=%016llX\n"),
		static_cast<unsigned long long>(
			FrozenReferenceBoundaryOracleHash)));
	Payload.Append(FString::Printf(
		TEXT("DisplayIdentity=%d|%016llX|%016llX|%016llX|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d\n"),
		DisplaySeed,
		static_cast<unsigned long long>(FrozenDisplayResultHash),
		static_cast<unsigned long long>(FrozenDisplaySnapshotHash),
		static_cast<unsigned long long>(FrozenDisplayCandidateHash),
		DisplayAttemptedRouteCandidates,
		DisplaySpatialHardPassCount,
		DisplayRetainedCandidates,
		DisplayRecomputedRouteLengthCM,
		DisplayEncounterCount,
		DisplayPocketCount,
		DisplayBiomeDistrictCount,
		DisplayPlayableCellCount,
		DisplayApprovedTransitionCellCount,
		DisplayActiveCoveragePermille,
		DisplayDeepWildPermille,
		DisplayOptimizedPVSRays));
	Payload.Append(FString::Printf(
		TEXT("FailureIdentity=%016llX|%016llX|%016llX|%016llX|%016llX|%016llX\n"),
		static_cast<unsigned long long>(FrozenBlockedRoadResultHash),
		static_cast<unsigned long long>(
			FrozenBlockedRoadSnapshotHash),
		static_cast<unsigned long long>(FrozenInvalidPVSResultHash),
		static_cast<unsigned long long>(FrozenInvalidPVSSnapshotHash),
		static_cast<unsigned long long>(FrozenRayBudgetResultHash),
		static_cast<unsigned long long>(
			FrozenRayBudgetSnapshotHash)));
	for (const FABTSM3R3AcceptanceEntry& Entry : GetEntries())
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

uint64 FABTSM3R3AcceptanceManifest::ComputeManifestHash()
{
	return ABTSM3R3ManifestPrivate::HashUtf8FNV1a64(
		BuildCanonicalPayload());
}

bool FABTSM3R3AcceptanceManifest::Validate(FString& OutFailure)
{
	OutFailure.Reset();
	FString R2Failure;
	if (!FABTSM3R2AcceptanceManifest::Validate(R2Failure)
		|| FABTSM3R2AcceptanceManifest::ComputeManifestHash()
			!= RequiredR2ManifestHash)
	{
		OutFailure = FString::Printf(
			TEXT("RequiredR2Manifest:%s"),
			*R2Failure);
		return false;
	}
	if (GetSweepSeeds().Num() != SweepSeedCount
		|| GetReferenceSeeds().Num() != ReferenceSeedCount
		|| GetSweepSeeds()[0] != DisplaySeed
		|| GetReferenceSeeds()[0] != DisplaySeed)
	{
		OutFailure = TEXT("SeedCountOrDisplaySeed");
		return false;
	}
	TSet<int32> UniqueSweepSeeds;
	for (const int32 Seed : GetSweepSeeds())
	{
		if (UniqueSweepSeeds.Contains(Seed))
		{
			OutFailure = FString::Printf(
				TEXT("DuplicateSweepSeed:%d"),
				Seed);
			return false;
		}
		UniqueSweepSeeds.Add(Seed);
	}
	TSet<int32> UniqueReferenceSeeds;
	for (const int32 Seed : GetReferenceSeeds())
	{
		if (UniqueReferenceSeeds.Contains(Seed))
		{
			OutFailure = FString::Printf(
				TEXT("DuplicateReferenceSeed:%d"),
				Seed);
			return false;
		}
		UniqueReferenceSeeds.Add(Seed);
	}
	if (GetEntries().Num() != 9)
	{
		OutFailure = TEXT("AcceptanceEntryCount");
		return false;
	}
	TSet<FString> UniqueEntryIds;
	for (const FABTSM3R3AcceptanceEntry& Entry : GetEntries())
	{
		if (Entry.EntryId == nullptr
			|| Entry.Target == nullptr
			|| Entry.EntryId[0] == TEXT('\0')
			|| Entry.Target[0] == TEXT('\0')
			|| Entry.ExpectedCaseCount <= 0
			|| Entry.ExpectedTerminalCount != 1
			|| UniqueEntryIds.Contains(Entry.EntryId))
		{
			OutFailure = TEXT("AcceptanceEntry");
			return false;
		}
		UniqueEntryIds.Add(Entry.EntryId);
	}
	if (ComputeSweepSeedManifestHash()
			!= FrozenSweepSeedManifestHash
		|| ComputeReferenceSeedManifestHash()
			!= FrozenReferenceSeedManifestHash)
	{
		OutFailure = TEXT("SeedManifestHashDrift");
		return false;
	}
	if (FrozenFixtureProfileCatalogHash == 0
		|| FABTSM3MonthlyEncounterBuilder::
				ComputeFixtureProfileCatalogHash()
			!= FrozenFixtureProfileCatalogHash)
	{
		OutFailure = TEXT("FixtureProfileCatalogHashDrift");
		return false;
	}
	if (FrozenSweepOracleHash == 0
		|| FrozenReferencePVSOracleHash == 0
		|| FrozenReferenceBoundaryOracleHash == 0
		|| FrozenDisplayResultHash == 0
		|| FrozenDisplaySnapshotHash == 0
		|| FrozenDisplayCandidateHash == 0
		|| FrozenBlockedRoadResultHash == 0
		|| FrozenBlockedRoadSnapshotHash == 0
		|| FrozenInvalidPVSResultHash == 0
		|| FrozenInvalidPVSSnapshotHash == 0
		|| FrozenRayBudgetResultHash == 0
		|| FrozenRayBudgetSnapshotHash == 0)
	{
		OutFailure = TEXT("SpatialIdentityNotFrozen");
		return false;
	}
	if (FrozenManifestHash == 0
		|| ComputeManifestHash() != FrozenManifestHash)
	{
		OutFailure = TEXT("AcceptanceManifestHashDrift");
		return false;
	}
	return true;
}
