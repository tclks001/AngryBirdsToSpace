// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/ABTSM11PlayerController.h"

#include "EngineUtils.h"
#include "Game/ABTSM11GameMode.h"
#include "InputCoreTypes.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "UI/ABTSM11FinaleHUD.h"
#include "World/ABTSM11FinaleInteractionSystem.h"
#include "World/ABTSM51WorldActors.h"

namespace
{
	const FName M11OrbitAction(TEXT("ABTS_CameraOrbitHold"));

	void SuspendM11OrbitReleaseBindings(
		UInputComponent& Input,
		TArray<FInputActionBinding>& OutSuspendedBindings)
	{
		OutSuspendedBindings.Reset();
		for (int32 BindingIndex = Input.GetNumActionBindings() - 1;
			BindingIndex >= 0;
			--BindingIndex)
		{
			const FInputActionBinding& Binding =
				Input.GetActionBinding(BindingIndex);
			if (Binding.GetActionName() != M11OrbitAction
				|| Binding.KeyEvent != IE_Released)
			{
				continue;
			}
			OutSuspendedBindings.Insert(Binding, 0);
			Input.RemoveActionBinding(BindingIndex);
		}
	}

	void RestoreM11OrbitReleaseBindings(
		UInputComponent& Input,
		TArray<FInputActionBinding>& SuspendedBindings)
	{
		for (int32 BindingIndex = Input.GetNumActionBindings() - 1;
			BindingIndex >= 0;
			--BindingIndex)
		{
			const FInputActionBinding& Binding =
				Input.GetActionBinding(BindingIndex);
			if (Binding.GetActionName() == M11OrbitAction
				&& Binding.KeyEvent == IE_Released)
			{
				Input.RemoveActionBinding(BindingIndex);
			}
		}
		for (FInputActionBinding& Binding : SuspendedBindings)
		{
			Input.AddActionBinding(MoveTemp(Binding));
		}
		SuspendedBindings.Reset();
	}
}

void AABTSM11PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindAction(
		TEXT("ABTS_PrimaryInteract"),
		IE_Released,
		this,
		&AABTSM11PlayerController::M11PrimaryReleased);
	InputComponent->BindAction(
		TEXT("ABTS_PrimaryInteract"),
		IE_DoubleClick,
		this,
		&AABTSM11PlayerController::M11PrimaryDoubleClicked);
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
	if (bActive && Interaction->IsAiming())
	{
		float MouseX = 0.0f;
		float MouseY = 0.0f;
		if (GetMousePosition(MouseX, MouseY))
		{
			if (AABTSM11FinaleHUD* FinaleHud =
				Cast<AABTSM11FinaleHUD>(GetHUD()))
			{
				FinaleHud->HandleFinalePointerMoved(
					*Interaction,
					FVector2D(MouseX, MouseY));
			}
		}
	}
	if (bActive != bWasM11FinaleActive)
	{
		SetM11FinaleInputMode(bActive);
		if (!bActive)
		{
			if (AABTSM11FinaleHUD* FinaleHud =
				Cast<AABTSM11FinaleHUD>(GetHUD()))
			{
				FinaleHud->CancelFinaleHudCapture();
			}
		}
		bWasM11FinaleActive = bActive;
	}
}

void AABTSM11PlayerController::FlushPressedKeys()
{
	// Focus loss may synthesize a release. Disarm before the base class
	// flushes keys so an inherited/synthetic release can never launch.
	if (AABTSM11FinaleHUD* FinaleHud =
		Cast<AABTSM11FinaleHUD>(GetHUD()))
	{
		FinaleHud->CancelFinaleHudCapture();
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
				// HUD-1B enters a console. The entry click never arms launch;
				// only the explicit LAUNCH button may request Release.
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
		Interaction != nullptr
		&& ABTSM11RequiresExclusiveFinaleHudPointerRouting(
			Interaction->IsFinaleActive()))
	{
		// Route the custom Canvas console before any inherited Inventory HUD
		// consumption gate. The M11 HUD suppresses that visual layer but its
		// cached layout can still overlap the bottom control deck.
		float MouseX = 0.0f;
		float MouseY = 0.0f;
		if (Interaction->IsAiming()
			&& GetMousePosition(MouseX, MouseY))
		{
			if (AABTSM11FinaleHUD* FinaleHud =
				Cast<AABTSM11FinaleHUD>(GetHUD()))
			{
				FinaleHud->HandleFinalePrimaryPressed(
					*Interaction,
					FVector2D(MouseX, MouseY));
			}
		}
		return;
	}

	// The finale console owns the primary pointer while active. Only consult
	// inherited HUD hit targets after the exclusive finale route above; the
	// hidden M5 inventory HUD retains cached bottom-deck hit boxes and would
	// otherwise consume all four M11 buttons before they can establish capture.
	if (ShouldConsumePrimaryPointerForHUD()) return;

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
		&& Interaction->IsFinaleActive())
	{
		float MouseX = 0.0f;
		float MouseY = 0.0f;
		if (GetMousePosition(MouseX, MouseY))
		{
			if (AABTSM11FinaleHUD* FinaleHud =
				Cast<AABTSM11FinaleHUD>(GetHUD()))
			{
				FinaleHud->HandleFinalePrimaryReleased(
					*Interaction,
					FVector2D(MouseX, MouseY));
			}
		}
	}
}

