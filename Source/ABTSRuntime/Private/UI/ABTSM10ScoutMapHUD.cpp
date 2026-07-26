// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSM10ScoutMapHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Party/ABTSBirdParty.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "World/ABTSM10ScoutMapSystem.h"

void AABTSM10ScoutMapHUD::DrawHUD()
{
	// Draw first so the modal backpack naturally covers the minimap, while the
	// normal party portraits and hotbar remain layered above it.
	if (Canvas != nullptr)
	{
		if (AABTSM10ScoutMapSystem* System = FindScoutMapSystem(); System && System->IsScoutMapRevealed())
		{
			DrawScoutMap(*System);
		}
	}
	Super::DrawHUD();
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
