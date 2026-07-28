// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11GravityAssistTypes.h"

namespace
{
	bool IsFiniteVector(const FVector3d& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}

	bool IsFiniteColor(const FLinearColor& Value)
	{
		return FMath::IsFinite(Value.R)
			&& FMath::IsFinite(Value.G)
			&& FMath::IsFinite(Value.B)
			&& FMath::IsFinite(Value.A);
	}

	bool Reject(FString* OutFailure, const TCHAR* Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		return false;
	}
}

bool FABTSM11GravityBodySpec::IsAssist() const
{
	return Role >= EABTSM110FinaleGravityRole::AssistPlanet1
		&& Role <= EABTSM110FinaleGravityRole::AssistPlanet3;
}

int32 FABTSM11GravityBodySpec::GetAssistIndex() const
{
	return IsAssist() ? static_cast<int32>(Role) : 0;
}

bool FABTSM11GravityBodySpec::IsValid(FString* OutFailure) const
{
	if (BodyId == INDEX_NONE)
	{
		return Reject(OutFailure, TEXT("MissingBodyId"));
	}
	if (Role < EABTSM110FinaleGravityRole::Primary
		|| Role >= EABTSM110FinaleGravityRole::Count)
	{
		return Reject(OutFailure, TEXT("InvalidBodyRole"));
	}
	if (!IsFiniteVector(CenterCM)
		|| !FMath::IsFinite(GravitationalParameterCM3PerSec2)
		|| GravitationalParameterCM3PerSec2 <= 0.0)
	{
		return Reject(OutFailure, TEXT("InvalidBodyGravity"));
	}
	if (!FMath::IsFinite(MinimumEvaluationRadiusCM)
		|| !FMath::IsFinite(VisualRadiusCM)
		|| !FMath::IsFinite(CollisionRadiusCM)
		|| MinimumEvaluationRadiusCM <= 0.0
		|| VisualRadiusCM <= 0.0
		|| CollisionRadiusCM <= 0.0
		|| MinimumEvaluationRadiusCM > CollisionRadiusCM)
	{
		return Reject(OutFailure, TEXT("InvalidBodyRadii"));
	}

	if (Role == EABTSM110FinaleGravityRole::Primary)
	{
		if (!FMath::IsFinite(MaximumSimulationRadiusCM)
			|| MaximumSimulationRadiusCM <= CollisionRadiusCM)
		{
			return Reject(OutFailure, TEXT("InvalidPrimarySimulationRadius"));
		}
		return true;
	}

	if (!FMath::IsFinite(InfluenceRadiusCM)
		|| !FMath::IsFinite(AssistReferenceRadiusCM)
		|| !FMath::IsFinite(InfluenceBlendWidthCM)
		|| InfluenceRadiusCM <= CollisionRadiusCM
		|| AssistReferenceRadiusCM <= CollisionRadiusCM
		|| AssistReferenceRadiusCM > InfluenceRadiusCM
		|| InfluenceBlendWidthCM < 0.0
		|| InfluenceBlendWidthCM >= InfluenceRadiusCM - CollisionRadiusCM
		|| AssistReferenceRadiusCM
			> InfluenceRadiusCM - InfluenceBlendWidthCM
				+ FMath::Max(1.0e-6, InfluenceRadiusCM * 1.0e-12))
	{
		return Reject(OutFailure, TEXT("InvalidAssistRadii"));
	}
	if (!IsFiniteVector(VirtualOrbitalVelocityCMPerSec)
		|| !IsFiniteVector(BPlaneReferenceNormal)
		|| !IsFiniteVector(BPlaneFallbackAxis)
		|| BPlaneReferenceNormal.SquaredLength() <= UE_DOUBLE_SMALL_NUMBER
		|| BPlaneFallbackAxis.SquaredLength() <= UE_DOUBLE_SMALL_NUMBER)
	{
		return Reject(OutFailure, TEXT("InvalidAssistVectors"));
	}
	if (!FMath::IsFinite(BPlaneTargetTCM)
		|| !FMath::IsFinite(BPlaneTargetRCM)
		|| !FMath::IsFinite(BPlaneSigmaTCM)
		|| !FMath::IsFinite(BPlaneSigmaRCM)
		|| !FMath::IsFinite(BPlaneOuterChiSquared)
		|| static_cast<uint8>(AllowedPassSide)
			> static_cast<uint8>(EABTSM11AllowedPassSide::NegativeR)
		|| BPlaneSigmaTCM <= 0.0
		|| BPlaneSigmaRCM <= 0.0
		|| BPlaneOuterChiSquared <= 1.0)
	{
		return Reject(OutFailure, TEXT("InvalidBPlaneCorridor"));
	}
	if (!FMath::IsFinite(MinimumEnergyChangeCM2PerSec2)
		|| !FMath::IsFinite(MaximumEnergyChangeCM2PerSec2)
		|| MinimumEnergyChangeCM2PerSec2 > 0.0
		|| MaximumEnergyChangeCM2PerSec2 < 0.0
		|| MinimumEnergyChangeCM2PerSec2 > MaximumEnergyChangeCM2PerSec2
		|| !IsFiniteColor(DebugColor))
	{
		return Reject(OutFailure, TEXT("InvalidAssistEnergyLimits"));
	}
	return true;
}

