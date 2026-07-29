// Copyright Epic Games, Inc. All Rights Reserved.

#include "M11Core/ABTSM11CoreSolver.h"

#include "M11Core/ABTSM11CoreInternal.h"

#include <optional>
#include <utility>

namespace ABTS::M11Core::SolverDetail
{
	enum class Transition : std::uint8_t
	{
		None,
		InfluenceEnter,
		ReferenceEnter,
		Closest,
		NaturalCapture,
		ReferenceExit,
		InfluenceExit
	};
}

bool ABTS::M11Core::GravityAssistSolver::Solve(
	const TrajectoryRequest& Request,
	TrajectoryResult& OutResult,
	std::string* OutFailure)
{
	using namespace Internal;

	OutResult.Reset();
	std::string ValidationFailure;
	if (!Request.IsValid(&ValidationFailure))
	{
		OutResult.Termination = TrajectoryTermination::InvalidInput;
		OutResult.Diagnostic = ValidationFailure;
		if (OutFailure != nullptr)
		{
			*OutFailure = ValidationFailure;
		}
		return false;
	}

	const GravityBodySpec& Primary = Request.Scenario.GetPrimary();
	State CurrentState;
	CurrentState.TimeSeconds = Request.InitialTimeSeconds;
	CurrentState.PositionCM = Request.InitialPositionCM;
	CurrentState.VelocityCMPerSec =
		Request.InitialVelocityCMPerSec;
	std::int32_t ExpectedAssistIndex =
		Request.InitialExpectedAssistIndex;
	std::int32_t QualifiedAssistCount =
		Request.InitialExpectedAssistIndex - 1;
	OutResult.CompletedAssistCount =
		Request.InitialExpectedAssistIndex - 1;
	bool TargetContactInside = false;
	std::optional<ActiveEncounter> ActiveEncounterValue;
	OutResult.Points.push_back(MakePoint(Primary, CurrentState));

	const double EndTimeSeconds =
		Request.InitialTimeSeconds
		+ Request.Config.MaximumSimulationTimeSeconds;
	std::int32_t ExecutedStepCount = 0;
	for (;
		ExecutedStepCount < Request.Config.MaximumStepCount
			&& CurrentState.TimeSeconds < EndTimeSeconds;
		++ExecutedStepCount)
	{
		double StepSeconds = 0.0;
		if (!SelectStepSeconds(
			Request,
			ExpectedAssistIndex,
			CurrentState,
			StepSeconds))
		{
			const GravityBodySpec* Assist =
				ExpectedAssistIndex >= 1
					&& ExpectedAssistIndex
						<= GravityScenario::AssistCount
				? &Request.Scenario.GetAssist(ExpectedAssistIndex)
				: nullptr;
			FinalizeAssistFailure(
				Request,
				Primary,
				CurrentState,
				Assist,
				ActiveEncounterValue.has_value()
					? &ActiveEncounterValue.value()
					: nullptr,
				TrajectoryTermination::AssistSolveFailed,
				TrajectoryEventType::AssistSolveFailed,
				"SubdivisionLimitExceeded",
				OutResult);
			return true;
		}
		StepSeconds =
			Min(StepSeconds, EndTimeSeconds - CurrentState.TimeSeconds);
		State Candidate = ConservativeStep(
			Request,
			ExpectedAssistIndex,
			CurrentState,
			StepSeconds);
		const double FixedStepSeconds =
			Request.Config.FixedTimeStepSeconds;
		while (Request.Config.SolverVersion >= 2
			&& StepSeconds > FixedStepSeconds
			&& IsFiniteVector(Candidate.PositionCM)
			&& IsFiniteVector(Candidate.VelocityCMPerSec)
			&& IsFinite(Candidate.TimeSeconds)
			&& !IsV2MacroStepSphereTopologyCertified(
				Request,
				CurrentState,
				Candidate,
				StepSeconds))
		{
			StepSeconds = Max(
				FixedStepSeconds,
				StepSeconds * 0.5);
			Candidate = ConservativeStep(
				Request,
				ExpectedAssistIndex,
				CurrentState,
				StepSeconds);
		}
		if (!IsFiniteVector(Candidate.PositionCM)
			|| !IsFiniteVector(Candidate.VelocityCMPerSec)
			|| !IsFinite(Candidate.TimeSeconds))
		{
			const GravityBodySpec* Assist =
				ExpectedAssistIndex >= 1
					&& ExpectedAssistIndex
						<= GravityScenario::AssistCount
				? &Request.Scenario.GetAssist(ExpectedAssistIndex)
				: nullptr;
			FinalizeAssistFailure(
				Request,
				Primary,
				CurrentState,
				Assist,
				ActiveEncounterValue.has_value()
					? &ActiveEncounterValue.value()
					: nullptr,
				TrajectoryTermination::AssistSolveFailed,
				TrajectoryEventType::AssistSolveFailed,
				"NonFiniteState",
				OutResult);
			return true;
		}

		const HardHitResult HardHitValue = FindHardHit(
			Request,
			CurrentState,
			Candidate,
			QualifiedAssistCount);
		double TransitionAlpha = 2.0;
		std::int32_t TransitionAssistIndex = 0;
		SphereRoots TransitionRoots;
		SolverDetail::Transition CurrentTransition =
			SolverDetail::Transition::None;

		if (!ActiveEncounterValue.has_value())
		{
			for (std::int32_t AssistIndex = 1;
				AssistIndex <= GravityScenario::AssistCount;
				++AssistIndex)
			{
				const GravityBodySpec& Assist =
					Request.Scenario.GetAssist(AssistIndex);
				SphereRoots Roots;
				if (SegmentSphereRoots(
					CurrentState.PositionCM,
					Candidate.PositionCM,
					Assist.CenterCM,
					Assist.InfluenceRadiusCM,
					Roots)
					&& !Roots.StartsInside
					&& Roots.EnterAlpha < TransitionAlpha)
				{
					CurrentTransition =
						SolverDetail::Transition::InfluenceEnter;
					TransitionAlpha = Roots.EnterAlpha;
					TransitionAssistIndex = AssistIndex;
					TransitionRoots = Roots;
				}
			}
		}
		else
		{
			ActiveEncounter& Encounter =
				ActiveEncounterValue.value();
			const GravityBodySpec& Assist =
				Request.Scenario.GetAssist(Encounter.AssistIndex);
			const double StartDistanceCM =
				(CurrentState.PositionCM - Assist.CenterCM).Length();
			const double CandidateDistanceCM =
				(Candidate.PositionCM - Assist.CenterCM).Length();
			const double StartRadialRate = Vec3d::DotProduct(
				CurrentState.PositionCM - Assist.CenterCM,
				CurrentState.VelocityCMPerSec);
			const double CandidateRadialRate = Vec3d::DotProduct(
				Candidate.PositionCM - Assist.CenterCM,
				Candidate.VelocityCMPerSec);

			if (Encounter.AwaitingNaturalTerminal)
			{
				if (!Encounter.PassedClosestApproach
					&& StartRadialRate <= 0.0
					&& CandidateRadialRate >= 0.0)
				{
					CurrentTransition =
						SolverDetail::Transition::Closest;
					TransitionAlpha = FindRadialRootAlpha(
						CurrentState,
						Candidate,
						Assist.CenterCM,
						Request.Config);
				}
				else if (Encounter.PassedClosestApproach
					&& StartRadialRate >= 0.0
					&& CandidateRadialRate <= 0.0)
				{
					CurrentTransition =
						SolverDetail::Transition::NaturalCapture;
					TransitionAlpha = FindRadialStepFraction(
						Request,
						ExpectedAssistIndex,
						CurrentState,
						StepSeconds,
						Assist.CenterCM,
						Request.Config,
						false);
				}
				else
				{
					SphereRoots InfluenceRoots;
					if (StartDistanceCM
							<= Assist.InfluenceRadiusCM
						&& CandidateDistanceCM
							>= Assist.InfluenceRadiusCM
						&& CandidateRadialRate > 0.0
						&& SegmentSphereRoots(
							CurrentState.PositionCM,
							Candidate.PositionCM,
							Assist.CenterCM,
							Assist.InfluenceRadiusCM,
							InfluenceRoots))
					{
						CurrentTransition =
							SolverDetail::Transition::InfluenceExit;
						TransitionAlpha =
							InfluenceRoots.ExitAlpha;
						TransitionRoots = InfluenceRoots;
					}
				}
			}
			else if (!Encounter.PlanReady)
			{
				SphereRoots ReferenceRoots;
				if (SegmentSphereRoots(
					CurrentState.PositionCM,
					Candidate.PositionCM,
					Assist.CenterCM,
					Assist.AssistReferenceRadiusCM,
					ReferenceRoots)
					&& !ReferenceRoots.StartsInside)
				{
					CurrentTransition =
						SolverDetail::Transition::ReferenceEnter;
					TransitionAlpha = ReferenceRoots.EnterAlpha;
					TransitionRoots = ReferenceRoots;
				}
				SphereRoots InfluenceRoots;
				if (StartDistanceCM <= Assist.InfluenceRadiusCM
					&& CandidateDistanceCM
						>= Assist.InfluenceRadiusCM
					&& CandidateRadialRate > 0.0
					&& SegmentSphereRoots(
						CurrentState.PositionCM,
						Candidate.PositionCM,
						Assist.CenterCM,
						Assist.InfluenceRadiusCM,
						InfluenceRoots)
					&& InfluenceRoots.ExitAlpha < TransitionAlpha)
				{
					CurrentTransition =
						SolverDetail::Transition::InfluenceExit;
					TransitionAlpha = InfluenceRoots.ExitAlpha;
					TransitionRoots = InfluenceRoots;
				}
			}
			else if (!Encounter.PassedClosestApproach)
			{
				if (StartRadialRate <= 0.0
					&& CandidateRadialRate >= 0.0)
				{
					CurrentTransition =
						SolverDetail::Transition::Closest;
					TransitionAlpha = FindRadialRootAlpha(
						CurrentState,
						Candidate,
						Assist.CenterCM,
						Request.Config);
				}
			}
			else if (!Encounter.ReferenceExited)
			{
				SphereRoots ReferenceRoots;
				if (StartDistanceCM
						<= Assist.AssistReferenceRadiusCM
					&& CandidateDistanceCM
						>= Assist.AssistReferenceRadiusCM
					&& CandidateRadialRate > 0.0
					&& SegmentSphereRoots(
						CurrentState.PositionCM,
						Candidate.PositionCM,
						Assist.CenterCM,
						Assist.AssistReferenceRadiusCM,
						ReferenceRoots))
				{
					CurrentTransition =
						SolverDetail::Transition::ReferenceExit;
					TransitionAlpha = ReferenceRoots.ExitAlpha;
					TransitionRoots = ReferenceRoots;
				}
				else if (StartRadialRate > 0.0
					&& CandidateRadialRate < 0.0)
				{
					CurrentTransition =
						SolverDetail::Transition::NaturalCapture;
					TransitionAlpha = FindRadialStepFraction(
						Request,
						ExpectedAssistIndex,
						CurrentState,
						StepSeconds,
						Assist.CenterCM,
						Request.Config,
						false);
				}
			}
			else
			{
				SphereRoots InfluenceRoots;
				if (StartDistanceCM <= Assist.InfluenceRadiusCM
					&& CandidateDistanceCM
						>= Assist.InfluenceRadiusCM
					&& CandidateRadialRate > 0.0
					&& SegmentSphereRoots(
						CurrentState.PositionCM,
						Candidate.PositionCM,
						Assist.CenterCM,
						Assist.InfluenceRadiusCM,
						InfluenceRoots))
				{
					CurrentTransition =
						SolverDetail::Transition::InfluenceExit;
					TransitionAlpha = InfluenceRoots.ExitAlpha;
					TransitionRoots = InfluenceRoots;
				}
			}
		}

		const bool NonTerminalTargetContactBeforeBoundary =
			!TargetContactInside
			&& HardHitValue.HasTargetContact
			&& HardHitValue.Type != HardHit::Target
			&& (HardHitValue.Type == HardHit::None
				|| HardHitValue.TargetContactAlpha
					<= HardHitValue.Alpha
						+ Request.Config.RootAlphaTolerance)
			&& (CurrentTransition == SolverDetail::Transition::None
				|| HardHitValue.TargetContactAlpha
					<= TransitionAlpha
						+ Request.Config.RootAlphaTolerance);
		if (NonTerminalTargetContactBeforeBoundary)
		{
			const State ContactState = LerpState(
				CurrentState,
				Candidate,
				HardHitValue.TargetContactAlpha);
			OutResult.Points.push_back(
				MakePoint(Primary, ContactState));
			OutResult.Events.push_back(MakeEvent(
				TrajectoryEventType::TargetContact,
				Request.Scenario.Target.TargetId,
				0,
				ContactState));
			++OutResult.TargetContactCount;
			TargetContactInside = true;
		}

		if (HardHitValue.Type != HardHit::None
			&& (CurrentTransition
					== SolverDetail::Transition::None
				|| HardHitValue.Alpha
					<= TransitionAlpha
						+ Request.Config.RootAlphaTolerance))
		{
			FinalizeHardHit(
				Request,
				Primary,
				CurrentState,
				Candidate,
				HardHitValue,
				OutResult);
			return true;
		}

		if (CurrentTransition != SolverDetail::Transition::None)
		{
			State TransitionState;
			if (CurrentTransition == SolverDetail::Transition::Closest
				|| CurrentTransition
					== SolverDetail::Transition::NaturalCapture)
			{
				const ActiveEncounter& Encounter =
					ActiveEncounterValue.value();
				const GravityBodySpec& Assist =
					Request.Scenario.GetAssist(
						Encounter.AssistIndex);
				const double Fraction = FindRadialStepFraction(
					Request,
					ExpectedAssistIndex,
					CurrentState,
					StepSeconds,
					Assist.CenterCM,
					Request.Config,
					CurrentTransition
						== SolverDetail::Transition::Closest);
				TransitionState = ConservativeStep(
					Request,
					ExpectedAssistIndex,
					CurrentState,
					StepSeconds * Fraction);
			}
			else
			{
				const std::int32_t AssistIndex =
					CurrentTransition
						== SolverDetail::Transition::InfluenceEnter
					? TransitionAssistIndex
					: ActiveEncounterValue.value().AssistIndex;
				const GravityBodySpec& Assist =
					Request.Scenario.GetAssist(AssistIndex);
				const bool Influence =
					CurrentTransition
						== SolverDetail::Transition::InfluenceEnter
					|| CurrentTransition
						== SolverDetail::Transition::InfluenceExit;
				const bool Entering =
					CurrentTransition
						== SolverDetail::Transition::InfluenceEnter
					|| CurrentTransition
						== SolverDetail::Transition::ReferenceEnter;
				const double RadiusCM = Influence
					? Assist.InfluenceRadiusCM
					: Assist.AssistReferenceRadiusCM;
				const double Fraction =
					ExactSphereBoundaryFraction(
						Request,
						ExpectedAssistIndex,
						CurrentState,
						StepSeconds,
						Assist.CenterCM,
						RadiusCM,
						Entering,
						TransitionRoots);
				TransitionState = ConservativeStep(
					Request,
					ExpectedAssistIndex,
					CurrentState,
					StepSeconds * Fraction);
			}

			if (CurrentTransition
				== SolverDetail::Transition::InfluenceEnter)
			{
				const GravityBodySpec& Assist =
					Request.Scenario.GetAssist(
						TransitionAssistIndex);
				if (TransitionAssistIndex != ExpectedAssistIndex)
				{
					OutResult.Points.push_back(
						MakePoint(Primary, TransitionState));
					OutResult.Events.push_back(MakeEvent(
						TrajectoryEventType::WrongOrder,
						Assist.BodyId,
						TransitionAssistIndex,
						TransitionState));
					FinalizeResult(
						Request,
						OutResult,
						TrajectoryTermination::WrongOrder,
						"WrongOrder");
					return true;
				}
				ActiveEncounter Encounter;
				Encounter.AssistIndex = TransitionAssistIndex;
				Encounter.EnterEventIndex =
					static_cast<std::int32_t>(
						OutResult.Events.size());
				OutResult.Events.push_back(MakeEvent(
					TrajectoryEventType::AssistEnter,
					Assist.BodyId,
					TransitionAssistIndex,
					TransitionState));
				ActiveEncounterValue = std::move(Encounter);
			}
			else
			{
				ActiveEncounter& Encounter =
					ActiveEncounterValue.value();
				const GravityBodySpec& Assist =
					Request.Scenario.GetAssist(
						Encounter.AssistIndex);
				if (CurrentTransition
					== SolverDetail::Transition::ReferenceEnter)
				{
					NaturalEncounterPlan Plan;
					if (!BuildNaturalEncounterPlan(
						Request,
						Encounter.AssistIndex,
						QualifiedAssistCount,
						TransitionState,
						Plan))
					{
						const bool DeferredNaturalTerminal =
							Plan.Failure
								== TrajectoryTermination::BodyCollision
							|| Plan.Failure
								== TrajectoryTermination::TargetHit
							|| Plan.Failure
								== TrajectoryTermination::SolarCaptured
							|| Plan.Failure
								== TrajectoryTermination::Timeout
							|| Plan.Failure
								== TrajectoryTermination::PlanetCaptured;
						if (DeferredNaturalTerminal)
						{
							Encounter.AwaitingNaturalTerminal = true;
						}
						else
						{
							OutResult.Points.push_back(
								MakePoint(
									Primary,
									TransitionState));
							TrajectoryEvent FailureEvent =
								MakeEvent(
									Plan.FailureEvent,
									Assist.BodyId,
									Encounter.AssistIndex,
									TransitionState);
							FillPlanDiagnostics(
								Plan,
								FailureEvent);
							OutResult.Events.push_back(
								FailureEvent);
							FinalizeResult(
								Request,
								OutResult,
								Plan.Failure,
								"NaturalEncounterPlanFailed");
							return true;
						}
					}
					else
					{
						Encounter.Plan = std::move(Plan);
						Encounter.PlanReady = true;
						if (Encounter.EnterEventIndex >= 0
							&& static_cast<std::size_t>(
								Encounter.EnterEventIndex)
								< OutResult.Events.size())
						{
							FillPlanDiagnostics(
								Encounter.Plan,
								OutResult.Events[
									static_cast<std::size_t>(
										Encounter
											.EnterEventIndex)]);
						}
					}
				}
				else if (CurrentTransition
					== SolverDetail::Transition::Closest)
				{
					Encounter.PassedClosestApproach = true;
					TrajectoryEvent Event = MakeEvent(
						TrajectoryEventType::ClosestApproach,
						Assist.BodyId,
						Encounter.AssistIndex,
						TransitionState);
					if (Encounter.PlanReady)
					{
						FillPlanDiagnostics(
							Encounter.Plan,
							Event);
					}
					Event.ClosestDistanceCM =
						(TransitionState.PositionCM
							- Assist.CenterCM).Length();
					OutResult.Events.push_back(Event);
				}
				else if (CurrentTransition
					== SolverDetail::Transition::NaturalCapture)
				{
					FinalizeAssistFailure(
						Request,
						Primary,
						TransitionState,
						&Assist,
						&Encounter,
						TrajectoryTermination::PlanetCaptured,
						TrajectoryEventType::PlanetCaptured,
						"NaturalPlanetCaptured",
						OutResult);
					return true;
				}
				else if (CurrentTransition
					== SolverDetail::Transition::ReferenceExit)
				{
					const double RawWeight =
						EvaluateOutboundKernel(
							Assist,
							Encounter.Plan.ClosestDistanceCM,
							LerpVector(
								CurrentState.PositionCM,
								TransitionState.PositionCM,
								0.5))
						* (TransitionState.TimeSeconds
							- CurrentState.TimeSeconds);
					const double EnergyStep =
						ClampToRemainingEnergy(
							Encounter.Plan
								.RequestedEnergyChangeCM2PerSec2
								* RawWeight
								/ Encounter.Plan
									.KernelNormalizationSeconds,
							Encounter.Plan
								.RequestedEnergyChangeCM2PerSec2,
							Encounter
								.AppliedEnergyChangeCM2PerSec2);
					const double RemainingEnergy =
						Encounter.Plan
							.RequestedEnergyChangeCM2PerSec2
						- Encounter
							.AppliedEnergyChangeCM2PerSec2
						- EnergyStep;
					if (!ApplyEnergyKick(
							TransitionState.VelocityCMPerSec,
							EnergyStep,
							Request.Config)
						|| !ApplyEnergyKick(
							TransitionState.VelocityCMPerSec,
							RemainingEnergy,
							Request.Config))
					{
						FinalizeAssistFailure(
							Request,
							Primary,
							TransitionState,
							&Assist,
							&Encounter,
							TrajectoryTermination
								::AssistSolveFailed,
							TrajectoryEventType
								::AssistSolveFailed,
							"ExitEnergyKickNegativeRoot",
							OutResult);
						return true;
					}
					Encounter.AppliedEnergyChangeCM2PerSec2 +=
						EnergyStep + RemainingEnergy;
					const double ActualExitEnergy =
						ComputePrimarySpecificEnergy(
							Primary,
							TransitionState.PositionCM,
							TransitionState.VelocityCMPerSec);
					const double EnergyResidual =
						ActualExitEnergy
						- Encounter.Plan
							.NaturalExitPrimaryEnergyCM2PerSec2
						- Encounter.Plan
							.RequestedEnergyChangeCM2PerSec2;
					if (std::abs(EnergyResidual)
						> Request.Config
							.ExitEnergyResidualToleranceCM2PerSec2)
					{
						FinalizeAssistFailure(
							Request,
							Primary,
							TransitionState,
							&Assist,
							&Encounter,
							TrajectoryTermination
								::AssistSolveFailed,
							TrajectoryEventType
								::AssistSolveFailed,
							"ExitEnergyResidual",
							OutResult);
						return true;
					}
					Encounter.ReferenceExited = true;
				}
				else
				{
					if (!Encounter.PlanReady
						|| !Encounter.PassedClosestApproach
						|| !Encounter.ReferenceExited)
					{
						FinalizeAssistFailure(
							Request,
							Primary,
							TransitionState,
							&Assist,
							&Encounter,
							TrajectoryTermination
								::AssistSolveFailed,
							TrajectoryEventType
								::AssistSolveFailed,
							"ReferenceSphereMissed",
							OutResult);
						return true;
					}
					TrajectoryEvent ExitEvent = MakeEvent(
						TrajectoryEventType::AssistExit,
						Assist.BodyId,
						Encounter.AssistIndex,
						TransitionState);
					if (Encounter.PlanReady)
					{
						FillPlanDiagnostics(
							Encounter.Plan,
							ExitEvent);
					}
					ExitEvent.ExitSpeedCMPerSec =
						TransitionState.VelocityCMPerSec.Length();
					ExitEvent.AppliedEnergyChangeCM2PerSec2 =
						Encounter.AppliedEnergyChangeCM2PerSec2;
					OutResult.Events.push_back(ExitEvent);
					if (QualifiedAssistCount
							== Encounter.AssistIndex - 1
						&& AssistExitQualifiesTarget(
							Request,
							ExitEvent))
					{
						QualifiedAssistCount =
							Encounter.AssistIndex;
					}
					OutResult.CompletedAssistCount = Max(
						OutResult.CompletedAssistCount,
						Encounter.AssistIndex);
					ExpectedAssistIndex =
						Encounter.AssistIndex + 1;
					ActiveEncounterValue.reset();
				}
			}

			CurrentState = TransitionState;
			TargetContactInside =
				(CurrentState.PositionCM
					- Request.Scenario.Target
						.GetGeometricContactCenterCM())
					.SquaredLength()
				<= Square(
					Request.Scenario.Target
						.GetGeometricContactRadiusCM());
			OutResult.Points.push_back(
				MakePoint(Primary, CurrentState));
			continue;
		}

		if (ActiveEncounterValue.has_value())
		{
			ActiveEncounter& Encounter =
				ActiveEncounterValue.value();
			if (Encounter.PlanReady
				&& Encounter.PassedClosestApproach
				&& !Encounter.ReferenceExited)
			{
				const GravityBodySpec& Assist =
					Request.Scenario.GetAssist(
						Encounter.AssistIndex);
				const double RawWeight =
					EvaluateOutboundKernel(
						Assist,
						Encounter.Plan.ClosestDistanceCM,
						LerpVector(
							CurrentState.PositionCM,
							Candidate.PositionCM,
							0.5))
					* StepSeconds;
				const double EnergyStep =
					ClampToRemainingEnergy(
						Encounter.Plan
							.RequestedEnergyChangeCM2PerSec2
							* RawWeight
							/ Encounter.Plan
								.KernelNormalizationSeconds,
						Encounter.Plan
							.RequestedEnergyChangeCM2PerSec2,
						Encounter
							.AppliedEnergyChangeCM2PerSec2);
				if (!ApplyEnergyKick(
					Candidate.VelocityCMPerSec,
					EnergyStep,
					Request.Config))
				{
					FinalizeAssistFailure(
						Request,
						Primary,
						Candidate,
						&Assist,
						&Encounter,
						TrajectoryTermination::AssistSolveFailed,
						TrajectoryEventType::AssistSolveFailed,
						"EnergyKickNegativeRoot",
						OutResult);
					return true;
				}
				Encounter.AppliedEnergyChangeCM2PerSec2 +=
					EnergyStep;
			}
		}

		if ((Candidate.PositionCM - Primary.CenterCM).Length()
			> Primary.MaximumSimulationRadiusCM)
		{
			SphereRoots Roots;
			State ExitState = Candidate;
			if (SegmentSphereRoots(
				CurrentState.PositionCM,
				Candidate.PositionCM,
				Primary.CenterCM,
				Primary.MaximumSimulationRadiusCM,
				Roots))
			{
				const double Fraction =
					ExactSphereBoundaryFraction(
						Request,
						ExpectedAssistIndex,
						CurrentState,
						StepSeconds,
						Primary.CenterCM,
						Primary.MaximumSimulationRadiusCM,
						false,
						Roots);
				ExitState = ConservativeStep(
					Request,
					ExpectedAssistIndex,
					CurrentState,
					StepSeconds * Fraction);
			}
			OutResult.Points.push_back(
				MakePoint(Primary, ExitState));
			OutResult.Events.push_back(MakeEvent(
				TrajectoryEventType::OutOfBounds,
				Primary.BodyId,
				0,
				ExitState));
			FinalizeResult(
				Request,
				OutResult,
				TrajectoryTermination::OutOfBounds,
				"OutOfBounds");
			return true;
		}

		CurrentState = Candidate;
		TargetContactInside =
			(CurrentState.PositionCM
				- Request.Scenario.Target
					.GetGeometricContactCenterCM()).SquaredLength()
			<= Square(
				Request.Scenario.Target
					.GetGeometricContactRadiusCM());
		OutResult.Points.push_back(
			MakePoint(Primary, CurrentState));
	}

	const double TimeToleranceSeconds =
		Request.Config.FixedTimeStepSeconds
		* Request.Config.RootAlphaTolerance;
	if (CurrentState.TimeSeconds
		< EndTimeSeconds - TimeToleranceSeconds)
	{
		const GravityBodySpec* Assist =
			ExpectedAssistIndex >= 1
				&& ExpectedAssistIndex
					<= GravityScenario::AssistCount
			? &Request.Scenario.GetAssist(ExpectedAssistIndex)
			: nullptr;
		FinalizeAssistFailure(
			Request,
			Primary,
			CurrentState,
			Assist,
			ActiveEncounterValue.has_value()
				? &ActiveEncounterValue.value()
				: nullptr,
			TrajectoryTermination::AssistSolveFailed,
			TrajectoryEventType::AssistSolveFailed,
			"StepBudgetExceeded",
			OutResult);
		return true;
	}

	const double FinalEnergy = ComputePrimarySpecificEnergy(
		Primary,
		CurrentState.PositionCM,
		CurrentState.VelocityCMPerSec);
	if (FinalEnergy < 0.0)
	{
		OutResult.Events.push_back(MakeEvent(
			TrajectoryEventType::SolarCaptured,
			Primary.BodyId,
			0,
			CurrentState));
		FinalizeResult(
			Request,
			OutResult,
			TrajectoryTermination::SolarCaptured,
			"SolarCaptured");
	}
	else
	{
		OutResult.Events.push_back(MakeEvent(
			TrajectoryEventType::Timeout,
			InvalidIndex,
			0,
			CurrentState));
		FinalizeResult(
			Request,
			OutResult,
			TrajectoryTermination::Timeout,
			"Timeout");
	}
	return true;
}
