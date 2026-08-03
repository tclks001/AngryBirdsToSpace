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
	bool HandleFinaleSecondaryPressed(
		AABTSM11FinaleInteractionSystem& System,
		const FVector2D& MousePosition);
	bool HandleFinaleSecondaryReleased(
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
	bool ResolveTargetPreviewLayout(
		const UTextureRenderTarget2D& RenderTarget,
		FVector2D& OutPosition,
		FVector2D& OutSize) const;
	void DrawTargetPreviewFrame(
		const FVector2D& Position,
		const FVector2D& Size,
		const FString& Title,
		const FString& Subtitle);
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
	void DrawPipEdgeIndicator(
		const FVector2D& Position,
		const FVector2D& Size,
		const FVector2d& PointUV,
		const FLinearColor& Color,
		const FString& Label);

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|HUD-1C|Controls",
		meta = (ClampMin = "0.25", ClampMax = "4.0"))
	double KnobDragSensitivity = 1.0;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|HUD-1C|Controls",
		meta = (ClampMin = "0.25", ClampMax = "4.0"))
	double KnobWheelSensitivity = 1.0;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|HUD-1C|Controls",
		meta = (ClampMin = "0.02", ClampMax = "0.08"))
	float KnobRadiusViewportHeightFraction = 0.044f;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|HUD-1C|Controls",
		meta = (ClampMin = "20.0", ClampMax = "80.0"))
	float MinimumKnobRadiusPixels = 30.0f;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|HUD-1C|Controls",
		meta = (ClampMin = "20.0", ClampMax = "100.0"))
	float MaximumKnobRadiusPixels = 42.0f;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|HUD-1C|Selection",
		meta = (ClampMin = "4.0", ClampMax = "40.0"))
	double TrajectoryHitRadiusPixels = 12.0;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|HUD-1C|Selection",
		meta = (ClampMin = "4.0", ClampMax = "48.0"))
	double TrajectoryScrubHitRadiusPixels = 16.0;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|HUD-1C|Move",
		meta = (ClampMin = "0.1", ClampMax = "4.0"))
	double OverviewPanSensitivity = 1.0;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|HUD-1C|Move",
		meta = (ClampMin = "0.02", ClampMax = "1.0"))
	double OverviewOrbitDegreesPerPixel = 0.22;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|HUD-1C|Move",
		meta = (ClampMin = "1.01", ClampMax = "1.5"))
	double OverviewZoomPerWheelStep = 1.12;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|HUD-1C|PIP",
		meta = (ClampMin = "0.02", ClampMax = "0.20"))
	double PipEdgeMarginUV = 0.065;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|HUD-1C|PIP",
		meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float PipReferenceLineThickness = 1.3f;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|HUD-1C|PIP",
		meta = (ClampMin = "0.5", ClampMax = "6.0"))
	float PipCurrentLineThickness = 2.6f;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|HUD-1C|PIP|Layout",
		meta = (ClampMin = "160.0", ClampMax = "960.0"))
	float PipMaximumWidthPixels = 420.0f;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|HUD-1C|PIP|Layout",
		meta = (ClampMin = "0.1", ClampMax = "0.8"))
	float PipViewportWidthFraction = 0.34f;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|HUD-1C|PIP|Layout",
		meta = (ClampMin = "90.0", ClampMax = "540.0"))
	float PipMaximumHeightPixels = 250.0f;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|HUD-1C|PIP|Layout",
		meta = (ClampMin = "0.1", ClampMax = "0.8"))
	float PipViewportHeightFraction = 0.30f;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|HUD-1C|PIP|Layout",
		meta = (ClampMin = "0.0", ClampMax = "240.0"))
	float PipTopMarginPixels = 24.0f;

	FABTSM11TargetPipTrajectory CachedPipTrajectory;
	FABTSM11TargetWedgeTracker TargetWedgeTracker;
	FABTSM11FinaleHudCaptureState HudCapture;
	EABTSM11ControlSpeedGear HudSpeedGear =
		EABTSM11ControlSpeedGear::Coarse;
	EABTSM11OverviewInteractionMode OverviewMode =
		EABTSM11OverviewInteractionMode::Select;
	FVector2D LastCapturedPointer = FVector2D::ZeroVector;
	FABTSM11TrajectoryHit PendingTrajectoryHit;
	FABTSM11TrajectoryHit HoveredTrajectoryHit;
	FVector2D HudDiagramCenter = FVector2D::ZeroVector;
	float HudDiagramRadius = 1.0f;
	TStaticArray<FVector2D, 3> HudKnobCenters;
	float HudKnobRadius = 38.0f;
	FBox2D HudGearCoarse;
	FBox2D HudGearFine;
	FBox2D HudGearUltraFine;
	FBox2D HudLaunchButton;
	FBox2D HudSelectButton;
	FBox2D HudMoveButton;
	FBox2D HudResetViewButton;
	FBox2D HudRebasePipButton;
	FBox2D HudFollowAutoButton;
	FVector2D HudPlayerViewOrigin = FVector2D::ZeroVector;
	FVector2D HudPlayerViewSize = FVector2D::ZeroVector;
	FVector2D HudCanvasSize = FVector2D::ZeroVector;
	bool bHudLayoutValid = false;
};
