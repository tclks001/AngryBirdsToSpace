// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "World/ABTSM11FinaleLayoutTypes.h"

/**
 * Frozen identity of one M11-B v2.1 Candidate.
 *
 * This is deliberately separate from FABTSM11FinaleLayoutPreset's certified
 * manifest fields. A Candidate never acquires a CertificationHash or a
 * CertifiedBundleHash merely by being reconstructed for Editor PIE.
 */
struct ABTSRUNTIME_API FABTSM11CandidateExperienceIdentity
{
	int32 Rank = 0;
	uint64 GlobalWorkIndex = 0;
	uint64 CandidateSourceHash = 0;
	uint64 NominalRequestHash = 0;
	uint64 NominalResultHash = 0;
	uint64 ScoreHash = 0;

	bool IsValid() const;
	FString ToLogString() const;
};

/**
 * Editor-only bridge from the standard C++ M11-B v2.1 search authority to an
 * Unreal-facing, explicitly non-certified experience preset.
 *
 * No Intermediate manifest is loaded. The selected frozen work item is
 * rebuilt by CandidateSearch::EvaluateWorkItem and all four frozen hashes are
 * checked before a preset is returned.
 */
class ABTSRUNTIME_API FABTSM11CandidateExperienceCatalog final
{
public:
	static constexpr int32 FirstCandidateRank = 1;
	static constexpr int32 LastCandidateRank = 8;

#if WITH_EDITOR
	static bool BuildCandidate(
		int32 CandidateRank,
		FABTSM11FinaleLayoutPreset& OutPreset,
		FABTSM11CandidateExperienceIdentity& OutIdentity,
		FString* OutFailure = nullptr);
#endif
};
