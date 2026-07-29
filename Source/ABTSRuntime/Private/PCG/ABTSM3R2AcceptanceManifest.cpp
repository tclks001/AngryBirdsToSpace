// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3R2AcceptanceManifest.h"

#include "Containers/StringConv.h"
#include "PCG/ABTSM3MonthlyRoute.h"
#include "PCG/ABTSM3R1AcceptanceManifest.h"

namespace ABTSM3R2ManifestPrivate
{
constexpr uint64 Fnv1a64OffsetBasis = 14695981039346656037ull;
constexpr uint64 Fnv1a64Prime = 1099511628211ull;

const TArray<int32>& GetSweepSeedStorage()
{
	static const TArray<int32> Seeds = []
	{
		TArray<int32> Result;
		Result.Reserve(200);
		Result.Add(FABTSM3R2AcceptanceManifest::DisplaySeed);
		for (int32 Seed = 0; Seed <= 198; ++Seed)
		{
			Result.Add(Seed);
		}
		return Result;
	}();
	return Seeds;
}

const FABTSM3R2AcceptanceEntry AcceptanceEntries[] = {
	{
		TEXT("Automation.RouteCore"),
		EABTSM3R2AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.RouteCore"),
		7,
		1
	},
	{
		TEXT("Automation.RouteFailure"),
		EABTSM3R2AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.RouteFailure"),
		1,
		1
	},
	{
		TEXT("Automation.MonthlySchema"),
		EABTSM3R2AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.Schema"),
		8,
		1
	},
	{
		TEXT("Automation.WeekOne"),
		EABTSM3R2AcceptanceLayer::Automation,
		TEXT("ABTS.M3.WeekOne"),
		2,
		1
	},
	{
		TEXT("Automation.WorldGenerationContract"),
		EABTSM3R2AcceptanceLayer::Automation,
		TEXT("ABTS.Contracts.WorldGeneration"),
		2,
		1
	},
	{
		TEXT("Automation.M110FinaleSeparation"),
		EABTSM3R2AcceptanceLayer::Automation,
		TEXT("ABTS.M110.TaskGraphFinaleSeparation"),
		1,
		1
	},
	{
		TEXT("Runtime.LABTSM3"),
		EABTSM3R2AcceptanceLayer::FreshRuntime,
		TEXT("/Game/Maps/L_ABTS_M3?-ABTSM3R2Smoke"),
		1,
		1
	}
};

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

FString BuildCanonicalAcceptanceProfile()
{
	const FABTSM3MonthlyAcceptanceProfileV1 Profile;
	return FString::Printf(
		TEXT("%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d"),
		Profile.MinRouteLengthCM,
		Profile.TargetRouteLengthCM,
		Profile.MaxRouteLengthCM,
		Profile.BendSampleSpacingCM,
		Profile.BendWindowCM,
		Profile.MinBendAngleDegrees,
		Profile.MinBendSeparationCM,
		Profile.StraightTurnThresholdDegrees,
		Profile.MaxStraightCM,
		Profile.SelfApproachIgnoreAlongRouteCM,
		Profile.MinSelfApproachCells,
		Profile.MinScenicBendCount);
}
}

TConstArrayView<int32> FABTSM3R2AcceptanceManifest::GetSweepSeeds()
{
	return MakeArrayView(ABTSM3R2ManifestPrivate::GetSweepSeedStorage());
}

TConstArrayView<FABTSM3R2AcceptanceEntry>
FABTSM3R2AcceptanceManifest::GetEntries()
{
	return MakeArrayView(ABTSM3R2ManifestPrivate::AcceptanceEntries);
}

const TCHAR* FABTSM3R2AcceptanceManifest::GetLayerName(
	const EABTSM3R2AcceptanceLayer Layer)
{
	switch (Layer)
	{
	case EABTSM3R2AcceptanceLayer::Automation:
		return TEXT("Automation");
	case EABTSM3R2AcceptanceLayer::FreshRuntime:
		return TEXT("FreshRuntime");
	default:
		return TEXT("Invalid");
	}
}

uint64 FABTSM3R2AcceptanceManifest::ComputeSweepSeedManifestHash()
{
	return ABTSM3R2ManifestPrivate::HashUtf8FNV1a64(
		ABTSM3R2ManifestPrivate::BuildCanonicalSeedList(
			GetSweepSeeds()));
}

uint64 FABTSM3R2AcceptanceManifest::ComputeAcceptanceProfileHash()
{
	return ABTSM3R2ManifestPrivate::HashUtf8FNV1a64(
		ABTSM3R2ManifestPrivate::BuildCanonicalAcceptanceProfile());
}

