// Copyright Epic Games, Inc. All Rights Reserved.

#include "Contracts/ABTSM11PresentationAcceptanceContract.h"

namespace ABTSM11PresentationAcceptancePrivate
{
	constexpr uint64 FnvOffset = 1469598103934665603ull;
	constexpr uint64 FnvPrime = 1099511628211ull;

	bool Reject(FString* OutFailure, const TCHAR* Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		return false;
	}

	struct FHash
	{
		uint64 Value = FnvOffset;

		template <typename T>
		void AddPod(const T& Pod)
		{
			const uint8* Bytes = reinterpret_cast<const uint8*>(&Pod);
			for (int32 Index = 0; Index < sizeof(T); ++Index)
			{
				Value ^= Bytes[Index];
				Value *= FnvPrime;
			}
		}

		void AddBool(const bool bValue)
		{
			const uint8 Value8 = bValue ? 1u : 0u;
			AddPod(Value8);
		}

	};

	bool IsSuccessRoute(const EABTSM11PresentationRoute Route)
	{
		return Route == EABTSM11PresentationRoute::StrictF4Success
			|| Route
				== EABTSM11PresentationRoute::EarlyPhysicalContactSuccess;
	}
}

bool FABTSM11PresentationCandidateIdentity::IsValid(
	FString* OutFailure) const
{
	using namespace ABTSM11PresentationAcceptancePrivate;
	if (CandidateRank < 1 || CandidateRank > 12)
	{
		return Reject(OutFailure, TEXT("CandidateRankOutsideFrozenCatalog"));
	}
	if (GlobalWorkIndex == 0
		|| CandidateSourceHash == 0
		|| NominalRequestHash == 0
		|| NominalResultHash == 0
		|| ScoreHash == 0)
	{
		return Reject(OutFailure, TEXT("IncompleteCandidateIdentity"));
	}
	return true;
}

uint64 FABTSM11PresentationCandidateIdentity::ComputeIdentityHash() const
{
	using namespace ABTSM11PresentationAcceptancePrivate;
	FHash Hash;
	Hash.AddPod(static_cast<uint32>(0x11a1c001u));
	Hash.AddPod(CandidateRank);
	Hash.AddPod(GlobalWorkIndex);
	Hash.AddPod(CandidateSourceHash);
	Hash.AddPod(NominalRequestHash);
	Hash.AddPod(NominalResultHash);
	Hash.AddPod(ScoreHash);
	return Hash.Value;
}

bool FABTSM11PresentationAcceptancePolicy::IsValid(
	FString* OutFailure) const
{
	using namespace ABTSM11PresentationAcceptancePrivate;
	if (ContractVersion != 1
		|| RouteSchemaVersion != 1
		|| EvidenceHashSchemaVersion != 1
		|| ManifestHashSchemaVersion != 1
		|| RequiredAssistCount != 3
		|| !bAllowEarlyPhysicalContactSuccess
		|| !bAllowOrdinaryFlightFallbackForFailure)
	{
		return Reject(OutFailure, TEXT("UnsupportedPresentationAcceptancePolicy"));
	}
	return true;
}

uint64 FABTSM11PresentationAcceptancePolicy::ComputePolicyHash() const
{
	using namespace ABTSM11PresentationAcceptancePrivate;
	FHash Hash;
	Hash.AddPod(static_cast<uint32>(0x11a1c002u));
	Hash.AddPod(ContractVersion);
	Hash.AddPod(RouteSchemaVersion);
	Hash.AddPod(EvidenceHashSchemaVersion);
	Hash.AddPod(ManifestHashSchemaVersion);
	Hash.AddPod(RequiredAssistCount);
	Hash.AddBool(bAllowEarlyPhysicalContactSuccess);
	Hash.AddBool(bAllowOrdinaryFlightFallbackForFailure);
	return Hash.Value;
}

FABTSM11PresentationAcceptancePolicy
FABTSM11PresentationAcceptancePolicy::MakeV1()
{
	return FABTSM11PresentationAcceptancePolicy();
}

