// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSM11FinaleHUD.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Game/ABTSM11GameMode.h"
#include "GameFramework/PlayerController.h"
#include "SceneView.h"
#include "UObject/ConstructorHelpers.h"
#include "World/ABTSM11FinaleInteractionSystem.h"
#include "World/ABTSM11FinaleSystem.h"

namespace
{
	enum class EM11FinaleButtonIcon : int32
	{
		Select,
		Move,
		ResetView,
		Rebase,
		AutoPip,
		Coarse,
		Fine,
		UltraFine,
		Launch,
		Count
	};

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

	const TCHAR* SemanticLegLabel(
		const EABTSM11TrajectorySemanticLeg Leg)
	{
		switch (Leg)
		{
		case EABTSM11TrajectorySemanticLeg::LaunchToAssist1:
			return TEXT("LAUNCH -> ASSIST 1");
		case EABTSM11TrajectorySemanticLeg::Assist1Encounter:
			return TEXT("ASSIST 1 ENCOUNTER");
		case EABTSM11TrajectorySemanticLeg::Assist1ToAssist2:
			return TEXT("COAST 1 -> 2");
		case EABTSM11TrajectorySemanticLeg::Assist2Encounter:
			return TEXT("ASSIST 2 ENCOUNTER");
		case EABTSM11TrajectorySemanticLeg::Assist2ToAssist3:
			return TEXT("COAST 2 -> 3");
		case EABTSM11TrajectorySemanticLeg::Assist3Encounter:
			return TEXT("ASSIST 3 ENCOUNTER");
		case EABTSM11TrajectorySemanticLeg::Assist3ToTarget:
			return TEXT("ASSIST 3 -> UFO");
		case EABTSM11TrajectorySemanticLeg::TargetApproach:
			return TEXT("UFO APPROACH");
		default:
			return TEXT("NO TRAJECTORY SEGMENT");
		}
	}

	const TCHAR* ProbeRemapLabel(const EABTSM11ProbeRemapStatus Status)
	{
		switch (Status)
		{
		case EABTSM11ProbeRemapStatus::ExactSemanticLeg:
			return TEXT("TRACKING SAME PHASE");
		case EABTSM11ProbeRemapStatus::ClosestMissFallback:
			return TEXT("CLOSEST MISS TO FROZEN BODY");
		case EABTSM11ProbeRemapStatus::TrajectoryEndedBeforeLeg:
			return TEXT("TRAJECTORY ENDS BEFORE THIS LEG");
		default:
			return TEXT("WAITING FOR CURRENT TRAJECTORY");
		}
	}
}

