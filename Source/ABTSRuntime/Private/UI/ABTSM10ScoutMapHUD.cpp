// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSM10ScoutMapHUD.h"

#include "ABTSRuntime.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HighResScreenshot.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Party/ABTSBirdParty.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "UI/ABTSCanvasUI.h"
#include "UI/ABTSUITheme.h"
#include "World/ABTSM10ScoutMapSystem.h"

namespace
{
	TAutoConsoleVariable<float> CVarFlightPanelCutPx(
		TEXT("abts.UI.Flight.PanelCutPx"), 13.0f,
		TEXT("Cut-corner size for minimap and landing-preview panels [4, 28]."));
	TAutoConsoleVariable<float> CVarFlightPanelPaddingPx(
		TEXT("abts.UI.Flight.PanelPaddingPx"), 8.0f,
		TEXT("Image-to-frame padding for flight panels [3, 20]."));
	TAutoConsoleVariable<float> CVarFlightHeaderPx(
		TEXT("abts.UI.Flight.HeaderPx"), 27.0f,
		TEXT("Header rail height for flight panels [20, 46]."));
	TAutoConsoleVariable<float> CVarFlightTrajectoryGlowPx(
		TEXT("abts.UI.Flight.TrajectoryGlowPx"), 2.0f,
		TEXT("Dark underlay added around trajectory strokes [0, 6]."));

	void DumpM10FlightUISettings()
	{
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][FlightUI][M10] PanelCutPx=%.2f PanelPaddingPx=%.2f HeaderPx=%.2f TrajectoryGlowPx=%.2f"),
			CVarFlightPanelCutPx.GetValueOnGameThread(),
			CVarFlightPanelPaddingPx.GetValueOnGameThread(),
			CVarFlightHeaderPx.GetValueOnGameThread(),
			CVarFlightTrajectoryGlowPx.GetValueOnGameThread());
	}

	FAutoConsoleCommand DumpM10FlightUICommand(
		TEXT("abts.UI.Flight.M10.Dump"),
		TEXT("Print live minimap, PIP and trajectory frame settings."),
		FConsoleCommandDelegate::CreateStatic(&DumpM10FlightUISettings));
}

void AABTSM10ScoutMapHUD::DrawHUD()
{
	// Draw first so the modal backpack naturally covers the minimap, while the
	// normal party portraits and hotbar remain layered above it.
	if (Canvas != nullptr)
	{
		AABTSM10ScoutMapSystem* System = FindScoutMapSystem();
		UpdateOffscreenFlightUICapture(System);
		if (System && System->IsScoutMapRevealed())
		{
			DrawOrbitalOverview(*System);
			DrawScoutMap(*System);
			DrawLandingPreview(*System);
		}
		if (bFlightCaptureInitialized && FlightCaptureMode == TEXT("instruments"))
		{
			DrawFlightUIReferencePreview();
		}
	}
	Super::DrawHUD();
}

