// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSM5InventoryHUD.h"

#include "Crafting/ABTSCraftingCatalog.h"
#include "Crafting/ABTSCraftingSystem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Inventory/ABTSInventoryComponent.h"
#include "Player/ABTSM5PlayerController.h"

namespace
{
	const FName InventoryHitBox(TEXT("ABTS_M5_Inventory"));
	const FName HeldItemHitBox(TEXT("ABTS_M51_HeldItem"));
	const FName CloseHitBox(TEXT("ABTS_M5_Close"));
	const FName QuantityMinusTen(TEXT("ABTS_M5_QtyMinus10"));
	const FName QuantityMinusOne(TEXT("ABTS_M5_QtyMinus1"));
	const FName QuantityPlusOne(TEXT("ABTS_M5_QtyPlus1"));
	const FName QuantityPlusTen(TEXT("ABTS_M5_QtyPlus10"));
	const FName QuantityCraft(TEXT("ABTS_M5_QtyCraft"));
	const FName QuantityCancel(TEXT("ABTS_M5_QtyCancel"));
	constexpr float HotbarSlotSize = 78.0f;
	constexpr float RecipeRowHeight = 66.0f;

	int32 EvaluationSortRank(const FABTSCraftingEvaluation& Value)
	{
		// Red-bird ownership is a global crafting permission and must not destroy
		// the recipe list's useful material/station ordering while another bird is controlled.
		if (!Value.bStationAvailable) return 2;
		if (!Value.bHasMaterials) return 1;
		return 0;
	}
}

void AABTSM5InventoryHUD::DrawHUD()
{
	Super::DrawHUD();
	AABTSCraftingSystem* System = FindCraftingSystem();
	if (Canvas == nullptr || System == nullptr) return;
	DrawHotbar(*System);
	if (const AABTSM5PlayerController* Controller = GetM5Controller(); Controller && Controller->IsCraftingInterfaceOpen())
	{
		DrawCraftingInterface(*System);
	}
}

void AABTSM5InventoryHUD::DrawPanel(const FVector2D& Origin, const FVector2D& Size, const FLinearColor& Color) const
{
	Canvas->K2_DrawTexture(Canvas->DefaultTexture, Origin, Size, FVector2D::ZeroVector, FVector2D::UnitVector, Color);
}

void AABTSM5InventoryHUD::DrawCell(const FVector2D& Origin, const FVector2D& Size, const FLinearColor& Color) const
{
	Canvas->K2_DrawTexture(Canvas->DefaultTexture, Origin, Size, FVector2D::ZeroVector, FVector2D::UnitVector,
		FLinearColor(0.035f, 0.035f, 0.045f, 0.96f));
	Canvas->K2_DrawTexture(Canvas->DefaultTexture, Origin + FVector2D(3.0f), Size - FVector2D(6.0f),
		FVector2D::ZeroVector, FVector2D::UnitVector, Color);
}

FName AABTSM5InventoryHUD::MakeHotbarName(const int32 Slot) const
{
	return FName(*FString::Printf(TEXT("ABTS_M5_Hotbar_%d"), Slot));
}

FName AABTSM5InventoryHUD::MakeRecipeName(const int32 RecipeIndex) const
{
	return FName(*FString::Printf(TEXT("ABTS_M5_Recipe_%d"), RecipeIndex));
}

