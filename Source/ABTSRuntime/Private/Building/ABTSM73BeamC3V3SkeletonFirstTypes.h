// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ABTSM73BeamAGenerator.h"
#include "Building/ABTSM73BeamD1Types.h"

namespace ABTSM73BeamC3V3
{
	/** Stage-1 leaf work must remain interactive; matrices are timed per leaf. */
	inline constexpr double Stage1LeafTimeBudgetMilliseconds = 10000.0;

	enum class EGenerationStage : uint8
	{
		CoreAndShared,
		CompleteStaticDAG
	};

	/** Stable ownership recorded before the Beam-A compatible IR is emitted. */
	enum class EOwnerKind : uint8
	{
		CoreCell,
		ShellFace,
		Floor,
		Roof,
		SupportedSpan,
		BuildingGroupShell,
		StyleInfill
	};

	/** Structural purpose is deliberately independent from Beam-A's visual role. */
	enum class ESkeletonMemberKind : uint8
	{
		CoreCourse,
		ThroughCourse,
		FacadeCourse,
		ExteriorPost,
		FloorCourse,
		RoofCourse,
		SharedCourse,
		BridgeDiaphragm
	};

	/** Stage-1 semantic ownership. Every role remains physically ground-rooted. */
	enum class ECoreHierarchyRole : uint8
	{
		Continuous,
		PodiumMain,
		TowerChild,
		SharedEndpoint
	};

	enum EGroundedFace : uint8
	{
		NegativeX = 1 << 0,
		PositiveX = 1 << 1,
		NegativeY = 1 << 2,
		PositiveY = 1 << 3,
		AllFaces = NegativeX | PositiveX | NegativeY | PositiveY
	};

	/** One canonical member. Its endpoints are physical end planes, not block centres. */
	struct FPlannedMember
	{
		EABTSM73BeamC3GenerationStage ProducedStage =
			EABTSM73BeamC3GenerationStage::CoreAndShared;
		EOwnerKind OwnerKind = EOwnerKind::CoreCell;
		ESkeletonMemberKind SkeletonKind = ESkeletonMemberKind::CoreCourse;
		int32 OwnerId = INDEX_NONE;
		int32 ComponentId = INDEX_NONE;
		int32 SourceVolumeId = INDEX_NONE;
		/** The grounded core from which a non-shared member was grown. */
		int32 OriginCoreCellId = INDEX_NONE;
		int32 CourseIndex = INDEX_NONE;
		int32 StationA = INDEX_NONE;
		int32 StationB = INDEX_NONE;
		uint8 FaceMask = 0;
		EABTSM73BeamAFrameAxis Axis = EABTSM73BeamAFrameAxis::X;
		EABTSM73BeamAMemberRole Role = EABTSM73BeamAMemberRole::CoreCourse;
		FVector LocalStart = FVector::ZeroVector;
		FVector LocalEnd = FVector::ZeroVector;
		/** True only when the member's lower face is seated on the real ground plane. */
		bool bRequiresGroundSeat = false;
		/** Planned member indices which provide the immediately lower seat. */
		TArray<int32> RequiredLowerMemberIndices;
		/** Deterministic core-to-shell lineage; unlike lower seats, this need not touch vertically. */
		TArray<int32> RequiredInwardMemberIndices;
		/** Exactly two distinct core ids for a SharedCourse segment; empty otherwise. */
		TArray<int32> EndpointCoreCellIds;
		/** Stable logical lane id within one span/course. SharedCourse only. */
		int32 SharedLaneIndex = INDEX_NONE;
		/** Stable end-to-end segment ordinal within the logical lane. */
		int32 SharedSegmentIndex = INDEX_NONE;
		/** True for the unique segment which enters both endpoint cores. */
		bool bSharedCrossCoreSegment = false;
	};

	/** One unequal-core shared rail, possibly materialized as several <=720 cm members. */
	struct FSharedCourseLanePlan
	{
		int32 CourseIndex = INDEX_NONE;
		int32 LaneIndex = INDEX_NONE;
		int32 DonorCoreCellId = INDEX_NONE;
		int32 ReceiverCoreCellId = INDEX_NONE;
		double CrossStationCM = 0.0;
		double RequiredMinimumCM = 0.0;
		double RequiredMaximumCM = 0.0;
		TArray<int32> SegmentMemberIndices;
		int32 CrossCoreSegmentMemberIndex = INDEX_NONE;
		bool bReceiverSlotReplaced = false;
	};

	/** Pairing is frozen after all core cells are selected and before slot replacement. */
	struct FSharedCourseIntent
	{
		int32 SpanVolumeId = INDEX_NONE;
		int32 NegativeCoreCellId = INDEX_NONE;
		int32 PositiveCoreCellId = INDEX_NONE;
		EABTSM73BeamAFrameAxis Axis = EABTSM73BeamAFrameAxis::X;
		TArray<int32> CourseIndices;
		double OpeningMinimumCM = 0.0;
		double OpeningMaximumCM = 0.0;
		FBox PredictedBounds = FBox(EForceInit::ForceInit);
		TArray<FSharedCourseLanePlan> LanePlans;
	};

