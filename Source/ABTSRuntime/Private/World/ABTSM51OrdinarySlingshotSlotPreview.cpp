// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM51OrdinarySlingshotSlotPreview.h"

#include "PCG/ABTSM3MonthlySlingshotField.h"
#include "PCG/ABTSM3TaskGraphTypes.h"
#include "Planet/ABTSM2Planet.h"

namespace
{
	bool IsProductionSlotCellUsable(
		const TArray<FABTSM2Cell>& Cells,
		const TArray<FABTSM3CellState>& CellStates,
		const int32 CellId,
		const int32 ExcludedFinaleCellId)
	{
		return Cells.IsValidIndex(CellId)
			&& CellStates.IsValidIndex(CellId)
			&& CellId != ExcludedFinaleCellId
			&& CellStates[CellId].bBuildable
			&& !CellStates[CellId].bWater
			&& !CellStates[CellId].bBuildingAnchor;
	}
}

bool FABTSM51OrdinarySlingshotSlotPreviewAdapter::
	BuildFromExplicitCandidate(
		const FABTSM3MonthlySlingshotFieldResult& Result,
		const int32 ExplicitSourceRouteCandidateId,
		FABTSM51OrdinarySlingshotSlotSnapshot& OutSnapshot,
		FString& OutFailure)
{
	OutSnapshot = FABTSM51OrdinarySlingshotSlotSnapshot();
	OutFailure.Reset();
	if (ExplicitSourceRouteCandidateId == INDEX_NONE)
	{
		OutFailure = TEXT("ExplicitCandidateRequired");
		return false;
	}
	if (!Result.bSlingshotFieldResultValid
		|| Result.bMonthlyWorldAccepted
		|| Result.RejectReason
			!= EABTSM3MonthlySlingshotFieldRejectReason::None
		|| Result.ResultHash == 0
		|| Result.MaxCordLengthCM
			< FABTSM51OrdinarySlingshotSlotSnapshot::MinimumCordLengthCM
		|| Result.MaxCordLengthCM
			> FABTSM51OrdinarySlingshotSlotSnapshot::MaximumCordLengthCM)
	{
		OutFailure = TEXT("PreviewSourceResultInvalid");
		return false;
	}

	const FABTSM3MonthlySlingshotFieldCandidate* Candidate =
		Result.RetainedCandidates.FindByPredicate(
			[ExplicitSourceRouteCandidateId](
				const FABTSM3MonthlySlingshotFieldCandidate& Entry)
			{
				return Entry.SourceRouteCandidateId
					== ExplicitSourceRouteCandidateId;
			});
	if (Candidate == nullptr || Candidate->CandidateHash == 0)
	{
		OutFailure = TEXT("ExplicitCandidateNotFound");
		return false;
	}
	if (Candidate->Fields.Num() != Result.FieldsPerCandidate
		|| Candidate->TotalSlotCount != Result.SlotsPerCandidate)
	{
		OutFailure = TEXT("CandidateCardinalityMismatch");
		return false;
	}

	OutSnapshot.LayoutHash = static_cast<uint64>(Result.ResultHash);
	OutSnapshot.CandidateHash =
		static_cast<uint64>(Candidate->CandidateHash);
	OutSnapshot.MaxCordLengthCM = Result.MaxCordLengthCM;
	OutSnapshot.SlotGroups.Reserve(Candidate->Fields.Num());
	int32 AdaptedSlotCount = 0;
	for (const FABTSM3MonthlySlingshotField& Field : Candidate->Fields)
	{
		FABTSM51OrdinarySlingshotSlotGroup& Group =
			OutSnapshot.SlotGroups.AddDefaulted_GetRef();
		Group.SlotCellIds = Field.SlotCellIds;
		AdaptedSlotCount += Group.SlotCellIds.Num();
	}
	if (AdaptedSlotCount != Candidate->TotalSlotCount
		|| !OutSnapshot.IsStructurallyUsable())
	{
		OutSnapshot = FABTSM51OrdinarySlingshotSlotSnapshot();
		OutFailure = TEXT("AdaptedSnapshotInvalid");
		return false;
	}
	return true;
}

