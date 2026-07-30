// Copyright Epic Games, Inc. All Rights Reserved.

#include "M11Core/ABTSM11CoreConformance.h"
#include "M11Core/ABTSM11CoreSolver.h"
#include "M11Search/ABTSM11CandidateSearch.h"
#include "Misc/AutomationTest.h"
#include "World/ABTSM11GravityAssistCoreAdapter.h"
#include "World/ABTSM11GravityAssistSolver.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11B21ConstructionPolicyTest,
	"ABTS.M11B.V2_1.ConstructionPolicy",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11B21ConstructionPolicyTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTS::M11Core;
	using namespace ABTS::M11Search;

	const CandidateSearchContract Contract =
		CandidateSearchContract::MakeV2_1();
	const std::array<std::int8_t, 3> Pattern =
		CandidateSearch::BuildPreferredPassSidePattern(Contract, 17ull);
	TestTrue(
		TEXT("A work item reproduces the same pass-side preference"),
		Pattern
			== CandidateSearch::BuildPreferredPassSidePattern(
				Contract, 17ull));
	TestTrue(
		TEXT("The three pass-side preferences strictly alternate"),
		(Pattern[0] == -1 || Pattern[0] == 1)
			&& Pattern[1] == -Pattern[0]
			&& Pattern[2] == Pattern[0]);

	bool bSawNegativeFirst = false;
	bool bSawPositiveFirst = false;
	for (std::uint64_t WorkIndex = 0; WorkIndex < 146ull; ++WorkIndex)
	{
		const std::array<std::int8_t, 3> WorkPattern =
			CandidateSearch::BuildPreferredPassSidePattern(
				Contract, WorkIndex);
		bSawNegativeFirst |= WorkPattern[0] < 0;
		bSawPositiveFirst |= WorkPattern[0] > 0;
	}
	TestTrue(
		TEXT("The deterministic corpus covers both alternating patterns"),
		bSawNegativeFirst && bSawPositiveFirst);

	TrajectoryResult NoEncounter;
	TestFalse(
		TEXT("A low-power miss remains eligible"),
		CandidateSearch::ShouldRejectLowPowerResult(
			NoEncounter, false));

	TrajectoryResult WrongAssistOnly;
	TrajectoryEvent Assist2Enter;
	Assist2Enter.Type = TrajectoryEventType::AssistEnter;
	Assist2Enter.AssistIndex = 2;
	WrongAssistOnly.Events.push_back(Assist2Enter);
	TestFalse(
		TEXT("An unrelated event does not trigger the Assist1 gate"),
		CandidateSearch::ShouldRejectLowPowerResult(
			WrongAssistOnly, false));

	TrajectoryResult EnteredAssist1;
	TrajectoryEvent Assist1Enter;
	Assist1Enter.Type = TrajectoryEventType::AssistEnter;
	Assist1Enter.AssistIndex = 1;
	EnteredAssist1.Events.push_back(Assist1Enter);
	TestFalse(
		TEXT("Brushing Assist1 influence without completing it stays eligible"),
		CandidateSearch::ShouldRejectLowPowerResult(
			EnteredAssist1, false));

	TrajectoryResult CompletedAssist1;
	CompletedAssist1.CompletedAssistCount = 1;
	TestFalse(
		TEXT("A non-qualifying Assist1 exit stays eligible"),
		CandidateSearch::ShouldRejectLowPowerResult(
			CompletedAssist1, false));
	TestTrue(
		TEXT("A qualifying Assist1 prefix triggers the low-power gate"),
		CandidateSearch::ShouldRejectLowPowerResult(
			CompletedAssist1, true));

	TrajectoryResult HitTarget;
	HitTarget.Termination = TrajectoryTermination::TargetHit;
	TestTrue(
		TEXT("A low-power target hit triggers the low-power gate"),
		CandidateSearch::ShouldRejectLowPowerResult(
			HitTarget, false));

	CandidateLayout AlternatingLayout;
	AlternatingLayout.Launch.PouchLocalPositionCM =
		Vec3d(0.0, 0.0, 0.0);
	AlternatingLayout.Scenario.Bodies[1].CenterCM =
		Vec3d(1.0, 0.0, 0.0);
	AlternatingLayout.Scenario.Bodies[2].CenterCM =
		Vec3d(1.0, 1.0, 0.0);
	AlternatingLayout.Scenario.Bodies[3].CenterCM =
		Vec3d(2.0, 1.0, 0.0);
	TrajectoryResult AlternatingResult;
	AlternatingResult.CompletedAssistCount = 3;
	const auto AddTurn =
		[&AlternatingResult](
			const std::int32_t AssistIndex,
			const Vec3d& EntryVelocity,
			const Vec3d& ExitVelocity)
		{
			TrajectoryEvent Enter;
			Enter.Type = TrajectoryEventType::AssistEnter;
			Enter.AssistIndex = AssistIndex;
			Enter.VelocityCMPerSec = EntryVelocity;
			AlternatingResult.Events.push_back(Enter);
			TrajectoryEvent Exit;
			Exit.Type = TrajectoryEventType::AssistExit;
			Exit.AssistIndex = AssistIndex;
			Exit.VelocityCMPerSec = ExitVelocity;
			AlternatingResult.Events.push_back(Exit);
		};
	AddTurn(1, Vec3d(1.0, 0.0, 0.0), Vec3d(0.0, 1.0, 0.0));
	AddTurn(2, Vec3d(0.0, 1.0, 0.0), Vec3d(1.0, 0.0, 0.0));
	AddTurn(3, Vec3d(1.0, 0.0, 0.0), Vec3d(0.0, 1.0, 0.0));
	const PartialAlternationMetrics Assist1Metrics =
		CandidateSearch::MeasurePartialAlternation(
			AlternatingLayout, Contract, AlternatingResult, 1);
	const PartialAlternationMetrics Assist2Metrics =
		CandidateSearch::MeasurePartialAlternation(
			AlternatingLayout, Contract, AlternatingResult, 2);
	const PartialAlternationMetrics Assist3Metrics =
		CandidateSearch::MeasurePartialAlternation(
			AlternatingLayout, Contract, AlternatingResult, 3);
	TestEqual(
		TEXT("Assist1 has no adjacent turn pair"),
		Assist1Metrics.PartialAlternationCount,
		0);
	TestEqual(
		TEXT("Assist2 detects the first actual lateral alternation"),
		Assist2Metrics.PartialAlternationCount,
		1);
	TestEqual(
		TEXT("Assist3 detects both actual lateral alternations"),
		Assist3Metrics.PartialAlternationCount,
		2);
	TestTrue(
		TEXT("Signed lateral turns use the final presentation normal"),
		Assist3Metrics.SignedLateralTurnRadians[0] > 0.0
			&& Assist3Metrics.SignedLateralTurnRadians[1] < 0.0
			&& Assist3Metrics.SignedLateralTurnRadians[2] > 0.0);
	TestTrue(
		TEXT("Partial alternation measurement is deterministic"),
		Assist3Metrics.SignedLateralTurnRadians
				== CandidateSearch::MeasurePartialAlternation(
					AlternatingLayout,
					Contract,
					AlternatingResult,
					3).SignedLateralTurnRadians
			&& Assist3Metrics.PartialAlternationCount
				== CandidateSearch::MeasurePartialAlternation(
					AlternatingLayout,
					Contract,
					AlternatingResult,
					3).PartialAlternationCount);

	TrajectoryResult OffPlaneResult;
	OffPlaneResult.CompletedAssistCount = 1;
	const auto AddOffPlaneTurn =
		[&OffPlaneResult](
			const TrajectoryEventType Type,
			const Vec3d& Velocity)
		{
			TrajectoryEvent Event;
			Event.Type = Type;
			Event.AssistIndex = 1;
			Event.VelocityCMPerSec = Velocity;
			OffPlaneResult.Events.push_back(Event);
		};
	AddOffPlaneTurn(
		TrajectoryEventType::AssistEnter,
		Vec3d(1.0, 0.0, 0.0));
	AddOffPlaneTurn(
		TrajectoryEventType::AssistExit,
		Vec3d(0.0, 0.0, 1.0));
	const PartialAlternationMetrics OffPlaneMetrics =
		CandidateSearch::MeasurePartialAlternation(
			AlternatingLayout, Contract, OffPlaneResult, 1);
	TestEqual(
		TEXT("A turn below the readable-axis threshold has zero sign"),
		OffPlaneMetrics.SignedLateralTurnRadians[0],
		0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11B21PortableCandidateReplayTest,
	"ABTS.M11B.V2_1.PortableCandidateReplay",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11B21PortableCandidateReplayTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTS::M11Core;
	using namespace ABTS::M11Search;

	constexpr std::uint64_t FrozenWorkIndex = 2278ull;
	constexpr std::uint64_t FrozenCandidateSourceHash =
		0xaaae0dd44f14f785ull;
	constexpr std::uint64_t FrozenNominalRequestHash =
		0x5ecc893f6eb7003dull;
	constexpr std::uint64_t FrozenNominalResultHash =
		0xb47d8314ebe69376ull;
	constexpr std::uint64_t FrozenScoreHash =
		0xd6e03f2d9e0f3b8bull;

	CandidateSearchContract Contract =
		CandidateSearchContract::MakeV2_1();
	std::string ContractFailure;
	TestTrue(
		TEXT("The v2.1 dual-domain search contract is valid"),
		Contract.IsValid(&ContractFailure));
	CandidateSearchContract InvalidRatioContract = Contract;
	InvalidRatioContract.MinimumPrefixRetentionRatio =
		InvalidRatioContract.FullScoreMinimumPrefixRetentionRatio;
	TestFalse(
		TEXT("The hard and full-score prefix bands cannot overlap"),
		InvalidRatioContract.IsValid(nullptr));
	CandidateSearchContract InvalidScreenSampleContract = Contract;
	InvalidScreenSampleContract.ScreenAimSampleCount = 4999;
	TestFalse(
		TEXT("The fixed screen-aim corpus must contain 5000 samples"),
		InvalidScreenSampleContract.IsValid(nullptr));
	CandidateSearchContract InvalidLateralProjectionContract = Contract;
	InvalidLateralProjectionContract
		.MinimumLateralTurnAxisProjection = 0.0;
	TestFalse(
		TEXT("The lateral turn readability threshold must be positive"),
		InvalidLateralProjectionContract.IsValid(nullptr));
	InvalidLateralProjectionContract
		.MinimumLateralTurnAxisProjection = 1.01;
	TestFalse(
		TEXT("The lateral turn readability threshold cannot exceed one"),
		InvalidLateralProjectionContract.IsValid(nullptr));
	CandidateRecord Candidate;
	std::string SearchFailure;
	const bool bEvaluated = CandidateSearch::EvaluateWorkItem(
		Contract,
		FrozenWorkIndex,
		Candidate,
		&SearchFailure);
	TestTrue(
		TEXT("The frozen portable candidate can be reconstructed"),
		bEvaluated);
	if (!bEvaluated)
	{
		AddError(FString::Printf(
			TEXT("Candidate reconstruction failed: %s"),
			UTF8_TO_TCHAR(SearchFailure.c_str())));
		return false;
	}

	TestTrue(
		TEXT("The reconstructed record remains a non-certified Candidate"),
		Candidate.IsAccepted());
	TestEqual(
		TEXT("Candidate source hash is frozen"),
		Candidate.CandidateSourceHash,
		FrozenCandidateSourceHash);
	TestEqual(
		TEXT("Nominal request hash is frozen"),
		Candidate.NominalRequestHash,
		FrozenNominalRequestHash);
	TestEqual(
		TEXT("Nominal result hash is frozen"),
		Candidate.NominalResultHash,
		FrozenNominalResultHash);
	TestEqual(
		TEXT("Candidate score hash is frozen"),
		Candidate.ScoreHash,
		FrozenScoreHash);
	TestEqual(
		TEXT("At least four local probes survive"),
		Candidate.Metrics.RobustSurvivorCount,
		4);
	TestEqual(
		TEXT("The 0.90 power probe raw completion remains diagnostic"),
		Candidate.Metrics.LowPowerCompletedAssistCount,
		3);
	TestTrue(
		TEXT("Raw low-power completion does not replace the qualified gate"),
		Candidate.IsAccepted());
	TestTrue(
		TEXT("The nominal trajectory alternates assist side at least once"),
		Candidate.Metrics.AlternatingLateralTurnCount
			>= Contract.MinimumAlternatingLateralTurnCount);
	TestTrue(
		TEXT("The candidate completes within 60 seconds"),
		Candidate.Metrics.TotalFlightTimeSeconds <= 60.0);
	TestTrue(
		TEXT("The candidate layout remains visibly non-collinear"),
		Candidate.Metrics.MinimumLayoutTurnRadians >= 0.30);
	TestEqual(
		TEXT("The candidate estimate covers 5000 full-domain inputs"),
		Candidate.Metrics.FullDomainSampleCount,
		5000);
	TestEqual(
		TEXT("The candidate estimate covers 5000 screen-aim inputs"),
		Candidate.Metrics.ScreenAimSampleCount,
		5000);
	TestEqual(
		TEXT("Accepted candidate has no full-domain solve failure"),
		Candidate.Metrics.FullDomainSolveFailureCount,
		0);
	TestEqual(
		TEXT("Accepted candidate has no screen-aim solve failure"),
		Candidate.Metrics.ScreenAimSolveFailureCount,
		0);
	TestEqual(
		TEXT("Accepted candidate has no conditional solve failure"),
		Candidate.Metrics.ConditionalSolveFailureCount,
		0);
	int32 ParentFullDomainCount =
		Candidate.Metrics.FullDomainSampleCount;
	int32 ParentScreenAimCount =
		Candidate.Metrics.ScreenAimSampleCount;
	for (int32 SetIndex = 0;
		SetIndex < static_cast<int32>(Candidate.Metrics.InputSets.size());
		++SetIndex)
	{
		const InputSetMetrics& Set =
			Candidate.Metrics.InputSets[
				static_cast<std::size_t>(SetIndex)];
		TestTrue(
			*FString::Printf(
				TEXT("S%d full-domain count is nested"),
				SetIndex + 1),
			Set.FullDomainCount >= 0
				&& Set.FullDomainCount <= ParentFullDomainCount);
		TestTrue(
			*FString::Printf(
				TEXT("S%d screen-aim count is nested"),
				SetIndex + 1),
			Set.ScreenAimCount >= 0
				&& Set.ScreenAimCount <= ParentScreenAimCount);
		const double ExpectedFullDomainRatio =
			ParentFullDomainCount > 0
				? static_cast<double>(Set.FullDomainCount)
					/ static_cast<double>(ParentFullDomainCount)
				: 0.0;
		const double ExpectedScreenAimRatio =
			ParentScreenAimCount > 0
				? static_cast<double>(Set.ScreenAimCount)
					/ static_cast<double>(ParentScreenAimCount)
				: 0.0;
		TestTrue(
			*FString::Printf(
				TEXT("S%d full-domain retention is self-consistent"),
				SetIndex + 1),
			FMath::IsNearlyEqual(
				Set.FullDomainRetentionRatio,
				ExpectedFullDomainRatio));
		TestTrue(
			*FString::Printf(
				TEXT("S%d screen-aim retention is self-consistent"),
				SetIndex + 1),
			FMath::IsNearlyEqual(
				Set.ScreenAimRetentionRatio,
				ExpectedScreenAimRatio));
		TestTrue(
			*FString::Printf(
				TEXT("S%d conditional evidence remains separately labelled"),
				SetIndex + 1),
			Set.ConditionalMemberCount <= Set.ConditionalParentCount
				&& Set.ConditionalParentCount
					<= Set.ConditionalProbeCount);
		if (SetIndex < 3)
		{
			TestTrue(
				*FString::Printf(
					TEXT("S%d screen ratio passes before hull"),
					SetIndex + 1),
				Set.ScreenAimRetentionCompliant);
			TestTrue(
				*FString::Printf(
					TEXT("S%d screen Yaw-Pitch hull passes the broad gate"),
					SetIndex + 1),
				Set.ScreenAimHullCompliant);
		}
		TestEqual(
			*FString::Printf(
				TEXT("S%d hull contains only unbiased screen members"),
				SetIndex + 1),
			Set.ScreenAimHullEvidencePointCount,
			Set.ScreenAimCount);
		ParentFullDomainCount = Set.FullDomainCount;
		ParentScreenAimCount = Set.ScreenAimCount;
	}
	TestTrue(
		TEXT("The soft score is finite and positive"),
		FMath::IsFinite(Candidate.Metrics.SoftScore)
			&& Candidate.Metrics.SoftScore > 0.0);
	for (std::size_t Index = 0;
		Index < Candidate.Metrics.AblationHitTarget.size();
		++Index)
	{
		TestFalse(
			*FString::Printf(
				TEXT("Ablation mask %u does not hit the target"),
				static_cast<uint32>(
					Candidate.Metrics.AblationMasks[Index])),
			Candidate.Metrics.AblationHitTarget[Index]);
	}

	TrajectoryRequest CoreRequest;
	std::string RequestFailure;
	const bool bBuiltRequest = Candidate.Layout.BuildRequest(
		Candidate.Layout.NominalInput,
		0x7u,
		CoreRequest,
		&RequestFailure);
	TestTrue(
		TEXT("The frozen candidate builds its nominal Core request"),
		bBuiltRequest);
	if (!bBuiltRequest)
	{
		AddError(FString::Printf(
			TEXT("Candidate request failed: %s"),
			UTF8_TO_TCHAR(RequestFailure.c_str())));
		return false;
	}

	const FABTSM11TrajectoryRequest UnrealRequest =
		ABTSM11GravityAssistAdapter::FromCore(CoreRequest);
	const TrajectoryRequest RoundTripRequest =
		ABTSM11GravityAssistAdapter::ToCore(UnrealRequest);
	TestTrue(
		TEXT("Candidate request survives the UE adapter exactly"),
		Testing::RequestsExactlyEqual(CoreRequest, RoundTripRequest));

	TrajectoryResult DirectResult;
	std::string DirectFailure;
	const bool bDirectSolved = GravityAssistSolver::Solve(
		CoreRequest,
		DirectResult,
		&DirectFailure);
	FABTSM11TrajectoryResult UnrealResult;
	FString UnrealFailure;
	const bool bUnrealSolved = FABTSM11GravityAssistSolver::Solve(
		UnrealRequest,
		UnrealResult,
		&UnrealFailure);
	TestEqual(
		TEXT("Core and UE facade solve acceptance matches"),
		bUnrealSolved,
		bDirectSolved);
	TestEqual(
		TEXT("Direct nominal result hash is frozen"),
		DirectResult.ValidationHash,
		FrozenNominalResultHash);
	TestTrue(
		TEXT("Core and UE facade results are exactly equal"),
		Testing::ResultsExactlyEqual(
			DirectResult,
			ABTSM11GravityAssistAdapter::ToCore(UnrealResult)));

	AddInfo(FString::Printf(
		TEXT("[ABTS][M11-B-v2.1][PortableCandidateReplay] ")
		TEXT("Work=%llu Source=0x%016llx Request=0x%016llx ")
		TEXT("Result=0x%016llx Score=0x%016llx Seconds=%.3f ")
		TEXT("MaxCoast=%.3f MinTurn=%.6f Robust=%d LowPowerAssist=%d"),
		static_cast<unsigned long long>(FrozenWorkIndex),
		static_cast<unsigned long long>(
			Candidate.CandidateSourceHash),
		static_cast<unsigned long long>(
			Candidate.NominalRequestHash),
		static_cast<unsigned long long>(
			Candidate.NominalResultHash),
		static_cast<unsigned long long>(Candidate.ScoreHash),
		Candidate.Metrics.TotalFlightTimeSeconds,
		Candidate.Metrics.MaximumCoastSeconds,
		Candidate.Metrics.MinimumLayoutTurnRadians,
		Candidate.Metrics.RobustSurvivorCount,
		Candidate.Metrics.LowPowerCompletedAssistCount));
	return true;
}

#endif
