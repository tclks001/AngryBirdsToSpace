// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Inventory/ABTSInventoryTypes.h"
#include "ABTSCraftingTypes.generated.h"

UENUM(BlueprintType)
enum class EABTSCraftingStationType : uint8
{
	None UMETA(DisplayName = "徒手"),
	Workbench UMETA(DisplayName = "工作台"),
	Furnace UMETA(DisplayName = "熔炉")
};

USTRUCT(BlueprintType)
struct FABTSCraftingIngredient
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5|Crafting")
	EABTSItemId ItemId = EABTSItemId::Branch;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5|Crafting", meta = (ClampMin = "1"))
	int32 Quantity = 1;
};

USTRUCT(BlueprintType)
struct FABTSCraftingRecipe
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5|Crafting")
	FName RecipeId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5|Crafting")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5|Crafting")
	EABTSItemId OutputItem = EABTSItemId::SimpleStake;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5|Crafting", meta = (ClampMin = "1"))
	int32 OutputQuantity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5|Crafting")
	TArray<FABTSCraftingIngredient> Ingredients;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5|Crafting")
	EABTSCraftingStationType RequiredStation = EABTSCraftingStationType::None;
};

USTRUCT()
struct FABTSCraftingEvaluation
{
	GENERATED_BODY()

	const FABTSCraftingRecipe* Recipe = nullptr;
	TArray<FABTSCraftingIngredient> MissingIngredients;
	int32 MaxCraftCount = 0;
	bool bRedBirdControlled = false;
	bool bStationAvailable = false;
	bool bHasMaterials = false;
	bool IsCraftable() const { return bRedBirdControlled && bStationAvailable && bHasMaterials && MaxCraftCount > 0; }
};

ABTSRUNTIME_API FText ABTSGetCraftingStationDisplayName(EABTSCraftingStationType StationType);
ABTSRUNTIME_API FString ABTSGetCraftingStationFallbackLabel(EABTSCraftingStationType StationType);
