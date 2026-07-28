// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAGSupportGeometry.h"

namespace
{
	bool HasPairwiseColumnClearance(
		const TArray<FVector2D>& Centers,
		const float ColumnWidthCM,
		const float ColumnClearanceCM)
	{
		const float RequiredAxisSeparation = ColumnWidthCM + ColumnClearanceCM;
		for (int32 A = 0; A < Centers.Num(); ++A)
		{
			for (int32 B = A + 1; B < Centers.Num(); ++B)
			{
				const FVector2D Delta = (Centers[A] - Centers[B]).GetAbs();
				if (Delta.X + KINDA_SMALL_NUMBER < RequiredAxisSeparation
					&& Delta.Y + KINDA_SMALL_NUMBER < RequiredAxisSeparation)
				{
					return false;
				}
			}
		}
		return true;
	}

	bool CanFitPattern(
		const FBox2D& Region,
		const FABTSM73DAGLayoutSettings& Settings,
		const EABTSM73DAGSupportPattern Pattern,
		const float Width)
	{
		if (!Region.bIsValid) return false;
		const FVector2D Size = Region.GetSize();
		const float SingleAxis = Width + Settings.ColumnClearanceCM * 2.0f;
		const float PairAxis = Width * 2.0f + Settings.ColumnClearanceCM * 3.0f;
		switch (Pattern)
		{
		case EABTSM73DAGSupportPattern::SingleColumnInterface:
			return Size.X >= SingleAxis && Size.Y >= SingleAxis;
		case EABTSM73DAGSupportPattern::TwoColumnLine:
			return FMath::Min(Size.X, Size.Y) >= SingleAxis
				&& FMath::Max(Size.X, Size.Y) >= PairAxis;
		case EABTSM73DAGSupportPattern::ThreeColumnTripod:
		case EABTSM73DAGSupportPattern::FourColumnFootprint:
			return Size.X >= PairAxis && Size.Y >= PairAxis;
		default:
			return false;
		}
	}
}

bool FABTSM73DAGSupportGeometry::MakeColumnCenters(
	const FBox2D& Region,
	const FABTSM73DAGLayoutSettings& Settings,
	const EABTSM73DAGSupportPattern Pattern,
	const float ColumnWidthCM,
	TArray<FVector2D>& OutCenters)
{
	OutCenters.Reset();
	if (!CanFitPattern(Region, Settings, Pattern, ColumnWidthCM)) return false;

	const float Half = ColumnWidthCM * 0.5f + Settings.ColumnClearanceCM;
	const FVector2D SafeMin = Region.Min + FVector2D(Half, Half);
	const FVector2D SafeMax = Region.Max - FVector2D(Half, Half);
	const FVector2D Center = (SafeMin + SafeMax) * 0.5f;
	const FVector2D SafeSpan = SafeMax - SafeMin;
	const float OffsetX = SafeSpan.X * 0.5f;
	const float OffsetY = SafeSpan.Y * 0.5f;

	switch (Pattern)
	{
	case EABTSM73DAGSupportPattern::SingleColumnInterface:
		OutCenters.Add(Center);
		break;
	case EABTSM73DAGSupportPattern::TwoColumnLine:
		if (SafeSpan.X >= SafeSpan.Y)
		{
			OutCenters.Add(Center + FVector2D(-OffsetX, 0.0f));
			OutCenters.Add(Center + FVector2D(OffsetX, 0.0f));
		}
		else
		{
			OutCenters.Add(Center + FVector2D(0.0f, -OffsetY));
			OutCenters.Add(Center + FVector2D(0.0f, OffsetY));
		}
		break;
	case EABTSM73DAGSupportPattern::ThreeColumnTripod:
		// The load solver distributes a centered resultant equally across the
		// three columns. Therefore their equal-area contact centroid must also
		// be Center. The old (-a,-b),(+a,-b),(0,+b) layout biased it by -b/3.
		if (SafeSpan.X >= SafeSpan.Y)
		{
			OutCenters.Add(Center + FVector2D(-OffsetX, -OffsetY * 0.5f));
			OutCenters.Add(Center + FVector2D(OffsetX, -OffsetY * 0.5f));
			OutCenters.Add(Center + FVector2D(0.0f, OffsetY));
		}
		else
		{
			OutCenters.Add(Center + FVector2D(-OffsetX * 0.5f, -OffsetY));
			OutCenters.Add(Center + FVector2D(-OffsetX * 0.5f, OffsetY));
			OutCenters.Add(Center + FVector2D(OffsetX, 0.0f));
		}
		break;
	case EABTSM73DAGSupportPattern::FourColumnFootprint:
		OutCenters.Add(Center + FVector2D(-OffsetX, -OffsetY));
		OutCenters.Add(Center + FVector2D(OffsetX, -OffsetY));
		OutCenters.Add(Center + FVector2D(-OffsetX, OffsetY));
		OutCenters.Add(Center + FVector2D(OffsetX, OffsetY));
		break;
	default:
		return false;
	}

	if (!HasPairwiseColumnClearance(OutCenters, ColumnWidthCM, Settings.ColumnClearanceCM))
	{
		OutCenters.Reset();
		return false;
	}
	return true;
}
