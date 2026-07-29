// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3MonthlySchema.h"

#include "ABTSRuntime.h"
#include "Algo/Unique.h"
#include "Containers/StringConv.h"

namespace ABTSM3R1SchemaPrivate
{
constexpr uint64 Fnv1a64OffsetBasis = 14695981039346656037ull;
constexpr uint64 Fnv1a64Prime = 1099511628211ull;
constexpr int32 KnownActiveRoleMask =
	static_cast<int32>(EABTSM3ActiveRole::Route)
	| static_cast<int32>(EABTSM3ActiveRole::RoadArrival)
	| static_cast<int32>(EABTSM3ActiveRole::Reveal)
	| static_cast<int32>(EABTSM3ActiveRole::Slingshot)
	| static_cast<int32>(EABTSM3ActiveRole::Target)
	| static_cast<int32>(EABTSM3ActiveRole::Reward)
	| static_cast<int32>(EABTSM3ActiveRole::Exit)
	| static_cast<int32>(EABTSM3ActiveRole::Resource);
constexpr int32 RouteBeatIdBase = 1000000;
constexpr int32 EncounterIdBase = 2000000;
constexpr int32 PocketIdBase = 3000000;
constexpr int32 EnvelopeIdBase = 4000000;
constexpr int32 BiomeDistrictIdBase = 5000000;
constexpr int32 PocketRoleStride = 16;
constexpr int32 MaxSourceTaskIdForStableSchema =
	(MAX_int32 - PocketIdBase
		- static_cast<int32>(EABTSM3PocketRole::Exit))
	/ PocketRoleStride;

class FCanonicalHash64 final
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
		const uint32 Bits = static_cast<uint32>(Value);
		for (int32 Shift = 0; Shift < 32; Shift += 8)
		{
			AddByte(static_cast<uint8>((Bits >> Shift) & 0xffu));
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

	void AddName(const FName Value)
	{
		FString Canonical = Value.IsNone() ? FString() : Value.ToString();
		Canonical.ToLowerInline();
		const FTCHARToUTF8 Utf8(*Canonical);
		AddInt32(Utf8.Length());
		for (int32 Index = 0; Index < Utf8.Length(); ++Index)
		{
			AddByte(static_cast<uint8>(Utf8.Get()[Index]));
		}
	}

	void AddCentimeters(const float Value)
	{
		AddInt64(FMath::IsFinite(Value)
			? FMath::RoundToInt64(static_cast<double>(Value) * 10.0)
			: MIN_int64);
	}

	void AddUnitInterval(const float Value)
	{
		AddInt64(FMath::IsFinite(Value)
			? FMath::RoundToInt64(static_cast<double>(Value) * 1000000.0)
			: MIN_int64);
	}

	uint64 Get() const
	{
		return Hash;
	}

private:
	uint64 Hash = Fnv1a64OffsetBasis;
};

int64 QuantizeCentimeters(const float Value)
{
	return FMath::RoundToInt64(static_cast<double>(Value) * 10.0);
}

bool IsOrdinaryBuildingTask(const EABTSM3TaskType Type)
{
	return Type == EABTSM3TaskType::Workshop
		|| Type == EABTSM3TaskType::TargetBuilding
		|| Type == EABTSM3TaskType::FurnaceRuins;
}

EABTSM3RouteBeatRole ResolveBeatRole(const EABTSM3TaskType Type)
{
	switch (Type)
	{
	case EABTSM3TaskType::Start:
		return EABTSM3RouteBeatRole::Start;
	case EABTSM3TaskType::Workshop:
	case EABTSM3TaskType::TargetBuilding:
	case EABTSM3TaskType::FurnaceRuins:
		return EABTSM3RouteBeatRole::Attack;
	case EABTSM3TaskType::BridgeGate:
		return EABTSM3RouteBeatRole::Gate;
	case EABTSM3TaskType::Scout:
		return EABTSM3RouteBeatRole::Reveal;
	case EABTSM3TaskType::SlingshotRange:
	case EABTSM3TaskType::SatelliteWindow:
		return EABTSM3RouteBeatRole::Training;
	case EABTSM3TaskType::LaunchSite:
		return EABTSM3RouteBeatRole::Finale;
	default:
		return EABTSM3RouteBeatRole::Travel;
	}
}

EABTSM3BuildingPurpose ResolveBuildingPurpose(const EABTSM3TaskType Type)
{
	switch (Type)
	{
	case EABTSM3TaskType::Workshop:
		return EABTSM3BuildingPurpose::Crafting;
	case EABTSM3TaskType::TargetBuilding:
		return EABTSM3BuildingPurpose::ProgressionTarget;
	case EABTSM3TaskType::FurnaceRuins:
		return EABTSM3BuildingPurpose::ResourceTarget;
	case EABTSM3TaskType::SatelliteWindow:
		return EABTSM3BuildingPurpose::GravityTraining;
	case EABTSM3TaskType::LaunchSite:
		return EABTSM3BuildingPurpose::FinaleSupport;
	default:
		return EABTSM3BuildingPurpose::None;
	}
}

EABTSM3BiomeArchetype ResolveBiomeArchetype(
	const EABTSM3TerrainType TerrainType)
{
	switch (TerrainType)
	{
	case EABTSM3TerrainType::Forest:
		return EABTSM3BiomeArchetype::Forest;
	case EABTSM3TerrainType::Highland:
		return EABTSM3BiomeArchetype::Highland;
	case EABTSM3TerrainType::Mountain:
		return EABTSM3BiomeArchetype::Mountain;
	case EABTSM3TerrainType::Water:
		return EABTSM3BiomeArchetype::Water;
	default:
		return EABTSM3BiomeArchetype::Plain;
	}
}

bool IsValidTerrainType(const EABTSM3TerrainType TerrainType)
{
	return static_cast<uint8>(TerrainType)
		<= static_cast<uint8>(EABTSM3TerrainType::Water);
}

bool IsValidGenerationMode(const EABTSM3GenerationMode Mode)
{
	return static_cast<uint8>(Mode)
		<= static_cast<uint8>(EABTSM3GenerationMode::MonthlyDevelopment);
}

bool IsValidBeatRole(const EABTSM3RouteBeatRole Role)
{
	return static_cast<uint8>(Role)
		<= static_cast<uint8>(EABTSM3RouteBeatRole::Finale);
}

bool IsValidEncounterRole(const EABTSM3EncounterRole Role)
{
	return static_cast<uint8>(Role)
		<= static_cast<uint8>(EABTSM3EncounterRole::RewardCache);
}

bool IsValidBuildingPurpose(const EABTSM3BuildingPurpose Purpose)
{
	return static_cast<uint8>(Purpose)
		<= static_cast<uint8>(EABTSM3BuildingPurpose::FinaleSupport);
}

bool IsValidBiomeArchetype(const EABTSM3BiomeArchetype Archetype)
{
	return static_cast<uint8>(Archetype)
		<= static_cast<uint8>(EABTSM3BiomeArchetype::Background);
}

bool IsValidPocketRole(const EABTSM3PocketRole Role)
{
	return static_cast<uint8>(Role)
		<= static_cast<uint8>(EABTSM3PocketRole::Exit);
}

bool IsValidResolution(const EABTSM3SchemaResolution Resolution)
{
	return static_cast<uint8>(Resolution)
		<= static_cast<uint8>(EABTSM3SchemaResolution::Finalized);
}

bool IsValidRejectReason(const EABTSM3SchemaRejectReason Reason)
{
	return static_cast<uint8>(Reason)
		<= static_cast<uint8>(EABTSM3SchemaRejectReason::LayoutHashMismatch);
}

bool IsValidProgressKey(const EABTSM3ProgressKey Key)
{
	return static_cast<uint8>(Key)
		<= static_cast<uint8>(EABTSM3ProgressKey::HaveCrystalCore);
}

int32 MakeRouteBeatId(const int32 MissionTaskId)
{
	return RouteBeatIdBase + MissionTaskId;
}

int32 MakeEncounterId(const int32 MissionTaskId)
{
	return EncounterIdBase + MissionTaskId;
}

int32 MakePocketId(
	const int32 MissionTaskId,
	const EABTSM3PocketRole Role)
{
	return PocketIdBase
		+ MissionTaskId * PocketRoleStride
		+ static_cast<int32>(Role);
}

int32 MakeEnvelopeId(const int32 MissionTaskId)
{
	return EnvelopeIdBase + MissionTaskId;
}

int32 MakeBiomeDistrictId(const EABTSM3TerrainType TerrainType)
{
	return BiomeDistrictIdBase + static_cast<int32>(TerrainType);
}

int32 BiomeDistrictForCell(
	const TArray<FABTSM3CellState>& CellStates,
	const int32 CellId)
{
	if (!CellStates.IsValidIndex(CellId)
		|| !IsValidTerrainType(CellStates[CellId].TerrainType))
	{
		return INDEX_NONE;
	}
	return MakeBiomeDistrictId(CellStates[CellId].TerrainType);
}

void AddUniqueSortedProgressKey(
	TArray<EABTSM3ProgressKey>& Keys,
	const EABTSM3ProgressKey Key)
{
	if (Key == EABTSM3ProgressKey::None)
	{
		return;
	}
	Keys.AddUnique(Key);
	Keys.Sort([](
		const EABTSM3ProgressKey A,
		const EABTSM3ProgressKey B)
	{
		return static_cast<uint8>(A) < static_cast<uint8>(B);
	});
}

void AddOrCombinePlayableRole(
	TMap<int32, int32>& RolesByCell,
	const int32 CellId,
	const EABTSM3ActiveRole Role)
{
	if (CellId == INDEX_NONE)
	{
		return;
	}
	RolesByCell.FindOrAdd(CellId) |= static_cast<int32>(Role);
}

bool IsStrictlyAscending(const TArray<int32>& Values)
{
	for (int32 Index = 1; Index < Values.Num(); ++Index)
	{
		if (Values[Index - 1] >= Values[Index])
		{
			return false;
		}
	}
	return true;
}

template <typename EnumType>
bool IsStrictlyAscendingEnum(const TArray<EnumType>& Values)
{
	for (int32 Index = 1; Index < Values.Num(); ++Index)
	{
		if (static_cast<uint8>(Values[Index - 1])
			>= static_cast<uint8>(Values[Index]))
		{
			return false;
		}
	}
	return true;
}

bool AreValidProgressKeys(const TArray<EABTSM3ProgressKey>& Values)
{
	for (const EABTSM3ProgressKey Value : Values)
	{
		if (!IsValidProgressKey(Value)
			|| Value == EABTSM3ProgressKey::None)
		{
			return false;
		}
	}
	return true;
}

void HashIntArray(
	FCanonicalHash64& Hash,
	const TArray<int32>& Values)
{
	Hash.AddInt32(Values.Num());
	for (const int32 Value : Values)
	{
		Hash.AddInt32(Value);
	}
}

void HashProgressKeys(
	FCanonicalHash64& Hash,
	const TArray<EABTSM3ProgressKey>& Values)
{
	Hash.AddInt32(Values.Num());
	for (const EABTSM3ProgressKey Value : Values)
	{
		Hash.AddByte(static_cast<uint8>(Value));
	}
}
}

