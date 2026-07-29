// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "M11Core/ABTSM11CoreMath.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ABTS::M11Core
{
	enum class GravityRole : std::uint8_t
	{
		Primary = 0,
		AssistPlanet1,
		AssistPlanet2,
		AssistPlanet3,
		Count
	};

	enum class TrajectoryEventType : std::uint8_t
	{
		AssistEnter = 0,
		ClosestApproach,
		AssistExit,
		BodyCollision,
		TargetHit,
		Timeout,
		SolarCaptured,
		WrongOrder,
		OutOfBounds,
		AssistInvalidHyperbola,
		PlanetCaptured,
		AssistInvalidBPlaneBasis,
		AssistSolveFailed,
		TargetContact
	};

	enum class TrajectoryTermination : std::uint8_t
	{
		None = 0,
		TargetHit,
		BodyCollision,
		WrongOrder,
		OutOfBounds,
		SolarCaptured,
		Timeout,
		AssistInvalidHyperbola,
		PlanetCaptured,
		AssistInvalidBPlaneBasis,
		AssistSolveFailed,
		InvalidInput
	};

	enum class AllowedPassSide : std::uint8_t
	{
		Any = 0,
		PositiveT,
		NegativeT,
		PositiveR,
		NegativeR
	};

	struct GravityBodySpec
	{
		std::int32_t BodyId = InvalidIndex;
		GravityRole Role = GravityRole::Primary;
		Vec3d CenterCM;
		double GravitationalParameterCM3PerSec2 = 0.0;
		double MinimumEvaluationRadiusCM = 1.0;
		double VisualRadiusCM = 1.0;
		double CollisionRadiusCM = 1.0;
		double MaximumSimulationRadiusCM = 0.0;
		double InfluenceRadiusCM = 0.0;
		double AssistReferenceRadiusCM = 0.0;
		double InfluenceBlendWidthCM = 0.0;
		Vec3d VirtualOrbitalVelocityCMPerSec;
		Vec3d BPlaneReferenceNormal = Vec3d(0.0, 0.0, 1.0);
		Vec3d BPlaneFallbackAxis = Vec3d(0.0, 1.0, 0.0);
		double BPlaneTargetTCM = 0.0;
		double BPlaneTargetRCM = 0.0;
		double BPlaneSigmaTCM = 1.0;
		double BPlaneSigmaRCM = 1.0;
		double BPlaneOuterChiSquared = 4.0;
		AllowedPassSide AllowedPassSideValue = AllowedPassSide::Any;
		double MinimumEnergyChangeCM2PerSec2 = -1.0e12;
		double MaximumEnergyChangeCM2PerSec2 = 1.0e12;
		Color4f DebugColor;

		[[nodiscard]] bool IsAssist() const;
		[[nodiscard]] std::int32_t GetAssistIndex() const;
		[[nodiscard]] bool IsValid(std::string* OutFailure = nullptr) const;
	};

	struct TargetSpec
	{
		std::int32_t TargetId = InvalidIndex;
		Vec3d CenterCM;
		double HitRadiusCM = 0.0;
		double GeometricContactRadiusCM = 0.0;
		bool UseSeparateGeometricContactCenter = false;
		Vec3d GeometricContactCenterCM;
		std::int32_t RequiredQualifiedAssistCount = 0;
		double MinimumQualifyingCorridorQuality = 0.0;
		double MinimumQualifyingEnergyGainCM2PerSec2 = 0.0;
		bool RequireAllowedPassSide = false;
		Vec3d PresentationForward = Vec3d(1.0, 0.0, 0.0);

		[[nodiscard]] double GetGeometricContactRadiusCM() const;
		[[nodiscard]] Vec3d GetGeometricContactCenterCM() const;
		[[nodiscard]] bool IsValid(std::string* OutFailure = nullptr) const;
	};

	struct GravityScenario
	{
		static constexpr std::int32_t BodyCount =
			static_cast<std::int32_t>(GravityRole::Count);
		static constexpr std::int32_t AssistCount = BodyCount - 1;

		std::int32_t LayoutVersion = 1;
		std::uint32_t ScenarioHash = 0;
		std::array<GravityBodySpec, BodyCount> Bodies;
		TargetSpec Target;

		GravityScenario();

		[[nodiscard]] const GravityBodySpec& GetPrimary() const
		{
			return Bodies[0];
		}

		[[nodiscard]] const GravityBodySpec& GetAssist(
			const std::int32_t AssistIndex) const;

		[[nodiscard]] bool IsValid(std::string* OutFailure = nullptr) const;
	};

	struct SolverConfig
	{
		std::int32_t SolverVersion = 1;
		std::int32_t HashSchemaVersion = 1;
		double FixedTimeStepSeconds = 1.0 / 120.0;
		double MaximumSimulationTimeSeconds = 120.0;
		std::int32_t MaximumStepCount = 2000000;
		std::int32_t MaximumSubdivisionDepth = 6;
		std::int32_t MaximumCoastStepExpansionDepth = 0;
		double AssistStepRadiusFraction = 0.04;
		double CollisionStepRadiusFraction = 0.25;
		double GravityTimescaleFraction = 0.05;
		double PositionErrorLimitCM = 0.5;
		std::int32_t RootBisectionIterations = 24;
		double RootAlphaTolerance = 1.0e-10;
		double BPlaneBasisMinimumLength = 1.0e-8;
		double MinimumVInfinityCMPerSec = 1.0;
		double MaximumNaturalDeflectionErrorRadians = 0.35;
		double EnergyQualityPower = 2.0;
		double EnergyRootEpsilonCM2PerSec2 = 1.0e-6;
		double ExitEnergyResidualToleranceCM2PerSec2 = 5.0;
		std::int32_t EnergyShootingIterationCount = 3;
		double NaturalCloneMaximumTimeSeconds = 120.0;
		std::int32_t NaturalCloneMaximumStepCount = 1000000;
		std::uint8_t EnabledAssistMask = 0x7u;

		[[nodiscard]] static SolverConfig MakeV2();
		[[nodiscard]] bool IsGameplayAssistEnabled(
			std::int32_t AssistIndex) const;
		[[nodiscard]] bool IsValid(std::string* OutFailure = nullptr) const;
	};

	struct TrajectoryRequest
	{
		GravityScenario Scenario;
		SolverConfig Config;
		Vec3d InitialPositionCM;
		Vec3d InitialVelocityCMPerSec;
		double InitialTimeSeconds = 0.0;
		std::int32_t InitialExpectedAssistIndex = 1;

		[[nodiscard]] bool IsValid(std::string* OutFailure = nullptr) const;
	};

	struct TrajectoryPoint
	{
		double TimeSeconds = 0.0;
		Vec3d PositionCM;
		Vec3d VelocityCMPerSec;
		double PrimarySpecificEnergyCM2PerSec2 = 0.0;
	};

	struct TrajectoryEvent
	{
		TrajectoryEventType Type = TrajectoryEventType::Timeout;
		std::int32_t BodyId = InvalidIndex;
		std::int32_t AssistIndex = 0;
		double TimeSeconds = 0.0;
		Vec3d PositionCM;
		Vec3d VelocityCMPerSec;
		double EntrySpeedCMPerSec = 0.0;
		double ExitSpeedCMPerSec = 0.0;
		double ClosestDistanceCM = 0.0;
		double BPlaneTCM = 0.0;
		double BPlaneRCM = 0.0;
		double BPlaneChiSquared = 0.0;
		double CorridorQuality = 0.0;
		double NaturalDeflectionRadians = 0.0;
		double IdealDeflectionRadians = 0.0;
		double RawEnergyChangeCM2PerSec2 = 0.0;
		double RequestedEnergyChangeCM2PerSec2 = 0.0;
		double AppliedEnergyChangeCM2PerSec2 = 0.0;
	};

	struct AssistPhaseDiagnostics
	{
		bool Complete = false;
		double EnterTimeSeconds = 0.0;
		double ClosestTimeSeconds = 0.0;
		double ExitTimeSeconds = 0.0;
		double CoastBeforeEnterSeconds = 0.0;
		double InfluenceDurationSeconds = 0.0;
		double ActualDeflectionRadians = 0.0;
		double NaturalDeflectionRadians = 0.0;
		double EntrySpeedCMPerSec = 0.0;
		double ExitSpeedCMPerSec = 0.0;
		double AppliedEnergyChangeCM2PerSec2 = 0.0;
	};

	struct TrajectoryPacingDiagnostics
	{
		std::int32_t DiagnosticsVersion = 1;
		double StartTimeSeconds = 0.0;
		double EndTimeSeconds = 0.0;
		double TotalFlightTimeSeconds = 0.0;
		std::int32_t FirstObservedAssistIndex = 0;
		std::int32_t LastObservedAssistIndex = 0;
		std::int32_t ObservedAssistCount = 0;
		std::array<AssistPhaseDiagnostics, GravityScenario::AssistCount> Assists;
		bool TargetHit = false;
		double TargetHitTimeSeconds = 0.0;
		double FinalCoastSeconds = 0.0;
		double TotalCoastSeconds = 0.0;
		double TotalInfluenceDurationSeconds = 0.0;
		double MaximumCoastSeconds = 0.0;
		double MaximumInfluenceDurationSeconds = 0.0;
	};

	struct TrajectoryResult
	{
		std::vector<TrajectoryPoint> Points;
		std::vector<TrajectoryEvent> Events;
		TrajectoryTermination Termination = TrajectoryTermination::None;
		std::int32_t CompletedAssistCount = 0;
		std::int32_t TargetContactCount = 0;
		std::uint64_t ValidationHash = 0;
		std::string Diagnostic;

		void Reset();
		[[nodiscard]] bool DidHitTarget() const;
		[[nodiscard]] bool DidContactTarget() const;
		[[nodiscard]] const TrajectoryEvent* FindFirstEvent(
			TrajectoryEventType Type) const;
		[[nodiscard]] const TrajectoryEvent* FindAssistEvent(
			TrajectoryEventType Type,
			std::int32_t AssistIndex) const;
		[[nodiscard]] bool BuildPacingDiagnostics(
			TrajectoryPacingDiagnostics& OutDiagnostics,
			std::string* OutFailure = nullptr) const;
	};
}
