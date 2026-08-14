// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSUITheme.h"

#include "ABTSRuntime.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// Frozen Theme v1 defaults derived from the four StyleOn T0 captures and the
	// master style board, then accepted in PIE on 2026-08-14.
	TAutoConsoleVariable<FString> CVarPanelPrimary(
		TEXT("abts.UI.Theme.PanelPrimary"), TEXT("0B1830F2"), TEXT("Primary panel RGBA hex."));
	TAutoConsoleVariable<FString> CVarPanelSecondary(
		TEXT("abts.UI.Theme.PanelSecondary"), TEXT("162844EE"), TEXT("Secondary panel RGBA hex."));
	TAutoConsoleVariable<FString> CVarPanelBorder(
		TEXT("abts.UI.Theme.PanelBorder"), TEXT("293F61F5"), TEXT("Panel border RGBA hex."));
	TAutoConsoleVariable<FString> CVarSlotBorder(
		TEXT("abts.UI.Theme.SlotBorder"), TEXT("06101FF8"), TEXT("Slot border RGBA hex."));
	TAutoConsoleVariable<FString> CVarSlotNormal(
		TEXT("abts.UI.Theme.SlotNormal"), TEXT("243752F2"), TEXT("Normal slot RGBA hex."));
	TAutoConsoleVariable<FString> CVarSlotHeld(
		TEXT("abts.UI.Theme.SlotHeld"), TEXT("5E4725F6"), TEXT("Held slot RGBA hex."));
	TAutoConsoleVariable<FString> CVarSlotSelected(
		TEXT("abts.UI.Theme.SlotSelected"), TEXT("7B5D25F8"), TEXT("Selected slot RGBA hex."));
	TAutoConsoleVariable<FString> CVarDisabled(
		TEXT("abts.UI.Theme.Disabled"), TEXT("253047D0"), TEXT("Disabled state RGBA hex."));
	TAutoConsoleVariable<FString> CVarSuccess(
		TEXT("abts.UI.Theme.Success"), TEXT("2E735EEB"), TEXT("Success state RGBA hex."));
	TAutoConsoleVariable<FString> CVarWarning(
		TEXT("abts.UI.Theme.Warning"), TEXT("C78A27FA"), TEXT("Warning state RGBA hex."));
	TAutoConsoleVariable<FString> CVarDanger(
		TEXT("abts.UI.Theme.Danger"), TEXT("A83C42FA"), TEXT("Danger state RGBA hex."));
	TAutoConsoleVariable<FString> CVarDangerFlash(
		TEXT("abts.UI.Theme.DangerFlash"), TEXT("E34C4FFA"), TEXT("Danger flash RGBA hex."));
	TAutoConsoleVariable<FString> CVarAccentPrimary(
		TEXT("abts.UI.Theme.AccentPrimary"), TEXT("F2BD4CFF"), TEXT("Warm primary accent RGBA hex."));
	TAutoConsoleVariable<FString> CVarAccentSecondary(
		TEXT("abts.UI.Theme.AccentSecondary"), TEXT("64D7E8FF"), TEXT("Cyan information accent RGBA hex."));
	TAutoConsoleVariable<FString> CVarTextPrimary(
		TEXT("abts.UI.Theme.TextPrimary"), TEXT("F4F0E5FF"), TEXT("Primary text RGBA hex."));
	TAutoConsoleVariable<FString> CVarTextMuted(
		TEXT("abts.UI.Theme.TextMuted"), TEXT("ACBBD0FF"), TEXT("Muted text RGBA hex."));
	TAutoConsoleVariable<FString> CVarCountAccent(
		TEXT("abts.UI.Theme.CountAccent"), TEXT("FFDC70FF"), TEXT("Inventory count RGBA hex."));
	TAutoConsoleVariable<FString> CVarPortraitBacking(
		TEXT("abts.UI.Theme.PortraitBacking"), TEXT("10192EF2"), TEXT("Portrait backing RGBA hex."));

	TAutoConsoleVariable<float> CVarGlobalOpacity(
		TEXT("abts.UI.Theme.GlobalOpacity"), 1.0f, TEXT("Global UI opacity multiplier [0.2, 1]."));
	TAutoConsoleVariable<float> CVarBorderThicknessPx(
		TEXT("abts.UI.Theme.BorderPx"), 3.0f, TEXT("Canvas cell border thickness in pixels [1, 12]."));
	TAutoConsoleVariable<float> CVarCellInsetPx(
		TEXT("abts.UI.Theme.CellInsetPx"), 3.0f, TEXT("Canvas cell inner inset in pixels [1, 12]."));
	TAutoConsoleVariable<float> CVarHotbarSlotSizePx(
		TEXT("abts.UI.Theme.HotbarSlotPx"), 78.0f, TEXT("M5 hotbar slot size in pixels [52, 128]."));
	TAutoConsoleVariable<float> CVarInventoryRowHeightPx(
		TEXT("abts.UI.Theme.InventoryRowPx"), 46.0f, TEXT("M5 inventory row pitch in pixels [34, 80]."));
	TAutoConsoleVariable<float> CVarInventoryCellHeightPx(
		TEXT("abts.UI.Theme.InventoryCellPx"), 42.0f, TEXT("M5 inventory cell height in pixels [30, 76]."));
	TAutoConsoleVariable<float> CVarRecipeRowHeightPx(
		TEXT("abts.UI.Theme.RecipeRowPx"), 66.0f, TEXT("M5 recipe row pitch in pixels [48, 104]."));
	TAutoConsoleVariable<float> CVarTextScale(
		TEXT("abts.UI.Theme.TextScale"), 1.0f, TEXT("Shared text scale multiplier [0.75, 1.5]."));
	TAutoConsoleVariable<int32> CVarDebugOverlay(
		TEXT("abts.UI.Theme.DebugOverlay"), 0,
		TEXT("Show a live token/metric overlay in PIE. 0=off, 1=on."));

	struct FABTSUIThemeTokenDefinition
	{
		const TCHAR* Token;
		const TCHAR* DefaultValue;
		bool bColor;
	};

	const FABTSUIThemeTokenDefinition ThemeTokenDefinitions[] =
	{
		{ TEXT("PanelPrimary"), TEXT("0B1830F2"), true },
		{ TEXT("PanelSecondary"), TEXT("162844EE"), true },
		{ TEXT("PanelBorder"), TEXT("293F61F5"), true },
		{ TEXT("SlotBorder"), TEXT("06101FF8"), true },
		{ TEXT("SlotNormal"), TEXT("243752F2"), true },
		{ TEXT("SlotHeld"), TEXT("5E4725F6"), true },
		{ TEXT("SlotSelected"), TEXT("7B5D25F8"), true },
		{ TEXT("Disabled"), TEXT("253047D0"), true },
		{ TEXT("Success"), TEXT("2E735EEB"), true },
		{ TEXT("Warning"), TEXT("C78A27FA"), true },
		{ TEXT("Danger"), TEXT("A83C42FA"), true },
		{ TEXT("DangerFlash"), TEXT("E34C4FFA"), true },
		{ TEXT("AccentPrimary"), TEXT("F2BD4CFF"), true },
		{ TEXT("AccentSecondary"), TEXT("64D7E8FF"), true },
		{ TEXT("TextPrimary"), TEXT("F4F0E5FF"), true },
		{ TEXT("TextMuted"), TEXT("ACBBD0FF"), true },
		{ TEXT("CountAccent"), TEXT("FFDC70FF"), true },
		{ TEXT("PortraitBacking"), TEXT("10192EF2"), true },
		{ TEXT("GlobalOpacity"), TEXT("1.0"), false },
		{ TEXT("BorderPx"), TEXT("3.0"), false },
		{ TEXT("CellInsetPx"), TEXT("3.0"), false },
		{ TEXT("HotbarSlotPx"), TEXT("78.0"), false },
		{ TEXT("InventoryRowPx"), TEXT("46.0"), false },
		{ TEXT("InventoryCellPx"), TEXT("42.0"), false },
		{ TEXT("RecipeRowPx"), TEXT("66.0"), false },
		{ TEXT("TextScale"), TEXT("1.0"), false },
		{ TEXT("DebugOverlay"), TEXT("0"), false },
	};

	const FABTSUIThemeTokenDefinition* FindThemeTokenDefinition(const FString& Token)
	{
		for (const FABTSUIThemeTokenDefinition& Definition : ThemeTokenDefinitions)
		{
			if (Token.Equals(Definition.Token, ESearchCase::IgnoreCase)) return &Definition;
		}
		return nullptr;
	}

	void NotifyThemeCommand(const FString& Message, const bool bError = false)
	{
		UE_LOG(LogABTSRuntime, Display, TEXT("[ABTS][UITheme][Live] %s"), *Message);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				0x0AB75001,
				4.0f,
				bError ? FColor(227, 76, 79) : FColor(100, 215, 232),
				Message);
		}
	}

	void ExecuteThemeSetCommand(const TArray<FString>& Args);
	void ExecuteThemeResetCommand();
	void ExecuteThemeHelpCommand();

	FLinearColor ReadColor(TAutoConsoleVariable<FString>& Variable, const TCHAR* FallbackHex)
	{
		FLinearColor Color;
		if (FABTSUITheme::TryParseHexColor(Variable.GetValueOnGameThread(), Color)) return Color;
		FABTSUITheme::TryParseHexColor(FString(FallbackHex), Color);
		return Color;
	}

	FAutoConsoleCommand DumpThemeCommand(
		TEXT("abts.UI.Theme.Dump"),
		TEXT("Print the resolved frozen theme and a reproducible -ExecCmds payload."),
		FConsoleCommandDelegate::CreateStatic(&FABTSUITheme::Dump));

	FAutoConsoleCommand SetThemeCommand(
		TEXT("abts.UI.Theme.Set"),
		TEXT("Set a live token with validation. Usage: abts.UI.Theme.Set SlotNormal FF00FFFF"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExecuteThemeSetCommand));

	FAutoConsoleCommand ResetThemeCommand(
		TEXT("abts.UI.Theme.Reset"),
		TEXT("Reset all live UI theme tokens to frozen Theme v1 C++ defaults."),
		FConsoleCommandDelegate::CreateStatic(&ExecuteThemeResetCommand));

	FAutoConsoleCommand HelpThemeCommand(
		TEXT("abts.UI.Theme.Help"),
		TEXT("Print live UI theme commands and token names."),
		FConsoleCommandDelegate::CreateStatic(&ExecuteThemeHelpCommand));
}

