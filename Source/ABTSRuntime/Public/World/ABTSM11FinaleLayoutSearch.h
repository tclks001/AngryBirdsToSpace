// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "World/ABTSM11FinaleLayoutTypes.h"

struct ABTSRUNTIME_API FABTSM11FinaleSearchConfig
{
	int32 SearchConfigVersion = 1;
	int32 SearchSeed = 110031;
	int32 BeamWidth = 12;
	int32 RobustPreselectionWidth = 64;
	/** Nominal and at least three of its six final-precision neighbors survive. */
	int32 MinimumRobustSurvivorCount = 4;
	double FirstAssistMinimumTimeSeconds = 45.0;
	double FirstAssistMaximumTimeSeconds = 320.0;
	double LaterLegMinimumTimeSeconds = 60.0;
	double LaterLegMaximumTimeSeconds = 240.0;
	double ArcSampleIntervalSeconds = 12.0;
	TArray<double> ImpactFractions = {0.38, 0.44, 0.50};
	TArray<double> RadialImpactFractions = {-0.15, 0.0, 0.15};
	double VirtualMomentumSpeedCMPerSec = 650.0;
	double TargetFlightTimeSeconds = 40.0;
	double LowPowerGate = 0.90;
	double LowPowerClearanceCM = 3000.0;
	double Assist12MinimumCenterDistanceCM = 50000.0;
	double Assist23MinimumCenterDistanceCM = 68000.0;
	double RobustMinimumCorridorQuality = 0.05;
	double RobustMinimumEnergyGainCM2PerSec2 = 1000.0;

	bool IsValid(FString* OutFailure = nullptr) const;
};

struct ABTSRUNTIME_API FABTSM11FinaleSearchReport
{
	int32 CandidateSolveCount = 0;
	int32 RejectedGeometryCount = 0;
	int32 RejectedEncounterCount = 0;
	int32 CompletedAssistCount = 0;
	FABTSM11FinaleLaunchInput NominalInput;
	TStaticArray<double, FABTSM11GravityScenario::AssistCount> AssistExitTimes;
	uint64 NominalTrajectoryHash = 0;
	uint64 SearchOutputHash = 0;
	FString Failure;

	FABTSM11FinaleSearchReport();
};

/**
 * Deterministic offline forward constructor.
 *
 * It never runs in PIE. Each candidate is replayed through M11-A; geometric
 * arc samples merely choose the next body's local center.
 */
class ABTSRUNTIME_API FABTSM11FinaleLayoutSearch final
{
public:
	static bool BuildConstructiveSeed(
		const FABTSM11FinaleLayoutPreset& SeedPreset,
		const FABTSM11FinaleSearchConfig& Config,
		FABTSM11FinaleLayoutPreset& OutPreset,
		FABTSM11FinaleSearchReport& OutReport,
		FString* OutFailure = nullptr);
};
