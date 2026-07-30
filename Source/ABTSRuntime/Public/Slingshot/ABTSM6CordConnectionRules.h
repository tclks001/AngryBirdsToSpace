// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Stable fail-closed result of the ordinary/Space cord geometry gate. */
enum class EABTSM6CordConnectionRejectReason : uint8
{
	None = 0,
	InvalidInput,
	DegenerateCandidate,
	ExceedsMaximumLength,
	InvalidStakeObstacle,
	StakeObstacleBlocked,
	InvalidCordObstacle,
	CordObstacleBlocked
};

/** Capsule-like obstruction represented by a visible stake centre line. */
struct ABTSRUNTIME_API FABTSM6StakeConnectionObstacle
{
	FVector SegmentStart = FVector::ZeroVector;
	FVector SegmentEnd = FVector::ZeroVector;
	float RadiusCM = 0.0f;
};

/** Existing cord obstruction represented by its two installed endpoints. */
struct ABTSRUNTIME_API FABTSM6CordConnectionObstacle
{
	FVector SegmentStart = FVector::ZeroVector;
	FVector SegmentEnd = FVector::ZeroVector;
	float RadiusCM = 0.0f;
};

/** Complete pure-data query used before any inventory or Actor mutation. */
struct ABTSRUNTIME_API FABTSM6CordConnectionQuery
{
	FVector EndpointA = FVector::ZeroVector;
	FVector EndpointB = FVector::ZeroVector;
	float MaxCordLengthCM = 0.0f;
	float CandidateCordRadiusCM = 0.0f;
	float ClearanceCM = 0.0f;
	TArray<FABTSM6StakeConnectionObstacle> StakeObstacles;
	TArray<FABTSM6CordConnectionObstacle> CordObstacles;
};

struct ABTSRUNTIME_API FABTSM6CordConnectionResult
{
	EABTSM6CordConnectionRejectReason RejectReason =
		EABTSM6CordConnectionRejectReason::InvalidInput;
	float CandidateLengthCM = 0.0f;
	float BlockingDistanceCM = 0.0f;
	int32 BlockingObstacleIndex = INDEX_NONE;

	bool IsAccepted() const
	{
		return RejectReason == EABTSM6CordConnectionRejectReason::None;
	}
};

/**
 * Deterministic three-dimensional cord validation.
 *
 * No collision trace is used: all obstruction checks use explicit segment
 * distance so NoCollision presentation meshes cannot change gameplay.
 */
class ABTSRUNTIME_API FABTSM6CordConnectionRules
{
public:
	static FABTSM6CordConnectionResult Evaluate(
		const FABTSM6CordConnectionQuery& Query);

	static const TCHAR* GetRejectReasonName(
		EABTSM6CordConnectionRejectReason Reason);
};