void AABTSM10ScoutMapHUD::UpdateOffscreenFlightUICapture(AABTSM10ScoutMapSystem* System)
{
	if (bFlightCaptureFinished || Canvas == nullptr) return;
	if (!bFlightCaptureParsed)
	{
		bFlightCaptureParsed = true;
		if (!FParse::Value(FCommandLine::Get(), TEXT("ABTSFlightUICapture="), FlightCaptureMode))
		{
			bFlightCaptureFinished = true;
			return;
		}
		FlightCaptureMode = FlightCaptureMode.ToLower();
		FParse::Value(FCommandLine::Get(), TEXT("ABTSFlightUICaptureOutput="), FlightCaptureOutputPath);
		if (FlightCaptureOutputPath.IsEmpty())
		{
			FlightCaptureOutputPath = FPaths::Combine(
				FPaths::ProjectSavedDir(), TEXT("FlightUI"), TEXT("FlightUI_Minimap.png"));
		}
		else if (FPaths::IsRelative(FlightCaptureOutputPath))
		{
			FlightCaptureOutputPath = FPaths::ConvertRelativePathToFull(
				FPaths::ProjectDir(), FlightCaptureOutputPath);
		}
		FlightCaptureOutputPath = FPaths::ConvertRelativePathToFull(FlightCaptureOutputPath);
		if (FlightCaptureMode != TEXT("minimap") && FlightCaptureMode != TEXT("instruments"))
		{
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][FlightUI][Capture] Rejected Mode=%s Expected=minimap|instruments"),
				*FlightCaptureMode);
			bFlightCaptureFinished = true;
			return;
		}
	}

	if (!bFlightCaptureInitialized)
	{
		AABTSBirdParty* ResolvedParty = FindScoutParty();
		if (System == nullptr || ResolvedParty == nullptr || !ResolvedParty->IsPartyReady()) return;
		FVector RevealOrigin = FVector::ZeroVector;
		bool bHasRevealOrigin = false;
		for (AABTSM25BirdCharacter* Bird : ResolvedParty->GetPartyMembers())
		{
			if (Bird != nullptr && !Bird->IsActorBeingDestroyed())
			{
				RevealOrigin = Bird->GetActorLocation();
				bHasRevealOrigin = true;
				break;
			}
		}
		if (!bHasRevealOrigin || !System->RevealForSlingshotCalibration(RevealOrigin)) return;
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(FlightCaptureOutputPath), true);
		bFlightCaptureInitialized = true;
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][FlightUI][Capture] Initialized Authority=PreviewTest Mode=%s Output=%s"),
			*FlightCaptureMode, *FlightCaptureOutputPath);
	}

	++FlightCaptureFrame;
	if (!bFlightCaptureRequested && FlightCaptureFrame >= 45)
	{
		if (FScreenshotRequest::IsScreenshotRequested()) return;
		FScreenshotRequest::RequestScreenshot(FlightCaptureOutputPath, false, false);
		bFlightCaptureRequested = FScreenshotRequest::IsScreenshotRequested();
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][FlightUI][Capture] Requested=%d Frame=%d Output=%s"),
			bFlightCaptureRequested ? 1 : 0, FlightCaptureFrame, *FlightCaptureOutputPath);
	}
	if (bFlightCaptureRequested
		&& !FScreenshotRequest::IsScreenshotRequested()
		&& IFileManager::Get().FileExists(*FlightCaptureOutputPath))
	{
		bFlightCaptureFinished = true;
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][FlightUI][Capture] Complete Success=1 Authority=PreviewTest Frame=%d Output=%s"),
			FlightCaptureFrame, *FlightCaptureOutputPath);
		FPlatformMisc::RequestExit(false);
	}
	else if (FlightCaptureFrame > 600)
	{
		bFlightCaptureFinished = true;
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][FlightUI][Capture] Complete Success=0 Reason=Timeout Output=%s"),
			*FlightCaptureOutputPath);
		FPlatformMisc::RequestExit(true);
	}
}

