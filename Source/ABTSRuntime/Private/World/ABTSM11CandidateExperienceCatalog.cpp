// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11CandidateExperienceCatalog.h"

#if WITH_EDITOR

#include "M11Search/ABTSM11CandidateSearch.h"
#include "World/ABTSM11GravityAssistCoreAdapter.h"

#include <array>
#include <string>

namespace
{
	using ABTS::M11Search::CandidateLayout;
	using ABTS::M11Search::CandidateRecord;
	using ABTS::M11Search::CandidateSearch;
	using ABTS::M11Search::CandidateSearchContract;
	using ABTS::M11Search::LaunchInput;
	using ABTS::M11Search::LaunchModel;

	struct FFrozenCandidateIdentity
	{
		int32 Rank;
		uint64 GlobalWorkIndex;
		uint64 CandidateSourceHash;
		uint64 NominalRequestHash;
		uint64 NominalResultHash;
		uint64 ScoreHash;
	};

	constexpr std::array<FFrozenCandidateIdentity, 2> FrozenCandidates = {{
		{
			1,
			2278ull,
			0xaaae0dd44f14f785ull,
			0x5ecc893f6eb7003dull,
			0xb47d8314ebe69376ull,
			0xd6e03f2d9e0f3b8bull},
		{
			2,
			772ull,
			0xe2c810b38f338e06ull,
			0x5c07be6f9371448eull,
			0xe465b9c154c235a1ull,
			0xdd1613e3dbb4c1b0ull}
	}};

	bool Reject(FString* OutFailure, const FString& Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		return false;
	}

	const FFrozenCandidateIdentity* FindFrozenCandidate(
		const int32 CandidateRank)
	{
		for (const FFrozenCandidateIdentity& Frozen : FrozenCandidates)
		{
			if (Frozen.Rank == CandidateRank)
			{
				return &Frozen;
			}
		}
		return nullptr;
	}

	FABTSM11FinaleLaunchInput FromSearchInput(const LaunchInput& Input)
	{
		FABTSM11FinaleLaunchInput Result;
		Result.YawDegrees = Input.YawDegrees;
		Result.PitchDegrees = Input.PitchDegrees;
		Result.Power = Input.Power;
		return Result;
	}

	FABTSM11FinaleLaunchModel FromSearchLaunchModel(
		const LaunchModel& Launch)
	{
		FABTSM11FinaleLaunchModel Result;
		Result.LaunchModelVersion = Launch.Version;
		Result.PouchLocalPositionCM =
			ABTSM11GravityAssistAdapter::FromCore(
				Launch.PouchLocalPositionCM);
		Result.MinimumYawDegrees = Launch.MinimumYawDegrees;
		Result.MaximumYawDegrees = Launch.MaximumYawDegrees;
		Result.MinimumPitchDegrees = Launch.MinimumPitchDegrees;
		Result.MaximumPitchDegrees = Launch.MaximumPitchDegrees;
		Result.MinimumPower = Launch.MinimumPower;
		Result.MaximumPower = Launch.MaximumPower;
		Result.MinimumLaunchSpeedCMPerSec =
			Launch.MinimumLaunchSpeedCMPerSec;
		Result.MaximumLaunchSpeedCMPerSec =
			Launch.MaximumLaunchSpeedCMPerSec;
		Result.MaximumSimulationTimeSeconds =
			Launch.MaximumSimulationTimeSeconds;
		return Result;
	}

