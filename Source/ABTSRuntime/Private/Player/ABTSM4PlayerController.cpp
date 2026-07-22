// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/ABTSM4PlayerController.h"

#include "ABTSRuntime.h"
#include "Camera/ABTSM4PartyCamera.h"
#include "EngineUtils.h"
#include "Party/ABTSBirdParty.h"

AABTSM4PlayerController::AABTSM4PlayerController()
{
	bAutoManageActiveCameraTarget = false;
}

void AABTSM4PlayerController::BeginPlay()
{
	Super::BeginPlay();
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::LockOnCapture);
	SetInputMode(InputMode);
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PartyCamera = GetWorld()->SpawnActor<AABTSM4PartyCamera>(AABTSM4PartyCamera::StaticClass(), FTransform::Identity, SpawnParameters);
	EnsurePartyCameraView();
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M4][Controller] HUD mouse input and party switching ready."));
}

void AABTSM4PlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	EnsurePartyCameraView();
}

void AABTSM4PlayerController::EnsurePartyCameraView()
{
	if (PartyCamera != nullptr && GetViewTarget() != PartyCamera)
	{
		SetViewTarget(PartyCamera);
	}
}

void AABTSM4PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindAction(TEXT("ABTS_CycleBird"), IE_Pressed, this, &AABTSM4PlayerController::CycleBird);
}

void AABTSM4PlayerController::CycleBird()
{
	for (TActorIterator<AABTSBirdParty> It(GetWorld()); It; ++It)
	{
		if (It->CycleControlledBird()) return;
	}
}
