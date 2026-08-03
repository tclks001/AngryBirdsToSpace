// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "World/ABTSM51OrdinarySlingshotSlotSnapshot.h"

struct FABTSM3MonthlySlingshotFieldResult;

/**
 * Integration-owned Preview/Test adapter for the M3R-3.1 candidate pool.
 *
 * This is deliberately not a production-selection API. The caller must name
 * one SourceRouteCandidateId, and the adapter never mutates or promotes the M3
 * result that it reads.
 */
struct ABTSRUNTIME_API FABTSM51OrdinarySlingshotSlotPreviewAdapter
{
	static bool BuildFromExplicitCandidate(
		const FABTSM3MonthlySlingshotFieldResult& Result,
		int32 ExplicitSourceRouteCandidateId,
		FABTSM51OrdinarySlingshotSlotSnapshot& OutSnapshot,
		FString& OutFailure);
};
