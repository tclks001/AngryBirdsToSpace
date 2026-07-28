// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11GravityAssistSolverInternal.h"

namespace ABTSM11GravityAssist
{
	void FillPlanDiagnostics(
		const FNaturalEncounterPlan& Plan,
		FABTSM11TrajectoryEvent& Event)
	{
		Event.EntrySpeedCMPerSec = Plan.EntryState.VelocityCMPerSec.Length();
		Event.ExitSpeedCMPerSec = Plan.ExitState.VelocityCMPerSec.Length();
		Event.ClosestDistanceCM = Plan.ClosestDistanceCM;
		Event.BPlaneTCM = Plan.BPlaneTCM;
		Event.BPlaneRCM = Plan.BPlaneRCM;
		Event.BPlaneChiSquared = Plan.BPlaneChiSquared;
		Event.CorridorQuality = Plan.CorridorQuality;
		Event.NaturalDeflectionRadians = Plan.NaturalDeflectionRadians;
		Event.IdealDeflectionRadians = Plan.IdealDeflectionRadians;
		Event.RawEnergyChangeCM2PerSec2 = Plan.RawEnergyChangeCM2PerSec2;
		Event.RequestedEnergyChangeCM2PerSec2 =
			Plan.RequestedEnergyChangeCM2PerSec2;
	}

	FABTSM11TrajectoryEvent MakeEvent(
		const EABTSM11TrajectoryEventType Type,
		const int32 BodyId,
		const int32 AssistIndex,
		const FState& State)
	{
		FABTSM11TrajectoryEvent Event;
		Event.Type = Type;
		Event.BodyId = BodyId;
		Event.AssistIndex = AssistIndex;
		Event.TimeSeconds = State.TimeSeconds;
		Event.PositionCM = State.PositionCM;
		Event.VelocityCMPerSec = State.VelocityCMPerSec;
		return Event;
	}

	FABTSM11TrajectoryPoint MakePoint(
		const FABTSM11GravityBodySpec& Primary,
		const FState& State)
	{
		FABTSM11TrajectoryPoint Point;
		Point.TimeSeconds = State.TimeSeconds;
		Point.PositionCM = State.PositionCM;
		Point.VelocityCMPerSec = State.VelocityCMPerSec;
		Point.PrimarySpecificEnergyCM2PerSec2 =
			FABTSM11GravityAssistSolver::ComputePrimarySpecificEnergy(
				Primary, State.PositionCM, State.VelocityCMPerSec);
		return Point;
	}

	bool ApplyEnergyKick(
		FVector3d& InOutVelocityCMPerSec,
		const double EnergyChangeCM2PerSec2,
		const FABTSM11SolverConfig& Config)
	{
		if (FMath::Abs(EnergyChangeCM2PerSec2) <= UE_DOUBLE_SMALL_NUMBER)
		{
			return true;
		}
		const double SpeedCMPerSec = InOutVelocityCMPerSec.Length();
		if (SpeedCMPerSec <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}
		double NewSpeedSquared =
			SpeedCMPerSec * SpeedCMPerSec + 2.0 * EnergyChangeCM2PerSec2;
		if (NewSpeedSquared < -Config.EnergyRootEpsilonCM2PerSec2)
		{
			return false;
		}
		NewSpeedSquared = FMath::Max(0.0, NewSpeedSquared);
		InOutVelocityCMPerSec *= FMath::Sqrt(NewSpeedSquared) / SpeedCMPerSec;
		return IsFiniteVector(InOutVelocityCMPerSec);
	}

	double ClampToRemainingEnergy(
		const double ProposedEnergy,
		const double RequestedEnergy,
		const double AppliedEnergy)
	{
		const double RemainingEnergy = RequestedEnergy - AppliedEnergy;
		return RequestedEnergy >= 0.0
			? FMath::Clamp(ProposedEnergy, 0.0, RemainingEnergy)
			: FMath::Clamp(ProposedEnergy, RemainingEnergy, 0.0);
	}

