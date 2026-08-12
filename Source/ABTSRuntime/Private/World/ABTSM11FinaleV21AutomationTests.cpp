// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "HAL/IConsoleManager.h"
#include "Camera/ABTSM11FinaleCameraDirector.h"
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
		TestEqual(
			TEXT("GameMode and Integration preview read one Candidate authority"),
			FABTSM11CandidateExperienceCatalog::
				GetRequestedCandidateRank(),
			CandidateRankVariable->GetInt());
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
		2278ull);
	TestEqual(
		TEXT("Candidate source hash is frozen"),
		Identity.CandidateSourceHash,
		0xaaae0dd44f14f785ull);
	TestEqual(
		TEXT("Candidate request hash is frozen"),
		Identity.NominalRequestHash,
		0x5ecc893f6eb7003dull);
	TestEqual(
		TEXT("Candidate result hash is frozen"),
		Identity.NominalResultHash,
		0xb47d8314ebe69376ull);
	TestEqual(
		TEXT("Candidate score hash is frozen"),
		Identity.ScoreHash,
		0xd6e03f2d9e0f3b8bull);
	TestTrue(TEXT("Candidate preset is structurally valid"), Preset.IsValid());
	TestEqual(
		TEXT("Candidate search algorithm is v3"),
		Preset.SearchAlgorithmVersion,
		3);
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

	FABTSM11PlaybackPlan FirstContactPlan;
	FABTSM11PlaybackPlan SecondContactPlan;
	const bool bFirstContactBuilt =
		FirstContactPlan.BuildCandidatePresentationContact(
			Preset,
			Result,
			Classification);
	const bool bSecondContactBuilt =
		SecondContactPlan.BuildCandidatePresentationContact(
			Preset,
			Result,
			Classification);
	TestTrue(
		*FString::Printf(
			TEXT("Candidate presentation appends a visible physical contact: %s"),
			*FirstContactPlan.Failure),
		bFirstContactBuilt);
	TestTrue(
		*FString::Printf(
			TEXT("Candidate presentation contact rebuild is deterministic: %s"),
			*SecondContactPlan.Failure),
		bSecondContactBuilt);
	if (!bFirstContactBuilt || !bSecondContactBuilt)
	{
		return false;
	}
	TestTrue(
		TEXT("Candidate source qualification remains explicit"),
		FirstContactPlan.bCandidateQualifiedIntercept);
	TestTrue(
		TEXT("Candidate presentation reaches the 800 cm contact sphere"),
		FirstContactPlan.bPhysicalTargetHit);
	TestTrue(
		TEXT("Candidate presentation explicitly types its transfer"),
		FirstContactPlan.bUsesVisibleTerminalTransfer);
	TestEqual(
		TEXT("Candidate contact plan hash is deterministic"),
		FirstContactPlan.PlanHash,
		SecondContactPlan.PlanHash);
	TestTrue(
		TEXT("Candidate contact extends rather than rewrites the source"),
		FirstContactPlan.TransferStartTimeSeconds
			== FirstPlan.DurationSeconds);
	TestTrue(
		TEXT("Candidate contact duration is positive"),
		FirstContactPlan.TransferEndTimeSeconds
			> FirstContactPlan.TransferStartTimeSeconds);
	const FVector3d PhysicalCenter =
		Preset.CanonicalScenario.Target.GetGeometricContactCenterCM();
	const double PhysicalRadius =
		Preset.CanonicalScenario.Target.GetGeometricContactRadiusCM();
	TestTrue(
		TEXT("Candidate contact endpoint lies on the physical sphere"),
		FMath::IsNearlyEqual(
			(FirstContactPlan.Points.Last().PositionCM - PhysicalCenter)
				.Length(),
			PhysicalRadius,
			1.0e-3));
	bool bAuthoritativePrefixPreserved =
		FirstContactPlan.Points.Num() >= FirstPlan.Points.Num();
	for (int32 Index = 0;
		Index < FirstPlan.Points.Num() && bAuthoritativePrefixPreserved;
		++Index)
	{
		bAuthoritativePrefixPreserved =
			FirstContactPlan.Points[Index].TimeSeconds
				== FirstPlan.Points[Index].TimeSeconds
				&& FirstContactPlan.Points[Index].PositionCM.Equals(
					FirstPlan.Points[Index].PositionCM,
					0.0)
				&& FirstContactPlan.Points[Index].VelocityCMPerSec.Equals(
					FirstPlan.Points[Index].VelocityCMPerSec,
					0.0)
				&& FirstContactPlan.Points[Index].SegmentKind
					== FirstPlan.Points[Index].SegmentKind;
	}
	TestTrue(
		TEXT("Candidate authoritative prefix is byte-for-byte preserved"),
		bAuthoritativePrefixPreserved);

	FABTSM11FinaleLayoutPreset Rank11Preset;
	FABTSM11CandidateExperienceIdentity Rank11Identity;
	TestTrue(
		TEXT("Rank11 candidate rebuilds for terminal contact coverage"),
		FABTSM11CandidateExperienceCatalog::BuildCandidate(
			11,
			Rank11Preset,
			Rank11Identity,
			&Failure));
	FABTSM11TrajectoryRequest Rank11Request;
	FABTSM11TrajectoryResult Rank11Result;
	TestTrue(
		TEXT("Rank11 nominal request builds"),
		Rank11Preset.BuildRequest(
			Rank11Preset.NominalInput,
			0x7u,
			Rank11Request,
			&Failure));
	TestTrue(
		TEXT("Rank11 nominal request solves"),
		FABTSM11GravityAssistSolver::Solve(
			Rank11Request,
			Rank11Result,
			&Failure));
	const FABTSM11PrefixClassification Rank11Classification =
		FABTSM11PrefixClassifier::Classify(
			Rank11Preset,
			Rank11Result,
			0x7u);
	TestTrue(TEXT("Rank11 nominal input remains F4"), Rank11Classification.IsF(4));
	FABTSM11PlaybackPlan Rank11ContactPlan;
	const bool bRank11ContactBuilt =
		Rank11ContactPlan.BuildCandidatePresentationContact(
			Rank11Preset,
			Rank11Result,
			Rank11Classification);
	TestTrue(
		*FString::Printf(
			TEXT("Rank11 appends a visible physical contact: %s"),
			*Rank11ContactPlan.Failure),
		bRank11ContactBuilt);
	if (!bRank11ContactBuilt)
	{
		return false;
	}
	const FVector3d Rank11PhysicalCenter =
		Rank11Preset.CanonicalScenario.Target
			.GetGeometricContactCenterCM();
	const double Rank11PhysicalRadius =
		Rank11Preset.CanonicalScenario.Target
			.GetGeometricContactRadiusCM();
	TestTrue(
		TEXT("Rank11 endpoint lies on the 800 cm contact sphere"),
		FMath::IsNearlyEqual(
			(Rank11ContactPlan.Points.Last().PositionCM
				- Rank11PhysicalCenter).Length(),
			Rank11PhysicalRadius,
			1.0e-3));
	TestTrue(
		TEXT("Rank11 transfer duration remains cinematic"),
		Rank11ContactPlan.TransferEndTimeSeconds
			- Rank11ContactPlan.TransferStartTimeSeconds
			>= 0.5
			&& Rank11ContactPlan.TransferEndTimeSeconds
				- Rank11ContactPlan.TransferStartTimeSeconds
				<= 8.0);

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
			FABTSM11CandidateExperienceCatalog::LastCandidateRank + 1,
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11CV21FrozenV4CandidateCatalogTest,
	"ABTS.M11C.V2_1.FrozenV4CandidateCatalog",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11CV21FrozenV4CandidateCatalogTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;

	struct FFrozenExpectation
	{
		int32 Rank;
		uint64 SourceHash;
		uint64 ResultHash;
		bool bRequiresRuntimeF4 = true;
	};
	constexpr FFrozenExpectation Expectations[] = {
		{3, 0xed74ffaf0de8028full, 0x791c9a64b195b0d4ull},
		{4, 0xf22ad256fd791e07ull, 0xbf710eb5c1e114c1ull},
		{5, 0xcdc6e41075d99493ull, 0xa7695a10b44f8281ull},
		{6, 0x80d274a67e1e9944ull, 0x9de084d9f77c9ee7ull},
		{7, 0xb3e0f00ca35d499aull, 0xe7c6c093e3cc9533ull, false},
		{8, 0x617687274ed0c29aull, 0xaac8ba98079011fdull},
		{9, 0x166f0aa067d54328ull, 0x22675cdfb00406d5ull},
		{10, 0x2b06db2cf348d75full, 0x99012cedf3d01c06ull},
		{11, 0xcb23499fc6f7c9d3ull, 0x505f3312ac8ae07full},
		{12, 0x58840ee73ddd70f5ull, 0xf746bbe4ca7b9748ull},
	};

	for (const FFrozenExpectation& Expected : Expectations)
	{
		FABTSM11FinaleLayoutPreset Preset;
		FABTSM11CandidateExperienceIdentity Identity;
		FString Failure;
		const bool bBuilt =
			FABTSM11CandidateExperienceCatalog::BuildCandidate(
				Expected.Rank,
				Preset,
				Identity,
				&Failure);
		TestTrue(
			*FString::Printf(
				TEXT("Frozen v4 Candidate rank %d builds"),
				Expected.Rank),
			bBuilt);
		if (!bBuilt)
		{
			AddError(FString::Printf(
				TEXT("Frozen v4 Candidate rank %d rejected: %s"),
				Expected.Rank,
				*Failure));
			continue;
		}

		TestEqual(
			*FString::Printf(
				TEXT("Rank %d retains its source identity"),
				Expected.Rank),
			Identity.CandidateSourceHash,
			Expected.SourceHash);
		FABTSM11TrajectoryRequest Request;
		const bool bRequestBuilt = Preset.BuildRequest(
			Preset.NominalInput,
			0x7u,
			Request,
			&Failure);
		TestTrue(
			*FString::Printf(
				TEXT("Rank %d nominal request builds"),
				Expected.Rank),
			bRequestBuilt);
		if (!bRequestBuilt)
		{
			continue;
		}
		FABTSM11TrajectoryResult Result;
		const bool bSolved = FABTSM11GravityAssistSolver::Solve(
			Request,
			Result,
			&Failure);
		TestTrue(
			*FString::Printf(
				TEXT("Rank %d nominal trajectory solves"),
				Expected.Rank),
			bSolved);
		if (!bSolved)
		{
			continue;
		}
		TestEqual(
			*FString::Printf(
				TEXT("Rank %d nominal result identity is frozen"),
				Expected.Rank),
			Result.ValidationHash,
			Expected.ResultHash);
		const FABTSM11PrefixClassification Classification =
			FABTSM11PrefixClassifier::Classify(
				Preset,
				Result,
				0x7u);
		if (Expected.bRequiresRuntimeF4)
		{
			TestTrue(
				*FString::Printf(
					TEXT("Rank %d nominal input reaches F4"),
					Expected.Rank),
				Classification.IsF(4));
		}
		else
		{
			TestTrue(
				TEXT("Rank 7 retains its diagnostic terminal hit"),
				Result.DidHitTarget()
					&& Result.CompletedAssistCount == 3);
			TestFalse(
				TEXT("Rank 7 does not claim runtime-qualified F4"),
				Classification.IsF(4));
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11CM7RandomF4WitnessTest,
	"ABTS.M11C.M7.RandomF4Witnesses",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11CM7RandomF4WitnessTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FABTSM11FinaleLayoutPreset Preset;
	FABTSM11CandidateExperienceIdentity Identity;
	FString Failure;
	if (!FABTSM11CandidateExperienceCatalog::BuildCandidate(
		11, Preset, Identity, &Failure))
	{
		AddError(FString::Printf(
			TEXT("Rank11 rebuild failed: %s"), *Failure));
		return false;
	}

	FRandomStream Random(0x4d3757a1);
	TSet<uint64> WitnessHashes;
	int32 WitnessCount = 0;
	for (int32 Attempt = 0; Attempt < 96 && WitnessCount < 2; ++Attempt)
	{
		FABTSM11FinaleLaunchInput Input = Preset.NominalInput;
		Input.YawDegrees += Random.FRandRange(-0.20f, 0.20f);
		Input.PitchDegrees += Random.FRandRange(-0.20f, 0.20f);
		Input.Power -= Random.FRandRange(0.00025f, 0.00450f);
		if (!Preset.LaunchModel.Contains(Input))
		{
			continue;
		}
		FABTSM11TrajectoryRequest Request;
		FABTSM11TrajectoryResult Result;
		if (!Preset.BuildRequest(Input, 0x7u, Request, &Failure)
			|| !FABTSM11GravityAssistSolver::Solve(Request, Result, &Failure))
		{
			continue;
		}
		const FABTSM11PrefixClassification Classification =
			FABTSM11PrefixClassifier::Classify(Preset, Result, 0x7u);
		if (!Classification.IsF(4)
			|| WitnessHashes.Contains(Result.ValidationHash))
		{
			continue;
		}
		FABTSM11PlaybackPlan ContactPlan;
		if (!ContactPlan.BuildCandidatePresentationContact(
			Preset, Result, Classification))
		{
			continue;
		}
		FABTSM11FinaleCameraShotPlan CameraPlan;
		if (!CameraPlan.Build(
			Result, FABTSM11FinaleCameraShotSettings(), &Failure))
		{
			continue;
		}
		WitnessHashes.Add(Result.ValidationHash);
		++WitnessCount;
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[ABTS][M11-C][M7] RandomF4Witness Seed=0x4d3757a1 Index=%d Attempt=%d Yaw=%.12f Pitch=%.12f Power=%.12f Result=0x%016llx Plan=0x%016llx Adaptive=%d"),
			WitnessCount,
			Attempt,
			Input.YawDegrees,
			Input.PitchDegrees,
			Input.Power,
			Result.ValidationHash,
			ContactPlan.PlanHash,
			CameraPlan.bUsesAdaptiveCompression ? 1 : 0);
	}
	TestEqual(
		TEXT("Deterministic Rank11 neighborhood yields two distinct F4 recordings"),
		WitnessCount,
		2);
	return !HasAnyErrors();
}

#endif
