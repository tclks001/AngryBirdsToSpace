// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "World/ABTSM11FinaleActors.h"
#include "World/ABTSM11FinaleLayoutCertification.h"
#include "World/ABTSM11FinaleLayoutSearch.h"
#include "World/ABTSM11GravityAssistSolver.h"

namespace
{
	void RefreshOfflineM11BIdentity(
		FABTSM11FinaleLayoutPreset& Preset)
	{
		Preset.PresetSourceHash = 0;
		Preset.PresetHash = 0;
		Preset.ScanContractHash = 0;
		Preset.CertificationHash = 0;
		Preset.NominalTrajectoryHash = 0;
		Preset.PhysicalPlaybackTrajectoryHash = 0;
		Preset.CertifiedBundleHash = 0;
		Preset.CanonicalScenario.ScenarioHash = 1;
		Preset.PresetSourceHash =
			FABTSM11FinaleLayoutHash::ComputePresetSourceHash(Preset);
		Preset.PresetHash =
			FABTSM11FinaleLayoutHash::ComputePresetHash(Preset);
		Preset.CanonicalScenario.ScenarioHash =
			FABTSM11FinaleLayoutHash::FoldScenarioHash(
				Preset.PresetHash);
		Preset.ScanContractHash =
			FABTSM11FinaleLayoutHash::ComputeScanContractHash(Preset);
	}

