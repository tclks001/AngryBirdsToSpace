// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ABTSM73DAGTypes.generated.h"

/** M7.3-DAG-1 seed topology. This is independent from the Legacy silhouette enum. */
UENUM(BlueprintType)
enum class EABTSM73DAGPreset : uint8
{
	SingleTower,
	Arch,
	TwinTowerBridge
};

/** Operator stored in the derivation tree. */
UENUM()
enum class EABTSM73DAGOperator : uint8
{
	Atom,
	Series,
	Parallel
};

/** Physical meaning of a parallel expression. */
UENUM(BlueprintType)
enum class EABTSM73DAGParallelPolicy : uint8
{
	AllRequired,
	AnySufficient,
	KOfN,
	Independent
};

/** Grammar rule that replaced an Atom. SeedTopology is used by authored preset nodes. */
UENUM()
enum class EABTSM73DAGRule : uint8
{
	None,
	SeedTopology,
	SerialSplit,
	ParallelSplit
};

/** Pure-data M7.3-DAG generation settings. No Actor or World access is allowed here. */
USTRUCT(BlueprintType)
struct FABTSM73DAGGenerationSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-1", meta = (ClampMin = "1"))
	int32 BuildingSeed = 7301;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-1", meta = (ClampMin = "1"))
	int32 GeneratorVersion = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-1")
	EABTSM73DAGPreset Preset = EABTSM73DAGPreset::SingleTower;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-1", meta = (ClampMin = "0", ClampMax = "6"))
	int32 MinExpansionDepth = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-1", meta = (ClampMin = "0", ClampMax = "6"))
	int32 MaxExpansionDepth = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-1", meta = (ClampMin = "0", ClampMax = "32"))
	int32 ExpansionStepBudget = 6;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-1", meta = (ClampMin = "1", ClampMax = "256"))
	int32 MaxAbstractNodeCount = 64;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-1", meta = (ClampMin = "1", ClampMax = "256"))
	int32 MaxEstimatedBrickCount = 50;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-1", meta = (ClampMin = "0", ClampMax = "64"))
	int32 ReservedWeaknessBrickCount = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-1", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SeriesRuleWeight = 0.55f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-1", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ParallelRuleWeight = 0.45f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-1")
	EABTSM73DAGParallelPolicy DefaultParallelPolicy = EABTSM73DAGParallelPolicy::AllRequired;
};

/** Pure-data M7.3-DAG-2 lowering settings: Scope split, sparse columns and plate compilation. */
USTRUCT(BlueprintType)
struct FABTSM73DAGLayoutSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-2|Scope", meta = (ClampMin = "160.0", UIMax = "1600.0"))
	float TargetWidthCM = 460.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-2|Scope", meta = (ClampMin = "160.0", UIMax = "1600.0"))
	float TargetDepthCM = 300.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-2|Scope", meta = (ClampMin = "160.0", UIMax = "2400.0"))
	float TargetHeightCM = 760.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-2|Scope", meta = (ClampMin = "0.0", UIMax = "160.0"))
	float ParallelGapCM = 28.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-2|Scope", meta = (ClampMin = "0.0", UIMax = "160.0"))
	float SeriesGapCM = 20.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-2|Scope")
	bool bAlternateParallelAxes = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-2|Plates", meta = (ClampMin = "20.0", UIMax = "160.0"))
	float PlateThicknessCM = 58.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-2|Plates", meta = (ClampMin = "40.0", UIMax = "400.0"))
	float MinPlateExtentCM = 90.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-2|Plates", meta = (ClampMin = "0.50", ClampMax = "1.0"))
	float PlateFootprintRatio = 0.92f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-2|Columns", meta = (ClampMin = "20.0", UIMax = "180.0"))
	float ColumnWidthCM = 88.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-2|Columns", meta = (ClampMin = "10.0", UIMax = "300.0"))
	float MinColumnHeightCM = 30.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-2|Columns", meta = (ClampMin = "0.0", UIMax = "30.0"))
	float ColumnClearanceCM = 3.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-2|Columns", meta = (ClampMin = "1", ClampMax = "2"))
	int32 ColumnsPerSelectedSupport = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-2|Sparse Support", meta = (ClampMin = "1", ClampMax = "4"))
	int32 PreferredLogicalSupportsPerLoad = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-2|Sparse Support", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxLogicalSupportsPerLoad = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-2|Validation", meta = (ClampMin = "0.01", UIMax = "10.0"))
	float ContactToleranceCM = 0.5f;
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

/** Axis-aligned local Scope assigned to a terminal macro node by the DAG-2 layout solver. */
struct FABTSM73DAGMacroLayout
{
	int32 MacroNodeId = INDEX_NONE;
	FBox AllowedScope = FBox(EForceInit::ForceInit);
	FVector PlateCenter = FVector::ZeroVector;
	FVector PlateDimensionsCM = FVector::ZeroVector;
	int32 StructuralLevel = INDEX_NONE;
	bool bGroundTerminal = false;
};

/** One sparse logical DAG support selected for lowering into one or more physical columns. */
struct FABTSM73DAGSelectedSupport
{
	int32 SupportMacroNodeId = INDEX_NONE;
	int32 LoadMacroNodeId = INDEX_NONE;
	FBox2D FeasibleColumnRegion;
	float Cost = 0.0f;
};

/** Pure-data output of the DAG-2 Scope layout and sparse-support solve. */
struct FABTSM73DAGSpatialLayout
{
	bool bAccepted = false;
	TArray<FABTSM73DAGMacroLayout> MacroLayouts;
	TArray<FABTSM73DAGSelectedSupport> SelectedSupports;
	int32 RejectedCandidateEdgeCount = 0;
	FString RejectReason;
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
