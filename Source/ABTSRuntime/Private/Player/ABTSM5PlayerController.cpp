// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/ABTSM5PlayerController.h"

#include "ABTSRuntime.h"
#include "Crafting/ABTSCraftingStation.h"
#include "Crafting/ABTSCraftingSystem.h"
#include "EngineUtils.h"
#include "UI/ABTSM5InventoryHUD.h"

void AABTSM5PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindAction(TEXT("ABTS_ToggleCrafting"), IE_Pressed, this, &AABTSM5PlayerController::ToggleCraftingInterface);
}

void AABTSM5PlayerController::OpenCraftingInterface()
{
	if (bCraftingInterfaceOpen) return;
	bCraftingInterfaceOpen = true;
	SetGameplayInputBlocked(true);
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M5][UI] Crafting opened. Red=%d"),
		FindCraftingSystem() && FindCraftingSystem()->IsRedBirdControlled() ? 1 : 0);
}

void AABTSM5PlayerController::CloseCraftingInterface()
{
	if (!bCraftingInterfaceOpen) return;
	bCraftingInterfaceOpen = false;
	SetGameplayInputBlocked(false);
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::LockOnCapture);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
	if (AABTSM5InventoryHUD* InventoryHUD = Cast<AABTSM5InventoryHUD>(GetHUD())) InventoryHUD->ResetCraftingSelection();
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M5][UI] Crafting closed."));
}

void AABTSM5PlayerController::ToggleCraftingInterface()
{
	if (bCraftingInterfaceOpen) CloseCraftingInterface();
	else OpenCraftingInterface();
}

void AABTSM5PlayerController::OpenCraftingFromStation(const AABTSCraftingStation* Station)
{
	OpenCraftingInterface();
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M5][UI] Station click Type=%d Nearby=%d"),
		Station ? static_cast<int32>(Station->GetStationType()) : INDEX_NONE,
		Station && FindCraftingSystem() && FindCraftingSystem()->IsStationAvailable(Station->GetStationType()) ? 1 : 0);
}

AABTSCraftingSystem* AABTSM5PlayerController::FindCraftingSystem()
{
	if (CraftingSystem.IsValid()) return CraftingSystem.Get();
	for (TActorIterator<AABTSCraftingSystem> It(GetWorld()); It; ++It)
	{
		CraftingSystem = *It;
		return CraftingSystem.Get();
	}
	return nullptr;
}

