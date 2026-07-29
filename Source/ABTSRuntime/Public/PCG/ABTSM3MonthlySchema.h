// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCG/ABTSM3TaskGraphTypes.h"
#include "ABTSM3MonthlySchema.generated.h"

/**
 * R-1 only observes an already accepted compatibility world.  MonthlyDevelopment
 * reserves a distinct policy identity for R-2+ and is never publication eligible
 * while the schema resolution is CompatibilityObserved.
 */
UENUM(BlueprintType)
enum class EABTSM3GenerationMode : uint8
{
	CompatibilityOracle = 0,
	MonthlyDevelopment = 1
};

static_assert(
	static_cast<uint8>(EABTSM3GenerationMode::CompatibilityOracle) == 0
	&& static_cast<uint8>(EABTSM3GenerationMode::MonthlyDevelopment) == 1,
	"EABTSM3GenerationMode values are stable M3 monthly-schema identities.");

UENUM(BlueprintType)
enum class EABTSM3SchemaResolution : uint8
{
	Unresolved = 0,
	CompatibilityObserved = 1,
	Reserved = 2,
	Finalized = 3
};

UENUM(BlueprintType)
enum class EABTSM3RouteBeatRole : uint8
{
	Start = 0,
	Travel = 1,
	Reveal = 2,
	Attack = 3,
	Reward = 4,
	Gate = 5,
	Training = 6,
	Finale = 7
};

UENUM(BlueprintType)
enum class EABTSM3EncounterRole : uint8
{
	FacilityShell = 0,
	DestructibleTarget = 1,
	Landmark = 2,
	RewardCache = 3
};

UENUM(BlueprintType)
enum class EABTSM3BuildingPurpose : uint8
{
	None = 0,
	Crafting = 1,
	ProgressionTarget = 2,
	ResourceTarget = 3,
	GravityTraining = 4,
	FinaleSupport = 5
};

UENUM(BlueprintType)
enum class EABTSM3PocketRole : uint8
{
	RoadArrival = 0,
	ScoutReveal = 1,
	Slingshot = 2,
	TargetEnvelope = 3,
	TargetAnchor = 4,
	Reward = 5,
	Exit = 6
};

UENUM(BlueprintType)
enum class EABTSM3BiomeArchetype : uint8
{
	Plain = 0,
	Forest = 1,
	Highland = 2,
	Mountain = 3,
	Water = 4,
	Background = 5
};

UENUM(BlueprintType, meta = (Bitflags))
enum class EABTSM3ActiveRole : uint8
{
	None = 0,
	Route = 1 << 0,
	RoadArrival = 1 << 1,
	Reveal = 1 << 2,
	Slingshot = 1 << 3,
	Target = 1 << 4,
	Reward = 1 << 5,
	Exit = 1 << 6,
	Resource = 1 << 7
};
ENUM_CLASS_FLAGS(EABTSM3ActiveRole);

UENUM(BlueprintType)
enum class EABTSM3SchemaRejectReason : uint8
{
	None = 0,
	NotEvaluated = 1,
	SourceWorldRejected = 2,
	InvalidModeIdentity = 3,
	InvalidRange = 4,
	DuplicateStableId = 5,
	InvalidReference = 6,
	NonDeterministicOrder = 7,
	IncompleteBiomeCoverage = 8,
	LayoutHashMismatch = 9
};

