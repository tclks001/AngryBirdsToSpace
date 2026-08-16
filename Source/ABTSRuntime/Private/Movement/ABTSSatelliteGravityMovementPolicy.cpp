// Copyright Epic Games, Inc. All Rights Reserved.

#include "Movement/ABTSSatelliteGravityMovementPolicy.h"

FVector FABTSSatelliteGravityMovementPolicy::ResolveAcceleration(
	const bool bIsBallisticFlight,
	const FVector& RawSatelliteAcceleration)
{
	return bIsBallisticFlight
		? RawSatelliteAcceleration
		: FVector::ZeroVector;
}
