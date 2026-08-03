// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCG/ABTSM3MonthlyEncounter.h"
#include "ABTSM3MonthlySlingshotField.generated.h"

struct FABTSM2Cell;
struct FABTSM3MonthlyFinaleAnchorPlanResult;

UENUM(BlueprintType)
enum class EABTSM3MonthlySlingshotFieldKind : uint8
{
	EncounterRequired = 0,
	RoadAuxiliary = 1
};

UENUM(BlueprintType)
enum class EABTSM3MonthlySlingshotFieldRejectReason : uint8
{
	None = 0,
	NotEvaluated = 1,
	InvalidConfig = 2,
	InvalidTopology = 3,
	InvalidSpatialResult = 4,
	FieldGenerationFailed = 5,
	HashMismatch = 6,
	InvalidFinaleAnchorPlanResult = 7
};

/**
 * M3R-3.1 configuration for ordinary slingshot slot fields.
 *
 * A field always owns two base slots. AdditionalSlotsPerOrdinaryField adds
 * player choice without authoring an allowed-pair graph. MaxCordLengthCM is
 * the single distance limit that the Integration-owned cord installer must
 * consume when it creates a cord.
 */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlySlingshotFieldConfig
{
	GENERATED_BODY()

	/** Keeps R-3.1 as a parallel observation until the shared M5.1 consumer is integrated. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	bool bBuildSlingshotFields = true;

	/** Diagnostic only; excluded from deterministic identity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	bool bEmitSlingshotFieldLogs = true;

	/** Additional slots beyond the two ordinary-field base slots. Default 5 means seven slots. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field",
		meta = (ClampMin = "0", ClampMax = "10"))
	int32 AdditionalSlotsPerOrdinaryField = 5;

	/** Extra ordinary slot fields distributed along the accepted main road. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field",
		meta = (ClampMin = "0", ClampMax = "12"))
	int32 AdditionalRoadFieldCount = 2;

	/**
	 * Maximum straight-line distance between the two stake cord sockets.
	 * R-3.1 uses the same value conservatively at CellTopo centers; the shared
	 * installer must re-evaluate the exact runtime socket positions.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field",
		meta = (ClampMin = "100", ClampMax = "4000", UIMin = "300", UIMax = "1800", Units = "cm"))
	int32 MaxCordLengthCM = 1200;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field|Version")
	int32 FieldPlannerVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field|Version")
	int32 SlotSelectionVersion = 1;
};

/**
 * One compact set of freely selectable slots. SlotCellIds is stable field-local
 * order (anchor first), not a list of prescribed pairs.
 */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlySlingshotField
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int32 FieldId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	EABTSM3MonthlySlingshotFieldKind Kind =
		EABTSM3MonthlySlingshotFieldKind::EncounterRequired;

	/** INDEX_NONE for RoadAuxiliary. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int32 EncounterId = INDEX_NONE;

	/**
	 * R-3 Slingshot Pocket search center for EncounterRequired; it may overlap
	 * the conservative target footprint and therefore is not itself a slot.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int32 SourcePocketAnchorCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int32 AnchorCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int32 FlowQ = 0;

	/**
	 * Anchor first, followed by deterministic scattered slots. This array
	 * deliberately contains no allowed-pair or neighbor-edge data.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	TArray<int32> SlotCellIds;

	/** Diagnostic count derived only from MaxCordLengthCM; no pair identities are stored. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int32 DistanceReachablePairCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int64 FieldHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlySlingshotFieldCandidate
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int32 SourceRouteCandidateId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int64 SourceSpatialCandidateHash = 0;

	/** Zero for legacy callers; otherwise the exact M3R-5.2 terminal-apron plan joined to this candidate. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int64 SourceFinaleAnchorPlanCandidateHash = 0;

	/** EncounterRequired fields first in Encounter order, then RoadAuxiliary order. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	TArray<FABTSM3MonthlySlingshotField> Fields;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int32 TotalSlotCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int64 CandidateHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlySlingshotFieldResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int32 SchemaVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int32 GeneratorVersion = 3;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int32 LayoutPolicyVersion = 2;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int32 WorldSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int64 TopologyHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int64 SourceSpatialResultHash = 0;

	/** Zero for legacy callers; non-zero means finale clearance was consumed during slot planning. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int64 SourceFinaleAnchorPlanResultHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int64 ConfigHash = 0;

	/** Authoritative value to copy into the future accepted-layout Integration DTO. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field",
		meta = (Units = "cm"))
	int32 MaxCordLengthCM = 0;

	/** Exact count in every retained candidate; candidates are alternatives, not additive worlds. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int32 FieldsPerCandidate = 0;

	/** Exact count in every retained candidate; excludes the finale's two Space slots. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int32 SlotsPerCandidate = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	bool bSlingshotFieldResultValid = false;

	/** R-3.1 remains an M3-local layer and cannot publish the monthly world. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	bool bMonthlyWorldAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	EABTSM3MonthlySlingshotFieldRejectReason RejectReason =
		EABTSM3MonthlySlingshotFieldRejectReason::NotEvaluated;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	TArray<FABTSM3MonthlySlingshotFieldCandidate> RetainedCandidates;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	int64 ResultHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlySlingshotFieldDebugData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field|Debug")
	TArray<int32> EncounterSlotCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field|Debug")
	TArray<int32> RoadSlotCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field|Debug")
	TArray<int32> FieldAnchorCellIds;
};

class ABTSRUNTIME_API FABTSM3MonthlySlingshotFieldBuilder
{
public:
	static constexpr int32 SchemaVersion = 1;
	static constexpr int32 GeneratorVersion = 3;
	static constexpr int32 MonthlyLayoutPolicyVersion = 2;
	static constexpr int32 BaseSlotsPerOrdinaryField = 2;
	static constexpr int32 RequiredEncounterFieldCount = 6;

	static bool Build(
		int32 WorldSeed,
		const FABTSM3MonthlySlingshotFieldConfig& Config,
		const TArray<FABTSM2Cell>& Cells,
		float PlanetRadiusCM,
		const FABTSM3MonthlySpatialResult& SpatialResult,
		FABTSM3MonthlySlingshotFieldResult& OutResult,
		FString& OutFailure);

	/** M3R-5.2 overload that excludes each candidate's finale apron from ordinary slots. */
	static bool Build(
		int32 WorldSeed,
		const FABTSM3MonthlySlingshotFieldConfig& Config,
		const TArray<FABTSM2Cell>& Cells,
		float PlanetRadiusCM,
		const FABTSM3MonthlySpatialResult& SpatialResult,
		const FABTSM3MonthlyFinaleAnchorPlanResult& FinaleAnchorPlanResult,
		FABTSM3MonthlySlingshotFieldResult& OutResult,
		FString& OutFailure);

	static bool Validate(
		const FABTSM3MonthlySlingshotFieldConfig& Config,
		const TArray<FABTSM2Cell>& Cells,
		float PlanetRadiusCM,
		const FABTSM3MonthlySpatialResult& SpatialResult,
		const FABTSM3MonthlySlingshotFieldResult& Result,
		EABTSM3MonthlySlingshotFieldRejectReason& OutReason,
		FString& OutFailure);

	static bool Validate(
		const FABTSM3MonthlySlingshotFieldConfig& Config,
		const TArray<FABTSM2Cell>& Cells,
		float PlanetRadiusCM,
		const FABTSM3MonthlySpatialResult& SpatialResult,
		const FABTSM3MonthlyFinaleAnchorPlanResult& FinaleAnchorPlanResult,
		const FABTSM3MonthlySlingshotFieldResult& Result,
		EABTSM3MonthlySlingshotFieldRejectReason& OutReason,
		FString& OutFailure);

	static uint64 ComputeConfigHash(
		const FABTSM3MonthlySlingshotFieldConfig& Config,
		float PlanetRadiusCM,
		uint64 TopologyHash);

	static uint64 ComputeFieldHash(
		const FABTSM3MonthlySlingshotField& Field);

	static uint64 ComputeCandidateHash(
		const FABTSM3MonthlySlingshotFieldCandidate& Candidate);

	static uint64 ComputeResultHash(
		const FABTSM3MonthlySlingshotFieldResult& Result);

	static void BuildDebugData(
		const FABTSM3MonthlySlingshotFieldResult& Result,
		FABTSM3MonthlySlingshotFieldDebugData& OutDebugData);

	static void LogSummary(
		const FABTSM3MonthlySlingshotFieldResult& Result);

	static const TCHAR* GetRejectReasonName(
		EABTSM3MonthlySlingshotFieldRejectReason Reason);
};
