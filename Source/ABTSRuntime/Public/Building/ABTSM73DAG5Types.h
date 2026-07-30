// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ABTSM73DAG5Types.generated.h"

/** Stable stage classification for one DAG5-A candidate attempt. */
UENUM(BlueprintType)
enum class EABTSM73DAG5ARejectStage : uint8
{
	None,
	Settings,
	Capacity,
	Grammar,
	ScopeCapacity,
	Pipeline,
	CompiledBrickBudget,
	StaticStability
};

/** Explicitly opt-in DAG5-A feasibility search. Defaults preserve the existing one-shot DAG2.3 path. */
USTRUCT(BlueprintType)
struct FABTSM73DAG5ASettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG5-A")
	bool bEnableFeasibilitySearch = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG5-A", meta = (ClampMin = "1", ClampMax = "64"))
	int32 SearchVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG5-A", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxCandidateAttempts = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG5-A")
	bool bEnableCapacityPreflight = true;

	/**
	 * Final physical BrickNode limit. Zero delegates to GenerationSettings.MaxBrickCount.
	 * This is checked after compilation and never removes supports to make a candidate fit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG5-A", meta = (ClampMin = "0", ClampMax = "256"))
	int32 MaxCompiledBrickCount = 0;
};

/** Deterministic trace for one bounded DAG5-A candidate. */
USTRUCT(BlueprintType)
struct FABTSM73DAG5AAttemptResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 AttemptIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 CandidateSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int64 TopologyHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 CompiledBrickCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	bool bAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	EABTSM73DAG5ARejectStage RejectStage = EABTSM73DAG5ARejectStage::None;

	/** Stable prefix before the first ':' in RejectReason. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	FString RejectCode;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	FString RejectReason;
};

/** Complete evidence for one DAG5-A search transaction. */
USTRUCT(BlueprintType)
struct FABTSM73DAG5AResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	bool bEnabled = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	bool bCapacityPreflightPassed = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	bool bAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 InputSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 AttemptCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 ScopePreflightRejectCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 CompiledCandidateCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 SelectedAttemptIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 SelectedCandidateSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 EffectiveCompiledBrickLimit = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 CompiledBrickCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 RequiredMinimumExpansionSteps = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 RequiredMinimumTerminalCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int64 SearchHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	TArray<FABTSM73DAG5AAttemptResult> Attempts;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	FString RejectReason;
};

/** Authored macro silhouette selected before local semantic WFC. */
UENUM(BlueprintType)
enum class EABTSM73DAG5BShapeFamily : uint8
{
	Auto,
	SetbackTower,
	OffsetBridge,
	ThroughOpeningWall,
	OneSidedHighTower
};

/** Low-resolution intent only. Final load-bearing truth remains DAG2.3 + Contact DAG. */
UENUM(BlueprintType)
enum class EABTSM73DAG5BSemanticCell : uint8
{
	Void,
	Foundation,
	FloorCarrier,
	ColumnZone,
	WallPier,
	Frame,
	DoorVoid,
	WindowVoid,
	BeamZone,
	Roof,
	Cantilever
};

UENUM(BlueprintType)
enum class EABTSM73DAG5BOccupancy : uint8
{
	MayOccupy,
	MustOccupy,
	MustVoid
};

enum class EABTSM73DAG5BFeature : uint32
{
	None = 0,
	Setback = 1u << 0,
	FootprintCentroidShift = 1u << 1,
	ThroughOpening = 1u << 2,
	HeightAsymmetry = 1u << 3,
	BridgeSpan = 1u << 4,
	Cantilever = 1u << 5,
	NonUniformRoofline = 1u << 6
};
ENUM_CLASS_FLAGS(EABTSM73DAG5BFeature);

enum class EABTSM73DAG5BPort : uint16
{
	None = 0,
	TopLoad = 1u << 0,
	BottomSupport = 1u << 1,
	LeftBeam = 1u << 2,
	RightBeam = 1u << 3,
	FrontFacade = 1u << 4,
	BackFacade = 1u << 5,
	Bridge = 1u << 6,
	Frame = 1u << 7,
	AttackClearance = 1u << 8
};
ENUM_CLASS_FLAGS(EABTSM73DAG5BPort);

/** Explicit opt-in. Defaults preserve DAG5-A and all earlier golden geometry. */
USTRUCT(BlueprintType)
struct FABTSM73DAG5BSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG5-B")
	bool bEnableSemanticEnvelope = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG5-B",
		meta = (ClampMin = "1", ClampMax = "64"))
	int32 EnvelopeVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG5-B")
	EABTSM73DAG5BShapeFamily ShapeFamily =
		EABTSM73DAG5BShapeFamily::Auto;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG5-B|Grid",
		meta = (ClampMin = "5", ClampMax = "9"))
	int32 GridSizeX = 7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG5-B|Grid",
		meta = (ClampMin = "3", ClampMax = "5"))
	int32 GridSizeY = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG5-B|Grid",
		meta = (ClampMin = "4", ClampMax = "8"))
	int32 GridSizeZ = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG5-B|Budget",
		meta = (ClampMin = "64", ClampMax = "65536"))
	int32 MaxWFCPropagationOperations = 65536;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG5-B|Budget",
		meta = (ClampMin = "0", ClampMax = "256"))
	int32 MaxWFCBacktrackSteps = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG5-B|Shape",
		meta = (ClampMin = "0.05", ClampMax = "0.35"))
	float SetbackRatio = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG5-B|Shape",
		meta = (ClampMin = "0.02", ClampMax = "0.25"))
	float OffsetRatio = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG5-B|Shape",
		meta = (ClampMin = "0.05", ClampMax = "0.30"))
	float CantileverRatio = 0.14f;
};