	/** Pure-data WFC reachability witness for one minimum grounded endpoint cell.
	 * It contains no emitted member identity and is safe to inspect even when the
	 * later shared-course plan fails closed. */
	struct FSharedEndpointReachabilityDiagnostic
	{
		int32 SpanVolumeId = INDEX_NONE;
		int32 ComponentId = INDEX_NONE;
		bool bNegativeEndpoint = false;
		int32 RequiredTopCourse = 0;
		int32 CandidateBaseSourceVolumeId = INDEX_NONE;
		int32 CandidateTopSourceVolumeId = INDEX_NONE;
		FString CandidateBranchPath;
		FBox CandidateBounds = FBox(EForceInit::ForceInit);
		double OpeningBoundaryCM = 0.0;
		double CandidateInnerFaceCM = 0.0;
		double EndpointInsetCM = 0.0;
		/** Opening length + this endpoint's 36 cm embed and non-negative inset. */
		double MinimumCrossContributionCM = 0.0;
		bool bTransverseOverlap = false;
		bool bGrounded = false;
		bool bBodyCrownStackCovered = false;
		/** WFC-only reachability does not yet know selected composite lanes. */
		bool bCompositeLaneConflictEvaluated = false;
		bool bCompositeLaneConflict = false;
		bool bReachableInWFC = false;
		FString FirstRejectReason;
	};

	/** Derived-only union of compatible grounded WFC bases. WFC itself is immutable. */
	struct FCoreMergeRegionPlan
	{
		int32 RegionId = INDEX_NONE;
		int32 ComponentId = INDEX_NONE;
		int32 SourceGroundComponentCount = 0;
		TArray<int32> SourceVolumeIds;
		/** Parallel to SourceVolumeIds/GroundSourceBounds; identity before
		 * lateral CoupledGround merging. Diagnostic-only. */
		TArray<int32> SourceOriginalGroundComponentIds;
		/** Exact source base prisms for diagnostics; never replace these with the union AABB. */
		TArray<FBox> GroundSourceBounds;
		FBox LocalBounds = FBox(EForceInit::ForceInit);
		int32 SelectedRailCount = 2;
	};

	/** 36 cm raster audit of how grounded core footprints occupy one derived
	 * podium. Visual thresholds are intentionally observational in Stage 1;
	 * accounting completeness and finite values are hard contracts. */
	struct FPodiumCoreCoverageDiagnostic
	{
		int32 RegionId = INDEX_NONE;
		int32 ComponentId = INDEX_NONE;
		FBox PodiumBounds = FBox(EForceInit::ForceInit);
		int32 TotalPodiumCellCount = 0;
		int32 MainCoveredCellCount = 0;
		int32 AnyCoreCoveredCellCount = 0;
		int32 AnyCoreUncoveredCellCount = 0;
		int32 PodiumMainCount = 0;
		int32 GroundedCoreCount = 0;
		FVector2D PodiumSupportAnchorCM = FVector2D::ZeroVector;
		bool bPodiumSupportAnchorCovered = false;
		double MainCoverageRatio = 0.0;
		double AnyCoreCoverageRatio = 0.0;
		double MaximumCorelessRadiusCM = 0.0;
		double PodiumCentroidToNearestCoreCM = 0.0;
		FBox MainCoreUnionBounds = FBox(EForceInit::ForceInit);
		/** Positive distances left uncovered from podium AABB sides: -X,+X,-Y,+Y. */
		FVector4 MainCoreBoundaryInsetsCM = FVector4(0.0, 0.0, 0.0, 0.0);
	};

	struct FPodiumSourceCoverageDiagnostic
	{
		int32 RegionId = INDEX_NONE;
		int32 ComponentId = INDEX_NONE;
		int32 SourceVolumeId = INDEX_NONE;
		int32 OriginalGroundComponentId = INDEX_NONE;
		FBox SourceBounds = FBox(EForceInit::ForceInit);
		int32 TotalCellCount = 0;
		int32 MainCoveredCellCount = 0;
		int32 AnyCoreCoveredCellCount = 0;
		int32 UncoveredCellCount = 0;
	};

	struct FPodiumUncoveredIslandDiagnostic
	{
		int32 RegionId = INDEX_NONE;
		int32 ComponentId = INDEX_NONE;
		int32 IslandId = INDEX_NONE;
		int32 CellCount = 0;
		FBox2D Bounds = FBox2D(EForceInit::ForceInit);
		/** -X,+X,-Y,+Y contacts against the complete podium AABB. */
		uint8 BoundaryDirectionMask = 0;
	};

	struct FPodiumMainSelectionDiagnostic
	{
		int32 RegionId = INDEX_NONE;
		int32 ComponentId = INDEX_NONE;
		int32 CoreCellId = INDEX_NONE;
		FBox CoreBounds = FBox(EForceInit::ForceInit);
		TArray<int32> CoveredHighProjectionRegionIds;
		TArray<int32> CoveredGroundSourceVolumeIds;
		int32 CoveredPodiumCellCount = 0;
		bool bCoversPodiumSupportAnchor = false;
		FString SelectionReason;
	};

	struct FPodiumMainOverlapDiagnostic
	{
		int32 RegionId = INDEX_NONE;
		int32 ComponentId = INDEX_NONE;
		int32 FirstCoreCellId = INDEX_NONE;
		int32 SecondCoreCellId = INDEX_NONE;
		double XOverlapCM = 0.0;
		double YOverlapCM = 0.0;
		double ProjectedOverlapAreaCM2 = 0.0;
	};

	/** One persistent terminal upper-load branch above a coupled podium.
	 * EntryBounds records the diagnostic podium-seam footprint, while
	 * TerminalBounds and RequiredTopCourse are the independent load demand.
	 * Every region must bind exactly one independently grounded TowerChild. */
	struct FHighProjectionRegionPlan
	{
		int32 RegionId = INDEX_NONE;
		int32 ComponentId = INDEX_NONE;
		/** Authoritative semantic support-column demand.  Several regions may
		 * intentionally share one legacy terminal slice after a Crown merge. */
		int32 SemanticDemandId = INDEX_NONE;
		int32 PodiumTopCourse = 0;
		/** Exclusive highest complete 36 cm course required by this branch. */
		int32 RequiredTopCourse = 0;
		/** Raw terminal course/component in the course-slice branch DAG. */
		int32 TerminalSliceCourse = INDEX_NONE;
		int32 TerminalSliceComponentId = INDEX_NONE;
		TArray<int32> SourceVolumeIds;
		FBox EntryBounds = FBox(EForceInit::ForceInit);
		FBox TerminalBounds = FBox(EForceInit::ForceInit);
		FBox LocalBounds = FBox(EForceInit::ForceInit);
		/** The grounded podium main that owns this branch in Stage 1.  When their
		 * footprints overlap it is also a direct bearing partner; separated child
		 * cores remain independently grounded and are coupled by Stage 2. */
		int32 BoundPodiumMainCoreCellId = INDEX_NONE;
		int32 BoundCoreCellId = INDEX_NONE;
	};

