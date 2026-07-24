// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UPrimitiveComponent;

struct FABTSM6PhysicsSettleConfig
{
	float LinearSpeedThresholdCMPerSec = 20.0f;
	float AngularSpeedThresholdDegPerSec = 10.0f;
	float StableHoldSeconds = 2.0f;
	float MinimumPostActivitySeconds = 2.5f;
	float MaximumWaitSeconds = 15.0f;
	float SampleIntervalSeconds = 0.1f;
};

struct FABTSM6PhysicsActivitySummary
{
	int32 ActiveBodyCount = 0;
	int32 MovingBodyCount = 0;
	int32 AwakeBodyCount = 0;
	float MaximumLinearSpeedCMPerSec = 0.0f;
	float MaximumAngularSpeedDegPerSec = 0.0f;
	float StableElapsedSeconds = 0.0f;
	float SecondsSinceLastActivity = 0.0f;
	float SettlementElapsedSeconds = 0.0f;
};

enum class EABTSM6PhysicsSettleResult : uint8
{
	Monitoring,
	Settled,
	TimedOut
};

/** Samples all launch-time Chaos bodies and requires continuous low motion before return. */
class ABTSRUNTIME_API FABTSM6PhysicsSettleMonitor final
{
public:
	void Configure(const FABTSM6PhysicsSettleConfig& InConfig);
	void Reset(float WorldTimeSeconds);
	void BeginSettlement(float WorldTimeSeconds);
	void MarkActivity(float WorldTimeSeconds);
	void MarkActivityAtLeast(float WorldTimeSeconds);
	EABTSM6PhysicsSettleResult Update(
		float DeltaSeconds,
		float WorldTimeSeconds,
		const TArray<UPrimitiveComponent*>& DynamicBodies,
		FABTSM6PhysicsActivitySummary& OutSummary);

private:
	void SampleBodies(const TArray<UPrimitiveComponent*>& DynamicBodies);

	FABTSM6PhysicsSettleConfig Config;
	FABTSM6PhysicsActivitySummary LastSummary;
	float LastActivityTimeSeconds = 0.0f;
	float SettlementStartTimeSeconds = 0.0f;
	float SampleAccumulatorSeconds = 0.0f;
	float StableElapsedSeconds = 0.0f;
	bool bSettlementActive = false;
};