AABTSM11FinaleHUD::AABTSM11FinaleHUD()
{
	static ConstructorHelpers::FObjectFinder<UTexture2D> SelectIcon(
		TEXT("/Game/M11/UI/Buttons/T_M11_Button_Select.T_M11_Button_Select"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> MoveIcon(
		TEXT("/Game/M11/UI/Buttons/T_M11_Button_Move.T_M11_Button_Move"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> ResetViewIcon(
		TEXT("/Game/M11/UI/Buttons/T_M11_Button_ResetView.T_M11_Button_ResetView"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> RebaseIcon(
		TEXT("/Game/M11/UI/Buttons/T_M11_Button_Rebase.T_M11_Button_Rebase"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> AutoPipIcon(
		TEXT("/Game/M11/UI/Buttons/T_M11_Button_AutoPip.T_M11_Button_AutoPip"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> CoarseIcon(
		TEXT("/Game/M11/UI/Buttons/T_M11_Button_Coarse.T_M11_Button_Coarse"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> FineIcon(
		TEXT("/Game/M11/UI/Buttons/T_M11_Button_Fine.T_M11_Button_Fine"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> UltraFineIcon(
		TEXT("/Game/M11/UI/Buttons/T_M11_Button_UltraFine.T_M11_Button_UltraFine"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> LaunchIcon(
		TEXT("/Game/M11/UI/Buttons/T_M11_Button_Launch.T_M11_Button_Launch"));

	ButtonIcons = {
		SelectIcon.Object,
		MoveIcon.Object,
		ResetViewIcon.Object,
		RebaseIcon.Object,
		AutoPipIcon.Object,
		CoarseIcon.Object,
		FineIcon.Object,
		UltraFineIcon.Object,
		LaunchIcon.Object
	};
}

void AABTSM11FinaleHUD::DrawHUD()
{
	AABTSM11FinaleInteractionSystem* System =
		FindInteractionSystem();
	const bool bFinaleActive = System != nullptr && System->IsFinaleActive();
	if (bFinaleActive)
	{
		DrawFinaleLayer(*System);
	}
	else
	{
		CachedPipTrajectory.Reset();
		CancelFinaleHudCapture();
		bHudLayoutValid = false;
		// Inventory, party and earlier milestone HUD remain unchanged outside
		// the finale interaction.
		Super::DrawHUD();
	}
	// M11 owns the complete finale HUD. Earlier hotbar/party layers would cover
	// the launch console and are deliberately suppressed while it is active.
	// The deterministic failure fade remains the final compositing layer.
	if (bFinaleActive)
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
	FABTSM11FinaleHudVisualLayout Layout;
	bHudLayoutValid = ABTSM11BuildFinaleHudVisualLayout(
		HudCanvasSize,
		KnobRadiusViewportHeightFraction,
		MinimumKnobRadiusPixels,
		FMath::Max(MinimumKnobRadiusPixels, MaximumKnobRadiusPixels),
		Layout);
	if (!bHudLayoutValid)
	{
		return;
	}
	HudMissionStrip = Layout.MissionStrip;
	HudOrbitPanel = Layout.OrbitPanel;
	HudControlDeck = Layout.ControlDeck;
	HudPreviewBay = Layout.PreviewBay;
	HudDiagramCenter = Layout.DiagramCenter;
	HudDiagramRadius = Layout.DiagramRadius;
	HudKnobCenters = Layout.KnobCenters;
	HudKnobRadius = Layout.KnobRadius;
	HudGearCoarse = Layout.GearCoarse;
	HudGearFine = Layout.GearFine;
	HudGearUltraFine = Layout.GearUltraFine;
	HudLaunchButton = Layout.LaunchButton;
	HudSelectButton = Layout.SelectButton;
	HudMoveButton = Layout.MoveButton;
	HudResetViewButton = Layout.ResetViewButton;
	HudRebasePipButton = Layout.RebasePipButton;
	HudFollowAutoButton = Layout.FollowAutoButton;
	bHudCompactLayout = Layout.bCompact;
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
			TrajectoryHitRadiusPixels,
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
	if (HudCapture.GetCapture() == EABTSM11FinaleHudCapture::None)
	{
		HoveredTrajectoryHit = FABTSM11TrajectoryHit();
		if (OverviewMode == EABTSM11OverviewInteractionMode::Select
			&& IsInsideDiagram(HudPosition))
		{
			ABTSM11HitTestOverviewTrajectory(
				System.GetHudOverviewProjection(),
				FVector2D(
					HudPosition.X,
					HudDiagramCenter.Y * 2.0f - HudPosition.Y),
				HudDiagramCenter,
				HudDiagramRadius,
				TrajectoryHitRadiusPixels,
				HoveredTrajectoryHit,
				System.HasHudTrajectoryProbe()
					? System.GetHudTrajectoryProbe().Leg
					: EABTSM11TrajectorySemanticLeg::Invalid);
		}
		return false;
	}
	switch (HudCapture.GetCapture())
	{
	case EABTSM11FinaleHudCapture::AdjustYaw:
		return System.ApplyHudControlDrag(
			EABTSM11FinaleControlAxis::Yaw,
			(Delta.X - Delta.Y) * 0.5 * KnobDragSensitivity,
			HudSpeedGear);
	case EABTSM11FinaleHudCapture::AdjustPitch:
		return System.ApplyHudControlDrag(
			EABTSM11FinaleControlAxis::Pitch,
			(Delta.X - Delta.Y) * 0.5 * KnobDragSensitivity,
			HudSpeedGear);
	case EABTSM11FinaleHudCapture::AdjustPower:
		return System.ApplyHudControlDrag(
			EABTSM11FinaleControlAxis::Power,
			(Delta.X - Delta.Y) * 0.5 * KnobDragSensitivity,
			HudSpeedGear);
	case EABTSM11FinaleHudCapture::PanOverview:
		return System.PanHudOverview(FVector2d(
			Delta.X / FMath::Max(HudDiagramRadius, 1.0f)
				* OverviewPanSensitivity,
			Delta.Y / FMath::Max(HudDiagramRadius, 1.0f)
				* OverviewPanSensitivity));
	case EABTSM11FinaleHudCapture::RotateOverview:
		return System.RotateHudOverview(
			Delta.X * OverviewOrbitDegreesPerPixel,
			Delta.Y * OverviewOrbitDegreesPerPixel);
	case EABTSM11FinaleHudCapture::ScrubTrajectoryProbe:
		return ABTSM11HitTestOverviewTrajectory(
			System.GetHudOverviewProjection(),
			FVector2D(
				HudPosition.X,
				HudDiagramCenter.Y * 2.0f - HudPosition.Y),
			HudDiagramCenter,
			HudDiagramRadius,
			TrajectoryScrubHitRadiusPixels,
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
	HoveredTrajectoryHit = FABTSM11TrajectoryHit();
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
		// Preserve the established knob trim direction while overview zoom
		// consumes the raw wheel sign (up = zoom in).
		return System.ApplyHudControlWheel(
			static_cast<EABTSM11FinaleControlAxis>(KnobIndex),
			-WheelSteps * KnobWheelSensitivity,
			HudSpeedGear);
	}
	if (IsInsideDiagram(HudPosition)
		&& OverviewMode == EABTSM11OverviewInteractionMode::Move)
	{
		return System.ZoomHudOverview(
			ABTSM11ResolveOverviewWheelZoomMultiplier(
				OverviewZoomPerWheelStep,
				WheelSteps));
	}
	return true;
}

void AABTSM11FinaleHUD::CancelFinaleHudCapture()
{
	HudCapture.CancelForFocusLoss();
	PendingTrajectoryHit = FABTSM11TrajectoryHit();
	HoveredTrajectoryHit = FABTSM11TrajectoryHit();
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
	if (!bHudLayoutValid)
	{
		return;
	}
	HudTheme = FABTSUITheme::Get();
	DrawMissionStrip(System);
	DrawFacetedPanel(
		HudOrbitPanel,
		HudTheme.PanelPrimary,
		HudTheme.PanelBorder,
		TEXT("ORBITAL SOLUTION"),
		HudTheme.AccentSecondary);
	if (System.IsAiming())
	{
		DrawFacetedPanel(
			HudControlDeck,
			HudTheme.PanelPrimary,
			HudTheme.PanelBorder,
			TEXT("FINAL APPROACH CONTROL"),
			HudTheme.AccentPrimary);
		DrawFacetedPanel(
			HudPreviewBay,
			HudTheme.PortraitBacking,
			HudTheme.PanelBorder,
			TEXT("TARGET MONITOR"),
			HudTheme.AccentSecondary);
	}
	DrawOrbitalDiagram(System, HudDiagramCenter, HudDiagramRadius);
	if (System.IsAiming())
	{
		DrawTargetPreview(System);
		DrawFinaleControlConsole(System);
	}
	else
	{
		// The PIP is an aiming aid. Once the release gesture has completed,
		// the authority flight camera owns spatial guidance.
		CachedPipTrajectory.Reset();
	}
	DrawStatus(System, HudDiagramCenter, HudDiagramRadius);
}

void AABTSM11FinaleHUD::DrawFacetedPanel(
	const FBox2D& Box,
	const FLinearColor& Fill,
	const FLinearColor& Border,
	const FString& SectionLabel,
	const FLinearColor& Accent)
{
	if (Canvas == nullptr || !Box.bIsValid)
	{
		return;
	}
	const FVector2D Size = Box.Max - Box.Min;
	const float Cut = FMath::Clamp(FMath::Min(Size.X, Size.Y) * 0.055f, 8.0f, 16.0f);
	const FVector2D P[8] = {
		FVector2D(Box.Min.X + Cut, Box.Min.Y),
		FVector2D(Box.Max.X - Cut, Box.Min.Y),
		FVector2D(Box.Max.X, Box.Min.Y + Cut),
		FVector2D(Box.Max.X, Box.Max.Y - Cut),
		FVector2D(Box.Max.X - Cut, Box.Max.Y),
		FVector2D(Box.Min.X + Cut, Box.Max.Y),
		FVector2D(Box.Min.X, Box.Max.Y - Cut),
		FVector2D(Box.Min.X, Box.Min.Y + Cut)};

	// Fill the same octagon that the border traces. Drawing a full rectangle
	// first would leave its square corners visible outside the diagonal cuts.
	const FVector2D PanelCenter = Box.GetCenter();
	const FLinearColor PanelFill = HudTheme.ApplyOpacity(Fill);
	TArray<FCanvasUVTri> FillTriangles;
	FillTriangles.Reserve(UE_ARRAY_COUNT(P));
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(P); ++Index)
	{
		FCanvasUVTri& Triangle = FillTriangles.AddDefaulted_GetRef();
		Triangle.V0_Pos = PanelCenter;
		Triangle.V1_Pos = P[Index];
		Triangle.V2_Pos = P[(Index + 1) % UE_ARRAY_COUNT(P)];
		Triangle.V0_UV = FVector2D::ZeroVector;
		Triangle.V1_UV = FVector2D::ZeroVector;
		Triangle.V2_UV = FVector2D::ZeroVector;
		Triangle.V0_Color = PanelFill;
		Triangle.V1_Color = PanelFill;
		Triangle.V2_Color = PanelFill;
	}
	const FTexture* FillTexture = Canvas->DefaultTexture != nullptr
		? Canvas->DefaultTexture->GetResource()
		: nullptr;
	if (FillTexture != nullptr)
	{
		FCanvasTriangleItem FillItem(FillTriangles, FillTexture);
		FillItem.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(FillItem);
	}

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(P); ++Index)
	{
		const FVector2D& A = P[Index];
		const FVector2D& B = P[(Index + 1) % UE_ARRAY_COUNT(P)];
		DrawLine(A.X, A.Y, B.X, B.Y,
			HudTheme.ApplyOpacity(Border), HudTheme.BorderThicknessPx);
	}
	DrawLine(
		Box.Min.X + Cut + 5.0f,
		Box.Min.Y + 26.0f,
		FMath::Min(Box.Max.X - Cut, Box.Min.X + 128.0f),
		Box.Min.Y + 26.0f,
		HudTheme.ApplyOpacity(Accent),
		2.0f);
	DrawText(
		SectionLabel,
		HudTheme.ApplyOpacity(HudTheme.TextMuted),
		Box.Min.X + Cut + 5.0f,
		Box.Min.Y + 8.0f,
		GEngine->GetSmallFont(),
		0.60f * HudTheme.TextScale,
		false);
}

void AABTSM11FinaleHUD::DrawMissionStrip(
	AABTSM11FinaleInteractionSystem& System)
{
	DrawFacetedPanel(
		HudMissionStrip,
		HudTheme.PanelPrimary,
		HudTheme.PanelBorder,
		TEXT("FINAL GRAVITY-ASSIST SEQUENCE"),
		HudTheme.AccentPrimary);
	const FABTSM11PrefixClassification& Classification =
		System.GetClassification();
	const AABTSM11FinaleSystem* FinaleSystem = System.GetFinaleSystem();
	const bool bCandidate = FinaleSystem != nullptr
		&& FinaleSystem->IsEditorCandidateMode();
	const FString Chain = FString::Printf(
		TEXT("F1 %s   F2 %s   F3 %s   F4 %s"),
		Classification.IsF(1) ? TEXT("LOCK") : TEXT("--"),
		Classification.IsF(2) ? TEXT("LOCK") : TEXT("--"),
		Classification.IsF(3) ? TEXT("LOCK") : TEXT("--"),
		Classification.IsF(4) ? TEXT("READY") : TEXT("--"));
	DrawText(
		Chain,
		HudTheme.ApplyOpacity(
			Classification.IsF(4) ? HudTheme.Success : HudTheme.TextPrimary),
		HudMissionStrip.GetCenter().X - (bHudCompactLayout ? 112.0f : 145.0f),
		HudMissionStrip.Min.Y + 15.0f,
		GEngine->GetSmallFont(),
		(bHudCompactLayout ? 0.68f : 0.78f) * HudTheme.TextScale,
		false);
	const FString State = bCandidate
		? TEXT("EDITOR CANDIDATE / NOT CERTIFIED")
		: System.IsReleasePending()
			? TEXT("FREEZING EXACT RELEASE")
			: System.IsAiming() ? TEXT("AIMING") : TEXT("FLIGHT");
	DrawText(
		State,
		HudTheme.ApplyOpacity(bCandidate
			? HudTheme.Warning
			: Classification.IsF(4) ? HudTheme.Success : HudTheme.AccentSecondary),
		HudMissionStrip.Max.X - (bHudCompactLayout ? 218.0f : 272.0f),
		HudMissionStrip.Min.Y + 15.0f,
		GEngine->GetSmallFont(),
		(bHudCompactLayout ? 0.62f : 0.72f) * HudTheme.TextScale,
		false);
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
		HudTheme.ApplyOpacity(HudTheme.PortraitBacking.CopyWithNewOpacity(0.88f)));
	Background.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Background);
	DrawCircleOutline(
		Center,
		Radius,
		HudTheme.ApplyOpacity(HudTheme.AccentSecondary),
		HudTheme.BorderThicknessPx);
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
		DrawDiagramCircleOutline(
			Center,
			Radius,
			Primary.Center,
			PrimaryRadius / FMath::Max(Radius, 1.0f),
			FLinearColor(0.35f, 0.72f, 1.0f, 0.9f),
			1.5f,
			48);

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
		const FABTSM11TrajectoryHit& ActiveHit =
			PendingTrajectoryHit.bValid
				? PendingTrajectoryHit
				: HoveredTrajectoryHit;
		if (OverviewMode == EABTSM11OverviewInteractionMode::Select
			&& ActiveHit.bValid
			&& !HudProjection.Trajectory.IsEmpty())
		{
			const FABTSM11OverviewProjectedPoint* HoverPoint = nullptr;
			double BestTimeDelta = TNumericLimits<double>::Max();
			for (const FABTSM11OverviewProjectedPoint& Point
				: HudProjection.Trajectory)
			{
				const double TimeDelta = FMath::Abs(
					Point.TimeSeconds - ActiveHit.TimeSeconds);
				if (TimeDelta < BestTimeDelta)
				{
					BestTimeDelta = TimeDelta;
					HoverPoint = &Point;
				}
			}
			if (HoverPoint != nullptr)
			{
				const FVector2D Marker = ToScreen(
					Center,
					Radius,
					HoverPoint->Position);
				const FLinearColor HoverColor = ActiveHit.bHiddenByBody
					? FLinearColor(0.62f, 0.72f, 0.82f, 0.92f)
					: FLinearColor(1.0f, 0.86f, 0.32f, 1.0f);
				DrawDiagramCircleOutline(
					Center,
					Radius,
					HoverPoint->Position,
					7.0 / FMath::Max<double>(Radius, 1.0),
					HoverColor,
					1.8f,
					18);
				const FString HoverLabel = SemanticLegLabel(ActiveHit.Leg);
				const FVector2D HoverLabelPosition =
					ResolveDiagramTextPosition(
						Center,
						Radius,
						HoverLabel,
						0.58f,
						Marker + FVector2D(10.0f, -18.0f));
				DrawText(
					HoverLabel,
					HoverColor,
					HoverLabelPosition.X,
					HoverLabelPosition.Y,
					GEngine->GetSmallFont(),
					0.58f,
					false);
			}
		}
		if (System.HasHudTrajectoryProbe())
		{
			const FVector2d Reference = HudView.Project(
				System.GetHudTrajectoryProbe().ReferenceLocalPosition);
			const double ReferenceHalfSize =
				6.0 / FMath::Max<double>(Radius, 1.0);
			DrawDiagramSegment(
				Center,
				Radius,
				Reference - FVector2d(ReferenceHalfSize, 0.0),
				Reference + FVector2d(ReferenceHalfSize, 0.0),
				FLinearColor::White,
				1.6f,
				false);
			DrawDiagramSegment(
				Center,
				Radius,
				Reference - FVector2d(0.0, ReferenceHalfSize),
				Reference + FVector2d(0.0, ReferenceHalfSize),
				FLinearColor::White,
				1.6f,
				false);
			if (System.GetHudProbeProjection().bValid)
			{
				DrawDiagramCircleOutline(
					Center,
					Radius,
					HudView.Project(
						System.GetHudProbeProjection().PositionCM),
					5.0 / FMath::Max<double>(Radius, 1.0),
					FLinearColor(0.25f, 1.0f, 0.82f),
					1.8f,
					16);
			}
		}
		DrawDiagramEdgeLabel(
			Center,
			Radius,
			OverviewMode == EABTSM11OverviewInteractionMode::Select
				? TEXT("ORBIT OVERVIEW / SELECT")
				: TEXT("ORBIT OVERVIEW / MOVE"),
			FLinearColor(0.72f, 0.90f, 1.0f),
			0.8f,
			true);
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

	DrawDiagramEdgeLabel(
		Center,
		Radius,
		TEXT("ORBIT OVERVIEW"),
		FLinearColor(0.72f, 0.90f, 1.0f),
		0.8f,
		true);
	if (System.GetPreviewPlaybackPlan()
		.bUsesVisibleTerminalTransfer)
	{
		DrawDiagramEdgeLabel(
			Center,
			Radius,
			TEXT("AMBER: VISIBLE TERMINAL TRANSFER"),
			FLinearColor(1.0f, 0.62f, 0.18f),
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
			DrawDiagramEdgeLabel(
				Center,
				Radius,
				TEXT("RAW 1X PLAYBACK / QUALIFIED ENDPOINT"),
				FLinearColor(1.0f, 0.72f, 0.24f),
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
	UTexture2D* Icon,
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
			? HudTheme.ApplyOpacity(Accent.CopyWithNewOpacity(0.82f))
			: HudTheme.ApplyOpacity(HudTheme.SlotNormal),
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
		DrawLine(
			A.X, A.Y, B.X, B.Y,
			HudTheme.ApplyOpacity(
				bActive ? Accent : Accent.CopyWithNewOpacity(0.68f)),
			bActive ? HudTheme.BorderThicknessPx : 1.0f);
	}
	if (Icon != nullptr && Icon->GetResource() != nullptr)
	{
		const float IconExtent = FMath::Max(
			12.0f,
			FMath::Min(Size.X - 4.0f, Size.Y));
		const FVector2D IconSize(IconExtent, IconExtent);
		const FVector2D IconPosition = Box.Min + (Size - IconSize) * 0.5f;
		const bool bMuted = Accent.Equals(HudTheme.Disabled, 0.02f);
		FCanvasTileItem IconTile(
			IconPosition,
			Icon->GetResource(),
			IconSize,
			HudTheme.ApplyOpacity(
				bMuted
					? FLinearColor(0.62f, 0.68f, 0.74f, 0.62f)
					: FLinearColor::White));
		IconTile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(IconTile);
	}
	else
	{
		DrawText(
			Label,
			HudTheme.ApplyOpacity(HudTheme.TextPrimary),
			Box.Min.X + 7.0f,
			Box.Min.Y + 6.0f,
			GEngine->GetSmallFont(),
			0.68f * HudTheme.TextScale,
			false);
	}
}

UTexture2D* AABTSM11FinaleHUD::GetButtonIcon(const int32 IconIndex) const
{
	return ButtonIcons.IsValidIndex(IconIndex) ? ButtonIcons[IconIndex] : nullptr;
}

void AABTSM11FinaleHUD::DrawKnob(
	const FVector2D& Center,
	const float Radius,
	const FString& Label,
	const double ValueAlpha,
	const FString& ValueText,
	const bool bCaptured,
	const EABTSM11F4GuidanceDirection GuidanceDirection,
	const double GuidanceTargetAlpha,
	const FString& GuidanceTargetText,
	const bool bHorizontalGuidance,
	const bool bStrictF4)
{
	const double ClampedValueAlpha = FMath::Clamp(ValueAlpha, 0.0, 1.0);
	constexpr double DialMinimumAngle = -135.0;
	constexpr double DialMaximumAngle = 135.0;
	constexpr int32 DialArcSegments = 36;
	constexpr int32 DialTickCount = 11;
	const auto DialDirection = [](const double AngleRadians)
	{
		return FVector2D(
			static_cast<float>(FMath::Cos(AngleRadians)),
			static_cast<float>(FMath::Sin(AngleRadians)));
	};

	// A nested bezel, inset face and double outline read as one physical
	// instrument without changing the original interaction radius.
	FCanvasNGonItem Bezel(
		Center,
		FVector2D(Radius + 2.0f, Radius + 2.0f),
		48,
		HudTheme.ApplyOpacity(
			bCaptured
				? HudTheme.AccentSecondary.CopyWithNewOpacity(0.64f)
				: HudTheme.PanelBorder.CopyWithNewOpacity(0.86f)));
	Bezel.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Bezel);

	FCanvasNGonItem Fill(
		Center,
		FVector2D(Radius - 2.0f, Radius - 2.0f),
		48,
		bCaptured
			? HudTheme.ApplyOpacity(HudTheme.SlotSelected.CopyWithNewOpacity(0.96f))
			: HudTheme.ApplyOpacity(HudTheme.SlotNormal.CopyWithNewOpacity(0.96f)));
	Fill.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Fill);

	DrawCircleOutline(
		Center,
		Radius - 2.0f,
		bCaptured
			? HudTheme.ApplyOpacity(HudTheme.AccentSecondary)
			: HudTheme.ApplyOpacity(HudTheme.SlotBorder),
		bCaptured ? HudTheme.BorderThicknessPx : 1.2f,
		48);
	DrawCircleOutline(
		Center,
		Radius - 6.0f,
		HudTheme.ApplyOpacity(HudTheme.PanelBorder.CopyWithNewOpacity(0.46f)),
		1.0f,
		48);

	const float ArcRadius = Radius * 0.80f;
	for (int32 SegmentIndex = 0; SegmentIndex < DialArcSegments; ++SegmentIndex)
	{
		const double SegmentStartAlpha =
			static_cast<double>(SegmentIndex) / DialArcSegments;
		const double SegmentEndAlpha =
			static_cast<double>(SegmentIndex + 1) / DialArcSegments;
		const double StartAngle = FMath::DegreesToRadians(FMath::Lerp(
			DialMinimumAngle,
			DialMaximumAngle,
			SegmentStartAlpha));
		const double EndAngle = FMath::DegreesToRadians(FMath::Lerp(
			DialMinimumAngle,
			DialMaximumAngle,
			SegmentEndAlpha));
		const FVector2D Start = Center + DialDirection(StartAngle) * ArcRadius;
		const FVector2D End = Center + DialDirection(EndAngle) * ArcRadius;
		const bool bProgressSegment = SegmentEndAlpha <= ClampedValueAlpha + 0.001;
		DrawLine(
			Start.X,
			Start.Y,
			End.X,
			End.Y,
			HudTheme.ApplyOpacity(
				bProgressSegment
					? HudTheme.AccentSecondary.CopyWithNewOpacity(0.92f)
					: HudTheme.SlotBorder.CopyWithNewOpacity(0.42f)),
			bProgressSegment ? 3.1f : 2.0f);
	}

	for (int32 TickIndex = 0; TickIndex < DialTickCount; ++TickIndex)
	{
		const double TickAlpha =
			static_cast<double>(TickIndex) / (DialTickCount - 1);
		const double TickAngle = FMath::DegreesToRadians(FMath::Lerp(
			DialMinimumAngle,
			DialMaximumAngle,
			TickAlpha));
		const FVector2D Direction = DialDirection(TickAngle);
		const bool bMajorTick = TickIndex == 0
			|| TickIndex == DialTickCount / 2
			|| TickIndex == DialTickCount - 1;
		const FVector2D TickInner = Center + Direction * Radius
			* (bMajorTick ? 0.60f : 0.66f);
		const FVector2D TickOuter = Center + Direction * Radius * 0.71f;
		DrawLine(
			TickInner.X,
			TickInner.Y,
			TickOuter.X,
			TickOuter.Y,
			HudTheme.ApplyOpacity(
				HudTheme.TextMuted.CopyWithNewOpacity(
					bMajorTick ? 0.78f : 0.48f)),
			bMajorTick ? 1.8f : 1.0f);
	}

	if (GuidanceTargetAlpha >= 0.0 && GuidanceTargetAlpha <= 1.0)
	{
		const double TargetAngle = FMath::DegreesToRadians(FMath::Lerp(
			DialMinimumAngle,
			DialMaximumAngle,
			GuidanceTargetAlpha));
		const FVector2D TargetDirection = DialDirection(TargetAngle);
		const FVector2D TargetInner =
			Center + TargetDirection * Radius * 0.68f;
		const FVector2D TargetOuter =
			Center + TargetDirection * Radius * 0.94f;
		DrawLine(
			TargetInner.X,
			TargetInner.Y,
			TargetOuter.X,
			TargetOuter.Y,
			HudTheme.ApplyOpacity(HudTheme.Success),
			2.6f);
	}

	const double Angle = FMath::Lerp(
		FMath::DegreesToRadians(DialMinimumAngle),
		FMath::DegreesToRadians(DialMaximumAngle),
		ClampedValueAlpha);
	const FVector2D Needle = DialDirection(Angle);
	const FVector2D NeedleEnd = Center + Needle * Radius * 0.67f;
	DrawLine(
		Center.X + 1.0f,
		Center.Y + 1.0f,
		NeedleEnd.X + 1.0f,
		NeedleEnd.Y + 1.0f,
		HudTheme.ApplyOpacity(HudTheme.PanelPrimary.CopyWithNewOpacity(0.92f)),
		5.0f);
	DrawLine(
		Center.X,
		Center.Y,
		NeedleEnd.X,
		NeedleEnd.Y,
		HudTheme.ApplyOpacity(HudTheme.AccentPrimary),
		2.8f);

	FCanvasNGonItem HubOuter(
		Center,
		FVector2D(4.8f, 4.8f),
		20,
		HudTheme.ApplyOpacity(HudTheme.PanelPrimary));
	HubOuter.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(HubOuter);
	FCanvasNGonItem HubCore(
		Center,
		FVector2D(2.6f, 2.6f),
		16,
		HudTheme.ApplyOpacity(HudTheme.AccentPrimary));
	HubCore.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(HubCore);

	const auto DrawCenteredDialText = [this, &Center](
		const FString& Text,
		const float Y,
		const float Scale,
		const FLinearColor& Color)
	{
		float TextWidth = 0.0f;
		float TextHeight = 0.0f;
		Canvas->StrLen(
			GEngine->GetSmallFont(),
			Text,
			TextWidth,
			TextHeight,
			true);
		const float X = Center.X - TextWidth * Scale * 0.5f;
		DrawText(
			Text,
			HudTheme.ApplyOpacity(HudTheme.PanelPrimary.CopyWithNewOpacity(0.88f)),
			X + 1.0f,
			Y + 1.0f,
			GEngine->GetSmallFont(),
			Scale,
			false);
		DrawText(
			Text,
			HudTheme.ApplyOpacity(Color),
			X,
			Y,
			GEngine->GetSmallFont(),
			Scale,
			false);
	};
	DrawCenteredDialText(
		Label,
		Center.Y - Radius - 10.0f,
		0.64f * HudTheme.TextScale,
		HudTheme.TextMuted);
	DrawCenteredDialText(
		ValueText,
		Center.Y + Radius + 4.0f,
		0.62f * HudTheme.TextScale,
		HudTheme.TextPrimary);
	if (!GuidanceTargetText.IsEmpty())
	{
		DrawText(
			GuidanceTargetText,
			bStrictF4
				? HudTheme.ApplyOpacity(HudTheme.Success)
				: HudTheme.ApplyOpacity(HudTheme.AccentSecondary),
			Center.X - Radius * 0.72f,
			Center.Y + Radius + 15.0f,
			GEngine->GetSmallFont(),
			0.54f * HudTheme.TextScale,
			false);
	}
	if (bStrictF4)
	{
		DrawCircleOutline(
			Center,
			Radius + 5.0f,
			HudTheme.ApplyOpacity(HudTheme.Success),
			2.4f,
			40);
		return;
	}
	if (GuidanceDirection == EABTSM11F4GuidanceDirection::Aligned)
	{
		return;
	}

	const float Sign = GuidanceDirection
		== EABTSM11F4GuidanceDirection::Increase
		? 1.0f
		: -1.0f;
	// Positive drag is right for Yaw and up for Pitch/Power.
	const FVector2D Direction = bHorizontalGuidance
		? FVector2D(Sign, 0.0f)
		: FVector2D(0.0f, -Sign);
	const FVector2D Perpendicular(-Direction.Y, Direction.X);
	const FVector2D ShaftStart = Center + Direction * (Radius + 2.0f);
	const FVector2D Tip = Center + Direction * (Radius + 19.0f);
	const FVector2D HeadBase = Tip - Direction * 8.0f;
	const FLinearColor GuidanceColor = HudTheme.ApplyOpacity(HudTheme.Success);
	DrawLine(
		ShaftStart.X,
		ShaftStart.Y,
		Tip.X,
		Tip.Y,
		GuidanceColor,
		3.4f);
	DrawLine(
		Tip.X,
		Tip.Y,
		HeadBase.X + Perpendicular.X * 5.5f,
		HeadBase.Y + Perpendicular.Y * 5.5f,
		GuidanceColor,
		3.0f);
	DrawLine(
		Tip.X,
		Tip.Y,
		HeadBase.X - Perpendicular.X * 5.5f,
		HeadBase.Y - Perpendicular.Y * 5.5f,
		GuidanceColor,
		3.0f);
}

void AABTSM11FinaleHUD::DrawPipEdgeIndicator(
	const FVector2D& Position,
	const FVector2D& Size,
	const FVector2d& PointUV,
	const FLinearColor& Color,
	const FString& Label)
{
	FABTSM11PipEdgeIndicator Indicator;
	if (!ABTSM11BuildPipEdgeIndicator(
			PointUV,
			PipEdgeMarginUV,
			Indicator)
		|| !Indicator.bVisible)
	{
		return;
	}

	const FVector2D Anchor = Position + FVector2D(
		static_cast<float>(Indicator.AnchorUV.X) * Size.X,
		static_cast<float>(Indicator.AnchorUV.Y) * Size.Y);
	FVector2D Direction(
		static_cast<float>(Indicator.DirectionUV.X) * Size.X,
		static_cast<float>(Indicator.DirectionUV.Y) * Size.Y);
	Direction.Normalize();
	if (Direction.IsNearlyZero())
	{
		return;
	}
	const FVector2D Perpendicular(-Direction.Y, Direction.X);
	const FVector2D Tip = Anchor;
	const FVector2D Base = Tip - Direction * 15.0f;
	const FVector2D Left = Base + Perpendicular * 7.0f;
	const FVector2D Right = Base - Perpendicular * 7.0f;
	DrawLine(Tip.X, Tip.Y, Left.X, Left.Y, Color, 2.5f);
	DrawLine(Tip.X, Tip.Y, Right.X, Right.Y, Color, 2.5f);
	DrawLine(Left.X, Left.Y, Right.X, Right.Y, Color, 1.5f);
	if (!Label.IsEmpty())
	{
		const FVector2D RawLabel = Base
			- Direction * 13.0f
			+ Perpendicular * 8.0f;
		DrawText(
			Label,
			Color,
			FMath::Clamp(
				RawLabel.X,
				Position.X + 6.0f,
				Position.X + Size.X - 92.0f),
			FMath::Clamp(
				RawLabel.Y,
				Position.Y + 30.0f,
				Position.Y + Size.Y - 34.0f),
			GEngine->GetSmallFont(),
			0.58f,
			false);
	}
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
	const FABTSM11F4GuidanceTarget& Guidance =
		System.GetF4GuidanceTarget();
	const bool bGuidanceValid = Guidance.IsValid(Model);
	const bool bStrictF4 = System.IsCurrentInputStrictF4();
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
		HudCapture.GetCapture() == EABTSM11FinaleHudCapture::AdjustYaw,
		bGuidanceValid
			? Guidance.GetDirection(Input, EABTSM11FinaleControlAxis::Yaw)
			: EABTSM11F4GuidanceDirection::Aligned,
		bGuidanceValid
			? Alpha(
				Guidance.Input.YawDegrees,
				Model.MinimumYawDegrees,
				Model.MaximumYawDegrees)
			: -1.0,
		bGuidanceValid
			? FString::Printf(
				TEXT("TGT %+.3f"), Guidance.Input.YawDegrees)
			: FString(),
		true,
		bStrictF4);
	DrawKnob(
		HudKnobCenters[1],
		HudKnobRadius,
		TEXT("PITCH"),
		Alpha(Input.PitchDegrees, Model.MinimumPitchDegrees, Model.MaximumPitchDegrees),
		FString::Printf(TEXT("%+.3f deg"), Input.PitchDegrees),
		HudCapture.GetCapture() == EABTSM11FinaleHudCapture::AdjustPitch,
		bGuidanceValid
			? Guidance.GetDirection(Input, EABTSM11FinaleControlAxis::Pitch)
			: EABTSM11F4GuidanceDirection::Aligned,
		bGuidanceValid
			? Alpha(
				Guidance.Input.PitchDegrees,
				Model.MinimumPitchDegrees,
				Model.MaximumPitchDegrees)
			: -1.0,
		bGuidanceValid
			? FString::Printf(
				TEXT("TGT %+.3f"), Guidance.Input.PitchDegrees)
			: FString(),
		false,
		bStrictF4);
	DrawKnob(
		HudKnobCenters[2],
		HudKnobRadius,
		TEXT("POWER"),
		Alpha(Input.Power, Model.MinimumPower, Model.MaximumPower),
		FString::Printf(TEXT("%.4f"), Input.Power),
		HudCapture.GetCapture() == EABTSM11FinaleHudCapture::AdjustPower,
		bGuidanceValid
			? Guidance.GetDirection(Input, EABTSM11FinaleControlAxis::Power)
			: EABTSM11F4GuidanceDirection::Aligned,
		bGuidanceValid
			? Alpha(
				Guidance.Input.Power,
				Model.MinimumPower,
				Model.MaximumPower)
			: -1.0,
		bGuidanceValid
			? FString::Printf(TEXT("TGT %.4f"), Guidance.Input.Power)
			: FString(),
		false,
		bStrictF4);

	const FString GuidanceStatus = bStrictF4
		? TEXT("F4 LOCKED - READY TO LAUNCH")
		: bGuidanceValid
			? TEXT("FOLLOW GREEN ARROWS TO THE F4 CENTRE")
			: System.IsF4GuidanceInFlight()
				? TEXT("CALCULATING F4 GUIDANCE...")
				: TEXT("F4 GUIDANCE UNAVAILABLE");
	DrawText(
		GuidanceStatus,
		bStrictF4
			? HudTheme.ApplyOpacity(HudTheme.Success)
			: bGuidanceValid || System.IsF4GuidanceInFlight()
				? HudTheme.ApplyOpacity(HudTheme.AccentSecondary)
				: HudTheme.ApplyOpacity(HudTheme.Danger),
		HudKnobCenters[1].X - 138.0f,
		HudKnobCenters[1].Y - HudKnobRadius - 32.0f,
		GEngine->GetSmallFont(),
		0.66f * HudTheme.TextScale,
		false);

	const FLinearColor Accent = HudTheme.AccentSecondary;
	DrawConsoleButton(HudGearCoarse, TEXT("1x"),
		GetButtonIcon(static_cast<int32>(EM11FinaleButtonIcon::Coarse)),
		HudSpeedGear == EABTSM11ControlSpeedGear::Coarse, Accent);
	DrawConsoleButton(HudGearFine, TEXT("0.1x"),
		GetButtonIcon(static_cast<int32>(EM11FinaleButtonIcon::Fine)),
		HudSpeedGear == EABTSM11ControlSpeedGear::Fine, Accent);
	DrawConsoleButton(HudGearUltraFine, TEXT("0.01x"),
		GetButtonIcon(static_cast<int32>(EM11FinaleButtonIcon::UltraFine)),
		HudSpeedGear == EABTSM11ControlSpeedGear::UltraFine, Accent);
	DrawConsoleButton(
		HudLaunchButton,
		TEXT("LAUNCH"),
		GetButtonIcon(static_cast<int32>(EM11FinaleButtonIcon::Launch)),
		HudCapture.GetCapture() == EABTSM11FinaleHudCapture::LaunchButton,
		System.IsCurrentInputStrictF4()
			? HudTheme.Success
			: HudTheme.AccentPrimary);
	DrawConsoleButton(HudSelectButton, TEXT("SELECT"),
		GetButtonIcon(static_cast<int32>(EM11FinaleButtonIcon::Select)),
		OverviewMode == EABTSM11OverviewInteractionMode::Select, Accent);
	DrawConsoleButton(HudMoveButton, TEXT("MOVE"),
		GetButtonIcon(static_cast<int32>(EM11FinaleButtonIcon::Move)),
		OverviewMode == EABTSM11OverviewInteractionMode::Move, Accent);
	DrawConsoleButton(
		HudResetViewButton,
		TEXT("RESET VIEW"),
		GetButtonIcon(static_cast<int32>(EM11FinaleButtonIcon::ResetView)),
		false,
		OverviewMode == EABTSM11OverviewInteractionMode::Move
			? Accent
			: HudTheme.Disabled);
	DrawConsoleButton(HudRebasePipButton, TEXT("REBASE"),
		GetButtonIcon(static_cast<int32>(EM11FinaleButtonIcon::Rebase)), false,
		System.HasHudTrajectoryProbe()
			? HudTheme.Success
			: HudTheme.Disabled);
	DrawConsoleButton(HudFollowAutoButton, TEXT("AUTO PIP"),
		GetButtonIcon(static_cast<int32>(EM11FinaleButtonIcon::AutoPip)),
		!System.HasHudTrajectoryProbe(), Accent);
}

bool AABTSM11FinaleHUD::ResolveTargetPreviewLayout(
	const UTextureRenderTarget2D& RenderTarget,
	FVector2D& OutPosition,
	FVector2D& OutSize) const
{
	OutPosition = FVector2D::ZeroVector;
	OutSize = FVector2D::ZeroVector;
	if (Canvas == nullptr || RenderTarget.SizeX <= 0 || RenderTarget.SizeY <= 0)
	{
		return false;
	}

	const float RenderAspect =
		static_cast<float>(RenderTarget.SizeX)
		/ static_cast<float>(RenderTarget.SizeY);
	const float BayInset = FMath::Max(6.0f, HudTheme.CellInsetPx * 2.0f);
	constexpr float BayHeaderHeight = 28.0f;
	const FBox2D PreviewContentBox(
		HudPreviewBay.Min + FVector2D(BayInset, BayHeaderHeight + BayInset),
		HudPreviewBay.Max - FVector2D(BayInset, BayInset));
	const FVector2D PreviewContentSize =
		PreviewContentBox.Max - PreviewContentBox.Min;
	float PreviewWidth = FMath::Min3<float>(
		PipMaximumWidthPixels,
		static_cast<float>(Canvas->SizeX) * PipViewportWidthFraction,
		FMath::Max(0.0f, static_cast<float>(PreviewContentSize.X)));
	float PreviewHeight = PreviewWidth / RenderAspect;
	const float MaximumHeight = FMath::Min3<float>(
		PipMaximumHeightPixels,
		static_cast<float>(Canvas->SizeY) * PipViewportHeightFraction,
		FMath::Max(0.0f, static_cast<float>(PreviewContentSize.Y)));
	if (PreviewHeight > MaximumHeight)
	{
		PreviewHeight = MaximumHeight;
		PreviewWidth = PreviewHeight * RenderAspect;
	}
	if (PreviewWidth <= 0.0f || PreviewHeight <= 0.0f)
	{
		return false;
	}

	OutSize = FVector2D(PreviewWidth, PreviewHeight);
	// Aspect-ratio clamping can leave spare room on either axis. Center the
	// preview in the body below the panel header instead of pinning that spare
	// room to the left/top with a bottom-right anchor.
	OutPosition = PreviewContentBox.GetCenter() - OutSize * 0.5f;
	return true;
}

void AABTSM11FinaleHUD::DrawTargetPreviewFrame(
	const FVector2D& Position,
	const FVector2D& Size,
	const FString& Title,
	const FString& Subtitle)
{
	if (Canvas == nullptr)
	{
		return;
	}

	DrawRect(
		HudTheme.ApplyOpacity(HudTheme.PanelSecondary),
		Position.X,
		Position.Y,
		Size.X,
		26.0f);
	DrawText(
		Title,
		HudTheme.ApplyOpacity(HudTheme.TextPrimary),
		Position.X + 8.0f,
		Position.Y + 6.0f,
		GEngine->GetSmallFont(),
		0.72f * HudTheme.TextScale,
		false);
	DrawText(
		Subtitle,
		HudTheme.ApplyOpacity(HudTheme.TextMuted),
		Position.X + 8.0f,
		Position.Y + Size.Y - 18.0f,
		GEngine->GetSmallFont(),
		0.58f * HudTheme.TextScale,
		false);
	for (int32 Edge = 0; Edge < 4; ++Edge)
	{
		const FVector2D A = Edge == 0 ? Position
			: Edge == 1 ? FVector2D(Position.X + Size.X, Position.Y)
			: Edge == 2 ? Position + Size
			: FVector2D(Position.X, Position.Y + Size.Y);
		const FVector2D B = Edge == 0
			? FVector2D(Position.X + Size.X, Position.Y)
			: Edge == 1 ? Position + Size
			: Edge == 2 ? FVector2D(Position.X, Position.Y + Size.Y)
			: Position;
		DrawLine(
			A.X,
			A.Y,
			B.X,
			B.Y,
			HudTheme.ApplyOpacity(HudTheme.AccentSecondary),
			HudTheme.BorderThicknessPx);
	}
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
	FVector2D Position;
	FVector2D Size;
	if (!ResolveTargetPreviewLayout(*RenderTarget, Position, Size))
	{
		CachedPipTrajectory.Reset();
		return;
	}
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
					PipCurrentLineThickness);
		}

		for (const FABTSM11TargetPipTrajectoryPoint& Point
			: CachedPipTrajectory.Points)
		{
			if (!Point.bClosestApproach || !Point.bInFront)
			{
				continue;
			}
				const FLinearColor MarkerColor =
					System.GetPreviewSelection().bEnteredTargetRegion
						? FLinearColor(0.38f, 1.0f, 0.58f, 1.0f)
						: FLinearColor(1.0f, 0.62f, 0.18f, 1.0f);
				FABTSM11PipEdgeIndicator Edge;
				ABTSM11BuildPipEdgeIndicator(
					FVector2d(Point.UV.X, Point.UV.Y),
					PipEdgeMarginUV,
					Edge);
				if (Edge.bVisible)
				{
					DrawPipEdgeIndicator(
						Position,
						Size,
						FVector2d(Point.UV.X, Point.UV.Y),
						MarkerColor,
						TEXT("CLOSEST"));
				}
				else
				{
					const FVector2D Marker = Position + Point.UV * Size;
					DrawCircleOutline(
						Marker,
						6.5f,
						MarkerColor,
						1.8f,
						18);
				}
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

	DrawTargetPreviewFrame(
		Position,
		Size,
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
		FString::Printf(
			TEXT("CYAN: CURRENT  /  RING: CLOSEST  /  DIST %.0f cm"),
			System.GetPreviewSelection().ClosestDistanceCM));
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
	FVector2D Position;
	FVector2D Size;
	if (!ResolveTargetPreviewLayout(RenderTarget, Position, Size))
	{
		return;
	}
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
	const auto DrawPipWorldCircle =
		[this, &ToUv, &Position, &Size, &View, RenderAspect](
			const FVector3d& WorldCenter,
			const double WorldRadius,
			const FLinearColor& Color,
			const float Thickness,
			const int32 SegmentCount)
	{
		if (WorldRadius <= 0.0 || View.HalfExtentCM <= 0.0)
		{
			return;
		}
		const FVector2D CenterUV = ToUv(WorldCenter);
		const float RadiusX = static_cast<float>(
			WorldRadius / View.HalfExtentCM * 0.5);
		const float RadiusY = RadiusX * RenderAspect;
		FVector2D Previous = CenterUV + FVector2D(RadiusX, 0.0f);
		for (int32 Index = 1; Index <= SegmentCount; ++Index)
		{
			const double Angle = UE_TWO_PI
				* static_cast<double>(Index)
				/ static_cast<double>(SegmentCount);
			FVector2D Current = CenterUV + FVector2D(
				FMath::Cos(Angle) * RadiusX,
				FMath::Sin(Angle) * RadiusY);
			FVector2D Start = Previous;
			FVector2D End = Current;
			if (ABTSM11ClipPipLineToRect(Start, End, 0.02f))
			{
				Start = Position + Start * Size;
				End = Position + End * Size;
				DrawLine(Start.X, Start.Y, End.X, End.Y, Color, Thickness);
			}
			Previous = Current;
		}
	};
	FVector3d ContextCenter;
	FVector3d ContextVelocity;
	double ContextVisualRadius = 0.0;
	double ContextInfluenceRadius = 0.0;
	const bool bHasContextGeometry =
		System.GetHudOrbitalScene().GetContextGeometry(
			Probe.ContextBodyIndex,
			Probe.bContextIsTarget,
			ContextCenter,
			ContextVelocity,
			ContextVisualRadius,
			ContextInfluenceRadius);
	if (bHasContextGeometry)
	{
		if (ContextInfluenceRadius > ContextVisualRadius)
		{
			DrawPipWorldCircle(
				ContextCenter,
				ContextInfluenceRadius,
				FLinearColor(0.42f, 0.74f, 1.0f, 0.24f),
				1.0f,
				64);
		}
		DrawPipWorldCircle(
			ContextCenter,
			ContextVisualRadius,
			FLinearColor(0.58f, 0.88f, 1.0f, 0.42f),
			1.0f,
			48);
	}
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
		FLinearColor(0.70f, 0.74f, 0.80f, 0.82f),
		PipReferenceLineThickness,
		true);
	DrawSceneTrajectory(
		System.GetHudOrbitalScene(),
		FLinearColor(0.22f, 0.96f, 1.0f, 0.98f),
		PipCurrentLineThickness,
		false);

	const FVector2D ReferenceUV = ToUv(Probe.ReferenceLocalPosition);
	FABTSM11PipEdgeIndicator ReferenceEdge;
	ABTSM11BuildPipEdgeIndicator(
		FVector2d(ReferenceUV.X, ReferenceUV.Y),
		PipEdgeMarginUV,
		ReferenceEdge);
	if (ReferenceEdge.bVisible)
	{
		DrawPipEdgeIndicator(
			Position,
			Size,
			FVector2d(ReferenceUV.X, ReferenceUV.Y),
			FLinearColor(0.82f, 0.84f, 0.88f, 0.92f),
			TEXT("REFERENCE"));
	}
	else
	{
		const FVector2D ReferenceMarker = Position + ReferenceUV * Size;
		DrawLine(ReferenceMarker.X - 7.0f, ReferenceMarker.Y,
			ReferenceMarker.X + 7.0f, ReferenceMarker.Y,
			FLinearColor::White, 1.6f);
		DrawLine(ReferenceMarker.X, ReferenceMarker.Y - 7.0f,
			ReferenceMarker.X, ReferenceMarker.Y + 7.0f,
			FLinearColor::White, 1.6f);
	}
	const FABTSM11ProbeProjection& Current =
		System.GetHudProbeProjection();
	if (Current.bValid)
	{
		const FVector2D CurrentUV = ToUv(Current.PositionCM);
		FABTSM11PipEdgeIndicator CurrentEdge;
		ABTSM11BuildPipEdgeIndicator(
			FVector2d(CurrentUV.X, CurrentUV.Y),
			PipEdgeMarginUV,
			CurrentEdge);
		const FLinearColor CurrentColor = Current.Status
			== EABTSM11ProbeRemapStatus::TrajectoryEndedBeforeLeg
			? FLinearColor(1.0f, 0.48f, 0.22f, 1.0f)
			: Current.Status == EABTSM11ProbeRemapStatus::ClosestMissFallback
				? FLinearColor(1.0f, 0.76f, 0.22f, 1.0f)
				: FLinearColor(0.28f, 1.0f, 0.64f, 1.0f);
		if (CurrentEdge.bVisible)
		{
			DrawPipEdgeIndicator(
				Position,
				Size,
				FVector2d(CurrentUV.X, CurrentUV.Y),
				CurrentColor,
				TEXT("CURRENT"));
		}
		else
		{
			const FVector2D CurrentMarker = Position + CurrentUV * Size;
			const FVector2D DiamondX(7.0f, 0.0f);
			const FVector2D DiamondY(0.0f, 7.0f);
			DrawLine((CurrentMarker - DiamondX).X,
				(CurrentMarker - DiamondX).Y,
				(CurrentMarker - DiamondY).X,
				(CurrentMarker - DiamondY).Y, CurrentColor, 2.0f);
			DrawLine((CurrentMarker - DiamondY).X,
				(CurrentMarker - DiamondY).Y,
				(CurrentMarker + DiamondX).X,
				(CurrentMarker + DiamondX).Y, CurrentColor, 2.0f);
			DrawLine((CurrentMarker + DiamondX).X,
				(CurrentMarker + DiamondX).Y,
				(CurrentMarker + DiamondY).X,
				(CurrentMarker + DiamondY).Y, CurrentColor, 2.0f);
			DrawLine((CurrentMarker + DiamondY).X,
				(CurrentMarker + DiamondY).Y,
				(CurrentMarker - DiamondX).X,
				(CurrentMarker - DiamondX).Y, CurrentColor, 2.0f);

			const FVector3d Tangent =
				Current.VelocityCMPerSec.GetSafeNormal();
			if (!Tangent.IsNearlyZero())
			{
				FVector2D TangentStart = CurrentUV;
				FVector2D TangentEnd = ToUv(
					Current.PositionCM + Tangent * View.HalfExtentCM * 0.28);
				if (ABTSM11ClipPipLineToRect(
						TangentStart,
						TangentEnd,
						PipEdgeMarginUV))
				{
					const FVector2D ArrowStart = Position + TangentStart * Size;
					const FVector2D ArrowEnd = Position + TangentEnd * Size;
					FVector2D ArrowDirection = ArrowEnd - ArrowStart;
					ArrowDirection.Normalize();
					const FVector2D ArrowPerpendicular(
						-ArrowDirection.Y,
						ArrowDirection.X);
					DrawLine(ArrowStart.X, ArrowStart.Y,
						ArrowEnd.X, ArrowEnd.Y, CurrentColor, 1.4f);
					DrawLine(ArrowEnd.X, ArrowEnd.Y,
						(ArrowEnd - ArrowDirection * 8.0f
							+ ArrowPerpendicular * 4.0f).X,
						(ArrowEnd - ArrowDirection * 8.0f
							+ ArrowPerpendicular * 4.0f).Y,
						CurrentColor, 1.4f);
					DrawLine(ArrowEnd.X, ArrowEnd.Y,
						(ArrowEnd - ArrowDirection * 8.0f
							- ArrowPerpendicular * 4.0f).X,
						(ArrowEnd - ArrowDirection * 8.0f
							- ArrowPerpendicular * 4.0f).Y,
						CurrentColor, 1.4f);
				}
			}
		}
	}

	DrawTargetPreviewFrame(
		Position,
		Size,
		FString::Printf(
			TEXT("%s  /  %s"),
			SemanticLegLabel(Probe.Leg),
			ProbeRemapLabel(Current.Status)),
		FString::Printf(
			TEXT("PHASE %.1f%%  /  GRAY REFERENCE  /  CYAN CURRENT  /  DIST %.0f cm"),
			Probe.PhaseWithinLeg * 100.0,
			Current.bValid ? Current.ContextDistanceCM : 0.0));
}

void AABTSM11FinaleHUD::DrawStatus(
	AABTSM11FinaleInteractionSystem& System,
	const FVector2D& DiagramCenter,
	const float DiagramRadius)
{
	const FABTSM11PrefixClassification& Classification =
		System.GetClassification();
	const float X = HudOrbitPanel.Min.X + 10.0f;
	const float Y = HudOrbitPanel.Min.Y + 31.0f;
	const AABTSM11FinaleSystem* FinaleSystem =
		System.GetFinaleSystem();
	const bool bEditorCandidate =
		FinaleSystem != nullptr
		&& FinaleSystem->IsEditorCandidateMode();
	if (bEditorCandidate && HudTheme.bDebugOverlay)
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
		HudTheme.ApplyOpacity(HudTheme.TextPrimary),
		X,
		Y,
		GEngine->GetSmallFont(),
		0.62f * HudTheme.TextScale,
		false);

	const FABTSM11PrefixStabilizer& Stabilizer =
		System.GetStabilizer();
	FString StabilizerText = TEXT("FREE AIM");
	FLinearColor StabilizerColor = HudTheme.ApplyOpacity(HudTheme.TextMuted);
	if (Stabilizer.GetNearPrefixLevel() > 0)
	{
		StabilizerText = bEditorCandidate
			? FString::Printf(
				TEXT("CANDIDATE PRECISION F%d / TEMPORARY"),
				Stabilizer.GetNearPrefixLevel())
			: FString::Printf(
				TEXT("PRECISION MODE: F%d"),
				Stabilizer.GetNearPrefixLevel());
		StabilizerColor = HudTheme.ApplyOpacity(HudTheme.Warning);
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
		StabilizerColor = HudTheme.ApplyOpacity(HudTheme.Success);
	}
	DrawText(
		StabilizerText,
		StabilizerColor,
		X,
		Y + 18.0f,
		GEngine->GetSmallFont(),
		0.64f * HudTheme.TextScale,
		false);

	if (HudTheme.bDebugOverlay)
	{
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
	}

	const float BarWidth = DiagramRadius * 1.25f;
	const float BarY = HudOrbitPanel.Max.Y
		- (bHudCompactLayout ? 40.0f : 48.0f);
	DrawRect(
		HudTheme.ApplyOpacity(HudTheme.SlotBorder),
		X,
		BarY,
		BarWidth,
		9.0f);
	DrawRect(
		HudTheme.ApplyOpacity(HudTheme.AccentPrimary),
		X,
		BarY,
		BarWidth * static_cast<float>(
			FMath::Clamp(System.GetCurrentInput().Power, 0.0, 1.0)),
		9.0f);
	DrawText(
		TEXT("LAUNCH POWER"),
		HudTheme.ApplyOpacity(HudTheme.TextMuted),
		X + BarWidth + 8.0f,
		BarY - 4.0f,
		GEngine->GetSmallFont(),
		0.60f * HudTheme.TextScale,
		false);

	const FString Status = System.IsReleasePending()
		? TEXT("FREEZING EXACT RELEASE...")
		: ((System.GetInteractionState() == EABTSM11FinaleInteractionState::Failed
				|| HudTheme.bDebugOverlay)
			? System.GetRuntimeFailure()
			: FString());
	if (!Status.IsEmpty())
	{
		DrawText(
			Status,
			Classification.IsF(4)
				? HudTheme.ApplyOpacity(HudTheme.Success)
				: HudTheme.ApplyOpacity(HudTheme.Danger),
			X,
			BarY + 16.0f,
			GEngine->GetSmallFont(),
			0.64f * HudTheme.TextScale,
			false);
	}
	DrawText(
		bHudCompactLayout
			? TEXT("KNOBS: DRAG / WHEEL   MOVE: PAN / ORBIT   R: RESET")
			: TEXT("Drag knob: adjust  |  Wheel: trim  |  MOVE: pan/orbit  |  LAUNCH only: release  |  R: reset"),
		HudTheme.ApplyOpacity(HudTheme.TextMuted),
		X,
		BarY + 34.0f,
		GEngine->GetSmallFont(),
		(bHudCompactLayout ? 0.48f : 0.54f) * HudTheme.TextScale,
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
	const double SafeNormalizedRadius = FMath::Clamp(
		1.0 - FMath::Max(
			static_cast<double>(OverviewClipInsetPixels),
			static_cast<double>(Thickness) * 0.5 + 0.5)
			/ FMath::Max<double>(Radius, 1.0),
		0.01,
		1.0);
	Start /= SafeNormalizedRadius;
	End /= SafeNormalizedRadius;
	if (!ABTSM11ClipDiagramSegmentToUnitCircle(Start, End))
	{
		return;
	}
	Start *= SafeNormalizedRadius;
	End *= SafeNormalizedRadius;
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

FVector2D AABTSM11FinaleHUD::ResolveDiagramTextPosition(
	const FVector2D& PanelCenter,
	const float PanelRadius,
	const FString& Text,
	const float Scale,
	const FVector2D& PreferredPosition) const
{
	if (Canvas == nullptr || GEngine == nullptr || GEngine->GetSmallFont() == nullptr)
	{
		return PreferredPosition;
	}
	float TextWidth = 0.0f;
	float TextHeight = 0.0f;
	Canvas->StrLen(
		GEngine->GetSmallFont(),
		Text,
		TextWidth,
		TextHeight,
		true);
	const FVector2D HalfSize(
		TextWidth * Scale * 0.5f,
		TextHeight * Scale * 0.5f);
	FVector2D TextCenter = PreferredPosition + HalfSize;
	FVector2D Offset = TextCenter - PanelCenter;
	const float MaximumCenterDistance = FMath::Max(
		0.0f,
		PanelRadius - OverviewClipInsetPixels - HalfSize.Size());
	const float Distance = Offset.Size();
	if (Distance > MaximumCenterDistance && Distance > KINDA_SMALL_NUMBER)
	{
		Offset *= MaximumCenterDistance / Distance;
		TextCenter = PanelCenter + Offset;
	}
	return TextCenter - HalfSize;
}

void AABTSM11FinaleHUD::DrawDiagramEdgeLabel(
	const FVector2D& PanelCenter,
	const float PanelRadius,
	const FString& Text,
	const FLinearColor& Color,
	const float Scale,
	const bool bTop)
{
	if (Canvas == nullptr || GEngine == nullptr || GEngine->GetSmallFont() == nullptr)
	{
		return;
	}
	float TextWidth = 0.0f;
	float TextHeight = 0.0f;
	Canvas->StrLen(
		GEngine->GetSmallFont(),
		Text,
		TextWidth,
		TextHeight,
		true);
	const FVector2D ScaledSize(TextWidth * Scale, TextHeight * Scale);
	const float HalfWidth = ScaledSize.X * 0.5f;
	const float SafeRadius = FMath::Max(
		1.0f,
		PanelRadius - OverviewClipInsetPixels - 2.0f);
	const float MaximumVerticalOffset = FMath::Max(
		0.0f,
		FMath::Sqrt(FMath::Max(
			0.0f,
			SafeRadius * SafeRadius - HalfWidth * HalfWidth))
			- ScaledSize.Y * 0.5f);
	const float VerticalOffset = FMath::Min(
		SafeRadius * 0.78f,
		MaximumVerticalOffset);
	const FVector2D Position(
		PanelCenter.X - HalfWidth,
		PanelCenter.Y
			+ (bTop ? -VerticalOffset : VerticalOffset)
			- ScaledSize.Y * 0.5f);
	DrawText(
		Text,
		Color,
		Position.X,
		Position.Y,
		GEngine->GetSmallFont(),
		Scale,
		false);
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
