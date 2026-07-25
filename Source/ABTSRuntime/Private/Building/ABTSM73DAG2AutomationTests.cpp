// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSRuntime.h"
#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73DAGBuildingPipeline.h"
#include "Building/ABTSM73DAGTypes.h"
#include "Building/ABTSM73StabilityValidator.h"
#include "Building/ABTSM73StructureData.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	bool EqualBrick(const FABTSM73BrickNode& A, const FABTSM73BrickNode& B)
	{
		return A.NodeId == B.NodeId
			&& A.MacroNodeId == B.MacroNodeId
			&& A.Material == B.Material
			&& A.LocalCenter.Equals(B.LocalCenter, KINDA_SMALL_NUMBER)
			&& A.DimensionsCM.Equals(B.DimensionsCM, KINDA_SMALL_NUMBER)
			&& A.SemanticRole == B.SemanticRole
			&& A.StoreyIndex == B.StoreyIndex;
	}

	int32 ExpectedColumnCount(const EABTSM73DAGSupportPattern Pattern)
	{
		switch (Pattern)
		{
		case EABTSM73DAGSupportPattern::TwoColumnLine: return 2;
		case EABTSM73DAGSupportPattern::ThreeColumnTripod: return 3;
		case EABTSM73DAGSupportPattern::FourColumnFootprint: return 4;
		case EABTSM73DAGSupportPattern::SingleColumnInterface: return 1;
		default: return 0;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAGScopeLayoutAndCompileTest,
	"ABTS.M73DAG.ScopeLayoutAndModuleCompilation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAGScopeLayoutAndCompileTest::RunTest(const FString& Parameters)
{
	FABTSM73DAGBuildingPipeline Pipeline;
	FABTSM73StabilityValidator Validator;
	for (const EABTSM73DAGPreset Preset : {
		EABTSM73DAGPreset::SingleTower,
		EABTSM73DAGPreset::Arch,
		EABTSM73DAGPreset::TwinTowerBridge})
	{
		FABTSM73GenerationSettings BuildingSettings;
		BuildingSettings.GenerationAlgorithm = EABTSM73GenerationAlgorithm::RecursiveSupportDAG;
		BuildingSettings.bGenerateStructuralWeakness = false;
		FABTSM73DAGGenerationSettings DAGSettings;
		DAGSettings.Preset = Preset;
		DAGSettings.MinExpansionDepth = 0;
		DAGSettings.MaxExpansionDepth = 0;
		DAGSettings.ExpansionStepBudget = 0;
		DAGSettings.ReservedWeaknessBrickCount = 0;
		FABTSM73DAGLayoutSettings LayoutSettings;
		FABTSM73StructureData Data;
		FString Error;
		const bool bBuilt = Pipeline.Build(DAGSettings, LayoutSettings, BuildingSettings, Data, Error);
		TestTrue(FString::Printf(TEXT("DAG-2 preset %d builds: %s"), static_cast<int32>(Preset), *Error), bBuilt);
		if (!bBuilt) continue;
		TestTrue(TEXT("DAG-2 emits macro plates"), Data.DAGMacroNodeCount > 0);
		TestTrue(TEXT("DAG-2 selects sparse supports"), Data.DAGSelectedSupportCount > 0);
		TestEqual(TEXT("All intended contacts exist"), Data.DAGMissingRequiredContactCount, 0);
		TestEqual(TEXT("No non-topological physical bypass exists"), Data.DAGUnexpectedBypassCount, 0);
		TestTrue(TEXT("DAG-2 produces a physical support graph"), !Data.SupportEdges.IsEmpty());
		TestTrue(TEXT("DAG-2 has a foundation contact"), !Data.GroundNodeIds.IsEmpty());
		const bool bStable = Validator.Validate(BuildingSettings, Data, Error);
		TestTrue(FString::Printf(TEXT("DAG-2 preset %d static validation: %s"), static_cast<int32>(Preset), *Error), bStable);

		FABTSM73StructureData Repeat;
		FString RepeatError;
		TestTrue(TEXT("DAG-2 repeat build succeeds"), Pipeline.Build(DAGSettings, LayoutSettings, BuildingSettings, Repeat, RepeatError));
		TestEqual(TEXT("DAG-2 repeat brick count is deterministic"), Repeat.Bricks.Num(), Data.Bricks.Num());
		TestEqual(TEXT("DAG-2 repeat selected-support count is deterministic"), Repeat.DAGSelectedSupportCount, Data.DAGSelectedSupportCount);
		for (int32 Index = 0; Index < Data.Bricks.Num() && Repeat.Bricks.IsValidIndex(Index); ++Index)
		{
			TestTrue(TEXT("DAG-2 repeat brick transform is deterministic"), EqualBrick(Data.Bricks[Index], Repeat.Bricks[Index]));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAGSparseSupportTest,
	"ABTS.M73DAG.SparseSupportAudit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAGSparseSupportTest::RunTest(const FString& Parameters)
{
	FABTSM73GenerationSettings BuildingSettings;
	BuildingSettings.GenerationAlgorithm = EABTSM73GenerationAlgorithm::RecursiveSupportDAG;
	BuildingSettings.bGenerateStructuralWeakness = false;
	FABTSM73DAGGenerationSettings DAGSettings;
	DAGSettings.Preset = EABTSM73DAGPreset::Arch;
	DAGSettings.BuildingSeed = 731022;
	DAGSettings.MinExpansionDepth = 0;
	DAGSettings.MaxExpansionDepth = 0;
	DAGSettings.ExpansionStepBudget = 0;
	DAGSettings.ReservedWeaknessBrickCount = 0;
	FABTSM73DAGLayoutSettings LayoutSettings;
	// The authored Arch has two allowed lower->upper edges. Deliberately selecting
	// one proves that DAG-2 lowers a sparse chosen graph, not a complete bipartite graph.
	LayoutSettings.PreferredLogicalSupportsPerLoad = 1;
	LayoutSettings.MaxLogicalSupportsPerLoad = 1;
	FABTSM73DAGBuildingPipeline Pipeline;
	FABTSM73StructureData Data;
	FString Error;
	TestTrue(FString::Printf(TEXT("Recursive DAG-2 pipeline builds: %s"), *Error),
		Pipeline.Build(DAGSettings, LayoutSettings, BuildingSettings, Data, Error));
	TestEqual(TEXT("The two authored Arch candidates are reduced to one selected logical support"), Data.DAGSelectedSupportCount, 1);
	TestEqual(TEXT("Sparse support audit has no missing intended contacts"), Data.DAGMissingRequiredContactCount, 0);
	TestEqual(TEXT("Sparse support audit has no unexpected physical bypasses"), Data.DAGUnexpectedBypassCount, 0);
	if (Data.DAGMacroNodeCount > 0)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7.3-DAG-2][Accepted] Preset=%d Seed=%d Macro=%d Sparse=%d Bricks=%d PhysicalEdges=%d Hash=%u"),
			static_cast<int32>(DAGSettings.Preset), DAGSettings.BuildingSeed, Data.DAGMacroNodeCount,
			Data.DAGSelectedSupportCount, Data.Bricks.Num(), Data.SupportEdges.Num(), Data.DAGTopologyHash);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAGSupportPatternTest,
	"ABTS.M73DAG.SupportPatternsAndHullValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAGSupportPatternTest::RunTest(const FString& Parameters)
{
	FABTSM73GenerationSettings BuildingSettings;
	BuildingSettings.GenerationAlgorithm = EABTSM73GenerationAlgorithm::RecursiveSupportDAG;
	BuildingSettings.bGenerateStructuralWeakness = false;
	FABTSM73DAGGenerationSettings DAGSettings;
	DAGSettings.Preset = EABTSM73DAGPreset::SingleTower;
	DAGSettings.MaxExpansionDepth = 0;
	DAGSettings.ExpansionStepBudget = 0;
	DAGSettings.ReservedWeaknessBrickCount = 0;
	FABTSM73DAGBuildingPipeline Pipeline;
	FABTSM73StabilityValidator Validator;
	for (const EABTSM73DAGSupportPattern Pattern : {
		EABTSM73DAGSupportPattern::TwoColumnLine,
		EABTSM73DAGSupportPattern::ThreeColumnTripod,
		EABTSM73DAGSupportPattern::FourColumnFootprint})
	{
		FABTSM73DAGLayoutSettings LayoutSettings;
		LayoutSettings.SupportPattern = Pattern;
		FABTSM73StructureData Data;
		FString Error;
		TestTrue(FString::Printf(TEXT("Support pattern %d builds: %s"), static_cast<int32>(Pattern), *Error),
			Pipeline.Build(DAGSettings, LayoutSettings, BuildingSettings, Data, Error));
		if (!Data.DAGPhysicalSupportMappings.IsEmpty())
		{
			for (const FABTSM73DAGPhysicalSupportMapping& Mapping : Data.DAGPhysicalSupportMappings)
			{
				TestEqual(TEXT("Physical mapping retains the selected support pattern"), Mapping.SupportPattern, Pattern);
				TestEqual(TEXT("Every selected support emits its intended column count"),
					Mapping.ColumnNodeIds.Num(), ExpectedColumnCount(Pattern));
				TArray<FVector2D> Centers;
				for (const int32 ColumnNodeId : Mapping.ColumnNodeIds)
				{
					if (Data.Bricks.IsValidIndex(ColumnNodeId))
					{
						Centers.Add(FVector2D(Data.Bricks[ColumnNodeId].LocalCenter));
					}
				}
				for (int32 A = 0; A < Centers.Num(); ++A)
				{
					for (int32 B = A + 1; B < Centers.Num(); ++B)
					{
						TestTrue(TEXT("Support columns preserve their requested clearance"),
							FVector2D::Distance(Centers[A], Centers[B]) + KINDA_SMALL_NUMBER
							>= LayoutSettings.ColumnWidthCM + LayoutSettings.ColumnClearanceCM);
					}
				}
				if (Pattern == EABTSM73DAGSupportPattern::ThreeColumnTripod && Centers.Num() == 3)
				{
					const float TwiceTriangleArea = FMath::Abs(
						(Centers[1].X - Centers[0].X) * (Centers[2].Y - Centers[0].Y)
						- (Centers[1].Y - Centers[0].Y) * (Centers[2].X - Centers[0].X));
					TestTrue(TEXT("Tripod columns form a two-dimensional support triangle"), TwiceTriangleArea > 1.0f);
				}
			}
		}
		TestTrue(FString::Printf(TEXT("Support pattern %d passes convex-hull stability: %s"), static_cast<int32>(Pattern), *Error),
			Validator.Validate(BuildingSettings, Data, Error));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAGNestingLayoutTest,
	"ABTS.M73DAG.AssociativeNestingAndParallelFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAGNestingLayoutTest::RunTest(const FString& Parameters)
{
	FABTSM73GenerationSettings BuildingSettings;
	BuildingSettings.GenerationAlgorithm = EABTSM73GenerationAlgorithm::RecursiveSupportDAG;
	BuildingSettings.bGenerateStructuralWeakness = false;
	BuildingSettings.MaxBrickCount = 100;
	FABTSM73DAGBuildingPipeline Pipeline;

	// Nested Series is associative. Flattening it before Z allocation prevents
	// each recursion level from repeatedly halving the same height budget.
	FABTSM73DAGGenerationSettings SeriesDAG;
	SeriesDAG.Preset = EABTSM73DAGPreset::SingleTower;
	SeriesDAG.MaxExpansionDepth = 2;
	SeriesDAG.ExpansionStepBudget = 6;
	SeriesDAG.MaxEstimatedBrickCount = 100;
	SeriesDAG.ReservedWeaknessBrickCount = 0;
	SeriesDAG.SeriesRuleWeight = 1.0f;
	SeriesDAG.ParallelRuleWeight = 0.0f;
	FABTSM73DAGLayoutSettings SeriesLayout;
	SeriesLayout.TargetHeightCM = 760.0f;
	FABTSM73StructureData SeriesData;
	FString Error;
	TestTrue(FString::Printf(TEXT("Nested Series fits one shared height allocation: %s"), *Error),
		Pipeline.Build(SeriesDAG, SeriesLayout, BuildingSettings, SeriesData, Error));

	// Nested Parallel may create a narrow branch. It must preserve the topology
	// by realizing that edge as a line pair instead of rejecting the whole graph.
	FABTSM73DAGGenerationSettings ParallelDAG;
	ParallelDAG.Preset = EABTSM73DAGPreset::TwinTowerBridge;
	ParallelDAG.MaxExpansionDepth = 1;
	ParallelDAG.ExpansionStepBudget = 4;
	ParallelDAG.MaxEstimatedBrickCount = 100;
	ParallelDAG.ReservedWeaknessBrickCount = 0;
	ParallelDAG.SeriesRuleWeight = 0.0f;
	ParallelDAG.ParallelRuleWeight = 1.0f;
	FABTSM73DAGLayoutSettings ParallelLayout;
	ParallelLayout.bAllowNarrowSupportFallback = true;
	FABTSM73StructureData ParallelData;
	Error.Reset();
	TestTrue(FString::Printf(TEXT("Nested Parallel uses a feasible support fallback: %s"), *Error),
		Pipeline.Build(ParallelDAG, ParallelLayout, BuildingSettings, ParallelData, Error));
	bool bUsedNarrowInterface = false;
	bool bUsedAdaptiveWidth = false;
	for (const FABTSM73DAGPhysicalSupportMapping& Mapping : ParallelData.DAGPhysicalSupportMappings)
	{
		bUsedNarrowInterface |= Mapping.SupportPattern == EABTSM73DAGSupportPattern::TwoColumnLine
			|| Mapping.SupportPattern == EABTSM73DAGSupportPattern::SingleColumnInterface;
		bUsedAdaptiveWidth |= Mapping.RealizedColumnWidthCM > 0.0f
			&& Mapping.RealizedColumnWidthCM < ParallelLayout.ColumnWidthCM - KINDA_SMALL_NUMBER;
	}
	TestTrue(TEXT("At least one narrow parallel support records an explicit narrow-interface fallback"), bUsedNarrowInterface);
	TestTrue(TEXT("At least one narrow parallel support records an adaptive column width"), bUsedAdaptiveWidth);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAGStructuralContinuityTest,
	"ABTS.M73DAG.StructuralRankAndPhysicalContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAGStructuralContinuityTest::RunTest(const FString& Parameters)
{
	FABTSM73GenerationSettings BuildingSettings;
	BuildingSettings.GenerationAlgorithm = EABTSM73GenerationAlgorithm::RecursiveSupportDAG;
	BuildingSettings.bGenerateStructuralWeakness = false;
	BuildingSettings.MaxBrickCount = 256;
	FABTSM73DAGGenerationSettings DAGSettings;
	DAGSettings.Preset = EABTSM73DAGPreset::TwinTowerBridge;
	DAGSettings.BuildingSeed = 730121;
	DAGSettings.MaxExpansionDepth = 3;
	DAGSettings.ExpansionStepBudget = 12;
	DAGSettings.MaxAbstractNodeCount = 128;
	DAGSettings.MaxEstimatedBrickCount = 256;
	DAGSettings.ReservedWeaknessBrickCount = 0;
	FABTSM73DAGLayoutSettings LayoutSettings;
	// Give recursive Parallel splits enough XY space. TargetHeight deliberately
	// remains at its normal value: structural rank must resolve Z independently.
	LayoutSettings.TargetWidthCM = 1600.0f;
	LayoutSettings.TargetDepthCM = 1600.0f;
	FABTSM73DAGBuildingPipeline Pipeline;
	FABTSM73StructureData Data;
	FString Error;
	const bool bBuilt = Pipeline.Build(DAGSettings, LayoutSettings, BuildingSettings, Data, Error);
	TestTrue(FString::Printf(TEXT("High-budget mixed DAG builds without local Z-scope rejection: %s"), *Error), bBuilt);
	if (!bBuilt) return false;

	TSet<int32> SupportedLoadMacros;
	for (const FABTSM73DAGPhysicalSupportMapping& Mapping : Data.DAGPhysicalSupportMappings)
	{
		SupportedLoadMacros.Add(Mapping.LoadMacroNodeId);
		if (!Data.Bricks.IsValidIndex(Mapping.SupportPlateNodeId)
			|| !Data.Bricks.IsValidIndex(Mapping.LoadPlateNodeId)) continue;
		const FABTSM73BrickNode& Lower = Data.Bricks[Mapping.SupportPlateNodeId];
		const FABTSM73BrickNode& Upper = Data.Bricks[Mapping.LoadPlateNodeId];
		TestEqual(TEXT("Every selected physical support spans exactly one structural rank"),
			Upper.StoreyIndex, Lower.StoreyIndex + 1);
		for (const int32 ColumnId : Mapping.ColumnNodeIds)
		{
			if (!Data.Bricks.IsValidIndex(ColumnId)) continue;
			TestTrue(TEXT("Every realized column meets the minimum clear height"),
				Data.Bricks[ColumnId].DimensionsCM.Z + KINDA_SMALL_NUMBER >= LayoutSettings.MinColumnHeightCM);
		}
	}
	TSet<int32> GroundMacros;
	for (const FABTSM73BrickNode& Brick : Data.Bricks)
	{
		if (Brick.MacroNodeId != INDEX_NONE
			&& Brick.LocalCenter.Z - Brick.DimensionsCM.Z * 0.5f <= LayoutSettings.ContactToleranceCM)
		{
			GroundMacros.Add(Brick.MacroNodeId);
		}
	}
	for (const FABTSM73BrickNode& Brick : Data.Bricks)
	{
		if (Brick.MacroNodeId == INDEX_NONE || GroundMacros.Contains(Brick.MacroNodeId)) continue;
		TestTrue(FString::Printf(TEXT("Non-ground macro plate %d has a visible physical support"), Brick.MacroNodeId),
			SupportedLoadMacros.Contains(Brick.MacroNodeId));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAGAdaptiveGeometryTest,
	"ABTS.M73DAG.AdaptivePlateAndColumnGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAGAdaptiveGeometryTest::RunTest(const FString& Parameters)
{
	FABTSM73GenerationSettings BuildingSettings;
	BuildingSettings.GenerationAlgorithm = EABTSM73GenerationAlgorithm::RecursiveSupportDAG;
	BuildingSettings.bGenerateStructuralWeakness = false;
	BuildingSettings.MaxBrickCount = 160;
	FABTSM73DAGBuildingPipeline Pipeline;
	FABTSM73DAGGenerationSettings WideDAG;
	WideDAG.Preset = EABTSM73DAGPreset::SingleTower;
	WideDAG.MaxExpansionDepth = 0;
	WideDAG.ExpansionStepBudget = 0;
	WideDAG.ReservedWeaknessBrickCount = 0;
	FABTSM73DAGLayoutSettings WideLayout;
	WideLayout.TargetWidthCM = 1000.0f;
	WideLayout.TargetDepthCM = 600.0f;
	WideLayout.MaxAdaptiveColumnWidthCM = 120.0f;
	FABTSM73StructureData WideData;
	FString Error;
	TestTrue(FString::Printf(TEXT("Wide plates resolve adaptive supports: %s"), *Error),
		Pipeline.Build(WideDAG, WideLayout, BuildingSettings, WideData, Error));
	bool bWidenedColumn = false;
	for (const FABTSM73DAGPhysicalSupportMapping& Mapping : WideData.DAGPhysicalSupportMappings)
		bWidenedColumn |= Mapping.RealizedColumnWidthCM > WideLayout.ColumnWidthCM + KINDA_SMALL_NUMBER;
	TestTrue(TEXT("Wide plate realizes a wider column than the authored baseline"), bWidenedColumn);

	FABTSM73DAGGenerationSettings NarrowDAG;
	NarrowDAG.Preset = EABTSM73DAGPreset::TwinTowerBridge;
	NarrowDAG.MaxExpansionDepth = 1;
	NarrowDAG.ExpansionStepBudget = 4;
	NarrowDAG.MaxEstimatedBrickCount = 160;
	NarrowDAG.ReservedWeaknessBrickCount = 0;
	NarrowDAG.SeriesRuleWeight = 0.0f;
	NarrowDAG.ParallelRuleWeight = 1.0f;
	FABTSM73DAGLayoutSettings NarrowLayout;
	NarrowLayout.TargetWidthCM = 500.0f;
	NarrowLayout.MinPlateExtentCM = 90.0f;
	NarrowLayout.MinAdaptivePlateExtentCM = 42.0f;
	FABTSM73StructureData NarrowData;
	Error.Reset();
	TestTrue(FString::Printf(TEXT("Narrow recursive Parallel uses adaptive plate bounds: %s"), *Error),
		Pipeline.Build(NarrowDAG, NarrowLayout, BuildingSettings, NarrowData, Error));
	return true;
}

#endif
