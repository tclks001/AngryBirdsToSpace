// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Inventory/ABTSInventoryTypes.h"

/** Pure-data geometry shared by M5 HUD drawing, hit testing and automation. */
struct ABTSRUNTIME_API FABTSM5InventoryUILayout
{
	FBox2D HotbarShell;
	FBox2D BagButton;
	TArray<FBox2D> HotbarSlots;
	FBox2D HeldSlot;
	FBox2D ModalShell;
	FBox2D InventoryPanel;
	FBox2D RecipePanel;
	FBox2D CloseButton;
	bool bCompact = false;
};

/** Deterministic layout and asset-path contract for the shared inventory HUD. */
class ABTSRUNTIME_API FABTSM5InventoryHUDData
{
public:
	static bool ResolveLayout(
		float ViewportWidth,
		float ViewportHeight,
		float RequestedSlotSize,
		int32 SlotCount,
		FABTSM5InventoryUILayout& OutLayout);

	static void BuildFacetedVertices(
		const FBox2D& Box,
		float RequestedCutPx,
		TStaticArray<FVector2D, 8>& OutVertices);

	/** Centers a source rectangle inside the bounds without cropping or changing its aspect ratio. */
	static bool FitAspectRatio(
		const FBox2D& Bounds,
		const FVector2D& SourceSize,
		FBox2D& OutBox);

	static const TCHAR* GetItemIconAssetPath(EABTSItemId ItemId);
};
