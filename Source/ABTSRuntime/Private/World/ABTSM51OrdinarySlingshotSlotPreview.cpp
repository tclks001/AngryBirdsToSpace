// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM51OrdinarySlingshotSlotPreview.h"

#include "PCG/ABTSM3MonthlySlingshotField.h"

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