void AABTSM10ScoutMapHUD::DrawFlightUIReferencePreview()
{
	if (Canvas == nullptr || GEngine == nullptr) return;
	const FABTSUIThemeSnapshot Theme = FABTSUITheme::Get();
	const float Scale = FMath::Clamp(Canvas->ClipX / 1920.0f, 0.72f, 1.15f);
	const float Cut = 13.0f * Scale;

	// Explicit Preview/Test pixel fixture. It exercises the same frame, palette,
	// trajectory and terminal-marker vocabulary without publishing gameplay data.
	const FVector2D PipOrigin(Canvas->ClipX * 0.37f, 30.0f * Scale);
	const FVector2D PipSize(520.0f * Scale, 300.0f * Scale);
	const FBox2D PipBox(PipOrigin, PipOrigin + PipSize);
	FABTSCanvasUI::DrawFacetedBox(*Canvas, Theme, PipBox, Theme.PanelPrimary,
		Theme.AccentSecondary, Cut, Theme.BorderThicknessPx);
	const float HeaderY = PipOrigin.Y + 30.0f * Scale;
	DrawLine(PipOrigin.X + Cut, HeaderY, PipBox.Max.X - Cut, HeaderY,
		Theme.ApplyOpacity(Theme.PanelBorder), 1.0f);
	Canvas->K2_DrawPolygon(Canvas->DefaultTexture,
		PipOrigin + FVector2D(14.0f, 14.0f) * Scale,
		FVector2D(3.0f * Scale), 16, Theme.ApplyOpacity(Theme.AccentSecondary));
	DrawText(TEXT("SATELLITE LANDING PREVIEW  //  SIGNAL LOCK"),
		Theme.ApplyOpacity(Theme.TextPrimary), PipOrigin.X + 26.0f * Scale,
		PipOrigin.Y + 7.0f * Scale, GEngine->GetSmallFont(),
		0.72f * Theme.TextScale * Scale, false);
	const FBox2D PipImage(
		PipOrigin + FVector2D(8.0f, 38.0f) * Scale,
		PipBox.Max - FVector2D(8.0f));
	FABTSCanvasUI::DrawFacetedBox(*Canvas, Theme, PipImage, Theme.PortraitBacking,
		Theme.PanelBorder, 6.0f * Scale, 1.0f);
	FABTSCanvasUI::DrawCornerBrackets(*Canvas, Theme, PipImage,
		Theme.AccentSecondary, 20.0f * Scale, 6.0f * Scale, 1.5f);
	const float HorizonY = PipImage.Min.Y + PipImage.GetSize().Y * 0.68f;
	DrawLine(PipImage.Min.X + 4.0f, HorizonY, PipImage.Max.X - 4.0f, HorizonY,
		Theme.ApplyOpacity(Theme.TextMuted), 2.0f * Scale);
	DrawLine(PipImage.Min.X + 4.0f, HorizonY + 4.0f * Scale,
		PipImage.Max.X - 4.0f, HorizonY + 4.0f * Scale,
		Theme.ApplyOpacity(Theme.PanelBorder), 8.0f * Scale);
	const FVector2D TrajectoryStart = PipImage.Min + FVector2D(64.0f, 140.0f) * Scale;
	FVector2D Previous = TrajectoryStart;
	for (int32 Index = 1; Index <= 12; ++Index)
	{
		const float Alpha = static_cast<float>(Index) / 12.0f;
		const FVector2D Current(
			FMath::Lerp(TrajectoryStart.X, PipImage.Max.X - 74.0f * Scale, Alpha),
			PipImage.Min.Y + (132.0f - 86.0f * FMath::Sin(Alpha * PI)) * Scale);
		DrawLine(Previous.X, Previous.Y, Current.X, Current.Y,
			Theme.ApplyOpacity(Theme.SlotBorder), 4.0f * Scale);
		DrawLine(Previous.X, Previous.Y, Current.X, Current.Y,
			Theme.ApplyOpacity(Theme.AccentSecondary), 2.0f * Scale);
		Previous = Current;
	}
	Canvas->K2_DrawPolygon(Canvas->DefaultTexture, Previous, FVector2D(6.0f * Scale),
		4, Theme.ApplyOpacity(Theme.AccentPrimary));

	const float OrbitDiameter = 250.0f * Scale;
	const FVector2D OrbitOrigin(36.0f * Scale, Canvas->ClipY - OrbitDiameter - 72.0f * Scale);
	const FBox2D OrbitPanel(
		OrbitOrigin - FVector2D(8.0f * Scale),
		OrbitOrigin + FVector2D(OrbitDiameter + 8.0f * Scale, OrbitDiameter + 34.0f * Scale));
	FABTSCanvasUI::DrawFacetedBox(*Canvas, Theme, OrbitPanel, Theme.PanelPrimary,
		Theme.PanelBorder, Cut, 2.0f);
	const FVector2D OrbitCenter = OrbitOrigin + FVector2D(OrbitDiameter * 0.5f);
	const float OrbitRadius = OrbitDiameter * 0.5f;
	Canvas->K2_DrawPolygon(Canvas->DefaultTexture, OrbitCenter,
		FVector2D(OrbitRadius), 80, Theme.ApplyOpacity(Theme.AccentSecondary));
	Canvas->K2_DrawPolygon(Canvas->DefaultTexture, OrbitCenter,
		FVector2D(OrbitRadius - 4.0f * Scale), 80, Theme.ApplyOpacity(Theme.PortraitBacking));
	DrawLine(OrbitCenter.X - OrbitRadius + 10.0f * Scale, OrbitCenter.Y,
		OrbitCenter.X + OrbitRadius - 10.0f * Scale, OrbitCenter.Y,
		Theme.ApplyOpacity(Theme.PanelBorder), 1.0f);
	DrawLine(OrbitCenter.X, OrbitCenter.Y - OrbitRadius + 10.0f * Scale,
		OrbitCenter.X, OrbitCenter.Y + OrbitRadius - 10.0f * Scale,
		Theme.ApplyOpacity(Theme.PanelBorder), 1.0f);
	Canvas->K2_DrawPolygon(Canvas->DefaultTexture,
		OrbitCenter - FVector2D(32.0f, 4.0f) * Scale,
		FVector2D(25.0f * Scale), 48, Theme.ApplyOpacity(Theme.TextMuted));
	Canvas->K2_DrawPolygon(Canvas->DefaultTexture,
		OrbitCenter + FVector2D(47.0f, 11.0f) * Scale,
		FVector2D(12.0f * Scale), 32, Theme.ApplyOpacity(Theme.AccentSecondary));
	Previous = OrbitCenter + FVector2D(-92.0f, 55.0f) * Scale;
	for (int32 Index = 1; Index <= 24; ++Index)
	{
		const float Angle = FMath::Lerp(2.6f, -0.8f, static_cast<float>(Index) / 24.0f);
		const FVector2D Current = OrbitCenter + FVector2D(
			FMath::Cos(Angle) * 92.0f, FMath::Sin(Angle) * 62.0f) * Scale;
		DrawLine(Previous.X, Previous.Y, Current.X, Current.Y,
			Theme.ApplyOpacity(Theme.SlotBorder), 4.0f * Scale);
		DrawLine(Previous.X, Previous.Y, Current.X, Current.Y,
			Theme.ApplyOpacity(Theme.AccentSecondary), 2.0f * Scale);
		Previous = Current;
	}
	Canvas->K2_DrawPolygon(Canvas->DefaultTexture, Previous, FVector2D(5.5f * Scale),
		4, Theme.ApplyOpacity(Theme.AccentPrimary));
	DrawText(TEXT("ORBIT OVERVIEW  //  PREDICTED"), Theme.ApplyOpacity(Theme.TextMuted),
		OrbitOrigin.X + 10.0f * Scale, OrbitOrigin.Y + OrbitDiameter + 8.0f * Scale,
		GEngine->GetSmallFont(), 0.70f * Theme.TextScale * Scale, false);
}

