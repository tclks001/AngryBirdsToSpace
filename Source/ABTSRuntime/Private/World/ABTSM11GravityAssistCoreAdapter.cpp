// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11GravityAssistCoreAdapter.h"

namespace ABTSM11GravityAssistAdapter::AdapterDetail
{
	using namespace ABTS::M11Core;

	static_assert(
		static_cast<uint8>(EABTSM110FinaleGravityRole::Primary)
			== static_cast<std::uint8_t>(GravityRole::Primary));
	static_assert(
		static_cast<uint8>(EABTSM110FinaleGravityRole::AssistPlanet1)
			== static_cast<std::uint8_t>(GravityRole::AssistPlanet1));
	static_assert(
		static_cast<uint8>(EABTSM110FinaleGravityRole::AssistPlanet2)
			== static_cast<std::uint8_t>(GravityRole::AssistPlanet2));
	static_assert(
		static_cast<uint8>(EABTSM110FinaleGravityRole::AssistPlanet3)
			== static_cast<std::uint8_t>(GravityRole::AssistPlanet3));
	static_assert(
		static_cast<uint8>(EABTSM110FinaleGravityRole::Count)
			== static_cast<std::uint8_t>(GravityRole::Count));

#define ABTS_M11_ASSERT_ENUM_VALUE(UeEnum, CoreEnum, Enumerator) \
	static_assert( \
		static_cast<uint8>(UeEnum::Enumerator) \
			== static_cast<std::uint8_t>(CoreEnum::Enumerator))

	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryEventType,
		TrajectoryEventType,
		AssistEnter);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryEventType,
		TrajectoryEventType,
		ClosestApproach);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryEventType,
		TrajectoryEventType,
		AssistExit);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryEventType,
		TrajectoryEventType,
		BodyCollision);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryEventType,
		TrajectoryEventType,
		TargetHit);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryEventType,
		TrajectoryEventType,
		Timeout);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryEventType,
		TrajectoryEventType,
		SolarCaptured);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryEventType,
		TrajectoryEventType,
		WrongOrder);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryEventType,
		TrajectoryEventType,
		OutOfBounds);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryEventType,
		TrajectoryEventType,
		AssistInvalidHyperbola);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryEventType,
		TrajectoryEventType,
		PlanetCaptured);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryEventType,
		TrajectoryEventType,
		AssistInvalidBPlaneBasis);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryEventType,
		TrajectoryEventType,
		AssistSolveFailed);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryEventType,
		TrajectoryEventType,
		TargetContact);

	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryTermination,
		TrajectoryTermination,
		None);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryTermination,
		TrajectoryTermination,
		TargetHit);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryTermination,
		TrajectoryTermination,
		BodyCollision);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryTermination,
		TrajectoryTermination,
		WrongOrder);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryTermination,
		TrajectoryTermination,
		OutOfBounds);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryTermination,
		TrajectoryTermination,
		SolarCaptured);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryTermination,
		TrajectoryTermination,
		Timeout);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryTermination,
		TrajectoryTermination,
		AssistInvalidHyperbola);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryTermination,
		TrajectoryTermination,
		PlanetCaptured);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryTermination,
		TrajectoryTermination,
		AssistInvalidBPlaneBasis);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryTermination,
		TrajectoryTermination,
		AssistSolveFailed);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11TrajectoryTermination,
		TrajectoryTermination,
		InvalidInput);

	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11AllowedPassSide,
		AllowedPassSide,
		Any);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11AllowedPassSide,
		AllowedPassSide,
		PositiveT);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11AllowedPassSide,
		AllowedPassSide,
		NegativeT);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11AllowedPassSide,
		AllowedPassSide,
		PositiveR);
	ABTS_M11_ASSERT_ENUM_VALUE(
		EABTSM11AllowedPassSide,
		AllowedPassSide,
		NegativeR);

#undef ABTS_M11_ASSERT_ENUM_VALUE

	static_assert(
		FABTSM11GravityScenario::BodyCount
			== GravityScenario::BodyCount);
	static_assert(
		FABTSM11GravityScenario::AssistCount
			== GravityScenario::AssistCount);
}

