// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM51GameMode.h"

#include "ABTSRuntime.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Player/ABTSM51PlayerController.h"
#include "Terrain/ABTSM3Planet.h"
#include "World/ABTSM51OrdinarySlingshotSlotPreview.h"
#include "World/ABTSM51WorldSystem.h"

namespace
{
constexpr int32 ReleaseSlotsPerOrdinaryGroup = 12;

bool IsReleaseOrdinarySlotCellUsable(
	const AABTSM3Planet& Planet,
	const int32 CellId,
	const int32 ExcludedFinaleCellId)
{
	const TArray<FABTSM3CellState>& CellStates =
		Planet.GetGeneratedCellStates();
	return Planet.LogicalCells.IsValidIndex(CellId)
		&& CellStates.IsValidIndex(CellId)
		&& CellId != ExcludedFinaleCellId
		&& CellStates[CellId].bBuildable
		&& !CellStates[CellId].bWater
		&& !CellStates[CellId].bBuildingAnchor;
}

int32 ExpandReleaseOrdinarySlotGroups(
	const AABTSM3Planet& Planet,
	const int32 ExcludedFinaleCellId,
	FABTSM51OrdinarySlingshotSlotSnapshot& InOutSnapshot)
{
	TSet<int32> UsedCells;
	int32 RemovedInvalidSlots = 0;
	for (FABTSM51OrdinarySlingshotSlotGroup& Group :
		InOutSnapshot.SlotGroups)
	{
		for (int32 Index = Group.SlotCellIds.Num() - 1;
			Index >= 0;
			--Index)
		{
			const int32 CellId = Group.SlotCellIds[Index];
			if (!IsReleaseOrdinarySlotCellUsable(
					Planet,
					CellId,
					ExcludedFinaleCellId)
				|| UsedCells.Contains(CellId))
			{
				Group.SlotCellIds.RemoveAt(Index);
				++RemovedInvalidSlots;
				continue;
			}
			UsedCells.Add(CellId);
		}
	}

	int32 AddedSlots = 0;
	for (FABTSM51OrdinarySlingshotSlotGroup& Group :
		InOutSnapshot.SlotGroups)
	{
		TArray<int32> SearchQueue = Group.SlotCellIds;
		SearchQueue.Sort();
		int32 QueueIndex = 0;
		while (Group.SlotCellIds.Num() < ReleaseSlotsPerOrdinaryGroup
			&& QueueIndex < SearchQueue.Num())
		{
			const int32 SourceCellId = SearchQueue[QueueIndex++];
			if (!Planet.LogicalCells.IsValidIndex(SourceCellId))
			{
				continue;
			}
			TArray<int32> OrderedNeighbors =
				Planet.LogicalCells[SourceCellId].NeighborCellIds;
			OrderedNeighbors.Sort();
			for (const int32 NeighborCellId : OrderedNeighbors)
			{
				if (Group.SlotCellIds.Num()
					>= ReleaseSlotsPerOrdinaryGroup)
				{
					break;
				}
				if (UsedCells.Contains(NeighborCellId)
					|| !IsReleaseOrdinarySlotCellUsable(
						Planet,
						NeighborCellId,
						ExcludedFinaleCellId))
				{
					continue;
				}
				UsedCells.Add(NeighborCellId);
				Group.SlotCellIds.Add(NeighborCellId);
				SearchQueue.Add(NeighborCellId);
				++AddedSlots;
			}
		}
	}

	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M5.1][OrdinarySlots][ReleaseCapacity] Groups=%d TargetPerGroup=%d RemovedInvalid=%d Added=%d"),
		InOutSnapshot.SlotGroups.Num(),
		ReleaseSlotsPerOrdinaryGroup,
		RemovedInvalidSlots,
		AddedSlots);
	return AddedSlots;
}
}

AABTSM51GameMode::AABTSM51GameMode()
{
	PlayerControllerClass = AABTSM51PlayerController::StaticClass();
	WorldSystemClass = AABTSM51WorldSystem::StaticClass();
}

