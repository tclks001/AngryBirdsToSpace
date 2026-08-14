// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSCanvasUI.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "UI/ABTSUITheme.h"

void FABTSCanvasUI::BuildFacetedVertices(
	const FBox2D& Box,
	const float CutPx,
	TStaticArray<FVector2D, 8>& OutVertices)
{
	const float Cut = FMath::Clamp(
		CutPx,
		0.0f,
		FMath::Max(0.0f, FMath::Min(Box.GetSize().X, Box.GetSize().Y) * 0.5f));
	OutVertices = {
		FVector2D(Box.Min.X + Cut, Box.Min.Y),
		FVector2D(Box.Max.X - Cut, Box.Min.Y),
		FVector2D(Box.Max.X, Box.Min.Y + Cut),
		FVector2D(Box.Max.X, Box.Max.Y - Cut),
		FVector2D(Box.Max.X - Cut, Box.Max.Y),
		FVector2D(Box.Min.X + Cut, Box.Max.Y),
		FVector2D(Box.Min.X, Box.Max.Y - Cut),
		FVector2D(Box.Min.X, Box.Min.Y + Cut)
	};
}

void FABTSCanvasUI::DrawFacetedBox(
	UCanvas& Canvas,
	const FABTSUIThemeSnapshot& Theme,
	const FBox2D& Box,
	const FLinearColor& Fill,
	const FLinearColor& Border,
	const float CutPx,
	const float BorderPx)
{
	if (!Box.bIsValid || Box.GetSize().GetMin() <= 0.0f) return;
	TStaticArray<FVector2D, 8> Vertices;
	BuildFacetedVertices(Box, CutPx, Vertices);
	const FTexture* FillTexture = Canvas.DefaultTexture != nullptr
		? Canvas.DefaultTexture->GetResource()
		: nullptr;
	if (FillTexture != nullptr)
	{
		TArray<FCanvasUVTri> Triangles;
		Triangles.Reserve(Vertices.Num());
		const FVector2D Center = Box.GetCenter();
		const FLinearColor ResolvedFill = Theme.ApplyOpacity(Fill);
		for (int32 Index = 0; Index < Vertices.Num(); ++Index)
		{
			FCanvasUVTri& Triangle = Triangles.AddDefaulted_GetRef();
			Triangle.V0_Pos = Center;
			Triangle.V1_Pos = Vertices[Index];
			Triangle.V2_Pos = Vertices[(Index + 1) % Vertices.Num()];
			Triangle.V0_UV = FVector2D::ZeroVector;
			Triangle.V1_UV = FVector2D::ZeroVector;
			Triangle.V2_UV = FVector2D::ZeroVector;
			Triangle.V0_Color = ResolvedFill;
			Triangle.V1_Color = ResolvedFill;
			Triangle.V2_Color = ResolvedFill;
		}
		FCanvasTriangleItem FillItem(Triangles, FillTexture);
		FillItem.BlendMode = SE_BLEND_Translucent;
		Canvas.DrawItem(FillItem);
	}
	for (int32 Index = 0; Index < Vertices.Num(); ++Index)
	{
		Canvas.K2_DrawLine(
			Vertices[Index],
			Vertices[(Index + 1) % Vertices.Num()],
			BorderPx,
			Theme.ApplyOpacity(Border));
	}
}

void FABTSCanvasUI::DrawCornerBrackets(
	UCanvas& Canvas,
	const FABTSUIThemeSnapshot& Theme,
	const FBox2D& Box,
	const FLinearColor& Color,
	const float LengthPx,
	const float InsetPx,
	const float ThicknessPx)
{
	if (!Box.bIsValid) return;
	const float Length = FMath::Clamp(LengthPx, 1.0f, Box.GetSize().GetMin() * 0.35f);
	const FVector2D Min = Box.Min + FVector2D(InsetPx);
	const FVector2D Max = Box.Max - FVector2D(InsetPx);
	const FLinearColor Resolved = Theme.ApplyOpacity(Color);
	Canvas.K2_DrawLine(Min, Min + FVector2D(Length, 0.0f), ThicknessPx, Resolved);
	Canvas.K2_DrawLine(Min, Min + FVector2D(0.0f, Length), ThicknessPx, Resolved);
	Canvas.K2_DrawLine(FVector2D(Max.X, Min.Y), FVector2D(Max.X - Length, Min.Y), ThicknessPx, Resolved);
	Canvas.K2_DrawLine(FVector2D(Max.X, Min.Y), FVector2D(Max.X, Min.Y + Length), ThicknessPx, Resolved);
	Canvas.K2_DrawLine(FVector2D(Min.X, Max.Y), FVector2D(Min.X + Length, Max.Y), ThicknessPx, Resolved);
	Canvas.K2_DrawLine(FVector2D(Min.X, Max.Y), FVector2D(Min.X, Max.Y - Length), ThicknessPx, Resolved);
	Canvas.K2_DrawLine(Max, Max - FVector2D(Length, 0.0f), ThicknessPx, Resolved);
	Canvas.K2_DrawLine(Max, Max - FVector2D(0.0f, Length), ThicknessPx, Resolved);
}

void FABTSCanvasUI::DrawTextureFitted(
	UCanvas& Canvas,
	UTexture2D& Texture,
	const FBox2D& Box,
	const FLinearColor& Tint)
{
	if (!Box.bIsValid || Texture.GetSizeX() <= 0 || Texture.GetSizeY() <= 0) return;
	const FVector2D Available = Box.GetSize();
	const float Scale = FMath::Min(
		Available.X / static_cast<float>(Texture.GetSizeX()),
		Available.Y / static_cast<float>(Texture.GetSizeY()));
	const FVector2D DrawSize(
		static_cast<float>(Texture.GetSizeX()) * Scale,
		static_cast<float>(Texture.GetSizeY()) * Scale);
	Canvas.K2_DrawTexture(
		&Texture,
		Box.GetCenter() - DrawSize * 0.5f,
		DrawSize,
		FVector2D::ZeroVector,
		FVector2D::UnitVector,
		Tint,
		BLEND_Translucent);
}
