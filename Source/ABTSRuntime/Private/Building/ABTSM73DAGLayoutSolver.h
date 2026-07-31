// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FABTSM73DAGGenerationResult;
struct FABTSM73DAGFailureRewriteIntent;
struct FABTSM73DAGLayoutSettings;
struct FABTSM73DAGSpatialLayout;
struct FABTSM73SemanticEnvelope;

/** DAG-2: assigns non-overlapping local Scopes and selects a sparse, geometrically feasible support graph. */
class FABTSM73DAGLayoutSolver
{
public:
	bool Solve(const FABTSM73DAGGenerationResult& Graph, const FABTSM73DAGLayoutSettings& Settings,
		FABTSM73DAGSpatialLayout& OutLayout, FString& OutError,
		const FABTSM73DAGFailureRewriteIntent* RewriteIntent = nullptr) const;

	/**
	 * DAG5-B entry point. Shape/WFC already owns each macro Scope, so this
	 * skips the legacy Series/Parallel rectangular subdivision while retaining
	 * the same DAG2.3 support/load and structural-level authority.
	 */
	bool SolveSemantic(
		const FABTSM73DAGGenerationResult& Graph,
		const FABTSM73DAGLayoutSettings& Settings,
		const FABTSM73DAGSpatialLayout& InitialLayout,
		const FABTSM73SemanticEnvelope& Envelope,
		FABTSM73DAGSpatialLayout& OutLayout,
		FString& OutError,
		const FABTSM73DAGFailureRewriteIntent* RewriteIntent = nullptr) const;

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
		FString& OutError, const FABTSM73DAGFailureRewriteIntent* RewriteIntent,
		const FABTSM73SemanticEnvelope* SemanticEnvelope = nullptr) const;
};