void AABTSM5InventoryHUD::DrawHotbar(AABTSCraftingSystem& System)
{
	const UABTSInventoryComponent* Inventory = System.GetInventory();
	if (Inventory == nullptr || GEngine == nullptr) return;
	const int32 SlotCount = Inventory->GetHotbarSlotCount();
	const float SlotStripWidth = SlotCount * HotbarSlotSize;
	const float BagWidth = 76.0f;
	const float HeldGap = 18.0f;
	const float TotalWidth = BagWidth + 8.0f + SlotStripWidth + HeldGap + HotbarSlotSize;
	const FVector2D BarOrigin((Canvas->ClipX - TotalWidth) * 0.5f, Canvas->ClipY - HotbarSlotSize - 20.0f);
	const FVector2D BagOrigin = BarOrigin;
	const FVector2D Origin = BagOrigin + FVector2D(BagWidth + 8.0f, 0.0f);
	DrawCell(BagOrigin, FVector2D(BagWidth, HotbarSlotSize), FLinearColor(0.12f, 0.16f, 0.25f, 0.96f));
	DrawText(TEXT("BAG"), FLinearColor::White, BagOrigin.X + 18.0f, BagOrigin.Y + 26.0f,
		GEngine->GetSmallFont(), 1.0f, false);
	AddHitBox(BagOrigin, FVector2D(BagWidth, HotbarSlotSize), InventoryHitBox, true, 25);
	for (int32 Slot = 0; Slot < SlotCount; ++Slot)
	{
		const FVector2D CellOrigin = Origin + FVector2D(Slot * HotbarSlotSize, 0.0f);
		DrawCell(CellOrigin, FVector2D(HotbarSlotSize), FLinearColor(0.16f, 0.17f, 0.20f, 0.96f));
		EABTSItemId ItemId;
		if (Inventory->GetHotbarItemAt(Slot, ItemId))
		{
			DrawText(ABTSGetItemFallbackLabel(ItemId), FLinearColor::White,
				CellOrigin.X + 7.0f, CellOrigin.Y + 12.0f, GEngine->GetSmallFont(), 0.82f, false);
			DrawText(FString::FromInt(Inventory->GetQuantity(ItemId)), FLinearColor(1.0f, 0.9f, 0.32f),
				CellOrigin.X + 55.0f, CellOrigin.Y + 52.0f, GEngine->GetSmallFont(), 0.9f, false);
		}
		AddHitBox(CellOrigin, FVector2D(HotbarSlotSize), MakeHotbarName(Slot), true, 20);
	}
	const FVector2D HeldOrigin = Origin + FVector2D(SlotStripWidth + HeldGap, 0.0f);
	DrawCell(HeldOrigin, FVector2D(HotbarSlotSize), FLinearColor(0.28f, 0.19f, 0.08f, 0.98f));
	EABTSItemId HeldItemId;
	if (Inventory->GetHeldItem(HeldItemId))
	{
		DrawText(ABTSGetItemFallbackLabel(HeldItemId), FLinearColor::White,
			HeldOrigin.X + 7.0f, HeldOrigin.Y + 12.0f, GEngine->GetSmallFont(), 0.78f, false);
		DrawText(FString::FromInt(Inventory->GetQuantity(HeldItemId)), FLinearColor(1.0f, 0.9f, 0.32f),
			HeldOrigin.X + 55.0f, HeldOrigin.Y + 52.0f, GEngine->GetSmallFont(), 0.9f, false);
	}
	else
	{
		DrawText(TEXT("HELD"), FLinearColor(0.7f, 0.7f, 0.74f), HeldOrigin.X + 18.0f, HeldOrigin.Y + 27.0f,
			GEngine->GetSmallFont(), 0.86f, false);
	}
	AddHitBox(HeldOrigin, FVector2D(HotbarSlotSize), HeldItemHitBox, true, 25);
	DrawText(TEXT("K/BAG: Crafting | Click item: Hold | Click HELD: Clear"), FLinearColor::White,
		BarOrigin.X, BarOrigin.Y - 22.0f, GEngine->GetSmallFont(), 0.82f, false);
}

