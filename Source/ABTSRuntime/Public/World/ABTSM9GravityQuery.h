// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWorld;

/** Read-only query used by the bird movement and M6 trajectory paths. */
namespace ABTSM9Gravity
{
	ABTSRUNTIME_API FVector GetSatelliteAcceleration(const UWorld* World, const FVector& WorldLocation);
	/**
	 * Stable cache identity for every resolved M9 satellite, relative to the
	 * supplied primary centre. Actor names, pointers and absolute origin are excluded.
	 */
	ABTSRUNTIME_API uint64 GetSatelliteGravitySnapshotHash(
		const UWorld* World,
		const FVector& PrimaryCenterWorld);
}
