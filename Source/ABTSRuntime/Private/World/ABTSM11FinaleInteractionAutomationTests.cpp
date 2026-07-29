// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Async/ParallelFor.h"
#include "Camera/ABTSM6SlingshotCamera.h"
#include "Contracts/ABTSWorldGenerationContracts.h"
#include "Engine/World.h"
#include "Game/ABTSM11GameMode.h"
#include "Player/ABTSM11PlayerController.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "UI/ABTSM11FinaleHUD.h"
#include "UObject/UObjectGlobals.h"
#include "World/ABTSM11FinaleInteractionSystem.h"
#include "World/ABTSM11FinaleInteractionTypes.h"
#include "World/ABTSM11FinaleLayoutCertification.h"
#include "World/ABTSM11FinaleSystem.h"
#include "World/ABTSM11GravityAssistSolver.h"
#include "World/ABTSM9Satellite.h"

#include <atomic>

namespace
{
	FABTSM11FinaleLaunchInput Midpoint(
		const FABTSM11PrefixTrustRegion& Region)
	{
		return FABTSM11FinaleLaunchInput{
			(Region.Minimum.YawDegrees
				+ Region.Maximum.YawDegrees) * 0.5,
			(Region.Minimum.PitchDegrees
				+ Region.Maximum.PitchDegrees) * 0.5,
			(Region.Minimum.Power + Region.Maximum.Power) * 0.5};
	}

	FABTSM110FinaleLocalFrame MakeIdentityFrame()
	{
		FABTSM110FinaleLocalFrame Frame;
		Frame.LayoutVersion = 1;
		Frame.LaunchTaskId = 1;
		Frame.AnchorCellId = 2;
		Frame.SlotPairId = 3;
		Frame.WorldTransform = FTransform::Identity;
		Frame.LeftSlotWorldLocation = FVector(0.0f, -105.0f, 0.0f);
		Frame.RightSlotWorldLocation = FVector(0.0f, 105.0f, 0.0f);
		Frame.bValid = true;
		return Frame;
	}

	bool SolveNominal(
		const FABTSM11FinaleLayoutPreset& Preset,
		FABTSM11TrajectoryResult& OutQualified,
		FABTSM11TrajectoryResult& OutPhysical,
		FABTSM11PrefixClassification& OutClassification)
	{
		FABTSM11TrajectoryRequest QualifiedRequest;
		FABTSM11TrajectoryRequest PhysicalRequest;
		return Preset.BuildRequest(
				Preset.NominalInput,
				0x7u,
				QualifiedRequest)
			&& Preset.BuildPhysicalPlaybackRequest(
				Preset.NominalInput,
				0x7u,
				PhysicalRequest)
			&& FABTSM11GravityAssistSolver::Solve(
				QualifiedRequest,
				OutQualified)
			&& FABTSM11GravityAssistSolver::Solve(
				PhysicalRequest,
				OutPhysical)
			&& (OutClassification =
				FABTSM11PrefixClassifier::Classify(
					Preset,
					OutQualified,
					0x7u)).IsF(4);
	}

	FVector3d SimulatePlaybackAt(
		const FABTSM11PlaybackPlan& Plan,
		const double FramesPerSecond,
		const double TargetTime)
	{
		double Elapsed = Plan.Points[0].TimeSeconds;
		const double Step = 1.0 / FramesPerSecond;
		while (Elapsed + Step < TargetTime)
		{
			Elapsed += Step;
		}
		Elapsed = TargetTime;
		FVector3d Position;
		FVector3d Velocity;
		Plan.Sample(Elapsed, Position, Velocity);
		return Position;
	}

	FABTSFinaleWorldContract MakeM11CWorldContract(
		const FABTSM110FinaleLocalFrame& Frame)
	{
		FABTSFinaleWorldContract Contract;
		Contract.Identity.WorldSeed = 1103001;
		Contract.Identity.GeneratorVersion = 3;
		Contract.Identity.GenerationAttempt = 0;
		Contract.Identity.bSourceWorldAccepted = true;
		Contract.PrimaryRadiusCM =
			FABTSM11FinaleLayoutPreset::MakeCertifiedV1()
				.ReferencePrimaryRadiusCM;
		Contract.LaunchFrame = Frame;
		return Contract;
	}

	class FScopedM11CAutomationWorld
	{
	public:
		FScopedM11CAutomationWorld()
		{
			const UWorld::InitializationValues Values =
				UWorld::InitializationValues()
					.InitializeScenes(false)
					.AllowAudioPlayback(false)
					.RequiresHitProxies(false)
					.CreatePhysicsScene(false)
					.CreateNavigation(false)
					.CreateAISystem(false)
					.ShouldSimulatePhysics(false)
					.EnableTraceCollision(false)
					.SetTransactional(false)
					.CreateFXSystem(false);
			World = UWorld::CreateWorld(
				EWorldType::Game,
				false,
				TEXT("ABTSM11CInteractionAutomationWorld"),
				nullptr,
				true,
				ERHIFeatureLevel::Num,
				&Values);
		}

		~FScopedM11CAutomationWorld()
		{
			if (World != nullptr)
			{
				World->DestroyWorld(false);
				World->RemoveFromRoot();
			}
		}

		UWorld* Get() const { return World; }

