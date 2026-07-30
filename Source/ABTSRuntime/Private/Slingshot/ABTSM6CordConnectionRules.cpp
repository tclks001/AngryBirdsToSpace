// Copyright Epic Games, Inc. All Rights Reserved.

#include "Slingshot/ABTSM6CordConnectionRules.h"

namespace
{
bool IsFiniteVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X)
		&& FMath::IsFinite(Value.Y)
		&& FMath::IsFinite(Value.Z);
}

bool IsFiniteNonNegative(const float Value)
{
	return FMath::IsFinite(Value) && Value >= 0.0f;
}

float SegmentDistance(
	const FVector& StartA,
	const FVector& EndA,
	const FVector& StartB,
	const FVector& EndB)
{
	FVector ClosestA = FVector::ZeroVector;
	FVector ClosestB = FVector::ZeroVector;
	FMath::SegmentDistToSegmentSafe(
		StartA,
		EndA,
		StartB,
		EndB,
		ClosestA,
		ClosestB);
	return FVector::Distance(ClosestA, ClosestB);
}
}

FABTSM6CordConnectionResult FABTSM6CordConnectionRules::Evaluate(
	const FABTSM6CordConnectionQuery& Query)
{
	FABTSM6CordConnectionResult Result;
	if (!IsFiniteVector(Query.EndpointA)
		|| !IsFiniteVector(Query.EndpointB)
		|| !FMath::IsFinite(Query.MaxCordLengthCM)
		|| Query.MaxCordLengthCM <= 0.0f
		|| !FMath::IsFinite(Query.CandidateCordRadiusCM)
		|| Query.CandidateCordRadiusCM <= 0.0f
		|| !IsFiniteNonNegative(Query.ClearanceCM))
	{
		Result.RejectReason =
			EABTSM6CordConnectionRejectReason::InvalidInput;
		return Result;
	}

	Result.CandidateLengthCM =
		FVector::Distance(Query.EndpointA, Query.EndpointB);
	if (!FMath::IsFinite(Result.CandidateLengthCM))
	{
		Result.RejectReason =
			EABTSM6CordConnectionRejectReason::InvalidInput;
		return Result;
	}
	if (Result.CandidateLengthCM <= UE_KINDA_SMALL_NUMBER)
	{
		Result.RejectReason =
			EABTSM6CordConnectionRejectReason::DegenerateCandidate;
		return Result;
	}
	if (Result.CandidateLengthCM
		> Query.MaxCordLengthCM + UE_KINDA_SMALL_NUMBER)
	{
		Result.RejectReason =
			EABTSM6CordConnectionRejectReason::ExceedsMaximumLength;
		return Result;
	}

	for (int32 Index = 0; Index < Query.StakeObstacles.Num(); ++Index)
	{
		const FABTSM6StakeConnectionObstacle& Obstacle =
			Query.StakeObstacles[Index];
		if (!IsFiniteVector(Obstacle.SegmentStart)
			|| !IsFiniteVector(Obstacle.SegmentEnd)
			|| !FMath::IsFinite(Obstacle.RadiusCM)
			|| Obstacle.RadiusCM <= 0.0f
			|| FVector::DistSquared(
				Obstacle.SegmentStart,
				Obstacle.SegmentEnd) <= FMath::Square(UE_KINDA_SMALL_NUMBER))
		{
			Result.RejectReason =
				EABTSM6CordConnectionRejectReason::InvalidStakeObstacle;
			Result.BlockingObstacleIndex = Index;
			return Result;
		}
		const float Distance = SegmentDistance(
			Query.EndpointA,
			Query.EndpointB,
			Obstacle.SegmentStart,
			Obstacle.SegmentEnd);
		if (!FMath::IsFinite(Distance))
		{
			Result.RejectReason =
				EABTSM6CordConnectionRejectReason::InvalidStakeObstacle;
			Result.BlockingObstacleIndex = Index;
			return Result;
		}
		const float RejectionDistance =
			Query.CandidateCordRadiusCM
			+ Obstacle.RadiusCM
			+ Query.ClearanceCM;
		if (Distance <= RejectionDistance + UE_KINDA_SMALL_NUMBER)
		{
			Result.RejectReason =
				EABTSM6CordConnectionRejectReason::StakeObstacleBlocked;
			Result.BlockingDistanceCM = Distance;
			Result.BlockingObstacleIndex = Index;
			return Result;
		}
	}

	for (int32 Index = 0; Index < Query.CordObstacles.Num(); ++Index)
	{
		const FABTSM6CordConnectionObstacle& Obstacle =
			Query.CordObstacles[Index];
		if (!IsFiniteVector(Obstacle.SegmentStart)
			|| !IsFiniteVector(Obstacle.SegmentEnd)
			|| !FMath::IsFinite(Obstacle.RadiusCM)
			|| Obstacle.RadiusCM <= 0.0f
			|| FVector::DistSquared(
				Obstacle.SegmentStart,
				Obstacle.SegmentEnd) <= FMath::Square(UE_KINDA_SMALL_NUMBER))
		{
			Result.RejectReason =
				EABTSM6CordConnectionRejectReason::InvalidCordObstacle;
			Result.BlockingObstacleIndex = Index;
			return Result;
		}
		const float Distance = SegmentDistance(
			Query.EndpointA,
			Query.EndpointB,
			Obstacle.SegmentStart,
			Obstacle.SegmentEnd);
		if (!FMath::IsFinite(Distance))
		{
			Result.RejectReason =
				EABTSM6CordConnectionRejectReason::InvalidCordObstacle;
			Result.BlockingObstacleIndex = Index;
			return Result;
		}
		const float RejectionDistance =
			Query.CandidateCordRadiusCM
			+ Obstacle.RadiusCM
			+ Query.ClearanceCM;
		if (Distance <= RejectionDistance + UE_KINDA_SMALL_NUMBER)
		{
			Result.RejectReason =
				EABTSM6CordConnectionRejectReason::CordObstacleBlocked;
			Result.BlockingDistanceCM = Distance;
			Result.BlockingObstacleIndex = Index;
			return Result;
		}
	}

	Result.RejectReason = EABTSM6CordConnectionRejectReason::None;
	return Result;
}

const TCHAR* FABTSM6CordConnectionRules::GetRejectReasonName(
	const EABTSM6CordConnectionRejectReason Reason)
{
	switch (Reason)
	{
	case EABTSM6CordConnectionRejectReason::None:
		return TEXT("None");
	case EABTSM6CordConnectionRejectReason::InvalidInput:
		return TEXT("InvalidInput");
	case EABTSM6CordConnectionRejectReason::DegenerateCandidate:
		return TEXT("DegenerateCandidate");
	case EABTSM6CordConnectionRejectReason::ExceedsMaximumLength:
		return TEXT("ExceedsMaximumLength");
	case EABTSM6CordConnectionRejectReason::InvalidStakeObstacle:
		return TEXT("InvalidStakeObstacle");
	case EABTSM6CordConnectionRejectReason::StakeObstacleBlocked:
		return TEXT("StakeObstacleBlocked");
	case EABTSM6CordConnectionRejectReason::InvalidCordObstacle:
		return TEXT("InvalidCordObstacle");
	case EABTSM6CordConnectionRejectReason::CordObstacleBlocked:
		return TEXT("CordObstacleBlocked");
	default:
		return TEXT("Unknown");
	}
}
