// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73StructureBuilder.h"

#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73StructureData.h"
#include "Building/ABTSM73WeaknessStructureBuilder.h"

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
	const EABTSM7BuildingMaterial Material,
	const EABTSM73BrickSemanticRole SemanticRole,
	const int32 StoreyIndex,
	const int32 BayIndex)
{
	FABTSM73BrickNode& Node = Data.Bricks.AddDefaulted_GetRef();
	Node.NodeId = Data.Bricks.Num() - 1;
	Node.Material = Material;
	Node.OriginalMaterial = Material;
	Node.LocalCenter = Center;
	Node.DimensionsCM = Dimensions.ComponentMax(FVector(1.0f));
	Node.SemanticRole = SemanticRole;
	Node.StoreyIndex = StoreyIndex;
	Node.BayIndex = BayIndex;
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
	const bool bAddRoof,
	const int32 StoreyIndex,
	const int32 BayIndex)
{
	const float HalfX = FMath::Max(0.0f, Depth * 0.5f - ColumnWidth * 0.5f);
	const float HalfY = FMath::Max(0.0f, Width * 0.5f - ColumnWidth * 0.5f);
	const float ColumnHeight = FMath::Max(ColumnWidth, LevelHeight - BeamHeight);
	const float ColumnZ = BottomZ + ColumnHeight * 0.5f;
	for (const float X : {-HalfX, HalfX})
	{
		for (const float Y : {CenterY - HalfY, CenterY + HalfY})
		{
			AddBrick(Data, FVector(X, Y, ColumnZ), FVector(ColumnWidth, ColumnWidth, ColumnHeight), Material,
				EABTSM73BrickSemanticRole::Column, StoreyIndex, BayIndex);
		}
	}
	if (bAddRoof)
	{
		AddBrick(Data, FVector(0.0f, CenterY, BottomZ + ColumnHeight + BeamHeight * 0.5f),
			FVector(Depth, Width, BeamHeight), Material, EABTSM73BrickSemanticRole::Deck, StoreyIndex, BayIndex);
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
				LevelHeight, Column, Beam, Settings.PrimaryMaterial, true, Level, 0);
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
				Settings.PrimaryMaterial, true, Level, 0);
			AddFourColumnStorey(OutData, OffsetY, TowerWidth, Depth, BottomZ, LevelHeight, Column, Beam,
				Settings.PrimaryMaterial, true, Level, 1);
		}
		// The connecting lintel is a new course above both tower caps. Placing it
		// at the same Z as the cap beams creates a large initial penetration.
		const float LintelZ = Levels * LevelHeight + Beam * 0.5f;
		AddBrick(OutData, FVector(0.0f, 0.0f, LintelZ), FVector(Depth, Gap + Column * 2.0f, Beam),
			EABTSM7BuildingMaterial::Iron, EABTSM73BrickSemanticRole::Connector, Levels, INDEX_NONE);
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
				Settings.PrimaryMaterial, true, Level, 0);
			AddFourColumnStorey(OutData, OffsetY, TowerWidth, Depth, BottomZ, LevelHeight, Column, Beam,
				Settings.PrimaryMaterial, true, Level, 1);
		}
		// A true middle corridor: the deck lands on the two tower floor slabs,
		// while its narrow X depth stays inside the aisle between front/back corner
		// columns of the next storey. This gives it vertical support without the
		// penetration that the old full-depth mid-height bridge produced.
		const int32 BridgeFloor = FMath::Clamp(Levels / 2, 1, FMath::Max(1, Levels - 1));
		const float BridgeBaseZ = BridgeFloor * LevelHeight;
		const float AvailableAisleDepth = FMath::Max(8.0f, Depth - Column * 2.0f);
		const float CorridorDepth = FMath::Max(8.0f, AvailableAisleDepth - 8.0f);
		const float LandingOverlap = FMath::Min(Column * 0.70f, TowerWidth * 0.25f);
		const float CorridorLength = Gap + LandingOverlap * 2.0f;
		AddBrick(OutData,
			FVector(0.0f, 0.0f, BridgeBaseZ + Beam * 0.5f),
			FVector(CorridorDepth, CorridorLength, Beam),
			EABTSM7BuildingMaterial::Iron, EABTSM73BrickSemanticRole::Connector, BridgeFloor, INDEX_NONE);

		const float RailThickness = FMath::Clamp(Column * 0.20f, 8.0f, CorridorDepth * 0.30f);
		const float RailHeight = FMath::Max(12.0f, LevelHeight - Beam * 2.0f);
		const float RailX = FMath::Max(0.0f, CorridorDepth * 0.5f - RailThickness * 0.5f);
		for (const float X : {-RailX, RailX})
		{
			AddBrick(OutData,
				FVector(X, 0.0f, BridgeBaseZ + Beam + RailHeight * 0.5f),
				FVector(RailThickness, CorridorLength, RailHeight),
				Settings.PrimaryMaterial, EABTSM73BrickSemanticRole::Rail, BridgeFloor, INDEX_NONE);
		}
		break;
	}
	default:
		OutError = TEXT("UnsupportedSilhouette");
		return false;
	}

	if (OutData.Bricks.IsEmpty())
	{
		OutError = TEXT("NoBricksGenerated");
		return false;
	}
	const int32 BrickBudget = FMath::Clamp(Settings.MaxBrickCount, 5, 100);
	if (OutData.Bricks.Num() > BrickBudget)
	{
		OutError = FString::Printf(TEXT("BrickBudgetExceeded:%d:%d"), OutData.Bricks.Num(), Settings.MaxBrickCount);
		return false;
	}
	FABTSM73WeaknessStructureBuilder WeaknessBuilder;
	if (!WeaknessBuilder.Apply(Settings, LevelHeight, Column, Beam, OutData, OutError)) return false;
	if (OutData.Bricks.Num() > BrickBudget)
	{
		OutError = FString::Printf(TEXT("BrickBudgetExceededWithWeakness:%d:%d"), OutData.Bricks.Num(), Settings.MaxBrickCount);
		return false;
	}
	FinalizeBoundsAndSupports(OutData);
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