	private:
		UWorld* World = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11CStabilizerTest,
	"ABTS.M11C.Unit.Stabilizer",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11CStabilizerTest::RunTest(const FString& Parameters)
{
	const FABTSM11FinaleLayoutPreset Preset =
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
	const FABTSM11PrefixTrustRegion& F1 =
		Preset.PrefixTrustRegions[0];
	const FABTSM11PrefixTrustRegion& F2 =
		Preset.PrefixTrustRegions[1];
	FABTSM11PrefixClassification Classification;

	FABTSM11PrefixStabilizer Stabilizer;
	FABTSM11FinaleLaunchInput NearInput = Midpoint(F1);
	NearInput.YawDegrees = F1.Minimum.YawDegrees
		- Preset.ScanContract.FinalYawPrecisionDegrees;
	TestTrue(
		TEXT("Stabilizer initializes in the one-cell Near envelope"),
		Stabilizer.Initialize(Preset, NearInput));
	Stabilizer.Update(0.01, Classification);
	TestEqual(
		TEXT("Near envelope lowers sensitivity without claiming F1"),
		Stabilizer.GetNearPrefixLevel(),
		1);
	TestEqual(
		TEXT("Near does not clamp"),
		Stabilizer.GetStablePrefixLevel(),
		0);
	const FABTSM11FinaleLaunchInput BeforeNearCursor =
		Stabilizer.GetDesiredInput();
	FABTSM11FinaleLaunchInput MovedNearCursor =
		BeforeNearCursor;
	MovedNearCursor.YawDegrees += 1.0;
	Stabilizer.SetAbsoluteDirectionInput(MovedNearCursor);
	TestTrue(
		TEXT("Absolute cursor deltas use the Near sensitivity scale"),
		FMath::IsNearlyEqual(
			Stabilizer.GetDesiredInput().YawDegrees
				- BeforeNearCursor.YawDegrees,
			0.45,
			1.0e-12));

	Stabilizer.Reset(Midpoint(F1));
	Classification.HighestPrefixLevel = 1;
	Stabilizer.Update(0.10, Classification);
	TestEqual(TEXT("Capture dwell is not instantaneous"),
		Stabilizer.GetStablePrefixLevel(), 0);
	Stabilizer.Update(0.11, Classification);
	TestEqual(TEXT("F1 captures after 0.2 seconds"),
		Stabilizer.GetStablePrefixLevel(), 1);

	Stabilizer.ApplyInputDelta(20.0, 0.0, 0.0);
	TestTrue(
		TEXT("Stable output remains inside the F1 core"),
		F1.Contains(Stabilizer.GetControlledInput()));
	TestTrue(
		TEXT("Raw desired input remains distinct and is not sucked nominal"),
		Stabilizer.GetDesiredInput().YawDegrees
			> F1.Maximum.YawDegrees);
	Stabilizer.Update(0.17, Classification);
	TestEqual(
		TEXT("Raw input beyond the two-cell release envelope exits protection"),
		Stabilizer.GetStablePrefixLevel(),
		0);
	TestEqual(
		TEXT("Released output returns to raw desired yaw"),
		Stabilizer.GetControlledInput().YawDegrees,
		Stabilizer.GetDesiredInput().YawDegrees);

	Stabilizer.Reset(Midpoint(F2));
	Classification.HighestPrefixLevel = 3;
	Stabilizer.Update(0.21, Classification);
	TestEqual(TEXT("Nested capture starts with F1"),
		Stabilizer.GetStablePrefixLevel(), 1);
	Stabilizer.Update(0.21, Classification);
	TestEqual(TEXT("Same-geometry TrustF2 remains a distinct state"),
		Stabilizer.GetStablePrefixLevel(), 2);
	Stabilizer.Update(0.21, Classification);
	TestEqual(TEXT("Same-geometry TrustF3 remains a distinct state"),
		Stabilizer.GetStablePrefixLevel(), 3);
	Stabilizer.CancelProtection();
	TestEqual(TEXT("Explicit cancel always clears protection"),
		Stabilizer.GetStablePrefixLevel(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11CTargetSelectorTest,
	"ABTS.M11C.Unit.TargetSelector",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11CTargetSelectorTest::RunTest(const FString& Parameters)
{
	const FABTSM11FinaleLayoutPreset Preset =
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
	FABTSM11TrajectoryResult Result;
	Result.ValidationHash = 0x11c70001ull;
	FABTSM11TrajectoryPoint& Point =
		Result.Points.AddDefaulted_GetRef();
	Point.PositionCM = Preset.LaunchModel.PouchLocalPositionCM;
	Point.VelocityCMPerSec = FVector3d(1.0, 0.0, 0.0);
	FABTSM11PrefixClassification Classification;
	FABTSM11PreviewTargetSelector Selector;

	FABTSM11PreviewSelection Selection = Selector.Update(
		0.0, Preset, Result, Classification);
	TestEqual(TEXT("No valid assists previews planet 1"),
		Selection.Target, EABTSM11PreviewTarget::Assist1);
	const int32 InitialGeometryBuildCount =
		Selector.GetGeometryBuildCount();
	Selection = Selector.Update(
		0.10, Preset, Result, Classification);
	TestEqual(
		TEXT("Stable target and Result hash reuse closest-point geometry"),
		Selector.GetGeometryBuildCount(),
		InitialGeometryBuildCount);
	Classification.ValidAssistMask = 0x1u;
	Selection = Selector.Update(0.19, Preset, Result, Classification);
	TestEqual(TEXT("Advance target requires hysteresis"),
		Selection.Target, EABTSM11PreviewTarget::Assist1);
	Selection = Selector.Update(0.02, Preset, Result, Classification);
	TestEqual(TEXT("Valid assist 1 advances to planet 2"),
		Selection.Target, EABTSM11PreviewTarget::Assist2);
	TestEqual(
		TEXT("A latched target change rebuilds geometry exactly once"),
		Selector.GetGeometryBuildCount(),
		InitialGeometryBuildCount + 1);
	Classification.ValidAssistMask = 0x3u;
	Selection = Selector.Update(0.21, Preset, Result, Classification);
	TestEqual(TEXT("Valid assists 1 and 2 advance to planet 3"),
		Selection.Target, EABTSM11PreviewTarget::Assist3);
	Classification.ValidAssistMask = 0x7u;
	Selection = Selector.Update(0.21, Preset, Result, Classification);
	TestEqual(TEXT("All three valid assists advance to physical UFO"),
		Selection.Target, EABTSM11PreviewTarget::UFO);

	FABTSM11TrajectoryEvent& QualifiedHit =
		Result.Events.AddDefaulted_GetRef();
	QualifiedHit.Type = EABTSM11TrajectoryEventType::TargetHit;
	const int32 BeforeQualifiedHashChange =
		Selector.GetGeometryBuildCount();
	++Result.ValidationHash;
	Selection = Selector.Update(0.0, Preset, Result, Classification);
	TestEqual(
		TEXT("A new Result hash refreshes same-target geometry once"),
		Selector.GetGeometryBuildCount(),
		BeforeQualifiedHashChange + 1);
	TestFalse(
		TEXT("16k qualified TargetHit is not physical UFO entry"),
		Selection.bEnteredTargetRegion);
	FABTSM11TrajectoryEvent& PhysicalContact =
		Result.Events.AddDefaulted_GetRef();
	PhysicalContact.Type = EABTSM11TrajectoryEventType::TargetContact;
	++Result.ValidationHash;
	Selection = Selector.Update(0.0, Preset, Result, Classification);
	TestTrue(TEXT("TargetContact is physical UFO entry"),
		Selection.bEnteredTargetRegion);

	FABTSM11TrajectoryResult UnhashedResult = Result;
	UnhashedResult.ValidationHash = 0;
	FABTSM11PreviewTargetSelector UnhashedSelector;
	UnhashedSelector.Update(
		0.0, Preset, UnhashedResult, Classification);
	UnhashedSelector.Update(
		0.0, Preset, UnhashedResult, Classification);
	TestEqual(
		TEXT("Unhashed synthetic results fail safe without caching"),
		UnhashedSelector.GetGeometryBuildCount(),
		2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11CPreviewReleasePlaybackTest,
	"ABTS.M11C.Unit.PreviewReleasePlayback",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11CPreviewReleasePlaybackTest::RunTest(
	const FString& Parameters)
{
	const FABTSM11FinaleLayoutPreset Preset =
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
	FABTSM11TrajectoryResult Qualified;
	FABTSM11TrajectoryResult Physical;
	FABTSM11PrefixClassification Classification;
	TestTrue(TEXT("Nominal qualified and physical paths solve"),
		SolveNominal(Preset, Qualified, Physical, Classification));
	TestEqual(TEXT("Qualified preview identity remains frozen"),
		Qualified.ValidationHash, Preset.NominalTrajectoryHash);
	TestEqual(TEXT("Physical playback identity remains frozen"),
		Physical.ValidationHash,
		Preset.PhysicalPlaybackTrajectoryHash);

	FABTSM11PlaybackPlan ExactPlan;
	TestTrue(
		TEXT("Same-input physical result builds exact Release plan"),
		ExactPlan.Build(
			Preset,
			Qualified,
			Classification,
			&Physical,
			&Physical));
	TestEqual(TEXT("Release source hash equals preview hash"),
		ExactPlan.ReleasedTrajectoryHash,
		Qualified.ValidationHash);
	TestTrue(TEXT("Exact plan reaches the physical target"),
		ExactPlan.bPhysicalTargetHit);
	TestFalse(TEXT("Exact same-input plan needs no transfer"),
		ExactPlan.bUsesVisibleTerminalTransfer);

	const double CommonTime = FMath::Min(
		100.0,
		ExactPlan.DurationSeconds * 0.5);
	const FVector3d At30 =
		SimulatePlaybackAt(ExactPlan, 30.0, CommonTime);
	const FVector3d At60 =
		SimulatePlaybackAt(ExactPlan, 60.0, CommonTime);
	const FVector3d At120 =
		SimulatePlaybackAt(ExactPlan, 120.0, CommonTime);
	TestTrue(TEXT("30 and 60 FPS sample the same absolute state"),
		At30.Equals(At60, 1.0e-8));
	TestTrue(TEXT("60 and 120 FPS sample the same absolute state"),
		At60.Equals(At120, 1.0e-8));

	FABTSM11PlaybackPlan VisibleTransferPlan;
	TestTrue(
		TEXT("F4 can explicitly build a visible C2 terminal transfer"),
		VisibleTransferPlan.Build(
			Preset,
			Qualified,
			Classification,
			nullptr,
			&Physical));
	TestTrue(TEXT("Transfer is explicitly typed and published"),
		VisibleTransferPlan.bUsesVisibleTerminalTransfer);
	TestTrue(TEXT("Transfer plan reaches physical target"),
		VisibleTransferPlan.bPhysicalTargetHit);
	TestTrue(TEXT("Transfer has a non-zero deterministic identity"),
		VisibleTransferPlan.PlanHash != 0);
	TestTrue(TEXT("Transfer time is ordered"),
		VisibleTransferPlan.TransferEndTimeSeconds
			> VisibleTransferPlan.TransferStartTimeSeconds);

	bool bExercisedNeighborTransfer = false;
	for (int32 YawOffset = -1;
		YawOffset <= 1 && !bExercisedNeighborTransfer;
		++YawOffset)
	{
		for (int32 PitchOffset = -1;
			PitchOffset <= 1 && !bExercisedNeighborTransfer;
			++PitchOffset)
		{
			for (int32 PowerOffset = -1;
				PowerOffset <= 1 && !bExercisedNeighborTransfer;
				++PowerOffset)
			{
				if (YawOffset == 0
					&& PitchOffset == 0
					&& PowerOffset == 0)
				{
					continue;
				}
				FABTSM11FinaleLaunchInput Neighbor = Preset.NominalInput;
				Neighbor.YawDegrees += YawOffset
					* Preset.ScanContract.FinalYawPrecisionDegrees;
				Neighbor.PitchDegrees += PitchOffset
					* Preset.ScanContract.FinalPitchPrecisionDegrees;
				Neighbor.Power += PowerOffset
					* Preset.ScanContract.FinalPowerPrecision;
				FABTSM11TrajectoryRequest NeighborQualifiedRequest;
				FABTSM11TrajectoryRequest NeighborPhysicalRequest;
				FABTSM11TrajectoryResult NeighborQualified;
				FABTSM11TrajectoryResult NeighborPhysical;
				if (!Preset.BuildRequest(
						Neighbor,
						0x7u,
						NeighborQualifiedRequest)
					|| !Preset.BuildPhysicalPlaybackRequest(
						Neighbor,
						0x7u,
						NeighborPhysicalRequest)
					|| !FABTSM11GravityAssistSolver::Solve(
						NeighborQualifiedRequest,
						NeighborQualified)
					|| !FABTSM11GravityAssistSolver::Solve(
						NeighborPhysicalRequest,
						NeighborPhysical))
				{
					continue;
				}
				const FABTSM11PrefixClassification NeighborClassification =
					FABTSM11PrefixClassifier::Classify(
						Preset,
						NeighborQualified,
						0x7u);
				if (!NeighborClassification.IsF(4)
					|| NeighborPhysical.DidHitTarget())
				{
					continue;
				}
				bExercisedNeighborTransfer = true;
				FABTSM11PlaybackPlan NeighborPlan;
				if (!NeighborPlan.Build(
						Preset,
						NeighborQualified,
						NeighborClassification,
						&NeighborPhysical,
						&Physical))
				{
					AddError(FString::Printf(
						TEXT("Neighbor F4 transfer failed Input=(%.9f,%.9f,%.9f) SourceTime=%.9f Failure=%s"),
						Neighbor.YawDegrees,
						Neighbor.PitchDegrees,
						Neighbor.Power,
						NeighborQualified.Points.Last().TimeSeconds,
						*NeighborPlan.Failure));
				}
				else
				{
					TestTrue(
						TEXT("A non-nominal neighboring F4 uses the visible transfer"),
						NeighborPlan.bUsesVisibleTerminalTransfer);
				}
			}
		}
	}
	TestTrue(
		TEXT("At least one neighboring non-physical F4 exercises transfer"),
		bExercisedNeighborTransfer);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11COrbitalDiagramTest,
	"ABTS.M11C.Unit.OrbitalDiagram",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11COrbitalDiagramTest::RunTest(const FString& Parameters)
{
	const FABTSM11FinaleLayoutPreset Preset =
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
	const FABTSM110FinaleLocalFrame Frame = MakeIdentityFrame();
	TArray<FABTSM11PlaybackPoint> Points;
	auto AddPoint = [&Points](const FVector3d& Position)
	{
		FABTSM11PlaybackPoint& Point =
			Points.AddDefaulted_GetRef();
		Point.TimeSeconds = Points.Num() - 1;
		Point.PositionCM = Position;
		Point.VelocityCMPerSec = FVector3d(100.0, 0.0, 0.0);
	};
	AddPoint(FVector3d(-20000.0, -10000.0, -20000.0));
	AddPoint(FVector3d(0.0, 10000.0, -20000.0));
	AddPoint(FVector3d(20000.0, -10000.0, -20000.0));

	FABTSM11OrbitalDiagramSnapshot Snapshot;
	TestTrue(TEXT("Fitted orbital projection builds"),
		FABTSM11OrbitalDiagramBuilder::Build(
			Preset, Frame, Points, 0x11c001ull, Snapshot, 64));
	TestTrue(TEXT("Slingshot/start remains on the left"),
		!Snapshot.Trajectory.IsEmpty()
			&& Snapshot.Trajectory[0].Position.X < 0.0);
	bool bAllInside = true;
	bool bHasHidden = false;
	bool bHasVisible = false;
	for (const FABTSM11DiagramPoint& Point : Snapshot.Trajectory)
	{
		bAllInside &= Point.Position.Length() <= 1.000001;
		bHasHidden |= Point.bHiddenByBody;
		bHasVisible |= !Point.bHiddenByBody;
	}
	TestTrue(TEXT("Complete trajectory fits the circular frame"),
		bAllInside);
	TestTrue(TEXT("Sphere-behind portions are represented"),
		bHasHidden);
	TestTrue(TEXT("Unoccluded portions remain solid"),
		bHasVisible);
	TestTrue(TEXT("Absolute primary latitude/longitude grid exists"),
		Snapshot.PrimaryGrid.Num() > 100);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11CPIERegressionContractsTest,
	"ABTS.M11C.Unit.PIERegressionContracts",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11CPIERegressionContractsTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;

	FABTSM11PrimaryReleaseGate ReleaseGate;
	ReleaseGate.Enter(true);
	TestTrue(
		TEXT("The Space-pouch press that enters aim launches on its release"),
		ReleaseGate.OnPrimaryReleased(true));
	ReleaseGate.Enter(true);
	ReleaseGate.Reset();
	TestFalse(
		TEXT("Focus loss cancels an armed launch gesture"),
		ReleaseGate.OnPrimaryReleased(true));
	ReleaseGate.Enter(false);
	ReleaseGate.OnPrimaryPressed(true);
	TestTrue(
		TEXT("A later pouch press/release remains valid"),
		ReleaseGate.OnPrimaryReleased(true));

	const FABTSM11FinaleLayoutPreset Preset =
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
	FABTSM11FinaleLaunchInput LowPull = Preset.NominalInput;
	LowPull.Power = Preset.LaunchModel.MinimumPower;
	FABTSM11FinaleLaunchInput HighPull = Preset.NominalInput;
	HighPull.Power = Preset.LaunchModel.MaximumPower;
	const FVector3d LowVisualPosition =
		ABTSM11ComputeAimPouchLocalPosition(
			Preset.LaunchModel,
			LowPull,
			60.0,
			180.0,
			70.0);
	const FVector3d HighVisualPosition =
		ABTSM11ComputeAimPouchLocalPosition(
			Preset.LaunchModel,
			HighPull,
			60.0,
			180.0,
			70.0);
	TestFalse(
		TEXT("Power visibly changes the pulled-pouch pose"),
		LowVisualPosition.Equals(HighVisualPosition, 1.0e-6));
	FABTSM11TrajectoryRequest LowRequest;
	FABTSM11TrajectoryRequest HighRequest;
	TestTrue(
		TEXT("Both visual-pose inputs still build authoritative requests"),
		Preset.BuildRequest(LowPull, 0x7u, LowRequest)
			&& Preset.BuildRequest(HighPull, 0x7u, HighRequest));
	TestTrue(
		TEXT("Visual pull never moves the solver launch origin"),
		LowRequest.InitialPositionCM.Equals(
			Preset.LaunchModel.PouchLocalPositionCM,
			1.0e-9)
		&& HighRequest.InitialPositionCM.Equals(
			Preset.LaunchModel.PouchLocalPositionCM,
			1.0e-9));

	FABTSM11FailurePresentationConfig FailureConfig;
	FailureConfig.ReadableHoldSeconds = 0.10;
	FailureConfig.FadeToBlackSeconds = 0.20;
	FailureConfig.BlackHoldSeconds = 0.10;
	FailureConfig.FadeFromBlackSeconds = 0.20;
	FABTSM11FailurePresentationTimeline FailureTimeline;
	TestTrue(
		TEXT("Failure presentation accepts a deterministic timing contract"),
		FailureTimeline.Begin(FailureConfig));
	bool bRestoreWorld = false;
	FailureTimeline.Advance(0.29, bRestoreWorld);
	TestFalse(
		TEXT("World is not restored before full black"),
		bRestoreWorld);
	FailureTimeline.Advance(0.02, bRestoreWorld);
	TestTrue(
		TEXT("World restoration is emitted at full black"),
		bRestoreWorld);
	TestTrue(
		TEXT("Failure reaches full black at the restoration boundary"),
		FMath::IsNearlyEqual(
			FailureTimeline.GetBlackoutAlpha(),
			1.0,
			1.0e-9));
	FailureTimeline.Advance(0.01, bRestoreWorld);
	TestFalse(
		TEXT("World restoration is not emitted twice"),
		bRestoreWorld);
	FailureTimeline.Advance(0.30, bRestoreWorld);
	TestTrue(
		TEXT("Failure fade completes and cannot remain stuck"),
		FailureTimeline.IsComplete());
	TestTrue(
		TEXT("Failure timeline can restart for another attempt"),
		FailureTimeline.Begin(FailureConfig));
	FailureTimeline.Advance(10.0, bRestoreWorld);
	TestTrue(
		TEXT("A long hitch still emits restoration"),
		bRestoreWorld);
	TestTrue(
		TEXT("A long hitch is clamped to a visible full-black frame"),
		!FailureTimeline.IsComplete()
			&& FMath::IsNearlyEqual(
				FailureTimeline.GetBlackoutAlpha(),
				1.0,
				1.0e-9));
	FailureTimeline.Advance(0.31, bRestoreWorld);
	TestTrue(
		TEXT("The clamped hitch resumes and completes normally"),
		FailureTimeline.IsComplete());

	const FABTSM11GravityBodySpec& Primary =
		Preset.CanonicalScenario.Bodies[0];
	const double BirdClearanceCM = 50.0;
	const double SafeRadius = FMath::Max(
		Primary.VisualRadiusCM,
		Primary.CollisionRadiusCM) + BirdClearanceCM;
	FABTSM11TrajectoryResult CollisionResult;
	CollisionResult.Termination =
		EABTSM11TrajectoryTermination::BodyCollision;
	FABTSM11TrajectoryEvent& Collision =
		CollisionResult.Events.AddDefaulted_GetRef();
	Collision.Type = EABTSM11TrajectoryEventType::BodyCollision;
	Collision.BodyId = Primary.BodyId;
	FABTSM11PlaybackPlan CollisionPlan;
	FABTSM11PlaybackPoint& Outside =
		CollisionPlan.Points.AddDefaulted_GetRef();
	Outside.TimeSeconds = 0.0;
	Outside.PositionCM = Primary.CenterCM
		+ FVector3d(SafeRadius + 100.0, 0.0, 0.0);
	FABTSM11PlaybackPoint& Inside =
		CollisionPlan.Points.AddDefaulted_GetRef();
	Inside.TimeSeconds = 1.0;
	Inside.PositionCM = Primary.CenterCM;
	CollisionPlan.DurationSeconds = 1.0;
	CollisionPlan.PlanHash = 0x11c0ffeeull;
	const TArray<FABTSM11PlaybackPoint> OriginalPoints =
		CollisionPlan.Points;
	const uint64 OriginalPlanHash = CollisionPlan.PlanHash;
	const double PresentationEnd =
		ABTSM11ResolveFailurePresentationEndTime(
			Preset,
			CollisionResult,
			CollisionPlan,
			BirdClearanceCM);
	TestTrue(
		TEXT("Body collision presentation stops before the analytic center"),
		PresentationEnd > 0.0 && PresentationEnd < 1.0);
	FVector3d SafePosition;
	FVector3d SafeVelocity;
	TestTrue(
		TEXT("The visual body-clearance stop remains sampleable"),
		CollisionPlan.Sample(
			PresentationEnd,
			SafePosition,
			SafeVelocity));
	TestTrue(
		TEXT("Visual stop preserves body radius plus bird clearance"),
		FMath::IsNearlyEqual(
			(SafePosition - Primary.CenterCM).Length(),
			SafeRadius,
			1.0e-4));
	TestEqual(
		TEXT("Presentation stop does not change playback identity"),
		CollisionPlan.PlanHash,
		OriginalPlanHash);
	TestTrue(
		TEXT("Presentation stop does not mutate authoritative points"),
		CollisionPlan.Points.Num() == OriginalPoints.Num()
			&& CollisionPlan.Points[0].PositionCM.Equals(
				OriginalPoints[0].PositionCM,
				1.0e-9)
			&& CollisionPlan.Points[1].PositionCM.Equals(
				OriginalPoints[1].PositionCM,
				1.0e-9));

	FVector2d ClipStart(-2.0, 0.0);
	FVector2d ClipEnd(2.0, 0.0);
	TestTrue(
		TEXT("A crossing diagram primitive is clipped to the circle"),
		ABTSM11ClipDiagramSegmentToUnitCircle(
			ClipStart,
			ClipEnd));
	TestTrue(
		TEXT("Clipped primitive endpoints stay inside the circular panel"),
		ClipStart.Equals(FVector2d(-1.0, 0.0), 1.0e-9)
			&& ClipEnd.Equals(FVector2d(1.0, 0.0), 1.0e-9));
	FVector2d OutsideStart(2.0, -0.5);
	FVector2d OutsideEnd(2.0, 0.5);
	TestFalse(
		TEXT("A fully exterior planet/glyph segment is rejected"),
		ABTSM11ClipDiagramSegmentToUnitCircle(
			OutsideStart,
			OutsideEnd));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11CCameraClassParityTest,
	"ABTS.M11C.Runtime.CameraClassParity",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11CCameraClassParityTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FScopedM11CAutomationWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("Transient camera parity World is created"), World);
	if (World == nullptr)
	{
		return false;
	}

	UClass* BlueprintCameraClass = LoadClass<AABTSM6SlingshotCamera>(
		nullptr,
		TEXT("/Game/Blueprints/BP_ABTSM6SlingshotCamera.BP_ABTSM6SlingshotCamera_C"));
	TestNotNull(
		TEXT("Configured M6 Blueprint camera class loads"),
		BlueprintCameraClass);
	TestTrue(
		TEXT("Regression fixture is a Blueprint subclass, not the native fallback"),
		BlueprintCameraClass != nullptr
			&& BlueprintCameraClass
				!= AABTSM6SlingshotCamera::StaticClass());
	if (BlueprintCameraClass == nullptr)
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM6SlingshotSystem* RuntimeSlingshotSystem =
		World->SpawnActor<AABTSM6SlingshotSystem>(
			AABTSM6SlingshotSystem::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	TestNotNull(
		TEXT("M6 runtime source system spawns"),
		RuntimeSlingshotSystem);
	if (RuntimeSlingshotSystem == nullptr)
	{
		return false;
	}
	SpawnParameters.Owner = RuntimeSlingshotSystem;
	AABTSM6SlingshotCamera* SourceCamera =
		World->SpawnActor<AABTSM6SlingshotCamera>(
			BlueprintCameraClass,
			FTransform::Identity,
			SpawnParameters);
	TestNotNull(
		TEXT("Configured M6 Blueprint camera source spawns"),
		SourceCamera);

	int32 MatchingCameraCount = 0;
	const TSubclassOf<AABTSM6SlingshotCamera> ResolvedClass =
		AABTSM11GameMode::ResolveRuntimeSlingshotCameraClass(
			*World,
			*RuntimeSlingshotSystem,
			&MatchingCameraCount);
	TestEqual(
		TEXT("Exactly one M6-owned camera is selected"),
		MatchingCameraCount,
		1);
	TestTrue(
		TEXT("M11 resolves the exact configured Blueprint camera class"),
		ResolvedClass.Get() == BlueprintCameraClass);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11CRuntimeContractRoutingTest,
	"ABTS.M11C.Runtime.ContractRoutingAndM9Isolation",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11CRuntimeContractRoutingTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const AABTSM11GameMode* GameModeCDO =
		GetDefault<AABTSM11GameMode>();
	TestNotNull(TEXT("M11 GameMode CDO exists"), GameModeCDO);
	if (GameModeCDO == nullptr)
	{
		return false;
	}
	TestTrue(
		TEXT("M11 GameMode installs the Space-aware controller"),
		GameModeCDO->PlayerControllerClass
			== AABTSM11PlayerController::StaticClass());
	TestTrue(
		TEXT("M11 GameMode installs the finale HUD"),
		GameModeCDO->HUDClass
			== AABTSM11FinaleHUD::StaticClass());

	FScopedM11CAutomationWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("Transient M11-C World is created"), World);
	if (World == nullptr)
	{
		return false;
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM11FinaleSystem* FinaleSystem =
		World->SpawnActor<AABTSM11FinaleSystem>(
			AABTSM11FinaleSystem::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	TestNotNull(TEXT("M11-B authority spawns"), FinaleSystem);
	if (FinaleSystem == nullptr)
	{
		return false;
	}
	const FABTSM110FinaleLocalFrame Frame = MakeIdentityFrame();
	TestTrue(
		TEXT("Certified stable contract initializes"),
		FinaleSystem->InitializeFromWorldContract(
			MakeM11CWorldContract(Frame)));
	FString ContractFailure;
	TestTrue(
		TEXT("M11-C accepts the complete frozen identity"),
		AABTSM11FinaleInteractionSystem::ValidateInteractionContract(
			*FinaleSystem,
			&ContractFailure));

	FABTSM11TrajectoryRequest BeforeSatellite;
	TestTrue(
		TEXT("Request builds before an M9 Actor exists"),
		FinaleSystem->BuildRequest(
			FinaleSystem->GetLayoutPreset().NominalInput,
			0x7u,
			BeforeSatellite));
	AABTSM9Satellite* Satellite =
		World->SpawnActor<AABTSM9Satellite>(
			AABTSM9Satellite::StaticClass(),
			FTransform(
				FQuat::Identity,
				FVector(987654.0, -456789.0, 123456.0)),
			SpawnParameters);
	TestNotNull(TEXT("Unrelated M9 satellite spawns"), Satellite);
	if (Satellite != nullptr)
	{
		Satellite->bGravityEnabled = true;
		Satellite->SurfaceGravityAccelerationCMPerSec2 = 2999.0f;
		Satellite->SetActorHiddenInGame(false);
		TestFalse(
			TEXT("M9 explicitly rejects M11 gravity authority"),
			Satellite->IsM11FinaleGravitySource());
	}
	FABTSM11TrajectoryRequest AfterSatellite;
	TestTrue(
		TEXT("Request builds after M9 mutation"),
		FinaleSystem->BuildRequest(
			FinaleSystem->GetLayoutPreset().NominalInput,
			0x7u,
			AfterSatellite));
	TestEqual(
		TEXT("M9 cannot alter the frozen Scenario identity"),
		AfterSatellite.Scenario.ScenarioHash,
		BeforeSatellite.Scenario.ScenarioHash);
	TestTrue(
		TEXT("M9 cannot alter initial position"),
		AfterSatellite.InitialPositionCM
			.Equals(BeforeSatellite.InitialPositionCM, 0.0));
	TestTrue(
		TEXT("M9 cannot alter initial velocity"),
		AfterSatellite.InitialVelocityCMPerSec
			.Equals(BeforeSatellite.InitialVelocityCMPerSec, 0.0));
	FABTSM11TrajectoryResult BeforeResult;
	FABTSM11TrajectoryResult AfterResult;
	TestTrue(
		TEXT("Pre-M9 request solves"),
		FABTSM11GravityAssistSolver::Solve(
			BeforeSatellite,
			BeforeResult));
	TestTrue(
		TEXT("Post-M9 request solves"),
		FABTSM11GravityAssistSolver::Solve(
			AfterSatellite,
			AfterResult));
	TestEqual(
		TEXT("M9 existence, position and gravity leave trajectory Hash unchanged"),
		AfterResult.ValidationHash,
		BeforeResult.ValidationHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11CTerminalTransferDomainTest,
	"ABTS.M11C.Certification.TerminalTransferDomain",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11CTerminalTransferDomainTest::RunTest(
	const FString& Parameters)
{
	const FABTSM11FinaleLayoutPreset Preset =
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
	FABTSM11TrajectoryResult NominalQualified;
	FABTSM11TrajectoryResult NominalPhysical;
	FABTSM11PrefixClassification NominalClassification;
	if (!TestTrue(TEXT("Frozen nominal paths solve"),
		SolveNominal(
			Preset,
			NominalQualified,
			NominalPhysical,
			NominalClassification)))
	{
		return false;
	}

	FABTSM11InputGrid Grid;
	Grid.Minimum = FABTSM11FinaleLaunchInput{-2.25, 27.0, 0.925};
	Grid.Maximum = FABTSM11FinaleLaunchInput{3.0, 34.0, 1.0};
	Grid.YawStepDegrees =
		Preset.ScanContract.FinalYawPrecisionDegrees;
	Grid.PitchStepDegrees =
		Preset.ScanContract.FinalPitchPrecisionDegrees;
	Grid.PowerStep = Preset.ScanContract.FinalPowerPrecision;
	FABTSM11LayoutCertificationReport Report;
	FString ScanFailure;
	FABTSM11FinaleLayoutCertification::ScanGrid(
		Preset,
		Grid,
		0x7u,
		Report,
		&ScanFailure);
	TestEqual(TEXT("Refined closure sample count"),
		Report.TotalSampleCount, 21025);

	TArray<int32> F4Indices;
	for (int32 Index = 0; Index < Report.Samples.Num(); ++Index)
	{
		if (Report.Samples[Index].HighestPrefixLevel >= 4)
		{
			F4Indices.Add(Index);
		}
	}
	if (!TestEqual(TEXT("Frozen refined closure F4 count"),
		F4Indices.Num(), 558))
	{
		AddError(FString::Printf(
			TEXT("Scan failure/report context: %s"),
			*ScanFailure));
		return false;
	}

	std::atomic<int32> BuildFailures = 0;
	std::atomic<int32> DirectPhysicalCount = 0;
	std::atomic<int32> TransferCount = 0;
	ParallelFor(
		F4Indices.Num(),
		[
			&Preset,
			&Grid,
			&Report,
			&F4Indices,
			&NominalPhysical,
			&BuildFailures,
			&DirectPhysicalCount,
			&TransferCount](const int32 WorkIndex)
		{
			const int32 FlatIndex = F4Indices[WorkIndex];
			const int32 YawIndex =
				FlatIndex % Report.YawCount;
			const int32 PitchPowerIndex =
				FlatIndex / Report.YawCount;
			const int32 PitchIndex =
				PitchPowerIndex % Report.PitchCount;
			const int32 PowerIndex =
				PitchPowerIndex / Report.PitchCount;
			const FABTSM11FinaleLaunchInput Input =
				Grid.GetInput(YawIndex, PitchIndex, PowerIndex);

			FABTSM11TrajectoryRequest QualifiedRequest;
			FABTSM11TrajectoryRequest PhysicalRequest;
			FABTSM11TrajectoryResult QualifiedResult;
			FABTSM11TrajectoryResult PhysicalResult;
			if (!Preset.BuildRequest(
					Input, 0x7u, QualifiedRequest)
				|| !FABTSM11GravityAssistSolver::Solve(
					QualifiedRequest, QualifiedResult)
				|| !Preset.BuildPhysicalPlaybackRequest(
					Input, 0x7u, PhysicalRequest)
				|| !FABTSM11GravityAssistSolver::Solve(
					PhysicalRequest, PhysicalResult))
			{
				++BuildFailures;
				return;
			}
			const FABTSM11PrefixClassification Classification =
				FABTSM11PrefixClassifier::Classify(
					Preset, QualifiedResult, 0x7u);
			FABTSM11PlaybackPlan Plan;
			if (!Classification.IsF(4)
				|| !Plan.Build(
					Preset,
					QualifiedResult,
					Classification,
					&PhysicalResult,
					&NominalPhysical)
				|| !Plan.bPhysicalTargetHit
				|| Plan.PlanHash == 0)
			{
				++BuildFailures;
				return;
			}
			if (Plan.bUsesVisibleTerminalTransfer)
			{
				++TransferCount;
			}
			else
			{
				++DirectPhysicalCount;
			}
		});

	TestEqual(
		TEXT("Every refined F4 sample has a safe physical playback plan"),
		BuildFailures.load(),
		0);
	TestEqual(
		TEXT("Direct plus visible-transfer plans cover the full F4 closure"),
		DirectPhysicalCount.load() + TransferCount.load(),
		F4Indices.Num());
	AddInfo(FString::Printf(
		TEXT("Terminal transfer domain: F4=%d Direct=%d Transfer=%d"),
		F4Indices.Num(),
		DirectPhysicalCount.load(),
		TransferCount.load()));
	return true;
}

#endif
