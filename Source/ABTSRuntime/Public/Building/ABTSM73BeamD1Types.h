// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM7BuildingTypes.h"
#include "Building/ABTSM73BeamAPreviewTypes.h"
#include "ABTSM73BeamD1Types.generated.h"

UENUM(BlueprintType)
enum class EABTSM73BeamD1StructuralRole : uint8
{
	PrimaryFrame,
	SecondaryFrame,
	Connector
};

UENUM(BlueprintType)
enum class EABTSM73BeamD1DeviceRole : uint8
{
	None,
	Anchor,
	Payload
};

/** Real Beam-C3 production stop. Unimplemented future stages fail closed. */
UENUM(BlueprintType)
enum class EABTSM73BeamC3GenerationStage : uint8
{
	SemanticEnvelope UMETA(DisplayName = "Stage 0 - WFC Semantic Envelope"),
	CoreAndShared UMETA(DisplayName = "Stage 1 - Core + Shared Courses"),
	CouplingCourses UMETA(DisplayName = "Stage 2 - Core / Facade Coupling Courses"),
	CommonExteriorFrame UMETA(DisplayName = "Stage 3 - Common Exterior Frame"),
	FloorInfillRoof UMETA(DisplayName = "Stage 4 - Floor / Top Frames"),
	StaticDAG UMETA(DisplayName = "Stage 5 - Complete Static DAG (Legacy Baseline)"),
	DeviceAssembly UMETA(DisplayName = "Stage 5.5 - Barrel / Piston Assembly"),
	FrozenBuildingV3 UMETA(DisplayName = "Frozen V3 - Final Material Recipe")
};

/** Frozen jury-demo selection. Custom preserves manually authored preview settings. */
UENUM(BlueprintType)
enum class EABTSM73BeamDemoBuilding : uint8
{
	Custom UMETA(DisplayName = "Custom Settings"),
	E1ColumnBreak UMETA(DisplayName = "Complexity E1 - ColumnBreak / 710000"),
	E2DropTrigger UMETA(DisplayName = "Complexity E2 - DropTrigger / 740000"),
	E3SlideRelease UMETA(DisplayName = "Complexity E3 - SlideRelease / 750137"),
	E4TipOver UMETA(DisplayName = "Complexity E4 - TipOver / 730000"),
	E5SeamRelease UMETA(DisplayName = "Complexity E5 - SeamRelease / 720000"),
	E6TipOver UMETA(DisplayName = "Complexity E6 - TipOver / 750000")
};

/** Mutually exclusive Stage-2 visual evidence layer on the D1 preview Actor. */
UENUM(BlueprintType)
enum class EABTSM73BeamC3Stage2DiagnosticLayer : uint8
{
	CouplingCoursesOnly UMETA(DisplayName = "1 - Coupling Courses Only"),
	CouplingProvenance UMETA(DisplayName = "2 - Coupling Provenance"),
	CoreAndCouplingCourses UMETA(DisplayName = "3 - Core + Coupling Courses"),
	PerimeterCoreFaces UMETA(DisplayName = "4 - Perimeter Cores / Touched Faces"),
	FacadePartitionsAndHeightAnchors UMETA(DisplayName = "5 - Facade Partitions / Height Anchors")
};

/** Mutually exclusive Stage-3 production evidence layers. */
UENUM(BlueprintType)
enum class EABTSM73BeamC3Stage3DiagnosticLayer : uint8
{
	ExteriorFramesOnly UMETA(DisplayName = "1 - Exterior Frames Only"),
	GroundSillOnly UMETA(DisplayName = "2 - Ground Sill Only"),
	GroundToFirstFrameColumns UMETA(DisplayName = "3 - Ground / First-Frame Columns"),
	ExteriorColumnsOnly UMETA(DisplayName = "4 - Inter-Frame Exterior Columns"),
	Stage123Overview UMETA(DisplayName = "5 - Stage 1 / 2 / 3 Overview")
};

/** Stage-4 first-stop evidence. Later Stage-4 geometry layers are added only
 * after this ownership ledger is visually accepted. */
