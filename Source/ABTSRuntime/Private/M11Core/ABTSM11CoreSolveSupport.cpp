// Copyright Epic Games, Inc. All Rights Reserved.

#include "M11Core/ABTSM11CoreInternal.h"

void ABTS::M11Core::Internal::FillPlanDiagnostics(
	const NaturalEncounterPlan& Plan,
	TrajectoryEvent& Event)
{
	Event.EntrySpeedCMPerSec =
		Plan.EntryState.VelocityCMPerSec.Length();
	Event.ExitSpeedCMPerSec =
		Plan.ExitState.VelocityCMPerSec.Length();
	Event.ClosestDistanceCM = Plan.ClosestDistanceCM;
	Event.BPlaneTCM = Plan.BPlaneTCM;
	Event.BPlaneRCM = Plan.BPlaneRCM;
	Event.BPlaneChiSquared = Plan.BPlaneChiSquared;
	Event.CorridorQuality = Plan.CorridorQuality;
	Event.NaturalDeflectionRadians =
		Plan.NaturalDeflectionRadians;
	Event.IdealDeflectionRadians = Plan.IdealDeflectionRadians;
	Event.RawEnergyChangeCM2PerSec2 =
		Plan.RawEnergyChangeCM2PerSec2;
	Event.RequestedEnergyChangeCM2PerSec2 =
		Plan.RequestedEnergyChangeCM2PerSec2;
}

ABTS::M11Core::TrajectoryEvent
ABTS::M11Core::Internal::MakeEvent(
	const TrajectoryEventType Type,
	const std::int32_t BodyId,
	const std::int32_t AssistIndex,
	const State& CurrentState)
{
	TrajectoryEvent Event;
	Event.Type = Type;
	Event.BodyId = BodyId;
	Event.AssistIndex = AssistIndex;
	Event.TimeSeconds = CurrentState.TimeSeconds;
	Event.PositionCM = CurrentState.PositionCM;
	Event.VelocityCMPerSec = CurrentState.VelocityCMPerSec;
	return Event;
}

ABTS::M11Core::TrajectoryPoint
ABTS::M11Core::Internal::MakePoint(
	const GravityBodySpec& Primary,
	const State& CurrentState)
{
	TrajectoryPoint Point;
	Point.TimeSeconds = CurrentState.TimeSeconds;
	Point.PositionCM = CurrentState.PositionCM;
	Point.VelocityCMPerSec = CurrentState.VelocityCMPerSec;
	Point.PrimarySpecificEnergyCM2PerSec2 =
		GravityAssistSolver::ComputePrimarySpecificEnergy(
			Primary,
			CurrentState.PositionCM,
			CurrentState.VelocityCMPerSec);
	return Point;
}

bool ABTS::M11Core::Internal::ApplyEnergyKick(
	Vec3d& InOutVelocityCMPerSec,
	const double EnergyChangeCM2PerSec2,
	const SolverConfig& Config)
{
	if (std::abs(EnergyChangeCM2PerSec2) <= DoubleSmallNumber)
	{
		return true;
	}
	const double SpeedCMPerSec = InOutVelocityCMPerSec.Length();
	if (SpeedCMPerSec <= DoubleSmallNumber)
	{
		return false;
	}
	double NewSpeedSquared =
		SpeedCMPerSec * SpeedCMPerSec
		+ 2.0 * EnergyChangeCM2PerSec2;
	if (NewSpeedSquared < -Config.EnergyRootEpsilonCM2PerSec2)
	{
		return false;
	}
	NewSpeedSquared = Max(0.0, NewSpeedSquared);
	InOutVelocityCMPerSec *=
		Sqrt(NewSpeedSquared) / SpeedCMPerSec;
	return IsFiniteVector(InOutVelocityCMPerSec);
}

double ABTS::M11Core::Internal::ClampToRemainingEnergy(
	const double ProposedEnergy,
	const double RequestedEnergy,
	const double AppliedEnergy)
{
	const double RemainingEnergy = RequestedEnergy - AppliedEnergy;
	return RequestedEnergy >= 0.0
		? Clamp(ProposedEnergy, 0.0, RemainingEnergy)
		: Clamp(ProposedEnergy, RemainingEnergy, 0.0);
}

