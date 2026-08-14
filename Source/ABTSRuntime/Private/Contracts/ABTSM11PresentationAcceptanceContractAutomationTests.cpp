// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Contracts/ABTSM11PresentationAcceptanceContract.h"
#include "Misc/AutomationTest.h"

namespace ABTSM11PresentationAcceptanceTestPrivate
{
	FABTSM11PresentationCandidateIdentity MakeRank12Identity()
	{
		FABTSM11PresentationCandidateIdentity Identity;
		Identity.CandidateRank = 12;
		Identity.GlobalWorkIndex = 25;
		Identity.CandidateSourceHash = 0x58840ee73ddd70f5ull;
		Identity.NominalRequestHash = 0xf76a37a38221a425ull;
		Identity.NominalResultHash = 0xf746bbe4ca7b9748ull;
		Identity.ScoreHash = 0xf364c0098bec8112ull;
		return Identity;
	}

	FABTSM11PresentationRouteEvidence MakeStrictSuccess(
		const FABTSM11PresentationAcceptancePolicy& Policy,
		const FABTSM11PresentationCandidateIdentity& Candidate,
		const int32 InputOrdinal)
	{
		FABTSM11PresentationRouteEvidence Evidence;
		Evidence.InputOrdinal = InputOrdinal;
		Evidence.PolicyHash = Policy.ComputePolicyHash();
		Evidence.CandidateIdentityHash = Candidate.ComputeIdentityHash();
		Evidence.LaunchInputHash = 0x1000ull + InputOrdinal;
		Evidence.TrajectoryHash = 0x2000ull + InputOrdinal;
		Evidence.PlaybackPlanHash = 0x3000ull + InputOrdinal;
		Evidence.ShotPlanHash = 0x4000ull + InputOrdinal;
		Evidence.TerminalPresentationHash = 0x5000ull + InputOrdinal;
		Evidence.OutcomeHash = 0x6000ull + InputOrdinal;
		Evidence.Route = EABTSM11PresentationRoute::StrictF4Success;
		Evidence.EndpointAuthority =
			EABTSM11PresentationEndpointAuthority::CandidateQualified;
		Evidence.CompletedAssistCount = Policy.RequiredAssistCount;
		Evidence.bStrictF4 = true;
		Evidence.bAuthorityDataFiniteAndOrdered = true;
		Evidence.bPlaybackPlanBuilt = true;
		Evidence.bPlaybackPlanMatchesTrajectory = true;
		Evidence.bShotPlanBuilt = true;
		Evidence.bShotPlanMatchesTrajectory = true;
		Evidence.bTerminalPresentationBuilt = true;
		Evidence.bTerminalTransferBuilt = true;
		Evidence.bTargetHit = true;
		Evidence.EvidenceHash = Evidence.ComputeEvidenceHash();
		return Evidence;
	}

	FABTSM11PresentationRouteEvidence MakeEarlyPhysicalSuccess(
		const FABTSM11PresentationAcceptancePolicy& Policy,
		const FABTSM11PresentationCandidateIdentity& Candidate,
		const int32 InputOrdinal)
	{
		FABTSM11PresentationRouteEvidence Evidence =
			MakeStrictSuccess(Policy, Candidate, InputOrdinal);
		Evidence.Route =
			EABTSM11PresentationRoute::EarlyPhysicalContactSuccess;
		Evidence.EndpointAuthority =
			EABTSM11PresentationEndpointAuthority::PhysicalContact;
		Evidence.bStrictF4 = false;
		Evidence.bTerminalTransferBuilt = false;
		Evidence.EvidenceHash = Evidence.ComputeEvidenceHash();
		return Evidence;
	}

