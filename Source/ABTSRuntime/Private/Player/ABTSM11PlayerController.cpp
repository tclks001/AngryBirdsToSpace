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
		TEXT("ABTS_Turn"),
		this,
		&AABTSM11PlayerController::M11Yaw);
	InputComponent->BindAxis(
		TEXT("ABTS_LookUp"),
		this,
		&AABTSM11PlayerController::M11Pitch);
	InputComponent->BindAxis(
		TEXT("ABTS_CameraZoom"),
		this,
		&AABTSM11PlayerController::M11Power);
	InputComponent->BindAction(
		TEXT("ABTS_CameraRecenter"),
		IE_Pressed,
		this,
		&AABTSM11PlayerController::M11Cancel);
}

void AABTSM11PlayerController::PlayerTick(const float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	AABTSM11FinaleInteractionSystem* Interaction =
		FindM11Interaction();
	const bool bActive =
		Interaction != nullptr && Interaction->IsFinaleActive();
	if (bActive)
	{
		PrimaryReleaseGate.UpdateEntryButtonState(
			IsInputKeyDown(EKeys::LeftMouseButton));
		if (bM11PointerCaptureNeedsRefresh || bShowMouseCursor)
		{
			ApplyM11PointerMode(true);
			bM11PointerCaptureNeedsRefresh = false;
		}
	}
	if (bActive != bWasM11FinaleActive)
	{
		SetM11FinaleInputMode(bActive);
		if (!bActive)
		{
			PrimaryReleaseGate.Reset();
		}
		bWasM11FinaleActive = bActive;
	}
}

void AABTSM11PlayerController::FlushPressedKeys()
{
	// Focus loss must invalidate a partially armed launch. A fresh full
	// press/release gesture is required after the viewport becomes active.
	PrimaryReleaseGate.Reset();
	if (bWasM11FinaleActive)
	{
		bM11PointerCaptureNeedsRefresh = true;
	}
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
		if (IsCraftingInterfaceOpen())
		{
			return;
		}
		if (AABTSM11FinaleInteractionSystem* Interaction =
			FindM11Interaction())
		{
			if (Interaction->TryEnterFinale(*Cord, *this))
			{
				// The actor click that entered aim must be consumed in full.
				// Only a later press/release pair may launch.
				PrimaryReleaseGate.Enter(true);
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
		PrimaryReleaseGate.OnPrimaryPressed(
			Interaction->IsAiming());
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
		&& PrimaryReleaseGate.OnPrimaryReleased(
			Interaction->IsAiming()))
	{
		Interaction->RequestRelease();
	}
}

void AABTSM11PlayerController::M11Yaw(const float Value)
{
	if (!FMath::IsNearlyZero(Value))
	{
		if (AABTSM11FinaleInteractionSystem* Interaction =
			FindM11Interaction();
			Interaction != nullptr && Interaction->IsAiming())
		{
			Interaction->ApplyAimAxis(Value, 0.0, 0.0);
		}
	}
}

void AABTSM11PlayerController::M11Pitch(const float Value)
{
	if (!FMath::IsNearlyZero(Value))
	{
		if (AABTSM11FinaleInteractionSystem* Interaction =
			FindM11Interaction();
			Interaction != nullptr && Interaction->IsAiming())
		{
			Interaction->ApplyAimAxis(0.0, Value, 0.0);
		}
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
			Interaction->ApplyAimAxis(0.0, 0.0, -Value);
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
	ApplyM11PointerMode(bActive);
	bM11PointerCaptureNeedsRefresh = false;
}

void AABTSM11PlayerController::ApplyM11PointerMode(
	const bool bActive)
{
	bShowMouseCursor = !bActive;
	if (bActive)
	{
		FInputModeGameOnly InputMode;
		SetInputMode(InputMode);
		return;
	}
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(
		EMouseLockMode::LockOnCapture);
	SetInputMode(InputMode);
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
