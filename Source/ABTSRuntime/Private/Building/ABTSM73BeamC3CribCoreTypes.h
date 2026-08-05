// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73BeamAPreviewTypes.h"

/** M7-private policy resolved from GameplayProfileId and DifficultyTier. */
struct FABTSM73BeamC3CribCoreSettings
{
	bool bEnabled = true;

	/** Hard geometry target shared by every tier; tiers change density, not safety. */
	float MaximumUnbracedCorePostSpanCM = 720.0f;

	/** Reject decorative micro-cores that cannot provide a meaningful lever arm. */
	float MinimumCoreArmSpanCM = 144.0f;

	/** Desired number of alternating X/Y belt pairs on one selected core host. */
	int32 TargetBeltCount = 1;

	/** Bounded number of local crib hosts available to cover a large silhouette. */
	int32 MaximumHostCount = 8;

	/** Net member growth allowed before later Beam-C2 support closure. */
	int32 MaximumNetMemberIncrease = 8;

	/** Final D1 Brick ceiling; C3 is not allowed to silently expand it. */
	int32 MaximumFinalMemberCount = 49;

	/** Capacity retained for Beam-C2 support-resultant repairs. */
	int32 BeamC2MemberReserve = 2;

	/** At low tiers redundant roof lanes may fund the stability core. */
	bool bAllowRoofLaneBudgetReallocation = true;

	bool Validate(FString& OutError) const;
};

struct FABTSM73BeamC3CribCoreSummary
{
	bool bAccepted = false;
	bool bCoreTopologyCertified = false;
	bool bStabilityCoreCertified = false;
	int32 HostCount = 0;
	int32 BeltCount = 0;
	int32 ClosedCoreCourseCount = 0;
	int32 TargetedTieCourseCount = 0;
	/** Existing X/Y courses rooted into a certified crib through real contacts. */
	int32 RootedExistingCourseCount = 0;
	int32 CoreCornerBearingCount = 0;
	int32 ReusedCoreMemberCount = 0;
	int32 InsertedCoreMemberCount = 0;
	int32 RemovedBudgetDonorMemberCount = 0;
	int32 NetMemberDelta = 0;
	float MaximumUnbracedCorePostSpanBeforeCM = 0.0f;
	float MaximumUnbracedCorePostSpanAfterCM = 0.0f;
	int64 CorePlanHash = 0;
	int64 RootedEvidenceHash = 0;
	FString RejectReason;
};

struct FABTSM73BeamC3CribCoreHostPlan
{
	TArray<FVector2D> StationPositions;
	TArray<double> BeltMidZs;
	double MinimumZ = 0.0;
	double MaximumZ = 0.0;
	int32 BayId = INDEX_NONE;
	int32 SourceVolumeId = INDEX_NONE;
};

/** Geometry-stable identity for one tie rooted directly in a closed crib host. */
struct FABTSM73BeamC3TargetedTiePlan
{
	FVector2D AnchorStation = FVector2D::ZeroVector;
	FVector2D TargetStation = FVector2D::ZeroVector;
	EABTSM73BeamAFrameAxis Axis = EABTSM73BeamAFrameAxis::X;
	double CourseCenterZ = 0.0;
	double MinimumZ = 0.0;
	double MaximumZ = 0.0;
	/**
	 * A portal tie ends on a physically rooted perpendicular floor course
	 * instead of inventing another full-height core post at its far end.
	 */
	bool bAnchorIsRootedCourse = false;
	int32 AnchorHostPlanIndex = INDEX_NONE;
	int32 BayId = INDEX_NONE;
	int32 SourceVolumeId = INDEX_NONE;
};

struct FABTSM73BeamC3CribCoreResult
{
	FABTSM73BeamC3CribCoreSummary Summary;
	TArray<FABTSM73BeamC3CribCoreHostPlan> HostPlans;
	TArray<FABTSM73BeamC3TargetedTiePlan> TiePlans;
	/** First host mirrors retained for editor diagnostics. */
	TArray<FVector2D> CoreStationPositions;
	TArray<double> CoreBeltMidZs;
	int32 HostBayId = INDEX_NONE;
	int32 HostSourceVolumeId = INDEX_NONE;
};
