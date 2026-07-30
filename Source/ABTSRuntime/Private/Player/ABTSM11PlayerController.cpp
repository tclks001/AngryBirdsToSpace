// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/ABTSM11PlayerController.h"

#include "EngineUtils.h"
#include "Game/ABTSM11GameMode.h"
#include "InputCoreTypes.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "World/ABTSM11FinaleInteractionSystem.h"
#include "World/ABTSM51WorldActors.h"

void AABTSM11PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindAction(
		TEXT("ABTS_PrimaryInteract"),
		IE_Released,
		this,
		&AABTSM11PlayerController::M11PrimaryReleased);
	InputComponent->BindAxis(
		TEXT("ABTS_CameraZoom"),
		this,
		&AABTSM11PlayerController::M11Power);
	InputComponent->BindAction(
		TEXT("ABTS_CameraRecenter"),
		IE_Pressed,
		this,
		&AABTSM11PlayerController::M11Cancel);
	InputComponent->BindAction(
		TEXT("ABTS_CameraOrbitHold"),
		IE_Pressed,
		this,
		&AABTSM11PlayerController::M11OrbitPressed);
	InputComponent->BindAction(
		TEXT("ABTS_CameraOrbitHold"),
		IE_Released,
		this,
		&AABTSM11PlayerController::M11OrbitReleased);
}

void AABTSM11PlayerController::PlayerTick(const float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	AABTSM11FinaleInteractionSystem* Interaction =
		FindM11Interaction();
	const bool bActive =
		Interaction != nullptr && Interaction->IsFinaleActive();
	const bool bRestoreOrbitCursorThisFrame =
		bActive
		&& bRestoreM11CursorAfterOrbitRelease
		&& bM11OrbitCursorSaved;
	if (bRestoreOrbitCursorThisFrame)
	{
		SetMouseLocation(
			FMath::RoundToInt(M11OrbitCursorX),
			FMath::RoundToInt(M11OrbitCursorY));
		bRestoreM11CursorAfterOrbitRelease = false;
	}
	if (bActive
		&& !bRestoreOrbitCursorThisFrame
		&& bM11PullReleaseArmed
		&& Interaction->IsAiming()
		&& IsInputKeyDown(EKeys::LeftMouseButton))
	{
		Interaction->UpdateAimFromCursor(*this);
	}
	if (bActive != bWasM11FinaleActive)
	{
		SetM11FinaleInputMode(bActive);
		if (!bActive)
		{
			bM11PullReleaseArmed = false;
		}
		bWasM11FinaleActive = bActive;
	}
}

void AABTSM11PlayerController::FlushPressedKeys()
{
	// Focus loss may synthesize a release. Disarm before the base class
	// flushes keys so an inherited/synthetic release can never launch.
	bM11PullReleaseArmed = false;
	bM11OrbitCursorSaved = false;
	bRestoreM11CursorAfterOrbitRelease = false;
	Super::FlushPressedKeys();
}

void AABTSM11PlayerController::InteractWithSlingshotCord(
	AABTSM51SlingshotCord* Cord)
{
	if (Cord == nullptr)
	{
		return;
	}
	if (Cord->IsFinaleSpaceSlingshot())
	{
		// Finale Space is fail-closed. It must never fall through to M6's
		// Chaos flight even if the M11 runtime is unavailable.
		if (IsCraftingInterfaceOpen()
			|| IsInputKeyDown(EKeys::RightMouseButton))
		{
			return;
		}
		if (AABTSM11FinaleInteractionSystem* Interaction =
			FindM11Interaction())
		{
			if (Interaction->TryEnterFinale(*Cord, *this))
			{
				// The Space-pouch actor press is also the first drag press.
				// Its matching release launches; no second click is required.
				bM11PullReleaseArmed = true;
				SetM11FinaleInputMode(true);
				bWasM11FinaleActive = true;
			}
		}
		return;
	}
	Super::InteractWithSlingshotCord(Cord);
}

void AABTSM11PlayerController::PrimaryWorldInteract()
{
	if (AABTSM11FinaleInteractionSystem* Interaction =
		FindM11Interaction();
		Interaction != nullptr && Interaction->IsFinaleActive())
	{
		if (Interaction->IsAiming()
			&& Interaction->BeginAimFromCursor(*this))
		{
			bM11PullReleaseArmed = true;
		}
		return;
	}

	AABTSM6SlingshotSystem* System =
		FindOrdinarySlingshotSystem();
	if (System == nullptr)
	{
		AABTSM51PlayerController::PrimaryWorldInteract();
		return;
	}
	if (System->GetLaunchState() == EABTSM6LaunchState::Ready)
	{
		System->BeginPull(*this);
		return;
	}
	if (System->GetLaunchState() == EABTSM6LaunchState::Flying
		|| System->GetLaunchState()
			== EABTSM6LaunchState::Settling)
	{
		FHitResult Hit;
		if (GetHitResultUnderCursor(ECC_Visibility, false, Hit))
		{
			System->TryManualBlackDetonation(Hit.GetActor());
		}
		return;
	}
	if (!System->IsLaunchModeActive())
	{
		AABTSM51PlayerController::PrimaryWorldInteract();
	}
}

