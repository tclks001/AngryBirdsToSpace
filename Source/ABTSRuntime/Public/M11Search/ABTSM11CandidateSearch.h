// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "M11Search/ABTSM11SearchTypes.h"

namespace ABTS::M11Search
{
	class CandidateSearch final
	{
	public:
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