FString FABTSM3R2AcceptanceManifest::BuildCanonicalPayload()
{
	FString Payload;
	Payload.Reserve(4096);
	Payload.Append(TEXT("M3R2AcceptanceManifest\n"));
	Payload.Append(FString::Printf(
		TEXT("ManifestSchemaVersion=%d\n"),
		ManifestSchemaVersion));
	Payload.Append(FString::Printf(
		TEXT("RoutePoolSchemaVersion=%d\n"),
		RoutePoolSchemaVersion));
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
		TEXT("RequiredR1ManifestHash=%016llX\n"),
		static_cast<unsigned long long>(RequiredR1ManifestHash)));
	Payload.Append(FString::Printf(
		TEXT("SweepSeedManifestHash=%016llX\n"),
		static_cast<unsigned long long>(
			ComputeSweepSeedManifestHash())));
	Payload.Append(TEXT("SweepSeeds="));
	Payload.Append(ABTSM3R2ManifestPrivate::BuildCanonicalSeedList(
		GetSweepSeeds()));
	Payload.AppendChar(TEXT('\n'));
	Payload.Append(FString::Printf(
		TEXT("AcceptanceProfileHash=%016llX\n"),
		static_cast<unsigned long long>(
			ComputeAcceptanceProfileHash())));
	Payload.Append(TEXT("AcceptanceProfile="));
	Payload.Append(
		ABTSM3R2ManifestPrivate::BuildCanonicalAcceptanceProfile());
	Payload.AppendChar(TEXT('\n'));
	Payload.Append(FString::Printf(
		TEXT("RouteOracleHash=%016llX\n"),
		static_cast<unsigned long long>(FrozenRouteOracleHash)));
	Payload.Append(FString::Printf(
		TEXT("FailureIdentity=%016llX|%016llX|%016llX\n"),
		static_cast<unsigned long long>(FrozenFailureFallbackHash),
		static_cast<unsigned long long>(FrozenFailurePoolHash),
		static_cast<unsigned long long>(FrozenFailureSnapshotHash)));
	Payload.Append(FString::Printf(
		TEXT("DisplayIdentity=%d|%016llX|%016llX|%d|%d|%d|%d|%d|%d|%d|%d\n"),
		DisplaySeed,
		static_cast<unsigned long long>(FrozenDisplayPoolHash),
		static_cast<unsigned long long>(FrozenDisplaySnapshotHash),
		DisplayAttemptedCandidates,
		DisplayNormalHardPassCount,
		DisplayRetainedCandidates,
		DisplayBestRouteLengthCM,
		DisplayBestScenicBendCount,
		DisplayBestMaxStraightCM,
		DisplayBestSelfApproachCells,
		DisplayBestScore));
	for (const FABTSM3R2AcceptanceEntry& Entry : GetEntries())
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

uint64 FABTSM3R2AcceptanceManifest::ComputeManifestHash()
{
	return ABTSM3R2ManifestPrivate::HashUtf8FNV1a64(
		BuildCanonicalPayload());
}

bool FABTSM3R2AcceptanceManifest::Validate(FString& OutFailure)
{
	OutFailure.Reset();
	FString R1Failure;
	if (!FABTSM3R1AcceptanceManifest::Validate(R1Failure)
		|| FABTSM3R1AcceptanceManifest::ComputeManifestHash()
			!= RequiredR1ManifestHash)
	{
		OutFailure = FString::Printf(
			TEXT("RequiredR1Manifest:%s"),
			*R1Failure);
		return false;
	}
	if (GetSweepSeeds().Num() != 200
		|| GetSweepSeeds()[0] != DisplaySeed)
	{
		OutFailure = TEXT("SweepSeedCount");
		return false;
	}
	TSet<int32> UniqueSeeds;
	for (const int32 Seed : GetSweepSeeds())
	{
		if (UniqueSeeds.Contains(Seed))
		{
			OutFailure = FString::Printf(
				TEXT("DuplicateSweepSeed:%d"),
				Seed);
			return false;
		}
		UniqueSeeds.Add(Seed);
	}
	if (GetEntries().Num() != 7)
	{
		OutFailure = TEXT("AcceptanceEntryCount");
		return false;
	}
	TSet<FString> UniqueEntryIds;
	for (const FABTSM3R2AcceptanceEntry& Entry : GetEntries())
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
		!= FrozenSweepSeedManifestHash)
	{
		OutFailure = TEXT("SweepSeedManifestHashDrift");
		return false;
	}
	if (FrozenAcceptanceProfileHash == 0
		|| ComputeAcceptanceProfileHash()
			!= FrozenAcceptanceProfileHash)
	{
		OutFailure = TEXT("AcceptanceProfileHashDrift");
		return false;
	}
	if (FrozenRouteOracleHash == 0
		|| FrozenFailureFallbackHash == 0
		|| FrozenFailurePoolHash == 0
		|| FrozenFailureSnapshotHash == 0
		|| FrozenDisplayPoolHash == 0
		|| FrozenDisplaySnapshotHash == 0)
	{
		OutFailure = TEXT("RouteIdentityNotFrozen");
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