int32 FABTSM3MonthlySchemaBuilder::ResolveLayoutPolicyVersion(
	const FABTSM3MonthlySchemaConfig& Config)
{
	return Config.Mode == EABTSM3GenerationMode::CompatibilityOracle
		? CompatibilityLayoutPolicyVersion
		: Config.MonthlyLayoutPolicyVersion;
}

uint64 FABTSM3MonthlySchemaBuilder::ComputeConfigHash(
	const FABTSM3MonthlySchemaConfig& Config,
	const int64 SourceConfigHash)
{
	using namespace ABTSM3R1SchemaPrivate;
	FCanonicalHash64 Hash;
	Hash.AddInt32(SchemaVersion);
	Hash.AddInt32(GeneratorVersion);
	Hash.AddInt32(ResolveLayoutPolicyVersion(Config));
	Hash.AddByte(static_cast<uint8>(Config.Mode));
	Hash.AddInt64(SourceConfigHash);
	Hash.AddInt32(Config.MonthlyLayoutPolicyVersion);
	Hash.AddInt64(Config.RouteTemplateCatalogHash);
	Hash.AddInt64(Config.EncounterTemplateCatalogHash);
	Hash.AddInt64(Config.BiomeTemplateCatalogHash);
	Hash.AddInt64(Config.M7ProfileCatalogHash);
	Hash.AddInt32(Config.M6SolverVersion);
	Hash.AddInt32(Config.M9SolverVersion);
	Hash.AddBool(Config.bBuildObservation);
	// bEmitLayerLogs is diagnostic-only and intentionally excluded.
	return Hash.Get();
}

uint64 FABTSM3MonthlySchemaBuilder::ComputeLayoutHash(
	const FABTSM3MonthlyWorldSchema& Schema)
{
	using namespace ABTSM3R1SchemaPrivate;
	FCanonicalHash64 Hash;
	const FABTSM3MonthlySchemaIdentity& Identity = Schema.Identity;
	Hash.AddInt32(Identity.SchemaVersion);
	Hash.AddInt32(Identity.GeneratorVersion);
	Hash.AddInt32(Identity.LayoutPolicyVersion);
	Hash.AddByte(static_cast<uint8>(Identity.Mode));
	Hash.AddInt32(Identity.WorldSeed);
	Hash.AddInt64(Identity.SourceConfigHash);
	Hash.AddInt64(Identity.SourceLayoutHash);
	Hash.AddInt64(Identity.SchemaConfigHash);
	// Identity.SchemaLayoutHash is excluded to make hashing idempotent.

	Hash.AddInt32(Schema.RouteBeats.Num());
	for (const FABTSM3RouteBeatPlan& Beat : Schema.RouteBeats)
	{
		Hash.AddInt32(Beat.BeatId);
		Hash.AddInt32(Beat.OrderIndex);
		Hash.AddInt32(Beat.MissionTaskId);
		Hash.AddInt32(Beat.RouteCandidateId);
		Hash.AddByte(static_cast<uint8>(Beat.Role));
		Hash.AddInt32(Beat.EncounterId);
		Hash.AddInt32(Beat.RoadPortalCellId);
		Hash.AddInt32(Beat.RevealPocketId);
		Hash.AddInt32(Beat.WitnessId);
		Hash.AddInt32(Beat.BiomeDistrictId);
		Hash.AddCentimeters(Beat.ProgressDistanceCM);
		Hash.AddUnitInterval(Beat.FlowS);
		Hash.AddByte(static_cast<uint8>(Beat.Resolution));
	}

	Hash.AddInt32(Schema.Encounters.Num());
	for (const FABTSM3EncounterContract& Encounter : Schema.Encounters)
	{
		Hash.AddInt32(Encounter.EncounterId);
		Hash.AddInt32(Encounter.OrderIndex);
		Hash.AddInt32(Encounter.MissionTaskId);
		Hash.AddInt32(Encounter.RouteBeatId);
		Hash.AddByte(static_cast<uint8>(Encounter.Role));
		Hash.AddByte(static_cast<uint8>(Encounter.BuildingPurpose));
		Hash.AddInt32(Encounter.DifficultyBand);
		Hash.AddCentimeters(Encounter.ProgressDistanceCM);
		Hash.AddUnitInterval(Encounter.FlowS);
		HashProgressKeys(Hash, Encounter.RequiredKeys);
		HashProgressKeys(Hash, Encounter.GrantedKeys);
		Hash.AddInt32(Encounter.RoadArrivalPocketId);
		Hash.AddInt32(Encounter.ScoutRevealPocketId);
		Hash.AddInt32(Encounter.SlingshotPocketId);
		Hash.AddInt32(Encounter.TargetEnvelopePocketId);
		Hash.AddInt32(Encounter.TargetAnchorPocketId);
		Hash.AddInt32(Encounter.RewardPocketId);
		Hash.AddInt32(Encounter.ExitPocketId);
		Hash.AddInt32(Encounter.BallisticWitnessId);
		Hash.AddInt32(Encounter.BiomeDistrictId);
		Hash.AddName(Encounter.ResolvedM7ProfileId);
		Hash.AddInt64(Encounter.ProfileCatalogHash);
		Hash.AddByte(static_cast<uint8>(Encounter.Resolution));
	}

	Hash.AddInt32(Schema.Pockets.Num());
	for (const FABTSM3PocketContract& Pocket : Schema.Pockets)
	{
		Hash.AddInt32(Pocket.PocketId);
		Hash.AddInt32(Pocket.EncounterId);
		Hash.AddInt32(Pocket.RouteBeatId);
		Hash.AddByte(static_cast<uint8>(Pocket.Role));
		Hash.AddInt32(Pocket.AnchorCellId);
		HashIntArray(Hash, Pocket.CellIds);
		Hash.AddInt32(Pocket.BiomeDistrictId);
		Hash.AddByte(static_cast<uint8>(Pocket.Resolution));
	}

	Hash.AddInt32(Schema.BiomeDistricts.Num());
	for (const FABTSM3BiomeDistrict& District : Schema.BiomeDistricts)
	{
		Hash.AddInt32(District.BiomeDistrictId);
		Hash.AddByte(static_cast<uint8>(District.Archetype));
		Hash.AddByte(static_cast<uint8>(District.ObservedTerrainType));
		HashIntArray(Hash, District.CellIds);
		Hash.AddCentimeters(District.MinProgressDistanceCM);
		Hash.AddCentimeters(District.MaxProgressDistanceCM);
		Hash.AddUnitInterval(District.MinFlowS);
		Hash.AddUnitInterval(District.MaxFlowS);
		Hash.AddBool(District.bBackground);
		Hash.AddByte(static_cast<uint8>(District.Resolution));
	}

	Hash.AddInt32(Schema.PlayableEnvelopes.Num());
	for (const FABTSM3PlayableEnvelope& Envelope : Schema.PlayableEnvelopes)
	{
		Hash.AddInt32(Envelope.EnvelopeId);
		Hash.AddInt32(Envelope.RouteBeatId);
		Hash.AddInt32(Envelope.EncounterId);
		Hash.AddCentimeters(Envelope.MinProgressDistanceCM);
		Hash.AddCentimeters(Envelope.MaxProgressDistanceCM);
		Hash.AddInt32(Envelope.Cells.Num());
		for (const FABTSM3PlayableCellRole& Cell : Envelope.Cells)
		{
			Hash.AddInt32(Cell.CellId);
			Hash.AddInt32(Cell.ActiveRoleMask);
			Hash.AddInt32(Cell.BiomeDistrictId);
		}
		Hash.AddByte(static_cast<uint8>(Envelope.Resolution));
	}

	const FABTSM3WorldQualityReport& Quality = Schema.Quality;
	Hash.AddBool(Quality.bSchemaValid);
	Hash.AddBool(Quality.bMonthlyWorldAccepted);
	Hash.AddByte(static_cast<uint8>(Quality.RejectReason));
	Hash.AddInt32(Quality.SourceCellCount);
	Hash.AddInt32(Quality.RouteCandidateCount);
	Hash.AddInt32(Quality.BeatCount);
	Hash.AddInt32(Quality.EncounterCount);
	Hash.AddInt32(Quality.PocketCount);
	Hash.AddInt32(Quality.BiomeDistrictCount);
	Hash.AddInt32(Quality.PlayableEnvelopeCount);
	Hash.AddInt32(Quality.PlayableCellAssignmentCount);
	Hash.AddInt32(Quality.ActiveRoleCellCount);
	Hash.AddInt32(Quality.DeepWildCellCount);
	Hash.AddCentimeters(Quality.MainRouteLengthCM);
	Hash.AddInt32(Quality.RouteScore);
	Hash.AddInt32(Quality.EncounterScore);
	Hash.AddInt32(Quality.BiomeScore);
	Hash.AddInt32(Quality.OverallScore);
	return Hash.Get();
}

