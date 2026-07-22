// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Crafting/ABTSCraftingTypes.h"
#include "ABTSCraftingCatalog.generated.h"

class UABTSInventoryComponent;

/** Data-driven recipe catalog and inventory transaction authority. */
UCLASS(ClassGroup = (ABTS), meta = (BlueprintSpawnableComponent))
class ABTSRUNTIME_API UABTSCraftingCatalog : public UActorComponent
{
	GENERATED_BODY()

public:
	UABTSCraftingCatalog();

	const TArray<FABTSCraftingRecipe>& GetRecipes() const { return Recipes; }
	const FABTSCraftingRecipe* FindRecipe(FName RecipeId) const;
	FABTSCraftingEvaluation Evaluate(
		const FABTSCraftingRecipe& Recipe,
		const UABTSInventoryComponent& Inventory,
		bool bRedBirdControlled,
		bool bWorkbenchAvailable,
		bool bFurnaceAvailable) const;
	bool Craft(
		FName RecipeId,
		int32 CraftCount,
		UABTSInventoryComponent& Inventory,
		bool bRedBirdControlled,
		bool bWorkbenchAvailable,
		bool bFurnaceAvailable);

private:
	void BuildDefaultRecipes();

	UPROPERTY(EditAnywhere, Category = "ABTS|M5|Crafting")
	TArray<FABTSCraftingRecipe> Recipes;
};