	FABTSM11PresentationRouteEvidence MakeDirectedFailure(
		const FABTSM11PresentationAcceptancePolicy& Policy,
		const FABTSM11PresentationCandidateIdentity& Candidate,
		const int32 InputOrdinal)
	{
		FABTSM11PresentationRouteEvidence Evidence =
			MakeStrictSuccess(Policy, Candidate, InputOrdinal);
		Evidence.Route = EABTSM11PresentationRoute::DirectedFailureRecovery;
		Evidence.EndpointAuthority =
			EABTSM11PresentationEndpointAuthority::None;
		Evidence.TerminalPresentationHash = 0;
		Evidence.bStrictF4 = false;
		Evidence.bTerminalPresentationBuilt = false;
		Evidence.bTerminalTransferBuilt = false;
		Evidence.bTargetHit = false;
		Evidence.bFailedStateObserved = true;
		Evidence.bRecoveringStateObserved = true;
		Evidence.bReadyStateObserved = true;
		Evidence.EvidenceHash = Evidence.ComputeEvidenceHash();
		return Evidence;
	}

	FABTSM11PresentationRouteEvidence MakeFallbackFailure(
		const FABTSM11PresentationAcceptancePolicy& Policy,
		const FABTSM11PresentationCandidateIdentity& Candidate,
		const int32 InputOrdinal)
	{
		FABTSM11PresentationRouteEvidence Evidence =
			MakeDirectedFailure(Policy, Candidate, InputOrdinal);
		Evidence.Route =
			EABTSM11PresentationRoute::OrdinaryFlightFallbackRecovery;
		Evidence.CompletedAssistCount = 2;
		Evidence.ShotPlanHash = 0;
		Evidence.bShotPlanBuilt = false;
		Evidence.bShotPlanMatchesTrajectory = false;
		Evidence.bCameraDirectorUnavailable = true;
		Evidence.bOrdinaryFlightFallbackUsed = true;
		Evidence.EvidenceHash = Evidence.ComputeEvidenceHash();
		return Evidence;
	}

