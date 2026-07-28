// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11GravityAssistSolverInternal.h"

namespace ABTSM11GravityAssist
{
	namespace
	{
		double SmoothStep5(const double Value)
		{
			const double X = FMath::Clamp(Value, 0.0, 1.0);
			return X * X * X * (X * (X * 6.0 - 15.0) + 10.0);
		}

		bool IsPassSideAllowed(
			const EABTSM11AllowedPassSide Side,
			const double BPlaneTCM,
			const double BPlaneRCM)
		{
			switch (Side)
			{
			case EABTSM11AllowedPassSide::Any:
				return true;
			case EABTSM11AllowedPassSide::PositiveT:
				return BPlaneTCM > 0.0;
			case EABTSM11AllowedPassSide::NegativeT:
				return BPlaneTCM < 0.0;
			case EABTSM11AllowedPassSide::PositiveR:
				return BPlaneRCM > 0.0;
			case EABTSM11AllowedPassSide::NegativeR:
				return BPlaneRCM < 0.0;
			default:
				return false;
			}
		}

		double CorridorQuality(
			const FABTSM11GravityBodySpec& Assist,
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

		bool FitHyperbolicAsymptoteDirection(
			const FABTSM11GravityBodySpec& Assist,
			const FState& State,
			const bool bIncoming,
			FVector3d& OutDirection)
		{
			const FVector3d RelativePositionCM =
				State.PositionCM - Assist.CenterCM;
			const FVector3d RelativeVelocityCMPerSec = State.VelocityCMPerSec;
			const double RadiusCM = RelativePositionCM.Length();
			const FVector3d AngularMomentum =
				FVector3d::CrossProduct(
					RelativePositionCM, RelativeVelocityCMPerSec);
			const double AngularMomentumLength = AngularMomentum.Length();
			if (RadiusCM <= UE_DOUBLE_SMALL_NUMBER
				|| AngularMomentumLength <= UE_DOUBLE_SMALL_NUMBER)
			{
				return false;
			}

			const FVector3d EccentricityVector =
				FVector3d::CrossProduct(RelativeVelocityCMPerSec, AngularMomentum)
					/ Assist.GravitationalParameterCM3PerSec2
				- RelativePositionCM / RadiusCM;
			const double Eccentricity = EccentricityVector.Length();
			if (!FMath::IsFinite(Eccentricity) || Eccentricity <= 1.0)
			{
				return false;
			}

			const FVector3d PeriapsisAxis = EccentricityVector / Eccentricity;
			const FVector3d NormalAxis = AngularMomentum / AngularMomentumLength;
			const FVector3d TransverseAxis =
				FVector3d::CrossProduct(NormalAxis, PeriapsisAxis).GetSafeNormal();
			if (TransverseAxis.SquaredLength() <= UE_DOUBLE_SMALL_NUMBER)
			{
				return false;
			}

			const double HyperbolicRoot =
				FMath::Sqrt(FMath::Max(0.0, Eccentricity * Eccentricity - 1.0));
			OutDirection = (
				(bIncoming ? PeriapsisAxis : -PeriapsisAxis)
				+ TransverseAxis * HyperbolicRoot) / Eccentricity;
			OutDirection.Normalize();
			return OutDirection.SquaredLength() > UE_DOUBLE_SMALL_NUMBER;
		}

		double ClampToRemainingEnergy(
			const double ProposedEnergy,
			const double RequestedEnergy,
			const double AppliedEnergy)
		{
			const double RemainingEnergy = RequestedEnergy - AppliedEnergy;
			if (RequestedEnergy >= 0.0)
			{
				return FMath::Clamp(ProposedEnergy, 0.0, RemainingEnergy);
			}
			return FMath::Clamp(ProposedEnergy, RemainingEnergy, 0.0);
		}

		bool ApplySpecificEnergy(
			FVector3d& InOutVelocityCMPerSec,
			const double EnergyChangeCM2PerSec2,
			const FABTSM11SolverConfig& Config)
		{
			if (FMath::Abs(EnergyChangeCM2PerSec2) <= UE_DOUBLE_SMALL_NUMBER)
			{
				return true;
			}
			const double SpeedCMPerSec = InOutVelocityCMPerSec.Length();
			double NewSpeedSquared =
				SpeedCMPerSec * SpeedCMPerSec + 2.0 * EnergyChangeCM2PerSec2;
			if (SpeedCMPerSec <= UE_DOUBLE_SMALL_NUMBER
				|| NewSpeedSquared < -Config.EnergyRootEpsilonCM2PerSec2)
			{
				return false;
			}
			NewSpeedSquared = FMath::Max(0.0, NewSpeedSquared);
			InOutVelocityCMPerSec *= FMath::Sqrt(NewSpeedSquared) / SpeedCMPerSec;
			return IsFiniteVector(InOutVelocityCMPerSec);
		}

		bool CalibrateKernelNormalization(
			const FABTSM11TrajectoryRequest& Request,
			const int32 AssistIndex,
			FNaturalEncounterPlan& Plan)
		{
			const FABTSM11SolverConfig& Config = Request.Config;
			const FABTSM11GravityBodySpec& Assist =
				Request.Scenario.GetAssist(AssistIndex);
			if (FMath::Abs(Plan.RequestedEnergyChangeCM2PerSec2)
				<= Config.EnergyRootEpsilonCM2PerSec2)
			{
				return true;
			}
			const double RequestEndTimeSeconds =
				Request.InitialTimeSeconds + Config.MaximumSimulationTimeSeconds;
			const double RequestTimeToleranceSeconds =
				Config.FixedTimeStepSeconds * Config.RootAlphaTolerance;
			auto SetHorizonFailure = [&Request, &Plan](const FState& HorizonState)
			{
				const bool bCentralBound =
					FABTSM11GravityAssistSolver::ComputePrimarySpecificEnergy(
						Request.Scenario.GetPrimary(),
						HorizonState.PositionCM,
						HorizonState.VelocityCMPerSec) < 0.0;
				Plan.Failure = bCentralBound
					? EABTSM11TrajectoryTermination::SolarCaptured
					: EABTSM11TrajectoryTermination::Timeout;
				Plan.FailureEvent = bCentralBound
					? EABTSM11TrajectoryEventType::SolarCaptured
					: EABTSM11TrajectoryEventType::Timeout;
				Plan.FailureBodyId = bCentralBound
					? Request.Scenario.GetPrimary().BodyId
					: INDEX_NONE;
				Plan.FailureState = HorizonState;
			};

			double NormalizationSeconds = Plan.KernelNormalizationSeconds;
			for (int32 Iteration = 0;
				Iteration < Config.EnergyShootingIterationCount;
				++Iteration)
			{
				FState State = Plan.ClosestState;
				double AppliedEnergy = 0.0;
				double RawWeightSum = 0.0;
				bool bExited = false;
				for (int32 StepIndex = 0;
					StepIndex < Config.NaturalCloneMaximumStepCount
						&& State.TimeSeconds - Plan.ClosestState.TimeSeconds
							< Config.NaturalCloneMaximumTimeSeconds;
					++StepIndex)
				{
					double StepSeconds = 0.0;
					if (!SelectStepSeconds(
						Request, AssistIndex, State, StepSeconds))
					{
						return false;
					}
					const double RemainingRequestTimeSeconds =
						RequestEndTimeSeconds - State.TimeSeconds;
					if (RemainingRequestTimeSeconds <= RequestTimeToleranceSeconds)
					{
						SetHorizonFailure(State);
						return false;
					}
					const bool bEndsAtRequestHorizon =
						StepSeconds >= RemainingRequestTimeSeconds;
					StepSeconds =
						FMath::Min(StepSeconds, RemainingRequestTimeSeconds);
					FState Candidate =
						ConservativeStep(Request, AssistIndex, State, StepSeconds);
					if (!IsFiniteVector(Candidate.PositionCM)
						|| !IsFiniteVector(Candidate.VelocityCMPerSec))
					{
						return false;
					}

					const double StartDistanceCM =
						(State.PositionCM - Assist.CenterCM).Length();
					const double CandidateDistanceCM =
						(Candidate.PositionCM - Assist.CenterCM).Length();
					const double CandidateRadialRate = FVector3d::DotProduct(
						Candidate.PositionCM - Assist.CenterCM,
						Candidate.VelocityCMPerSec);
					const bool bWillExit =
						StartDistanceCM < Assist.AssistReferenceRadiusCM
						&& CandidateDistanceCM >= Assist.AssistReferenceRadiusCM
						&& CandidateRadialRate > 0.0;
					if (bWillExit)
					{
						const double ExitFraction = FindSphereBoundaryStepFraction(
							Request,
							AssistIndex,
							State,
							StepSeconds,
							Assist.CenterCM,
							Assist.AssistReferenceRadiusCM,
							false,
							Config);
						StepSeconds *= ExitFraction;
						Candidate =
							ConservativeStep(Request, AssistIndex, State, StepSeconds);
					}

					double CollisionAlpha = 1.0;
					if (FABTSM11GravityAssistSolver::SweptSphereFirstHit(
						State.PositionCM,
						Candidate.PositionCM,
						Assist.CenterCM,
						Assist.CollisionRadiusCM,
						CollisionAlpha))
					{
						return false;
					}

					const FVector3d MidPositionCM =
						LerpVector(State.PositionCM, Candidate.PositionCM, 0.5);
					const double RawWeight =
						EvaluateOutboundKernel(
							Assist, Plan.ClosestDistanceCM, MidPositionCM)
						* StepSeconds;
					RawWeightSum += RawWeight;
					const double ProposedEnergy =
						Plan.RequestedEnergyChangeCM2PerSec2
						* RawWeight / NormalizationSeconds;
					const double EnergyStep = ClampToRemainingEnergy(
						ProposedEnergy,
						Plan.RequestedEnergyChangeCM2PerSec2,
						AppliedEnergy);
					if (!ApplySpecificEnergy(
						Candidate.VelocityCMPerSec, EnergyStep, Config))
					{
						return false;
					}
					AppliedEnergy += EnergyStep;
					State = Candidate;

					if (bWillExit)
					{
						const double RemainingEnergy =
							Plan.RequestedEnergyChangeCM2PerSec2 - AppliedEnergy;
						if (!ApplySpecificEnergy(
							State.VelocityCMPerSec, RemainingEnergy, Config))
						{
							return false;
						}
						bExited = true;
						break;
					}
					if (bEndsAtRequestHorizon)
					{
						SetHorizonFailure(State);
						return false;
					}
				}

				if (!bExited || RawWeightSum <= UE_DOUBLE_SMALL_NUMBER)
				{
					return false;
				}
				NormalizationSeconds = RawWeightSum;
			}
			Plan.KernelNormalizationSeconds = NormalizationSeconds;
			return true;
		}
	}

