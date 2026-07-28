// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "World/ABTSM110FinaleTypes.h"

/** Stable, solver-side trajectory events. UI failure labels are mapped from these values. */
enum class EABTSM11TrajectoryEventType : uint8
{
	AssistEnter = 0,
	ClosestApproach,
	AssistExit,
	BodyCollision,
	TargetHit,
	Timeout,
	SolarCaptured,
	WrongOrder,
	OutOfBounds,
	AssistInvalidHyperbola,
	PlanetCaptured,
	AssistInvalidBPlaneBasis,
	AssistSolveFailed,
	/**
	 * A swept segment entered the target sphere without producing success.
	 *
	 * Qualified contact remains TargetHit. Appending this value preserves the
	 * numeric identity of every M11-A event in hash schema version 1.
	 */
	TargetContact
};

/** The first terminal condition is authoritative and ends integration. */
enum class EABTSM11TrajectoryTermination : uint8
{
	None = 0,
	TargetHit,
	BodyCollision,
	WrongOrder,
	OutOfBounds,
	SolarCaptured,
	Timeout,
	AssistInvalidHyperbola,
	PlanetCaptured,
	AssistInvalidBPlaneBasis,
	AssistSolveFailed,
	InvalidInput
};

/** Which signed B-plane coordinate is allowed to receive a positive gameplay assist. */
enum class EABTSM11AllowedPassSide : uint8
{
	Any = 0,
	PositiveT,
	NegativeT,
	PositiveR,
	NegativeR
};

/**
 * Immutable analytic body consumed by the M11 solver.
 *
 * Visual meshes and collision components are deliberately absent. Primary and
 * all three assist roles remain fixed, so the M9 satellite cannot enter this
 * data contract.
 */
struct ABTSRUNTIME_API FABTSM11GravityBodySpec
{
	int32 BodyId = INDEX_NONE;
	EABTSM110FinaleGravityRole Role = EABTSM110FinaleGravityRole::Primary;
	FVector3d CenterCM = FVector3d::ZeroVector;
	double GravitationalParameterCM3PerSec2 = 0.0;
	double MinimumEvaluationRadiusCM = 1.0;
	double VisualRadiusCM = 1.0;
	double CollisionRadiusCM = 1.0;
	double MaximumSimulationRadiusCM = 0.0;

	/** Assist-only analytic radii. Natural gravity fades between the two outer radii. */
	double InfluenceRadiusCM = 0.0;
	double AssistReferenceRadiusCM = 0.0;
	double InfluenceBlendWidthCM = 0.0;

	/** Fixed visual planet momentum proxy. It never changes CenterCM. */
	FVector3d VirtualOrbitalVelocityCMPerSec = FVector3d::ZeroVector;

	/** Author-authored, deterministic B-plane basis and corridor. */
	FVector3d BPlaneReferenceNormal = FVector3d(0.0, 0.0, 1.0);
	FVector3d BPlaneFallbackAxis = FVector3d(0.0, 1.0, 0.0);
	double BPlaneTargetTCM = 0.0;
	double BPlaneTargetRCM = 0.0;
	double BPlaneSigmaTCM = 1.0;
	double BPlaneSigmaRCM = 1.0;
	double BPlaneOuterChiSquared = 4.0;
	EABTSM11AllowedPassSide AllowedPassSide = EABTSM11AllowedPassSide::Any;

	double MinimumEnergyChangeCM2PerSec2 = -1.0e12;
	double MaximumEnergyChangeCM2PerSec2 = 1.0e12;
	FLinearColor DebugColor = FLinearColor::White;

	bool IsAssist() const;
	int32 GetAssistIndex() const;
	bool IsValid(FString* OutFailure = nullptr) const;
};

/**
 * Immutable analytic target.
 *
 * CenterCM/HitRadiusCM are the qualified terminal-intercept envelope. The
 * optional geometric center/radius identify the later physical UFO contact
 * used by bypass certification and presentation. Mesh bounds alter neither.
 */
struct ABTSRUNTIME_API FABTSM11TargetSpec
{
	int32 TargetId = INDEX_NONE;
	FVector3d CenterCM = FVector3d::ZeroVector;
	/** Qualified gameplay success envelope. */
	double HitRadiusCM = 0.0;
	/**
	 * Unqualified physical-contact sphere used by bypass certification.
	 * Zero preserves M11-A behavior by falling back to HitRadiusCM.
	 */
	double GeometricContactRadiusCM = 0.0;
	/**
	 * M11-B may place the physical UFO farther down the certified coast than
	 * the terminal-intercept envelope. False preserves generic M11-A behavior.
	 */
	bool bUseSeparateGeometricContactCenter = false;
	FVector3d GeometricContactCenterCM = FVector3d::ZeroVector;
	/**
	 * TargetHit success is authoritative only after this many consecutive
	 * qualifying assists. Geometric contact remains observable independently.
	 * Zero preserves generic M11-A target behavior.
	 */
	int32 RequiredQualifiedAssistCount = 0;
	double MinimumQualifyingCorridorQuality = 0.0;
	double MinimumQualifyingEnergyGainCM2PerSec2 = 0.0;
	bool bRequireAllowedPassSide = false;
	FVector3d PresentationForward = FVector3d(1.0, 0.0, 0.0);

	double GetGeometricContactRadiusCM() const;
	FVector3d GetGeometricContactCenterCM() const;
	bool IsValid(FString* OutFailure = nullptr) const;
};

/** Primary + three ordered assist planets and one non-gravitating UFO target. */
struct ABTSRUNTIME_API FABTSM11GravityScenario
{
	static constexpr int32 BodyCount = FABTSM110FinaleGravityScenario::BodyCount;
	static constexpr int32 AssistCount = BodyCount - 1;

