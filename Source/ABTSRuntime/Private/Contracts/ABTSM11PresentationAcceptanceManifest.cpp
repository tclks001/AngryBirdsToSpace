// Copyright Epic Games, Inc. All Rights Reserved.

#include "Contracts/ABTSM11PresentationAcceptanceContract.h"

namespace ABTSM11PresentationManifestPrivate
{
	constexpr uint64 ManifestFnvOffset = 1469598103934665603ull;
	constexpr uint64 ManifestFnvPrime = 1099511628211ull;

	bool RejectManifest(FString* OutFailure, const TCHAR* Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		return false;
	}

	bool RejectManifest(FString* OutFailure, const FString& Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		return false;
	}

	struct FManifestHash
	{
		uint64 Value = ManifestFnvOffset;

		template <typename T>
		void AddPod(const T& Pod)
		{
			const uint8* Bytes = reinterpret_cast<const uint8*>(&Pod);
			for (int32 Index = 0; Index < sizeof(T); ++Index)
			{
				Value ^= Bytes[Index];
				Value *= ManifestFnvPrime;
			}
		}

		void AddBool(const bool bValue)
		{
			const uint8 Value8 = bValue ? 1u : 0u;
			AddPod(Value8);
		}

		void AddString(const FString& String)
		{
			FTCHARToUTF8 Utf8(*String);
			const int32 Length = Utf8.Length();
			AddPod(Length);
			for (int32 Index = 0; Index < Length; ++Index)
			{
				Value ^= static_cast<uint8>(Utf8.Get()[Index]);
				Value *= ManifestFnvPrime;
			}
		}
	};

	bool IsKnownManifestStrictStatus(
		const EABTSM11StrictCertificationStatus Status)
	{
		return Status == EABTSM11StrictCertificationStatus::StrictUncertified
			|| Status == EABTSM11StrictCertificationStatus::StrictCertified;
	}
}

uint64 FABTSM11PresentationAcceptanceContract::ComputeEvidenceAggregateHash(
	const TConstArrayView<FABTSM11PresentationRouteEvidence> Evidence)
{
	using namespace ABTSM11PresentationManifestPrivate;
	FManifestHash Hash;
	Hash.AddPod(static_cast<uint32>(0x11a1e002u));
	Hash.AddPod(Evidence.Num());
	for (const FABTSM11PresentationRouteEvidence& Item : Evidence)
	{
		Hash.AddPod(Item.ComputeEvidenceHash());
	}
	return Hash.Value;
}

uint64 FABTSM11PresentationAcceptanceContract::ComputeManifestHash(
	const FABTSM11PresentationAcceptanceManifest& Manifest)
{
	using namespace ABTSM11PresentationManifestPrivate;
	FManifestHash Hash;
	Hash.AddPod(static_cast<uint32>(0x11a1f001u));
	Hash.AddPod(Manifest.ManifestVersion);
	Hash.AddPod(Manifest.PolicyHash);
	Hash.AddPod(Manifest.CandidateIdentityHash);
	Hash.AddPod(Manifest.InputDomainHash);
	Hash.AddPod(Manifest.PresentationImplementationHash);
	Hash.AddPod(Manifest.ReplayIdentity.ReplayVersion);
	Hash.AddPod(Manifest.ReplayIdentity.ResultHash30Hz);
	Hash.AddPod(Manifest.ReplayIdentity.ResultHash60Hz);
	Hash.AddPod(Manifest.ReplayIdentity.ResultHash120Hz);
	Hash.AddPod(Manifest.ReplayIdentityHash);
	Hash.AddPod(static_cast<uint8>(Manifest.StrictCertificationStatus));
	Hash.AddPod(Manifest.TotalInputCount);
	Hash.AddPod(Manifest.StrictF4SuccessCount);
	Hash.AddPod(Manifest.EarlyPhysicalContactSuccessCount);
	Hash.AddPod(Manifest.DirectedFailureRecoveryCount);
	Hash.AddPod(Manifest.OrdinaryFlightFallbackRecoveryCount);
	Hash.AddPod(Manifest.RejectedInputCount);
	Hash.AddPod(Manifest.EvidenceAggregateHash);
	Hash.AddBool(Manifest.bPresentationAccepted);
	Hash.AddString(Manifest.Failure);
	return Hash.Value;
}