FLinearColor FABTSUIThemeSnapshot::ApplyOpacity(const FLinearColor& Color) const
{
	return FLinearColor(Color.R, Color.G, Color.B, FMath::Clamp(Color.A * GlobalOpacity, 0.0f, 1.0f));
}

bool FABTSUITheme::TryParseHexColor(const FString& Value, FLinearColor& OutColor)
{
	FString Hex = Value.TrimStartAndEnd();
	Hex.RemoveFromStart(TEXT("#"));
	if (Hex.Len() == 6) Hex += TEXT("FF");
	if (Hex.Len() != 8) return false;
	for (const TCHAR Character : Hex)
	{
		if (!FChar::IsHexDigit(Character)) return false;
	}
	const uint32 Packed = FParse::HexNumber(*Hex);
	const FColor SRGB(
		static_cast<uint8>((Packed >> 24) & 0xff),
		static_cast<uint8>((Packed >> 16) & 0xff),
		static_cast<uint8>((Packed >> 8) & 0xff),
		static_cast<uint8>(Packed & 0xff));
	OutColor = FLinearColor::FromSRGBColor(SRGB);
	return true;
}

FString FABTSUITheme::ToHex(const FLinearColor& Color)
{
	return Color.ToFColorSRGB().ToHex();
}

FABTSUIThemeSnapshot FABTSUITheme::Get()
{
	FABTSUIThemeSnapshot Theme;
	Theme.PanelPrimary = ReadColor(CVarPanelPrimary, TEXT("0B1830F2"));
	Theme.PanelSecondary = ReadColor(CVarPanelSecondary, TEXT("162844EE"));
	Theme.PanelBorder = ReadColor(CVarPanelBorder, TEXT("293F61F5"));
	Theme.SlotBorder = ReadColor(CVarSlotBorder, TEXT("06101FF8"));
	Theme.SlotNormal = ReadColor(CVarSlotNormal, TEXT("243752F2"));
	Theme.SlotHeld = ReadColor(CVarSlotHeld, TEXT("5E4725F6"));
	Theme.SlotSelected = ReadColor(CVarSlotSelected, TEXT("7B5D25F8"));
	Theme.Disabled = ReadColor(CVarDisabled, TEXT("253047D0"));
	Theme.Success = ReadColor(CVarSuccess, TEXT("2E735EEB"));
	Theme.Warning = ReadColor(CVarWarning, TEXT("C78A27FA"));
	Theme.Danger = ReadColor(CVarDanger, TEXT("A83C42FA"));
	Theme.DangerFlash = ReadColor(CVarDangerFlash, TEXT("E34C4FFA"));
	Theme.AccentPrimary = ReadColor(CVarAccentPrimary, TEXT("F2BD4CFF"));
	Theme.AccentSecondary = ReadColor(CVarAccentSecondary, TEXT("64D7E8FF"));
	Theme.TextPrimary = ReadColor(CVarTextPrimary, TEXT("F4F0E5FF"));
	Theme.TextMuted = ReadColor(CVarTextMuted, TEXT("ACBBD0FF"));
	Theme.CountAccent = ReadColor(CVarCountAccent, TEXT("FFDC70FF"));
	Theme.PortraitBacking = ReadColor(CVarPortraitBacking, TEXT("10192EF2"));
	Theme.GlobalOpacity = FMath::Clamp(CVarGlobalOpacity.GetValueOnGameThread(), 0.2f, 1.0f);
	Theme.BorderThicknessPx = FMath::Clamp(CVarBorderThicknessPx.GetValueOnGameThread(), 1.0f, 12.0f);
	Theme.CellInsetPx = FMath::Clamp(CVarCellInsetPx.GetValueOnGameThread(), 1.0f, 12.0f);
	Theme.HotbarSlotSizePx = FMath::Clamp(CVarHotbarSlotSizePx.GetValueOnGameThread(), 52.0f, 128.0f);
	Theme.InventoryRowHeightPx = FMath::Clamp(CVarInventoryRowHeightPx.GetValueOnGameThread(), 34.0f, 80.0f);
	Theme.InventoryCellHeightPx = FMath::Min(
		FMath::Clamp(CVarInventoryCellHeightPx.GetValueOnGameThread(), 30.0f, 76.0f),
		Theme.InventoryRowHeightPx - 2.0f);
	Theme.RecipeRowHeightPx = FMath::Clamp(CVarRecipeRowHeightPx.GetValueOnGameThread(), 48.0f, 104.0f);
	Theme.TextScale = FMath::Clamp(CVarTextScale.GetValueOnGameThread(), 0.75f, 1.5f);
	Theme.bDebugOverlay = CVarDebugOverlay.GetValueOnGameThread() != 0;
	return Theme;
}

