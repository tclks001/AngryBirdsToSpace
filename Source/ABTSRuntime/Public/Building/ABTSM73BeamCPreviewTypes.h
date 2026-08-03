// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73BeamBPreviewTypes.h"
#include "ABTSM73BeamCPreviewTypes.generated.h"

USTRUCT(BlueprintType)
struct FABTSM73BeamCPreviewSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Beam-B")
	FABTSM73BeamBPreviewSettings BeamB;

	/** Relative self weight used before Beam-D selects material and real bricks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Load",
		meta = (ClampMin = "0.001", ClampMax = "10.0"))
	float MemberLinearDensityKGPerCM = 0.10f;

	/** Minimum explicit bearing area divided by the square block section area. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Load",
		meta = (ClampMin = "0.0001", ClampMax = "1.0"))
	float MinimumBearingAreaRatio = 0.001f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Proxy",
		meta = (ClampMin = "1.0", ClampMax = "1000000.0"))
	float ReferenceLoadKG = 10000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Proxy",
		meta = (ClampMin = "10.0", ClampMax = "10000.0", Units = "cm"))
	float ReferenceSpanCM = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Proxy",
		meta = (ClampMin = "0.01", ClampMax = "1000.0"))
	float SpanStiffnessScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Proxy",
		meta = (ClampMin = "10.0", ClampMax = "10000.0", Units = "cm"))
	float MaximumUnsupportedSpanCM = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Proxy",
		meta = (ClampMin = "0.01", ClampMax = "1000.0"))
	float MaximumSpanUtilization = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Proxy",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaximumCantileverRatio = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Proxy",
		meta = (ClampMin = "1.0", ClampMax = "1000.0"))
	float MaximumColumnSlenderness = 100.0f;

	/** Require both horizontal member families when the structure has Z posts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Proxy")
	bool bRequireBidirectionalLateralTies = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budget",
		meta = (ClampMin = "32", ClampMax = "65536"))
	int32 MaximumLoadNodeCount = 32768;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budget",
		meta = (ClampMin = "32", ClampMax = "131072"))
	int32 MaximumLoadEdgeCount = 65536;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budget",
		meta = (ClampMin = "128", ClampMax = "1048576"))
	int32 MaximumTopologyOperationCount = 262144;
};

USTRUCT(BlueprintType)
struct FABTSM73BeamCLoadNode
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 MemberId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	EABTSM73BeamAFrameAxis Axis = EABTSM73BeamAFrameAxis::X;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	bool bGround = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	bool bGroundReachable = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Load")
	float SelfLoadKG = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Load")
	float AccumulatedLoadKG = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Load")
	FVector LoadResultant = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Load")
	int32 SupportCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Static Proxy")
	float EffectiveSpanCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Static Proxy")
	float CantileverRatio = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Static Proxy")
	float SpanUtilization = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Static Proxy")
	float ColumnSlenderness = 0.0f;
};

USTRUCT(BlueprintType)
struct FABTSM73BeamCLoadEdge
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 EdgeId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 BearingContactId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	int32 UpperMemberId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	int32 LowerMemberId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geometry")
	FVector ContactPosition = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geometry")
	float ContactAreaCM2 = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Load")
	float LoadShare = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Load")
	float ReactionLoadKG = 0.0f;
};

USTRUCT(BlueprintType)
struct FABTSM73BeamCPreviewSummary
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	bool bAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 LoadNodeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 LoadEdgeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 GroundNodeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int32 CycleNodeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int32 GroundUnreachableNodeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int32 BearingAreaViolationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int32 ReactionBalanceViolationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int32 SpanViolationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int32 CantileverViolationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int32 ColumnSlendernessViolationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int32 LateralMechanismViolationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	float TotalSelfLoadKG = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	float TotalGroundReactionKG = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	float MaximumObservedSpanUtilization = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	float MaximumObservedColumnSlenderness = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int64 LoadDAGHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	FString RejectReason;
};
