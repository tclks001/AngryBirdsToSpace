// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Inventory/ABTSInventoryTypes.h"

/** Fixed semantic cells in the shared 4x2 action-icon atlas. */
enum class EABTSM5ActionIcon : uint8
{
	Backpack = 0,
	Craft,
	Cancel,
	DecreaseOne,
	IncreaseOne,
	DecreaseLarge,
	IncreaseLarge,
	Close,
	Count
};

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

/** Pure-data result for a count plate embedded into an item card's lower-right corner. */
struct ABTSRUNTIME_API FABTSM5CountBadgeLayout
{
	FBox2D BadgeBox;
	FVector2D TextOrigin = FVector2D::ZeroVector;
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

	/** Resolves a measured count label into a lower-right badge without leaving the card bounds. */
	static bool ResolveCountBadgeLayout(
		const FBox2D& CardBox,
		const FVector2D& ScaledTextSize,
		float RequestedHeightPx,
		float RequestedPaddingXPx,
		float RequestedEdgeInsetPx,
		FABTSM5CountBadgeLayout& OutLayout);

	/**
	 * Stable input barrier for the backpack and hotbar. This deliberately does not depend on
	 * AHUD's transient hit-box list, which may be between draw-frame revisions during input.
	 */
	static bool ConsumesPrimaryPointer(
		const FABTSM5InventoryUILayout& Layout,
		const FVector2D& ScreenPosition,
		bool bCraftingInterfaceOpen);

	static const TCHAR* GetItemIconAssetPath(EABTSItemId ItemId);

	static const TCHAR* GetActionIconAtlasAssetPath();

	/** Resolves a semantic action to normalized UVs in the shared 4x2 atlas. */
	static bool GetActionIconUV(EABTSM5ActionIcon Icon, FBox2D& OutUV);
};