bool FABTSM11TargetSpec::IsValid(FString* OutFailure) const
{
	if (TargetId == INDEX_NONE)
	{
		return Reject(OutFailure, TEXT("MissingTargetId"));
	}
	if (!IsFiniteVector(CenterCM)
		|| !FMath::IsFinite(HitRadiusCM)
		|| HitRadiusCM <= 0.0
		|| !IsFiniteVector(PresentationForward)
		|| PresentationForward.SquaredLength() <= UE_DOUBLE_SMALL_NUMBER)
	{
		return Reject(OutFailure, TEXT("InvalidTarget"));
	}
	return true;
}

FABTSM11GravityScenario::FABTSM11GravityScenario()
{
	for (int32 BodyIndex = 0; BodyIndex < BodyCount; ++BodyIndex)
	{
		Bodies[BodyIndex].Role = static_cast<EABTSM110FinaleGravityRole>(BodyIndex);
	}
}

bool FABTSM11GravityScenario::IsValid(FString* OutFailure) const
{
	if (LayoutVersion <= 0)
	{
		return Reject(OutFailure, TEXT("InvalidLayoutVersion"));
	}
	if (ScenarioHash == 0)
	{
		return Reject(OutFailure, TEXT("MissingScenarioHash"));
	}
	if (!Target.IsValid(OutFailure))
	{
		return false;
	}

	TSet<int32> StableIds;
	StableIds.Reserve(BodyCount + 1);
	StableIds.Add(Target.TargetId);
	for (int32 BodyIndex = 0; BodyIndex < BodyCount; ++BodyIndex)
	{
		const FABTSM11GravityBodySpec& Body = Bodies[BodyIndex];
		if (Body.Role != static_cast<EABTSM110FinaleGravityRole>(BodyIndex))
		{
			return Reject(OutFailure, TEXT("GravityRoleOrder"));
		}
		if (!Body.IsValid(OutFailure))
		{
			return false;
		}
		if (StableIds.Contains(Body.BodyId))
		{
			return Reject(OutFailure, TEXT("DuplicateStableId"));
		}
		StableIds.Add(Body.BodyId);
	}

	for (int32 FirstAssist = 1; FirstAssist <= AssistCount; ++FirstAssist)
	{
		const FABTSM11GravityBodySpec& Assist = Bodies[FirstAssist];
		const double PrimarySeparationCM =
			(Assist.CenterCM - GetPrimary().CenterCM).Length();
		if (PrimarySeparationCM
			<= Assist.InfluenceRadiusCM + GetPrimary().CollisionRadiusCM)
		{
			return Reject(OutFailure, TEXT("AssistInfluenceOverlapsPrimaryCollision"));
		}
		if (PrimarySeparationCM + Assist.InfluenceRadiusCM
			>= GetPrimary().MaximumSimulationRadiusCM)
		{
			return Reject(OutFailure, TEXT("AssistOutsidePrimarySimulationDomain"));
		}
		for (int32 SecondAssist = FirstAssist + 1; SecondAssist <= AssistCount; ++SecondAssist)
		{
			const FABTSM11GravityBodySpec& A = Bodies[FirstAssist];
			const FABTSM11GravityBodySpec& B = Bodies[SecondAssist];
			if ((A.CenterCM - B.CenterCM).Length()
				<= A.InfluenceRadiusCM + B.InfluenceRadiusCM)
			{
				return Reject(OutFailure, TEXT("OverlappingAssistInfluenceSpheres"));
			}
		}
	}
	if ((Target.CenterCM - GetPrimary().CenterCM).Length() + Target.HitRadiusCM
		>= GetPrimary().MaximumSimulationRadiusCM)
	{
		return Reject(OutFailure, TEXT("TargetOutsidePrimarySimulationDomain"));
	}
	return true;
}

bool FABTSM11SolverConfig::IsGameplayAssistEnabled(const int32 AssistIndex) const
{
	return AssistIndex >= 1
		&& AssistIndex <= FABTSM11GravityScenario::AssistCount
		&& (EnabledAssistMask & (1u << (AssistIndex - 1))) != 0;
}

