// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/ABTSM73DAG4SettledContactValidator.h"
#include "CoreMinimal.h"

struct FABTSM73DAG4TrialPlan
{
	EABTSM73DAG4TrialKind Kind = EABTSM73DAG4TrialKind::Ordinary;
	int32 ProbeIndex = INDEX_NONE;
	TArray<int32> RemovedNodeIds;
	TArray<int32> PredictedAffectedMainBodyNodeIds;
	float PredictedAffectedMainBodyMassRatio = 0.0f;
};

struct FABTSM73DAG4TrialPlanningInput
{
	TArray<FABTSM73DAG4SettledNode> Nodes;
	TArray<FABTSM73DAG4SettledContact> Contacts;
	TArray<int32> GroundNodeIds;
	TArray<int32> WeakNodeIds;
	TArray<int32> RemainingSupportNodeIds;
	TArray<int32> ExpectedAffectedMainBodyNodeIds;
	FVector AttackDirectionLocal = FVector::ForwardVector;
	float ProjectileRadiusCM = 42.0f;
	float AttackApproachDistanceCM = 900.0f;
};

/** Pure deterministic weak/ordinary plan construction from one settled graph. */
class FABTSM73DAG4TrialPlanner
{
public:
	bool BuildPlans(
		const FABTSM73DAG4ValidationSettings& Settings,
		const FABTSM73DAG4TrialPlanningInput& Input,
		TArray<FABTSM73DAG4TrialPlan>& OutPlans,
		FString& OutError) const;
};
