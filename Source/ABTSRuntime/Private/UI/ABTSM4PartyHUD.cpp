// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSM4PartyHUD.h"

#include "ABTSRuntime.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Party/ABTSBirdParty.h"
#include "Party/ABTSBirdPartySettings.h"
#include "UI/ABTSUITheme.h"

FName AABTSM4PartyHUD::MakeBirdHitBoxName(const int32 BirdIndex) const
{
	return FName(*FString::Printf(TEXT("ABTS_Bird_%d"), BirdIndex));
}

void AABTSM4PartyHUD::DrawHUD()
{
	Super::DrawHUD();
	if (Canvas == nullptr) return;
	const FABTSUIThemeSnapshot Theme = FABTSUITheme::Get();
	DrawThemeDebugOverlay(Theme);
	AABTSBirdParty* ResolvedParty = FindParty();
	if (ResolvedParty == nullptr || !ResolvedParty->IsPartyReady()) return;

	const AABTSBirdPartySettings* Settings = ResolvedParty->GetResolvedSettings();
	const float Diameter = Settings ? Settings->PortraitDiameterPx : 72.0f;
	const float Gap = Settings ? Settings->PortraitGapPx : 18.0f;
	const float RightMargin = Settings ? Settings->RightMarginPx : 42.0f;
	const float TotalHeight = Diameter * 4.0f + Gap * 3.0f;
	const float X = Canvas->ClipX - RightMargin - Diameter;
	const float StartY = (Canvas->ClipY - TotalHeight) * 0.5f;
	UTexture2D* FillTexture = Canvas->DefaultTexture;

	for (int32 BirdIndex = 0; BirdIndex < 4; ++BirdIndex)
	{
		const EABTSBirdId BirdId = static_cast<EABTSBirdId>(BirdIndex);
		const FABTSBirdPresentationConfig* Presentation = ResolvedParty->GetPresentation(BirdId);
		if (Presentation == nullptr) continue;
		const FVector2D Center(X + Diameter * 0.5f, StartY + BirdIndex * (Diameter + Gap) + Diameter * 0.5f);
		const bool bControlled = ResolvedParty->GetControlledBirdId() == BirdId;
		if (bControlled)
		{
			Canvas->K2_DrawPolygon(FillTexture, Center, FVector2D(Diameter * 0.61f), 48, Theme.ApplyOpacity(Theme.AccentPrimary));
		}
		Canvas->K2_DrawPolygon(FillTexture, Center, FVector2D(Diameter * 0.53f), 48, Theme.ApplyOpacity(Theme.PortraitBacking));
		if (Presentation->PortraitTexture)
		{
			Canvas->K2_DrawPolygon(Presentation->PortraitTexture, Center, FVector2D(Diameter * 0.48f), 48, FLinearColor::White);
		}
		else
		{
			Canvas->K2_DrawPolygon(FillTexture, Center, FVector2D(Diameter * 0.48f), 48, Presentation->FallbackColor);
		}
		AddHitBox(FVector2D(X, Center.Y - Diameter * 0.5f), FVector2D(Diameter), MakeBirdHitBoxName(BirdIndex), true, 10);
	}

	if (GEngine)
	{
		DrawText(TEXT("TAB: Switch Bird"), Theme.ApplyOpacity(Theme.TextPrimary), X - 22.0f, StartY + TotalHeight + 20.0f, GEngine->GetSmallFont(), 0.9f * Theme.TextScale, false);
	}
}