void AABTSM5InventoryHUD::DrawCraftingInterface(AABTSCraftingSystem& System)
{
	const FVector2D Origin(58.0f, 44.0f);
	const FVector2D Size(Canvas->ClipX - 116.0f, Canvas->ClipY - 104.0f);
	DrawPanel(Origin, Size, FLinearColor(0.045f, 0.05f, 0.065f, 0.985f));
	DrawText(TEXT("PARTY BACKPACK"), FLinearColor::White, Origin.X + 22.0f, Origin.Y + 14.0f, GEngine->GetMediumFont(), 1.0f, false);
	DrawText(System.IsRedBirdControlled() ? TEXT("Red bird: crafting enabled") : TEXT("RED BIRD REQUIRED TO CRAFT"),
		System.IsRedBirdControlled() ? FLinearColor(0.4f, 1.0f, 0.52f) : FLinearColor(1.0f, 0.18f, 0.14f),
		Origin.X + Size.X * 0.52f, Origin.Y + 18.0f, GEngine->GetSmallFont(), 0.95f, false);
	const FVector2D CloseOrigin(Origin.X + Size.X - 48.0f, Origin.Y + 10.0f);
	DrawCell(CloseOrigin, FVector2D(34.0f), FLinearColor(0.55f, 0.12f, 0.12f, 1.0f));
	DrawText(TEXT("X"), FLinearColor::White, CloseOrigin.X + 10.0f, CloseOrigin.Y + 5.0f, GEngine->GetSmallFont(), 1.0f, false);
	AddHitBox(CloseOrigin, FVector2D(34.0f), CloseHitBox, true, 100);

	const float ContentTop = Origin.Y + 58.0f;
	const float ContentHeight = Size.Y - 76.0f;
	const float LeftWidth = Size.X * 0.38f;
	DrawInventoryPanel(*System.GetInventory(), FVector2D(Origin.X + 18.0f, ContentTop), FVector2D(LeftWidth - 26.0f, ContentHeight));
	DrawRecipePanel(System, FVector2D(Origin.X + LeftWidth, ContentTop), FVector2D(Size.X - LeftWidth - 18.0f, ContentHeight));
	if (!SelectedRecipeId.IsNone()) DrawQuantityModal(System);
	else DrawTooltip(System);
}

void AABTSM5InventoryHUD::DrawInventoryPanel(
	const UABTSInventoryComponent& Inventory,
	const FVector2D& Origin,
	const FVector2D& Size)
{
	DrawPanel(Origin, Size, FLinearColor(0.08f, 0.085f, 0.105f, 0.98f));
	DrawText(TEXT("Owned items"), FLinearColor(0.92f, 0.93f, 1.0f), Origin.X + 12.0f, Origin.Y + 10.0f,
		GEngine->GetSmallFont(), 1.0f, false);
	float Y = Origin.Y + 40.0f;
	for (const FABTSItemStack& Stack : Inventory.GetOrderedStacks())
	{
		if (Stack.Quantity <= 0) continue;
		DrawCell(FVector2D(Origin.X + 10.0f, Y), FVector2D(Size.X - 20.0f, 42.0f), FLinearColor(0.13f, 0.14f, 0.17f, 1.0f));
		DrawText(ABTSGetItemFallbackLabel(Stack.ItemId), FLinearColor::White,
			Origin.X + 20.0f, Y + 9.0f, GEngine->GetSmallFont(), 0.92f, false);
		DrawText(FString::Printf(TEXT("x%d"), Stack.Quantity), FLinearColor(1.0f, 0.9f, 0.32f),
			Origin.X + Size.X - 68.0f, Y + 9.0f, GEngine->GetSmallFont(), 0.92f, false);
		Y += 46.0f;
		if (Y + 42.0f > Origin.Y + Size.Y) break;
	}
}