bool FABTSM3MonthlySchemaBuilder::Build(
	const int32 WorldSeed,
	const FABTSM3MonthlySchemaConfig& Config,
	const TArray<FABTSM3TaskNode>& Tasks,
	const TArray<FABTSM3TaskLink>& Links,
	const TArray<FABTSM3CellState>& CellStates,
	const FABTSM3PCGSummary& SourceSummary,
	FABTSM3MonthlyWorldSchema& OutSchema,
	FString& OutFailure)
{
	using namespace ABTSM3R1SchemaPrivate;
	OutSchema = FABTSM3MonthlyWorldSchema();
	OutFailure.Reset();

	if (!IsValidGenerationMode(Config.Mode)
		|| Config.MonthlyLayoutPolicyVersion < FirstMonthlyLayoutPolicyVersion
		|| Config.MonthlyLayoutPolicyVersion > 255
		|| Config.M6SolverVersion < 0
		|| Config.M9SolverVersion < 0
		|| SourceSummary.GeneratorVersion != GeneratorVersion
		|| SourceSummary.LayoutPolicyVersion != CompatibilityLayoutPolicyVersion)
	{
		OutFailure = TEXT("InvalidModeIdentity");
		OutSchema.Quality.RejectReason =
			EABTSM3SchemaRejectReason::InvalidModeIdentity;
		return false;
	}
	if (!SourceSummary.bAccepted)
	{
		OutFailure = TEXT("SourceWorldRejected");
		OutSchema.Quality.RejectReason =
			EABTSM3SchemaRejectReason::SourceWorldRejected;
		return false;
	}

	OutSchema.Identity.SchemaVersion = SchemaVersion;
	OutSchema.Identity.GeneratorVersion = GeneratorVersion;
	OutSchema.Identity.LayoutPolicyVersion =
		ResolveLayoutPolicyVersion(Config);
	OutSchema.Identity.Mode = Config.Mode;
	OutSchema.Identity.WorldSeed = WorldSeed;
	OutSchema.Identity.SourceConfigHash = SourceSummary.ConfigHash;
	OutSchema.Identity.SourceLayoutHash = SourceSummary.LayoutHash;
	OutSchema.Identity.SchemaConfigHash = static_cast<int64>(
		ComputeConfigHash(Config, SourceSummary.ConfigHash));
	OutSchema.Quality.SourceCellCount = CellStates.Num();
	OutSchema.Quality.MainRouteLengthCM = SourceSummary.MainRouteLengthCM;
	OutSchema.Quality.bMonthlyWorldAccepted = false;
	OutSchema.Quality.RejectReason = EABTSM3SchemaRejectReason::NotEvaluated;

	if (!Config.bBuildObservation)
	{
		if (Config.Mode != EABTSM3GenerationMode::CompatibilityOracle)
		{
			OutFailure = TEXT("MonthlyObservationDisabled");
			OutSchema.Quality.RejectReason =
				EABTSM3SchemaRejectReason::InvalidModeIdentity;
			return false;
		}
		OutSchema.Quality.bSchemaValid = true;
		OutSchema.Identity.SchemaLayoutHash = static_cast<int64>(
			ComputeLayoutHash(OutSchema));
		if (Config.bEmitLayerLogs)
		{
			LogLayerSummaries(OutSchema);
		}
		return true;
	}

	if (Tasks.IsEmpty() || CellStates.IsEmpty())
	{
		OutFailure = TEXT("EmptySourceWorld");
		OutSchema.Quality.RejectReason =
			EABTSM3SchemaRejectReason::InvalidReference;
		return false;
	}

	// R-1 observes all five stable legacy terrain classes as separate proxy
	// districts. R-3 will replace this with actual spatial BiomeDistrict planning.
	constexpr int32 LegacyTerrainTypeCount =
		static_cast<int32>(EABTSM3TerrainType::Water) + 1;
	OutSchema.BiomeDistricts.SetNum(LegacyTerrainTypeCount);
	TArray<bool> bDistrictHasCells;
	bDistrictHasCells.Init(false, LegacyTerrainTypeCount);
	for (int32 DistrictId = 0;
		DistrictId < LegacyTerrainTypeCount;
		++DistrictId)
	{
		FABTSM3BiomeDistrict& District =
			OutSchema.BiomeDistricts[DistrictId];
		District.BiomeDistrictId = MakeBiomeDistrictId(
			static_cast<EABTSM3TerrainType>(DistrictId));
		District.ObservedTerrainType =
			static_cast<EABTSM3TerrainType>(DistrictId);
		District.Archetype =
			ResolveBiomeArchetype(District.ObservedTerrainType);
		District.MinProgressDistanceCM = MAX_flt;
		District.MaxProgressDistanceCM = -MAX_flt;
		District.MinFlowS = 1.0f;
		District.MaxFlowS = 0.0f;
		District.Resolution =
			EABTSM3SchemaResolution::CompatibilityObserved;
	}
	for (int32 CellId = 0; CellId < CellStates.Num(); ++CellId)
	{
		const FABTSM3CellState& State = CellStates[CellId];
		if (!IsValidTerrainType(State.TerrainType)
			|| !FMath::IsFinite(State.ProgressDistanceCM)
			|| !FMath::IsFinite(State.FlowS))
		{
			OutFailure =
				FString::Printf(TEXT("InvalidCellState:%d"), CellId);
			OutSchema.Quality.RejectReason =
				EABTSM3SchemaRejectReason::InvalidRange;
			return false;
		}
		const int32 DistrictId = static_cast<int32>(State.TerrainType);
		FABTSM3BiomeDistrict& District =
			OutSchema.BiomeDistricts[DistrictId];
		District.CellIds.Add(CellId);
		District.MinProgressDistanceCM = FMath::Min(
			District.MinProgressDistanceCM,
			State.ProgressDistanceCM);
		District.MaxProgressDistanceCM = FMath::Max(
			District.MaxProgressDistanceCM,
			State.ProgressDistanceCM);
		District.MinFlowS = FMath::Min(District.MinFlowS, State.FlowS);
		District.MaxFlowS = FMath::Max(District.MaxFlowS, State.FlowS);
		bDistrictHasCells[DistrictId] = true;
	}
	for (int32 DistrictId = 0;
		DistrictId < LegacyTerrainTypeCount;
		++DistrictId)
	{
		if (!bDistrictHasCells[DistrictId])
		{
			FABTSM3BiomeDistrict& District =
				OutSchema.BiomeDistricts[DistrictId];
			District.MinProgressDistanceCM = 0.0f;
			District.MaxProgressDistanceCM = 0.0f;
			District.MinFlowS = 0.0f;
			District.MaxFlowS = 0.0f;
		}
	}

	TArray<int32> OrderedTaskIndices;
	OrderedTaskIndices.Reserve(Tasks.Num());
	TSet<int32> UniqueTaskIds;
	for (int32 TaskIndex = 0; TaskIndex < Tasks.Num(); ++TaskIndex)
	{
		const FABTSM3TaskNode& Task = Tasks[TaskIndex];
		if (Task.TaskId < 0
			|| Task.TaskId > MaxSourceTaskIdForStableSchema
			|| UniqueTaskIds.Contains(Task.TaskId)
			|| !FMath::IsFinite(Task.RouteProgressDistanceCM)
			|| !FMath::IsFinite(Task.FlowS)
			|| Task.RouteProgressDistanceCM < 0.0f
			|| Task.FlowS < 0.0f
			|| Task.FlowS > 1.0f)
		{
			OutFailure =
				FString::Printf(TEXT("InvalidTask:%d"), TaskIndex);
			OutSchema.Quality.RejectReason =
				UniqueTaskIds.Contains(Task.TaskId)
					? EABTSM3SchemaRejectReason::DuplicateStableId
					: EABTSM3SchemaRejectReason::InvalidRange;
			return false;
		}
		UniqueTaskIds.Add(Task.TaskId);
		OrderedTaskIndices.Add(TaskIndex);
	}
	OrderedTaskIndices.Sort([&Tasks](
		const int32 AIndex,
		const int32 BIndex)
	{
		const FABTSM3TaskNode& A = Tasks[AIndex];
		const FABTSM3TaskNode& B = Tasks[BIndex];
		const int64 AProgress =
			QuantizeCentimeters(A.RouteProgressDistanceCM);
		const int64 BProgress =
			QuantizeCentimeters(B.RouteProgressDistanceCM);
		if (AProgress != BProgress)
		{
			return AProgress < BProgress;
		}
		return A.TaskId < B.TaskId;
	});

	for (int32 OrderIndex = 0;
		OrderIndex < OrderedTaskIndices.Num();
		++OrderIndex)
	{
		const FABTSM3TaskNode& Task =
			Tasks[OrderedTaskIndices[OrderIndex]];
		FABTSM3RouteBeatPlan& Beat =
			OutSchema.RouteBeats.AddDefaulted_GetRef();
		Beat.BeatId = MakeRouteBeatId(Task.TaskId);
		Beat.OrderIndex = OrderIndex;
		Beat.MissionTaskId = Task.TaskId;
		Beat.RouteCandidateId = 0;
		Beat.Role = ResolveBeatRole(Task.Type);
		Beat.RoadPortalCellId = Task.RoadPortalCellId;
		Beat.BiomeDistrictId =
			BiomeDistrictForCell(CellStates, Task.RoadPortalCellId);
		Beat.ProgressDistanceCM = Task.RouteProgressDistanceCM;
		Beat.FlowS = Task.FlowS;
		Beat.Resolution =
			EABTSM3SchemaResolution::CompatibilityObserved;
	}

	for (const FABTSM3RouteBeatPlan& Beat : OutSchema.RouteBeats)
	{
		const FABTSM3TaskNode* Task = Tasks.FindByPredicate(
			[TaskId = Beat.MissionTaskId](const FABTSM3TaskNode& Candidate)
			{
				return Candidate.TaskId == TaskId;
			});
		if (Task == nullptr || !IsOrdinaryBuildingTask(Task->Type))
		{
			continue;
		}

		FABTSM3EncounterContract& Encounter =
			OutSchema.Encounters.AddDefaulted_GetRef();
		Encounter.EncounterId = MakeEncounterId(Task->TaskId);
		Encounter.OrderIndex = OutSchema.Encounters.Num() - 1;
		Encounter.MissionTaskId = Task->TaskId;
		Encounter.RouteBeatId = Beat.BeatId;
		Encounter.Role = EABTSM3EncounterRole::DestructibleTarget;
		Encounter.BuildingPurpose = ResolveBuildingPurpose(Task->Type);
		Encounter.DifficultyBand = Encounter.OrderIndex;
		Encounter.ProgressDistanceCM = Task->RouteProgressDistanceCM;
		Encounter.FlowS = Task->FlowS;
		Encounter.BiomeDistrictId = BiomeDistrictForCell(
			CellStates,
			Task->BuildingAnchorCellId);
		Encounter.ProfileCatalogHash =
			Config.M7ProfileCatalogHash;
		Encounter.Resolution =
			EABTSM3SchemaResolution::CompatibilityObserved;

		for (const FABTSM3TaskLink& Link : Links)
		{
			if (Link.TaskB == Task->TaskId)
			{
				AddUniqueSortedProgressKey(
					Encounter.RequiredKeys,
					Link.RequiredKey);
			}
		}

		for (int32 PocketRoleValue = 0;
			PocketRoleValue <= static_cast<int32>(EABTSM3PocketRole::Exit);
			++PocketRoleValue)
		{
			const EABTSM3PocketRole PocketRole =
				static_cast<EABTSM3PocketRole>(PocketRoleValue);
			FABTSM3PocketContract& Pocket =
				OutSchema.Pockets.AddDefaulted_GetRef();
			Pocket.PocketId =
				MakePocketId(Task->TaskId, PocketRole);
			Pocket.EncounterId = Encounter.EncounterId;
			Pocket.RouteBeatId = Encounter.RouteBeatId;
			Pocket.Role = PocketRole;
			switch (PocketRole)
			{
			case EABTSM3PocketRole::RoadArrival:
				Pocket.AnchorCellId = Task->RoadPortalCellId;
				break;
			case EABTSM3PocketRole::TargetAnchor:
				Pocket.AnchorCellId = Task->BuildingAnchorCellId;
				break;
			default:
				break;
			}
			if (Pocket.AnchorCellId != INDEX_NONE)
			{
				Pocket.CellIds.Add(Pocket.AnchorCellId);
				Pocket.BiomeDistrictId = BiomeDistrictForCell(
					CellStates,
					Pocket.AnchorCellId);
				Pocket.Resolution =
					EABTSM3SchemaResolution::CompatibilityObserved;
			}
			if (PocketRole == EABTSM3PocketRole::TargetEnvelope)
			{
				for (int32 CellId = 0;
					CellId < CellStates.Num();
					++CellId)
				{
					const FABTSM3CellState& State = CellStates[CellId];
					if (State.TaskId == Task->TaskId
						&& (State.bBuildingRoadExclusion
							|| State.bBuildingAnchor))
					{
						Pocket.CellIds.Add(CellId);
					}
				}
				Pocket.CellIds.Sort();
				Pocket.CellIds.SetNum(Algo::Unique(Pocket.CellIds));
				if (!Pocket.CellIds.IsEmpty())
				{
					Pocket.AnchorCellId = Task->BuildingAnchorCellId;
					Pocket.BiomeDistrictId = BiomeDistrictForCell(
						CellStates,
						Task->BuildingAnchorCellId);
					Pocket.Resolution =
						EABTSM3SchemaResolution::CompatibilityObserved;
				}
			}
		}

		Encounter.RoadArrivalPocketId = MakePocketId(
			Task->TaskId,
			EABTSM3PocketRole::RoadArrival);
		Encounter.ScoutRevealPocketId = MakePocketId(
			Task->TaskId,
			EABTSM3PocketRole::ScoutReveal);
		Encounter.SlingshotPocketId = MakePocketId(
			Task->TaskId,
			EABTSM3PocketRole::Slingshot);
		Encounter.TargetEnvelopePocketId = MakePocketId(
			Task->TaskId,
			EABTSM3PocketRole::TargetEnvelope);
		Encounter.TargetAnchorPocketId = MakePocketId(
			Task->TaskId,
			EABTSM3PocketRole::TargetAnchor);
		Encounter.RewardPocketId = MakePocketId(
			Task->TaskId,
			EABTSM3PocketRole::Reward);
		Encounter.ExitPocketId = MakePocketId(
			Task->TaskId,
			EABTSM3PocketRole::Exit);

		FABTSM3RouteBeatPlan* MutableBeat =
			OutSchema.RouteBeats.FindByPredicate(
				[BeatId = Encounter.RouteBeatId](
					const FABTSM3RouteBeatPlan& Candidate)
				{
					return Candidate.BeatId == BeatId;
				});
		if (MutableBeat == nullptr)
		{
			OutFailure = TEXT("EncounterBeatReference");
			OutSchema.Quality.RejectReason =
				EABTSM3SchemaRejectReason::InvalidReference;
			return false;
		}
		MutableBeat->EncounterId = Encounter.EncounterId;
		MutableBeat->RevealPocketId = Encounter.ScoutRevealPocketId;
	}

	for (const FABTSM3EncounterContract& Encounter : OutSchema.Encounters)
	{
		const FABTSM3TaskNode* Task = Tasks.FindByPredicate(
			[TaskId = Encounter.MissionTaskId](
				const FABTSM3TaskNode& Candidate)
			{
				return Candidate.TaskId == TaskId;
			});
		if (Task == nullptr)
		{
			OutFailure = TEXT("EncounterTaskReference");
			OutSchema.Quality.RejectReason =
				EABTSM3SchemaRejectReason::InvalidReference;
			return false;
		}

		FABTSM3PlayableEnvelope& Envelope =
			OutSchema.PlayableEnvelopes.AddDefaulted_GetRef();
		Envelope.EnvelopeId = MakeEnvelopeId(Encounter.MissionTaskId);
		Envelope.RouteBeatId = Encounter.RouteBeatId;
		Envelope.EncounterId = Encounter.EncounterId;
		Envelope.MinProgressDistanceCM = Encounter.ProgressDistanceCM;
		Envelope.MaxProgressDistanceCM = Encounter.ProgressDistanceCM;
		Envelope.Resolution =
			EABTSM3SchemaResolution::CompatibilityObserved;

		TMap<int32, int32> RolesByCell;
		for (int32 CellId = 0; CellId < CellStates.Num(); ++CellId)
		{
			const FABTSM3CellState& State = CellStates[CellId];
			if (State.TaskId != Task->TaskId)
			{
				continue;
			}
			if (State.bRoad)
			{
				AddOrCombinePlayableRole(
					RolesByCell,
					CellId,
					EABTSM3ActiveRole::Route);
			}
			if (State.bBuildingRoadExclusion || State.bBuildingAnchor)
			{
				AddOrCombinePlayableRole(
					RolesByCell,
					CellId,
					EABTSM3ActiveRole::Target);
			}
		}
		AddOrCombinePlayableRole(
			RolesByCell,
			Task->RoadPortalCellId,
			EABTSM3ActiveRole::Route);
		AddOrCombinePlayableRole(
			RolesByCell,
			Task->RoadPortalCellId,
			EABTSM3ActiveRole::RoadArrival);

		TArray<int32> EnvelopeCellIds;
		RolesByCell.GenerateKeyArray(EnvelopeCellIds);
		EnvelopeCellIds.Sort();
		for (const int32 CellId : EnvelopeCellIds)
		{
			FABTSM3PlayableCellRole& CellRole =
				Envelope.Cells.AddDefaulted_GetRef();
			CellRole.CellId = CellId;
			CellRole.ActiveRoleMask = RolesByCell.FindChecked(CellId);
			CellRole.BiomeDistrictId =
				BiomeDistrictForCell(CellStates, CellId);
		}
	}

	OutSchema.Quality.RouteCandidateCount = 1;
	OutSchema.Quality.BeatCount = OutSchema.RouteBeats.Num();
	OutSchema.Quality.EncounterCount = OutSchema.Encounters.Num();
	OutSchema.Quality.PocketCount = OutSchema.Pockets.Num();
	OutSchema.Quality.BiomeDistrictCount =
		OutSchema.BiomeDistricts.Num();
	OutSchema.Quality.PlayableEnvelopeCount =
		OutSchema.PlayableEnvelopes.Num();
	for (const FABTSM3PlayableEnvelope& Envelope :
		OutSchema.PlayableEnvelopes)
	{
		OutSchema.Quality.PlayableCellAssignmentCount +=
			Envelope.Cells.Num();
		for (const FABTSM3PlayableCellRole& Cell : Envelope.Cells)
		{
			OutSchema.Quality.ActiveRoleCellCount +=
				Cell.ActiveRoleMask != 0 ? 1 : 0;
			OutSchema.Quality.DeepWildCellCount +=
				Cell.ActiveRoleMask == 0 ? 1 : 0;
		}
	}
	OutSchema.Quality.bSchemaValid = true;
	OutSchema.Identity.SchemaLayoutHash = static_cast<int64>(
		ComputeLayoutHash(OutSchema));

	EABTSM3SchemaRejectReason ValidationReason =
		EABTSM3SchemaRejectReason::None;
	if (!Validate(OutSchema, ValidationReason, OutFailure))
	{
		OutSchema.Quality.bSchemaValid = false;
		OutSchema.Quality.RejectReason = ValidationReason;
		OutSchema.Identity.SchemaLayoutHash = static_cast<int64>(
			ComputeLayoutHash(OutSchema));
		return false;
	}

	if (Config.bEmitLayerLogs)
	{
		LogLayerSummaries(OutSchema);
	}
	return true;
}

