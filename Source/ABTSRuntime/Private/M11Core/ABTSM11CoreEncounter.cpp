// Copyright Epic Games, Inc. All Rights Reserved.

#include "M11Core/ABTSM11CoreInternal.h"

namespace ABTS::M11Core::EncounterDetail
{
	using namespace Internal;

	[[nodiscard]] double SmoothStep5(const double Value)
	{
		const double X = Clamp(Value, 0.0, 1.0);
		return X * X * X * (X * (X * 6.0 - 15.0) + 10.0);
	}

	[[nodiscard]] bool IsPassSideAllowed(
		const AllowedPassSide Side,
		const double BPlaneTCM,
		const double BPlaneRCM)
	{
		switch (Side)
		{
		case AllowedPassSide::Any:
			return true;
		case AllowedPassSide::PositiveT:
			return BPlaneTCM > 0.0;
		case AllowedPassSide::NegativeT:
			return BPlaneTCM < 0.0;
		case AllowedPassSide::PositiveR:
			return BPlaneRCM > 0.0;
		case AllowedPassSide::NegativeR:
			return BPlaneRCM < 0.0;
		default:
			return false;
		}
	}

	[[nodiscard]] double ComputeCorridorQuality(
		const GravityBodySpec& Assist,
		const double ChiSquared)
	{
		if (ChiSquared <= 1.0)
		{
			return 1.0;
		}
		if (ChiSquared >= Assist.BPlaneOuterChiSquared)
		{
			return 0.0;
		}
		const double Normalized =
			(Assist.BPlaneOuterChiSquared - ChiSquared)
			/ (Assist.BPlaneOuterChiSquared - 1.0);
		return SmoothStep5(Normalized);
	}

	[[nodiscard]] bool FitHyperbolicAsymptoteDirection(
		const GravityBodySpec& Assist,
		const State& CurrentState,
		const bool Incoming,
		Vec3d& OutDirection)
	{
		const Vec3d RelativePositionCM =
			CurrentState.PositionCM - Assist.CenterCM;
		const Vec3d RelativeVelocityCMPerSec =
			CurrentState.VelocityCMPerSec;
		const double RadiusCM = RelativePositionCM.Length();
		const Vec3d AngularMomentum = Vec3d::CrossProduct(
			RelativePositionCM,
			RelativeVelocityCMPerSec);
		const double AngularMomentumLength =
			AngularMomentum.Length();
		if (RadiusCM <= DoubleSmallNumber
			|| AngularMomentumLength <= DoubleSmallNumber)
		{
			return false;
		}

		const Vec3d EccentricityVector =
			Vec3d::CrossProduct(
				RelativeVelocityCMPerSec,
				AngularMomentum)
				/ Assist.GravitationalParameterCM3PerSec2
			- RelativePositionCM / RadiusCM;
		const double Eccentricity = EccentricityVector.Length();
		if (!IsFinite(Eccentricity) || Eccentricity <= 1.0)
		{
			return false;
		}

		const Vec3d PeriapsisAxis =
			EccentricityVector / Eccentricity;
		const Vec3d NormalAxis =
			AngularMomentum / AngularMomentumLength;
		const Vec3d TransverseAxis =
			Vec3d::CrossProduct(
				NormalAxis,
				PeriapsisAxis).GetSafeNormal();
		if (TransverseAxis.SquaredLength() <= DoubleSmallNumber)
		{
			return false;
		}

		const double HyperbolicRoot = Sqrt(
			Max(0.0, Eccentricity * Eccentricity - 1.0));
		OutDirection = (
			(Incoming ? PeriapsisAxis : -PeriapsisAxis)
			+ TransverseAxis * HyperbolicRoot) / Eccentricity;
		OutDirection.Normalize();
		return OutDirection.SquaredLength() > DoubleSmallNumber;
	}

	[[nodiscard]] double ClampEncounterEnergyToRemaining(
		const double ProposedEnergy,
		const double RequestedEnergy,
		const double AppliedEnergy)
	{
		const double RemainingEnergy =
			RequestedEnergy - AppliedEnergy;
		if (RequestedEnergy >= 0.0)
		{
			return Clamp(ProposedEnergy, 0.0, RemainingEnergy);
		}
		return Clamp(ProposedEnergy, RemainingEnergy, 0.0);
	}

