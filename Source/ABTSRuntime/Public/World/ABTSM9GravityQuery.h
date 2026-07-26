// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWorld;

/** Read-only query used by the bird movement and M6 trajectory paths. */
namespace ABTSM9Gravity
{
	ABTSRUNTIME_API FVector GetSatelliteAcceleration(const UWorld* World, const FVector& WorldLocation);
}
