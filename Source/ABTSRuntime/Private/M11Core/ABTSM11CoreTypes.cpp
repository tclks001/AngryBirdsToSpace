// Copyright Epic Games, Inc. All Rights Reserved.

#include "M11Core/ABTSM11CoreTypes.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>

namespace ABTS::M11Core::TypesDetail
{
	[[nodiscard]] bool IsFiniteVector(const Vec3d& Value)
	{
		return IsFinite(Value.X)
			&& IsFinite(Value.Y)
			&& IsFinite(Value.Z);
	}

	[[nodiscard]] bool IsFiniteColor(const Color4f& Value)
	{
		return IsFinite(Value.R)
			&& IsFinite(Value.G)
			&& IsFinite(Value.B)
			&& IsFinite(Value.A);
	}

	[[nodiscard]] bool Reject(
		std::string* OutFailure,
		const char* Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		return false;
	}
}

bool ABTS::M11Core::GravityBodySpec::IsAssist() const
{
	return Role >= GravityRole::AssistPlanet1
		&& Role <= GravityRole::AssistPlanet3;
}

std::int32_t ABTS::M11Core::GravityBodySpec::GetAssistIndex() const
{
	return IsAssist() ? static_cast<std::int32_t>(Role) : 0;
}

bool ABTS::M11Core::GravityBodySpec::IsValid(
	std::string* OutFailure) const
{
	using namespace TypesDetail;
	if (BodyId == InvalidIndex)
	{
		return Reject(OutFailure, "MissingBodyId");
	}
	if (Role < GravityRole::Primary || Role >= GravityRole::Count)
	{
		return Reject(OutFailure, "InvalidBodyRole");
	}
	if (!IsFiniteVector(CenterCM)
		|| !IsFinite(GravitationalParameterCM3PerSec2)
		|| GravitationalParameterCM3PerSec2 <= 0.0)
	{
		return Reject(OutFailure, "InvalidBodyGravity");
	}
	if (!IsFinite(MinimumEvaluationRadiusCM)
		|| !IsFinite(VisualRadiusCM)
		|| !IsFinite(CollisionRadiusCM)
		|| MinimumEvaluationRadiusCM <= 0.0
		|| VisualRadiusCM <= 0.0
		|| CollisionRadiusCM <= 0.0
		|| MinimumEvaluationRadiusCM > CollisionRadiusCM)
	{
		return Reject(OutFailure, "InvalidBodyRadii");
	}

	if (Role == GravityRole::Primary)
	{
		if (!IsFinite(MaximumSimulationRadiusCM)
			|| MaximumSimulationRadiusCM <= CollisionRadiusCM)
		{
			return Reject(OutFailure, "InvalidPrimarySimulationRadius");
		}
		return true;
	}

	if (!IsFinite(InfluenceRadiusCM)
		|| !IsFinite(AssistReferenceRadiusCM)
		|| !IsFinite(InfluenceBlendWidthCM)
		|| InfluenceRadiusCM <= CollisionRadiusCM
		|| AssistReferenceRadiusCM <= CollisionRadiusCM
		|| AssistReferenceRadiusCM > InfluenceRadiusCM
		|| InfluenceBlendWidthCM < 0.0
		|| InfluenceBlendWidthCM >= InfluenceRadiusCM - CollisionRadiusCM
		|| AssistReferenceRadiusCM
			> InfluenceRadiusCM - InfluenceBlendWidthCM
				+ Max(1.0e-6, InfluenceRadiusCM * 1.0e-12))
	{
		return Reject(OutFailure, "InvalidAssistRadii");
	}
	if (!IsFiniteVector(VirtualOrbitalVelocityCMPerSec)
		|| !IsFiniteVector(BPlaneReferenceNormal)
		|| !IsFiniteVector(BPlaneFallbackAxis)
		|| BPlaneReferenceNormal.SquaredLength() <= DoubleSmallNumber
		|| BPlaneFallbackAxis.SquaredLength() <= DoubleSmallNumber)
	{
		return Reject(OutFailure, "InvalidAssistVectors");
	}
	if (!IsFinite(BPlaneTargetTCM)
		|| !IsFinite(BPlaneTargetRCM)
		|| !IsFinite(BPlaneSigmaTCM)
		|| !IsFinite(BPlaneSigmaRCM)
		|| !IsFinite(BPlaneOuterChiSquared)
		|| static_cast<std::uint8_t>(AllowedPassSideValue)
			> static_cast<std::uint8_t>(AllowedPassSide::NegativeR)
		|| BPlaneSigmaTCM <= 0.0
		|| BPlaneSigmaRCM <= 0.0
		|| BPlaneOuterChiSquared <= 1.0)
	{
		return Reject(OutFailure, "InvalidBPlaneCorridor");
	}
	if (!IsFinite(MinimumEnergyChangeCM2PerSec2)
		|| !IsFinite(MaximumEnergyChangeCM2PerSec2)
		|| MinimumEnergyChangeCM2PerSec2 > 0.0
		|| MaximumEnergyChangeCM2PerSec2 < 0.0
		|| MinimumEnergyChangeCM2PerSec2
			> MaximumEnergyChangeCM2PerSec2
		|| !IsFiniteColor(DebugColor))
	{
		return Reject(OutFailure, "InvalidAssistEnergyLimits");
	}
	return true;
}

