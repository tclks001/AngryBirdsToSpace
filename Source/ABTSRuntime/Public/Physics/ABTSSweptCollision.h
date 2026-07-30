// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Small deterministic swept-shape helpers shared by M6 preview and M9 calibration. */
namespace ABTSSweptCollision
{
	/** Returns the first segment alpha in [0,1]. A segment starting inside returns zero. */
	ABTSRUNTIME_API bool SegmentSphereFirstAlpha(
		const FVector& Start,
		const FVector& End,
		const FVector& Center,
		float RadiusCM,
		float& OutAlpha);

	/**
	 * Exact sphere-versus-oriented-box sweep. The segment is partitioned at
	 * box-face crossings, where squared distance to the box is quadratic.
	 */
	ABTSRUNTIME_API bool SegmentExpandedOrientedBoxFirstAlpha(
		const FVector& Start,
		const FVector& End,
		const FTransform& BoxWorldTransform,
		const FVector& BoxHalfExtentCM,
		float SweptSphereRadiusCM,
		float& OutAlpha);

	/**
	 * Exact minimum clearance over a sphere-centre segment. The returned value
	 * is point-to-OBB distance minus sphere radius; misses are positive and
	 * contacts are zero or negative.
	 */
	ABTSRUNTIME_API float SegmentExpandedOrientedBoxMinimumClearance(
		const FVector& Start,
		const FVector& End,
		const FTransform& BoxWorldTransform,
		const FVector& BoxHalfExtentCM,
		float SweptSphereRadiusCM,
		float* OutClosestAlpha = nullptr);

	/** Exact signed sphere clearance from an oriented box. */
	ABTSRUNTIME_API float PointExpandedOrientedBoxClearance(
		const FVector& Point,
		const FTransform& BoxWorldTransform,
		const FVector& BoxHalfExtentCM,
		float SweptSphereRadiusCM);
}