void AABTSM5InventoryHUD::DrawRecipePanel(
	AABTSCraftingSystem& System,
	const FVector2D& Origin,
	const FVector2D& Size)
{
	DrawPanel(Origin, Size, FLinearColor(0.075f, 0.08f, 0.095f, 0.98f));
	DrawText(TEXT("RECIPES: craftable / missing materials / missing station"), FLinearColor::White,
		Origin.X + 12.0f, Origin.Y + 10.0f, GEngine->GetSmallFont(), 0.88f, false);
	UABTSCraftingCatalog* Catalog = System.GetCatalog();
	UABTSInventoryComponent* Inventory = System.GetInventory();
	if (Catalog == nullptr || Inventory == nullptr) return;
	TArray<FABTSCraftingEvaluation> Evaluations;
	for (const FABTSCraftingRecipe& Recipe : Catalog->GetRecipes())
	{
		Evaluations.Add(Catalog->Evaluate(Recipe, *Inventory, System.IsRedBirdControlled(),
			System.IsStationAvailable(EABTSCraftingStationType::Workbench),
			System.IsStationAvailable(EABTSCraftingStationType::Furnace)));
	}
	Evaluations.StableSort([](const FABTSCraftingEvaluation& A, const FABTSCraftingEvaluation& B)
	{
		return EvaluationSortRank(A) < EvaluationSortRank(B);
	});
	VisibleRecipeIds.Reset();
	float Y = Origin.Y + 38.0f;
	const double Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	for (int32 Index = 0; Index < Evaluations.Num(); ++Index)
	{
		const FABTSCraftingEvaluation& Evaluation = Evaluations[Index];
		const FABTSCraftingRecipe& Recipe = *Evaluation.Recipe;
		VisibleRecipeIds.Add(Recipe.RecipeId);
		const bool bFlash = Recipe.RecipeId == InvalidHighlightRecipeId && Now < InvalidHighlightUntilSeconds;
		const FLinearColor RowColor = Evaluation.IsCraftable()
			? FLinearColor(0.11f, 0.30f, 0.16f, 1.0f)
			: (bFlash ? FLinearColor(0.68f, 0.03f, 0.03f, 1.0f) : FLinearColor(0.22f, 0.12f, 0.13f, 1.0f));
		DrawCell(FVector2D(Origin.X + 8.0f, Y), FVector2D(Size.X - 16.0f, RecipeRowHeight - 4.0f), RowColor);
		DrawText(Recipe.DisplayName.ToString(), FLinearColor::White, Origin.X + 18.0f, Y + 7.0f,
			GEngine->GetSmallFont(), 0.94f, false);
		float MaterialX = Origin.X + 18.0f;
		for (const FABTSCraftingIngredient& Required : Recipe.Ingredients)
		{
			const int32 Owned = Inventory->GetQuantity(Required.ItemId);
			const FLinearColor CountColor = Owned < Required.Quantity ? FLinearColor(1.0f, 0.18f, 0.14f) : FLinearColor(0.78f, 0.92f, 0.82f);
			DrawText(FString::Printf(TEXT("%s %d/%d"), *ABTSGetItemFallbackLabel(Required.ItemId), Owned, Required.Quantity),
				CountColor, MaterialX, Y + 34.0f, GEngine->GetSmallFont(), 0.76f, false);
			MaterialX += 118.0f;
		}
		TArray<FString> RequirementLabels;
		if (!Evaluation.bRedBirdControlled) RequirementLabels.Add(TEXT("RED BIRD"));
		if (!Evaluation.bStationAvailable)
		{
			RequirementLabels.Add(FString::Printf(TEXT("[%s]"), *ABTSGetCraftingStationFallbackLabel(Recipe.RequiredStation)));
		}
		if (!RequirementLabels.IsEmpty())
		{
			DrawText(FString::Printf(TEXT("Need: %s"), *FString::Join(RequirementLabels, TEXT(" + "))),
				FLinearColor(1.0f, 0.12f, 0.1f), Origin.X + Size.X - 190.0f, Y + 7.0f,
				GEngine->GetSmallFont(), 0.72f, false);
		}
		AddHitBox(FVector2D(Origin.X + 8.0f, Y), FVector2D(Size.X - 16.0f, RecipeRowHeight - 4.0f), MakeRecipeName(Index), true, 50);
		Y += RecipeRowHeight;
		if (Y + RecipeRowHeight > Origin.Y + Size.Y) break;
	}
}

