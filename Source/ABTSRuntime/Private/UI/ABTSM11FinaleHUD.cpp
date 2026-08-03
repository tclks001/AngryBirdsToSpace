// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSM11FinaleHUD.h"

#include "CanvasItem.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Game/ABTSM11GameMode.h"
#include "GameFramework/PlayerController.h"
#include "SceneView.h"
#include "World/ABTSM11FinaleInteractionSystem.h"
#include "World/ABTSM11FinaleSystem.h"

namespace
{
	FVector2D ToScreen(
		const FVector2D& Center,
		const float Radius,
		const FVector2d& Normalized)
	{
		return Center + FVector2D(
			static_cast<float>(Normalized.X) * Radius,
			static_cast<float>(-Normalized.Y) * Radius);
	}

	FLinearColor SegmentColor(
		const EABTSM11PlaybackSegmentKind Kind)
	{
		switch (Kind)
		{
		case EABTSM11PlaybackSegmentKind::VisibleTerminalTransfer:
			return FLinearColor(1.0f, 0.58f, 0.12f, 1.0f);
		case EABTSM11PlaybackSegmentKind::CertifiedNominalTail:
			return FLinearColor(0.25f, 0.85f, 1.0f, 1.0f);
		default:
			return FLinearColor(0.88f, 0.96f, 1.0f, 1.0f);
		}
	}

	const TCHAR* TargetLabel(const EABTSM11PreviewTarget Target)
	{
		switch (Target)
		{
		case EABTSM11PreviewTarget::Assist1: return TEXT("PLANET 1");
		case EABTSM11PreviewTarget::Assist2: return TEXT("PLANET 2");
		case EABTSM11PreviewTarget::Assist3: return TEXT("PLANET 3");
		default: return TEXT("UFO");
		}
	}

	FLinearColor M11TargetColor(
		const EABTSM11PreviewTarget Target)
	{
		switch (Target)
		{
		case EABTSM11PreviewTarget::Assist1:
			return FLinearColor(1.0f, 0.34f, 0.20f, 1.0f);
		case EABTSM11PreviewTarget::Assist2:
			return FLinearColor(1.0f, 0.72f, 0.20f, 1.0f);
		case EABTSM11PreviewTarget::Assist3:
			return FLinearColor(0.70f, 0.50f, 1.0f, 1.0f);
		default:
			return FLinearColor(0.42f, 1.0f, 0.82f, 1.0f);
		}
	}
}

void AABTSM11FinaleHUD::DrawHUD()
{
	AABTSM11FinaleInteractionSystem* System =
		FindInteractionSystem();
	if (System != nullptr && System->IsFinaleActive())
	{
		DrawFinaleLayer(*System);
	}
	else
	{
		CachedPipTrajectory.Reset();
		TargetWedgeTracker.Reset();
		CancelFinaleHudCapture();
		bHudLayoutValid = false;
	}
	// Inventory, party and modal UI remain the top layer.
	Super::DrawHUD();
	// The deterministic failure fade is the final compositing layer so no
	// inherited inventory or party widget can remain visible through black.
	if (System != nullptr && System->IsFinaleActive())
	{
		DrawFailureOverlay(*System);
	}
}

AABTSM11FinaleInteractionSystem*
AABTSM11FinaleHUD::FindInteractionSystem() const
{
	const AABTSM11GameMode* GameMode =
		GetWorld() != nullptr
		? Cast<AABTSM11GameMode>(GetWorld()->GetAuthGameMode())
		: nullptr;
	return GameMode != nullptr
		? GameMode->GetFinaleInteractionSystem()
		: nullptr;
}

void AABTSM11FinaleHUD::UpdateFinaleHudLayout(
	const float Width,
	const float Height)
{
	HudCanvasSize = FVector2D(Width, Height);
	HudDiagramRadius = FMath::Min(170.0f, Height * 0.18f);
	HudDiagramCenter = FVector2D(
		HudDiagramRadius + 22.0f,
		Height - HudDiagramRadius - 110.0f);
	HudKnobRadius = FMath::Clamp(Height * 0.044f, 30.0f, 42.0f);
	const float KnobY = Height - 180.0f;
	const float KnobSpacing = HudKnobRadius * 2.75f;
	const float KnobStartX = Width * 0.5f - KnobSpacing;
	for (int32 Index = 0; Index < HudKnobCenters.Num(); ++Index)
	{
		HudKnobCenters[Index] = FVector2D(
			KnobStartX + KnobSpacing * Index,
			KnobY);
	}
	const auto Box = [](const float X, const float Y, const float W, const float H)
	{
		return FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H));
	};
	const float GearY = Height - 118.0f;
	HudGearCoarse = Box(Width * 0.5f - 185.0f, GearY, 62.0f, 28.0f);
	HudGearFine = Box(Width * 0.5f - 117.0f, GearY, 62.0f, 28.0f);
	HudGearUltraFine = Box(Width * 0.5f - 49.0f, GearY, 62.0f, 28.0f);
	HudLaunchButton = Box(Width * 0.5f + 38.0f, GearY - 4.0f, 148.0f, 36.0f);
	const float ModeX = HudDiagramCenter.X + HudDiagramRadius + 12.0f;
	const float ModeY = HudDiagramCenter.Y - HudDiagramRadius + 18.0f;
	HudSelectButton = Box(ModeX, ModeY, 82.0f, 27.0f);
	HudMoveButton = Box(ModeX, ModeY + 33.0f, 82.0f, 27.0f);
	HudResetViewButton = Box(ModeX, ModeY + 66.0f, 82.0f, 27.0f);
	HudRebasePipButton = Box(ModeX, ModeY + 99.0f, 82.0f, 27.0f);
	HudFollowAutoButton = Box(ModeX, ModeY + 132.0f, 82.0f, 27.0f);
	bHudLayoutValid = Width > 1.0f && Height > 1.0f;
}

FVector2D AABTSM11FinaleHUD::ToHudCanvasPosition(
	const FVector2D& ViewportPosition) const
{
	return ABTSM11MapViewportPointToHudCanvas(
		ViewportPosition,
		HudPlayerViewOrigin,
		HudPlayerViewSize,
		HudCanvasSize);
}

bool AABTSM11FinaleHUD::IsInside(
	const FVector2D& Point,
	const FBox2D& Box) const
{
	return Box.bIsValid && Box.IsInside(Point);
}

bool AABTSM11FinaleHUD::IsInsideDiagram(
	const FVector2D& Point) const
{
	return bHudLayoutValid
		&& FVector2D::Distance(Point, HudDiagramCenter)
			<= HudDiagramRadius;
}

