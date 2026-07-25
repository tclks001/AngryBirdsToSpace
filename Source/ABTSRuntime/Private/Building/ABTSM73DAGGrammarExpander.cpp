// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAGGrammarExpander.h"

#include "Building/ABTSM73DAGValidator.h"
#include "Misc/Crc.h"

namespace
{
	constexpr uint32 ExpansionPrioritySalt = 0xD7311001u;
	constexpr uint32 RuleChoiceSalt = 0xD7311002u;

	int32 AddExpressionNode(
		FABTSM73DAGGenerationResult& Result,
		const int32 ParentNodeId,
		const EABTSM73DAGOperator Operator,
		const FString& Path,
		const int32 ExpansionDepth,
		const EABTSM73DAGParallelPolicy ParallelPolicy)
	{
		FABTSM73DAGExpressionNode& Node = Result.ExpressionNodes.AddDefaulted_GetRef();
		Node.NodeId = Result.ExpressionNodes.Num() - 1;
		Node.ParentNodeId = ParentNodeId;
		Node.Operator = Operator;
		Node.ParallelPolicy = ParallelPolicy;
		Node.AppliedRule = EABTSM73DAGRule::SeedTopology;
		Node.DerivationPath = Path;
		Node.ExpansionDepth = ExpansionDepth;
		if (Result.ExpressionNodes.IsValidIndex(ParentNodeId))
		{
			Result.ExpressionNodes[ParentNodeId].ChildNodeIds.Add(Node.NodeId);
		}
		return Node.NodeId;
	}

	void AddUniqueSorted(TArray<int32>& Target, const TArray<int32>& Source)
	{
		for (const int32 Value : Source) Target.AddUnique(Value);
		Target.Sort();
	}

	uint64 MakeEdgeKey(const int32 SupportNodeId, const int32 LoadNodeId)
	{
		return (static_cast<uint64>(static_cast<uint32>(SupportNodeId)) << 32)
			| static_cast<uint32>(LoadNodeId);
	}

	FString JoinStrings(const TArray<FString>& Values, const TCHAR* Separator)
	{
		FString Result;
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			if (Index > 0) Result += Separator;
			Result += Values[Index];
		}
		return Result;
	}

	int32 CountTerminalExpressions(const FABTSM73DAGGenerationResult& Result)
	{
		int32 Count = 0;
		for (const FABTSM73DAGExpressionNode& Node : Result.ExpressionNodes)
		{
			if (Node.Operator == EABTSM73DAGOperator::Atom) ++Count;
		}
		return Count;
	}
}

bool FABTSM73DAGGrammarExpander::Generate(
	const FABTSM73DAGGenerationSettings& Settings,
	FABTSM73DAGGenerationResult& OutResult,
	FString& OutError) const
{
	OutResult = FABTSM73DAGGenerationResult();
	OutError.Reset();
	OutResult.BuildingSeed = Settings.BuildingSeed;
	OutResult.GeneratorVersion = Settings.GeneratorVersion;
	OutResult.Preset = Settings.Preset;

	if (!ValidateSettings(Settings, OutError))
	{
		OutResult.RejectReason = OutError;
		return false;
	}

	BuildSeedExpression(Settings, OutResult);
	OutResult.InitialTerminalCount = CountTerminalExpressions(OutResult);
	const int32 InitialEstimatedBrickCount = OutResult.InitialTerminalCount + Settings.ReservedWeaknessBrickCount;
	if (OutResult.ExpressionNodes.Num() > Settings.MaxAbstractNodeCount
		|| InitialEstimatedBrickCount > Settings.MaxEstimatedBrickCount)
	{
		OutError = FString::Printf(TEXT("InitialBudgetExceeded:Expr=%d:%d Brick=%d:%d"),
			OutResult.ExpressionNodes.Num(), Settings.MaxAbstractNodeCount,
			InitialEstimatedBrickCount, Settings.MaxEstimatedBrickCount);
		OutResult.RejectReason = OutError;
		return false;
	}

	ExpandExpression(Settings, OutResult);
	if (!OutResult.bMinimumDepthSatisfied)
	{
		OutError = FString::Printf(TEXT("MinimumExpansionDepthUnsatisfied:%d:Steps=%d:Budget=%d"),
			Settings.MinExpansionDepth, OutResult.ExpansionStepsApplied, Settings.ExpansionStepBudget);
		OutResult.RejectReason = OutError;
		return false;
	}
	CompileSupportDAG(OutResult);
	OutResult.EstimatedBrickCount = OutResult.MacroNodes.Num() + Settings.ReservedWeaknessBrickCount;
	OutResult.CanonicalExpression = BuildCanonicalExpression(OutResult.RootExpressionNodeId, OutResult);
	OutResult.DebugExpression = BuildDebugExpression(OutResult.RootExpressionNodeId, OutResult);
	OutResult.CanonicalTopologyHash = FCrc::StrCrc32(*OutResult.CanonicalExpression);

	FABTSM73DAGValidator Validator;
	if (!Validator.Validate(Settings, OutResult, OutError))
	{
		OutResult.RejectReason = OutError;
		return false;
	}
	OutResult.bAccepted = true;
	return true;
}

