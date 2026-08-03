// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11CandidateExperienceCatalog.h"

#if WITH_EDITOR

#include "HAL/IConsoleManager.h"
#include "M11Search/ABTSM11CandidateSearch.h"
#include "M11Search/ABTSM11FrozenCandidateLayouts.h"
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

	TAutoConsoleVariable<int32> CVarABTSM11CandidateRank(
		TEXT("abts.M11.CandidateRank"),
		0,
		TEXT("Editor-only M11 Candidate layout selection. ")
		TEXT("0 keeps the production Certified v1 bundle; ")
		TEXT("1..11 load the corresponding frozen, UNCERTIFIED ")
		TEXT("M11-B Candidate. Set before PIE and restart PIE after changing."),
		ECVF_Default);

	struct FFrozenCandidateIdentity
	{
		int32 Rank;
		uint64 GlobalWorkIndex;
		uint64 CandidateSourceHash;
		uint64 NominalRequestHash;
		uint64 NominalResultHash;
		uint64 ScoreHash;
	};

	constexpr std::array<FFrozenCandidateIdentity, 11> FrozenCandidates = {{
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
			0xdd1613e3dbb4c1b0ull},
		{
			3,
			20ull,
			0xed74ffaf0de8028full,
			0x19a6a15736704d7bull,
			0x791c9a64b195b0d4ull,
			0x938f4825be418ebeull},
		{
			4,
			20ull,
			0xf22ad256fd791e07ull,
			0xa8fdff5512fc4743ull,
			0xbf710eb5c1e114c1ull,
			0xfee62a58f2e1dfb7ull},
		{
			5,
			30ull,
			0xcdc6e41075d99493ull,
			0xfb9a637bf71a38dfull,
			0xa7695a10b44f8281ull,
			0x4689059277f93880ull},
		{
			6,
			30ull,
			0x80d274a67e1e9944ull,
			0x3e64212a606348f0ull,
			0x9de084d9f77c9ee7ull,
			0xf8b1ff45fa8f1adfull},
		{
			7,
			353ull,
			0xb3e0f00ca35d499aull,
			0x48ffe272661916b2ull,
			0xe7c6c093e3cc9533ull,
			0x0baef62a673e8e55ull},
		{
			8,
			21ull,
			0x617687274ed0c29aull,
			0xa2a41077916aadb2ull,
			0xaac8ba98079011fdull,
			0xb77f6d2f3f954005ull},
		{
			9,
			22ull,
			0x166f0aa067d54328ull,
			0x11e775a2b20e0b64ull,
			0x22675cdfb00406d5ull,
			0xa9bd918ee812d572ull},
		{
			10,
			23ull,
			0x2b06db2cf348d75full,
			0xa1d91650dc3d3f36ull,
			0x99012cedf3d01c06ull,
			0x22c3f67f46d49e70ull},
		{
			11,
			25ull,
			0xcb23499fc6f7c9d3ull,
			0x4f0e3c66a1a0a737ull,
			0x505f3312ac8ae07full,
			0xd71f1166493c07aaull}
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

int32 FABTSM11CandidateExperienceCatalog::GetRequestedCandidateRank()
{
	return CVarABTSM11CandidateRank.GetValueOnGameThread();
}

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
	if (CandidateRank >= 3)
	{
		std::string LayoutFailure;
		if (!ABTS::M11Search::BuildFrozenV4CandidateLayout(
				CandidateRank,
				Candidate.Layout)
			|| !Candidate.Layout.IsValid(&LayoutFailure)
			|| ComputeCandidateSourceHash(Candidate.Layout, Contract)
				!= Frozen->CandidateSourceHash)
		{
			return Reject(
				OutFailure,
				FString::Printf(
					TEXT("FrozenV4CandidateLayoutRejected:")
					TEXT("Rank=%d Source=0x%016llx Detail=%s"),
					CandidateRank,
					static_cast<unsigned long long>(
						ComputeCandidateSourceHash(
							Candidate.Layout,
							Contract)),
					UTF8_TO_TCHAR(LayoutFailure.c_str())));
		}
		Candidate.GlobalWorkIndex = Frozen->GlobalWorkIndex;
		Candidate.Status = ABTS::M11Search::EvaluationStatus::Accepted;
		Candidate.CandidateSourceHash = Frozen->CandidateSourceHash;
		Candidate.NominalRequestHash = Frozen->NominalRequestHash;
		Candidate.NominalResultHash = Frozen->NominalResultHash;
		Candidate.ScoreHash = Frozen->ScoreHash;
	}
	else
	{
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
