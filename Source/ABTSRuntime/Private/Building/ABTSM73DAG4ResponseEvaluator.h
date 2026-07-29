// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/ABTSM73DAG4TrialPlanner.h"
#include "CoreMinimal.h"

struct FABTSM73DAG4NodeOutcome
{
	int32 NodeId = INDEX_NONE;
	FVector FinalDisplacementLocal = FVector::ZeroVector;
	float FinalRotationDegrees = 0.0f;
	float MaxDisplacementCM = 0.0f;
	float MaxRotationDegrees = 0.0f;
	float MaxDropDistanceCM = 0.0f;
	float MaxExpectedDirectionSlideCM = 0.0f;
};

struct FABTSM73DAG4TrialEvaluationInput
{
	TArray<FABTSM73DAG4SettledNode> Nodes;
	TArray<FABTSM73DAG4SettledContact> SettledContacts;
	FABTSM73DAG4TrialPlan Plan;
	TArray<FABTSM73DAG4NodeOutcome> Outcomes;
	TArray<int32> SecondaryContactNodePairs;
	EABTSM73DAGFailureMotion ExpectedMotion = EABTSM73DAGFailureMotion::None;
	FVector ExpectedFailureDirectionLocal = FVector::ZeroVector;
	float DurationSeconds = 0.0f;
	int32 TickCount = 0;
};

/** Pure reduction and final weak-vs-ordinary certification. */
class FABTSM73DAG4ResponseEvaluator
{
public:
	bool EvaluateTrial(
		const FABTSM73DAG4ValidationSettings& Settings,
		const FABTSM73DAG4TrialEvaluationInput& Input,
		FABTSM73DAG4TrialMetrics& OutMetrics,
		FString& OutError) const;

	bool CertifyComparison(
		const FABTSM73DAG4ValidationSettings& Settings,
		EABTSM73DAGFailureMotion ExpectedMotion,
		TConstArrayView<FABTSM73DAG4TrialMetrics> Trials,
		FABTSM73DAG4ValidationResult& InOutResult,
	FString& OutError) const;
};