void AABTSM11PlayerController::M11PrimaryDoubleClicked()
{
	if (AABTSM11FinaleInteractionSystem* Interaction = FindM11Interaction();
		Interaction != nullptr && Interaction->IsAiming())
	{
		float MouseX = 0.0f;
		float MouseY = 0.0f;
		if (GetMousePosition(MouseX, MouseY))
		{
			if (AABTSM11FinaleHUD* FinaleHud =
				Cast<AABTSM11FinaleHUD>(GetHUD()))
			{
				FinaleHud->HandleFinalePrimaryDoubleClicked(
					*Interaction,
					FVector2D(MouseX, MouseY));
			}
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
			float MouseX = 0.0f;
			float MouseY = 0.0f;
			if (GetMousePosition(MouseX, MouseY))
			{
				if (AABTSM11FinaleHUD* FinaleHud =
					Cast<AABTSM11FinaleHUD>(GetHUD()))
				{
					FinaleHud->HandleFinaleWheel(
						*Interaction,
						FVector2D(MouseX, MouseY),
						Value);
				}
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
		float MouseX = 0.0f;
		float MouseY = 0.0f;
		if (Interaction->IsAiming()
			&& GetMousePosition(MouseX, MouseY))
		{
			if (AABTSM11FinaleHUD* FinaleHud =
				Cast<AABTSM11FinaleHUD>(GetHUD()))
			{
				FinaleHud->HandleFinaleSecondaryPressed(
					*Interaction,
					FVector2D(MouseX, MouseY));
			}
		}
	}
}

void AABTSM11PlayerController::M11OrbitReleased()
{
	if (AABTSM11FinaleInteractionSystem* Interaction =
		FindM11Interaction();
		Interaction != nullptr && Interaction->IsFinaleActive())
	{
		float MouseX = 0.0f;
		float MouseY = 0.0f;
		if (GetMousePosition(MouseX, MouseY))
		{
			if (AABTSM11FinaleHUD* FinaleHud =
				Cast<AABTSM11FinaleHUD>(GetHUD()))
			{
				FinaleHud->HandleFinaleSecondaryReleased(
					*Interaction,
					FVector2D(MouseX, MouseY));
			}
		}
	}
}

void AABTSM11PlayerController::SetM11FinaleInputMode(
	const bool bActive)
{
	SetLaunchModeInputBlocked(bActive);
	SetM11OrbitReleaseBindingIsolation(bActive);
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
}

void AABTSM11PlayerController::SetM11OrbitReleaseBindingIsolation(
	const bool bIsolated)
{
	if (InputComponent == nullptr
		|| bM11OrbitReleaseBindingsIsolated == bIsolated)
	{
		return;
	}
	if (bIsolated)
	{
		SuspendM11OrbitReleaseBindings(
			*InputComponent,
			SuspendedM11OrbitReleaseBindings);
		InputComponent->BindAction(
			M11OrbitAction,
			IE_Released,
			this,
			&AABTSM11PlayerController::M11OrbitReleased);
		bM11OrbitReleaseBindingsIsolated = true;
		return;
	}

	RestoreM11OrbitReleaseBindings(
		*InputComponent,
		SuspendedM11OrbitReleaseBindings);
	bM11OrbitReleaseBindingsIsolated = false;
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

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11FinaleOrbitReleaseBindingIsolationTest,
	"ABTS.M11C.HUD.Unit.CursorReleaseBinding",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11FinaleOrbitReleaseBindingIsolationTest::RunTest(
	const FString& Parameters)
{
	UInputComponent* Input = NewObject<UInputComponent>();
	Input->AddActionBinding(FInputActionBinding(M11OrbitAction, IE_Pressed));
	Input->AddActionBinding(FInputActionBinding(M11OrbitAction, IE_Released));
	Input->AddActionBinding(FInputActionBinding(M11OrbitAction, IE_Released));
	Input->AddActionBinding(FInputActionBinding(TEXT("ABTS_Other"), IE_Released));

	TArray<FInputActionBinding> Suspended;
	SuspendM11OrbitReleaseBindings(*Input, Suspended);
	TestEqual(TEXT("Both inherited and M11 releases are suspended"),
		Suspended.Num(), 2);
	TestEqual(TEXT("Only unrelated and press bindings remain"),
		Input->GetNumActionBindings(), 2);
	for (int32 BindingIndex = 0;
		BindingIndex < Input->GetNumActionBindings();
		++BindingIndex)
	{
		const FInputActionBinding& Binding =
			Input->GetActionBinding(BindingIndex);
		TestFalse(TEXT("No orbit release can warp the finale cursor"),
			Binding.GetActionName() == M11OrbitAction
				&& Binding.KeyEvent == IE_Released);
	}

	Input->AddActionBinding(
		FInputActionBinding(M11OrbitAction, IE_Released));
	Input->AddActionBinding(
		FInputActionBinding(M11OrbitAction, IE_Released));
	RestoreM11OrbitReleaseBindings(*Input, Suspended);
	int32 RestoredReleaseCount = 0;
	for (int32 BindingIndex = 0;
		BindingIndex < Input->GetNumActionBindings();
		++BindingIndex)
	{
		const FInputActionBinding& Binding =
			Input->GetActionBinding(BindingIndex);
		if (Binding.GetActionName() == M11OrbitAction
			&& Binding.KeyEvent == IE_Released)
		{
			++RestoredReleaseCount;
		}
	}
	TestEqual(TEXT("Ordinary orbit releases return after finale exit"),
		RestoredReleaseCount, 2);
	TestEqual(TEXT("Suspended storage is drained after restoration"),
		Suspended.Num(), 0);
	return true;
}
#endif