void AABTSM5InventoryHUD::DrawQuantityModal(AABTSCraftingSystem& System)
{
	const FABTSCraftingRecipe* Recipe = System.GetCatalog()->FindRecipe(SelectedRecipeId);
	if (Recipe == nullptr) { ResetCraftingSelection(); return; }
	const FABTSCraftingEvaluation Evaluation = System.GetCatalog()->Evaluate(*Recipe, *System.GetInventory(),
		System.IsRedBirdControlled(), System.IsStationAvailable(EABTSCraftingStationType::Workbench),
		System.IsStationAvailable(EABTSCraftingStationType::Furnace));
	PendingCraftCount = FMath::Clamp(PendingCraftCount, 0, Evaluation.MaxCraftCount);
	const FVector2D Size(460.0f, 210.0f);
	const FVector2D Origin((Canvas->ClipX - Size.X) * 0.5f, (Canvas->ClipY - Size.Y) * 0.5f);
	DrawPanel(Origin, Size, FLinearColor(0.025f, 0.028f, 0.038f, 1.0f));
	DrawText(FString::Printf(TEXT("Craft %s"), *Recipe->DisplayName.ToString()), FLinearColor::White,
		Origin.X + 24.0f, Origin.Y + 20.0f, GEngine->GetMediumFont(), 1.0f, false);
	DrawText(FString::Printf(TEXT("Quantity: %d / max %d"), PendingCraftCount, Evaluation.MaxCraftCount), FLinearColor(1.0f, 0.9f, 0.3f),
		Origin.X + 150.0f, Origin.Y + 72.0f, GEngine->GetSmallFont(), 1.0f, false);
	const TCHAR* Labels[] = { TEXT("--"), TEXT("-"), TEXT("+"), TEXT("++") };
	const FName Names[] = { QuantityMinusTen, QuantityMinusOne, QuantityPlusOne, QuantityPlusTen };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVector2D ButtonOrigin(Origin.X + 46.0f + Index * 92.0f, Origin.Y + 108.0f);
		DrawCell(ButtonOrigin, FVector2D(76.0f, 40.0f), FLinearColor(0.18f, 0.20f, 0.27f, 1.0f));
		DrawText(Labels[Index], FLinearColor::White, ButtonOrigin.X + 28.0f, ButtonOrigin.Y + 8.0f,
			GEngine->GetSmallFont(), 1.0f, false);
		AddHitBox(ButtonOrigin, FVector2D(76.0f, 40.0f), Names[Index], true, 120);
	}
	const FVector2D CraftOrigin(Origin.X + 100.0f, Origin.Y + 162.0f);
	const FVector2D CancelOrigin(Origin.X + 250.0f, Origin.Y + 162.0f);
	DrawCell(CraftOrigin, FVector2D(120.0f, 36.0f), PendingCraftCount > 0 ? FLinearColor(0.10f, 0.42f, 0.18f, 1.0f) : FLinearColor(0.18f, 0.18f, 0.18f, 1.0f));
	DrawCell(CancelOrigin, FVector2D(120.0f, 36.0f), FLinearColor(0.42f, 0.12f, 0.12f, 1.0f));
	DrawText(TEXT("CRAFT"), FLinearColor::White, CraftOrigin.X + 34.0f, CraftOrigin.Y + 7.0f, GEngine->GetSmallFont(), 0.9f, false);
	DrawText(TEXT("CANCEL"), FLinearColor::White, CancelOrigin.X + 30.0f, CancelOrigin.Y + 7.0f, GEngine->GetSmallFont(), 0.9f, false);
	AddHitBox(CraftOrigin, FVector2D(120.0f, 36.0f), QuantityCraft, true, 120);
	AddHitBox(CancelOrigin, FVector2D(120.0f, 36.0f), QuantityCancel, true, 120);
}