bool FABTSUITheme::SetToken(const FString& Token, const FString& Value, FString& OutMessage)
{
	const FABTSUIThemeTokenDefinition* Definition = FindThemeTokenDefinition(Token);
	if (Definition == nullptr)
	{
		OutMessage = FString::Printf(TEXT("Unknown token '%s'. Run abts.UI.Theme.Help."), *Token);
		return false;
	}

	const FString TrimmedValue = Value.TrimStartAndEnd();
	if (Definition->bColor)
	{
		FLinearColor Parsed;
		if (!TryParseHexColor(TrimmedValue, Parsed))
		{
			OutMessage = FString::Printf(TEXT("Rejected %s=%s; expected RRGGBB or RRGGBBAA."),
				Definition->Token, *TrimmedValue);
			return false;
		}
	}
	else if (!TrimmedValue.IsNumeric())
	{
		OutMessage = FString::Printf(TEXT("Rejected %s=%s; expected a number."), Definition->Token, *TrimmedValue);
		return false;
	}

	const FString CVarName = FString::Printf(TEXT("abts.UI.Theme.%s"), Definition->Token);
	IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*CVarName);
	if (Variable == nullptr)
	{
		OutMessage = FString::Printf(TEXT("Registered token is unavailable: %s"), *CVarName);
		return false;
	}
	Variable->Set(*TrimmedValue, ECVF_SetByConsole);
	OutMessage = FString::Printf(TEXT("%s = %s (live)"), Definition->Token, *Variable->GetString());
	return true;
}