void AABTSM10ScoutMapHUD::DrawLandingPreview(AABTSM10ScoutMapSystem& System)
{
	if (Canvas == nullptr || !System.IsLandingPreviewActive()) return;
	UTextureRenderTarget2D* RenderTarget = System.GetLandingPreviewRenderTarget();
	if (RenderTarget == nullptr) return;
	const FABTSM10ScoutMapSettings& Settings = System.GetSettings();
	const float RequestedWidth = FMath::Clamp(Settings.LandingViewScreenWidthPx, 180.0f, 1200.0f);
	const float RequestedHeight = FMath::Clamp(Settings.LandingViewScreenHeightPx, 100.0f, 700.0f);
	const float TopMargin = FMath::Clamp(Settings.LandingViewTopMarginPx, 0.0f, 400.0f);
	const float FramePadding = FMath::Clamp(CVarFlightPanelPaddingPx.GetValueOnGameThread(), 3.0f, 20.0f);
	const float HeaderHeight = FMath::Clamp(CVarFlightHeaderPx.GetValueOnGameThread(), 20.0f, 46.0f);
	const float AvailableHeight = FMath::Max(0.0f, Canvas->ClipY - TopMargin);
	const float MaxFrameWidth = FMath::Max(0.0f, Canvas->ClipX);
	const float MaxFrameHeight = AvailableHeight;
	const float RequestedFrameWidth = RequestedWidth + FramePadding * 2.0f;
	const float RequestedFrameHeight = RequestedHeight + FramePadding * 2.0f + HeaderHeight;
	float ScaleToFit = FMath::Min(1.0f, FMath::Min(
		MaxFrameWidth / FMath::Max(RequestedFrameWidth, 1.0f),
		MaxFrameHeight / FMath::Max(RequestedFrameHeight, 1.0f)));
	const float ScoutRight = FMath::Clamp(Settings.TopLeftMarginPx, 0.0f, 200.0f)
		+ FMath::Clamp(Settings.MapDiameterPx, 120.0f, 700.0f) + 12.0f;
	const float PreferredWidth = RequestedFrameWidth * ScaleToFit;
	const float RightSideWidth = FMath::Max(0.0f, Canvas->ClipX - ScoutRight);
	if (RightSideWidth > FramePadding * 2.0f && PreferredWidth > RightSideWidth)
	{
		ScaleToFit = FMath::Min(ScaleToFit, RightSideWidth / RequestedFrameWidth);
	}
	const float FrameWidth = RequestedFrameWidth * ScaleToFit;
	const float FrameHeight = RequestedFrameHeight * ScaleToFit;
	if (FrameWidth <= FramePadding * 2.0f || FrameHeight <= FramePadding * 2.0f) return;

	// Keep the preview clear of the fixed top-left scout map wherever the viewport
	// permits it; on narrow screens scale it down instead of covering the map.
	const float PreferredLeft = FMath::Max(0.0f, (Canvas->ClipX - FrameWidth) * 0.5f);
	const float SafeLeft = FMath::Min(FMath::Max(0.0f, ScoutRight), FMath::Max(0.0f, Canvas->ClipX - FrameWidth));
	const float FrameLeft = FMath::Max(PreferredLeft, SafeLeft);
	const FVector2D OuterSize(FrameWidth, FrameHeight);
	const FVector2D OuterOrigin(FrameLeft, TopMargin);
	const float ScaledPadding = FramePadding * ScaleToFit;
	const float ScaledHeaderHeight = HeaderHeight * ScaleToFit;
	const FVector2D ImageOrigin = OuterOrigin + FVector2D(ScaledPadding, ScaledPadding + ScaledHeaderHeight);
	const FVector2D ImageSize(
		FrameWidth - ScaledPadding * 2.0f,
		FrameHeight - ScaledPadding * 2.0f - ScaledHeaderHeight);
	const FABTSUIThemeSnapshot Theme = FABTSUITheme::Get();

	const float CutPx = FMath::Clamp(CVarFlightPanelCutPx.GetValueOnGameThread(), 4.0f, 28.0f) * ScaleToFit;
	const FBox2D OuterBox(OuterOrigin, OuterOrigin + OuterSize);
	FABTSCanvasUI::DrawFacetedBox(*Canvas, Theme, OuterBox, Theme.PanelPrimary,
		Theme.AccentSecondary, CutPx, Theme.BorderThicknessPx);
	const float HeaderY = OuterOrigin.Y + ScaledPadding + ScaledHeaderHeight - 2.0f * ScaleToFit;
	DrawLine(OuterOrigin.X + CutPx, HeaderY, OuterOrigin.X + FrameWidth - CutPx, HeaderY,
		Theme.ApplyOpacity(Theme.PanelBorder), 1.0f);
	Canvas->K2_DrawPolygon(Canvas->DefaultTexture,
		FVector2D(OuterOrigin.X + 13.0f * ScaleToFit, OuterOrigin.Y + 13.0f * ScaleToFit),
		FVector2D(3.0f * ScaleToFit), 16, Theme.ApplyOpacity(Theme.AccentSecondary));
	// FinalColorLDR guarantees the captured image in RGB, but its alpha is zero
	// on the default desktop tonemapper path when alpha propagation is disabled.
	// Canvas defaults to translucent blending, which would discard that valid RGB.
	Canvas->K2_DrawTexture(RenderTarget, ImageOrigin, ImageSize, FVector2D::ZeroVector,
		FVector2D::UnitVector, FLinearColor::White, BLEND_Opaque);
	Canvas->K2_DrawBox(ImageOrigin, ImageSize, 1.0f, Theme.ApplyOpacity(Theme.PanelBorder));
	FABTSCanvasUI::DrawCornerBrackets(*Canvas, Theme,
		FBox2D(ImageOrigin, ImageOrigin + ImageSize), Theme.AccentSecondary,
		16.0f * ScaleToFit, 4.0f * ScaleToFit, 1.5f);
	if (GEngine)
	{
		const FString PreviewLabel =
			System.IsSatelliteLandingPreviewActive()
				? TEXT("SATELLITE LANDING PREVIEW")
				: TEXT("LANDING PREVIEW");
		DrawText(PreviewLabel, Theme.ApplyOpacity(Theme.TextPrimary),
			OuterOrigin.X + 23.0f * ScaleToFit, OuterOrigin.Y + 6.0f * ScaleToFit,
			GEngine->GetSmallFont(), 0.76f * Theme.TextScale * ScaleToFit, false);
	}
}

