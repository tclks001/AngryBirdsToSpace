// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "M11Core/ABTSM11CoreTypes.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ABTS::M11Search
{
	inline constexpr std::int32_t SearchContractVersion = 3;
	inline constexpr std::int32_t SearchAlgorithmVersion = 3;
	inline constexpr std::int32_t CandidateManifestVersion = 3;
	inline constexpr std::int32_t ParticleBeamContractVersion = 1;
	inline constexpr std::int32_t ParticleBeamAlgorithmVersion = 4;
	inline constexpr std::int32_t ParticleBeamManifestVersion = 1;

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
		double MinimumLateralTurnAxisProjection = 0.25;
		double MinimumBodyClearanceCM = 1200.0;
		double LowPowerProbe = 0.90;

		double RobustYawStepDegrees = 0.25;
		double RobustPitchStepDegrees = 0.25;
		double RobustPowerStep = 0.005;

		/**
		 * Candidate-only statistical estimate over the complete Launch
		 * Yaw/Pitch/Power domain. This is not the exhaustive M11-B v2.2
		 * certification grid.
		 */
		std::uint64_t MonteCarloSeed = 0x11b215000ull;
		std::int32_t MonteCarloSampleCount = 5000;
		/**
		 * Candidate-only estimate of the mouse-addressable Yaw/Pitch plane
		 * at NominalInput.Power. It intentionally does not sample Power.
		 */
		std::uint64_t ScreenAimSeed = 0x11b215002ull;
		std::int32_t ScreenAimSampleCount = 5000;
		double MinimumPrefixRetentionRatio = 0.08;
		double MaximumPrefixRetentionRatio = 0.55;
		double FullScoreMinimumPrefixRetentionRatio = 0.15;
		double FullScoreMaximumPrefixRetentionRatio = 0.40;
		std::int32_t ConditionalProbeSamplesPerSet = 512;
		double ConditionalYawHalfExtentDegrees = 1.0;
		double ConditionalPitchHalfExtentDegrees = 1.0;
		double ConditionalPowerHalfExtent = 0.05;
		std::int32_t MinimumHullEvidenceCount = 3;
		double MinimumHullAreaSquareDegrees = 0.0001;
		double MinimumHullYawSpanDegrees = 0.01;
		double MinimumHullPitchSpanDegrees = 0.01;

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
		std::int32_t MinimumAlternatingLateralTurnCount = 1;

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
		InputDomainDegenerate,
		InternalError
	};

	struct YawPitchPoint
	{
		double YawDegrees = 0.0;
		double PitchDegrees = 0.0;
	};

	struct InputSetMetrics
	{
		/** Unbiased count from the fixed 5000-point full Launch-domain set. */
		std::int32_t FullDomainCount = 0;
		/** |Sn| / |S(n-1)| from the same full-domain set. */
		double FullDomainRetentionRatio = 0.0;
		/**
		 * Unbiased fixed-power count from the 5000-point screen Yaw/Pitch
		 * set. S1-S3 candidate gates and UX scores use only this set.
		 */
		std::int32_t ScreenAimCount = 0;
		double ScreenAimRetentionRatio = 0.0;
		bool ScreenAimRetentionCompliant = false;
		/**
		 * Separately labelled local conditional evidence. It is never added
		 * to either unbiased domain count, hull, or UX score.
		 */
		std::int32_t ConditionalProbeCount = 0;
		std::int32_t ConditionalParentCount = 0;
		std::int32_t ConditionalMemberCount = 0;
		double ConditionalRetentionRatio = 0.0;
		std::int32_t ScreenAimHullEvidencePointCount = 0;
		std::vector<YawPitchPoint> ScreenAimHullYawPitch;
		double ScreenAimHullAreaSquareDegrees = 0.0;
		double ScreenAimHullYawSpanDegrees = 0.0;
		double ScreenAimHullPitchSpanDegrees = 0.0;
		double ScreenAimHullNormalizedArea = 0.0;
		double ScreenAimHullCompactness = 0.0;
		bool ScreenAimHullContainsNominal = false;
		bool ScreenAimHullCompliant = false;
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
		/** Signed in the deterministic layout presentation plane. */
		double SignedLateralTurnRadians = 0.0;
		/** Absolute alignment of the turn axis to the presentation normal. */
		double LateralTurnAxisProjection = 0.0;
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
		double MinimumReadableDeflectionRadians = 0.0;
		std::int32_t AlternatingLateralTurnCount = 0;
		std::int32_t RobustSurvivorCount = 0;
		std::int32_t LowPowerCompletedAssistCount = 0;
		std::int32_t FullDomainSampleCount = 0;
		std::int32_t ScreenAimSampleCount = 0;
		std::int32_t FullDomainSolveFailureCount = 0;
		std::int32_t ScreenAimSolveFailureCount = 0;
		std::int32_t ConditionalSolveFailureCount = 0;
		/** S1, S2, S3, and optional S4/target-hit set metrics. */
		std::array<InputSetMetrics, 4> InputSets;
		double PrefixRetentionScore = 0.0;
		double PrefixHullScore = 0.0;
		double DeflectionReadabilityScore = 0.0;
		double AlternationScore = 0.0;
		double PacingScore = 0.0;
		double SoftScore = 0.0;
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

	/** Portable per-input authority used by v2.2 certification tooling. */
	struct InputEvaluation
	{
		LaunchInput Input;
		std::uint8_t EnabledAssistMask = 0x7u;
		std::array<bool, 4> PrefixMembership{};
		M11Core::TrajectoryTermination Termination =
			M11Core::TrajectoryTermination::InvalidInput;
		std::int32_t CompletedAssistCount = 0;
		std::int32_t TargetContactCount = 0;
		std::uint64_t ResultHash = 0;
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

	/**
	 * Additive Search v4 constructor contract. The nested evaluation
	 * contract remains the frozen Search v3 acceptance authority; these
	 * fields only govern how provisional layouts are constructed.
	 */
	struct ParticleBeamSearchContract
	{
		std::int32_t ContractVersion = ParticleBeamContractVersion;
		std::int32_t AlgorithmVersion = ParticleBeamAlgorithmVersion;
		std::uint64_t ConstructionSeed = 0x11b24001ull;
		std::uint64_t ExplorationSeed = 0x11b245001ull;
		std::uint64_t HoldoutSeed = 0x11b245002ull;
		CandidateSearchContract EvaluationContract =
			CandidateSearchContract::MakeV2_1();

		std::int32_t RootParameterCount = 16;
		std::int32_t ExplorationSampleCount = 256;
		std::int32_t GeometryTimeSampleCount = 5;
		std::int32_t GeometryRadiusSampleCount = 4;
		std::int32_t GeometryImpactSampleCount = 4;
		std::int32_t GeometryRadialSampleCount = 3;
		std::int32_t GeometryMomentumSampleCount = 7;
		std::int32_t NominalProposalBudget = 96;
		std::int32_t CoarseProposalBudget = 48;
		std::int32_t RefinementProposalBudget = 12;
		std::int32_t CoarseParticleLimit = 48;
		std::int32_t BeamWidth = 6;
		std::int32_t HoldoutSampleCount = 512;
		std::int32_t MaximumFinalAuditCandidates = 6;
		std::int32_t RobustGuardSurvivorCount = 1;
		std::int32_t TargetRefinementTimeSampleCount = 9;

		double TargetPrefixRetentionRatio = 0.25;
		double ExplorationMinimumRetentionRatio = 0.05;
		double ExplorationMaximumRetentionRatio = 0.60;
		double PreferredMinimumRetentionRatio = 0.15;
		double PreferredMaximumRetentionRatio = 0.40;
		double MinimumBeamDiversityDistanceCM = 1800.0;
		double FinalTargetTurnGuardRadians = 0.03;
		double TargetRefinementMaximumCoastSeconds = 11.0;
		double ConstructionInterEncounterCoastMinimumSeconds = 2.5;
		double ConstructionInterEncounterCoastMaximumSeconds = 5.5;
		double IdealMinimumDeflectionRadians = 0.45;
		double IdealMaximumDeflectionRadians = 0.85;
		double IdealMinimumAxisProjection = 0.60;
		double IdealMinimumInfluenceSeconds = 4.0;
		double IdealMaximumInfluenceSeconds = 6.5;
		double IdealMaximumCoastSeconds = 5.5;
		double IdealMaximumFlightSeconds = 42.0;

		[[nodiscard]] static ParticleBeamSearchContract MakeV4();
		[[nodiscard]] bool IsValid(
			std::string* OutFailure = nullptr) const;
	};

	struct ParticleBeamStageMetrics
	{
		std::int32_t AssistIndex = 0;
		std::int32_t ParentParticleCount = 0;
		std::int32_t MemberParticleCount = 0;
		double RetentionRatio = 0.0;
		double HullAreaSquareDegrees = 0.0;
		double HullYawSpanDegrees = 0.0;
		double HullPitchSpanDegrees = 0.0;
		double HullCompactness = 0.0;
		double ActualDeflectionRadians = 0.0;
		double SignedLateralTurnRadians = 0.0;
		double InfluenceDurationSeconds = 0.0;
		double CoastBeforeEnterSeconds = 0.0;
		/** Local 3x3x3 release neighborhood that preserves this prefix. */
		std::int32_t RobustPrefixSurvivorCount = 0;
		double StageScore = 0.0;
	};

	struct ParticleBeamConstructionMetrics
	{
		std::uint64_t InitialParticleSolveCount = 0;
		std::array<std::uint64_t, M11Core::GravityScenario::AssistCount>
			GeometryProposalCounts{};
		std::array<std::uint64_t, M11Core::GravityScenario::AssistCount>
			NominalProposalSolveCounts{};
		std::array<std::uint64_t, M11Core::GravityScenario::AssistCount>
			CoarseParticleSolveCounts{};
		std::array<std::uint64_t, M11Core::GravityScenario::AssistCount>
			RefinementParticleSolveCounts{};
		std::array<std::int32_t, M11Core::GravityScenario::AssistCount>
			BeamSurvivorCounts{};
		std::uint64_t HoldoutSolveCount = 0;
		std::uint64_t FinalAuditSolveCount = 0;
	};

	struct ParticleBeamCandidateRecord
	{
		CandidateRecord Candidate;
		std::uint64_t ConstructionHash = 0;
		double ConstructionScore = 0.0;
		std::array<ParticleBeamStageMetrics,
			M11Core::GravityScenario::AssistCount> Stages{};
		std::int32_t HoldoutSampleCount = 0;
		std::array<InputSetMetrics, 4> HoldoutInputSets{};
	};

	struct ParticleBeamSearchResult
	{
		std::vector<ParticleBeamCandidateRecord> Evaluations;
		std::vector<ParticleBeamCandidateRecord> TopCandidates;
		ParticleBeamConstructionMetrics Construction;
		std::uint64_t ContractHash = 0;
		std::uint64_t ConstructionAggregateHash = 0;
		std::uint64_t CandidateAggregateHash = 0;
		double WallClockSeconds = 0.0;
		std::string Diagnostic;
	};

	[[nodiscard]] const char* ToString(EvaluationStatus Status);
	[[nodiscard]] std::uint64_t ComputeCandidateSearchContractHash(
		const CandidateSearchContract& Contract);
	[[nodiscard]] std::uint64_t ComputeCandidateSourceHash(
		const CandidateLayout& Layout,
		const CandidateSearchContract& Contract);
	[[nodiscard]] std::uint64_t ComputeCandidateScoreHash(
		const CandidateRecord& Candidate);
	[[nodiscard]] std::uint64_t ComputeEvaluationAggregateHash(
		const std::vector<CandidateRecord>& Evaluations);
}