bool FABTSM3MonthlySchemaBuilder::Validate(
	const FABTSM3MonthlyWorldSchema& Schema,
	EABTSM3SchemaRejectReason& OutReason,
	FString& OutFailure)
{
	using namespace ABTSM3R1SchemaPrivate;
	OutReason = EABTSM3SchemaRejectReason::None;
	OutFailure.Reset();
	const auto Fail = [&OutReason, &OutFailure](
		const EABTSM3SchemaRejectReason Reason,
		const FString& Failure)
	{
		OutReason = Reason;
		OutFailure = Failure;
		return false;
	};

	const FABTSM3MonthlySchemaIdentity& Identity = Schema.Identity;
	const bool bPolicyIdentityValid =
		Identity.Mode == EABTSM3GenerationMode::CompatibilityOracle
			? Identity.LayoutPolicyVersion
				== CompatibilityLayoutPolicyVersion
			: Identity.LayoutPolicyVersion
				>= FirstMonthlyLayoutPolicyVersion
				&& Identity.LayoutPolicyVersion <= 255;
	if (Identity.SchemaVersion != SchemaVersion
		|| Identity.GeneratorVersion != GeneratorVersion
		|| !IsValidGenerationMode(Identity.Mode)
		|| !bPolicyIdentityValid
		|| Identity.SourceConfigHash == 0
		|| Identity.SourceLayoutHash == 0
		|| Identity.SchemaConfigHash == 0)
	{
		return Fail(
			EABTSM3SchemaRejectReason::InvalidModeIdentity,
			TEXT("Identity"));
	}
	if (!Schema.Quality.bSchemaValid
		|| Schema.Quality.bMonthlyWorldAccepted
		|| Schema.Quality.RejectReason
			!= EABTSM3SchemaRejectReason::NotEvaluated
		|| !IsValidRejectReason(Schema.Quality.RejectReason)
		|| Schema.Quality.SourceCellCount < 0
		|| !FMath::IsFinite(Schema.Quality.MainRouteLengthCM)
		|| Schema.Quality.MainRouteLengthCM < 0.0f
		|| Schema.Quality.RouteCandidateCount < 0)
	{
		return Fail(
			EABTSM3SchemaRejectReason::InvalidRange,
			TEXT("QualityDomain"));
	}

	const bool bEmptyObservation =
		Schema.RouteBeats.IsEmpty()
		&& Schema.Encounters.IsEmpty()
		&& Schema.Pockets.IsEmpty()
		&& Schema.BiomeDistricts.IsEmpty()
		&& Schema.PlayableEnvelopes.IsEmpty();
	if (bEmptyObservation
		&& Identity.Mode != EABTSM3GenerationMode::CompatibilityOracle)
	{
		return Fail(
			EABTSM3SchemaRejectReason::InvalidModeIdentity,
			TEXT("MonthlyEmptyObservation"));
	}

	TSet<int32> BeatIds;
	TMap<int32, const FABTSM3RouteBeatPlan*> BeatsById;
	TSet<int32> MissionTaskIds;
	int64 PreviousBeatProgress = MIN_int64;
	for (int32 Index = 0; Index < Schema.RouteBeats.Num(); ++Index)
	{
		const FABTSM3RouteBeatPlan& Beat = Schema.RouteBeats[Index];
		const int64 Progress =
			QuantizeCentimeters(Beat.ProgressDistanceCM);
		if (Beat.BeatId < 0
			|| Beat.OrderIndex != Index
			|| Beat.MissionTaskId < 0
			|| Beat.BeatId != MakeRouteBeatId(Beat.MissionTaskId)
			|| Beat.RouteCandidateId < 0
			|| Beat.RouteCandidateId
				>= Schema.Quality.RouteCandidateCount
			|| BeatIds.Contains(Beat.BeatId)
			|| MissionTaskIds.Contains(Beat.MissionTaskId)
			|| !IsValidBeatRole(Beat.Role)
			|| !IsValidResolution(Beat.Resolution)
			|| !FMath::IsFinite(Beat.ProgressDistanceCM)
			|| Beat.ProgressDistanceCM < 0.0f
			|| !FMath::IsFinite(Beat.FlowS)
			|| Beat.FlowS < 0.0f
			|| Beat.FlowS > 1.0f
			|| Progress < PreviousBeatProgress
			|| Beat.RoadPortalCellId < 0
			|| Beat.RoadPortalCellId >= Schema.Quality.SourceCellCount
			|| Beat.BiomeDistrictId < 0
			|| Beat.EncounterId < INDEX_NONE
			|| Beat.RevealPocketId < INDEX_NONE
			|| Beat.WitnessId < INDEX_NONE)
		{
			return Fail(
				BeatIds.Contains(Beat.BeatId)
					|| MissionTaskIds.Contains(Beat.MissionTaskId)
						? EABTSM3SchemaRejectReason::DuplicateStableId
						: EABTSM3SchemaRejectReason::NonDeterministicOrder,
				FString::Printf(TEXT("Beat:%d"), Index));
		}
		BeatIds.Add(Beat.BeatId);
		BeatsById.Add(Beat.BeatId, &Beat);
		MissionTaskIds.Add(Beat.MissionTaskId);
		PreviousBeatProgress = Progress;
	}

	TSet<int32> EncounterIds;
	TMap<int32, const FABTSM3EncounterContract*> EncountersById;
	int64 PreviousEncounterProgress = MIN_int64;
	for (int32 Index = 0; Index < Schema.Encounters.Num(); ++Index)
	{
		const FABTSM3EncounterContract& Encounter =
			Schema.Encounters[Index];
		const int64 Progress =
			QuantizeCentimeters(Encounter.ProgressDistanceCM);
		if (Encounter.EncounterId < 0
			|| Encounter.OrderIndex != Index
			|| Encounter.EncounterId
				!= MakeEncounterId(Encounter.MissionTaskId)
			|| EncounterIds.Contains(Encounter.EncounterId)
			|| !MissionTaskIds.Contains(Encounter.MissionTaskId)
			|| !BeatIds.Contains(Encounter.RouteBeatId)
			|| !IsValidEncounterRole(Encounter.Role)
			|| !IsValidBuildingPurpose(Encounter.BuildingPurpose)
			|| !IsValidResolution(Encounter.Resolution)
			|| Encounter.DifficultyBand < 0
			|| !FMath::IsFinite(Encounter.ProgressDistanceCM)
			|| Encounter.ProgressDistanceCM < 0.0f
			|| !FMath::IsFinite(Encounter.FlowS)
			|| Encounter.FlowS < 0.0f
			|| Encounter.FlowS > 1.0f
			|| Progress < PreviousEncounterProgress
			|| !IsStrictlyAscendingEnum(Encounter.RequiredKeys)
			|| !IsStrictlyAscendingEnum(Encounter.GrantedKeys)
			|| !AreValidProgressKeys(Encounter.RequiredKeys)
			|| !AreValidProgressKeys(Encounter.GrantedKeys)
			|| Encounter.BiomeDistrictId < 0
			|| Encounter.BallisticWitnessId < INDEX_NONE)
		{
			return Fail(
				EncounterIds.Contains(Encounter.EncounterId)
					? EABTSM3SchemaRejectReason::DuplicateStableId
					: EABTSM3SchemaRejectReason::InvalidReference,
				FString::Printf(TEXT("Encounter:%d"), Index));
		}
		const FABTSM3RouteBeatPlan* Beat =
			BeatsById.FindRef(Encounter.RouteBeatId);
		if (Beat == nullptr
			|| Beat->MissionTaskId != Encounter.MissionTaskId
			|| Beat->EncounterId != Encounter.EncounterId)
		{
			return Fail(
				EABTSM3SchemaRejectReason::InvalidReference,
				FString::Printf(TEXT("EncounterBeat:%d"), Index));
		}
		EncounterIds.Add(Encounter.EncounterId);
		EncountersById.Add(Encounter.EncounterId, &Encounter);
		PreviousEncounterProgress = Progress;
	}

	TSet<int32> PocketIds;
	TMap<int32, const FABTSM3PocketContract*> PocketsById;
	int32 PreviousPocketEncounterOrder = INDEX_NONE;
	int32 PreviousPocketRole = INDEX_NONE;
	for (int32 Index = 0; Index < Schema.Pockets.Num(); ++Index)
	{
		const FABTSM3PocketContract& Pocket = Schema.Pockets[Index];
		const FABTSM3EncounterContract* Owner =
			EncountersById.FindRef(Pocket.EncounterId);
		const int32 OwnerOrder =
			Owner != nullptr ? Owner->OrderIndex : INDEX_NONE;
		const int32 RoleValue = static_cast<int32>(Pocket.Role);
		const bool bCanonicalPocketOrder =
			OwnerOrder > PreviousPocketEncounterOrder
			|| (OwnerOrder == PreviousPocketEncounterOrder
				&& RoleValue > PreviousPocketRole);
		if (Pocket.PocketId < 0
			|| PocketIds.Contains(Pocket.PocketId)
			|| Owner == nullptr
			|| !BeatIds.Contains(Pocket.RouteBeatId)
			|| !IsValidPocketRole(Pocket.Role)
			|| !IsValidResolution(Pocket.Resolution)
			|| !IsStrictlyAscending(Pocket.CellIds)
			|| !bCanonicalPocketOrder
			|| Pocket.RouteBeatId != Owner->RouteBeatId
			|| Pocket.PocketId
				!= MakePocketId(Owner->MissionTaskId, Pocket.Role)
			|| (Pocket.AnchorCellId != INDEX_NONE
				&& (Pocket.AnchorCellId < 0
					|| Pocket.AnchorCellId
						>= Schema.Quality.SourceCellCount))
			|| (Pocket.BiomeDistrictId != INDEX_NONE
				&& Pocket.BiomeDistrictId < 0))
		{
			return Fail(
				PocketIds.Contains(Pocket.PocketId)
					? EABTSM3SchemaRejectReason::DuplicateStableId
					: EABTSM3SchemaRejectReason::NonDeterministicOrder,
				FString::Printf(TEXT("Pocket:%d"), Index));
		}
		for (const int32 CellId : Pocket.CellIds)
		{
			if (CellId < 0 || CellId >= Schema.Quality.SourceCellCount)
			{
				return Fail(
					EABTSM3SchemaRejectReason::InvalidReference,
					FString::Printf(TEXT("PocketCell:%d"), CellId));
			}
		}
		PocketIds.Add(Pocket.PocketId);
		PocketsById.Add(Pocket.PocketId, &Pocket);
		PreviousPocketEncounterOrder = OwnerOrder;
		PreviousPocketRole = RoleValue;
	}

	for (const FABTSM3EncounterContract& Encounter : Schema.Encounters)
	{
		const int32 References[] = {
			Encounter.RoadArrivalPocketId,
			Encounter.ScoutRevealPocketId,
			Encounter.SlingshotPocketId,
			Encounter.TargetEnvelopePocketId,
			Encounter.TargetAnchorPocketId,
			Encounter.RewardPocketId,
			Encounter.ExitPocketId
		};
		for (int32 RoleIndex = 0;
			RoleIndex <= static_cast<int32>(EABTSM3PocketRole::Exit);
			++RoleIndex)
		{
			const int32 PocketId = References[RoleIndex];
			const FABTSM3PocketContract* Pocket =
				PocketsById.FindRef(PocketId);
			if (Pocket == nullptr
				|| Pocket->EncounterId != Encounter.EncounterId
				|| Pocket->RouteBeatId != Encounter.RouteBeatId
				|| static_cast<int32>(Pocket->Role) != RoleIndex)
			{
				return Fail(
					EABTSM3SchemaRejectReason::InvalidReference,
					FString::Printf(
						TEXT("EncounterPocket:%d:%d"),
						Encounter.EncounterId,
						PocketId));
			}
		}
	}

	for (const FABTSM3RouteBeatPlan& Beat : Schema.RouteBeats)
	{
		if (Beat.EncounterId == INDEX_NONE)
		{
			if (Beat.RevealPocketId != INDEX_NONE)
			{
				return Fail(
					EABTSM3SchemaRejectReason::InvalidReference,
					FString::Printf(
						TEXT("BeatRevealWithoutEncounter:%d"),
						Beat.BeatId));
			}
			continue;
		}
		const FABTSM3EncounterContract* Encounter =
			EncountersById.FindRef(Beat.EncounterId);
		const FABTSM3PocketContract* Reveal =
			PocketsById.FindRef(Beat.RevealPocketId);
		if (Encounter == nullptr
			|| Encounter->RouteBeatId != Beat.BeatId
			|| Encounter->MissionTaskId != Beat.MissionTaskId
			|| Reveal == nullptr
			|| Reveal->EncounterId != Encounter->EncounterId
			|| Reveal->Role != EABTSM3PocketRole::ScoutReveal)
		{
			return Fail(
				EABTSM3SchemaRejectReason::InvalidReference,
				FString::Printf(TEXT("BeatEncounter:%d"), Beat.BeatId));
		}
	}

	TSet<int32> DistrictIds;
	TArray<int32> CellCoverage;
	CellCoverage.Init(0, Schema.Quality.SourceCellCount);
	TArray<int32> DistrictIdByCell;
	DistrictIdByCell.Init(INDEX_NONE, Schema.Quality.SourceCellCount);
	int32 PreviousDistrictId = INDEX_NONE;
	for (int32 Index = 0; Index < Schema.BiomeDistricts.Num(); ++Index)
	{
		const FABTSM3BiomeDistrict& District =
			Schema.BiomeDistricts[Index];
		if (District.BiomeDistrictId <= PreviousDistrictId
			|| DistrictIds.Contains(District.BiomeDistrictId)
			|| District.BiomeDistrictId
				!= MakeBiomeDistrictId(District.ObservedTerrainType)
			|| !IsValidBiomeArchetype(District.Archetype)
			|| !IsValidTerrainType(District.ObservedTerrainType)
			|| !IsValidResolution(District.Resolution)
			|| !IsStrictlyAscending(District.CellIds)
			|| !FMath::IsFinite(District.MinProgressDistanceCM)
			|| !FMath::IsFinite(District.MaxProgressDistanceCM)
			|| District.MinProgressDistanceCM
				> District.MaxProgressDistanceCM
			|| !FMath::IsFinite(District.MinFlowS)
			|| !FMath::IsFinite(District.MaxFlowS)
			|| District.MinFlowS < 0.0f
			|| District.MaxFlowS > 1.0f
			|| District.MinFlowS > District.MaxFlowS)
		{
			return Fail(
				DistrictIds.Contains(District.BiomeDistrictId)
					? EABTSM3SchemaRejectReason::DuplicateStableId
					: EABTSM3SchemaRejectReason::NonDeterministicOrder,
				FString::Printf(TEXT("Biome:%d"), Index));
		}
		for (const int32 CellId : District.CellIds)
		{
			if (!CellCoverage.IsValidIndex(CellId))
			{
				return Fail(
					EABTSM3SchemaRejectReason::InvalidReference,
					FString::Printf(TEXT("BiomeCell:%d"), CellId));
			}
			++CellCoverage[CellId];
			if (DistrictIdByCell[CellId] == INDEX_NONE)
			{
				DistrictIdByCell[CellId] = District.BiomeDistrictId;
			}
		}
		DistrictIds.Add(District.BiomeDistrictId);
		PreviousDistrictId = District.BiomeDistrictId;
	}
	if (!bEmptyObservation)
	{
		for (int32 CellId = 0; CellId < CellCoverage.Num(); ++CellId)
		{
			if (CellCoverage[CellId] != 1)
			{
				return Fail(
					EABTSM3SchemaRejectReason::IncompleteBiomeCoverage,
					FString::Printf(TEXT("BiomeCoverage:%d"), CellId));
			}
		}
	}

	for (const FABTSM3RouteBeatPlan& Beat : Schema.RouteBeats)
	{
		if (!DistrictIds.Contains(Beat.BiomeDistrictId)
			|| !DistrictIdByCell.IsValidIndex(Beat.RoadPortalCellId)
			|| DistrictIdByCell[Beat.RoadPortalCellId]
				!= Beat.BiomeDistrictId)
		{
			return Fail(
				EABTSM3SchemaRejectReason::InvalidReference,
				FString::Printf(TEXT("BeatBiome:%d"), Beat.BeatId));
		}
	}
	for (const FABTSM3EncounterContract& Encounter : Schema.Encounters)
	{
		const FABTSM3PocketContract* TargetAnchor =
			PocketsById.FindRef(Encounter.TargetAnchorPocketId);
		if (!DistrictIds.Contains(Encounter.BiomeDistrictId)
			|| TargetAnchor == nullptr
			|| !DistrictIdByCell.IsValidIndex(
				TargetAnchor->AnchorCellId)
			|| DistrictIdByCell[TargetAnchor->AnchorCellId]
				!= Encounter.BiomeDistrictId)
		{
			return Fail(
				EABTSM3SchemaRejectReason::InvalidReference,
				FString::Printf(
					TEXT("EncounterBiome:%d"),
					Encounter.EncounterId));
		}
	}
	for (const FABTSM3PocketContract& Pocket : Schema.Pockets)
	{
		const bool bHasBiome = Pocket.BiomeDistrictId != INDEX_NONE;
		if ((bHasBiome
				&& !DistrictIds.Contains(Pocket.BiomeDistrictId))
			|| (!bHasBiome
				&& (Pocket.AnchorCellId != INDEX_NONE
					|| !Pocket.CellIds.IsEmpty()))
			|| (Pocket.AnchorCellId != INDEX_NONE
				&& DistrictIdByCell[Pocket.AnchorCellId]
					!= Pocket.BiomeDistrictId))
		{
			return Fail(
				EABTSM3SchemaRejectReason::InvalidReference,
				FString::Printf(TEXT("PocketBiome:%d"), Pocket.PocketId));
		}
		for (const int32 CellId : Pocket.CellIds)
		{
			if (DistrictIdByCell[CellId] != Pocket.BiomeDistrictId)
			{
				return Fail(
					EABTSM3SchemaRejectReason::InvalidReference,
					FString::Printf(
						TEXT("PocketBiomeCell:%d:%d"),
						Pocket.PocketId,
						CellId));
			}
		}
	}

	TSet<int32> EnvelopeIds;
	int32 CountedPlayableCells = 0;
	int32 CountedActiveRoleCells = 0;
	int32 CountedDeepWildCells = 0;
	for (int32 Index = 0; Index < Schema.PlayableEnvelopes.Num(); ++Index)
	{
		const FABTSM3PlayableEnvelope& Envelope =
			Schema.PlayableEnvelopes[Index];
		const FABTSM3EncounterContract* Encounter =
			EncountersById.FindRef(Envelope.EncounterId);
		if (Envelope.EnvelopeId < 0
			|| EnvelopeIds.Contains(Envelope.EnvelopeId)
			|| Encounter == nullptr
			|| Encounter->OrderIndex != Index
			|| Envelope.EnvelopeId
				!= MakeEnvelopeId(Encounter->MissionTaskId)
			|| Envelope.RouteBeatId != Encounter->RouteBeatId
			|| !IsValidResolution(Envelope.Resolution)
			|| !FMath::IsFinite(Envelope.MinProgressDistanceCM)
			|| !FMath::IsFinite(Envelope.MaxProgressDistanceCM)
			|| Envelope.MinProgressDistanceCM
				> Envelope.MaxProgressDistanceCM)
		{
			return Fail(
				EnvelopeIds.Contains(Envelope.EnvelopeId)
					? EABTSM3SchemaRejectReason::DuplicateStableId
					: EABTSM3SchemaRejectReason::InvalidReference,
				FString::Printf(TEXT("Envelope:%d"), Index));
		}
		int32 PreviousCellId = INDEX_NONE;
		for (const FABTSM3PlayableCellRole& Cell : Envelope.Cells)
		{
			if (Cell.CellId <= PreviousCellId
				|| Cell.CellId < 0
				|| Cell.CellId >= Schema.Quality.SourceCellCount
				|| Cell.ActiveRoleMask < 0
				|| (Cell.ActiveRoleMask & ~KnownActiveRoleMask) != 0
				|| !DistrictIds.Contains(Cell.BiomeDistrictId)
				|| DistrictIdByCell[Cell.CellId]
					!= Cell.BiomeDistrictId)
			{
				return Fail(
					EABTSM3SchemaRejectReason::NonDeterministicOrder,
					FString::Printf(TEXT("EnvelopeCell:%d"), Cell.CellId));
			}
			PreviousCellId = Cell.CellId;
			++CountedPlayableCells;
			CountedActiveRoleCells += Cell.ActiveRoleMask != 0 ? 1 : 0;
			CountedDeepWildCells += Cell.ActiveRoleMask == 0 ? 1 : 0;
		}
		EnvelopeIds.Add(Envelope.EnvelopeId);
	}

	if (Schema.Quality.BeatCount != Schema.RouteBeats.Num()
		|| Schema.Quality.RouteCandidateCount
			!= (bEmptyObservation ? 0 : 1)
		|| Schema.Quality.EncounterCount != Schema.Encounters.Num()
		|| Schema.Quality.PocketCount != Schema.Pockets.Num()
		|| Schema.Quality.BiomeDistrictCount
			!= Schema.BiomeDistricts.Num()
		|| Schema.Quality.PlayableEnvelopeCount
			!= Schema.PlayableEnvelopes.Num()
		|| Schema.Quality.PlayableCellAssignmentCount
			!= CountedPlayableCells
		|| Schema.Quality.ActiveRoleCellCount
			!= CountedActiveRoleCells
		|| Schema.Quality.DeepWildCellCount
			!= CountedDeepWildCells)
	{
		return Fail(
			EABTSM3SchemaRejectReason::InvalidRange,
			TEXT("QualityCounts"));
	}

	const uint64 ExpectedHash = ComputeLayoutHash(Schema);
	if (static_cast<uint64>(Identity.SchemaLayoutHash) != ExpectedHash)
	{
		return Fail(
			EABTSM3SchemaRejectReason::LayoutHashMismatch,
			TEXT("SchemaLayoutHash"));
	}
	return true;
}

