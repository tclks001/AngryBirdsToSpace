// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSRuntime.h"
#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73DAGBuildingPipeline.h"
#include "Building/ABTSM73DAGLoadSupportSolver.h"
#include "Building/ABTSM73DAGSupportGeometry.h"
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
		// The authored Arch has two lower->upper interfaces. DAG-2.3 must retain
		// both because one side alone cannot contain the upper plate's resultant.
		LayoutSettings.PreferredLogicalSupportsPerLoad = 2;
		LayoutSettings.MaxLogicalSupportsPerLoad = 2;
	FABTSM73DAGBuildingPipeline Pipeline;
	FABTSM73StructureData Data;
	FString Error;
	TestTrue(FString::Printf(TEXT("Recursive DAG-2 pipeline builds: %s"), *Error),
		Pipeline.Build(DAGSettings, LayoutSettings, BuildingSettings, Data, Error));
	TestEqual(TEXT("The two authored Arch candidates form one joint support group"), Data.DAGSelectedSupportCount, 2);
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
	FABTSM73DAGLayoutSettings CenteredTripodSettings;
	for (const FBox2D& TooNarrowRegion : {
		FBox2D(FVector2D(-60.5f, -60.5f), FVector2D(60.5f, 60.5f)),
		FBox2D(FVector2D(-89.5f, -60.5f), FVector2D(89.5f, 60.5f)),
		FBox2D(FVector2D(-60.5f, -89.5f), FVector2D(60.5f, 89.5f))})
	{
		TArray<FVector2D> RejectedCenters;
		TestFalse(TEXT("Tripod rejects a region whose diagonal square columns would overlap"),
			FABTSM73DAGSupportGeometry::MakeColumnCenters(
				TooNarrowRegion,
				CenteredTripodSettings,
				EABTSM73DAGSupportPattern::ThreeColumnTripod,
				CenteredTripodSettings.ColumnWidthCM,
				RejectedCenters));
		TestTrue(TEXT("Rejected tripod leaves no partial centers"), RejectedCenters.IsEmpty());
	}
	for (const FBox2D& MinimumClearRegion : {
		FBox2D(FVector2D(-70.5f, -70.5f), FVector2D(70.5f, 70.5f)),
		FBox2D(FVector2D(-90.5f, -60.5f), FVector2D(90.5f, 60.5f)),
		FBox2D(FVector2D(-60.5f, -90.5f), FVector2D(60.5f, 90.5f))})
	{
		TArray<FVector2D> AcceptedCenters;
		TestTrue(TEXT("Tripod accepts the boundary region once every square column has clearance"),
			FABTSM73DAGSupportGeometry::MakeColumnCenters(
				MinimumClearRegion,
				CenteredTripodSettings,
				EABTSM73DAGSupportPattern::ThreeColumnTripod,
				CenteredTripodSettings.ColumnWidthCM,
				AcceptedCenters));
	}

	// Candidate selection runs at the minimum adaptive width, while contact-area
	// realization may make the final columns wider. Lock the final-width recovery:
	// a 179x121 interface cannot hold this Tripod, but can hold a recalculated
	// TwoColumn fallback without rejecting the graph.
	FABTSM73DAGGenerationResult FallbackGraph;
	FallbackGraph.bAccepted = true;
	FABTSM73DAGMacroNode& GroundMacro = FallbackGraph.MacroNodes.AddDefaulted_GetRef();
	GroundMacro.NodeId = 0;
	FABTSM73DAGMacroNode& LoadMacro = FallbackGraph.MacroNodes.AddDefaulted_GetRef();
	LoadMacro.NodeId = 1;
	FABTSM73DAGSupportEdge& FallbackEdge = FallbackGraph.SupportEdges.AddDefaulted_GetRef();
	FallbackEdge.SupportNodeId = GroundMacro.NodeId;
	FallbackEdge.LoadNodeId = LoadMacro.NodeId;
	FallbackGraph.GroundNodeIds.Add(GroundMacro.NodeId);

	FABTSM73DAGSpatialLayout FallbackLayout;
	FallbackLayout.bAccepted = true;
	FABTSM73DAGMacroLayout& GroundLayout = FallbackLayout.MacroLayouts.AddDefaulted_GetRef();
	GroundLayout.MacroNodeId = GroundMacro.NodeId;
	GroundLayout.PlateCenter = FVector(0.0f, 0.0f, 20.0f);
	GroundLayout.PlateDimensionsCM = FVector(200.0f, 150.0f, 40.0f);
	GroundLayout.bGroundTerminal = true;
	FABTSM73DAGMacroLayout& LoadLayout = FallbackLayout.MacroLayouts.AddDefaulted_GetRef();
	LoadLayout.MacroNodeId = LoadMacro.NodeId;
	LoadLayout.PlateCenter = FVector(0.0f, 0.0f, 180.0f);
	LoadLayout.PlateDimensionsCM = FVector(560.0f, 400.0f, 40.0f);

	TMap<int32, TArray<FABTSM73DAGSelectedSupport>> FallbackCandidates;
	FABTSM73DAGSelectedSupport& FallbackCandidate =
		FallbackCandidates.FindOrAdd(LoadMacro.NodeId).AddDefaulted_GetRef();
	FallbackCandidate.SupportMacroNodeId = GroundMacro.NodeId;
	FallbackCandidate.LoadMacroNodeId = LoadMacro.NodeId;
	FallbackCandidate.FeasibleColumnRegion =
		FBox2D(FVector2D(-89.5f, -60.5f), FVector2D(89.5f, 60.5f));
	FallbackCandidate.SupportPattern = EABTSM73DAGSupportPattern::ThreeColumnTripod;

	FABTSM73DAGLayoutSettings FallbackSettings;
	FallbackSettings.MaxLogicalSupportsPerLoad = 1;
	FABTSM73DAGLoadSupportSolver LoadSupportSolver;
	FString FallbackError;
	TestTrue(
		FString::Printf(TEXT("Final-width narrow support resolves a lower-column fallback: %s"), *FallbackError),
		LoadSupportSolver.Solve(
			FallbackGraph,
			FallbackSettings,
			FallbackCandidates,
			FallbackLayout,
			FallbackError));
	TestEqual(TEXT("Final-width fallback records the realized two-column pattern"),
		FallbackLayout.SelectedSupports.Num(), 1);
	if (FallbackLayout.SelectedSupports.Num() == 1)
	{
		TestEqual(TEXT("Final-width fallback lowers Tripod to TwoColumn"),
			FallbackLayout.SelectedSupports[0].SupportPattern,
			EABTSM73DAGSupportPattern::TwoColumnLine);
		TestEqual(TEXT("Final-width fallback realizes two authoritative centers"),
			FallbackLayout.SelectedSupports[0].RealizedColumnCenters.Num(), 2);
		const float ContactAreaRatio =
			FallbackLayout.SelectedSupports[0].RealizedColumnCenters.Num()
			* FMath::Square(FallbackLayout.SelectedSupports[0].RealizedColumnWidthCM)
			/ (LoadLayout.PlateDimensionsCM.X * LoadLayout.PlateDimensionsCM.Y);
		TestTrue(TEXT("Final-width fallback recalculates enough two-column contact area"),
			ContactAreaRatio + KINDA_SMALL_NUMBER >= FallbackSettings.MinSupportContactAreaRatio);
	}

	// Two interfaces can fail in sequence as the shared contact-area width grows:
	// A: Tripod -> Two -> Single, then B: Tripod -> Two. This needs a monotonic
	// pass bound based on the whole support group, not a fixed three passes.
	FABTSM73DAGGenerationResult CascadeGraph;
	CascadeGraph.bAccepted = true;
	for (int32 NodeId = 0; NodeId < 3; ++NodeId)
	{
		FABTSM73DAGMacroNode& Macro = CascadeGraph.MacroNodes.AddDefaulted_GetRef();
		Macro.NodeId = NodeId;
	}
	for (int32 SupportId = 0; SupportId < 2; ++SupportId)
	{
		FABTSM73DAGSupportEdge& Edge = CascadeGraph.SupportEdges.AddDefaulted_GetRef();
		Edge.SupportNodeId = SupportId;
		Edge.LoadNodeId = 2;
		CascadeGraph.GroundNodeIds.Add(SupportId);
	}

	FABTSM73DAGSpatialLayout CascadeLayout;
	CascadeLayout.bAccepted = true;
	for (int32 SupportId = 0; SupportId < 2; ++SupportId)
	{
		FABTSM73DAGMacroLayout& SupportLayout = CascadeLayout.MacroLayouts.AddDefaulted_GetRef();
		SupportLayout.MacroNodeId = SupportId;
		SupportLayout.PlateCenter = FVector(0.0f, 0.0f, 20.0f);
		SupportLayout.PlateDimensionsCM = FVector(200.0f, 150.0f, 40.0f);
		SupportLayout.bGroundTerminal = true;
	}
	FABTSM73DAGMacroLayout& CascadeLoad = CascadeLayout.MacroLayouts.AddDefaulted_GetRef();
	CascadeLoad.MacroNodeId = 2;
	CascadeLoad.PlateCenter = FVector(0.0f, 0.0f, 180.0f);
	CascadeLoad.PlateDimensionsCM = FVector(600.0f, 595.2381f, 40.0f);

	TMap<int32, TArray<FABTSM73DAGSelectedSupport>> CascadeCandidates;
	const FBox2D CascadeRegions[] = {
		FBox2D(FVector2D(-57.0f, -50.0f), FVector2D(57.0f, 50.0f)),
		FBox2D(FVector2D(-85.0f, -70.0f), FVector2D(85.0f, 70.0f))
	};
	for (int32 SupportId = 0; SupportId < 2; ++SupportId)
	{
		FABTSM73DAGSelectedSupport& Candidate =
			CascadeCandidates.FindOrAdd(2).AddDefaulted_GetRef();
		Candidate.SupportMacroNodeId = SupportId;
		Candidate.LoadMacroNodeId = 2;
		Candidate.FeasibleColumnRegion = CascadeRegions[SupportId];
		Candidate.SupportPattern = EABTSM73DAGSupportPattern::ThreeColumnTripod;
	}

	FABTSM73DAGLayoutSettings CascadeSettings;
	CascadeSettings.MaxLogicalSupportsPerLoad = 2;
	FString CascadeError;
	TestTrue(
		FString::Printf(TEXT("Multi-support final-width fallback converges monotonically: %s"), *CascadeError),
		LoadSupportSolver.Solve(
			CascadeGraph,
			CascadeSettings,
			CascadeCandidates,
			CascadeLayout,
			CascadeError));
	TestEqual(TEXT("Cascade retains the required two-interface support group"),
		CascadeLayout.SelectedSupports.Num(), 2);
	if (CascadeLayout.SelectedSupports.Num() == 2)
	{
		TestEqual(TEXT("Cascade first interface reaches SingleColumn"),
			CascadeLayout.SelectedSupports[0].SupportPattern,
			EABTSM73DAGSupportPattern::SingleColumnInterface);
		TestEqual(TEXT("Cascade second interface reaches TwoColumn"),
			CascadeLayout.SelectedSupports[1].SupportPattern,
			EABTSM73DAGSupportPattern::TwoColumnLine);
		int32 TotalCascadeColumns = 0;
		for (const FABTSM73DAGSelectedSupport& Support : CascadeLayout.SelectedSupports)
		{
			TotalCascadeColumns += Support.RealizedColumnCenters.Num();
		}
		const float CascadeContactAreaRatio =
			TotalCascadeColumns
			* FMath::Square(CascadeLayout.SelectedSupports[0].RealizedColumnWidthCM)
			/ (CascadeLoad.PlateDimensionsCM.X * CascadeLoad.PlateDimensionsCM.Y);
		TestTrue(TEXT("Cascade recalculates the shared width after every pattern reduction"),
			CascadeContactAreaRatio + KINDA_SMALL_NUMBER >= CascadeSettings.MinSupportContactAreaRatio);
	}

	for (const FBox2D& Region : {
		FBox2D(FVector2D(-180.0f, -100.0f), FVector2D(180.0f, 100.0f)),
		FBox2D(FVector2D(-100.0f, -180.0f), FVector2D(100.0f, 180.0f))})
	{
		TArray<FVector2D> TripodCenters;
		TestTrue(TEXT("Shared tripod geometry fits the test region"),
			FABTSM73DAGSupportGeometry::MakeColumnCenters(
				Region,
				CenteredTripodSettings,
				EABTSM73DAGSupportPattern::ThreeColumnTripod,
				CenteredTripodSettings.ColumnWidthCM,
				TripodCenters));
		TestEqual(TEXT("Shared tripod geometry emits three contacts"), TripodCenters.Num(), 3);
		if (TripodCenters.Num() == 3)
		{
			const FVector2D ContactCentroid =
				(TripodCenters[0] + TripodCenters[1] + TripodCenters[2]) / 3.0f;
			const FVector2D RegionCenter = (Region.Min + Region.Max) * 0.5f;
			TestTrue(
				TEXT("Equal-area tripod contact centroid stays on the centered resultant"),
				FVector2D::Distance(ContactCentroid, RegionCenter) < 0.01f);
		}
	}

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
						const FVector2D Delta = (Centers[A] - Centers[B]).GetAbs();
						const float RequiredAxisSeparation =
							Mapping.RealizedColumnWidthCM + LayoutSettings.ColumnClearanceCM;
						TestTrue(TEXT("Compiled square columns preserve their requested AABB clearance"),
							Delta.X + KINDA_SMALL_NUMBER >= RequiredAxisSeparation
							|| Delta.Y + KINDA_SMALL_NUMBER >= RequiredAxisSeparation);
					}
				}
				if (Pattern == EABTSM73DAGSupportPattern::ThreeColumnTripod && Centers.Num() == 3)
				{
					const float TwiceTriangleArea = FMath::Abs(
						(Centers[1].X - Centers[0].X) * (Centers[2].Y - Centers[0].Y)
						- (Centers[1].Y - Centers[0].Y) * (Centers[2].X - Centers[0].X));
					TestTrue(TEXT("Tripod columns form a two-dimensional support triangle"), TwiceTriangleArea > 1.0f);
					if (Data.Bricks.IsValidIndex(Mapping.LoadPlateNodeId))
					{
						const FVector2D CompiledCentroid =
							(Centers[0] + Centers[1] + Centers[2]) / 3.0f;
						const FVector2D LoadPlateCenter(Data.Bricks[Mapping.LoadPlateNodeId].LocalCenter);
						TestTrue(
							TEXT("Compiled tripod contact centroid stays on the centered load plate"),
							FVector2D::Distance(CompiledCentroid, LoadPlateCenter) < 0.01f);
					}
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
	DAGSettings.BuildingSeed = 7301;
	DAGSettings.MaxExpansionDepth = 3;
	DAGSettings.ExpansionStepBudget = 1;
	DAGSettings.MaxAbstractNodeCount = 128;
	DAGSettings.MaxEstimatedBrickCount = 256;
	DAGSettings.ReservedWeaknessBrickCount = 0;
	FABTSM73DAGLayoutSettings LayoutSettings;
	// Give recursive Parallel splits enough XY space. TargetHeight deliberately
	// remains at its normal value: structural rank must resolve Z independently.
	LayoutSettings.TargetWidthCM = 460.0f;
	LayoutSettings.TargetDepthCM = 300.0f;
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
		TestTrue(TEXT("Every selected physical support rises to a higher structural rank"),
			Upper.StoreyIndex > Lower.StoreyIndex);
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