	FHardHitResult FindHardHit(
		const FABTSM11TrajectoryRequest& Request,
		const FState& Start,
		const FState& End,
		const int32 QualifiedAssistCount)
	{
		FHardHitResult Result;
		for (int32 BodyIndex = 0;
			BodyIndex < FABTSM11GravityScenario::BodyCount;
			++BodyIndex)
		{
			double Alpha = 1.0;
			const FABTSM11GravityBodySpec& Body = Request.Scenario.Bodies[BodyIndex];
			if (FABTSM11GravityAssistSolver::SweptSphereFirstHit(
				Start.PositionCM,
				End.PositionCM,
				Body.CenterCM,
				Body.CollisionRadiusCM,
				Alpha)
				&& (Result.Type == EHardHit::None || Alpha < Result.Alpha))
			{
				Result.Type = EHardHit::Body;
				Result.Alpha = Alpha;
				Result.BodyIndex = BodyIndex;
			}
		}

		double TargetContactAlpha = 1.0;
		if (FABTSM11GravityAssistSolver::SweptSphereFirstHit(
			Start.PositionCM,
			End.PositionCM,
			Request.Scenario.Target.GetGeometricContactCenterCM(),
			Request.Scenario.Target.GetGeometricContactRadiusCM(),
			TargetContactAlpha))
		{
			Result.bHasTargetContact = true;
			Result.TargetContactAlpha = TargetContactAlpha;
		}
		double QualifiedHitAlpha = 1.0;
		if (QualifiedAssistCount
				>= Request.Scenario.Target.RequiredQualifiedAssistCount
			&& FABTSM11GravityAssistSolver::SweptSphereFirstHit(
				Start.PositionCM,
				End.PositionCM,
				Request.Scenario.Target.CenterCM,
				Request.Scenario.Target.HitRadiusCM,
				QualifiedHitAlpha)
			&& (Result.Type == EHardHit::None
				|| QualifiedHitAlpha
					< Result.Alpha - Request.Config.RootAlphaTolerance))
		{
			Result.Type = EHardHit::Target;
			Result.Alpha = QualifiedHitAlpha;
			Result.BodyIndex = INDEX_NONE;
		}
		return Result;
	}

	bool AssistExitQualifiesTarget(
		const FABTSM11TrajectoryRequest& Request,
		const FABTSM11TrajectoryEvent& ExitEvent)
	{
		if (ExitEvent.AssistIndex < 1
			|| ExitEvent.AssistIndex > FABTSM11GravityScenario::AssistCount
			|| !Request.Config.IsGameplayAssistEnabled(
				ExitEvent.AssistIndex)
			|| ExitEvent.CorridorQuality
				< Request.Scenario.Target.MinimumQualifyingCorridorQuality
			|| ExitEvent.AppliedEnergyChangeCM2PerSec2
				< Request.Scenario.Target
					.MinimumQualifyingEnergyGainCM2PerSec2)
		{
			return false;
		}
		if (!Request.Scenario.Target.bRequireAllowedPassSide)
		{
			return true;
		}
		const FABTSM11GravityBodySpec& Body =
			Request.Scenario.GetAssist(ExitEvent.AssistIndex);
		switch (Body.AllowedPassSide)
		{
		case EABTSM11AllowedPassSide::PositiveT:
			return ExitEvent.BPlaneTCM > 0.0;
		case EABTSM11AllowedPassSide::NegativeT:
			return ExitEvent.BPlaneTCM < 0.0;
		case EABTSM11AllowedPassSide::PositiveR:
			return ExitEvent.BPlaneRCM > 0.0;
		case EABTSM11AllowedPassSide::NegativeR:
			return ExitEvent.BPlaneRCM < 0.0;
		case EABTSM11AllowedPassSide::Any:
			return true;
		default:
			return false;
		}
	}

	void FinalizeResult(
		const FABTSM11TrajectoryRequest& Request,
		FABTSM11TrajectoryResult& Result,
		const EABTSM11TrajectoryTermination Termination,
		const TCHAR* Diagnostic)
	{
		Result.Termination = Termination;
		Result.Diagnostic = Diagnostic;
		Result.ValidationHash = ComputeResultHash(Request, Result);
	}

