// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FABTSM11TrajectoryResult;

/** Event-derived narrative stage shared by M1 observation and M2+ direction. */
enum class EABTSM11FinaleCameraStage : uint8
{
	PreLaunch = 0,
	CruiseToBody,
	Approach,
	Periapsis,
	Handoff,
	FinalApproach,
	Terminal,
	Unavailable
};

/** Pure-data result of resolving one playback time against authority events. */
struct ABTSRUNTIME_API FABTSM11FinaleCameraStageSelection
{
	EABTSM11FinaleCameraStage Stage =
		EABTSM11FinaleCameraStage::PreLaunch;
	int32 AssistIndex = 1;
	double StageProgress = 0.0;
	FString TargetLabel = TEXT("Assist1");
	FString Reason = TEXT("AwaitingLaunch");
	bool bTargetIsUFO = false;

	bool IsUsable() const;
	bool IsM2Assist1Window() const;
};

/** One renderer-independent director sample supplied by the interaction owner. */
struct ABTSRUNTIME_API FABTSM11FinaleCameraDirectorSample
{
	FABTSM11FinaleCameraStageSelection Selection;
	FVector TargetCenter = FVector::ZeroVector;
	double TargetRadiusCM = 0.0;
	double BirdRadiusCM = 60.0;
	/** Frozen chronological flyby direction; this must project to screen-right. */
	FVector EncounterScreenRight = FVector::ForwardVector;
	/** Frozen screen-up direction, normal to the closest-approach plane. */
	FVector EncounterScreenUp = FVector::UpVector;

	bool IsUsable() const;
};

namespace ABTSM11FinaleCameraDirector
{
	ABTSRUNTIME_API const TCHAR* StageLabel(EABTSM11FinaleCameraStage Stage);

	/**
	 * Resolves stage only from frozen authority events and playback time.
	 * Missing/incomplete events fail closed as Unavailable.
	 */
	ABTSRUNTIME_API FABTSM11FinaleCameraStageSelection ResolveStage(
		bool bLaunched,
		bool bTargetHit,
		double PlaybackSeconds,
		const FABTSM11TrajectoryResult* Result);

	/**
	 * Builds one stable planet-anchored encounter basis from authority events.
	 * The basis is chronological rather than recomputed from the live radial,
	 * so the bird crosses the planet screen-left to screen-right while the
	 * closest radial remains primarily a depth relationship.
	 */
	ABTSRUNTIME_API bool BuildAssistEncounterBasis(
		const FVector& TargetCenter,
		const FVector& EnterPosition,
		const FVector& ClosestPosition,
		const FVector& ClosestVelocity,
		const FVector& ExitPosition,
		FVector& OutScreenRight,
		FVector& OutScreenUp);

	/** Process-wide developer switch, frozen by each camera at follow start. */
	ABTSRUNTIME_API void SetM2Enabled(bool bEnabled);
	ABTSRUNTIME_API bool IsM2Enabled();
}
