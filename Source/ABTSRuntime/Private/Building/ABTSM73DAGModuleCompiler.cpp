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
		const float RealizedColumnWidthCM = Support.RealizedColumnWidthCM > 0.0f
			? Support.RealizedColumnWidthCM : LayoutSettings.ColumnWidthCM;
		if (Support.RealizedColumnCenters.IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("DAGColumnCentersMissing:%d:%d"),
				Support.SupportMacroNodeId,
				Support.LoadMacroNodeId);
			return false;
		}
		FABTSM73DAGPhysicalSupportMapping& Mapping = OutData.DAGPhysicalSupportMappings.AddDefaulted_GetRef();
		Mapping.SupportMacroNodeId = Support.SupportMacroNodeId;
		Mapping.LoadMacroNodeId = Support.LoadMacroNodeId;
		Mapping.SupportPlateNodeId = *SupportPlateId;
		Mapping.LoadPlateNodeId = *LoadPlateId;
		Mapping.SupportPattern = Support.SupportPattern;
		Mapping.RealizedColumnWidthCM = RealizedColumnWidthCM;
		for (const FVector2D& CenterXY : Support.RealizedColumnCenters)
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
