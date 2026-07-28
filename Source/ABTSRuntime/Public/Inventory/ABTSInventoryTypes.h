// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Slingshot/ABTSSlingshotTypes.h"
#include "ABTSInventoryTypes.generated.h"

UENUM(BlueprintType)
enum class EABTSItemId : uint8
{
	Branch UMETA(DisplayName = "树枝"),
	Stone UMETA(DisplayName = "石料"),
	Wood UMETA(DisplayName = "木材"),
	PlantFiber UMETA(DisplayName = "植物纤维"),
	MetalParts UMETA(DisplayName = "金属部件"),
	CrystalCore UMETA(DisplayName = "晶体核心"),
	WorkbenchKit UMETA(DisplayName = "工作台组件"),
	SimpleStake UMETA(DisplayName = "简易弹弓桩"),
	SimpleCord UMETA(DisplayName = "简易弹弓弦"),
	FurnaceKit UMETA(DisplayName = "熔炉组件"),
	ReinforcedStake UMETA(DisplayName = "强化弹弓桩"),
	ReinforcedCord UMETA(DisplayName = "强化弹弓弦"),
	/** Retained only so existing serialized enum values remain readable. No recipe or assembly path consumes it. */
	SpaceSlingshotPart UMETA(Hidden, DisplayName = "已弃用：太空弹弓部件"),
	Glass UMETA(DisplayName = "玻璃"),
	BridgeKit UMETA(DisplayName = "桥梁组件"),
	SpaceStake UMETA(DisplayName = "太空弹弓桩"),
	SpaceCord UMETA(DisplayName = "太空弹弓弦")
};

USTRUCT(BlueprintType)
struct FABTSItemStack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ABTS|M5|Inventory")
	EABTSItemId ItemId = EABTSItemId::Branch;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ABTS|M5|Inventory", meta = (ClampMin = "0"))
	int32 Quantity = 0;

	/** Stable acquisition order. Existing items never move when their quantity changes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M5|Inventory")
	int32 FirstAcquiredOrder = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct FABTSHotbarSlot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M5|Inventory")
	bool bOccupied = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M5|Inventory")
	EABTSItemId ItemId = EABTSItemId::Branch;
};

ABTSRUNTIME_API FText ABTSGetItemDisplayName(EABTSItemId ItemId);
ABTSRUNTIME_API FString ABTSGetItemFallbackLabel(EABTSItemId ItemId);
ABTSRUNTIME_API bool ABTSIsPlaceableTool(EABTSItemId ItemId);
ABTSRUNTIME_API bool ABTSIsBridgeKit(EABTSItemId ItemId);
ABTSRUNTIME_API bool ABTSIsSlingshotStake(EABTSItemId ItemId);
ABTSRUNTIME_API bool ABTSIsSlingshotCord(EABTSItemId ItemId);
/** Resolves the gameplay tier shared by inventory stake/cord parts. Returns false for non-slingshot items. */
ABTSRUNTIME_API bool ABTSTryResolveSlingshotPartTier(EABTSItemId ItemId, EABTSSlingshotTier& OutTier);
/** Stable assembly contract: one recognized stake and one recognized cord must resolve to the same tier. */
ABTSRUNTIME_API bool ABTSAreSlingshotPartsCompatible(
	EABTSItemId StakeItem,
	EABTSItemId CordItem,
	EABTSSlingshotTier& OutTier);
ABTSRUNTIME_API const TArray<EABTSItemId>& ABTSGetAllItemIds();