ABTS::M11Core::Vec3d ABTSM11GravityAssistAdapter::ToCore(
	const FVector3d& Value)
{
	return ABTS::M11Core::Vec3d(Value.X, Value.Y, Value.Z);
}

FVector3d ABTSM11GravityAssistAdapter::FromCore(
	const ABTS::M11Core::Vec3d& Value)
{
	return FVector3d(Value.X, Value.Y, Value.Z);
}

ABTS::M11Core::Color4f ABTSM11GravityAssistAdapter::ToCore(
	const FLinearColor& Value)
{
	ABTS::M11Core::Color4f Result;
	Result.R = Value.R;
	Result.G = Value.G;
	Result.B = Value.B;
	Result.A = Value.A;
	return Result;
}

FLinearColor ABTSM11GravityAssistAdapter::FromCore(
	const ABTS::M11Core::Color4f& Value)
{
	return FLinearColor(Value.R, Value.G, Value.B, Value.A);
}

ABTS::M11Core::GravityBodySpec
ABTSM11GravityAssistAdapter::ToCore(
	const FABTSM11GravityBodySpec& Value)
{
	ABTS::M11Core::GravityBodySpec Result;
	Result.BodyId = Value.BodyId;
	Result.Role = static_cast<ABTS::M11Core::GravityRole>(
		static_cast<uint8>(Value.Role));
	Result.CenterCM = ToCore(Value.CenterCM);
	Result.GravitationalParameterCM3PerSec2 =
		Value.GravitationalParameterCM3PerSec2;
	Result.MinimumEvaluationRadiusCM =
		Value.MinimumEvaluationRadiusCM;
	Result.VisualRadiusCM = Value.VisualRadiusCM;
	Result.CollisionRadiusCM = Value.CollisionRadiusCM;
	Result.MaximumSimulationRadiusCM =
		Value.MaximumSimulationRadiusCM;
	Result.InfluenceRadiusCM = Value.InfluenceRadiusCM;
	Result.AssistReferenceRadiusCM = Value.AssistReferenceRadiusCM;
	Result.InfluenceBlendWidthCM = Value.InfluenceBlendWidthCM;
	Result.VirtualOrbitalVelocityCMPerSec =
		ToCore(Value.VirtualOrbitalVelocityCMPerSec);
	Result.BPlaneReferenceNormal =
		ToCore(Value.BPlaneReferenceNormal);
	Result.BPlaneFallbackAxis = ToCore(Value.BPlaneFallbackAxis);
	Result.BPlaneTargetTCM = Value.BPlaneTargetTCM;
	Result.BPlaneTargetRCM = Value.BPlaneTargetRCM;
	Result.BPlaneSigmaTCM = Value.BPlaneSigmaTCM;
	Result.BPlaneSigmaRCM = Value.BPlaneSigmaRCM;
	Result.BPlaneOuterChiSquared = Value.BPlaneOuterChiSquared;
	Result.AllowedPassSideValue =
		static_cast<ABTS::M11Core::AllowedPassSide>(
			static_cast<uint8>(Value.AllowedPassSide));
	Result.MinimumEnergyChangeCM2PerSec2 =
		Value.MinimumEnergyChangeCM2PerSec2;
	Result.MaximumEnergyChangeCM2PerSec2 =
		Value.MaximumEnergyChangeCM2PerSec2;
	Result.DebugColor = ToCore(Value.DebugColor);
	return Result;
}

