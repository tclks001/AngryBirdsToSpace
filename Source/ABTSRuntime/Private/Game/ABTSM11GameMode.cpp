// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM11GameMode.h"

#include "ABTSRuntime.h"
#include "Camera/ABTSM6SlingshotCamera.h"
#include "Contracts/ABTSWorldGenerationContracts.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Party/ABTSBirdParty.h"
#include "Player/ABTSM11PlayerController.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "Terrain/ABTSM3Planet.h"
#include "UI/ABTSM11FinaleHUD.h"
#include "World/ABTSM11FinaleInteractionSystem.h"
#include "World/ABTSM11FinaleSystem.h"
#include "World/ABTSM51WorldSystem.h"

TSubclassOf<AABTSM6SlingshotCamera>
AABTSM11GameMode::ResolveRuntimeSlingshotCameraClass(
	UWorld& World,
	const AABTSM6SlingshotSystem& SlingshotSystem,
	int32* OutMatchingCameraCount)
{
	int32 MatchingCameraCount = 0;
	TSubclassOf<AABTSM6SlingshotCamera> ResolvedClass;
	for (TActorIterator<AABTSM6SlingshotCamera> It(&World); It; ++It)
	{
		AABTSM6SlingshotCamera* Camera = *It;
		if (!IsValid(Camera)
			|| Camera->GetOwner() != &SlingshotSystem)
		{
			continue;
		}
		++MatchingCameraCount;
		ResolvedClass = Camera->GetClass();
	}
	if (OutMatchingCameraCount != nullptr)
	{
		*OutMatchingCameraCount = MatchingCameraCount;
	}
	return MatchingCameraCount == 1
		? ResolvedClass
		: TSubclassOf<AABTSM6SlingshotCamera>();
}

AABTSM11GameMode::AABTSM11GameMode()
{
	PlayerControllerClass = AABTSM11PlayerController::StaticClass();
	HUDClass = AABTSM11FinaleHUD::StaticClass();
	FinaleInteractionSystemClass =
		AABTSM11FinaleInteractionSystem::StaticClass();
}

void AABTSM11GameMode::InitGame(
	const FString& MapName,
	const FString& Options,
	FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	if (!bEnableMonthlyFinalePreviewIntegration
		|| GetWorld() == nullptr)
	{
		return;
	}

	int32 ConfiguredPlanetCount = 0;
	for (TActorIterator<AABTSM3Planet> It(GetWorld()); It; ++It)
	{
		It->bEnableMonthlyPresentationPreview = true;
		It->MonthlyPresentationPreviewCandidateId =
			MonthlyFinalePreviewCandidateId;
		++ConfiguredPlanetCount;
	}
	if (ConfiguredPlanetCount == 1
		&& MonthlyFinalePreviewCandidateId >= 0)
	{
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][Integration][PreviewFinaleFrame][PreBeginPlay] Enabled=1 Candidate=%d Planets=%d Map=%s MonthlyAccepted=0"),
			MonthlyFinalePreviewCandidateId,
			ConfiguredPlanetCount,
			*MapName);
	}
	else
	{
		UE_LOG(
			LogABTSRuntime,
			Error,
			TEXT("[ABTS][Integration][PreviewFinaleFrame][PreBeginPlay] Rejected Candidate=%d Planets=%d Map=%s MonthlyAccepted=0"),
			MonthlyFinalePreviewCandidateId,
			ConfiguredPlanetCount,
			*MapName);
	}
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
	AABTSM6SlingshotSystem* SourceSlingshotSystem =
		GetRuntimeSlingshotSystem();
	int32 RuntimeCameraCount = 0;
	const TSubclassOf<AABTSM6SlingshotCamera> FinaleAimCameraClass =
		IsValid(SourceSlingshotSystem)
		? ResolveRuntimeSlingshotCameraClass(
			*GetWorld(),
			*SourceSlingshotSystem,
			&RuntimeCameraCount)
		: TSubclassOf<AABTSM6SlingshotCamera>();
	if (!FinaleAimCameraClass)
	{
		UE_LOG(
			LogABTSRuntime,
			Error,
			TEXT("[ABTS][M11-C][CameraClass] ResolveFailed RuntimeSystem=%s MatchingCameras=%d"),
			*GetNameSafe(SourceSlingshotSystem),
			RuntimeCameraCount);
		return;
	}
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11-C][CameraClass] SourceSystem=%s MatchingCameras=%d Selected=%s"),
		*GetNameSafe(SourceSlingshotSystem),
		RuntimeCameraCount,
		*GetNameSafe(FinaleAimCameraClass.Get()));

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
	const FABTSM51PreviewFinaleFrameContext* PreviewFinaleContext = nullptr;
	int32 WorldSystemCount = 0;
	for (TActorIterator<AABTSM51WorldSystem> It(GetWorld()); It; ++It)
	{
		++WorldSystemCount;
		if (PreviewFinaleContext == nullptr)
		{
			PreviewFinaleContext =
				It->GetPreviewFinaleFrameContext();
		}
	}
	if (bEnableMonthlyFinalePreviewIntegration)
	{
		if (WorldSystemCount != 1
			|| PreviewFinaleContext == nullptr
			|| !PreviewFinaleContext->IsUsable())
		{
			UE_LOG(
				LogABTSRuntime,
				Error,
				TEXT("[ABTS][Integration][PreviewFinaleFrame] M11 initialization rejected. WorldSystems=%d Context=%d Candidate=%d"),
				WorldSystemCount,
				PreviewFinaleContext != nullptr ? 1 : 0,
				MonthlyFinalePreviewCandidateId);
			return;
		}
		WorldContract.LaunchFrame = PreviewFinaleContext->Frame;
	}
