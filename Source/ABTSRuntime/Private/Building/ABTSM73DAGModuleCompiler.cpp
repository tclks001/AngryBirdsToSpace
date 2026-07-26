// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAGModuleCompiler.h"

#include "Building/ABTSM73DAGContactGraphBuilder.h"
#include "Building/ABTSM73DAGTypes.h"
#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73StructureData.h"

namespace
{
	const FABTSM73DAGMacroLayout* FindMacroLayoutForCompilation(const FABTSM73DAGSpatialLayout& Layout, const int32 MacroNodeId)
	{
		return Layout.MacroLayouts.FindByPredicate([MacroNodeId](const FABTSM73DAGMacroLayout& Candidate)
		{
			return Candidate.MacroNodeId == MacroNodeId;
		});
	}

	int32 CountSupportEdges(const TArray<FABTSM73DAGSelectedSupport>& Supports, const int32 MacroNodeId)
	{
		int32 Count = 0;
		for (const FABTSM73DAGSelectedSupport& Edge : Supports)
		{
			if (Edge.SupportMacroNodeId == MacroNodeId || Edge.LoadMacroNodeId == MacroNodeId) ++Count;
		}
		return Count;
	}

	bool MakeColumnCenters(const FBox2D& Region, const FABTSM73DAGLayoutSettings& Settings,
		const EABTSM73DAGSupportPattern Pattern, const float ColumnWidthCM,
		TArray<FVector2D>& OutCenters)
	{
		OutCenters.Reset();
		const float Half = ColumnWidthCM * 0.5f + Settings.ColumnClearanceCM;
		const FVector2D SafeMin = Region.Min + FVector2D(Half, Half);
		const FVector2D SafeMax = Region.Max - FVector2D(Half, Half);
		if (SafeMax.X < SafeMin.X || SafeMax.Y < SafeMin.Y) return false;
		const FVector2D Center = (SafeMin + SafeMax) * 0.5f;
		const FVector2D SafeSpan = SafeMax - SafeMin;
		// SafeMin/SafeMax already include half a column plus clearance. Use the
		// complete safe span so thin columns produce the largest valid support hull.
		const float OffsetX = SafeSpan.X * 0.5f;
		const float OffsetY = SafeSpan.Y * 0.5f;
		switch (Pattern)
		{
		case EABTSM73DAGSupportPattern::SingleColumnInterface:
			OutCenters.Add(Center);
			return true;
		case EABTSM73DAGSupportPattern::TwoColumnLine:
		{
			const bool bAlongX = SafeSpan.X >= SafeSpan.Y;
			if ((bAlongX ? OffsetX : OffsetY) <= KINDA_SMALL_NUMBER) return false;
			if (bAlongX)
			{
				OutCenters.Add(Center + FVector2D(-OffsetX, 0.0f));
				OutCenters.Add(Center + FVector2D(OffsetX, 0.0f));
			}
			else
			{
				OutCenters.Add(Center + FVector2D(0.0f, -OffsetY));
				OutCenters.Add(Center + FVector2D(0.0f, OffsetY));
			}
			return true;
		}
		case EABTSM73DAGSupportPattern::ThreeColumnTripod:
		{
			if (OffsetX <= KINDA_SMALL_NUMBER || OffsetY <= KINDA_SMALL_NUMBER) return false;
			const bool bBaseAlongX = SafeSpan.X >= SafeSpan.Y;
			if (bBaseAlongX)
			{
				OutCenters.Add(Center + FVector2D(-OffsetX, -OffsetY));
				OutCenters.Add(Center + FVector2D(OffsetX, -OffsetY));
				OutCenters.Add(Center + FVector2D(0.0f, OffsetY));
			}
			else
			{
				OutCenters.Add(Center + FVector2D(-OffsetX, -OffsetY));
				OutCenters.Add(Center + FVector2D(-OffsetX, OffsetY));
				OutCenters.Add(Center + FVector2D(OffsetX, 0.0f));
			}
			return true;
		}
		case EABTSM73DAGSupportPattern::FourColumnFootprint:
			if (OffsetX <= KINDA_SMALL_NUMBER || OffsetY <= KINDA_SMALL_NUMBER) return false;
			OutCenters.Add(Center + FVector2D(-OffsetX, -OffsetY));
			OutCenters.Add(Center + FVector2D(OffsetX, -OffsetY));
			OutCenters.Add(Center + FVector2D(-OffsetX, OffsetY));
			OutCenters.Add(Center + FVector2D(OffsetX, OffsetY));
			return true;
		default:
			return false;
		}
	}
}

void FABTSM73DAGModuleCompiler::AddBrick(
	FABTSM73StructureData& Data,
	const int32 MacroNodeId,
	const FVector& Center,
	const FVector& Dimensions,
	const EABTSM73BrickSemanticRole Role,
	const int32 StructuralLevel,
	const EABTSM7BuildingMaterial Material)
{
	FABTSM73BrickNode& Node = Data.Bricks.AddDefaulted_GetRef();
	Node.NodeId = Data.Bricks.Num() - 1;
	Node.MacroNodeId = MacroNodeId;
	Node.LocalCenter = Center;
	Node.DimensionsCM = Dimensions;
	Node.Material = Material;
	Node.OriginalMaterial = Material;
	Node.SemanticRole = Role;
	Node.StoreyIndex = StructuralLevel;
}

