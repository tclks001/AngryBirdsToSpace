// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** M7.3-DAG-1 seed topology. This is independent from the Legacy silhouette enum. */
enum class EABTSM73DAGPreset : uint8
{
	SingleTower,
	Arch,
	TwinTowerBridge
};

/** Operator stored in the derivation tree. */
enum class EABTSM73DAGOperator : uint8
{
	Atom,
	Series,
	Parallel
};

/** Physical meaning of a parallel expression. */
enum class EABTSM73DAGParallelPolicy : uint8
{
	AllRequired,
	AnySufficient,
	KOfN,
	Independent
};

/** Grammar rule that replaced an Atom. SeedTopology is used by authored preset nodes. */
enum class EABTSM73DAGRule : uint8
{
	None,
	SeedTopology,
	SerialSplit,
	ParallelSplit
};

/** Pure-data M7.3-DAG-1 generation settings. No Actor or World access is allowed here. */
struct FABTSM73DAGGenerationSettings
{
	int32 BuildingSeed = 7301;
	int32 GeneratorVersion = 1;
	EABTSM73DAGPreset Preset = EABTSM73DAGPreset::SingleTower;

	int32 MinExpansionDepth = 0;
	int32 MaxExpansionDepth = 3;
	int32 ExpansionStepBudget = 6;
	int32 MaxAbstractNodeCount = 64;
	int32 MaxEstimatedBrickCount = 50;
	int32 ReservedWeaknessBrickCount = 6;

	float SeriesRuleWeight = 0.55f;
	float ParallelRuleWeight = 0.45f;
	EABTSM73DAGParallelPolicy DefaultParallelPolicy = EABTSM73DAGParallelPolicy::AllRequired;
};

/** One node in the rule derivation tree. An Atom is a terminal structural region. */
struct FABTSM73DAGExpressionNode
{
	int32 NodeId = INDEX_NONE;
	int32 ParentNodeId = INDEX_NONE;
	EABTSM73DAGOperator Operator = EABTSM73DAGOperator::Atom;
	EABTSM73DAGParallelPolicy ParallelPolicy = EABTSM73DAGParallelPolicy::AllRequired;
	EABTSM73DAGRule AppliedRule = EABTSM73DAGRule::None;
	TArray<int32> ChildNodeIds;
	FString DerivationPath;
	int32 ExpansionDepth = 0;
};

/** Terminal structural region emitted by the expression compiler. It is not a brick Actor. */
struct FABTSM73DAGMacroNode
{
	int32 NodeId = INDEX_NONE;
	int32 SourceExpressionNodeId = INDEX_NONE;
	FString DerivationPath;
	int32 ExpansionDepth = 0;
};

/** Support direction is always SupportNodeId -> LoadNodeId (Ground to top load). */
struct FABTSM73DAGSupportEdge
{
	int32 SupportNodeId = INDEX_NONE;
	int32 LoadNodeId = INDEX_NONE;

	bool operator==(const FABTSM73DAGSupportEdge& Other) const
	{
		return SupportNodeId == Other.SupportNodeId && LoadNodeId == Other.LoadNodeId;
	}
};

/** Stable trace item used to explain and reproduce recursive expansion. */
struct FABTSM73DAGExpansionTrace
{
	FString ReplacedPath;
	EABTSM73DAGRule Rule = EABTSM73DAGRule::None;
	int32 NewDepth = 0;
	uint32 PathSeed = 0;
};

/** Complete pure-data result of DAG-1. */
struct FABTSM73DAGGenerationResult
{
	bool bAccepted = false;
	bool bBudgetTerminated = false;
	bool bStepLimitTerminated = false;
	bool bMinimumDepthSatisfied = true;
	int32 BuildingSeed = 0;
	int32 GeneratorVersion = 0;
	EABTSM73DAGPreset Preset = EABTSM73DAGPreset::SingleTower;
	int32 RootExpressionNodeId = INDEX_NONE;
	int32 ExpansionStepsApplied = 0;
	int32 InitialTerminalCount = 0;
	int32 EstimatedBrickCount = 0;
	uint32 CanonicalTopologyHash = 0;
	FString CanonicalExpression;
	FString DebugExpression;
	FString RejectReason;

	TArray<FABTSM73DAGExpressionNode> ExpressionNodes;
	TArray<FABTSM73DAGMacroNode> MacroNodes;
	TArray<FABTSM73DAGSupportEdge> SupportEdges;
	TArray<int32> GroundNodeIds;
	TArray<int32> TopLoadNodeIds;
	TArray<FABTSM73DAGExpansionTrace> ExpansionTrace;
};