void AABTSM10ScoutMapHUD::DrawScoutMap(AABTSM10ScoutMapSystem& System)
{
	UTexture2D* TerrainTexture = System.GetTerrainTexture();
	if (Canvas == nullptr || TerrainTexture == nullptr) return;
	const FABTSM10ScoutMapSettings& Settings = System.GetSettings();
	const float Diameter = FMath::Clamp(Settings.MapDiameterPx, 120.0f, 700.0f);
	const float Margin = FMath::Clamp(Settings.TopLeftMarginPx, 0.0f, 200.0f);
	const FVector2D Origin(Margin, Margin);
	const FVector2D Center = Origin + FVector2D(Diameter * 0.5f);
	const float Radius = Diameter * 0.5f;
	const FABTSUIThemeSnapshot Theme = FABTSUITheme::Get();
	const float PanelPadding = FMath::Clamp(CVarFlightPanelPaddingPx.GetValueOnGameThread(), 3.0f, 20.0f);
	const float PanelCut = FMath::Clamp(CVarFlightPanelCutPx.GetValueOnGameThread(), 4.0f, 28.0f);
	const FBox2D PanelBox(
		Origin - FVector2D(PanelPadding),
		Origin + FVector2D(Diameter + PanelPadding, Diameter + 34.0f));
	FABTSCanvasUI::DrawFacetedBox(*Canvas, Theme, PanelBox, Theme.PanelPrimary,
		Theme.PanelBorder, PanelCut, 2.0f);
	DrawLine(PanelBox.Min.X + PanelCut, PanelBox.Min.Y + 3.0f,
		PanelBox.Min.X + PanelCut + 56.0f, PanelBox.Min.Y + 3.0f,
		Theme.ApplyOpacity(Theme.AccentSecondary), 3.0f);

	Canvas->K2_DrawPolygon(Canvas->DefaultTexture, Center, FVector2D(Radius + 6.0f), 96,
		Theme.ApplyOpacity(Theme.AccentSecondary));
	Canvas->K2_DrawPolygon(Canvas->DefaultTexture, Center, FVector2D(Radius + 2.0f), 96,
		Theme.ApplyOpacity(Theme.PortraitBacking));
	Canvas->K2_DrawTexture(TerrainTexture, Origin, FVector2D(Diameter), FVector2D::ZeroVector,
		FVector2D::UnitVector, FLinearColor::White);

	for (const FABTSM10ScoutMapMarker& Marker : System.GetEnvironmentMarkers())
	{
		if (Marker.Type == EABTSM10ScoutMarkerType::Building) continue;
		const FVector2D MarkerCenter = Center + Marker.NormalizedMapPosition * Radius;
		switch (Marker.Type)
		{
		case EABTSM10ScoutMarkerType::Tree:
			DrawEnvironmentMarker(MarkerCenter, Settings.TreeIconTexture, Settings.TreeIconSizePx,
				FLinearColor(0.08f, 0.78f, 0.18f, 0.95f));
			break;
		case EABTSM10ScoutMarkerType::Stone:
			DrawEnvironmentMarker(MarkerCenter, Settings.StoneIconTexture, Settings.StoneIconSizePx,
				FLinearColor(0.72f, 0.74f, 0.78f, 0.95f));
			break;
		default:
			break;
		}
	}
	// Draw landmarks after dense resource icons so foliage can never hide them.
	for (const FABTSM10ScoutMapMarker& Marker : System.GetEnvironmentMarkers())
	{
		if (Marker.Type != EABTSM10ScoutMarkerType::Building) continue;
		const FVector2D MarkerCenter = Center + Marker.NormalizedMapPosition * Radius;
		DrawEnvironmentMarker(MarkerCenter, Settings.BuildingIconTexture, Settings.BuildingIconSizePx,
			FLinearColor(1.0f, 0.53f, 0.08f, 1.0f));
	}

	// Prediction is more transient and important than environment landmarks,
	// while live bird portraits remain the topmost minimap information layer.
	DrawTrajectoryPreview(System, Center, Radius);

	AABTSBirdParty* ResolvedParty = FindScoutParty();
	if (ResolvedParty != nullptr && ResolvedParty->IsPartyReady())
	{
		const float BirdSize = FMath::Clamp(Settings.BirdIconSizePx, 6.0f, 128.0f);
		for (AABTSM25BirdCharacter* Bird : ResolvedParty->GetPartyMembers())
		{
			if (Bird == nullptr || Bird->IsActorBeingDestroyed()) continue;
			FVector2D MapPosition;
			if (!System.ProjectWorldLocation(Bird->GetActorLocation(), MapPosition)) continue;
			const FABTSBirdPresentationConfig* Presentation = ResolvedParty->GetPresentation(Bird->GetBirdId());
			if (Presentation == nullptr) continue;
			const FVector2D BirdCenter = Center + MapPosition * Radius;
			const FBox2D MarkerBox(
				BirdCenter - FVector2D(BirdSize * 0.55f),
				BirdCenter + FVector2D(BirdSize * 0.55f));
			FABTSCanvasUI::DrawFacetedBox(*Canvas, Theme, MarkerBox, Theme.PortraitBacking,
				Bird->GetBirdId() == ResolvedParty->GetControlledBirdId()
					? Theme.AccentPrimary
					: Theme.AccentSecondary,
				3.0f, 1.4f);
			UTexture2D* Portrait = GetBirdPortraitTexture(Bird->GetBirdId());
			if (Portrait == nullptr) Portrait = Presentation->PortraitTexture;
			if (Portrait != nullptr)
			{
				FABTSCanvasUI::DrawTextureFitted(*Canvas, *Portrait,
					FBox2D(MarkerBox.Min + FVector2D(2.0f), MarkerBox.Max - FVector2D(2.0f)));
			}
			else
			{
				Canvas->K2_DrawPolygon(Canvas->DefaultTexture, BirdCenter, FVector2D(BirdSize * 0.42f), 24,
					Presentation->FallbackColor);
			}
		}
	}

	if (GEngine)
	{
		DrawText(TEXT("SCOUT MAP  //  LOCAL"), Theme.ApplyOpacity(Theme.TextMuted), Origin.X + 10.0f,
			Origin.Y + Diameter + 8.0f, GEngine->GetSmallFont(), 0.88f * Theme.TextScale, false);
	}
}