#if WITH_EDITOR
	const int32 CandidateRank =
		FABTSM11CandidateExperienceCatalog::GetRequestedCandidateRank();
	const bool bCandidateRequested = CandidateRank != 0;
	const bool bIsPIEWorld =
		GetWorld() != nullptr
		&& GetWorld()->WorldType == EWorldType::PIE;
	const bool bUseEditorCandidate =
		bCandidateRequested && bIsPIEWorld;
	if (bCandidateRequested && !bIsPIEWorld)
	{
		UE_LOG(
			LogABTSRuntime,
			Warning,
			TEXT("[ABTS][M11-C-v2.1][GameMode] Candidate Rank=%d ignored outside PIE; production Certified v1 remains active."),
			CandidateRank);
	}
	const bool bReady = bUseEditorCandidate
		? FinaleSystem->InitializeFromEditorCandidateRank(
			CandidateRank,
			WorldContract)
		: FinaleSystem->InitializeFromWorldContract(WorldContract);
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11-C-v2.1][GameMode] LayoutSelection=%s Rank=%d"),
		bUseEditorCandidate
			? TEXT("EditorCandidate-UNCERTIFIED")
			: TEXT("ProductionCertifiedV1"),
		CandidateRank);
	if (PreviewFinaleContext != nullptr)
	{
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][Integration][PreviewFinaleFrame] Authority=PreviewTest Candidate=%d Spatial=%016llX Plan=%016llX Preview=%016llX Context=%016llX M11Frame=%016llX CandidateRank=%d MonthlyAccepted=0"),
			PreviewFinaleContext->SourceRouteCandidateId,
			static_cast<unsigned long long>(
				static_cast<uint64>(PreviewFinaleContext->SourceSpatialCandidateHash)),
			static_cast<unsigned long long>(
				static_cast<uint64>(PreviewFinaleContext->SourcePlanResultHash)),
			static_cast<unsigned long long>(
				static_cast<uint64>(PreviewFinaleContext->SourcePreviewHash)),
			static_cast<unsigned long long>(
				static_cast<uint64>(PreviewFinaleContext->ContextHash)),
			static_cast<unsigned long long>(
				AABTSM11FinaleSystem::ComputeFinaleFrameDiagnosticHash(
					WorldContract.LaunchFrame)),
			CandidateRank);
	}
#else
	const bool bReady =
		FinaleSystem->InitializeFromWorldContract(WorldContract);
#endif
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
				*ReadyParty,
				FinaleAimCameraClass))
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
