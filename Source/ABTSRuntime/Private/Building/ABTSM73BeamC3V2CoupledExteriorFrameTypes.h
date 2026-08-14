// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73BeamAPreviewTypes.h"

namespace ABTSM73BeamC3V2
{
	/** Geometry role retained outside the public Beam-A IR. */
	enum class ECoupledExteriorFrameMemberKind : uint8
	{
		CoreRail,
		ThroughOutrigger,
		FacadeRail,
		ExteriorPost,
		/** One continuous rail embedded in both grounded cores. */
		SharedCourse
	};

	/** Bit contract used by static and production certification. */
	enum ECoupledExteriorFrameFace : uint8
	{
		NegativeX = 1 << 0,
		PositiveX = 1 << 1,
		NegativeY = 1 << 2,
		PositiveY = 1 << 3,
		AllFaces = NegativeX | PositiveX | NegativeY | PositiveY
	};

	/** One independently grounded semantic body cell selected by D1. */
	struct FCoupledExteriorFrameCellRequest
	{
		FBox LocalBounds = FBox(EForceInit::ForceInit);
		int32 BayId = INDEX_NONE;
		int32 SourceVolumeId = INDEX_NONE;
		/** Stable witness that the grounded root belongs to a local vertical Bay chain. */
		uint32 RootAuthorityCrc32 = 0;
		/** Optional SupportedSpan identity; INDEX_NONE for an ordinary single cell. */
		int32 CoupledSpanVolumeId = INDEX_NONE;
		/** Endpoint support Bay reached by the root-authority witness. */
		int32 CoupledSupportBayId = INDEX_NONE;
		/** -1 for the negative endpoint, +1 for positive, 0 for uncoupled. */
		int8 CoupledEndpointSign = 0;
		/** SeamRelease.E6-only atomic pair gate. Ordinary/multi-cell requests keep false. */
		bool bRequireSharedCoursePair = false;
		/** E6-only common local height; zero for every ordinary single cell. */
		int32 SharedCoursePairCourseCount = 0;
		/** X or Y axis of the Beam-B SupportedSpan consumed by the shared course. */
		EABTSM73BeamAFrameAxis CoupledSpanAxis =
			EABTSM73BeamAFrameAxis::Diagonal;
		/** Endpoint plane at which the protected Beam-B rail enters this core. */
		double CoupledBearingPlaneCM = 0.0;
		/** Authoritative Beam-B rail height consumed by the E6 shared course. */
		double CoupledRailCenterZCM = 0.0;
		/** Beam-B outer rail pair defining the shared R-course envelope. */
		TArray<double> CoupledRailStationsCM;
		/** Shared course bottom must clear the registered SupportedSpan void. */
		double CoupledMinimumSharedCourseBottomZCM = 0.0;
	};

	/** Immutable member geometry produced before any Beam-A closure rewrite. */
	struct FCoupledExteriorFramePlannedMember
	{
		ECoupledExteriorFrameMemberKind Kind =
			ECoupledExteriorFrameMemberKind::CoreRail;
		int32 CellIndex = INDEX_NONE;
		int32 CourseIndex = INDEX_NONE;
		int32 MacroBandIndex = INDEX_NONE;
		int32 RailIndex = INDEX_NONE;
		uint8 FaceMask = 0;
		EABTSM73BeamAFrameAxis Axis = EABTSM73BeamAFrameAxis::X;
		EABTSM73BeamAMemberRole Role = EABTSM73BeamAMemberRole::CoreCourse;
		FVector LocalStart = FVector::ZeroVector;
		FVector LocalEnd = FVector::ZeroVector;
		FBox LocalBounds = FBox(EForceInit::ForceInit);
	};

	struct FCoupledExteriorFrameCellPlan
	{
		FCoupledExteriorFrameCellRequest Cell;
		int32 CellIndex = INDEX_NONE;
		int32 CourseCount = 0;
		int32 RailCount = 0;
		int32 MacroBandCount = 0;
		uint8 GroundedFaceMask = 0;
		TArray<int32> MacroBandStartCourses;
		TArray<FCoupledExteriorFramePlannedMember> Members;
		uint32 GeometryCrc32 = 0;
	};

	struct FCoupledExteriorFrameSummary
	{
		bool bAccepted = false;
		bool bGeometryCertified = false;
		bool bDAGCertified = false;
		int32 RequestedCellCount = 0;
		int32 CellCount = 0;
		int32 BudgetLimitedCellCount = 0;
		int32 GeometryLimitedCellCount = 0;
		int32 ClosureReserveMemberCount = 0;
		int32 MacroBandCount = 0;
		int32 CoreRailCount = 0;
		int32 ThroughOutriggerCount = 0;
		int32 FacadeRailCount = 0;
		int32 ExteriorPostCount = 0;
		int32 SharedCourseCount = 0;
		int32 SharedCourseRailCount = 0;
		int32 ReplacedMemberCount = 0;
		int32 InsertedMemberCount = 0;
		int32 FinalMemberCount = 0;
		int32 RequiredBearingContactCount = 0;
		float MaximumMemberLengthCM = 0.0f;
		float MaximumPostSegmentSpanCM = 0.0f;
		uint8 GroundedFaceMask = 0;
		uint32 PlanCrc32 = 0;
		uint32 FinalGeometryCrc32 = 0;
		uint32 DAGEvidenceCrc32 = 0;
		uint32 SharedCourseCrc32 = 0;
		bool bSharedCoursePairCertified = false;
		FString RejectReason;
	};

	struct FCoupledExteriorFrameResult
	{
		FCoupledExteriorFrameSummary Summary;
		TArray<FCoupledExteriorFrameCellPlan> CellPlans;
		TArray<FCoupledExteriorFramePlannedMember> PlannedMembers;
	};
}
