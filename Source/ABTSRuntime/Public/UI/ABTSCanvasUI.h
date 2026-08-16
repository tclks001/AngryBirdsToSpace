// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UCanvas;
class UTexture2D;
struct FABTSUIThemeSnapshot;

/** Small Canvas drawing vocabulary shared by the non-Slate runtime HUDs. */
class ABTSRUNTIME_API FABTSCanvasUI
{
public:
	static void BuildFacetedVertices(
		const FBox2D& Box,
		float CutPx,
		TStaticArray<FVector2D, 8>& OutVertices);

	static void DrawFacetedBox(
		UCanvas& Canvas,
		const FABTSUIThemeSnapshot& Theme,
		const FBox2D& Box,
		const FLinearColor& Fill,
		const FLinearColor& Border,
		float CutPx,
		float BorderPx);

	static void DrawCornerBrackets(
		UCanvas& Canvas,
		const FABTSUIThemeSnapshot& Theme,
		const FBox2D& Box,
		const FLinearColor& Color,
		float LengthPx,
		float InsetPx,
		float ThicknessPx);

	static void DrawTextureFitted(
		UCanvas& Canvas,
		UTexture2D& Texture,
		const FBox2D& Box,
		const FLinearColor& Tint = FLinearColor::White);
};
