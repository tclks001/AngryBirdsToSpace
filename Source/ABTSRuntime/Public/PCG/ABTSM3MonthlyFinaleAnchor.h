// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCG/ABTSM3MonthlyEncounter.h"
#include "ABTSM3MonthlyFinaleAnchor.generated.h"

struct FABTSM2Cell;

UENUM(BlueprintType)
enum class EABTSM3MonthlyFinaleAnchorRejectReason : uint8
{
	None = 0,
	NotEvaluated = 1,
	InvalidConfig = 2,
	InvalidTopology = 3,
	InvalidSpatialResult = 4,
	TerminalWindowUnavailable = 5,
	CandidateJoinMismatch = 6,
	SurfaceResolutionFailed = 7,
	HashMismatch = 8
};

/**
 * M3R-5.2 plans a finale apron at the end of every retained monthly route.
 * It remains an observation-only source for a future Integration adapter and
 * never replaces the compatibility TaskGraph finale frame.
 */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyFinaleAnchorConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	bool bBuildFinaleAnchorPlans = true;

	/** Diagnostic only; excluded from deterministic identity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	bool bEmitFinaleAnchorLogs = true;

	/** Route cells inspected backwards from the exact terminal cell. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor",
		meta = (ClampMin = "3", ClampMax = "32"))
	int32 TerminalSearchWindowCells = 10;

	/** Minimum legal terminal cells required before the plan may be published. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor",
		meta = (ClampMin = "1", ClampMax = "16"))
	int32 MinimumTerminalCandidateCount = 3;

	/** Ordered route cells used to smooth the terminal launch-forward tangent. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor",
		meta = (ClampMin = "2", ClampMax = "12"))
	int32 TangentFitWindowCells = 5;

	/** CellTopo rings excluded from ordinary slot placement around the terminal window. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor",
		meta = (ClampMin = "0", ClampMax = "4"))
	int32 ClearanceRings = 1;

	/** Requested world-space separation of the finale Space-slot pair. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor",
		meta = (ClampMin = "100", ClampMax = "600", Units = "cm"))
	float SlotSeparationCM = 210.0f;

	/** Lift applied after the left and right slot positions are grounded independently. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor",
		meta = (ClampMin = "0", ClampMax = "40", Units = "cm"))
	float SurfaceOffsetCM = 4.0f;

	/** Maximum angle between a sampled terrain normal and primary radial up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor",
		meta = (ClampMin = "0", ClampMax = "60", Units = "deg"))
	float MaxSurfaceSlopeDegrees = 35.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Version")
	int32 PlannerVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Version")
	int32 SurfaceResolutionVersion = 1;
};

/** Topology-only candidate produced before continuous terrain is available. */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyFinaleAnchorPlanCandidate
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	int32 SourceRouteCandidateId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	int64 SourceSpatialCandidateHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	int64 SourceRecomputedRouteCandidateHash = 0;

	/** Exact final Cell in the ordered route, even when surface resolution falls back within the window. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	int32 RoadTerminalCellId = INDEX_NONE;

	/** Legal candidates in terminal-to-earlier order. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	TArray<int32> TerminalCandidateCellIds;

	/** Strictly ascending cells reserved from ordinary slot-field placement. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	TArray<int32> ClearanceCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	int64 CandidateHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyFinaleAnchorPlanResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	int32 SchemaVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	int32 GeneratorVersion = 5;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	int32 LayoutPolicyVersion = 4;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	int32 WorldSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	int64 TopologyHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	int64 SourceSpatialResultHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	int64 ConfigHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	bool bPlanResultValid = false;

	/** Observation-only; M3R-5.2 cannot accept the monthly world. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	bool bMonthlyWorldAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	EABTSM3MonthlyFinaleAnchorRejectReason RejectReason =
		EABTSM3MonthlyFinaleAnchorRejectReason::NotEvaluated;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	TArray<FABTSM3MonthlyFinaleAnchorPlanCandidate> RetainedCandidates;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	int64 ResultHash = 0;
};

/** Minimal real-surface query shared by Planet and asset-free fixtures. */
struct ABTSRUNTIME_API FABTSM3MonthlyFinaleSurfaceSample
{
	FVector WorldLocation = FVector::ZeroVector;
	FVector WorldNormal = FVector::UpVector;
	int32 NearestCellId = INDEX_NONE;
};

