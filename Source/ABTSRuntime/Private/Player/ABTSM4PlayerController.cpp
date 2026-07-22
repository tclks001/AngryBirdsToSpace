// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/ABTSM4PlayerController.h"

#include "ABTSRuntime.h"
#include "Camera/ABTSM4PartyCamera.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"
#include "Party/ABTSBirdParty.h"

AABTSM4PlayerController::AABTSM4PlayerController()
{
	bAutoManageActiveCameraTarget = false;
}

void AABTSM4PlayerController::BeginPlay()
{
	Super::BeginPlay();
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	SetCursorInteractionMode(true);
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PartyCamera = GetWorld()->SpawnActor<AABTSM4PartyCamera>(AABTSM4PartyCamera::StaticClass(), FTransform::Identity, SpawnParameters);
	EnsurePartyCameraView();
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M4][Controller] Orbit camera, HUD mouse input and party switching ready."));
}

void AABTSM4PlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	EnsurePartyCameraView();
}

void AABTSM4PlayerController::EnsurePartyCameraView()
{
	if (PartyCamera != nullptr && GetViewTarget() != PartyCamera) SetViewTarget(PartyCamera);
}

void AABTSM4PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindAction(TEXT("ABTS_CycleBird"), IE_Pressed, this, &AABTSM4PlayerController::CycleBird);
	InputComponent->BindAction(TEXT("ABTS_CameraOrbitHold"), IE_Pressed, this, &AABTSM4PlayerController::BeginOrbitInput);
	InputComponent->BindAction(TEXT("ABTS_CameraOrbitHold"), IE_Released, this, &AABTSM4PlayerController::EndOrbitInput);
	InputComponent->BindAction(TEXT("ABTS_CameraRecenter"), IE_Pressed, this, &AABTSM4PlayerController::RecenterCamera);
	InputComponent->BindAxis(TEXT("ABTS_Turn"), this, &AABTSM4PlayerController::ApplyOrbitYaw);
	InputComponent->BindAxis(TEXT("ABTS_LookUp"), this, &AABTSM4PlayerController::ApplyOrbitPitch);
	InputComponent->BindAxis(TEXT("ABTS_CameraZoom"), this, &AABTSM4PlayerController::ApplyCameraZoom);
}

void AABTSM4PlayerController::CycleBird()
{
	for (TActorIterator<AABTSBirdParty> It(GetWorld()); It; ++It)
	{
		if (It->CycleControlledBird()) return;
	}
}

void AABTSM4PlayerController::BeginOrbitInput()
{
	bOrbitInputHeld = true;
	bSavedCursorPositionValid = GetMousePosition(SavedCursorX, SavedCursorY);
	SetCursorInteractionMode(false);
}

void AABTSM4PlayerController::EndOrbitInput()
{
	bOrbitInputHeld = false;
	SetCursorInteractionMode(true);
	if (bSavedCursorPositionValid)
	{
		SetMouseLocation(FMath::RoundToInt(SavedCursorX), FMath::RoundToInt(SavedCursorY));
	}
}

void AABTSM4PlayerController::SetCursorInteractionMode(const bool bEnableCursor)
{
	bShowMouseCursor = bEnableCursor;
	if (bEnableCursor)
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::LockOnCapture);
		SetInputMode(InputMode);
	}
	else
	{
		FInputModeGameOnly InputMode;
		SetInputMode(InputMode);
	}
}

void AABTSM4PlayerController::ApplyOrbitYaw(const float Value)
{
	const bool bGamepadInput = FMath::Abs(GetInputAnalogKeyState(EKeys::Gamepad_RightX)) > 0.05f;
	if (PartyCamera && (bOrbitInputHeld || bGamepadInput)) PartyCamera->AddOrbitYawInput(Value);
}

void AABTSM4PlayerController::ApplyOrbitPitch(const float Value)
{
	const bool bGamepadInput = FMath::Abs(GetInputAnalogKeyState(EKeys::Gamepad_RightY)) > 0.05f;
	if (PartyCamera && (bOrbitInputHeld || bGamepadInput)) PartyCamera->AddOrbitPitchInput(Value);
}

void AABTSM4PlayerController::ApplyCameraZoom(const float Value)
{
	if (PartyCamera) PartyCamera->AddZoomInput(Value);
}

void AABTSM4PlayerController::RecenterCamera()
{
	if (PartyCamera) PartyCamera->RequestRecenter();
}

bool AABTSM4PlayerController::GetCameraRelativeMovementBasis(
	const FVector& WorldLocation,
	FVector& OutForward,
	FVector& OutRight) const
{
	return PartyCamera != nullptr && PartyCamera->GetMovementBasisAt(WorldLocation, OutForward, OutRight);
}