UENUM(BlueprintType)
enum class EABTSM73BeamC3Stage4DiagnosticLayer : uint8
{
	TopSurfaceIntent UMETA(DisplayName = "1 - GroundSill / TopSurface Intent"),
	FloorTopFrames UMETA(DisplayName = "2 - Floor / Top Frames"),
	FacadeToTopConnections UMETA(DisplayName = "3 - Facade-to-Top Connections"),
	FloorStyleInfill UMETA(DisplayName = "4 - Floor / StyleInfill"),
	RoofCrown UMETA(DisplayName = "5 - Roof / Crown"),
	Stage14Overview UMETA(DisplayName = "6 - Stage 1 / 2 / 3 / 4 Overview")
};

/** Mutually exclusive Stage-1 visual evidence layer on the D1 preview Actor. */
UENUM(BlueprintType)
enum class EABTSM73BeamC3Stage1DiagnosticLayer : uint8
{
	WFCSemanticEnvelope UMETA(DisplayName = "1 - WFC Semantic Envelope"),
	CorePlacementIntent UMETA(DisplayName = "2 - Core Placement / Pairing Intent"),
	CoreAndSharedCourses UMETA(DisplayName = "3 - Core + Shared Courses"),
	CoreMergeRegions UMETA(DisplayName = "4 - Core Merge Regions"),
	CompositeCoreXLanes UMETA(DisplayName = "5 - Composite Core X Lanes"),
	CompositeCoreYLanes UMETA(DisplayName = "6 - Composite Core Y Lanes"),
	SemanticSupportDemandDAG UMETA(DisplayName = "7 - Semantic Support Demand DAG"),
	SupportProvincePartition UMETA(DisplayName = "8 - Support Province Partition"),
	SupportProvinceMainAssignment UMETA(DisplayName = "9 - Province / Main Assignment"),
	DemandCoreCouplingLedger UMETA(DisplayName = "10 - Demand / Child / Main Ledger"),
	LocalPodiumHeightPlan UMETA(DisplayName = "11 - Local Podium Height Plan")
};

USTRUCT(BlueprintType)
struct FABTSM73BeamD1Settings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile")
	FName GameplayProfileId = TEXT("ColumnBreak");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile",
		meta = (ClampMin = "0", ClampMax = "5"))
	int32 DifficultyTier = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile")
	int32 BuildingSeed = 940211;
};

/** Complete one-to-one binding from a closed Beam Member to one real M7 Brick. */
USTRUCT(BlueprintType)
struct FABTSM73BeamD1BrickBinding
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 BrickId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 MemberId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	EABTSM73BeamAFrameAxis Axis = EABTSM73BeamAFrameAxis::X;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Role")
	EABTSM73BeamD1StructuralRole StructuralRole =
		EABTSM73BeamD1StructuralRole::PrimaryFrame;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Role")
	bool bWeaknessCandidate = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Role")
	EABTSM73BeamD1DeviceRole DeviceRole = EABTSM73BeamD1DeviceRole::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Brick")
	FABTSM7BrickSpec BrickSpec;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Brick")
	FTransform LocalTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Brick")
	FBox LocalBounds = FBox(EForceInit::ForceInit);
};

/** One rigid device bound to an integer 36 cm occupancy box. Devices are not
 * split into fake brick/load nodes; their support members remain Stage-5 ids. */