	void PopulateTemporaryTrustRegions(
		FABTSM11FinaleLayoutPreset& Preset)
	{
		const FABTSM11FinaleLaunchInput& Nominal = Preset.NominalInput;
		for (int32 Index = 0;
			Index < FABTSM11FinaleLayoutPreset::AssistCount;
			++Index)
		{
			FABTSM11PrefixTrustRegion& Region =
				Preset.PrefixTrustRegions[Index];
			Region.PrefixLevel = Index + 1;
			Region.Minimum = FABTSM11FinaleLaunchInput{
				FMath::Max(
					Preset.LaunchModel.MinimumYawDegrees,
					Nominal.YawDegrees - 0.25),
				FMath::Max(
					Preset.LaunchModel.MinimumPitchDegrees,
					Nominal.PitchDegrees - 0.25),
				FMath::Max(
					Preset.LaunchModel.MinimumPower,
					Nominal.Power - 0.005)};
			Region.Maximum = FABTSM11FinaleLaunchInput{
				FMath::Min(
					Preset.LaunchModel.MaximumYawDegrees,
					Nominal.YawDegrees + 0.25),
				FMath::Min(
					Preset.LaunchModel.MaximumPitchDegrees,
					Nominal.PitchDegrees + 0.25),
				FMath::Min(
					Preset.LaunchModel.MaximumPower,
					Nominal.Power)};
			Region.CaptureMarginCells = 0.0;
			Region.ReleaseMarginCells = 0.0;
			// Zero is intentional: this is an experience aid, not a
			// certified Trust Region from M11-B v2.2.
			Region.RegionHash = 0;
		}
	}

	bool ConvertCandidateLayout(
		const CandidateLayout& Candidate,
		const CandidateSearchContract& Contract,
		FABTSM11FinaleLayoutPreset& OutPreset,
		FString* OutFailure)
	{
		FABTSM11FinaleLayoutPreset Preset;
		Preset.PresetVersion = 1;
		Preset.CompatibleGeneratorVersion = 3;
		Preset.CompatibleFrameLayoutVersion = 1;
		Preset.SearchAlgorithmVersion =
			Contract.AlgorithmVersion;
		Preset.ReferencePrimaryRadiusCM =
			Candidate.Scenario.GetPrimary().VisualRadiusCM;
		Preset.ReferenceLaunchRadiusCM =
			(Candidate.Launch.PouchLocalPositionCM
				- Candidate.Scenario.GetPrimary().CenterCM).Length();
		Preset.PrimaryCompatibilityToleranceCM = 25.0;
		Preset.LaunchModel =
			FromSearchLaunchModel(Candidate.Launch);
		Preset.CanonicalScenario =
			ABTSM11GravityAssistAdapter::FromCore(
				Candidate.Scenario);
		Preset.SolverConfig =
			ABTSM11GravityAssistAdapter::FromCore(
				Candidate.Solver);
		Preset.TargetApproachRadiusCM =
			Candidate.Scenario.Target.HitRadiusCM
			+ Contract.TargetCoverageMarginCM;
		Preset.NominalInput =
			FromSearchInput(Candidate.NominalInput);
		for (int32 Index = 0;
			Index < FABTSM11FinaleLayoutPreset::AssistCount;
			++Index)
		{
			Preset.MinimumCertifiedCorridorQuality[Index] =
				Candidate.Scenario.Target
					.MinimumQualifyingCorridorQuality;
			Preset.MinimumCertifiedEnergyGainCM2PerSec2[Index] =
				Candidate.Scenario.Target
					.MinimumQualifyingEnergyGainCM2PerSec2;
		}
		PopulateTemporaryTrustRegions(Preset);

		// A v2.1 Candidate has no production manifest. Its frozen search
		// identity is returned separately in OutIdentity.
		Preset.PresetSourceHash = 0;
		Preset.PresetHash = 0;
		Preset.ScanContractHash = 0;
		Preset.CertificationHash = 0;
		Preset.NominalTrajectoryHash = 0;
		Preset.PhysicalPlaybackTrajectoryHash = 0;
		Preset.CertifiedBundleHash = 0;

		FString PresetFailure;
		if (!Preset.IsValid(&PresetFailure))
		{
			return Reject(
				OutFailure,
				FString::Printf(
					TEXT("CandidatePresetInvalid:%s"),
					*PresetFailure));
		}
		OutPreset = MoveTemp(Preset);
		return true;
	}
}

#endif

bool FABTSM11CandidateExperienceIdentity::IsValid() const
{
	return Rank
			>= FABTSM11CandidateExperienceCatalog::FirstCandidateRank
		&& Rank
			<= FABTSM11CandidateExperienceCatalog::LastCandidateRank
		&& GlobalWorkIndex != 0
		&& CandidateSourceHash != 0
		&& NominalRequestHash != 0
		&& NominalResultHash != 0
		&& ScoreHash != 0;
}