void AABTSM10ScoutMapHUD::DrawTrajectoryPreview(
	AABTSM10ScoutMapSystem& System,
	const FVector2D& MapCenter,
	const float MapRadius)
{
	if (Canvas == nullptr) return;
	const FABTSM10ScoutMapSettings& Settings = System.GetSettings();
	if (!Settings.bShowReinforcedTrajectoryPreview) return;
	const FABTSUIThemeSnapshot Theme = FABTSUITheme::Get();

	FABTSM6TrajectoryPreview Preview;
	if (!System.CopyCurrentTrajectoryPreview(Preview)
		|| Preview.SlingshotTier != EABTSSlingshotTier::Reinforced
		|| !Preview.bHasPrimarySurfaceLanding)
	{
		return;
	}

	// The landing governs visibility for the complete overlay. A trajectory may
	// briefly cross the scout cap, but that alone must not reveal an unknown aim.
	FVector2D LandingMapPosition;
	if (!System.ProjectWorldLocation(Preview.PrimarySurfaceLandingWorld, LandingMapPosition)) return;

	const float Thickness = FMath::Clamp(Settings.TrajectoryLineThicknessPx, 0.5f, 10.0f);
	const float DashLength = FMath::Clamp(Settings.TrajectoryDashLengthPx, 1.0f, 40.0f);
	const float GapLength = FMath::Clamp(Settings.TrajectoryGapLengthPx, 0.0f, 40.0f);
	const float GlowPx = FMath::Clamp(CVarFlightTrajectoryGlowPx.GetValueOnGameThread(), 0.0f, 6.0f);
	FVector2D PreviousScreenPosition = FVector2D::ZeroVector;
	bool bPreviousPointInsideMap = false;
	float DashPatternDistance = 0.0f;
	for (const FVector& WorldPoint : Preview.WorldPoints)
	{
		FVector2D MapPosition;
		const bool bPointInsideMap = System.ProjectWorldLocation(WorldPoint, MapPosition);
		if (bPointInsideMap)
		{
			const FVector2D ScreenPosition = MapCenter + MapPosition * MapRadius;
			// A large discontinuity indicates a spherical projection seam. Do not
			// draw a false chord across the scout disc.
			const bool bContinuousProjection = bPreviousPointInsideMap
				&& FVector2D::Distance(PreviousScreenPosition, ScreenPosition) <= MapRadius * 0.5f;
			if (bContinuousProjection)
			{
				float UnderlayPatternDistance = DashPatternDistance;
				DrawDashedMapSegment(PreviousScreenPosition, ScreenPosition,
					Theme.ApplyOpacity(Theme.SlotBorder), Thickness + GlowPx,
					DashLength, GapLength, UnderlayPatternDistance);
				DrawDashedMapSegment(PreviousScreenPosition, ScreenPosition,
					Theme.ApplyOpacity(Theme.AccentSecondary), Thickness, DashLength, GapLength,
					DashPatternDistance);
			}
			else
			{
				DashPatternDistance = 0.0f;
			}
			PreviousScreenPosition = ScreenPosition;
		}
		bPreviousPointInsideMap = bPointInsideMap;
	}

	const FVector2D LandingScreenPosition = MapCenter + LandingMapPosition * MapRadius;
	const float HalfCrossSize = FMath::Clamp(Settings.PredictedLandingCrossSizePx, 4.0f, 64.0f) * 0.5f;
	const float CrossThickness = FMath::Clamp(Settings.PredictedLandingCrossThicknessPx, 0.5f, 10.0f);
	const FVector2D DownRight(HalfCrossSize, HalfCrossSize);
	const FVector2D UpRight(HalfCrossSize, -HalfCrossSize);
	// Dark underlay keeps the focus marker legible over snow, road and cyan dashes.
	DrawLine(LandingScreenPosition.X - DownRight.X, LandingScreenPosition.Y - DownRight.Y,
		LandingScreenPosition.X + DownRight.X, LandingScreenPosition.Y + DownRight.Y,
		FLinearColor(0.02f, 0.01f, 0.01f, 0.95f), CrossThickness + 2.0f);
	DrawLine(LandingScreenPosition.X - UpRight.X, LandingScreenPosition.Y - UpRight.Y,
		LandingScreenPosition.X + UpRight.X, LandingScreenPosition.Y + UpRight.Y,
		FLinearColor(0.02f, 0.01f, 0.01f, 0.95f), CrossThickness + 2.0f);
	DrawLine(LandingScreenPosition.X - DownRight.X, LandingScreenPosition.Y - DownRight.Y,
		LandingScreenPosition.X + DownRight.X, LandingScreenPosition.Y + DownRight.Y,
		Theme.ApplyOpacity(Theme.AccentPrimary), CrossThickness);
	DrawLine(LandingScreenPosition.X - UpRight.X, LandingScreenPosition.Y - UpRight.Y,
		LandingScreenPosition.X + UpRight.X, LandingScreenPosition.Y + UpRight.Y,
		Theme.ApplyOpacity(Theme.AccentPrimary), CrossThickness);
}

