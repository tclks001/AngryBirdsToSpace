// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AABTSM2Planet;
class UWorld;

/**
 * Resolves the production primary planet for ground movement, party following,
 * and the ground camera. M9 satellites derive from AABTSM2Planet, so raw
 * TActorIterator<AABTSM2Planet> order is never a valid primary-body authority.
 */
namespace ABTSPrimaryPlanetMovementAuthority
{
	ABTSRUNTIME_API bool IsPrimaryCandidate(const AABTSM2Planet* Planet);

	/** Keeps a valid cached primary or deterministically selects the largest ready non-satellite body. */
	ABTSRUNTIME_API AABTSM2Planet* Resolve(
		const UWorld* World,
		TWeakObjectPtr<AABTSM2Planet>& InOutCachedPrimary);
}
