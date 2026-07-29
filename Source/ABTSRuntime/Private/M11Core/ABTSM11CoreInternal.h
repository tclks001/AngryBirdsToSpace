// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "M11Core/ABTSM11CoreSolver.h"

#include <cstdint>
#include <optional>
#include <string>

namespace ABTS::M11Core::Internal
{
	struct State
	{
		double TimeSeconds = 0.0;
		Vec3d PositionCM;
		Vec3d VelocityCMPerSec;
	};

	struct NaturalEncounterPlan
	{
		TrajectoryTermination Failure = TrajectoryTermination::None;
		TrajectoryEventType FailureEvent =
			TrajectoryEventType::AssistSolveFailed;
		std::int32_t FailureBodyId = InvalidIndex;
		std::int32_t FailureAssistIndex = 0;
		State EntryState;
		State ClosestState;
		State ExitState;
		State FailureState;
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

	struct ActiveEncounter
	{
		std::int32_t AssistIndex = 0;
		std::int32_t EnterEventIndex = InvalidIndex;
		bool PlanReady = false;
		bool AwaitingNaturalTerminal = false;
		bool PassedClosestApproach = false;
		bool ReferenceExited = false;
		NaturalEncounterPlan Plan;
		double AppliedEnergyChangeCM2PerSec2 = 0.0;
	};

	enum class HardHit : std::uint8_t
	{
		None,
		Body,
		Target
	};

	struct HardHitResult
	{
		HardHit Type = HardHit::None;
		double Alpha = 1.0;
		std::int32_t BodyIndex = InvalidIndex;
		bool HasTargetContact = false;
		double TargetContactAlpha = 1.0;
	};

	struct SphereRoots
	{
		double EnterAlpha = 0.0;
		double ExitAlpha = 0.0;
		bool StartsInside = false;
	};

	[[nodiscard]] bool IsFiniteVector(const Vec3d& Value);
	[[nodiscard]] Vec3d LerpVector(
		const Vec3d& A,
		const Vec3d& B,
		double Alpha);
	[[nodiscard]] State LerpState(
		const State& A,
		const State& B,
		double Alpha);

	[[nodiscard]] State ConservativeStep(
		const TrajectoryRequest& Request,
		std::int32_t ExpectedAssistIndex,
		const State& CurrentState,
		double DeltaSeconds);
	[[nodiscard]] bool SelectStepSeconds(
		const TrajectoryRequest& Request,
		std::int32_t ExpectedAssistIndex,
		const State& CurrentState,
		double& OutStepSeconds);
	[[nodiscard]] bool SegmentSphereRoots(
		const Vec3d& StartCM,
		const Vec3d& EndCM,
		const Vec3d& CenterCM,
		double RadiusCM,
		SphereRoots& OutRoots);
	[[nodiscard]] bool IsV2MacroStepSphereTopologyCertified(
		const TrajectoryRequest& Request,
		const State& Start,
		const State& End,
		double FullStepSeconds);
	[[nodiscard]] double FindRadialRootAlpha(
		const State& Start,
		const State& End,
		const Vec3d& CenterCM,
		const SolverConfig& Config);
	[[nodiscard]] double FindRadialStepFraction(
		const TrajectoryRequest& Request,
		std::int32_t ExpectedAssistIndex,
		const State& CurrentState,
		double FullStepSeconds,
		const Vec3d& CenterCM,
		const SolverConfig& Config,
		bool Increasing = true);
	[[nodiscard]] double FindSphereBoundaryStepFraction(
		const TrajectoryRequest& Request,
		std::int32_t ExpectedAssistIndex,
		const State& CurrentState,
		double FullStepSeconds,
		const Vec3d& CenterCM,
		double RadiusCM,
		bool Entering,
		const SolverConfig& Config);
	[[nodiscard]] double EvaluateOutboundKernel(
		const GravityBodySpec& Assist,
		double ClosestDistanceCM,
		const Vec3d& PositionCM);

	void FillPlanDiagnostics(
		const NaturalEncounterPlan& Plan,
		TrajectoryEvent& Event);
	[[nodiscard]] TrajectoryEvent MakeEvent(
		TrajectoryEventType Type,
		std::int32_t BodyId,
		std::int32_t AssistIndex,
		const State& CurrentState);
	[[nodiscard]] TrajectoryPoint MakePoint(
		const GravityBodySpec& Primary,
		const State& CurrentState);
	[[nodiscard]] bool ApplyEnergyKick(
		Vec3d& InOutVelocityCMPerSec,
		double EnergyChangeCM2PerSec2,
		const SolverConfig& Config);
	[[nodiscard]] double ClampToRemainingEnergy(
		double ProposedEnergy,
		double RequestedEnergy,
		double AppliedEnergy);
	[[nodiscard]] HardHitResult FindHardHit(
		const TrajectoryRequest& Request,
		const State& Start,
		const State& End,
		std::int32_t QualifiedAssistCount);
	[[nodiscard]] bool AssistExitQualifiesTarget(
		const TrajectoryRequest& Request,
		const TrajectoryEvent& ExitEvent);
	void FinalizeResult(
		const TrajectoryRequest& Request,
		TrajectoryResult& Result,
		TrajectoryTermination Termination,
		const char* Diagnostic);
	void FinalizeHardHit(
		const TrajectoryRequest& Request,
		const GravityBodySpec& Primary,
		const State& Start,
		const State& End,
		const HardHitResult& HardHit,
		TrajectoryResult& Result);
	void FinalizeAssistFailure(
		const TrajectoryRequest& Request,
		const GravityBodySpec& Primary,
		const State& FailureState,
		const GravityBodySpec* Assist,
		const ActiveEncounter* Encounter,
		TrajectoryTermination Termination,
		TrajectoryEventType EventType,
		const char* Diagnostic,
		TrajectoryResult& Result);
	[[nodiscard]] double ExactSphereBoundaryFraction(
		const TrajectoryRequest& Request,
		std::int32_t ExpectedAssistIndex,
		const State& CurrentState,
		double FullStepSeconds,
		const Vec3d& CenterCM,
		double RadiusCM,
		bool Entering,
		const SphereRoots& SegmentRoots);

	[[nodiscard]] bool BuildNaturalEncounterPlan(
		const TrajectoryRequest& Request,
		std::int32_t AssistIndex,
		std::int32_t QualifiedAssistCount,
		const State& EntryState,
		NaturalEncounterPlan& OutPlan);

	[[nodiscard]] std::uint64_t ComputeResultHash(
		const TrajectoryRequest& Request,
		const TrajectoryResult& Result);
}