int32 AABTSM11FinaleHUD::FindKnobAt(
	const FVector2D& Point) const
{
	for (int32 Index = 0; Index < HudKnobCenters.Num(); ++Index)
	{
		if (FVector2D::Distance(Point, HudKnobCenters[Index])
			<= HudKnobRadius + 8.0f)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool AABTSM11FinaleHUD::HandleFinalePrimaryPressed(
	AABTSM11FinaleInteractionSystem& System,
	const FVector2D& MousePosition)
{
	if (!System.IsAiming())
	{
		return false;
	}
	if (!bHudLayoutValid)
	{
		int32 Width = 0;
		int32 Height = 0;
		if (APlayerController* Controller = GetOwningPlayerController())
		{
			Controller->GetViewportSize(Width, Height);
		}
		HudPlayerViewOrigin = FVector2D::ZeroVector;
		HudPlayerViewSize = FVector2D(
			static_cast<float>(Width),
			static_cast<float>(Height));
		UpdateFinaleHudLayout(
			static_cast<float>(Width),
			static_cast<float>(Height));
	}
	const FVector2D HudPosition = ToHudCanvasPosition(MousePosition);
	if (HudCapture.GetCapture() != EABTSM11FinaleHudCapture::None)
	{
		return true;
	}

	const int32 KnobIndex = FindKnobAt(HudPosition);
	if (KnobIndex != INDEX_NONE)
	{
		const EABTSM11FinaleHudCapture Capture = KnobIndex == 0
			? EABTSM11FinaleHudCapture::AdjustYaw
			: KnobIndex == 1
				? EABTSM11FinaleHudCapture::AdjustPitch
				: EABTSM11FinaleHudCapture::AdjustPower;
		LastCapturedPointer = HudPosition;
		return HudCapture.TryBegin(Capture);
	}
	if (IsInside(HudPosition, HudGearCoarse))
	{
		HudSpeedGear = EABTSM11ControlSpeedGear::Coarse;
		return true;
	}
	if (IsInside(HudPosition, HudGearFine))
	{
		HudSpeedGear = EABTSM11ControlSpeedGear::Fine;
		return true;
	}
	if (IsInside(HudPosition, HudGearUltraFine))
	{
		HudSpeedGear = EABTSM11ControlSpeedGear::UltraFine;
		return true;
	}
	if (IsInside(HudPosition, HudSelectButton))
	{
		OverviewMode = EABTSM11OverviewInteractionMode::Select;
		return true;
	}
	if (IsInside(HudPosition, HudMoveButton))
	{
		OverviewMode = EABTSM11OverviewInteractionMode::Move;
		return true;
	}
	if (IsInside(HudPosition, HudResetViewButton))
	{
		if (OverviewMode == EABTSM11OverviewInteractionMode::Move)
		{
			System.ResetHudOverview();
		}
		return true;
	}
	if (IsInside(HudPosition, HudRebasePipButton))
	{
		System.RebaseHudTrajectoryProbe();
		return true;
	}
	if (IsInside(HudPosition, HudFollowAutoButton))
	{
		System.FollowAutomaticPreviewTarget();
		return true;
	}
	if (IsInside(HudPosition, HudLaunchButton))
	{
		LastCapturedPointer = HudPosition;
		return HudCapture.TryBeginLaunch();
	}
	if (IsInsideDiagram(HudPosition))
	{
		LastCapturedPointer = HudPosition;
		if (OverviewMode == EABTSM11OverviewInteractionMode::Move)
		{
			return HudCapture.TryBegin(
				EABTSM11FinaleHudCapture::PanOverview);
		}
		PendingTrajectoryHit = FABTSM11TrajectoryHit();
		if (!ABTSM11HitTestOverviewTrajectory(
			System.GetHudOverviewProjection(),
			FVector2D(
				HudPosition.X,
				HudDiagramCenter.Y * 2.0f - HudPosition.Y),
			HudDiagramCenter,
			HudDiagramRadius,
			10.0,
			PendingTrajectoryHit,
			System.HasHudTrajectoryProbe()
				? System.GetHudTrajectoryProbe().Leg
				: EABTSM11TrajectorySemanticLeg::Invalid))
		{
			return true;
		}
		return HudCapture.TryBegin(
			EABTSM11FinaleHudCapture::ScrubTrajectoryProbe);
	}
	return true;
}

bool AABTSM11FinaleHUD::HandleFinaleSecondaryPressed(
	AABTSM11FinaleInteractionSystem& System,
	const FVector2D& MousePosition)
{
	if (!System.IsAiming()
		|| OverviewMode != EABTSM11OverviewInteractionMode::Move)
	{
		return false;
	}
	if (!bHudLayoutValid)
	{
		int32 Width = 0;
		int32 Height = 0;
		if (APlayerController* Controller = GetOwningPlayerController())
		{
			Controller->GetViewportSize(Width, Height);
		}
		HudPlayerViewOrigin = FVector2D::ZeroVector;
		HudPlayerViewSize = FVector2D(
			static_cast<float>(Width),
			static_cast<float>(Height));
		UpdateFinaleHudLayout(
			static_cast<float>(Width),
			static_cast<float>(Height));
	}
	const FVector2D HudPosition = ToHudCanvasPosition(MousePosition);
	if (!IsInsideDiagram(HudPosition)
		|| HudCapture.GetCapture()
			!= EABTSM11FinaleHudCapture::None)
	{
		return false;
	}
	LastCapturedPointer = HudPosition;
	return HudCapture.TryBegin(
		EABTSM11FinaleHudCapture::RotateOverview);
}

bool AABTSM11FinaleHUD::HandleFinalePointerMoved(
	AABTSM11FinaleInteractionSystem& System,
	const FVector2D& MousePosition)
{
	if (!System.IsAiming())
	{
		CancelFinaleHudCapture();
		return false;
	}
	const FVector2D HudPosition = ToHudCanvasPosition(MousePosition);
	const FVector2D Delta = HudPosition - LastCapturedPointer;
	LastCapturedPointer = HudPosition;
	switch (HudCapture.GetCapture())
	{
	case EABTSM11FinaleHudCapture::AdjustYaw:
		return System.ApplyHudControlDrag(
			EABTSM11FinaleControlAxis::Yaw,
			(Delta.X - Delta.Y) * 0.5,
			HudSpeedGear);
	case EABTSM11FinaleHudCapture::AdjustPitch:
		return System.ApplyHudControlDrag(
			EABTSM11FinaleControlAxis::Pitch,
			(Delta.X - Delta.Y) * 0.5,
			HudSpeedGear);
	case EABTSM11FinaleHudCapture::AdjustPower:
		return System.ApplyHudControlDrag(
			EABTSM11FinaleControlAxis::Power,
			(Delta.X - Delta.Y) * 0.5,
			HudSpeedGear);
	case EABTSM11FinaleHudCapture::PanOverview:
		return System.PanHudOverview(FVector2d(
			Delta.X / FMath::Max(HudDiagramRadius, 1.0f),
			Delta.Y / FMath::Max(HudDiagramRadius, 1.0f)));
	case EABTSM11FinaleHudCapture::RotateOverview:
		return System.RotateHudOverview(
			Delta.X * 0.25,
			Delta.Y * 0.25);
	case EABTSM11FinaleHudCapture::ScrubTrajectoryProbe:
		return ABTSM11HitTestOverviewTrajectory(
			System.GetHudOverviewProjection(),
			FVector2D(
				HudPosition.X,
				HudDiagramCenter.Y * 2.0f - HudPosition.Y),
			HudDiagramCenter,
			HudDiagramRadius,
			12.0,
			PendingTrajectoryHit,
			PendingTrajectoryHit.Leg);
	default:
		return HudCapture.GetCapture()
			!= EABTSM11FinaleHudCapture::None;
	}
}

bool AABTSM11FinaleHUD::HandleFinalePrimaryReleased(
	AABTSM11FinaleInteractionSystem& System,
	const FVector2D& MousePosition)
{
	const FVector2D HudPosition = ToHudCanvasPosition(MousePosition);
	const EABTSM11FinaleHudCapture Capture = HudCapture.GetCapture();
	if (Capture == EABTSM11FinaleHudCapture::None)
	{
		return System.IsFinaleActive();
	}
	// Right-button rotation owns its own release path. A coincident left-click
	// must not terminate the secondary-button capture.
	if (Capture == EABTSM11FinaleHudCapture::RotateOverview)
	{
		return true;
	}
	if (Capture == EABTSM11FinaleHudCapture::ScrubTrajectoryProbe
		&& PendingTrajectoryHit.bValid)
	{
		System.SelectHudTrajectoryProbe(PendingTrajectoryHit);
	}
	else if (ABTSM11ShouldCommitFinaleHudLaunch(
		Capture,
		IsInside(HudPosition, HudLaunchButton),
		System.IsAiming()))
	{
		System.RequestRelease();
	}
	HudCapture.End(Capture);
	PendingTrajectoryHit = FABTSM11TrajectoryHit();
	return true;
}

bool AABTSM11FinaleHUD::HandleFinaleSecondaryReleased(
	AABTSM11FinaleInteractionSystem& System,
	const FVector2D& MousePosition)
{
	if (!System.IsFinaleActive())
	{
		CancelFinaleHudCapture();
		return false;
	}
	if (HudCapture.GetCapture()
		!= EABTSM11FinaleHudCapture::RotateOverview)
	{
		return false;
	}
	HudCapture.End(EABTSM11FinaleHudCapture::RotateOverview);
	return true;
}

bool AABTSM11FinaleHUD::HandleFinalePrimaryDoubleClicked(
	AABTSM11FinaleInteractionSystem& System,
	const FVector2D& MousePosition)
{
	if (!System.IsAiming())
	{
		return false;
	}
	const FVector2D HudPosition = ToHudCanvasPosition(MousePosition);
	const int32 KnobIndex = FindKnobAt(HudPosition);
	if (KnobIndex != INDEX_NONE)
	{
		CancelFinaleHudCapture();
		return System.ResetHudControlAxis(
			static_cast<EABTSM11FinaleControlAxis>(KnobIndex));
	}
	if (IsInsideDiagram(HudPosition)
		&& OverviewMode == EABTSM11OverviewInteractionMode::Move)
	{
		return System.ResetHudOverview();
	}
	return true;
}

bool AABTSM11FinaleHUD::HandleFinaleWheel(
	AABTSM11FinaleInteractionSystem& System,
	const FVector2D& MousePosition,
	const double WheelSteps)
{
	if (!System.IsAiming() || FMath::IsNearlyZero(WheelSteps))
	{
		return false;
	}
	const FVector2D HudPosition = ToHudCanvasPosition(MousePosition);
	const int32 KnobIndex = FindKnobAt(HudPosition);
	if (KnobIndex != INDEX_NONE)
	{
		return System.ApplyHudControlWheel(
			static_cast<EABTSM11FinaleControlAxis>(KnobIndex),
			WheelSteps,
			HudSpeedGear);
	}
	if (IsInsideDiagram(HudPosition)
		&& OverviewMode == EABTSM11OverviewInteractionMode::Move)
	{
		return System.ZoomHudOverview(
			FMath::Pow(1.12, WheelSteps));
	}
	return true;
}

void AABTSM11FinaleHUD::CancelFinaleHudCapture()
{
	HudCapture.CancelForFocusLoss();
	PendingTrajectoryHit = FABTSM11TrajectoryHit();
}

void AABTSM11FinaleHUD::DrawFinaleLayer(
	AABTSM11FinaleInteractionSystem& System)
{
	if (Canvas == nullptr)
	{
		return;
	}
	// GameViewportClient translates the Canvas by UnscaledViewRect.Min before
	// AHUD::DrawHUD. Mouse input remains relative to the full viewport, so the
	// same player-view origin and extent must be retained for hit testing.
	if (Canvas->SceneView != nullptr)
	{
		const FIntRect& PlayerViewRect =
			Canvas->SceneView->UnscaledViewRect;
		HudPlayerViewOrigin = FVector2D(PlayerViewRect.Min);
		HudPlayerViewSize = FVector2D(PlayerViewRect.Size());
	}
	else
	{
		HudPlayerViewOrigin = FVector2D::ZeroVector;
		HudPlayerViewSize = FVector2D(Canvas->SizeX, Canvas->SizeY);
	}
	// ClipX/ClipY are the logical Canvas extent after any Canvas DPI scale.
	UpdateFinaleHudLayout(Canvas->ClipX, Canvas->ClipY);
	DrawOrbitalDiagram(System, HudDiagramCenter, HudDiagramRadius);
	if (System.IsAiming())
	{
		DrawTargetPreview(System);
		DrawTargetWedge(System);
		DrawFinaleControlConsole(System);
	}
	else
	{
		// The PIP and Wedge are aiming aids. Once the release gesture has
		// completed, the authority flight camera owns spatial guidance.
		CachedPipTrajectory.Reset();
		TargetWedgeTracker.Reset();
	}
	DrawStatus(System, HudDiagramCenter, HudDiagramRadius);
}

void AABTSM11FinaleHUD::DrawOrbitalDiagram(
	AABTSM11FinaleInteractionSystem& System,
	const FVector2D& Center,
	const float Radius)
{
	const FABTSM11OrbitalDiagramSnapshot& Snapshot =
		System.GetDiagramSnapshot();
	FCanvasNGonItem Background(
		Center,
		FVector2D(Radius, Radius),
		48,
		FLinearColor(0.008f, 0.018f, 0.035f, 0.82f));
	Background.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Background);
	DrawCircleOutline(
		Center,
		Radius,
		FLinearColor(0.50f, 0.78f, 0.92f, 0.9f),
		2.0f);
	const FABTSM11OverviewProjection& HudProjection =
		System.GetHudOverviewProjection();
	const FABTSM11OrbitalSceneSnapshot& HudScene =
		System.GetHudOrbitalScene();
	const FABTSM11OverviewViewState& HudView =
		System.GetHudOverviewView();
	if (HudProjection.bValid && HudScene.bValid && HudView.bValid)
	{
		const FABTSM11OverviewProjectedBody& Primary =
			HudProjection.Bodies[0];
		const float PrimaryRadius = FMath::Max(
			14.0f,
			static_cast<float>(Primary.VisualRadius) * Radius);
		DrawCircleOutline(
			ToScreen(Center, Radius, Primary.Center),
			PrimaryRadius,
			FLinearColor(0.35f, 0.72f, 1.0f, 0.9f),
			1.5f);

		// Absolute finale-local latitude/longitude grid. It is projected by
		// the frozen overview view, so aim changes cannot move the sphere.
		const FVector3d PrimaryCenter = HudScene.Bodies[0].CenterCM;
		const double PrimaryRadiusCM = HudScene.Bodies[0].VisualRadiusCM;
		for (int32 LatitudeIndex = -2; LatitudeIndex <= 2; ++LatitudeIndex)
		{
			const double Latitude = FMath::DegreesToRadians(
				static_cast<double>(LatitudeIndex) * 30.0);
			FVector3d Previous;
			bool bHasPrevious = false;
			for (int32 Step = 0; Step <= 48; ++Step)
			{
				const double Longitude = UE_TWO_PI
					* static_cast<double>(Step) / 48.0;
				const FVector3d Point = PrimaryCenter + PrimaryRadiusCM
					* FVector3d(
						FMath::Cos(Latitude) * FMath::Cos(Longitude),
						FMath::Cos(Latitude) * FMath::Sin(Longitude),
						FMath::Sin(Latitude));
				if (bHasPrevious)
				{
					DrawDiagramSegment(
						Center,
						Radius,
						HudView.Project(Previous),
						HudView.Project(Point),
						FLinearColor(0.28f, 0.58f, 0.88f, 0.55f),
						0.7f,
						HudView.ProjectDepth((Previous + Point) * 0.5)
							< HudView.ProjectDepth(PrimaryCenter));
				}
				Previous = Point;
				bHasPrevious = true;
			}
		}
		for (int32 LongitudeIndex = 0; LongitudeIndex < 12; ++LongitudeIndex)
		{
			const double Longitude = UE_TWO_PI
				* static_cast<double>(LongitudeIndex) / 12.0;
			FVector3d Previous;
			bool bHasPrevious = false;
			for (int32 Step = 0; Step <= 32; ++Step)
			{
				const double Latitude = -UE_HALF_PI + UE_PI
					* static_cast<double>(Step) / 32.0;
				const FVector3d Point = PrimaryCenter + PrimaryRadiusCM
					* FVector3d(
						FMath::Cos(Latitude) * FMath::Cos(Longitude),
						FMath::Cos(Latitude) * FMath::Sin(Longitude),
						FMath::Sin(Latitude));
				if (bHasPrevious)
				{
					DrawDiagramSegment(
						Center,
						Radius,
						HudView.Project(Previous),
						HudView.Project(Point),
						FLinearColor(0.28f, 0.58f, 0.88f, 0.55f),
						0.7f,
						HudView.ProjectDepth((Previous + Point) * 0.5)
							< HudView.ProjectDepth(PrimaryCenter));
				}
				Previous = Point;
				bHasPrevious = true;
			}
		}

		for (int32 AssistIndex = 1;
			AssistIndex <= FABTSM11GravityScenario::AssistCount;
			++AssistIndex)
		{
			const FABTSM11OverviewProjectedBody& Body =
				HudProjection.Bodies[AssistIndex];
			const FABTSM11OrbitalSceneBody& SceneBody =
				HudScene.Bodies[AssistIndex];
			const FLinearColor BodyColor = M11TargetColor(
				static_cast<EABTSM11PreviewTarget>(AssistIndex - 1));
			const double InfluenceRadius = SceneBody.InfluenceRadiusCM
				* HudView.Zoom / HudView.ProjectionScaleCM;
			DrawDiagramCircleOutline(
				Center,
				Radius,
				Body.Center,
				InfluenceRadius,
				BodyColor.CopyWithNewOpacity(0.20f),
				0.8f);
			DrawPlanetGlyph(
				AssistIndex,
				Center,
				Radius,
				Body.Center,
				FMath::Max(
					8.0 / FMath::Max<double>(Radius, 1.0),
					Body.VisualRadius),
				BodyColor);
		}
		DrawUFOGlyph(
			Center,
			Radius,
			HudProjection.TargetCenter,
			FMath::Max(
				9.0 / FMath::Max<double>(Radius, 1.0),
				HudProjection.TargetRadius),
			M11TargetColor(EABTSM11PreviewTarget::UFO));

		for (const FABTSM11OverviewHitProxy& Proxy
			: HudProjection.HitProxies)
		{
			DrawDiagramSegment(
				Center,
				Radius,
				Proxy.Start,
				Proxy.End,
				FLinearColor(0.88f, 0.96f, 1.0f, 1.0f),
				1.8f,
				Proxy.bHiddenByBody);
		}
		if (System.HasHudTrajectoryProbe())
		{
			const FVector2D Reference = ToScreen(
				Center,
				Radius,
				HudView.Project(
					System.GetHudTrajectoryProbe().ReferenceLocalPosition));
			DrawLine(Reference.X - 6.0f, Reference.Y, Reference.X + 6.0f, Reference.Y,
				FLinearColor::White, 1.6f);
			DrawLine(Reference.X, Reference.Y - 6.0f, Reference.X, Reference.Y + 6.0f,
				FLinearColor::White, 1.6f);
			if (System.GetHudProbeProjection().bValid)
			{
				const FVector2D Current = ToScreen(
					Center,
					Radius,
					HudView.Project(
						System.GetHudProbeProjection().PositionCM));
				DrawCircleOutline(Current, 5.0f,
					FLinearColor(0.25f, 1.0f, 0.82f), 1.8f, 16);
			}
		}
		DrawText(
			OverviewMode == EABTSM11OverviewInteractionMode::Select
				? TEXT("ORBIT OVERVIEW / SELECT")
				: TEXT("ORBIT OVERVIEW / MOVE"),
			FLinearColor(0.72f, 0.90f, 1.0f),
			Center.X - Radius + 10.0f,
			Center.Y - Radius + 8.0f,
			GEngine->GetSmallFont(),
			0.8f,
			false);
		return;
	}
	if (!Snapshot.bValid)
	{
		DrawText(
			TEXT("CALCULATING ORBIT..."),
			FLinearColor(0.7f, 0.85f, 0.95f),
			Center.X - Radius * 0.62f,
			Center.Y - 8.0f,
			GEngine->GetSmallFont(),
			0.85f,
			false);
		return;
	}

	const FABTSM11DiagramBody& Primary = Snapshot.Bodies[0];
	const float ActualPrimaryRadius =
		static_cast<float>(Primary.VisualRadius) * Radius;
	const float DisplayPrimaryRadius =
		FMath::Max(14.0f, ActualPrimaryRadius);
	const double DisplayPrimaryNormalizedRadius =
		DisplayPrimaryRadius / FMath::Max(Radius, 1.0f);
	const float PrimaryScale = ActualPrimaryRadius > KINDA_SMALL_NUMBER
		? DisplayPrimaryRadius / ActualPrimaryRadius
		: 1.0f;
	for (const FABTSM11DiagramGridSegment& Grid
		: Snapshot.PrimaryGrid)
	{
		FVector2d Start = Primary.Center
			+ (Grid.Start - Primary.Center) * PrimaryScale;
		FVector2d End = Primary.Center
			+ (Grid.End - Primary.Center) * PrimaryScale;
		DrawDiagramSegment(
			Center,
			Radius,
			Start,
			End,
			FLinearColor(0.30f, 0.62f, 0.90f, 0.65f),
			0.8f,
			Grid.bHiddenHemisphere);
	}
	DrawDiagramCircleOutline(
		Center,
		Radius,
		Primary.Center,
		DisplayPrimaryNormalizedRadius,
		FLinearColor(0.35f, 0.72f, 1.0f, 0.9f),
		1.5f);

	for (int32 AssistIndex = 1;
		AssistIndex <= FABTSM11GravityScenario::AssistCount;
		++AssistIndex)
	{
		const FABTSM11DiagramBody& Body =
			Snapshot.Bodies[AssistIndex];
		if (Body.InfluenceRadius * Radius > 2.0f)
		{
			DrawDiagramCircleOutline(
				Center,
				Radius,
				Body.Center,
				Body.InfluenceRadius,
				FLinearColor(
					Body.Color.R,
					Body.Color.G,
					Body.Color.B,
					0.22f),
				0.8f);
		}
		DrawPlanetGlyph(
			AssistIndex,
			Center,
			Radius,
			Body.Center,
			FMath::Max(
				8.0 / FMath::Max<double>(Radius, 1.0),
				Body.VisualRadius),
			Body.Color);
	}
	DrawUFOGlyph(
		Center,
		Radius,
		Snapshot.UFOCenter,
		FMath::Max(
			9.0 / FMath::Max<double>(Radius, 1.0),
			Snapshot.UFORadius),
		FLinearColor(0.45f, 1.0f, 0.85f, 1.0f));

	for (int32 Index = 1;
		Index < Snapshot.Trajectory.Num();
		Index += 2)
	{
		const FABTSM11DiagramPoint& A =
			Snapshot.Trajectory[Index - 1];
		const FABTSM11DiagramPoint& B =
			Snapshot.Trajectory[Index];
		DrawDiagramSegment(
			Center,
			Radius,
			A.Position,
			B.Position,
			SegmentColor(B.SegmentKind),
			B.SegmentKind
				== EABTSM11PlaybackSegmentKind::VisibleTerminalTransfer
				? 2.4f
				: 1.65f,
			A.bHiddenByBody || B.bHiddenByBody);
	}

	DrawText(
		TEXT("ORBIT OVERVIEW"),
		FLinearColor(0.72f, 0.90f, 1.0f),
		Center.X - Radius + 10.0f,
		Center.Y - Radius + 8.0f,
		GEngine->GetSmallFont(),
		0.8f,
		false);
	if (System.GetPreviewPlaybackPlan()
		.bUsesVisibleTerminalTransfer)
	{
		DrawText(
			TEXT("AMBER: VISIBLE TERMINAL TRANSFER"),
			FLinearColor(1.0f, 0.62f, 0.18f),
			Center.X - Radius + 10.0f,
			Center.Y + Radius - 22.0f,
			GEngine->GetSmallFont(),
			0.68f,
			false);
	}
	else
	{
		const AABTSM11FinaleSystem* FinaleSystem =
			System.GetFinaleSystem();
		if (FinaleSystem != nullptr
			&& FinaleSystem->IsEditorCandidateMode())
		{
			DrawText(
				TEXT("RAW 1X PLAYBACK / QUALIFIED ENDPOINT"),
				FLinearColor(1.0f, 0.72f, 0.24f),
				Center.X - Radius + 10.0f,
				Center.Y + Radius - 22.0f,
				GEngine->GetSmallFont(),
				0.58f,
				false);
		}
	}
}

void AABTSM11FinaleHUD::DrawFailureOverlay(
	AABTSM11FinaleInteractionSystem& System)
{
	if (Canvas == nullptr)
	{
		return;
	}
	const float Alpha = static_cast<float>(
		FMath::Clamp(System.GetFailureBlackoutAlpha(), 0.0, 1.0));
	if (Alpha <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	DrawRect(
		FLinearColor(0.0f, 0.0f, 0.0f, Alpha),
		0.0f,
		0.0f,
		Canvas->SizeX,
		Canvas->SizeY);
}

void AABTSM11FinaleHUD::DrawConsoleButton(
	const FBox2D& Box,
	const FString& Label,
	const bool bActive,
	const FLinearColor& Accent)
{
	if (!Box.bIsValid)
	{
		return;
	}
	const FVector2D Size = Box.Max - Box.Min;
	DrawRect(
		bActive
			? Accent.CopyWithNewOpacity(0.72f)
			: FLinearColor(0.025f, 0.055f, 0.09f, 0.88f),
		Box.Min.X,
		Box.Min.Y,
		Size.X,
		Size.Y);
	for (int32 Edge = 0; Edge < 4; ++Edge)
	{
		const FVector2D A = Edge == 0 ? Box.Min
			: Edge == 1 ? FVector2D(Box.Max.X, Box.Min.Y)
			: Edge == 2 ? Box.Max
			: FVector2D(Box.Min.X, Box.Max.Y);
		const FVector2D B = Edge == 0 ? FVector2D(Box.Max.X, Box.Min.Y)
			: Edge == 1 ? Box.Max
			: Edge == 2 ? FVector2D(Box.Min.X, Box.Max.Y)
			: Box.Min;
		DrawLine(A.X, A.Y, B.X, B.Y, Accent, bActive ? 2.0f : 1.0f);
	}
	DrawText(
		Label,
		bActive ? FLinearColor::White : Accent,
		Box.Min.X + 7.0f,
		Box.Min.Y + 6.0f,
		GEngine->GetSmallFont(),
		0.68f,
		false);
}

void AABTSM11FinaleHUD::DrawKnob(
	const FVector2D& Center,
	const float Radius,
	const FString& Label,
	const double ValueAlpha,
	const FString& ValueText,
	const bool bCaptured)
{
	FCanvasNGonItem Fill(
		Center,
		FVector2D(Radius, Radius),
		40,
		bCaptured
			? FLinearColor(0.12f, 0.34f, 0.50f, 0.94f)
			: FLinearColor(0.025f, 0.07f, 0.12f, 0.92f));
	Fill.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Fill);
	DrawCircleOutline(
		Center,
		Radius,
		bCaptured
			? FLinearColor(0.35f, 1.0f, 0.84f)
			: FLinearColor(0.48f, 0.78f, 0.96f),
		bCaptured ? 2.4f : 1.5f,
		40);
	const double Angle = FMath::Lerp(
		FMath::DegreesToRadians(-135.0),
		FMath::DegreesToRadians(135.0),
		FMath::Clamp(ValueAlpha, 0.0, 1.0));
	const FVector2D Needle(
		static_cast<float>(FMath::Cos(Angle)),
		static_cast<float>(FMath::Sin(Angle)));
	DrawLine(
		Center.X,
		Center.Y,
		Center.X + Needle.X * Radius * 0.72f,
		Center.Y + Needle.Y * Radius * 0.72f,
		FLinearColor(1.0f, 0.74f, 0.22f),
		2.8f);
	DrawText(
		Label,
		FLinearColor(0.76f, 0.91f, 1.0f),
		Center.X - Radius * 0.55f,
		Center.Y - 8.0f,
		GEngine->GetSmallFont(),
		0.72f,
		false);
	DrawText(
		ValueText,
		FLinearColor::White,
		Center.X - Radius * 0.62f,
		Center.Y + 11.0f,
		GEngine->GetSmallFont(),
		0.62f,
		false);
}

void AABTSM11FinaleHUD::DrawFinaleControlConsole(
	AABTSM11FinaleInteractionSystem& System)
{
	if (Canvas == nullptr || System.GetFinaleSystem() == nullptr)
	{
		return;
	}
	const FABTSM11FinaleLaunchModel& Model =
		System.GetFinaleSystem()->GetLayoutPreset().LaunchModel;
	const FABTSM11FinaleLaunchInput& Input = System.GetCurrentInput();
	const auto Alpha = [](const double Value, const double Minimum, const double Maximum)
	{
		return Maximum > Minimum
			? FMath::Clamp((Value - Minimum) / (Maximum - Minimum), 0.0, 1.0)
			: 0.0;
	};
	DrawKnob(
		HudKnobCenters[0],
		HudKnobRadius,
		TEXT("YAW"),
		Alpha(Input.YawDegrees, Model.MinimumYawDegrees, Model.MaximumYawDegrees),
		FString::Printf(TEXT("%+.3f deg"), Input.YawDegrees),
		HudCapture.GetCapture() == EABTSM11FinaleHudCapture::AdjustYaw);
	DrawKnob(
		HudKnobCenters[1],
		HudKnobRadius,
		TEXT("PITCH"),
		Alpha(Input.PitchDegrees, Model.MinimumPitchDegrees, Model.MaximumPitchDegrees),
		FString::Printf(TEXT("%+.3f deg"), Input.PitchDegrees),
		HudCapture.GetCapture() == EABTSM11FinaleHudCapture::AdjustPitch);
	DrawKnob(
		HudKnobCenters[2],
		HudKnobRadius,
		TEXT("POWER"),
		Alpha(Input.Power, Model.MinimumPower, Model.MaximumPower),
		FString::Printf(TEXT("%.4f"), Input.Power),
		HudCapture.GetCapture() == EABTSM11FinaleHudCapture::AdjustPower);

	const FLinearColor Accent(0.42f, 0.86f, 1.0f);
	DrawConsoleButton(HudGearCoarse, TEXT("1x"),
		HudSpeedGear == EABTSM11ControlSpeedGear::Coarse, Accent);
	DrawConsoleButton(HudGearFine, TEXT("0.1x"),
		HudSpeedGear == EABTSM11ControlSpeedGear::Fine, Accent);
	DrawConsoleButton(HudGearUltraFine, TEXT("0.01x"),
		HudSpeedGear == EABTSM11ControlSpeedGear::UltraFine, Accent);
	DrawConsoleButton(
		HudLaunchButton,
		TEXT("LAUNCH"),
		HudCapture.GetCapture() == EABTSM11FinaleHudCapture::LaunchButton,
		FLinearColor(1.0f, 0.48f, 0.16f));
	DrawConsoleButton(HudSelectButton, TEXT("SELECT"),
		OverviewMode == EABTSM11OverviewInteractionMode::Select, Accent);
	DrawConsoleButton(HudMoveButton, TEXT("MOVE"),
		OverviewMode == EABTSM11OverviewInteractionMode::Move, Accent);
	DrawConsoleButton(
		HudResetViewButton,
		TEXT("RESET VIEW"),
		false,
		OverviewMode == EABTSM11OverviewInteractionMode::Move
			? Accent
			: FLinearColor(0.35f, 0.42f, 0.48f));
	DrawConsoleButton(HudRebasePipButton, TEXT("REBASE"), false,
		System.HasHudTrajectoryProbe()
			? FLinearColor(0.42f, 1.0f, 0.72f)
			: FLinearColor(0.35f, 0.42f, 0.48f));
	DrawConsoleButton(HudFollowAutoButton, TEXT("AUTO PIP"),
		!System.HasHudTrajectoryProbe(), Accent);
}

void AABTSM11FinaleHUD::DrawTargetPreview(
	AABTSM11FinaleInteractionSystem& System)
{
	UTextureRenderTarget2D* RenderTarget =
		System.GetTargetPreviewRenderTarget();
	if (RenderTarget == nullptr || Canvas == nullptr)
	{
		CachedPipTrajectory.Reset();
		return;
	}
	if (System.HasHudTrajectoryProbe())
	{
		DrawProbeTargetPreview(System, *RenderTarget);
		return;
	}
	const FABTSM11TrajectoryResult* Prediction =
		System.GetTargetPreviewPrediction();
	const AABTSM11FinaleSystem* FinaleSystem =
		System.GetFinaleSystem();
	if (Prediction == nullptr || FinaleSystem == nullptr)
	{
		CachedPipTrajectory.Reset();
		return;
	}
	const float RenderAspect =
		static_cast<float>(FMath::Max(1, RenderTarget->SizeX))
		/ static_cast<float>(FMath::Max(1, RenderTarget->SizeY));
	float PreviewWidth =
		FMath::Min(420.0f, Canvas->SizeX * 0.34f);
	float PreviewHeight = PreviewWidth / RenderAspect;
	const float MaximumHeight =
		FMath::Min(250.0f, Canvas->SizeY * 0.30f);
	if (PreviewHeight > MaximumHeight)
	{
		PreviewHeight = MaximumHeight;
		PreviewWidth = PreviewHeight * RenderAspect;
	}
	const FVector2D Size(PreviewWidth, PreviewHeight);
	const FVector2D Position(
		(Canvas->SizeX - Size.X) * 0.5f,
		24.0f);
	FCanvasTileItem Tile(
		Position,
		RenderTarget->GetResource(),
		Size,
		FLinearColor::White);
	Tile.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(Tile);

	FABTSM11TargetPipView PipView;
	if (ABTSM11BuildTargetPipView(
			FinaleSystem->GetLayoutPreset(),
			System.GetPreviewSelection(),
			RenderTarget->SizeX,
			RenderTarget->SizeY,
			PipView))
	{
		if (!CachedPipTrajectory.bValid
			|| CachedPipTrajectory.SourceTrajectoryHash
				!= Prediction->ValidationHash
			|| CachedPipTrajectory.Target
				!= System.GetPreviewSelection().Target)
		{
			ABTSM11BuildTargetPipTrajectory(
				PipView,
				System.GetPreviewSelection(),
				*Prediction,
				CachedPipTrajectory,
				96);
		}
	}
	else
	{
		CachedPipTrajectory.Reset();
	}

	const FLinearColor TargetColor =
		M11TargetColor(System.GetPreviewSelection().Target);
	if (CachedPipTrajectory.bValid)
	{
		for (int32 Index = 1;
			Index < CachedPipTrajectory.Points.Num();
			++Index)
		{
			const FABTSM11TargetPipTrajectoryPoint& A =
				CachedPipTrajectory.Points[Index - 1];
			const FABTSM11TargetPipTrajectoryPoint& B =
				CachedPipTrajectory.Points[Index];
			if (!A.bInFront || !B.bInFront)
			{
				continue;
			}
			FVector2D Start = A.UV;
			FVector2D End = B.UV;
			if (!ABTSM11ClipPipLineToRect(Start, End, 0.025f))
			{
				continue;
			}
			Start = Position + Start * Size;
			End = Position + End * Size;
			DrawLine(
				Start.X,
				Start.Y,
				End.X,
				End.Y,
				FLinearColor(0.30f, 0.92f, 1.0f, 0.96f),
				2.2f);
		}

		for (const FABTSM11TargetPipTrajectoryPoint& Point
			: CachedPipTrajectory.Points)
		{
			if (!Point.bClosestApproach || !Point.bInFront)
			{
				continue;
			}
			FVector2D MarkerUV = Point.UV;
			const bool bOutside =
				MarkerUV.X < 0.045f
				|| MarkerUV.X > 0.955f
				|| MarkerUV.Y < 0.12f
				|| MarkerUV.Y > 0.955f;
			if (bOutside)
			{
				FVector2D RayStart(0.5f, 0.5f);
				if (ABTSM11ClipPipLineToRect(
					RayStart,
					MarkerUV,
					0.055f))
				{
					MarkerUV = FVector2D(
						FMath::Clamp(
							MarkerUV.X,
							0.055f,
							0.945f),
						FMath::Clamp(
							MarkerUV.Y,
							0.12f,
							0.945f));
				}
			}
			const FVector2D Marker =
				Position + MarkerUV * Size;
			DrawCircleOutline(
				Marker,
				bOutside ? 5.0f : 6.5f,
				System.GetPreviewSelection().bEnteredTargetRegion
					? FLinearColor(0.38f, 1.0f, 0.58f, 1.0f)
					: FLinearColor(1.0f, 0.62f, 0.18f, 1.0f),
				1.8f,
				18);
			break;
		}
	}

	const FVector2D TargetReticle =
		Position + Size * 0.5f;
	DrawCircleOutline(
		TargetReticle,
		10.0f,
		TargetColor.CopyWithNewOpacity(0.92f),
		1.4f,
		24);
	DrawLine(
		TargetReticle.X - 15.0f,
		TargetReticle.Y,
		TargetReticle.X - 7.0f,
		TargetReticle.Y,
		TargetColor,
		1.2f);
	DrawLine(
		TargetReticle.X + 7.0f,
		TargetReticle.Y,
		TargetReticle.X + 15.0f,
		TargetReticle.Y,
		TargetColor,
		1.2f);

	DrawRect(
		FLinearColor(0.02f, 0.06f, 0.11f, 0.75f),
		Position.X,
		Position.Y,
		Size.X,
		24.0f);
	DrawText(
		FString::Printf(
			TEXT("CURRENT APPROACH: %s%s"),
			TargetLabel(System.GetPreviewSelection().Target),
			System.GetFinaleSystem() != nullptr
				&& System.GetFinaleSystem()->IsEditorCandidateMode()
				&& System.GetClassification().IsF(4)
				&& System.GetPreviewSelection().Target
					== EABTSM11PreviewTarget::UFO
				? TEXT(" / QUALIFIED ENDPOINT")
				: System.GetPreviewSelection().bEnteredTargetRegion
				? TEXT(" / ENTERED")
				: TEXT(" / CLOSEST MISS")),
		FLinearColor(0.78f, 0.92f, 1.0f),
		Position.X + 8.0f,
		Position.Y + 5.0f,
		GEngine->GetSmallFont(),
		0.75f,
		false);
	DrawText(
		TEXT("CYAN: CURRENT PREDICTION  /  RING: CLOSEST"),
		FLinearColor(0.62f, 0.84f, 0.94f),
		Position.X + 8.0f,
		Position.Y + Size.Y - 18.0f,
		GEngine->GetSmallFont(),
		0.58f,
		false);
	for (int32 Edge = 0; Edge < 4; ++Edge)
	{
		const FVector2D A = Edge == 0
			? Position
			: Edge == 1
				? FVector2D(Position.X + Size.X, Position.Y)
				: Edge == 2
					? FVector2D(
						Position.X + Size.X,
						Position.Y + Size.Y)
					: FVector2D(Position.X, Position.Y + Size.Y);
		const FVector2D B = Edge == 0
			? FVector2D(Position.X + Size.X, Position.Y)
			: Edge == 1
				? FVector2D(
					Position.X + Size.X,
					Position.Y + Size.Y)
				: Edge == 2
					? FVector2D(Position.X, Position.Y + Size.Y)
					: Position;
		DrawLine(
			A.X,
			A.Y,
			B.X,
			B.Y,
			FLinearColor(0.48f, 0.76f, 0.94f),
			1.5f);
	}
}

void AABTSM11FinaleHUD::DrawProbeTargetPreview(
	AABTSM11FinaleInteractionSystem& System,
	UTextureRenderTarget2D& RenderTarget)
{
	if (Canvas == nullptr
		|| !System.GetHudTrajectoryProbe().bValid
		|| !System.GetHudTrajectoryProbe().FrozenPipView.bValid)
	{
		return;
	}
	const float RenderAspect =
		static_cast<float>(FMath::Max(1, RenderTarget.SizeX))
		/ static_cast<float>(FMath::Max(1, RenderTarget.SizeY));
	float PreviewWidth = FMath::Min(480.0f, Canvas->SizeX * 0.38f);
	float PreviewHeight = PreviewWidth / RenderAspect;
	const float MaximumHeight = FMath::Min(280.0f, Canvas->SizeY * 0.33f);
	if (PreviewHeight > MaximumHeight)
	{
		PreviewHeight = MaximumHeight;
		PreviewWidth = PreviewHeight * RenderAspect;
	}
	const FVector2D Size(PreviewWidth, PreviewHeight);
	const FVector2D Position((Canvas->SizeX - Size.X) * 0.5f, 24.0f);
	FCanvasTileItem Tile(
		Position,
		RenderTarget.GetResource(),
		Size,
		FLinearColor::White);
	Tile.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(Tile);

	const FABTSM11TrajectoryProbe& Probe =
		System.GetHudTrajectoryProbe();
	const FABTSM11FrozenPipView& View = Probe.FrozenPipView;
	const auto ToUv = [&View, RenderAspect](const FVector3d& Point)
	{
		const FVector2d Projected = View.Project(Point);
		return FVector2D(
			static_cast<float>(0.5 + Projected.X * 0.5),
			static_cast<float>(
				0.5 - Projected.Y * 0.5 * RenderAspect));
	};
	const auto DrawSceneTrajectory = [this, &ToUv, &Position, &Size](
		const FABTSM11OrbitalSceneSnapshot& Scene,
		const FLinearColor& Color,
		const float Thickness,
		const bool bDashed)
	{
		for (int32 Index = 1; Index < Scene.Trajectory.Num(); ++Index)
		{
			if (bDashed && (Index & 1) == 0)
			{
				continue;
			}
			FVector2D Start = ToUv(Scene.Trajectory[Index - 1].PositionCM);
			FVector2D End = ToUv(Scene.Trajectory[Index].PositionCM);
			if (!ABTSM11ClipPipLineToRect(Start, End, 0.02f))
			{
				continue;
			}
			Start = Position + Start * Size;
			End = Position + End * Size;
			DrawLine(Start.X, Start.Y, End.X, End.Y, Color, Thickness);
		}
	};
	DrawSceneTrajectory(
		System.GetHudProbeReferenceScene(),
		FLinearColor(0.68f, 0.72f, 0.78f, 0.72f),
		1.2f,
		true);
	DrawSceneTrajectory(
		System.GetHudOrbitalScene(),
		FLinearColor(0.22f, 0.96f, 1.0f, 0.98f),
		2.4f,
		false);

	const FVector2D ReferenceMarker =
		Position + ToUv(Probe.ReferenceLocalPosition) * Size;
	DrawLine(ReferenceMarker.X - 7.0f, ReferenceMarker.Y,
		ReferenceMarker.X + 7.0f, ReferenceMarker.Y,
		FLinearColor::White, 1.6f);
	DrawLine(ReferenceMarker.X, ReferenceMarker.Y - 7.0f,
		ReferenceMarker.X, ReferenceMarker.Y + 7.0f,
		FLinearColor::White, 1.6f);
	const FABTSM11ProbeProjection& Current =
		System.GetHudProbeProjection();
	if (Current.bValid)
	{
		const FVector2D CurrentMarker =
			Position + ToUv(Current.PositionCM) * Size;
		DrawCircleOutline(
			CurrentMarker,
			6.0f,
			FLinearColor(0.28f, 1.0f, 0.64f),
			2.0f,
			16);
	}

	DrawRect(
		FLinearColor(0.02f, 0.06f, 0.11f, 0.80f),
		Position.X,
		Position.Y,
		Size.X,
		26.0f);
	const TCHAR* RemapLabel = Current.Status
		== EABTSM11ProbeRemapStatus::ExactSemanticLeg
		? TEXT("TRACKING SAME PHASE")
		: Current.Status == EABTSM11ProbeRemapStatus::ClosestMissFallback
			? TEXT("CLOSEST MISS")
			: TEXT("TRAJECTORY ENDS BEFORE LEG");
	DrawText(
		FString::Printf(
			TEXT("TRAJECTORY PROBE / LEG %d / %s"),
			static_cast<int32>(Probe.Leg),
			RemapLabel),
		FLinearColor(0.78f, 0.94f, 1.0f),
		Position.X + 8.0f,
		Position.Y + 6.0f,
		GEngine->GetSmallFont(),
		0.72f,
		false);
	DrawText(
		FString::Printf(
			TEXT("GRAY: CLICK REFERENCE  /  CYAN: CURRENT  /  DIST %.0f cm"),
			Current.ContextDistanceCM),
		FLinearColor(0.68f, 0.84f, 0.92f),
		Position.X + 8.0f,
		Position.Y + Size.Y - 18.0f,
		GEngine->GetSmallFont(),
		0.58f,
		false);
	for (int32 Edge = 0; Edge < 4; ++Edge)
	{
		const FVector2D A = Edge == 0 ? Position
			: Edge == 1 ? FVector2D(Position.X + Size.X, Position.Y)
			: Edge == 2 ? Position + Size
			: FVector2D(Position.X, Position.Y + Size.Y);
		const FVector2D B = Edge == 0 ? FVector2D(Position.X + Size.X, Position.Y)
			: Edge == 1 ? Position + Size
			: Edge == 2 ? FVector2D(Position.X, Position.Y + Size.Y)
			: Position;
		DrawLine(A.X, A.Y, B.X, B.Y,
			FLinearColor(0.48f, 0.82f, 0.98f), 1.5f);
	}
}

void AABTSM11FinaleHUD::DrawTargetWedge(
	AABTSM11FinaleInteractionSystem& System)
{
	if (Canvas == nullptr)
	{
		return;
	}
	const AABTSM11FinaleSystem* FinaleSystem =
		System.GetFinaleSystem();
	APlayerController* Controller = GetOwningPlayerController();
	APlayerCameraManager* CameraManager =
		Controller != nullptr
		? Controller->PlayerCameraManager
		: nullptr;
	if (FinaleSystem == nullptr
		|| CameraManager == nullptr
		|| System.GetTargetPreviewPrediction() == nullptr)
	{
		TargetWedgeTracker.Reset();
		return;
	}

	const FABTSM110FinaleLocalFrame& Frame =
		FinaleSystem->GetFinaleFrame();
	EABTSM11PreviewTarget WedgeTarget =
		System.GetPreviewSelection().Target;
	FVector3d WedgeTargetLocal =
		System.GetPreviewSelection().TargetCenterCM;
	if (System.HasHudTrajectoryProbe())
	{
		const FABTSM11TrajectoryProbe& Probe =
			System.GetHudTrajectoryProbe();
		if (Probe.bContextIsTarget)
		{
			WedgeTarget = EABTSM11PreviewTarget::UFO;
			WedgeTargetLocal = System.GetHudOrbitalScene().TargetCenterCM;
		}
		else if (Probe.ContextBodyIndex >= 1
			&& Probe.ContextBodyIndex
				<= FABTSM11GravityScenario::AssistCount)
		{
			WedgeTarget = static_cast<EABTSM11PreviewTarget>(
				Probe.ContextBodyIndex - 1);
			WedgeTargetLocal = System.GetHudOrbitalScene()
				.Bodies[Probe.ContextBodyIndex].CenterCM;
		}
	}
	const FVector TargetWorld = Frame.TransformLocalPosition(
		FVector(WedgeTargetLocal));
	const FRotator CameraRotation =
		CameraManager->GetCameraRotation();
	const FRotationMatrix CameraBasis(CameraRotation);
	const FVector2D ViewportSize(Canvas->SizeX, Canvas->SizeY);
	const FABTSM11TargetWedgeProjection Projection =
		ABTSM11ProjectTargetForWedge(
			FVector3d(TargetWorld),
			FVector3d(CameraManager->GetCameraLocation()),
			FVector3d(
				CameraBasis.GetScaledAxis(EAxis::X)),
			FVector3d(
				CameraBasis.GetScaledAxis(EAxis::Y)),
			FVector3d(
				CameraBasis.GetScaledAxis(EAxis::Z)),
			CameraManager->GetFOVAngle(),
			ViewportSize);
	const FABTSM11TargetWedgeOutput Wedge =
		TargetWedgeTracker.Update(
			GetWorld() != nullptr
				? GetWorld()->GetDeltaSeconds()
				: 0.0,
			WedgeTarget,
			Projection,
			ViewportSize);
	if (!Wedge.bVisible)
	{
		return;
	}

	const FLinearColor Color = M11TargetColor(Wedge.Target);
	const FVector2D Perpendicular(
		-Wedge.Direction.Y,
		Wedge.Direction.X);
	const FVector2D Tip = Wedge.Anchor;
	const FVector2D Base =
		Tip - Wedge.Direction * 19.0f;
	const FVector2D Left = Base + Perpendicular * 8.0f;
	const FVector2D Right = Base - Perpendicular * 8.0f;
	DrawLine(Tip.X, Tip.Y, Left.X, Left.Y, Color, 3.0f);
	DrawLine(Tip.X, Tip.Y, Right.X, Right.Y, Color, 3.0f);
	DrawLine(Left.X, Left.Y, Right.X, Right.Y, Color, 2.0f);
	DrawCircleOutline(Base, 4.0f, Color, 1.4f, 16);

	const FVector2D RawLabelPosition =
		Base - Wedge.Direction * 16.0f
			+ Perpendicular * 9.0f;
	const FVector2D LabelPosition(
		FMath::Clamp(
			RawLabelPosition.X,
			12.0f,
			ViewportSize.X - 92.0f),
		FMath::Clamp(
			RawLabelPosition.Y,
			12.0f,
			ViewportSize.Y - 28.0f));
	DrawText(
		TargetLabel(Wedge.Target),
		Color,
		LabelPosition.X,
		LabelPosition.Y,
		GEngine->GetSmallFont(),
		0.72f,
		false);
}

void AABTSM11FinaleHUD::DrawStatus(
	AABTSM11FinaleInteractionSystem& System,
	const FVector2D& DiagramCenter,
	const float DiagramRadius)
{
	const FABTSM11PrefixClassification& Classification =
		System.GetClassification();
	const float X = DiagramCenter.X - DiagramRadius;
	const float Y = DiagramCenter.Y - DiagramRadius - 48.0f;
	const AABTSM11FinaleSystem* FinaleSystem =
		System.GetFinaleSystem();
	const bool bEditorCandidate =
		FinaleSystem != nullptr
		&& FinaleSystem->IsEditorCandidateMode();
	if (bEditorCandidate)
	{
		const FABTSM11CandidateExperienceIdentity& Identity =
			FinaleSystem->GetEditorCandidateIdentity();
		const float CandidateY = FMath::Max(20.0f, Y - 158.0f);
		const float PanelWidth = FMath::Min(
			440.0f,
			Canvas->SizeX * 0.45f);
		DrawRect(
			FLinearColor(0.20f, 0.045f, 0.018f, 0.82f),
			X - 6.0f,
			CandidateY - 5.0f,
			PanelWidth,
			148.0f);
		DrawText(
			TEXT("EDITOR CANDIDATE / NOT CERTIFIED"),
			FLinearColor(1.0f, 0.42f, 0.16f),
			X,
			CandidateY,
			GEngine->GetSmallFont(),
			0.80f,
			false);
		DrawText(
			FString::Printf(
				TEXT("RANK %d  /  GLOBAL WORK %llu"),
				Identity.Rank,
				static_cast<unsigned long long>(
					Identity.GlobalWorkIndex)),
			FLinearColor(1.0f, 0.78f, 0.34f),
			X,
			CandidateY + 18.0f,
			GEngine->GetSmallFont(),
			0.68f,
			false);
		DrawText(
			FString::Printf(
				TEXT("SOURCE  0x%016llX"),
				static_cast<unsigned long long>(
					Identity.CandidateSourceHash)),
			FLinearColor(0.92f, 0.82f, 0.66f),
			X,
			CandidateY + 34.0f,
			GEngine->GetSmallFont(),
			0.62f,
			false);
		DrawText(
			FString::Printf(
				TEXT("REQUEST 0x%016llX"),
				static_cast<unsigned long long>(
					Identity.NominalRequestHash)),
			FLinearColor(0.92f, 0.82f, 0.66f),
			X,
			CandidateY + 49.0f,
			GEngine->GetSmallFont(),
			0.62f,
			false);
		DrawText(
			FString::Printf(
				TEXT("RESULT  0x%016llX"),
				static_cast<unsigned long long>(
					Identity.NominalResultHash)),
			FLinearColor(0.92f, 0.82f, 0.66f),
			X,
			CandidateY + 64.0f,
			GEngine->GetSmallFont(),
			0.62f,
			false);
		DrawText(
			FString::Printf(
				TEXT("SCORE   0x%016llX"),
				static_cast<unsigned long long>(
					Identity.ScoreHash)),
			FLinearColor(0.92f, 0.82f, 0.66f),
			X,
			CandidateY + 79.0f,
			GEngine->GetSmallFont(),
			0.62f,
			false);
		DrawText(
			TEXT("RAW 1X CANDIDATE PLAYBACK / QUALIFIED ENDPOINT"),
			FLinearColor(1.0f, 0.66f, 0.18f),
			X,
			CandidateY + 96.0f,
			GEngine->GetSmallFont(),
			0.62f,
			false);
		DrawText(
			TEXT("THREE KNOBS = YAW / PITCH / POWER"),
			FLinearColor(0.80f, 0.88f, 0.96f),
			X,
			CandidateY + 112.0f,
			GEngine->GetSmallFont(),
			0.58f,
			false);
		DrawText(
			TEXT("1x / 0.1x / 0.01x | EXPLICIT LAUNCH | R = RESET"),
			FLinearColor(0.80f, 0.88f, 0.96f),
			X,
			CandidateY + 128.0f,
			GEngine->GetSmallFont(),
			0.58f,
			false);
	}
	const FString Chain = bEditorCandidate
		? FString::Printf(
			TEXT("SPACE  >  1%s  >  2%s  >  3%s  >  ENDPOINT%s"),
			Classification.IsF(1) ? TEXT("[PASS]") : TEXT(""),
			Classification.IsF(2) ? TEXT("[PASS]") : TEXT(""),
			Classification.IsF(3) ? TEXT("[PASS]") : TEXT(""),
			Classification.IsF(4)
				? TEXT("[QUALIFIED]")
				: TEXT(""))
		: FString::Printf(
			TEXT("SPACE  >  1%s  >  2%s  >  3%s  >  UFO%s"),
			Classification.IsF(1) ? TEXT("[OK]") : TEXT(""),
			Classification.IsF(2) ? TEXT("[OK]") : TEXT(""),
			Classification.IsF(3) ? TEXT("[OK]") : TEXT(""),
			Classification.IsF(4) ? TEXT("[LOCK]") : TEXT(""));
	DrawText(
		Chain,
		FLinearColor(0.76f, 0.91f, 1.0f),
		X,
		Y,
		GEngine->GetSmallFont(),
		0.72f,
		false);

	const FABTSM11PrefixStabilizer& Stabilizer =
		System.GetStabilizer();
	FString StabilizerText = TEXT("FREE AIM");
	FLinearColor StabilizerColor(0.72f, 0.78f, 0.84f);
	if (Stabilizer.GetNearPrefixLevel() > 0)
	{
		StabilizerText = bEditorCandidate
			? FString::Printf(
				TEXT("CANDIDATE PRECISION F%d / TEMPORARY"),
				Stabilizer.GetNearPrefixLevel())
			: FString::Printf(
				TEXT("PRECISION MODE: F%d"),
				Stabilizer.GetNearPrefixLevel());
		StabilizerColor = FLinearColor(0.95f, 0.75f, 0.22f);
	}
	else if (Stabilizer.GetStablePrefixLevel() > 0)
	{
		StabilizerText = bEditorCandidate
			? FString::Printf(
				TEXT("CANDIDATE PREFIX F%d HELD / TEMPORARY"),
				Stabilizer.GetStablePrefixLevel())
			: FString::Printf(
				TEXT("CORRIDOR %d STABLE / %d OF 3"),
				Stabilizer.GetStablePrefixLevel(),
				Stabilizer.GetStablePrefixLevel());
		StabilizerColor = FLinearColor(0.32f, 1.0f, 0.58f);
	}
	DrawText(
		StabilizerText,
		StabilizerColor,
		X,
		Y + 18.0f,
		GEngine->GetSmallFont(),
		0.75f,
		false);

	const double LastSolveMilliseconds =
		System.GetLastPreviewSolveMilliseconds();
	const double LastLatencyMilliseconds =
		System.GetLastPreviewLatencyMilliseconds();
	const bool bPreviewInFlight =
		System.IsPreviewSolveInFlight();
	const bool bPreviewDirty =
		System.IsPreviewDirty();
	FString PreviewState;
	if (bPreviewInFlight)
	{
		if (LastLatencyMilliseconds <= 0.0)
		{
			PreviewState = TEXT("IN FLIGHT / NO PUBLISHED RESULT");
		}
		else if (bPreviewDirty)
		{
			PreviewState =
				TEXT("IN FLIGHT / STALE / LATEST QUEUED");
		}
		else
		{
			PreviewState = TEXT("IN FLIGHT / STALE");
		}
	}
	else if (bPreviewDirty)
	{
		PreviewState = TEXT("STALE / QUEUED");
	}
	else
	{
		PreviewState = LastLatencyMilliseconds > 0.0
			? TEXT("LATEST")
			: TEXT("WAITING FOR FIRST SOLVE");
	}
	DrawText(
		FString::Printf(
			TEXT("PREVIEW %.2f ms SOLVE / %.2f ms LATENCY / %s / DISCARDED %llu"),
			LastSolveMilliseconds,
			LastLatencyMilliseconds,
			*PreviewState,
			static_cast<unsigned long long>(
				System.GetDiscardedPreviewSolveCount())),
		PreviewState == TEXT("LATEST")
			? FLinearColor(0.38f, 1.0f, 0.62f)
			: FLinearColor(1.0f, 0.72f, 0.25f),
		X,
		Y + 36.0f,
		GEngine->GetSmallFont(),
		0.58f,
		false);
	DrawText(
		FString::Printf(
			TEXT("HUD OVERVIEW %llu / PROBE %llu / STATIC CAPTURE %llu"),
			static_cast<unsigned long long>(System.GetHudOverviewRevision()),
			static_cast<unsigned long long>(System.GetHudProbeRevision()),
			static_cast<unsigned long long>(System.GetTargetCaptureCount())),
		FLinearColor(0.58f, 0.78f, 0.90f),
		X,
		Y + 52.0f,
		GEngine->GetSmallFont(),
		0.55f,
		false);

	const float BarWidth = DiagramRadius * 1.25f;
	const float BarY = DiagramCenter.Y + DiagramRadius + 12.0f;
	DrawRect(
		FLinearColor(0.03f, 0.05f, 0.08f, 0.8f),
		X,
		BarY,
		BarWidth,
		9.0f);
	DrawRect(
		FLinearColor(0.95f, 0.63f, 0.16f, 0.95f),
		X,
		BarY,
		BarWidth * static_cast<float>(
			FMath::Clamp(System.GetCurrentInput().Power, 0.0, 1.0)),
		9.0f);
	DrawText(
		TEXT("LAUNCH POWER"),
		FLinearColor(0.76f, 0.82f, 0.90f),
		X + BarWidth + 8.0f,
		BarY - 4.0f,
		GEngine->GetSmallFont(),
		0.68f,
		false);

	const FString Status = System.IsReleasePending()
		? TEXT("FREEZING EXACT RELEASE...")
		: System.GetRuntimeFailure();
	if (!Status.IsEmpty())
	{
		DrawText(
			Status,
			Classification.IsF(4)
				? FLinearColor(0.35f, 1.0f, 0.62f)
				: FLinearColor(1.0f, 0.55f, 0.28f),
			X,
			BarY + 16.0f,
			GEngine->GetSmallFont(),
			0.72f,
			false);
	}
	DrawText(
		TEXT("Drag knob: adjust  |  Wheel over knob: trim  |  MOVE: LMB pan / RMB orbit  |  LAUNCH only: release  |  R: reset"),
		FLinearColor(0.66f, 0.72f, 0.80f),
		X,
		BarY + 34.0f,
		GEngine->GetSmallFont(),
		0.62f,
		false);
}

void AABTSM11FinaleHUD::DrawDiagramSegment(
	const FVector2D& Center,
	const float Radius,
	const FVector2d& NormalizedStart,
	const FVector2d& NormalizedEnd,
	const FLinearColor& Color,
	const float Thickness,
	const bool bDashed)
{
	FVector2d Start = NormalizedStart;
	FVector2d End = NormalizedEnd;
	if (!ABTSM11ClipDiagramSegmentToUnitCircle(Start, End))
	{
		return;
	}
	const FVector2D ScreenStart = ToScreen(Center, Radius, Start);
	const FVector2D ScreenEnd = ToScreen(Center, Radius, End);
	if (!bDashed)
	{
		DrawLine(
			ScreenStart.X,
			ScreenStart.Y,
			ScreenEnd.X,
			ScreenEnd.Y,
			Color,
			Thickness);
		return;
	}
	const FVector2D Delta = ScreenEnd - ScreenStart;
	const float Length = Delta.Size();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	const FVector2D Direction = Delta / Length;
	constexpr float Dash = 6.0f;
	constexpr float Gap = 4.0f;
	for (float Distance = 0.0f; Distance < Length; Distance += Dash + Gap)
	{
		const FVector2D A = ScreenStart + Direction * Distance;
		const FVector2D B = ScreenStart
			+ Direction * FMath::Min(Length, Distance + Dash);
		DrawLine(A.X, A.Y, B.X, B.Y, Color, Thickness);
	}
}

void AABTSM11FinaleHUD::DrawDiagramCircleOutline(
	const FVector2D& PanelCenter,
	const float PanelRadius,
	const FVector2d& NormalizedCenter,
	const double NormalizedRadius,
	const FLinearColor& Color,
	const float Thickness,
	const int32 SegmentCount)
{
	const int32 SafeSegments = FMath::Max(8, SegmentCount);
	FVector2d Previous = NormalizedCenter
		+ FVector2d(NormalizedRadius, 0.0);
	for (int32 Index = 1; Index <= SafeSegments; ++Index)
	{
		const double Angle = 2.0 * UE_DOUBLE_PI
			* static_cast<double>(Index)
			/ static_cast<double>(SafeSegments);
		const FVector2d Current = NormalizedCenter + FVector2d(
			FMath::Cos(Angle) * NormalizedRadius,
			FMath::Sin(Angle) * NormalizedRadius);
		DrawDiagramSegment(
			PanelCenter,
			PanelRadius,
			Previous,
			Current,
			Color,
			Thickness,
			false);
		Previous = Current;
	}
}

void AABTSM11FinaleHUD::DrawCircleOutline(
	const FVector2D& Center,
	const float Radius,
	const FLinearColor& Color,
	const float Thickness,
	const int32 SegmentCount)
{
	const int32 SafeSegments = FMath::Max(8, SegmentCount);
	FVector2D Previous = Center + FVector2D(Radius, 0.0f);
	for (int32 Index = 1; Index <= SafeSegments; ++Index)
	{
		const float Angle = 2.0f * PI
			* static_cast<float>(Index)
			/ static_cast<float>(SafeSegments);
		const FVector2D Current = Center + FVector2D(
			FMath::Cos(Angle) * Radius,
			FMath::Sin(Angle) * Radius);
		DrawLine(
			Previous.X,
			Previous.Y,
			Current.X,
			Current.Y,
			Color,
			Thickness);
		Previous = Current;
	}
}

void AABTSM11FinaleHUD::DrawPlanetGlyph(
	const int32 AssistIndex,
	const FVector2D& PanelCenter,
	const float PanelRadius,
	const FVector2d& NormalizedCenter,
	const double NormalizedRadius,
	const FLinearColor& Color)
{
	DrawDiagramCircleOutline(
		PanelCenter,
		PanelRadius,
		NormalizedCenter,
		NormalizedRadius,
		Color,
		1.5f,
		28);
	if (AssistIndex == 1)
	{
		DrawDiagramCircleOutline(
			PanelCenter,
			PanelRadius,
			NormalizedCenter
				+ FVector2d(
					-NormalizedRadius * 0.25,
					NormalizedRadius * 0.18),
			NormalizedRadius * 0.18,
			Color,
			0.8f,
			12);
	}
	else if (AssistIndex == 2)
	{
		for (int32 Band = -1; Band <= 1; ++Band)
		{
			const double Y = NormalizedCenter.Y
				+ Band * NormalizedRadius * 0.34;
			DrawDiagramSegment(
				PanelCenter,
				PanelRadius,
				FVector2d(
					NormalizedCenter.X
						- NormalizedRadius * 0.82,
					Y),
				FVector2d(
					NormalizedCenter.X
						+ NormalizedRadius * 0.82,
					Y),
				Color,
				0.8f,
				false);
		}
	}
	else
	{
		DrawDiagramSegment(
			PanelCenter,
			PanelRadius,
			NormalizedCenter + FVector2d(
				-NormalizedRadius * 1.55,
				-NormalizedRadius * 0.42),
			NormalizedCenter + FVector2d(
				NormalizedRadius * 1.55,
				NormalizedRadius * 0.42),
			Color,
			1.2f,
			false);
		DrawDiagramSegment(
			PanelCenter,
			PanelRadius,
			NormalizedCenter + FVector2d(
				-NormalizedRadius * 1.35,
				-NormalizedRadius * 0.62),
			NormalizedCenter + FVector2d(
				NormalizedRadius * 1.35,
				NormalizedRadius * 0.62),
			Color,
			0.8f,
			false);
	}
}

void AABTSM11FinaleHUD::DrawUFOGlyph(
	const FVector2D& PanelCenter,
	const float PanelRadius,
	const FVector2d& NormalizedCenter,
	const double NormalizedRadius,
	const FLinearColor& Color)
{
	DrawDiagramCircleOutline(
		PanelCenter,
		PanelRadius,
		NormalizedCenter,
		NormalizedRadius,
		Color,
		1.4f,
		24);
	DrawDiagramSegment(
		PanelCenter,
		PanelRadius,
		NormalizedCenter + FVector2d(
			-NormalizedRadius * 1.65,
			0.0),
		NormalizedCenter + FVector2d(
			NormalizedRadius * 1.65,
			0.0),
		Color,
		1.5f,
		false);
	DrawDiagramCircleOutline(
		PanelCenter,
		PanelRadius,
		NormalizedCenter
			+ FVector2d(0.0, NormalizedRadius * 0.28),
		NormalizedRadius * 0.52,
		Color,
		0.9f,
		18);
}
