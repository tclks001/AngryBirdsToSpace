// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/ABTSM51PlayerController.h"

#include "Crafting/ABTSCraftingStation.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "UI/ABTSM5InventoryHUD.h"
#include "World/ABTSM51WorldActors.h"
#include "World/ABTSM51WorldSystem.h"
#include "World/ABTSM8RecoveryBridgeSystem.h"

void AABTSM51PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindAction(TEXT("ABTS_PrimaryInteract"), IE_Pressed, this, &AABTSM51PlayerController::PrimaryWorldInteract);
}

void AABTSM51PlayerController::PrimaryWorldInteract()
{
	if (ShouldConsumePrimaryPointerForHUD()) return;
	FHitResult Hit;
	if (GetHitResultUnderCursor(ECC_Visibility, false, Hit))
	{
		if (Hit.GetActor() && (Hit.GetActor()->IsA<AABTSM51SlingshotDirtHole>()
			|| Hit.GetActor()->IsA<AABTSM51SlingshotStake>()
			|| Hit.GetActor()->IsA<AABTSM51SlingshotCord>()
			|| Hit.GetActor()->IsA<AABTSCraftingStation>())) return;
	}
	if (AABTSM51WorldSystem* System = FindWorldSystem())
	{
		if (System->PlaceHeldToolAtAim(*this)) return;
		if (System->PlaceHeldStakeAtAim(*this)) return;
	}
	for (TActorIterator<AABTSM8RecoveryBridgeSystem> It(GetWorld()); It; ++It)
	{
		if (It->PlaceHeldBridgeAtAim(*this)) return;
	}
}

bool AABTSM51PlayerController::ShouldConsumePrimaryPointerForHUD() const
{
	if (IsCraftingInterfaceOpen()) return true;
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (!GetMousePosition(MouseX, MouseY)) return false;
	const FVector2D ScreenPosition(MouseX, MouseY);
	if (const AABTSM5InventoryHUD* InventoryHUD = Cast<AABTSM5InventoryHUD>(GetHUD());
		InventoryHUD != nullptr
		&& InventoryHUD->ConsumesPrimaryPointerAtScreenPosition(ScreenPosition))
	{
		return true;
	}
	const AHUD* HUD = GetHUD();
	return HUD != nullptr
		&& HUD->GetHitBoxAtCoordinates(ScreenPosition, true) != nullptr;
}

void AABTSM51PlayerController::InteractWithDirtHole(AABTSM51SlingshotDirtHole* Hole)
{
	if (!IsCraftingInterfaceOpen() && Hole)
	{
		if (AABTSM51WorldSystem* System = FindWorldSystem()) System->InstallHeldStake(*Hole);
	}
}

void AABTSM51PlayerController::InteractWithStake(AABTSM51SlingshotStake* Stake)
{
	if (!IsCraftingInterfaceOpen() && Stake)
	{
		if (AABTSM51WorldSystem* System = FindWorldSystem()) System->SelectStakeForHeldCord(*Stake);
	}
}

AABTSM51WorldSystem* AABTSM51PlayerController::FindWorldSystem()
{
	if (WorldSystem.IsValid()) return WorldSystem.Get();
	for (TActorIterator<AABTSM51WorldSystem> It(GetWorld()); It; ++It)
	{
		WorldSystem = *It;
		return WorldSystem.Get();
	}
	return nullptr;
}
