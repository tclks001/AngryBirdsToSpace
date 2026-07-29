// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSM11FinaleHUD.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Game/ABTSM11GameMode.h"
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
}

void AABTSM11FinaleHUD::DrawHUD()
{
	AABTSM11FinaleInteractionSystem* System =
		FindInteractionSystem();
	if (System != nullptr && System->IsFinaleActive())
	{
		DrawFinaleLayer(*System);
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

void AABTSM11FinaleHUD::DrawFinaleLayer(
	AABTSM11FinaleInteractionSystem& System)
{
	if (Canvas == nullptr)
	{
		return;
	}
	const float Radius = FMath::Min(170.0f, Canvas->SizeY * 0.18f);
	const FVector2D Center(
		Radius + 22.0f,
		Canvas->SizeY - Radius - 110.0f);
	DrawOrbitalDiagram(System, Center, Radius);
	DrawTargetPreview(System);
	DrawStatus(System, Center, Radius);
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

void AABTSM11FinaleHUD::DrawTargetPreview(
	AABTSM11FinaleInteractionSystem& System)
{
	UTextureRenderTarget2D* RenderTarget =
		System.GetTargetPreviewRenderTarget();
	if (RenderTarget == nullptr || Canvas == nullptr)
	{
		return;
	}
	const FVector2D Size(
		FMath::Min(420.0f, Canvas->SizeX * 0.34f),
		FMath::Min(250.0f, Canvas->SizeY * 0.30f));
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
	DrawRect(
		FLinearColor(0.02f, 0.06f, 0.11f, 0.75f),
		Position.X,
		Position.Y,
		Size.X,
		24.0f);
	DrawText(
		FString::Printf(
			TEXT("APPROACH PREVIEW: %s%s"),
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
			TEXT("VISIBLE CURSOR DRAG = POUCH DIRECTION"),
			FLinearColor(0.80f, 0.88f, 0.96f),
			X,
			CandidateY + 112.0f,
			GEngine->GetSmallFont(),
			0.58f,
			false);
		DrawText(
			TEXT("WHEEL +/-0.08 = POWER | SAME RELEASE = LAUNCH | R = RESET"),
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
		TEXT("Visible cursor drag: pouch direction  |  Wheel: power (down +0.08 / up -0.08)  |  Release same press: launch  |  R: reset"),
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