void FABTSM3MonthlySchemaBuilder::BuildDebugData(
	const FABTSM3MonthlyWorldSchema& Schema,
	FABTSM3MonthlySchemaDebugData& OutDebugData)
{
	OutDebugData = FABTSM3MonthlySchemaDebugData();
	for (const FABTSM3RouteBeatPlan& Beat : Schema.RouteBeats)
	{
		if (Beat.RoadPortalCellId != INDEX_NONE)
		{
			OutDebugData.RoutePortalCellIds.AddUnique(
				Beat.RoadPortalCellId);
		}
	}
	for (const FABTSM3PocketContract& Pocket : Schema.Pockets)
	{
		if (Pocket.AnchorCellId == INDEX_NONE)
		{
			continue;
		}
		if (Pocket.Role == EABTSM3PocketRole::ScoutReveal)
		{
			OutDebugData.RevealCellIds.AddUnique(Pocket.AnchorCellId);
		}
		else if (Pocket.Role == EABTSM3PocketRole::TargetAnchor)
		{
			OutDebugData.TargetCellIds.AddUnique(Pocket.AnchorCellId);
		}
	}
	for (const FABTSM3PlayableEnvelope& Envelope :
		Schema.PlayableEnvelopes)
	{
		for (const FABTSM3PlayableCellRole& Cell : Envelope.Cells)
		{
			OutDebugData.PlayableEnvelopeCellIds.AddUnique(Cell.CellId);
		}
	}
	for (const FABTSM3BiomeDistrict& District : Schema.BiomeDistricts)
	{
		OutDebugData.BiomeDistrictIds.Add(District.BiomeDistrictId);
	}
	OutDebugData.RoutePortalCellIds.Sort();
	OutDebugData.RevealCellIds.Sort();
	OutDebugData.TargetCellIds.Sort();
	OutDebugData.PlayableEnvelopeCellIds.Sort();
	OutDebugData.BiomeDistrictIds.Sort();
	OutDebugData.LastRejectReason = Schema.Quality.RejectReason;
}

