// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73WeaknessStructureBuilder.h"

#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73StructureData.h"

namespace
{
	int32 AddNode(
		FABTSM73StructureData& Data,
		const FVector& Center,
		const FVector& Dimensions,
		const EABTSM7BuildingMaterial Material,
		const EABTSM73BrickSemanticRole Role,
		const int32 StoreyIndex,
		const int32 BayIndex)
	{
		FABTSM73BrickNode& Node = Data.Bricks.AddDefaulted_GetRef();
		Node.NodeId = Data.Bricks.Num() - 1;
		Node.Material = Material;
		Node.OriginalMaterial = Material;
		Node.LocalCenter = Center;
		Node.DimensionsCM = Dimensions.ComponentMax(FVector(1.0f));
		Node.SemanticRole = Role;
		Node.StoreyIndex = StoreyIndex;
		Node.BayIndex = BayIndex;
		return Node.NodeId;
	}

	EABTSM73StructuralWeaknessPattern ResolvePattern(const FABTSM73GenerationSettings& Settings)
	{
		if (Settings.StructuralWeaknessPattern != EABTSM73StructuralWeaknessPattern::Auto)
		{
			return Settings.StructuralWeaknessPattern;
		}
		switch (Settings.Silhouette)
		{
		case EABTSM73Silhouette::SingleTower:
			return EABTSM73StructuralWeaknessPattern::AsymmetricDualSupport;
		case EABTSM73Silhouette::Gatehouse:
			return EABTSM73StructuralWeaknessPattern::CriticalCorner;
		case EABTSM73Silhouette::TwinTowerBridge:
		default:
			return EABTSM73StructuralWeaknessPattern::OffsetSeam;
		}
	}

	FVector CornerVector(const uint32 Hash)
	{
		return FVector((Hash & 1u) != 0u ? 1.0f : -1.0f, (Hash & 2u) != 0u ? 1.0f : -1.0f, 0.0f);
	}
}

