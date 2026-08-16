// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCG/ABTSM3MonthlySlingshotField.h"
#include "ABTSM3MonthlySatellitePreview.generated.h"

struct FABTSM2Cell;
struct FABTSCalibrationGravitySnapshot;
struct FABTSCalibrationSweepSummary;
struct FABTSM6CalibrationLaunchFrame;
struct FABTSM6LaunchProfileCatalog;
struct FABTSSatellitePracticePreset;

UENUM(BlueprintType)
enum class EABTSM3MonthlySatellitePreviewRejectReason : uint8
{
	None = 0,
	NotEvaluated = 1,
	InvalidConfig = 2,
	InvalidTopology = 3,
	InvalidSpatialResult = 4,
	InvalidSlingshotFieldResult = 5,
	FrozenPresetMismatch = 6,
	CandidateJoinMismatch = 7,
	PracticeEncounterMissing = 8,
	ReferencePairMissing = 9,
	SurfaceQueryFailed = 10,
	TargetTransformFailed = 11,
	HashMismatch = 12
};

/** Target geometry authority carried by one satellite-preview candidate. */
UENUM(BlueprintType)
enum class EABTSM3MonthlySatelliteTargetAuthority : uint8
{
	LegacyCalibrationProxy = 0,
	FrozenE1BuildingModules = 1
};

/**
 * R-5.1 is an observation-only bridge from the frozen calibration preset to
 * each exact R-3/R-3.1 candidate. It does not spawn M9 or M7 actors and does
 * not select or accept a monthly world.
 */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlySatellitePreviewConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	bool bBuildSatellitePreview = true;

	/** Diagnostic only; excluded from deterministic identity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	bool bEmitPreviewLogs = true;

	/** Zero-based encounter order. E5 is order four in the six-encounter contract. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int32 PracticeEncounterOrder = 4;

	/** Frozen primary gravity used by the calibration data set. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview", meta = (Units = "cm/s^2"))
	float PrimarySurfaceGravityCMPerSec2 = 980.0f;

	/** Legacy identity input retained for serialized compatibility; the real reinforced pouch geometry is authoritative. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview", meta = (ClampMin = "0.0", ClampMax = "1000.0", Units = "cm"))
	float ReferencePouchHeightCM = 190.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview|Version")
	int32 PlannerVersion = 1;
};

/** Minimal continuous-surface sample used by both Planet and automation fixtures. */
struct ABTSRUNTIME_API FABTSM3MonthlySatelliteSurfaceSample
{
	FVector WorldLocation = FVector::ZeroVector;
	FVector WorldNormal = FVector::UpVector;
	int32 NearestCellId = INDEX_NONE;
};

class ABTSRUNTIME_API IABTSM3MonthlySatellitePreviewSurface
{
public:
	virtual ~IABTSM3MonthlySatellitePreviewSurface() = default;
	virtual FVector GetPrimaryCenterWorld() const = 0;
	virtual float GetPrimaryRadiusCM() const = 0;
	virtual bool QuerySurface(
		const FVector& UnitDirection,
		FABTSM3MonthlySatelliteSurfaceSample& OutSample) const = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlySatellitePreviewCandidate
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int32 SourceRouteCandidateId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int64 SourceSpatialCandidateHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int64 SourceSlingshotFieldCandidateHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int32 PracticeEncounterId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int64 PracticeFieldHash = 0;

