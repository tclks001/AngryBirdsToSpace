// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSM5InventoryHUD.h"

#include "ABTSRuntime.h"
#include "Audio/ABTSAudioWorldSubsystem.h"
#include "Crafting/ABTSCraftingCatalog.h"
#include "Crafting/ABTSCraftingSystem.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HighResScreenshot.h"
#include "Inventory/ABTSInventoryComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Player/ABTSM5PlayerController.h"
#include "UObject/ConstructorHelpers.h"

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
	constexpr int32 M5CaptureWarmupFrames = 36;
	constexpr int32 M5CaptureTimeoutFrames = 600;

	int32 EvaluationSortRank(const FABTSCraftingEvaluation& Value)
	{
		// Red-bird ownership is a global crafting permission and must not destroy
		// the recipe list's useful material/station ordering while another bird is controlled.
		if (!Value.bStationAvailable) return 2;
		if (!Value.bHasMaterials) return 1;
		return 0;
	}
}

AABTSM5InventoryHUD::AABTSM5InventoryHUD()
{
	ItemIcons.SetNum(static_cast<int32>(EABTSItemId::SpaceCord) + 1);
	for (const EABTSItemId ItemId : ABTSGetAllItemIds())
	{
		const TCHAR* AssetPath = FABTSM5InventoryHUDData::GetItemIconAssetPath(ItemId);
		if (AssetPath == nullptr) continue;
		ConstructorHelpers::FObjectFinder<UTexture2D> IconFinder(AssetPath);
		if (IconFinder.Succeeded())
		{
			ItemIcons[static_cast<int32>(ItemId)] = IconFinder.Object;
		}
	}
}

void AABTSM5InventoryHUD::DrawHUD()
{
	Super::DrawHUD();
	AABTSCraftingSystem* System = FindCraftingSystem();
	if (Canvas == nullptr || System == nullptr) return;
	ActiveTheme = FABTSUITheme::Get();
	if (!FABTSM5InventoryHUDData::ResolveLayout(
		Canvas->ClipX,
		Canvas->ClipY,
		ActiveTheme.HotbarSlotSizePx,
		System->GetInventory() ? System->GetInventory()->GetHotbarSlotCount() : 8,
		ActiveLayout))
	{
		return;
	}
	UpdateOffscreenCapture(*System);
	const AABTSM5PlayerController* Controller = GetM5Controller();
	if (Controller != nullptr && Controller->IsCraftingInterfaceOpen()) DrawCraftingInterface(*System);
	else DrawHotbar(*System);
}

void AABTSM5InventoryHUD::DrawPanel(const FVector2D& Origin, const FVector2D& Size, const FLinearColor& Color)
{
	DrawFacetedBox(FBox2D(Origin, Origin + Size), Color, ActiveTheme.PanelBorder, 10.0f, 1.5f);
}

void AABTSM5InventoryHUD::DrawCell(const FVector2D& Origin, const FVector2D& Size, const FLinearColor& Color)
{
	const float MaxInset = FMath::Min(Size.X, Size.Y) * 0.25f;
	const float BorderInset = FMath::Min(ActiveTheme.BorderThicknessPx, MaxInset);
	const float FillInset = FMath::Min(BorderInset + ActiveTheme.CellInsetPx, MaxInset);
	DrawFacetedBox(FBox2D(Origin, Origin + Size), ActiveTheme.SlotBorder, ActiveTheme.SlotBorder, 9.0f, 1.0f);
	DrawFacetedBox(
		FBox2D(Origin + FVector2D(BorderInset), Origin + Size - FVector2D(BorderInset)),
		ActiveTheme.PanelBorder,
		ActiveTheme.PanelBorder,
		FMath::Max(3.0f, 9.0f - BorderInset),
		1.0f);
	DrawFacetedBox(
		FBox2D(Origin + FVector2D(FillInset), Origin + Size - FVector2D(FillInset)),
		Color,
		Color,
		FMath::Max(2.0f, 9.0f - FillInset),
		1.0f);
}

void AABTSM5InventoryHUD::DrawFacetedBox(
	const FBox2D& Box,
	const FLinearColor& Fill,
	const FLinearColor& Border,
	const float CutPx,
	const float BorderPx)
{
	if (Canvas == nullptr || !Box.bIsValid) return;
	TStaticArray<FVector2D, 8> Vertices;
	FABTSM5InventoryHUDData::BuildFacetedVertices(Box, CutPx, Vertices);
	const FTexture* FillTexture = Canvas->DefaultTexture != nullptr ? Canvas->DefaultTexture->GetResource() : nullptr;
	if (FillTexture != nullptr)
	{
		TArray<FCanvasUVTri> Triangles;
		Triangles.Reserve(Vertices.Num());
		const FVector2D Center = Box.GetCenter();
		const FLinearColor ResolvedFill = ActiveTheme.ApplyOpacity(Fill);
		for (int32 Index = 0; Index < Vertices.Num(); ++Index)
		{
			FCanvasUVTri& Triangle = Triangles.AddDefaulted_GetRef();
			Triangle.V0_Pos = Center;
			Triangle.V1_Pos = Vertices[Index];
			Triangle.V2_Pos = Vertices[(Index + 1) % Vertices.Num()];
			Triangle.V0_UV = FVector2D::ZeroVector;
			Triangle.V1_UV = FVector2D::ZeroVector;
			Triangle.V2_UV = FVector2D::ZeroVector;
			Triangle.V0_Color = ResolvedFill;
			Triangle.V1_Color = ResolvedFill;
			Triangle.V2_Color = ResolvedFill;
		}
		FCanvasTriangleItem FillItem(Triangles, FillTexture);
		FillItem.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(FillItem);
	}
	for (int32 Index = 0; Index < Vertices.Num(); ++Index)
	{
		const FVector2D& A = Vertices[Index];
		const FVector2D& B = Vertices[(Index + 1) % Vertices.Num()];
		DrawLine(A.X, A.Y, B.X, B.Y, ActiveTheme.ApplyOpacity(Border), BorderPx);
	}
}