double ABTS::M11Core::TargetSpec::GetGeometricContactRadiusCM() const
{
	return GeometricContactRadiusCM > 0.0
		? GeometricContactRadiusCM
		: HitRadiusCM;
}

ABTS::M11Core::Vec3d
ABTS::M11Core::TargetSpec::GetGeometricContactCenterCM() const
{
	return UseSeparateGeometricContactCenter
		? GeometricContactCenterCM
		: CenterCM;
}

bool ABTS::M11Core::TargetSpec::IsValid(std::string* OutFailure) const
{
	using namespace TypesDetail;
	if (TargetId == InvalidIndex)
	{
		return Reject(OutFailure, "MissingTargetId");
	}
	if (!IsFiniteVector(CenterCM)
		|| !IsFinite(HitRadiusCM)
		|| HitRadiusCM <= 0.0
		|| !IsFinite(GeometricContactRadiusCM)
		|| GeometricContactRadiusCM < 0.0
		|| GetGeometricContactRadiusCM() > HitRadiusCM
		|| !IsFiniteVector(GeometricContactCenterCM)
		|| RequiredQualifiedAssistCount < 0
		|| RequiredQualifiedAssistCount > GravityScenario::AssistCount
		|| !IsFinite(MinimumQualifyingCorridorQuality)
		|| MinimumQualifyingCorridorQuality < 0.0
		|| MinimumQualifyingCorridorQuality > 1.0
		|| !IsFinite(MinimumQualifyingEnergyGainCM2PerSec2)
		|| MinimumQualifyingEnergyGainCM2PerSec2 < 0.0
		|| !IsFiniteVector(PresentationForward)
		|| PresentationForward.SquaredLength() <= DoubleSmallNumber)
	{
		return Reject(OutFailure, "InvalidTarget");
	}
	return true;
}

ABTS::M11Core::GravityScenario::GravityScenario()
{
	for (std::int32_t BodyIndex = 0; BodyIndex < BodyCount; ++BodyIndex)
	{
		Bodies[static_cast<std::size_t>(BodyIndex)].Role =
			static_cast<GravityRole>(BodyIndex);
	}
}

const ABTS::M11Core::GravityBodySpec&
ABTS::M11Core::GravityScenario::GetAssist(
	const std::int32_t AssistIndex) const
{
	// The public UE API has the same checked precondition. The standalone
	// core remains exception-free and fail-fast for an internal contract bug.
	if (AssistIndex < 1 || AssistIndex > AssistCount)
	{
		std::abort();
	}
	return Bodies[static_cast<std::size_t>(AssistIndex)];
}