FString FABTSM11CandidateExperienceIdentity::ToLogString() const
{
	return FString::Printf(
		TEXT("Rank=%d Work=%llu Source=0x%016llx ")
		TEXT("Request=0x%016llx Result=0x%016llx Score=0x%016llx"),
		Rank,
		static_cast<unsigned long long>(GlobalWorkIndex),
		static_cast<unsigned long long>(CandidateSourceHash),
		static_cast<unsigned long long>(NominalRequestHash),
		static_cast<unsigned long long>(NominalResultHash),
		static_cast<unsigned long long>(ScoreHash));
}

#if WITH_EDITOR

bool FABTSM11CandidateExperienceCatalog::BuildCandidate(
	const int32 CandidateRank,
	FABTSM11FinaleLayoutPreset& OutPreset,
	FABTSM11CandidateExperienceIdentity& OutIdentity,
	FString* OutFailure)
{
	OutPreset = FABTSM11FinaleLayoutPreset();
	OutIdentity = FABTSM11CandidateExperienceIdentity();

	const FFrozenCandidateIdentity* Frozen =
		FindFrozenCandidate(CandidateRank);
	if (Frozen == nullptr)
	{
		return Reject(
			OutFailure,
			FString::Printf(
				TEXT("CandidateRankOutsideFrozenCatalog:%d"),
				CandidateRank));
	}

	const CandidateSearchContract Contract =
		CandidateSearchContract::MakeV2_1();
	CandidateRecord Candidate;
	std::string SearchFailure;
	if (!CandidateSearch::EvaluateWorkItem(
			Contract,
			Frozen->GlobalWorkIndex,
			Candidate,
			&SearchFailure))
	{
		return Reject(
			OutFailure,
			FString::Printf(
				TEXT("CandidateRebuildFailed:%s"),
				UTF8_TO_TCHAR(SearchFailure.c_str())));
	}
	if (!Candidate.IsAccepted()
		|| Candidate.GlobalWorkIndex != Frozen->GlobalWorkIndex
		|| Candidate.CandidateSourceHash
			!= Frozen->CandidateSourceHash
		|| Candidate.NominalRequestHash
			!= Frozen->NominalRequestHash
		|| Candidate.NominalResultHash
			!= Frozen->NominalResultHash
		|| Candidate.ScoreHash != Frozen->ScoreHash
		|| ABTS::M11Search::ComputeCandidateScoreHash(Candidate)
			!= Frozen->ScoreHash)
	{
		return Reject(
			OutFailure,
			FString::Printf(
				TEXT("CandidateFrozenIdentityMismatch:")
				TEXT("Rank=%d Work=%llu Source=0x%016llx ")
				TEXT("Request=0x%016llx Result=0x%016llx ")
				TEXT("Score=0x%016llx"),
				CandidateRank,
				static_cast<unsigned long long>(
					Candidate.GlobalWorkIndex),
				static_cast<unsigned long long>(
					Candidate.CandidateSourceHash),
				static_cast<unsigned long long>(
					Candidate.NominalRequestHash),
				static_cast<unsigned long long>(
					Candidate.NominalResultHash),
				static_cast<unsigned long long>(
					Candidate.ScoreHash)));
	}

	FABTSM11FinaleLayoutPreset Preset;
	if (!ConvertCandidateLayout(
			Candidate.Layout,
			Contract,
			Preset,
			OutFailure))
	{
		return false;
	}

	FABTSM11CandidateExperienceIdentity Identity;
	Identity.Rank = Frozen->Rank;
	Identity.GlobalWorkIndex = Frozen->GlobalWorkIndex;
	Identity.CandidateSourceHash = Frozen->CandidateSourceHash;
	Identity.NominalRequestHash = Frozen->NominalRequestHash;
	Identity.NominalResultHash = Frozen->NominalResultHash;
	Identity.ScoreHash = Frozen->ScoreHash;
	if (!Identity.IsValid())
	{
		return Reject(
			OutFailure,
			TEXT("CandidateIdentityInvalid"));
	}

	OutPreset = MoveTemp(Preset);
	OutIdentity = Identity;
	return true;
}

#endif
