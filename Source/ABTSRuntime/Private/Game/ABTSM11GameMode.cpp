// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM11GameMode.h"

#include "ABTSRuntime.h"
#include "Contracts/ABTSWorldGenerationContracts.h"
#include "EngineUtils.h"
#include "Party/ABTSBirdParty.h"
#include "Player/ABTSM11PlayerController.h"
#include "Terrain/ABTSM3Planet.h"
#include "UI/ABTSM11FinaleHUD.h"
#include "World/ABTSM11FinaleInteractionSystem.h"
#include "World/ABTSM11FinaleSystem.h"

AABTSM11GameMode::AABTSM11GameMode()
{
	PlayerControllerClass = AABTSM11PlayerController::StaticClass();
	HUDClass = AABTSM11FinaleHUD::StaticClass();
	FinaleInteractionSystemClass =
		AABTSM11FinaleInteractionSystem::StaticClass();
}

void AABTSM11GameMode::OnInitialPlayerPlaced(
	ACharacter& Character,
	const FTransform& SpawnTransform,
	const int32 SpawnCellId)
{
	Super::OnInitialPlayerPlaced(
		Character,
		SpawnTransform,
		SpawnCellId);
	if (GetWorld() == nullptr)
	{
		UE_LOG(
			LogABTSRuntime,
			Error,
			TEXT("[ABTS][M11-B][GameMode] World is missing."));
		return;
	}
	if (IsValid(FinaleSystem))
	{
		UE_LOG(
			LogABTSRuntime,
			Warning,
			TEXT("[ABTS][M11-B][GameMode] Duplicate player-placement hook ignored."));
		return;
	}

	AABTSM3Planet* PrimaryPlanet = nullptr;
	int32 ReadyPlanetCount = 0;
	for (TActorIterator<AABTSM3Planet> It(GetWorld()); It; ++It)
	{
		if (!It->IsPlanetReady())
		{
			continue;
		}
		++ReadyPlanetCount;
		if (PrimaryPlanet == nullptr)
		{
			PrimaryPlanet = *It;
		}
	}
	if (ReadyPlanetCount != 1 || PrimaryPlanet == nullptr)
	{
		UE_LOG(
			LogABTSRuntime,
			Error,
			TEXT("[ABTS][M11-B][GameMode] Expected exactly one ready M3 primary planet; found %d."),
			ReadyPlanetCount);
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FinaleSystem = GetWorld()->SpawnActor<AABTSM11FinaleSystem>(
		AABTSM11FinaleSystem::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	if (!IsValid(FinaleSystem))
	{
		UE_LOG(
			LogABTSRuntime,
			Error,
			TEXT("[ABTS][M11-B][GameMode] Failed to spawn finale system."));
		return;
	}

	FABTSFinaleWorldContract WorldContract;
	PrimaryPlanet->TryExportFinaleWorldContract(WorldContract);
	const bool bReady =
		FinaleSystem->InitializeFromWorldContract(WorldContract);
	if (bReady)
	{
		AABTSBirdParty* ReadyParty = nullptr;
		int32 ReadyPartyCount = 0;
		for (TActorIterator<AABTSBirdParty> It(GetWorld()); It; ++It)
		{
			if (!It->IsPartyReady())
			{
				continue;
			}
			++ReadyPartyCount;
			if (ReadyParty == nullptr)
			{
				ReadyParty = *It;
			}
		}
		if (ReadyPartyCount != 1
			|| ReadyParty == nullptr
			|| !FinaleInteractionSystemClass)
		{
			UE_LOG(
				LogABTSRuntime,
				Error,
				TEXT("[ABTS][M11-C][GameMode] Expected one ready Party and an interaction class; Parties=%d Class=%d"),
				ReadyPartyCount,
				FinaleInteractionSystemClass ? 1 : 0);
			return;
		}
		FinaleInteractionSystem =
			GetWorld()->SpawnActor<AABTSM11FinaleInteractionSystem>(
				FinaleInteractionSystemClass,
				FTransform::Identity,
				SpawnParameters);
		if (!IsValid(FinaleInteractionSystem)
			|| !FinaleInteractionSystem->Initialize(
				*FinaleSystem,
				*ReadyParty))
		{
			UE_LOG(
				LogABTSRuntime,
				Error,
				TEXT("[ABTS][M11-C][GameMode] Interaction initialization failed."));
			return;
		}
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M11-C][GameMode] Entry Ready=1 StartCell=%d"),
			SpawnCellId);
	}
	else
	{
		UE_LOG(
			LogABTSRuntime,
			Error,
			TEXT("[ABTS][M11-B][GameMode] Entry Ready=0 StartCell=%d Failure=%s"),
			SpawnCellId,
			*FinaleSystem->GetFailureReason());
	}
}
