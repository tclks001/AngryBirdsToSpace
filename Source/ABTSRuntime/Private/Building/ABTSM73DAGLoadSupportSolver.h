// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FABTSM73DAGGenerationResult;
struct FABTSM73DAGFailureRewriteIntent;
struct FABTSM73DAGLayoutSettings;
struct FABTSM73DAGSelectedSupport;
struct FABTSM73DAGSpatialLayout;

/** DAG-2.3: chooses each load plate's support group from its accumulated gravitational resultant. */
class FABTSM73DAGLoadSupportSolver
{
public:
	bool Solve(const FABTSM73DAGGenerationResult& Graph, const FABTSM73DAGLayoutSettings& Settings,
		const TMap<int32, TArray<FABTSM73DAGSelectedSupport>>& CandidatesByLoad,
		FABTSM73DAGSpatialLayout& InOutLayout, FString& OutError,
		const FABTSM73DAGFailureRewriteIntent* RewriteIntent = nullptr) const;
};