USTRUCT(BlueprintType)
struct FABTSM73BeamD1DeviceBinding
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 DeviceId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Device")
	EABTSM7ModuleKind Kind = EABTSM7ModuleKind::ExplosiveBarrel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Device")
	EABTSM73BeamAFrameAxis Axis = EABTSM73BeamAFrameAxis::Z;

	/** Inclusive minimum occupied cell in the building-local 36 cm grid. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel")
	FIntVector VoxelMin = FIntVector::ZeroValue;

	/** Local-space phase of this building's unified 36 cm grid. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel", meta = (Units = "cm"))
	FVector GridOriginCM = FVector::ZeroVector;

	/** Occupancy and contact cell size; fixed by the Stage-1..5 grid contract. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel", meta = (Units = "cm"))
	float VoxelCellSizeCM = 36.0f;

	/** Positive occupied cell counts along local X/Y/Z. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel")
	FIntVector VoxelExtent = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Device")
	FABTSM7DeviceSpec DeviceSpec;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Device")
	FTransform LocalTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Device")
	FBox LocalBounds = FBox(EForceInit::ForceInit);

	/** Stage-5 member ids carrying the device bottom contact cells. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Static DAG")
	TArray<int32> SupportMemberIds;

	/** True when the device occupies free ground cells inside the envelope. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Static DAG")
	bool bDirectGroundSupport = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Static DAG")
	int32 SupportContactCellCount = 0;

	/** Preliminary deterministic mass used only by the derived device ledger. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Static DAG", meta = (Units = "kg"))
	float StaticMassKG = 0.0f;

	/** Conservative swept/effect corridor; it is diagnostic, not a brick. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Device")
	FBox EffectCorridorLocalBounds = FBox(EForceInit::ForceInit);
};

USTRUCT(BlueprintType)
struct FABTSM73BeamD1Summary
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	bool bAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	EABTSM73BeamC3GenerationStage GenerationStage =
		EABTSM73BeamC3GenerationStage::StaticDAG;

	/** Stage-local Bearing/Load DAG ran; this never means Chaos was evaluated. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	bool bStageStaticDAGEvaluated = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	FName GameplayProfileId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 DifficultyTier = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	FName ResolvedM7ProfileId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 MemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 BrickCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Complexity")
	int32 TargetMinimumBrickCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Complexity")
	int32 TargetMaximumBrickCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Complexity")
	int32 VisualCandidateAttempt = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Complexity")
	int32 SemanticVolumeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Complexity")
	int32 SemanticBoxCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Complexity")
	int32 SemanticPrismCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Complexity")
	int32 SemanticPyramidCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Complexity")
	int32 RoofCourseBrickCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Complexity")
	int32 DistinctMotifCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Complexity")
	int32 SupportedSpanCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Complexity")
	bool bVisualComplexityCertified = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Assembly Quality")
	int32 XColumnStationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Assembly Quality")
	int32 YColumnStationCount = 0;

	/** Lower normalized station density divided by the higher density. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Assembly Quality")
	float AxisStationDensityRatio = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Assembly Quality")
	float StructuralClosurePostRatio = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Assembly Quality")
	bool bAssemblyQualityCertified = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int32 CompleteReferenceCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Material")
	int32 WoodBrickCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Material")
	int32 StoneBrickCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Material")
	int32 IronBrickCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Material")
	int32 GlassBrickCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Role")
	int32 WeaknessCandidateCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Role")
	int32 DeviceRoleCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Role")
	EABTSM73BeamAMemberRole WeaknessCandidateMemberRole =
		EABTSM73BeamAMemberRole::Post;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int32 StrictPenetrationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Structural Closure")
	int32 StructuralClosurePassCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Structural Closure")
	int32 AddedStructuralSupportPostCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Structural Closure")
	int32 RealContactMismatchCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Structural Closure")
	int32 RemainingSupportViolationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Structural Closure")
	int32 SupportResultantAdvisoryCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stability Core")
	bool bStabilityCoreCertified = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stability Core")
	int32 StabilityCoreHostCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stability Core")
	int32 StabilityCoreBeltCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stability Core")
	int32 StabilityCoreTieCourseCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stability Core")
	int32 StabilityRootedExistingCourseCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stability Core")
	int32 ReusedStabilityCoreMemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stability Core")
	int32 InsertedStabilityCoreMemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stability Core")
	int32 StabilityCoreNetMemberDelta = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stability Core",
		meta = (Units = "cm"))
	float MaximumUnbracedCorePostSpanBeforeCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stability Core",
		meta = (Units = "cm"))
	float MaximumUnbracedCorePostSpanAfterCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stability Core")
	int64 StabilityCorePlanHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stability Core")
	int64 StabilityRootedEvidenceHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geometry")
	FBox LocalBounds = FBox(EForceInit::ForceInit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int64 ResolvedSettingsHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int64 UpstreamBeamHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int64 BrickGeometryHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	FString RejectReason;

	/** Stage-1 V2 geometry/DAG certificate. Chaos is intentionally separate. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coupled Exterior Frame")
	bool bCoupledExteriorFrameCertified = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coupled Exterior Frame")
	int32 CoupledExteriorFrameCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coupled Exterior Frame")
	int32 CoupledExteriorFrameMacroBandCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coupled Exterior Frame|Role")
	int32 CoupledExteriorFrameCoreRailCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coupled Exterior Frame|Role")
	int32 CoupledExteriorFrameThroughOutriggerCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coupled Exterior Frame|Role")
	int32 CoupledExteriorFrameFacadeRailCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coupled Exterior Frame|Role")
	int32 CoupledExteriorFrameExteriorPostCount = 0;

	/** SeamRelease.E6-only; false for every ordinary single-cell result. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coupled Exterior Frame|Shared Course")
	bool bCoupledExteriorFrameSharedCourseCertified = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coupled Exterior Frame|Shared Course")
	int32 CoupledExteriorFrameSharedCourseCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coupled Exterior Frame|Shared Course")
	int64 CoupledExteriorFrameSharedCourseHash = 0;

	/** Four low bits are -X, +X, -Y and +Y grounded faces. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coupled Exterior Frame")
	uint8 CoupledExteriorFrameGroundedFaceMask = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coupled Exterior Frame",
		meta = (Units = "cm"))
	float CoupledExteriorFrameMaximumMemberSpanCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coupled Exterior Frame",
		meta = (Units = "cm"))
	float CoupledExteriorFrameMaximumPostSegmentSpanCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coupled Exterior Frame|Identity")
	int64 CoupledExteriorFramePlanHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coupled Exterior Frame|Identity")
	int64 CoupledExteriorFrameFinalGeometryHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coupled Exterior Frame|Identity")
	int64 CoupledExteriorFrameDAGEvidenceHash = 0;

	/** False until the separately authorized full-building Chaos phase runs. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coupled Exterior Frame")
	bool bPhysicalStabilityEvaluated = false;

	/** Stage-1 V3 skeleton-first geometry and static-DAG certificate. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First")
	bool bSkeletonFirstCertified = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Performance")
	bool bSkeletonFirstTimingEvaluated = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Performance")
	bool bSkeletonFirstWithinTimeBudget = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Performance", meta = (Units = "ms"))
	double SkeletonFirstTimeBudgetMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Performance", meta = (Units = "ms"))
	double SkeletonFirstTotalMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Performance", meta = (Units = "ms"))
	double SkeletonFirstTerminalDemandMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Performance", meta = (Units = "ms"))
	double SkeletonFirstChildCandidateMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Performance", meta = (Units = "ms"))
	double SkeletonFirstPodiumMainCandidateMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Performance", meta = (Units = "ms"))
	double SkeletonFirstJointSelectionMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Performance", meta = (Units = "ms"))
	double SkeletonFirstMemberEmissionMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Performance", meta = (Units = "ms"))
	double SkeletonFirstStaticDAGMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Performance")
	FString SkeletonFirstTimeoutPhase;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First")
	int32 SkeletonFirstGroundedComponentCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Semantic Support")
	int32 SkeletonFirstSemanticSupportNodeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Semantic Support")
	int32 SkeletonFirstSemanticSupportLedgerCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Semantic Support")
	int32 SkeletonFirstSemanticTerminalDemandCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Semantic Support")
	int32 SkeletonFirstSemanticTerminalDemandWithoutContinuousFitCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Semantic Support|Identity")
	int64 SkeletonFirstSemanticSupportDemandHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Semantic Support|Core Binding")
	int32 SkeletonFirstSemanticDemandCoreBindingCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Semantic Support|Core Binding")
	int32 SkeletonFirstUnmappedSemanticDemandCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Semantic Support|Core Binding")
	int32 SkeletonFirstAmbiguousSemanticDemandCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Semantic Support|Core Binding")
	int32 SkeletonFirstSemanticDemandChildOutsideBodyCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Semantic Support|Core Binding")
	int32 SkeletonFirstSemanticDemandChildWithoutDirectMainCouplingCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Semantic Support|Core Binding")
	int32 SkeletonFirstReusedTowerChildBindingCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Semantic Support|Core Binding")
	int32 SkeletonFirstUnreferencedTowerChildCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Semantic Support|Core Binding|Identity")
	int64 SkeletonFirstSemanticDemandCoreBindingHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Support Province")
	int32 SkeletonFirstSupportProvinceCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Support Province")
	int32 SkeletonFirstMultiDemandSupportProvinceCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Support Province")
	int32 SkeletonFirstSupportProvinceGroundCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Support Province")
	int32 SkeletonFirstSupportProvinceBoundaryCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Support Province")
	int32 SkeletonFirstSupportProvinceTieBreakCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Support Province")
	int32 SkeletonFirstSupportProvinceNearestSeedFallbackCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Support Province")
	int32 SkeletonFirstBoundSupportProvinceCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Support Province")
	int32 SkeletonFirstDistinctProvinceGroundCoreCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Support Province|Identity")
	int64 SkeletonFirstSupportProvinceHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Support Province|Identity")
	int64 SkeletonFirstSupportProvinceMainBindingHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First")
	int32 SkeletonFirstCoreCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Core Merge")
	int32 SkeletonFirstCoreMergeRegionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Core Merge")
	int32 SkeletonFirstMergedGroundComponentCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Core Merge")
	int32 SkeletonFirstMaximumCoreRailCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Core Merge")
	int32 SkeletonFirstCoreBearingPatchCountPerInterface = 0;

	/**
	 * Explicit compact grounded cores. The base is Body-owned; only a required
	 * shared-course sandwich may extend the same continuous core into Crown.
	 * This is not an occupied-raster-cell count.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Core")
	int32 SkeletonFirstExplicitCoreCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Core")
	int32 SkeletonFirstGroundedCoreCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Core")
	int32 SkeletonFirstSuspendedCoreCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Shell")
	int32 SkeletonFirstShellMemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Shell")
	int32 SkeletonFirstCoreDerivedShellMemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Shared Course")
	int32 SkeletonFirstSharedCourseCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Shared Course")
	int32 SkeletonFirstSharedCourseNonCoreEndpointViolationCount = 0;

	/** Number of endpoint-core rail slots physically replaced by shared rails. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Shared Course")
	int32 SkeletonFirstSharedCourseReplacementSlotCount = 0;

	/** Shared rails emitted outside their declared common course band. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Shared Course")
	int32 SkeletonFirstSharedCourseBandViolationCount = 0;

	/** Stage-1 requires one candidate-wide building group, not one shell per core. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Common Shell")
	int32 SkeletonFirstBuildingGroupCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Common Shell")
	int32 SkeletonFirstCommonShellMemberCount = 0;

	/** Distinct planned cores reached by the candidate-wide common shell. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Common Shell")
	int32 SkeletonFirstCommonShellConnectedCoreCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First")
	int32 SkeletonFirstSupportPlaneCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First")
	int32 SkeletonFirstVisibleFeatureCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First")
	int32 SkeletonFirstPlannedMemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First")
	int32 SkeletonFirstEmittedMemberCount = 0;

	/** V3 structural generation/Beam-C/D1 are executed once after WFC preselection. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First")
	int32 SkeletonFirstStructuralAttemptCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Identity")
	int32 SkeletonFirstCandidateSeed = 0;

	/** Four low bits are -X, +X, -Y and +Y grounded faces. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First")
	uint8 SkeletonFirstGroundedFaceMask = 0;

	/** Minimum distinct grounded exterior Z-post stations on each face of each component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First")
	int32 SkeletonFirstMinimumExteriorPostStationsPerFace = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First",
		meta = (Units = "cm"))
	float SkeletonFirstMaximumMemberSpanCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First",
		meta = (Units = "cm"))
	float SkeletonFirstMaximumPostSegmentSpanCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Identity")
	int64 SkeletonFirstEnvelopeHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Identity")
	int64 SkeletonFirstCoreMergeRegionHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Identity")
	int64 SkeletonFirstCorePlanHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Identity")
	int64 SkeletonFirstSupportPlanHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Identity")
	int64 SkeletonFirstFinalGeometryHash = 0;

	/** Exact accepted Stage-1 geometry consumed as the immutable Stage-2 input. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int64 SkeletonFirstStage1InputGeometryHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstCouplingCourseCount = 0;

	/** Four low bits are the Stage-2 -X,+X,-Y,+Y reached facade directions. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	uint8 SkeletonFirstCouplingFaceMask = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstCouplingParentViolationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstCouplingEndpointViolationCount = 0;

	/** Courses on the wrong building half or without one full block of net
	 * extension beyond the origin core face. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstCouplingOutwardViolationCount = 0;

	/** Couplings whose outward-only segment enters a non-origin core envelope. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstCouplingOtherCoreViolationCount = 0;

	/** Double-course bands whose two facade endpoints do not coincide. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstCouplingBandEndpointViolationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstResolvedFacadeEnvelopeVolumeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstResolvedFacadeEnvelopeRaisedVolumeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstResolvedFacadeEnvelopeBindingViolationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int64 SkeletonFirstResolvedFacadeEnvelopeHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int64 SkeletonFirstStage2InputFacadeEnvelopeHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstFacadePartitionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstFacadePartitionWithPerimeterCoreCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstFacadePartitionWithHeightAnchorCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstDeferredFacadePartitionCount = 0;

	/** Deferred partitions may contribute to more than one reason count. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstDeferredNoCoursePairPartitionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstDeferredNoEligibleCorePartitionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstDeferredNoFreeCrossStationPartitionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstDeferredNoStage1BearingPartitionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstDeferredNoFacadeTargetPartitionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstDeferredLengthLimitPartitionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstDeferredNotOutwardPartitionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstDeferredEnvelopeGapPartitionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstDeferredOtherCoreBlockedPartitionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstDeferredProtectedVoidPartitionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstDeferredMemberCollisionPartitionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstDeferredExhaustedCandidatePartitionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstFacadeHeightAnchorBandCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstFacadePartitionBindingViolationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstPerimeterCoreCount = 0;

	/** Sum of marked -X,+X,-Y,+Y face incidences across perimeter cores. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstPerimeterCoreFaceCount = 0;

	/** Exact per-course unoccluded exterior intervals consumed by Stage 3. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int32 SkeletonFirstPerimeterFaceExposureSpanCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2")
	int64 SkeletonFirstStage2PlanHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2|Timing")
	bool bSkeletonFirstStage2TimingEvaluated = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2|Timing")
	double SkeletonFirstStage2FacadeEnvelopeMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2|Timing")
	double SkeletonFirstStage2FacadeExtractionMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2|Timing")
	double SkeletonFirstStage2AnchorSearchMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2|Timing")
	double SkeletonFirstStage2MemberEmissionMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2|Timing")
	double SkeletonFirstStage2StaticDAGMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 2|Timing")
	double SkeletonFirstStage2TotalMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4TopSurfaceIntentCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4GroundSillIntentCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4ResolvedTopSurfaceIntentCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4ExposedSetbackTopIntentCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4DirectStackSeatIntentCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4UnresolvedIntentCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4IntentBindingViolationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int64 SkeletonFirstStage4IntentHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4|Timing")
	double SkeletonFirstStage4IntentMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4TopFrameSegmentCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4EmittedTopFrameSegmentCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4ReusedTopFrameSegmentCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4TopFrameBindingViolationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4TopFrameConflictCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "M7.3|Beam-C3|Stage 4")
	int32 SkeletonFirstStage4DeferredFacadeColumnJunctionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4FacadeToTopConnectionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4FacadeToTopSeatCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4FacadeToTopPostSegmentCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4SuppressedStage3ColumnMemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4ResolvedDeferredJunctionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4FacadeToTopBindingViolationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4FacadeToTopConflictCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int64 SkeletonFirstStage4FacadeToTopHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4|Timing")
	double SkeletonFirstStage4FacadeToTopMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4FloorSupportPairCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4FloorSpanCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4StyleInfillSpanCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4ReusedFloorSpanCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4DeferredFloorSpanCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4FloorBindingViolationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4FloorConflictCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4RoofReservedMemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int64 SkeletonFirstStage4FloorStyleInfillHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4|Timing")
	double SkeletonFirstStage4FloorStyleInfillMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4RoofCrownVolumeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4RoofBandCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4RoofCourseCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4EmittedRoofMemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4RoofPostMemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4ReusedRoofCarrierCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4DeferredRoofCandidateCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4OccludedRoofCourseCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4UnsupportedRoofMemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4RoofBindingViolationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int32 SkeletonFirstStage4RoofConflictCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int64 SkeletonFirstStage4RoofCrownHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4|Timing")
	double SkeletonFirstStage4RoofCrownMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4")
	int64 SkeletonFirstStage4TopFrameHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skeleton First|Stage 4|Timing")
	double SkeletonFirstStage4TopFrameMilliseconds = 0.0;

	/** Stage 5.5 derives devices without changing the frozen Stage-5 identity. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Device Assembly")
	bool bDeviceAssemblyEvaluated = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Device Assembly")
	int32 DeviceAssemblyCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Device Assembly")
	int32 DeviceAssemblySupportCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Device Assembly")
	int32 DeviceAssemblyUnsupportedCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Device Assembly|Identity")
	int64 DeviceSlotHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Device Assembly|Identity")
	int64 DeviceLoadDAGHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Device Assembly|Identity")
	int64 DeviceAssemblyHash = 0;
};
