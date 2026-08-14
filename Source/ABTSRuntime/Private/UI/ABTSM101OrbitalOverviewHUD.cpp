// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSM10ScoutMapHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "UI/ABTSUITheme.h"
#include "World/ABTSM101OrbitalOverviewTypes.h"
#include "World/ABTSM10ScoutMapSystem.h"

namespace
{
	bool ClipSegmentToCircle(
		const FVector2D& Start,
		const FVector2D& End,
		const FVector2D& CircleCenter,
		const float CircleRadius,
		float& OutStartT,
		float& OutEndT)
	{
		OutStartT = 0.0f;
		OutEndT = 1.0f;
		const FVector2D Direction = End - Start;
		const FVector2D Offset = Start - CircleCenter;
		const double A = Direction.SizeSquared();
		if (A <= UE_DOUBLE_SMALL_NUMBER)
		{
			return Offset.SizeSquared() <= FMath::Square(CircleRadius);
		}
		const double B = 2.0 * FVector2D::DotProduct(Offset, Direction);
		const double C = Offset.SizeSquared() - FMath::Square(static_cast<double>(CircleRadius));
		const double Discriminant = B * B - 4.0 * A * C;
		const bool bStartInside = C <= 0.0;
		const bool bEndInside = FVector2D::DistSquared(End, CircleCenter) <= FMath::Square(CircleRadius);
		if (Discriminant < 0.0) return bStartInside && bEndInside;
		const double Root = FMath::Sqrt(FMath::Max(0.0, Discriminant));
		const double T0 = (-B - Root) / (2.0 * A);
		const double T1 = (-B + Root) / (2.0 * A);
		OutStartT = FMath::Clamp(static_cast<float>(T0), 0.0f, 1.0f);
		OutEndT = FMath::Clamp(static_cast<float>(T1), 0.0f, 1.0f);
		if (bStartInside) OutStartT = 0.0f;
		if (bEndInside) OutEndT = 1.0f;
		return OutStartT <= OutEndT
			&& (bStartInside || bEndInside || (T0 <= 1.0 && T1 >= 0.0));
	}

	void DrawSolidClipped(
		UCanvas& Canvas,
		const FVector2D& Start,
		const FVector2D& End,
		const FVector2D& ClipCenter,
		const float ClipRadius,
		const float Thickness,
		const FLinearColor& Color)
	{
		float StartT = 0.0f;
		float EndT = 1.0f;
		if (!ClipSegmentToCircle(Start, End, ClipCenter, ClipRadius, StartT, EndT)) return;
		Canvas.K2_DrawLine(
			FMath::Lerp(Start, End, StartT),
			FMath::Lerp(Start, End, EndT),
			Thickness,
			Color);
	}

	void DrawDashedClipped(
		UCanvas& Canvas,
		const FVector2D& Start,
		const FVector2D& End,
		const FVector2D& ClipCenter,
		const float ClipRadius,
		const float Thickness,
		const float DashLength,
		const float GapLength,
		const FLinearColor& Color,
		float& InOutPatternDistance)
	{
		const FVector2D OriginalSegment = End - Start;
		const float OriginalLength = OriginalSegment.Size();
		if (OriginalLength <= KINDA_SMALL_NUMBER) return;
		float StartT = 0.0f;
		float EndT = 1.0f;
		const float Period = FMath::Max(DashLength + GapLength, 1.0f);
		if (!ClipSegmentToCircle(Start, End, ClipCenter, ClipRadius, StartT, EndT))
		{
			InOutPatternDistance = FMath::Fmod(InOutPatternDistance + OriginalLength, Period);
			return;
		}

		const FVector2D Direction = OriginalSegment / OriginalLength;
		const float ClippedStartDistance = StartT * OriginalLength;
		const float ClippedEndDistance = EndT * OriginalLength;
		float Cursor = ClippedStartDistance;
		float PatternDistance = FMath::Fmod(InOutPatternDistance + ClippedStartDistance, Period);
		while (Cursor < ClippedEndDistance)
		{
			const float PatternPosition = FMath::Fmod(PatternDistance, Period);
			const bool bInsideDash = PatternPosition < DashLength;
			const float RemainingPatternLength = bInsideDash
				? DashLength - PatternPosition
				: Period - PatternPosition;
			const float StepLength = FMath::Min(
				FMath::Max(RemainingPatternLength, 0.01f),
				ClippedEndDistance - Cursor);
			if (bInsideDash)
			{
				Canvas.K2_DrawLine(
					Start + Direction * Cursor,
					Start + Direction * (Cursor + StepLength),
					Thickness,
					Color);
			}
			Cursor += StepLength;
			PatternDistance = FMath::Fmod(PatternDistance + StepLength, Period);
		}
		InOutPatternDistance = FMath::Fmod(InOutPatternDistance + OriginalLength, Period);
	}

	FVector2D ToScreen(
		const FVector2D& PlanePoint,
		const FABTSM101OrbitalOverviewSnapshot& Snapshot,
		const FVector2D& ScreenCenter,
		const float PixelsPerCM)
	{
		const FVector2D Relative = PlanePoint - Snapshot.ContentCenter;
		return ScreenCenter + FVector2D(Relative.X, -Relative.Y) * PixelsPerCM;
	}

