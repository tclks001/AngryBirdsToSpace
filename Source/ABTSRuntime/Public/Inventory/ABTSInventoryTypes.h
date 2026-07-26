// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
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
	SpaceSlingshotPart UMETA(DisplayName = "太空弹弓部件"),
	Glass UMETA(DisplayName = "玻璃"),
	BridgeKit UMETA(DisplayName = "桥梁组件")
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
ABTSRUNTIME_API const TArray<EABTSItemId>& ABTSGetAllItemIds();