bool FABTSM11PresentationAcceptanceManifest::IsValidFor(
	const FABTSM11PresentationAcceptancePolicy& Policy,
	const FABTSM11PresentationCandidateIdentity& Candidate,
	FString* OutFailure) const
{
	using namespace ABTSM11PresentationManifestPrivate;
	if (!Policy.IsValid(OutFailure) || !Candidate.IsValid(OutFailure))
	{
		return false;
	}
	const int32 ClassifiedCount = StrictF4SuccessCount
		+ EarlyPhysicalContactSuccessCount
		+ DirectedFailureRecoveryCount
		+ OrdinaryFlightFallbackRecoveryCount
		+ RejectedInputCount;
	if (ManifestVersion != 1
		|| PolicyHash != Policy.ComputePolicyHash()
		|| CandidateIdentityHash != Candidate.ComputeIdentityHash()
		|| InputDomainHash == 0
		|| PresentationImplementationHash == 0
		|| !ReplayIdentity.IsDeterministic(OutFailure)
		|| ReplayIdentityHash != ReplayIdentity.ComputeIdentityHash()
		|| ReplayIdentityHash == 0
		|| !IsKnownManifestStrictStatus(StrictCertificationStatus)
		|| TotalInputCount <= 0
		|| StrictF4SuccessCount < 0
		|| EarlyPhysicalContactSuccessCount < 0
		|| DirectedFailureRecoveryCount < 0
		|| OrdinaryFlightFallbackRecoveryCount < 0
		|| RejectedInputCount < 0
		|| ClassifiedCount != TotalInputCount
		|| EvidenceAggregateHash == 0)
	{
		return RejectManifest(
			OutFailure,
			TEXT("InvalidPresentationAcceptanceManifest"));
	}
	const bool bShouldBeAccepted = RejectedInputCount == 0
		&& StrictF4SuccessCount > 0
		&& Failure.IsEmpty();
	if (bPresentationAccepted != bShouldBeAccepted)
	{
		return RejectManifest(
			OutFailure,
			TEXT("PresentationAcceptanceStateMismatch"));
	}
	if (ManifestHash == 0
		|| ManifestHash
			!= FABTSM11PresentationAcceptanceContract::ComputeManifestHash(*this))
	{
		return RejectManifest(
			OutFailure,
			TEXT("PresentationManifestHashMismatch"));
	}
	return true;
}

bool FABTSM11PresentationAcceptanceContract::BuildManifest(
	const FABTSM11PresentationAcceptancePolicy& Policy,
	const FABTSM11PresentationCandidateIdentity& Candidate,
	const EABTSM11StrictCertificationStatus StrictCertificationStatus,
	const uint64 InputDomainHash,
	const uint64 PresentationImplementationHash,
	const FABTSM11PresentationReplayIdentity& ReplayIdentity,
	const TConstArrayView<FABTSM11PresentationRouteEvidence> Evidence,
	FABTSM11PresentationAcceptanceManifest& OutManifest,
	FString* OutFailure)
{
	using namespace ABTSM11PresentationManifestPrivate;
	OutManifest = FABTSM11PresentationAcceptanceManifest();
	OutManifest.PolicyHash = Policy.ComputePolicyHash();
	OutManifest.CandidateIdentityHash = Candidate.ComputeIdentityHash();
	OutManifest.InputDomainHash = InputDomainHash;
	OutManifest.PresentationImplementationHash =
		PresentationImplementationHash;
	OutManifest.ReplayIdentity = ReplayIdentity;
	OutManifest.ReplayIdentityHash = ReplayIdentity.ComputeIdentityHash();
	OutManifest.StrictCertificationStatus = StrictCertificationStatus;
	OutManifest.TotalInputCount = Evidence.Num();
	OutManifest.EvidenceAggregateHash = ComputeEvidenceAggregateHash(Evidence);

	FString StructuralFailure;
	if (!Policy.IsValid(&StructuralFailure)
		|| !Candidate.IsValid(&StructuralFailure)
		|| !IsKnownManifestStrictStatus(StrictCertificationStatus)
		|| InputDomainHash == 0
		|| PresentationImplementationHash == 0
		|| !ReplayIdentity.IsDeterministic(&StructuralFailure)
		|| Evidence.IsEmpty())
	{
		OutManifest.Failure = StructuralFailure.IsEmpty()
			? TEXT("IncompletePresentationManifestInput")
			: StructuralFailure;
	}

	for (int32 Index = 0; Index < Evidence.Num(); ++Index)
	{
		const FABTSM11PresentationRouteEvidence& Item = Evidence[Index];
		FString ItemFailure;
		const bool bAccepted = Item.InputOrdinal == Index
			&& Item.IsAcceptedBy(Policy, Candidate, &ItemFailure);
		if (!bAccepted)
		{
			++OutManifest.RejectedInputCount;
			if (OutManifest.Failure.IsEmpty())
			{
				OutManifest.Failure = FString::Printf(
					TEXT("Input[%d]:%s"),
					Index,
					Item.InputOrdinal == Index
						? *ItemFailure
						: TEXT("NonCanonicalInputOrder"));
			}
			continue;
		}

		switch (Item.Route)
		{
		case EABTSM11PresentationRoute::StrictF4Success:
			++OutManifest.StrictF4SuccessCount;
			break;
		case EABTSM11PresentationRoute::EarlyPhysicalContactSuccess:
			++OutManifest.EarlyPhysicalContactSuccessCount;
			break;
		case EABTSM11PresentationRoute::DirectedFailureRecovery:
			++OutManifest.DirectedFailureRecoveryCount;
			break;
		case EABTSM11PresentationRoute::OrdinaryFlightFallbackRecovery:
			++OutManifest.OrdinaryFlightFallbackRecoveryCount;
			break;
		default:
			++OutManifest.RejectedInputCount;
			break;
		}
	}

	if (OutManifest.Failure.IsEmpty()
		&& OutManifest.StrictF4SuccessCount == 0)
	{
		OutManifest.Failure = TEXT("NoStrictF4PresentationSuccess");
	}
	OutManifest.bPresentationAccepted = OutManifest.Failure.IsEmpty()
		&& OutManifest.RejectedInputCount == 0
		&& OutManifest.StrictF4SuccessCount > 0;
	OutManifest.ManifestHash = ComputeManifestHash(OutManifest);

	FString ManifestFailure;
	if (!OutManifest.IsValidFor(Policy, Candidate, &ManifestFailure))
	{
		return RejectManifest(OutFailure, ManifestFailure);
	}
	if (!OutManifest.bPresentationAccepted)
	{
		return RejectManifest(OutFailure, OutManifest.Failure);
	}
	return true;
}

