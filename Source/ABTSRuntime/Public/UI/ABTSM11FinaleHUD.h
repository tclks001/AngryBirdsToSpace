// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/ABTSM10ScoutMapHUD.h"
#include "UI/ABTSM11FinaleHUDData.h"
#include "UI/ABTSM11FinalePresentation.h"
#include "ABTSM11FinaleHUD.generated.h"

class AABTSM11FinaleInteractionSystem;
class UTextureRenderTarget2D;

/** M10 HUD plus M11-C orbital explanation and target approach preview. */
UCLASS()
class ABTSRUNTIME_API AABTSM11FinaleHUD : public AABTSM10ScoutMapHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
	bool HandleFinalePrimaryPressed(
		AABTSM11FinaleInteractionSystem& System,
		const FVector2D& MousePosition);
	bool HandleFinalePrimaryReleased(
		AABTSM11FinaleInteractionSystem& System,
		const FVector2D& MousePosition);
	bool HandleFinalePrimaryDoubleClicked(
		AABTSM11FinaleInteractionSystem& System,
		const FVector2D& MousePosition);
	bool HandleFinalePointerMoved(
		AABTSM11FinaleInteractionSystem& System,
		const FVector2D& MousePosition);
	bool HandleFinaleWheel(
		AABTSM11FinaleInteractionSystem& System,
		const FVector2D& MousePosition,
		double WheelSteps);
	void CancelFinaleHudCapture();
	EABTSM11FinaleHudCapture GetFinaleHudCapture() const
	{
		return HudCapture.GetCapture();
	}

private:
	AABTSM11FinaleInteractionSystem* FindInteractionSystem() const;
	void DrawFinaleLayer(AABTSM11FinaleInteractionSystem& System);
	void DrawFinaleControlConsole(
		AABTSM11FinaleInteractionSystem& System);
	void DrawOrbitalDiagram(
		AABTSM11FinaleInteractionSystem& System,
		const FVector2D& Center,
		float Radius);
	void DrawTargetPreview(
		AABTSM11FinaleInteractionSystem& System);
	void DrawProbeTargetPreview(
		AABTSM11FinaleInteractionSystem& System,
		UTextureRenderTarget2D& RenderTarget);
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
	void UpdateFinaleHudLayout(float Width, float Height);
	FVector2D ToHudCanvasPosition(
		const FVector2D& ViewportPosition) const;
	bool IsInside(const FVector2D& Point, const FBox2D& Box) const;
	bool IsInsideDiagram(const FVector2D& Point) const;
	int32 FindKnobAt(const FVector2D& Point) const;
	void DrawConsoleButton(
		const FBox2D& Box,
		const FString& Label,
		bool bActive,
		const FLinearColor& Accent);
	void DrawKnob(
		const FVector2D& Center,
		float Radius,
		const FString& Label,
		double ValueAlpha,
		const FString& ValueText,
		bool bCaptured);

	FABTSM11TargetPipTrajectory CachedPipTrajectory;
	FABTSM11TargetWedgeTracker TargetWedgeTracker;
	FABTSM11FinaleHudCaptureState HudCapture;
	EABTSM11ControlSpeedGear HudSpeedGear =
		EABTSM11ControlSpeedGear::Coarse;
	EABTSM11OverviewInteractionMode OverviewMode =
		EABTSM11OverviewInteractionMode::Select;
	FVector2D LastCapturedPointer = FVector2D::ZeroVector;
	FABTSM11TrajectoryHit PendingTrajectoryHit;
	FVector2D HudDiagramCenter = FVector2D::ZeroVector;
	float HudDiagramRadius = 1.0f;
	TStaticArray<FVector2D, 3> HudKnobCenters;
	float HudKnobRadius = 38.0f;
	FBox2D HudGearCoarse;
	FBox2D HudGearFine;
	FBox2D HudGearUltraFine;
	FBox2D HudLaunchButton;
	FBox2D HudSelectButton;
	FBox2D HudRotateButton;
	FBox2D HudResetViewButton;
	FBox2D HudRebasePipButton;
	FBox2D HudFollowAutoButton;
	FVector2D HudCanvasSize = FVector2D::ZeroVector;
	bool bHudLayoutValid = false;
};