FABTSM11GravityBodySpec ABTSM11GravityAssistAdapter::FromCore(
	const ABTS::M11Core::GravityBodySpec& Value)
{
	FABTSM11GravityBodySpec Result;
	Result.BodyId = Value.BodyId;
	Result.Role = static_cast<EABTSM110FinaleGravityRole>(
		static_cast<std::uint8_t>(Value.Role));
	Result.CenterCM = FromCore(Value.CenterCM);
	Result.GravitationalParameterCM3PerSec2 =
		Value.GravitationalParameterCM3PerSec2;
	Result.MinimumEvaluationRadiusCM =
		Value.MinimumEvaluationRadiusCM;
	Result.VisualRadiusCM = Value.VisualRadiusCM;
	Result.CollisionRadiusCM = Value.CollisionRadiusCM;
	Result.MaximumSimulationRadiusCM =
		Value.MaximumSimulationRadiusCM;
	Result.InfluenceRadiusCM = Value.InfluenceRadiusCM;
	Result.AssistReferenceRadiusCM = Value.AssistReferenceRadiusCM;
	Result.InfluenceBlendWidthCM = Value.InfluenceBlendWidthCM;
	Result.VirtualOrbitalVelocityCMPerSec =
		FromCore(Value.VirtualOrbitalVelocityCMPerSec);
	Result.BPlaneReferenceNormal =
		FromCore(Value.BPlaneReferenceNormal);
	Result.BPlaneFallbackAxis = FromCore(Value.BPlaneFallbackAxis);
	Result.BPlaneTargetTCM = Value.BPlaneTargetTCM;
	Result.BPlaneTargetRCM = Value.BPlaneTargetRCM;
	Result.BPlaneSigmaTCM = Value.BPlaneSigmaTCM;
	Result.BPlaneSigmaRCM = Value.BPlaneSigmaRCM;
	Result.BPlaneOuterChiSquared = Value.BPlaneOuterChiSquared;
	Result.AllowedPassSide =
		static_cast<EABTSM11AllowedPassSide>(
			static_cast<std::uint8_t>(Value.AllowedPassSideValue));
	Result.MinimumEnergyChangeCM2PerSec2 =
		Value.MinimumEnergyChangeCM2PerSec2;
	Result.MaximumEnergyChangeCM2PerSec2 =
		Value.MaximumEnergyChangeCM2PerSec2;
	Result.DebugColor = FromCore(Value.DebugColor);
	return Result;
}

ABTS::M11Core::TargetSpec ABTSM11GravityAssistAdapter::ToCore(
	const FABTSM11TargetSpec& Value)
{
	ABTS::M11Core::TargetSpec Result;
	Result.TargetId = Value.TargetId;
	Result.CenterCM = ToCore(Value.CenterCM);
	Result.HitRadiusCM = Value.HitRadiusCM;
	Result.GeometricContactRadiusCM =
		Value.GeometricContactRadiusCM;
	Result.UseSeparateGeometricContactCenter =
		Value.bUseSeparateGeometricContactCenter;
	Result.GeometricContactCenterCM =
		ToCore(Value.GeometricContactCenterCM);
	Result.RequiredQualifiedAssistCount =
		Value.RequiredQualifiedAssistCount;
	Result.MinimumQualifyingCorridorQuality =
		Value.MinimumQualifyingCorridorQuality;
	Result.MinimumQualifyingEnergyGainCM2PerSec2 =
		Value.MinimumQualifyingEnergyGainCM2PerSec2;
	Result.RequireAllowedPassSide = Value.bRequireAllowedPassSide;
	Result.PresentationForward = ToCore(Value.PresentationForward);
	return Result;
}

FABTSM11TargetSpec ABTSM11GravityAssistAdapter::FromCore(
	const ABTS::M11Core::TargetSpec& Value)
{
	FABTSM11TargetSpec Result;
	Result.TargetId = Value.TargetId;
	Result.CenterCM = FromCore(Value.CenterCM);
	Result.HitRadiusCM = Value.HitRadiusCM;
	Result.GeometricContactRadiusCM =
		Value.GeometricContactRadiusCM;
	Result.bUseSeparateGeometricContactCenter =
		Value.UseSeparateGeometricContactCenter;
	Result.GeometricContactCenterCM =
		FromCore(Value.GeometricContactCenterCM);
	Result.RequiredQualifiedAssistCount =
		Value.RequiredQualifiedAssistCount;
	Result.MinimumQualifyingCorridorQuality =
		Value.MinimumQualifyingCorridorQuality;
	Result.MinimumQualifyingEnergyGainCM2PerSec2 =
		Value.MinimumQualifyingEnergyGainCM2PerSec2;
	Result.bRequireAllowedPassSide = Value.RequireAllowedPassSide;
	Result.PresentationForward = FromCore(Value.PresentationForward);
	return Result;
}

