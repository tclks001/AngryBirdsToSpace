// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Runtime-resolved shared UI theme. Defaults live in C++; console variables can
 * override every frozen token without creating or editing Unreal assets.
 */
struct ABTSRUNTIME_API FABTSUIThemeSnapshot
{
	static constexpr int32 ThemeVersion = 1;
	static constexpr bool bFrozen = true;

	FLinearColor PanelPrimary;
	FLinearColor PanelSecondary;
	FLinearColor PanelBorder;
	FLinearColor SlotBorder;
	FLinearColor SlotNormal;
	FLinearColor SlotHeld;
	FLinearColor SlotSelected;
	FLinearColor Disabled;
	FLinearColor Success;
	FLinearColor Warning;
	FLinearColor Danger;
	FLinearColor DangerFlash;
	FLinearColor AccentPrimary;
	FLinearColor AccentSecondary;
	FLinearColor TextPrimary;
	FLinearColor TextMuted;
	FLinearColor CountAccent;
	FLinearColor PortraitBacking;

	float GlobalOpacity = 1.0f;
	float BorderThicknessPx = 3.0f;
	float CellInsetPx = 3.0f;
	float HotbarSlotSizePx = 78.0f;
	float InventoryRowHeightPx = 46.0f;
	float InventoryCellHeightPx = 42.0f;
	float RecipeRowHeightPx = 66.0f;
	float TextScale = 1.0f;
	bool bDebugOverlay = false;

	FLinearColor ApplyOpacity(const FLinearColor& Color) const;
};

/** Pure C++ access point for the shared frozen theme and its diagnostic dump. */
class ABTSRUNTIME_API FABTSUITheme
{
public:
	static FABTSUIThemeSnapshot Get();
	static bool TryParseHexColor(const FString& Value, FLinearColor& OutColor);
	static FString ToHex(const FLinearColor& Color);
	static bool SetToken(const FString& Token, const FString& Value, FString& OutMessage);
	static void ResetToDefaults();
	static FString GetHelp();
	static void Dump();
};
