// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3R1AcceptanceManifest.h"

#include "Containers/StringConv.h"

namespace ABTSM3R1ManifestPrivate
{
constexpr uint64 Fnv1a64OffsetBasis = 14695981039346656037ull;
constexpr uint64 Fnv1a64Prime = 1099511628211ull;

class FCompatibilitySnapshotHash64 final
{
public:
	void AddByte(const uint8 Value)
	{
		Hash ^= Value;
		Hash *= Fnv1a64Prime;
	}

	void AddBool(const bool Value)
	{
		AddByte(Value ? 1u : 0u);
	}

	void AddInt32(const int32 Value)
	{
		AddUInt32(static_cast<uint32>(Value));
	}

	void AddUInt32(const uint32 Value)
	{
		for (int32 Shift = 0; Shift < 32; Shift += 8)
		{
			AddByte(static_cast<uint8>((Value >> Shift) & 0xffu));
		}
	}

	void AddInt64(const int64 Value)
	{
		const uint64 Bits = static_cast<uint64>(Value);
		for (int32 Shift = 0; Shift < 64; Shift += 8)
		{
			AddByte(static_cast<uint8>((Bits >> Shift) & 0xffull));
		}
	}

	void AddFloatBits(const float Value)
	{
		uint32 Bits = 0;
		static_assert(sizeof(Bits) == sizeof(Value));
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		AddUInt32(Bits);
	}

	void AddIntArray(const TArray<int32>& Values)
	{
		AddInt32(Values.Num());
		for (const int32 Value : Values)
		{
			AddInt32(Value);
		}
	}