	double EvaluateOutboundKernel(
		const FABTSM11GravityBodySpec& Assist,
		const double ClosestDistanceCM,
		const FVector3d& PositionCM)
	{
		const double DistanceCM = (PositionCM - Assist.CenterCM).Length();
		const double Progress = FMath::Clamp(
			(DistanceCM - ClosestDistanceCM)
				/ FMath::Max(
					Assist.AssistReferenceRadiusCM - ClosestDistanceCM,
					UE_DOUBLE_SMALL_NUMBER),
			0.0,
			1.0);
		return 30.0 * Progress * Progress
			* (1.0 - Progress) * (1.0 - Progress);
	}

	bool BuildNaturalEncounterPlan(
		const FABTSM11TrajectoryRequest& Request,
		const int32 AssistIndex,
		const FState& EntryState,
		FNaturalEncounterPlan& OutPlan)
	{
		OutPlan = FNaturalEncounterPlan();
		OutPlan.EntryState = EntryState;
		const FABTSM11GravityBodySpec& Assist = Request.Scenario.GetAssist(AssistIndex);
		const FABTSM11SolverConfig& Config = Request.Config;

		const double EntryDistanceCM = (EntryState.PositionCM - Assist.CenterCM).Length();
		const double VInfinitySquared =
			EntryState.VelocityCMPerSec.SquaredLength()
			- 2.0 * Assist.GravitationalParameterCM3PerSec2 / EntryDistanceCM;
		if (!FMath::IsFinite(VInfinitySquared)
			|| VInfinitySquared
			<= Config.MinimumVInfinityCMPerSec * Config.MinimumVInfinityCMPerSec)
		{
			OutPlan.Failure = EABTSM11TrajectoryTermination::AssistInvalidHyperbola;
			OutPlan.FailureEvent = EABTSM11TrajectoryEventType::AssistInvalidHyperbola;
			return false;
		}
		OutPlan.VInfinityCMPerSec = FMath::Sqrt(VInfinitySquared);

		FState State = EntryState;
		double PreviousRadialRate = FVector3d::DotProduct(
			State.PositionCM - Assist.CenterCM, State.VelocityCMPerSec);
		if (PreviousRadialRate >= 0.0)
		{
			OutPlan.Failure = EABTSM11TrajectoryTermination::AssistInvalidHyperbola;
			OutPlan.FailureEvent = EABTSM11TrajectoryEventType::AssistInvalidHyperbola;
			return false;
		}

		bool bFoundClosest = false;
		double RawWeightSum = 0.0;
		const double RequestEndTimeSeconds =
			Request.InitialTimeSeconds + Config.MaximumSimulationTimeSeconds;
		const double RequestTimeToleranceSeconds =
			Config.FixedTimeStepSeconds * Config.RootAlphaTolerance;
		for (int32 StepIndex = 0;
			StepIndex < Config.NaturalCloneMaximumStepCount
				&& State.TimeSeconds - EntryState.TimeSeconds
					< Config.NaturalCloneMaximumTimeSeconds;
			++StepIndex)
		{
			double StepSeconds = 0.0;
			if (!SelectStepSeconds(Request, AssistIndex, State, StepSeconds))
			{
				OutPlan.Failure = EABTSM11TrajectoryTermination::AssistSolveFailed;
				OutPlan.FailureEvent = EABTSM11TrajectoryEventType::AssistSolveFailed;
				OutPlan.FailureState = State;
				return false;
			}
			const double RemainingRequestTimeSeconds =
				RequestEndTimeSeconds - State.TimeSeconds;
			if (RemainingRequestTimeSeconds <= RequestTimeToleranceSeconds)
			{
				const bool bCentralBound =
					FABTSM11GravityAssistSolver::ComputePrimarySpecificEnergy(
						Request.Scenario.GetPrimary(),
						State.PositionCM,
						State.VelocityCMPerSec) < 0.0;
				OutPlan.Failure = bCentralBound
					? EABTSM11TrajectoryTermination::SolarCaptured
					: EABTSM11TrajectoryTermination::Timeout;
				OutPlan.FailureEvent = bCentralBound
					? EABTSM11TrajectoryEventType::SolarCaptured
					: EABTSM11TrajectoryEventType::Timeout;
				OutPlan.FailureBodyId = bCentralBound
					? Request.Scenario.GetPrimary().BodyId
					: INDEX_NONE;
				OutPlan.FailureState = State;
				return false;
			}
			const bool bEndsAtRequestHorizon =
				StepSeconds >= RemainingRequestTimeSeconds;
			StepSeconds = FMath::Min(StepSeconds, RemainingRequestTimeSeconds);
			FState Candidate =
				ConservativeStep(Request, AssistIndex, State, StepSeconds);
			if (!FMath::IsFinite(Candidate.TimeSeconds)
				|| !IsFiniteVector(Candidate.PositionCM)
				|| !IsFiniteVector(Candidate.VelocityCMPerSec))
			{
				OutPlan.Failure = EABTSM11TrajectoryTermination::AssistSolveFailed;
				OutPlan.FailureEvent = EABTSM11TrajectoryEventType::AssistSolveFailed;
				OutPlan.FailureState = State;
				return false;
			}

			const double CandidateDistanceCM =
				(Candidate.PositionCM - Assist.CenterCM).Length();
			const double CandidateRadialRate = FVector3d::DotProduct(
				Candidate.PositionCM - Assist.CenterCM,
				Candidate.VelocityCMPerSec);

			const bool bClosestInStep =
				!bFoundClosest
				&& PreviousRadialRate <= 0.0
				&& CandidateRadialRate >= 0.0;
			const double ClosestAlpha = bClosestInStep
				? FindRadialRootAlpha(State, Candidate, Assist.CenterCM, Config)
				: 2.0;
			if (!bFoundClosest)
			{
				const FHardHitResult HardHit = FindHardHit(Request, State, Candidate);
				if (HardHit.Type != EHardHit::None
					&& HardHit.Alpha
						<= ClosestAlpha + Config.RootAlphaTolerance)
				{
					const FState HitState = LerpState(
						State, Candidate, HardHit.Alpha);
					if (HardHit.Type == EHardHit::Target)
					{
						OutPlan.Failure =
							EABTSM11TrajectoryTermination::TargetHit;
						OutPlan.FailureEvent =
							EABTSM11TrajectoryEventType::TargetHit;
						OutPlan.FailureBodyId =
							Request.Scenario.Target.TargetId;
						OutPlan.FailureState = HitState;
					}
					else
					{
						const FABTSM11GravityBodySpec& HitBody =
							Request.Scenario.Bodies[HardHit.BodyIndex];
						OutPlan.Failure =
							EABTSM11TrajectoryTermination::BodyCollision;
						OutPlan.FailureEvent =
							EABTSM11TrajectoryEventType::BodyCollision;
						OutPlan.FailureBodyId = HitBody.BodyId;
						OutPlan.FailureAssistIndex = HitBody.GetAssistIndex();
						OutPlan.FailureState = HitState;
					}
					return false;
				}
			}

			if (bClosestInStep)
			{
				const double RootFraction = FindRadialStepFraction(
					Request,
					AssistIndex,
					State,
					StepSeconds,
					Assist.CenterCM,
					Config);
				OutPlan.ClosestState = ConservativeStep(
					Request, AssistIndex, State, StepSeconds * RootFraction);
				OutPlan.ClosestDistanceCM =
					(OutPlan.ClosestState.PositionCM - Assist.CenterCM).Length();
				bFoundClosest = true;
				State = OutPlan.ClosestState;
				PreviousRadialRate = FVector3d::DotProduct(
					State.PositionCM - Assist.CenterCM,
					State.VelocityCMPerSec);
				continue;
			}

			if (bFoundClosest
				&& PreviousRadialRate > 0.0
				&& CandidateRadialRate < 0.0)
			{
				OutPlan.Failure = EABTSM11TrajectoryTermination::PlanetCaptured;
				OutPlan.FailureEvent = EABTSM11TrajectoryEventType::PlanetCaptured;
				OutPlan.FailureState = State;
				return false;
			}

			if (bFoundClosest
				&& CandidateDistanceCM >= Assist.AssistReferenceRadiusCM
				&& CandidateRadialRate > 0.0)
			{
				FSphereRoots Roots;
				if (!SegmentSphereRoots(
					State.PositionCM,
					Candidate.PositionCM,
					Assist.CenterCM,
					Assist.AssistReferenceRadiusCM,
					Roots))
				{
					OutPlan.Failure = EABTSM11TrajectoryTermination::AssistSolveFailed;
					OutPlan.FailureEvent =
						EABTSM11TrajectoryEventType::AssistSolveFailed;
					return false;
				}
				const double ExitFraction = FindSphereBoundaryStepFraction(
					Request,
					AssistIndex,
					State,
					StepSeconds,
					Assist.CenterCM,
					Assist.AssistReferenceRadiusCM,
					false,
					Config);
				StepSeconds *= ExitFraction;
				Candidate =
					ConservativeStep(Request, AssistIndex, State, StepSeconds);
				const FVector3d MidPositionCM =
					LerpVector(State.PositionCM, Candidate.PositionCM, 0.5);
				RawWeightSum += EvaluateOutboundKernel(
					Assist, OutPlan.ClosestDistanceCM, MidPositionCM) * StepSeconds;
				OutPlan.ExitState = Candidate;
				break;
			}

			if (bEndsAtRequestHorizon)
			{
				const bool bCentralBound =
					FABTSM11GravityAssistSolver::ComputePrimarySpecificEnergy(
						Request.Scenario.GetPrimary(),
						Candidate.PositionCM,
						Candidate.VelocityCMPerSec) < 0.0;
				OutPlan.Failure = bCentralBound
					? EABTSM11TrajectoryTermination::SolarCaptured
					: EABTSM11TrajectoryTermination::Timeout;
				OutPlan.FailureEvent = bCentralBound
					? EABTSM11TrajectoryEventType::SolarCaptured
					: EABTSM11TrajectoryEventType::Timeout;
				OutPlan.FailureBodyId = bCentralBound
					? Request.Scenario.GetPrimary().BodyId
					: INDEX_NONE;
				OutPlan.FailureState = Candidate;
				return false;
			}

			if (bFoundClosest)
			{
				const FVector3d MidPositionCM =
					LerpVector(State.PositionCM, Candidate.PositionCM, 0.5);
				RawWeightSum += EvaluateOutboundKernel(
					Assist, OutPlan.ClosestDistanceCM, MidPositionCM) * StepSeconds;
			}
			State = Candidate;
			PreviousRadialRate = CandidateRadialRate;
		}

		if (!bFoundClosest || OutPlan.ExitState.TimeSeconds <= EntryState.TimeSeconds)
		{
			OutPlan.Failure = EABTSM11TrajectoryTermination::AssistSolveFailed;
			OutPlan.FailureEvent = EABTSM11TrajectoryEventType::AssistSolveFailed;
			OutPlan.FailureState = State;
			return false;
		}

		FVector3d IncomingDirection = FVector3d::ZeroVector;
		FVector3d OutgoingDirection = FVector3d::ZeroVector;
		if (!FitHyperbolicAsymptoteDirection(
				Assist, EntryState, true, IncomingDirection)
			|| !FitHyperbolicAsymptoteDirection(
				Assist, OutPlan.ExitState, false, OutgoingDirection))
		{
			OutPlan.Failure = EABTSM11TrajectoryTermination::AssistInvalidHyperbola;
			OutPlan.FailureEvent = EABTSM11TrajectoryEventType::AssistInvalidHyperbola;
			return false;
		}

		OutPlan.NaturalDeflectionRadians = FMath::Acos(FMath::Clamp(
			FVector3d::DotProduct(IncomingDirection, OutgoingDirection), -1.0, 1.0));
		const double HyperbolicEccentricity =
			1.0
			+ OutPlan.ClosestDistanceCM * VInfinitySquared
				/ Assist.GravitationalParameterCM3PerSec2;
		if (HyperbolicEccentricity <= 1.0)
		{
			OutPlan.Failure = EABTSM11TrajectoryTermination::AssistInvalidHyperbola;
			OutPlan.FailureEvent = EABTSM11TrajectoryEventType::AssistInvalidHyperbola;
			return false;
		}
		OutPlan.IdealDeflectionRadians =
			2.0 * FMath::Asin(FMath::Clamp(1.0 / HyperbolicEccentricity, 0.0, 1.0));
		if (FMath::Abs(
			OutPlan.NaturalDeflectionRadians - OutPlan.IdealDeflectionRadians)
			> Config.MaximumNaturalDeflectionErrorRadians)
		{
			OutPlan.Failure = EABTSM11TrajectoryTermination::AssistSolveFailed;
			OutPlan.FailureEvent = EABTSM11TrajectoryEventType::AssistSolveFailed;
			return false;
		}

		auto BuildBPlaneAxis = [&IncomingDirection](const FVector3d& CandidateAxis)
		{
			return CandidateAxis
				- IncomingDirection
					* FVector3d::DotProduct(CandidateAxis, IncomingDirection);
		};
		FVector3d TAxis = BuildBPlaneAxis(Assist.BPlaneReferenceNormal);
		if (TAxis.Length() < Config.BPlaneBasisMinimumLength)
		{
			TAxis = BuildBPlaneAxis(Assist.BPlaneFallbackAxis);
		}
		if (TAxis.Length() < Config.BPlaneBasisMinimumLength)
		{
			OutPlan.Failure = EABTSM11TrajectoryTermination::AssistInvalidBPlaneBasis;
			OutPlan.FailureEvent =
				EABTSM11TrajectoryEventType::AssistInvalidBPlaneBasis;
			return false;
		}
		TAxis.Normalize();
		const FVector3d RAxis =
			FVector3d::CrossProduct(IncomingDirection, TAxis).GetSafeNormal();
		if (RAxis.SquaredLength() <= UE_DOUBLE_SMALL_NUMBER)
		{
			OutPlan.Failure = EABTSM11TrajectoryTermination::AssistInvalidBPlaneBasis;
			OutPlan.FailureEvent =
				EABTSM11TrajectoryEventType::AssistInvalidBPlaneBasis;
			return false;
		}

		const FVector3d RelativePositionCM =
			EntryState.PositionCM - Assist.CenterCM;
		const FVector3d SpecificAngularMomentum =
			FVector3d::CrossProduct(RelativePositionCM, EntryState.VelocityCMPerSec);
		const FVector3d BVectorCM =
			FVector3d::CrossProduct(IncomingDirection, SpecificAngularMomentum)
			/ OutPlan.VInfinityCMPerSec;
		OutPlan.BPlaneTCM = FVector3d::DotProduct(BVectorCM, TAxis);
		OutPlan.BPlaneRCM = FVector3d::DotProduct(BVectorCM, RAxis);
		OutPlan.BPlaneChiSquared =
			FMath::Square(
				(OutPlan.BPlaneTCM - Assist.BPlaneTargetTCM) / Assist.BPlaneSigmaTCM)
			+ FMath::Square(
				(OutPlan.BPlaneRCM - Assist.BPlaneTargetRCM) / Assist.BPlaneSigmaRCM);
		OutPlan.CorridorQuality =
			CorridorQuality(Assist, OutPlan.BPlaneChiSquared);

		const FVector3d IncomingVInfinity =
			IncomingDirection * OutPlan.VInfinityCMPerSec;
		const FVector3d OutgoingVInfinity =
			OutgoingDirection * OutPlan.VInfinityCMPerSec;
		OutPlan.RawEnergyChangeCM2PerSec2 = FVector3d::DotProduct(
			Assist.VirtualOrbitalVelocityCMPerSec,
			OutgoingVInfinity - IncomingVInfinity);
		double ClampedEnergyChange = FMath::Clamp(
			OutPlan.RawEnergyChangeCM2PerSec2,
			Assist.MinimumEnergyChangeCM2PerSec2,
			Assist.MaximumEnergyChangeCM2PerSec2);
		if (!IsPassSideAllowed(
			Assist.AllowedPassSide, OutPlan.BPlaneTCM, OutPlan.BPlaneRCM)
			&& ClampedEnergyChange > 0.0)
		{
			ClampedEnergyChange = 0.0;
		}
		if (!Config.IsGameplayAssistEnabled(AssistIndex))
		{
			ClampedEnergyChange = 0.0;
		}
		OutPlan.RequestedEnergyChangeCM2PerSec2 =
			FMath::Pow(OutPlan.CorridorQuality, Config.EnergyQualityPower)
			* ClampedEnergyChange;
		OutPlan.NaturalExitPrimaryEnergyCM2PerSec2 =
			FABTSM11GravityAssistSolver::ComputePrimarySpecificEnergy(
				Request.Scenario.GetPrimary(),
				OutPlan.ExitState.PositionCM,
				OutPlan.ExitState.VelocityCMPerSec);

		if (RawWeightSum <= UE_DOUBLE_SMALL_NUMBER)
		{
			if (FMath::Abs(OutPlan.RequestedEnergyChangeCM2PerSec2)
				> Config.EnergyRootEpsilonCM2PerSec2)
			{
				OutPlan.Failure = EABTSM11TrajectoryTermination::AssistSolveFailed;
				OutPlan.FailureEvent = EABTSM11TrajectoryEventType::AssistSolveFailed;
				return false;
			}
			OutPlan.KernelNormalizationSeconds = 1.0;
		}
		else
		{
			OutPlan.KernelNormalizationSeconds = RawWeightSum;
		}
		if (!CalibrateKernelNormalization(Request, AssistIndex, OutPlan))
		{
			if (OutPlan.Failure == EABTSM11TrajectoryTermination::None)
			{
				OutPlan.Failure = EABTSM11TrajectoryTermination::AssistSolveFailed;
				OutPlan.FailureEvent =
					EABTSM11TrajectoryEventType::AssistSolveFailed;
				OutPlan.FailureState = OutPlan.ClosestState;
			}
			return false;
		}
		return true;
	}
}
