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
	const bool bCommandLinePreview = FParse::Value(
		FCommandLine::Get(),
		TEXT("ABTSM3R31SlotPreviewCandidate="),
		ExplicitPreviewCandidateId);
	const bool bPreviewRequested =
		bEnableOrdinarySlingshotSlotPreview || bCommandLinePreview;
	AABTSM51WorldSystem* System =
		GetWorld()->SpawnActorDeferred<AABTSM51WorldSystem>(
			WorldSystemClass,
			FTransform::Identity,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	bool bPreviewConfigured = !bPreviewRequested;
	FString PreviewFailure;
	if (System != nullptr && bPreviewRequested)
	{
		AABTSM3Planet* Planet = nullptr;
		for (TActorIterator<AABTSM3Planet> It(GetWorld()); It; ++It)
		{
			if (It->IsPlanetReady())
			{
				Planet = *It;
				break;
			}
		}
		FABTSM51OrdinarySlingshotSlotSnapshot Snapshot;
		bPreviewConfigured = Planet != nullptr
			&& FABTSM51OrdinarySlingshotSlotPreviewAdapter::
				BuildFromExplicitCandidate(
					Planet->GetMonthlySlingshotFieldResult(),
					ExplicitPreviewCandidateId,
					Snapshot,
					PreviewFailure)
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
	}
	if (System != nullptr)
	{
		UGameplayStatics::FinishSpawningActor(
			System,
			FTransform::Identity);
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M5.1] Entry ready=%d StartCell=%d PreviewTest=%d Candidate=%d Configured=%d MonthlyAccepted=0"),
		System ? 1 : 0,
		SpawnCellId,
		bPreviewRequested ? 1 : 0,
		bPreviewRequested ? ExplicitPreviewCandidateId : INDEX_NONE,
		bPreviewConfigured ? 1 : 0);
}

