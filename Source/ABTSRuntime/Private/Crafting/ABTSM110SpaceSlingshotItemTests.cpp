// Copyright Epic Games, Inc. All Rights Reserved.

#include "Crafting/ABTSCraftingCatalog.h"
#include "Inventory/ABTSInventoryTypes.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	int32 FindIngredientQuantity(const FABTSCraftingRecipe& Recipe, const EABTSItemId ItemId)
	{
		const FABTSCraftingIngredient* Ingredient = Recipe.Ingredients.FindByPredicate(
			[ItemId](const FABTSCraftingIngredient& Value)
			{
				return Value.ItemId == ItemId;
			});
		return Ingredient != nullptr ? Ingredient->Quantity : 0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM110SpaceSlingshotItemContractTest,
	"ABTS.M110.SpaceSlingshotItemContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM110SpaceSlingshotItemContractTest::RunTest(const FString& Parameters)
{
	EABTSSlingshotTier Tier = EABTSSlingshotTier::Simple;
	TestTrue(TEXT("Space stake is recognized as a slingshot stake"), ABTSIsSlingshotStake(EABTSItemId::SpaceStake));
	TestTrue(TEXT("Space cord is recognized as a slingshot cord"), ABTSIsSlingshotCord(EABTSItemId::SpaceCord));
	TestTrue(TEXT("Space stake and cord form a compatible pair"),
		ABTSAreSlingshotPartsCompatible(EABTSItemId::SpaceStake, EABTSItemId::SpaceCord, Tier));
	TestEqual(TEXT("The compatible pair resolves to the Space tier"),
		static_cast<uint8>(Tier), static_cast<uint8>(EABTSSlingshotTier::Space));
	TestFalse(TEXT("A reinforced cord cannot complete a Space slingshot"),
		ABTSAreSlingshotPartsCompatible(EABTSItemId::SpaceStake, EABTSItemId::ReinforcedCord, Tier));

	const TArray<EABTSItemId>& PublicItems = ABTSGetAllItemIds();
	TestTrue(TEXT("Space stake is available to inventory/debug enumeration"), PublicItems.Contains(EABTSItemId::SpaceStake));
	TestTrue(TEXT("Space cord is available to inventory/debug enumeration"), PublicItems.Contains(EABTSItemId::SpaceCord));
	TestFalse(TEXT("Retired aggregate part is not available to new inventory/debug enumeration"),
		PublicItems.Contains(EABTSItemId::SpaceSlingshotPart));

	const UABTSCraftingCatalog* Catalog = NewObject<UABTSCraftingCatalog>();
	TestNotNull(TEXT("Crafting catalog can be constructed"), Catalog);
	if (Catalog == nullptr) return false;
	TestNull(TEXT("Retired aggregate recipe is unavailable"), Catalog->FindRecipe(FName(TEXT("SpaceSlingshotPart"))));

	const FABTSCraftingRecipe* ReinforcedStakeRecipe =
		Catalog->FindRecipe(FName(TEXT("ReinforcedStake")));
	TestNotNull(TEXT("Reinforced stake recipe exists"), ReinforcedStakeRecipe);
	if (ReinforcedStakeRecipe != nullptr)
	{
		TestEqual(TEXT("Reinforced stake metal cost"),
			FindIngredientQuantity(*ReinforcedStakeRecipe,
				EABTSItemId::MetalParts), 1);
	}
	const FABTSCraftingRecipe* ReinforcedCordRecipe =
		Catalog->FindRecipe(FName(TEXT("ReinforcedCord")));
	TestNotNull(TEXT("Reinforced cord recipe exists"), ReinforcedCordRecipe);
	if (ReinforcedCordRecipe != nullptr)
	{
		TestEqual(TEXT("Reinforced cord metal cost"),
			FindIngredientQuantity(*ReinforcedCordRecipe,
				EABTSItemId::MetalParts), 1);
	}

	const FABTSCraftingRecipe* StakeRecipe = Catalog->FindRecipe(FName(TEXT("SpaceStakePair")));
	TestNotNull(TEXT("Space stake pair recipe exists"), StakeRecipe);
	if (StakeRecipe != nullptr)
	{
		TestEqual(TEXT("Space stake pair recipe output"), static_cast<uint8>(StakeRecipe->OutputItem),
			static_cast<uint8>(EABTSItemId::SpaceStake));
		TestEqual(TEXT("Space stake pair recipe output count"), StakeRecipe->OutputQuantity, 2);
		TestEqual(TEXT("Space stake pair recipe station"), static_cast<uint8>(StakeRecipe->RequiredStation),
			static_cast<uint8>(EABTSCraftingStationType::Furnace));
		TestEqual(TEXT("Space stake pair metal cost"), FindIngredientQuantity(*StakeRecipe, EABTSItemId::MetalParts), 6);
		TestEqual(TEXT("Space stake pair wood cost"), FindIngredientQuantity(*StakeRecipe, EABTSItemId::Wood), 5);
	}

	const FABTSCraftingRecipe* CordRecipe = Catalog->FindRecipe(FName(TEXT("SpaceCord")));
	TestNotNull(TEXT("Space cord recipe exists"), CordRecipe);
	if (CordRecipe != nullptr)
	{
		TestEqual(TEXT("Space cord recipe output"), static_cast<uint8>(CordRecipe->OutputItem),
			static_cast<uint8>(EABTSItemId::SpaceCord));
		TestEqual(TEXT("Space cord recipe output count"), CordRecipe->OutputQuantity, 1);
		TestEqual(TEXT("Space cord recipe station"), static_cast<uint8>(CordRecipe->RequiredStation),
			static_cast<uint8>(EABTSCraftingStationType::Furnace));
		TestEqual(TEXT("Space cord metal cost"), FindIngredientQuantity(*CordRecipe, EABTSItemId::MetalParts), 2);
		TestEqual(TEXT("Space cord crystal-core cost"), FindIngredientQuantity(*CordRecipe, EABTSItemId::CrystalCore), 1);
	}
	return true;
}

#endif