	FABTSM11PresentationReplayIdentity MakeDeterministicReplay()
	{
		FABTSM11PresentationReplayIdentity Replay;
		Replay.ResultHash30Hz = 0x1203060ull;
		Replay.ResultHash60Hz = Replay.ResultHash30Hz;
		Replay.ResultHash120Hz = Replay.ResultHash30Hz;
		return Replay;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11PresentationRouteContractTest,
	"ABTS.Contracts.M11PresentationAcceptance.Routes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11PresentationRouteContractTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM11PresentationAcceptanceTestPrivate;
	(void)Parameters;

	const FABTSM11PresentationAcceptancePolicy Policy =
		FABTSM11PresentationAcceptancePolicy::MakeV1();
	const FABTSM11PresentationCandidateIdentity Candidate =
		MakeRank12Identity();
	TestTrue(TEXT("Frozen v1 policy is valid"), Policy.IsValid());
	TestTrue(TEXT("Rank12 frozen identity is valid"), Candidate.IsValid());

	const FABTSM11PresentationRouteEvidence Strict =
		MakeStrictSuccess(Policy, Candidate, 0);
	const FABTSM11PresentationRouteEvidence Early =
		MakeEarlyPhysicalSuccess(Policy, Candidate, 1);
	const FABTSM11PresentationRouteEvidence DirectedFailure =
		MakeDirectedFailure(Policy, Candidate, 2);
	const FABTSM11PresentationRouteEvidence FallbackFailure =
		MakeFallbackFailure(Policy, Candidate, 3);
	TestTrue(TEXT("Strict F4 success route is accepted"),
		Strict.IsAcceptedBy(Policy, Candidate));
	TestTrue(TEXT("Early physical contact is a presentation success only"),
		Early.IsAcceptedBy(Policy, Candidate));
	TestTrue(TEXT("Directed failure closes Failed-Recovering-Ready"),
		DirectedFailure.IsAcceptedBy(Policy, Candidate));
	TestTrue(TEXT("Incomplete assist events may use the ordinary flight fallback"),
		FallbackFailure.IsAcceptedBy(Policy, Candidate));

	FABTSM11PresentationRouteEvidence Tampered = Strict;
	Tampered.PlaybackPlanHash++;
	TestFalse(TEXT("Plan hash tampering fails closed"),
		Tampered.IsAcceptedBy(Policy, Candidate));

	FABTSM11PresentationRouteEvidence EarlyBeforeThreeAssists = Early;
	EarlyBeforeThreeAssists.CompletedAssistCount = 2;
	EarlyBeforeThreeAssists.EvidenceHash =
		EarlyBeforeThreeAssists.ComputeEvidenceHash();
	TestFalse(TEXT("Early contact cannot skip an assist"),
		EarlyBeforeThreeAssists.IsAcceptedBy(Policy, Candidate));

	FABTSM11PresentationRouteEvidence FallbackWithAuthority = FallbackFailure;
	FallbackWithAuthority.EndpointAuthority =
		EABTSM11PresentationEndpointAuthority::CandidateQualified;
	FallbackWithAuthority.EvidenceHash =
		FallbackWithAuthority.ComputeEvidenceHash();
	TestFalse(TEXT("A failure fallback cannot acquire success authority"),
		FallbackWithAuthority.IsAcceptedBy(Policy, Candidate));

	FABTSM11PresentationRouteEvidence OpenRecovery = DirectedFailure;
	OpenRecovery.bReadyStateObserved = false;
	OpenRecovery.EvidenceHash = OpenRecovery.ComputeEvidenceHash();
	TestFalse(TEXT("Failure recovery must close back to Ready"),
		OpenRecovery.IsAcceptedBy(Policy, Candidate));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11PresentationManifestContractTest,
	"ABTS.Contracts.M11PresentationAcceptance.Manifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11PresentationManifestContractTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM11PresentationAcceptanceTestPrivate;
	(void)Parameters;

	const FABTSM11PresentationAcceptancePolicy Policy =
		FABTSM11PresentationAcceptancePolicy::MakeV1();
	const FABTSM11PresentationCandidateIdentity Candidate =
		MakeRank12Identity();
	const FABTSM11PresentationReplayIdentity Replay =
		MakeDeterministicReplay();
	TArray<FABTSM11PresentationRouteEvidence> Evidence;
	Evidence.Add(MakeStrictSuccess(Policy, Candidate, 0));
	Evidence.Add(MakeEarlyPhysicalSuccess(Policy, Candidate, 1));
	Evidence.Add(MakeDirectedFailure(Policy, Candidate, 2));
	Evidence.Add(MakeFallbackFailure(Policy, Candidate, 3));

	FABTSM11PresentationAcceptanceManifest Manifest;
	FString Failure;
	TestTrue(
		TEXT("A complete deterministic route domain is PresentationAccepted"),
		FABTSM11PresentationAcceptanceContract::BuildManifest(
			Policy,
			Candidate,
			EABTSM11StrictCertificationStatus::StrictUncertified,
			0x404201ull,
			0x11c0ffeeull,
			Replay,
			Evidence,
			Manifest,
			&Failure));
	if (!Failure.IsEmpty())
	{
		AddError(FString::Printf(TEXT("Unexpected manifest failure: %s"),
			*Failure));
	}
	TestTrue(TEXT("Manifest publishes PresentationAccepted"),
		Manifest.bPresentationAccepted);
	TestEqual(TEXT("Strict certification remains explicitly uncertified"),
		Manifest.StrictCertificationStatus,
		EABTSM11StrictCertificationStatus::StrictUncertified);
	TestEqual(TEXT("All four route classes are counted"),
		Manifest.TotalInputCount, 4);
	TestEqual(TEXT("No input is rejected"), Manifest.RejectedInputCount, 0);
	TestTrue(TEXT("Sealed manifest validates against its frozen identities"),
		Manifest.IsValidFor(Policy, Candidate));

	FABTSM11PresentationAcceptanceManifest RepeatManifest;
	TestTrue(TEXT("The same ordered evidence seals again"),
		FABTSM11PresentationAcceptanceContract::BuildManifest(
			Policy,
			Candidate,
			EABTSM11StrictCertificationStatus::StrictUncertified,
			0x404201ull,
			0x11c0ffeeull,
			Replay,
			Evidence,
			RepeatManifest));
	TestEqual(TEXT("Manifest hash is deterministic"),
		RepeatManifest.ManifestHash, Manifest.ManifestHash);

	FABTSM11PresentationReplayIdentity RateMismatch = Replay;
	RateMismatch.ResultHash60Hz++;
	FABTSM11PresentationAcceptanceManifest RejectedManifest;
	TestFalse(TEXT("30/60/120 Hz disagreement rejects the candidate"),
		FABTSM11PresentationAcceptanceContract::BuildManifest(
			Policy,
			Candidate,
			EABTSM11StrictCertificationStatus::StrictUncertified,
			0x404201ull,
			0x11c0ffeeull,
			RateMismatch,
			Evidence,
			RejectedManifest));

	TArray<FABTSM11PresentationRouteEvidence> NoStrictSuccess;
	NoStrictSuccess.Add(MakeEarlyPhysicalSuccess(Policy, Candidate, 0));
	NoStrictSuccess.Add(MakeFallbackFailure(Policy, Candidate, 1));
	TestFalse(TEXT("Early contacts alone cannot replace the strict F4 route"),
		FABTSM11PresentationAcceptanceContract::BuildManifest(
			Policy,
			Candidate,
			EABTSM11StrictCertificationStatus::StrictUncertified,
			0x404201ull,
			0x11c0ffeeull,
			Replay,
			NoStrictSuccess,
			RejectedManifest));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11PresentationProductionBindingContractTest,
	"ABTS.Contracts.M11PresentationAcceptance.ProductionBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11PresentationProductionBindingContractTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM11PresentationAcceptanceTestPrivate;
	(void)Parameters;

	const FABTSM11PresentationAcceptancePolicy Policy =
		FABTSM11PresentationAcceptancePolicy::MakeV1();
	const FABTSM11PresentationCandidateIdentity Candidate =
		MakeRank12Identity();
	TArray<FABTSM11PresentationRouteEvidence> Evidence;
	Evidence.Add(MakeStrictSuccess(Policy, Candidate, 0));
	Evidence.Add(MakeFallbackFailure(Policy, Candidate, 1));
	FABTSM11PresentationAcceptanceManifest Manifest;
	TestTrue(TEXT("Fixture manifest is accepted"),
		FABTSM11PresentationAcceptanceContract::BuildManifest(
			Policy,
			Candidate,
			EABTSM11StrictCertificationStatus::StrictUncertified,
			0x404201ull,
			0x11c0ffeeull,
			MakeDeterministicReplay(),
			Evidence,
			Manifest));

	FString Failure;
	TestFalse(TEXT("Accepted evidence does not self-authorize production"),
		FABTSM11PresentationAcceptanceContract::IsProductionConsumptionAllowed(
			Policy,
			Candidate,
			Manifest,
			&Failure));
	TestEqual(TEXT("The current Integration binding is explicitly unbound"),
		Failure, FString(TEXT("PresentationProductionBindingUnbound")));

	FABTSM11PresentationProductionBinding HypotheticalBinding;
	HypotheticalBinding.CandidateRank = Candidate.CandidateRank;
	HypotheticalBinding.PolicyHash = Policy.ComputePolicyHash();
	HypotheticalBinding.CandidateIdentityHash = Candidate.ComputeIdentityHash();
	HypotheticalBinding.AcceptanceManifestHash = Manifest.ManifestHash;
	HypotheticalBinding.BindingHash = HypotheticalBinding.ComputeBindingHash();
	TestTrue(TEXT("An exact future Integration tuple can authorize production"),
		HypotheticalBinding.IsValidFor(Policy, Candidate, Manifest));

	FABTSM11PresentationAcceptanceManifest TamperedManifest = Manifest;
	TamperedManifest.InputDomainHash++;
	TestFalse(TEXT("A rebound or tampered manifest fails closed"),
		HypotheticalBinding.IsValidFor(Policy, Candidate, TamperedManifest));
	return true;
}

#endif
