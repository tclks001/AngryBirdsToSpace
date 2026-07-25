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

#endif
