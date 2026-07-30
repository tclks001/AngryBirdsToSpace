// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ABTSM73DAG5Types.generated.h"

/** Stable stage classification for one DAG5-A candidate attempt. */
UENUM(BlueprintType)
enum class EABTSM73DAG5ARejectStage : uint8
{
	None,
	Settings,
	Capacity,
	Grammar,
	ScopeCapacity,
	Pipeline,
	CompiledBrickBudget,
	StaticStability
};

/** Explicitly opt-in DAG5-A feasibility search. Defaults preserve the existing one-shot DAG2.3 path. */
USTRUCT(BlueprintType)
struct FABTSM73DAG5ASettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG5-A")
	bool bEnableFeasibilitySearch = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG5-A", meta = (ClampMin = "1", ClampMax = "64"))
	int32 SearchVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG5-A", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxCandidateAttempts = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG5-A")
	bool bEnableCapacityPreflight = true;

	/**
	 * Final physical BrickNode limit. Zero delegates to GenerationSettings.MaxBrickCount.
	 * This is checked after compilation and never removes supports to make a candidate fit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG5-A", meta = (ClampMin = "0", ClampMax = "256"))
	int32 MaxCompiledBrickCount = 0;
};

/** Deterministic trace for one bounded DAG5-A candidate. */
USTRUCT(BlueprintType)
struct FABTSM73DAG5AAttemptResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 AttemptIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 CandidateSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int64 TopologyHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 CompiledBrickCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	bool bAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	EABTSM73DAG5ARejectStage RejectStage = EABTSM73DAG5ARejectStage::None;

	/** Stable prefix before the first ':' in RejectReason. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	FString RejectCode;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	FString RejectReason;
};

/** Complete evidence for one DAG5-A search transaction. */
USTRUCT(BlueprintType)
struct FABTSM73DAG5AResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	bool bEnabled = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	bool bCapacityPreflightPassed = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	bool bAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 InputSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 AttemptCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 ScopePreflightRejectCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 CompiledCandidateCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 SelectedAttemptIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 SelectedCandidateSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 EffectiveCompiledBrickLimit = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 CompiledBrickCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 RequiredMinimumExpansionSteps = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int32 RequiredMinimumTerminalCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	int64 SearchHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	TArray<FABTSM73DAG5AAttemptResult> Attempts;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG5-A")
	FString RejectReason;
};