bool FABTSM73WeaknessStructureBuilder::Apply(
	const FABTSM73GenerationSettings& Settings,
	const float ResolvedLevelHeightCM,
	const float ResolvedColumnWidthCM,
	const float ResolvedBeamHeightCM,
	FABTSM73StructureData& InOutData,
	FString& OutError) const
{
	OutError.Reset();
	if (!Settings.bGenerateStructuralWeakness) return true;

	TArray<int32> AvailableBays;
	for (const FABTSM73BrickNode& Node : InOutData.Bricks)
	{
		if (Node.SemanticRole == EABTSM73BrickSemanticRole::Deck && Node.BayIndex != INDEX_NONE)
		{
			AvailableBays.AddUnique(Node.BayIndex);
		}
	}
	AvailableBays.Sort();
	if (AvailableBays.IsEmpty())
	{
		OutError = TEXT("B2NoDeckBay");
		return false;
	}
	int32 SelectedBay = Settings.StructuralWeaknessBayIndex;
	if (!AvailableBays.Contains(SelectedBay))
	{
		const uint32 BayHash = HashCombineFast(GetTypeHash(Settings.BuildingSeed), 0xB273u);
		SelectedBay = AvailableBays[BayHash % AvailableBays.Num()];
	}

	const FABTSM73BrickNode* HighestDeck = nullptr;
	for (const FABTSM73BrickNode& Node : InOutData.Bricks)
	{
		if (Node.SemanticRole != EABTSM73BrickSemanticRole::Deck || Node.BayIndex != SelectedBay) continue;
		if (HighestDeck == nullptr || Node.LocalCenter.Z > HighestDeck->LocalCenter.Z) HighestDeck = &Node;
	}
	if (HighestDeck == nullptr)
	{
		OutError = FString::Printf(TEXT("B2NoCarrierBaseDeck:%d"), SelectedBay);
		return false;
	}
	// The Gatehouse already owns a strong iron lintel above both tower caps.
	// Author its crown on that lintel instead of on a breakable tower deck;
	// otherwise the ordinary deck becomes a more effective bypass target than
	// the intended glass support after the crown is moved clear of the lintel.
	const FABTSM73BrickNode* WeaknessBase = HighestDeck;
	if (Settings.Silhouette == EABTSM73Silhouette::Gatehouse)
	{
		const float HighestDeckTop = HighestDeck->LocalCenter.Z + HighestDeck->DimensionsCM.Z * 0.5f;
		for (const FABTSM73BrickNode& Node : InOutData.Bricks)
		{
			if (Node.SemanticRole != EABTSM73BrickSemanticRole::Connector) continue;
			const float ConnectorBottom = Node.LocalCenter.Z - Node.DimensionsCM.Z * 0.5f;
			if (ConnectorBottom + 1.5f < HighestDeckTop) continue;
			if (WeaknessBase == HighestDeck || Node.LocalCenter.Z > WeaknessBase->LocalCenter.Z)
			{
				WeaknessBase = &Node;
			}
		}
	}
	const FVector BaseCenter = WeaknessBase->LocalCenter;
	const FVector BaseDimensions = WeaknessBase->DimensionsCM;
	const int32 WeakStorey = WeaknessBase->StoreyIndex + 1;
	const EABTSM73StructuralWeaknessPattern Pattern = ResolvePattern(Settings);
	const uint32 PatternHash = HashCombineFast(GetTypeHash(Settings.BuildingSeed), GetTypeHash(static_cast<uint8>(Pattern)));

	const float FootprintRatio = FMath::Clamp(Settings.WeaknessFootprintRatio, 0.40f, 0.85f);
	const float CapX = FMath::Max(90.0f, BaseDimensions.X * FootprintRatio);
	const float CapY = FMath::Max(90.0f, BaseDimensions.Y * FootprintRatio);
	const float CapThickness = FMath::Clamp(ResolvedBeamHeightCM, 20.0f, ResolvedLevelHeightCM * 0.40f);
	const float SupportWidth = FMath::Clamp(ResolvedColumnWidthCM * 0.95f, 20.0f, FMath::Min(CapX, CapY) * 0.42f);
	const float SupportHeight = FMath::Max(SupportWidth, Settings.WeaknessSupportHeightCM);
	const float SpanX = FMath::Max(SupportWidth * 0.75f, CapX * 0.22f);
	const float SpanY = FMath::Max(SupportWidth * 0.75f, CapY * 0.22f);
	const float BiasRatio = FMath::Clamp(Settings.WeaknessBiasRatio, 0.10f, 0.80f);
	const float BaseTopZ = BaseCenter.Z + BaseDimensions.Z * 0.5f;

	FVector CrownOrigin = BaseCenter;
	if (AvailableBays.Num() > 1 && !FMath::IsNearlyZero(BaseCenter.Y))
	{
		const float OutwardSign = FMath::Sign(BaseCenter.Y);
		// Multi-bay silhouettes also own a center lintel/corridor. Move the crown
		// to the outer half of the selected tower so its inner support cannot
		// penetrate that pre-existing connector when the weakness course is added.
		CrownOrigin.Y += OutwardSign * BaseDimensions.Y * (1.0f - FootprintRatio) * 0.50f;
	}
	CrownOrigin.Z = BaseTopZ;

	TArray<FVector> SupportOffsets;
	int32 WeakSupportIndex = INDEX_NONE;
	FVector TipDirection = FVector::ForwardVector;
	EABTSM73PredictedCollapseMode CollapseMode = EABTSM73PredictedCollapseMode::Tip;
	FVector CarrierBias = FVector::ZeroVector;
	FVector Corner = CornerVector(PatternHash);
	if (AvailableBays.Num() > 1 && !FMath::IsNearlyZero(BaseCenter.Y)
		&& FMath::Sign(Corner.Y) != FMath::Sign(BaseCenter.Y))
	{
		// Multi-bay crowns live on a selected tower. Mirror the authored weak side
		// toward that tower's exterior; keeping an inner weak side would either be
		// hidden by the center connector or require moving the crown off its base.
		// The four seed quadrants are still preserved across the two deterministic
		// bay choices, while every generated intent remains attackable and supported.
		Corner.Y *= -1.0f;
	}

	switch (Pattern)
	{
	case EABTSM73StructuralWeaknessPattern::CriticalCorner:
		for (const float X : {-SpanX, SpanX})
		{
			for (const float Y : {-SpanY, SpanY}) SupportOffsets.Add(FVector(X, Y, 0.0f));
		}
		for (int32 Index = 0; Index < SupportOffsets.Num(); ++Index)
		{
			if (FMath::Sign(SupportOffsets[Index].X) == FMath::Sign(Corner.X)
				&& FMath::Sign(SupportOffsets[Index].Y) == FMath::Sign(Corner.Y))
			{
				WeakSupportIndex = Index;
				break;
			}
		}
		TipDirection = Corner.GetSafeNormal();
		CarrierBias = FVector(Corner.X * SpanX * BiasRatio, Corner.Y * SpanY * BiasRatio, 0.0f);
		break;

	case EABTSM73StructuralWeaknessPattern::AsymmetricDualSupport:
	{
		const float WeakSign = Corner.Y;
		// The two supports remain a single structural line, but the seed also
		// chooses which X side of the crown carries that line. This preserves the
		// recognizable dual-support silhouette while exposing all four authored
		// failure quadrants instead of collapsing every seed to local +/-Y.
		const float SupportLineX = Corner.X * SpanX;
		SupportOffsets.Add(FVector(SupportLineX, -WeakSign * SpanY, 0.0f));
		SupportOffsets.Add(FVector(SupportLineX, WeakSign * SpanY, 0.0f));
		WeakSupportIndex = 1;
		TipDirection = Corner.GetSafeNormal();
		CarrierBias = FVector(Corner.X * SpanX * BiasRatio, Corner.Y * SpanY * BiasRatio, 0.0f);
		break;
	}

	case EABTSM73StructuralWeaknessPattern::OffsetSeam:
	default:
	{
		const FVector WeakOffset(Corner.X * SpanX, Corner.Y * SpanY, 0.0f);
		SupportOffsets.Add(WeakOffset);
		SupportOffsets.Add(FVector(-Corner.X * SpanX, Corner.Y * SpanY, 0.0f));
		SupportOffsets.Add(FVector(Corner.X * SpanX, -Corner.Y * SpanY, 0.0f));
		WeakSupportIndex = 0;
		TipDirection = Corner.GetSafeNormal();
		CarrierBias = FVector(Corner.X * SpanX * BiasRatio, Corner.Y * SpanY * BiasRatio, 0.0f);
		CollapseMode = EABTSM73PredictedCollapseMode::SlideAndTip;
		break;
	}
	}
	if (!SupportOffsets.IsValidIndex(WeakSupportIndex))
	{
		OutError = TEXT("B2WeakSupportSelectionFailed");
		return false;
	}
	// Ratio bias controls the silhouette, while this small absolute reserve keeps
	// the post-failure COM beyond the hard tip boundary when PrimaryMaterial
	// changes the Carrier/Payload mass balance (Iron is the worst current case).
	CarrierBias += TipDirection.GetSafeNormal() * FMath::Max(0.0f, Settings.WeaknessTipReserveCM);

	FABTSM73StructuralWeaknessIntent Intent;
	Intent.Pattern = Pattern;
	Intent.ExpectedCollapseMode = CollapseMode;
	Intent.BayIndex = SelectedBay;
	Intent.ExpectedTipDirectionLocal = TipDirection;
	for (int32 Index = 0; Index < SupportOffsets.Num(); ++Index)
	{
		const FVector Center = CrownOrigin + SupportOffsets[Index] + FVector(0.0f, 0.0f, SupportHeight * 0.5f);
		const EABTSM73BrickSemanticRole Role = Index == WeakSupportIndex
			? EABTSM73BrickSemanticRole::WeakSupport
			: EABTSM73BrickSemanticRole::Column;
		const int32 NodeId = AddNode(InOutData, Center, FVector(SupportWidth, SupportWidth, SupportHeight),
			Settings.PrimaryMaterial, Role, WeakStorey, SelectedBay);
		Intent.DirectSupportNodeIds.Add(NodeId);
		if (Index == WeakSupportIndex) Intent.CandidateNodeId = NodeId;
	}

	const FVector CarrierCenter = CrownOrigin + CarrierBias
		+ FVector(0.0f, 0.0f, SupportHeight + CapThickness * 0.5f);
	Intent.CarrierNodeId = AddNode(InOutData, CarrierCenter, FVector(CapX, CapY, CapThickness),
		Settings.PrimaryMaterial, EABTSM73BrickSemanticRole::Carrier, WeakStorey, SelectedBay);

	const float PayloadWidth = FMath::Clamp(ResolvedColumnWidthCM * 0.90f, 24.0f, FMath::Min(CapX, CapY) * 0.22f);
	const float PayloadHeight = FMath::Max(PayloadWidth, Settings.WeaknessPayloadHeightCM);
	FVector Perpendicular(-TipDirection.Y, TipDirection.X, 0.0f);
	if (Perpendicular.IsNearlyZero()) Perpendicular = FVector::RightVector;
	const float PayloadForward = FMath::Min(CapX, CapY) * 0.10f;
	// Payload bricks remain axis-aligned. A fixed radial offset is insufficient
	// on a diagonal tip direction because both projected axis separations shrink
	// by sqrt(2). Guarantee a small gap on at least one local axis.
	const float PerpendicularMaxAxis = FMath::Max(FMath::Abs(Perpendicular.X), FMath::Abs(Perpendicular.Y));
	const float PayloadSide = FMath::Max(
		PayloadWidth * 0.62f,
		(PayloadWidth + 4.0f) / FMath::Max(0.01f, 2.0f * PerpendicularMaxAxis));
	for (const float Side : {-PayloadSide, PayloadSide})
	{
		const FVector PayloadCenter = CarrierCenter + TipDirection * PayloadForward + Perpendicular * Side
			+ FVector(0.0f, 0.0f, CapThickness * 0.5f + PayloadHeight * 0.5f);
		const int32 PayloadNodeId = AddNode(InOutData, PayloadCenter,
			FVector(PayloadWidth, PayloadWidth, PayloadHeight), EABTSM7BuildingMaterial::Stone,
			EABTSM73BrickSemanticRole::Payload, WeakStorey + 1, SelectedBay);
		Intent.PayloadNodeIds.Add(PayloadNodeId);
	}
	InOutData.StructuralWeaknessIntents.Add(MoveTemp(Intent));
	return true;
}