	void DrawBodyOutline(
		UCanvas& Canvas,
		const FABTSM101OrbitalBody& Body,
		const FABTSM101OrbitalOverviewSnapshot& Snapshot,
		const FVector2D& ScreenCenter,
		const float PixelsPerCM,
		const float ClipRadius)
	{
		const FVector2D BodyCenter = ToScreen(Body.Center, Snapshot, ScreenCenter, PixelsPerCM);
		const float BodyRadius = Body.RadiusCM * PixelsPerCM;
		if (FVector2D::Distance(BodyCenter, ScreenCenter) - BodyRadius > ClipRadius) return;
		const FLinearColor Color = Body.bPrimary
			? FLinearColor(0.58f, 0.76f, 0.92f, 0.82f)
			: FLinearColor(0.72f, 0.62f, 0.92f, 0.88f);
		const float Thickness = Body.bPrimary ? 1.35f : 1.6f;
		constexpr int32 CircleSegments = 96;
		FVector2D Previous = BodyCenter + FVector2D(BodyRadius, 0.0f);
		for (int32 Index = 1; Index <= CircleSegments; ++Index)
		{
			const float Angle = 2.0f * PI * static_cast<float>(Index) / CircleSegments;
			const FVector2D Current = BodyCenter + FVector2D(
				FMath::Cos(Angle) * BodyRadius,
				FMath::Sin(Angle) * BodyRadius);
			DrawSolidClipped(
				Canvas, Previous, Current, ScreenCenter, ClipRadius, Thickness, Color);
			Previous = Current;
		}
	}

	void DrawCrossMarker(
		UCanvas& Canvas,
		const FVector2D& Position,
		const FVector2D& ClipCenter,
		const float ClipRadius,
		const float HalfSize)
	{
		if (FVector2D::DistSquared(Position, ClipCenter)
			> FMath::Square(FMath::Max(0.0f, ClipRadius - HalfSize))) return;
		const FLinearColor Underlay(0.02f, 0.01f, 0.01f, 0.96f);
		const FLinearColor Red(1.0f, 0.045f, 0.025f, 1.0f);
		Canvas.K2_DrawLine(
			Position - FVector2D(HalfSize, HalfSize),
			Position + FVector2D(HalfSize, HalfSize),
			4.0f,
			Underlay);
		Canvas.K2_DrawLine(
			Position + FVector2D(-HalfSize, HalfSize),
			Position + FVector2D(HalfSize, -HalfSize),
			4.0f,
			Underlay);
		Canvas.K2_DrawLine(
			Position - FVector2D(HalfSize, HalfSize),
			Position + FVector2D(HalfSize, HalfSize),
			2.0f,
			Red);
		Canvas.K2_DrawLine(
			Position + FVector2D(-HalfSize, HalfSize),
			Position + FVector2D(HalfSize, -HalfSize),
			2.0f,
			Red);
	}

	void DrawLaunchMarker(
		UCanvas& Canvas,
		const FVector2D& Position,
		const FVector2D& ClipCenter,
		const float ClipRadius)
	{
		if (FVector2D::DistSquared(Position, ClipCenter)
			> FMath::Square(FMath::Max(0.0f, ClipRadius - 8.0f))) return;
		const FLinearColor Color(1.0f, 0.71f, 0.20f, 1.0f);
		Canvas.K2_DrawPolygon(
			Canvas.DefaultTexture,
			Position,
			FVector2D(4.2f),
			20,
			Color);
		Canvas.K2_DrawLine(
			Position + FVector2D(-5.0f, 7.0f),
			Position + FVector2D(-1.5f, 0.0f),
			1.8f,
			Color);
		Canvas.K2_DrawLine(
			Position + FVector2D(5.0f, 7.0f),
			Position + FVector2D(1.5f, 0.0f),
			1.8f,
			Color);
	}
}

