// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSRuntime.h"
#include "Building/ABTSM73DAGGrammarExpander.h"
#include "Building/ABTSM73DAGTypes.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FSeedTopologyCase
	{
		EABTSM73DAGPreset Preset;
		const TCHAR* ExpectedCanonicalExpression;
		int32 ExpectedMacroNodes;
		int32 ExpectedSupportEdges;
		int32 ExpectedGroundNodes;
		int32 ExpectedTopNodes;
	};

	bool EqualExpressionNodes(
		const FABTSM73DAGExpressionNode& A,
		const FABTSM73DAGExpressionNode& B)
	{
		return A.NodeId == B.NodeId
			&& A.ParentNodeId == B.ParentNodeId
			&& A.Operator == B.Operator
			&& A.ParallelPolicy == B.ParallelPolicy
			&& A.AppliedRule == B.AppliedRule
			&& A.ChildNodeIds == B.ChildNodeIds
			&& A.DerivationPath == B.DerivationPath
			&& A.ExpansionDepth == B.ExpansionDepth;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAGExpressionSemanticsTest,
	"ABTS.M73DAG.ExpressionSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAGExpressionSemanticsTest::RunTest(const FString& Parameters)
{
	const FSeedTopologyCase Cases[] = {
		{EABTSM73DAGPreset::SingleTower, TEXT("S(N,N,N,N)"), 4, 3, 1, 1},
		{EABTSM73DAGPreset::Arch, TEXT("S(N,P0(N,N))"), 3, 2, 2, 1},
		{EABTSM73DAGPreset::TwinTowerBridge, TEXT("S(P0(N,N),N,P0(N,N))"), 5, 4, 2, 2}
	};
	FABTSM73DAGGrammarExpander Expander;
	TSet<uint32> TopologyHashes;
	for (const FSeedTopologyCase& Case : Cases)
	{
		FABTSM73DAGGenerationSettings Settings;
		Settings.Preset = Case.Preset;
		Settings.MinExpansionDepth = 0;
		Settings.MaxExpansionDepth = 0;
		Settings.ExpansionStepBudget = 0;
		FABTSM73DAGGenerationResult Result;
		FString Error;
		const bool bGenerated = Expander.Generate(Settings, Result, Error);
		TestTrue(FString::Printf(TEXT("Preset %d generates: %s"), static_cast<int32>(Case.Preset), *Error), bGenerated);
		if (!bGenerated) continue;
		TestEqual(TEXT("Canonical seed expression"), Result.CanonicalExpression, FString(Case.ExpectedCanonicalExpression));
		TestEqual(TEXT("Seed terminal count"), Result.InitialTerminalCount, Case.ExpectedMacroNodes);
		TestEqual(TEXT("Macro node count"), Result.MacroNodes.Num(), Case.ExpectedMacroNodes);
		TestEqual(TEXT("Support edge count"), Result.SupportEdges.Num(), Case.ExpectedSupportEdges);
		TestEqual(TEXT("Ground frontier count"), Result.GroundNodeIds.Num(), Case.ExpectedGroundNodes);
		TestEqual(TEXT("Top-load frontier count"), Result.TopLoadNodeIds.Num(), Case.ExpectedTopNodes);
		TestTrue(TEXT("Seed expression has no recursive trace"), Result.ExpansionTrace.IsEmpty());
		TopologyHashes.Add(Result.CanonicalTopologyHash);
	}
	TestEqual(TEXT("The three seed topologies have distinct canonical hashes"), TopologyHashes.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAGRecursiveExpansionDeterminismTest,
	"ABTS.M73DAG.RecursiveExpansionDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAGRecursiveExpansionDeterminismTest::RunTest(const FString& Parameters)
{
	FABTSM73DAGGenerationSettings Settings;
	Settings.Preset = EABTSM73DAGPreset::Arch;
	Settings.BuildingSeed = 731011;
	Settings.GeneratorVersion = 1;
	Settings.MinExpansionDepth = 1;
	Settings.MaxExpansionDepth = 3;
	Settings.ExpansionStepBudget = 10;
	Settings.SeriesRuleWeight = 0.40f;
	Settings.ParallelRuleWeight = 0.60f;

	FABTSM73DAGGrammarExpander Expander;
	FABTSM73DAGGenerationResult First;
	FABTSM73DAGGenerationResult Second;
	FString FirstError;
	FString SecondError;
	TestTrue(TEXT("First recursive generation succeeds"), Expander.Generate(Settings, First, FirstError));
	TestTrue(TEXT("Second recursive generation succeeds"), Expander.Generate(Settings, Second, SecondError));
	TestEqual(TEXT("Canonical expression is deterministic"), First.CanonicalExpression, Second.CanonicalExpression);
	TestEqual(TEXT("Canonical topology hash is deterministic"), First.CanonicalTopologyHash, Second.CanonicalTopologyHash);
	TestEqual(TEXT("Expression node count is deterministic"), First.ExpressionNodes.Num(), Second.ExpressionNodes.Num());
	TestEqual(TEXT("Macro node count is deterministic"), First.MacroNodes.Num(), Second.MacroNodes.Num());
	TestEqual(TEXT("Support edge count is deterministic"), First.SupportEdges.Num(), Second.SupportEdges.Num());
	TestEqual(TEXT("Expansion trace count is deterministic"), First.ExpansionTrace.Num(), Second.ExpansionTrace.Num());
	for (int32 Index = 0; Index < First.ExpressionNodes.Num() && Second.ExpressionNodes.IsValidIndex(Index); ++Index)
	{
		TestTrue(TEXT("Expression node data is deterministic"), EqualExpressionNodes(First.ExpressionNodes[Index], Second.ExpressionNodes[Index]));
	}
	for (int32 Index = 0; Index < First.ExpansionTrace.Num() && Second.ExpansionTrace.IsValidIndex(Index); ++Index)
	{
		const FABTSM73DAGExpansionTrace& A = First.ExpansionTrace[Index];
		const FABTSM73DAGExpansionTrace& B = Second.ExpansionTrace[Index];
		TestEqual(TEXT("Trace path is deterministic"), A.ReplacedPath, B.ReplacedPath);
		TestEqual(TEXT("Trace rule is deterministic"), A.Rule, B.Rule);
		TestEqual(TEXT("Trace path seed is deterministic"), A.PathSeed, B.PathSeed);
	}
	TestTrue(TEXT("Minimum expansion depth is satisfied"), First.bMinimumDepthSatisfied);
	TestEqual(TEXT("Requested expansion steps are applied"), First.ExpansionStepsApplied, Settings.ExpansionStepBudget);

	FABTSM73DAGGenerationSettings PrefixSettings = Settings;
	PrefixSettings.ExpansionStepBudget = 6;
	FABTSM73DAGGenerationResult Prefix;
	FString PrefixError;
	TestTrue(TEXT("Shorter expansion succeeds"), Expander.Generate(PrefixSettings, Prefix, PrefixError));
	TestEqual(TEXT("Shorter generation applies its full step budget"), Prefix.ExpansionTrace.Num(), PrefixSettings.ExpansionStepBudget);
	for (int32 Index = 0; Index < Prefix.ExpansionTrace.Num() && First.ExpansionTrace.IsValidIndex(Index); ++Index)
	{
		TestEqual(TEXT("Increasing the step budget preserves prior path choices"),
			Prefix.ExpansionTrace[Index].ReplacedPath, First.ExpansionTrace[Index].ReplacedPath);
		TestEqual(TEXT("Increasing the step budget preserves prior rule choices"),
			Prefix.ExpansionTrace[Index].Rule, First.ExpansionTrace[Index].Rule);
	}

	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7.3-DAG-1][Accepted] Seed=%d Version=%d Preset=%d Expr=%d Macro=%d Edges=%d Steps=%d Hash=%u Canonical=%s"),
		Settings.BuildingSeed, Settings.GeneratorVersion, static_cast<int32>(Settings.Preset),
		First.ExpressionNodes.Num(), First.MacroNodes.Num(), First.SupportEdges.Num(),
		First.ExpansionStepsApplied, First.CanonicalTopologyHash, *First.CanonicalExpression);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAGBudgetTerminationTest,
	"ABTS.M73DAG.BudgetTermination",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAGBudgetTerminationTest::RunTest(const FString& Parameters)
{
	FABTSM73DAGGrammarExpander Expander;
	FABTSM73DAGGenerationSettings Settings;
	Settings.Preset = EABTSM73DAGPreset::SingleTower;
	Settings.MinExpansionDepth = 0;
	Settings.MaxExpansionDepth = 5;
	Settings.ExpansionStepBudget = 20;
	Settings.ReservedWeaknessBrickCount = 2;
	Settings.MaxEstimatedBrickCount = 7;
	FABTSM73DAGGenerationResult Result;
	FString Error;
	TestTrue(FString::Printf(TEXT("Budget-limited generation succeeds: %s"), *Error), Expander.Generate(Settings, Result, Error));
	TestTrue(TEXT("Generation records budget termination"), Result.bBudgetTerminated);
	TestFalse(TEXT("Budget termination is not confused with the step limit"), Result.bStepLimitTerminated);
	TestEqual(TEXT("Exactly one split fits the brick budget"), Result.ExpansionStepsApplied, 1);
	TestEqual(TEXT("Budget produces five terminal macro nodes"), Result.MacroNodes.Num(), 5);
	TestEqual(TEXT("Reserved weakness capacity is included in the estimate"), Result.EstimatedBrickCount, 7);
	TestTrue(TEXT("Budget is respected"), Result.EstimatedBrickCount <= Settings.MaxEstimatedBrickCount);

	FABTSM73DAGGenerationSettings ImpossibleMinimum = Settings;
	ImpossibleMinimum.MinExpansionDepth = 1;
	ImpossibleMinimum.ExpansionStepBudget = 1;
	ImpossibleMinimum.MaxEstimatedBrickCount = 50;
	FABTSM73DAGGenerationResult ImpossibleResult;
	FString ImpossibleError;
	TestFalse(TEXT("An unsatisfied requested minimum depth is rejected"),
		Expander.Generate(ImpossibleMinimum, ImpossibleResult, ImpossibleError));
	TestTrue(TEXT("Minimum-depth rejection is explicit"),
		ImpossibleError.StartsWith(TEXT("MinimumExpansionDepthUnsatisfied")));
	return true;
}

#endif

