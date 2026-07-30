// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ABTSM3MonthlyRoute.generated.h"

struct FABTSM2Cell;

UENUM(BlueprintType)
enum class EABTSM3MonthlyRouteOrigin : uint8
{
	Normal = 0,
	MonthlyRouteFallback = 1
};

UENUM(BlueprintType)
enum class EABTSM3MonthlyRouteRejectReason : uint8
{
	None = 0,
	NotEvaluated = 1,
	InvalidConfig = 2,
	InvalidTopology = 3,
	InvalidContext = 4,
	SkeletonGenerationFailed = 5,
	AntipodalInstability = 6,
	HardBlocked = 7,
	CorridorDisconnected = 8,
	SearchBudgetExceeded = 9,
	NonAdjacentPath = 10,
	RepeatedCell = 11,
	RouteLengthOutOfRange = 12,
	InsufficientScenicBends = 13,
	StraightRunTooLong = 14,
	NonLocalSelfApproach = 15,
	ProgressInvalid = 16,
	AllNormalCandidatesFailed = 17,
	FallbackFailed = 18,
	HashMismatch = 19
};

/** The one numerical source for R-2 geometry acceptance. */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyAcceptanceProfileV1
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Acceptance",
		meta = (ClampMin = "1", Units = "cm"))
	int32 MinRouteLengthCM = 28000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Acceptance",
		meta = (ClampMin = "1", Units = "cm"))
	int32 TargetRouteLengthCM = 32000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Acceptance",
		meta = (ClampMin = "1", Units = "cm"))
	int32 MaxRouteLengthCM = 36000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Acceptance",
		meta = (ClampMin = "1", Units = "cm"))
	int32 BendSampleSpacingCM = 250;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Acceptance",
		meta = (ClampMin = "2", Units = "cm"))
	int32 BendWindowCM = 1500;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Acceptance",
		meta = (ClampMin = "1", ClampMax = "179", Units = "deg"))
	int32 MinBendAngleDegrees = 18;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Acceptance",
		meta = (ClampMin = "1", Units = "cm"))
	int32 MinBendSeparationCM = 2000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Acceptance",
		meta = (ClampMin = "0", ClampMax = "179", Units = "deg"))
	int32 StraightTurnThresholdDegrees = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Acceptance",
		meta = (ClampMin = "1", Units = "cm"))
	int32 MaxStraightCM = 5500;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Acceptance",
		meta = (ClampMin = "0", Units = "cm"))
	int32 SelfApproachIgnoreAlongRouteCM = 3000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Acceptance",
		meta = (ClampMin = "1", ClampMax = "32"))
	int32 MinSelfApproachCells = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Acceptance",
		meta = (ClampMin = "1", ClampMax = "16"))
	int32 MinScenicBendCount = 3;
};