	/** Candidate accounting before and during the bounded podium-main/tower-child
	 * joint selection.  "Full height" means the maximum continuous course that
	 * any fixed 36 cm lattice footprint can reach inside this region before a
	 * podium main, sibling child or shared-endpoint reservation is imposed. */
	struct FFullHeightChildCandidateDiagnostic
	{
		int32 ComponentId = INDEX_NONE;
		int32 SemanticDemandId = INDEX_NONE;
		/** Stable index inside the component's demand list.  RegionId is later
		 * remapped to the plan-global region identity and must not be used to
		 * locate this row while siblings are being materialized. */
		int32 LocalProjectionIndex = INDEX_NONE;
		int32 RegionId = INDEX_NONE;
		int32 PodiumTopCourse = 0;
		int32 RequiredFullHeightCourse = 0;
		int32 EnumeratedFootprintCount = 0;
		int32 InvalidLatticeRejectCount = 0;
		int32 GroundSourceRejectCount = 0;
		int32 WFCEnvelopeRejectCount = 0;
		int32 WFCFullHeightWitnessCount = 0;
		int32 MainLaneConflictRejectCount = 0;
		/** Legal grounded candidates without direct alternating-course bearing
		 * contact to a selected podium main.  This is diagnostic/preference data,
		 * not a rejection count. */
		int32 NoDirectMainCouplingCandidateCount = 0;
		int32 SiblingLaneConflictRejectCount = 0;
		int32 SharedReservationRejectCount = 0;
		int32 JointFeasibleCandidateCount = 0;
		int32 SelectedPodiumMainCoreCellId = INDEX_NONE;
		FBox SelectedChildBounds = FBox(EForceInit::ForceInit);
		FString SelectionReason;
	};

	/** One bounded, deterministic joint-selection audit for a coupled podium. */
	struct FJointCoreSelectionDiagnostic
	{
		int32 ComponentId = INDEX_NONE;
		int32 HighProjectionRegionCount = 0;
		int32 SupportProvinceCount = 0;
		int32 PodiumMainCandidateCount = 0;
		int32 MainCandidateWithoutFullHeightCompatibilityCount = 0;
		/** Number of retained podium-main candidates that can host at least one
		 * full-height child for each terminal demand, before main/main conflicts. */
		TArray<int32> CompatibleMainCandidateCountByRegion;
		/** Number of retained mains whose rails pass through each terminal
		 * demand's podium-entry footprint.  This drives main placement; child
		 * compatibility is validated only after a spatial main set is chosen. */
		TArray<int32> PodiumCoverageMainCandidateCountByRegion;
		/** Retained main candidates whose footprint contains each province anchor. */
		TArray<int32> MainCandidateCountBySupportProvince;
		/** Number of unique conflict-free main-selection states searched. */
		int32 MainSelectionStateCount = 0;
		int32 MaximumCoveredRegionCount = 0;
		uint32 MaximumCoveredRegionMask = 0u;
		int32 MaximumCoveredSupportProvinceCount = 0;
		uint32 MaximumCoveredSupportProvinceMask = 0u;
		TArray<int32> BestPartialMainCandidateIndices;
		int32 MainSelectionsVisited = 0;
		int32 FullHeightFeasibleMainSelectionCount = 0;
		int32 SelectedPodiumMainCount = 0;
		bool bEveryRegionHasFullHeightChild = false;
		bool bEverySupportProvinceCovered = false;
		FString SelectionReason;
	};

	struct FHighProjectionSeedDiagnostic
	{
		int32 ComponentId = INDEX_NONE;
		int32 RegionId = INDEX_NONE;
		int32 SourceVolumeId = INDEX_NONE;
		FString DerivationPath;
		FBox SourceBounds = FBox(EForceInit::ForceInit);
	};

	struct FHighProjectionAdjacencyDiagnostic
	{
		int32 ComponentId = INDEX_NONE;
		int32 FirstSourceVolumeId = INDEX_NONE;
		int32 SecondSourceVolumeId = INDEX_NONE;
		double XOverlapCM = 0.0;
		double YOverlapCM = 0.0;
		double ZOverlapCM = 0.0;
		bool bPositiveAreaOverlap = false;
		bool bFullEdgeContact = false;
		bool bAccepted = false;
		FString Reason;
	};

	struct FHighProjectionSliceComponentDiagnostic
	{
		int32 ComponentId = INDEX_NONE;
		int32 SliceCourse = INDEX_NONE;
		int32 SliceComponentId = INDEX_NONE;
		double SliceMinZCM = 0.0;
		double SliceMaxZCM = 0.0;
		TArray<int32> SourceVolumeIds;
		FBox LocalBounds = FBox(EForceInit::ForceInit);
		TArray<int32> BoundTowerChildCoreCellIds;
	};

	struct FHighProjectionSplitDiagnostic
	{
		int32 ComponentId = INDEX_NONE;
		int32 LowerSliceCourse = INDEX_NONE;
		int32 LowerSliceComponentId = INDEX_NONE;
		TArray<int32> UpperSliceComponentIds;
		TArray<int32> UpperSourceVolumeIds;
	};

