// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Inventory/ABTSInventoryTypes.h"
#include "ABTSInventoryComponent.generated.h"

DECLARE_MULTICAST_DELEGATE(FABTSInventoryChangedNative);

/** Shared party inventory. Stack ordering is permanently defined by first acquisition. */
UCLASS(ClassGroup = (ABTS), meta = (BlueprintSpawnableComponent))
class ABTSRUNTIME_API UABTSInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UABTSInventoryComponent();

	UFUNCTION(BlueprintCallable, Category = "ABTS|M5|Inventory")
	bool AddItem(EABTSItemId ItemId, int32 Quantity);

	UFUNCTION(BlueprintCallable, Category = "ABTS|M5|Inventory")
	bool RemoveItem(EABTSItemId ItemId, int32 Quantity);

	UFUNCTION(BlueprintPure, Category = "ABTS|M5|Inventory")
	int32 GetQuantity(EABTSItemId ItemId) const;

	const TArray<FABTSItemStack>& GetOrderedStacks() const { return OrderedStacks; }

	UFUNCTION(BlueprintPure, Category = "ABTS|M5|Inventory")
	int32 GetHotbarSlotCount() const { return HotbarSlotCount; }

	bool GetHotbarItemAt(int32 SlotIndex, EABTSItemId& OutItemId) const;
	bool SetHeldItem(EABTSItemId ItemId);
	void ClearHeldItem();
	bool GetHeldItem(EABTSItemId& OutItemId) const;

	/** Temporary M5-only seed used until M5.1 supplies pickups and persistence. */
	void SeedMilestoneInventory(const TArray<FABTSItemStack>& StarterItems);

	FABTSInventoryChangedNative OnInventoryChanged;

private:
	FABTSItemStack* FindStack(EABTSItemId ItemId);
	const FABTSItemStack* FindStack(EABTSItemId ItemId) const;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M5|Inventory")
	TArray<FABTSItemStack> OrderedStacks;

	UPROPERTY(EditAnywhere, Category = "ABTS|M5|Inventory", meta = (ClampMin = "1", ClampMax = "12"))
	int32 HotbarSlotCount = 8;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M5|Inventory")
	TArray<FABTSHotbarSlot> HotbarSlots;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M5.1|Held")
	bool bHasHeldItem = false;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M5.1|Held")
	EABTSItemId HeldItemId = EABTSItemId::Branch;

	int32 NextAcquisitionOrder = 0;
	bool bMilestoneSeeded = false;
};
