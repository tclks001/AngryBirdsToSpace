// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM51OrdinarySlingshotSlotSnapshot.h"

bool FABTSM51OrdinarySlingshotSlotSnapshot::IsStructurallyUsable() const
{
	if (LayoutHash == 0
		|| CandidateHash == 0
		|| MaxCordLengthCM < MinimumCordLengthCM
		|| MaxCordLengthCM > MaximumCordLengthCM
		|| SlotGroups.IsEmpty())
	{
		return false;
	}

	TSet<int32> UniqueCells;
	for (const FABTSM51OrdinarySlingshotSlotGroup& Group : SlotGroups)
	{
		if (Group.SlotCellIds.Num() < 2)
		{
			return false;
		}
		for (const int32 CellId : Group.SlotCellIds)
		{
			if (CellId < 0 || UniqueCells.Contains(CellId))
			{
				return false;
			}
			UniqueCells.Add(CellId);
		}
	}
	return true;
}

bool FABTSM51OrdinarySlingshotSlotSnapshot::TryBuildCellList(
	const int32 CellCount,
	TArray<int32>& OutCellIds) const
{
	OutCellIds.Reset();
	if (CellCount <= 0 || !IsStructurallyUsable())
	{
		return false;
	}

	int32 SlotCount = 0;
	for (const FABTSM51OrdinarySlingshotSlotGroup& Group : SlotGroups)
	{
		SlotCount += Group.SlotCellIds.Num();
	}
	OutCellIds.Reserve(SlotCount);
	for (const FABTSM51OrdinarySlingshotSlotGroup& Group : SlotGroups)
	{
		for (const int32 CellId : Group.SlotCellIds)
		{
			if (CellId >= CellCount)
			{
				OutCellIds.Reset();
				return false;
			}
			OutCellIds.Add(CellId);
		}
	}
	return true;
}