	struct FHighProjectionBranchBindingDiagnostic
	{
		int32 ComponentId = INDEX_NONE;
		int32 SliceCourse = INDEX_NONE;
		int32 SliceComponentId = INDEX_NONE;
		bool bSplitChild = false;
		bool bTerminal = false;
		bool bRequiresTowerChild = false;
		FBox LocalBounds = FBox(EForceInit::ForceInit);
		TArray<int32> SourceVolumeIds;
		TArray<int32> RequiredRegionIds;
		TArray<int32> BoundTowerChildCoreCellIds;
	};

	/** One immutable WFC semantic volume in the diagnostic Body -> Crown graph.
	 * Node ids are plan-global and never alias emitted core/member identities. */
	struct FSemanticSupportVolumeNodeDiagnostic
	{
		int32 NodeId = INDEX_NONE;
		int32 ComponentId = INDEX_NONE;
		int32 SourceVolumeId = INDEX_NONE;
		EABTSM73DAG5BV2VolumeRole Role = EABTSM73DAG5BV2VolumeRole::Body;
		EABTSM73DAG5BV2Primitive Primitive = EABTSM73DAG5BV2Primitive::Box;
		FString DerivationPath;
		FBox LocalBounds = FBox(EForceInit::ForceInit);
		TArray<int32> ParentNodeIds;
		TArray<int32> ChildNodeIds;
		bool bGrounded = false;
		bool bSyntheticCoupledGround = false;
		bool bSquareBody = false;
		/** True when the semantic support DAG node has no outgoing edge.  Crown
		 * leaves are graph terminals but are not automatically Body demands. */
	bool bGraphTerminal = false;
	/** True only when this graph leaf rises far enough above the coupled podium
	 * to require a distinct TowerChild rather than being carried by the main. */
	bool bTowerChildLoadLeaf = false;
	bool bTerminalBody = false;
		/** Number of distinct terminal load leaves reachable from a terminal Body. */
		int32 TerminalLoadBranchCount = 0;
		FString DemandClassificationReason;
	};

	/** One connected bipartite transition at a semantic support plane.  A row
	 * may be a continuation, split, merge, or simultaneous split/merge. */
	struct FSemanticSupportMergeLedgerDiagnostic
	{
		int32 LedgerId = INDEX_NONE;
		int32 ComponentId = INDEX_NONE;
		int32 ContactCourse = INDEX_NONE;
		double ContactZCM = 0.0;
		TArray<int32> LowerNodeIds;
		TArray<int32> UpperNodeIds;
		bool bSplit = false;
		bool bMerge = false;
	};

	/** Exact 36 cm lattice occupancy for one semantic course.  Bit index is
	 * (Y * SizeX + X), relative to MinimumXUnit/MinimumYUnit. */
	struct FSemanticSupportCourseOccupancyDiagnostic
	{
		int32 ComponentId = INDEX_NONE;
		int32 CourseIndex = INDEX_NONE;
		int32 MinimumXUnit = 0;
		int32 MinimumYUnit = 0;
		int32 SizeX = 0;
		int32 SizeY = 0;
		int32 OccupiedCellCount = 0;
		TArray<uint64> OccupiedWords;
		TArray<int32> SourceVolumeIds;
		FBox OccupiedBounds = FBox(EForceInit::ForceInit);
	};

	/** One independent support-column demand rooted in the highest Body volume,
	 * not in a roof terminal.  Several demands may intentionally share Crown
	 * descendants after a semantic roof merge. */
	struct FSemanticTerminalDemandDiagnostic
	{
		int32 DemandId = INDEX_NONE;
		int32 ComponentId = INDEX_NONE;
		int32 TerminalBodyNodeId = INDEX_NONE;
		int32 TerminalBodySourceVolumeId = INDEX_NONE;
		/** The terminal load leaf that keeps this demand distinct from sibling
		 * Crown branches sharing the same terminal Body. */
		int32 TerminalLoadNodeId = INDEX_NONE;
		int32 TerminalLoadSourceVolumeId = INDEX_NONE;
		int32 RequiredTopCourse = 0;
		FBox BodyBounds = FBox(EForceInit::ForceInit);
		FBox TerminalLoadBounds = FBox(EForceInit::ForceInit);
		FBox GroundProjectionBounds = FBox(EForceInit::ForceInit);
		FBox ContinuousCoreFitBounds = FBox(EForceInit::ForceInit);
		FBox LoadBranchBounds = FBox(EForceInit::ForceInit);
		TArray<int32> LineageNodeIds;
		TArray<int32> CrownSourceVolumeIds;
		TArray<int32> GroundSourceVolumeIds;
		TArray<int32> AdjacentDemandIds;
		/** Diagnostic-only ground catchment selected after the semantic graph closes. */
		int32 SupportProvinceId = INDEX_NONE;
		bool bHasContinuousCoreFit = false;
		bool bSharesMergedCrown = false;
	};

