// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSM4PartyHUD.h"

#include "ABTSRuntime.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Guide/ABTSGuideWorldSubsystem.h"
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
	TAutoConsoleVariable<float> CVarGuideUIScale(
		TEXT("abts.Guide.UIScale"), 1.0f,
		TEXT("Manual multiplier applied after automatic 1280x720 guide DPI scaling [0.75, 1.5]."));
	TAutoConsoleVariable<float> CVarGuideBubbleWidthPx(
		TEXT("abts.Guide.BubbleWidthPx"), 500.0f,
		TEXT("Guide bubble baseline width at 1280x720 [440, 620]."));
	TAutoConsoleVariable<float> CVarGuideBubbleHeightPx(
		TEXT("abts.Guide.BubbleHeightPx"), 150.0f,
		TEXT("Guide bubble baseline height at 1280x720 [138, 190]."));
	TAutoConsoleVariable<float> CVarGuidePictogramPx(
		TEXT("abts.Guide.PictogramPx"), 92.0f,
		TEXT("Guide pictogram slot baseline size at 1280x720 [76, 118]."));

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
	DrawGuideOverlay(Theme);
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

void AABTSM4PartyHUD::DrawGuideOverlay(const FABTSUIThemeSnapshot& Theme)
{
	if (Canvas == nullptr || GEngine == nullptr || GetWorld() == nullptr) return;
	const UABTSGuideWorldSubsystem* GuideSubsystem = GetWorld()->GetSubsystem<UABTSGuideWorldSubsystem>();
	FABTSGuidePresentationSnapshot Guide;
	if (GuideSubsystem == nullptr || !GuideSubsystem->GetActiveGuide(Guide)) return;

	const float AutomaticScale = FMath::Min(Canvas->ClipX / 1280.0f, Canvas->ClipY / 720.0f);
	const float ManualScale = FMath::Clamp(CVarGuideUIScale.GetValueOnGameThread(), 0.75f, 1.5f);
	const float UIScale = FMath::Clamp(AutomaticScale * ManualScale, 0.80f, 1.45f);
	const float BubbleWidth = FMath::Clamp(
		CVarGuideBubbleWidthPx.GetValueOnGameThread(), 440.0f, 620.0f) * UIScale;
	const float BubbleHeight = FMath::Clamp(
		CVarGuideBubbleHeightPx.GetValueOnGameThread(), 138.0f, 190.0f) * UIScale;
	const float IconSize = FMath::Min(
		FMath::Clamp(CVarGuidePictogramPx.GetValueOnGameThread(), 76.0f, 118.0f) * UIScale,
		BubbleHeight - 28.0f * UIScale);
	const float ViewMargin = 24.0f * UIScale;
	FVector2D AnchorScreen(Canvas->ClipX * 0.5f, ViewMargin + BubbleHeight);
	bool bProjectedAnchor = false;
	FVector AnchorWorld = Guide.WorldLocation;
	if (AActor* AnchorActor = Guide.AnchorActor.Get())
	{
		FVector BoundsOrigin;
		FVector BoundsExtent;
		AnchorActor->GetActorBounds(true, BoundsOrigin, BoundsExtent);
		AnchorWorld = BoundsOrigin + AnchorActor->GetActorUpVector()
			* FMath::Max(10.0f, BoundsExtent.GetMax());
	}
	if ((Guide.AnchorActor.IsValid() || Guide.bHasWorldLocation) && PlayerOwner != nullptr)
	{
		bProjectedAnchor = PlayerOwner->ProjectWorldLocationToScreen(AnchorWorld, AnchorScreen, true);
	}

	FVector2D Origin(
		bProjectedAnchor ? AnchorScreen.X - BubbleWidth * 0.5f : (Canvas->ClipX - BubbleWidth) * 0.5f,
		bProjectedAnchor ? AnchorScreen.Y - BubbleHeight - 16.0f * UIScale : 36.0f * UIScale);
	Origin.X = FMath::Clamp(Origin.X, ViewMargin, FMath::Max(ViewMargin, Canvas->ClipX - BubbleWidth - ViewMargin));
	Origin.Y = FMath::Clamp(Origin.Y, ViewMargin, FMath::Max(ViewMargin, Canvas->ClipY - BubbleHeight - ViewMargin));
	const FBox2D BubbleBox(Origin, Origin + FVector2D(BubbleWidth, BubbleHeight));
	FABTSCanvasUI::DrawFacetedBox(*Canvas, Theme, BubbleBox,
		Theme.PanelPrimary, Theme.AccentPrimary, 13.0f * UIScale,
		FMath::Max(2.0f, Theme.BorderThicknessPx) * UIScale);

	if (bProjectedAnchor)
	{
		const FVector2D TailStart(
			FMath::Clamp(AnchorScreen.X,
				BubbleBox.Min.X + 35.0f * UIScale,
				BubbleBox.Max.X - 35.0f * UIScale),
			BubbleBox.Max.Y);
		DrawLine(TailStart.X, TailStart.Y, AnchorScreen.X, AnchorScreen.Y,
			Theme.ApplyOpacity(Theme.AccentPrimary), 2.8f * UIScale);
	}

	const float Padding = 14.0f * UIScale;
	const FBox2D IconBox(
		FVector2D(BubbleBox.Min.X + Padding, BubbleBox.GetCenter().Y - IconSize * 0.5f),
		FVector2D(BubbleBox.Min.X + Padding + IconSize, BubbleBox.GetCenter().Y + IconSize * 0.5f));
	DrawGuidePictogram(Guide, IconBox, Theme);
	const float TextX = IconBox.Max.X + 16.0f * UIScale;
	const FString StepText = FString::Printf(TEXT("P0  %d/%d"), Guide.StepNumber, Guide.TotalSteps);
	DrawText(StepText, Theme.ApplyOpacity(Theme.AccentSecondary),
		TextX, BubbleBox.Min.Y + 13.0f * UIScale,
		GEngine->GetSmallFont(), 0.72f * UIScale * Theme.TextScale, false);
	DrawText(Guide.Title.ToString(), Theme.ApplyOpacity(Theme.TextPrimary),
		TextX, BubbleBox.Min.Y + 38.0f * UIScale,
		GEngine->GetSmallFont(), 1.03f * UIScale * Theme.TextScale, false);
	DrawText(Guide.Body.ToString(), Theme.ApplyOpacity(Theme.TextMuted),
		TextX, BubbleBox.Min.Y + 76.0f * UIScale,
		GEngine->GetSmallFont(), 0.82f * UIScale * Theme.TextScale, false);
	DrawText(Guide.InputHint.ToString(), Theme.ApplyOpacity(Theme.AccentPrimary),
		TextX, BubbleBox.Min.Y + 116.0f * UIScale,
		GEngine->GetSmallFont(), 0.84f * UIScale * Theme.TextScale, false);
}

