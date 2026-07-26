// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Crafting/ABTSCraftingTypes.h"
#include "UI/ABTSM4PartyHUD.h"
#include "ABTSM5InventoryHUD.generated.h"

class AABTSCraftingSystem;
class AABTSM5PlayerController;
class UABTSInventoryComponent;

/** Asset-free M5 hotbar, inventory, recipe list and craft-quantity modal. */
UCLASS()
class ABTSRUNTIME_API AABTSM5InventoryHUD : public AABTSM4PartyHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
	virtual void NotifyHitBoxClick(FName BoxName) override;
	virtual void NotifyHitBoxBeginCursorOver(FName BoxName) override;
	virtual void NotifyHitBoxEndCursorOver(FName BoxName) override;
	void ResetCraftingSelection();
	/** Routes the mouse wheel to the left-side backpack while the modal is open. */
	void ScrollInventoryRows(float WheelValue);

private:
	void DrawHotbar(AABTSCraftingSystem& System);
	void DrawCraftingInterface(AABTSCraftingSystem& System);
	void DrawInventoryPanel(const UABTSInventoryComponent& Inventory, const FVector2D& Origin, const FVector2D& Size);
	void DrawRecipePanel(AABTSCraftingSystem& System, const FVector2D& Origin, const FVector2D& Size);
	void DrawQuantityModal(AABTSCraftingSystem& System);
	void DrawTooltip(AABTSCraftingSystem& System);
	void DrawPanel(const FVector2D& Origin, const FVector2D& Size, const FLinearColor& Color) const;
	void DrawCell(const FVector2D& Origin, const FVector2D& Size, const FLinearColor& Color) const;
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
};

