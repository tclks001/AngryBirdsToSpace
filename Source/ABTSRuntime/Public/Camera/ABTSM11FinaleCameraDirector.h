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

/** Presentation-only shot phase layered over the immutable authority stage. */
enum class EABTSM11FinaleCameraShotPhase : uint8
{
	Authority = 0,
	OutgoingHold,
	DualBodyBridge,
	IncomingReveal,
	IncomingTrack,
	IncomingEntryMatch
};

/** Deterministic timing policy used to schedule one inter-body shot. */
struct ABTSRUNTIME_API FABTSM11FinaleCameraShotSettings
{
	double IncomingRevealLeadSeconds = 3.25;
	double IncomingAcquireSeconds = 0.40;
	double DualBodyBridgeSeconds = 0.60;
	double MinimumDepartureHoldSeconds = 0.75;
	double OutgoingReleaseSeconds = 0.40;
	double EntryMatchSeconds = 0.50;

	bool IsUsable() const;
};

/** Pure-data result of resolving one playback time against authority events. */
struct ABTSRUNTIME_API FABTSM11FinaleCameraStageSelection
{
	EABTSM11FinaleCameraStage Stage =
		EABTSM11FinaleCameraStage::PreLaunch;
	int32 AssistIndex = 1;
	/** Assist used to build the camera frame; may lead CurrentBody in Handoff. */
	int32 FramingAssistIndex = 1;
	/** Previous assist retained as the left-hand bridge subject; zero otherwise. */
	int32 OutgoingAssistIndex = 0;
	/** Next assist retained as the right-hand bridge subject; zero otherwise. */
	int32 IncomingAssistIndex = 0;
	double StageProgress = 0.0;
	double StageDurationSeconds = 0.0;
	EABTSM11FinaleCameraShotPhase ShotPhase =
		EABTSM11FinaleCameraShotPhase::Authority;
	double ShotProgress = 0.0;
	double ShotDurationSeconds = 0.0;
	/** Normalized end slope that matches IncomingReveal to Approach motion. */
	double ShotEndSlope = 0.0;
	FString TargetLabel = TEXT("Assist1");
	FString FramingTargetLabel = TEXT("Assist1");
	FString Reason = TEXT("AwaitingLaunch");
	FString ShotReason = TEXT("AuthorityStage");
	bool bTargetIsUFO = false;

	bool IsUsable() const;
	bool IsM2Assist1Window() const;
	bool IsM3AssistWindow() const;
	bool IsM3IncomingShot() const;
	bool IsM3IncomingAcquire() const;
	bool IsM3DualBodyBridge() const;
	bool IsM3InterBodyTransition() const;
	bool IsM3TransitionShot() const;
};

/** One renderer-independent director sample supplied by the interaction owner. */
struct ABTSRUNTIME_API FABTSM11FinaleCameraDirectorSample
{
	FABTSM11FinaleCameraStageSelection Selection;
	FVector TargetCenter = FVector::ZeroVector;
	double TargetRadiusCM = 0.0;
	/** Optional previous planet used by an inter-body bridge composition. */
	FVector OutgoingTargetCenter = FVector::ZeroVector;
	double OutgoingTargetRadiusCM = 0.0;
	FVector IncomingTargetCenter = FVector::ZeroVector;
	double IncomingTargetRadiusCM = 0.0;
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
	ABTSRUNTIME_API const TCHAR* ShotPhaseLabel(
		EABTSM11FinaleCameraShotPhase ShotPhase);

	/**
	 * Resolves stage only from frozen authority events and playback time.
	 * Missing/incomplete events fail closed as Unavailable.
	 */
	ABTSRUNTIME_API FABTSM11FinaleCameraStageSelection ResolveStage(
		bool bLaunched,
		bool bTargetHit,
		double PlaybackSeconds,
		const FABTSM11TrajectoryResult* Result,
		bool bUseM3ShotPlan = false,
		const FABTSM11FinaleCameraShotSettings* M3ShotSettings = nullptr);

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

	/** M3 directs all three assists while preserving the M2-only switch. */
	ABTSRUNTIME_API void SetM3Enabled(bool bEnabled);
	ABTSRUNTIME_API bool IsM3Enabled();

}
