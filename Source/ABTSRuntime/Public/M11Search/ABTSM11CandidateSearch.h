// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "M11Search/ABTSM11SearchTypes.h"

namespace ABTS::M11Search
{
	struct PartialAlternationMetrics
	{
		std::array<double, M11Core::GravityScenario::AssistCount>
			SignedLateralTurnRadians{};
		std::int32_t CompletedAssistCount = 0;
		std::int32_t PartialAlternationCount = 0;
	};

	class CandidateSearch final
	{
	public:
		/**
		 * Returns the deterministic construction preference for assists
		 * 1..3. Values are always +1/-1 and strictly alternate.
		 */
		[[nodiscard]] static std::array<std::int8_t, 3>
			BuildPreferredPassSidePattern(
				const CandidateSearchContract& Contract,
				std::uint64_t GlobalWorkIndex);

		/**
		 * Shared construction/final low-power exclusion predicate. A
		 * low-power trajectory may enter and leave the enlarged influence
		 * shell, but it must not complete a qualifying Assist1 prefix or hit
		 * the target.
		 */
		[[nodiscard]] static bool ShouldRejectLowPowerResult(
			const M11Core::TrajectoryResult& Result,
			bool bQualifiedAssist1);

		/**
		 * Measures completed assist turns in the same deterministic
		 * presentation plane used by final candidate metrics.
		 */
		[[nodiscard]] static PartialAlternationMetrics
			MeasurePartialAlternation(
				const CandidateLayout& Layout,
				const CandidateSearchContract& Contract,
				const M11Core::TrajectoryResult& Result,
				std::int32_t LastAssistIndex);

		[[nodiscard]] static bool EvaluateWorkItem(
			const CandidateSearchContract& Contract,
			std::uint64_t GlobalWorkIndex,
			CandidateRecord& OutCandidate,
			std::string* OutFailure = nullptr);

		[[nodiscard]] static bool RunBatch(
			const CandidateSearchContract& Contract,
			const BatchRequest& Request,
			BatchResult& OutResult,
			std::string* OutFailure = nullptr);

		[[nodiscard]] static bool ReplayCandidate(
			const CandidateRecord& Candidate,
			std::uint8_t EnabledAssistMask,
			M11Core::TrajectoryResult& OutResult,
			std::string* OutFailure = nullptr);

		[[nodiscard]] static bool CandidateRanksBefore(
			const CandidateRecord& Left,
			const CandidateRecord& Right);

		[[nodiscard]] static std::vector<CandidateRecord>
			SelectTopCandidates(
				const CandidateSearchContract& Contract,
				const std::vector<CandidateRecord>& Evaluations,
				std::uint32_t RequestedCount);
	};
}
