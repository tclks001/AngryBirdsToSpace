// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FABTSM73DAGGenerationResult;
struct FABTSM73DAGLayoutSettings;
struct FABTSM73DAGSpatialLayout;

/** DAG-2: assigns non-overlapping local Scopes and selects a sparse, geometrically feasible support graph. */
class FABTSM73DAGLayoutSolver
{
public:
	bool Solve(const FABTSM73DAGGenerationResult& Graph, const FABTSM73DAGLayoutSettings& Settings,
		FABTSM73DAGSpatialLayout& OutLayout, FString& OutError) const;

private:
	bool AssignExpressionScope(int32 ExpressionNodeId, const FBox& Scope,
		const FABTSM73DAGGenerationResult& Graph, const FABTSM73DAGLayoutSettings& Settings,
		const TMap<int32, int32>& MacroByExpression, FABTSM73DAGSpatialLayout& InOutLayout,
		FString& OutError) const;
	bool AssignStructuralLevels(const FABTSM73DAGGenerationResult& Graph,
		const FABTSM73DAGLayoutSettings& Settings, FABTSM73DAGSpatialLayout& InOutLayout,
		FString& OutError) const;
	bool SelectSparseSupports(const FABTSM73DAGGenerationResult& Graph,
		const FABTSM73DAGLayoutSettings& Settings, FABTSM73DAGSpatialLayout& InOutLayout,
		FString& OutError) const;
};
