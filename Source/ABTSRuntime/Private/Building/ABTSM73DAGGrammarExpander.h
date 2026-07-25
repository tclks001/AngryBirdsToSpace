// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Building/ABTSM73DAGTypes.h"

/** Pure-data M7.3-DAG-1 grammar expansion and abstract support-DAG compilation. */
class FABTSM73DAGGrammarExpander
{
public:
	bool Generate(
		const FABTSM73DAGGenerationSettings& Settings,
		FABTSM73DAGGenerationResult& OutResult,
		FString& OutError) const;

private:
	struct FCompiledFrontier
	{
		TArray<int32> TopNodeIds;
		TArray<int32> BottomNodeIds;
	};

	bool ValidateSettings(const FABTSM73DAGGenerationSettings& Settings, FString& OutError) const;
	void BuildSeedExpression(const FABTSM73DAGGenerationSettings& Settings, FABTSM73DAGGenerationResult& OutResult) const;
	void ExpandExpression(const FABTSM73DAGGenerationSettings& Settings, FABTSM73DAGGenerationResult& OutResult) const;
	void CompileSupportDAG(FABTSM73DAGGenerationResult& OutResult) const;
	FCompiledFrontier CompileExpressionNode(int32 ExpressionNodeId, FABTSM73DAGGenerationResult& OutResult) const;
	FString BuildCanonicalExpression(int32 NodeId, const FABTSM73DAGGenerationResult& Result) const;
	FString BuildDebugExpression(int32 NodeId, const FABTSM73DAGGenerationResult& Result) const;
	void GatherCanonicalTerms(
		int32 NodeId,
		EABTSM73DAGOperator Operator,
		EABTSM73DAGParallelPolicy ParallelPolicy,
		const FABTSM73DAGGenerationResult& Result,
		TArray<FString>& OutTerms) const;
	uint32 MakePathSeed(
		const FABTSM73DAGGenerationSettings& Settings,
		const FString& Path,
		uint32 Salt) const;
};

