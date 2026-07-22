// Copyright Epic Games, Inc. All Rights Reserved.

#include "Crafting/ABTSCraftingCatalog.h"

#include "Inventory/ABTSInventoryComponent.h"

namespace
{
	FABTSCraftingIngredient Ingredient(const EABTSItemId ItemId, const int32 Quantity)
	{
		FABTSCraftingIngredient Result;
		Result.ItemId = ItemId;
		Result.Quantity = Quantity;
		return Result;
	}

	FABTSCraftingRecipe Recipe(
		const TCHAR* Id,
		const TCHAR* Name,
		const EABTSItemId Output,
		const int32 OutputQuantity,
		const EABTSCraftingStationType Station,
		std::initializer_list<FABTSCraftingIngredient> Ingredients)
	{
		FABTSCraftingRecipe Result;
		Result.RecipeId = FName(Id);
		Result.DisplayName = FText::FromString(Name);
		Result.OutputItem = Output;
		Result.OutputQuantity = OutputQuantity;
		Result.RequiredStation = Station;
		for (const FABTSCraftingIngredient& Value : Ingredients) Result.Ingredients.Add(Value);
		return Result;
	}
}

UABTSCraftingCatalog::UABTSCraftingCatalog()
{
	PrimaryComponentTick.bCanEverTick = false;
	BuildDefaultRecipes();
}

void UABTSCraftingCatalog::BuildDefaultRecipes()
{
	if (!Recipes.IsEmpty()) return;
	Recipes = {
		Recipe(TEXT("WorkbenchKit"), TEXT("Workbench Kit"), EABTSItemId::WorkbenchKit, 1, EABTSCraftingStationType::None,
			{ Ingredient(EABTSItemId::Branch, 4), Ingredient(EABTSItemId::Stone, 3) }),
		Recipe(TEXT("SimpleStake"), TEXT("Simple Slingshot Stake"), EABTSItemId::SimpleStake, 1, EABTSCraftingStationType::Workbench,
			{ Ingredient(EABTSItemId::Branch, 3), Ingredient(EABTSItemId::Stone, 2) }),
		Recipe(TEXT("SimpleCord"), TEXT("Simple Slingshot Cord"), EABTSItemId::SimpleCord, 1, EABTSCraftingStationType::Workbench,
			{ Ingredient(EABTSItemId::Branch, 2), Ingredient(EABTSItemId::PlantFiber, 3) }),
		Recipe(TEXT("FurnaceKit"), TEXT("Furnace Kit"), EABTSItemId::FurnaceKit, 1, EABTSCraftingStationType::Workbench,
			{ Ingredient(EABTSItemId::Stone, 8), Ingredient(EABTSItemId::Wood, 4) }),
		Recipe(TEXT("ReinforcedStake"), TEXT("Reinforced Stake"), EABTSItemId::ReinforcedStake, 1, EABTSCraftingStationType::Furnace,
			{ Ingredient(EABTSItemId::MetalParts, 4), Ingredient(EABTSItemId::Stone, 3) }),
		Recipe(TEXT("ReinforcedCord"), TEXT("Reinforced Cord"), EABTSItemId::ReinforcedCord, 1, EABTSCraftingStationType::Furnace,
			{ Ingredient(EABTSItemId::MetalParts, 2), Ingredient(EABTSItemId::PlantFiber, 4) }),
		Recipe(TEXT("SpaceSlingshotPart"), TEXT("Space Slingshot Part"), EABTSItemId::SpaceSlingshotPart, 1, EABTSCraftingStationType::Furnace,
			{ Ingredient(EABTSItemId::MetalParts, 8), Ingredient(EABTSItemId::CrystalCore, 1), Ingredient(EABTSItemId::Wood, 5) })
	};
}

const FABTSCraftingRecipe* UABTSCraftingCatalog::FindRecipe(const FName RecipeId) const
{
	return Recipes.FindByPredicate([RecipeId](const FABTSCraftingRecipe& Value) { return Value.RecipeId == RecipeId; });
}

FABTSCraftingEvaluation UABTSCraftingCatalog::Evaluate(
	const FABTSCraftingRecipe& Recipe,
	const UABTSInventoryComponent& Inventory,
	const bool bRedBirdControlled,
	const bool bWorkbenchAvailable,
	const bool bFurnaceAvailable) const
{
	FABTSCraftingEvaluation Result;
	Result.Recipe = &Recipe;
	Result.bRedBirdControlled = bRedBirdControlled;
	Result.bStationAvailable = Recipe.RequiredStation == EABTSCraftingStationType::None
		|| (Recipe.RequiredStation == EABTSCraftingStationType::Workbench && bWorkbenchAvailable)
		|| (Recipe.RequiredStation == EABTSCraftingStationType::Furnace && bFurnaceAvailable);
	Result.MaxCraftCount = TNumericLimits<int32>::Max();
	for (const FABTSCraftingIngredient& Required : Recipe.Ingredients)
	{
		const int32 Owned = Inventory.GetQuantity(Required.ItemId);
		Result.MaxCraftCount = FMath::Min(Result.MaxCraftCount, Owned / FMath::Max(1, Required.Quantity));
		if (Owned < Required.Quantity) Result.MissingIngredients.Add(Required);
	}
	if (Recipe.Ingredients.IsEmpty()) Result.MaxCraftCount = 0;
	Result.bHasMaterials = Result.MissingIngredients.IsEmpty() && Result.MaxCraftCount > 0;
	return Result;
}

bool UABTSCraftingCatalog::Craft(
	const FName RecipeId,
	const int32 CraftCount,
	UABTSInventoryComponent& Inventory,
	const bool bRedBirdControlled,
	const bool bWorkbenchAvailable,
	const bool bFurnaceAvailable)
{
	const FABTSCraftingRecipe* ResolvedRecipe = FindRecipe(RecipeId);
	if (ResolvedRecipe == nullptr || CraftCount <= 0) return false;
	const FABTSCraftingEvaluation Evaluation = Evaluate(
		*ResolvedRecipe, Inventory, bRedBirdControlled, bWorkbenchAvailable, bFurnaceAvailable);
	if (!Evaluation.IsCraftable() || CraftCount > Evaluation.MaxCraftCount) return false;
	for (const FABTSCraftingIngredient& Required : ResolvedRecipe->Ingredients)
	{
		if (!Inventory.RemoveItem(Required.ItemId, Required.Quantity * CraftCount)) return false;
	}
	Inventory.AddItem(ResolvedRecipe->OutputItem, ResolvedRecipe->OutputQuantity * CraftCount);
	return true;
}