	/** Authoritative correspondence witness from SemanticTerminalDemand to the
	 * emitted Stage-1 hierarchy.  Generation assigns the identity directly and
	 * fails closed unless every demand owns exactly one spatially valid child.
	 * Direct child/main bearing remains diagnostic until Stage 2 defines the
	 * coupling path. */
	struct FSemanticDemandCoreBindingDiagnostic
	{
		int32 DemandId = INDEX_NONE;
		int32 ComponentId = INDEX_NONE;
		int32 SupportProvinceId = INDEX_NONE;
		int32 TerminalBodySourceVolumeId = INDEX_NONE;
		int32 TerminalLoadNodeId = INDEX_NONE;
		int32 TerminalLoadSourceVolumeId = INDEX_NONE;
		int32 CandidateRegionCount = 0;
		int32 CandidateChildCount = 0;
		int32 BoundHighProjectionRegionId = INDEX_NONE;
		int32 BoundTowerChildCoreCellId = INDEX_NONE;
		int32 AssignedPodiumMainCoreCellId = INDEX_NONE;
		int32 BoundChildDemandMultiplicity = 0;
		double BodyChildXYOverlapAreaCM2 = 0.0;
		bool bChildCenterInsideBodyXY = false;
		bool bChildInsideContinuousFitXY = false;
		bool bDirectMainCoupling = false;
		bool bAmbiguousRegionMatch = false;
		FBox DemandBodyBounds = FBox(EForceInit::ForceInit);
		FBox TerminalLoadBounds = FBox(EForceInit::ForceInit);
		FBox ContinuousFitBounds = FBox(EForceInit::ForceInit);
		FBox ChildBounds = FBox(EForceInit::ForceInit);
		FBox MainBounds = FBox(EForceInit::ForceInit);
		FString MappingReason;
	};

	/** A deterministic ground catchment for one or more overlapping terminal
	 * demand seeds.  Provinces partition course-0 semantic occupancy exactly;
	 * they do not yet alter podium/core geometry. */
	struct FSupportProvinceDiagnostic
	{
		int32 ProvinceId = INDEX_NONE;
		int32 ComponentId = INDEX_NONE;
		int32 StableSeedDemandId = INDEX_NONE;
		int32 MinimumXUnit = 0;
		int32 MinimumYUnit = 0;
		int32 SizeX = 0;
		int32 SizeY = 0;
		int32 GroundCellCount = 0;
		int32 TieBreakCellCount = 0;
		/** Stable occupied lattice cell nearest the province centroid. */
		int32 AnchorXUnit = 0;
		int32 AnchorYUnit = 0;
		bool bHasAnchorCell = false;
		/** Exclusive highest course for which every assigned ground cell remains occupied. */
		int32 HighestFullyOccupiedTopCourse = 0;
		/** Conservative, diagnostic-only local podium proposal. */
		int32 ProposedPodiumTopCourse = 0;
		int32 MinimumRequiredTopCourse = 0;
		TArray<uint64> GroundCellWords;
		TArray<int32> DemandIds;
		TArray<int32> TerminalBodyNodeIds;
		TArray<int32> GroundSourceVolumeIds;
		TArray<int32> AdjacentProvinceIds;
		FBox GroundBounds = FBox(EForceInit::ForceInit);
		FVector GroundCentroid = FVector::ZeroVector;
		/** Grounded core selected to serve this catchment after joint selection. */
		int32 BoundGroundCoreCellId = INDEX_NONE;
		bool bAnchorCoveredByBoundCore = false;
		bool bBoundToPodiumMain = false;
		bool bUsedNearestGroundSeed = false;
		bool bSyntheticGroundOnly = false;
	};

	/** One semantic separation-course decision for a support province.  This is
	 * diagnostic-only until the local podium plan receives visual approval. */
	struct FLocalPodiumHeightCandidateDiagnostic
	{
		int32 ProvinceId = INDEX_NONE;
		int32 ComponentId = INDEX_NONE;
		/** PodiumMain (or grounded fallback core) whose child-demand family owns
		 * the province's ground anchor.  It may differ from the structural parent
		 * and is retained to expose that distinction. */
		int32 BoundGroundCoreCellId = INDEX_NONE;
		/** Authoritative parent obtained from each demand's TowerChild.  Event
		 * courses and raised groups are shared only inside this family. */
		int32 StructuralPodiumMainCoreCellId = INDEX_NONE;
		int32 CandidateTopCourse = 0;
		int32 ActualPodiumTopCourse = 0;
		bool bActualBaseline = false;
		/** The course is a common semantic event on this province's own demand
		 * lineages. */
		bool bOwnSemanticBoundary = false;
		/** The course is contributed by any province whose TowerChild has the same
		 * structural PodiumMain parent.  This permits staggered WFC step events to be evaluated at a
		 * common physical height without mixing unrelated mains. */
		bool bSharedPodiumMainSemanticEvent = false;
		bool bCommonSemanticBoundary = false;
		bool bFullyOccupiedThroughCandidate = false;
		bool bCoversEveryDemandSeed = false;
		bool bSingleConnectedFootprint = false;
		bool bLeavesTwoChildCourses = false;
		bool bProtectedVoidClear = false;
		bool bAccepted = false;
		bool bSelected = false;
		/** Empty 36 cm cells between the nearest face-adjacent lattice cells of
		 * this candidate and any adjacent province in the same structural main
		 * family at the same course.  INDEX_NONE means no sibling candidate. */
		int32 MinimumSiblingFootprintGapUnits = INDEX_NONE;
		/** Cell count at this province's first accepted raised semantic event. */
		int32 FirstRaisedPersistentCellCount = 0;
		/** Candidate footprint / first-raised footprint, in per-mille. */
		int32 RetainedFootprintPermille = 0;
		/** A podium plate must retain at least half of its first-raised section;
		 * otherwise the semantic volume has become a tower neck. */
		bool bRetainsHalfFirstRaisedFootprint = false;
		/** The nearest sibling seam can be crossed without exceeding the 720 cm
		 * member hard gate. */
		bool bSiblingBridgeWithinMemberSpan = false;
		bool bSiblingBridgeVoidClear = false;
		int32 PersistentCellCount = 0;
		/** Same lattice as the owning support province. */
		TArray<uint64> PersistentCellWords;
		FString DecisionReason;
	};