	FABTSM11FinaleLayoutPreset MakeOfflineM11BCertificationCandidate()
	{
		FABTSM11FinaleLayoutPreset Preset =
			FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
		RefreshOfflineM11BIdentity(Preset);
		return Preset;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11BLaunchModelContractTest,
	"ABTS.M11B.Unit.LaunchModelContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11BLaunchModelContractTest::RunTest(const FString& Parameters)
{
	const FABTSM11FinaleLayoutPreset Preset =
		FABTSM11FinaleLayoutPreset::MakeConstructiveSearchSeed();
	FString Failure;
	TestTrue(TEXT("Search seed is a valid local-layout contract"),
		Preset.IsValid(&Failure));

	FABTSM11FinaleLaunchInput Zero;
	Zero.YawDegrees = 0.0;
	Zero.PitchDegrees = 0.0;
	Zero.Power = 0.5;
	FABTSM11FinaleLaunchInput Yaw = Zero;
	Yaw.YawDegrees = 10.0;
	FABTSM11FinaleLaunchInput Pitch = Zero;
	Pitch.PitchDegrees = 10.0;
	TestTrue(TEXT("Zero angles map exactly to +X"),
		Preset.LaunchModel.MapDirection(Zero).Equals(
			FVector3d(1.0, 0.0, 0.0), 1.0e-12));
	TestTrue(TEXT("Positive yaw rotates toward +Y"),
		Preset.LaunchModel.MapDirection(Yaw).Y > 0.0
			&& FMath::Abs(Preset.LaunchModel.MapDirection(Yaw).Z) < 1.0e-12);
	TestTrue(TEXT("Positive pitch rotates toward +Z"),
		Preset.LaunchModel.MapDirection(Pitch).Z > 0.0
			&& FMath::Abs(Preset.LaunchModel.MapDirection(Pitch).Y) < 1.0e-12);

	FABTSM11FinaleLaunchInput Minimum = Zero;
	Minimum.Power = Preset.LaunchModel.MinimumPower;
	FABTSM11FinaleLaunchInput Mid = Zero;
	Mid.Power = FMath::Lerp(
		Preset.LaunchModel.MinimumPower,
		Preset.LaunchModel.MaximumPower,
		0.5);
	FABTSM11FinaleLaunchInput Maximum = Zero;
	Maximum.Power = Preset.LaunchModel.MaximumPower;
	TestEqual(TEXT("Minimum power maps to minimum speed"),
		Preset.LaunchModel.MapSpeedCMPerSec(Minimum),
		Preset.LaunchModel.MinimumLaunchSpeedCMPerSec);
	TestEqual(TEXT("Maximum power maps to maximum speed"),
		Preset.LaunchModel.MapSpeedCMPerSec(Maximum),
		Preset.LaunchModel.MaximumLaunchSpeedCMPerSec);
	TestTrue(TEXT("Power interpolation is exactly linear"),
		FMath::IsNearlyEqual(
			Preset.LaunchModel.MapSpeedCMPerSec(Mid),
			0.5 * (
				Preset.LaunchModel.MinimumLaunchSpeedCMPerSec
				+ Preset.LaunchModel.MaximumLaunchSpeedCMPerSec),
			1.0e-12));
	TestTrue(TEXT("Power never changes direction"),
		Preset.LaunchModel.MapDirection(Minimum).Equals(
			Preset.LaunchModel.MapDirection(Maximum), 0.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11BScanAndSearchContractsTest,
	"ABTS.M11B.Unit.ScanAndSearchContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11BScanAndSearchContractsTest::RunTest(
	const FString& Parameters)
{
	const FABTSM11FinaleLayoutPreset Preset =
		FABTSM11FinaleLayoutPreset::MakeConstructiveSearchSeed();
	FString Failure;

	FABTSM11LayoutScanContract ScanContract = Preset.ScanContract;
	ScanContract.bIncludeHalfCellOffsetPass = false;
	TestFalse(
		TEXT("Scan-contract v2 requires the half-cell discovery pass"),
		ScanContract.IsValid(Preset.LaunchModel, &Failure));
	TestEqual(
		TEXT("Missing half-cell pass has a stable rejection reason"),
		Failure,
		FString(TEXT("UnsupportedScanContract")));

	FABTSM11InputGrid Grid;
	Grid.Minimum.YawDegrees = -1.0;
	Grid.Minimum.PitchDegrees = 29.0;
	Grid.Minimum.Power = 0.9;
	Grid.Maximum.YawDegrees = 1.0;
	Grid.Maximum.PitchDegrees = 31.0;
	Grid.Maximum.Power = 1.0;
	Grid.YawStepDegrees = 0.5;
	Grid.PitchStepDegrees = 0.5;
	Grid.PowerStep = 0.025;
	Failure.Reset();
	TestTrue(
		*FString::Printf(
			TEXT("Integral closed input grid is valid: %s"),
			*Failure),
		Grid.IsValid(Preset.LaunchModel, &Failure));
	const FABTSM11FinaleLaunchInput LastInput = Grid.GetInput(
		Grid.GetYawCount() - 1,
		Grid.GetPitchCount() - 1,
		Grid.GetPowerCount() - 1);
	TestEqual(
		TEXT("Yaw maximum is sampled exactly"),
		LastInput.YawDegrees,
		Grid.Maximum.YawDegrees);
	TestEqual(
		TEXT("Pitch maximum is sampled exactly"),
		LastInput.PitchDegrees,
		Grid.Maximum.PitchDegrees);
	TestEqual(
		TEXT("Power maximum is sampled exactly"),
		LastInput.Power,
		Grid.Maximum.Power);

	FABTSM11InputGrid InvalidGrid = Grid;
	InvalidGrid.YawStepDegrees = 0.3;
	Failure.Reset();
	TestFalse(
		TEXT("Yaw step must tile the closed interval"),
		InvalidGrid.IsValid(Preset.LaunchModel, &Failure));
	TestEqual(
		TEXT("Non-integral yaw step has a stable rejection reason"),
		Failure,
		FString(TEXT("NonIntegralInputGrid")));

	InvalidGrid = Grid;
	InvalidGrid.PitchStepDegrees = 0.3;
	Failure.Reset();
	TestFalse(
		TEXT("Pitch step must tile the closed interval"),
		InvalidGrid.IsValid(Preset.LaunchModel, &Failure));
	TestEqual(
		TEXT("Non-integral pitch step has a stable rejection reason"),
		Failure,
		FString(TEXT("NonIntegralInputGrid")));

	InvalidGrid = Grid;
	InvalidGrid.PowerStep = 0.03;
	Failure.Reset();
	TestFalse(
		TEXT("Power step must tile the closed interval"),
		InvalidGrid.IsValid(Preset.LaunchModel, &Failure));
	TestEqual(
		TEXT("Non-integral power step has a stable rejection reason"),
		Failure,
		FString(TEXT("NonIntegralInputGrid")));

	FABTSM11FinaleSearchConfig SearchConfig;
	Failure.Reset();
	TestTrue(
		*FString::Printf(
			TEXT("Default constructive-search contract is valid: %s"),
			*Failure),
		SearchConfig.IsValid(&Failure));
	TestTrue(
		TEXT("Default search requires nominal plus face-neighbor robustness"),
		SearchConfig.MinimumRobustSurvivorCount >= 2);

	SearchConfig.MinimumRobustSurvivorCount = 1;
	Failure.Reset();
	TestFalse(
		TEXT("A nominal-only robust survivor count is rejected"),
		SearchConfig.IsValid(&Failure));
	SearchConfig.MinimumRobustSurvivorCount = 8;
	Failure.Reset();
	TestFalse(
		TEXT("Robust survivor count cannot exceed nominal plus six faces"),
		SearchConfig.IsValid(&Failure));

	SearchConfig = FABTSM11FinaleSearchConfig();
	SearchConfig.SearchSeed = 0;
	Failure.Reset();
	TestFalse(
		TEXT("Deterministic tie-break seed must be positive"),
		SearchConfig.IsValid(&Failure));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11BCertifiedBundleIdentityTest,
	"ABTS.M11B.Unit.CertifiedBundleIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11BCertifiedBundleIdentityTest::RunTest(
	const FString& Parameters)
{
	const FABTSM11FinaleLayoutPreset Preset =
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
	FString Failure;
	AddInfo(FString::Printf(
		TEXT("M11-B frozen manifest: Source=0x%016llx Preset=0x%016llx Scenario=0x%08x Scan=0x%016llx Certification=0x%016llx Nominal=0x%016llx PlaybackV=%d Playback=0x%016llx Bundle=0x%016llx ComputedBundle=0x%016llx"),
		static_cast<unsigned long long>(Preset.PresetSourceHash),
		static_cast<unsigned long long>(Preset.PresetHash),
		Preset.CanonicalScenario.ScenarioHash,
		static_cast<unsigned long long>(Preset.ScanContractHash),
		static_cast<unsigned long long>(Preset.CertificationHash),
		static_cast<unsigned long long>(Preset.NominalTrajectoryHash),
		Preset.PhysicalPlaybackContractVersion,
		static_cast<unsigned long long>(
			Preset.PhysicalPlaybackTrajectoryHash),
		static_cast<unsigned long long>(Preset.CertifiedBundleHash),
		static_cast<unsigned long long>(
			FABTSM11FinaleLayoutHash::ComputeCertifiedBundleHash(Preset))));
	TestTrue(
		*FString::Printf(TEXT("Frozen certified bundle is valid: %s"), *Failure),
		Preset.IsValid(&Failure));
	TestTrue(TEXT("Preset source identity is frozen"),
		Preset.PresetSourceHash != 0);
	TestEqual(TEXT("Preset source identity replays exactly"),
		Preset.PresetSourceHash,
		FABTSM11FinaleLayoutHash::ComputePresetSourceHash(Preset));
	TestEqual(TEXT("Preset compatibility identity replays exactly"),
		Preset.PresetHash,
		FABTSM11FinaleLayoutHash::ComputePresetHash(Preset));
	TestEqual(TEXT("Preset source and v1 compatibility identities agree"),
		Preset.PresetSourceHash,
		Preset.PresetHash);
	TestEqual(TEXT("Scenario identity is derived from the preset identity"),
		Preset.CanonicalScenario.ScenarioHash,
		FABTSM11FinaleLayoutHash::FoldScenarioHash(Preset.PresetHash));
	TestEqual(TEXT("Scan-contract identity replays exactly"),
		Preset.ScanContractHash,
		FABTSM11FinaleLayoutHash::ComputeScanContractHash(Preset));
	TestTrue(TEXT("Certification identity is frozen"),
		Preset.CertificationHash != 0);
	TestTrue(TEXT("Nominal trajectory identity is frozen"),
		Preset.NominalTrajectoryHash != 0);
	TestEqual(TEXT("Physical playback contract version is frozen"),
		Preset.PhysicalPlaybackContractVersion, 1);
	TestTrue(TEXT("Physical playback trajectory identity is frozen"),
		Preset.PhysicalPlaybackTrajectoryHash != 0);
	for (int32 Index = 0;
		Index < FABTSM11FinaleLayoutPreset::AssistCount;
		++Index)
	{
		const FABTSM11PrefixTrustRegion& Region =
			Preset.PrefixTrustRegions[Index];
		TestTrue(
			*FString::Printf(TEXT("F%d trust region is valid"), Index + 1),
			Region.IsValid(Preset.LaunchModel));
		TestEqual(
			*FString::Printf(
				TEXT("F%d trust-region identity replays exactly"),
				Index + 1),
			Region.RegionHash,
			FABTSM11FinaleLayoutHash::ComputeTrustRegionHash(Region));
	}
	TestTrue(TEXT("Certified bundle identity is frozen"),
		Preset.CertifiedBundleHash != 0);
	TestEqual(TEXT("Certified bundle identity replays exactly"),
		Preset.CertifiedBundleHash,
		FABTSM11FinaleLayoutHash::ComputeCertifiedBundleHash(Preset));

	FABTSM11FinaleLayoutPreset ScenarioTamper = Preset;
	ScenarioTamper.CanonicalScenario.ScenarioHash ^= 0x1u;
	TestFalse(TEXT("A wrong non-zero scenario identity is rejected"),
		ScenarioTamper.IsValid(&Failure));

	FABTSM11FinaleLayoutPreset SourceIdentityTamper = Preset;
	SourceIdentityTamper.PresetSourceHash ^= 0x1ull;
	TestFalse(TEXT("A wrong non-zero source identity is rejected"),
		SourceIdentityTamper.IsValid(&Failure));

	FABTSM11FinaleLayoutPreset PresetIdentityTamper = Preset;
	PresetIdentityTamper.PresetHash ^= 0x1ull;
	TestFalse(TEXT("A wrong non-zero preset identity is rejected"),
		PresetIdentityTamper.IsValid(&Failure));

	FABTSM11FinaleLayoutPreset ScanIdentityTamper = Preset;
	ScanIdentityTamper.ScanContractHash ^= 0x1ull;
	TestFalse(TEXT("A wrong non-zero scan identity is rejected"),
		ScanIdentityTamper.IsValid(&Failure));

	FABTSM11FinaleLayoutPreset TrustFieldTamper = Preset;
	TrustFieldTamper.PrefixTrustRegions[0].Minimum.YawDegrees += 0.01;
	TestFalse(TEXT("A trust field tamper is rejected by its region hash"),
		TrustFieldTamper.IsValid(&Failure));

	FABTSM11FinaleLayoutPreset RehashedTrustFieldTamper = Preset;
	RehashedTrustFieldTamper.PrefixTrustRegions[0].Minimum.YawDegrees += 0.01;
	RehashedTrustFieldTamper.PrefixTrustRegions[0].RegionHash =
		FABTSM11FinaleLayoutHash::ComputeTrustRegionHash(
			RehashedTrustFieldTamper.PrefixTrustRegions[0]);
	TestFalse(
		TEXT("A rehashed trust field tamper is rejected by the bundle"),
		RehashedTrustFieldTamper.IsValid(&Failure));

	FABTSM11FinaleLayoutPreset TrustHashTamper = Preset;
	TrustHashTamper.PrefixTrustRegions[1].RegionHash ^= 0x1ull;
	TestFalse(TEXT("A wrong non-zero trust-region identity is rejected"),
		TrustHashTamper.IsValid(&Failure));

	FABTSM11FinaleLayoutPreset CertificationTamper = Preset;
	CertificationTamper.CertificationHash ^= 0x1ull;
	TestFalse(TEXT("A certification tamper is rejected by the bundle"),
		CertificationTamper.IsValid(&Failure));

	FABTSM11FinaleLayoutPreset NominalTrajectoryTamper = Preset;
	NominalTrajectoryTamper.NominalTrajectoryHash ^= 0x1ull;
	TestFalse(TEXT("A nominal-trajectory tamper is rejected by the bundle"),
		NominalTrajectoryTamper.IsValid(&Failure));

	FABTSM11FinaleLayoutPreset PlaybackVersionTamper = Preset;
	PlaybackVersionTamper.PhysicalPlaybackContractVersion = 2;
	TestFalse(TEXT("An unsupported physical-playback version is rejected"),
		PlaybackVersionTamper.IsValid(&Failure));

	FABTSM11FinaleLayoutPreset PlaybackTrajectoryTamper = Preset;
	PlaybackTrajectoryTamper.PhysicalPlaybackTrajectoryHash ^= 0x1ull;
	TestFalse(TEXT("A physical-playback tamper is rejected by the bundle"),
		PlaybackTrajectoryTamper.IsValid(&Failure));

	FABTSM11FinaleLayoutPreset BundleTamper = Preset;
	BundleTamper.CertifiedBundleHash ^= 0x1ull;
	TestFalse(TEXT("A wrong non-zero bundle identity is rejected"),
		BundleTamper.IsValid(&Failure));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11BConnectivityContractTest,
	"ABTS.M11B.Unit.Connectivity6",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11BConnectivityContractTest::RunTest(const FString& Parameters)
{
	TArray<FABTSM11CertificationSample> FaceConnected;
	FaceConnected.SetNum(8);
	FaceConnected[0].HighestPrefixLevel = 4;
	FaceConnected[1].HighestPrefixLevel = 4;
	TestEqual(TEXT("Face-adjacent samples form one component"),
		FABTSM11FinaleLayoutCertification::CountComponents6(
			FaceConnected, 2, 2, 2, 4),
		1);

	TArray<FABTSM11CertificationSample> CornerOnly;
	CornerOnly.SetNum(8);
	CornerOnly[0].HighestPrefixLevel = 4;
	CornerOnly[7].HighestPrefixLevel = 4;
	TestEqual(TEXT("Corner-only contact remains two components"),
		FABTSM11FinaleLayoutCertification::CountComponents6(
			CornerOnly, 2, 2, 2, 4),
		2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11BConnectivityBridgeClosureV3Test,
	"ABTS.M11B.Unit.ConnectivityBridgeClosureV3",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11BConnectivityBridgeClosureV3Test::RunTest(
	const FString& Parameters)
{
	FABTSM11FinaleLayoutPreset Legacy =
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
	const uint64 FrozenV2Hash = Legacy.ScanContractHash;
	TestEqual(TEXT("Frozen production contract remains v2"),
		Legacy.ScanContract.ScanContractVersion, 2);
	TestEqual(TEXT("Frozen production connectivity remains six-neighbor"),
		Legacy.ScanContract.Connectivity, 6);
	TestTrue(TEXT("Frozen v2 contract has no bridge policy"),
		Legacy.ScanContract.BridgeClosurePolicy.IsDisabled());
	TestEqual(TEXT("Frozen v2 scan hash remains byte-compatible"),
		FrozenV2Hash,
		FABTSM11FinaleLayoutHash::ComputeScanContractHash(Legacy));

	FABTSM11FinaleLayoutPreset CandidateV3 = Legacy;
	CandidateV3.ScanContract =
		FABTSM11LayoutScanContract::MakeBridgeClosureV3(
			Legacy.ScanContract);
	RefreshOfflineM11BIdentity(CandidateV3);
	FString Failure;
	TestTrue(
		*FString::Printf(TEXT("Connectivity v3 contract is valid: %s"), *Failure),
		CandidateV3.ScanContract.IsValid(
			CandidateV3.LaunchModel,
			&Failure));
	TestTrue(TEXT("v3 changes scan identity"),
		CandidateV3.ScanContractHash != FrozenV2Hash);

	FABTSM11FinaleLayoutPreset ChangedBudget = CandidateV3;
	++ChangedBudget.ScanContract.BridgeClosurePolicy.MaximumRecursionDepth;
	RefreshOfflineM11BIdentity(ChangedBudget);
	TestTrue(TEXT("Recursion budget is bound into the scan hash"),
		ChangedBudget.ScanContractHash != CandidateV3.ScanContractHash);
	FABTSM11FinaleLayoutPreset ChangedOrder = CandidateV3;
	++ChangedOrder.ScanContract.BridgeClosurePolicy.VisitOrderVersion;
	RefreshOfflineM11BIdentity(ChangedOrder);
	TestTrue(TEXT("Visit-order version is bound into the scan hash"),
		ChangedOrder.ScanContractHash != CandidateV3.ScanContractHash);

	FABTSM11LayoutCertificationReport RejectedReport;
	Failure.Reset();
	TestFalse(TEXT("v3 cannot certify without bridge evidence"),
		FABTSM11FinaleLayoutCertification::ScanRegularGrid(
			CandidateV3,
			0x7u,
			RejectedReport,
			&Failure));
	TestEqual(TEXT("Missing v3 evidence fails closed"),
		Failure,
		FString(TEXT("BridgeClosureEvidenceRequired")));

	FABTSM11ConnectivityGridShape Shape;
	Shape.YawCount = 3;
	Shape.PitchCount = 3;
	Shape.PowerCount = 1;
	TArray<uint8> DiagonalMask;
	DiagonalMask.Init(0, Shape.GetSampleCount());
	DiagonalMask[0] = 1;
	DiagonalMask[4] = 1;
	DiagonalMask[8] = 1;
	FABTSM11ConnectivityDiscoveryPlan FirstPlan;
	FABTSM11ConnectivityDiscoveryPlan SecondPlan;
	Failure.Reset();
	TestTrue(TEXT("18-neighbor discovery proposes deterministic bridges"),
		FABTSM11ConnectivityClosure::BuildDiscoveryPlan18(
			DiagonalMask, Shape, 4, FirstPlan, &Failure));
	TestTrue(TEXT("Repeated discovery succeeds"),
		FABTSM11ConnectivityClosure::BuildDiscoveryPlan18(
			DiagonalMask, Shape, 4, SecondPlan, &Failure));
	TestEqual(TEXT("Six-neighbor graph retains three components"),
		FirstPlan.FaceComponentCount, 3);
	TestEqual(TEXT("18-neighbor discovery graph is one component"),
		FirstPlan.DiscoveryComponentCount, 1);
	TestEqual(TEXT("A canonical spanning set has two pending bridges"),
		FirstPlan.RequiredBridgeEdges.Num(), 2);
	TestEqual(TEXT("Discovery plan is deterministic"),
		FirstPlan.PlanHash, SecondPlan.PlanHash);

	const FABTSM11BridgeClosurePolicy& Policy =
		CandidateV3.ScanContract.BridgeClosurePolicy;
	FABTSM11ConnectivityClosureResult Closure;
	Failure.Reset();
	TestFalse(TEXT("Discovery alone never certifies"),
		FABTSM11ConnectivityClosure::CloseWithEvidence(
			FirstPlan, Policy, {}, Closure, &Failure));
	TestEqual(TEXT("Missing evidence has a stable failure"),
		Failure,
		FString(TEXT("MissingBridgeClosureEvidence")));
	TestFalse(TEXT("Missing evidence result is not passed"), Closure.bPassed);

	TArray<FABTSM11BridgeClosureEvidence> Evidence;
	for (int32 Index = 0; Index < FirstPlan.RequiredBridgeEdges.Num(); ++Index)
	{
		FABTSM11BridgeClosureEvidence Item;
		Item.EdgeHash = FirstPlan.RequiredBridgeEdges[Index].EdgeHash;
		Item.PolicyHash = Policy.ComputePolicyHash();
		Item.RecursionDepth = Policy.MaximumRecursionDepth;
		Item.SampleCount = 16 + Index;
		Item.PathSampleCount = 4 + Index;
		Item.ReachedYawPrecisionDegrees =
			Policy.FinalYawPrecisionDegrees;
		Item.ReachedPitchPrecisionDegrees =
			Policy.FinalPitchPrecisionDegrees;
		Item.ReachedPowerPrecision = Policy.FinalPowerPrecision;
		Item.VisitOrderHash = 0x100ull + Index;
		Item.ContinuousPathHash = 0x200ull + Index;
		Item.bReachedFinalPrecision = true;
		Item.bProvenContinuousF4Path = true;
		Item.EvidenceHash = Item.ComputeEvidenceHash();
		Evidence.Add(Item);
	}
	Failure.Reset();
	TestTrue(TEXT("Every required bridge proof closes to one component"),
		FABTSM11ConnectivityClosure::CloseWithEvidence(
			FirstPlan, Policy, Evidence, Closure, &Failure));
	TestTrue(TEXT("Complete evidence result is passed"), Closure.bPassed);
	TestEqual(TEXT("Closed graph is one component"),
		Closure.FinalComponentCount, 1);
	const uint64 PassedResultHash = Closure.ResultHash;

	Evidence[0].ContinuousPathHash ^= 0x1ull;
	Evidence[0].EvidenceHash = Evidence[0].ComputeEvidenceHash();
	FABTSM11ConnectivityClosureResult ChangedEvidenceResult;
	TestTrue(TEXT("A different valid bridge proof remains consumable"),
		FABTSM11ConnectivityClosure::CloseWithEvidence(
			FirstPlan,
			Policy,
			Evidence,
			ChangedEvidenceResult,
			&Failure));
	TestTrue(TEXT("Bridge evidence changes the closure identity"),
		ChangedEvidenceResult.ResultHash != PassedResultHash);

	FABTSM11BridgeClosureEvidence Unexpected = Evidence[0];
	Unexpected.EdgeHash ^= 0x1000ull;
	Unexpected.EvidenceHash = Unexpected.ComputeEvidenceHash();
	Evidence.Add(Unexpected);
	FABTSM11ConnectivityClosureResult UnexpectedEvidenceResult;
	Failure.Reset();
	TestFalse(TEXT("Evidence for an unrequested edge fails closed"),
		FABTSM11ConnectivityClosure::CloseWithEvidence(
			FirstPlan,
			Policy,
			Evidence,
			UnexpectedEvidenceResult,
			&Failure));
	TestEqual(TEXT("Unexpected evidence has a stable failure"),
		Failure,
		FString(TEXT("UnexpectedBridgeClosureEvidence")));
	Evidence.Pop();

	Evidence.RemoveAt(0);
	FABTSM11ConnectivityClosureResult Incomplete;
	Failure.Reset();
	TestFalse(TEXT("Removing one required bridge fails closed"),
		FABTSM11ConnectivityClosure::CloseWithEvidence(
			FirstPlan, Policy, Evidence, Incomplete, &Failure));
	TestFalse(TEXT("Incomplete closure cannot pass"), Incomplete.bPassed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11BCertificationHashCoverageTest,
	"ABTS.M11B.Unit.CertificationHashCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11BCertificationHashCoverageTest::RunTest(
	const FString& Parameters)
{
	FABTSM11LayoutCertificationReport Report;
	Report.PresetHash = 0x1234ull;
	Report.ScenarioHash = 0x5678u;
	Report.ScanContractHash = 0x9abcull;
	Report.bPassed = true;
	Report.ReportHash =
		FABTSM11FinaleLayoutHash::ComputeReportHash(Report);
	const uint64 PassedReportHash = Report.ReportHash;

	FABTSM11LayoutCertificationReport FailedReport = Report;
	FailedReport.bPassed = false;
	FailedReport.Failure = TEXT("SyntheticFailure");
	TestTrue(
		TEXT("Report identity binds pass/failure metadata"),
		FABTSM11FinaleLayoutHash::ComputeReportHash(FailedReport)
			!= PassedReportHash);

	FABTSM11CertificationSuiteReport Suite;
	Suite.Baseline = Report;
	Suite.bDiscoveryCoverageComplete = true;
	Suite.bClosureConverged = true;
	Suite.bPassed = true;
	const uint64 PassedSuiteHash =
		FABTSM11FinaleLayoutHash::ComputeCertificationSuiteHash(Suite);

	FABTSM11CertificationSuiteReport MutatedBody = Suite;
	++MutatedBody.Baseline.TotalSampleCount;
	TestTrue(
		TEXT("Suite identity recomputes a child body instead of trusting its cached hash"),
		FABTSM11FinaleLayoutHash::ComputeCertificationSuiteHash(
			MutatedBody) != PassedSuiteHash);

	FABTSM11CertificationSuiteReport MutatedStoredHash = Suite;
	MutatedStoredHash.Baseline.ReportHash ^= 0x1ull;
	TestTrue(
		TEXT("Suite identity also binds a changed stored child identity"),
		FABTSM11FinaleLayoutHash::ComputeCertificationSuiteHash(
			MutatedStoredHash) != PassedSuiteHash);

	FABTSM11CertificationSuiteReport MutatedClosure = Suite;
	MutatedClosure.bClosureConverged = false;
	TestTrue(
		TEXT("Suite identity binds closure state"),
		FABTSM11FinaleLayoutHash::ComputeCertificationSuiteHash(
			MutatedClosure) != PassedSuiteHash);

	FABTSM11CertificationSuiteReport MutatedAblation = Suite;
	MutatedAblation.RefinedAblations[2].TargetContactCount = 1;
	TestTrue(
		TEXT("Suite identity binds every refined ablation body"),
		FABTSM11FinaleLayoutHash::ComputeCertificationSuiteHash(
			MutatedAblation) != PassedSuiteHash);

	FABTSM11CertificationSuiteReport MutatedFailure = Suite;
	MutatedFailure.bPassed = false;
	MutatedFailure.Failure = TEXT("SyntheticSuiteFailure");
	TestTrue(
		TEXT("Suite identity binds pass/failure metadata"),
		FABTSM11FinaleLayoutHash::ComputeCertificationSuiteHash(
			MutatedFailure) != PassedSuiteHash);

	FABTSM11FinaleLayoutPreset Candidate =
		MakeOfflineM11BCertificationCandidate();
	FABTSM11InputGrid InvalidGrid =
		FABTSM11InputGrid::MakeFullDomain(
			Candidate.LaunchModel,
			Candidate.ScanContract);
	InvalidGrid.YawStepDegrees = 0.7;
	FABTSM11LayoutCertificationReport RejectedReport;
	FString Failure;
	TestFalse(
		TEXT("An invalid scan grid is rejected"),
		FABTSM11FinaleLayoutCertification::ScanGrid(
			Candidate,
			InvalidGrid,
			0x7u,
			RejectedReport,
			&Failure));
	TestTrue(
		TEXT("An early rejected report still has an audit identity"),
		RejectedReport.ReportHash != 0);
	TestEqual(
		TEXT("Rejected report exposes the canonical reason"),
		RejectedReport.Failure,
		FString(TEXT("NonIntegralInputGrid")));

	Candidate.PresetVersion = 99;
	FABTSM11CertificationSuiteReport RejectedSuite;
	Failure.Reset();
	TestFalse(
		TEXT("An invalid preset is rejected before certification"),
		FABTSM11FinaleLayoutCertification::Certify(
			Candidate,
			RejectedSuite,
			&Failure));
	TestTrue(
		TEXT("An early rejected suite still has an audit identity"),
		RejectedSuite.SuiteHash != 0);
	TestTrue(
		TEXT("Rejected suite exposes a non-empty canonical reason"),
		!RejectedSuite.Failure.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11BPrimaryOrbitLimitTest,
	"ABTS.M11B.Unit.PrimaryOrbitLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11BPrimaryOrbitLimitTest::RunTest(const FString& Parameters)
{
	FABTSM11FinaleLayoutPreset Preset =
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
	Preset.ScanContract.MaximumCompletePrimaryOrbits = 1;
	const FVector3d PrimaryCenter =
		Preset.CanonicalScenario.GetPrimary().CenterCM;
	const double OrbitRadiusCM =
		Preset.CanonicalScenario.GetPrimary().VisualRadiusCM + 1000.0;
	auto MakeArc = [&](const double CompleteOrbits)
	{
		FABTSM11TrajectoryResult Result;
		constexpr int32 SegmentCount = 256;
		for (int32 Index = 0; Index <= SegmentCount; ++Index)
		{
			const double Angle =
				2.0 * UE_PI * CompleteOrbits
				* static_cast<double>(Index)
				/ static_cast<double>(SegmentCount);
			FABTSM11TrajectoryPoint& Point = Result.Points.AddDefaulted_GetRef();
			Point.PositionCM = PrimaryCenter + FVector3d(
				OrbitRadiusCM * FMath::Cos(Angle),
				OrbitRadiusCM * FMath::Sin(Angle),
				0.0);
		}
		return Result;
	};

	const FABTSM11PrefixClassification BelowLimit =
		FABTSM11PrefixClassifier::Classify(Preset, MakeArc(0.9), 0x7u);
	const FABTSM11PrefixClassification AboveLimit =
		FABTSM11PrefixClassifier::Classify(Preset, MakeArc(1.1), 0x7u);
	TestFalse(TEXT("Less than the configured complete orbit is allowed"),
		BelowLimit.bExceededOrbitLimit);
	TestTrue(TEXT("Travel beyond the configured complete orbit is rejected"),
		AboveLimit.bExceededOrbitLimit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11BNominalSequenceTest,
	"ABTS.M11B.Unit.NominalSequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11BNominalSequenceTest::RunTest(const FString& Parameters)
{
	FABTSM11FinaleLayoutPreset Preset =
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
	FString Failure;
	TestTrue(TEXT("Frozen v1 layout is structurally valid"),
		Preset.IsValid(&Failure));
	FABTSM11TrajectoryRequest Request;
	FABTSM11TrajectoryResult Result;
	TestTrue(TEXT("Frozen nominal request builds"),
		Preset.BuildRequest(Preset.NominalInput, 0x7u, Request, &Failure));
	TestTrue(TEXT("Frozen nominal request solves"),
		FABTSM11GravityAssistSolver::Solve(Request, Result, &Failure));
	TestEqual(TEXT("Frozen nominal trajectory identity replays exactly"),
		Result.ValidationHash,
		Preset.NominalTrajectoryHash);
	TestTrue(TEXT("Full-domain certification identity is frozen"),
		Preset.CertificationHash != 0);
	TestTrue(TEXT("Scan-contract identity is frozen"),
		Preset.ScanContractHash != 0);
	for (int32 Index = 0;
		Index < FABTSM11FinaleLayoutPreset::AssistCount;
		++Index)
	{
		TestEqual(
			*FString::Printf(
				TEXT("F%d trust-region identity replays exactly"),
				Index + 1),
			Preset.PrefixTrustRegions[Index].RegionHash,
			FABTSM11FinaleLayoutHash::ComputeTrustRegionHash(
				Preset.PrefixTrustRegions[Index]));
	}
	const FABTSM11PrefixClassification Classification =
		FABTSM11PrefixClassifier::Classify(Preset, Result, 0x7u);
	TestEqual(TEXT("Frozen nominal input reaches F4"),
		static_cast<int32>(Classification.HighestPrefixLevel), 4);
	TestTrue(TEXT("Frozen nominal input hits the analytic target"),
		Result.DidHitTarget());
	double PreviousTime = -1.0;
	for (int32 AssistIndex = 1;
		AssistIndex <= FABTSM11GravityScenario::AssistCount;
		++AssistIndex)
	{
		const FABTSM11TrajectoryEvent* Enter = Result.FindAssistEvent(
			EABTSM11TrajectoryEventType::AssistEnter, AssistIndex);
		const FABTSM11TrajectoryEvent* Closest = Result.FindAssistEvent(
			EABTSM11TrajectoryEventType::ClosestApproach, AssistIndex);
		const FABTSM11TrajectoryEvent* Exit = Result.FindAssistEvent(
			EABTSM11TrajectoryEventType::AssistExit, AssistIndex);
		TestNotNull(*FString::Printf(TEXT("Assist%d Enter exists"), AssistIndex),
			Enter);
		TestNotNull(*FString::Printf(TEXT("Assist%d Closest exists"), AssistIndex),
			Closest);
		TestNotNull(*FString::Printf(TEXT("Assist%d Exit exists"), AssistIndex),
			Exit);
		if (Enter != nullptr && Closest != nullptr && Exit != nullptr)
		{
			TestTrue(*FString::Printf(TEXT("Assist%d events are ordered"), AssistIndex),
				PreviousTime < Enter->TimeSeconds
					&& Enter->TimeSeconds < Closest->TimeSeconds
					&& Closest->TimeSeconds < Exit->TimeSeconds);
			PreviousTime = Exit->TimeSeconds;
		}
	}
	const FABTSM11TrajectoryEvent* Hit =
		Result.FindFirstEvent(EABTSM11TrajectoryEventType::TargetHit);
	TestNotNull(TEXT("TargetHit exists"), Hit);
	if (Hit != nullptr)
	{
		TestTrue(TEXT("TargetHit follows Assist3 exit"),
			Hit->TimeSeconds > PreviousTime);
	}
	AddInfo(FString::Printf(
		TEXT("M11-B frozen nominal: PresetHash=0x%016llx ScenarioHash=0x%08x ScanHash=0x%016llx CertificationHash=0x%016llx TrajectoryHash=0x%016llx TrustHash=(0x%016llx,0x%016llx,0x%016llx) Points=%d HitTime=%.6f"),
		static_cast<unsigned long long>(Preset.PresetHash),
		Preset.CanonicalScenario.ScenarioHash,
		static_cast<unsigned long long>(Preset.ScanContractHash),
		static_cast<unsigned long long>(Preset.CertificationHash),
		static_cast<unsigned long long>(Result.ValidationHash),
		static_cast<unsigned long long>(
			Preset.PrefixTrustRegions[0].RegionHash),
		static_cast<unsigned long long>(
			Preset.PrefixTrustRegions[1].RegionHash),
		static_cast<unsigned long long>(
			Preset.PrefixTrustRegions[2].RegionHash),
		Result.Points.Num(),
		Hit != nullptr ? Hit->TimeSeconds : -1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11BConstructiveSearchTest,
	"ABTS.M11B.ConstructiveSearch",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::ProductFilter)

bool FABTSM11BConstructiveSearchTest::RunTest(const FString& Parameters)
{
	const FABTSM11FinaleLayoutPreset Seed =
		FABTSM11FinaleLayoutPreset::MakeConstructiveSearchSeed();
	FABTSM11FinaleSearchConfig Config;
	FABTSM11FinaleLayoutPreset First;
	FABTSM11FinaleSearchReport FirstReport;
	FString Failure;
	const bool bFirstBuilt =
		FABTSM11FinaleLayoutSearch::BuildConstructiveSeed(
			Seed, Config, First, FirstReport, &Failure);
	TestTrue(
		*FString::Printf(TEXT("Constructive search finds a three-assist hit: %s"),
			*Failure),
		bFirstBuilt);
	if (!bFirstBuilt)
	{
		AddInfo(FString::Printf(
			TEXT("Search counts: Solves=%d GeometryReject=%d EncounterReject=%d Prefix=%d"),
			FirstReport.CandidateSolveCount,
			FirstReport.RejectedGeometryCount,
			FirstReport.RejectedEncounterCount,
			FirstReport.CompletedAssistCount));
		return false;
	}

	FABTSM11FinaleLayoutPreset Second;
	FABTSM11FinaleSearchReport SecondReport;
	TestTrue(TEXT("Repeated constructive search also succeeds"),
		FABTSM11FinaleLayoutSearch::BuildConstructiveSeed(
			Seed, Config, Second, SecondReport, &Failure));
	TestEqual(TEXT("Search output hash is deterministic"),
		FirstReport.SearchOutputHash, SecondReport.SearchOutputHash);
	TestEqual(TEXT("Nominal result hash is deterministic"),
		FirstReport.NominalTrajectoryHash,
		SecondReport.NominalTrajectoryHash);

	FABTSM11TrajectoryRequest Request;
	FABTSM11TrajectoryResult Result;
	TestTrue(TEXT("Constructed nominal request remains valid"),
		First.BuildRequest(First.NominalInput, 0x7u, Request, &Failure));
	TestTrue(TEXT("Constructed nominal replay runs"),
		FABTSM11GravityAssistSolver::Solve(Request, Result, &Failure));
	const FABTSM11PrefixClassification Classification =
		FABTSM11PrefixClassifier::Classify(First, Result, 0x7u);
	TestEqual(TEXT("Nominal search result reaches F4"),
		static_cast<int32>(Classification.HighestPrefixLevel), 4);
	TestTrue(TEXT("Nominal search result hits the analytic UFO"),
		Result.DidHitTarget());

	AddInfo(FString::Printf(
		TEXT("M11-B search: Solves=%d GeometryReject=%d EncounterReject=%d PresetHash=0x%016llx TrajectoryHash=0x%016llx"),
		FirstReport.CandidateSolveCount,
		FirstReport.RejectedGeometryCount,
		FirstReport.RejectedEncounterCount,
		static_cast<unsigned long long>(First.PresetHash),
		static_cast<unsigned long long>(Result.ValidationHash)));
	for (int32 AssistIndex = 1;
		AssistIndex <= FABTSM11GravityScenario::AssistCount;
		++AssistIndex)
	{
		const FABTSM11GravityBodySpec& Body =
			First.CanonicalScenario.GetAssist(AssistIndex);
		const FABTSM11TrajectoryEvent* Exit = Result.FindAssistEvent(
			EABTSM11TrajectoryEventType::AssistExit, AssistIndex);
		AddInfo(FString::Printf(
			TEXT("M11-B Assist%d Center=(%.17g,%.17g,%.17g) Mu=%.17g U=(%.17g,%.17g,%.17g) BTarget=(%.17g,%.17g) Side=%d ExitT=%.17g Quality=%.17g Energy=%.17g"),
			AssistIndex,
			Body.CenterCM.X,
			Body.CenterCM.Y,
			Body.CenterCM.Z,
			Body.GravitationalParameterCM3PerSec2,
			Body.VirtualOrbitalVelocityCMPerSec.X,
			Body.VirtualOrbitalVelocityCMPerSec.Y,
			Body.VirtualOrbitalVelocityCMPerSec.Z,
			Body.BPlaneTargetTCM,
			Body.BPlaneTargetRCM,
			static_cast<int32>(Body.AllowedPassSide),
			Exit != nullptr ? Exit->TimeSeconds : -1.0,
			Exit != nullptr ? Exit->CorridorQuality : -1.0,
			Exit != nullptr ? Exit->AppliedEnergyChangeCM2PerSec2 : -1.0));
	}
	AddInfo(FString::Printf(
		TEXT("M11-B Target Center=(%.17g,%.17g,%.17g) Hit=%.17g Approach=%.17g"),
		First.CanonicalScenario.Target.CenterCM.X,
		First.CanonicalScenario.Target.CenterCM.Y,
		First.CanonicalScenario.Target.CenterCM.Z,
		First.CanonicalScenario.Target.HitRadiusCM,
		First.TargetApproachRadiusCM));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11BFullDomainCertificationTest,
	"ABTS.M11B.Certification.FullInputDomain",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::ProductFilter)

bool FABTSM11BFullDomainCertificationTest::RunTest(const FString& Parameters)
{
	const FABTSM11FinaleLayoutPreset FrozenPreset =
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
	const FABTSM11FinaleLayoutPreset Candidate =
		MakeOfflineM11BCertificationCandidate();
	FABTSM11CertificationSuiteReport Suite;
	FString Failure;
	TestTrue(TEXT("Progressive full-domain certification executes"),
		FABTSM11FinaleLayoutCertification::Certify(
			Candidate, Suite, &Failure));
	const FABTSM11LayoutCertificationReport& Report =
		Suite.RefinedBaseline;
	AddInfo(FString::Printf(
		TEXT("M11-B progressive certification: DiscoverySamples=%d RefinedSamples=%d TotalSolves=%d Iterations=%d Coverage=%d Closure=%d TargetHits=%d Bypass=%d F=(%d,%d,%d,%d) Components=(%d,%d,%d,%d) Passed=%d Failure=%s SuiteHash=0x%016llx RefinedHash=0x%016llx"),
		Suite.DiscoverySampleCount,
		Suite.RefinementSampleCount,
		Suite.TotalSolverInvocationCount,
		Suite.RefinementIterationCount,
		Suite.bDiscoveryCoverageComplete ? 1 : 0,
		Suite.bClosureConverged ? 1 : 0,
		Report.TargetHitCount,
		Report.BypassTargetHitCount,
		Report.PrefixSampleCounts[1],
		Report.PrefixSampleCounts[2],
		Report.PrefixSampleCounts[3],
		Report.PrefixSampleCounts[4],
		Report.Prefixes[0].ComponentCount,
		Report.Prefixes[1].ComponentCount,
		Report.Prefixes[2].ComponentCount,
		Report.Prefixes[3].ComponentCount,
		Suite.bPassed ? 1 : 0,
		*Suite.Failure,
		static_cast<unsigned long long>(Suite.SuiteHash),
		static_cast<unsigned long long>(Report.ReportHash)));
	AddInfo(FString::Printf(
		TEXT("M11-B discovery phases: Base Samples=%d Solves=%d F4=%d; Half Samples=%d Solves=%d F4=%d; Refined Grid Yaw[%.6f,%.6f] Pitch[%.6f,%.6f] Power[%.6f,%.6f]"),
		Suite.Baseline.TotalSampleCount,
		Suite.Baseline.SolverInvocationCount,
		Suite.Baseline.PrefixSampleCounts[4],
		Suite.HalfCellBaseline.TotalSampleCount,
		Suite.HalfCellBaseline.SolverInvocationCount,
		Suite.HalfCellBaseline.PrefixSampleCounts[4],
		Report.Grid.Minimum.YawDegrees,
		Report.Grid.Maximum.YawDegrees,
		Report.Grid.Minimum.PitchDegrees,
		Report.Grid.Maximum.PitchDegrees,
		Report.Grid.Minimum.Power,
		Report.Grid.Maximum.Power));
	for (int32 Index = 0; Index < Suite.Ablations.Num(); ++Index)
	{
		AddInfo(FString::Printf(
			TEXT("M11-B ablation Mask=0x%02x BaseHits=%d HalfHits=%d RefinedHits=%d BaseContact=%d HalfContact=%d RefinedContact=%d BaseBypass=%d HalfBypass=%d RefinedBypass=%d"),
			Suite.AblationMasks[Index],
			Suite.Ablations[Index].TargetHitCount,
			Suite.HalfCellAblations[Index].TargetHitCount,
			Suite.RefinedAblations[Index].TargetHitCount,
			Suite.Ablations[Index].TargetContactCount,
			Suite.HalfCellAblations[Index].TargetContactCount,
			Suite.RefinedAblations[Index].TargetContactCount,
			Suite.Ablations[Index].BypassTargetHitCount,
			Suite.HalfCellAblations[Index].BypassTargetHitCount,
			Suite.RefinedAblations[Index].BypassTargetHitCount));
	}
	for (const FABTSM11PrefixComponentSummary& Prefix : Report.Prefixes)
	{
		AddInfo(FString::Printf(
			TEXT("M11-B F%d MainSamples=%d Bounds Yaw[%.6f,%.6f] Pitch[%.6f,%.6f] Power[%.6f,%.6f] Trust Yaw[%.6f,%.6f] Pitch[%.6f,%.6f] Power[%.6f,%.6f]"),
			Prefix.PrefixLevel,
			Prefix.NominalComponentSampleCount,
			Prefix.Minimum.YawDegrees,
			Prefix.Maximum.YawDegrees,
			Prefix.Minimum.PitchDegrees,
			Prefix.Maximum.PitchDegrees,
			Prefix.Minimum.Power,
			Prefix.Maximum.Power,
			Prefix.TrustRegion.Minimum.YawDegrees,
			Prefix.TrustRegion.Maximum.YawDegrees,
			Prefix.TrustRegion.Minimum.PitchDegrees,
			Prefix.TrustRegion.Maximum.PitchDegrees,
			Prefix.TrustRegion.Minimum.Power,
			Prefix.TrustRegion.Maximum.Power));
	}
	TestEqual(TEXT("No invalid refined requests"),
		Report.InvalidRequestCount,
		0);
	TestTrue(*FString::Printf(
		TEXT("Frozen v1 passes progressive full-domain gates: %s"),
		*Suite.Failure),
		Suite.bPassed);
	for (int32 Index = 0;
		Index < FABTSM11FinaleLayoutPreset::AssistCount;
		++Index)
	{
		const FABTSM11PrefixTrustRegion& Frozen =
			FrozenPreset.PrefixTrustRegions[Index];
		FABTSM11PrefixTrustRegion Certified =
			Report.Prefixes[Index].TrustRegion;
		Certified.RegionHash =
			FABTSM11FinaleLayoutHash::ComputeTrustRegionHash(Certified);
		const FString Prefix = FString::Printf(TEXT("Certified F%d"), Index + 1);
		TestEqual(*FString::Printf(TEXT("%s prefix level"), *Prefix),
			Certified.PrefixLevel, Frozen.PrefixLevel);
		TestEqual(*FString::Printf(TEXT("%s minimum yaw"), *Prefix),
			Certified.Minimum.YawDegrees, Frozen.Minimum.YawDegrees);
		TestEqual(*FString::Printf(TEXT("%s minimum pitch"), *Prefix),
			Certified.Minimum.PitchDegrees, Frozen.Minimum.PitchDegrees);
		TestEqual(*FString::Printf(TEXT("%s minimum power"), *Prefix),
			Certified.Minimum.Power, Frozen.Minimum.Power);
		TestEqual(*FString::Printf(TEXT("%s maximum yaw"), *Prefix),
			Certified.Maximum.YawDegrees, Frozen.Maximum.YawDegrees);
		TestEqual(*FString::Printf(TEXT("%s maximum pitch"), *Prefix),
			Certified.Maximum.PitchDegrees, Frozen.Maximum.PitchDegrees);
		TestEqual(*FString::Printf(TEXT("%s maximum power"), *Prefix),
			Certified.Maximum.Power, Frozen.Maximum.Power);
		TestEqual(*FString::Printf(TEXT("%s capture margin"), *Prefix),
			Certified.CaptureMarginCells, Frozen.CaptureMarginCells);
		TestEqual(*FString::Printf(TEXT("%s release margin"), *Prefix),
			Certified.ReleaseMarginCells, Frozen.ReleaseMarginCells);
		TestEqual(*FString::Printf(TEXT("%s region identity"), *Prefix),
			Certified.RegionHash, Frozen.RegionHash);
	}

	FABTSM11TrajectoryRequest NominalRequest;
	FABTSM11TrajectoryResult NominalResult;
	TestTrue(
		TEXT("Certification candidate builds its nominal request"),
		Candidate.BuildRequest(
			Candidate.NominalInput,
			0x7u,
			NominalRequest,
			&Failure));
	TestTrue(
		TEXT("Certification candidate solves its nominal request"),
		FABTSM11GravityAssistSolver::Solve(
			NominalRequest,
			NominalResult,
			&Failure));
	FABTSM11TrajectoryRequest PlaybackRequest;
	FABTSM11TrajectoryResult PlaybackResult;
	TestTrue(
		TEXT("Certification candidate builds its physical playback request"),
		Candidate.BuildPhysicalPlaybackRequest(
			Candidate.NominalInput,
			0x7u,
			PlaybackRequest,
			&Failure));
	TestTrue(
		TEXT("Certification candidate solves its physical playback request"),
		FABTSM11GravityAssistSolver::Solve(
			PlaybackRequest,
			PlaybackResult,
			&Failure));

	FABTSM11FinaleLayoutPreset GeneratedManifest = FrozenPreset;
	GeneratedManifest.CertificationHash = Suite.SuiteHash;
	GeneratedManifest.NominalTrajectoryHash =
		NominalResult.ValidationHash;
	GeneratedManifest.PhysicalPlaybackTrajectoryHash =
		PlaybackResult.ValidationHash;
	for (int32 Index = 0;
		Index < FABTSM11FinaleLayoutPreset::AssistCount;
		++Index)
	{
		GeneratedManifest.PrefixTrustRegions[Index] =
			Report.Prefixes[Index].TrustRegion;
		GeneratedManifest.PrefixTrustRegions[Index].RegionHash =
			FABTSM11FinaleLayoutHash::ComputeTrustRegionHash(
				GeneratedManifest.PrefixTrustRegions[Index]);
	}
	GeneratedManifest.CertifiedBundleHash = 0;
	GeneratedManifest.CertifiedBundleHash =
		FABTSM11FinaleLayoutHash::ComputeCertifiedBundleHash(
			GeneratedManifest);
	AddInfo(FString::Printf(
		TEXT("M11-B generated manifest: Source=0x%016llx Preset=0x%016llx Scenario=0x%08x Scan=0x%016llx Certification=0x%016llx Refined=0x%016llx Nominal=0x%016llx Playback=0x%016llx Trust=(0x%016llx,0x%016llx,0x%016llx) Bundle=0x%016llx"),
		static_cast<unsigned long long>(
			GeneratedManifest.PresetSourceHash),
		static_cast<unsigned long long>(GeneratedManifest.PresetHash),
		GeneratedManifest.CanonicalScenario.ScenarioHash,
		static_cast<unsigned long long>(
			GeneratedManifest.ScanContractHash),
		static_cast<unsigned long long>(
			GeneratedManifest.CertificationHash),
		static_cast<unsigned long long>(Report.ReportHash),
		static_cast<unsigned long long>(
			GeneratedManifest.NominalTrajectoryHash),
		static_cast<unsigned long long>(
			GeneratedManifest.PhysicalPlaybackTrajectoryHash),
		static_cast<unsigned long long>(
			GeneratedManifest.PrefixTrustRegions[0].RegionHash),
		static_cast<unsigned long long>(
			GeneratedManifest.PrefixTrustRegions[1].RegionHash),
		static_cast<unsigned long long>(
			GeneratedManifest.PrefixTrustRegions[2].RegionHash),
		static_cast<unsigned long long>(
			GeneratedManifest.CertifiedBundleHash)));
	TestEqual(TEXT("Progressive suite reproduces the frozen certification identity"),
		Suite.SuiteHash,
		FrozenPreset.CertificationHash);
	TestEqual(TEXT("Nominal replay reproduces the frozen trajectory identity"),
		NominalResult.ValidationHash,
		FrozenPreset.NominalTrajectoryHash);
	TestEqual(TEXT("Physical replay reproduces the frozen trajectory identity"),
		PlaybackResult.ValidationHash,
		FrozenPreset.PhysicalPlaybackTrajectoryHash);
	TestEqual(TEXT("Generated bundle reproduces the frozen manifest identity"),
		GeneratedManifest.CertifiedBundleHash,
		FrozenPreset.CertifiedBundleHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11BPhysicalPlaybackTest,
	"ABTS.M11B.Unit.PhysicalPlayback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11BPhysicalPlaybackTest::RunTest(
	const FString& Parameters)
{
	const FABTSM11FinaleLayoutPreset Preset =
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
	FString Failure;
	TestTrue(
		*FString::Printf(
			TEXT("Frozen physical-playback manifest is valid: %s"),
			*Failure),
		Preset.IsValid(&Failure));

	FABTSM11TrajectoryRequest QualifiedRequest;
	FABTSM11TrajectoryRequest PlaybackRequest;
	TestTrue(
		TEXT("Qualified-intercept request builds"),
		Preset.BuildRequest(
			Preset.NominalInput,
			0x7u,
			QualifiedRequest,
			&Failure));
	TestTrue(
		TEXT("Physical playback request builds"),
		Preset.BuildPhysicalPlaybackRequest(
			Preset.NominalInput,
			0x7u,
			PlaybackRequest,
			&Failure));

	const FVector3d ExpectedLaunchVelocity =
		Preset.LaunchModel.MapDirection(Preset.NominalInput)
			* Preset.LaunchModel.MapSpeedCMPerSec(
				Preset.NominalInput);
	TestTrue(
		TEXT("Physical playback starts at the original pouch"),
		PlaybackRequest.InitialPositionCM.Equals(
			Preset.LaunchModel.PouchLocalPositionCM,
			0.0));
	TestTrue(
		TEXT("Physical playback starts with the canonical launch velocity"),
		PlaybackRequest.InitialVelocityCMPerSec.Equals(
			ExpectedLaunchVelocity,
			0.0));
	TestEqual(
		TEXT("Physical playback starts at canonical time zero"),
		PlaybackRequest.InitialTimeSeconds,
		0.0);
	TestEqual(
		TEXT("Physical playback expects Assist1 from its first step"),
		PlaybackRequest.InitialExpectedAssistIndex,
		1);
	TestTrue(
		TEXT("Qualified and physical requests share the exact initial position"),
		PlaybackRequest.InitialPositionCM.Equals(
			QualifiedRequest.InitialPositionCM,
			0.0));
	TestTrue(
		TEXT("Qualified and physical requests share the exact initial velocity"),
		PlaybackRequest.InitialVelocityCMPerSec.Equals(
			QualifiedRequest.InitialVelocityCMPerSec,
			0.0));
	TestTrue(
		TEXT("Physical playback uses an independent scenario identity"),
		PlaybackRequest.Scenario.ScenarioHash
			!= QualifiedRequest.Scenario.ScenarioHash);

	for (int32 BodyIndex = 0;
		BodyIndex < FABTSM11GravityScenario::BodyCount;
		++BodyIndex)
	{
		const FABTSM11GravityBodySpec& QualifiedBody =
			QualifiedRequest.Scenario.Bodies[BodyIndex];
		const FABTSM11GravityBodySpec& PlaybackBody =
			PlaybackRequest.Scenario.Bodies[BodyIndex];
		TestTrue(
			*FString::Printf(
				TEXT("Physical playback reuses body %d center"),
				BodyIndex),
			PlaybackBody.CenterCM.Equals(
				QualifiedBody.CenterCM,
				0.0));
		TestEqual(
			*FString::Printf(
				TEXT("Physical playback reuses body %d gravity"),
				BodyIndex),
			PlaybackBody.GravitationalParameterCM3PerSec2,
			QualifiedBody.GravitationalParameterCM3PerSec2);
		TestTrue(
			*FString::Printf(
				TEXT("Physical playback reuses body %d virtual momentum"),
				BodyIndex),
			PlaybackBody.VirtualOrbitalVelocityCMPerSec.Equals(
				QualifiedBody.VirtualOrbitalVelocityCMPerSec,
				0.0));
	}

	const FABTSM11TargetSpec& CertifiedTarget =
		Preset.CanonicalScenario.Target;
	const FABTSM11TargetSpec& PlaybackTarget =
		PlaybackRequest.Scenario.Target;
	TestTrue(
		TEXT("Physical playback terminal center is the independent UFO center"),
		PlaybackTarget.CenterCM.Equals(
			CertifiedTarget.GetGeometricContactCenterCM(),
			0.0));
	TestEqual(
		TEXT("Physical playback terminal radius is exactly the 800 cm UFO sphere"),
		PlaybackTarget.HitRadiusCM,
		CertifiedTarget.GetGeometricContactRadiusCM());
	TestFalse(
		TEXT("Physical playback target no longer carries a second contact center"),
		PlaybackTarget.bUseSeparateGeometricContactCenter);
	TestEqual(
		TEXT("Physical playback keeps the same corridor qualification"),
		PlaybackTarget.MinimumQualifyingCorridorQuality,
		CertifiedTarget.MinimumQualifyingCorridorQuality);

	FABTSM11TrajectoryResult PlaybackResult;
	TestTrue(
		TEXT("Physical playback request solves"),
		FABTSM11GravityAssistSolver::Solve(
			PlaybackRequest,
			PlaybackResult,
			&Failure));
	TestEqual(
		TEXT("Physical playback terminates with TargetHit"),
		static_cast<int32>(PlaybackResult.Termination),
		static_cast<int32>(
			EABTSM11TrajectoryTermination::TargetHit));
	TestTrue(
		TEXT("Physical playback reports a target hit"),
		PlaybackResult.DidHitTarget());
	TestTrue(
		TEXT("Physical playback observes the analytic target contact"),
		PlaybackResult.DidContactTarget());
	TestEqual(
		TEXT("Physical playback completes all three assists"),
		PlaybackResult.CompletedAssistCount,
		3);
	TestEqual(
		TEXT("Physical playback crosses the target sphere once"),
		PlaybackResult.TargetContactCount,
		1);
	TestEqual(
		TEXT("Physical playback trajectory identity replays exactly"),
		PlaybackResult.ValidationHash,
		Preset.PhysicalPlaybackTrajectoryHash);
	TestTrue(
		TEXT("Physical playback retains at least an initial and terminal point"),
		PlaybackResult.Points.Num() >= 2);

	const double EndpointDistanceCM =
		PlaybackResult.Points.IsEmpty()
		? TNumericLimits<double>::Max()
		: (PlaybackResult.Points.Last().PositionCM
			- CertifiedTarget
				.GetGeometricContactCenterCM()).Length();
	TestTrue(
		TEXT("Physical playback terminates on the 800 cm surface"),
		FMath::IsNearlyEqual(
			EndpointDistanceCM,
			CertifiedTarget.GetGeometricContactRadiusCM(),
			1.0e-6));

	double MaximumSegmentDistanceCM = 0.0;
	bool bStrictlyIncreasingTime = true;
	bool bFinitePoints = true;
	for (int32 PointIndex = 1;
		PointIndex < PlaybackResult.Points.Num();
		++PointIndex)
	{
		const FABTSM11TrajectoryPoint& Previous =
			PlaybackResult.Points[PointIndex - 1];
		const FABTSM11TrajectoryPoint& Current =
			PlaybackResult.Points[PointIndex];
		const double DeltaTime =
			Current.TimeSeconds - Previous.TimeSeconds;
		bStrictlyIncreasingTime &=
			DeltaTime > 0.0
			&& DeltaTime
				<= PlaybackRequest.Config.FixedTimeStepSeconds
					+ 1.0e-9;
		bFinitePoints &=
			!Current.PositionCM.ContainsNaN()
			&& !Current.VelocityCMPerSec.ContainsNaN();
		MaximumSegmentDistanceCM = FMath::Max(
			MaximumSegmentDistanceCM,
			(Current.PositionCM - Previous.PositionCM).Length());
	}
	TestTrue(
		TEXT("Physical playback time is continuous at fixed-step-or-finer cadence"),
		bStrictlyIncreasingTime);
	TestTrue(
		TEXT("Physical playback contains only finite states"),
		bFinitePoints);
	TestTrue(
		TEXT("Physical playback contains no position jump"),
		MaximumSegmentDistanceCM < 500.0);

	double PreviousEventTime = -1.0;
	for (int32 AssistIndex = 1;
		AssistIndex <= FABTSM11GravityScenario::AssistCount;
		++AssistIndex)
	{
		const FABTSM11TrajectoryEvent* Enter =
			PlaybackResult.FindAssistEvent(
				EABTSM11TrajectoryEventType::AssistEnter,
				AssistIndex);
		const FABTSM11TrajectoryEvent* Closest =
			PlaybackResult.FindAssistEvent(
				EABTSM11TrajectoryEventType::ClosestApproach,
				AssistIndex);
		const FABTSM11TrajectoryEvent* Exit =
			PlaybackResult.FindAssistEvent(
				EABTSM11TrajectoryEventType::AssistExit,
				AssistIndex);
		TestNotNull(
			*FString::Printf(TEXT("Playback Assist%d Enter exists"), AssistIndex),
			Enter);
		TestNotNull(
			*FString::Printf(TEXT("Playback Assist%d Closest exists"), AssistIndex),
			Closest);
		TestNotNull(
			*FString::Printf(TEXT("Playback Assist%d Exit exists"), AssistIndex),
			Exit);
		if (Enter != nullptr && Closest != nullptr && Exit != nullptr)
		{
			TestTrue(
				*FString::Printf(
					TEXT("Playback Assist%d events remain ordered"),
					AssistIndex),
				PreviousEventTime < Enter->TimeSeconds
					&& Enter->TimeSeconds < Closest->TimeSeconds
					&& Closest->TimeSeconds < Exit->TimeSeconds);
			PreviousEventTime = Exit->TimeSeconds;
		}
	}
	const FABTSM11TrajectoryEvent* TargetHit =
		PlaybackResult.FindFirstEvent(
			EABTSM11TrajectoryEventType::TargetHit);
	TestNotNull(TEXT("Physical playback TargetHit event exists"), TargetHit);
	if (TargetHit != nullptr)
	{
		TestTrue(
			TEXT("Physical playback hits only after Assist3 exit"),
			TargetHit->TimeSeconds > PreviousEventTime);
	}
	TestEqual(
		TEXT("Physical playback emits exactly three complete assists and one terminal hit"),
		PlaybackResult.Events.Num(),
		FABTSM11GravityScenario::AssistCount * 3 + 1);
	if (PlaybackResult.Events.Num()
		== FABTSM11GravityScenario::AssistCount * 3 + 1)
	{
		for (int32 AssistIndex = 1;
			AssistIndex <= FABTSM11GravityScenario::AssistCount;
			++AssistIndex)
		{
			const int32 EventOffset = (AssistIndex - 1) * 3;
			const FABTSM11TrajectoryEvent& EnterEvent =
				PlaybackResult.Events[EventOffset];
			const FABTSM11TrajectoryEvent& ClosestEvent =
				PlaybackResult.Events[EventOffset + 1];
			const FABTSM11TrajectoryEvent& ExitEvent =
				PlaybackResult.Events[EventOffset + 2];
			TestEqual(
				*FString::Printf(
					TEXT("Playback event %d is Assist%d Enter"),
					EventOffset,
					AssistIndex),
				static_cast<int32>(EnterEvent.Type),
				static_cast<int32>(
					EABTSM11TrajectoryEventType::AssistEnter));
			TestEqual(
				*FString::Printf(
					TEXT("Playback event %d is Assist%d ClosestApproach"),
					EventOffset + 1,
					AssistIndex),
				static_cast<int32>(ClosestEvent.Type),
				static_cast<int32>(
					EABTSM11TrajectoryEventType::ClosestApproach));
			TestEqual(
				*FString::Printf(
					TEXT("Playback event %d is Assist%d Exit"),
					EventOffset + 2,
					AssistIndex),
				static_cast<int32>(ExitEvent.Type),
				static_cast<int32>(
					EABTSM11TrajectoryEventType::AssistExit));
			TestEqual(
				*FString::Printf(
					TEXT("Playback Enter carries Assist%d identity"),
					AssistIndex),
				EnterEvent.AssistIndex,
				AssistIndex);
			TestEqual(
				*FString::Printf(
					TEXT("Playback ClosestApproach carries Assist%d identity"),
					AssistIndex),
				ClosestEvent.AssistIndex,
				AssistIndex);
			TestEqual(
				*FString::Printf(
					TEXT("Playback Exit carries Assist%d identity"),
					AssistIndex),
				ExitEvent.AssistIndex,
				AssistIndex);
		}
		TestEqual(
			TEXT("Physical playback final and only non-assist event is TargetHit"),
			static_cast<int32>(PlaybackResult.Events.Last().Type),
			static_cast<int32>(
				EABTSM11TrajectoryEventType::TargetHit));
	}

	AddInfo(FString::Printf(
		TEXT("M11-B frozen physical playback: Hash=0x%016llx Termination=%d Assists=%d Contacts=%d Points=%d EndDistance=%.9f MaxSegment=%.9f Failure=%s"),
		static_cast<unsigned long long>(
			PlaybackResult.ValidationHash),
		static_cast<int32>(PlaybackResult.Termination),
		PlaybackResult.CompletedAssistCount,
		PlaybackResult.TargetContactCount,
		PlaybackResult.Points.Num(),
		EndpointDistanceCM,
		MaximumSegmentDistanceCM,
		*Failure));
	return true;
}

#endif