void AABTSM11PlayerController::M11PrimaryReleased()
{
	if (AABTSM11FinaleInteractionSystem* Interaction =
		FindM11Interaction();
		Interaction != nullptr
		&& Interaction->IsAiming()
		&& bM11PullReleaseArmed)
	{
		bM11PullReleaseArmed = false;
		Interaction->RequestRelease();
	}
	else
	{
		bM11PullReleaseArmed = false;
	}
}

void AABTSM11PlayerController::M11Power(const float Value)
{
	if (!FMath::IsNearlyZero(Value)
		&& !IsCraftingInterfaceOpen())
	{
		if (AABTSM11FinaleInteractionSystem* Interaction =
			FindM11Interaction();
			Interaction != nullptr && Interaction->IsAiming())
		{
			// Match the existing ABTS convention: wheel down increases power.
			Interaction->AdjustAimPower(-Value);
			if (bM11PullReleaseArmed)
			{
				Interaction->UpdateAimFromCursor(*this);
			}
		}
	}
}

void AABTSM11PlayerController::M11Cancel()
{
	if (AABTSM11FinaleInteractionSystem* Interaction =
		FindM11Interaction();
		Interaction != nullptr && Interaction->IsFinaleActive())
	{
		Interaction->CancelStabilizerOrResetAttempt();
	}
}

void AABTSM11PlayerController::M11OrbitPressed()
{
	if (AABTSM11FinaleInteractionSystem* Interaction =
		FindM11Interaction();
		Interaction != nullptr && Interaction->IsFinaleActive())
	{
		bM11OrbitCursorSaved = GetMousePosition(
			M11OrbitCursorX,
			M11OrbitCursorY);
		bRestoreM11CursorAfterOrbitRelease = false;
	}
}

void AABTSM11PlayerController::M11OrbitReleased()
{
	if (AABTSM11FinaleInteractionSystem* Interaction =
		FindM11Interaction();
		Interaction != nullptr && Interaction->IsFinaleActive())
	{
		// M4's release handler restores its historical orbit cursor even when
		// finale input blocked the matching press. Restore the M11 cursor
		// after all input delegates have run, in PlayerTick.
		bRestoreM11CursorAfterOrbitRelease =
			bM11OrbitCursorSaved;
	}
}

void AABTSM11PlayerController::SetM11FinaleInputMode(
	const bool bActive)
{
	SetLaunchModeInputBlocked(bActive);
	if (bActive)
	{
		if (!bM11SavedPointerEventFlags)
		{
			bSavedClickEvents = bEnableClickEvents;
			bSavedMouseOverEvents = bEnableMouseOverEvents;
			bM11SavedPointerEventFlags = true;
		}
		// Finale launch presses are consumed by InputComponent only. Prevent
		// the same press from also clicking a stake, slot, or crafting actor.
		bEnableClickEvents = false;
		bEnableMouseOverEvents = false;
	}
	else if (bM11SavedPointerEventFlags)
	{
		bEnableClickEvents = bSavedClickEvents;
		bEnableMouseOverEvents = bSavedMouseOverEvents;
		bM11SavedPointerEventFlags = false;
	}
	if (!bActive)
	{
		bM11OrbitCursorSaved = false;
		bRestoreM11CursorAfterOrbitRelease = false;
	}
	ApplyM11PointerMode(bActive);
}

void AABTSM11PlayerController::ApplyM11PointerMode(
	const bool bFinaleActive)
{
	bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(
		EMouseLockMode::LockOnCapture);
	SetInputMode(InputMode);
	if (bFinaleActive)
	{
		return;
	}
	RestorePartyCameraView();
}

AABTSM11FinaleInteractionSystem*
AABTSM11PlayerController::FindM11Interaction() const
{
	const AABTSM11GameMode* GameMode =
		GetWorld() != nullptr
		? Cast<AABTSM11GameMode>(GetWorld()->GetAuthGameMode())
		: nullptr;
	return GameMode != nullptr
		? GameMode->GetFinaleInteractionSystem()
		: nullptr;
}

AABTSM6SlingshotSystem*
AABTSM11PlayerController::FindOrdinarySlingshotSystem()
{
	if (OrdinarySlingshotSystem.IsValid())
	{
		return OrdinarySlingshotSystem.Get();
	}
	if (GetWorld() == nullptr)
	{
		return nullptr;
	}
	for (TActorIterator<AABTSM6SlingshotSystem> It(GetWorld()); It; ++It)
	{
		OrdinarySlingshotSystem = *It;
		return OrdinarySlingshotSystem.Get();
	}
	return nullptr;
}