void AABTSM10ScoutMapHUD::DrawDashedMapSegment(
	const FVector2D& Start,
	const FVector2D& End,
	const FLinearColor& Color,
	const float Thickness,
	const float DashLength,
	const float GapLength,
	float& InOutPatternDistance)
{
	const FVector2D Segment = End - Start;
	const float SegmentLength = Segment.Size();
	if (SegmentLength <= KINDA_SMALL_NUMBER) return;
	const FVector2D Direction = Segment / SegmentLength;
	const float Period = FMath::Max(DashLength + GapLength, 1.0f);
	float Cursor = 0.0f;
	while (Cursor < SegmentLength)
	{
		const float PatternPosition = FMath::Fmod(InOutPatternDistance, Period);
		const bool bInsideDash = PatternPosition < DashLength;
		const float RemainingPatternLength = bInsideDash
			? DashLength - PatternPosition
			: Period - PatternPosition;
		const float StepLength = FMath::Min(FMath::Max(RemainingPatternLength, 0.01f), SegmentLength - Cursor);
		if (bInsideDash)
		{
			const FVector2D DashStartPosition = Start + Direction * Cursor;
			const FVector2D DashEndPosition = Start + Direction * (Cursor + StepLength);
			DrawLine(DashStartPosition.X, DashStartPosition.Y, DashEndPosition.X, DashEndPosition.Y,
				Color, Thickness);
		}
		Cursor += StepLength;
		InOutPatternDistance = FMath::Fmod(InOutPatternDistance + StepLength, Period);
	}
}