void AABTSM4PartyHUD::DrawThemeDebugOverlay(const FABTSUIThemeSnapshot& Theme)
{
	if (!Theme.bDebugOverlay || Canvas == nullptr || GEngine == nullptr) return;

	struct FThemeDebugColor
	{
		const TCHAR* Name;
		FLinearColor Color;
	};
	const FThemeDebugColor Colors[] =
	{
		{ TEXT("PanelPrimary"), Theme.PanelPrimary },
		{ TEXT("PanelSecondary"), Theme.PanelSecondary },
		{ TEXT("PanelBorder"), Theme.PanelBorder },
		{ TEXT("SlotBorder"), Theme.SlotBorder },
		{ TEXT("SlotNormal"), Theme.SlotNormal },
		{ TEXT("SlotHeld"), Theme.SlotHeld },
		{ TEXT("SlotSelected"), Theme.SlotSelected },
		{ TEXT("Disabled"), Theme.Disabled },
		{ TEXT("Success"), Theme.Success },
		{ TEXT("Warning"), Theme.Warning },
		{ TEXT("Danger"), Theme.Danger },
		{ TEXT("DangerFlash"), Theme.DangerFlash },
		{ TEXT("AccentPrimary"), Theme.AccentPrimary },
		{ TEXT("AccentSecondary"), Theme.AccentSecondary },
		{ TEXT("TextPrimary"), Theme.TextPrimary },
		{ TEXT("TextMuted"), Theme.TextMuted },
		{ TEXT("CountAccent"), Theme.CountAccent },
		{ TEXT("PortraitBacking"), Theme.PortraitBacking },
	};

	const FVector2D Origin(24.0f, 24.0f);
	const FVector2D Size(748.0f, 286.0f);
	Canvas->K2_DrawTexture(Canvas->DefaultTexture, Origin, Size, FVector2D::ZeroVector,
		FVector2D::UnitVector, Theme.ApplyOpacity(Theme.PanelPrimary));
	Canvas->K2_DrawBox(Origin, Size, Theme.BorderThicknessPx, Theme.ApplyOpacity(Theme.AccentSecondary));
	DrawText(TEXT("UI THEME LIVE  |  Theme.Set <Token> <Value>  |  DebugOverlay 0 hides"),
		Theme.ApplyOpacity(Theme.TextPrimary), Origin.X + 14.0f, Origin.Y + 10.0f,
		GEngine->GetSmallFont(), 0.82f * Theme.TextScale, false);

	constexpr int32 RowsPerColumn = 9;
	constexpr float ColumnWidth = 360.0f;
	constexpr float RowHeight = 22.0f;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Colors); ++Index)
	{
		const int32 Column = Index / RowsPerColumn;
		const int32 Row = Index % RowsPerColumn;
		const float X = Origin.X + 14.0f + Column * ColumnWidth;
		const float Y = Origin.Y + 40.0f + Row * RowHeight;
		Canvas->K2_DrawTexture(Canvas->DefaultTexture, FVector2D(X, Y), FVector2D(18.0f),
			FVector2D::ZeroVector, FVector2D::UnitVector, Colors[Index].Color);
		Canvas->K2_DrawBox(FVector2D(X, Y), FVector2D(18.0f), 1.0f, Theme.ApplyOpacity(Theme.TextPrimary));
		DrawText(FString::Printf(TEXT("%s  %s"), Colors[Index].Name, *FABTSUITheme::ToHex(Colors[Index].Color)),
			Theme.ApplyOpacity(Theme.TextPrimary), X + 27.0f, Y + 1.0f,
			GEngine->GetSmallFont(), 0.70f * Theme.TextScale, false);
	}

	DrawText(FString::Printf(
		TEXT("Opacity %.2f  Border %.1f  Inset %.1f  Hotbar %.0f  Inv %.0f/%.0f  Recipe %.0f  Text %.2f"),
		Theme.GlobalOpacity, Theme.BorderThicknessPx, Theme.CellInsetPx, Theme.HotbarSlotSizePx,
		Theme.InventoryRowHeightPx, Theme.InventoryCellHeightPx, Theme.RecipeRowHeightPx, Theme.TextScale),
		Theme.ApplyOpacity(Theme.TextMuted), Origin.X + 14.0f, Origin.Y + Size.Y - 30.0f,
		GEngine->GetSmallFont(), 0.72f * Theme.TextScale, false);
}

void AABTSM4PartyHUD::NotifyHitBoxClick(const FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);
	AABTSBirdParty* ResolvedParty = FindParty();
	if (ResolvedParty == nullptr) return;
	for (int32 BirdIndex = 0; BirdIndex < 4; ++BirdIndex)
	{
		if (BoxName == MakeBirdHitBoxName(BirdIndex))
		{
			ResolvedParty->SwitchControlledBird(static_cast<EABTSBirdId>(BirdIndex));
			return;
		}
	}
}

AABTSBirdParty* AABTSM4PartyHUD::FindParty()
{
	if (Party.IsValid()) return Party.Get();
	for (TActorIterator<AABTSBirdParty> It(GetWorld()); It; ++It)
	{
		Party = *It;
		return Party.Get();
	}
	return nullptr;
}