ABTS::M11Core::Internal::HardHitResult
ABTS::M11Core::Internal::FindHardHit(
	const TrajectoryRequest& Request,
	const State& Start,
	const State& End,
	const std::int32_t QualifiedAssistCount)
{
	HardHitResult Result;
	for (std::int32_t BodyIndex = 0;
		BodyIndex < GravityScenario::BodyCount;
		++BodyIndex)
	{
		double Alpha = 1.0;
		const GravityBodySpec& Body =
			Request.Scenario.Bodies[
				static_cast<std::size_t>(BodyIndex)];
		if (GravityAssistSolver::SweptSphereFirstHit(
				Start.PositionCM,
				End.PositionCM,
				Body.CenterCM,
				Body.CollisionRadiusCM,
				Alpha)
			&& (Result.Type == HardHit::None
				|| Alpha < Result.Alpha))
		{
			Result.Type = HardHit::Body;
			Result.Alpha = Alpha;
			Result.BodyIndex = BodyIndex;
		}
	}

	double TargetContactAlpha = 1.0;
	if (GravityAssistSolver::SweptSphereFirstHit(
			Start.PositionCM,
			End.PositionCM,
			Request.Scenario.Target.GetGeometricContactCenterCM(),
			Request.Scenario.Target.GetGeometricContactRadiusCM(),
			TargetContactAlpha))
	{
		Result.HasTargetContact = true;
		Result.TargetContactAlpha = TargetContactAlpha;
	}
	double QualifiedHitAlpha = 1.0;
	if (QualifiedAssistCount
			>= Request.Scenario.Target.RequiredQualifiedAssistCount
		&& GravityAssistSolver::SweptSphereFirstHit(
			Start.PositionCM,
			End.PositionCM,
			Request.Scenario.Target.CenterCM,
			Request.Scenario.Target.HitRadiusCM,
			QualifiedHitAlpha)
		&& (Result.Type == HardHit::None
			|| QualifiedHitAlpha
				< Result.Alpha
					- Request.Config.RootAlphaTolerance))
	{
		Result.Type = HardHit::Target;
		Result.Alpha = QualifiedHitAlpha;
		Result.BodyIndex = InvalidIndex;
	}
	return Result;
}

bool ABTS::M11Core::Internal::AssistExitQualifiesTarget(
	const TrajectoryRequest& Request,
	const TrajectoryEvent& ExitEvent)
{
	if (ExitEvent.AssistIndex < 1
		|| ExitEvent.AssistIndex > GravityScenario::AssistCount
		|| !Request.Config.IsGameplayAssistEnabled(
			ExitEvent.AssistIndex)
		|| ExitEvent.CorridorQuality
			< Request.Scenario.Target
				.MinimumQualifyingCorridorQuality
		|| ExitEvent.AppliedEnergyChangeCM2PerSec2
			< Request.Scenario.Target
				.MinimumQualifyingEnergyGainCM2PerSec2)
	{
		return false;
	}
	if (!Request.Scenario.Target.RequireAllowedPassSide)
	{
		return true;
	}
	const GravityBodySpec& Body =
		Request.Scenario.GetAssist(ExitEvent.AssistIndex);
	switch (Body.AllowedPassSideValue)
	{
	case AllowedPassSide::PositiveT:
		return ExitEvent.BPlaneTCM > 0.0;
	case AllowedPassSide::NegativeT:
		return ExitEvent.BPlaneTCM < 0.0;
	case AllowedPassSide::PositiveR:
		return ExitEvent.BPlaneRCM > 0.0;
	case AllowedPassSide::NegativeR:
		return ExitEvent.BPlaneRCM < 0.0;
	case AllowedPassSide::Any:
		return true;
	default:
		return false;
	}
}

void ABTS::M11Core::Internal::FinalizeResult(
	const TrajectoryRequest& Request,
	TrajectoryResult& Result,
	const TrajectoryTermination Termination,
	const char* Diagnostic)
{
	Result.Termination = Termination;
	Result.Diagnostic = Diagnostic != nullptr ? Diagnostic : "";
	Result.ValidationHash = ComputeResultHash(Request, Result);
}