bool FABTSM73DAGGrammarExpander::ValidateSettings(
	const FABTSM73DAGGenerationSettings& Settings,
	FString& OutError) const
{
	if (Settings.GeneratorVersion <= 0) { OutError = TEXT("GeneratorVersionInvalid"); return false; }
	if (Settings.MinExpansionDepth < 0 || Settings.MaxExpansionDepth < Settings.MinExpansionDepth)
	{
		OutError = TEXT("ExpansionDepthRangeInvalid");
		return false;
	}
	if (Settings.ExpansionStepBudget < 0 || Settings.MaxAbstractNodeCount < 1
		|| Settings.MaxEstimatedBrickCount < 1 || Settings.ReservedWeaknessBrickCount < 0)
	{
		OutError = TEXT("GenerationBudgetInvalid");
		return false;
	}
	if (Settings.SeriesRuleWeight < 0.0f || Settings.ParallelRuleWeight < 0.0f)
	{
		OutError = TEXT("RuleWeightNegative");
		return false;
	}
	if (Settings.ExpansionStepBudget > 0 && Settings.MaxExpansionDepth > 0
		&& Settings.SeriesRuleWeight + Settings.ParallelRuleWeight <= SMALL_NUMBER)
	{
		OutError = TEXT("NoEnabledExpansionRule");
		return false;
	}
	return true;
}

void FABTSM73DAGGrammarExpander::BuildSeedExpression(
	const FABTSM73DAGGenerationSettings& Settings,
	FABTSM73DAGGenerationResult& OutResult) const
{
	const EABTSM73DAGParallelPolicy Policy = Settings.DefaultParallelPolicy;
	switch (Settings.Preset)
	{
	case EABTSM73DAGPreset::SingleTower:
	{
		const int32 Root = AddExpressionNode(OutResult, INDEX_NONE, EABTSM73DAGOperator::Series, TEXT("R"), 0, Policy);
		OutResult.RootExpressionNodeId = Root;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			AddExpressionNode(OutResult, Root, EABTSM73DAGOperator::Atom,
				FString::Printf(TEXT("R/%d"), Index), 0, Policy);
		}
		break;
	}
	case EABTSM73DAGPreset::Arch:
	{
		const int32 Root = AddExpressionNode(OutResult, INDEX_NONE, EABTSM73DAGOperator::Series, TEXT("R"), 0, Policy);
		OutResult.RootExpressionNodeId = Root;
		AddExpressionNode(OutResult, Root, EABTSM73DAGOperator::Atom, TEXT("R/0"), 0, Policy);
		const int32 Parallel = AddExpressionNode(OutResult, Root, EABTSM73DAGOperator::Parallel, TEXT("R/1"), 0, Policy);
		AddExpressionNode(OutResult, Parallel, EABTSM73DAGOperator::Atom, TEXT("R/1/0"), 0, Policy);
		AddExpressionNode(OutResult, Parallel, EABTSM73DAGOperator::Atom, TEXT("R/1/1"), 0, Policy);
		break;
	}
	case EABTSM73DAGPreset::TwinTowerBridge:
	{
		const int32 Root = AddExpressionNode(OutResult, INDEX_NONE, EABTSM73DAGOperator::Series, TEXT("R"), 0, Policy);
		OutResult.RootExpressionNodeId = Root;
		const int32 UpperParallel = AddExpressionNode(OutResult, Root, EABTSM73DAGOperator::Parallel, TEXT("R/0"), 0, Policy);
		AddExpressionNode(OutResult, UpperParallel, EABTSM73DAGOperator::Atom, TEXT("R/0/0"), 0, Policy);
		AddExpressionNode(OutResult, UpperParallel, EABTSM73DAGOperator::Atom, TEXT("R/0/1"), 0, Policy);
		AddExpressionNode(OutResult, Root, EABTSM73DAGOperator::Atom, TEXT("R/1"), 0, Policy);
		const int32 LowerParallel = AddExpressionNode(OutResult, Root, EABTSM73DAGOperator::Parallel, TEXT("R/2"), 0, Policy);
		AddExpressionNode(OutResult, LowerParallel, EABTSM73DAGOperator::Atom, TEXT("R/2/0"), 0, Policy);
		AddExpressionNode(OutResult, LowerParallel, EABTSM73DAGOperator::Atom, TEXT("R/2/1"), 0, Policy);
		break;
	}
	}
}