FString AABTSM5InventoryHUD::BuildFailureTooltip(
	const FABTSCraftingEvaluation& Evaluation,
	const UABTSInventoryComponent& Inventory) const
{
	TArray<FString> Reasons;
	if (!Evaluation.bRedBirdControlled) Reasons.Add(TEXT("Only Red bird can craft"));
	if (!Evaluation.bStationAvailable && Evaluation.Recipe)
	{
		Reasons.Add(FString::Printf(TEXT("Missing nearby %s"), *ABTSGetCraftingStationFallbackLabel(Evaluation.Recipe->RequiredStation)));
	}
	for (const FABTSCraftingIngredient& Required : Evaluation.MissingIngredients)
	{
		Reasons.Add(FString::Printf(TEXT("Missing %s: %d"), *ABTSGetItemFallbackLabel(Required.ItemId),
			FMath::Max(0, Required.Quantity - Inventory.GetQuantity(Required.ItemId))));
	}
	return FString::Join(Reasons, TEXT(" | "));
}

void AABTSM5InventoryHUD::DrawTooltip(AABTSCraftingSystem& System)
{
	const FString HoverName = HoveredHitBox.ToString();
	if (!HoverName.StartsWith(TEXT("ABTS_M5_Recipe_"))) return;
	const int32 Index = FCString::Atoi(*HoverName.RightChop(16));
	if (!VisibleRecipeIds.IsValidIndex(Index)) return;
	const FABTSCraftingRecipe* Recipe = System.GetCatalog()->FindRecipe(VisibleRecipeIds[Index]);
	if (Recipe == nullptr) return;
	const FABTSCraftingEvaluation Evaluation = System.GetCatalog()->Evaluate(*Recipe, *System.GetInventory(),
		System.IsRedBirdControlled(), System.IsStationAvailable(EABTSCraftingStationType::Workbench),
		System.IsStationAvailable(EABTSCraftingStationType::Furnace));
	const FString Message = Evaluation.IsCraftable()
		? FString::Printf(TEXT("Can craft up to %d"), Evaluation.MaxCraftCount)
		: BuildFailureTooltip(Evaluation, *System.GetInventory());
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (PlayerOwner == nullptr || !PlayerOwner->GetMousePosition(MouseX, MouseY)) return;
	DrawPanel(FVector2D(MouseX + 14.0f, MouseY + 14.0f), FVector2D(390.0f, 38.0f), FLinearColor(0.02f, 0.02f, 0.025f, 0.98f));
	DrawText(Message, Evaluation.IsCraftable() ? FLinearColor::White : FLinearColor(1.0f, 0.28f, 0.20f),
		MouseX + 23.0f, MouseY + 23.0f, GEngine->GetSmallFont(), 0.78f, false);
}

