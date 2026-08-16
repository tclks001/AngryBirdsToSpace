// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Shared movement-state boundary for the secondary-body gravity mechanic. */
struct ABTSRUNTIME_API FABTSSatelliteGravityMovementPolicy
{
	/** Ground locomotion never consumes satellite gravity; slingshot flight does. */
	static FVector ResolveAcceleration(
		bool bIsBallisticFlight,
		const FVector& RawSatelliteAcceleration);
};
