// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73StructureBuilder.h"

#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73StructureData.h"

namespace
{
	constexpr float ContactToleranceCM = 1.5f;

	float OverlapLength(const float ACenter, const float ASize, const float BCenter, const float BSize)
	{
		const float AMin = ACenter - ASize * 0.5f;
		const float AMax = ACenter + ASize * 0.5f;
		const float BMin = BCenter - BSize * 0.5f;
		const float BMax = BCenter + BSize * 0.5f;
		return FMath::Max(0.0f, FMath::Min(AMax, BMax) - FMath::Max(AMin, BMin));
	}
}

void FABTSM73StructureBuilder::AddBrick(
	FABTSM73StructureData& Data,
	const FVector& Center,
	const FVector& Dimensions,
	const EABTSM7BuildingMaterial Material)
{
	FABTSM73BrickNode& Node = Data.Bricks.AddDefaulted_GetRef();
	Node.NodeId = Data.Bricks.Num() - 1;
	Node.Material = Material;
	Node.LocalCenter = Center;
	Node.DimensionsCM = Dimensions.ComponentMax(FVector(1.0f));
}

void FABTSM73StructureBuilder::AddFourColumnStorey(
	FABTSM73StructureData& Data,
	const float CenterY,
	const float Width,
	const float Depth,
	const float BottomZ,
	const float LevelHeight,
	const float ColumnWidth,
	const float BeamHeight,
	const EABTSM7BuildingMaterial Material,
	const bool bAddRoof)
{
	const float HalfX = FMath::Max(0.0f, Depth * 0.5f - ColumnWidth * 0.5f);
	const float HalfY = FMath::Max(0.0f, Width * 0.5f - ColumnWidth * 0.5f);
	const float ColumnHeight = FMath::Max(ColumnWidth, LevelHeight - BeamHeight);
	const float ColumnZ = BottomZ + ColumnHeight * 0.5f;
	for (const float X : {-HalfX, HalfX})
	{
		for (const float Y : {CenterY - HalfY, CenterY + HalfY})
		{
			AddBrick(Data, FVector(X, Y, ColumnZ), FVector(ColumnWidth, ColumnWidth, ColumnHeight), Material);
		}
	}
	if (bAddRoof)
	{
		AddBrick(Data, FVector(0.0f, CenterY, BottomZ + ColumnHeight + BeamHeight * 0.5f),
			FVector(Depth, Width, BeamHeight), Material);
	}
}

bool FABTSM73StructureBuilder::Build(
	const FABTSM73GenerationSettings& Settings,
	FABTSM73StructureData& OutData,
	FString& OutError) const
{
	OutData = FABTSM73StructureData();
	OutError.Reset();
	FRandomStream Stream(Settings.BuildingSeed);
	const int32 Levels = FMath::Clamp(Settings.Levels, 1, 6);
	const float Width = FMath::Max(120.0f, Settings.BayWidthCM * Stream.FRandRange(0.94f, 1.06f));
	const float Depth = FMath::Max(120.0f, Settings.BuildingDepthCM * Stream.FRandRange(0.96f, 1.04f));
	const float LevelHeight = FMath::Max(80.0f, Settings.LevelHeightCM * Stream.FRandRange(0.97f, 1.03f));
	const float Column = FMath::Clamp(Settings.ColumnWidthCM, 20.0f, FMath::Min(Width, Depth) * 0.45f);
	const float Beam = FMath::Clamp(Settings.BeamHeightCM, 20.0f, LevelHeight * 0.45f);

	switch (Settings.Silhouette)
	{
	case EABTSM73Silhouette::SingleTower:
	{
		float BottomZ = 0.0f;
		for (int32 Level = 0; Level < Levels; ++Level)
		{
			const float Taper = FMath::Pow(0.94f, static_cast<float>(Level));
			AddFourColumnStorey(OutData, 0.0f, Width * Taper, Depth * Taper, BottomZ,
				LevelHeight, Column, Beam, Settings.PrimaryMaterial, true);
			BottomZ += LevelHeight;
		}
		break;
	}
	case EABTSM73Silhouette::Gatehouse:
	{
		const float TowerWidth = Width * 0.62f;
		const float Gap = Width * 0.62f;
		const float OffsetY = (Gap + TowerWidth) * 0.5f;
		for (int32 Level = 0; Level < Levels; ++Level)
		{
			const float BottomZ = Level * LevelHeight;
			AddFourColumnStorey(OutData, -OffsetY, TowerWidth, Depth, BottomZ, LevelHeight, Column, Beam,
				Settings.PrimaryMaterial, true);
			AddFourColumnStorey(OutData, OffsetY, TowerWidth, Depth, BottomZ, LevelHeight, Column, Beam,
				Settings.PrimaryMaterial, true);
		}
		// The connecting lintel is a new course above both tower caps. Placing it
		// at the same Z as the cap beams creates a large initial penetration.
		const float LintelZ = Levels * LevelHeight + Beam * 0.5f;
		AddBrick(OutData, FVector(0.0f, 0.0f, LintelZ), FVector(Depth, Gap + Column * 2.0f, Beam),
			EABTSM7BuildingMaterial::Iron);
		break;
	}
	case EABTSM73Silhouette::TwinTowerBridge:
	{
		const float TowerWidth = Width * 0.58f;
		const float Gap = Width * 0.82f;
		const float OffsetY = (Gap + TowerWidth) * 0.5f;
		for (int32 Level = 0; Level < Levels; ++Level)
		{
			const float BottomZ = Level * LevelHeight;
			AddFourColumnStorey(OutData, -OffsetY, TowerWidth, Depth, BottomZ, LevelHeight, Column, Beam,
				Settings.PrimaryMaterial, true);
			AddFourColumnStorey(OutData, OffsetY, TowerWidth, Depth, BottomZ, LevelHeight, Column, Beam,
				Settings.PrimaryMaterial, true);
		}
		// M7.3-A keeps the bridge on the top course so no upper tower column can
		// overlap the span. Mid-height bridges are deferred to the connection pass.
		const float BridgeZ = Levels * LevelHeight + Beam * 0.5f;
		AddBrick(OutData, FVector(0.0f, 0.0f, BridgeZ), FVector(Depth * 0.82f, Gap + Column * 2.0f, Beam),
			EABTSM7BuildingMaterial::Iron);
		break;
	}
	default:
		OutError = TEXT("UnsupportedSilhouette");
		return false;
	}

	FinalizeBoundsAndSupports(OutData);
	if (OutData.Bricks.IsEmpty())
	{
		OutError = TEXT("NoBricksGenerated");
		return false;
	}
	if (OutData.Bricks.Num() > FMath::Clamp(Settings.MaxBrickCount, 5, 100))
	{
		OutError = FString::Printf(TEXT("BrickBudgetExceeded:%d:%d"), OutData.Bricks.Num(), Settings.MaxBrickCount);
		return false;
	}
	return true;
}