ABTS::M11Core::GravityScenario ABTSM11GravityAssistAdapter::ToCore(
	const FABTSM11GravityScenario& Value)
{
	ABTS::M11Core::GravityScenario Result;
	Result.LayoutVersion = Value.LayoutVersion;
	Result.ScenarioHash = Value.ScenarioHash;
	for (int32 Index = 0; Index < FABTSM11GravityScenario::BodyCount;
		++Index)
	{
		Result.Bodies[static_cast<std::size_t>(Index)] =
			ToCore(Value.Bodies[Index]);
	}
	Result.Target = ToCore(Value.Target);
	return Result;
}

FABTSM11GravityScenario ABTSM11GravityAssistAdapter::FromCore(
	const ABTS::M11Core::GravityScenario& Value)
{
	FABTSM11GravityScenario Result;
	Result.LayoutVersion = Value.LayoutVersion;
	Result.ScenarioHash = Value.ScenarioHash;
	for (int32 Index = 0; Index < FABTSM11GravityScenario::BodyCount;
		++Index)
	{
		Result.Bodies[Index] =
			FromCore(Value.Bodies[static_cast<std::size_t>(Index)]);
	}
	Result.Target = FromCore(Value.Target);
	return Result;
}

ABTS::M11Core::SolverConfig ABTSM11GravityAssistAdapter::ToCore(
	const FABTSM11SolverConfig& Value)
{
	ABTS::M11Core::SolverConfig Result;
	Result.SolverVersion = Value.SolverVersion;
	Result.HashSchemaVersion = Value.HashSchemaVersion;
	Result.FixedTimeStepSeconds = Value.FixedTimeStepSeconds;
	Result.MaximumSimulationTimeSeconds =
		Value.MaximumSimulationTimeSeconds;
	Result.MaximumStepCount = Value.MaximumStepCount;
	Result.MaximumSubdivisionDepth = Value.MaximumSubdivisionDepth;
	Result.MaximumCoastStepExpansionDepth =
		Value.MaximumCoastStepExpansionDepth;
	Result.AssistStepRadiusFraction = Value.AssistStepRadiusFraction;
	Result.CollisionStepRadiusFraction =
		Value.CollisionStepRadiusFraction;
	Result.GravityTimescaleFraction = Value.GravityTimescaleFraction;
	Result.PositionErrorLimitCM = Value.PositionErrorLimitCM;
	Result.RootBisectionIterations = Value.RootBisectionIterations;
	Result.RootAlphaTolerance = Value.RootAlphaTolerance;
	Result.BPlaneBasisMinimumLength =
		Value.BPlaneBasisMinimumLength;
	Result.MinimumVInfinityCMPerSec = Value.MinimumVInfinityCMPerSec;
	Result.MaximumNaturalDeflectionErrorRadians =
		Value.MaximumNaturalDeflectionErrorRadians;
	Result.EnergyQualityPower = Value.EnergyQualityPower;
	Result.EnergyRootEpsilonCM2PerSec2 =
		Value.EnergyRootEpsilonCM2PerSec2;
	Result.ExitEnergyResidualToleranceCM2PerSec2 =
		Value.ExitEnergyResidualToleranceCM2PerSec2;
	Result.EnergyShootingIterationCount =
		Value.EnergyShootingIterationCount;
	Result.NaturalCloneMaximumTimeSeconds =
		Value.NaturalCloneMaximumTimeSeconds;
	Result.NaturalCloneMaximumStepCount =
		Value.NaturalCloneMaximumStepCount;
	Result.EnabledAssistMask = Value.EnabledAssistMask;
	return Result;
}