void ABTS::M11Core::Internal::FinalizeHardHit(
	const TrajectoryRequest& Request,
	const GravityBodySpec& Primary,
	const State& Start,
	const State& End,
	const HardHitResult& HardHitValue,
	TrajectoryResult& Result)
{
	const State HitState =
		LerpState(Start, End, HardHitValue.Alpha);
	Result.Points.push_back(MakePoint(Primary, HitState));
	if (HardHitValue.Type == HardHit::Body)
	{
		const GravityBodySpec& HitBody =
			Request.Scenario.Bodies[
				static_cast<std::size_t>(
					HardHitValue.BodyIndex)];
		Result.Events.push_back(MakeEvent(
			TrajectoryEventType::BodyCollision,
			HitBody.BodyId,
			HitBody.GetAssistIndex(),
			HitState));
		FinalizeResult(
			Request,
			Result,
			TrajectoryTermination::BodyCollision,
			"BodyCollision");
	}
	else
	{
		if (HardHitValue.HasTargetContact
			&& HardHitValue.TargetContactAlpha
				<= HardHitValue.Alpha
					+ Request.Config.RootAlphaTolerance)
		{
			++Result.TargetContactCount;
		}
		Result.Events.push_back(MakeEvent(
			TrajectoryEventType::TargetHit,
			Request.Scenario.Target.TargetId,
			0,
			HitState));
		FinalizeResult(
			Request,
			Result,
			TrajectoryTermination::TargetHit,
			"TargetHit");
	}
}

void ABTS::M11Core::Internal::FinalizeAssistFailure(
	const TrajectoryRequest& Request,
	const GravityBodySpec& Primary,
	const State& FailureState,
	const GravityBodySpec* Assist,
	const ActiveEncounter* Encounter,
	const TrajectoryTermination Termination,
	const TrajectoryEventType EventType,
	const char* Diagnostic,
	TrajectoryResult& Result)
{
	Result.Points.push_back(MakePoint(Primary, FailureState));
	TrajectoryEvent Event = MakeEvent(
		EventType,
		Assist != nullptr ? Assist->BodyId : InvalidIndex,
		Encounter != nullptr ? Encounter->AssistIndex : 0,
		FailureState);
	if (Encounter != nullptr && Encounter->PlanReady)
	{
		FillPlanDiagnostics(Encounter->Plan, Event);
		Event.AppliedEnergyChangeCM2PerSec2 =
			Encounter->AppliedEnergyChangeCM2PerSec2;
	}
	Result.Events.push_back(Event);
	FinalizeResult(Request, Result, Termination, Diagnostic);
}

double ABTS::M11Core::Internal::ExactSphereBoundaryFraction(
	const TrajectoryRequest& Request,
	const std::int32_t ExpectedAssistIndex,
	const State& CurrentState,
	const double FullStepSeconds,
	const Vec3d& CenterCM,
	const double RadiusCM,
	const bool Entering,
	const SphereRoots& SegmentRoots)
{
	double SearchFraction = 1.0;
	if (Entering && !SegmentRoots.StartsInside)
	{
		SearchFraction = Clamp(
			0.5 * (
				SegmentRoots.EnterAlpha
				+ SegmentRoots.ExitAlpha),
			SegmentRoots.EnterAlpha,
			1.0);
	}
	return SearchFraction * FindSphereBoundaryStepFraction(
		Request,
		ExpectedAssistIndex,
		CurrentState,
		FullStepSeconds * SearchFraction,
		CenterCM,
		RadiusCM,
		Entering,
		Request.Config);
}

double ABTS::M11Core::GravityAssistSolver::ComputePrimarySpecificEnergy(
	const GravityBodySpec& Primary,
	const Vec3d& PositionCM,
	const Vec3d& VelocityCMPerSec)
{
	const double RadiusCM = Max(
		(PositionCM - Primary.CenterCM).Length(),
		Primary.MinimumEvaluationRadiusCM);
	return 0.5 * VelocityCMPerSec.SquaredLength()
		- Primary.GravitationalParameterCM3PerSec2 / RadiusCM;
}

bool ABTS::M11Core::GravityAssistSolver::SweptSphereFirstHit(
	const Vec3d& SegmentStartCM,
	const Vec3d& SegmentEndCM,
	const Vec3d& SphereCenterCM,
	const double SphereRadiusCM,
	double& OutAlpha)
{
	Internal::SphereRoots Roots;
	if (!IsFinite(SphereRadiusCM)
		|| SphereRadiusCM <= 0.0
		|| !Internal::IsFiniteVector(SegmentStartCM)
		|| !Internal::IsFiniteVector(SegmentEndCM)
		|| !Internal::IsFiniteVector(SphereCenterCM)
		|| !Internal::SegmentSphereRoots(
			SegmentStartCM,
			SegmentEndCM,
			SphereCenterCM,
			SphereRadiusCM,
			Roots))
	{
		return false;
	}
	OutAlpha = Roots.EnterAlpha;
	return true;
}
