// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/ABTSM10ScoutMapHUD.h"
#include "UI/ABTSM11FinalePresentation.h"
#include "ABTSM11FinaleHUD.generated.h"

class AABTSM11FinaleInteractionSystem;

/** M10 HUD plus M11-C orbital explanation and target approach preview. */
UCLASS()
class ABTSRUNTIME_API AABTSM11FinaleHUD : public AABTSM10ScoutMapHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	AABTSM11FinaleInteractionSystem* FindInteractionSystem() const;
	void DrawFinaleLayer(AABTSM11FinaleInteractionSystem& System);
	void DrawOrbitalDiagram(
		AABTSM11FinaleInteractionSystem& System,
		const FVector2D& Center,
		float Radius);
	void DrawTargetPreview(
		AABTSM11FinaleInteractionSystem& System);
	void DrawTargetWedge(
		AABTSM11FinaleInteractionSystem& System);
	void DrawFailureOverlay(
		AABTSM11FinaleInteractionSystem& System);
	void DrawStatus(
		AABTSM11FinaleInteractionSystem& System,
		const FVector2D& DiagramCenter,
		float DiagramRadius);
	void DrawDiagramSegment(
		const FVector2D& Center,
		float Radius,
		const FVector2d& NormalizedStart,
		const FVector2d& NormalizedEnd,
		const FLinearColor& Color,
		float Thickness,
		bool bDashed);
	void DrawDiagramCircleOutline(
		const FVector2D& PanelCenter,
		float PanelRadius,
		const FVector2d& NormalizedCenter,
		double NormalizedRadius,
		const FLinearColor& Color,
		float Thickness,
		int32 SegmentCount = 48);
	void DrawCircleOutline(
		const FVector2D& Center,
		float Radius,
		const FLinearColor& Color,
		float Thickness,
		int32 SegmentCount = 48);
	void DrawPlanetGlyph(
		int32 AssistIndex,
		const FVector2D& PanelCenter,
		float PanelRadius,
		const FVector2d& NormalizedCenter,
		double NormalizedRadius,
		const FLinearColor& Color);
	void DrawUFOGlyph(
		const FVector2D& PanelCenter,
		float PanelRadius,
		const FVector2d& NormalizedCenter,
		double NormalizedRadius,
		const FLinearColor& Color);

	FABTSM11TargetPipTrajectory CachedPipTrajectory;
	FABTSM11TargetWedgeTracker TargetWedgeTracker;
};