bool ABTS::M11Core::GravityScenario::IsValid(
	std::string* OutFailure) const
{
	using namespace TypesDetail;
	if (LayoutVersion <= 0)
	{
		return Reject(OutFailure, "InvalidLayoutVersion");
	}
	if (ScenarioHash == 0)
	{
		return Reject(OutFailure, "MissingScenarioHash");
	}
	if (!Target.IsValid(OutFailure))
	{
		return false;
	}

	std::array<std::int32_t, BodyCount + 1> StableIds{};
	std::size_t StableIdCount = 0;
	StableIds[StableIdCount++] = Target.TargetId;
	for (std::int32_t BodyIndex = 0; BodyIndex < BodyCount; ++BodyIndex)
	{
		const GravityBodySpec& Body =
			Bodies[static_cast<std::size_t>(BodyIndex)];
		if (Body.Role != static_cast<GravityRole>(BodyIndex))
		{
			return Reject(OutFailure, "GravityRoleOrder");
		}
		if (!Body.IsValid(OutFailure))
		{
			return false;
		}
		if (std::find(
				StableIds.begin(),
				StableIds.begin()
					+ static_cast<std::ptrdiff_t>(StableIdCount),
				Body.BodyId)
			!= StableIds.begin()
				+ static_cast<std::ptrdiff_t>(StableIdCount))
		{
			return Reject(OutFailure, "DuplicateStableId");
		}
		StableIds[StableIdCount++] = Body.BodyId;
	}

	for (std::int32_t FirstAssist = 1;
		FirstAssist <= AssistCount;
		++FirstAssist)
	{
		const GravityBodySpec& Assist =
			Bodies[static_cast<std::size_t>(FirstAssist)];
		const double PrimarySeparationCM =
			(Assist.CenterCM - GetPrimary().CenterCM).Length();
		if (PrimarySeparationCM
			<= Assist.InfluenceRadiusCM
				+ GetPrimary().CollisionRadiusCM)
		{
			return Reject(
				OutFailure,
				"AssistInfluenceOverlapsPrimaryCollision");
		}
		if (PrimarySeparationCM + Assist.InfluenceRadiusCM
			>= GetPrimary().MaximumSimulationRadiusCM)
		{
			return Reject(
				OutFailure,
				"AssistOutsidePrimarySimulationDomain");
		}
		for (std::int32_t SecondAssist = FirstAssist + 1;
			SecondAssist <= AssistCount;
			++SecondAssist)
		{
			const GravityBodySpec& A =
				Bodies[static_cast<std::size_t>(FirstAssist)];
			const GravityBodySpec& B =
				Bodies[static_cast<std::size_t>(SecondAssist)];
			if ((A.CenterCM - B.CenterCM).Length()
				<= A.InfluenceRadiusCM + B.InfluenceRadiusCM)
			{
				return Reject(
					OutFailure,
					"OverlappingAssistInfluenceSpheres");
			}
		}
	}
	if ((Target.CenterCM - GetPrimary().CenterCM).Length()
			+ Target.HitRadiusCM
		>= GetPrimary().MaximumSimulationRadiusCM)
	{
		return Reject(
			OutFailure,
			"TargetOutsidePrimarySimulationDomain");
	}
	if ((Target.GetGeometricContactCenterCM()
			- GetPrimary().CenterCM).Length()
			+ Target.GetGeometricContactRadiusCM()
		>= GetPrimary().MaximumSimulationRadiusCM)
	{
		return Reject(
			OutFailure,
			"GeometricTargetOutsidePrimarySimulationDomain");
	}
	return true;
}

bool ABTS::M11Core::SolverConfig::IsGameplayAssistEnabled(
	const std::int32_t AssistIndex) const
{
	return AssistIndex >= 1
		&& AssistIndex <= GravityScenario::AssistCount
		&& (EnabledAssistMask
			& (1u << static_cast<std::uint32_t>(AssistIndex - 1))) != 0;
}

ABTS::M11Core::SolverConfig ABTS::M11Core::SolverConfig::MakeV2()
{
	SolverConfig Config;
	Config.SolverVersion = 2;
	Config.HashSchemaVersion = 2;
	Config.MaximumCoastStepExpansionDepth = 6;
	return Config;
}