	int32 LayoutVersion = 1;
	uint32 ScenarioHash = 0;
	TStaticArray<FABTSM11GravityBodySpec, BodyCount> Bodies;
	FABTSM11TargetSpec Target;

	FABTSM11GravityScenario();

	const FABTSM11GravityBodySpec& GetPrimary() const { return Bodies[0]; }
	const FABTSM11GravityBodySpec& GetAssist(const int32 AssistIndex) const
	{
		check(AssistIndex >= 1 && AssistIndex <= AssistCount);
		return Bodies[AssistIndex];
	}

	bool IsValid(FString* OutFailure = nullptr) const;
};

/** Frozen numerical policy. Every value contributes to the result hash through SolverVersion. */
struct ABTSRUNTIME_API FABTSM11SolverConfig
{
	int32 SolverVersion = 1;
	int32 HashSchemaVersion = 1;
	double FixedTimeStepSeconds = 1.0 / 120.0;
	double MaximumSimulationTimeSeconds = 120.0;
	int32 MaximumStepCount = 2000000;
	int32 MaximumSubdivisionDepth = 6;

	double AssistStepRadiusFraction = 0.04;
	double CollisionStepRadiusFraction = 0.25;
	double GravityTimescaleFraction = 0.05;
	double PositionErrorLimitCM = 0.5;

	int32 RootBisectionIterations = 24;
	double RootAlphaTolerance = 1.0e-10;
	double BPlaneBasisMinimumLength = 1.0e-8;
	double MinimumVInfinityCMPerSec = 1.0;
	double MaximumNaturalDeflectionErrorRadians = 0.35;
	double EnergyQualityPower = 2.0;
	double EnergyRootEpsilonCM2PerSec2 = 1.0e-6;
	double ExitEnergyResidualToleranceCM2PerSec2 = 5.0;
	/** Version 1 performs three fixed normalization passes before the residual gate. */
	int32 EnergyShootingIterationCount = 3;
	double NaturalCloneMaximumTimeSeconds = 120.0;
	int32 NaturalCloneMaximumStepCount = 1000000;

	/** Bit 0/1/2 controls only the gameplay energy exchange of assist 1/2/3. */
	uint8 EnabledAssistMask = 0x7u;

	bool IsGameplayAssistEnabled(int32 AssistIndex) const;
	bool IsValid(FString* OutFailure = nullptr) const;
};

/** UObject-free request used by preview, offline search and later deterministic playback. */
struct ABTSRUNTIME_API FABTSM11TrajectoryRequest
{
	FABTSM11GravityScenario Scenario;
	FABTSM11SolverConfig Config;
	FVector3d InitialPositionCM = FVector3d::ZeroVector;
	FVector3d InitialVelocityCMPerSec = FVector3d::ZeroVector;
	double InitialTimeSeconds = 0.0;
	int32 InitialExpectedAssistIndex = 1;

	bool IsValid(FString* OutFailure = nullptr) const;
};

struct ABTSRUNTIME_API FABTSM11TrajectoryPoint
{
	double TimeSeconds = 0.0;
	FVector3d PositionCM = FVector3d::ZeroVector;
	FVector3d VelocityCMPerSec = FVector3d::ZeroVector;
	double PrimarySpecificEnergyCM2PerSec2 = 0.0;
};

/** One ordered, fully diagnostic solver event. */
struct ABTSRUNTIME_API FABTSM11TrajectoryEvent
{
	EABTSM11TrajectoryEventType Type = EABTSM11TrajectoryEventType::Timeout;
	int32 BodyId = INDEX_NONE;
	int32 AssistIndex = 0;
	double TimeSeconds = 0.0;
	FVector3d PositionCM = FVector3d::ZeroVector;
	FVector3d VelocityCMPerSec = FVector3d::ZeroVector;

	double EntrySpeedCMPerSec = 0.0;
	double ExitSpeedCMPerSec = 0.0;
	double ClosestDistanceCM = 0.0;
	double BPlaneTCM = 0.0;
	double BPlaneRCM = 0.0;
	double BPlaneChiSquared = 0.0;
	double CorridorQuality = 0.0;
	double NaturalDeflectionRadians = 0.0;
	double IdealDeflectionRadians = 0.0;
	double RawEnergyChangeCM2PerSec2 = 0.0;
	double RequestedEnergyChangeCM2PerSec2 = 0.0;
	double AppliedEnergyChangeCM2PerSec2 = 0.0;
};

/** Complete authoritative trajectory; rendering may derive a decimated copy later. */
struct ABTSRUNTIME_API FABTSM11TrajectoryResult
{
	void Reset();

	TArray<FABTSM11TrajectoryPoint> Points;
	TArray<FABTSM11TrajectoryEvent> Events;
	EABTSM11TrajectoryTermination Termination = EABTSM11TrajectoryTermination::None;
	int32 CompletedAssistCount = 0;
	/** Number of swept entries into the analytic target sphere. */
	int32 TargetContactCount = 0;
	uint64 ValidationHash = 0;
	FString Diagnostic;

	bool DidHitTarget() const { return Termination == EABTSM11TrajectoryTermination::TargetHit; }
	bool DidContactTarget() const { return TargetContactCount > 0; }
	const FABTSM11TrajectoryEvent* FindFirstEvent(EABTSM11TrajectoryEventType Type) const;
	const FABTSM11TrajectoryEvent* FindAssistEvent(EABTSM11TrajectoryEventType Type, int32 AssistIndex) const;
};