void AABTSM51GameMode::OnInitialPlayerPlaced(
	ACharacter& Character,
	const FTransform& SpawnTransform,
	const int32 SpawnCellId)
{
	Super::OnInitialPlayerPlaced(Character, SpawnTransform, SpawnCellId);
	if (GetWorld() == nullptr || !WorldSystemClass) return;

	int32 ExplicitPreviewCandidateId =
		OrdinarySlingshotSlotPreviewCandidateId;
#if UE_BUILD_SHIPPING
	// Authored production snapshots remain valid in Shipping. Shipping only
	// rejects command-line preview overrides; disabling this property restored
	// the compatibility slot pairs even when M3 had published the frozen V3
	// presentation.
	const bool bPreviewRequested =
		bEnableOrdinarySlingshotSlotPreview;
#else
	const bool bCommandLinePreview = FParse::Value(
		FCommandLine::Get(),
		TEXT("ABTSM3R31SlotPreviewCandidate="),
		ExplicitPreviewCandidateId);
	const bool bPreviewRequested =
		bEnableOrdinarySlingshotSlotPreview || bCommandLinePreview;
#endif
	AABTSM3Planet* Planet = nullptr;
	for (TActorIterator<AABTSM3Planet> It(GetWorld()); It; ++It)
	{
		if (It->IsPlanetReady())
		{
			Planet = *It;
			break;
		}
	}
	const FABTSM3MonthlyFinaleAnchorPreview* ActiveFinalePreview =
		Planet != nullptr
			&& Planet->GetActiveMonthlyFinaleAnchorPreview().bPreviewValid
		? &Planet->GetActiveMonthlyFinaleAnchorPreview()
		: nullptr;
	if (!bPreviewRequested && ActiveFinalePreview != nullptr)
	{
		ExplicitPreviewCandidateId =
			ActiveFinalePreview->SourceRouteCandidateId;
	}
	const bool bResolvedPreviewRequested =
		bPreviewRequested || ActiveFinalePreview != nullptr;
	AABTSM51WorldSystem* System =
		GetWorld()->SpawnActorDeferred<AABTSM51WorldSystem>(
			WorldSystemClass,
			FTransform::Identity,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	bool bPreviewConfigured = !bResolvedPreviewRequested;
	bool bFinalePreviewConfigured = !bResolvedPreviewRequested;
	FString PreviewFailure;
	if (System != nullptr && bResolvedPreviewRequested)
	{
		FABTSM51OrdinarySlingshotSlotSnapshot Snapshot;
		const bool bSnapshotBuilt = Planet != nullptr
			&& FABTSM51OrdinarySlingshotSlotPreviewAdapter::
				BuildFromExplicitCandidate(
					Planet->GetMonthlySlingshotFieldResult(),
					ExplicitPreviewCandidateId,
					Snapshot,
					PreviewFailure);
		if (bSnapshotBuilt
			&& !bPreviewRequested
			&& ActiveFinalePreview != nullptr)
		{
			ExpandReleaseOrdinarySlotGroups(
				*Planet,
				ActiveFinalePreview->AnchorCellId,
				Snapshot);
		}
		bPreviewConfigured = bSnapshotBuilt
			&& System->ConfigurePreviewOrdinarySlingshotSlotSnapshot(
				Snapshot);
		if (!bPreviewConfigured)
		{
			if (Planet == nullptr)
			{
				PreviewFailure = TEXT("PlanetNotReady");
			}
			// A requested Preview/Test mode must fail closed instead of silently
			// returning to the compatibility TaskGraph slot pairs.
			System->ConfigurePreviewOrdinarySlingshotSlotSnapshot(
				FABTSM51OrdinarySlingshotSlotSnapshot());
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M5.1][OrdinarySlots][PreviewTest] Rejected Candidate=%d Reason=%s MonthlyAccepted=0"),
				ExplicitPreviewCandidateId,
				*PreviewFailure);
		}

		FABTSM3MonthlyFinaleAnchorPreview BuiltFinalePreview;
		const FABTSM3MonthlyFinaleAnchorPreview* FinalePreview =
			ActiveFinalePreview != nullptr
				&& ActiveFinalePreview->SourceRouteCandidateId
					== ExplicitPreviewCandidateId
			? ActiveFinalePreview
			: nullptr;
		FString FinalePreviewFailure;
		if (ActiveFinalePreview != nullptr
			&& FinalePreview == nullptr)
		{
			FinalePreviewFailure = TEXT("ActiveCandidateMismatch");
		}
		if (FinalePreview == nullptr
			&& FinalePreviewFailure.IsEmpty()
			&& Planet != nullptr
			&& Planet->TryBuildMonthlyFinaleAnchorPreview(
				ExplicitPreviewCandidateId,
				BuiltFinalePreview,
				FinalePreviewFailure))
		{
			FinalePreview = &BuiltFinalePreview;
		}
		FABTSM51PreviewFinaleFrameContext FinaleContext;
		bFinalePreviewConfigured = Planet != nullptr
			&& FinalePreview != nullptr
			&& FABTSM51PreviewFinaleFrameAdapter::Build(
				*FinalePreview,
				Planet->GetFinaleLaunchFrame(),
				FinaleContext,
				FinalePreviewFailure)
			&& System->ConfigurePreviewFinaleFrame(FinaleContext);
		if (!bFinalePreviewConfigured)
		{
			if (Planet == nullptr)
			{
				FinalePreviewFailure = TEXT("PlanetNotReady");
			}
			System->ConfigurePreviewFinaleFrame(
				FABTSM51PreviewFinaleFrameContext());
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M5.1][PreviewFinaleFrame] Rejected Candidate=%d Reason=%s MonthlyAccepted=0"),
				ExplicitPreviewCandidateId,
				*FinalePreviewFailure);
		}
	}
	if (System != nullptr)
	{
		UGameplayStatics::FinishSpawningActor(
			System,
			FTransform::Identity);
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M5.1] Entry ready=%d StartCell=%d PreviewTest=%d Candidate=%d OrdinaryConfigured=%d FinaleConfigured=%d MonthlyAccepted=0"),
		System ? 1 : 0,
		SpawnCellId,
		bResolvedPreviewRequested ? 1 : 0,
		bResolvedPreviewRequested ? ExplicitPreviewCandidateId : INDEX_NONE,
		bPreviewConfigured ? 1 : 0,
		bFinalePreviewConfigured ? 1 : 0);
}

