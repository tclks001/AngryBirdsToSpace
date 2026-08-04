// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/ABTSM4PlayerController.h"

#include "ABTSRuntime.h"
#include "Camera/ABTSM4PartyCamera.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"
#include "Party/ABTSBirdParty.h"
#include "Party/ABTSBirdPartySettings.h"
#include "Player/ABTSM25BirdCharacter.h"

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
	// These experiment bindings are always registered so the editor toggle can
	// change at runtime. They must never consume input: when the experiment is
	// off, Pawn input remains the authoritative path; when it is on, the pawn
	// callbacks explicitly no-op and only this controller route acts.
	FInputAxisBinding& RoutedForwardBinding = InputComponent->BindAxis(TEXT("ABTS_MoveForward"), this, &AABTSM4PlayerController::RouteMoveForwardToControlledBird);
	RoutedForwardBinding.bConsumeInput = false;
	FInputAxisBinding& RoutedRightBinding = InputComponent->BindAxis(TEXT("ABTS_MoveRight"), this, &AABTSM4PlayerController::RouteMoveRightToControlledBird);
	RoutedRightBinding.bConsumeInput = false;
	FInputActionBinding& RoutedJumpBinding = InputComponent->BindAction(TEXT("ABTS_Jump"), IE_Pressed, this, &AABTSM4PlayerController::RouteJumpToControlledBird);
	RoutedJumpBinding.bConsumeInput = false;
	InputComponent->BindAxis(TEXT("ABTS_Turn"), this, &AABTSM4PlayerController::ApplyOrbitYaw);
	InputComponent->BindAxis(TEXT("ABTS_LookUp"), this, &AABTSM4PlayerController::ApplyOrbitPitch);
	InputComponent->BindAxis(TEXT("ABTS_CameraZoom"), this, &AABTSM4PlayerController::ApplyCameraZoom);
}

bool AABTSM4PlayerController::IsControllerRoutedMovementInputExperimentEnabled() const
{
	const AABTSBirdParty* Party = FindParty();
	const AABTSBirdPartySettings* Settings = Party ? Party->GetResolvedSettings() : nullptr;
	return Settings != nullptr && Settings->bUseControllerRoutedMovementInputExperiment;
}

bool AABTSM4PlayerController::IsClearMotionBeforePlayerJumpExperimentEnabled() const
{
	const AABTSBirdParty* Party = FindParty();
	const AABTSBirdPartySettings* Settings = Party ? Party->GetResolvedSettings() : nullptr;
	return Settings != nullptr && Settings->bClearMotionImmediatelyBeforePlayerJumpExperiment;
}

void AABTSM4PlayerController::RouteMoveForwardToControlledBird(const float Value)
{
	if (bGameplayInputBlocked || FMath::IsNearlyZero(Value) || !IsControllerRoutedMovementInputExperimentEnabled()) return;
	AABTSBirdParty* Party = FindParty();
	AABTSM25BirdCharacter* Bird = Party ? Party->GetControlledBird() : nullptr;
	if (Bird == nullptr) return;
	if (Bird->IsControlHandoffDiagnosticsActive()) UE_LOG(LogABTSRuntime, Warning,
		TEXT("[ABTS][M4][InputExperiment] RoutedForward Target=%d Value=%.2f ControllerPawn=%s"),
		ABTSBirdIdToIndex(Bird->GetBirdId()), Value, *GetNameSafe(GetPawn()));
	Bird->HandleControllerRoutedMoveForward(Value);
}

void AABTSM4PlayerController::RouteMoveRightToControlledBird(const float Value)
{
	if (bGameplayInputBlocked || FMath::IsNearlyZero(Value) || !IsControllerRoutedMovementInputExperimentEnabled()) return;
	AABTSBirdParty* Party = FindParty();
	AABTSM25BirdCharacter* Bird = Party ? Party->GetControlledBird() : nullptr;
	if (Bird == nullptr) return;
	if (Bird->IsControlHandoffDiagnosticsActive()) UE_LOG(LogABTSRuntime, Warning,
		TEXT("[ABTS][M4][InputExperiment] RoutedRight Target=%d Value=%.2f ControllerPawn=%s"),
		ABTSBirdIdToIndex(Bird->GetBirdId()), Value, *GetNameSafe(GetPawn()));
	Bird->HandleControllerRoutedMoveRight(Value);
}

