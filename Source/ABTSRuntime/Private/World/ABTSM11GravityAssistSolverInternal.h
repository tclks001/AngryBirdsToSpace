// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "World/ABTSM11GravityAssistSolver.h"

namespace ABTSM11GravityAssist
{
	struct FState
	{
		double TimeSeconds = 0.0;
		FVector3d PositionCM = FVector3d::ZeroVector;
		FVector3d VelocityCMPerSec = FVector3d::ZeroVector;
	};

	struct FNaturalEncounterPlan
	{
		EABTSM11TrajectoryTermination Failure = EABTSM11TrajectoryTermination::None;
		EABTSM11TrajectoryEventType FailureEvent = EABTSM11TrajectoryEventType::AssistSolveFailed;
		int32 FailureBodyId = INDEX_NONE;
		int32 FailureAssistIndex = 0;
		FState EntryState;
		FState ClosestState;
		FState ExitState;
		FState FailureState;
		double ClosestDistanceCM = 0.0;
		double VInfinityCMPerSec = 0.0;
		double BPlaneTCM = 0.0;
		double BPlaneRCM = 0.0;
		double BPlaneChiSquared = 0.0;
		double CorridorQuality = 0.0;
		double NaturalDeflectionRadians = 0.0;
		double IdealDeflectionRadians = 0.0;
		double RawEnergyChangeCM2PerSec2 = 0.0;
		double RequestedEnergyChangeCM2PerSec2 = 0.0;
		double NaturalExitPrimaryEnergyCM2PerSec2 = 0.0;
		double KernelNormalizationSeconds = 0.0;
	};

	struct FActiveEncounter
	{
		int32 AssistIndex = 0;
		int32 EnterEventIndex = INDEX_NONE;
		bool bPlanReady = false;
		bool bAwaitingNaturalTerminal = false;
		bool bPassedClosestApproach = false;
		bool bReferenceExited = false;
		FNaturalEncounterPlan Plan;
		double AppliedEnergyChangeCM2PerSec2 = 0.0;
	};

	enum class EHardHit : uint8
	{
		None,
		Body,
		Target
	};

	struct FHardHitResult
	{
		EHardHit Type = EHardHit::None;
		double Alpha = 1.0;
		int32 BodyIndex = INDEX_NONE;
		/**
		 * Geometric target contact is reported independently of Type. Type is
		 * Target only when the qualification gate is also satisfied.
		 */
		bool bHasTargetContact = false;
		double TargetContactAlpha = 1.0;
	};

	struct FSphereRoots
	{
		double EnterAlpha = 0.0;
		double ExitAlpha = 0.0;
		bool bStartsInside = false;
	};

	bool IsFiniteVector(const FVector3d& Value);
	FVector3d LerpVector(const FVector3d& A, const FVector3d& B, double Alpha);
	FState LerpState(const FState& A, const FState& B, double Alpha);

	FState ConservativeStep(
		const FABTSM11TrajectoryRequest& Request,
		int32 ExpectedAssistIndex,
		const FState& State,
		double DeltaSeconds);
	bool SelectStepSeconds(
		const FABTSM11TrajectoryRequest& Request,
		int32 ExpectedAssistIndex,
		const FState& State,
		double& OutStepSeconds);

	bool SegmentSphereRoots(
		const FVector3d& StartCM,
		const FVector3d& EndCM,
		const FVector3d& CenterCM,
		double RadiusCM,
		FSphereRoots& OutRoots);
	double FindRadialRootAlpha(
		const FState& Start,
		const FState& End,
		const FVector3d& CenterCM,
		const FABTSM11SolverConfig& Config);
	double FindRadialStepFraction(
		const FABTSM11TrajectoryRequest& Request,
		int32 ExpectedAssistIndex,
		const FState& State,
		double FullStepSeconds,
		const FVector3d& CenterCM,
		const FABTSM11SolverConfig& Config,
		bool bIncreasing = true);
	double FindSphereBoundaryStepFraction(
		const FABTSM11TrajectoryRequest& Request,
		int32 ExpectedAssistIndex,
		const FState& State,
		double FullStepSeconds,
		const FVector3d& CenterCM,
		double RadiusCM,
		bool bEntering,
		const FABTSM11SolverConfig& Config);
	double EvaluateOutboundKernel(
		const FABTSM11GravityBodySpec& Assist,
		double ClosestDistanceCM,
		const FVector3d& PositionCM);

	void FillPlanDiagnostics(
		const FNaturalEncounterPlan& Plan,
		FABTSM11TrajectoryEvent& Event);
	FABTSM11TrajectoryEvent MakeEvent(
		EABTSM11TrajectoryEventType Type,
		int32 BodyId,
		int32 AssistIndex,
		const FState& State);
	FABTSM11TrajectoryPoint MakePoint(
		const FABTSM11GravityBodySpec& Primary,
		const FState& State);
	bool ApplyEnergyKick(
		FVector3d& InOutVelocityCMPerSec,
		double EnergyChangeCM2PerSec2,
		const FABTSM11SolverConfig& Config);
	double ClampToRemainingEnergy(
		double ProposedEnergy,
		double RequestedEnergy,
		double AppliedEnergy);
	FHardHitResult FindHardHit(
		const FABTSM11TrajectoryRequest& Request,
		const FState& Start,
		const FState& End,
		int32 QualifiedAssistCount);
	bool AssistExitQualifiesTarget(
		const FABTSM11TrajectoryRequest& Request,
		const FABTSM11TrajectoryEvent& ExitEvent);
	void FinalizeResult(
		const FABTSM11TrajectoryRequest& Request,
		FABTSM11TrajectoryResult& Result,
		EABTSM11TrajectoryTermination Termination,
		const TCHAR* Diagnostic);
	void FinalizeHardHit(
		const FABTSM11TrajectoryRequest& Request,
		const FABTSM11GravityBodySpec& Primary,
		const FState& Start,
		const FState& End,
		const FHardHitResult& HardHit,
		FABTSM11TrajectoryResult& Result);
	void FinalizeAssistFailure(
		const FABTSM11TrajectoryRequest& Request,
		const FABTSM11GravityBodySpec& Primary,
		const FState& FailureState,
		const FABTSM11GravityBodySpec* Assist,
		const FActiveEncounter* Encounter,
		EABTSM11TrajectoryTermination Termination,
		EABTSM11TrajectoryEventType EventType,
		const TCHAR* Diagnostic,
		FABTSM11TrajectoryResult& Result);
	double ExactSphereBoundaryFraction(
		const FABTSM11TrajectoryRequest& Request,
		int32 ExpectedAssistIndex,
		const FState& State,
		double FullStepSeconds,
		const FVector3d& CenterCM,
		double RadiusCM,
		bool bEntering,
		const FSphereRoots& SegmentRoots);

	bool BuildNaturalEncounterPlan(
		const FABTSM11TrajectoryRequest& Request,
		int32 AssistIndex,
		int32 QualifiedAssistCount,
		const FState& EntryState,
		FNaturalEncounterPlan& OutPlan);

	uint64 ComputeResultHash(
		const FABTSM11TrajectoryRequest& Request,
		const FABTSM11TrajectoryResult& Result);
}