bool FABTSM73DAGModuleCompiler::Compile(
	const FABTSM73GenerationSettings& BuildingSettings,
	const FABTSM73DAGGenerationResult& Graph,
	const FABTSM73DAGLayoutSettings& LayoutSettings,
	const FABTSM73DAGSpatialLayout& Layout,
	FABTSM73StructureData& OutData,
	FString& OutError) const
{
	OutData = FABTSM73StructureData();
	OutError.Reset();
	if (!Graph.bAccepted || !Layout.bAccepted)
	{
		OutError = TEXT("DAGCompileInputRejected");
		return false;
	}
	TMap<int32, int32> PlateByMacro;
	for (const FABTSM73DAGMacroLayout& MacroLayout : Layout.MacroLayouts)
	{
		const EABTSM73BrickSemanticRole Role = CountSupportEdges(Layout.SelectedSupports, MacroLayout.MacroNodeId) > 1
			? EABTSM73BrickSemanticRole::Carrier : EABTSM73BrickSemanticRole::Deck;
		AddBrick(OutData, MacroLayout.MacroNodeId, MacroLayout.PlateCenter, MacroLayout.PlateDimensionsCM,
			Role, MacroLayout.StructuralLevel, BuildingSettings.PrimaryMaterial);
		PlateByMacro.Add(MacroLayout.MacroNodeId, OutData.Bricks.Last().NodeId);
	}

	for (const FABTSM73DAGSelectedSupport& Support : Layout.SelectedSupports)
	{
		const int32* SupportPlateId = PlateByMacro.Find(Support.SupportMacroNodeId);
		const int32* LoadPlateId = PlateByMacro.Find(Support.LoadMacroNodeId);
		const FABTSM73DAGMacroLayout* SupportLayout = FindMacroLayoutForCompilation(Layout, Support.SupportMacroNodeId);
		const FABTSM73DAGMacroLayout* LoadLayout = FindMacroLayoutForCompilation(Layout, Support.LoadMacroNodeId);
		if (SupportPlateId == nullptr || LoadPlateId == nullptr || SupportLayout == nullptr || LoadLayout == nullptr)
		{
			OutError = TEXT("DAGCompilePlateMappingMissing");
			return false;
		}
		const float BottomZ = SupportLayout->PlateCenter.Z + SupportLayout->PlateDimensionsCM.Z * 0.5f;
		const float TopZ = LoadLayout->PlateCenter.Z - LoadLayout->PlateDimensionsCM.Z * 0.5f;
		const float Height = TopZ - BottomZ;
		if (Height < LayoutSettings.MinColumnHeightCM)
		{
			OutError = FString::Printf(TEXT("DAGColumnTooShort:%d:%d:%.2f"), Support.SupportMacroNodeId, Support.LoadMacroNodeId, Height);
			return false;
		}
		TArray<FVector2D> Centers;
		const float RealizedColumnWidthCM = Support.RealizedColumnWidthCM > 0.0f
			? Support.RealizedColumnWidthCM : LayoutSettings.ColumnWidthCM;
		if (!MakeColumnCenters(Support.FeasibleColumnRegion, LayoutSettings, Support.SupportPattern,
			RealizedColumnWidthCM, Centers))
		{
			OutError = FString::Printf(TEXT("DAGColumnRegionInvalid:%d:%d"), Support.SupportMacroNodeId, Support.LoadMacroNodeId);
			return false;
		}
		FABTSM73DAGPhysicalSupportMapping& Mapping = OutData.DAGPhysicalSupportMappings.AddDefaulted_GetRef();
		Mapping.SupportMacroNodeId = Support.SupportMacroNodeId;
		Mapping.LoadMacroNodeId = Support.LoadMacroNodeId;
		Mapping.SupportPlateNodeId = *SupportPlateId;
		Mapping.LoadPlateNodeId = *LoadPlateId;
		Mapping.SupportPattern = Support.SupportPattern;
		Mapping.RealizedColumnWidthCM = RealizedColumnWidthCM;
		for (const FVector2D& CenterXY : Centers)
		{
			AddBrick(OutData, INDEX_NONE, FVector(CenterXY.X, CenterXY.Y, (BottomZ + TopZ) * 0.5f),
				FVector(RealizedColumnWidthCM, RealizedColumnWidthCM, Height),
				EABTSM73BrickSemanticRole::Column, LoadLayout->StructuralLevel, BuildingSettings.PrimaryMaterial);
			Mapping.ColumnNodeIds.Add(OutData.Bricks.Last().NodeId);
		}
	}
	OutData.DAGMacroNodeCount = Graph.MacroNodes.Num();
	OutData.DAGSelectedSupportCount = Layout.SelectedSupports.Num();
	OutData.DAGMinSupportContactAreaRatio = LayoutSettings.MinSupportContactAreaRatio;
	OutData.DAGTopologyHash = Graph.CanonicalTopologyHash;
	FABTSM73DAGContactGraphBuilder ContactBuilder;
	if (!ContactBuilder.RebuildAndAudit(LayoutSettings, OutData, OutError)) return false;
	return true;
}