void FABTSM73DAGGrammarExpander::ExpandExpression(
	const FABTSM73DAGGenerationSettings& Settings,
	FABTSM73DAGGenerationResult& OutResult) const
{
	struct FCandidate
	{
		int32 NodeId = INDEX_NONE;
		bool bRequiredForMinimumDepth = false;
		uint32 Priority = 0;
		FString Path;
	};

	while (OutResult.ExpansionStepsApplied < Settings.ExpansionStepBudget)
	{
		TArray<FCandidate> Candidates;
		for (const FABTSM73DAGExpressionNode& Node : OutResult.ExpressionNodes)
		{
			if (Node.Operator != EABTSM73DAGOperator::Atom || Node.ExpansionDepth >= Settings.MaxExpansionDepth) continue;
			FCandidate& Candidate = Candidates.AddDefaulted_GetRef();
			Candidate.NodeId = Node.NodeId;
			Candidate.bRequiredForMinimumDepth = Node.ExpansionDepth < Settings.MinExpansionDepth;
			Candidate.Priority = MakePathSeed(Settings, Node.DerivationPath, ExpansionPrioritySalt);
			Candidate.Path = Node.DerivationPath;
		}
		if (Candidates.IsEmpty()) break;
		Candidates.Sort([](const FCandidate& A, const FCandidate& B)
		{
			if (A.bRequiredForMinimumDepth != B.bRequiredForMinimumDepth)
				return A.bRequiredForMinimumDepth;
			if (A.Priority != B.Priority) return A.Priority < B.Priority;
			return A.Path < B.Path;
		});

		const int32 NextExpressionCount = OutResult.ExpressionNodes.Num() + 2;
		const int32 CurrentTerminalCount = CountTerminalExpressions(OutResult);
		const int32 NextEstimatedBrickCount = CurrentTerminalCount + 1 + Settings.ReservedWeaknessBrickCount;
		if (NextExpressionCount > Settings.MaxAbstractNodeCount
			|| NextEstimatedBrickCount > Settings.MaxEstimatedBrickCount)
		{
			OutResult.bBudgetTerminated = true;
			break;
		}

		const FCandidate Selected = Candidates[0];
		FABTSM73DAGExpressionNode& Node = OutResult.ExpressionNodes[Selected.NodeId];
		const uint32 RuleSeed = MakePathSeed(Settings, Node.DerivationPath, RuleChoiceSalt);
		const float TotalWeight = Settings.SeriesRuleWeight + Settings.ParallelRuleWeight;
		const float UnitChoice = static_cast<float>(static_cast<double>(RuleSeed) / static_cast<double>(MAX_uint32));
		const bool bUseSeries = UnitChoice * TotalWeight < Settings.SeriesRuleWeight;
		Node.Operator = bUseSeries ? EABTSM73DAGOperator::Series : EABTSM73DAGOperator::Parallel;
		Node.AppliedRule = bUseSeries ? EABTSM73DAGRule::SerialSplit : EABTSM73DAGRule::ParallelSplit;
		Node.ParallelPolicy = Settings.DefaultParallelPolicy;
		const int32 ChildDepth = Node.ExpansionDepth + 1;
		const FString ParentPath = Node.DerivationPath;
		const int32 ParentNodeId = Node.NodeId;
		const EABTSM73DAGRule AppliedRule = Node.AppliedRule;
		AddExpressionNode(OutResult, ParentNodeId, EABTSM73DAGOperator::Atom, ParentPath + TEXT("/0"), ChildDepth,
			Settings.DefaultParallelPolicy);
		AddExpressionNode(OutResult, ParentNodeId, EABTSM73DAGOperator::Atom, ParentPath + TEXT("/1"), ChildDepth,
			Settings.DefaultParallelPolicy);

		FABTSM73DAGExpansionTrace& Trace = OutResult.ExpansionTrace.AddDefaulted_GetRef();
		Trace.ReplacedPath = ParentPath;
		Trace.Rule = AppliedRule;
		Trace.NewDepth = ChildDepth;
		Trace.PathSeed = RuleSeed;
		++OutResult.ExpansionStepsApplied;
	}

	bool bHasExpandableNode = false;
	OutResult.bMinimumDepthSatisfied = true;
	for (const FABTSM73DAGExpressionNode& Node : OutResult.ExpressionNodes)
	{
		if (Node.Operator != EABTSM73DAGOperator::Atom) continue;
		if (Node.ExpansionDepth < Settings.MinExpansionDepth) OutResult.bMinimumDepthSatisfied = false;
		if (Node.ExpansionDepth < Settings.MaxExpansionDepth) bHasExpandableNode = true;
	}
	OutResult.bStepLimitTerminated = !OutResult.bBudgetTerminated
		&& OutResult.ExpansionStepsApplied >= Settings.ExpansionStepBudget
		&& bHasExpandableNode;
}

