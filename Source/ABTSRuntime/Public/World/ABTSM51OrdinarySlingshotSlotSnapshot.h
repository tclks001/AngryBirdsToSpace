// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * One accepted ordinary slingshot slot group.
 *
 * Group membership is presentation/spawn data only. It never grants or denies
 * permission to connect two ordinary stakes.
 */
struct ABTSRUNTIME_API FABTSM51OrdinarySlingshotSlotGroup
{
	TArray<int32> SlotCellIds;
};

/**
 * Minimal read-only M3 -> M5.1 hand-off for an accepted monthly layout.
 *
 * This consumer-owned value type deliberately contains no M3 candidate array,
 * encounter identity, field identity, allowed-pair edges, or UObject reference.
 * Until M3R-4/R-6 publishes one accepted candidate, production code must keep
 * using the compatibility TaskGraph adapter and must not manufacture this
 * snapshot from RetainedCandidates[0].
 */
struct ABTSRUNTIME_API FABTSM51OrdinarySlingshotSlotSnapshot
{
	static constexpr int32 MinimumCordLengthCM = 100;
	static constexpr int32 MaximumCordLengthCM = 4000;

	/** Opaque identities of the accepted monthly layout and selected candidate. */
	uint64 LayoutHash = 0;
	uint64 CandidateHash = 0;

	/** The only authored distance gate for ordinary stake-to-stake connections. */
	int32 MaxCordLengthCM = 0;

	/** Stable group-local slot order. Groups do not define legal cord pairs. */
	TArray<FABTSM51OrdinarySlingshotSlotGroup> SlotGroups;

	/** Structural validation independent from a particular planet topology. */
	bool IsStructurallyUsable() const;

	/**
	 * Flattens every slot exactly once after validating it against CellCount.
	 * OutCellIds remains empty on failure.
	 */
	bool TryBuildCellList(int32 CellCount, TArray<int32>& OutCellIds) const;
};