bool FABTSM11PresentationReplayIdentity::IsDeterministic(
	FString* OutFailure) const
{
	using namespace ABTSM11PresentationAcceptancePrivate;
	if (ReplayVersion != 1)
	{
		return Reject(OutFailure, TEXT("UnsupportedPresentationReplayVersion"));
	}
	if (ResultHash30Hz == 0
		|| ResultHash30Hz != ResultHash60Hz
		|| ResultHash30Hz != ResultHash120Hz)
	{
		return Reject(OutFailure, TEXT("PresentationReplayRateMismatch"));
	}
	return true;
}

uint64 FABTSM11PresentationReplayIdentity::ComputeIdentityHash() const
{
	using namespace ABTSM11PresentationAcceptancePrivate;
	FHash Hash;
	Hash.AddPod(static_cast<uint32>(0x11a1c003u));
	Hash.AddPod(ReplayVersion);
	Hash.AddPod(ResultHash30Hz);
	Hash.AddPod(ResultHash60Hz);
	Hash.AddPod(ResultHash120Hz);
	return Hash.Value;
}

uint64 FABTSM11PresentationRouteEvidence::ComputeEvidenceHash() const
{
	using namespace ABTSM11PresentationAcceptancePrivate;
	FHash Hash;
	Hash.AddPod(static_cast<uint32>(0x11a1e001u));
	Hash.AddPod(EvidenceVersion);
	Hash.AddPod(InputOrdinal);
	Hash.AddPod(PolicyHash);
	Hash.AddPod(CandidateIdentityHash);
	Hash.AddPod(LaunchInputHash);
	Hash.AddPod(TrajectoryHash);
	Hash.AddPod(PlaybackPlanHash);
	Hash.AddPod(ShotPlanHash);
	Hash.AddPod(TerminalPresentationHash);
	Hash.AddPod(OutcomeHash);
	Hash.AddPod(static_cast<uint8>(Route));
	Hash.AddPod(static_cast<uint8>(EndpointAuthority));
	Hash.AddPod(CompletedAssistCount);
	Hash.AddBool(bStrictF4);
	Hash.AddBool(bAuthorityDataFiniteAndOrdered);
	Hash.AddBool(bPlaybackPlanBuilt);
	Hash.AddBool(bPlaybackPlanMatchesTrajectory);
	Hash.AddBool(bShotPlanBuilt);
	Hash.AddBool(bShotPlanMatchesTrajectory);
	Hash.AddBool(bTerminalPresentationBuilt);
	Hash.AddBool(bTerminalTransferBuilt);
	Hash.AddBool(bTargetHit);
	Hash.AddBool(bCameraDirectorUnavailable);
	Hash.AddBool(bOrdinaryFlightFallbackUsed);
	Hash.AddBool(bFailedStateObserved);
	Hash.AddBool(bRecoveringStateObserved);
	Hash.AddBool(bReadyStateObserved);
	return Hash.Value;
}