void AABTSM10ScoutMapHUD::DrawEnvironmentMarker(
	const FVector2D& Center,
	UTexture2D* Texture,
	const float SizePx,
	const FLinearColor& FallbackColor) const
{
	const float SafeSize = FMath::Clamp(SizePx, 2.0f, 128.0f);
	const FVector2D Size(SafeSize);
	if (Texture)
	{
		Canvas->K2_DrawTexture(Texture, Center - Size * 0.5f, Size, FVector2D::ZeroVector,
			FVector2D::UnitVector, FLinearColor::White);
	}
	else
	{
		Canvas->K2_DrawPolygon(Canvas->DefaultTexture, Center, Size * 0.5f, 24, FallbackColor);
	}
}

AABTSM10ScoutMapSystem* AABTSM10ScoutMapHUD::FindScoutMapSystem()
{
	if (ScoutMapSystem.IsValid()) return ScoutMapSystem.Get();
	for (TActorIterator<AABTSM10ScoutMapSystem> It(GetWorld()); It; ++It)
	{
		ScoutMapSystem = *It;
		return ScoutMapSystem.Get();
	}
	return nullptr;
}

AABTSBirdParty* AABTSM10ScoutMapHUD::FindScoutParty()
{
	if (ScoutParty.IsValid()) return ScoutParty.Get();
	for (TActorIterator<AABTSBirdParty> It(GetWorld()); It; ++It)
	{
		ScoutParty = *It;
		return ScoutParty.Get();
	}
	return nullptr;
}
