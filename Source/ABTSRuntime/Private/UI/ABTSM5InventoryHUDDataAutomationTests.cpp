// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSM5InventoryHUDData.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM5InventoryHUDVisualLayoutTest,
	"ABTS.M5.UI.VisualLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM5InventoryHUDVisualLayoutTest::RunTest(const FString& Parameters)
{
	for (const FVector2D Viewport : { FVector2D(1024.0f, 600.0f), FVector2D(1280.0f, 720.0f), FVector2D(1920.0f, 1080.0f) })
	{
		FABTSM5InventoryUILayout Layout;
		TestTrue(TEXT("Supported viewport resolves"), FABTSM5InventoryHUDData::ResolveLayout(
			Viewport.X, Viewport.Y, 78.0f, 8, Layout));
		TestEqual(TEXT("All hotbar slots are present"), Layout.HotbarSlots.Num(), 8);
		TestTrue(TEXT("Hotbar stays inside viewport"), Layout.HotbarShell.Min.X >= 0.0f
			&& Layout.HotbarShell.Max.X <= Viewport.X
			&& Layout.HotbarShell.Max.Y <= Viewport.Y);
		TestTrue(TEXT("Inventory and recipe panels do not overlap"),
			Layout.InventoryPanel.Max.X < Layout.RecipePanel.Min.X);
		TestTrue(TEXT("Modal stays inside viewport"), Layout.ModalShell.Min.X >= 0.0f
			&& Layout.ModalShell.Min.Y >= 0.0f
			&& Layout.ModalShell.Max.X <= Viewport.X
			&& Layout.ModalShell.Max.Y <= Viewport.Y);
	}

	TSet<FString> IconPaths;
	for (const EABTSItemId ItemId : ABTSGetAllItemIds())
	{
		const TCHAR* Path = FABTSM5InventoryHUDData::GetItemIconAssetPath(ItemId);
		TestNotNull(TEXT("Every live item has an icon path"), Path);
		if (Path != nullptr)
		{
			TestTrue(TEXT("Icon path is unique"), !IconPaths.Contains(Path));
			IconPaths.Add(Path);
		}
	}
	TestEqual(TEXT("All live item icons are mapped"), IconPaths.Num(), ABTSGetAllItemIds().Num());
	TestNull(TEXT("Retired item has no production icon contract"),
		FABTSM5InventoryHUDData::GetItemIconAssetPath(EABTSItemId::SpaceSlingshotPart));
	return true;
}

#endif