	/** Diagnostic reference only. Every distance-valid pair remains player-selectable. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int32 ReferenceSlotACellId = INDEX_NONE;

	/** Diagnostic reference only. It is not an allowed-pair restriction. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int32 ReferenceSlotBCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int64 LaunchProfileHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int32 SatellitePracticePresetVersion = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int64 SatellitePracticePresetHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	FVector LaunchWorldLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	FVector LaunchUpWorld = FVector::UpVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	FVector LaunchForwardWorld = FVector::ForwardVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	FVector LaunchRightWorld = FVector::RightVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	FVector SatelliteAnchorDirection = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int32 SatelliteAnchorCellId = INDEX_NONE;

	/** Small deterministic terrain-normal compensation applied on the frozen 30 degree arc ring. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview", meta = (Units = "deg"))
	float SatelliteFacingCorrectionAzimuthDegrees = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview", meta = (Units = "deg"))
	float SatelliteFacingErrorDegrees = 180.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	FVector SatelliteCenterWorld = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview", meta = (Units = "cm"))
	float SatelliteRadiusCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview", meta = (Units = "cm/s^2"))
	float SatelliteSurfaceGravityCMPerSec2 = 0.0f;

	/** Historical field name; final V3 stores the selected real frozen E1 module transform. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	FTransform E5TargetWorldTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview", meta = (Units = "cm"))
	FVector E5TargetHalfExtentCM = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	EABTSM3MonthlySatelliteTargetAuthority TargetAuthority =
		EABTSM3MonthlySatelliteTargetAuthority::LegacyCalibrationProxy;

	/** Site carrier is separate from the module transform, avoiding a circular frame. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	FTransform SatelliteSiteWorldTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int64 ProductionTargetDescriptorHash = 0;

	/** Frozen descriptor BrickId; INDEX_NONE is the legacy proxy path. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int32 ProductionTargetModuleId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int64 ProductionTargetIdentityHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	bool bProductionTargetTrajectoryCertified = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int64 ProductionTargetTrajectoryHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	bool bE5OnSatelliteBackside = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int64 CandidateHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlySatellitePreviewResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int32 SchemaVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int32 GeneratorVersion = 5;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int32 LayoutPolicyVersion = 4;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int32 WorldSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int64 TopologyHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int64 SourceSpatialResultHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int64 SourceSlingshotFieldResultHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int64 ConfigHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	bool bPreviewResultValid = false;

	/** Observation-only. R-5.1 cannot accept a monthly world. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	bool bMonthlyWorldAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	EABTSM3MonthlySatellitePreviewRejectReason RejectReason =
		EABTSM3MonthlySatellitePreviewRejectReason::NotEvaluated;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	TArray<FABTSM3MonthlySatellitePreviewCandidate> RetainedCandidates;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	int64 ResultHash = 0;
};

class ABTSRUNTIME_API FABTSM3MonthlySatellitePreviewBuilder
{
public:
	static constexpr int32 SchemaVersion = 1;
	static constexpr int32 GeneratorVersion = 6;
	static constexpr int32 MonthlyLayoutPolicyVersion = 4;

	static bool Build(
		int32 WorldSeed,
		const FABTSM3MonthlySatellitePreviewConfig& Config,
		const TArray<FABTSM2Cell>& Cells,
		const FABTSM3MonthlySpatialResult& SpatialResult,
		const FABTSM3MonthlySlingshotFieldResult& SlingshotFieldResult,
		const IABTSM3MonthlySatellitePreviewSurface& Surface,
		FABTSM3MonthlySatellitePreviewResult& OutResult,
		FString& OutFailure,
		EABTSM3MonthlySatelliteTargetAuthority TargetAuthority =
			EABTSM3MonthlySatelliteTargetAuthority::LegacyCalibrationProxy,
		int32 RequiredCertifiedSourceCandidateId = INDEX_NONE);

	static bool Validate(
		const FABTSM3MonthlySatellitePreviewConfig& Config,
		const TArray<FABTSM2Cell>& Cells,
		const FABTSM3MonthlySpatialResult& SpatialResult,
		const FABTSM3MonthlySlingshotFieldResult& SlingshotFieldResult,
		const IABTSM3MonthlySatellitePreviewSurface& Surface,
		const FABTSM3MonthlySatellitePreviewResult& Result,
		EABTSM3MonthlySatellitePreviewRejectReason& OutReason,
		FString& OutFailure,
		EABTSM3MonthlySatelliteTargetAuthority TargetAuthority =
			EABTSM3MonthlySatelliteTargetAuthority::LegacyCalibrationProxy,
		int32 RequiredCertifiedSourceCandidateId = INDEX_NONE);

	static const FABTSM3MonthlySatellitePreviewCandidate* FindCandidate(
		const FABTSM3MonthlySatellitePreviewResult& Result,
		int32 SourceRouteCandidateId);

	static uint64 ComputeConfigHash(
		const FABTSM3MonthlySatellitePreviewConfig& Config,
		uint64 TopologyHash);

	static uint64 ComputeCandidateHash(
		const FABTSM3MonthlySatellitePreviewCandidate& Candidate);

	static uint64 ComputeProductionTargetIdentityHash(
		uint64 DescriptorHash,
		const FTransform& SiteWorldTransform,
		const FTransform& TargetWorldTransform,
		const FVector& TargetHalfExtentCM);

	/** M3-private production math exposed to its runtime consumer: exact ordered
	 * public E1 Brick OBB union, stable first-hit, no max-axis cube fallback. */
	static bool RunFrozenE1BuildingModuleUnionSweep(
		const FABTSM6CalibrationLaunchFrame& LaunchFrame,
		const FABTSCalibrationGravitySnapshot& Gravity,
		const FTransform& SiteWorldTransform,
		const FABTSM6LaunchProfileCatalog& Catalog,
		const FABTSSatellitePracticePreset& Preset,
		FABTSCalibrationSweepSummary& OutSummary,
		int32& OutWitnessBrickId,
		uint64& OutTargetIdentityHash,
		FString& OutFailure);

	/** No-trajectory release gate: the historical reachable proxy centre is inside a real E1 Brick OBB expanded only by its documented volume and bird radius. */
	static bool EvaluateFrozenE1LegacyProxyOverlap(
		const FVector& LaunchWorldLocation,
		const FABTSCalibrationGravitySnapshot& CalibrationGravity,
		const FTransform& SiteWorldTransform,
		const FABTSSatellitePracticePreset& FrozenPreset,
		int32& OutOverlapBrickId,
		int32& OutOverlapBrickCount,
		uint64& OutTargetIdentityHash,
		uint64& OutOverlapHash,
		FString& OutFailure);

	static uint64 ComputeResultHash(
		const FABTSM3MonthlySatellitePreviewResult& Result);

	static void LogSummary(
		const FABTSM3MonthlySatellitePreviewResult& Result);

	static const TCHAR* GetRejectReasonName(
		EABTSM3MonthlySatellitePreviewRejectReason Reason);
};