bool FABTSM11PresentationRouteEvidence::IsAcceptedBy(
	const FABTSM11PresentationAcceptancePolicy& Policy,
	const FABTSM11PresentationCandidateIdentity& Candidate,
	FString* OutFailure) const
{
	using namespace ABTSM11PresentationAcceptancePrivate;
	if (!Policy.IsValid(OutFailure) || !Candidate.IsValid(OutFailure))
	{
		return false;
	}
	if (EvidenceVersion != 1
		|| InputOrdinal < 0
		|| PolicyHash != Policy.ComputePolicyHash()
		|| CandidateIdentityHash != Candidate.ComputeIdentityHash()
		|| LaunchInputHash == 0
		|| TrajectoryHash == 0
		|| PlaybackPlanHash == 0
		|| OutcomeHash == 0
		|| CompletedAssistCount < 0
		|| CompletedAssistCount > Policy.RequiredAssistCount)
	{
		return Reject(OutFailure, TEXT("IncompletePresentationRouteIdentity"));
	}
	if (EndpointAuthority != EABTSM11PresentationEndpointAuthority::None
		&& EndpointAuthority
			!= EABTSM11PresentationEndpointAuthority::CandidateQualified
		&& EndpointAuthority
			!= EABTSM11PresentationEndpointAuthority::PhysicalContact)
	{
		return Reject(OutFailure, TEXT("UnknownPresentationEndpointAuthority"));
	}
	if (!bAuthorityDataFiniteAndOrdered
		|| !bPlaybackPlanBuilt
		|| !bPlaybackPlanMatchesTrajectory)
	{
		return Reject(OutFailure, TEXT("PresentationAuthorityOrPlaybackRejected"));
	}
	if (bShotPlanBuilt != (ShotPlanHash != 0)
		|| (bShotPlanBuilt && !bShotPlanMatchesTrajectory)
		|| (!bShotPlanBuilt && bShotPlanMatchesTrajectory)
		|| bTerminalPresentationBuilt != (TerminalPresentationHash != 0)
		|| (bTerminalTransferBuilt && !bTerminalPresentationBuilt))
	{
		return Reject(OutFailure, TEXT("PresentationPlanHashMismatch"));
	}
	if (EvidenceHash == 0 || EvidenceHash != ComputeEvidenceHash())
	{
		return Reject(OutFailure, TEXT("PresentationEvidenceHashMismatch"));
	}

	const bool bSuccess = IsSuccessRoute(Route);
	if (bSuccess)
	{
		if (CompletedAssistCount != Policy.RequiredAssistCount
			|| !bShotPlanBuilt
			|| !bShotPlanMatchesTrajectory
			|| !bTerminalPresentationBuilt
			|| !bTargetHit
			|| bCameraDirectorUnavailable
			|| bOrdinaryFlightFallbackUsed
			|| bFailedStateObserved
			|| bRecoveringStateObserved
			|| bReadyStateObserved
			|| EndpointAuthority
				== EABTSM11PresentationEndpointAuthority::None)
		{
			return Reject(OutFailure, TEXT("IncompletePresentationSuccessRoute"));
		}
		if (Route == EABTSM11PresentationRoute::StrictF4Success)
		{
			return bStrictF4 && bTerminalTransferBuilt
				? true
				: Reject(OutFailure, TEXT("StrictF4PresentationRouteRejected"));
		}
		return Policy.bAllowEarlyPhysicalContactSuccess
			&& !bStrictF4
			&& EndpointAuthority
				== EABTSM11PresentationEndpointAuthority::PhysicalContact
			? true
			: Reject(OutFailure, TEXT("EarlyPhysicalContactRouteRejected"));
	}

	if (EndpointAuthority != EABTSM11PresentationEndpointAuthority::None
		|| bStrictF4
		|| bTargetHit
		|| bTerminalTransferBuilt
		|| !bFailedStateObserved
		|| !bRecoveringStateObserved
		|| !bReadyStateObserved)
	{
		return Reject(OutFailure, TEXT("FailureRecoveryStateMachineRejected"));
	}
	if (Route == EABTSM11PresentationRoute::DirectedFailureRecovery)
	{
		return CompletedAssistCount == Policy.RequiredAssistCount
			&& bShotPlanBuilt
			&& !bCameraDirectorUnavailable
			&& !bOrdinaryFlightFallbackUsed
			? true
			: Reject(OutFailure, TEXT("DirectedFailureRouteRejected"));
	}
	if (Route
		== EABTSM11PresentationRoute::OrdinaryFlightFallbackRecovery)
	{
		return Policy.bAllowOrdinaryFlightFallbackForFailure
			&& CompletedAssistCount < Policy.RequiredAssistCount
			&& !bShotPlanBuilt
			&& bCameraDirectorUnavailable
			&& bOrdinaryFlightFallbackUsed
			? true
			: Reject(OutFailure, TEXT("OrdinaryFlightFallbackRouteRejected"));
	}
	return Reject(OutFailure, TEXT("UnsupportedPresentationRoute"));
}