void AABTSM5InventoryHUD::DrawSectionFrame(
	const FBox2D& Box,
	const FString& Label,
	const FLinearColor& Accent)
{
	DrawFacetedBox(Box, ActiveTheme.PanelSecondary, ActiveTheme.PanelBorder, 14.0f, ActiveTheme.BorderThicknessPx);
	const float LabelX = Box.Min.X + 20.0f;
	DrawText(Label, ActiveTheme.ApplyOpacity(ActiveTheme.TextMuted), LabelX, Box.Min.Y + 10.0f,
		GEngine->GetSmallFont(), 0.72f * ActiveTheme.TextScale, false);
	DrawLine(LabelX, Box.Min.Y + 31.0f, FMath::Min(Box.Max.X - 20.0f, LabelX + 150.0f), Box.Min.Y + 31.0f,
		ActiveTheme.ApplyOpacity(Accent), 2.0f);
}

UTexture2D* AABTSM5InventoryHUD::GetItemIcon(const EABTSItemId ItemId) const
{
	const int32 Index = static_cast<int32>(ItemId);
	return ItemIcons.IsValidIndex(Index) ? ItemIcons[Index] : nullptr;
}

void AABTSM5InventoryHUD::DrawItemIcon(
	const EABTSItemId ItemId,
	const FBox2D& Box,
	const FLinearColor& Tint)
{
	UTexture2D* Icon = GetItemIcon(ItemId);
	if (Canvas == nullptr || Icon == nullptr || Icon->GetResource() == nullptr || !Box.bIsValid) return;
	FCanvasTileItem Tile(Box.Min, Icon->GetResource(), Box.Max - Box.Min, ActiveTheme.ApplyOpacity(Tint));
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

void AABTSM5InventoryHUD::DrawCountBadge(const int32 Quantity, const FBox2D& Box)
{
	if (Quantity <= 0 || !Box.bIsValid) return;
	const FString Count = Quantity > 999 ? TEXT("999+") : FString::FromInt(Quantity);
	const float Width = Count.Len() >= 3 ? 38.0f : Count.Len() == 2 ? 30.0f : 24.0f;
	const FBox2D Badge(
		FVector2D(Box.Max.X - Width - 5.0f, Box.Max.Y - 24.0f),
		FVector2D(Box.Max.X - 5.0f, Box.Max.Y - 5.0f));
	DrawFacetedBox(Badge, ActiveTheme.PanelPrimary, ActiveTheme.AccentPrimary, 4.0f, 1.0f);
	DrawText(Count, ActiveTheme.ApplyOpacity(ActiveTheme.CountAccent), Badge.Min.X + 5.0f, Badge.Min.Y + 1.0f,
		GEngine->GetSmallFont(), 0.68f * ActiveTheme.TextScale, false);
}

void AABTSM5InventoryHUD::DrawItemCard(
	const EABTSItemId ItemId,
	const int32 Quantity,
	const FBox2D& Box,
	const bool bHeld,
	const FString& Label,
	const bool bShowLabel)
{
	DrawCell(Box.Min, Box.Max - Box.Min, bHeld ? ActiveTheme.SlotSelected : ActiveTheme.SlotNormal);
	const float LabelHeight = bShowLabel ? 21.0f : 8.0f;
	const FBox2D IconBox(Box.Min + FVector2D(10.0f, 7.0f), Box.Max - FVector2D(10.0f, LabelHeight));
	DrawItemIcon(ItemId, IconBox, bHeld ? FLinearColor(1.0f, 0.94f, 0.72f, 1.0f) : FLinearColor::White);
	if (bShowLabel)
	{
		DrawText(Label, ActiveTheme.ApplyOpacity(bHeld ? ActiveTheme.AccentPrimary : ActiveTheme.TextPrimary),
			Box.Min.X + 8.0f, Box.Max.Y - 21.0f, GEngine->GetSmallFont(), 0.62f * ActiveTheme.TextScale, false);
	}
	DrawCountBadge(Quantity, Box);
}

FName AABTSM5InventoryHUD::MakeHotbarName(const int32 Slot) const
{
	return FName(*FString::Printf(TEXT("ABTS_M5_Hotbar_%d"), Slot));
}

FName AABTSM5InventoryHUD::MakeInventoryItemName(const int32 VisibleItemIndex) const
{
	return FName(*FString::Printf(TEXT("ABTS_M5_InventoryItem_%d"), VisibleItemIndex));
}

FName AABTSM5InventoryHUD::MakeRecipeName(const int32 RecipeIndex) const
{
	return FName(*FString::Printf(TEXT("ABTS_M5_Recipe_%d"), RecipeIndex));
}

void AABTSM5InventoryHUD::DrawHotbar(AABTSCraftingSystem& System)
{
	const UABTSInventoryComponent* Inventory = System.GetInventory();
	if (Inventory == nullptr || GEngine == nullptr) return;
	DrawFacetedBox(
		ActiveLayout.HotbarShell,
		ActiveTheme.PanelPrimary,
		ActiveTheme.PanelBorder,
		14.0f,
		ActiveTheme.BorderThicknessPx);
	DrawLine(
		ActiveLayout.HotbarShell.Min.X + 18.0f,
		ActiveLayout.HotbarShell.Min.Y + 4.0f,
		ActiveLayout.HotbarShell.Max.X - 18.0f,
		ActiveLayout.HotbarShell.Min.Y + 4.0f,
		ActiveTheme.ApplyOpacity(ActiveTheme.AccentSecondary.CopyWithNewOpacity(0.72f)),
		1.5f);

	DrawCell(ActiveLayout.BagButton.Min, ActiveLayout.BagButton.Max - ActiveLayout.BagButton.Min, ActiveTheme.PanelSecondary);
	DrawText(TEXT("BAG"), ActiveTheme.ApplyOpacity(ActiveTheme.TextPrimary),
		ActiveLayout.BagButton.Min.X + 16.0f, ActiveLayout.BagButton.Min.Y + 24.0f,
		GEngine->GetSmallFont(), 0.82f * ActiveTheme.TextScale, false);
	DrawText(TEXT("K"), ActiveTheme.ApplyOpacity(ActiveTheme.AccentSecondary),
		ActiveLayout.BagButton.Min.X + 7.0f, ActiveLayout.BagButton.Min.Y + 6.0f,
		GEngine->GetSmallFont(), 0.58f * ActiveTheme.TextScale, false);
	AddHitBox(ActiveLayout.BagButton.Min, ActiveLayout.BagButton.Max - ActiveLayout.BagButton.Min,
		InventoryHitBox, true, 25);

	EABTSItemId HeldItemId = EABTSItemId::Branch;
	const bool bHasHeldItem = Inventory->GetHeldItem(HeldItemId);
	for (int32 Slot = 0; Slot < ActiveLayout.HotbarSlots.Num(); ++Slot)
	{
		const FBox2D& SlotBox = ActiveLayout.HotbarSlots[Slot];
		EABTSItemId ItemId;
		if (Inventory->GetHotbarItemAt(Slot, ItemId))
		{
			const bool bHeld = bHasHeldItem && ItemId == HeldItemId;
			DrawItemCard(ItemId, Inventory->GetQuantity(ItemId), SlotBox, bHeld, FString(), false);
			const FString Label = ABTSGetItemFallbackLabel(ItemId).Left(12);
			DrawText(Label, ActiveTheme.ApplyOpacity(bHeld ? ActiveTheme.AccentPrimary : ActiveTheme.TextMuted),
				SlotBox.Min.X + 7.0f, SlotBox.Min.Y + 5.0f, GEngine->GetSmallFont(),
				0.47f * ActiveTheme.TextScale, false);
		}
		else
		{
			DrawCell(SlotBox.Min, SlotBox.Max - SlotBox.Min, ActiveTheme.SlotNormal.CopyWithNewOpacity(0.62f));
		}
		DrawText(FString::FromInt(Slot + 1), ActiveTheme.ApplyOpacity(ActiveTheme.TextMuted),
			SlotBox.Min.X + 6.0f, SlotBox.Max.Y - 20.0f, GEngine->GetSmallFont(),
			0.55f * ActiveTheme.TextScale, false);
		AddHitBox(SlotBox.Min, SlotBox.Max - SlotBox.Min, MakeHotbarName(Slot), true, 20);
	}

	DrawCell(ActiveLayout.HeldSlot.Min, ActiveLayout.HeldSlot.Max - ActiveLayout.HeldSlot.Min,
		bHasHeldItem ? ActiveTheme.SlotHeld : ActiveTheme.Disabled);
	DrawText(TEXT("HELD"), ActiveTheme.ApplyOpacity(bHasHeldItem ? ActiveTheme.AccentPrimary : ActiveTheme.TextMuted),
		ActiveLayout.HeldSlot.Min.X + 9.0f, ActiveLayout.HeldSlot.Min.Y + 5.0f,
		GEngine->GetSmallFont(), 0.54f * ActiveTheme.TextScale, false);
	if (bHasHeldItem)
	{
		const FBox2D IconBox(
			ActiveLayout.HeldSlot.Min + FVector2D(12.0f, 18.0f),
			ActiveLayout.HeldSlot.Max - FVector2D(12.0f, 8.0f));
		DrawItemIcon(HeldItemId, IconBox);
		DrawCountBadge(Inventory->GetQuantity(HeldItemId), ActiveLayout.HeldSlot);
	}
	AddHitBox(ActiveLayout.HeldSlot.Min, ActiveLayout.HeldSlot.Max - ActiveLayout.HeldSlot.Min,
		HeldItemHitBox, true, 25);
}

void AABTSM5InventoryHUD::DrawCraftingInterface(AABTSCraftingSystem& System)
{
	DrawRect(ActiveTheme.ApplyOpacity(FLinearColor(0.015f, 0.025f, 0.06f, 0.58f)),
		0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
	DrawFacetedBox(
		ActiveLayout.ModalShell,
		ActiveTheme.PanelPrimary,
		ActiveTheme.PanelBorder,
		20.0f,
		ActiveTheme.BorderThicknessPx);
	DrawLine(
		ActiveLayout.ModalShell.Min.X + 26.0f,
		ActiveLayout.ModalShell.Min.Y + 5.0f,
		ActiveLayout.ModalShell.Max.X - 26.0f,
		ActiveLayout.ModalShell.Min.Y + 5.0f,
		ActiveTheme.ApplyOpacity(ActiveTheme.AccentSecondary),
		2.0f);
	DrawText(TEXT("FIELD WORKSHOP  //  PARTY STORES"), ActiveTheme.ApplyOpacity(ActiveTheme.TextPrimary),
		ActiveLayout.ModalShell.Min.X + 28.0f, ActiveLayout.ModalShell.Min.Y + 16.0f,
		GEngine->GetMediumFont(), 0.92f * ActiveTheme.TextScale, false);
	DrawText(TEXT("Select an item to hold  |  Select a recipe to assemble"), ActiveTheme.ApplyOpacity(ActiveTheme.TextMuted),
		ActiveLayout.ModalShell.Min.X + 30.0f, ActiveLayout.ModalShell.Min.Y + 40.0f,
		GEngine->GetSmallFont(), 0.66f * ActiveTheme.TextScale, false);

	const FBox2D PermissionBox(
		FVector2D(ActiveLayout.CloseButton.Min.X - 218.0f, ActiveLayout.CloseButton.Min.Y + 2.0f),
		FVector2D(ActiveLayout.CloseButton.Min.X - 12.0f, ActiveLayout.CloseButton.Max.Y - 2.0f));
	DrawFacetedBox(PermissionBox,
		System.IsRedBirdControlled() ? ActiveTheme.Success.CopyWithNewOpacity(0.70f) : ActiveTheme.Danger.CopyWithNewOpacity(0.78f),
		System.IsRedBirdControlled() ? ActiveTheme.Success : ActiveTheme.DangerFlash,
		8.0f, 1.5f);
	DrawText(System.IsRedBirdControlled() ? TEXT("RED BIRD  //  ASSEMBLY READY") : TEXT("RED BIRD REQUIRED"),
		ActiveTheme.ApplyOpacity(ActiveTheme.TextPrimary), PermissionBox.Min.X + 14.0f, PermissionBox.Min.Y + 7.0f,
		GEngine->GetSmallFont(), 0.66f * ActiveTheme.TextScale, false);

	DrawCell(ActiveLayout.CloseButton.Min, ActiveLayout.CloseButton.Max - ActiveLayout.CloseButton.Min, ActiveTheme.Danger);
	DrawLine(ActiveLayout.CloseButton.Min.X + 11.0f, ActiveLayout.CloseButton.Min.Y + 11.0f,
		ActiveLayout.CloseButton.Max.X - 11.0f, ActiveLayout.CloseButton.Max.Y - 11.0f,
		ActiveTheme.ApplyOpacity(ActiveTheme.TextPrimary), 2.5f);
	DrawLine(ActiveLayout.CloseButton.Max.X - 11.0f, ActiveLayout.CloseButton.Min.Y + 11.0f,
		ActiveLayout.CloseButton.Min.X + 11.0f, ActiveLayout.CloseButton.Max.Y - 11.0f,
		ActiveTheme.ApplyOpacity(ActiveTheme.TextPrimary), 2.5f);
	AddHitBox(ActiveLayout.CloseButton.Min, ActiveLayout.CloseButton.Max - ActiveLayout.CloseButton.Min,
		CloseHitBox, true, 100);

	DrawInventoryPanel(*System.GetInventory(), ActiveLayout.InventoryPanel.Min,
		ActiveLayout.InventoryPanel.Max - ActiveLayout.InventoryPanel.Min);
	DrawRecipePanel(System, ActiveLayout.RecipePanel.Min,
		ActiveLayout.RecipePanel.Max - ActiveLayout.RecipePanel.Min);
	if (!SelectedRecipeId.IsNone()) DrawQuantityModal(System);
	else DrawTooltip(System);
}

void AABTSM5InventoryHUD::DrawInventoryPanel(
	const UABTSInventoryComponent& Inventory,
	const FVector2D& Origin,
	const FVector2D& Size)
{
	const FBox2D PanelBox(Origin, Origin + Size);
	DrawSectionFrame(PanelBox, TEXT("PARTY BACKPACK"), ActiveTheme.AccentSecondary);
	TArray<const FABTSItemStack*> NonEmptyStacks;
	for (const FABTSItemStack& Stack : Inventory.GetOrderedStacks())
	{
		if (Stack.Quantity > 0) NonEmptyStacks.Add(&Stack);
	}
	const int32 ColumnCount = FMath::Clamp(FMath::FloorToInt((Size.X - 22.0f) / 112.0f), 2, 4);
	const float Gap = 8.0f;
	const float GridLeft = Origin.X + 12.0f;
	const float GridTop = Origin.Y + 44.0f;
	const float CellWidth = (Size.X - 24.0f - Gap * (ColumnCount - 1)) / ColumnCount;
	const float CellHeight = FMath::Clamp(CellWidth * 0.88f, 82.0f, 112.0f);
	const float RowPitch = CellHeight + Gap;
	const int32 TotalRows = FMath::DivideAndRoundUp(NonEmptyStacks.Num(), ColumnCount);
	const int32 VisibleRowCount = FMath::Max(1, FMath::FloorToInt((Size.Y - 56.0f) / RowPitch));
	MaxInventoryScrollRowOffset = FMath::Max(0, TotalRows - VisibleRowCount);
	InventoryScrollRowOffset = FMath::Clamp(InventoryScrollRowOffset, 0, MaxInventoryScrollRowOffset);
	DrawText(FString::Printf(TEXT("%d TYPES"), NonEmptyStacks.Num()), ActiveTheme.ApplyOpacity(ActiveTheme.CountAccent),
		Origin.X + Size.X - 82.0f, Origin.Y + 10.0f, GEngine->GetSmallFont(), 0.64f * ActiveTheme.TextScale, false);
	if (MaxInventoryScrollRowOffset > 0)
	{
		const float TrackTop = Origin.Y + 42.0f;
		const float TrackHeight = Size.Y - 56.0f;
		const float ThumbHeight = FMath::Max(24.0f, TrackHeight * static_cast<float>(VisibleRowCount) / TotalRows);
		const float ThumbAlpha = static_cast<float>(InventoryScrollRowOffset) / MaxInventoryScrollRowOffset;
		DrawRect(ActiveTheme.ApplyOpacity(ActiveTheme.SlotNormal), Origin.X + Size.X - 7.0f, TrackTop, 3.0f, TrackHeight);
		DrawRect(ActiveTheme.ApplyOpacity(ActiveTheme.AccentSecondary), Origin.X + Size.X - 7.0f,
			TrackTop + (TrackHeight - ThumbHeight) * ThumbAlpha, 3.0f, ThumbHeight);
	}
	VisibleInventoryItemIds.Reset();
	const int32 FirstItem = InventoryScrollRowOffset * ColumnCount;
	const int32 LastExclusive = FMath::Min(NonEmptyStacks.Num(), FirstItem + VisibleRowCount * ColumnCount);
	for (int32 StackIndex = FirstItem; StackIndex < LastExclusive; ++StackIndex)
	{
		const FABTSItemStack& Stack = *NonEmptyStacks[StackIndex];
		const int32 LocalIndex = StackIndex - FirstItem;
		const int32 Row = LocalIndex / ColumnCount;
		const int32 Column = LocalIndex % ColumnCount;
		const FVector2D CellOrigin(GridLeft + Column * (CellWidth + Gap), GridTop + Row * RowPitch);
		const FBox2D CellBox(CellOrigin, CellOrigin + FVector2D(CellWidth, CellHeight));
		const bool bHeld = [&Inventory, &Stack]()
		{
			EABTSItemId HeldItemId;
			return Inventory.GetHeldItem(HeldItemId) && HeldItemId == Stack.ItemId;
		}();
		DrawItemCard(Stack.ItemId, Stack.Quantity, CellBox, bHeld,
			ABTSGetItemFallbackLabel(Stack.ItemId).Left(15), true);
		VisibleInventoryItemIds.Add(Stack.ItemId);
		AddHitBox(CellBox.Min, CellBox.Max - CellBox.Min,
			MakeInventoryItemName(VisibleInventoryItemIds.Num() - 1), true, 60);
	}
}

void AABTSM5InventoryHUD::DrawRecipePanel(
	AABTSCraftingSystem& System,
	const FVector2D& Origin,
	const FVector2D& Size)
{
	const FBox2D PanelBox(Origin, Origin + Size);
	DrawSectionFrame(PanelBox, TEXT("ASSEMBLY RECIPES"), ActiveTheme.AccentPrimary);
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
	const float AvailableRecipeRowHeight = (Size.Y - 48.0f) / FMath::Max(1, Evaluations.Num());
	const float RecipeRowHeight = FMath::Clamp(
		FMath::Min(ActiveTheme.RecipeRowHeightPx, AvailableRecipeRowHeight), 54.0f, 76.0f);
	float Y = Origin.Y + 42.0f;
	const double Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	for (int32 Index = 0; Index < Evaluations.Num(); ++Index)
	{
		const FABTSCraftingEvaluation& Evaluation = Evaluations[Index];
		const FABTSCraftingRecipe& Recipe = *Evaluation.Recipe;
		VisibleRecipeIds.Add(Recipe.RecipeId);
		const bool bFlash = Recipe.RecipeId == InvalidHighlightRecipeId && Now < InvalidHighlightUntilSeconds;
		const FLinearColor StatusAccent = Evaluation.IsCraftable()
			? ActiveTheme.Success
			: bFlash ? ActiveTheme.DangerFlash
			: !Evaluation.bStationAvailable ? ActiveTheme.Warning : ActiveTheme.Danger;
		const FBox2D RowBox(
			FVector2D(Origin.X + 10.0f, Y),
			FVector2D(Origin.X + Size.X - 10.0f, Y + RecipeRowHeight - 5.0f));
		DrawCell(RowBox.Min, RowBox.Max - RowBox.Min,
			Evaluation.IsCraftable() ? ActiveTheme.SlotNormal : ActiveTheme.Disabled);
		DrawRect(ActiveTheme.ApplyOpacity(StatusAccent), RowBox.Min.X + 5.0f, RowBox.Min.Y + 7.0f,
			4.0f, (RowBox.Max.Y - RowBox.Min.Y) - 14.0f);
		const FBox2D OutputIconBox(
			RowBox.Min + FVector2D(14.0f, 7.0f),
			FVector2D(RowBox.Min.X + 58.0f, RowBox.Max.Y - 7.0f));
		DrawItemIcon(Recipe.OutputItem, OutputIconBox,
			Evaluation.IsCraftable() ? FLinearColor::White : FLinearColor(0.66f, 0.72f, 0.80f, 0.76f));
		DrawText(Recipe.DisplayName.ToString(), ActiveTheme.ApplyOpacity(ActiveTheme.TextPrimary),
			RowBox.Min.X + 66.0f, RowBox.Min.Y + 7.0f,
			GEngine->GetSmallFont(), 0.74f * ActiveTheme.TextScale, false);
		DrawText(ABTSGetCraftingStationFallbackLabel(Recipe.RequiredStation).ToUpper(),
			ActiveTheme.ApplyOpacity(Evaluation.bStationAvailable ? ActiveTheme.AccentSecondary : ActiveTheme.Warning),
			RowBox.Min.X + 66.0f, RowBox.Min.Y + 29.0f,
			GEngine->GetSmallFont(), 0.54f * ActiveTheme.TextScale, false);
		float MaterialX = RowBox.Min.X + 202.0f;
		for (const FABTSCraftingIngredient& Required : Recipe.Ingredients)
		{
			const int32 Owned = Inventory->GetQuantity(Required.ItemId);
			const FBox2D IngredientIcon(
				FVector2D(MaterialX, RowBox.Min.Y + 9.0f),
				FVector2D(MaterialX + 28.0f, RowBox.Min.Y + 37.0f));
			DrawItemIcon(Required.ItemId, IngredientIcon,
				Owned < Required.Quantity ? FLinearColor(1.0f, 0.52f, 0.52f, 0.85f) : FLinearColor::White);
			DrawText(FString::Printf(TEXT("%d/%d"), Owned, Required.Quantity),
				ActiveTheme.ApplyOpacity(Owned < Required.Quantity ? ActiveTheme.DangerFlash : ActiveTheme.TextMuted),
				MaterialX + 2.0f, RowBox.Min.Y + 38.0f, GEngine->GetSmallFont(), 0.50f * ActiveTheme.TextScale, false);
			MaterialX += 64.0f;
		}
		const FString Status = Evaluation.IsCraftable() ? TEXT("READY")
			: !Evaluation.bRedBirdControlled ? TEXT("RED ONLY")
			: !Evaluation.bStationAvailable ? TEXT("NO STATION") : TEXT("NEED MATERIALS");
		DrawText(Status, ActiveTheme.ApplyOpacity(StatusAccent), RowBox.Max.X - 102.0f, RowBox.Min.Y + 22.0f,
			GEngine->GetSmallFont(), 0.54f * ActiveTheme.TextScale, false);
		AddHitBox(RowBox.Min, RowBox.Max - RowBox.Min, MakeRecipeName(Index), true, 50);
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
	const FVector2D Size(520.0f, 268.0f);
	const FVector2D Origin((Canvas->ClipX - Size.X) * 0.5f, (Canvas->ClipY - Size.Y) * 0.5f);
	DrawRect(ActiveTheme.ApplyOpacity(FLinearColor(0.0f, 0.0f, 0.02f, 0.42f)), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
	const FBox2D ModalBox(Origin, Origin + Size);
	DrawFacetedBox(ModalBox, ActiveTheme.PanelPrimary, ActiveTheme.AccentPrimary, 18.0f,
		ActiveTheme.BorderThicknessPx);
	DrawText(TEXT("ASSEMBLY QUANTITY"), ActiveTheme.ApplyOpacity(ActiveTheme.TextMuted),
		Origin.X + 24.0f, Origin.Y + 14.0f, GEngine->GetSmallFont(), 0.68f * ActiveTheme.TextScale, false);
	DrawLine(Origin.X + 24.0f, Origin.Y + 36.0f, Origin.X + 178.0f, Origin.Y + 36.0f,
		ActiveTheme.ApplyOpacity(ActiveTheme.AccentPrimary), 2.0f);
	const FBox2D ProductIcon(FVector2D(Origin.X + 28.0f, Origin.Y + 54.0f), FVector2D(Origin.X + 116.0f, Origin.Y + 142.0f));
	DrawCell(ProductIcon.Min, ProductIcon.Max - ProductIcon.Min, ActiveTheme.SlotSelected);
	DrawItemIcon(Recipe->OutputItem, FBox2D(ProductIcon.Min + FVector2D(10.0f), ProductIcon.Max - FVector2D(10.0f)));
	DrawText(Recipe->DisplayName.ToString(), ActiveTheme.ApplyOpacity(ActiveTheme.TextPrimary),
		Origin.X + 138.0f, Origin.Y + 58.0f, GEngine->GetMediumFont(), 0.88f * ActiveTheme.TextScale, false);
	DrawText(FString::Printf(TEXT("%d selected  //  %d maximum"), PendingCraftCount, Evaluation.MaxCraftCount),
		ActiveTheme.ApplyOpacity(ActiveTheme.CountAccent), Origin.X + 140.0f, Origin.Y + 96.0f,
		GEngine->GetSmallFont(), 0.76f * ActiveTheme.TextScale, false);
	const TCHAR* Labels[] = { TEXT("--"), TEXT("-"), TEXT("+"), TEXT("++") };
	const FName Names[] = { QuantityMinusTen, QuantityMinusOne, QuantityPlusOne, QuantityPlusTen };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVector2D ButtonOrigin(Origin.X + 138.0f + Index * 84.0f, Origin.Y + 126.0f);
		DrawCell(ButtonOrigin, FVector2D(70.0f, 42.0f), ActiveTheme.SlotNormal);
		DrawText(Labels[Index], ActiveTheme.ApplyOpacity(ActiveTheme.TextPrimary),
			ButtonOrigin.X + 25.0f, ButtonOrigin.Y + 8.0f,
			GEngine->GetSmallFont(), 0.90f * ActiveTheme.TextScale, false);
		AddHitBox(ButtonOrigin, FVector2D(70.0f, 42.0f), Names[Index], true, 120);
	}
	const FVector2D CancelOrigin(Origin.X + 138.0f, Origin.Y + 202.0f);
	const FVector2D CraftOrigin(Origin.X + 310.0f, Origin.Y + 202.0f);
	DrawCell(CancelOrigin, FVector2D(142.0f, 44.0f), ActiveTheme.SlotNormal);
	DrawCell(CraftOrigin, FVector2D(180.0f, 44.0f), PendingCraftCount > 0 ? ActiveTheme.Warning : ActiveTheme.Disabled);
	DrawText(TEXT("CANCEL"), ActiveTheme.ApplyOpacity(ActiveTheme.TextMuted), CancelOrigin.X + 43.0f, CancelOrigin.Y + 9.0f,
		GEngine->GetSmallFont(), 0.78f * ActiveTheme.TextScale, false);
	DrawText(TEXT("ASSEMBLE"), ActiveTheme.ApplyOpacity(ActiveTheme.TextPrimary), CraftOrigin.X + 50.0f, CraftOrigin.Y + 9.0f,
		GEngine->GetSmallFont(), 0.82f * ActiveTheme.TextScale, false);
	AddHitBox(CraftOrigin, FVector2D(180.0f, 44.0f), QuantityCraft, true, 120);
	AddHitBox(CancelOrigin, FVector2D(142.0f, 44.0f), QuantityCancel, true, 120);
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
	DrawPanel(FVector2D(MouseX + 14.0f, MouseY + 14.0f), FVector2D(390.0f, 38.0f), ActiveTheme.PanelPrimary);
	DrawText(Message, Evaluation.IsCraftable() ? ActiveTheme.TextPrimary : ActiveTheme.DangerFlash,
		MouseX + 23.0f, MouseY + 23.0f, GEngine->GetSmallFont(), 0.78f, false);
}

void AABTSM5InventoryHUD::UpdateOffscreenCapture(AABTSCraftingSystem& System)
{
	if (bCaptureFinished) return;
	if (!bCaptureParsed)
	{
		bCaptureParsed = true;
		if (!FParse::Value(FCommandLine::Get(), TEXT("ABTSM5UICapture="), CaptureMode))
		{
			bCaptureFinished = true;
			return;
		}
		CaptureMode = CaptureMode.TrimStartAndEnd().ToLower();
		FParse::Value(FCommandLine::Get(), TEXT("ABTSM5UICaptureOutput="), CaptureOutputPath);
		if (CaptureOutputPath.IsEmpty())
		{
			CaptureOutputPath = FPaths::Combine(
				FPaths::ProjectSavedDir(),
				TEXT("M5UI"),
				TEXT("Captures"),
				FString::Printf(TEXT("M5UI_%s.png"), *CaptureMode));
		}
		else if (FPaths::IsRelative(CaptureOutputPath))
		{
			CaptureOutputPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), CaptureOutputPath);
		}
		CaptureOutputPath = FPaths::ConvertRelativePathToFull(CaptureOutputPath);
	}

	if (!bCaptureInitialized)
	{
		if (CaptureMode != TEXT("hotbar") && CaptureMode != TEXT("backpack") && CaptureMode != TEXT("quantity"))
		{
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M5UI][Capture] Rejected Mode=%s Expected=hotbar|backpack|quantity"),
				*CaptureMode);
			bCaptureFinished = true;
			return;
		}
		UABTSInventoryComponent* Inventory = System.GetInventory();
		AABTSM5PlayerController* Controller = GetM5Controller();
		if (Inventory == nullptr || Controller == nullptr) return;
		for (const EABTSItemId ItemId : ABTSGetAllItemIds())
		{
			const int32 Existing = Inventory->GetQuantity(ItemId);
			if (Existing < 99) Inventory->AddItem(ItemId, 99 - Existing);
		}
		Inventory->SetHeldItem(EABTSItemId::BridgeKit);
		if (CaptureMode == TEXT("hotbar")) Controller->CloseCraftingInterface();
		else Controller->OpenCraftingInterface();
		if (CaptureMode == TEXT("quantity"))
		{
			SelectedRecipeId = FName(TEXT("WorkbenchKit"));
			PendingCraftCount = 3;
		}
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(CaptureOutputPath), true);
		bCaptureInitialized = true;
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M5UI][Capture] Initialized Mode=%s Items=%d Output=%s"),
			*CaptureMode, ABTSGetAllItemIds().Num(), *CaptureOutputPath);
	}

	++CaptureFrame;
	if (!bCaptureRequested && CaptureFrame >= M5CaptureWarmupFrames)
	{
		if (FScreenshotRequest::IsScreenshotRequested()) return;
		FScreenshotRequest::RequestScreenshot(CaptureOutputPath, false, false);
		bCaptureRequested = FScreenshotRequest::IsScreenshotRequested();
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M5UI][Capture] Requested=%d Frame=%d Mode=%s Output=%s"),
			bCaptureRequested ? 1 : 0, CaptureFrame, *CaptureMode, *CaptureOutputPath);
	}
	if (bCaptureRequested
		&& !FScreenshotRequest::IsScreenshotRequested()
		&& IFileManager::Get().FileExists(*CaptureOutputPath))
	{
		bCaptureFinished = true;
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M5UI][Capture] Complete Success=1 Mode=%s Frame=%d Output=%s"),
			*CaptureMode, CaptureFrame, *CaptureOutputPath);
		FPlatformMisc::RequestExit(false);
		return;
	}
	if (CaptureFrame > M5CaptureTimeoutFrames)
	{
		bCaptureFinished = true;
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M5UI][Capture] Complete Success=0 Reason=Timeout Mode=%s Output=%s"),
			*CaptureMode, *CaptureOutputPath);
		FPlatformMisc::RequestExit(true);
	}
}