FABTSM11SolverConfig ABTSM11GravityAssistAdapter::FromCore(
	const ABTS::M11Core::SolverConfig& Value)
{
	FABTSM11SolverConfig Result;
	Result.SolverVersion = Value.SolverVersion;
	Result.HashSchemaVersion = Value.HashSchemaVersion;
	Result.FixedTimeStepSeconds = Value.FixedTimeStepSeconds;
	Result.MaximumSimulationTimeSeconds =
		Value.MaximumSimulationTimeSeconds;
	Result.MaximumStepCount = Value.MaximumStepCount;
	Result.MaximumSubdivisionDepth = Value.MaximumSubdivisionDepth;
	Result.MaximumCoastStepExpansionDepth =
		Value.MaximumCoastStepExpansionDepth;
	Result.AssistStepRadiusFraction = Value.AssistStepRadiusFraction;
	Result.CollisionStepRadiusFraction =
		Value.CollisionStepRadiusFraction;
	Result.GravityTimescaleFraction = Value.GravityTimescaleFraction;
	Result.PositionErrorLimitCM = Value.PositionErrorLimitCM;
	Result.RootBisectionIterations = Value.RootBisectionIterations;
	Result.RootAlphaTolerance = Value.RootAlphaTolerance;
	Result.BPlaneBasisMinimumLength =
		Value.BPlaneBasisMinimumLength;
	Result.MinimumVInfinityCMPerSec = Value.MinimumVInfinityCMPerSec;
	Result.MaximumNaturalDeflectionErrorRadians =
		Value.MaximumNaturalDeflectionErrorRadians;
	Result.EnergyQualityPower = Value.EnergyQualityPower;
	Result.EnergyRootEpsilonCM2PerSec2 =
		Value.EnergyRootEpsilonCM2PerSec2;
	Result.ExitEnergyResidualToleranceCM2PerSec2 =
		Value.ExitEnergyResidualToleranceCM2PerSec2;
	Result.EnergyShootingIterationCount =
		Value.EnergyShootingIterationCount;
	Result.NaturalCloneMaximumTimeSeconds =
		Value.NaturalCloneMaximumTimeSeconds;
	Result.NaturalCloneMaximumStepCount =
		Value.NaturalCloneMaximumStepCount;
	Result.EnabledAssistMask = Value.EnabledAssistMask;
	return Result;
}

ABTS::M11Core::TrajectoryRequest
ABTSM11GravityAssistAdapter::ToCore(
	const FABTSM11TrajectoryRequest& Value)
{
	ABTS::M11Core::TrajectoryRequest Result;
	Result.Scenario = ToCore(Value.Scenario);
	Result.Config = ToCore(Value.Config);
	Result.InitialPositionCM = ToCore(Value.InitialPositionCM);
	Result.InitialVelocityCMPerSec =
		ToCore(Value.InitialVelocityCMPerSec);
	Result.InitialTimeSeconds = Value.InitialTimeSeconds;
	Result.InitialExpectedAssistIndex =
		Value.InitialExpectedAssistIndex;
	return Result;
}

FABTSM11TrajectoryRequest ABTSM11GravityAssistAdapter::FromCore(
	const ABTS::M11Core::TrajectoryRequest& Value)
{
	FABTSM11TrajectoryRequest Result;
	Result.Scenario = FromCore(Value.Scenario);
	Result.Config = FromCore(Value.Config);
	Result.InitialPositionCM = FromCore(Value.InitialPositionCM);
	Result.InitialVelocityCMPerSec =
		FromCore(Value.InitialVelocityCMPerSec);
	Result.InitialTimeSeconds = Value.InitialTimeSeconds;
	Result.InitialExpectedAssistIndex =
		Value.InitialExpectedAssistIndex;
	return Result;
}

ABTS::M11Core::TrajectoryPoint
ABTSM11GravityAssistAdapter::ToCore(
	const FABTSM11TrajectoryPoint& Value)
{
	ABTS::M11Core::TrajectoryPoint Result;
	Result.TimeSeconds = Value.TimeSeconds;
	Result.PositionCM = ToCore(Value.PositionCM);
	Result.VelocityCMPerSec = ToCore(Value.VelocityCMPerSec);
	Result.PrimarySpecificEnergyCM2PerSec2 =
		Value.PrimarySpecificEnergyCM2PerSec2;
	return Result;
}

