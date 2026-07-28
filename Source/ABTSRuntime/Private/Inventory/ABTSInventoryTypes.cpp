// Copyright Epic Games, Inc. All Rights Reserved.

#include "Inventory/ABTSInventoryTypes.h"

FText ABTSGetItemDisplayName(const EABTSItemId ItemId)
{
	switch (ItemId)
	{
	case EABTSItemId::Branch: return FText::FromString(TEXT("树枝"));
	case EABTSItemId::Stone: return FText::FromString(TEXT("石料"));
	case EABTSItemId::Wood: return FText::FromString(TEXT("木材"));
	case EABTSItemId::PlantFiber: return FText::FromString(TEXT("植物纤维"));
	case EABTSItemId::MetalParts: return FText::FromString(TEXT("金属部件"));
	case EABTSItemId::CrystalCore: return FText::FromString(TEXT("晶体核心"));
	case EABTSItemId::WorkbenchKit: return FText::FromString(TEXT("工作台组件"));
	case EABTSItemId::SimpleStake: return FText::FromString(TEXT("简易弹弓桩"));
	case EABTSItemId::SimpleCord: return FText::FromString(TEXT("简易弹弓弦"));
	case EABTSItemId::FurnaceKit: return FText::FromString(TEXT("熔炉组件"));
	case EABTSItemId::ReinforcedStake: return FText::FromString(TEXT("强化弹弓桩"));
	case EABTSItemId::ReinforcedCord: return FText::FromString(TEXT("强化弹弓弦"));
	case EABTSItemId::SpaceSlingshotPart: return FText::FromString(TEXT("太空弹弓部件"));
	case EABTSItemId::Glass: return FText::FromString(TEXT("玻璃"));
	case EABTSItemId::BridgeKit: return FText::FromString(TEXT("桥梁组件"));
	case EABTSItemId::SpaceStake: return FText::FromString(TEXT("太空弹弓桩"));
	case EABTSItemId::SpaceCord: return FText::FromString(TEXT("太空弹弓弦"));
	default: return FText::FromString(TEXT("未知物品"));
	}
}

FString ABTSGetItemFallbackLabel(const EABTSItemId ItemId)
{
	switch (ItemId)
	{
	case EABTSItemId::Branch: return TEXT("Branch");
	case EABTSItemId::Stone: return TEXT("Stone");
	case EABTSItemId::Wood: return TEXT("Wood");
	case EABTSItemId::PlantFiber: return TEXT("Plant Fiber");
	case EABTSItemId::MetalParts: return TEXT("Metal Parts");
	case EABTSItemId::CrystalCore: return TEXT("Crystal Core");
	case EABTSItemId::WorkbenchKit: return TEXT("Workbench Kit");
	case EABTSItemId::SimpleStake: return TEXT("Simple Stake");
	case EABTSItemId::SimpleCord: return TEXT("Simple Cord");
	case EABTSItemId::FurnaceKit: return TEXT("Furnace Kit");
	case EABTSItemId::ReinforcedStake: return TEXT("Reinforced Stake");
	case EABTSItemId::ReinforcedCord: return TEXT("Reinforced Cord");
	case EABTSItemId::SpaceSlingshotPart: return TEXT("Space Sling Part");
	case EABTSItemId::Glass: return TEXT("Glass");
	case EABTSItemId::BridgeKit: return TEXT("Bridge Kit");
	case EABTSItemId::SpaceStake: return TEXT("Space Stake");
	case EABTSItemId::SpaceCord: return TEXT("Space Cord");
	default: return TEXT("Unknown Item");
	}
}

bool ABTSIsPlaceableTool(const EABTSItemId ItemId)
{
	return ItemId == EABTSItemId::WorkbenchKit || ItemId == EABTSItemId::FurnaceKit;
}

bool ABTSIsBridgeKit(const EABTSItemId ItemId)
{
	return ItemId == EABTSItemId::BridgeKit;
}

bool ABTSIsSlingshotStake(const EABTSItemId ItemId)
{
	return ItemId == EABTSItemId::Branch
		|| ItemId == EABTSItemId::SimpleStake
		|| ItemId == EABTSItemId::ReinforcedStake
		|| ItemId == EABTSItemId::SpaceStake;
}

bool ABTSIsSlingshotCord(const EABTSItemId ItemId)
{
	return ItemId == EABTSItemId::PlantFiber
		|| ItemId == EABTSItemId::SimpleCord
		|| ItemId == EABTSItemId::ReinforcedCord
		|| ItemId == EABTSItemId::SpaceCord;
}

bool ABTSTryResolveSlingshotPartTier(const EABTSItemId ItemId, EABTSSlingshotTier& OutTier)
{
	switch (ItemId)
	{
	case EABTSItemId::Branch:
	case EABTSItemId::PlantFiber:
		OutTier = EABTSSlingshotTier::Twig;
		return true;
	case EABTSItemId::SimpleStake:
	case EABTSItemId::SimpleCord:
		OutTier = EABTSSlingshotTier::Simple;
		return true;
	case EABTSItemId::ReinforcedStake:
	case EABTSItemId::ReinforcedCord:
		OutTier = EABTSSlingshotTier::Reinforced;
		return true;
	case EABTSItemId::SpaceStake:
	case EABTSItemId::SpaceCord:
		OutTier = EABTSSlingshotTier::Space;
		return true;
	default:
		return false;
	}
}

bool ABTSAreSlingshotPartsCompatible(
	const EABTSItemId StakeItem,
	const EABTSItemId CordItem,
	EABTSSlingshotTier& OutTier)
{
	if (!ABTSIsSlingshotStake(StakeItem) || !ABTSIsSlingshotCord(CordItem)) return false;
	EABTSSlingshotTier StakeTier = EABTSSlingshotTier::Simple;
	EABTSSlingshotTier CordTier = EABTSSlingshotTier::Simple;
	if (!ABTSTryResolveSlingshotPartTier(StakeItem, StakeTier)
		|| !ABTSTryResolveSlingshotPartTier(CordItem, CordTier)
		|| StakeTier != CordTier)
	{
		return false;
	}
	OutTier = StakeTier;
	return true;
}

const TArray<EABTSItemId>& ABTSGetAllItemIds()
{
	static const TArray<EABTSItemId> Items = {
		EABTSItemId::Branch,
		EABTSItemId::Stone,
		EABTSItemId::Wood,
		EABTSItemId::PlantFiber,
		EABTSItemId::MetalParts,
		EABTSItemId::CrystalCore,
		EABTSItemId::WorkbenchKit,
		EABTSItemId::SimpleStake,
		EABTSItemId::SimpleCord,
		EABTSItemId::FurnaceKit,
		EABTSItemId::ReinforcedStake,
		EABTSItemId::ReinforcedCord,
		EABTSItemId::Glass,
		EABTSItemId::BridgeKit,
		EABTSItemId::SpaceStake,
		EABTSItemId::SpaceCord
	};
	return Items;
}