	/** A connected set of adjacent provinces which selected the same highest
	 * legal common semantic separation course.  Once applied, every bound
	 * TowerChild consumes this height as the boundary between its lower
	 * PodiumMain leg and its upper child-only courses.  Stage 2 remains the
	 * authority which emits physical cross-leg coupling courses. */
	struct FLocalPodiumHeightRegionDiagnostic
	{
		int32 RegionId = INDEX_NONE;
		int32 ComponentId = INDEX_NONE;
		int32 StructuralPodiumMainCoreCellId = INDEX_NONE;
		int32 ActualPodiumTopCourse = 0;
		int32 SelectedTopCourse = 0;
		TArray<int32> ProvinceIds;
		/** Production TowerChild cells whose lower courses consume this region. */
		TArray<int32> AppliedTowerChildCoreCellIds;
		FBox GroundBounds = FBox(EForceInit::ForceInit);
		bool bRaisesActualPodium = false;
		bool bAppliedToProductionCoreHierarchy = false;
	};

	/** One compact, ground-rooted, pure-XY layered core selected inside a body union. */
	struct FCoreCellPlan
	{
		int32 CoreCellId = INDEX_NONE;
		int32 ComponentId = INDEX_NONE;
		/** Ground-level Body source; never a Crown source. */
		int32 BodySourceVolumeId = INDEX_NONE;
		int32 CoreMergeRegionId = INDEX_NONE;
		ECoreHierarchyRole HierarchyRole = ECoreHierarchyRole::Continuous;
		/** Required for TowerChild and INDEX_NONE for every other role. */
		int32 HighProjectionRegionId = INDEX_NONE;
		/** Required for TowerChild and INDEX_NONE for every other role. */
		int32 SemanticDemandId = INDEX_NONE;
		/** Dedicated SharedEndpoint ownership; INDEX_NONE for ordinary cores. */
		int32 SharedEndpointSpanVolumeId = INDEX_NONE;
		bool bNegativeSharedEndpoint = false;
		/** Semantic lineage only; never interpreted as a suspended bearing seat. */
		int32 PodiumMainCoreCellId = INDEX_NONE;
		/** TowerChild only: the selected local podium region consumed by this
		 * independently grounded cell. */
		int32 LocalPodiumHeightRegionId = INDEX_NONE;
		/** TowerChild only: courses below this exclusive boundary are the physical
		 * legs of the owning local PodiumMain; courses at/above it remain child-only.
		 * This changes production ownership, not bearing or Stage-2 coupling. */
		int32 LocalPodiumTopCourseIndex = 0;
		/** Exclusive upper course; members occupy [0, TopCourseIndex). */
		int32 TopCourseIndex = 0;
		/** Exclusive Body-only source range for this footprint. Courses at and
		 * above this boundary may use Crown sources. This is deliberately local
		 * to the core, not the maximum Body height of the merged component. */
		int32 BodyTopCourseIndex = 0;
		/** All overlapping ground-rooted cores coordinated on one lane lattice
		 * publish the same non-negative group identity. */
		int32 CompositeCoreGroupId = INDEX_NONE;
		/** Actual parent/child bearing contacts rebuilt from member geometry. */
		int32 CrossCoreBearingContactCount = 0;
		int32 RailCount = 2;
		FBox LocalBounds = FBox(EForceInit::ForceInit);
		/** The physical rail stations, in 36 cm lattice units. */
		TArray<int32> XStations;
		TArray<int32> YStations;
		/** Complete ordered course membership, RailCount rails per course. */
		TArray<int32> MemberIndices;
		/** TowerChild only: exact physical slots below LocalPodiumTopCourseIndex.
		 * These members remain ground-rooted child geometry, but are the production
		 * legs consumed by the owning local PodiumMain hierarchy. */
		TArray<int32> LocalPodiumLegMemberIndices;
	};

	/** A positive-area top/bottom witness which roots an upper semantic volume. */
	struct FVerticalSupportWitness
	{
		int32 LowerSourceVolumeId = INDEX_NONE;
		int32 UpperSourceVolumeId = INDEX_NONE;
		double ContactZCM = 0.0;
		FBox2D PositiveXYOverlap = FBox2D(EForceInit::ForceInit);
	};

	/** A semantic-root component and the exact integer lattice selected for it. */
	struct FComponentPlan
	{
		int32 ComponentId = INDEX_NONE;
		FString SemanticRootPath;
		FBox OccupiedBounds = FBox(EForceInit::ForceInit);
		FBox BodyBounds = FBox(EForceInit::ForceInit);
		double GroundPlaneZCM = 0.0;
		TArray<int32> SourceVolumeIds;
		TArray<int32> CrownVolumeIds;
		TArray<int32> GroundSourceVolumeIds;
		/** Number of vertical-only grounded components represented by this derived region. */
		int32 SourceGroundComponentCount = 1;
		int32 CoreMergeRegionId = INDEX_NONE;
		TArray<FVerticalSupportWitness> VerticalSupportWitnesses;
		TArray<int32> XGridUnits;
		TArray<int32> YGridUnits;
		/** Bottom planes of each adjacent X/Y structural band, in 36 cm units. */
		TArray<int32> BandBaseCourseIndices;
		uint8 GroundedFaceMask = 0;
		/** Distinct grounded exterior Z-post XY stations for -X,+X,-Y,+Y. */
		TArray<int32> GroundedExteriorPostStationCounts;
		int32 FirstPlannedMemberIndex = INDEX_NONE;
		int32 PlannedMemberCount = 0;
		uint32 ComponentCrc32 = 0;
	};

