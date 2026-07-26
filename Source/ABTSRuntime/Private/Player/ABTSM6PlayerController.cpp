// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/ABTSM6PlayerController.h"

#include "EngineUtils.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "World/ABTSM51WorldActors.h"

void AABTSM6PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindAction(TEXT("ABTS_PrimaryInteract"), IE_Released, this, &AABTSM6PlayerController::M6PrimaryReleased);
	InputComponent->BindAxis(TEXT("ABTS_CameraZoom"), this, &AABTSM6PlayerController::M6AdjustPower);
}

void AABTSM6PlayerController::PlayerTick(const float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (AABTSM6SlingshotSystem* System = FindSlingshotSystem())
	{
		if (System->GetLaunchState() == EABTSM6LaunchState::Pulling) System->UpdateAimFromCursor(*this);
	}
}

void AABTSM6PlayerController::InteractWithSlingshotCord(AABTSM51SlingshotCord* Cord)
{
	if (Cord)
	{
		if (AABTSM6SlingshotSystem* System = FindSlingshotSystem()) System->TryEnterLaunchMode(*Cord);
	}
}

void AABTSM6PlayerController::PrimaryWorldInteract()
{
	AABTSM6SlingshotSystem* System = FindSlingshotSystem();
	if (System == nullptr) { Super::PrimaryWorldInteract(); return; }
	if (System->GetLaunchState() == EABTSM6LaunchState::Ready)
	{
		System->BeginPull(*this);
		return;
	}
	if (System->GetLaunchState() == EABTSM6LaunchState::Flying
		|| System->GetLaunchState() == EABTSM6LaunchState::Settling)
	{
		FHitResult Hit;
		if (GetHitResultUnderCursor(ECC_Visibility, false, Hit)) System->TryManualBlackDetonation(Hit.GetActor());
		return;
	}
	if (!System->IsLaunchModeActive()) Super::PrimaryWorldInteract();
}

void AABTSM6PlayerController::M6PrimaryReleased()
{
	if (AABTSM6SlingshotSystem* System = FindSlingshotSystem())
	{
		if (System->GetLaunchState() == EABTSM6LaunchState::Pulling) System->ReleaseLaunch();
	}
}

void AABTSM6PlayerController::M6AdjustPower(const float Value)
{
	if (FMath::IsNearlyZero(Value)) return;
	// M5 owns the wheel while its modal backpack is open.  Do not let an armed
	// slingshot consume the same input behind that interface.
	if (IsCraftingInterfaceOpen()) return;
	if (AABTSM6SlingshotSystem* System = FindSlingshotSystem()) System->AdjustPullPower(Value);
}

AABTSM6SlingshotSystem* AABTSM6PlayerController::FindSlingshotSystem()
{
	if (SlingshotSystem.IsValid()) return SlingshotSystem.Get();
	for (TActorIterator<AABTSM6SlingshotSystem> It(GetWorld()); It; ++It)
	{
		SlingshotSystem = *It;
		return SlingshotSystem.Get();
	}
	return nullptr;
}