	uint64 Get() const
	{
		return Hash;
	}

private:
	uint64 Hash = Fnv1a64OffsetBasis;
};

const int32 CompatibilitySeeds[] = {
	312503,
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
	10, 11, 12, 13, 14, 15, 16, 17, 18, 19
};

const int32 SchemaFixtureSeeds[] = {0, 7, 19, 312503};

const FABTSM3R1CompatibilityOracle CompatibilityOracles[] = {
	{312503, 2795535429ll, 2577447183ll, 0, 0x90A64C41E03728E3ull},
	{0, 2795535429ll, 2719829185ll, 0, 0x21B01D7A4B94079Eull},
	{1, 2795535429ll, 3651135596ll, 2, 0xC1CB3D57A30ED8D4ull},
	{2, 2795535429ll, 4282802927ll, 3, 0x900C9DE2BD2EECF4ull},
	{3, 2795535429ll, 3422024465ll, 0, 0xA806776B9B4A657Cull},
	{4, 2795535429ll, 2368652805ll, 0, 0x5556043621FA8E86ull},
	{5, 2795535429ll, 430191454ll, 0, 0x35FDF41AE3FF09B4ull},
	{6, 2795535429ll, 177936111ll, 1, 0x29782A6B5D5A28B6ull},
	{7, 2795535429ll, 3836969646ll, 1, 0x15F3C06E7177FF6Full},
	{8, 2795535429ll, 2170663830ll, 5, 0x8A28073DFA23FAB8ull},
	{9, 2795535429ll, 1643886460ll, 1, 0x5748347E7F919F36ull},
	{10, 2795535429ll, 3186535802ll, 2, 0xA355861B3D6889F6ull},
	{11, 2795535429ll, 1859129517ll, 1, 0x9069B65A257CD149ull},
	{12, 2795535429ll, 2707900238ll, 3, 0x266A2DD3E455BD80ull},
	{13, 2795535429ll, 3626698661ll, 0, 0x8DF37D26FC5FEA67ull},
	{14, 2795535429ll, 2581074699ll, 7, 0xC54DE853CEEA6B41ull},
	{15, 2795535429ll, 3193841240ll, 0, 0xEA02D28AE0EE02E9ull},
	{16, 2795535429ll, 3903887350ll, 0, 0x9AEF5F2BD7BCA3AAull},
	{17, 2795535429ll, 3592027174ll, 0, 0xF92674C53F7BA808ull},
	{18, 2795535429ll, 2738082501ll, 1, 0xD9981B88AD90AAC2ull},
	{19, 2795535429ll, 551620738ll, 0, 0x6C2153E759CA67B3ull}
};

const FABTSM3R1AcceptanceEntry AcceptanceEntries[] = {
	{
		TEXT("Automation.MonthlySchema"),
		EABTSM3R1AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.Schema"),
		8,
		1
	},
	{
		TEXT("Automation.WeekOne"),
		EABTSM3R1AcceptanceLayer::Automation,
		TEXT("ABTS.M3.WeekOne"),
		2,
		1
	},
	{
		TEXT("Automation.WorldGenerationContract"),
		EABTSM3R1AcceptanceLayer::Automation,
		TEXT("ABTS.Contracts.WorldGeneration"),
		2,
		1
	},
	{
		TEXT("Automation.M110FinaleSeparation"),
		EABTSM3R1AcceptanceLayer::Automation,
		TEXT("ABTS.M110.TaskGraphFinaleSeparation"),
		1,
		1
	},
	{
		TEXT("Runtime.LABTSM3"),
		EABTSM3R1AcceptanceLayer::FreshRuntime,
		TEXT("/Game/Maps/L_ABTS_M3?-ABTSM3R1Smoke"),
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

FString BuildCanonicalOracleList()
{
	FString Result;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(CompatibilityOracles);
		++Index)
	{
		if (Index > 0)
		{
			Result.AppendChar(TEXT('\n'));
		}
		const FABTSM3R1CompatibilityOracle& Oracle =
			CompatibilityOracles[Index];
		Result.Append(FString::Printf(
			TEXT("%d|%lld|%lld|%d|%016llX"),
			Oracle.Seed,
			static_cast<long long>(Oracle.ConfigHash),
			static_cast<long long>(Oracle.LayoutHash),
			Oracle.AttemptIndex,
			static_cast<unsigned long long>(Oracle.SnapshotHash)));
	}
	return Result;
}
}

TConstArrayView<int32>
FABTSM3R1AcceptanceManifest::GetCompatibilitySeeds()
{
	return MakeArrayView(ABTSM3R1ManifestPrivate::CompatibilitySeeds);
}

TConstArrayView<int32>
FABTSM3R1AcceptanceManifest::GetSchemaFixtureSeeds()
{
	return MakeArrayView(ABTSM3R1ManifestPrivate::SchemaFixtureSeeds);
}

TConstArrayView<FABTSM3R1CompatibilityOracle>
FABTSM3R1AcceptanceManifest::GetCompatibilityOracles()
{
	return MakeArrayView(ABTSM3R1ManifestPrivate::CompatibilityOracles);
}

TConstArrayView<FABTSM3R1AcceptanceEntry>
FABTSM3R1AcceptanceManifest::GetEntries()
{
	return MakeArrayView(ABTSM3R1ManifestPrivate::AcceptanceEntries);
}

const TCHAR* FABTSM3R1AcceptanceManifest::GetLayerName(
	const EABTSM3R1AcceptanceLayer Layer)
{
	switch (Layer)
	{
	case EABTSM3R1AcceptanceLayer::Automation:
		return TEXT("Automation");
	case EABTSM3R1AcceptanceLayer::FreshRuntime:
		return TEXT("FreshRuntime");
	default:
		return TEXT("Invalid");
	}
}

uint64
FABTSM3R1AcceptanceManifest::ComputeCompatibilitySeedManifestHash()
{
	return ABTSM3R1ManifestPrivate::HashUtf8FNV1a64(
		ABTSM3R1ManifestPrivate::BuildCanonicalSeedList(
			GetCompatibilitySeeds()));
}

uint64
FABTSM3R1AcceptanceManifest::ComputeSchemaFixtureSeedManifestHash()
{
	return ABTSM3R1ManifestPrivate::HashUtf8FNV1a64(
		ABTSM3R1ManifestPrivate::BuildCanonicalSeedList(
			GetSchemaFixtureSeeds()));
}

uint64 FABTSM3R1AcceptanceManifest::ComputeCompatibilityOracleHash()
{
	return ABTSM3R1ManifestPrivate::HashUtf8FNV1a64(
		ABTSM3R1ManifestPrivate::BuildCanonicalOracleList());
}

uint64 FABTSM3R1AcceptanceManifest::ComputeCompatibilitySnapshotHash(
	const TArray<FABTSM3TaskNode>& Tasks,
	const TArray<FABTSM3TaskLink>& Links,
	const TArray<FABTSM3CellState>& CellStates,
	const TArray<FABTSM3CellEdgeState>& EdgeStates,
	const FABTSM3PCGSummary& Summary)
{
	using namespace ABTSM3R1ManifestPrivate;
	FCompatibilitySnapshotHash64 Hash;
	Hash.AddInt32(0x4D335231); // "M3R1"
	Hash.AddInt32(CanonicalHashAlgorithmVersion);

	Hash.AddInt32(Tasks.Num());
	for (const FABTSM3TaskNode& Task : Tasks)
	{
		Hash.AddInt32(Task.TaskId);
		Hash.AddByte(static_cast<uint8>(Task.Type));
		Hash.AddInt32(Task.SeedCellId);
		Hash.AddInt32(Task.RoadPortalCellId);
		Hash.AddInt32(Task.BuildingAnchorCellId);
		Hash.AddFloatBits(Task.RouteProgressDistanceCM);
		Hash.AddFloatBits(Task.FlowS);
		Hash.AddIntArray(Task.CellIds);
		Hash.AddIntArray(Task.LinkedTaskIds);
	}

	Hash.AddInt32(Links.Num());
	for (const FABTSM3TaskLink& Link : Links)
	{
		Hash.AddInt32(Link.LinkId);
		Hash.AddInt32(Link.TaskA);
		Hash.AddInt32(Link.TaskB);
		Hash.AddByte(static_cast<uint8>(Link.Role));
		Hash.AddByte(static_cast<uint8>(Link.RequiredKey));
		Hash.AddIntArray(Link.CorridorCells);
		Hash.AddInt32(Link.CorridorEdges.Num());
		for (const FABTSM3CellEdgeKey& Edge : Link.CorridorEdges)
		{
			Hash.AddInt32(Edge.CellA);
			Hash.AddInt32(Edge.CellB);
		}
		Hash.AddFloatBits(Link.CorridorLengthCM);
	}

	Hash.AddInt32(CellStates.Num());
	for (const FABTSM3CellState& Cell : CellStates)
	{
		Hash.AddInt32(Cell.TaskId);
		Hash.AddByte(static_cast<uint8>(Cell.TerrainType));
		Hash.AddFloatBits(Cell.LogicalHeight01);
		Hash.AddFloatBits(Cell.Moisture01);
		Hash.AddFloatBits(Cell.LogicalSlopeDegrees);
		Hash.AddInt32(Cell.RoadDistance);
		Hash.AddInt32(Cell.MainRoadDistance);
		Hash.AddInt32(Cell.ProgressDistance);
		Hash.AddFloatBits(Cell.ProgressDistanceCM);
		Hash.AddFloatBits(Cell.FlowS);
		Hash.AddBool(Cell.bRoad);
		Hash.AddBool(Cell.bWater);
		Hash.AddBool(Cell.bBuildingAnchor);
		Hash.AddBool(Cell.bBuildingRoadExclusion);
		Hash.AddBool(Cell.bBuildable);
	}

	Hash.AddInt32(EdgeStates.Num());
	for (const FABTSM3CellEdgeState& Edge : EdgeStates)
	{
		Hash.AddInt32(Edge.Key.CellA);
		Hash.AddInt32(Edge.Key.CellB);
		Hash.AddByte(static_cast<uint8>(Edge.Transport));
		Hash.AddByte(static_cast<uint8>(Edge.Water));
		Hash.AddByte(static_cast<uint8>(Edge.Crossing));
		Hash.AddByte(static_cast<uint8>(Edge.RequiredKey));
		Hash.AddInt32(Edge.DownstreamCellId);
		Hash.AddFloatBits(Edge.FlowAccumulation);
		Hash.AddBool(Edge.bBlocksOnFoot);
	}

	Hash.AddInt32(Summary.GeneratorVersion);
	Hash.AddInt32(Summary.LayoutPolicyVersion);
	Hash.AddInt64(Summary.ConfigHash);
	Hash.AddInt64(Summary.LayoutHash);
	Hash.AddInt32(Summary.AttemptIndex);
	Hash.AddInt32(Summary.AssignedTaskCells);
	Hash.AddInt32(Summary.RiverEdges);
	Hash.AddInt32(Summary.RoadEdges);
	Hash.AddInt32(Summary.BridgeEdge.CellA);
	Hash.AddInt32(Summary.BridgeEdge.CellB);
	Hash.AddInt32(Summary.ShortcutEdge.CellA);
	Hash.AddInt32(Summary.ShortcutEdge.CellB);
	Hash.AddBool(Summary.bBridgeLockedBeforeBuild);
	Hash.AddBool(Summary.bMainPathReachableAfterBridge);
	Hash.AddFloatBits(Summary.SatelliteLaunchAngularSeparationDegrees);
	Hash.AddFloatBits(Summary.MainRouteLengthCM);
	Hash.AddFloatBits(Summary.MinAdjacentBuildingProgressCM);
	Hash.AddBool(Summary.bWorkshopVisibleAtDefaultOrbit);
	Hash.AddBool(Summary.bWorkshopVisibleAtMaxOrbit);
	Hash.AddBool(Summary.bTargetBuildingVisibleAtDefaultOrbit);
	Hash.AddBool(Summary.bTargetBuildingVisibleAtMaxOrbit);
	Hash.AddBool(Summary.bFurnaceVisibleAtDefaultOrbit);
	Hash.AddBool(Summary.bFurnaceVisibleAtMaxOrbit);
	Hash.AddBool(Summary.bAccepted);
	return Hash.Get();
}

FString FABTSM3R1AcceptanceManifest::BuildCanonicalPayload()
{
	FString Payload;
	Payload.Reserve(4096);
	Payload.Append(TEXT("M3R1AcceptanceManifest\n"));
	Payload.Append(FString::Printf(
		TEXT("ManifestSchemaVersion=%d\n"),
		ManifestSchemaVersion));
	Payload.Append(FString::Printf(
		TEXT("MonthlySchemaVersion=%d\n"),
		MonthlySchemaVersion));
	Payload.Append(FString::Printf(
		TEXT("CanonicalHashAlgorithmVersion=%d\n"),
		CanonicalHashAlgorithmVersion));
	Payload.Append(FString::Printf(
		TEXT("QuantizationVersion=%d\n"),
		QuantizationVersion));
	Payload.Append(FString::Printf(
		TEXT("GeneratorVersion=%d\n"),
		GeneratorVersion));
	Payload.Append(FString::Printf(
		TEXT("CompatibilityLayoutPolicyVersion=%d\n"),
		CompatibilityLayoutPolicyVersion));
	Payload.Append(FString::Printf(
		TEXT("MonthlyLayoutPolicyVersion=%d\n"),
		MonthlyLayoutPolicyVersion));
	Payload.Append(FString::Printf(
		TEXT("RequiredR0ManifestHash=%016llX\n"),
		static_cast<unsigned long long>(RequiredR0ManifestHash)));
	Payload.Append(FString::Printf(
		TEXT("CompatibilitySeedManifestHash=%016llX\n"),
		static_cast<unsigned long long>(
			ComputeCompatibilitySeedManifestHash())));
	Payload.Append(TEXT("CompatibilitySeeds="));
	Payload.Append(ABTSM3R1ManifestPrivate::BuildCanonicalSeedList(
		GetCompatibilitySeeds()));
	Payload.AppendChar(TEXT('\n'));
	Payload.Append(FString::Printf(
		TEXT("SchemaFixtureSeedManifestHash=%016llX\n"),
		static_cast<unsigned long long>(
			ComputeSchemaFixtureSeedManifestHash())));
	Payload.Append(TEXT("SchemaFixtureSeeds="));
	Payload.Append(ABTSM3R1ManifestPrivate::BuildCanonicalSeedList(
		GetSchemaFixtureSeeds()));
	Payload.AppendChar(TEXT('\n'));
	Payload.Append(FString::Printf(
		TEXT("CompatibilityOracleHash=%016llX\n"),
		static_cast<unsigned long long>(
			ComputeCompatibilityOracleHash())));
	for (const FABTSM3R1CompatibilityOracle& Oracle :
		GetCompatibilityOracles())
	{
		Payload.Append(FString::Printf(
			TEXT("Oracle=%d|%lld|%lld|%d|%016llX\n"),
			Oracle.Seed,
			static_cast<long long>(Oracle.ConfigHash),
			static_cast<long long>(Oracle.LayoutHash),
			Oracle.AttemptIndex,
			static_cast<unsigned long long>(Oracle.SnapshotHash)));
	}
	Payload.Append(FString::Printf(
		TEXT("DisplaySeed=%d\n"),
		DisplaySeed));
	Payload.Append(FString::Printf(
		TEXT("DisplaySchemaConfigHash=%016llX\n"),
		static_cast<unsigned long long>(
			FrozenDisplaySchemaConfigHash)));
	Payload.Append(FString::Printf(
		TEXT("DisplaySchemaLayoutHash=%016llX\n"),
		static_cast<unsigned long long>(
			FrozenDisplaySchemaLayoutHash)));
	Payload.Append(FString::Printf(
		TEXT("DisplayCounts=%d|%d|%d|%d|%d\n"),
		DisplayRouteBeatCount,
		DisplayEncounterCount,
		DisplayPocketCount,
		DisplayBiomeDistrictCount,
		DisplayPlayableEnvelopeCount));
	for (const FABTSM3R1AcceptanceEntry& Entry : GetEntries())
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

uint64 FABTSM3R1AcceptanceManifest::ComputeManifestHash()
{
	return ABTSM3R1ManifestPrivate::HashUtf8FNV1a64(
		BuildCanonicalPayload());
}

bool FABTSM3R1AcceptanceManifest::Validate(FString& OutFailure)
{
	OutFailure.Reset();
	if (GetCompatibilitySeeds().Num() != 21
		|| GetCompatibilityOracles().Num()
			!= GetCompatibilitySeeds().Num()
		|| GetCompatibilitySeeds()[0] != DisplaySeed)
	{
		OutFailure = TEXT("CompatibilitySeedOrOracleCount");
		return false;
	}
	TSet<int32> UniqueSeeds;
	for (int32 Index = 0; Index < GetCompatibilitySeeds().Num();
		++Index)
	{
		const int32 Seed = GetCompatibilitySeeds()[Index];
		const FABTSM3R1CompatibilityOracle& Oracle =
			GetCompatibilityOracles()[Index];
		if (UniqueSeeds.Contains(Seed)
			|| Oracle.Seed != Seed
			|| Oracle.ConfigHash == 0
			|| Oracle.LayoutHash == 0
			|| Oracle.AttemptIndex < 0
			|| Oracle.SnapshotHash == 0)
		{
			OutFailure =
				FString::Printf(TEXT("CompatibilityOracle:%d"), Index);
			return false;
		}
		UniqueSeeds.Add(Seed);
	}
	if (GetSchemaFixtureSeeds().Num() != 4)
	{
		OutFailure = TEXT("SchemaFixtureSeedCount");
		return false;
	}
	TSet<int32> UniqueFixtureSeeds;
	for (const int32 Seed : GetSchemaFixtureSeeds())
	{
		if (!UniqueSeeds.Contains(Seed)
			|| UniqueFixtureSeeds.Contains(Seed))
		{
			OutFailure =
				FString::Printf(TEXT("SchemaFixtureSeed:%d"), Seed);
			return false;
		}
		UniqueFixtureSeeds.Add(Seed);
	}
	if (GetEntries().Num() != 5)
	{
		OutFailure = TEXT("AcceptanceEntryCount");
		return false;
	}
	TSet<FString> UniqueEntryIds;
	for (const FABTSM3R1AcceptanceEntry& Entry : GetEntries())
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
	if (ComputeCompatibilitySeedManifestHash()
		!= FrozenCompatibilitySeedManifestHash)
	{
		OutFailure = TEXT("CompatibilitySeedManifestHashDrift");
		return false;
	}
	if (ComputeSchemaFixtureSeedManifestHash()
		!= FrozenSchemaFixtureSeedManifestHash)
	{
		OutFailure = TEXT("SchemaFixtureSeedManifestHashDrift");
		return false;
	}
	if (ComputeCompatibilityOracleHash()
		!= FrozenCompatibilityOracleHash)
	{
		OutFailure = TEXT("CompatibilityOracleHashDrift");
		return false;
	}
	if (FrozenDisplaySchemaConfigHash == 0
		|| FrozenDisplaySchemaLayoutHash == 0)
	{
		OutFailure = TEXT("DisplaySchemaIdentityNotFrozen");
		return false;
	}
	if (ComputeManifestHash() != FrozenManifestHash)
	{
		OutFailure = TEXT("AcceptanceManifestHashDrift");
		return false;
	}
	return true;
}
