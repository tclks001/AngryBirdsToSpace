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

	TestEqual(TEXT("Action icon atlas path is frozen"),
		FString(FABTSM5InventoryHUDData::GetActionIconAtlasAssetPath()),
		FString(TEXT("/Game/UI/Icons/T_ABTS_ActionIconAtlas_v001.T_ABTS_ActionIconAtlas_v001")));
	TSet<FString> ActionIconUVs;
	for (int32 IconIndex = 0; IconIndex < static_cast<int32>(EABTSM5ActionIcon::Count); ++IconIndex)
	{
		FBox2D UV;
		TestTrue(TEXT("Every action icon resolves"), FABTSM5InventoryHUDData::GetActionIconUV(
			static_cast<EABTSM5ActionIcon>(IconIndex), UV));
		TestTrue(TEXT("Action icon UV stays in atlas"), UV.bIsValid
			&& UV.Min.X >= 0.0f && UV.Min.Y >= 0.0f
			&& UV.Max.X <= 1.0f && UV.Max.Y <= 1.0f);
		const FString UVKey = FString::Printf(TEXT("%.3f,%.3f,%.3f,%.3f"), UV.Min.X, UV.Min.Y, UV.Max.X, UV.Max.Y);
		TestTrue(TEXT("Action icon UV is unique"), !ActionIconUVs.Contains(UVKey));
		ActionIconUVs.Add(UVKey);
	}
	TestEqual(TEXT("All action icon cells are mapped"), ActionIconUVs.Num(),
		static_cast<int32>(EABTSM5ActionIcon::Count));
	FBox2D InvalidActionUV;
	TestFalse(TEXT("Unknown action icon fails closed"), FABTSM5InventoryHUDData::GetActionIconUV(
		static_cast<EABTSM5ActionIcon>(255), InvalidActionUV));

	FBox2D FittedBox;
	TestTrue(TEXT("Square icon fits a wide card"), FABTSM5InventoryHUDData::FitAspectRatio(
		FBox2D(FVector2D(10.0f, 20.0f), FVector2D(130.0f, 100.0f)),
		FVector2D(64.0f, 64.0f),
		FittedBox));
	TestTrue(TEXT("Wide-card fit remains square"), FMath::IsNearlyEqual(
		FittedBox.GetSize().X / FittedBox.GetSize().Y, 1.0f));
	TestTrue(TEXT("Wide-card fit is centered"), FittedBox.GetCenter().Equals(FVector2D(70.0f, 60.0f)));

	TestTrue(TEXT("Wide icon fits a tall card"), FABTSM5InventoryHUDData::FitAspectRatio(
		FBox2D(FVector2D(0.0f), FVector2D(80.0f, 120.0f)),
		FVector2D(128.0f, 64.0f),
		FittedBox));
	TestTrue(TEXT("Wide icon preserves source aspect"), FMath::IsNearlyEqual(
		FittedBox.GetSize().X / FittedBox.GetSize().Y, 2.0f));
	TestTrue(TEXT("Tall-card fit is centered"), FittedBox.GetCenter().Equals(FVector2D(40.0f, 60.0f)));
	TestFalse(TEXT("Invalid source dimensions fail closed"), FABTSM5InventoryHUDData::FitAspectRatio(
		FBox2D(FVector2D(0.0f), FVector2D(80.0f)), FVector2D(0.0f, 64.0f), FittedBox));
	return true;
}

#endif
