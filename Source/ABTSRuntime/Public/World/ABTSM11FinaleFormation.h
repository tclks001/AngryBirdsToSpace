// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "World/ABTSM11FinaleInteractionTypes.h"

/** One dense, deterministic sample of the released presentation curve. */
struct ABTSRUNTIME_API FABTSM11FinaleFormationPathNode
{
	double TimeSeconds = 0.0;
	double ArcLengthCM = 0.0;
	FVector3d PositionCM = FVector3d::ZeroVector;
	FVector3d VelocityCMPerSec = FVector3d::ForwardVector;
};

/**
 * M6 presentation-only arc-length lookup for the four-bird train.
 *
 * It resamples the immutable released playback plan. It never changes plan
 * points, event times, hashes, collision authority or UFO contact semantics.
 */
struct ABTSRUNTIME_API FABTSM11FinaleFormationPath
{
	static constexpr int32 SubsamplesPerPlaybackSegment = 4;

	TArray<FABTSM11FinaleFormationPathNode> Nodes;
	double TotalArcLengthCM = 0.0;

	void Reset();
	bool Build(const FABTSM11PlaybackPlan& Plan, FString* OutFailure = nullptr);
	bool ResolveArcLengthAtTime(double TimeSeconds, double& OutArcLengthCM) const;
	bool SampleAtArcLength(
		double ArcLengthCM,
		FVector3d& OutPositionCM,
		FVector3d& OutVelocityCMPerSec) const;
};