void FABTSM3MonthlySchemaBuilder::LogLayerSummaries(
	const FABTSM3MonthlyWorldSchema& Schema)
{
	const FABTSM3MonthlySchemaIdentity& Identity = Schema.Identity;
	const FABTSM3WorldQualityReport& Quality = Schema.Quality;
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][PCG][Route] Stage=M3R1 Mode=%s Policy=%d Beats=%d RouteCandidates=%d MainLengthCM=%.1f SchemaLayoutHash=%016llX"),
		GetGenerationModeName(Identity.Mode),
		Identity.LayoutPolicyVersion,
		Schema.RouteBeats.Num(),
		Quality.RouteCandidateCount,
		Quality.MainRouteLengthCM,
		static_cast<unsigned long long>(
			static_cast<uint64>(Identity.SchemaLayoutHash)));
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][PCG][Encounter] Stage=M3R1 Mode=%s Encounters=%d Pockets=%d ResolvedProfiles=0 Witnesses=0 ProfileCatalogHash=0"),
		GetGenerationModeName(Identity.Mode),
		Schema.Encounters.Num(),
		Schema.Pockets.Num());
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][PCG][Biome] Stage=M3R1 Mode=%s Districts=%d SourceCells=%d PlayableEnvelopes=%d PlayableAssignments=%d ActiveRoleCells=%d DeepWildCells=%d"),
		GetGenerationModeName(Identity.Mode),
		Schema.BiomeDistricts.Num(),
		Quality.SourceCellCount,
		Schema.PlayableEnvelopes.Num(),
		Quality.PlayableCellAssignmentCount,
		Quality.ActiveRoleCellCount,
		Quality.DeepWildCellCount);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][PCG][Quality] Stage=M3R1 Mode=%s SchemaValid=%d MonthlyWorldAccepted=%d RejectReason=%s SchemaConfigHash=%016llX SchemaLayoutHash=%016llX"),
		GetGenerationModeName(Identity.Mode),
		Quality.bSchemaValid ? 1 : 0,
		Quality.bMonthlyWorldAccepted ? 1 : 0,
		GetRejectReasonName(Quality.RejectReason),
		static_cast<unsigned long long>(
			static_cast<uint64>(Identity.SchemaConfigHash)),
		static_cast<unsigned long long>(
			static_cast<uint64>(Identity.SchemaLayoutHash)));
}