void AABTSM4PlayerController::RouteJumpToControlledBird()
{
	if (bGameplayInputBlocked || !IsControllerRoutedMovementInputExperimentEnabled()) return;
	AABTSBirdParty* Party = FindParty();
	AABTSM25BirdCharacter* Bird = Party ? Party->GetControlledBird() : nullptr;
	if (Bird == nullptr) return;
	UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M4][InputExperiment] RoutedJump Target=%d ControllerPawn=%s"),
		ABTSBirdIdToIndex(Bird->GetBirdId()), *GetNameSafe(GetPawn()));
	Bird->HandleControllerRoutedJump();
}

void AABTSM4PlayerController::CycleBird()
{
	if (bGameplayInputBlocked) return;
	for (TActorIterator<AABTSBirdParty> It(GetWorld()); It; ++It)
	{
		if (It->CycleControlledBird()) return;
	}
}

void AABTSM4PlayerController::BeginOrbitInput()
{
	if (bGameplayInputBlocked) return;
	bOrbitInputHeld = true;
	if (PartyCamera) PartyCamera->BeginDirectManipulation();
	bSavedCursorPositionValid = GetMousePosition(SavedCursorX, SavedCursorY);
	SetCursorInteractionMode(false);
}

void AABTSM4PlayerController::EndOrbitInput()
{
	bOrbitInputHeld = false;
	if (PartyCamera) PartyCamera->EndDirectManipulation();
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
	(void)Value;
	if (bGameplayInputBlocked) return;
	if (PartyCamera == nullptr) return;
	if (bOrbitInputHeld)
	{
		PartyCamera->AddMouseOrbitYawInput(GetInputAnalogKeyState(EKeys::MouseX));
	}
	const float GamepadAxis = GetInputAnalogKeyState(EKeys::Gamepad_RightX);
	if (!FMath::IsNearlyZero(GamepadAxis))
	{
		PartyCamera->AddGamepadOrbitYawInput(GamepadAxis, GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f);
	}
}

void AABTSM4PlayerController::ApplyOrbitPitch(const float Value)
{
	(void)Value;
	if (bGameplayInputBlocked) return;
	if (PartyCamera == nullptr) return;
	if (bOrbitInputHeld)
	{
		// Preserve the existing ABTS_LookUp mouse mapping, whose MouseY scale is -1.
		PartyCamera->AddMouseOrbitPitchInput(-GetInputAnalogKeyState(EKeys::MouseY));
	}
	const float GamepadAxis = GetInputAnalogKeyState(EKeys::Gamepad_RightY);
	if (!FMath::IsNearlyZero(GamepadAxis))
	{
		PartyCamera->AddGamepadOrbitPitchInput(GamepadAxis, GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f);
	}
}

void AABTSM4PlayerController::ApplyCameraZoom(const float Value)
{
	if (bGameplayInputBlocked) return;
	if (PartyCamera) PartyCamera->AddZoomInput(Value);
}

void AABTSM4PlayerController::RecenterCamera()
{
	if (bGameplayInputBlocked) return;
	if (PartyCamera) PartyCamera->RequestRecenter();
}

void AABTSM4PlayerController::SetGameplayInputBlocked(const bool bBlocked)
{
	bGameplayInputBlocked = bBlocked;
	if (bBlocked && bOrbitInputHeld)
	{
		bOrbitInputHeld = false;
		if (PartyCamera) PartyCamera->EndDirectManipulation();
	}
	SetIgnoreMoveInput(bBlocked);
	SetIgnoreLookInput(bBlocked);
}

bool AABTSM4PlayerController::GetCameraRelativeMovementBasis(
	const FVector& WorldLocation,
	FVector& OutForward,
	FVector& OutRight) const
{
	return PartyCamera != nullptr && PartyCamera->GetMovementBasisAt(WorldLocation, OutForward, OutRight);
}

AABTSBirdParty* AABTSM4PlayerController::FindParty() const
{
	for (TActorIterator<AABTSBirdParty> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}
