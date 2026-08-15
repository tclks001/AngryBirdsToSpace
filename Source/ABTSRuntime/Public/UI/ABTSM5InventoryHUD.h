// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Crafting/ABTSCraftingTypes.h"
#include "UI/ABTSM4PartyHUD.h"
#include "UI/ABTSM5InventoryHUDData.h"
#include "UI/ABTSUITheme.h"
#include "ABTSM5InventoryHUD.generated.h"

class AABTSCraftingSystem;
class AABTSM5PlayerController;
class UABTSInventoryComponent;
class UTexture2D;

/** Shared-theme M5 hotbar, inventory, recipe list and craft-quantity modal. */
UCLASS()
class ABTSRUNTIME_API AABTSM5InventoryHUD : public AABTSM4PartyHUD
{
	GENERATED_BODY()

public:
	AABTSM5InventoryHUD();
	virtual void DrawHUD() override;
	virtual void NotifyHitBoxClick(FName BoxName) override;
	virtual void NotifyHitBoxBeginCursorOver(FName BoxName) override;
	virtual void NotifyHitBoxEndCursorOver(FName BoxName) override;
	void ResetCraftingSelection();
	/** Routes the mouse wheel to the left-side backpack while the modal is open. */
	void ScrollInventoryRows(float WheelValue);
	/** True when this pointer press belongs to backpack/hotbar UI rather than the world. */
	bool ConsumesPrimaryPointerAtScreenPosition(const FVector2D& ScreenPosition) const;

private:
	void DrawHotbar(AABTSCraftingSystem& System);
	void DrawCraftingInterface(AABTSCraftingSystem& System);
	void DrawInventoryPanel(const UABTSInventoryComponent& Inventory, const FVector2D& Origin, const FVector2D& Size);
	void DrawRecipePanel(AABTSCraftingSystem& System, const FVector2D& Origin, const FVector2D& Size);
	void DrawQuantityModal(AABTSCraftingSystem& System);
	void DrawTooltip(AABTSCraftingSystem& System);
	void DrawPanel(const FVector2D& Origin, const FVector2D& Size, const FLinearColor& Color);
	void DrawCell(const FVector2D& Origin, const FVector2D& Size, const FLinearColor& Color);
	void DrawFacetedBox(const FBox2D& Box, const FLinearColor& Fill, const FLinearColor& Border,
		float CutPx, float BorderPx);
	void DrawSectionFrame(const FBox2D& Box, const FString& Label, const FLinearColor& Accent);
	void DrawItemIcon(EABTSItemId ItemId, const FBox2D& Box, const FLinearColor& Tint = FLinearColor::White);
	void DrawActionIcon(EABTSM5ActionIcon Icon, const FBox2D& Box,
		const FLinearColor& Tint = FLinearColor::White, float Scale = 1.0f);
	void DrawCountBadge(int32 Quantity, const FBox2D& Box, bool bEmphasized = false);
	void DrawEmbeddedCountBadgeBox(const FBox2D& Box, const FLinearColor& Fill,
		const FLinearColor& Accent, float CutPx, float BorderPx);
	void DrawItemCard(EABTSItemId ItemId, int32 Quantity, const FBox2D& Box, bool bHeld,
		const FString& Label, bool bShowLabel);
	UTexture2D* GetItemIcon(EABTSItemId ItemId) const;
	void UpdateOffscreenCapture(AABTSCraftingSystem& System);
	AABTSCraftingSystem* FindCraftingSystem();
	AABTSM5PlayerController* GetM5Controller() const;
	FName MakeHotbarName(int32 Slot) const;
	FName MakeInventoryItemName(int32 VisibleItemIndex) const;
	FName MakeRecipeName(int32 RecipeIndex) const;
	FString BuildFailureTooltip(const FABTSCraftingEvaluation& Evaluation, const UABTSInventoryComponent& Inventory) const;

	TWeakObjectPtr<AABTSCraftingSystem> CraftingSystem;
	FName HoveredHitBox;
	FName SelectedRecipeId;
	FName InvalidHighlightRecipeId;
	int32 PendingCraftCount = 1;
	int32 InventoryScrollRowOffset = 0;
	int32 MaxInventoryScrollRowOffset = 0;
	double InvalidHighlightUntilSeconds = 0.0;
	TArray<EABTSItemId> VisibleInventoryItemIds;
	TArray<FName> VisibleRecipeIds;
	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> ItemIcons;
	UPROPERTY()
	TObjectPtr<UTexture2D> ActionIconAtlas;
	FABTSUIThemeSnapshot ActiveTheme;
	FABTSM5InventoryUILayout ActiveLayout;
	bool bCaptureParsed = false;
	bool bCaptureInitialized = false;
	bool bCaptureRequested = false;
	bool bCaptureFinished = false;
	int32 CaptureFrame = 0;
	FString CaptureMode;
	FString CaptureOutputPath;
};