FABTSM11TrajectoryPoint ABTSM11GravityAssistAdapter::FromCore(
	const ABTS::M11Core::TrajectoryPoint& Value)
{
	FABTSM11TrajectoryPoint Result;
	Result.TimeSeconds = Value.TimeSeconds;
	Result.PositionCM = FromCore(Value.PositionCM);
	Result.VelocityCMPerSec = FromCore(Value.VelocityCMPerSec);
	Result.PrimarySpecificEnergyCM2PerSec2 =
		Value.PrimarySpecificEnergyCM2PerSec2;
	return Result;
}

ABTS::M11Core::TrajectoryEvent
ABTSM11GravityAssistAdapter::ToCore(
	const FABTSM11TrajectoryEvent& Value)
{
	ABTS::M11Core::TrajectoryEvent Result;
	Result.Type = static_cast<ABTS::M11Core::TrajectoryEventType>(
		static_cast<uint8>(Value.Type));
	Result.BodyId = Value.BodyId;
	Result.AssistIndex = Value.AssistIndex;
	Result.TimeSeconds = Value.TimeSeconds;
	Result.PositionCM = ToCore(Value.PositionCM);
	Result.VelocityCMPerSec = ToCore(Value.VelocityCMPerSec);
	Result.EntrySpeedCMPerSec = Value.EntrySpeedCMPerSec;
	Result.ExitSpeedCMPerSec = Value.ExitSpeedCMPerSec;
	Result.ClosestDistanceCM = Value.ClosestDistanceCM;
	Result.BPlaneTCM = Value.BPlaneTCM;
	Result.BPlaneRCM = Value.BPlaneRCM;
	Result.BPlaneChiSquared = Value.BPlaneChiSquared;
	Result.CorridorQuality = Value.CorridorQuality;
	Result.NaturalDeflectionRadians =
		Value.NaturalDeflectionRadians;
	Result.IdealDeflectionRadians = Value.IdealDeflectionRadians;
	Result.RawEnergyChangeCM2PerSec2 =
		Value.RawEnergyChangeCM2PerSec2;
	Result.RequestedEnergyChangeCM2PerSec2 =
		Value.RequestedEnergyChangeCM2PerSec2;
	Result.AppliedEnergyChangeCM2PerSec2 =
		Value.AppliedEnergyChangeCM2PerSec2;
	return Result;
}

FABTSM11TrajectoryEvent ABTSM11GravityAssistAdapter::FromCore(
	const ABTS::M11Core::TrajectoryEvent& Value)
{
	FABTSM11TrajectoryEvent Result;
	Result.Type = static_cast<EABTSM11TrajectoryEventType>(
		static_cast<std::uint8_t>(Value.Type));
	Result.BodyId = Value.BodyId;
	Result.AssistIndex = Value.AssistIndex;
	Result.TimeSeconds = Value.TimeSeconds;
	Result.PositionCM = FromCore(Value.PositionCM);
	Result.VelocityCMPerSec = FromCore(Value.VelocityCMPerSec);
	Result.EntrySpeedCMPerSec = Value.EntrySpeedCMPerSec;
	Result.ExitSpeedCMPerSec = Value.ExitSpeedCMPerSec;
	Result.ClosestDistanceCM = Value.ClosestDistanceCM;
	Result.BPlaneTCM = Value.BPlaneTCM;
	Result.BPlaneRCM = Value.BPlaneRCM;
	Result.BPlaneChiSquared = Value.BPlaneChiSquared;
	Result.CorridorQuality = Value.CorridorQuality;
	Result.NaturalDeflectionRadians =
		Value.NaturalDeflectionRadians;
	Result.IdealDeflectionRadians = Value.IdealDeflectionRadians;
	Result.RawEnergyChangeCM2PerSec2 =
		Value.RawEnergyChangeCM2PerSec2;
	Result.RequestedEnergyChangeCM2PerSec2 =
		Value.RequestedEnergyChangeCM2PerSec2;
	Result.AppliedEnergyChangeCM2PerSec2 =
		Value.AppliedEnergyChangeCM2PerSec2;
	return Result;
}