/** Quantized road-search costs. All values participate in RouteConfigHash. */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyRouteCostProfileV1
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Cost",
		meta = (ClampMin = "0"))
	int32 CorridorRing3Penalty = 250;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Cost",
		meta = (ClampMin = "0"))
	int32 CorridorRing4Penalty = 750;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Cost",
		meta = (ClampMin = "0", ClampMax = "179", Units = "deg"))
	int32 SharpTurnStartDegrees = 35;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Cost",
		meta = (ClampMin = "0"))
	int32 SharpTurnCostPerDegree = 20;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Cost",
		meta = (ClampMin = "0", ClampMax = "179", Units = "deg"))
	int32 UTurnPenaltyStartDegrees = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Cost",
		meta = (ClampMin = "0"))
	int32 UTurnCostPerDegree = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Cost",
		meta = (ClampMin = "1", ClampMax = "179", Units = "deg"))
	int32 UTurnRejectDegrees = 135;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Cost",
		meta = (ClampMin = "0"))
	int32 LegalWaterCrossingCost = 800;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Cost",
		meta = (ClampMin = "0"))
	int32 SoftEncounterReservationCost = 1200;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Cost",
		meta = (ClampMin = "0"))
	int32 MaxReuseBonusPerStep = 200;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Cost",
		meta = (ClampMin = "0"))
	int32 MaxReuseBonusPerCandidate = 2000;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyRouteConfig
{
	GENERATED_BODY()

	/** Keeps R-2 as a parallel observation and never changes Gen3/Policy1 exports. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	bool bBuildRouteObservation = true;

	/** Diagnostic only and deliberately excluded from deterministic identity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	bool bEmitRouteLogs = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route",
		meta = (ClampMin = "4", ClampMax = "8"))
	int32 NormalCandidateSlots = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route",
		meta = (ClampMin = "1", ClampMax = "3"))
	int32 MaxRetainedCandidates = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route",
		meta = (ClampMin = "9", ClampMax = "33"))
	int32 SkeletonControlPointCount = 17;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route",
		meta = (ClampMin = "2", ClampMax = "12"))
	int32 RouteBeatPointCount = 9;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Corridor",
		meta = (ClampMin = "0", ClampMax = "8"))
	int32 CorridorCoreRadiusCells = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Corridor",
		meta = (ClampMin = "1", ClampMax = "12"))
	int32 CorridorAllowedRadiusCells = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Search Budget",
		meta = (ClampMin = "1024"))
	int32 MaxExpandedStatesPerCandidate = 131072;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Search Budget",
		meta = (ClampMin = "4096"))
	int32 MaxRelaxationsPerCandidate = 786432;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Search Budget",
		meta = (ClampMin = "0"))
	int32 MaxCandidateBacktracksPerSeed = 128;

	/** Reject control pairs near the antipode where the shortest tangent is unstable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route",
		meta = (ClampMin = "-1.0", ClampMax = "-0.5"))
	float AntipodalRejectDot = -0.98f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	FABTSM3MonthlyAcceptanceProfileV1 Acceptance;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	FABTSM3MonthlyRouteCostProfileV1 Costs;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Version")
	int32 RoutePlannerVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Version")
	int32 RouteMetricVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Version")
	int32 RoadSolverVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Version")
	int32 RouteScoreVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Version")
	int32 RouteFallbackVersion = 1;
};

/** R-3 injection seam. Empty Cells means a neutral route-only fixture. */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyRouteCellContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Context",
		meta = (ClampMin = "0"))
	int32 TerrainCost = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Context",
		meta = (ClampMin = "0"))
	int32 SlopeCost = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Context")
	bool bWater = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Context")
	bool bLegalWaterCrossing = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Context")
	bool bSoftEncounterReserved = false;

	/** Target footprint, NoRoad ring and illegal crossings are hard blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Context")
	bool bHardBlocked = false;

	/** Negative values are capped by the cost profile before use. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Context")
	int32 ReuseBias = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyRoadContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Context")
	TArray<FABTSM3MonthlyRouteCellContext> Cells;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyRouteBeatPoint
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Beat")
	int32 BeatPointId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Beat")
	int32 OrderIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Beat")
	int32 CellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Beat",
		meta = (Units = "cm"))
	int32 ProgressDistanceCM = 0;

	/** Quantized [0,1] route coordinate. Float is presentation only. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Beat")
	int32 FlowQ = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Beat")
	float FlowS = 0.0f;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyRouteMetrics
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Metrics",
		meta = (Units = "cm"))
	int32 RouteLengthCM = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Metrics")
	int32 ScenicBendCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Metrics",
		meta = (Units = "cm"))
	int32 MaxStraightCM = 0;

	/** Capped at MinSelfApproachCells+3 for stable diagnostics. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Metrics",
		meta = (ClampMin = "0"))
	int32 MinSelfApproachCells = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Metrics")
	int32 ShoulderEdgeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Metrics")
	int32 TotalEdgeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Metrics")
	int32 UTurnCount = 0;

	/** Reuse reward actually consumed by the search, capped per candidate. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Metrics")
	int32 AppliedReuseBonus = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Metrics")
	int64 SolverCost = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyRouteAttemptReport
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Attempt")
	int32 CandidateId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Attempt")
	EABTSM3MonthlyRouteOrigin Origin = EABTSM3MonthlyRouteOrigin::Normal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Attempt")
	bool bHardPass = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Attempt")
	EABTSM3MonthlyRouteRejectReason RejectReason =
		EABTSM3MonthlyRouteRejectReason::NotEvaluated;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Attempt")
	int32 RouteScore = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Attempt")
	int64 CandidateHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Attempt")
	int32 ExpandedStates = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Attempt")
	int32 Relaxations = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Attempt")
	int32 Backtracks = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyRouteCandidate
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	int32 CandidateId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	EABTSM3MonthlyRouteOrigin Origin = EABTSM3MonthlyRouteOrigin::Normal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	bool bHardPass = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	EABTSM3MonthlyRouteRejectReason RejectReason =
		EABTSM3MonthlyRouteRejectReason::NotEvaluated;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	TArray<int32> ControlCellIds;

	/** Strictly ascending set identity; the road order lives in OrderedRoadCellIds. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	TArray<int32> CorridorCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	TArray<int32> OrderedRoadCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	TArray<int32> ProgressDistanceCM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	TArray<FABTSM3MonthlyRouteBeatPoint> BeatPoints;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	FABTSM3MonthlyRouteMetrics Metrics;

	/** Higher is better; only evaluated after all hard gates pass. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	int32 RouteScore = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	int64 CandidateHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	int32 ExpandedStates = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	int32 Relaxations = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	int32 Backtracks = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyRoutePool
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	int32 SchemaVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	int32 GeneratorVersion = 3;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	int32 LayoutPolicyVersion = 2;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	int32 WorldSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	int64 RouteConfigHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	int64 TopologyHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	int64 RoadContextHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	int64 RouteCandidatePoolHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	bool bRoutePoolValid = false;

	/** R-2 can never publish a monthly world. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	bool bMonthlyWorldAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	bool bUsedRouteFallback = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	EABTSM3MonthlyRouteRejectReason RejectReason =
		EABTSM3MonthlyRouteRejectReason::NotEvaluated;

	/** Number of normal slots evaluated; fallback attempts live in AttemptReports. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	int32 AttemptedCandidateCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	int32 NormalHardPassCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	TArray<FABTSM3MonthlyRouteAttemptReport> AttemptReports;

	/** Stable best-first order, capped by MaxRetainedCandidates. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	TArray<FABTSM3MonthlyRouteCandidate> RetainedCandidates;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyRouteDebugData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Debug")
	TArray<int32> BestRouteCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Debug")
	TArray<int32> BestControlCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Debug")
	TArray<int32> BestCorridorCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Debug")
	int32 BestScenicBendCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Debug")
	int32 BestRouteLengthCM = 0;
};

class ABTSRUNTIME_API FABTSM3MonthlyRouteBuilder
{
public:
	static constexpr int32 GeneratorVersion = 3;
	static constexpr int32 MonthlyLayoutPolicyVersion = 2;
	static constexpr int32 FlowQuantization = 1000000;

	static bool Build(
		int32 WorldSeed,
		const FABTSM3MonthlyRouteConfig& Config,
		const TArray<FABTSM2Cell>& Cells,
		float PlanetRadiusCM,
		const FABTSM3MonthlyRoadContext& Context,
		FABTSM3MonthlyRoutePool& OutPool,
		FString& OutFailure);

	static bool Validate(
		const FABTSM3MonthlyRouteConfig& Config,
		const TArray<FABTSM2Cell>& Cells,
		float PlanetRadiusCM,
		const FABTSM3MonthlyRoadContext& Context,
		const FABTSM3MonthlyRoutePool& Pool,
		EABTSM3MonthlyRouteRejectReason& OutReason,
		FString& OutFailure);

	/**
	 * R-3 strict seam: regenerate exactly one previously retained skeleton under
	 * its own formal Terrain/Water/NoRoad context. Unlike Build(), this never
	 * creates a neutral-context fallback or reorders the candidate pool.
	 */
	static bool RebuildCandidateStrict(
		int32 WorldSeed,
		const FABTSM3MonthlyRouteConfig& Config,
		const TArray<FABTSM2Cell>& Cells,
		float PlanetRadiusCM,
		const FABTSM3MonthlyRoadContext& Context,
		const FABTSM3MonthlyRouteCandidate& SourceCandidate,
		FABTSM3MonthlyRouteCandidate& OutCandidate,
		EABTSM3MonthlyRouteRejectReason& OutReason,
		FString& OutFailure);

	static uint64 ComputeConfigHash(
		const FABTSM3MonthlyRouteConfig& Config,
		float PlanetRadiusCM,
		uint64 TopologyHash);

	static uint64 ComputeTopologyHash(const TArray<FABTSM2Cell>& Cells);

	static uint64 ComputeRoadContextHash(
		const FABTSM3MonthlyRoadContext& Context);

	static uint64 ComputeCandidateHash(
		const FABTSM3MonthlyRouteCandidate& Candidate);

	static uint64 ComputePoolHash(const FABTSM3MonthlyRoutePool& Pool);

	static uint64 ComputePoolSnapshotHash(
		const FABTSM3MonthlyRoutePool& Pool);

	static void BuildDebugData(
		const FABTSM3MonthlyRoutePool& Pool,
		FABTSM3MonthlyRouteDebugData& OutDebugData);

	static void LogSummary(const FABTSM3MonthlyRoutePool& Pool);

	static const TCHAR* GetRejectReasonName(
		EABTSM3MonthlyRouteRejectReason Reason);
};