bool ABTS::M11Core::SolverConfig::IsValid(
	std::string* OutFailure) const
{
	using namespace TypesDetail;
	const bool Version1 =
		SolverVersion == 1 && HashSchemaVersion == 1;
	const bool Version2 =
		SolverVersion == 2 && HashSchemaVersion == 2;
	if (!Version1 && !Version2)
	{
		return Reject(OutFailure, "UnsupportedSolverOrHashVersion");
	}
	if (!IsFinite(FixedTimeStepSeconds)
		|| !IsFinite(MaximumSimulationTimeSeconds)
		|| FixedTimeStepSeconds <= 0.0
		|| MaximumSimulationTimeSeconds <= 0.0
		|| MaximumStepCount <= 0
		|| MaximumSubdivisionDepth < 0
		|| MaximumSubdivisionDepth > 20
		|| MaximumCoastStepExpansionDepth < 0
		|| MaximumCoastStepExpansionDepth > 12
		|| (Version1 && MaximumCoastStepExpansionDepth != 0))
	{
		return Reject(OutFailure, "InvalidStepPolicy");
	}
	if (!IsFinite(AssistStepRadiusFraction)
		|| !IsFinite(CollisionStepRadiusFraction)
		|| !IsFinite(GravityTimescaleFraction)
		|| !IsFinite(PositionErrorLimitCM)
		|| AssistStepRadiusFraction <= 0.0
		|| CollisionStepRadiusFraction <= 0.0
		|| GravityTimescaleFraction <= 0.0
		|| PositionErrorLimitCM <= 0.0)
	{
		return Reject(OutFailure, "InvalidSubdivisionPolicy");
	}
	if (RootBisectionIterations <= 0
		|| RootBisectionIterations > 64
		|| !IsFinite(RootAlphaTolerance)
		|| RootAlphaTolerance <= 0.0
		|| RootAlphaTolerance > 1.0
		|| !IsFinite(BPlaneBasisMinimumLength)
		|| BPlaneBasisMinimumLength <= 0.0
		|| !IsFinite(MinimumVInfinityCMPerSec)
		|| MinimumVInfinityCMPerSec <= 0.0
		|| !IsFinite(MaximumNaturalDeflectionErrorRadians)
		|| MaximumNaturalDeflectionErrorRadians <= 0.0)
	{
		return Reject(OutFailure, "InvalidRootOrEncounterPolicy");
	}
	if (!IsFinite(EnergyQualityPower)
		|| EnergyQualityPower <= 0.0
		|| !IsFinite(EnergyRootEpsilonCM2PerSec2)
		|| EnergyRootEpsilonCM2PerSec2 < 0.0
		|| !IsFinite(ExitEnergyResidualToleranceCM2PerSec2)
		|| ExitEnergyResidualToleranceCM2PerSec2 < 0.0
		|| EnergyShootingIterationCount != 3)
	{
		return Reject(OutFailure, "InvalidEnergyPolicy");
	}
	if (!IsFinite(NaturalCloneMaximumTimeSeconds)
		|| NaturalCloneMaximumTimeSeconds <= 0.0
		|| NaturalCloneMaximumStepCount <= 0)
	{
		return Reject(OutFailure, "InvalidNaturalClonePolicy");
	}
	if ((EnabledAssistMask & static_cast<std::uint8_t>(~0x07u)) != 0)
	{
		return Reject(OutFailure, "InvalidAssistMask");
	}
	return true;
}

bool ABTS::M11Core::TrajectoryRequest::IsValid(
	std::string* OutFailure) const
{
	using namespace TypesDetail;
	if (!Scenario.IsValid(OutFailure) || !Config.IsValid(OutFailure))
	{
		return false;
	}
	if (!IsFiniteVector(InitialPositionCM)
		|| !IsFiniteVector(InitialVelocityCMPerSec)
		|| !IsFinite(InitialTimeSeconds)
		|| InitialExpectedAssistIndex < 1
		|| InitialExpectedAssistIndex
			> GravityScenario::AssistCount + 1)
	{
		return Reject(OutFailure, "InvalidInitialState");
	}
	if ((InitialPositionCM
			- Scenario.GetPrimary().CenterCM).Length()
		>= Scenario.GetPrimary().MaximumSimulationRadiusCM)
	{
		return Reject(
			OutFailure,
			"InitialStateOutsidePrimarySimulationDomain");
	}
	for (const GravityBodySpec& Body : Scenario.Bodies)
	{
		if ((InitialPositionCM - Body.CenterCM).Length()
			<= Body.CollisionRadiusCM)
		{
			return Reject(
				OutFailure,
				"InitialStateInsideBodyCollision");
		}
		if (Body.IsAssist()
			&& (InitialPositionCM - Body.CenterCM).Length()
				<= Body.InfluenceRadiusCM)
		{
			return Reject(
				OutFailure,
				"InitialStateInsideAssistInfluence");
		}
	}
	if ((InitialPositionCM - Scenario.Target.CenterCM).Length()
		<= Scenario.Target.HitRadiusCM)
	{
		return Reject(OutFailure, "InitialStateInsideTarget");
	}
	if ((InitialPositionCM
			- Scenario.Target.GetGeometricContactCenterCM()).Length()
		<= Scenario.Target.GetGeometricContactRadiusCM())
	{
		return Reject(
			OutFailure,
			"InitialStateInsideGeometricTarget");
	}
	return true;
}