const TCHAR* FABTSM3MonthlySchemaBuilder::GetGenerationModeName(
	const EABTSM3GenerationMode Mode)
{
	switch (Mode)
	{
	case EABTSM3GenerationMode::CompatibilityOracle:
		return TEXT("CompatibilityOracle");
	case EABTSM3GenerationMode::MonthlyDevelopment:
		return TEXT("MonthlyDevelopment");
	default:
		return TEXT("Invalid");
	}
}

const TCHAR* FABTSM3MonthlySchemaBuilder::GetRejectReasonName(
	const EABTSM3SchemaRejectReason Reason)
{
	switch (Reason)
	{
	case EABTSM3SchemaRejectReason::None:
		return TEXT("None");
	case EABTSM3SchemaRejectReason::NotEvaluated:
		return TEXT("NotEvaluated");
	case EABTSM3SchemaRejectReason::SourceWorldRejected:
		return TEXT("SourceWorldRejected");
	case EABTSM3SchemaRejectReason::InvalidModeIdentity:
		return TEXT("InvalidModeIdentity");
	case EABTSM3SchemaRejectReason::InvalidRange:
		return TEXT("InvalidRange");
	case EABTSM3SchemaRejectReason::DuplicateStableId:
		return TEXT("DuplicateStableId");
	case EABTSM3SchemaRejectReason::InvalidReference:
		return TEXT("InvalidReference");
	case EABTSM3SchemaRejectReason::NonDeterministicOrder:
		return TEXT("NonDeterministicOrder");
	case EABTSM3SchemaRejectReason::IncompleteBiomeCoverage:
		return TEXT("IncompleteBiomeCoverage");
	case EABTSM3SchemaRejectReason::LayoutHashMismatch:
		return TEXT("LayoutHashMismatch");
	default:
		return TEXT("Invalid");
	}
}
