// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Party/ABTSBirdTypes.h"
#include "UI/ABTSCanvasUI.h"
#include "UI/ABTSM4PartyHUD.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSFlightUIContractTest,
	"ABTS.UI.Flight.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSFlightUIContractTest::RunTest(const FString& Parameters)
{
	TSet<FString> PortraitPaths;
	for (int32 BirdIndex = 0; BirdIndex < 4; ++BirdIndex)
	{
		const TCHAR* AssetPath = AABTSM4PartyHUD::GetBirdPortraitAssetPath(
			static_cast<EABTSBirdId>(BirdIndex));
		TestNotNull(FString::Printf(TEXT("Bird %d has a portrait path"), BirdIndex), AssetPath);
		if (AssetPath != nullptr)
		{
			const FString Path(AssetPath);
			TestTrue(FString::Printf(TEXT("Bird %d uses the shared portrait folder"), BirdIndex),
				Path.StartsWith(TEXT("/Game/Icons/Birds/T_Icon_Bird_")));
			PortraitPaths.Add(Path);
		}
	}
	TestEqual(TEXT("All four portrait bindings are unique"), PortraitPaths.Num(), 4);
	TestNull(TEXT("Invalid bird id fails closed"),
		AABTSM4PartyHUD::GetBirdPortraitAssetPath(static_cast<EABTSBirdId>(255)));

	const FBox2D Box(FVector2D(10.0f, 20.0f), FVector2D(110.0f, 80.0f));
	TStaticArray<FVector2D, 8> Vertices;
	FABTSCanvasUI::BuildFacetedVertices(Box, 12.0f, Vertices);
	TestEqual(TEXT("Faceted panel has eight vertices"), Vertices.Num(), 8);
	TestEqual(TEXT("Top-left cut begins on the top edge"), Vertices[0], FVector2D(22.0f, 20.0f));
	TestEqual(TEXT("Top-right cut ends on the right edge"), Vertices[2], FVector2D(110.0f, 32.0f));
	TestEqual(TEXT("Bottom-left cut ends on the left edge"), Vertices[7], FVector2D(10.0f, 32.0f));

	FABTSCanvasUI::BuildFacetedVertices(Box, 500.0f, Vertices);
	TestEqual(TEXT("Oversized cut is clamped to half the short edge"),
		Vertices[0], FVector2D(40.0f, 20.0f));
	return true;
}

#endif