	[[nodiscard]] bool ApplySpecificEnergy(
		Vec3d& InOutVelocityCMPerSec,
		const double EnergyChangeCM2PerSec2,
		const SolverConfig& Config)
	{
		if (std::abs(EnergyChangeCM2PerSec2) <= DoubleSmallNumber)
		{
			return true;
		}
		const double SpeedCMPerSec =
			InOutVelocityCMPerSec.Length();
		double NewSpeedSquared =
			SpeedCMPerSec * SpeedCMPerSec
			+ 2.0 * EnergyChangeCM2PerSec2;
		if (SpeedCMPerSec <= DoubleSmallNumber
			|| NewSpeedSquared
				< -Config.EnergyRootEpsilonCM2PerSec2)
		{
			return false;
		}
		NewSpeedSquared = Max(0.0, NewSpeedSquared);
		InOutVelocityCMPerSec *=
			Sqrt(NewSpeedSquared) / SpeedCMPerSec;
		return IsFiniteVector(InOutVelocityCMPerSec);
	}

	[[nodiscard]] bool CalibrateKernelNormalization(
		const TrajectoryRequest& Request,
		const std::int32_t AssistIndex,
		NaturalEncounterPlan& Plan)
	{
		const SolverConfig& Config = Request.Config;
		const GravityBodySpec& Assist =
			Request.Scenario.GetAssist(AssistIndex);
		if (std::abs(Plan.RequestedEnergyChangeCM2PerSec2)
			<= Config.EnergyRootEpsilonCM2PerSec2)
		{
			return true;
		}
		const double RequestEndTimeSeconds =
			Request.InitialTimeSeconds
			+ Config.MaximumSimulationTimeSeconds;
		const double RequestTimeToleranceSeconds =
			Config.FixedTimeStepSeconds
			* Config.RootAlphaTolerance;
		const auto SetHorizonFailure =
			[&Request, &Plan](const State& HorizonState)
			{
				const bool CentralBound =
					GravityAssistSolver::ComputePrimarySpecificEnergy(
						Request.Scenario.GetPrimary(),
						HorizonState.PositionCM,
						HorizonState.VelocityCMPerSec) < 0.0;
				Plan.Failure = CentralBound
					? TrajectoryTermination::SolarCaptured
					: TrajectoryTermination::Timeout;
				Plan.FailureEvent = CentralBound
					? TrajectoryEventType::SolarCaptured
					: TrajectoryEventType::Timeout;
				Plan.FailureBodyId = CentralBound
					? Request.Scenario.GetPrimary().BodyId
					: InvalidIndex;
				Plan.FailureState = HorizonState;
			};

		double NormalizationSeconds =
			Plan.KernelNormalizationSeconds;
		for (std::int32_t Iteration = 0;
			Iteration < Config.EnergyShootingIterationCount;
			++Iteration)
		{
			State CurrentState = Plan.ClosestState;
			double AppliedEnergy = 0.0;
			double RawWeightSum = 0.0;
			bool Exited = false;
			for (std::int32_t StepIndex = 0;
				StepIndex < Config.NaturalCloneMaximumStepCount
					&& CurrentState.TimeSeconds
						- Plan.ClosestState.TimeSeconds
						< Config.NaturalCloneMaximumTimeSeconds;
				++StepIndex)
			{
				double StepSeconds = 0.0;
				if (!SelectStepSeconds(
					Request,
					AssistIndex,
					CurrentState,
					StepSeconds))
				{
					return false;
				}
				const double RemainingRequestTimeSeconds =
					RequestEndTimeSeconds
					- CurrentState.TimeSeconds;
				if (RemainingRequestTimeSeconds
					<= RequestTimeToleranceSeconds)
				{
					SetHorizonFailure(CurrentState);
					return false;
				}
				const bool EndsAtRequestHorizon =
					StepSeconds >= RemainingRequestTimeSeconds;
				StepSeconds =
					Min(StepSeconds, RemainingRequestTimeSeconds);
				State Candidate = ConservativeStep(
					Request,
					AssistIndex,
					CurrentState,
					StepSeconds);
				if (!IsFiniteVector(Candidate.PositionCM)
					|| !IsFiniteVector(
						Candidate.VelocityCMPerSec))
				{
					return false;
				}

				const double StartDistanceCM =
					(CurrentState.PositionCM
						- Assist.CenterCM).Length();
				const double CandidateDistanceCM =
					(Candidate.PositionCM
						- Assist.CenterCM).Length();
				const double CandidateRadialRate =
					Vec3d::DotProduct(
						Candidate.PositionCM - Assist.CenterCM,
						Candidate.VelocityCMPerSec);
				const bool WillExit =
					StartDistanceCM
						< Assist.AssistReferenceRadiusCM
					&& CandidateDistanceCM
						>= Assist.AssistReferenceRadiusCM
					&& CandidateRadialRate > 0.0;
				if (WillExit)
				{
					const double ExitFraction =
						FindSphereBoundaryStepFraction(
							Request,
							AssistIndex,
							CurrentState,
							StepSeconds,
							Assist.CenterCM,
							Assist.AssistReferenceRadiusCM,
							false,
							Config);
					StepSeconds *= ExitFraction;
					Candidate = ConservativeStep(
						Request,
						AssistIndex,
						CurrentState,
						StepSeconds);
				}

				double CollisionAlpha = 1.0;
				if (GravityAssistSolver::SweptSphereFirstHit(
					CurrentState.PositionCM,
					Candidate.PositionCM,
					Assist.CenterCM,
					Assist.CollisionRadiusCM,
					CollisionAlpha))
				{
					return false;
				}

				const Vec3d MidPositionCM = LerpVector(
					CurrentState.PositionCM,
					Candidate.PositionCM,
					0.5);
				const double RawWeight =
					EvaluateOutboundKernel(
						Assist,
						Plan.ClosestDistanceCM,
						MidPositionCM)
					* StepSeconds;
				RawWeightSum += RawWeight;
				const double ProposedEnergy =
					Plan.RequestedEnergyChangeCM2PerSec2
					* RawWeight / NormalizationSeconds;
				const double EnergyStep =
					ClampEncounterEnergyToRemaining(
						ProposedEnergy,
						Plan.RequestedEnergyChangeCM2PerSec2,
						AppliedEnergy);
				if (!ApplySpecificEnergy(
					Candidate.VelocityCMPerSec,
					EnergyStep,
					Config))
				{
					return false;
				}
				AppliedEnergy += EnergyStep;
				CurrentState = Candidate;

				if (WillExit)
				{
					const double RemainingEnergy =
						Plan.RequestedEnergyChangeCM2PerSec2
						- AppliedEnergy;
					if (!ApplySpecificEnergy(
						CurrentState.VelocityCMPerSec,
						RemainingEnergy,
						Config))
					{
						return false;
					}
					Exited = true;
					break;
				}
				if (EndsAtRequestHorizon)
				{
					SetHorizonFailure(CurrentState);
					return false;
				}
			}

			if (!Exited || RawWeightSum <= DoubleSmallNumber)
			{
				return false;
			}
			NormalizationSeconds = RawWeightSum;
		}
		Plan.KernelNormalizationSeconds = NormalizationSeconds;
		return true;
	}
}

