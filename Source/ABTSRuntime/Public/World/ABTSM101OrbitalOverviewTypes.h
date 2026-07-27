// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** One already-projected line segment. Coordinates remain in fitted-plane centimetres. */
struct FABTSM101OrbitalLineSegment
{
	FVector2D Start = FVector2D::ZeroVector;
	FVector2D End = FVector2D::ZeroVector;
	bool bDashed = false;
};

/** Ideal sphere projected orthographically into the fitted trajectory plane. */
struct FABTSM101OrbitalBody
{
	FVector2D Center = FVector2D::ZeroVector;
	float RadiusCM = 0.0f;
	bool bPrimary = false;
};

/**
 * Read-only presentation snapshot built from M6's authoritative trajectory.
 * No world integration or screen-resolution-specific data lives here.
 */
struct FABTSM101OrbitalOverviewSnapshot
{
	void Reset()
	{
		*this = FABTSM101OrbitalOverviewSnapshot();
	}

	bool bValid = false;
	FVector2D ContentCenter = FVector2D::ZeroVector;
	float ContentRadiusCM = 1.0f;
	FVector2D LaunchPoint = FVector2D::ZeroVector;
	bool bHasLandingPoint = false;
	FVector2D LandingPoint = FVector2D::ZeroVector;
	float SourcePathLengthCM = 0.0f;
	int32 SourcePointCount = 0;
	TArray<FABTSM101OrbitalLineSegment> TrajectorySegments;
	TArray<FABTSM101OrbitalLineSegment> PrimaryGridSegments;
	TArray<FABTSM101OrbitalBody> Bodies;
};