	/** Candidate-level coupled frame. Components remain separate ground proofs,
	 * while every core in this group contributes to one common outer frame. */
	struct FBuildingGroupPlan
	{
		int32 GroupId = INDEX_NONE;
		FString BuildingPath;
		double GroundPlaneZCM = 0.0;
		FBox LocalBounds = FBox(EForceInit::ForceInit);
		TArray<int32> ComponentIds;
		TArray<int32> CoreCellIds;
		TArray<int32> SpanVolumeIds;
		TArray<int32> CommonBandBaseCourseIndices;
		TArray<int32> MemberIndices;
		uint8 GroundedFaceMask = 0;
		/** Distinct grounded group-perimeter Z-post XY stations for -X,+X,-Y,+Y. */
		TArray<int32> GroundedExteriorPostStationCounts;
		uint32 GroupCrc32 = 0;
	};

	struct FPlanSummary
	{
		bool bAccepted = false;
		/** Stage-1 is deliberately static-only. This value may never be inferred. */
		bool bPhysicalStabilityEvaluated = false;
		/** Six-phase performance evidence for one CoreAndShared leaf. */
		bool bStage1TimingEvaluated = false;
		bool bStage1WithinTimeBudget = false;
		double Stage1TimeBudgetMilliseconds = Stage1LeafTimeBudgetMilliseconds;
		double Stage1TotalMilliseconds = 0.0;
		double TerminalDemandMilliseconds = 0.0;
		double ChildCandidateMilliseconds = 0.0;
		double PodiumMainCandidateMilliseconds = 0.0;
		double JointSelectionMilliseconds = 0.0;
		double MemberEmissionMilliseconds = 0.0;
		double StaticDAGMilliseconds = 0.0;
		FString Stage1TimeoutPhase;
		int32 GroundedComponentCount = 0;
		int32 VerticalWitnessCount = 0;
		int32 UnreachableVolumeCount = 0;
		int32 CoreCellCount = 0;
		int32 PodiumMainCoreCellCount = 0;
		int32 TowerChildCoreCellCount = 0;
		int32 HighProjectionRegionCount = 0;
		int32 BoundHighProjectionRegionCount = 0;
		int32 RequiredTerminalBranchCount = 0;
		int32 BoundTerminalBranchCount = 0;
		int32 SemanticSupportNodeCount = 0;
		int32 SemanticSupportLedgerCount = 0;
		int32 SemanticSupportSplitCount = 0;
		int32 SemanticSupportMergeCount = 0;
		int32 SemanticSupportCourseCount = 0;
		int32 SemanticTerminalLoadBranchCount = 0;
		int32 MultiBranchTerminalBodyCount = 0;
		int32 UnrepresentedSemanticTerminalLoadBranchCount = 0;
		int32 SemanticTerminalDemandCount = 0;
		int32 SemanticTerminalDemandWithoutContinuousFitCount = 0;
		int32 SemanticDemandCoreBindingCount = 0;
		int32 UnmappedSemanticDemandCount = 0;
		int32 AmbiguousSemanticDemandCount = 0;
		int32 SemanticDemandChildOutsideBodyCount = 0;
		int32 SemanticDemandChildWithoutDirectMainCouplingCount = 0;
		int32 ReusedTowerChildBindingCount = 0;
		int32 UnreferencedTowerChildCount = 0;
		int32 SupportProvinceCount = 0;
		int32 MultiDemandSupportProvinceCount = 0;
		int32 SupportProvinceGroundCellCount = 0;
		int32 SupportProvinceBoundaryCount = 0;
		int32 SupportProvinceTieBreakCellCount = 0;
		int32 SupportProvinceNearestSeedFallbackCount = 0;
		int32 BoundSupportProvinceCount = 0;
		int32 DistinctProvinceGroundCoreCount = 0;
		int32 LocalPodiumHeightCandidateCount = 0;
		int32 RejectedLocalPodiumHeightCandidateCount = 0;
		int32 LocalPodiumHeightRegionCount = 0;
		int32 RaisedLocalPodiumHeightRegionCount = 0;
		int32 AppliedLocalPodiumHeightRegionCount = 0;
		int32 LocalPodiumLegMemberCount = 0;
		int32 CoreMergeRegionCount = 0;
		int32 MergedGroundComponentCount = 0;
		int32 MaximumCoreRailCount = 0;
		int32 CoreBearingPatchCountPerInterface = 0;
		int32 CompositeCoreGroupCount = 0;
		int32 CrossCoreBearingContactCount = 0;
		int32 CompositeLaneConflictCount = 0;
		int32 PodiumCoverageDiagnosticCount = 0;
		int32 PodiumUncoveredCellCount = 0;
		int32 UncoveredPodiumSupportAnchorCount = 0;
		int32 ExplicitCoreCellCount = 0;
		int32 GroundedCoreCellCount = 0;
		int32 ShellMemberCount = 0;
		int32 CoreDerivedShellMemberCount = 0;
		/** Logical shared lanes; a lane may contain several physical members. */
		int32 SharedCourseCount = 0;
		int32 SharedCourseSegmentCount = 0;
		int32 SharedCourseCrossCoreSegmentCount = 0;
		int32 SharedCourseConflictOmissionCount = 0;
		int32 SharedCourseNonCoreEndpointViolationCount = 0;
		/** Donor slots plus only receiver slots whose conflicting rail was omitted. */
		int32 SharedCourseReplacementSlotCount = 0;
		int32 SharedCourseBandViolationCount = 0;
		int32 BuildingGroupCount = 0;
		int32 CommonShellMemberCount = 0;
		int32 CommonShellConnectedCoreCount = 0;
		int32 SuspendedCoreCount = 0;
		int32 SupportPlaneCount = 0;
		int32 SupportedSpanCount = 0;
		int32 RoofMemberCount = 0;
		int32 PlannedMemberCount = 0;
		int32 EmittedMemberCount = 0;
		int32 GroundSeatCount = 0;
		int32 PlannedSeatCount = 0;
		int32 VerifiedSeatCount = 0;
		int32 SeatMismatchCount = 0;
		int32 EnvelopeViolationCount = 0;
		int32 ProtectedVoidViolationCount = 0;
		int32 PenetrationCount = 0;
		int32 PlannedBayCount = 0;
		int32 PlannedJointCount = 0;
		int32 PlannedBearingContactCount = 0;
		int32 PlannedBearingPairCheckCount = 0;
		int32 MinimumBrickCount = 0;
		int32 MaximumBrickCount = 0;
		int32 BudgetMargin = 0;
		int32 DensityLevel = 0;
		/** The single resolved recipe used for this Profile/Tier; never candidate-scanned. */
		int32 HorizontalCellUnits = 0;
		int32 VerticalBandUnits = 0;
		int32 VisibleFeatureCount = 0;
		float MaximumMemberLengthCM = 0.0f;
		float MaximumPostSegmentSpanCM = 0.0f;
		double TotalMemberLengthCM = 0.0;
		double MinimumPodiumMainCoverageRatio = 0.0;
		double MinimumPodiumAnyCoreCoverageRatio = 0.0;
		double MaximumPodiumCorelessRadiusCM = 0.0;
		double MaximumPodiumCentroidToNearestCoreCM = 0.0;
		uint8 GroundedFaceMask = 0;
		TArray<uint8> ComponentGroundedFaceMasks;
		/** Minimum over every component and face; Stage-1 requires at least two. */
		int32 MinimumGroundedExteriorPostStationsPerFace = 0;
		int64 EnvelopeHash = 0;
		int64 CoreMergeRegionHash = 0;
		int64 CorePlanHash = 0;
		int64 SupportPlanHash = 0;
		/** Diagnostic identity only; deliberately excluded from Stage1 geometry hashes. */
		int64 SemanticSupportDemandHash = 0;
		/** Diagnostic identity only; excluded from every geometry hash. */
		int64 SemanticDemandCoreBindingHash = 0;
		/** Diagnostic support-catchment identity; excluded from all geometry hashes. */
		int64 SupportProvinceHash = 0;
		/** Selected grounded-core assignment for every support province. */
		int64 SupportProvinceMainBindingHash = 0;
		/** Deterministic local podium selection identity.  The selected boundary is
		 * also consumed by the production core hierarchy and CorePlanHash. */
		int64 LocalPodiumHeightPlanHash = 0;
		int64 FinalGeometryHash = 0;
		FString RejectReason;
	};