void AABTSM4PartyHUD::DrawGuidePictogram(
	const FABTSGuidePresentationSnapshot& Guide,
	const FBox2D& IconBox,
	const FABTSUIThemeSnapshot& Theme)
{
	if (Canvas == nullptr || GEngine == nullptr) return;
	const float Scale = IconBox.GetSize().X / 92.0f;
	const FLinearColor Primary = Theme.ApplyOpacity(Theme.AccentPrimary);
	const FLinearColor Secondary = Theme.ApplyOpacity(Theme.AccentSecondary);
	const FLinearColor Muted = Theme.ApplyOpacity(Theme.TextMuted);
	const FLinearColor Dark = Theme.ApplyOpacity(Theme.SlotBorder);
	FABTSCanvasUI::DrawFacetedBox(*Canvas, Theme, IconBox,
		Theme.PanelSecondary, Theme.PanelBorder, 8.0f * Scale, 1.5f * Scale);

	const auto Point = [&IconBox, Scale](const float X, const float Y)
	{
		return IconBox.Min + FVector2D(X, Y) * Scale;
	};
	const auto Stroke = [this, &Point, Scale](
		const float X0, const float Y0, const float X1, const float Y1,
		const FLinearColor& Color, const float Thickness = 2.4f)
	{
		const FVector2D A = Point(X0, Y0);
		const FVector2D B = Point(X1, Y1);
		DrawLine(A.X, A.Y, B.X, B.Y, Color, Thickness * Scale);
	};
	const auto Dot = [this, &Point, Scale](
		const float X, const float Y, const float Radius, const FLinearColor& Color)
	{
		Canvas->K2_DrawPolygon(Canvas->DefaultTexture, Point(X, Y),
			FVector2D(Radius * Scale), 18, Color);
	};
	const auto DrawStake = [&Stroke](const float X, const FLinearColor& Color)
	{
		Stroke(X, 28.0f, X, 71.0f, Color, 4.0f);
		Stroke(X, 28.0f, X - 7.0f, 19.0f, Color, 3.0f);
		Stroke(X, 28.0f, X + 7.0f, 18.0f, Color, 3.0f);
		Stroke(X - 8.0f, 72.0f, X + 8.0f, 72.0f, Color, 2.0f);
	};
	const auto DrawPouch = [&Stroke, &Dot, &Dark](const float X, const float Y, const FLinearColor& Color)
	{
		Dot(X, Y, 7.0f, Color);
		Stroke(X - 6.0f, Y - 2.0f, X + 6.0f, Y - 2.0f, Dark, 1.4f);
	};

	switch (Guide.Pictogram)
	{
	case EABTSGuidePictogram::CollectResources:
		Stroke(20.0f, 61.0f, 72.0f, 61.0f, Muted, 2.0f);
		Stroke(26.0f, 61.0f, 31.0f, 76.0f, Muted, 2.5f);
		Stroke(66.0f, 61.0f, 61.0f, 76.0f, Muted, 2.5f);
		Stroke(31.0f, 76.0f, 61.0f, 76.0f, Muted, 2.5f);
		Stroke(27.0f, 18.0f, 48.0f, 59.0f, Primary, 4.0f);
		Stroke(37.0f, 38.0f, 48.0f, 29.0f, Primary, 3.0f);
		Stroke(33.0f, 31.0f, 25.0f, 27.0f, Primary, 2.5f);
		Stroke(59.0f, 18.0f, 55.0f, 56.0f, Secondary, 2.4f);
		Stroke(66.0f, 21.0f, 61.0f, 56.0f, Secondary, 2.4f);
		Stroke(52.0f, 22.0f, 50.0f, 56.0f, Secondary, 2.4f);
		break;

	case EABTSGuidePictogram::InstallStakes:
		DrawStake(28.0f, Primary);
		DrawStake(64.0f, Primary);
		Stroke(14.0f, 72.0f, 78.0f, 72.0f, Muted, 2.0f);
		Stroke(28.0f, 7.0f, 28.0f, 16.0f, Secondary, 2.5f);
		Stroke(23.0f, 12.0f, 28.0f, 17.0f, Secondary, 2.5f);
		Stroke(33.0f, 12.0f, 28.0f, 17.0f, Secondary, 2.5f);
		Stroke(64.0f, 7.0f, 64.0f, 16.0f, Secondary, 2.5f);
		Stroke(59.0f, 12.0f, 64.0f, 17.0f, Secondary, 2.5f);
		Stroke(69.0f, 12.0f, 64.0f, 17.0f, Secondary, 2.5f);
		break;

	case EABTSGuidePictogram::ConnectFirst:
	case EABTSGuidePictogram::ConnectSecond:
		DrawStake(24.0f, Primary);
		DrawStake(68.0f, Guide.Pictogram == EABTSGuidePictogram::ConnectSecond ? Primary : Muted);
		DrawPouch(46.0f, 48.0f, Primary);
		Stroke(24.0f, 23.0f, 40.0f, 46.0f, Secondary, 3.0f);
		Stroke(52.0f, 46.0f, 68.0f, 23.0f,
			Guide.Pictogram == EABTSGuidePictogram::ConnectSecond ? Secondary : Muted, 3.0f);
		Dot(Guide.Pictogram == EABTSGuidePictogram::ConnectSecond ? 68.0f : 24.0f,
			23.0f, 4.0f, Secondary);
		break;

	case EABTSGuidePictogram::SwitchBird:
		Dot(43.0f, 45.0f, 23.0f, Secondary);
		Dot(35.0f, 40.0f, 2.2f, Dark);
		Dot(51.0f, 40.0f, 2.2f, Dark);
		Stroke(38.0f, 51.0f, 43.0f, 56.0f, Primary, 3.0f);
		Stroke(43.0f, 56.0f, 49.0f, 51.0f, Primary, 3.0f);
		Stroke(13.0f, 29.0f, 21.0f, 20.0f, Primary, 2.8f);
		Stroke(21.0f, 20.0f, 21.0f, 29.0f, Primary, 2.8f);
		DrawText(TEXT("2"), Primary, Point(66.0f, 57.0f).X, Point(66.0f, 57.0f).Y,
			GEngine->GetSmallFont(), 0.95f * Scale * Theme.TextScale, false);
		break;

	case EABTSGuidePictogram::EnterLaunch:
		DrawStake(24.0f, Muted);
		DrawStake(68.0f, Muted);
		DrawPouch(46.0f, 48.0f, Primary);
		Stroke(24.0f, 23.0f, 40.0f, 46.0f, Secondary, 3.0f);
		Stroke(52.0f, 46.0f, 68.0f, 23.0f, Secondary, 3.0f);
		Stroke(72.0f, 72.0f, 53.0f, 55.0f, Primary, 3.0f);
		Stroke(53.0f, 55.0f, 61.0f, 57.0f, Primary, 2.5f);
		Stroke(53.0f, 55.0f, 56.0f, 63.0f, Primary, 2.5f);
		break;

	case EABTSGuidePictogram::PullPouch:
		DrawStake(24.0f, Muted);
		DrawStake(68.0f, Muted);
		DrawPouch(46.0f, 66.0f, Primary);
		Stroke(24.0f, 23.0f, 40.0f, 63.0f, Secondary, 3.0f);
		Stroke(52.0f, 63.0f, 68.0f, 23.0f, Secondary, 3.0f);
		Stroke(46.0f, 40.0f, 46.0f, 55.0f, Primary, 2.8f);
		Stroke(40.0f, 50.0f, 46.0f, 56.0f, Primary, 2.8f);
		Stroke(52.0f, 50.0f, 46.0f, 56.0f, Primary, 2.8f);
		break;

	case EABTSGuidePictogram::AdjustPower:
		Canvas->K2_DrawBox(Point(29.0f, 14.0f), FVector2D(34.0f, 64.0f) * Scale,
			2.5f * Scale, Secondary);
		Stroke(29.0f, 39.0f, 63.0f, 39.0f, Secondary, 2.0f);
		Stroke(46.0f, 19.0f, 46.0f, 32.0f, Primary, 4.0f);
		Stroke(73.0f, 20.0f, 73.0f, 34.0f, Primary, 2.8f);
		Stroke(66.0f, 27.0f, 80.0f, 27.0f, Primary, 2.8f);
		Stroke(66.0f, 65.0f, 80.0f, 65.0f, Muted, 2.8f);
		break;

	case EABTSGuidePictogram::ReleaseLaunch:
		DrawStake(24.0f, Muted);
		DrawStake(68.0f, Muted);
		DrawPouch(46.0f, 59.0f, Primary);
		Stroke(24.0f, 23.0f, 40.0f, 57.0f, Secondary, 3.0f);
		Stroke(52.0f, 57.0f, 68.0f, 23.0f, Secondary, 3.0f);
		Stroke(46.0f, 49.0f, 46.0f, 10.0f, Primary, 3.2f);
		Stroke(39.0f, 18.0f, 46.0f, 10.0f, Primary, 3.2f);
		Stroke(53.0f, 18.0f, 46.0f, 10.0f, Primary, 3.2f);
		break;

	default:
		Dot(46.0f, 46.0f, 18.0f, Secondary);
		break;
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
