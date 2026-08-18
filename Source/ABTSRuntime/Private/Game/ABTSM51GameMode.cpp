// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM51GameMode.h"

#include "ABTSRuntime.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Player/ABTSM51PlayerController.h"
#include "Terrain/ABTSM3Planet.h"
#include "World/ABTSM51OrdinarySlingshotSlotPreview.h"
#include "World/ABTSM51WorldSystem.h"

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
		const bool bProductionCandidate =
			bSnapshotBuilt
			&& !bPreviewRequested
			&& ActiveFinalePreview != nullptr;
		bool bReleaseSnapshotReady = bSnapshotBuilt;
		if (bProductionCandidate)
		{
			int32 RemovedInvalidSlots = 0;
			int32 AddedSlots = 0;
			bReleaseSnapshotReady =
				FABTSM51OrdinarySlingshotSlotReleaseAdapter::
					AdaptToProductionSurface(
				Planet->LogicalCells,
				Planet->GetGeneratedCellStates(),
				ActiveFinalePreview->AnchorCellId,
				Snapshot,
				PreviewFailure,
				&RemovedInvalidSlots,
				&AddedSlots);
			if (bReleaseSnapshotReady)
			{
				UE_LOG(LogABTSRuntime, Log,
					TEXT("[ABTS][M5.1][OrdinarySlots][ReleaseCapacity] Accepted=1 Groups=%d TargetPerGroup=%d RemovedInvalid=%d Added=%d Failure=None"),
					Snapshot.SlotGroups.Num(),
					FABTSM51OrdinarySlingshotSlotReleaseAdapter::
						RequiredSlotsPerGroup,
					RemovedInvalidSlots,
					AddedSlots);
			}
			else
			{
				UE_LOG(LogABTSRuntime, Error,
					TEXT("[ABTS][M5.1][OrdinarySlots][ReleaseCapacity] Accepted=0 Groups=%d TargetPerGroup=%d RemovedInvalid=%d Added=%d Failure=%s"),
					Snapshot.SlotGroups.Num(),
					FABTSM51OrdinarySlingshotSlotReleaseAdapter::
						RequiredSlotsPerGroup,
					RemovedInvalidSlots,
					AddedSlots,
					*PreviewFailure);
			}
		}
		bPreviewConfigured = bReleaseSnapshotReady
			&& (bProductionCandidate
				? System->ConfigureAcceptedOrdinarySlingshotSlotSnapshot(
					Snapshot)
				: System->ConfigurePreviewOrdinarySlingshotSlotSnapshot(
					Snapshot));
		if (!bPreviewConfigured)
		{
			if (Planet == nullptr)
			{
				PreviewFailure = TEXT("PlanetNotReady");
			}
			// A requested Preview/Test mode must fail closed instead of silently
			// returning to the compatibility TaskGraph slot pairs.
			if (bProductionCandidate)
			{
				System->ConfigureAcceptedOrdinarySlingshotSlotSnapshot(
					FABTSM51OrdinarySlingshotSlotSnapshot());
			}
			else
			{
				System->ConfigurePreviewOrdinarySlingshotSlotSnapshot(
					FABTSM51OrdinarySlingshotSlotSnapshot());
			}
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M5.1][OrdinarySlots][%s] Rejected Candidate=%d Reason=%s MonthlyAccepted=0"),
				bProductionCandidate
					? TEXT("FrozenProduction")
					: TEXT("PreviewTest"),
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
		TEXT("[ABTS][M5.1] Entry ready=%d StartCell=%d CandidateFrame=%d ExplicitPreviewTest=%d Candidate=%d OrdinaryConfigured=%d OrdinaryAuthority=%d FinaleConfigured=%d MonthlyAccepted=0"),
		System ? 1 : 0,
		SpawnCellId,
		bResolvedPreviewRequested ? 1 : 0,
		bPreviewRequested ? 1 : 0,
		bResolvedPreviewRequested ? ExplicitPreviewCandidateId : INDEX_NONE,
		bPreviewConfigured ? 1 : 0,
		System != nullptr
			? static_cast<int32>(
				System->GetOrdinarySlotSnapshotAuthority())
			: 0,
		bFinalePreviewConfigured ? 1 : 0);
}

