// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Calibration/ABTSSlingshotSatelliteCalibrationTypes.h"
#include "CoreMinimal.h"
#include "PCG/ABTSM3MonthlyRoute.h"
#include "PCG/ABTSM3MonthlySchema.h"
#include "ABTSM3MonthlyEncounter.generated.h"

struct FABTSM2Cell;

/**
 * M3-local, map-independent snapshot built exclusively from Integration's
 * frozen M6/M9 V0 factories. R-3 signs it and uses the reach envelopes only
 * as a conservative placement prefilter; it is not a production trajectory
 * provider or a scene-instance gravity snapshot.
 */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3FrozenCalibrationBatch
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Calibration")
	int32 SchemaVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Calibration")
	int32 LaunchProfileVersion = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Calibration")
	int64 LaunchProfileHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Calibration")
	int32 SatellitePracticePresetVersion = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Calibration")
	int64 SatellitePracticePresetHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Calibration")
	TArray<FABTSM6ReachEnvelope> ReachEnvelopes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Calibration")
	int64 BatchHash = 0;
};

UENUM(BlueprintType)
enum class EABTSM3MonthlyRevealPolicy : uint8
{
	DirectVisual = 0,
	ScoutRequired = 1
};

UENUM(BlueprintType)
enum class EABTSM3MonthlyVisibilityClass : uint8
{
	Hidden = 0,
	LandmarkOnly = 1,
	AttackReadable = 2
};

UENUM(BlueprintType)
enum class EABTSM3MonthlyObserverRole : uint8
{
	Start = 0,
	PreReveal = 1,
	Reveal = 2
};

UENUM(BlueprintType)
enum class EABTSM3MonthlySpatialRejectReason : uint8
{
	None = 0,
	NotEvaluated = 1,
	InvalidConfig = 2,
	InvalidTopology = 3,
	InvalidRoutePool = 4,
	ProfileCatalogUnavailable = 5,
	ReservationFailed = 6,
	RoadRebuildFailed = 7,
	RouteMetricsFailed = 8,
	EncounterSpacingFailed = 9,
	PocketIdentityInvalid = 10,
	TargetOverlap = 11,
	BiomeCoverageFailed = 12,
	PVSInvalid = 13,
	PVSContractFailed = 14,
	RayBudgetExceeded = 15,
	SearchBudgetExceeded = 16,
	AllRouteCandidatesFailed = 17,
	HashMismatch = 18
};

/**
 * M3-local frozen stand-in for the future Integration/M7 read-only descriptor
 * catalog. R-3 may use it for Bounds/PVS only and must report its CatalogHash.
 */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyProfileDescriptorFixture
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Profile")
	FName ProfileId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Profile",
		meta = (Units = "cm"))
	FVector BoundsExtentCM = FVector(300.0, 300.0, 260.0);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Profile")
	int32 SilhouetteFamilyId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Profile")
	int32 MaterialProfileId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Profile")
	int32 WeaknessProfileId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Profile")
	int64 DescriptorHash = 0;
};