ABTS::M11Core::AssistPhaseDiagnostics
ABTSM11GravityAssistAdapter::ToCore(
	const FABTSM11AssistPhaseDiagnostics& Value)
{
	ABTS::M11Core::AssistPhaseDiagnostics Result;
	Result.Complete = Value.bComplete;
	Result.EnterTimeSeconds = Value.EnterTimeSeconds;
	Result.ClosestTimeSeconds = Value.ClosestTimeSeconds;
	Result.ExitTimeSeconds = Value.ExitTimeSeconds;
	Result.CoastBeforeEnterSeconds = Value.CoastBeforeEnterSeconds;
	Result.InfluenceDurationSeconds = Value.InfluenceDurationSeconds;
	Result.ActualDeflectionRadians = Value.ActualDeflectionRadians;
	Result.NaturalDeflectionRadians =
		Value.NaturalDeflectionRadians;
	Result.EntrySpeedCMPerSec = Value.EntrySpeedCMPerSec;
	Result.ExitSpeedCMPerSec = Value.ExitSpeedCMPerSec;
	Result.AppliedEnergyChangeCM2PerSec2 =
		Value.AppliedEnergyChangeCM2PerSec2;
	return Result;
}

FABTSM11AssistPhaseDiagnostics
ABTSM11GravityAssistAdapter::FromCore(
	const ABTS::M11Core::AssistPhaseDiagnostics& Value)
{
	FABTSM11AssistPhaseDiagnostics Result;
	Result.bComplete = Value.Complete;
	Result.EnterTimeSeconds = Value.EnterTimeSeconds;
	Result.ClosestTimeSeconds = Value.ClosestTimeSeconds;
	Result.ExitTimeSeconds = Value.ExitTimeSeconds;
	Result.CoastBeforeEnterSeconds = Value.CoastBeforeEnterSeconds;
	Result.InfluenceDurationSeconds = Value.InfluenceDurationSeconds;
	Result.ActualDeflectionRadians = Value.ActualDeflectionRadians;
	Result.NaturalDeflectionRadians =
		Value.NaturalDeflectionRadians;
	Result.EntrySpeedCMPerSec = Value.EntrySpeedCMPerSec;
	Result.ExitSpeedCMPerSec = Value.ExitSpeedCMPerSec;
	Result.AppliedEnergyChangeCM2PerSec2 =
		Value.AppliedEnergyChangeCM2PerSec2;
	return Result;
}

ABTS::M11Core::TrajectoryPacingDiagnostics
ABTSM11GravityAssistAdapter::ToCore(
	const FABTSM11TrajectoryPacingDiagnostics& Value)
{
	ABTS::M11Core::TrajectoryPacingDiagnostics Result;
	Result.DiagnosticsVersion = Value.DiagnosticsVersion;
	Result.StartTimeSeconds = Value.StartTimeSeconds;
	Result.EndTimeSeconds = Value.EndTimeSeconds;
	Result.TotalFlightTimeSeconds = Value.TotalFlightTimeSeconds;
	Result.FirstObservedAssistIndex = Value.FirstObservedAssistIndex;
	Result.LastObservedAssistIndex = Value.LastObservedAssistIndex;
	Result.ObservedAssistCount = Value.ObservedAssistCount;
	for (int32 Index = 0; Index < FABTSM11GravityScenario::AssistCount;
		++Index)
	{
		Result.Assists[static_cast<std::size_t>(Index)] =
			ToCore(Value.Assists[Index]);
	}
	Result.TargetHit = Value.bTargetHit;
	Result.TargetHitTimeSeconds = Value.TargetHitTimeSeconds;
	Result.FinalCoastSeconds = Value.FinalCoastSeconds;
	Result.TotalCoastSeconds = Value.TotalCoastSeconds;
	Result.TotalInfluenceDurationSeconds =
		Value.TotalInfluenceDurationSeconds;
	Result.MaximumCoastSeconds = Value.MaximumCoastSeconds;
	Result.MaximumInfluenceDurationSeconds =
		Value.MaximumInfluenceDurationSeconds;
	return Result;
}

