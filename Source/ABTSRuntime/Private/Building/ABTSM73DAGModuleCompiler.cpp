// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAGModuleCompiler.h"

#include "Building/ABTSM73DAGContactGraphBuilder.h"
#include "Building/ABTSM73DAGTypes.h"
#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73StructureData.h"

namespace
{
	const FABTSM73DAGMacroLayout* FindLayout(const FABTSM73DAGSpatialLayout& Layout, const int32 MacroNodeId)
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
		TArray<FVector2D>& OutCenters)
	{
		OutCenters.Reset();
		const float Half = Settings.ColumnWidthCM * 0.5f + Settings.ColumnClearanceCM;
		const FVector2D SafeMin = Region.Min + FVector2D(Half, Half);
		const FVector2D SafeMax = Region.Max - FVector2D(Half, Half);
		if (SafeMax.X < SafeMin.X || SafeMax.Y < SafeMin.Y) return false;
		const FVector2D Center = (SafeMin + SafeMax) * 0.5f;
		if (Settings.ColumnsPerSelectedSupport == 1)
		{
			OutCenters.Add(Center);
			return true;
		}
		const bool bAlongX = SafeMax.X - SafeMin.X >= SafeMax.Y - SafeMin.Y;
		const float Available = bAlongX ? SafeMax.X - SafeMin.X : SafeMax.Y - SafeMin.Y;
		const float Offset = FMath::Min(Available * 0.25f, Settings.ColumnWidthCM * 0.75f);
		if (Offset <= KINDA_SMALL_NUMBER) return false;
		FVector2D First = Center;
		FVector2D Second = Center;
		if (bAlongX) { First.X -= Offset; Second.X += Offset; }
		else { First.Y -= Offset; Second.Y += Offset; }
		OutCenters.Add(First);
		OutCenters.Add(Second);
		return true;
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
		const FABTSM73DAGMacroLayout* SupportLayout = FindLayout(Layout, Support.SupportMacroNodeId);
		const FABTSM73DAGMacroLayout* LoadLayout = FindLayout(Layout, Support.LoadMacroNodeId);
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
		if (!MakeColumnCenters(Support.FeasibleColumnRegion, LayoutSettings, Centers))
		{
			OutError = FString::Printf(TEXT("DAGColumnRegionInvalid:%d:%d"), Support.SupportMacroNodeId, Support.LoadMacroNodeId);
			return false;
		}
		FABTSM73DAGPhysicalSupportMapping& Mapping = OutData.DAGPhysicalSupportMappings.AddDefaulted_GetRef();
		Mapping.SupportMacroNodeId = Support.SupportMacroNodeId;
		Mapping.LoadMacroNodeId = Support.LoadMacroNodeId;
		Mapping.SupportPlateNodeId = *SupportPlateId;
		Mapping.LoadPlateNodeId = *LoadPlateId;
		for (const FVector2D& CenterXY : Centers)
		{
			AddBrick(OutData, INDEX_NONE, FVector(CenterXY.X, CenterXY.Y, (BottomZ + TopZ) * 0.5f),
				FVector(LayoutSettings.ColumnWidthCM, LayoutSettings.ColumnWidthCM, Height),
				EABTSM73BrickSemanticRole::Column, LoadLayout->StructuralLevel, BuildingSettings.PrimaryMaterial);
			Mapping.ColumnNodeIds.Add(OutData.Bricks.Last().NodeId);
		}
	}
	OutData.DAGMacroNodeCount = Graph.MacroNodes.Num();
	OutData.DAGSelectedSupportCount = Layout.SelectedSupports.Num();
	OutData.DAGTopologyHash = Graph.CanonicalTopologyHash;
	FABTSM73DAGContactGraphBuilder ContactBuilder;
	if (!ContactBuilder.RebuildAndAudit(LayoutSettings, OutData, OutError)) return false;
	return true;
}