void FABTSM73StructureBuilder::FinalizeBoundsAndSupports(FABTSM73StructureData& Data)
{
	Data.LocalBounds = FBox(EForceInit::ForceInit);
	for (const FABTSM73BrickNode& Node : Data.Bricks)
	{
		Data.LocalBounds += Node.LocalCenter - Node.DimensionsCM * 0.5f;
		Data.LocalBounds += Node.LocalCenter + Node.DimensionsCM * 0.5f;
		const float Bottom = Node.LocalCenter.Z - Node.DimensionsCM.Z * 0.5f;
		if (FMath::Abs(Bottom) <= ContactToleranceCM)
		{
			Data.GroundNodeIds.Add(Node.NodeId);
			Data.GroundSupportPoints.Add(FVector2D(Node.LocalCenter.X, Node.LocalCenter.Y));
		}
	}
	Data.FootprintHalfExtent = FVector2D(
		FMath::Max(FMath::Abs(Data.LocalBounds.Min.X), FMath::Abs(Data.LocalBounds.Max.X)),
		FMath::Max(FMath::Abs(Data.LocalBounds.Min.Y), FMath::Abs(Data.LocalBounds.Max.Y)));

	for (const FABTSM73BrickNode& Lower : Data.Bricks)
	{
		const float LowerTop = Lower.LocalCenter.Z + Lower.DimensionsCM.Z * 0.5f;
		for (const FABTSM73BrickNode& Upper : Data.Bricks)
		{
			if (Lower.NodeId == Upper.NodeId) continue;
			const float UpperBottom = Upper.LocalCenter.Z - Upper.DimensionsCM.Z * 0.5f;
			if (FMath::Abs(UpperBottom - LowerTop) > ContactToleranceCM) continue;
			const float XOverlap = OverlapLength(Lower.LocalCenter.X, Lower.DimensionsCM.X, Upper.LocalCenter.X, Upper.DimensionsCM.X);
			const float YOverlap = OverlapLength(Lower.LocalCenter.Y, Lower.DimensionsCM.Y, Upper.LocalCenter.Y, Upper.DimensionsCM.Y);
			if (XOverlap <= KINDA_SMALL_NUMBER || YOverlap <= KINDA_SMALL_NUMBER) continue;
			FABTSM73SupportEdge& Edge = Data.SupportEdges.AddDefaulted_GetRef();
			Edge.LowerNodeId = Lower.NodeId;
			Edge.UpperNodeId = Upper.NodeId;
			Edge.ContactAreaCM2 = XOverlap * YOverlap;
		}
	}
}