FABTSM11TrajectoryPacingDiagnostics
ABTSM11GravityAssistAdapter::FromCore(
	const ABTS::M11Core::TrajectoryPacingDiagnostics& Value)
{
	FABTSM11TrajectoryPacingDiagnostics Result;
	Result.DiagnosticsVersion = Value.DiagnosticsVersion;
	Result.StartTimeSeconds = Value.StartTimeSeconds;
	Result.EndTimeSeconds = Value.EndTimeSeconds;
	Result.TotalFlightTimeSeconds = Value.TotalFlightTimeSeconds;
	Result.FirstObservedAssistIndex = Value.FirstObservedAssistIndex;
	Result.LastObservedAssistIndex = Value.LastObservedAssistIndex;
	Result.ObservedAssistCount = Value.ObservedAssistCount;
	for (int32 Index = 0; Index < FABTSM11GravityScenario::AssistCount;
		++Index)
	{
		Result.Assists[Index] =
			FromCore(Value.Assists[static_cast<std::size_t>(Index)]);
	}
	Result.bTargetHit = Value.TargetHit;
	Result.TargetHitTimeSeconds = Value.TargetHitTimeSeconds;
	Result.FinalCoastSeconds = Value.FinalCoastSeconds;
	Result.TotalCoastSeconds = Value.TotalCoastSeconds;
	Result.TotalInfluenceDurationSeconds =
		Value.TotalInfluenceDurationSeconds;
	Result.MaximumCoastSeconds = Value.MaximumCoastSeconds;
	Result.MaximumInfluenceDurationSeconds =
		Value.MaximumInfluenceDurationSeconds;
	return Result;
}

ABTS::M11Core::TrajectoryResult ABTSM11GravityAssistAdapter::ToCore(
	const FABTSM11TrajectoryResult& Value)
{
	ABTS::M11Core::TrajectoryResult Result;
	Result.Points.reserve(static_cast<std::size_t>(Value.Points.Num()));
	for (const FABTSM11TrajectoryPoint& Point : Value.Points)
	{
		Result.Points.push_back(ToCore(Point));
	}
	Result.Events.reserve(static_cast<std::size_t>(Value.Events.Num()));
	for (const FABTSM11TrajectoryEvent& Event : Value.Events)
	{
		Result.Events.push_back(ToCore(Event));
	}
	Result.Termination =
		static_cast<ABTS::M11Core::TrajectoryTermination>(
			static_cast<uint8>(Value.Termination));
	Result.CompletedAssistCount = Value.CompletedAssistCount;
	Result.TargetContactCount = Value.TargetContactCount;
	Result.ValidationHash = Value.ValidationHash;
	Result.Diagnostic = TCHAR_TO_UTF8(*Value.Diagnostic);
	return Result;
}

FABTSM11TrajectoryResult ABTSM11GravityAssistAdapter::FromCore(
	const ABTS::M11Core::TrajectoryResult& Value)
{
	FABTSM11TrajectoryResult Result;
	FromCore(Value, Result);
	return Result;
}

void ABTSM11GravityAssistAdapter::FromCore(
	const ABTS::M11Core::TrajectoryResult& Value,
	FABTSM11TrajectoryResult& OutValue)
{
	OutValue.Reset();
	OutValue.Points.Reserve(
		static_cast<int32>(Value.Points.size()));
	for (const ABTS::M11Core::TrajectoryPoint& Point : Value.Points)
	{
		OutValue.Points.Add(FromCore(Point));
	}
	OutValue.Events.Reserve(
		static_cast<int32>(Value.Events.size()));
	for (const ABTS::M11Core::TrajectoryEvent& Event : Value.Events)
	{
		OutValue.Events.Add(FromCore(Event));
	}
	OutValue.Termination =
		static_cast<EABTSM11TrajectoryTermination>(
			static_cast<std::uint8_t>(Value.Termination));
	OutValue.CompletedAssistCount = Value.CompletedAssistCount;
	OutValue.TargetContactCount = Value.TargetContactCount;
	OutValue.ValidationHash = Value.ValidationHash;
	OutValue.Diagnostic = UTF8_TO_TCHAR(Value.Diagnostic.c_str());
}