bool FABTSM11PresentationProductionBinding::IsUnbound() const
{
	return BindingVersion == 1
		&& CandidateRank == 0
		&& PolicyHash == 0
		&& CandidateIdentityHash == 0
		&& AcceptanceManifestHash == 0
		&& BindingHash == 0;
}

uint64 FABTSM11PresentationProductionBinding::ComputeBindingHash() const
{
	using namespace ABTSM11PresentationManifestPrivate;
	FManifestHash Hash;
	Hash.AddPod(static_cast<uint32>(0x11a1b001u));
	Hash.AddPod(BindingVersion);
	Hash.AddPod(CandidateRank);
	Hash.AddPod(PolicyHash);
	Hash.AddPod(CandidateIdentityHash);
	Hash.AddPod(AcceptanceManifestHash);
	return Hash.Value;
}

bool FABTSM11PresentationProductionBinding::IsValidFor(
	const FABTSM11PresentationAcceptancePolicy& Policy,
	const FABTSM11PresentationCandidateIdentity& Candidate,
	const FABTSM11PresentationAcceptanceManifest& Manifest,
	FString* OutFailure) const
{
	using namespace ABTSM11PresentationManifestPrivate;
	if (IsUnbound())
	{
		return RejectManifest(
			OutFailure,
			TEXT("PresentationProductionBindingUnbound"));
	}
	if (!Manifest.IsValidFor(Policy, Candidate, OutFailure)
		|| !Manifest.bPresentationAccepted)
	{
		return false;
	}
	if (BindingVersion != 1
		|| CandidateRank != Candidate.CandidateRank
		|| PolicyHash != Policy.ComputePolicyHash()
		|| CandidateIdentityHash != Candidate.ComputeIdentityHash()
		|| AcceptanceManifestHash != Manifest.ManifestHash
		|| BindingHash == 0
		|| BindingHash != ComputeBindingHash())
	{
		return RejectManifest(
			OutFailure,
			TEXT("PresentationProductionBindingMismatch"));
	}
	return true;
}

FABTSM11PresentationProductionBinding
FABTSM11PresentationAcceptanceContract::GetFrozenProductionBindingV1()
{
	// Integration will freeze a non-zero candidate/manifest tuple only after
	// the M11 full-domain replay and final visible PIE have both passed.
	return FABTSM11PresentationProductionBinding();
}

bool FABTSM11PresentationAcceptanceContract::IsProductionConsumptionAllowed(
	const FABTSM11PresentationAcceptancePolicy& Policy,
	const FABTSM11PresentationCandidateIdentity& Candidate,
	const FABTSM11PresentationAcceptanceManifest& Manifest,
	FString* OutFailure)
{
	return GetFrozenProductionBindingV1().IsValidFor(
		Policy,
		Candidate,
		Manifest,
		OutFailure);
}
