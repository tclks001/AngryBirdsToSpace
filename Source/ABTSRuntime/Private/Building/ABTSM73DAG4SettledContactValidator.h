// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/ABTSM73DAG4Types.h"
#include "Building/ABTSM73DAGFailureFrontierTypes.h"
#include "Building/ABTSM73StructureData.h"
#include "CoreMinimal.h"

/** One runtime brick expressed in the settled building frame (Foundation top = Z 0). */
struct FABTSM73DAG4SettledNode
{
	int32 NodeId = INDEX_NONE;
	int32 MacroNodeId = INDEX_NONE;
	EABTSM7BuildingMaterial Material = EABTSM7BuildingMaterial::Wood;
	FVector DimensionsCM = FVector::ZeroVector;
	FTransform LocalTransform = FTransform::Identity;
	double Mass = 0.0;
	bool bMainBody = false;
};

/** One physical settled support patch reconstructed from oriented boxes. */
struct FABTSM73DAG4SettledContact
{
	int32 LowerNodeId = INDEX_NONE;
	int32 UpperNodeId = INDEX_NONE;
	float ContactAreaCM2 = 0.0f;
	float SignedGapCM = 0.0f;
	TArray<FVector2D> PatchVertices;
};

struct FABTSM73DAG4SettledContactInput
{
	TArray<FABTSM73DAG4SettledNode> Nodes;
	TArray<FABTSM73SupportEdge> BaselineAllowedContacts;
	TArray<FABTSM73SupportEdge> RequiredContacts;
	TArray<int32> BaselineGroundNodeIds;
	TArray<int32> WeakNodeIds;
	TArray<int32> RemainingSupportNodeIds;
	TArray<int32> ExpectedAffectedNodeIds;
	TArray<int32> ExpectedAffectedMainBodyNodeIds;
	int32 LoadPlateNodeId = INDEX_NONE;
	uint32 RealizedPatternHash = 0;
	EABTSM73DAGFailurePattern Pattern = EABTSM73DAGFailurePattern::Auto;
	EABTSM73DAGFailureMotion ExpectedMotion = EABTSM73DAGFailureMotion::None;
	FVector ExpectedFailureDirectionLocal = FVector::ZeroVector;
	float MinInitialSupportMarginCM = 0.0f;
	float MinPostFailureTipMarginCM = 0.0f;
	float MaxReseatRisk = 1.0f;
};

struct FABTSM73DAG4SettledContactResult
{
	TArray<FABTSM73DAG4SettledContact> Contacts;
	TArray<int32> GroundNodeIds;
	TArray<int32> MissingRequiredLowerNodeIds;
	TArray<int32> MissingRequiredUpperNodeIds;
	TArray<int32> NewContactLowerNodeIds;
	TArray<int32> NewContactUpperNodeIds;
	TArray<int32> FrontierBypassNodeIds;
	TArray<int32> DisconnectedAfterWeakNodeIds;
	uint32 BaselineContactHash = 0;
	uint32 SettledContactHash = 0;
	float InitialSupportMarginCM = 0.0f;
	float PostFailureTipMarginCM = 0.0f;
	float ReseatRisk = 1.0f;
	int32 PairQueryCount = 0;
	FString RejectReason;
};

/**
 * Pure bounded OBB contact rebuild and settled Failure Frontier revalidation.
 * It never reads World/Actor/Chaos state and never mutates the input snapshot.
 */
class FABTSM73DAG4SettledContactValidator
{
public:
	bool RebuildAndValidate(
		const FABTSM73DAG4ValidationSettings& Settings,
		const FABTSM73DAG4SettledContactInput& Input,
		FABTSM73DAG4SettledContactResult& OutResult,
		FString& OutError) const;
};
