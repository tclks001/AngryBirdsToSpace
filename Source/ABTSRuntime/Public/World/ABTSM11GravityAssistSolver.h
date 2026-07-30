// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "World/ABTSM11GravityAssistTypes.h"

/**
 * M11's sole orbital authority.
 *
 * The solver has no World, Actor, UObject, Chaos, frame delta or random-number
 * dependency. A later preview and flight provider must both consume this API.
 */
class ABTSRUNTIME_API FABTSM11GravityAssistSolver final
{
public:
	static bool Solve(
		const FABTSM11TrajectoryRequest& Request,
		FABTSM11TrajectoryResult& OutResult,
		FString* OutFailure = nullptr);

	static double ComputePrimarySpecificEnergy(
		const FABTSM11GravityBodySpec& Primary,
		const FVector3d& PositionCM,
		const FVector3d& VelocityCMPerSec);

	/** First segment/sphere intersection, including a segment that starts inside. */
	static bool SweptSphereFirstHit(
		const FVector3d& SegmentStartCM,
		const FVector3d& SegmentEndCM,
		const FVector3d& SphereCenterCM,
		double SphereRadiusCM,
		double& OutAlpha);
};
