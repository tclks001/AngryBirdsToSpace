// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSM10ScoutMapHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Party/ABTSBirdParty.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "World/ABTSM10ScoutMapSystem.h"

void AABTSM10ScoutMapHUD::DrawHUD()
{
	// Draw first so the modal backpack naturally covers the minimap, while the
	// normal party portraits and hotbar remain layered above it.
	if (Canvas != nullptr)
	{
		if (AABTSM10ScoutMapSystem* System = FindScoutMapSystem(); System && System->IsScoutMapRevealed())
		{
			DrawOrbitalOverview(*System);
			DrawScoutMap(*System);
			DrawLandingPreview(*System);
		}
	}
	Super::DrawHUD();
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
	const float FramePadding = 6.0f;
	const float AvailableHeight = FMath::Max(0.0f, Canvas->ClipY - TopMargin);
	const float MaxFrameWidth = FMath::Max(0.0f, Canvas->ClipX);
	const float MaxFrameHeight = AvailableHeight;
	const float RequestedFrameWidth = RequestedWidth + FramePadding * 2.0f;
	const float RequestedFrameHeight = RequestedHeight + FramePadding * 2.0f;
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
	const FVector2D ImageOrigin = OuterOrigin + FVector2D(ScaledPadding, ScaledPadding);
	const FVector2D ImageSize(FrameWidth - ScaledPadding * 2.0f, FrameHeight - ScaledPadding * 2.0f);

	Canvas->K2_DrawTexture(Canvas->DefaultTexture, OuterOrigin, OuterSize, FVector2D::ZeroVector,
		FVector2D::UnitVector, FLinearColor(0.006f, 0.012f, 0.023f, 0.94f));
	Canvas->K2_DrawBox(OuterOrigin, OuterSize, 2.0f, FLinearColor(0.60f, 0.82f, 1.0f, 0.96f));
	// FinalColorLDR guarantees the captured image in RGB, but its alpha is zero
	// on the default desktop tonemapper path when alpha propagation is disabled.
	// Canvas defaults to translucent blending, which would discard that valid RGB.
	Canvas->K2_DrawTexture(RenderTarget, ImageOrigin, ImageSize, FVector2D::ZeroVector,
		FVector2D::UnitVector, FLinearColor::White, BLEND_Opaque);
	Canvas->K2_DrawBox(ImageOrigin, ImageSize, 1.0f, FLinearColor(0.05f, 0.08f, 0.14f, 0.95f));
	if (GEngine)
	{
		const FString PreviewLabel =
			System.IsSatelliteLandingPreviewActive()
				? TEXT("SATELLITE LANDING PREVIEW")
				: TEXT("LANDING PREVIEW");
		DrawText(PreviewLabel, FLinearColor(0.89f, 0.95f, 1.0f),
			OuterOrigin.X + 10.0f, OuterOrigin.Y + 7.0f, GEngine->GetSmallFont(), 0.82f, false);
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

	Canvas->K2_DrawPolygon(Canvas->DefaultTexture, Center, FVector2D(Radius + 6.0f), 96,
		FLinearColor(0.82f, 0.90f, 1.0f, 0.96f));
	Canvas->K2_DrawPolygon(Canvas->DefaultTexture, Center, FVector2D(Radius + 2.0f), 96,
		FLinearColor(0.018f, 0.022f, 0.032f, 0.98f));
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
			Canvas->K2_DrawPolygon(Canvas->DefaultTexture, BirdCenter, FVector2D(BirdSize * 0.58f), 36,
				FLinearColor(0.015f, 0.015f, 0.02f, 0.96f));
			if (Presentation->PortraitTexture)
			{
				Canvas->K2_DrawPolygon(Presentation->PortraitTexture, BirdCenter, FVector2D(BirdSize * 0.48f), 36,
					FLinearColor::White);
			}
			else
			{
				Canvas->K2_DrawPolygon(Canvas->DefaultTexture, BirdCenter, FVector2D(BirdSize * 0.48f), 36,
					Presentation->FallbackColor);
			}
		}
	}

	if (GEngine)
	{
		DrawText(TEXT("SCOUT MAP"), FLinearColor(0.88f, 0.94f, 1.0f), Origin.X + 10.0f,
			Origin.Y + Diameter + 8.0f, GEngine->GetSmallFont(), 0.88f, false);
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
				DrawDashedMapSegment(PreviousScreenPosition, ScreenPosition,
					FLinearColor(0.96f, 0.98f, 1.0f, 0.95f), Thickness, DashLength, GapLength,
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
	// Dark underlay keeps the marker legible over snow, road and white dashes.
	DrawLine(LandingScreenPosition.X - DownRight.X, LandingScreenPosition.Y - DownRight.Y,
		LandingScreenPosition.X + DownRight.X, LandingScreenPosition.Y + DownRight.Y,
		FLinearColor(0.02f, 0.01f, 0.01f, 0.95f), CrossThickness + 2.0f);
	DrawLine(LandingScreenPosition.X - UpRight.X, LandingScreenPosition.Y - UpRight.Y,
		LandingScreenPosition.X + UpRight.X, LandingScreenPosition.Y + UpRight.Y,
		FLinearColor(0.02f, 0.01f, 0.01f, 0.95f), CrossThickness + 2.0f);
	DrawLine(LandingScreenPosition.X - DownRight.X, LandingScreenPosition.Y - DownRight.Y,
		LandingScreenPosition.X + DownRight.X, LandingScreenPosition.Y + DownRight.Y,
		FLinearColor(1.0f, 0.035f, 0.025f, 1.0f), CrossThickness);
	DrawLine(LandingScreenPosition.X - UpRight.X, LandingScreenPosition.Y - UpRight.Y,
		LandingScreenPosition.X + UpRight.X, LandingScreenPosition.Y + UpRight.Y,
		FLinearColor(1.0f, 0.035f, 0.025f, 1.0f), CrossThickness);
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
