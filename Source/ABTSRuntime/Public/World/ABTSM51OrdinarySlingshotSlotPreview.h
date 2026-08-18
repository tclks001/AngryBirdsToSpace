// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "World/ABTSM51OrdinarySlingshotSlotSnapshot.h"

struct FABTSM3MonthlySlingshotFieldResult;
struct FABTSM2Cell;
struct FABTSM3CellState;

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

/**
 * Integration-owned production adaptation for one explicitly selected monthly
 * snapshot.
 *
 * The frozen M3 candidate remains the source identity. This adapter removes
 * slots that are illegal on the final production surface, then performs a
 * deterministic topology walk from every original group root until each group
 * owns the required release capacity. Invalid roots remain traversal seeds:
 * removing them before the walk can strand a whole group inside a water or
 * building envelope and collapse the M5.1 world gate.
 */
struct ABTSRUNTIME_API FABTSM51OrdinarySlingshotSlotReleaseAdapter
{
	static constexpr int32 RequiredSlotsPerGroup = 12;

	static bool AdaptToProductionSurface(
		const TArray<FABTSM2Cell>& Cells,
		const TArray<FABTSM3CellState>& CellStates,
		int32 ExcludedFinaleCellId,
		FABTSM51OrdinarySlingshotSlotSnapshot& InOutSnapshot,
		FString& OutFailure,
		int32* OutRemovedInvalidSlots = nullptr,
		int32* OutAddedSlots = nullptr);
};
