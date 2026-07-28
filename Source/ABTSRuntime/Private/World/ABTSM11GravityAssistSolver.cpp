// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11GravityAssistSolver.h"

#include "World/ABTSM11GravityAssistSolverInternal.h"

bool FABTSM11GravityAssistSolver::Solve(
	const FABTSM11TrajectoryRequest& Request,
	FABTSM11TrajectoryResult& OutResult,
	FString* OutFailure)
{
	using namespace ABTSM11GravityAssist;

	OutResult.Reset();
	FString ValidationFailure;
	if (!Request.IsValid(&ValidationFailure))
	{
		OutResult.Termination = EABTSM11TrajectoryTermination::InvalidInput;
		OutResult.Diagnostic = ValidationFailure;
		if (OutFailure != nullptr)
		{
			*OutFailure = ValidationFailure;
		}
		return false;
	}

	const FABTSM11GravityBodySpec& Primary = Request.Scenario.GetPrimary();
	FState State;
	State.TimeSeconds = Request.InitialTimeSeconds;
	State.PositionCM = Request.InitialPositionCM;
	State.VelocityCMPerSec = Request.InitialVelocityCMPerSec;
	int32 ExpectedAssistIndex = Request.InitialExpectedAssistIndex;
	int32 QualifiedAssistCount = Request.InitialExpectedAssistIndex - 1;
	OutResult.CompletedAssistCount = Request.InitialExpectedAssistIndex - 1;
	bool bTargetContactInside = false;
	TOptional<FActiveEncounter> ActiveEncounter;
	OutResult.Points.Add(MakePoint(Primary, State));

	const double EndTimeSeconds =
		Request.InitialTimeSeconds + Request.Config.MaximumSimulationTimeSeconds;
	int32 ExecutedStepCount = 0;
	for (;
		ExecutedStepCount < Request.Config.MaximumStepCount
			&& State.TimeSeconds < EndTimeSeconds;
		++ExecutedStepCount)
	{
		double StepSeconds = 0.0;
		if (!SelectStepSeconds(Request, ExpectedAssistIndex, State, StepSeconds))
		{
			const FABTSM11GravityBodySpec* Assist =
				ExpectedAssistIndex >= 1
					&& ExpectedAssistIndex <= FABTSM11GravityScenario::AssistCount
				? &Request.Scenario.GetAssist(ExpectedAssistIndex)
				: nullptr;
			FinalizeAssistFailure(
				Request,
				Primary,
				State,
				Assist,
				ActiveEncounter.IsSet() ? &ActiveEncounter.GetValue() : nullptr,
				EABTSM11TrajectoryTermination::AssistSolveFailed,
				EABTSM11TrajectoryEventType::AssistSolveFailed,
				TEXT("SubdivisionLimitExceeded"),
				OutResult);
			return true;
		}
		StepSeconds = FMath::Min(
			StepSeconds, EndTimeSeconds - State.TimeSeconds);
		FState Candidate =
			ConservativeStep(Request, ExpectedAssistIndex, State, StepSeconds);
		if (!IsFiniteVector(Candidate.PositionCM)
			|| !IsFiniteVector(Candidate.VelocityCMPerSec)
			|| !FMath::IsFinite(Candidate.TimeSeconds))
		{
			const FABTSM11GravityBodySpec* Assist =
				ExpectedAssistIndex >= 1
					&& ExpectedAssistIndex <= FABTSM11GravityScenario::AssistCount
				? &Request.Scenario.GetAssist(ExpectedAssistIndex)
				: nullptr;
			FinalizeAssistFailure(
				Request,
				Primary,
				State,
				Assist,
				ActiveEncounter.IsSet() ? &ActiveEncounter.GetValue() : nullptr,
				EABTSM11TrajectoryTermination::AssistSolveFailed,
				EABTSM11TrajectoryEventType::AssistSolveFailed,
				TEXT("NonFiniteState"),
				OutResult);
			return true;
		}

		const FHardHitResult HardHit = FindHardHit(
			Request,
			State,
			Candidate,
			QualifiedAssistCount);
		double TransitionAlpha = 2.0;
		int32 TransitionAssistIndex = 0;
		FSphereRoots TransitionRoots;
		enum class ETransition : uint8
		{
			None,
			InfluenceEnter,
			ReferenceEnter,
			Closest,
			NaturalCapture,
			ReferenceExit,
			InfluenceExit
		};
		ETransition Transition = ETransition::None;

		if (!ActiveEncounter.IsSet())
		{
			for (int32 AssistIndex = 1;
				AssistIndex <= FABTSM11GravityScenario::AssistCount;
				++AssistIndex)
			{
				const FABTSM11GravityBodySpec& Assist =
					Request.Scenario.GetAssist(AssistIndex);
				FSphereRoots Roots;
				if (SegmentSphereRoots(
					State.PositionCM,
					Candidate.PositionCM,
					Assist.CenterCM,
					Assist.InfluenceRadiusCM,
					Roots)
					&& !Roots.bStartsInside
					&& Roots.EnterAlpha < TransitionAlpha)
				{
					Transition = ETransition::InfluenceEnter;
					TransitionAlpha = Roots.EnterAlpha;
					TransitionAssistIndex = AssistIndex;
					TransitionRoots = Roots;
				}
			}
		}
		else
		{
			FActiveEncounter& Encounter = ActiveEncounter.GetValue();
			const FABTSM11GravityBodySpec& Assist =
				Request.Scenario.GetAssist(Encounter.AssistIndex);
			const double StartDistanceCM =
				(State.PositionCM - Assist.CenterCM).Length();
			const double CandidateDistanceCM =
				(Candidate.PositionCM - Assist.CenterCM).Length();
			const double StartRadialRate = FVector3d::DotProduct(
				State.PositionCM - Assist.CenterCM, State.VelocityCMPerSec);
			const double CandidateRadialRate = FVector3d::DotProduct(
				Candidate.PositionCM - Assist.CenterCM,
				Candidate.VelocityCMPerSec);

			if (Encounter.bAwaitingNaturalTerminal)
			{
				if (!Encounter.bPassedClosestApproach
					&& StartRadialRate <= 0.0
					&& CandidateRadialRate >= 0.0)
				{
					Transition = ETransition::Closest;
					TransitionAlpha =
						FindRadialRootAlpha(
							State, Candidate, Assist.CenterCM, Request.Config);
				}
				else if (Encounter.bPassedClosestApproach
					&& StartRadialRate >= 0.0
					&& CandidateRadialRate <= 0.0)
				{
					Transition = ETransition::NaturalCapture;
					TransitionAlpha = FindRadialStepFraction(
						Request,
						ExpectedAssistIndex,
						State,
						StepSeconds,
						Assist.CenterCM,
						Request.Config,
						false);
				}
				else
				{
					FSphereRoots InfluenceRoots;
					if (StartDistanceCM <= Assist.InfluenceRadiusCM
						&& CandidateDistanceCM >= Assist.InfluenceRadiusCM
						&& CandidateRadialRate > 0.0
						&& SegmentSphereRoots(
							State.PositionCM,
							Candidate.PositionCM,
							Assist.CenterCM,
							Assist.InfluenceRadiusCM,
							InfluenceRoots))
					{
						Transition = ETransition::InfluenceExit;
						TransitionAlpha = InfluenceRoots.ExitAlpha;
						TransitionRoots = InfluenceRoots;
					}
				}
			}
			else if (!Encounter.bPlanReady)
			{
				FSphereRoots ReferenceRoots;
				if (SegmentSphereRoots(
					State.PositionCM,
					Candidate.PositionCM,
					Assist.CenterCM,
					Assist.AssistReferenceRadiusCM,
					ReferenceRoots)
					&& !ReferenceRoots.bStartsInside)
				{
					Transition = ETransition::ReferenceEnter;
					TransitionAlpha = ReferenceRoots.EnterAlpha;
					TransitionRoots = ReferenceRoots;
				}
				FSphereRoots InfluenceRoots;
				if (StartDistanceCM <= Assist.InfluenceRadiusCM
					&& CandidateDistanceCM >= Assist.InfluenceRadiusCM
					&& CandidateRadialRate > 0.0
					&& SegmentSphereRoots(
						State.PositionCM,
						Candidate.PositionCM,
						Assist.CenterCM,
						Assist.InfluenceRadiusCM,
						InfluenceRoots)
					&& InfluenceRoots.ExitAlpha < TransitionAlpha)
				{
					Transition = ETransition::InfluenceExit;
					TransitionAlpha = InfluenceRoots.ExitAlpha;
					TransitionRoots = InfluenceRoots;
				}
			}
			else if (!Encounter.bPassedClosestApproach)
			{
				if (StartRadialRate <= 0.0 && CandidateRadialRate >= 0.0)
				{
					Transition = ETransition::Closest;
					TransitionAlpha =
						FindRadialRootAlpha(State, Candidate, Assist.CenterCM, Request.Config);
				}
			}
			else if (!Encounter.bReferenceExited)
			{
				FSphereRoots ReferenceRoots;
				if (StartDistanceCM <= Assist.AssistReferenceRadiusCM
					&& CandidateDistanceCM >= Assist.AssistReferenceRadiusCM
					&& CandidateRadialRate > 0.0
					&& SegmentSphereRoots(
						State.PositionCM,
						Candidate.PositionCM,
						Assist.CenterCM,
						Assist.AssistReferenceRadiusCM,
						ReferenceRoots))
				{
					Transition = ETransition::ReferenceExit;
					TransitionAlpha = ReferenceRoots.ExitAlpha;
					TransitionRoots = ReferenceRoots;
				}
				else if (StartRadialRate > 0.0 && CandidateRadialRate < 0.0)
				{
					Transition = ETransition::NaturalCapture;
					TransitionAlpha = FindRadialStepFraction(
						Request,
						ExpectedAssistIndex,
						State,
						StepSeconds,
						Assist.CenterCM,
						Request.Config,
						false);
				}
			}
			else
			{
				FSphereRoots InfluenceRoots;
				if (StartDistanceCM <= Assist.InfluenceRadiusCM
					&& CandidateDistanceCM >= Assist.InfluenceRadiusCM
					&& CandidateRadialRate > 0.0
					&& SegmentSphereRoots(
						State.PositionCM,
						Candidate.PositionCM,
						Assist.CenterCM,
						Assist.InfluenceRadiusCM,
						InfluenceRoots))
				{
					Transition = ETransition::InfluenceExit;
					TransitionAlpha = InfluenceRoots.ExitAlpha;
					TransitionRoots = InfluenceRoots;
				}
			}
		}

		const bool bNonTerminalTargetContactBeforeBoundary =
			!bTargetContactInside
			&& HardHit.bHasTargetContact
			&& HardHit.Type != EHardHit::Target
			&& (HardHit.Type == EHardHit::None
				|| HardHit.TargetContactAlpha
					<= HardHit.Alpha + Request.Config.RootAlphaTolerance)
			&& (Transition == ETransition::None
				|| HardHit.TargetContactAlpha
					<= TransitionAlpha + Request.Config.RootAlphaTolerance);
		if (bNonTerminalTargetContactBeforeBoundary)
		{
			const FState ContactState = LerpState(
				State, Candidate, HardHit.TargetContactAlpha);
			OutResult.Points.Add(MakePoint(Primary, ContactState));
			OutResult.Events.Add(MakeEvent(
				EABTSM11TrajectoryEventType::TargetContact,
				Request.Scenario.Target.TargetId,
				0,
				ContactState));
			++OutResult.TargetContactCount;
			bTargetContactInside = true;
			// TargetContact is an observation-only event. Retain the exact
			// crossing point in the result, but do not turn it into an
			// integration boundary or discard the remainder of this step.
		}

		if (HardHit.Type != EHardHit::None
			&& (Transition == ETransition::None
				|| HardHit.Alpha
					<= TransitionAlpha + Request.Config.RootAlphaTolerance))
		{
			FinalizeHardHit(Request, Primary, State, Candidate, HardHit, OutResult);
			return true;
		}

		if (Transition != ETransition::None)
		{
			FState TransitionState;
			if (Transition == ETransition::Closest
				|| Transition == ETransition::NaturalCapture)
			{
				const FActiveEncounter& Encounter = ActiveEncounter.GetValue();
				const FABTSM11GravityBodySpec& Assist =
					Request.Scenario.GetAssist(Encounter.AssistIndex);
				const double Fraction = FindRadialStepFraction(
					Request,
					ExpectedAssistIndex,
					State,
					StepSeconds,
					Assist.CenterCM,
					Request.Config,
					Transition == ETransition::Closest);
				TransitionState = ConservativeStep(
					Request, ExpectedAssistIndex, State, StepSeconds * Fraction);
			}
			else
			{
				const int32 AssistIndex = Transition == ETransition::InfluenceEnter
					? TransitionAssistIndex
					: ActiveEncounter.GetValue().AssistIndex;
				const FABTSM11GravityBodySpec& Assist =
					Request.Scenario.GetAssist(AssistIndex);
				const bool bInfluence =
					Transition == ETransition::InfluenceEnter
					|| Transition == ETransition::InfluenceExit;
				const bool bEntering =
					Transition == ETransition::InfluenceEnter
					|| Transition == ETransition::ReferenceEnter;
				const double RadiusCM = bInfluence
					? Assist.InfluenceRadiusCM
					: Assist.AssistReferenceRadiusCM;
				const double Fraction = ExactSphereBoundaryFraction(
					Request,
					ExpectedAssistIndex,
					State,
					StepSeconds,
					Assist.CenterCM,
					RadiusCM,
					bEntering,
					TransitionRoots);
				TransitionState = ConservativeStep(
					Request, ExpectedAssistIndex, State, StepSeconds * Fraction);
			}

			if (Transition == ETransition::InfluenceEnter)
			{
				const FABTSM11GravityBodySpec& Assist =
					Request.Scenario.GetAssist(TransitionAssistIndex);
				if (TransitionAssistIndex != ExpectedAssistIndex)
				{
					OutResult.Points.Add(MakePoint(Primary, TransitionState));
					OutResult.Events.Add(MakeEvent(
						EABTSM11TrajectoryEventType::WrongOrder,
						Assist.BodyId,
						TransitionAssistIndex,
						TransitionState));
					FinalizeResult(
						Request,
						OutResult,
						EABTSM11TrajectoryTermination::WrongOrder,
						TEXT("WrongOrder"));
					return true;
				}
				FActiveEncounter Encounter;
				Encounter.AssistIndex = TransitionAssistIndex;
				Encounter.EnterEventIndex = OutResult.Events.Add(MakeEvent(
					EABTSM11TrajectoryEventType::AssistEnter,
					Assist.BodyId,
					TransitionAssistIndex,
					TransitionState));
				ActiveEncounter = MoveTemp(Encounter);
			}
			else
			{
				FActiveEncounter& Encounter = ActiveEncounter.GetValue();
				const FABTSM11GravityBodySpec& Assist =
					Request.Scenario.GetAssist(Encounter.AssistIndex);
				if (Transition == ETransition::ReferenceEnter)
				{
					FNaturalEncounterPlan Plan;
					if (!BuildNaturalEncounterPlan(
						Request,
						Encounter.AssistIndex,
						QualifiedAssistCount,
						TransitionState,
						Plan))
					{
						const bool bDeferredNaturalTerminal =
							Plan.Failure
								== EABTSM11TrajectoryTermination::BodyCollision
							|| Plan.Failure
								== EABTSM11TrajectoryTermination::TargetHit
							|| Plan.Failure
								== EABTSM11TrajectoryTermination::SolarCaptured
							|| Plan.Failure
								== EABTSM11TrajectoryTermination::Timeout
							|| Plan.Failure
								== EABTSM11TrajectoryTermination::PlanetCaptured;
						if (bDeferredNaturalTerminal)
						{
							Encounter.bAwaitingNaturalTerminal = true;
						}
						else
						{
							OutResult.Points.Add(MakePoint(Primary, TransitionState));
							FABTSM11TrajectoryEvent FailureEvent = MakeEvent(
								Plan.FailureEvent,
								Assist.BodyId,
								Encounter.AssistIndex,
								TransitionState);
							FillPlanDiagnostics(Plan, FailureEvent);
							OutResult.Events.Add(FailureEvent);
							FinalizeResult(
								Request,
								OutResult,
								Plan.Failure,
								TEXT("NaturalEncounterPlanFailed"));
							return true;
						}
					}
					else
					{
						Encounter.Plan = MoveTemp(Plan);
						Encounter.bPlanReady = true;
						if (OutResult.Events.IsValidIndex(Encounter.EnterEventIndex))
						{
							FillPlanDiagnostics(
								Encounter.Plan,
								OutResult.Events[Encounter.EnterEventIndex]);
						}
					}
				}
				else if (Transition == ETransition::Closest)
				{
					Encounter.bPassedClosestApproach = true;
					FABTSM11TrajectoryEvent Event = MakeEvent(
						EABTSM11TrajectoryEventType::ClosestApproach,
						Assist.BodyId,
						Encounter.AssistIndex,
						TransitionState);
					if (Encounter.bPlanReady)
					{
						FillPlanDiagnostics(Encounter.Plan, Event);
					}
					Event.ClosestDistanceCM =
						(TransitionState.PositionCM - Assist.CenterCM).Length();
					OutResult.Events.Add(Event);
				}
				else if (Transition == ETransition::NaturalCapture)
				{
					FinalizeAssistFailure(
						Request,
						Primary,
						TransitionState,
						&Assist,
						&Encounter,
						EABTSM11TrajectoryTermination::PlanetCaptured,
						EABTSM11TrajectoryEventType::PlanetCaptured,
						TEXT("NaturalPlanetCaptured"),
						OutResult);
					return true;
				}
				else if (Transition == ETransition::ReferenceExit)
				{
					const double RawWeight =
						EvaluateOutboundKernel(
							Assist,
							Encounter.Plan.ClosestDistanceCM,
							LerpVector(State.PositionCM, TransitionState.PositionCM, 0.5))
						* (TransitionState.TimeSeconds - State.TimeSeconds);
					const double EnergyStep = ClampToRemainingEnergy(
						Encounter.Plan.RequestedEnergyChangeCM2PerSec2
							* RawWeight
							/ Encounter.Plan.KernelNormalizationSeconds,
						Encounter.Plan.RequestedEnergyChangeCM2PerSec2,
						Encounter.AppliedEnergyChangeCM2PerSec2);
					const double RemainingEnergy =
						Encounter.Plan.RequestedEnergyChangeCM2PerSec2
						- Encounter.AppliedEnergyChangeCM2PerSec2 - EnergyStep;
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
							EABTSM11TrajectoryTermination::AssistSolveFailed,
							EABTSM11TrajectoryEventType::AssistSolveFailed,
							TEXT("ExitEnergyKickNegativeRoot"),
							OutResult);
						return true;
					}
					Encounter.AppliedEnergyChangeCM2PerSec2 +=
						EnergyStep + RemainingEnergy;
					const double ActualExitEnergy = ComputePrimarySpecificEnergy(
						Primary,
						TransitionState.PositionCM,
						TransitionState.VelocityCMPerSec);
					const double EnergyResidual =
						ActualExitEnergy
						- Encounter.Plan.NaturalExitPrimaryEnergyCM2PerSec2
						- Encounter.Plan.RequestedEnergyChangeCM2PerSec2;
					if (FMath::Abs(EnergyResidual)
						> Request.Config.ExitEnergyResidualToleranceCM2PerSec2)
					{
						FinalizeAssistFailure(
							Request,
							Primary,
							TransitionState,
							&Assist,
							&Encounter,
							EABTSM11TrajectoryTermination::AssistSolveFailed,
							EABTSM11TrajectoryEventType::AssistSolveFailed,
							TEXT("ExitEnergyResidual"),
							OutResult);
						return true;
					}
					Encounter.bReferenceExited = true;
				}
				else
				{
					if (!Encounter.bPlanReady
						|| !Encounter.bPassedClosestApproach
						|| !Encounter.bReferenceExited)
					{
						FinalizeAssistFailure(
							Request,
							Primary,
							TransitionState,
							&Assist,
							&Encounter,
							EABTSM11TrajectoryTermination::AssistSolveFailed,
							EABTSM11TrajectoryEventType::AssistSolveFailed,
							TEXT("ReferenceSphereMissed"),
							OutResult);
						return true;
					}
					FABTSM11TrajectoryEvent ExitEvent = MakeEvent(
						EABTSM11TrajectoryEventType::AssistExit,
						Assist.BodyId,
						Encounter.AssistIndex,
						TransitionState);
					if (Encounter.bPlanReady)
					{
						FillPlanDiagnostics(Encounter.Plan, ExitEvent);
					}
					ExitEvent.ExitSpeedCMPerSec =
						TransitionState.VelocityCMPerSec.Length();
					ExitEvent.AppliedEnergyChangeCM2PerSec2 =
						Encounter.AppliedEnergyChangeCM2PerSec2;
					OutResult.Events.Add(ExitEvent);
					if (QualifiedAssistCount
							== Encounter.AssistIndex - 1
						&& AssistExitQualifiesTarget(
							Request, ExitEvent))
					{
						QualifiedAssistCount = Encounter.AssistIndex;
					}
					OutResult.CompletedAssistCount = FMath::Max(
						OutResult.CompletedAssistCount, Encounter.AssistIndex);
					ExpectedAssistIndex = Encounter.AssistIndex + 1;
					ActiveEncounter.Reset();
				}
			}

			State = TransitionState;
			bTargetContactInside =
				(State.PositionCM
					- Request.Scenario.Target
						.GetGeometricContactCenterCM())
					.SquaredLength()
				<= FMath::Square(
					Request.Scenario.Target
						.GetGeometricContactRadiusCM());
			OutResult.Points.Add(MakePoint(Primary, State));
			continue;
		}

		if (ActiveEncounter.IsSet())
		{
			FActiveEncounter& Encounter = ActiveEncounter.GetValue();
			if (Encounter.bPlanReady
				&& Encounter.bPassedClosestApproach
				&& !Encounter.bReferenceExited)
			{
				const FABTSM11GravityBodySpec& Assist =
					Request.Scenario.GetAssist(Encounter.AssistIndex);
				const double RawWeight =
					EvaluateOutboundKernel(
						Assist,
						Encounter.Plan.ClosestDistanceCM,
						LerpVector(State.PositionCM, Candidate.PositionCM, 0.5))
					* StepSeconds;
				const double EnergyStep = ClampToRemainingEnergy(
					Encounter.Plan.RequestedEnergyChangeCM2PerSec2
						* RawWeight / Encounter.Plan.KernelNormalizationSeconds,
					Encounter.Plan.RequestedEnergyChangeCM2PerSec2,
					Encounter.AppliedEnergyChangeCM2PerSec2);
				if (!ApplyEnergyKick(
					Candidate.VelocityCMPerSec, EnergyStep, Request.Config))
				{
					FinalizeAssistFailure(
						Request,
						Primary,
						Candidate,
						&Assist,
						&Encounter,
						EABTSM11TrajectoryTermination::AssistSolveFailed,
						EABTSM11TrajectoryEventType::AssistSolveFailed,
						TEXT("EnergyKickNegativeRoot"),
						OutResult);
					return true;
				}
				Encounter.AppliedEnergyChangeCM2PerSec2 += EnergyStep;
			}
		}

		if ((Candidate.PositionCM - Primary.CenterCM).Length()
			> Primary.MaximumSimulationRadiusCM)
		{
			FSphereRoots Roots;
			FState ExitState = Candidate;
			if (SegmentSphereRoots(
				State.PositionCM,
				Candidate.PositionCM,
				Primary.CenterCM,
				Primary.MaximumSimulationRadiusCM,
				Roots))
			{
				const double Fraction = ExactSphereBoundaryFraction(
					Request,
					ExpectedAssistIndex,
					State,
					StepSeconds,
					Primary.CenterCM,
					Primary.MaximumSimulationRadiusCM,
					false,
					Roots);
				ExitState = ConservativeStep(
					Request, ExpectedAssistIndex, State, StepSeconds * Fraction);
			}
			OutResult.Points.Add(MakePoint(Primary, ExitState));
			OutResult.Events.Add(MakeEvent(
				EABTSM11TrajectoryEventType::OutOfBounds,
				Primary.BodyId,
				0,
				ExitState));
			FinalizeResult(
				Request,
				OutResult,
				EABTSM11TrajectoryTermination::OutOfBounds,
				TEXT("OutOfBounds"));
			return true;
		}

		State = Candidate;
		bTargetContactInside =
			(State.PositionCM
				- Request.Scenario.Target
					.GetGeometricContactCenterCM()).SquaredLength()
			<= FMath::Square(
				Request.Scenario.Target.GetGeometricContactRadiusCM());
		OutResult.Points.Add(MakePoint(Primary, State));
	}

	const double TimeToleranceSeconds =
		Request.Config.FixedTimeStepSeconds * Request.Config.RootAlphaTolerance;
	if (State.TimeSeconds < EndTimeSeconds - TimeToleranceSeconds)
	{
		const FABTSM11GravityBodySpec* Assist =
			ExpectedAssistIndex >= 1
				&& ExpectedAssistIndex <= FABTSM11GravityScenario::AssistCount
			? &Request.Scenario.GetAssist(ExpectedAssistIndex)
			: nullptr;
		FinalizeAssistFailure(
			Request,
			Primary,
			State,
			Assist,
			ActiveEncounter.IsSet() ? &ActiveEncounter.GetValue() : nullptr,
			EABTSM11TrajectoryTermination::AssistSolveFailed,
			EABTSM11TrajectoryEventType::AssistSolveFailed,
			TEXT("StepBudgetExceeded"),
			OutResult);
		return true;
	}

	const double FinalEnergy = ComputePrimarySpecificEnergy(
		Primary, State.PositionCM, State.VelocityCMPerSec);
	if (FinalEnergy < 0.0)
	{
		OutResult.Events.Add(MakeEvent(
			EABTSM11TrajectoryEventType::SolarCaptured,
			Primary.BodyId,
			0,
			State));
		FinalizeResult(
			Request,
			OutResult,
			EABTSM11TrajectoryTermination::SolarCaptured,
			TEXT("SolarCaptured"));
	}
	else
	{
		OutResult.Events.Add(MakeEvent(
			EABTSM11TrajectoryEventType::Timeout,
			INDEX_NONE,
			0,
			State));
		FinalizeResult(
			Request,
			OutResult,
			EABTSM11TrajectoryTermination::Timeout,
			TEXT("Timeout"));
	}
	return true;
}