double ABTS::M11Core::Internal::EvaluateOutboundKernel(
	const GravityBodySpec& Assist,
	const double ClosestDistanceCM,
	const Vec3d& PositionCM)
{
	const double DistanceCM =
		(PositionCM - Assist.CenterCM).Length();
	const double Progress = Clamp(
		(DistanceCM - ClosestDistanceCM)
			/ Max(
				Assist.AssistReferenceRadiusCM
					- ClosestDistanceCM,
				DoubleSmallNumber),
		0.0,
		1.0);
	return 30.0 * Progress * Progress
		* (1.0 - Progress) * (1.0 - Progress);
}

bool ABTS::M11Core::Internal::BuildNaturalEncounterPlan(
	const TrajectoryRequest& Request,
	const std::int32_t AssistIndex,
	const std::int32_t QualifiedAssistCount,
	const State& EntryState,
	NaturalEncounterPlan& OutPlan)
{
	OutPlan = NaturalEncounterPlan();
	OutPlan.EntryState = EntryState;
	const GravityBodySpec& Assist =
		Request.Scenario.GetAssist(AssistIndex);
	const SolverConfig& Config = Request.Config;

	const double EntryDistanceCM =
		(EntryState.PositionCM - Assist.CenterCM).Length();
	const double VInfinitySquared =
		EntryState.VelocityCMPerSec.SquaredLength()
		- 2.0 * Assist.GravitationalParameterCM3PerSec2
			/ EntryDistanceCM;
	if (!IsFinite(VInfinitySquared)
		|| VInfinitySquared
			<= Config.MinimumVInfinityCMPerSec
				* Config.MinimumVInfinityCMPerSec)
	{
		OutPlan.Failure =
			TrajectoryTermination::AssistInvalidHyperbola;
		OutPlan.FailureEvent =
			TrajectoryEventType::AssistInvalidHyperbola;
		return false;
	}
	OutPlan.VInfinityCMPerSec = Sqrt(VInfinitySquared);

	State CurrentState = EntryState;
	double PreviousRadialRate = Vec3d::DotProduct(
		CurrentState.PositionCM - Assist.CenterCM,
		CurrentState.VelocityCMPerSec);
	if (PreviousRadialRate >= 0.0)
	{
		OutPlan.Failure =
			TrajectoryTermination::AssistInvalidHyperbola;
		OutPlan.FailureEvent =
			TrajectoryEventType::AssistInvalidHyperbola;
		return false;
	}

	bool FoundClosest = false;
	double RawWeightSum = 0.0;
	const double RequestEndTimeSeconds =
		Request.InitialTimeSeconds
		+ Config.MaximumSimulationTimeSeconds;
	const double RequestTimeToleranceSeconds =
		Config.FixedTimeStepSeconds * Config.RootAlphaTolerance;
	for (std::int32_t StepIndex = 0;
		StepIndex < Config.NaturalCloneMaximumStepCount
			&& CurrentState.TimeSeconds - EntryState.TimeSeconds
				< Config.NaturalCloneMaximumTimeSeconds;
		++StepIndex)
	{
		double StepSeconds = 0.0;
		if (!SelectStepSeconds(
			Request,
			AssistIndex,
			CurrentState,
			StepSeconds))
		{
			OutPlan.Failure =
				TrajectoryTermination::AssistSolveFailed;
			OutPlan.FailureEvent =
				TrajectoryEventType::AssistSolveFailed;
			OutPlan.FailureState = CurrentState;
			return false;
		}
		const double RemainingRequestTimeSeconds =
			RequestEndTimeSeconds - CurrentState.TimeSeconds;
		if (RemainingRequestTimeSeconds
			<= RequestTimeToleranceSeconds)
		{
			const bool CentralBound =
				GravityAssistSolver::ComputePrimarySpecificEnergy(
					Request.Scenario.GetPrimary(),
					CurrentState.PositionCM,
					CurrentState.VelocityCMPerSec) < 0.0;
			OutPlan.Failure = CentralBound
				? TrajectoryTermination::SolarCaptured
				: TrajectoryTermination::Timeout;
			OutPlan.FailureEvent = CentralBound
				? TrajectoryEventType::SolarCaptured
				: TrajectoryEventType::Timeout;
			OutPlan.FailureBodyId = CentralBound
				? Request.Scenario.GetPrimary().BodyId
				: InvalidIndex;
			OutPlan.FailureState = CurrentState;
			return false;
		}
		const bool EndsAtRequestHorizon =
			StepSeconds >= RemainingRequestTimeSeconds;
		StepSeconds =
			Min(StepSeconds, RemainingRequestTimeSeconds);
		State Candidate = ConservativeStep(
			Request,
			AssistIndex,
			CurrentState,
			StepSeconds);
		if (!IsFinite(Candidate.TimeSeconds)
			|| !IsFiniteVector(Candidate.PositionCM)
			|| !IsFiniteVector(Candidate.VelocityCMPerSec))
		{
			OutPlan.Failure =
				TrajectoryTermination::AssistSolveFailed;
			OutPlan.FailureEvent =
				TrajectoryEventType::AssistSolveFailed;
			OutPlan.FailureState = CurrentState;
			return false;
		}

		const double CandidateDistanceCM =
			(Candidate.PositionCM - Assist.CenterCM).Length();
		const double CandidateRadialRate = Vec3d::DotProduct(
			Candidate.PositionCM - Assist.CenterCM,
			Candidate.VelocityCMPerSec);

		const bool ClosestInStep =
			!FoundClosest
			&& PreviousRadialRate <= 0.0
			&& CandidateRadialRate >= 0.0;
		const double ClosestAlpha = ClosestInStep
			? FindRadialRootAlpha(
				CurrentState,
				Candidate,
				Assist.CenterCM,
				Config)
			: 2.0;
		if (!FoundClosest)
		{
			const HardHitResult HardHitValue = FindHardHit(
				Request,
				CurrentState,
				Candidate,
				QualifiedAssistCount);
			if (HardHitValue.Type != HardHit::None
				&& HardHitValue.Alpha
					<= ClosestAlpha
						+ Config.RootAlphaTolerance)
			{
				const State HitState = LerpState(
					CurrentState,
					Candidate,
					HardHitValue.Alpha);
				if (HardHitValue.Type == HardHit::Target)
				{
					OutPlan.Failure =
						TrajectoryTermination::TargetHit;
					OutPlan.FailureEvent =
						TrajectoryEventType::TargetHit;
					OutPlan.FailureBodyId =
						Request.Scenario.Target.TargetId;
					OutPlan.FailureState = HitState;
				}
				else
				{
					const GravityBodySpec& HitBody =
						Request.Scenario.Bodies[
							static_cast<std::size_t>(
								HardHitValue.BodyIndex)];
					OutPlan.Failure =
						TrajectoryTermination::BodyCollision;
					OutPlan.FailureEvent =
						TrajectoryEventType::BodyCollision;
					OutPlan.FailureBodyId = HitBody.BodyId;
					OutPlan.FailureAssistIndex =
						HitBody.GetAssistIndex();
					OutPlan.FailureState = HitState;
				}
				return false;
			}
		}

		if (ClosestInStep)
		{
			const double RootFraction = FindRadialStepFraction(
				Request,
				AssistIndex,
				CurrentState,
				StepSeconds,
				Assist.CenterCM,
				Config);
			OutPlan.ClosestState = ConservativeStep(
				Request,
				AssistIndex,
				CurrentState,
				StepSeconds * RootFraction);
			OutPlan.ClosestDistanceCM =
				(OutPlan.ClosestState.PositionCM
					- Assist.CenterCM).Length();
			FoundClosest = true;
			CurrentState = OutPlan.ClosestState;
			PreviousRadialRate = Vec3d::DotProduct(
				CurrentState.PositionCM - Assist.CenterCM,
				CurrentState.VelocityCMPerSec);
			continue;
		}

		if (FoundClosest
			&& PreviousRadialRate > 0.0
			&& CandidateRadialRate < 0.0)
		{
			OutPlan.Failure =
				TrajectoryTermination::PlanetCaptured;
			OutPlan.FailureEvent =
				TrajectoryEventType::PlanetCaptured;
			OutPlan.FailureState = CurrentState;
			return false;
		}

		if (FoundClosest
			&& CandidateDistanceCM
				>= Assist.AssistReferenceRadiusCM
			&& CandidateRadialRate > 0.0)
		{
			SphereRoots Roots;
			if (!SegmentSphereRoots(
				CurrentState.PositionCM,
				Candidate.PositionCM,
				Assist.CenterCM,
				Assist.AssistReferenceRadiusCM,
				Roots))
			{
				OutPlan.Failure =
					TrajectoryTermination::AssistSolveFailed;
				OutPlan.FailureEvent =
					TrajectoryEventType::AssistSolveFailed;
				return false;
			}
			const double ExitFraction =
				FindSphereBoundaryStepFraction(
					Request,
					AssistIndex,
					CurrentState,
					StepSeconds,
					Assist.CenterCM,
					Assist.AssistReferenceRadiusCM,
					false,
					Config);
			StepSeconds *= ExitFraction;
			Candidate = ConservativeStep(
				Request,
				AssistIndex,
				CurrentState,
				StepSeconds);
			const Vec3d MidPositionCM = LerpVector(
				CurrentState.PositionCM,
				Candidate.PositionCM,
				0.5);
			RawWeightSum += EvaluateOutboundKernel(
				Assist,
				OutPlan.ClosestDistanceCM,
				MidPositionCM) * StepSeconds;
			OutPlan.ExitState = Candidate;
			break;
		}

		if (EndsAtRequestHorizon)
		{
			const bool CentralBound =
				GravityAssistSolver::ComputePrimarySpecificEnergy(
					Request.Scenario.GetPrimary(),
					Candidate.PositionCM,
					Candidate.VelocityCMPerSec) < 0.0;
			OutPlan.Failure = CentralBound
				? TrajectoryTermination::SolarCaptured
				: TrajectoryTermination::Timeout;
			OutPlan.FailureEvent = CentralBound
				? TrajectoryEventType::SolarCaptured
				: TrajectoryEventType::Timeout;
			OutPlan.FailureBodyId = CentralBound
				? Request.Scenario.GetPrimary().BodyId
				: InvalidIndex;
			OutPlan.FailureState = Candidate;
			return false;
		}

		if (FoundClosest)
		{
			const Vec3d MidPositionCM = LerpVector(
				CurrentState.PositionCM,
				Candidate.PositionCM,
				0.5);
			RawWeightSum += EvaluateOutboundKernel(
				Assist,
				OutPlan.ClosestDistanceCM,
				MidPositionCM) * StepSeconds;
		}
		CurrentState = Candidate;
		PreviousRadialRate = CandidateRadialRate;
	}

	if (!FoundClosest
		|| OutPlan.ExitState.TimeSeconds
			<= EntryState.TimeSeconds)
	{
		OutPlan.Failure =
			TrajectoryTermination::AssistSolveFailed;
		OutPlan.FailureEvent =
			TrajectoryEventType::AssistSolveFailed;
		OutPlan.FailureState = CurrentState;
		return false;
	}

	Vec3d IncomingDirection;
	Vec3d OutgoingDirection;
	if (!EncounterDetail::FitHyperbolicAsymptoteDirection(
			Assist,
			EntryState,
			true,
			IncomingDirection)
		|| !EncounterDetail::FitHyperbolicAsymptoteDirection(
			Assist,
			OutPlan.ExitState,
			false,
			OutgoingDirection))
	{
		OutPlan.Failure =
			TrajectoryTermination::AssistInvalidHyperbola;
		OutPlan.FailureEvent =
			TrajectoryEventType::AssistInvalidHyperbola;
		return false;
	}

	OutPlan.NaturalDeflectionRadians = Acos(Clamp(
		Vec3d::DotProduct(
			IncomingDirection,
			OutgoingDirection),
		-1.0,
		1.0));
	const double HyperbolicEccentricity =
		1.0
		+ OutPlan.ClosestDistanceCM * VInfinitySquared
			/ Assist.GravitationalParameterCM3PerSec2;
	if (HyperbolicEccentricity <= 1.0)
	{
		OutPlan.Failure =
			TrajectoryTermination::AssistInvalidHyperbola;
		OutPlan.FailureEvent =
			TrajectoryEventType::AssistInvalidHyperbola;
		return false;
	}
	OutPlan.IdealDeflectionRadians =
		2.0 * Asin(Clamp(
			1.0 / HyperbolicEccentricity,
			0.0,
			1.0));
	if (std::abs(
			OutPlan.NaturalDeflectionRadians
				- OutPlan.IdealDeflectionRadians)
		> Config.MaximumNaturalDeflectionErrorRadians)
	{
		OutPlan.Failure =
			TrajectoryTermination::AssistSolveFailed;
		OutPlan.FailureEvent =
			TrajectoryEventType::AssistSolveFailed;
		return false;
	}

	const auto BuildBPlaneAxis =
		[&IncomingDirection](const Vec3d& CandidateAxis)
		{
			return CandidateAxis
				- IncomingDirection
					* Vec3d::DotProduct(
						CandidateAxis,
						IncomingDirection);
		};
	Vec3d TAxis =
		BuildBPlaneAxis(Assist.BPlaneReferenceNormal);
	if (TAxis.Length() < Config.BPlaneBasisMinimumLength)
	{
		TAxis = BuildBPlaneAxis(Assist.BPlaneFallbackAxis);
	}
	if (TAxis.Length() < Config.BPlaneBasisMinimumLength)
	{
		OutPlan.Failure =
			TrajectoryTermination::AssistInvalidBPlaneBasis;
		OutPlan.FailureEvent =
			TrajectoryEventType::AssistInvalidBPlaneBasis;
		return false;
	}
	TAxis.Normalize();
	const Vec3d RAxis =
		Vec3d::CrossProduct(
			IncomingDirection,
			TAxis).GetSafeNormal();
	if (RAxis.SquaredLength() <= DoubleSmallNumber)
	{
		OutPlan.Failure =
			TrajectoryTermination::AssistInvalidBPlaneBasis;
		OutPlan.FailureEvent =
			TrajectoryEventType::AssistInvalidBPlaneBasis;
		return false;
	}

	const Vec3d RelativePositionCM =
		EntryState.PositionCM - Assist.CenterCM;
	const Vec3d SpecificAngularMomentum =
		Vec3d::CrossProduct(
			RelativePositionCM,
			EntryState.VelocityCMPerSec);
	const Vec3d BVectorCM =
		Vec3d::CrossProduct(
			IncomingDirection,
			SpecificAngularMomentum)
		/ OutPlan.VInfinityCMPerSec;
	OutPlan.BPlaneTCM = Vec3d::DotProduct(BVectorCM, TAxis);
	OutPlan.BPlaneRCM = Vec3d::DotProduct(BVectorCM, RAxis);
	OutPlan.BPlaneChiSquared =
		Square(
			(OutPlan.BPlaneTCM - Assist.BPlaneTargetTCM)
				/ Assist.BPlaneSigmaTCM)
		+ Square(
			(OutPlan.BPlaneRCM - Assist.BPlaneTargetRCM)
				/ Assist.BPlaneSigmaRCM);
	OutPlan.CorridorQuality =
		EncounterDetail::ComputeCorridorQuality(
			Assist,
			OutPlan.BPlaneChiSquared);

	const Vec3d IncomingVInfinity =
		IncomingDirection * OutPlan.VInfinityCMPerSec;
	const Vec3d OutgoingVInfinity =
		OutgoingDirection * OutPlan.VInfinityCMPerSec;
	OutPlan.RawEnergyChangeCM2PerSec2 = Vec3d::DotProduct(
		Assist.VirtualOrbitalVelocityCMPerSec,
		OutgoingVInfinity - IncomingVInfinity);
	double ClampedEnergyChange = Clamp(
		OutPlan.RawEnergyChangeCM2PerSec2,
		Assist.MinimumEnergyChangeCM2PerSec2,
		Assist.MaximumEnergyChangeCM2PerSec2);
	if (!EncounterDetail::IsPassSideAllowed(
			Assist.AllowedPassSideValue,
			OutPlan.BPlaneTCM,
			OutPlan.BPlaneRCM)
		&& ClampedEnergyChange > 0.0)
	{
		ClampedEnergyChange = 0.0;
	}
	if (!Config.IsGameplayAssistEnabled(AssistIndex))
	{
		ClampedEnergyChange = 0.0;
	}
	OutPlan.RequestedEnergyChangeCM2PerSec2 =
		Pow(
			OutPlan.CorridorQuality,
			Config.EnergyQualityPower)
		* ClampedEnergyChange;
	OutPlan.NaturalExitPrimaryEnergyCM2PerSec2 =
		GravityAssistSolver::ComputePrimarySpecificEnergy(
			Request.Scenario.GetPrimary(),
			OutPlan.ExitState.PositionCM,
			OutPlan.ExitState.VelocityCMPerSec);

	if (RawWeightSum <= DoubleSmallNumber)
	{
		if (std::abs(OutPlan.RequestedEnergyChangeCM2PerSec2)
			> Config.EnergyRootEpsilonCM2PerSec2)
		{
			OutPlan.Failure =
				TrajectoryTermination::AssistSolveFailed;
			OutPlan.FailureEvent =
				TrajectoryEventType::AssistSolveFailed;
			return false;
		}
		OutPlan.KernelNormalizationSeconds = 1.0;
	}
	else
	{
		OutPlan.KernelNormalizationSeconds = RawWeightSum;
	}
	if (!EncounterDetail::CalibrateKernelNormalization(
		Request,
		AssistIndex,
		OutPlan))
	{
		if (OutPlan.Failure == TrajectoryTermination::None)
		{
			OutPlan.Failure =
				TrajectoryTermination::AssistSolveFailed;
			OutPlan.FailureEvent =
				TrajectoryEventType::AssistSolveFailed;
			OutPlan.FailureState = OutPlan.ClosestState;
		}
		return false;
	}
	return true;
}