bool FABTSM11SolverConfig::IsValid(FString* OutFailure) const
{
	if (SolverVersion != 1 || HashSchemaVersion != 1)
	{
		return Reject(OutFailure, TEXT("UnsupportedSolverOrHashVersion"));
	}
	if (!FMath::IsFinite(FixedTimeStepSeconds)
		|| !FMath::IsFinite(MaximumSimulationTimeSeconds)
		|| FixedTimeStepSeconds <= 0.0
		|| MaximumSimulationTimeSeconds <= 0.0
		|| MaximumStepCount <= 0
		|| MaximumSubdivisionDepth < 0
		|| MaximumSubdivisionDepth > 20)
	{
		return Reject(OutFailure, TEXT("InvalidStepPolicy"));
	}
	if (!FMath::IsFinite(AssistStepRadiusFraction)
		|| !FMath::IsFinite(CollisionStepRadiusFraction)
		|| !FMath::IsFinite(GravityTimescaleFraction)
		|| !FMath::IsFinite(PositionErrorLimitCM)
		|| AssistStepRadiusFraction <= 0.0
		|| CollisionStepRadiusFraction <= 0.0
		|| GravityTimescaleFraction <= 0.0
		|| PositionErrorLimitCM <= 0.0)
	{
		return Reject(OutFailure, TEXT("InvalidSubdivisionPolicy"));
	}
	if (RootBisectionIterations <= 0
		|| RootBisectionIterations > 64
		|| !FMath::IsFinite(RootAlphaTolerance)
		|| RootAlphaTolerance <= 0.0
		|| RootAlphaTolerance > 1.0
		|| !FMath::IsFinite(BPlaneBasisMinimumLength)
		|| BPlaneBasisMinimumLength <= 0.0
		|| !FMath::IsFinite(MinimumVInfinityCMPerSec)
		|| MinimumVInfinityCMPerSec <= 0.0
		|| !FMath::IsFinite(MaximumNaturalDeflectionErrorRadians)
		|| MaximumNaturalDeflectionErrorRadians <= 0.0)
	{
		return Reject(OutFailure, TEXT("InvalidRootOrEncounterPolicy"));
	}
	if (!FMath::IsFinite(EnergyQualityPower)
		|| EnergyQualityPower <= 0.0
		|| !FMath::IsFinite(EnergyRootEpsilonCM2PerSec2)
		|| EnergyRootEpsilonCM2PerSec2 < 0.0
		|| !FMath::IsFinite(ExitEnergyResidualToleranceCM2PerSec2)
		|| ExitEnergyResidualToleranceCM2PerSec2 < 0.0
		|| EnergyShootingIterationCount != 3)
	{
		return Reject(OutFailure, TEXT("InvalidEnergyPolicy"));
	}
	if (!FMath::IsFinite(NaturalCloneMaximumTimeSeconds)
		|| NaturalCloneMaximumTimeSeconds <= 0.0
		|| NaturalCloneMaximumStepCount <= 0)
	{
		return Reject(OutFailure, TEXT("InvalidNaturalClonePolicy"));
	}
	return true;
}

bool FABTSM11TrajectoryRequest::IsValid(FString* OutFailure) const
{
	if (!Scenario.IsValid(OutFailure) || !Config.IsValid(OutFailure))
	{
		return false;
	}
	if (!IsFiniteVector(InitialPositionCM)
		|| !IsFiniteVector(InitialVelocityCMPerSec)
		|| !FMath::IsFinite(InitialTimeSeconds)
		|| InitialExpectedAssistIndex < 1
		|| InitialExpectedAssistIndex > FABTSM11GravityScenario::AssistCount + 1)
	{
		return Reject(OutFailure, TEXT("InvalidInitialState"));
	}
	if ((InitialPositionCM - Scenario.GetPrimary().CenterCM).Length()
		>= Scenario.GetPrimary().MaximumSimulationRadiusCM)
	{
		return Reject(OutFailure, TEXT("InitialStateOutsidePrimarySimulationDomain"));
	}
	for (const FABTSM11GravityBodySpec& Body : Scenario.Bodies)
	{
		if ((InitialPositionCM - Body.CenterCM).Length() <= Body.CollisionRadiusCM)
		{
			return Reject(OutFailure, TEXT("InitialStateInsideBodyCollision"));
		}
		if (Body.IsAssist()
			&& (InitialPositionCM - Body.CenterCM).Length()
				<= Body.InfluenceRadiusCM)
		{
			return Reject(OutFailure, TEXT("InitialStateInsideAssistInfluence"));
		}
	}
	if ((InitialPositionCM - Scenario.Target.CenterCM).Length()
		<= Scenario.Target.HitRadiusCM)
	{
		return Reject(OutFailure, TEXT("InitialStateInsideTarget"));
	}
	return true;
}

void FABTSM11TrajectoryResult::Reset()
{
	*this = FABTSM11TrajectoryResult();
}

const FABTSM11TrajectoryEvent* FABTSM11TrajectoryResult::FindFirstEvent(
	const EABTSM11TrajectoryEventType Type) const
{
	return Events.FindByPredicate([Type](const FABTSM11TrajectoryEvent& Event)
	{
		return Event.Type == Type;
	});
}

const FABTSM11TrajectoryEvent* FABTSM11TrajectoryResult::FindAssistEvent(
	const EABTSM11TrajectoryEventType Type,
	const int32 AssistIndex) const
{
	return Events.FindByPredicate([Type, AssistIndex](const FABTSM11TrajectoryEvent& Event)
	{
		return Event.Type == Type && Event.AssistIndex == AssistIndex;
	});
}
