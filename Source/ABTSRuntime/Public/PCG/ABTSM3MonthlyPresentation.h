// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCG/ABTSM3MonthlyEncounter.h"
#include "ABTSM3MonthlyPresentation.generated.h"

struct FABTSM2Cell;

UENUM(BlueprintType)
enum class EABTSM3MonthlyPresentationRejectReason : uint8
{
	None = 0,
	NotEvaluated = 1,
	Disabled = 2,
	InvalidConfig = 3,
	InvalidSourceSpatial = 4,
	SourceCandidateInvalid = 5,
	CandidateIdentityMismatch = 6,
	BiomeCoverageFailed = 7,
	EnvelopeCoverageFailed = 8,
	BiomeFragmented = 9,
	VisualRhythmFailed = 10,
	DecorationBudgetFailed = 11,
	FaultInjected = 12,
	HashMismatch = 13
};

UENUM(BlueprintType, meta = (Bitflags))
enum class EABTSM3MonthlyDecorationKind : uint8
{
	None = 0 UMETA(Hidden),
	Forest = 1 << 0,
	Rock = 1 << 1
};
ENUM_CLASS_FLAGS(EABTSM3MonthlyDecorationKind);

/**
 * R-5 is a candidate-bound presentation planner. These values may alter visual
 * cadence and decoration density, but never R-3 identity, QuerySurface or any
 * gameplay witness.
 */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyPresentationConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	bool bBuildPresentation = true;

	/** Diagnostic only; excluded from deterministic identity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	bool bEmitPresentationLogs = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Rhythm",
		meta = (ClampMin = "100", Units = "cm"))
	int32 MinVisualBeatLengthCM = 2000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Rhythm",
		meta = (ClampMin = "100", Units = "cm"))
	int32 TargetVisualBeatLengthCM = 3000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Rhythm",
		meta = (ClampMin = "100", Units = "cm"))
	int32 MaxVisualBeatLengthCM = 4500;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Coverage",
		meta = (ClampMin = "1", ClampMax = "6"))
	int32 MinBiomeArchetypeCount = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Coverage",
		meta = (ClampMin = "0", ClampMax = "1000"))
	int32 MinActiveRoleCoveragePermille = 750;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Coverage",
		meta = (ClampMin = "0", ClampMax = "1000"))
	int32 MaxDeepWildPermille = 200;

	/** Display-only fragments smaller than this are merged without rewriting R-3 identity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Coverage",
		meta = (ClampMin = "2", ClampMax = "64"))
	int32 MinVisualBiomeComponentCells = 3;

	/**
	 * Coarse whole-planet ratio of unique neighbor edges crossing display
	 * biomes. Playable-local fragmentation remains a Visible PIE/R-7 gate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Coverage",
		meta = (ClampMin = "0", ClampMax = "1000"))
	int32 MaxVisualBiomeBoundaryPermille = 250;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Decoration",
		meta = (ClampMin = "0", ClampMax = "8"))
	int32 MaxDecorInstancesPerCell = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Decoration",
		meta = (ClampMin = "0", ClampMax = "10000"))
	int32 MaxDecorInstancesPerCandidate = 1024;

	/** Role-bearing cells stay visually readable and collision free. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Decoration")
	bool bSuppressDecorOnActiveRoles = true;
};

/** Test-only failures are part of the input identity and never used by runtime. */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyPresentationFaultInjection
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Fault Injection")
	int32 RejectedSourceCandidateId = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyDistrictStyle
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	int32 BiomeDistrictId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	EABTSM3BiomeArchetype Archetype = EABTSM3BiomeArchetype::Plain;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	int32 ThemeId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	int32 ThemeVariantCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	bool bBackground = false;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyEnvelopePresentation
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	int32 EnvelopeId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	int32 RouteBeatId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	int32 EncounterId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation",
		meta = (Units = "cm"))
	int32 MinProgressCM = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation",
		meta = (Units = "cm"))
	int32 MaxProgressCM = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyVisualBeat
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	int32 VisualBeatId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	int32 BeatOrdinal = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation",
		meta = (Units = "cm"))
	int32 StartProgressCM = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation",
		meta = (Units = "cm"))
	int32 EndProgressCM = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	int32 AccentVariantId = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyPresentationCell
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	int32 CellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	int32 BiomeDistrictId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	EABTSM3BiomeArchetype BiomeArchetype = EABTSM3BiomeArchetype::Plain;

	/** Visual-only orphan merge; BiomeDistrictId and BiomeArchetype remain frozen. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	EABTSM3BiomeArchetype DisplayBiomeArchetype = EABTSM3BiomeArchetype::Plain;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	EABTSM3TerrainType VisualTerrainType = EABTSM3TerrainType::Plain;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	int32 PrimaryEnvelopeId = INDEX_NONE;

	/** Sorted complete membership; PrimaryEnvelopeId alone is not authoritative. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	TArray<int32> EnvelopeIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation",
		meta = (Bitmask, BitmaskEnum = "/Script/ABTSRuntime.EABTSM3ActiveRole"))
	int32 ActiveRoleMask = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	int32 VisualBeatId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	int32 ThemeVariantId = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation",
		meta = (Bitmask, BitmaskEnum = "/Script/ABTSRuntime.EABTSM3MonthlyDecorationKind"))
	int32 DecorationKindMask = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	int32 MaxDecorationInstances = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	bool bPlayable = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	bool bDeepWild = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	bool bApprovedTransition = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	bool bWater = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	bool bTargetFootprint = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	bool bNoRoad = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	bool bAttackCorridor = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	bool bDecorationProtected = false;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyCandidatePresentation
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Identity")
	int32 SourceRouteCandidateId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Identity")
	int64 SourceRouteCandidateHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Identity")
	int64 SourceRecomputedRouteCandidateHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Identity")
	int64 SourceSpatialCandidateHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Identity")
	int64 PresentationConfigHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation",
		meta = (Units = "cm"))
	int32 RouteLengthCM = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	TArray<FABTSM3MonthlyDistrictStyle> DistrictStyles;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	TArray<FABTSM3MonthlyEnvelopePresentation> Envelopes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	TArray<FABTSM3MonthlyVisualBeat> VisualBeats;

	/** Dense CellId-indexed array. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	TArray<FABTSM3MonthlyPresentationCell> Cells;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Metrics")
	int32 PlayableCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Metrics")
	int32 ActiveRoleCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Metrics")
	int32 DeepWildCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Metrics")
	int32 ActiveRoleCoveragePermille = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Metrics")
	int32 DeepWildPermille = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Metrics")
	int32 BiomeArchetypeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Metrics")
	int32 BiomeComponentCount = 0;

	/** R-3 logical singletons preserved for diagnostics and visually merged only. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Metrics")
	int32 MergedLogicalSingletonCellCount = 0;

	/** Additional display fragments below MinVisualBiomeComponentCells merged after singleton repair. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Metrics")
	int32 MergedSmallVisualFragmentCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Metrics")
	int32 SingleCellBiomeComponentCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Metrics")
	int32 MinVisualBiomeComponentCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Metrics")
	int32 VisualBiomeBoundaryPermille = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Metrics",
		meta = (Units = "cm"))
	int32 MinVisualBeatLengthCM = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Metrics",
		meta = (Units = "cm"))
	int32 MaxVisualBeatLengthCM = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Metrics")
	int32 DecorationProtectedCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Metrics")
	int32 PlannedDecorationInstanceCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Identity")
	int64 CandidatePresentationHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyPresentationResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	int32 SchemaVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	int32 PlannerVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	int32 WorldSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Identity")
	int64 SourceTopologyHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Identity")
	int64 SourceSpatialConfigHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Identity")
	int64 SourceSpatialResultHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Identity")
	int64 PresentationConfigHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Identity")
	int64 FaultInjectionHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	bool bPresentationValid = false;

	/** R-5 never selects or publishes a monthly world. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	bool bMonthlyWorldAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	EABTSM3MonthlyPresentationRejectReason RejectReason =
		EABTSM3MonthlyPresentationRejectReason::NotEvaluated;

	/** Stable R-3 best-first order, without adding a selection identity. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	TArray<FABTSM3MonthlyCandidatePresentation> CandidatePresentations;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Identity")
	int64 PresentationResultHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyPresentationDebugData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Debug")
	int32 SourceRouteCandidateId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Debug")
	TArray<int32> PlayableCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Debug")
	TArray<int32> ActiveRoleCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Debug")
	TArray<int32> DeepWildCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Debug")
	TArray<int32> BackgroundCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Debug")
	TArray<int32> DecorationProtectedCellIds;
};

class ABTSRUNTIME_API FABTSM3MonthlyPresentationBuilder
{
public:
	static constexpr int32 PresentationSchemaVersion = 1;
	static constexpr int32 PlannerVersion = 1;

	static bool Build(
		int32 WorldSeed,
		const FABTSM3MonthlyPresentationConfig& Config,
		const FABTSM3MonthlyEncounterSpatialConfig& SpatialConfig,
		const FABTSM3MonthlyRouteConfig& RouteConfig,
		const TArray<FABTSM2Cell>& Cells,
		float PlanetRadiusCM,
		const FABTSM3MonthlyRoutePool& SourceRoutePool,
		const FABTSM3MonthlySpatialResult& SourceSpatialResult,
		const FABTSM3MonthlyPresentationFaultInjection& FaultInjection,
		FABTSM3MonthlyPresentationResult& OutResult,
		FString& OutFailure);

	static bool Validate(
		const FABTSM3MonthlyPresentationConfig& Config,
		const FABTSM3MonthlyEncounterSpatialConfig& SpatialConfig,
		const FABTSM3MonthlyRouteConfig& RouteConfig,
		const TArray<FABTSM2Cell>& Cells,
		float PlanetRadiusCM,
		const FABTSM3MonthlyRoutePool& SourceRoutePool,
		const FABTSM3MonthlySpatialResult& SourceSpatialResult,
		const FABTSM3MonthlyPresentationFaultInjection& FaultInjection,
		const FABTSM3MonthlyPresentationResult& Result,
		EABTSM3MonthlyPresentationRejectReason& OutReason,
		FString& OutFailure);

	static uint64 ComputeConfigHash(
		const FABTSM3MonthlyPresentationConfig& Config);

	static uint64 ComputeFaultInjectionHash(
		const FABTSM3MonthlyPresentationFaultInjection& FaultInjection);

	static uint64 ComputeCandidateHash(
		const FABTSM3MonthlyCandidatePresentation& Candidate);

	static uint64 ComputeResultHash(
		const FABTSM3MonthlyPresentationResult& Result);

	static const FABTSM3MonthlyCandidatePresentation*
		FindCandidatePresentation(
			const FABTSM3MonthlyPresentationResult& Result,
			int32 SourceRouteCandidateId);

	static void BuildDebugData(
		const FABTSM3MonthlyPresentationResult& Result,
		int32 SourceRouteCandidateId,
		FABTSM3MonthlyPresentationDebugData& OutDebugData);

	static void LogSummary(
		const FABTSM3MonthlyPresentationResult& Result);

	static const TCHAR* GetRejectReasonName(
		EABTSM3MonthlyPresentationRejectReason Reason);
};