void FABTSM73DAGGrammarExpander::CompileSupportDAG(FABTSM73DAGGenerationResult& OutResult) const
{
	OutResult.MacroNodes.Reset();
	OutResult.SupportEdges.Reset();
	OutResult.GroundNodeIds.Reset();
	OutResult.TopLoadNodeIds.Reset();
	const FCompiledFrontier Frontier = CompileExpressionNode(OutResult.RootExpressionNodeId, OutResult);
	OutResult.TopLoadNodeIds = Frontier.TopNodeIds;
	OutResult.GroundNodeIds = Frontier.BottomNodeIds;
	OutResult.TopLoadNodeIds.Sort();
	OutResult.GroundNodeIds.Sort();
}

FABTSM73DAGGrammarExpander::FCompiledFrontier FABTSM73DAGGrammarExpander::CompileExpressionNode(
	const int32 ExpressionNodeId,
	FABTSM73DAGGenerationResult& OutResult) const
{
	const FABTSM73DAGExpressionNode& ExpressionNode = OutResult.ExpressionNodes[ExpressionNodeId];
	FCompiledFrontier Result;
	if (ExpressionNode.Operator == EABTSM73DAGOperator::Atom)
	{
		FABTSM73DAGMacroNode& MacroNode = OutResult.MacroNodes.AddDefaulted_GetRef();
		MacroNode.NodeId = OutResult.MacroNodes.Num() - 1;
		MacroNode.SourceExpressionNodeId = ExpressionNode.NodeId;
		MacroNode.DerivationPath = ExpressionNode.DerivationPath;
		MacroNode.ExpansionDepth = ExpressionNode.ExpansionDepth;
		Result.TopNodeIds.Add(MacroNode.NodeId);
		Result.BottomNodeIds.Add(MacroNode.NodeId);
		return Result;
	}

	TArray<FCompiledFrontier> ChildFrontiers;
	for (const int32 ChildId : ExpressionNode.ChildNodeIds)
	{
		ChildFrontiers.Add(CompileExpressionNode(ChildId, OutResult));
	}
	if (ExpressionNode.Operator == EABTSM73DAGOperator::Parallel)
	{
		for (const FCompiledFrontier& Child : ChildFrontiers)
		{
			AddUniqueSorted(Result.TopNodeIds, Child.TopNodeIds);
			AddUniqueSorted(Result.BottomNodeIds, Child.BottomNodeIds);
		}
		return Result;
	}

	Result.TopNodeIds = ChildFrontiers[0].TopNodeIds;
	Result.BottomNodeIds = ChildFrontiers.Last().BottomNodeIds;
	TSet<uint64> ExistingEdges;
	for (const FABTSM73DAGSupportEdge& Edge : OutResult.SupportEdges)
		ExistingEdges.Add(MakeEdgeKey(Edge.SupportNodeId, Edge.LoadNodeId));
	for (int32 ChildIndex = 0; ChildIndex + 1 < ChildFrontiers.Num(); ++ChildIndex)
	{
		const FCompiledFrontier& Upper = ChildFrontiers[ChildIndex];
		const FCompiledFrontier& Lower = ChildFrontiers[ChildIndex + 1];
		for (const int32 SupportNodeId : Lower.TopNodeIds)
		{
			for (const int32 LoadNodeId : Upper.BottomNodeIds)
			{
				const uint64 EdgeKey = MakeEdgeKey(SupportNodeId, LoadNodeId);
				if (ExistingEdges.Contains(EdgeKey)) continue;
				ExistingEdges.Add(EdgeKey);
				FABTSM73DAGSupportEdge& Edge = OutResult.SupportEdges.AddDefaulted_GetRef();
				Edge.SupportNodeId = SupportNodeId;
				Edge.LoadNodeId = LoadNodeId;
			}
		}
	}
	return Result;
}

