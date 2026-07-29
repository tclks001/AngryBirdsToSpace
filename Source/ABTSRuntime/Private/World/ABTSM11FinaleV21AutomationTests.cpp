// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "HAL/IConsoleManager.h"
#include "World/ABTSM11CandidateExperienceCatalog.h"
#include "World/ABTSM11FinaleInteractionTypes.h"
#include "World/ABTSM11FinaleLayoutCertification.h"
#include "World/ABTSM11GravityAssistSolver.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11CV21InputParityAndLatestOnlyTest,
	"ABTS.M11C.V2_1.InputParityAndLatestOnly",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11CV21InputParityAndLatestOnlyTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;

	TestEqual(
		TEXT("M6 parity minimum pull remains 120 cm"),
		FABTSM11M6InputParityProfile::MinimumPullDistanceCM,
		120.0);
	TestEqual(
		TEXT("M6 parity maximum pull remains 430 cm"),
		FABTSM11M6InputParityProfile::MaximumPullDistanceCM,
		430.0);
	TestEqual(
		TEXT("M6 parity wheel step remains 0.08"),
		FABTSM11M6InputParityProfile::PowerWheelStep,
		0.08);
	TestEqual(
		TEXT("M6 parity aim-plane radius remains 260 cm"),
		FABTSM11M6InputParityProfile::MaximumAimPlaneOffsetCM,
		260.0);
	TestEqual(
		TEXT("M6 parity target lift remains 65 cm"),
		FABTSM11M6InputParityProfile::LaunchTargetLiftCM,
		65.0);
	TestEqual(
		TEXT("M6 parity bird pouch offset remains 20 cm"),
		FABTSM11M6InputParityProfile::BirdInPouchOffsetCM,
		20.0);

	const FABTSM11FinaleLayoutPreset V1 =
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
	FABTSM11FinaleLaunchInput Mapped;
	TestTrue(
		TEXT("A finite local launch direction maps into the launch domain"),
		ABTSM11MapLocalLaunchDirectionToInput(
			V1.LaunchModel,
			FVector3d(1.0, -0.20, 0.30),
			0.75,
			Mapped));
	TestTrue(
		TEXT("Pulling the pouch toward local +Y launches toward negative yaw"),
		Mapped.YawDegrees < 0.0);
	TestTrue(
		TEXT("A downward-relative pouch produces positive pitch"),
		Mapped.PitchDegrees > 0.0);
	TestEqual(
		TEXT("Direction mapping does not rewrite wheel-controlled power"),
		Mapped.Power,
		0.75);
	const FVector3d RoundTripDirection =
		V1.LaunchModel.MapDirection(Mapped);
	TestTrue(
		TEXT("Yaw/Pitch direction round-trips through the launch model"),
		RoundTripDirection.Equals(
			FVector3d(1.0, -0.20, 0.30).GetSafeNormal(),
			1.0e-12));

	TestFalse(
		TEXT("A clean preview state does not start work"),
		ABTSM11CanStartLatestOnlyPreview(false, false));
	TestTrue(
		TEXT("A dirty idle preview starts immediately"),
		ABTSM11CanStartLatestOnlyPreview(true, false));
	TestFalse(
		TEXT("One in-flight solve coalesces newer input"),
		ABTSM11CanStartLatestOnlyPreview(true, true));
	TestTrue(
		TEXT("Matching revision and input may publish"),
		ABTSM11CanPublishLatestOnlyPreview(42, 42, true));
	TestFalse(
		TEXT("A stale revision can never publish"),
		ABTSM11CanPublishLatestOnlyPreview(41, 42, true));
	TestFalse(
		TEXT("A same-revision input mismatch can never publish"),
		ABTSM11CanPublishLatestOnlyPreview(42, 42, false));
	TestTrue(
		TEXT("R may reset an in-flight candidate attempt"),
		ABTSM11IsResettableFinaleState(
			EABTSM11FinaleInteractionState::Launched));
	TestTrue(
		TEXT("R may reset the initial drag gesture"),
		ABTSM11IsResettableFinaleState(
			EABTSM11FinaleInteractionState::Aiming));
	TestFalse(
		TEXT("R does not mutate an inactive Ready system"),
		ABTSM11IsResettableFinaleState(
			EABTSM11FinaleInteractionState::Ready));
	const IConsoleVariable* CandidateRankVariable =
		IConsoleManager::Get().FindConsoleVariable(
			TEXT("abts.M11.CandidateRank"));
	TestNotNull(
		TEXT("Editor Candidate Rank console variable is registered"),
		CandidateRankVariable);
	if (CandidateRankVariable != nullptr)
	{
		TestEqual(
			TEXT("Uncertified Candidate playback requires explicit opt-in"),
			CandidateRankVariable->GetInt(),
			0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11CV21CandidateExperienceTest,
	"ABTS.M11C.V2_1.CandidateExperience",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11CV21CandidateExperienceTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;

	FABTSM11FinaleLayoutPreset Preset;
	FABTSM11CandidateExperienceIdentity Identity;
	FString Failure;
	const bool bBuilt =
		FABTSM11CandidateExperienceCatalog::BuildCandidate(
			1,
			Preset,
			Identity,
			&Failure);
	TestTrue(
		TEXT("Frozen Candidate rank 1 rebuilds for Editor experience"),
		bBuilt);
	if (!bBuilt)
	{
		AddError(FString::Printf(
			TEXT("Candidate rebuild failed: %s"),
			*Failure));
		return false;
	}

	TestEqual(TEXT("Candidate rank is frozen"), Identity.Rank, 1);
	TestEqual(
		TEXT("Candidate work index is frozen"),
		Identity.GlobalWorkIndex,
		166ull);
	TestEqual(
		TEXT("Candidate source hash is frozen"),
		Identity.CandidateSourceHash,
		0xbd7d63e871c524bfull);
	TestEqual(
		TEXT("Candidate request hash is frozen"),
		Identity.NominalRequestHash,
		0xa40f917f70db40abull);
	TestEqual(
		TEXT("Candidate result hash is frozen"),
		Identity.NominalResultHash,
		0xb2987a35306c3654ull);
	TestEqual(
		TEXT("Candidate score hash is frozen"),
		Identity.ScoreHash,
		0xfacaab57a03dd3beull);
	TestTrue(TEXT("Candidate preset is structurally valid"), Preset.IsValid());
	TestEqual(TEXT("Candidate solver version is v2"), Preset.SolverConfig.SolverVersion, 2);
	TestEqual(
		TEXT("Candidate hash schema is v2"),
		Preset.SolverConfig.HashSchemaVersion,
		2);
	TestEqual(
		TEXT("Candidate raw flight is capped at 60 seconds"),
		Preset.LaunchModel.MaximumSimulationTimeSeconds,
		60.0);
	TestEqual(TEXT("Candidate has no preset hash"), Preset.PresetHash, 0ull);
	TestEqual(
		TEXT("Candidate has no certification hash"),
		Preset.CertificationHash,
		0ull);
	TestEqual(
		TEXT("Candidate has no certified bundle hash"),
		Preset.CertifiedBundleHash,
		0ull);
	for (const FABTSM11PrefixTrustRegion& Region
		: Preset.PrefixTrustRegions)
	{
		TestEqual(
			TEXT("Candidate trust aid is explicitly non-certified"),
			Region.RegionHash,
			0ull);
	}

	FABTSM11TrajectoryRequest Request;
	TestTrue(
		TEXT("Candidate nominal request builds through the UE facade"),
		Preset.BuildRequest(
			Preset.NominalInput,
			0x7u,
			Request,
			&Failure));
	FABTSM11TrajectoryResult Result;
	TestTrue(
		TEXT("Candidate nominal request solves"),
		FABTSM11GravityAssistSolver::Solve(
			Request,
			Result,
			&Failure));
	TestEqual(
		TEXT("Candidate nominal result remains frozen"),
		Result.ValidationHash,
		Identity.NominalResultHash);
	const FABTSM11PrefixClassification Classification =
		FABTSM11PrefixClassifier::Classify(Preset, Result, 0x7u);
	TestTrue(TEXT("Candidate nominal input reaches F4"), Classification.IsF(4));

	FABTSM11PlaybackPlan FirstPlan;
	FABTSM11PlaybackPlan SecondPlan;
	TestTrue(
		TEXT("Candidate builds a raw qualified playback plan"),
		FirstPlan.BuildCandidateQualified(
			Preset,
			Result,
			Classification));
	TestTrue(
		TEXT("Candidate playback rebuild is deterministic"),
		SecondPlan.BuildCandidateQualified(
			Preset,
			Result,
			Classification));
	TestTrue(
		TEXT("Candidate plan records a qualified intercept"),
		FirstPlan.bCandidateQualifiedIntercept);
	TestFalse(
		TEXT("Candidate qualification never claims 800 cm physical contact"),
		FirstPlan.bPhysicalTargetHit);
	TestFalse(
		TEXT("Candidate raw playback never uses the v1 terminal transfer"),
		FirstPlan.bUsesVisibleTerminalTransfer);
	TestTrue(
		TEXT("Candidate playback remains within 60 seconds"),
		FirstPlan.DurationSeconds <= 60.0 + 1.0e-9);
	TestEqual(
		TEXT("Candidate playback plan hash is deterministic"),
		FirstPlan.PlanHash,
		SecondPlan.PlanHash);

	FABTSM110FinaleLocalFrame IdentityFrame;
	IdentityFrame.LayoutVersion = 1;
	IdentityFrame.LaunchTaskId = 1;
	IdentityFrame.AnchorCellId = 2;
	IdentityFrame.SlotPairId = 3;
	IdentityFrame.WorldTransform = FTransform::Identity;
	IdentityFrame.LeftSlotWorldLocation = FVector(0.0, -105.0, 0.0);
	IdentityFrame.RightSlotWorldLocation = FVector(0.0, 105.0, 0.0);
	IdentityFrame.bValid = true;
	FABTSM11OrbitalDiagramSnapshot Diagram;
	TestTrue(
		TEXT("Candidate raw path builds an orbital overview"),
		FABTSM11OrbitalDiagramBuilder::Build(
			Preset,
			IdentityFrame,
			FirstPlan.Points,
			FirstPlan.ReleasedTrajectoryHash,
			Diagram));
	bool bCausalGlyphsInsideViewport = Diagram.bValid;
	for (int32 BodyIndex = 1;
		BodyIndex < FABTSM11GravityScenario::BodyCount;
		++BodyIndex)
	{
		const FABTSM11DiagramBody& Body = Diagram.Bodies[BodyIndex];
		const double GlyphRadiusScale =
			BodyIndex == FABTSM11GravityScenario::AssistCount
				? 1.65
				: 1.0;
		bCausalGlyphsInsideViewport &=
			Body.Center.Length()
				+ Body.VisualRadius * GlyphRadiusScale
			<= 0.800001;
	}
	bCausalGlyphsInsideViewport &=
		Diagram.UFOCenter.Length()
			+ Diagram.UFORadius * 1.65
		<= 0.800001;
	TestTrue(
		TEXT("Three assist glyphs and UFO stay inside the circular viewport"),
		bCausalGlyphsInsideViewport);

	FABTSM11FinaleLayoutPreset RejectedPreset;
	FABTSM11CandidateExperienceIdentity RejectedIdentity;
	TestFalse(
		TEXT("Candidate rank zero is not a catalog candidate"),
		FABTSM11CandidateExperienceCatalog::BuildCandidate(
			0,
			RejectedPreset,
			RejectedIdentity,
			&Failure));
	TestFalse(
		TEXT("Unknown Candidate rank fails closed"),
		FABTSM11CandidateExperienceCatalog::BuildCandidate(
			5,
			RejectedPreset,
			RejectedIdentity,
			&Failure));

	AddInfo(FString::Printf(
		TEXT("[ABTS][M11-C-v2.1][CandidateExperience] %s ")
		TEXT("Plan=0x%016llx Duration=%.3f Points=%d"),
		*Identity.ToLogString(),
		static_cast<unsigned long long>(FirstPlan.PlanHash),
		FirstPlan.DurationSeconds,
		FirstPlan.Points.Num()));
	return true;
}

#endif