/** Inputs owned by the internal M3 monthly schema. They never enter a v1 shared contract. */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlySchemaConfig
{
	GENERATED_BODY()

	/** CompatibilityOracle keeps Gen3/Policy1 authoritative; MonthlyDevelopment reserves Policy 2+. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema")
	EABTSM3GenerationMode Mode = EABTSM3GenerationMode::CompatibilityOracle;

	/** Reserved policy identity for R-2+; GeneratorVersion remains 3 until Integration approves an upgrade. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema",
		meta = (ClampMin = "2", ClampMax = "255", UIMin = "2", UIMax = "16"))
	int32 MonthlyLayoutPolicyVersion = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Catalog Identity")
	int64 RouteTemplateCatalogHash = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Catalog Identity")
	int64 EncounterTemplateCatalogHash = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Catalog Identity")
	int64 BiomeTemplateCatalogHash = 0;

	/** Integration-owned future read-only M7 descriptor catalog identity. Zero means unavailable in R-1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Catalog Identity")
	int64 M7ProfileCatalogHash = 0;

	/** Integration/M6-owned future predictor identity. Zero means unavailable in R-1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Solver Identity",
		meta = (ClampMin = "0"))
	int32 M6SolverVersion = 0;

	/** Integration/M9-owned future predictor identity. Zero means unavailable in R-1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Solver Identity",
		meta = (ClampMin = "0"))
	int32 M9SolverVersion = 0;

	/** Disabling observation must not alter the compatibility TaskGraph or its shared exports. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Observation")
	bool bBuildObservation = true;

	/** Diagnostic-only switch; deliberately excluded from the deterministic config identity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Observation")
	bool bEmitLayerLogs = true;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlySchemaIdentity
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema")
	int32 SchemaVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema")
	int32 GeneratorVersion = 3;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema")
	int32 LayoutPolicyVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema")
	EABTSM3GenerationMode Mode = EABTSM3GenerationMode::CompatibilityOracle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema")
	int32 WorldSeed = 0;

	/** Frozen Gen3/Policy1 inputs. These remain byte-for-byte compatible with M3R-0. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema")
	int64 SourceConfigHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema")
	int64 SourceLayoutHash = 0;

	/** Hash of R-1 schema inputs, including all future catalog and solver identities. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema")
	int64 SchemaConfigHash = 0;

	/** Complete hash of ordered Beat/Encounter/Pocket/Biome/Envelope/Quality data. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema")
	int64 SchemaLayoutHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3RouteBeatPlan
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Route")
	int32 BeatId = INDEX_NONE;

	/** Explicit canonical sequence; identity and ordering are intentionally separate. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Route")
	int32 OrderIndex = INDEX_NONE;

	/** Independent Mission Task identity; never inferred from BeatId. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Route")
	int32 MissionTaskId = INDEX_NONE;

	/** R-1 observes candidate 0 only; R-2 owns the actual candidate pool. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Route")
	int32 RouteCandidateId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Route")
	EABTSM3RouteBeatRole Role = EABTSM3RouteBeatRole::Travel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Route")
	int32 EncounterId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Route")
	int32 RoadPortalCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Route")
	int32 RevealPocketId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Route")
	int32 WitnessId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Route")
	int32 BiomeDistrictId = INDEX_NONE;

	/** Centimeters along the accepted ordered Start-to-Launch main corridor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Route",
		meta = (Units = "cm"))
	float ProgressDistanceCM = 0.0f;

	/** Static normalized route coordinate in [0,1]. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Route")
	float FlowS = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Route")
	EABTSM3SchemaResolution Resolution = EABTSM3SchemaResolution::Unresolved;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3PocketContract
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Pocket")
	int32 PocketId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Pocket")
	int32 EncounterId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Pocket")
	int32 RouteBeatId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Pocket")
	EABTSM3PocketRole Role = EABTSM3PocketRole::RoadArrival;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Pocket")
	int32 AnchorCellId = INDEX_NONE;

	/** Stable ascending CellIds. Empty is a valid R-1 unresolved pocket. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Pocket")
	TArray<int32> CellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Pocket")
	int32 BiomeDistrictId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Pocket")
	EABTSM3SchemaResolution Resolution = EABTSM3SchemaResolution::Unresolved;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3EncounterContract
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	int32 EncounterId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	int32 OrderIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	int32 MissionTaskId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	int32 RouteBeatId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	EABTSM3EncounterRole Role = EABTSM3EncounterRole::DestructibleTarget;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	EABTSM3BuildingPurpose BuildingPurpose = EABTSM3BuildingPurpose::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	int32 DifficultyBand = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter",
		meta = (Units = "cm"))
	float ProgressDistanceCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	float FlowS = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	TArray<EABTSM3ProgressKey> RequiredKeys;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	TArray<EABTSM3ProgressKey> GrantedKeys;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	int32 RoadArrivalPocketId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	int32 ScoutRevealPocketId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	int32 SlingshotPocketId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	int32 TargetEnvelopePocketId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	int32 TargetAnchorPocketId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	int32 RewardPocketId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	int32 ExitPocketId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	int32 BallisticWitnessId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	int32 BiomeDistrictId = INDEX_NONE;

	/** Future Integration-owned descriptor identity; empty in R-1. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	FName ResolvedM7ProfileId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	int64 ProfileCatalogHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Encounter")
	EABTSM3SchemaResolution Resolution = EABTSM3SchemaResolution::Unresolved;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3BiomeDistrict
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Biome")
	int32 BiomeDistrictId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Biome")
	EABTSM3BiomeArchetype Archetype = EABTSM3BiomeArchetype::Plain;

	/** R-1 groups the legacy terrain observation only; R-3 owns actual district allocation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Biome")
	EABTSM3TerrainType ObservedTerrainType = EABTSM3TerrainType::Plain;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Biome")
	TArray<int32> CellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Biome",
		meta = (Units = "cm"))
	float MinProgressDistanceCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Biome",
		meta = (Units = "cm"))
	float MaxProgressDistanceCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Biome")
	float MinFlowS = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Biome")
	float MaxFlowS = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Biome")
	bool bBackground = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Biome")
	EABTSM3SchemaResolution Resolution = EABTSM3SchemaResolution::Unresolved;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3PlayableCellRole
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Envelope")
	int32 CellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Envelope",
		meta = (Bitmask, BitmaskEnum = "/Script/ABTSRuntime.EABTSM3ActiveRole"))
	int32 ActiveRoleMask = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Envelope")
	int32 BiomeDistrictId = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3PlayableEnvelope
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Envelope")
	int32 EnvelopeId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Envelope")
	int32 RouteBeatId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Envelope")
	int32 EncounterId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Envelope",
		meta = (Units = "cm"))
	float MinProgressDistanceCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Envelope",
		meta = (Units = "cm"))
	float MaxProgressDistanceCM = 0.0f;

	/** Strictly ascending CellIds; roles combine when one Cell serves several local responsibilities. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Envelope")
	TArray<FABTSM3PlayableCellRole> Cells;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Envelope")
	EABTSM3SchemaResolution Resolution = EABTSM3SchemaResolution::Unresolved;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3WorldQualityReport
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Quality")
	bool bSchemaValid = false;

	/** Always false in R-1; no schema observation can publish a monthly world. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Quality")
	bool bMonthlyWorldAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Quality")
	EABTSM3SchemaRejectReason RejectReason = EABTSM3SchemaRejectReason::NotEvaluated;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Quality")
	int32 SourceCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Quality")
	int32 RouteCandidateCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Quality")
	int32 BeatCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Quality")
	int32 EncounterCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Quality")
	int32 PocketCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Quality")
	int32 BiomeDistrictCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Quality")
	int32 PlayableEnvelopeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Quality")
	int32 PlayableCellAssignmentCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Quality")
	int32 ActiveRoleCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Quality")
	int32 DeepWildCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Quality",
		meta = (Units = "cm"))
	float MainRouteLengthCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Quality")
	int32 RouteScore = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Quality")
	int32 EncounterScore = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Quality")
	int32 BiomeScore = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Quality")
	int32 OverallScore = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyWorldSchema
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema")
	FABTSM3MonthlySchemaIdentity Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema")
	TArray<FABTSM3RouteBeatPlan> RouteBeats;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema")
	TArray<FABTSM3EncounterContract> Encounters;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema")
	TArray<FABTSM3PocketContract> Pockets;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema")
	TArray<FABTSM3BiomeDistrict> BiomeDistricts;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema")
	TArray<FABTSM3PlayableEnvelope> PlayableEnvelopes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema")
	FABTSM3WorldQualityReport Quality;
};

/** Editor-facing indices only. R-1 deliberately adds no new rendered overlay. */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlySchemaDebugData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Debug")
	TArray<int32> RoutePortalCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Debug")
	TArray<int32> RevealCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Debug")
	TArray<int32> TargetCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Debug")
	TArray<int32> PlayableEnvelopeCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Debug")
	TArray<int32> BiomeDistrictIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Debug")
	EABTSM3SchemaRejectReason LastRejectReason = EABTSM3SchemaRejectReason::NotEvaluated;
};

