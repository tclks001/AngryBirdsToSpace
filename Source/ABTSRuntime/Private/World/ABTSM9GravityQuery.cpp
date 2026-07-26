// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM9GravityQuery.h"

#include "EngineUtils.h"
#include "World/ABTSM9Satellite.h"

FVector ABTSM9Gravity::GetSatelliteAcceleration(const UWorld* World, const FVector& WorldLocation)
{
	if (World == nullptr) return FVector::ZeroVector;
	FVector Acceleration = FVector::ZeroVector;
	for (TActorIterator<AABTSM9Satellite> It(World); It; ++It)
	{
		Acceleration += It->GetGravityAccelerationAt(WorldLocation);
	}
	return Acceleration;
}