void ABTS::M11Core::TrajectoryResult::Reset()
{
	*this = TrajectoryResult();
}

bool ABTS::M11Core::TrajectoryResult::DidHitTarget() const
{
	return Termination == TrajectoryTermination::TargetHit;
}

bool ABTS::M11Core::TrajectoryResult::DidContactTarget() const
{
	return TargetContactCount > 0;
}

const ABTS::M11Core::TrajectoryEvent*
ABTS::M11Core::TrajectoryResult::FindFirstEvent(
	const TrajectoryEventType Type) const
{
	const auto Found = std::find_if(
		Events.begin(),
		Events.end(),
		[Type](const TrajectoryEvent& Event)
		{
			return Event.Type == Type;
		});
	return Found != Events.end() ? &(*Found) : nullptr;
}

const ABTS::M11Core::TrajectoryEvent*
ABTS::M11Core::TrajectoryResult::FindAssistEvent(
	const TrajectoryEventType Type,
	const std::int32_t AssistIndex) const
{
	const auto Found = std::find_if(
		Events.begin(),
		Events.end(),
		[Type, AssistIndex](const TrajectoryEvent& Event)
		{
			return Event.Type == Type
				&& Event.AssistIndex == AssistIndex;
		});
	return Found != Events.end() ? &(*Found) : nullptr;
}