/** One stable Shape Grammar volume emitted before WFC collapse. */
struct FABTSM73DAG5BShapeScope
{
	int32 MacroNodeId = INDEX_NONE;
	FBox NormalizedBounds = FBox(EForceInit::ForceInit);
	FString DerivationPath;
	EABTSM73DAG5BSemanticCell Semantic =
		EABTSM73DAG5BSemanticCell::Void;
};

/** Macro-level constraint consumed by DAG2.3 before support selection. */
struct FABTSM73DAG5BMacroConstraint
{
	int32 MacroNodeId = INDEX_NONE;
	FVector2D OffsetCM = FVector2D::ZeroVector;
	FVector2D FootprintScale = FVector2D(1.0f);
	FString DerivationPath;
};

/**
 * WFC-resolved physical support portal slice consumed before the DAG2.3 load
 * solve. The complete realized column prism is separately audited against
 * every MustVoid contract.
 */
struct FABTSM73DAG5BSupportPortConstraint
{
	int32 SupportMacroNodeId = INDEX_NONE;
	int32 LoadMacroNodeId = INDEX_NONE;
	FBox2D AllowedColumnRegion = FBox2D(EForceInit::ForceInit);
	FIntVector SourceCellMin = FIntVector::ZeroValue;
	FIntVector SourceCellMax = FIntVector::ZeroValue;
	EABTSM73DAG5BSemanticCell SourceSemantic =
		EABTSM73DAG5BSemanticCell::Void;
	uint32 SourceCellHash = 0;
	FString DerivationPath;
};

/** WFC-authored weakness attachment candidate; DAG-4 remains the authority. */
struct FABTSM73DAG5BWeaknessSocket
{
	FIntVector SourceCell = FIntVector::ZeroValue;
	FBox LocalBounds = FBox(EForceInit::ForceInit);
	EABTSM73DAG5BPort RequiredPort = EABTSM73DAG5BPort::None;
	uint32 SourceCellHash = 0;
	FString DerivationPath;
};

/** One collapsed local WFC cell and its physical audit contract. */
struct FABTSM73DAG5BSemanticCellRecord
{
	FIntVector Coordinate = FIntVector::ZeroValue;
	FBox LocalBounds = FBox(EForceInit::ForceInit);
	FBox RequiredSolidBounds = FBox(EForceInit::ForceInit);
	EABTSM73DAG5BSemanticCell Semantic =
		EABTSM73DAG5BSemanticCell::Void;
	EABTSM73DAG5BOccupancy Occupancy =
		EABTSM73DAG5BOccupancy::MayOccupy;
	EABTSM73DAG5BPort Ports = EABTSM73DAG5BPort::None;
	int32 RequiredMacroNodeId = INDEX_NONE;
	bool bHardAnchor = false;
	FString DerivationPath;
};

/** Stable Shape/WFC boundary consumed by layout, module compilation and audit. */
struct FABTSM73SemanticEnvelope
{
	bool bAccepted = false;
	int32 EnvelopeVersion = 0;
	EABTSM73DAG5BShapeFamily ShapeFamily =
		EABTSM73DAG5BShapeFamily::Auto;
	FIntVector GridSize = FIntVector::ZeroValue;
	FBox LocalBounds = FBox(EForceInit::ForceInit);
	EABTSM73DAG5BFeature FeatureMask = EABTSM73DAG5BFeature::None;
	TArray<FABTSM73DAG5BShapeScope> ShapeScopes;
	TArray<FABTSM73DAG5BMacroConstraint> MacroConstraints;
	TArray<FABTSM73DAG5BSupportPortConstraint> SupportPorts;
	TArray<FABTSM73DAG5BWeaknessSocket> WeaknessSockets;
	TArray<FABTSM73DAG5BSemanticCellRecord> Cells;
	TArray<FString> ShapeDerivationTrace;
	TArray<FString> WFCCollapseTrace;
	uint32 ShapeHash = 0;
	uint32 WFCHash = 0;
	uint32 EnvelopeHash = 0;
	FString RejectReason;
};

struct FABTSM73DAG5BAuditResult
{
	bool bAccepted = false;
	int32 MustOccupyCount = 0;
	int32 MustVoidCount = 0;
	int32 MustVoidViolationCount = 0;
	int32 UncoveredMustOccupyCount = 0;
	int32 OutOfEnvelopeBrickCount = 0;
	int32 OutOfShapeScopeBrickCount = 0;
	uint32 AuditHash = 0;
	FString RejectReason;
};

/** Complete deterministic evidence for one DAG5-B candidate. */
struct FABTSM73DAG5BResult
{
	bool bEnabled = false;
	bool bAccepted = false;
	EABTSM73DAG5BShapeFamily ShapeFamily =
		EABTSM73DAG5BShapeFamily::Auto;
	EABTSM73DAG5BFeature FeatureMask = EABTSM73DAG5BFeature::None;
	int32 PropagationOperationCount = 0;
	int32 BacktrackStepCount = 0;
	int32 CollapsedNonAnchorCellCount = 0;
	int32 WFCDerivedMustVoidCount = 0;
	int32 SemanticRegionMappingCount = 0;
	int32 WFCMappedBrickCount = 0;
	uint32 ShapeHash = 0;
	uint32 WFCHash = 0;
	uint32 EnvelopeHash = 0;
	uint32 ResultHash = 0;
	FABTSM73DAG5BAuditResult Audit;
	FString RejectReason;
};