FString FABTSM73DAGGrammarExpander::BuildCanonicalExpression(
	const int32 NodeId,
	const FABTSM73DAGGenerationResult& Result) const
{
	const FABTSM73DAGExpressionNode& Node = Result.ExpressionNodes[NodeId];
	if (Node.Operator == EABTSM73DAGOperator::Atom) return TEXT("N");
	TArray<FString> Terms;
	GatherCanonicalTerms(NodeId, Node.Operator, Node.ParallelPolicy, Result, Terms);
	if (Node.Operator == EABTSM73DAGOperator::Parallel) Terms.Sort();
	const TCHAR Prefix = Node.Operator == EABTSM73DAGOperator::Series ? TEXT('S') : TEXT('P');
	const FString Policy = Node.Operator == EABTSM73DAGOperator::Parallel
		? FString::Printf(TEXT("%d"), static_cast<int32>(Node.ParallelPolicy)) : FString();
	return FString::Printf(TEXT("%c%s(%s)"), Prefix, *Policy, *JoinStrings(Terms, TEXT(",")));
}

void FABTSM73DAGGrammarExpander::GatherCanonicalTerms(
	const int32 NodeId,
	const EABTSM73DAGOperator Operator,
	const EABTSM73DAGParallelPolicy ParallelPolicy,
	const FABTSM73DAGGenerationResult& Result,
	TArray<FString>& OutTerms) const
{
	const FABTSM73DAGExpressionNode& Node = Result.ExpressionNodes[NodeId];
	const bool bSameAssociativeOperator = Node.Operator == Operator
		&& (Operator != EABTSM73DAGOperator::Parallel || Node.ParallelPolicy == ParallelPolicy);
	if (!bSameAssociativeOperator)
	{
		OutTerms.Add(BuildCanonicalExpression(NodeId, Result));
		return;
	}
	for (const int32 ChildId : Node.ChildNodeIds)
	{
		const FABTSM73DAGExpressionNode& Child = Result.ExpressionNodes[ChildId];
		const bool bFlattenChild = Child.Operator == Operator
			&& (Operator != EABTSM73DAGOperator::Parallel || Child.ParallelPolicy == ParallelPolicy);
		if (bFlattenChild) GatherCanonicalTerms(ChildId, Operator, ParallelPolicy, Result, OutTerms);
		else OutTerms.Add(BuildCanonicalExpression(ChildId, Result));
	}
}

FString FABTSM73DAGGrammarExpander::BuildDebugExpression(
	const int32 NodeId,
	const FABTSM73DAGGenerationResult& Result) const
{
	const FABTSM73DAGExpressionNode& Node = Result.ExpressionNodes[NodeId];
	if (Node.Operator == EABTSM73DAGOperator::Atom)
		return FString::Printf(TEXT("N{%s}"), *Node.DerivationPath);
	TArray<FString> Children;
	for (const int32 ChildId : Node.ChildNodeIds) Children.Add(BuildDebugExpression(ChildId, Result));
	const TCHAR* Separator = Node.Operator == EABTSM73DAGOperator::Series ? TEXT("-") : TEXT("+");
	return TEXT("(") + JoinStrings(Children, Separator) + TEXT(")");
}

uint32 FABTSM73DAGGrammarExpander::MakePathSeed(
	const FABTSM73DAGGenerationSettings& Settings,
	const FString& Path,
	const uint32 Salt) const
{
	uint32 Hash = HashCombineFast(GetTypeHash(Settings.BuildingSeed), GetTypeHash(Settings.GeneratorVersion));
	Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(Settings.Preset)));
	Hash = HashCombineFast(Hash, GetTypeHash(Path));
	return HashCombineFast(Hash, Salt);
}
