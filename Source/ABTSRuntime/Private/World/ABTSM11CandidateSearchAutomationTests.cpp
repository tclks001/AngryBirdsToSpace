// Copyright Epic Games, Inc. All Rights Reserved.

#include "M11Core/ABTSM11CoreConformance.h"
#include "M11Core/ABTSM11CoreSolver.h"
#include "M11Search/ABTSM11CandidateSearch.h"
#include "Misc/AutomationTest.h"
#include "World/ABTSM11GravityAssistCoreAdapter.h"
#include "World/ABTSM11GravityAssistSolver.h"

#include <algorithm>

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
	FABTSM11B21ParticleBeamContractTest,
	"ABTS.M11B.V2_1.ParticleBeam.Contract",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11B21ParticleBeamContractTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTS::M11Search;

	const ParticleBeamSearchContract Contract =
		ParticleBeamSearchContract::MakeV4();
	std::string Failure;
	TestTrue(
		TEXT("The additive v4 particle-beam contract is valid"),
		Contract.IsValid(&Failure));
	if (!Failure.empty())
	{
		AddError(FString::Printf(
			TEXT("Unexpected v4 contract failure: %s"),
			UTF8_TO_TCHAR(Failure.c_str())));
	}

	const std::uint64_t ContractHash =
		ParticleBeamSearch::ComputeContractHash(Contract);
	const std::uint64_t EvaluationContractHash =
		ComputeCandidateSearchContractHash(
			Contract.EvaluationContract);
	TestTrue(
		TEXT("The valid v4 contract has a non-zero deterministic hash"),
		ContractHash != 0
			&& ContractHash
				== ParticleBeamSearch::ComputeContractHash(Contract));
	TestTrue(
		TEXT("The nested v3 evaluation contract has an explicit deterministic hash"),
		EvaluationContractHash != 0
			&& EvaluationContractHash
				== ComputeCandidateSearchContractHash(
					Contract.EvaluationContract));

	ParticleBeamSearchContract ChangedSeed = Contract;
	++ChangedSeed.ConstructionSeed;
	TestTrue(
		TEXT("The v4 contract hash includes the construction seed"),
		ParticleBeamSearch::ComputeContractHash(ChangedSeed)
			!= ContractHash);
	CandidateSearchContract ChangedEvaluation =
		Contract.EvaluationContract;
	ChangedEvaluation.MaximumCoastSeconds += 0.25;
	TestTrue(
		TEXT("The explicit v3 contract hash covers evaluation fields"),
		ComputeCandidateSearchContractHash(ChangedEvaluation)
			!= EvaluationContractHash);

	ParticleBeamSearchContract AliasedCorpus = Contract;
	AliasedCorpus.HoldoutSeed = AliasedCorpus.ExplorationSeed;
	TestFalse(
		TEXT("Exploration and holdout corpora must remain independent"),
		AliasedCorpus.IsValid(nullptr));

	ParticleBeamSearchContract InvalidRobustGuard = Contract;
	InvalidRobustGuard.RobustGuardSurvivorCount = 3;
	TestFalse(
		TEXT("The construction-only robust guard remains bounded"),
		InvalidRobustGuard.IsValid(nullptr));

	ParticleBeamSearchContract InvalidCombinedRobustGuard = Contract;
	InvalidCombinedRobustGuard
		.EvaluationContract.MinimumRobustSurvivorCount = 5;
	InvalidCombinedRobustGuard.RobustGuardSurvivorCount = 2;
	TestFalse(
		TEXT("The v3 robust requirement plus construction guard cannot exceed six"),
		InvalidCombinedRobustGuard.IsValid(nullptr));

	ParticleBeamSearchContract InvalidTargetRefinement = Contract;
	InvalidTargetRefinement.TargetRefinementTimeSampleCount = 1;
	TestFalse(
		TEXT("Target refinement requires at least two time samples"),
		InvalidTargetRefinement.IsValid(nullptr));

	ParticleBeamSearchContract InvalidRetentionBands = Contract;
	InvalidRetentionBands.PreferredMinimumRetentionRatio =
		InvalidRetentionBands.ExplorationMinimumRetentionRatio;
	TestFalse(
		TEXT("Exploration and preferred retention bands cannot collapse"),
		InvalidRetentionBands.IsValid(nullptr));

	ParticleBeamSearchResult RejectedResult;
	Failure.clear();
	TestFalse(
		TEXT("A zero-thread execution request fails closed"),
		ParticleBeamSearch::Run(
			Contract,
			0,
			1,
			RejectedResult,
			&Failure));
	TestEqual(
		TEXT("The rejected execution reports its stable diagnostic"),
		FString(UTF8_TO_TCHAR(RejectedResult.Diagnostic.c_str())),
		FString(TEXT("InvalidParticleBeamExecutionRequest")));
	TestEqual(
		TEXT("The rejected execution returns the same failure to the caller"),
		FString(UTF8_TO_TCHAR(Failure.c_str())),
		FString(TEXT("InvalidParticleBeamExecutionRequest")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11B21ParticleBeamDeterminismTest,
	"ABTS.M11B.V2_1.ParticleBeam.Determinism",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11B21ParticleBeamDeterminismTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTS::M11Search;

	ParticleBeamSearchContract Contract =
		ParticleBeamSearchContract::MakeV4();
	Contract.RootParameterCount = 4;
	Contract.ExplorationSampleCount = 32;
	Contract.GeometryTimeSampleCount = 3;
	Contract.GeometryRadiusSampleCount = 2;
	Contract.GeometryImpactSampleCount = 2;
	Contract.GeometryRadialSampleCount = 2;
	Contract.GeometryMomentumSampleCount = 2;
	Contract.NominalProposalBudget = 12;
	Contract.CoarseProposalBudget = 6;
	Contract.RefinementProposalBudget = 2;
	Contract.CoarseParticleLimit = 16;
	Contract.BeamWidth = 2;
	Contract.HoldoutSampleCount = 32;
	Contract.MaximumFinalAuditCandidates = 2;

	std::string Failure;
	TestTrue(
		TEXT("The compact deterministic-run contract is valid"),
		Contract.IsValid(&Failure));
	if (!Failure.empty())
	{
		AddError(FString::Printf(
			TEXT("Compact v4 contract failed: %s"),
			UTF8_TO_TCHAR(Failure.c_str())));
		return false;
	}

	ParticleBeamSearchResult SingleThread;
	const bool bSingleThreadRan = ParticleBeamSearch::Run(
		Contract,
		1,
		1,
		SingleThread,
		&Failure);
	TestTrue(
		TEXT("The compact search completes on one thread"),
		bSingleThreadRan);
	if (!bSingleThreadRan)
	{
		AddError(FString::Printf(
			TEXT("Single-thread v4 search failed: %s"),
			UTF8_TO_TCHAR(Failure.c_str())));
		return false;
	}

	ParticleBeamSearchResult FourThreads;
	Failure.clear();
	const bool bFourThreadsRan = ParticleBeamSearch::Run(
		Contract,
		4,
		1,
		FourThreads,
		&Failure);
	TestTrue(
		TEXT("The compact search completes on four threads"),
		bFourThreadsRan);
	if (!bFourThreadsRan)
	{
		AddError(FString::Printf(
			TEXT("Four-thread v4 search failed: %s"),
			UTF8_TO_TCHAR(Failure.c_str())));
		return false;
	}

	const auto CountSolves =
		[](const ParticleBeamConstructionMetrics& Metrics)
		{
			std::uint64_t Total = Metrics.InitialParticleSolveCount
				+ Metrics.HoldoutSolveCount
				+ Metrics.FinalAuditSolveCount;
			for (std::size_t Index = 0;
				Index < Metrics.NominalProposalSolveCounts.size();
				++Index)
			{
				Total += Metrics.NominalProposalSolveCounts[Index];
				Total += Metrics.CoarseParticleSolveCounts[Index];
				Total += Metrics.RefinementParticleSolveCounts[Index];
			}
			return Total;
		};
	const ParticleBeamConstructionMetrics& SingleMetrics =
		SingleThread.Construction;
	const ParticleBeamConstructionMetrics& FourMetrics =
		FourThreads.Construction;

	TestEqual(
		TEXT("The contract hash is independent of execution thread count"),
		SingleThread.ContractHash,
		FourThreads.ContractHash);
	TestTrue(
		TEXT("The terminal diagnostic is deterministic across thread counts"),
		SingleThread.Diagnostic == FourThreads.Diagnostic);
	TestTrue(
		TEXT("The compact corpus exercises the stage-one nominal gate"),
		SingleThread.Diagnostic.rfind(
			"Stage1NominalEmpty:", 0) == 0);
	TestEqual(
		TEXT("The nominal launch plus 32 exploration inputs are all counted"),
		SingleMetrics.InitialParticleSolveCount,
		static_cast<std::uint64_t>(
			Contract.ExplorationSampleCount + 1));
	TestEqual(
		TEXT("The compact fixture keeps its deterministic stage-one proposal count"),
		SingleMetrics.NominalProposalSolveCounts[0],
		static_cast<std::uint64_t>(21));
	TestEqual(
		TEXT("The early-termination solve ledger closes exactly"),
		CountSolves(SingleMetrics),
		static_cast<std::uint64_t>(54));
	TestEqual(
		TEXT("The solve ledger is identical across thread counts"),
		CountSolves(SingleMetrics),
		CountSolves(FourMetrics));
	TestTrue(
		TEXT("Initial-particle counters are deterministic"),
		SingleMetrics.InitialParticleSolveCount
			== FourMetrics.InitialParticleSolveCount);
	TestTrue(
		TEXT("Geometry-proposal counters are deterministic"),
		SingleMetrics.GeometryProposalCounts
			== FourMetrics.GeometryProposalCounts);
	TestTrue(
		TEXT("Nominal-proposal solve counters are deterministic"),
		SingleMetrics.NominalProposalSolveCounts
			== FourMetrics.NominalProposalSolveCounts);
	TestTrue(
		TEXT("Coarse-particle solve counters are deterministic"),
		SingleMetrics.CoarseParticleSolveCounts
			== FourMetrics.CoarseParticleSolveCounts);
	TestTrue(
		TEXT("Refinement solve counters are deterministic"),
		SingleMetrics.RefinementParticleSolveCounts
			== FourMetrics.RefinementParticleSolveCounts);
	TestTrue(
		TEXT("Beam survivor counters are deterministic"),
		SingleMetrics.BeamSurvivorCounts
			== FourMetrics.BeamSurvivorCounts);
	TestEqual(
		TEXT("No holdout solves run after the stage-one early exit"),
		SingleMetrics.HoldoutSolveCount,
		static_cast<std::uint64_t>(0));
	TestEqual(
		TEXT("No frozen-v3 audit runs after the stage-one early exit"),
		SingleMetrics.FinalAuditSolveCount,
		static_cast<std::uint64_t>(0));
	for (std::size_t Index = 0;
		Index < SingleMetrics.BeamSurvivorCounts.size();
		++Index)
	{
		TestTrue(
			*FString::Printf(
				TEXT("Stage %u survivor count stays within the beam width"),
				static_cast<uint32>(Index + 1)),
			SingleMetrics.BeamSurvivorCounts[Index] >= 0
				&& SingleMetrics.BeamSurvivorCounts[Index]
					<= Contract.BeamWidth);
	}

	AddInfo(FString::Printf(
		TEXT("[ABTS][M11-B-v2.1][ParticleBeamDeterminism] ")
		TEXT("Contract=0x%016llx Diagnostic=%s Solves=%llu ")
		TEXT("Initial=%llu Nominal1=%llu Geometry1=%llu"),
		static_cast<unsigned long long>(
			SingleThread.ContractHash),
		UTF8_TO_TCHAR(SingleThread.Diagnostic.c_str()),
		static_cast<unsigned long long>(CountSolves(SingleMetrics)),
		static_cast<unsigned long long>(
			SingleMetrics.InitialParticleSolveCount),
		static_cast<unsigned long long>(
			SingleMetrics.NominalProposalSolveCounts[0]),
		static_cast<unsigned long long>(
			SingleMetrics.GeometryProposalCounts[0])));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11B21ParticleBeamSuccessfulFixtureTest,
	"ABTS.M11B.V2_1.ParticleBeam.SuccessfulFixture",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11B21ParticleBeamSuccessfulFixtureTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTS::M11Search;

	ParticleBeamSearchContract Contract =
		ParticleBeamSearchContract::MakeV4();
	Contract.ConstructionSeed = 296883217ull;
	Contract.ExplorationSeed =
		Contract.ConstructionSeed ^ 0x11b245001ull;
	Contract.HoldoutSeed =
		Contract.ConstructionSeed ^ 0x11b245002ull;
	Contract.RootParameterCount = 64;
	Contract.ExplorationSampleCount = 1024;
	Contract.BeamWidth = 12;
	Contract.HoldoutSampleCount = 512;

	std::string Failure;
	ParticleBeamSearchResult SingleThreadResult;
	const bool bRanSingle = ParticleBeamSearch::Run(
		Contract,
		1,
		2,
		SingleThreadResult,
		&Failure);
	TestTrue(
		TEXT("The frozen full-chain particle-beam fixture completes on one thread"),
		bRanSingle);
	if (!bRanSingle)
	{
		AddError(FString::Printf(
			TEXT("Single-thread full-chain particle fixture failed: %s"),
			UTF8_TO_TCHAR(Failure.c_str())));
		return false;
	}

	Failure.clear();
	ParticleBeamSearchResult ParallelResult;
	const bool bRanParallel = ParticleBeamSearch::Run(
		Contract,
		4,
		2,
		ParallelResult,
		&Failure);
	TestTrue(
		TEXT("The frozen full-chain particle-beam fixture completes on four threads"),
		bRanParallel);
	if (!bRanParallel)
	{
		AddError(FString::Printf(
			TEXT("Four-thread full-chain particle fixture failed: %s"),
			UTF8_TO_TCHAR(Failure.c_str())));
		return false;
	}

	const ParticleBeamSearchResult& Result = SingleThreadResult;
	TestEqual(
		TEXT("Successful-chain diagnostics are thread-count invariant"),
		FString(UTF8_TO_TCHAR(ParallelResult.Diagnostic.c_str())),
		FString(UTF8_TO_TCHAR(Result.Diagnostic.c_str())));
	TestEqual(
		TEXT("Successful-chain contract identity is thread-count invariant"),
		ParallelResult.ContractHash,
		Result.ContractHash);
	TestEqual(
		TEXT("Successful-chain construction evidence is thread-count invariant"),
		ParallelResult.ConstructionAggregateHash,
		Result.ConstructionAggregateHash);
	TestEqual(
		TEXT("Successful-chain selected evidence is thread-count invariant"),
		ParallelResult.CandidateAggregateHash,
		Result.CandidateAggregateHash);
	TestEqual(
		TEXT("Successful-chain evaluation count is thread-count invariant"),
		ParallelResult.Evaluations.size(),
		Result.Evaluations.size());
	TestEqual(
		TEXT("Successful-chain selected count is thread-count invariant"),
		ParallelResult.TopCandidates.size(),
		Result.TopCandidates.size());

	const ParticleBeamConstructionMetrics& SingleLedger =
		Result.Construction;
	const ParticleBeamConstructionMetrics& ParallelLedger =
		ParallelResult.Construction;
	TestEqual(
		TEXT("Initial particle solve count is thread-count invariant"),
		ParallelLedger.InitialParticleSolveCount,
		SingleLedger.InitialParticleSolveCount);
	TestEqual(
		TEXT("Holdout solve count is thread-count invariant"),
		ParallelLedger.HoldoutSolveCount,
		SingleLedger.HoldoutSolveCount);
	TestEqual(
		TEXT("Final audit solve count is thread-count invariant"),
		ParallelLedger.FinalAuditSolveCount,
		SingleLedger.FinalAuditSolveCount);
	for (std::size_t Index = 0;
		Index < SingleLedger.GeometryProposalCounts.size();
		++Index)
	{
		const FString Prefix = FString::Printf(
			TEXT("Successful-chain stage %u"),
			static_cast<uint32>(Index + 1));
		TestEqual(
			*(Prefix + TEXT(" geometry proposals are thread-count invariant")),
			ParallelLedger.GeometryProposalCounts[Index],
			SingleLedger.GeometryProposalCounts[Index]);
		TestEqual(
			*(Prefix + TEXT(" nominal solves are thread-count invariant")),
			ParallelLedger.NominalProposalSolveCounts[Index],
			SingleLedger.NominalProposalSolveCounts[Index]);
		TestEqual(
			*(Prefix + TEXT(" coarse solves are thread-count invariant")),
			ParallelLedger.CoarseParticleSolveCounts[Index],
			SingleLedger.CoarseParticleSolveCounts[Index]);
		TestEqual(
			*(Prefix + TEXT(" refinement solves are thread-count invariant")),
			ParallelLedger.RefinementParticleSolveCounts[Index],
			SingleLedger.RefinementParticleSolveCounts[Index]);
		TestEqual(
			*(Prefix + TEXT(" beam survivors are thread-count invariant")),
			ParallelLedger.BeamSurvivorCounts[Index],
			SingleLedger.BeamSurvivorCounts[Index]);
	}

	const std::size_t ComparableEvaluationCount = std::min(
		Result.Evaluations.size(),
		ParallelResult.Evaluations.size());
	for (std::size_t Index = 0;
		Index < ComparableEvaluationCount;
		++Index)
	{
		const ParticleBeamCandidateRecord& SingleCandidate =
			Result.Evaluations[Index];
		const ParticleBeamCandidateRecord& ParallelCandidate =
			ParallelResult.Evaluations[Index];
		const FString Prefix = FString::Printf(
			TEXT("Successful-chain evaluation %u"),
			static_cast<uint32>(Index));
		TestEqual(
			*(Prefix + TEXT(" source identity is thread-count invariant")),
			ParallelCandidate.Candidate.CandidateSourceHash,
			SingleCandidate.Candidate.CandidateSourceHash);
		TestEqual(
			*(Prefix + TEXT(" request identity is thread-count invariant")),
			ParallelCandidate.Candidate.NominalRequestHash,
			SingleCandidate.Candidate.NominalRequestHash);
		TestEqual(
			*(Prefix + TEXT(" result identity is thread-count invariant")),
			ParallelCandidate.Candidate.NominalResultHash,
			SingleCandidate.Candidate.NominalResultHash);
		TestEqual(
			*(Prefix + TEXT(" score identity is thread-count invariant")),
			ParallelCandidate.Candidate.ScoreHash,
			SingleCandidate.Candidate.ScoreHash);
		TestEqual(
			*(Prefix + TEXT(" construction identity is thread-count invariant")),
			ParallelCandidate.ConstructionHash,
			SingleCandidate.ConstructionHash);
	}

	const std::size_t ComparableSelectedCount = std::min(
		Result.TopCandidates.size(),
		ParallelResult.TopCandidates.size());
	for (std::size_t Index = 0;
		Index < ComparableSelectedCount;
		++Index)
	{
		TestEqual(
			*FString::Printf(
				TEXT("Successful-chain selected rank %u is thread-count invariant"),
				static_cast<uint32>(Index + 1)),
			ParallelResult.TopCandidates[Index]
				.Candidate.CandidateSourceHash,
			Result.TopCandidates[Index].Candidate.CandidateSourceHash);
	}

	std::uint64_t SolverInvocationCount =
		Result.Construction.InitialParticleSolveCount
		+ Result.Construction.HoldoutSolveCount
		+ Result.Construction.FinalAuditSolveCount;
	for (std::size_t Index = 0;
		Index < Result.Construction.NominalProposalSolveCounts.size();
		++Index)
	{
		SolverInvocationCount +=
			Result.Construction.NominalProposalSolveCounts[Index]
			+ Result.Construction.CoarseParticleSolveCounts[Index]
			+ Result.Construction.RefinementParticleSolveCounts[Index];
	}
	const std::size_t AcceptedCount = static_cast<std::size_t>(
		std::count_if(
			Result.Evaluations.begin(),
			Result.Evaluations.end(),
			[](const ParticleBeamCandidateRecord& Candidate)
			{
				return Candidate.Candidate.IsAccepted();
			}));

	TestEqual(
		TEXT("The full-chain fixture contract hash remains frozen"),
		Result.ContractHash,
		static_cast<std::uint64_t>(0xaccb3830e7ed8d7eull));
	TestEqual(
		TEXT("The full-chain fixture reaches its requested completion"),
		FString(UTF8_TO_TCHAR(Result.Diagnostic.c_str())),
		FString(TEXT("Completed")));
	TestEqual(
		TEXT("The full-chain fixture audits five layouts"),
		Result.Evaluations.size(),
		static_cast<std::size_t>(5));
	TestEqual(
		TEXT("All five audited layouts pass the frozen v3 audit"),
		AcceptedCount,
		static_cast<std::size_t>(5));
	TestEqual(
		TEXT("The diversity selector keeps the two frozen layouts"),
		Result.TopCandidates.size(),
		static_cast<std::size_t>(2));
	if (Result.TopCandidates.size() == 2)
	{
		TestEqual(
			TEXT("The first full-chain layout identity remains frozen"),
			Result.TopCandidates[0].Candidate.CandidateSourceHash,
			static_cast<std::uint64_t>(0xed74ffaf0de8028full));
		TestEqual(
			TEXT("The second full-chain layout identity remains frozen"),
			Result.TopCandidates[1].Candidate.CandidateSourceHash,
			static_cast<std::uint64_t>(0xf22ad256fd791e07ull));
	}
	TestEqual(
		TEXT("The full-chain solve ledger remains frozen"),
		SolverInvocationCount,
		static_cast<std::uint64_t>(83097));
	TestEqual(
		TEXT("The construction aggregate covers stage and holdout evidence"),
		Result.ConstructionAggregateHash,
		static_cast<std::uint64_t>(0x9120faec9339f0d9ull));
	TestEqual(
		TEXT("The selected aggregate covers both complete candidates"),
		Result.CandidateAggregateHash,
		static_cast<std::uint64_t>(0xbb940a139884cc78ull));
	TestTrue(
		TEXT("The successful fixture executes independent holdout solves"),
		Result.Construction.HoldoutSolveCount > 0);
	TestTrue(
		TEXT("The successful fixture executes the frozen v3 final audit"),
		Result.Construction.FinalAuditSolveCount > 0);

	AddInfo(FString::Printf(
		TEXT("[ABTS][M11-B-v2.1][ParticleBeamSuccessfulFixture] ")
		TEXT("Contract=0x%016llx Construction=0x%016llx ")
		TEXT("Candidates=0x%016llx Solves=%llu Accepted=%llu"),
		static_cast<unsigned long long>(Result.ContractHash),
		static_cast<unsigned long long>(
			Result.ConstructionAggregateHash),
		static_cast<unsigned long long>(
			Result.CandidateAggregateHash),
		static_cast<unsigned long long>(SolverInvocationCount),
		static_cast<unsigned long long>(AcceptedCount)));
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