	struct FPlan
	{
		FPlanSummary Summary;
		FName GameplayProfileId;
		int32 DifficultyTier = INDEX_NONE;
		int64 ProfileCatalogHash = 0;
		int64 ResolvedSettingsHash = 0;
		int64 GrammarHash = 0;
		int64 WFCHash = 0;
		TArray<FComponentPlan> Components;
		TArray<FCoreMergeRegionPlan> CoreMergeRegions;
		TArray<FPodiumCoreCoverageDiagnostic> PodiumCoverageDiagnostics;
		TArray<FPodiumSourceCoverageDiagnostic> PodiumSourceCoverageDiagnostics;
		TArray<FPodiumUncoveredIslandDiagnostic> PodiumUncoveredIslandDiagnostics;
		TArray<FPodiumMainSelectionDiagnostic> PodiumMainSelectionDiagnostics;
		TArray<FPodiumMainOverlapDiagnostic> PodiumMainOverlapDiagnostics;
		TArray<FHighProjectionRegionPlan> HighProjectionRegions;
		TArray<FFullHeightChildCandidateDiagnostic>
			FullHeightChildCandidateDiagnostics;
		TArray<FJointCoreSelectionDiagnostic> JointCoreSelectionDiagnostics;
		TArray<FHighProjectionSeedDiagnostic> HighProjectionSeedDiagnostics;
		TArray<FHighProjectionAdjacencyDiagnostic> HighProjectionAdjacencyDiagnostics;
		TArray<FHighProjectionSliceComponentDiagnostic>
			HighProjectionSliceComponentDiagnostics;
		TArray<FHighProjectionSplitDiagnostic> HighProjectionSplitDiagnostics;
		TArray<FHighProjectionBranchBindingDiagnostic>
			HighProjectionBranchBindingDiagnostics;
		TArray<FSemanticSupportVolumeNodeDiagnostic>
			SemanticSupportVolumeNodes;
		TArray<FSemanticSupportMergeLedgerDiagnostic>
			SemanticSupportMergeLedger;
		TArray<FSemanticSupportCourseOccupancyDiagnostic>
			SemanticSupportCourseOccupancies;
		TArray<FSemanticTerminalDemandDiagnostic>
			SemanticTerminalDemands;
		TArray<FSemanticDemandCoreBindingDiagnostic>
			SemanticDemandCoreBindings;
		TArray<FSupportProvinceDiagnostic> SupportProvinces;
		TArray<FLocalPodiumHeightCandidateDiagnostic>
			LocalPodiumHeightCandidates;
		TArray<FLocalPodiumHeightRegionDiagnostic> LocalPodiumHeightRegions;
		TArray<FCoreCellPlan> CoreCells;
		TArray<FSharedEndpointReachabilityDiagnostic>
			SharedEndpointReachabilityDiagnostics;
		TArray<FSharedCourseIntent> SharedCourseIntents;
		TArray<FBuildingGroupPlan> BuildingGroups;
		TArray<FPlannedMember> Members;
		TArray<FABTSM73BeamASupportVoid> ReservedSupportVoids;
	};

	struct FGenerationResult
	{
		FPlan Plan;
		FABTSM73BeamAGenerationResult Assembly;
	};
}