/**
 * R-3's one source for six-Encounter reservation, scratch terrain/hydrology,
 * PVS and biome/envelope acceptance. Arrays are fixed-domain catalogs and are
 * validated as exactly six entries.
 */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyEncounterSpatialConfig
{
	GENERATED_BODY()

	FABTSM3MonthlyEncounterSpatialConfig();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	bool bBuildSpatialObservation = true;

	/** Diagnostic only; excluded from deterministic identity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	bool bEmitSpatialLogs = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter",
		meta = (ClampMin = "6", ClampMax = "6"))
	int32 DestructibleEncounterCount = 6;

	/** Quantized route targets for E1..E6. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Catalog")
	TArray<int32> EncounterFlowQ;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Catalog")
	TArray<FIntPoint> TargetRoadDistanceWindowsCells;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Catalog")
	TArray<int32> DifficultyBands;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Catalog")
	TArray<EABTSM3MonthlyRevealPolicy> RevealPolicies;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Catalog")
	TArray<EABTSM3BiomeArchetype> EncounterBiomeArchetypes;

	/** E1..E6 launch capability used by the frozen-V0 reach prefilter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Catalog")
	TArray<EABTSSlingshotTier> EncounterSlingshotTiers;

	/**
	 * E1..E6 usable fractions of the tier's frozen comfortable reach.
	 * X/Y are inclusive permille bounds. Target placement solves against the
	 * midpoint instead of merely checking the absolute maximum reach.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Catalog")
	TArray<FIntPoint> EncounterComfortableReachWindowsPermille;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Spacing",
		meta = (ClampMin = "1", Units = "cm"))
	int32 MinAdjacentEncounterProgressCM = 3500;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Spacing",
		meta = (ClampMin = "1", Units = "cm"))
	int32 MaxAdjacentEncounterProgressCM = 6000;

	/** Final RoadArrival must remain near its catalog Flow target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Spacing",
		meta = (ClampMin = "1", Units = "cm"))
	int32 MaxPlannedProgressDeviationCM = 1200;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Reservation",
		meta = (ClampMin = "0", ClampMax = "4"))
	int32 TargetEnvelopeRadiusCells = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Reservation",
		meta = (ClampMin = "0", ClampMax = "4"))
	int32 TargetFootprintRadiusCells = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Reservation",
		meta = (ClampMin = "1", ClampMax = "5"))
	int32 TargetNoRoadRadiusCells = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Reservation",
		meta = (ClampMin = "0", ClampMax = "4"))
	int32 PocketRadiusCells = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Reservation",
		meta = (ClampMin = "0", Units = "cm"))
	int32 RoadHalfWidthCM = 180;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Reservation",
		meta = (ClampMin = "0", Units = "cm"))
	int32 PadRoadBlendWidthCM = 180;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Reservation",
		meta = (ClampMin = "0", Units = "cm"))
	int32 FootprintSafetyMarginCM = 25;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Envelope",
		meta = (ClampMin = "1", ClampMax = "6"))
	int32 PlayableRouteRadiusCells = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Envelope",
		meta = (ClampMin = "1", ClampMax = "6"))
	int32 PlayablePocketRadiusCells = 2;

	/** Role-bearing core inside each pocket's playable transition ring. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Envelope",
		meta = (ClampMin = "0", ClampMax = "6"))
	int32 ActivePocketRadiusCells = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Reservation",
		meta = (ClampMin = "100", Units = "cm"))
	int32 PreRevealLeadCM = 5000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Reservation",
		meta = (ClampMin = "100", Units = "cm"))
	int32 ExitLeadCM = 1000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Reservation",
		meta = (ClampMin = "8", ClampMax = "2048"))
	int32 MaxAnchorCandidatesPerEncounter = 256;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Reservation",
		meta = (ClampMin = "0", ClampMax = "512"))
	int32 MaxSpatialBacktracksPerCandidate = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Road Context",
		meta = (ClampMin = "0"))
	int32 BaseTerrainCost = 12;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Road Context",
		meta = (ClampMin = "0"))
	int32 HeightCostScale = 40;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Road Context",
		meta = (ClampMin = "0"))
	int32 SlopeCostScale = 24;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Road Context",
		meta = (ClampMin = "0", Units = "cm"))
	int32 ScratchHeightScaleCM = 600;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Road Context",
		meta = (ClampMax = "0"))
	int32 ExistingRoadReuseBias = -120;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Hydrology",
		meta = (ClampMin = "0", ClampMax = "300"))
	int32 BackgroundWaterPermille = 45;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Hydrology",
		meta = (ClampMin = "1", ClampMax = "8"))
	int32 LegalCrossingHalfWidthCells = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS",
		meta = (ClampMin = "1", ClampMax = "4096"))
	int32 MaxOptimizedPVSRaysPerWorld = 1024;

	/** Maximum neighbor-Voronoi intervals for the optimized continuous trace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS",
		meta = (ClampMin = "4", ClampMax = "128"))
	int32 OptimizedTraceSamples = 128;

	/** Maximum exact Voronoi-height intervals for the continuous reference trace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS",
		meta = (ClampMin = "8", ClampMax = "256"))
	int32 ReferenceTraceSamples = 128;

	/** Matches M4's default orbit distance around the observer pivot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS",
		meta = (ClampMin = "1", Units = "cm"))
	int32 DefaultOrbitDistanceCM = 850;

	/** Conservative second sample matching M4's maximum orbit distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS",
		meta = (ClampMin = "1", Units = "cm"))
	int32 MaxOrbitDistanceCM = 1300;

	/** Matches M4's default camera elevation above the route tangent. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS",
		meta = (ClampMin = "1", ClampMax = "89", Units = "deg"))
	int32 CameraElevationDegrees = 60;

	/** M1 bird capsule center above the continuous surface. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS",
		meta = (ClampMin = "0", Units = "cm"))
	int32 ObserverCharacterCenterHeightCM = 60;

	/** M4 look-at pivot height above the bird Actor location. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS",
		meta = (ClampMin = "0", Units = "cm"))
	int32 ObserverLookAtHeightCM = 30;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS",
		meta = (ClampMin = "0", Units = "cm"))
	int32 TargetCenterHeightCM = 260;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS",
		meta = (ClampMin = "0", Units = "cm"))
	int32 VisibilityOcclusionEpsilonCM = 40;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS",
		meta = (ClampMin = "100", Units = "cm"))
	int32 AttackReadableMaxDistanceCM = 3200;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS",
		meta = (ClampMin = "100", Units = "cm"))
	int32 LandmarkMaxDistanceCM = 9000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS",
		meta = (ClampMin = "100", Units = "cm"))
	int32 ScoutDetectionRadiusCM = 8000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Envelope",
		meta = (ClampMin = "0", ClampMax = "1000"))
	int32 MinActiveRoleCoveragePermille = 750;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Envelope",
		meta = (ClampMin = "0", ClampMax = "1000"))
	int32 MaxDeepWildPermille = 200;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Biome",
		meta = (ClampMin = "1", ClampMax = "6"))
	int32 MinEncounterBiomeArchetypes = 4;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Version")
	int32 ReservationPlannerVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Version")
	int32 TerrainScratchVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Version")
	int32 HydrologyScratchVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Version")
	int32 BiomePlannerVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Version")
	int32 CameraSampleSetVersion = 2;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Version")
	int32 SpatialScoreVersion = 1;
};

/** Explicitly hashed fault injection used only by fail-closed certification. */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlySpatialFaultInjection
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Fault Injection")
	bool bBlockEverySourceRoadCell = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Fault Injection")
	bool bInvalidateStartObserver = false;

	/** Test-only: rejects the first N candidate-local reservation variants. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Fault Injection",
		meta = (ClampMin = "0", ClampMax = "64"))
	int32 ForcedReservationFailures = 0;

	/** Test-only: one source candidate fails non-retryably. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Fault Injection")
	int32 RejectedSourceCandidateId = INDEX_NONE;

	/** Test-only: hard-blocks one non-control source-road order index. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Fault Injection")
	int32 BlockedSourceRoadOrderIndex = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlySpatialCell
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Cell")
	int32 CellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Cell")
	int32 NearestRoadOrderIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Cell")
	int32 MainRoadDistanceCells = MAX_int32;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Cell")
	int32 FlowQ = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Cell")
	int32 BiomeDistrictId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Cell")
	int32 PrimaryEnvelopeId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Cell",
		meta = (Bitmask, BitmaskEnum = "/Script/ABTSRuntime.EABTSM3ActiveRole"))
	int32 ActiveRoleMask = 0;

	/** Intentional playable breathing room outside active role cores. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Cell")
	bool bApprovedTransition = false;

	/** Quantized monthly scratch height in [0,1000000]. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Cell")
	int32 HeightQ = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Cell")
	bool bWater = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Cell")
	bool bLegalWaterCrossing = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Cell")
	bool bTargetFootprint = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Cell")
	bool bNoRoad = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Cell")
	bool bAttackCorridor = false;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyVisibilityEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS")
	EABTSM3MonthlyObserverRole ObserverRole =
		EABTSM3MonthlyObserverRole::Start;

	/** INDEX_NONE for Start, otherwise E1..E6 EncounterId. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS")
	int32 ObserverEncounterId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS")
	int32 ObserverCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS")
	int32 TargetEncounterId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS")
	EABTSM3MonthlyVisibilityClass VisibilityClass =
		EABTSM3MonthlyVisibilityClass::Hidden;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS")
	bool bScoutDetectable = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS")
	bool bEvaluationValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS")
	bool bIdealSphereBlocked = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS")
	bool bTerrainBlocked = false;

	/** Number of deterministic orbit-distance samples aggregated by this relation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS")
	int32 CameraSampleCount = 0;

	/** Samples where both center and attack face are readable. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS")
	int32 AttackReadableCameraSampleCount = 0;

	/** Samples where at least one target proxy ray is visible. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS")
	int32 VisibleCameraSampleCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|PVS")
	int32 RayCount = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlySpatialEncounter
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	FABTSM3EncounterContract Contract;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	EABTSM3MonthlyRevealPolicy RevealPolicy =
		EABTSM3MonthlyRevealPolicy::DirectVisual;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int32 FlowQ = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int32 PreRevealCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int32 TargetAnchorCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int32 MainRoadDistanceCells = MAX_int32;

	/** Continuous road-band/building-bounds clearance, quantized to topology cells. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int32 RequiredRoadClearanceCells = 0;

	/** Final direct surface arc from the generated slingshot pocket to target. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter",
		meta = (Units = "cm"))
	int32 LaunchToTargetDistanceCM = 0;

	/** Final target-to-road side path used by the attack-corridor overlay. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter",
		meta = (Units = "cm"))
	int32 AttackCorridorLengthCM = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	TArray<int32> TargetFootprintCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	TArray<int32> TargetNoRoadCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	FName ResolvedFixtureProfileId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int64 ProfileCatalogHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter",
		meta = (Units = "cm"))
	FVector ProfileBoundsExtentCM = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	FVector AttackFaceDirection = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int64 VisualSignature = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int64 EncounterHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlySpatialAttemptReport
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Attempt")
	int32 SourceRouteCandidateId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Attempt")
	int64 SourceRouteCandidateHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Attempt")
	int64 ReservationHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Attempt")
	int64 RoadContextHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Attempt")
	int64 RecomputedRouteCandidateHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Attempt")
	bool bHardPass = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Attempt")
	EABTSM3MonthlySpatialRejectReason RejectReason =
		EABTSM3MonthlySpatialRejectReason::NotEvaluated;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Attempt")
	FName FailureCode = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Attempt")
	int32 SpatialScore = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Attempt")
	int32 Backtracks = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlySpatialCandidate
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int32 SourceRouteCandidateId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int64 SourceRouteCandidateHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int64 ReservationHash = 0;

	/** Frozen union of all pre-road playable reservation cells. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	TArray<int32> PreRoadReservedPlayableCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int64 RoadContextHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	FABTSM3MonthlyRouteCandidate RecomputedRoute;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	FABTSM3MonthlyRoadContext RoadContext;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	TArray<FABTSM3MonthlySpatialCell> Cells;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	TArray<FABTSM3MonthlySpatialEncounter> Encounters;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	TArray<FABTSM3PocketContract> Pockets;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	TArray<FABTSM3BiomeDistrict> BiomeDistricts;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	TArray<FABTSM3PlayableEnvelope> PlayableEnvelopes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	TArray<FABTSM3MonthlyVisibilityEntry> VisibilityEntries;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int32 OptimizedPVSRays = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int32 PlayableCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int32 ActiveRoleCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int32 ApprovedTransitionCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int32 DeepWildCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int32 ActiveRoleCoveragePermille = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int32 DeepWildPermille = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int32 SpatialScore = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int32 Backtracks = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	bool bHardPass = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	EABTSM3MonthlySpatialRejectReason RejectReason =
		EABTSM3MonthlySpatialRejectReason::NotEvaluated;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int64 SpatialCandidateHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlySpatialResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int32 SchemaVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int32 GeneratorVersion = 3;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int32 LayoutPolicyVersion = 2;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int32 WorldSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int64 TopologyHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int64 SourceRoutePoolHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int64 SpatialConfigHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int64 ProfileCatalogHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	FABTSM3FrozenCalibrationBatch FrozenCalibrationBatch;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int64 FaultInjectionHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int64 SpatialResultHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	bool bSpatialResultValid = false;

	/** R-3 cannot publish a complete monthly world or LayoutHash. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	bool bMonthlyWorldAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	bool bUsedRouteFallback = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	EABTSM3MonthlySpatialRejectReason RejectReason =
		EABTSM3MonthlySpatialRejectReason::NotEvaluated;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int32 AttemptedRouteCandidateCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	int32 SpatialHardPassCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	TArray<FABTSM3MonthlySpatialAttemptReport> AttemptReports;

	/** Stable best-first order. Each candidate owns its own formal RoadContext. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	TArray<FABTSM3MonthlySpatialCandidate> RetainedCandidates;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlySpatialDebugData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Debug")
	TArray<int32> RoadArrivalCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Debug")
	TArray<int32> RevealCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Debug")
	TArray<int32> SlingshotCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Debug")
	TArray<int32> TargetAnchorCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Debug")
	TArray<int32> NoRoadCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Debug")
	TArray<int32> PlayableEnvelopeCellIds;
};

class ABTSRUNTIME_API FABTSM3MonthlyEncounterBuilder
{
public:
	static constexpr int32 GeneratorVersion = 3;
	static constexpr int32 MonthlyLayoutPolicyVersion = 2;
	static constexpr int32 SpatialSchemaVersion = 1;
	static constexpr int32 FrozenCalibrationSchemaVersion = 1;
	static constexpr int32 RequiredObserverCount = 13;
	static constexpr int32 RequiredVisibilityEntryCount = 78;
	static constexpr int32 RequiredPocketCount = 42;
	static constexpr int32 RequiredCameraSampleCount = 2;

	static bool Build(
		int32 WorldSeed,
		const FABTSM3MonthlyEncounterSpatialConfig& Config,
		const FABTSM3MonthlyRouteConfig& RouteConfig,
		const TArray<FABTSM2Cell>& Cells,
		float PlanetRadiusCM,
		const FABTSM3MonthlyRoutePool& SourceRoutePool,
		const FABTSM3MonthlySpatialFaultInjection& FaultInjection,
		FABTSM3MonthlySpatialResult& OutResult,
		FString& OutFailure);

	static bool Validate(
		const FABTSM3MonthlyEncounterSpatialConfig& Config,
		const FABTSM3MonthlyRouteConfig& RouteConfig,
		const TArray<FABTSM2Cell>& Cells,
		float PlanetRadiusCM,
		const FABTSM3MonthlyRoutePool& SourceRoutePool,
		const FABTSM3MonthlySpatialFaultInjection& FaultInjection,
		const FABTSM3MonthlySpatialResult& Result,
		EABTSM3MonthlySpatialRejectReason& OutReason,
		FString& OutFailure);

	static uint64 ComputeConfigHash(
		const FABTSM3MonthlyEncounterSpatialConfig& Config,
		const FABTSM3MonthlyRouteConfig& RouteConfig,
		float PlanetRadiusCM,
		uint64 TopologyHash);

	static uint64 ComputeFaultInjectionHash(
		const FABTSM3MonthlySpatialFaultInjection& FaultInjection);

	static uint64 ComputeFixtureProfileCatalogHash();

	static bool BuildFrozenCalibrationBatchV0(
		float PlanetRadiusCM,
		FABTSM3FrozenCalibrationBatch& OutBatch,
		FString& OutFailure);

	static uint64 ComputeFrozenCalibrationBatchHash(
		const FABTSM3FrozenCalibrationBatch& Batch);

	static TConstArrayView<FABTSM3MonthlyProfileDescriptorFixture>
		GetFixtureProfileCatalog();

	/** Recomputes the stable payload hash persisted by each R-3 encounter. */
	static uint64 ComputeEncounterHash(
		const FABTSM3MonthlySpatialEncounter& Encounter);

	static uint64 ComputeCandidateHash(
		const FABTSM3MonthlySpatialCandidate& Candidate);

	static uint64 ComputeResultHash(
		const FABTSM3MonthlySpatialResult& Result);

	static uint64 ComputeResultSnapshotHash(
		const FABTSM3MonthlySpatialResult& Result);

	static bool ValidateVisibilityAgainstReference(
		const FABTSM3MonthlyEncounterSpatialConfig& Config,
		const TArray<FABTSM2Cell>& Cells,
		float PlanetRadiusCM,
		const FABTSM3MonthlySpatialCandidate& Candidate,
		int32& OutMismatchCount,
		uint64& OutReferenceHash,
		FString& OutFailure);

	static void BuildDebugData(
		const FABTSM3MonthlySpatialResult& Result,
		FABTSM3MonthlySpatialDebugData& OutDebugData);

	static void LogSummary(const FABTSM3MonthlySpatialResult& Result);

	static const TCHAR* GetRejectReasonName(
		EABTSM3MonthlySpatialRejectReason Reason);
};
