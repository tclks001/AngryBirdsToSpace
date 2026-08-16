// Copyright Epic Games, Inc. All Rights Reserved.

#include "Inventory/ABTSInventoryComponent.h"

#include "Guide/ABTSGuideEvents.h"

UABTSInventoryComponent::UABTSInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	HotbarSlots.SetNum(HotbarSlotCount);
}

FABTSItemStack* UABTSInventoryComponent::FindStack(const EABTSItemId ItemId)
{
	return OrderedStacks.FindByPredicate([ItemId](const FABTSItemStack& Stack) { return Stack.ItemId == ItemId; });
}

const FABTSItemStack* UABTSInventoryComponent::FindStack(const EABTSItemId ItemId) const
{
	return OrderedStacks.FindByPredicate([ItemId](const FABTSItemStack& Stack) { return Stack.ItemId == ItemId; });
}

bool UABTSInventoryComponent::AddItem(const EABTSItemId ItemId, const int32 Quantity)
{
	if (Quantity <= 0) return false;
	if (FABTSItemStack* Existing = FindStack(ItemId))
	{
		Existing->Quantity += Quantity;
	}
	else
	{
		FABTSItemStack& NewStack = OrderedStacks.AddDefaulted_GetRef();
		NewStack.ItemId = ItemId;
		NewStack.Quantity = Quantity;
		NewStack.FirstAcquiredOrder = NextAcquisitionOrder++;
		if (FABTSHotbarSlot* EmptySlot = HotbarSlots.FindByPredicate([](const FABTSHotbarSlot& Slot) { return !Slot.bOccupied; }))
		{
			EmptySlot->bOccupied = true;
			EmptySlot->ItemId = ItemId;
		}
	}
	OnInventoryChanged.Broadcast();
	FABTSGuideEventBus::Publish(this, FABTSGuideEventIds::ItemAcquired,
		FABTSGuideSubjects::FromItem(ItemId), nullptr, Quantity, GetQuantity(ItemId));
	return true;
}

bool UABTSInventoryComponent::RemoveItem(const EABTSItemId ItemId, const int32 Quantity)
{
	if (Quantity <= 0) return false;
	FABTSItemStack* Existing = FindStack(ItemId);
	if (Existing == nullptr || Existing->Quantity < Quantity) return false;
	Existing->Quantity -= Quantity;
	if (Existing->Quantity == 0)
	{
		if (bHasHeldItem && HeldItemId == ItemId) ClearHeldItem();
		OrderedStacks.RemoveAll([ItemId](const FABTSItemStack& Stack) { return Stack.ItemId == ItemId; });
		for (FABTSHotbarSlot& Slot : HotbarSlots)
		{
			if (Slot.bOccupied && Slot.ItemId == ItemId) Slot.bOccupied = false;
		}
	}
	OnInventoryChanged.Broadcast();
	return true;
}

bool UABTSInventoryComponent::SetHeldItem(const EABTSItemId ItemId)
{
	if (GetQuantity(ItemId) <= 0) return false;
	bHasHeldItem = true;
	HeldItemId = ItemId;
	OnInventoryChanged.Broadcast();
	FABTSGuideEventBus::Publish(this, FABTSGuideEventIds::HeldItemChanged,
		FABTSGuideSubjects::FromItem(ItemId));
	return true;
}

void UABTSInventoryComponent::ClearHeldItem()
{
	if (!bHasHeldItem) return;
	bHasHeldItem = false;
	OnInventoryChanged.Broadcast();
	FABTSGuideEventBus::Publish(this, FABTSGuideEventIds::HeldItemChanged);
}

bool UABTSInventoryComponent::GetHeldItem(EABTSItemId& OutItemId) const
{
	if (!bHasHeldItem || GetQuantity(HeldItemId) <= 0) return false;
	OutItemId = HeldItemId;
	return true;
}

bool UABTSInventoryComponent::GetHotbarItemAt(const int32 SlotIndex, EABTSItemId& OutItemId) const
{
	if (!HotbarSlots.IsValidIndex(SlotIndex) || !HotbarSlots[SlotIndex].bOccupied) return false;
	OutItemId = HotbarSlots[SlotIndex].ItemId;
	return true;
}

int32 UABTSInventoryComponent::GetQuantity(const EABTSItemId ItemId) const
{
	const FABTSItemStack* Existing = FindStack(ItemId);
	return Existing ? Existing->Quantity : 0;
}

void UABTSInventoryComponent::SeedMilestoneInventory(const TArray<FABTSItemStack>& StarterItems)
{
	if (bMilestoneSeeded) return;
	bMilestoneSeeded = true;
	for (const FABTSItemStack& Stack : StarterItems) AddItem(Stack.ItemId, Stack.Quantity);
}