/**
 * Deterministic, read-only projection from an accepted Gen3/Policy1 result into
 * the R-1 schema. It does not mutate TaskGraph arrays or shared export data.
 */
class ABTSRUNTIME_API FABTSM3MonthlySchemaBuilder final
{
public:
	static constexpr int32 SchemaVersion = 1;
	static constexpr int32 GeneratorVersion = 3;
	static constexpr int32 CompatibilityLayoutPolicyVersion = 1;
	static constexpr int32 FirstMonthlyLayoutPolicyVersion = 2;

	static int32 ResolveLayoutPolicyVersion(
		const FABTSM3MonthlySchemaConfig& Config);

	static uint64 ComputeConfigHash(
		const FABTSM3MonthlySchemaConfig& Config,
		int64 SourceConfigHash);

	static uint64 ComputeLayoutHash(
		const FABTSM3MonthlyWorldSchema& Schema);

	static bool Build(
		int32 WorldSeed,
		const FABTSM3MonthlySchemaConfig& Config,
		const TArray<FABTSM3TaskNode>& Tasks,
		const TArray<FABTSM3TaskLink>& Links,
		const TArray<FABTSM3CellState>& CellStates,
		const FABTSM3PCGSummary& SourceSummary,
		FABTSM3MonthlyWorldSchema& OutSchema,
		FString& OutFailure);

	static bool Validate(
		const FABTSM3MonthlyWorldSchema& Schema,
		EABTSM3SchemaRejectReason& OutReason,
		FString& OutFailure);

	static void BuildDebugData(
		const FABTSM3MonthlyWorldSchema& Schema,
		FABTSM3MonthlySchemaDebugData& OutDebugData);

	static void LogLayerSummaries(
		const FABTSM3MonthlyWorldSchema& Schema);

	static const TCHAR* GetGenerationModeName(EABTSM3GenerationMode Mode);
	static const TCHAR* GetRejectReasonName(EABTSM3SchemaRejectReason Reason);
};
