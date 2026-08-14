// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "UI/ABTSUITheme.h"

#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSUIThemeContractTest,
	"ABTS.UI.Theme.FrozenContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSUIThemeContractTest::RunTest(const FString& Parameters)
{
	FLinearColor Parsed;
	TestTrue(TEXT("Eight-digit RGBA parses"), FABTSUITheme::TryParseHexColor(TEXT("F2BD4C80"), Parsed));
	const FColor ParsedSRGB = Parsed.ToFColorSRGB();
	TestEqual(TEXT("Parsed red byte"), ParsedSRGB.R, static_cast<uint8>(0xF2));
	TestEqual(TEXT("Parsed green byte"), ParsedSRGB.G, static_cast<uint8>(0xBD));
	TestEqual(TEXT("Parsed blue byte"), ParsedSRGB.B, static_cast<uint8>(0x4C));
	TestEqual(TEXT("Parsed alpha byte"), ParsedSRGB.A, static_cast<uint8>(0x80));
	TestTrue(TEXT("Hash-prefixed RGB parses with opaque alpha"),
		FABTSUITheme::TryParseHexColor(TEXT("#64D7E8"), Parsed));
	TestEqual(TEXT("Six-digit RGB becomes opaque"), Parsed.ToFColorSRGB().A, static_cast<uint8>(0xFF));
	TestFalse(TEXT("Malformed colors fail closed"), FABTSUITheme::TryParseHexColor(TEXT("navy"), Parsed));

	const FABTSUIThemeSnapshot Theme = FABTSUITheme::Get();
	TestEqual(TEXT("Frozen theme version is v1"), FABTSUIThemeSnapshot::ThemeVersion, 1);
	TestTrue(TEXT("Theme contract is explicitly frozen"), FABTSUIThemeSnapshot::bFrozen);
	TestTrue(TEXT("Global opacity is clamped"), Theme.GlobalOpacity >= 0.2f && Theme.GlobalOpacity <= 1.0f);
	TestTrue(TEXT("Cell height fits its row"), Theme.InventoryCellHeightPx <= Theme.InventoryRowHeightPx - 2.0f);
	TestTrue(TEXT("Hotbar remains usable"), Theme.HotbarSlotSizePx >= 52.0f && Theme.HotbarSlotSizePx <= 128.0f);

	IConsoleVariable* SlotNormalVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("abts.UI.Theme.SlotNormal"));
	TestNotNull(TEXT("SlotNormal live CVar is registered"), SlotNormalVariable);
	if (SlotNormalVariable)
	{
		const FString OriginalValue = SlotNormalVariable->GetString();
		FString Message;
		TestTrue(TEXT("Validated live setter accepts RGBA"),
			FABTSUITheme::SetToken(TEXT("SlotNormal"), TEXT("ABCDEF80"), Message));
		TestEqual(TEXT("Live setter is visible through the next snapshot"),
			FABTSUITheme::ToHex(FABTSUITheme::Get().SlotNormal), FString(TEXT("ABCDEF80")));
		TestTrue(TEXT("Live setter can restore the pre-test value"),
			FABTSUITheme::SetToken(TEXT("SlotNormal"), OriginalValue, Message));
	}
	FString RejectionMessage;
	TestFalse(TEXT("Validated live setter rejects malformed color"),
		FABTSUITheme::SetToken(TEXT("SlotNormal"), TEXT("not-a-color"), RejectionMessage));
	TestFalse(TEXT("Validated live setter rejects unknown token"),
		FABTSUITheme::SetToken(TEXT("Imaginary"), TEXT("1"), RejectionMessage));
	TestNotNull(TEXT("PIE debug overlay CVar is registered"),
		IConsoleManager::Get().FindConsoleVariable(TEXT("abts.UI.Theme.DebugOverlay")));
	return true;
}

#endif