bool ABTS::M11Core::TrajectoryResult::BuildPacingDiagnostics(
	TrajectoryPacingDiagnostics& OutDiagnostics,
	std::string* OutFailure) const
{
	using namespace TypesDetail;
	OutDiagnostics = TrajectoryPacingDiagnostics();
	if (Points.empty())
	{
		return Reject(OutFailure, "MissingTrajectoryPoints");
	}
	OutDiagnostics.StartTimeSeconds = Points.front().TimeSeconds;
	OutDiagnostics.EndTimeSeconds = Points.back().TimeSeconds;
	OutDiagnostics.TotalFlightTimeSeconds =
		OutDiagnostics.EndTimeSeconds
			- OutDiagnostics.StartTimeSeconds;
	if (!IsFinite(OutDiagnostics.TotalFlightTimeSeconds)
		|| OutDiagnostics.TotalFlightTimeSeconds < 0.0)
	{
		return Reject(OutFailure, "InvalidTrajectoryTimeRange");
	}

	double PreviousExitTimeSeconds = OutDiagnostics.StartTimeSeconds;
	std::int32_t PreviousObservedAssistIndex = 0;
	for (std::int32_t AssistIndex = 1;
		AssistIndex <= GravityScenario::AssistCount;
		++AssistIndex)
	{
		const TrajectoryEvent* Enter =
			FindAssistEvent(
				TrajectoryEventType::AssistEnter,
				AssistIndex);
		const TrajectoryEvent* Closest =
			FindAssistEvent(
				TrajectoryEventType::ClosestApproach,
				AssistIndex);
		const TrajectoryEvent* Exit =
			FindAssistEvent(
				TrajectoryEventType::AssistExit,
				AssistIndex);
		if (Enter == nullptr && Closest == nullptr && Exit == nullptr)
		{
			continue;
		}
		if (Enter == nullptr || Closest == nullptr || Exit == nullptr
			|| (PreviousObservedAssistIndex != 0
				&& AssistIndex != PreviousObservedAssistIndex + 1)
			|| Enter->TimeSeconds < PreviousExitTimeSeconds
			|| Closest->TimeSeconds < Enter->TimeSeconds
			|| Exit->TimeSeconds < Closest->TimeSeconds
			|| Exit->TimeSeconds > OutDiagnostics.EndTimeSeconds)
		{
			return Reject(OutFailure, "MalformedAssistEventSequence");
		}
		const Vec3d EntryDirection =
			Enter->VelocityCMPerSec.GetSafeNormal();
		const Vec3d ExitDirection =
			Exit->VelocityCMPerSec.GetSafeNormal();
		if (EntryDirection.IsNearlyZero()
			|| ExitDirection.IsNearlyZero())
		{
			return Reject(OutFailure, "DegenerateAssistVelocity");
		}

		AssistPhaseDiagnostics& Assist =
			OutDiagnostics.Assists[
				static_cast<std::size_t>(AssistIndex - 1)];
		Assist.Complete = true;
		Assist.EnterTimeSeconds = Enter->TimeSeconds;
		Assist.ClosestTimeSeconds = Closest->TimeSeconds;
		Assist.ExitTimeSeconds = Exit->TimeSeconds;
		Assist.CoastBeforeEnterSeconds =
			Enter->TimeSeconds - PreviousExitTimeSeconds;
		Assist.InfluenceDurationSeconds =
			Exit->TimeSeconds - Enter->TimeSeconds;
		Assist.ActualDeflectionRadians = Acos(Clamp(
			Vec3d::DotProduct(EntryDirection, ExitDirection),
			-1.0,
			1.0));
		Assist.NaturalDeflectionRadians =
			Exit->NaturalDeflectionRadians;
		Assist.EntrySpeedCMPerSec =
			Enter->VelocityCMPerSec.Length();
		Assist.ExitSpeedCMPerSec =
			Exit->VelocityCMPerSec.Length();
		Assist.AppliedEnergyChangeCM2PerSec2 =
			Exit->AppliedEnergyChangeCM2PerSec2;
		if (OutDiagnostics.ObservedAssistCount == 0)
		{
			OutDiagnostics.FirstObservedAssistIndex = AssistIndex;
		}
		OutDiagnostics.LastObservedAssistIndex = AssistIndex;
		++OutDiagnostics.ObservedAssistCount;
		OutDiagnostics.TotalCoastSeconds +=
			Assist.CoastBeforeEnterSeconds;
		OutDiagnostics.TotalInfluenceDurationSeconds +=
			Assist.InfluenceDurationSeconds;
		OutDiagnostics.MaximumCoastSeconds = Max(
			OutDiagnostics.MaximumCoastSeconds,
			Assist.CoastBeforeEnterSeconds);
		OutDiagnostics.MaximumInfluenceDurationSeconds = Max(
			OutDiagnostics.MaximumInfluenceDurationSeconds,
			Assist.InfluenceDurationSeconds);
		PreviousExitTimeSeconds = Exit->TimeSeconds;
		PreviousObservedAssistIndex = AssistIndex;
	}

	const TrajectoryEvent* TargetHit =
		FindFirstEvent(TrajectoryEventType::TargetHit);
	if (TargetHit != nullptr)
	{
		if (TargetHit->TimeSeconds < PreviousExitTimeSeconds)
		{
			return Reject(OutFailure, "TargetPrecedesLastAssist");
		}
		if (TargetHit->TimeSeconds > OutDiagnostics.EndTimeSeconds)
		{
			return Reject(OutFailure, "TargetExceedsTrajectoryRange");
		}
		OutDiagnostics.TargetHit = true;
		OutDiagnostics.TargetHitTimeSeconds =
			TargetHit->TimeSeconds;
	}
	OutDiagnostics.FinalCoastSeconds =
		OutDiagnostics.EndTimeSeconds - PreviousExitTimeSeconds;
	OutDiagnostics.TotalCoastSeconds +=
		OutDiagnostics.FinalCoastSeconds;
	OutDiagnostics.MaximumCoastSeconds = Max(
		OutDiagnostics.MaximumCoastSeconds,
		OutDiagnostics.FinalCoastSeconds);

	if (CompletedAssistCount < 0
		|| CompletedAssistCount > GravityScenario::AssistCount
		|| (OutDiagnostics.ObservedAssistCount > 0
			&& OutDiagnostics.LastObservedAssistIndex
				!= CompletedAssistCount))
	{
		return Reject(OutFailure, "CompletedAssistCountMismatch");
	}
	return true;
}
