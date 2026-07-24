// Copyright Epic Games, Inc. All Rights Reserved.

#include "Slingshot/ABTSM6PhysicsSettleMonitor.h"

#include "Components/PrimitiveComponent.h"

void FABTSM6PhysicsSettleMonitor::Configure(const FABTSM6PhysicsSettleConfig& InConfig)
{
	Config.LinearSpeedThresholdCMPerSec = FMath::Max(0.0f, InConfig.LinearSpeedThresholdCMPerSec);
	Config.AngularSpeedThresholdDegPerSec = FMath::Max(0.0f, InConfig.AngularSpeedThresholdDegPerSec);
	Config.StableHoldSeconds = FMath::Max(0.0f, InConfig.StableHoldSeconds);
	Config.MinimumPostActivitySeconds = FMath::Max(0.0f, InConfig.MinimumPostActivitySeconds);
	Config.MaximumWaitSeconds = FMath::Max(0.1f, InConfig.MaximumWaitSeconds);
	Config.SampleIntervalSeconds = FMath::Max(0.01f, InConfig.SampleIntervalSeconds);
}

void FABTSM6PhysicsSettleMonitor::Reset(const float WorldTimeSeconds)
{
	LastSummary = FABTSM6PhysicsActivitySummary();
	LastActivityTimeSeconds = WorldTimeSeconds;
	SettlementStartTimeSeconds = WorldTimeSeconds;
	SampleAccumulatorSeconds = 0.0f;
	StableElapsedSeconds = 0.0f;
	bSettlementActive = false;
}

void FABTSM6PhysicsSettleMonitor::BeginSettlement(const float WorldTimeSeconds)
{
	SettlementStartTimeSeconds = WorldTimeSeconds;
	SampleAccumulatorSeconds = Config.SampleIntervalSeconds;
	StableElapsedSeconds = 0.0f;
	bSettlementActive = true;
	MarkActivity(WorldTimeSeconds);
}

void FABTSM6PhysicsSettleMonitor::MarkActivity(const float WorldTimeSeconds)
{
	// M7 exposes its latest activity timestamp and M6 polls it every sample.
	// Ignore an already-consumed timestamp or the stable window would reset forever.
	if (WorldTimeSeconds <= LastActivityTimeSeconds + UE_KINDA_SMALL_NUMBER) return;
	LastActivityTimeSeconds = WorldTimeSeconds;
	StableElapsedSeconds = 0.0f;
	LastSummary.StableElapsedSeconds = 0.0f;
}

void FABTSM6PhysicsSettleMonitor::MarkActivityAtLeast(const float WorldTimeSeconds)
{
	if (WorldTimeSeconds > LastActivityTimeSeconds + UE_KINDA_SMALL_NUMBER)
	{
		MarkActivity(WorldTimeSeconds);
	}
}

EABTSM6PhysicsSettleResult FABTSM6PhysicsSettleMonitor::Update(
	const float DeltaSeconds,
	const float WorldTimeSeconds,
	const TArray<UPrimitiveComponent*>& DynamicBodies,
	FABTSM6PhysicsActivitySummary& OutSummary)
{
	if (!bSettlementActive)
	{
		OutSummary = LastSummary;
		return EABTSM6PhysicsSettleResult::Monitoring;
	}

	LastSummary.SecondsSinceLastActivity = FMath::Max(0.0f, WorldTimeSeconds - LastActivityTimeSeconds);
	LastSummary.SettlementElapsedSeconds = FMath::Max(0.0f, WorldTimeSeconds - SettlementStartTimeSeconds);
	if (LastSummary.SettlementElapsedSeconds >= Config.MaximumWaitSeconds)
	{
		OutSummary = LastSummary;
		return EABTSM6PhysicsSettleResult::TimedOut;
	}

	SampleAccumulatorSeconds += FMath::Max(0.0f, DeltaSeconds);
	if (SampleAccumulatorSeconds < Config.SampleIntervalSeconds)
	{
		OutSummary = LastSummary;
		return EABTSM6PhysicsSettleResult::Monitoring;
	}

	const float SampleDuration = SampleAccumulatorSeconds;
	SampleAccumulatorSeconds = 0.0f;
	SampleBodies(DynamicBodies);
	if (LastSummary.MovingBodyCount == 0)
	{
		StableElapsedSeconds += SampleDuration;
	}
	else
	{
		StableElapsedSeconds = 0.0f;
	}
	LastSummary.StableElapsedSeconds = StableElapsedSeconds;
	LastSummary.SecondsSinceLastActivity = FMath::Max(0.0f, WorldTimeSeconds - LastActivityTimeSeconds);
	LastSummary.SettlementElapsedSeconds = FMath::Max(0.0f, WorldTimeSeconds - SettlementStartTimeSeconds);
	OutSummary = LastSummary;

	return StableElapsedSeconds >= Config.StableHoldSeconds
		&& LastSummary.SecondsSinceLastActivity >= Config.MinimumPostActivitySeconds
		? EABTSM6PhysicsSettleResult::Settled
		: EABTSM6PhysicsSettleResult::Monitoring;
}

void FABTSM6PhysicsSettleMonitor::SampleBodies(const TArray<UPrimitiveComponent*>& DynamicBodies)
{
	LastSummary.ActiveBodyCount = 0;
	LastSummary.MovingBodyCount = 0;
	LastSummary.AwakeBodyCount = 0;
	LastSummary.MaximumLinearSpeedCMPerSec = 0.0f;
	LastSummary.MaximumAngularSpeedDegPerSec = 0.0f;
	for (UPrimitiveComponent* Body : DynamicBodies)
	{
		if (!IsValid(Body) || !Body->IsRegistered() || !Body->IsSimulatingPhysics()) continue;
		++LastSummary.ActiveBodyCount;
		const float LinearSpeed = Body->GetPhysicsLinearVelocity().Size();
		const float AngularSpeed = Body->GetPhysicsAngularVelocityInDegrees().Size();
		LastSummary.MaximumLinearSpeedCMPerSec = FMath::Max(LastSummary.MaximumLinearSpeedCMPerSec, LinearSpeed);
		LastSummary.MaximumAngularSpeedDegPerSec = FMath::Max(LastSummary.MaximumAngularSpeedDegPerSec, AngularSpeed);
		if (Body->IsAnyRigidBodyAwake()) ++LastSummary.AwakeBodyCount;
		if (LinearSpeed > Config.LinearSpeedThresholdCMPerSec || AngularSpeed > Config.AngularSpeedThresholdDegPerSec)
		{
			++LastSummary.MovingBodyCount;
		}
	}
}