void AABTSM10ScoutMapHUD::DrawOrbitalOverview(AABTSM10ScoutMapSystem& System)
{
	if (Canvas == nullptr || !System.IsOrbitalOverviewActive()) return;
	const FABTSM101OrbitalOverviewSnapshot& Snapshot = System.GetOrbitalOverviewSnapshot();
	if (!Snapshot.bValid || Snapshot.ContentRadiusCM <= 1.0f) return;
	const FABTSM10ScoutMapSettings& Settings = System.GetSettings();

	const float Left = FMath::Clamp(Settings.TopLeftMarginPx, 0.0f, 200.0f);
	const float Top = Left
		+ FMath::Clamp(Settings.MapDiameterPx, 120.0f, 700.0f)
		+ FMath::Clamp(Settings.OrbitalDiagramScoutMapGapPx, 0.0f, 200.0f);
	const float RequestedDiameter = FMath::Clamp(Settings.OrbitalDiagramDiameterPx, 120.0f, 700.0f);
	const float BottomReserved = FMath::Clamp(
		Settings.OrbitalDiagramBottomReservedPx, 80.0f, 300.0f);
	const float MaximumDiameter = FMath::Min(
		Canvas->ClipX - Left - 8.0f,
		Canvas->ClipY - BottomReserved - Top);
	const float Diameter = FMath::Min(RequestedDiameter, MaximumDiameter);
	if (Diameter < 120.0f) return;

	const float Radius = Diameter * 0.5f;
	const FVector2D Center(Left + Radius, Top + Radius);
	const float FrameRadius = Radius - 3.0f;
	const float ContentPadding = FMath::Clamp(
		Settings.OrbitalDiagramContentPaddingPx, 4.0f, Radius * 0.35f);
	const float ContentScreenRadius = FMath::Max(8.0f, FrameRadius - ContentPadding - 5.0f);
	const float PixelsPerCM = ContentScreenRadius / Snapshot.ContentRadiusCM;
	const FABTSUIThemeSnapshot Theme = FABTSUITheme::Get();

	Canvas->K2_DrawPolygon(
		Canvas->DefaultTexture,
		Center,
		FVector2D(Radius + 3.0f),
		96,
		Theme.ApplyOpacity(Theme.AccentSecondary));
	Canvas->K2_DrawPolygon(
		Canvas->DefaultTexture,
		Center,
		FVector2D(FrameRadius),
		96,
		Theme.ApplyOpacity(Theme.PortraitBacking));

	const float GridDashLength = 3.5f;
	const float GridGapLength = 4.0f;
	float GridPatternDistance = 0.0f;
	for (const FABTSM101OrbitalLineSegment& Segment : Snapshot.PrimaryGridSegments)
	{
		if (!Segment.bDashed) continue;
		DrawDashedClipped(
			*Canvas,
			ToScreen(Segment.Start, Snapshot, Center, PixelsPerCM),
			ToScreen(Segment.End, Snapshot, Center, PixelsPerCM),
			Center,
			FrameRadius,
			0.65f,
			GridDashLength,
			GridGapLength,
			FLinearColor(0.36f, 0.53f, 0.70f, 0.22f),
			GridPatternDistance);
	}
	for (const FABTSM101OrbitalLineSegment& Segment : Snapshot.PrimaryGridSegments)
	{
		if (Segment.bDashed) continue;
		DrawSolidClipped(
			*Canvas,
			ToScreen(Segment.Start, Snapshot, Center, PixelsPerCM),
			ToScreen(Segment.End, Snapshot, Center, PixelsPerCM),
			Center,
			FrameRadius,
			0.85f,
			FLinearColor(0.53f, 0.72f, 0.88f, 0.52f));
	}
	for (const FABTSM101OrbitalBody& Body : Snapshot.Bodies)
	{
		DrawBodyOutline(*Canvas, Body, Snapshot, Center, PixelsPerCM, FrameRadius);
	}

	const float TrajectoryThickness = FMath::Clamp(
		Settings.OrbitalDiagramTrajectoryThicknessPx, 0.5f, 10.0f);
	const float DashLength = FMath::Clamp(
		Settings.OrbitalDiagramOccludedDashLengthPx, 1.0f, 40.0f);
	const float GapLength = FMath::Clamp(
		Settings.OrbitalDiagramOccludedGapLengthPx, 0.0f, 40.0f);
	float TrajectoryPatternDistance = 0.0f;
	for (const FABTSM101OrbitalLineSegment& Segment : Snapshot.TrajectorySegments)
	{
		if (!Segment.bDashed) continue;
		DrawDashedClipped(
			*Canvas,
			ToScreen(Segment.Start, Snapshot, Center, PixelsPerCM),
			ToScreen(Segment.End, Snapshot, Center, PixelsPerCM),
			Center,
			FrameRadius,
			TrajectoryThickness,
			DashLength,
			GapLength,
			FLinearColor(0.69f, 0.88f, 1.0f, 0.56f),
			TrajectoryPatternDistance);
	}
	for (const FABTSM101OrbitalLineSegment& Segment : Snapshot.TrajectorySegments)
	{
		if (Segment.bDashed) continue;
		DrawSolidClipped(
			*Canvas,
			ToScreen(Segment.Start, Snapshot, Center, PixelsPerCM),
			ToScreen(Segment.End, Snapshot, Center, PixelsPerCM),
			Center,
			FrameRadius,
			TrajectoryThickness,
			FLinearColor(0.74f, 0.92f, 1.0f, 0.98f));
	}

	DrawLaunchMarker(
		*Canvas,
		ToScreen(Snapshot.LaunchPoint, Snapshot, Center, PixelsPerCM),
		Center,
		FrameRadius);
	if (Snapshot.bHasLandingPoint)
	{
		DrawCrossMarker(
			*Canvas,
			ToScreen(Snapshot.LandingPoint, Snapshot, Center, PixelsPerCM),
			Center,
			FrameRadius,
			5.5f);
	}
	if (GEngine)
	{
		DrawText(
			TEXT("ORBIT OVERVIEW"),
			Theme.ApplyOpacity(Theme.TextPrimary),
			Left + 12.0f,
			Top + 8.0f,
			GEngine->GetSmallFont(),
			0.72f * Theme.TextScale,
			false);
	}
}
