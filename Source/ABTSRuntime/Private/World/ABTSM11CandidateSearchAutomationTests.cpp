// Copyright Epic Games, Inc. All Rights Reserved.

#include "M11Core/ABTSM11CoreConformance.h"
#include "M11Core/ABTSM11CoreSolver.h"
#include "M11Search/ABTSM11CandidateSearch.h"
#include "Misc/AutomationTest.h"
#include "World/ABTSM11GravityAssistCoreAdapter.h"
#include "World/ABTSM11GravityAssistSolver.h"

#if WITH_DEV_AUTOMATION_TESTS

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

	constexpr std::uint64_t FrozenWorkIndex = 166ull;
	constexpr std::uint64_t FrozenCandidateSourceHash =
		0xbd7d63e871c524bfull;
	constexpr std::uint64_t FrozenNominalRequestHash =
		0xa40f917f70db40abull;
	constexpr std::uint64_t FrozenNominalResultHash =
		0xb2987a35306c3654ull;
	constexpr std::uint64_t FrozenScoreHash =
		0xfacaab57a03dd3beull;

	CandidateSearchContract Contract =
		CandidateSearchContract::MakeV2_1();
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
	TestTrue(
		TEXT("The 0.90 power probe cannot complete the full chain"),
		Candidate.Metrics.LowPowerCompletedAssistCount < 3);
	TestTrue(
		TEXT("The candidate completes within 60 seconds"),
		Candidate.Metrics.TotalFlightTimeSeconds <= 60.0);
	TestTrue(
		TEXT("The candidate layout remains visibly non-collinear"),
		Candidate.Metrics.MinimumLayoutTurnRadians >= 0.30);
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