void AABTSM5InventoryHUD::NotifyHitBoxClick(const FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);
	AABTSM5PlayerController* Controller = GetM5Controller();
	AABTSCraftingSystem* System = FindCraftingSystem();
	if (Controller == nullptr || System == nullptr) return;
	if (BoxName == InventoryHitBox)
	{
		Controller->OpenCraftingInterface();
		return;
	}
	if (BoxName == HeldItemHitBox)
	{
		System->GetInventory()->ClearHeldItem();
		return;
	}
	if (BoxName.ToString().StartsWith(TEXT("ABTS_M5_Hotbar_")))
	{
		const FString Prefix(TEXT("ABTS_M5_Hotbar_"));
		const int32 Slot = FCString::Atoi(*BoxName.ToString().RightChop(Prefix.Len()));
		EABTSItemId ItemId;
		if (System->GetInventory()->GetHotbarItemAt(Slot, ItemId)) System->GetInventory()->SetHeldItem(ItemId);
		else Controller->OpenCraftingInterface();
		return;
	}
	if (BoxName == CloseHitBox) { Controller->CloseCraftingInterface(); return; }
	if (BoxName == QuantityCancel) { ResetCraftingSelection(); return; }
	const FABTSCraftingRecipe* SelectedRecipe = System->GetCatalog()->FindRecipe(SelectedRecipeId);
	if (SelectedRecipe)
	{
		const FABTSCraftingEvaluation Evaluation = System->GetCatalog()->Evaluate(*SelectedRecipe, *System->GetInventory(),
			System->IsRedBirdControlled(), System->IsStationAvailable(EABTSCraftingStationType::Workbench),
			System->IsStationAvailable(EABTSCraftingStationType::Furnace));
		if (BoxName == QuantityMinusTen) PendingCraftCount = FMath::Clamp(PendingCraftCount - 10, 0, Evaluation.MaxCraftCount);
		else if (BoxName == QuantityMinusOne) PendingCraftCount = FMath::Clamp(PendingCraftCount - 1, 0, Evaluation.MaxCraftCount);
		else if (BoxName == QuantityPlusOne) PendingCraftCount = FMath::Clamp(PendingCraftCount + 1, 0, Evaluation.MaxCraftCount);
		else if (BoxName == QuantityPlusTen) PendingCraftCount = FMath::Clamp(PendingCraftCount + 10, 0, Evaluation.MaxCraftCount);
		else if (BoxName == QuantityCraft && PendingCraftCount > 0)
		{
			if (System->Craft(SelectedRecipeId, PendingCraftCount)) ResetCraftingSelection();
		}
		return;
	}
	const FString Name = BoxName.ToString();
	if (!Name.StartsWith(TEXT("ABTS_M5_Recipe_"))) return;
	const int32 Index = FCString::Atoi(*Name.RightChop(16));
	if (!VisibleRecipeIds.IsValidIndex(Index)) return;
	const FABTSCraftingRecipe* Recipe = System->GetCatalog()->FindRecipe(VisibleRecipeIds[Index]);
	if (Recipe == nullptr) return;
	const FABTSCraftingEvaluation Evaluation = System->GetCatalog()->Evaluate(*Recipe, *System->GetInventory(),
		System->IsRedBirdControlled(), System->IsStationAvailable(EABTSCraftingStationType::Workbench),
		System->IsStationAvailable(EABTSCraftingStationType::Furnace));
	if (Evaluation.IsCraftable())
	{
		SelectedRecipeId = Recipe->RecipeId;
		PendingCraftCount = 1;
	}
	else
	{
		InvalidHighlightRecipeId = Recipe->RecipeId;
		InvalidHighlightUntilSeconds = GetWorld() ? GetWorld()->GetRealTimeSeconds() + 0.5 : 0.0;
	}
}

void AABTSM5InventoryHUD::NotifyHitBoxBeginCursorOver(const FName BoxName)
{
	Super::NotifyHitBoxBeginCursorOver(BoxName);
	HoveredHitBox = BoxName;
}

void AABTSM5InventoryHUD::NotifyHitBoxEndCursorOver(const FName BoxName)
{
	Super::NotifyHitBoxEndCursorOver(BoxName);
	if (HoveredHitBox == BoxName) HoveredHitBox = NAME_None;
}

void AABTSM5InventoryHUD::ResetCraftingSelection()
{
	SelectedRecipeId = NAME_None;
	PendingCraftCount = 1;
}

AABTSCraftingSystem* AABTSM5InventoryHUD::FindCraftingSystem()
{
	if (CraftingSystem.IsValid()) return CraftingSystem.Get();
	for (TActorIterator<AABTSCraftingSystem> It(GetWorld()); It; ++It)
	{
		CraftingSystem = *It;
		return CraftingSystem.Get();
	}
	return nullptr;
}

AABTSM5PlayerController* AABTSM5InventoryHUD::GetM5Controller() const
{
	return Cast<AABTSM5PlayerController>(PlayerOwner);
}