bool FABTSM51OrdinarySlingshotSlotReleaseAdapter::
	AdaptToProductionSurface(
		const TArray<FABTSM2Cell>& Cells,
		const TArray<FABTSM3CellState>& CellStates,
		const int32 ExcludedFinaleCellId,
		FABTSM51OrdinarySlingshotSlotSnapshot& InOutSnapshot,
		FString& OutFailure,
		int32* OutRemovedInvalidSlots,
		int32* OutAddedSlots)
{
	OutFailure.Reset();
	if (OutRemovedInvalidSlots != nullptr)
	{
		*OutRemovedInvalidSlots = 0;
	}
	if (OutAddedSlots != nullptr)
	{
		*OutAddedSlots = 0;
	}
	if (Cells.IsEmpty()
		|| Cells.Num() != CellStates.Num()
		|| !InOutSnapshot.IsStructurallyUsable())
	{
		OutFailure = TEXT("InvalidSourceSnapshotOrSurface");
		return false;
	}

	FABTSM51OrdinarySlingshotSlotSnapshot Candidate =
		InOutSnapshot;
	TSet<int32> UsedCells;
	int32 RemovedInvalidSlots = 0;
	int32 AddedSlots = 0;
	for (int32 GroupIndex = 0;
		GroupIndex < Candidate.SlotGroups.Num();
		++GroupIndex)
	{
		FABTSM51OrdinarySlingshotSlotGroup& Group =
			Candidate.SlotGroups[GroupIndex];
		const TArray<int32> OriginalRoots = Group.SlotCellIds;
		Group.SlotCellIds.Reset();

		for (const int32 CellId : OriginalRoots)
		{
			if (!IsProductionSlotCellUsable(
					Cells,
					CellStates,
					CellId,
					ExcludedFinaleCellId)
				|| UsedCells.Contains(CellId))
			{
				++RemovedInvalidSlots;
				continue;
			}
			UsedCells.Add(CellId);
			Group.SlotCellIds.Add(CellId);
		}

		TArray<int32> SearchQueue;
		TSet<int32> VisitedCells;
		for (const int32 RootCellId : OriginalRoots)
		{
			if (Cells.IsValidIndex(RootCellId)
				&& !VisitedCells.Contains(RootCellId))
			{
				VisitedCells.Add(RootCellId);
				SearchQueue.Add(RootCellId);
			}
		}
		SearchQueue.Sort();

		int32 QueueIndex = 0;
		while (Group.SlotCellIds.Num() < RequiredSlotsPerGroup
			&& QueueIndex < SearchQueue.Num())
		{
			const int32 SourceCellId = SearchQueue[QueueIndex++];
			TArray<int32> OrderedNeighbors =
				Cells[SourceCellId].NeighborCellIds;
			OrderedNeighbors.Sort();
			for (const int32 NeighborCellId : OrderedNeighbors)
			{
				if (!Cells.IsValidIndex(NeighborCellId)
					|| VisitedCells.Contains(NeighborCellId))
				{
					continue;
				}

				// Traverse every topology Cell, including water and building
				// envelopes. A whole authored group can be invalid on the final
				// surface, and valid release terrain may lie beyond that envelope.
				VisitedCells.Add(NeighborCellId);
				SearchQueue.Add(NeighborCellId);
				if (Group.SlotCellIds.Num() >= RequiredSlotsPerGroup
					|| UsedCells.Contains(NeighborCellId)
					|| !IsProductionSlotCellUsable(
						Cells,
						CellStates,
						NeighborCellId,
						ExcludedFinaleCellId))
				{
					continue;
				}
				UsedCells.Add(NeighborCellId);
				Group.SlotCellIds.Add(NeighborCellId);
				++AddedSlots;
			}
		}

		if (Group.SlotCellIds.Num() < RequiredSlotsPerGroup)
		{
			OutFailure = FString::Printf(
				TEXT("GroupCapacity:%d:%d/%d"),
				GroupIndex,
				Group.SlotCellIds.Num(),
				RequiredSlotsPerGroup);
			return false;
		}
	}

	if (!Candidate.IsStructurallyUsable())
	{
		OutFailure = TEXT("AdaptedSnapshotInvalid");
		return false;
	}
	InOutSnapshot = MoveTemp(Candidate);
	if (OutRemovedInvalidSlots != nullptr)
	{
		*OutRemovedInvalidSlots = RemovedInvalidSlots;
	}
	if (OutAddedSlots != nullptr)
	{
		*OutAddedSlots = AddedSlots;
	}
	return true;
}