	void FinalizeHardHit(
		const FABTSM11TrajectoryRequest& Request,
		const FABTSM11GravityBodySpec& Primary,
		const FState& Start,
		const FState& End,
		const FHardHitResult& HardHit,
		FABTSM11TrajectoryResult& Result)
	{
		const FState HitState = LerpState(Start, End, HardHit.Alpha);
		Result.Points.Add(MakePoint(Primary, HitState));
		if (HardHit.Type == EHardHit::Body)
		{
			const FABTSM11GravityBodySpec& HitBody =
				Request.Scenario.Bodies[HardHit.BodyIndex];
			Result.Events.Add(MakeEvent(
				EABTSM11TrajectoryEventType::BodyCollision,
				HitBody.BodyId,
				HitBody.GetAssistIndex(),
				HitState));
			FinalizeResult(
				Request,
				Result,
				EABTSM11TrajectoryTermination::BodyCollision,
				TEXT("BodyCollision"));
		}
		else
		{
			if (HardHit.bHasTargetContact
				&& HardHit.TargetContactAlpha
					<= HardHit.Alpha
						+ Request.Config.RootAlphaTolerance)
			{
				++Result.TargetContactCount;
			}
			Result.Events.Add(MakeEvent(
				EABTSM11TrajectoryEventType::TargetHit,
				Request.Scenario.Target.TargetId,
				0,
				HitState));
			FinalizeResult(
				Request,
				Result,
				EABTSM11TrajectoryTermination::TargetHit,
				TEXT("TargetHit"));
		}
	}

	void FinalizeAssistFailure(
		const FABTSM11TrajectoryRequest& Request,
		const FABTSM11GravityBodySpec& Primary,
		const FState& FailureState,
		const FABTSM11GravityBodySpec* Assist,
		const FActiveEncounter* Encounter,
		const EABTSM11TrajectoryTermination Termination,
		const EABTSM11TrajectoryEventType EventType,
		const TCHAR* Diagnostic,
		FABTSM11TrajectoryResult& Result)
	{
		Result.Points.Add(MakePoint(Primary, FailureState));
		FABTSM11TrajectoryEvent Event = MakeEvent(
			EventType,
			Assist != nullptr ? Assist->BodyId : INDEX_NONE,
			Encounter != nullptr ? Encounter->AssistIndex : 0,
			FailureState);
		if (Encounter != nullptr && Encounter->bPlanReady)
		{
			FillPlanDiagnostics(Encounter->Plan, Event);
			Event.AppliedEnergyChangeCM2PerSec2 =
				Encounter->AppliedEnergyChangeCM2PerSec2;
		}
		Result.Events.Add(Event);
		FinalizeResult(Request, Result, Termination, Diagnostic);
	}

	double ExactSphereBoundaryFraction(
		const FABTSM11TrajectoryRequest& Request,
		const int32 ExpectedAssistIndex,
		const FState& State,
		const double FullStepSeconds,
		const FVector3d& CenterCM,
		const double RadiusCM,
		const bool bEntering,
		const FSphereRoots& SegmentRoots)
	{
		double SearchFraction = 1.0;
		if (bEntering && !SegmentRoots.bStartsInside)
		{
			SearchFraction = FMath::Clamp(
				0.5 * (SegmentRoots.EnterAlpha + SegmentRoots.ExitAlpha),
				SegmentRoots.EnterAlpha,
				1.0);
		}
		return SearchFraction * FindSphereBoundaryStepFraction(
			Request,
			ExpectedAssistIndex,
			State,
			FullStepSeconds * SearchFraction,
			CenterCM,
			RadiusCM,
			bEntering,
			Request.Config);
	}
}

double FABTSM11GravityAssistSolver::ComputePrimarySpecificEnergy(
	const FABTSM11GravityBodySpec& Primary,
	const FVector3d& PositionCM,
	const FVector3d& VelocityCMPerSec)
{
	const double RadiusCM = FMath::Max(
		(PositionCM - Primary.CenterCM).Length(),
		Primary.MinimumEvaluationRadiusCM);
	return 0.5 * VelocityCMPerSec.SquaredLength()
		- Primary.GravitationalParameterCM3PerSec2 / RadiusCM;
}

bool FABTSM11GravityAssistSolver::SweptSphereFirstHit(
	const FVector3d& SegmentStartCM,
	const FVector3d& SegmentEndCM,
	const FVector3d& SphereCenterCM,
	const double SphereRadiusCM,
	double& OutAlpha)
{
	ABTSM11GravityAssist::FSphereRoots Roots;
	if (!FMath::IsFinite(SphereRadiusCM)
		|| SphereRadiusCM <= 0.0
		|| !ABTSM11GravityAssist::IsFiniteVector(SegmentStartCM)
		|| !ABTSM11GravityAssist::IsFiniteVector(SegmentEndCM)
		|| !ABTSM11GravityAssist::IsFiniteVector(SphereCenterCM)
		|| !ABTSM11GravityAssist::SegmentSphereRoots(
			SegmentStartCM, SegmentEndCM, SphereCenterCM, SphereRadiusCM, Roots))
	{
		return false;
	}
	OutAlpha = Roots.EnterAlpha;
	return true;
}
