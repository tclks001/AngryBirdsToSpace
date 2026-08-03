// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "M11Search/ABTSM11SearchTypes.h"

namespace ABTS::M11Search
{
	struct FrozenCandidateIdentity
	{
		std::int32_t Rank = 0;
		std::uint64_t GlobalWorkIndex = 0;
		std::uint64_t CandidateSourceHash = 0;
		std::uint64_t NominalRequestHash = 0;
		std::uint64_t NominalResultHash = 0;
		std::uint64_t ScoreHash = 0;
	};

	/**
	 * Portable, Unreal-free layout source for the Editor PIE v4-derived
	 * candidates. These remain Candidate / NOT CERTIFIED until v2.2 closes.
	 */
	[[nodiscard]] bool BuildFrozenV4CandidateLayout(
		std::int32_t Rank,
		CandidateLayout& OutLayout,
		FrozenCandidateIdentity* OutIdentity = nullptr);
}