void FABTSUITheme::ResetToDefaults()
{
	for (const FABTSUIThemeTokenDefinition& Definition : ThemeTokenDefinitions)
	{
		const FString CVarName = FString::Printf(TEXT("abts.UI.Theme.%s"), Definition.Token);
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*CVarName))
		{
			Variable->Set(Definition.DefaultValue, ECVF_SetByConsole);
		}
	}
}

FString FABTSUITheme::GetHelp()
{
	TArray<FString> Tokens;
	Tokens.Reserve(UE_ARRAY_COUNT(ThemeTokenDefinitions));
	for (const FABTSUIThemeTokenDefinition& Definition : ThemeTokenDefinitions)
	{
		Tokens.Add(Definition.Token);
	}
	return FString::Printf(
		TEXT("Set: abts.UI.Theme.Set <Token> <Value> | Overlay: abts.UI.Theme.DebugOverlay 1 | Reset: abts.UI.Theme.Reset | Tokens: %s"),
		*FString::Join(Tokens, TEXT(", ")));
}

void FABTSUITheme::Dump()
{
	const FABTSUIThemeSnapshot T = Get();
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][UITheme] ThemeVersion=%d Frozen=%d PanelPrimary=%s PanelSecondary=%s PanelBorder=%s SlotBorder=%s SlotNormal=%s SlotHeld=%s SlotSelected=%s Disabled=%s Success=%s Warning=%s Danger=%s DangerFlash=%s AccentPrimary=%s AccentSecondary=%s TextPrimary=%s TextMuted=%s CountAccent=%s PortraitBacking=%s GlobalOpacity=%.3f BorderPx=%.2f CellInsetPx=%.2f HotbarSlotPx=%.2f InventoryRowPx=%.2f InventoryCellPx=%.2f RecipeRowPx=%.2f TextScale=%.3f"),
		FABTSUIThemeSnapshot::ThemeVersion, FABTSUIThemeSnapshot::bFrozen ? 1 : 0,
		*ToHex(T.PanelPrimary), *ToHex(T.PanelSecondary), *ToHex(T.PanelBorder), *ToHex(T.SlotBorder),
		*ToHex(T.SlotNormal), *ToHex(T.SlotHeld), *ToHex(T.SlotSelected), *ToHex(T.Disabled),
		*ToHex(T.Success), *ToHex(T.Warning), *ToHex(T.Danger), *ToHex(T.DangerFlash),
		*ToHex(T.AccentPrimary), *ToHex(T.AccentSecondary), *ToHex(T.TextPrimary), *ToHex(T.TextMuted),
		*ToHex(T.CountAccent), *ToHex(T.PortraitBacking), T.GlobalOpacity, T.BorderThicknessPx,
		T.CellInsetPx, T.HotbarSlotSizePx, T.InventoryRowHeightPx, T.InventoryCellHeightPx,
		T.RecipeRowHeightPx, T.TextScale);
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][UITheme] Startup example: -ExecCmds=\"abts.UI.Theme.PanelPrimary %s,abts.UI.Theme.AccentPrimary %s,abts.UI.Theme.AccentSecondary %s,abts.UI.Theme.BorderPx %.2f,abts.UI.Theme.Dump\""),
		*ToHex(T.PanelPrimary), *ToHex(T.AccentPrimary), *ToHex(T.AccentSecondary), T.BorderThicknessPx);
}

namespace
{
	void ExecuteThemeSetCommand(const TArray<FString>& Args)
	{
		if (Args.Num() != 2)
		{
			NotifyThemeCommand(TEXT("Usage: abts.UI.Theme.Set <Token> <Value>"), true);
			return;
		}
		FString Message;
		const bool bSucceeded = FABTSUITheme::SetToken(Args[0], Args[1], Message);
		NotifyThemeCommand(Message, !bSucceeded);
	}

	void ExecuteThemeResetCommand()
	{
		FABTSUITheme::ResetToDefaults();
		NotifyThemeCommand(TEXT("All tokens reset to frozen Theme v1 defaults (live)."));
	}

	void ExecuteThemeHelpCommand()
	{
		const FString Help = FABTSUITheme::GetHelp();
		UE_LOG(LogABTSRuntime, Display, TEXT("[ABTS][UITheme][Help] %s"), *Help);
		NotifyThemeCommand(TEXT("UI Theme help printed to Output Log. Enable DebugOverlay for live swatches."));
	}
}
