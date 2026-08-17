// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM8GameMode.h"

#include "ABTSRuntime.h"
#include "Crafting/ABTSCraftingSystem.h"
#include "EngineUtils.h"
#include "Inventory/ABTSInventoryComponent.h"
#include "Inventory/ABTSInventoryTypes.h"
#include "World/ABTSM8RecoveryBridgeSystem.h"

AABTSM8GameMode::AABTSM8GameMode()
{
	RecoveryBridgeSystemClass = AABTSM8RecoveryBridgeSystem::StaticClass();
}

void AABTSM8GameMode::OnInitialPlayerPlaced(ACharacter& Character, const FTransform& SpawnTransform, const int32 SpawnCellId)
{
	Super::OnInitialPlayerPlaced(Character, SpawnTransform, SpawnCellId);
	if (GetWorld() == nullptr) return;
	const bool bResolvedSeedDebugMaximumInventory =
#if UE_BUILD_SHIPPING
		false;
#else
		bSeedDebugMaximumInventory;
#endif
	if (bResolvedSeedDebugMaximumInventory)
	{
		for (TActorIterator<AABTSCraftingSystem> It(GetWorld()); It; ++It)
		{
			if (UABTSInventoryComponent* Inventory = It->GetInventory())
			{
				// Bridge Kit is first so it is immediately visible in the eight-slot hotbar.
				Inventory->AddItem(EABTSItemId::BridgeKit, DebugMaximumInventoryQuantity);
				for (const EABTSItemId ItemId : ABTSGetAllItemIds())
				{
					if (ItemId != EABTSItemId::BridgeKit) Inventory->AddItem(ItemId, DebugMaximumInventoryQuantity);
				}
				UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M8][Debug] Maximum inventory seeded Quantity=%d"), DebugMaximumInventoryQuantity);
			}
			break;
		}
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM8RecoveryBridgeSystem* System = GetWorld()->SpawnActor<AABTSM8RecoveryBridgeSystem>(RecoveryBridgeSystemClass, FTransform::Identity, Params);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M8] Entry ready=%d StartCell=%d DebugInventory=%d ShippingDebugInventoryHardOff=%d"),
		System ? 1 : 0,
		SpawnCellId,
		bResolvedSeedDebugMaximumInventory ? 1 : 0,
		UE_BUILD_SHIPPING ? 1 : 0);
}
