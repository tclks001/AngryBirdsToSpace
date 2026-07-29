// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ABTSM73DAG4Types.generated.h"

/** One reversible DAG-4 removal trial. */
UENUM(BlueprintType)
enum class EABTSM73DAG4TrialKind : uint8
{
	WeakPoint,
	Ordinary
};

/**
 * DAG-4 settled-contact and reversible Chaos certification.
 *
 * Production defaults remain disabled. Enabling this requires a complete
 * DAG3-A/B/C candidate and extends the existing hidden IdleValidation gate.
 */
USTRUCT(BlueprintType)
struct FABTSM73DAG4ValidationSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Activation")
	bool bEnableSettledChaosValidation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Settled Contact",
		meta = (ClampMin = "0.05", ClampMax = "10.0", Units = "cm"))
	float ContactGapToleranceCM = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Settled Contact",
		meta = (ClampMin = "0.05", ClampMax = "10.0", Units = "cm"))
	float ContactPenetrationToleranceCM = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Settled Contact",
		meta = (ClampMin = "0.01", UIMax = "400.0"))
	float MinContactPatchAreaCM2 = 4.0f;

	/** Required settled patch area divided by its generated baseline area. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Settled Contact",
		meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float MinRequiredContactAreaRetention = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Comparison",
		meta = (ClampMin = "3", ClampMax = "8"))
	int32 NonWeakProbeCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Comparison",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxOrdinaryPredictedAffectedMassRatio = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Rollout",
		meta = (ClampMin = "0.25", ClampMax = "10.0", Units = "s"))
	float TrialDurationSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Rollout",
		meta = (ClampMin = "0.0", ClampMax = "1.0", Units = "s"))
	float TrialWarmupSeconds = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Response",
		meta = (ClampMin = "1.0", ClampMax = "300.0", Units = "cm"))
	float SignificantDisplacementCM = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Response",
		meta = (ClampMin = "0.5", ClampMax = "90.0", Units = "deg"))
	float SignificantRotationDegrees = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Response",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinWeakAffectedMassRatio = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Response",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxWeakAffectedMassRatio = 0.80f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Response",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinPredictedAffectedRealizationRatio = 0.50f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Response",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxOrdinaryAffectedMassRatio = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Response",
		meta = (ClampMin = "1.0", ClampMax = "5.0"))
	float MinWeakResponseAdvantage = 1.50f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Response",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinWeakAbsoluteAffectedMassAdvantage = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Response",
		meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float MinFailureDirectionAlignment = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Response",
		meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MinWeakResponseScore = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Secondary Contact",
		meta = (ClampMin = "0.0", ClampMax = "1000.0", Units = "cm/s"))
	float MinSecondaryContactSpeedCMPerSec = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Secondary Contact",
		meta = (ClampMin = "0.0", ClampMax = "1.0", Units = "s"))
	float SecondaryContactDebounceSeconds = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Budget",
		meta = (ClampMin = "4", ClampMax = "256"))
	int32 MaxSettledBodyCount = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Budget",
		meta = (ClampMin = "1", ClampMax = "65536"))
	int32 MaxContactPairQueryCount = 4096;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Budget",
		meta = (ClampMin = "4", ClampMax = "16"))
	int32 MaxTrialCount = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Budget",
		meta = (ClampMin = "1", ClampMax = "1800"))
	int32 MaxTrialTickCount = 720;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Budget",
		meta = (ClampMin = "1.0", ClampMax = "60.0", Units = "s"))
	float MaxTotalValidationSeconds = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG-4|Budget",
		meta = (ClampMin = "8", ClampMax = "4096"))
	int32 MaxContactEventCount = 256;
};

/** Dynamic structural response measured from one settled-state trial. */
USTRUCT(BlueprintType)
struct FABTSM73DAG4TrialMetrics
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Trial")
	EABTSM73DAG4TrialKind Kind = EABTSM73DAG4TrialKind::Ordinary;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Trial")
	int32 ProbeIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Trial")
	TArray<int32> RemovedNodeIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Trial")
	bool bCompleted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Trial")
	float DurationSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Trial")
	int32 TickCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Trial")
	float PredictedAffectedMainBodyMassRatio = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Trial")
	float AffectedMainBodyMassRatio = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Trial")
	float PredictedAffectedRealizationRatio = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Trial")
	float MaxDisplacementCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Trial")
	float MaxRotationDegrees = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Trial")
	float MaxDropDistanceCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Trial")
	float MaxExpectedDirectionSlideCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Trial")
	int32 PropagationDepth = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Trial")
	float DirectionAlignment = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Trial")
	int32 SecondaryContactCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Trial")
	float ResponseScore = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Trial")
	FString RejectReason;
};

/** Atomic result of settled contact audit and every weak/ordinary rollout. */
USTRUCT(BlueprintType)
struct FABTSM73DAG4ValidationResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Result")
	bool bEnabled = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Result")
	bool bSettledContactAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Result")
	bool bChaosComparisonAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Result")
	bool bAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Settled Contact")
	int32 SettledNodeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Settled Contact")
	int32 SettledContactCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Settled Contact")
	int32 MissingRequiredContactCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Settled Contact")
	int32 NewContactCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Settled Contact")
	int32 FrontierBypassCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Settled Contact")
	int64 BaselineContactHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Settled Contact")
	int64 SettledContactHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Settled Contact")
	float SettledInitialSupportMarginCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Settled Contact")
	float SettledPostFailureTipMarginCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Settled Contact")
	float SettledReseatRisk = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Rollout")
	TArray<FABTSM73DAG4TrialMetrics> Trials;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Rollout")
	int32 WeakTrialIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Rollout")
	float WeakResponseScore = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Rollout")
	float MaxOrdinaryResponseScore = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Rollout")
	float MaxOrdinaryAffectedMassRatio = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Rollout")
	float WeakResponseAdvantage = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Result")
	float TotalValidationSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Result")
	int64 ValidationHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DAG-4|Result")
	FString RejectReason;
};