class ABTSRUNTIME_API IABTSM3MonthlyFinaleAnchorSurface
{
public:
	virtual ~IABTSM3MonthlyFinaleAnchorSurface() = default;
	virtual FVector GetPrimaryCenterWorld() const = 0;
	virtual float GetPrimaryRadiusCM() const = 0;
	virtual bool QuerySurface(
		const FVector& UnitDirection,
		FABTSM3MonthlyFinaleSurfaceSample& OutSample) const = 0;
};

/** Fully grounded, candidate-bound Preview/Test value for the Integration adapter. */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyFinaleAnchorPreview
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview")
	int32 SourceRouteCandidateId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview")
	int64 SourceSpatialCandidateHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview")
	int64 SourcePlanCandidateHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview")
	int64 SourcePlanResultHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview")
	int32 RoadTerminalCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview")
	int32 AnchorCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview")
	int32 LeftSlotNearestCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview")
	int32 RightSlotNearestCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview")
	FVector FrameOriginWorld = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview")
	FVector ForwardWorld = FVector::ForwardVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview")
	FVector RightWorld = FVector::RightVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview")
	FVector UpWorld = FVector::UpVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview")
	FVector AnchorSurfaceWorld = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview")
	FVector LeftSlotSurfaceWorld = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview")
	FVector RightSlotSurfaceWorld = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview")
	FVector LeftSlotWorldLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview")
	FVector RightSlotWorldLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview",
		meta = (Units = "cm"))
	float ActualSlotSeparationCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview",
		meta = (Units = "deg"))
	float MaxResolvedSurfaceSlopeDegrees = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview")
	bool bPreviewValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview")
	bool bMonthlyWorldAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor|Preview")
	int64 PreviewHash = 0;
};

class ABTSRUNTIME_API FABTSM3MonthlyFinaleAnchorBuilder
{
public:
	static constexpr int32 SchemaVersion = 1;
	static constexpr int32 GeneratorVersion = 5;
	static constexpr int32 MonthlyLayoutPolicyVersion = 4;

	static bool Build(
		int32 WorldSeed,
		const FABTSM3MonthlyFinaleAnchorConfig& Config,
		const TArray<FABTSM2Cell>& Cells,
		const FABTSM3MonthlySpatialResult& SpatialResult,
		FABTSM3MonthlyFinaleAnchorPlanResult& OutResult,
		FString& OutFailure);

	static bool Validate(
		const FABTSM3MonthlyFinaleAnchorConfig& Config,
		const TArray<FABTSM2Cell>& Cells,
		const FABTSM3MonthlySpatialResult& SpatialResult,
		const FABTSM3MonthlyFinaleAnchorPlanResult& Result,
		EABTSM3MonthlyFinaleAnchorRejectReason& OutReason,
		FString& OutFailure);

	static bool BuildPreview(
		int32 SourceRouteCandidateId,
		const FABTSM3MonthlyFinaleAnchorConfig& Config,
		const TArray<FABTSM2Cell>& Cells,
		const FABTSM3MonthlySpatialResult& SpatialResult,
		const FABTSM3MonthlyFinaleAnchorPlanResult& PlanResult,
		const IABTSM3MonthlyFinaleAnchorSurface& Surface,
		FABTSM3MonthlyFinaleAnchorPreview& OutPreview,
		FString& OutFailure);

	static const FABTSM3MonthlyFinaleAnchorPlanCandidate* FindCandidate(
		const FABTSM3MonthlyFinaleAnchorPlanResult& Result,
		int32 SourceRouteCandidateId);

	static uint64 ComputeConfigHash(
		const FABTSM3MonthlyFinaleAnchorConfig& Config,
		uint64 TopologyHash);

	static uint64 ComputeCandidateHash(
		const FABTSM3MonthlyFinaleAnchorPlanCandidate& Candidate);

	static uint64 ComputeResultHash(
		const FABTSM3MonthlyFinaleAnchorPlanResult& Result);

	static uint64 ComputePreviewHash(
		const FABTSM3MonthlyFinaleAnchorPreview& Preview);

	static void LogSummary(
		const FABTSM3MonthlyFinaleAnchorPlanResult& Result);

	static const TCHAR* GetRejectReasonName(
		EABTSM3MonthlyFinaleAnchorRejectReason Reason);
};
