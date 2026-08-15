// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSM4PartyHUD.h"

#include "ABTSRuntime.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Party/ABTSBirdParty.h"
#include "Party/ABTSBirdPartySettings.h"
#include "UI/ABTSCanvasUI.h"
#include "UI/ABTSUITheme.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	TAutoConsoleVariable<float> CVarFlightPortraitCardPx(
		TEXT("abts.UI.Flight.PortraitCardPx"), 78.0f,
		TEXT("Bird portrait card size in pixels [52, 112]."));
	TAutoConsoleVariable<float> CVarFlightPortraitGapPx(
		TEXT("abts.UI.Flight.PortraitGapPx"), 10.0f,
		TEXT("Gap between bird portrait cards in pixels [4, 28]."));
	TAutoConsoleVariable<float> CVarFlightPortraitRightPx(
		TEXT("abts.UI.Flight.PortraitRightPx"), 30.0f,
		TEXT("Right viewport margin for the bird rail [8, 100]."));
	TAutoConsoleVariable<float> CVarFlightPortraitInsetPx(
		TEXT("abts.UI.Flight.PortraitInsetPx"), 8.0f,
		TEXT("Portrait image inset inside its card [3, 20]."));
	TAutoConsoleVariable<float> CVarFlightCutPx(
		TEXT("abts.UI.Flight.CutPx"), 11.0f,
		TEXT("Shared cut-corner size for flight HUD panels [4, 24]."));

	void DumpFlightUISettings()
	{
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][FlightUI] PortraitCardPx=%.2f PortraitGapPx=%.2f PortraitRightPx=%.2f PortraitInsetPx=%.2f CutPx=%.2f"),
			CVarFlightPortraitCardPx.GetValueOnGameThread(),
			CVarFlightPortraitGapPx.GetValueOnGameThread(),
			CVarFlightPortraitRightPx.GetValueOnGameThread(),
			CVarFlightPortraitInsetPx.GetValueOnGameThread(),
			CVarFlightCutPx.GetValueOnGameThread());
	}

	FAutoConsoleCommand DumpFlightUICommand(
		TEXT("abts.UI.Flight.Dump"),
		TEXT("Print the live flight HUD layout settings."),
		FConsoleCommandDelegate::CreateStatic(&DumpFlightUISettings));
}

const TCHAR* AABTSM4PartyHUD::GetBirdPortraitAssetPath(const EABTSBirdId BirdId)
{
	switch (BirdId)
	{
	case EABTSBirdId::Red: return TEXT("/Game/Icons/Birds/T_Icon_Bird_Red.T_Icon_Bird_Red");
	case EABTSBirdId::Blue: return TEXT("/Game/Icons/Birds/T_Icon_Bird_Blue.T_Icon_Bird_Blue");
	case EABTSBirdId::Yellow: return TEXT("/Game/Icons/Birds/T_Icon_Bird_Yellow.T_Icon_Bird_Yellow");
	case EABTSBirdId::Black: return TEXT("/Game/Icons/Birds/T_Icon_Bird_Black.T_Icon_Bird_Black");
	default: return nullptr;
	}
}

AABTSM4PartyHUD::AABTSM4PartyHUD()
{
	BirdPortraitTextures.SetNum(4);
	for (int32 BirdIndex = 0; BirdIndex < 4; ++BirdIndex)
	{
		const EABTSBirdId BirdId = static_cast<EABTSBirdId>(BirdIndex);
		const TCHAR* AssetPath = GetBirdPortraitAssetPath(BirdId);
		if (AssetPath == nullptr) continue;
		ConstructorHelpers::FObjectFinder<UTexture2D> PortraitFinder(AssetPath);
		if (PortraitFinder.Succeeded()) BirdPortraitTextures[BirdIndex] = PortraitFinder.Object;
	}
}

