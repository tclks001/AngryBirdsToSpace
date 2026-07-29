// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "M11Core/ABTSM11CoreTypes.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ABTS::M11Search
{
	inline constexpr std::int32_t SearchContractVersion = 1;
	inline constexpr std::int32_t SearchAlgorithmVersion = 1;
	inline constexpr std::int32_t CandidateManifestVersion = 1;

	struct LaunchInput
	{
		double YawDegrees = 0.0;
		double PitchDegrees = 30.0;
		double Power = 1.0;

		[[nodiscard]] bool IsFinite() const;
	};

	struct LaunchModel
	{
		std::int32_t Version = 1;
		M11Core::Vec3d PouchLocalPositionCM =
			M11Core::Vec3d(0.0, 0.0, 180.0);
		double MinimumYawDegrees = -18.0;
		double MaximumYawDegrees = 18.0;
		double MinimumPitchDegrees = 0.0;
		double MaximumPitchDegrees = 60.0;
		double MinimumPower = 0.0;
		double MaximumPower = 1.0;
		double MinimumLaunchSpeedCMPerSec = 900.0;
		double MaximumLaunchSpeedCMPerSec = 2300.0;
		double MaximumSimulationTimeSeconds = 60.0;

		[[nodiscard]] bool IsValid(
			std::string* OutFailure = nullptr) const;
		[[nodiscard]] bool Contains(const LaunchInput& Input) const;
		[[nodiscard]] M11Core::Vec3d MapDirection(
			const LaunchInput& Input) const;
		[[nodiscard]] double MapSpeedCMPerSec(
			const LaunchInput& Input) const;
		[[nodiscard]] bool ApplyToRequest(
			const LaunchInput& Input,
			M11Core::TrajectoryRequest& InOutRequest,
			std::string* OutFailure = nullptr) const;
	};

	struct CandidateLayout
	{
		std::int32_t LayoutVersion = 2;
		LaunchModel Launch;
		LaunchInput NominalInput;
		M11Core::GravityScenario Scenario;
		M11Core::SolverConfig Solver;

		[[nodiscard]] bool IsValid(
			std::string* OutFailure = nullptr) const;
		[[nodiscard]] bool BuildRequest(
			const LaunchInput& Input,
			std::uint8_t EnabledAssistMask,
			M11Core::TrajectoryRequest& OutRequest,
			std::string* OutFailure = nullptr) const;
	};

	struct CandidateSearchContract
	{
		std::int32_t ContractVersion = SearchContractVersion;
		std::int32_t AlgorithmVersion = SearchAlgorithmVersion;
		std::uint64_t SearchSeed = 0x11b21001ull;

		std::int32_t LocalTimeSampleCount = 3;
		std::int32_t LocalImpactSampleCount = 3;
		std::int32_t LocalRadialSampleCount = 3;
		std::int32_t LocalMomentumDirectionSampleCount = 3;
		std::int32_t TargetTimeSampleCount = 5;
		std::int32_t RobustPreselectionWidth = 18;
		std::int32_t MinimumRobustSurvivorCount = 4;

		double MaximumTotalFlightTimeSeconds = 60.0;
		double MaximumCoastSeconds = 11.0;
		double MinimumInfluenceDurationSeconds = 3.0;
		double MaximumInfluenceDurationSeconds = 8.0;
		double MinimumDeflectionRadians = 0.30;
		double MinimumEnergyGainCM2PerSec2 = 50000.0;
		double MinimumCorridorQuality = 0.05;
		double MinimumLayoutTurnRadians = 0.30;
		double MinimumBodyClearanceCM = 1200.0;
		double LowPowerProbe = 0.90;

		double RobustYawStepDegrees = 0.25;
		double RobustPitchStepDegrees = 0.25;
		double RobustPowerStep = 0.005;

		double FirstEncounterMinimumSeconds = 7.0;
		double FirstEncounterMaximumSeconds = 13.0;
		double InterEncounterCoastMinimumSeconds = 5.0;
		double InterEncounterCoastMaximumSeconds = 9.0;
		double TargetCoastMinimumSeconds = 3.0;
		double TargetCoastMaximumSeconds = 7.0;
		double MinimumTargetHitRadiusCM = 4500.0;
		double MaximumTargetHitRadiusCM = 12000.0;
		double TargetCoverageMarginCM = 500.0;

		double MinimumInfluenceRadiusCM = 8000.0;
		double MaximumInfluenceRadiusCM = 17000.0;
		double MinimumVirtualMomentumSpeedCMPerSec = 2200.0;
		double MaximumVirtualMomentumSpeedCMPerSec = 5200.0;
		double MinimumGravityScale = 0.70;
		double MaximumGravityScale = 2.20;

		std::int32_t RequestedCandidateCount = 5;
		double MinimumDiversityDistanceCM = 3500.0;

		[[nodiscard]] static CandidateSearchContract MakeV2_1();
		[[nodiscard]] bool IsValid(
			std::string* OutFailure = nullptr) const;
	};

	enum class EvaluationStatus : std::uint8_t
	{
		Accepted = 0,
		InvalidContract,
		InitialArcFailed,
		Assist1ConstructionFailed,
		Assist2ConstructionFailed,
		Assist3ConstructionFailed,
		TargetConstructionFailed,
		NominalRejected,
		PacingRejected,
		LowPowerGateRejected,
		RobustnessRejected,
		AblationRejected,
		InternalError
	};

	struct AssistMetrics
	{
		double EnterTimeSeconds = 0.0;
		double ClosestTimeSeconds = 0.0;
		double ExitTimeSeconds = 0.0;
		double CoastBeforeEnterSeconds = 0.0;
		double InfluenceDurationSeconds = 0.0;
		double ActualDeflectionRadians = 0.0;
		double NaturalDeflectionRadians = 0.0;
		double EntrySpeedCMPerSec = 0.0;
		double ExitSpeedCMPerSec = 0.0;
		double CorridorQuality = 0.0;
		double AppliedEnergyGainCM2PerSec2 = 0.0;
		double CollisionClearanceCM = 0.0;
	};

	struct CandidateMetrics
	{
		double TotalFlightTimeSeconds = 0.0;
		double FinalCoastSeconds = 0.0;
		double MaximumCoastSeconds = 0.0;
		double TotalInfluenceDurationSeconds = 0.0;
		double MinimumLayoutTurnRadians = 0.0;
		std::array<double, M11Core::GravityScenario::AssistCount>
			LayoutTurnsRadians{};
		double MinimumTargetDistanceCM = 0.0;
		std::array<AssistMetrics, M11Core::GravityScenario::AssistCount>
			Assists;
		std::int32_t RobustSurvivorCount = 0;
		std::int32_t LowPowerCompletedAssistCount = 0;
		std::array<bool, 4> AblationHitTarget{};
		std::array<std::uint64_t, 4> AblationResultHashes{};
		std::array<std::uint8_t, 4> AblationMasks{
			0x6u, 0x5u, 0x3u, 0x0u};
	};

	struct CandidateRecord
	{
		std::uint64_t GlobalWorkIndex = 0;
		EvaluationStatus Status = EvaluationStatus::InternalError;
		std::uint64_t CandidateSourceHash = 0;
		std::uint64_t NominalRequestHash = 0;
		std::uint64_t NominalResultHash = 0;
		std::uint64_t ScoreHash = 0;
		std::int32_t SolverInvocationCount = 0;
		CandidateLayout Layout;
		CandidateMetrics Metrics;
		std::string Rejection;

		[[nodiscard]] bool IsAccepted() const
		{
			return Status == EvaluationStatus::Accepted;
		}
	};

	struct BatchRequest
	{
		std::uint64_t GlobalWorkItemCount = 1;
		std::uint32_t ShardIndex = 0;
		std::uint32_t ShardCount = 1;
		std::uint32_t ThreadCount = 1;
		std::uint32_t RequestedTopCandidateCount = 5;
		std::uint64_t LocalBeginOffset = 0;
		/** Zero evaluates the remaining canonical shard suffix. */
		std::uint64_t LocalWorkItemLimit = 0;

		[[nodiscard]] bool IsValid(
			std::string* OutFailure = nullptr) const;
	};

	struct BatchResult
	{
		std::vector<CandidateRecord> Evaluations;
		std::vector<CandidateRecord> TopCandidates;
		std::uint64_t EvaluationAggregateHash = 0;
		std::uint64_t CandidateAggregateHash = 0;
		std::uint64_t SolverInvocationCount = 0;
		double WallClockSeconds = 0.0;
		std::string Diagnostic;
	};

	[[nodiscard]] const char* ToString(EvaluationStatus Status);
	[[nodiscard]] std::uint64_t ComputeCandidateSourceHash(
		const CandidateLayout& Layout,
		const CandidateSearchContract& Contract);
	[[nodiscard]] std::uint64_t ComputeCandidateScoreHash(
		const CandidateRecord& Candidate);
	[[nodiscard]] std::uint64_t ComputeEvaluationAggregateHash(
		const std::vector<CandidateRecord>& Evaluations);
}