void AABTSM5InventoryHUD::NotifyHitBoxClick(const FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);
	AABTSM5PlayerController* Controller = GetM5Controller();
	AABTSCraftingSystem* System = FindCraftingSystem();
	if (Controller == nullptr || System == nullptr) return;
	UABTSAudioWorldSubsystem* Audio = GetWorld() ? GetWorld()->GetSubsystem<UABTSAudioWorldSubsystem>() : nullptr;
	if (BoxName == InventoryHitBox)
	{
		Controller->OpenCraftingInterface();
		return;
	}
	if (BoxName == HeldItemHitBox)
	{
		System->GetInventory()->ClearHeldItem();
		if (Audio) Audio->PlayUIEvent(EABTSUIAudioEvent::Select);
		return;
	}
	const FString Name = BoxName.ToString();
	const FString InventoryItemPrefix(TEXT("ABTS_M5_InventoryItem_"));
	if (Name.StartsWith(InventoryItemPrefix))
	{
		const int32 VisibleItemIndex = FCString::Atoi(*Name.RightChop(InventoryItemPrefix.Len()));
		if (VisibleInventoryItemIds.IsValidIndex(VisibleItemIndex))
		{
			System->GetInventory()->SetHeldItem(VisibleInventoryItemIds[VisibleItemIndex]);
			if (Audio) Audio->PlayUIEvent(EABTSUIAudioEvent::Select);
		}
		return;
	}
	if (BoxName.ToString().StartsWith(TEXT("ABTS_M5_Hotbar_")))
	{
		const FString Prefix(TEXT("ABTS_M5_Hotbar_"));
		const int32 Slot = FCString::Atoi(*BoxName.ToString().RightChop(Prefix.Len()));
		EABTSItemId ItemId;
		if (System->GetInventory()->GetHotbarItemAt(Slot, ItemId))
		{
			System->GetInventory()->SetHeldItem(ItemId);
			if (Audio) Audio->PlayUIEvent(EABTSUIAudioEvent::Select);
		}
		else Controller->OpenCraftingInterface();
		return;
	}
	if (BoxName == CloseHitBox) { Controller->CloseCraftingInterface(); return; }
	if (BoxName == QuantityCancel)
	{
		ResetCraftingSelection();
		if (Audio) Audio->PlayUIEvent(EABTSUIAudioEvent::Select);
		return;
	}
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
		if ((BoxName == QuantityMinusTen || BoxName == QuantityMinusOne
			|| BoxName == QuantityPlusOne || BoxName == QuantityPlusTen) && Audio)
		{
			Audio->PlayUIEvent(EABTSUIAudioEvent::Tick);
		}
		return;
	}
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
		if (Audio) Audio->PlayUIEvent(EABTSUIAudioEvent::Select);
	}
	else
	{
		InvalidHighlightRecipeId = Recipe->RecipeId;
		InvalidHighlightUntilSeconds = GetWorld() ? GetWorld()->GetRealTimeSeconds() + 0.5 : 0.0;
		if (Audio) Audio->PlayUIEvent(EABTSUIAudioEvent::Error);
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

void AABTSM5InventoryHUD::ScrollInventoryRows(const float WheelValue)
{
	if (FMath::IsNearlyZero(WheelValue) || MaxInventoryScrollRowOffset <= 0) return;
	// Positive wheel values conventionally scroll the content upward (toward earlier acquisitions).
	InventoryScrollRowOffset = FMath::Clamp(InventoryScrollRowOffset - FMath::RoundToInt(WheelValue),
		0, MaxInventoryScrollRowOffset);
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
