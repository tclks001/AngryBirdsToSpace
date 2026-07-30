// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWorld;

/** Read-only ideal-sphere collision descriptor for one M9 satellite. */
struct ABTSRUNTIME_API FABTSM9SatelliteBodySnapshot
{
	FVector CenterWorld = FVector::ZeroVector;
	float RadiusCM = 0.0f;
};

/** Read-only query used by the bird movement and M6 trajectory paths. */
namespace ABTSM9Gravity
{
	ABTSRUNTIME_API FVector GetSatelliteAcceleration(const UWorld* World, const FVector& WorldLocation);
	ABTSRUNTIME_API void GatherSatelliteBodySnapshots(
		const UWorld* World,
		TArray<FABTSM9SatelliteBodySnapshot>& OutSnapshots);
	/** Earliest hit against satellite spheres expanded by the actual bird radius. */
	ABTSRUNTIME_API bool FindFirstSatelliteBodyHit(
		const TArray<FABTSM9SatelliteBodySnapshot>& Snapshots,
		const FVector& Start,
		const FVector& End,
		float BirdCollisionRadiusCM,
		float& OutAlpha,
		int32& OutSnapshotIndex);
	/**
	 * Stable cache identity for every resolved M9 satellite, relative to the
	 * supplied primary centre. Actor names, pointers and absolute origin are excluded.
	 */
	ABTSRUNTIME_API uint64 GetSatelliteGravitySnapshotHash(
		const UWorld* World,
		const FVector& PrimaryCenterWorld);
}