UTexture2D* AABTSM4PartyHUD::GetBirdPortraitTexture(const EABTSBirdId BirdId) const
{
	const int32 Index = static_cast<int32>(BirdId);
	return BirdPortraitTextures.IsValidIndex(Index) ? BirdPortraitTextures[Index] : nullptr;
}

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

	const float Diameter = FMath::Clamp(CVarFlightPortraitCardPx.GetValueOnGameThread(), 52.0f, 112.0f);
	const float Gap = FMath::Clamp(CVarFlightPortraitGapPx.GetValueOnGameThread(), 4.0f, 28.0f);
	const float RightMargin = FMath::Clamp(CVarFlightPortraitRightPx.GetValueOnGameThread(), 8.0f, 100.0f);
	const float ImageInset = FMath::Clamp(CVarFlightPortraitInsetPx.GetValueOnGameThread(), 3.0f, Diameter * 0.25f);
	const float CutPx = FMath::Clamp(CVarFlightCutPx.GetValueOnGameThread(), 4.0f, 24.0f);
	const float TotalHeight = Diameter * 4.0f + Gap * 3.0f;
	const float X = Canvas->ClipX - RightMargin - Diameter;
	const float StartY = (Canvas->ClipY - TotalHeight) * 0.5f;

	for (int32 BirdIndex = 0; BirdIndex < 4; ++BirdIndex)
	{
		const EABTSBirdId BirdId = static_cast<EABTSBirdId>(BirdIndex);
		const FABTSBirdPresentationConfig* Presentation = ResolvedParty->GetPresentation(BirdId);
		if (Presentation == nullptr) continue;
		const FVector2D CardOrigin(X, StartY + BirdIndex * (Diameter + Gap));
		const FBox2D CardBox(CardOrigin, CardOrigin + FVector2D(Diameter));
		const bool bControlled = ResolvedParty->GetControlledBirdId() == BirdId;
		const FLinearColor Border = bControlled ? Theme.AccentPrimary : Theme.PanelBorder;
		const FLinearColor Fill = bControlled ? Theme.SlotSelected : Theme.PanelSecondary;
		FABTSCanvasUI::DrawFacetedBox(*Canvas, Theme, CardBox, Theme.SlotBorder, Border,
			CutPx, bControlled ? Theme.BorderThicknessPx : 1.5f);
		const FBox2D InnerBox(CardBox.Min + FVector2D(3.0f), CardBox.Max - FVector2D(3.0f));
		FABTSCanvasUI::DrawFacetedBox(*Canvas, Theme, InnerBox, Fill, Fill,
			FMath::Max(3.0f, CutPx - 3.0f), 1.0f);
		const float RailX = CardBox.Min.X + 4.0f;
		DrawLine(RailX, CardBox.Min.Y + CutPx, RailX, CardBox.Max.Y - CutPx,
			Theme.ApplyOpacity(bControlled ? Theme.AccentPrimary : Theme.AccentSecondary),
			bControlled ? 3.0f : 1.0f);
		const FBox2D PortraitBox(
			CardBox.Min + FVector2D(ImageInset),
			CardBox.Max - FVector2D(ImageInset));
		UTexture2D* Portrait = GetBirdPortraitTexture(BirdId);
		if (Portrait == nullptr) Portrait = Presentation->PortraitTexture;
		if (Portrait != nullptr)
		{
			FABTSCanvasUI::DrawTextureFitted(*Canvas, *Portrait, PortraitBox);
		}
		else
		{
			Canvas->K2_DrawPolygon(Canvas->DefaultTexture, CardBox.GetCenter(),
				PortraitBox.GetSize() * 0.42f, 28, Presentation->FallbackColor);
		}
		if (GEngine)
		{
			DrawText(FString::FromInt(BirdIndex + 1), Theme.ApplyOpacity(Theme.TextMuted),
				CardBox.Max.X - 15.0f, CardBox.Min.Y + 5.0f,
				GEngine->GetSmallFont(), 0.60f * Theme.TextScale, false);
		}
		AddHitBox(CardBox.Min, CardBox.GetSize(), MakeBirdHitBoxName(BirdIndex), true, 10);
	}

	if (GEngine)
	{
		const FBox2D HintBox(
			FVector2D(X, StartY + TotalHeight + 12.0f),
			FVector2D(X + Diameter, StartY + TotalHeight + 38.0f));
		FABTSCanvasUI::DrawFacetedBox(*Canvas, Theme, HintBox, Theme.PanelPrimary,
			Theme.PanelBorder, 6.0f, 1.0f);
		DrawText(TEXT("TAB  SWITCH"), Theme.ApplyOpacity(Theme.TextMuted),
			HintBox.Min.X + 9.0f, HintBox.Min.Y + 5.0f,
			GEngine->GetSmallFont(), 0.62f * Theme.TextScale, false);
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
