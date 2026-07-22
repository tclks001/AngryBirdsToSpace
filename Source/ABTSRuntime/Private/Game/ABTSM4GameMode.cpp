// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM4GameMode.h"

#include "ABTSRuntime.h"
#include "Party/ABTSBirdParty.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Player/ABTSM4PlayerController.h"
#include "UI/ABTSM4PartyHUD.h"

AABTSM4GameMode::AABTSM4GameMode()
{
	PlayerControllerClass = AABTSM4PlayerController::StaticClass();
	HUDClass = AABTSM4PartyHUD::StaticClass();
	BirdPartyClass = AABTSBirdParty::StaticClass();
}

void AABTSM4GameMode::OnInitialPlayerPlaced(
	ACharacter& Character,
	const FTransform& SpawnTransform,
	const int32 SpawnCellId)
{
	AABTSM25BirdCharacter* InitialBird = Cast<AABTSM25BirdCharacter>(&Character);
	if (InitialBird == nullptr || GetWorld() == nullptr) return;
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSBirdParty* Party = GetWorld()->SpawnActor<AABTSBirdParty>(BirdPartyClass, FTransform::Identity, SpawnParameters);
	const bool bReady = Party != nullptr && Party->InitializeParty(InitialBird);
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M4] Party entry ready=%d StartCell=%d."), bReady ? 1 : 0, SpawnCellId);
}
