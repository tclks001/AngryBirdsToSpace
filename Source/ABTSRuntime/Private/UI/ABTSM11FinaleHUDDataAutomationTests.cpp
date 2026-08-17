// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "UI/ABTSM11FinaleHUDData.h"
#include "World/ABTSM11CandidateExperienceCatalog.h"
#include "World/ABTSM11FinaleLayoutCertification.h"
#include "World/ABTSM11GravityAssistSolver.h"

namespace ABTSM11FinaleHudDataTests
{
	struct FFixture
	{
		FABTSM11FinaleLayoutPreset Preset =
			FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
		FABTSM11TrajectoryResult Result;
		FABTSM11OrbitalSceneSnapshot Scene;
		bool bValid = false;

		FFixture()
		{
			FABTSM11TrajectoryRequest Request;
			bValid = Preset.BuildRequest(Preset.NominalInput, 0x7u, Request)
				&& FABTSM11GravityAssistSolver::Solve(Request, Result)
				&& FABTSM11OrbitalSceneBuilder::Build(
					Preset,
					Result,
					Scene,
					96);
		}
	};

	const FFixture& GetFixture()
	{
		static const FFixture Fixture;
		return Fixture;
	}

	FABTSM11OrbitalSceneSnapshot ShiftTrajectory(
		const FABTSM11OrbitalSceneSnapshot& Source,
		const FVector3d& Offset,
		const uint64 NewHash)
	{
		FABTSM11OrbitalSceneSnapshot Shifted = Source;
		for (FABTSM11OrbitalScenePoint& Point : Shifted.Trajectory)
		{
			Point.PositionCM += Offset;
		}
		Shifted.SourceTrajectoryHash = NewHash;
		return Shifted;
	}

	FABTSM11TrajectoryHit MakeHit(
		const EABTSM11TrajectorySemanticLeg Leg,
		const double Phase)
	{
		FABTSM11TrajectoryHit Hit;
		Hit.Leg = Leg;
		Hit.PhaseWithinLeg = Phase;
		Hit.bValid = true;
		return Hit;
	}

	bool NearlyEqual(const FVector2d& A, const FVector2d& B, const double Tolerance)
	{
		return (A - B).Length() <= Tolerance;
	}

	double PointSegmentDistanceSquared(
		const FVector2d& Point,
		const FVector2d& Start,
		const FVector2d& End)
	{
		const FVector2d Segment = End - Start;
		const double SegmentLengthSquared = Segment.SquaredLength();
		const double Alpha = SegmentLengthSquared > UE_DOUBLE_SMALL_NUMBER
			? FMath::Clamp(
				(Point - Start).Dot(Segment) / SegmentLengthSquared,
				0.0,
				1.0)
			: 0.0;
		return (Point - (Start + Segment * Alpha)).SquaredLength();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11HudControlKnobsTest,
	"ABTS.M11C.HUD.Unit.ControlKnobs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11HudControlKnobsTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM11FinaleHudDataTests;
	const FFixture& Fixture = GetFixture();
	TestTrue(TEXT("Certified fixture is valid"), Fixture.bValid);
	if (!Fixture.bValid)
	{
		return false;
	}

	FABTSM11FinaleControlPanelState State;
	TestTrue(
		TEXT("Control panel initializes"),
		State.Initialize(Fixture.Preset.LaunchModel, Fixture.Preset.NominalInput));
	const double InitialYaw = State.GetInput().YawDegrees;
	const double YawRange = Fixture.Preset.LaunchModel.MaximumYawDegrees
		- Fixture.Preset.LaunchModel.MinimumYawDegrees;
	TestTrue(TEXT("Coarse knob accepts continuous drag"),
		State.ApplyDragPixels(EABTSM11FinaleControlAxis::Yaw, 36.0));
	TestTrue(TEXT("Coarse drag is ten percent of the full range"),
		FMath::IsNearlyEqual(
			State.GetInput().YawDegrees,
			InitialYaw + YawRange * 0.1,
			1.0e-10));
	State.ResetAxis(EABTSM11FinaleControlAxis::Yaw);
	State.SetSpeedGear(EABTSM11ControlSpeedGear::Fine);
	State.ApplyDragPixels(EABTSM11FinaleControlAxis::Yaw, 36.0);
	TestTrue(TEXT("Fine gear is one tenth coarse"),
		FMath::IsNearlyEqual(
			State.GetInput().YawDegrees,
			InitialYaw + YawRange * 0.01,
			1.0e-10));
	State.ResetAxis(EABTSM11FinaleControlAxis::Yaw);
	State.SetSpeedGear(EABTSM11ControlSpeedGear::UltraFine);
	State.ApplyDragPixels(EABTSM11FinaleControlAxis::Yaw, 36.0);
	TestTrue(TEXT("Ultra-fine gear is one hundredth coarse"),
		FMath::IsNearlyEqual(
			State.GetInput().YawDegrees,
			InitialYaw + YawRange * 0.001,
			1.0e-10));
	TestTrue(TEXT("Wheel adjustment remains continuous"),
		State.ApplyWheelSteps(EABTSM11FinaleControlAxis::Pitch, 1.5));
	State.SetSpeedGear(EABTSM11ControlSpeedGear::Coarse);
	State.ApplyDragPixels(EABTSM11FinaleControlAxis::Power, 100000.0);
	TestTrue(TEXT("Knob values clamp to launch-model bounds"),
		FMath::IsNearlyEqual(
			State.GetInput().Power,
			Fixture.Preset.LaunchModel.MaximumPower));
	State.ResetAll();
	TestTrue(TEXT("Reset restores all three initial values"),
		FMath::IsNearlyEqual(State.GetInput().YawDegrees, Fixture.Preset.NominalInput.YawDegrees)
		&& FMath::IsNearlyEqual(State.GetInput().PitchDegrees, Fixture.Preset.NominalInput.PitchDegrees)
		&& FMath::IsNearlyEqual(State.GetInput().Power, Fixture.Preset.NominalInput.Power));
	return true;
}

#if WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11HudF4GuidanceTest,
	"ABTS.M11C.HUD.Unit.F4Guidance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11HudF4GuidanceTest::RunTest(const FString& Parameters)
{
	FABTSM11F4GuidanceTarget DirectionFixture;
	DirectionFixture.bValid = true;
	DirectionFixture.Input = FABTSM11FinaleLaunchInput{-2.5, 25.0, 1.0};
	DirectionFixture.YawToleranceDegrees = 0.1;
	DirectionFixture.PitchToleranceDegrees = 0.1;
	DirectionFixture.PowerTolerance = 0.001;
	DirectionFixture.SampleCount = 1;
	DirectionFixture.F4SampleCount = 1;
	const FABTSM11FinaleLaunchInput Current{0.0, 20.0, 0.9};
	TestTrue(
		TEXT("Yaw guidance points toward a lower value"),
		DirectionFixture.GetDirection(
			Current,
			EABTSM11FinaleControlAxis::Yaw)
			== EABTSM11F4GuidanceDirection::Decrease);
	TestTrue(
		TEXT("Pitch guidance points toward a higher value"),
		DirectionFixture.GetDirection(
			Current,
			EABTSM11FinaleControlAxis::Pitch)
			== EABTSM11F4GuidanceDirection::Increase);
	TestTrue(
		TEXT("Power guidance points toward a higher value"),
		DirectionFixture.GetDirection(
			Current,
			EABTSM11FinaleControlAxis::Power)
			== EABTSM11F4GuidanceDirection::Increase);

	FABTSM11F4GuidanceSearchConfig Config;
	Config.MinimumYawStepDegrees = 0.5;
	Config.MinimumPitchStepDegrees = 0.75;
	Config.MinimumPowerStep = 0.01;
	Config.MaximumSampleCount = 1024;
	for (int32 Rank = FABTSM11CandidateExperienceCatalog::FirstCandidateRank;
		Rank <= FABTSM11CandidateExperienceCatalog::LastCandidateRank;
		++Rank)
	{
		FABTSM11FinaleLayoutPreset Preset;
		FABTSM11CandidateExperienceIdentity Identity;
		FString Failure;
		const FString RankContext = FString::Printf(TEXT("Rank%d"), Rank);
		if (!TestTrue(
			*FString::Printf(TEXT("%s candidate builds"), *RankContext),
			FABTSM11CandidateExperienceCatalog::BuildCandidate(
				Rank,
				Preset,
				Identity,
				&Failure)))
		{
			AddError(FString::Printf(
				TEXT("%s build failure: %s"),
				*RankContext,
				*Failure));
			continue;
		}

		FABTSM11F4GuidanceTarget Target;
		Failure.Reset();
		if (!TestTrue(
			*FString::Printf(TEXT("%s guidance resolves"), *RankContext),
			FABTSM11F4GuidanceBuilder::Build(
				Preset,
				Target,
				&Failure,
				Config)))
		{
			AddError(FString::Printf(
				TEXT("%s guidance failure: %s"),
				*RankContext,
				*Failure));
			continue;
		}

		FABTSM11TrajectoryRequest Request;
		FABTSM11TrajectoryResult Result;
		const bool bTargetF4 = Preset.BuildRequest(
			Target.Input,
			0x7u,
			Request,
			&Failure)
			&& FABTSM11GravityAssistSolver::Solve(
				Request,
				Result,
				&Failure)
			&& FABTSM11PrefixClassifier::Classify(
				Preset,
				Result,
				0x7u).IsF(4);
		TestTrue(
			*FString::Printf(
				TEXT("%s guidance target is verified F4"),
				*RankContext),
			bTargetF4);
		AddInfo(FString::Printf(
			TEXT("%s F4Guide=(%.6f,%.6f,%.6f) Samples=%d F4=%d Truncated=%d"),
			*RankContext,
			Target.Input.YawDegrees,
			Target.Input.PitchDegrees,
			Target.Input.Power,
			Target.SampleCount,
			Target.F4SampleCount,
			Target.bSearchTruncated ? 1 : 0));

		if (Rank == 11 || Rank == 12)
		{
			FABTSM11F4GuidanceTarget RuntimeTarget;
			Failure.Reset();
			const bool bRuntimeTargetBuilt =
				FABTSM11F4GuidanceBuilder::Build(
					Preset,
					RuntimeTarget,
					&Failure);
			TestTrue(
				*FString::Printf(
					TEXT("%s runtime-resolution guidance resolves"),
					*RankContext),
				bRuntimeTargetBuilt);
			if (bRuntimeTargetBuilt)
			{
				AddInfo(FString::Printf(
					TEXT("%s RuntimeF4Guide=(%.6f,%.6f,%.6f) Samples=%d F4=%d Truncated=%d"),
					*RankContext,
					RuntimeTarget.Input.YawDegrees,
					RuntimeTarget.Input.PitchDegrees,
					RuntimeTarget.Input.Power,
					RuntimeTarget.SampleCount,
					RuntimeTarget.F4SampleCount,
					RuntimeTarget.bSearchTruncated ? 1 : 0));
			}
			else
			{
				AddError(FString::Printf(
					TEXT("%s runtime guidance failure: %s"),
					*RankContext,
					*Failure));
			}
		}
	}
	return true;
}

#endif // WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11HudOverviewViewInvarianceTest,
	"ABTS.M11C.HUD.Unit.OverviewViewInvariance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11HudOverviewViewInvarianceTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM11FinaleHudDataTests;
	const FFixture& Fixture = GetFixture();
	TestTrue(TEXT("Certified fixture is valid"), Fixture.bValid);
	if (!Fixture.bValid)
	{
		return false;
	}
	FABTSM11OverviewViewState View;
	TestTrue(TEXT("Frozen overview view initializes"),
		View.Initialize(
			FVector3d::ZeroVector,
			FVector3d::ForwardVector,
			FVector3d::RightVector,
			2000000.0));
	FABTSM11OrbitalSceneSnapshot BoundsScene;
	BoundsScene.bValid = true;
	BoundsScene.Trajectory.SetNum(2);
	BoundsScene.Trajectory[0].PositionCM = FVector3d(-10.0, -20.0, -30.0);
	BoundsScene.Trajectory[1].PositionCM = FVector3d(30.0, 40.0, 50.0);
	BoundsScene.Bodies[1].CenterCM = FVector3d(100.0, 0.0, 0.0);
	BoundsScene.Bodies[1].VisualRadiusCM = 10.0;
	BoundsScene.TargetCenterCM = FVector3d(0.0, 200.0, 0.0);
	BoundsScene.TargetRadiusCM = 20.0;
	FABTSM11OverviewViewState BoundsView;
	TestTrue(TEXT("Overview initializes from the route bounding sphere"),
		BoundsView.InitializeFromScene(
			BoundsScene,
			FVector3d::ForwardVector,
			FVector3d::RightVector));
	TestTrue(TEXT("Initial pivot is the three-dimensional bounds center"),
		BoundsView.ProjectionCenterCM.Equals(
			FVector3d(45.0, 100.0, 10.0),
			1.0e-9));
	const FVector3d PivotBeforePan = BoundsView.ProjectionCenterCM;
	const double PanWorldScale = BoundsView.ProjectionScaleCM / BoundsView.Zoom;
	TestTrue(TEXT("Move-mode normalized pan changes only the pivot"),
		BoundsView.ApplyPanNormalized(FVector2d(0.10, 0.20)));
	TestTrue(TEXT("Pan follows screen-space drag directions"),
		BoundsView.ProjectionCenterCM.Equals(
			PivotBeforePan
				- FVector3d::ForwardVector * PanWorldScale * 0.10
				+ FVector3d::RightVector * PanWorldScale * 0.20,
			1.0e-8));
	FABTSM11OverviewProjection A;
	FABTSM11OverviewProjection B;
	const FABTSM11OrbitalSceneSnapshot Shifted = ShiftTrajectory(
		Fixture.Scene,
		FVector3d(4000.0, -2500.0, 900.0),
		Fixture.Scene.SourceTrajectoryHash + 1);
	TestTrue(TEXT("Reference projection builds"),
		FABTSM11OverviewProjector::Build(Fixture.Scene, View, A));
	TestTrue(TEXT("Changed-aim projection builds in the same view"),
		FABTSM11OverviewProjector::Build(Shifted, View, B));
	for (int32 BodyIndex = 0; BodyIndex < A.Bodies.Num(); ++BodyIndex)
	{
		TestTrue(
			*FString::Printf(TEXT("Body %d remains invariant under aim changes"), BodyIndex),
			NearlyEqual(A.Bodies[BodyIndex].Center, B.Bodies[BodyIndex].Center, 1.0e-12));
	}
	TestFalse(TEXT("Only the trajectory moves under aim changes"),
		NearlyEqual(A.Trajectory[0].Position, B.Trajectory[0].Position, 1.0e-9));
	const FVector2d BodyBeforeRotate = A.Bodies[1].Center;
	const FVector3d PivotBeforeRotate = View.ProjectionCenterCM;
	const FVector3d InitialWorldUp = View.AxisY;
	TestTrue(TEXT("Vertical drag orbits around current screen Right"),
		View.ApplyOrbitRotation(0.0, 23.0));
	const FVector3d TiltedLocalUp = View.AxisY;
	TestFalse(TEXT("Vertical orbit tilts local Up away from initial world Up"),
		TiltedLocalUp.Equals(InitialWorldUp, 1.0e-8));
	TestTrue(TEXT("Horizontal drag orbits around the tilted local Up"),
		View.ApplyOrbitRotation(31.0, 0.0));
	TestTrue(TEXT("Local Up is preserved by its own horizontal orbit"),
		View.AxisY.Equals(TiltedLocalUp, 1.0e-8));
	TestTrue(TEXT("Composed local drags naturally accumulate world-relative roll"),
		FMath::Abs(View.AxisX.Dot(InitialWorldUp)) > 1.0e-4);
	TestTrue(TEXT("Orbit rotation preserves the explicit pivot"),
		View.ProjectionCenterCM.Equals(PivotBeforeRotate, 1.0e-12));
	TestTrue(TEXT("Free-orbit basis remains right handed and orthonormal"),
		View.AxisX.Cross(View.AxisY).Equals(View.ViewForward, 1.0e-8)
		&& FMath::Abs(View.AxisX.Dot(View.AxisY)) < 1.0e-8
		&& FMath::Abs(View.AxisX.Dot(View.ViewForward)) < 1.0e-8
		&& FMath::Abs(View.AxisY.Dot(View.ViewForward)) < 1.0e-8);
	FABTSM11OverviewProjection Rotated;
	TestTrue(TEXT("Rotated projection builds"),
		FABTSM11OverviewProjector::Build(Fixture.Scene, View, Rotated));
	TestFalse(TEXT("Celestial bodies move only after overview rotation"),
		NearlyEqual(BodyBeforeRotate, Rotated.Bodies[1].Center, 1.0e-8));
	const double ZoomBefore = View.Zoom;
	TestTrue(TEXT("Overview zoom is explicit and bounded"), View.ApplyZoom(1.5));
	TestTrue(TEXT("Zoom multiplier is applied"),
		FMath::IsNearlyEqual(View.Zoom, ZoomBefore * 1.5));
	TestTrue(TEXT("Wheel up resolves to overview zoom in"),
		ABTSM11ResolveOverviewWheelZoomMultiplier(1.12, 1.0) > 1.0);
	TestTrue(TEXT("Wheel down resolves to overview zoom out"),
		ABTSM11ResolveOverviewWheelZoomMultiplier(1.12, -1.0) < 1.0);
	TestTrue(TEXT("Invalid wheel zoom configuration is neutral"),
		FMath::IsNearlyEqual(
			ABTSM11ResolveOverviewWheelZoomMultiplier(1.0, 1.0),
			1.0));
	return true;
}

// Candidate catalog construction is intentionally editor-only. Keep the
// Rank11 terminal-transfer regression intact for Editor/NullRHI, but never
// compile it into the Win64 game target.
#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11HudTerminalTransferOverviewTest,
	"ABTS.M11C.HUD.Unit.TerminalTransferOverview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11HudTerminalTransferOverviewTest::RunTest(
	const FString& Parameters)
{
	FABTSM11FinaleLayoutPreset Preset;
	FABTSM11CandidateExperienceIdentity Identity;
	FString Failure;
	if (!TestTrue(
		TEXT("Rank11 candidate builds for terminal overview"),
		FABTSM11CandidateExperienceCatalog::BuildCandidate(
			11,
			Preset,
			Identity,
			&Failure)))
	{
		AddError(Failure);
		return false;
	}

	FABTSM11TrajectoryRequest Request;
	FABTSM11TrajectoryResult Result;
	if (!TestTrue(
		TEXT("Rank11 nominal trajectory solves"),
		Preset.BuildRequest(
			Preset.NominalInput,
			0x7u,
			Request,
			&Failure)
			&& FABTSM11GravityAssistSolver::Solve(
				Request,
				Result,
				&Failure)))
	{
		AddError(Failure);
		return false;
	}
	const FABTSM11PrefixClassification Classification =
		FABTSM11PrefixClassifier::Classify(Preset, Result, 0x7u);
	TestTrue(TEXT("Rank11 nominal trajectory is F4"), Classification.IsF(4));

	FABTSM11PlaybackPlan Plan;
	if (!TestTrue(
		TEXT("Rank11 presentation plan reaches physical contact"),
		Plan.BuildCandidatePresentationContact(
			Preset,
			Result,
			Classification)))
	{
		AddError(Plan.Failure);
		return false;
	}

	FABTSM11OrbitalSceneSnapshot Scene;
	if (!TestTrue(
		TEXT("Authoritative overview scene builds"),
		FABTSM11OrbitalSceneBuilder::Build(
			Preset,
			Result,
			Scene,
			96)))
	{
		return false;
	}
	const TArray<FABTSM11OrbitalScenePoint> OriginalTrajectory =
		Scene.Trajectory;
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
		TEXT("Runtime orbital diagram basis builds for terminal diagnostic"),
		FABTSM11OrbitalDiagramBuilder::Build(
			Preset,
			IdentityFrame,
			Plan.Points,
			Plan.ReleasedTrajectoryHash,
			Diagram));
	FABTSM11OverviewViewState View;
	TestTrue(
		TEXT("Base overview view initializes before terminal simplification"),
		View.InitializeFromScene(
			Scene,
			Diagram.PlaneAxisX,
			Diagram.PlaneAxisY));
	constexpr double MaximumCurveErrorPixels = 0.35;
	constexpr double DiagnosticDiagramRadiusPixels = 512.0;
	constexpr double MaximumOverviewZoom = 4.0;
	const double MaximumChordErrorCM = MaximumCurveErrorPixels
		* View.ProjectionScaleCM
		/ (DiagnosticDiagramRadiusPixels * MaximumOverviewZoom);
	TestTrue(
		TEXT("Visible terminal transfer appends to overview"),
		FABTSM11OrbitalSceneBuilder::AppendPlaybackExtension(
			Plan,
			Scene,
			720,
			MaximumChordErrorCM));
	TestTrue(
		TEXT("Overview replaces only the released suffix after guidance handoff"),
		Scene.Trajectory.Num() >= 3);
	int32 OriginalPrefixCount = 0;
	while (OriginalTrajectory.IsValidIndex(OriginalPrefixCount)
		&& OriginalTrajectory[OriginalPrefixCount].TimeSeconds
			< Plan.TransferStartTimeSeconds - 1.0e-8)
	{
		++OriginalPrefixCount;
	}
	bool bOverviewPrefixPreserved =
		Scene.Trajectory.Num() > OriginalPrefixCount;
	for (int32 Index = 0;
		Index < OriginalPrefixCount && bOverviewPrefixPreserved;
		++Index)
	{
		bOverviewPrefixPreserved =
			Scene.Trajectory[Index].TimeSeconds
				== OriginalTrajectory[Index].TimeSeconds
			&& Scene.Trajectory[Index].PositionCM.Equals(
				OriginalTrajectory[Index].PositionCM,
				0.0)
			&& Scene.Trajectory[Index].VelocityCMPerSec.Equals(
				OriginalTrajectory[Index].VelocityCMPerSec,
				0.0);
	}
	TestTrue(
		TEXT("Authoritative overview prefix before handoff remains unchanged"),
		bOverviewPrefixPreserved);
	TestTrue(
		TEXT("Overview white anchor equals the circular-guidance handoff"),
		FMath::IsNearlyEqual(
			Scene.Trajectory[OriginalPrefixCount].TimeSeconds,
			Plan.TransferStartTimeSeconds,
			1.0e-8)
			&& Scene.Trajectory[OriginalPrefixCount].SegmentKind
				== EABTSM11PlaybackSegmentKind::PlayerAuthoritative);
	TestEqual(
		TEXT("Overview records the exact playback-plan identity"),
		Scene.SourcePlaybackPlanHash,
		Plan.PlanHash);
	TestEqual(
		TEXT("Candidate extension remains explicitly presentation-only"),
		Scene.Trajectory.Last().SegmentKind,
		EABTSM11PlaybackSegmentKind::VisibleTerminalTransfer);
	TestTrue(
		TEXT("Overview extension ends on the physical UFO contact sphere"),
		FMath::IsNearlyEqual(
			(Scene.Trajectory.Last().PositionCM - Scene.TargetCenterCM).Length(),
			Scene.TargetRadiusCM,
			1.0e-3));

	FABTSM11OverviewProjection Projection;
	TestTrue(
		TEXT("Extended overview projects"),
		FABTSM11OverviewProjector::Build(Scene, View, Projection));
	TestTrue(
		TEXT("Projected overview retains terminal transfer typing"),
		Projection.HitProxies.ContainsByPredicate(
			[](const FABTSM11OverviewHitProxy& Proxy)
			{
				return Proxy.SegmentKind
					== EABTSM11PlaybackSegmentKind::VisibleTerminalTransfer;
			}));
	TestEqual(
		TEXT("Every adjacent overview point remains connected after handoff clipping"),
		Projection.HitProxies.Num(),
		Scene.Trajectory.Num() - 1);
	const bool bHasWhiteSegmentIntoHandoff =
		Projection.HitProxies.ContainsByPredicate(
			[&Plan](const FABTSM11OverviewHitProxy& Proxy)
			{
				return Proxy.SegmentKind
						== EABTSM11PlaybackSegmentKind::PlayerAuthoritative
					&& FMath::IsNearlyEqual(
						Proxy.EndTimeSeconds,
						Plan.TransferStartTimeSeconds,
						1.0e-8);
			});
	const bool bHasAmberSegmentOutOfHandoff =
		Projection.HitProxies.ContainsByPredicate(
			[&Plan](const FABTSM11OverviewHitProxy& Proxy)
			{
				return Proxy.SegmentKind
						== EABTSM11PlaybackSegmentKind::VisibleTerminalTransfer
					&& FMath::IsNearlyEqual(
						Proxy.StartTimeSeconds,
						Plan.TransferStartTimeSeconds,
						1.0e-8);
			});
	TestTrue(
		TEXT("White overview stroke reaches the exact playback handoff"),
		bHasWhiteSegmentIntoHandoff);
	TestTrue(
		TEXT("Amber overview stroke leaves from the same playback handoff"),
		bHasAmberSegmentOutOfHandoff);
	TestFalse(
		TEXT("Visible terminal transfer uses a continuous solid stroke"),
		ABTSM11ShouldDashOverviewTrajectorySegment(
			EABTSM11PlaybackSegmentKind::VisibleTerminalTransfer,
			false));
	TestFalse(
		TEXT("Certified nominal tail uses the same continuous solid stroke"),
		ABTSM11ShouldDashOverviewTrajectorySegment(
			EABTSM11PlaybackSegmentKind::CertifiedNominalTail,
			false));
	TestFalse(
		TEXT("Terminal extension remains solid even if depth classification changes"),
		ABTSM11ShouldDashOverviewTrajectorySegment(
			EABTSM11PlaybackSegmentKind::VisibleTerminalTransfer,
			true));
	TestTrue(
		TEXT("Hidden authoritative trajectory retains its depth dash cue"),
		ABTSM11ShouldDashOverviewTrajectorySegment(
			EABTSM11PlaybackSegmentKind::PlayerAuthoritative,
			true));

	int32 PlaybackAnchorIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Plan.Points.Num(); ++Index)
	{
		if (Plan.Points[Index].SegmentKind
				== EABTSM11PlaybackSegmentKind::PlayerAuthoritative
			&& FMath::IsNearlyEqual(
				Plan.Points[Index].TimeSeconds,
				Plan.TransferStartTimeSeconds,
				1.0e-8)
			&& Plan.Points[Index].PositionCM.Equals(
				Scene.Trajectory[OriginalPrefixCount].PositionCM,
				1.0e-6))
		{
			PlaybackAnchorIndex = Index;
		}
	}
	TestTrue(
		TEXT("Diagnostic finds the exact playback anchor"),
		PlaybackAnchorIndex != INDEX_NONE);
	TArray<FVector2d> RawProjectedPoints;
	if (PlaybackAnchorIndex != INDEX_NONE)
	{
		RawProjectedPoints.Add(View.Project(
			Plan.Points[PlaybackAnchorIndex].PositionCM));
		constexpr int32 PresentationSubstepsPerPlaybackInterval = 8;
		for (int32 SourceIndex = PlaybackAnchorIndex + 1;
			SourceIndex < Plan.Points.Num();
			++SourceIndex)
		{
			const FABTSM11PlaybackPoint& SourceA = Plan.Points[SourceIndex - 1];
			const FABTSM11PlaybackPoint& SourceB = Plan.Points[SourceIndex];
			for (int32 Substep = 1;
				Substep <= PresentationSubstepsPerPlaybackInterval;
				++Substep)
			{
				const double Alpha = static_cast<double>(Substep)
					/ static_cast<double>(
						PresentationSubstepsPerPlaybackInterval);
				const double SampleTime = FMath::Lerp(
					SourceA.TimeSeconds,
					SourceB.TimeSeconds,
					Alpha);
				FVector3d PositionCM;
				FVector3d VelocityCMPerSec;
				EABTSM11PlaybackSegmentKind SegmentKind =
					EABTSM11PlaybackSegmentKind::PlayerAuthoritative;
				if (Plan.Sample(
						SampleTime,
						PositionCM,
						VelocityCMPerSec,
						&SegmentKind))
				{
					RawProjectedPoints.Add(View.Project(PositionCM));
				}
			}
		}
	}

	double MaximumHudDeviationNormalized = 0.0;
	for (const FVector2d& RawPoint : RawProjectedPoints)
	{
		double BestDistanceSquared = TNumericLimits<double>::Max();
		for (int32 Index = OriginalPrefixCount + 1;
			Index < Scene.Trajectory.Num();
			++Index)
		{
			BestDistanceSquared = FMath::Min(
				BestDistanceSquared,
				ABTSM11FinaleHudDataTests::PointSegmentDistanceSquared(
					RawPoint,
					View.Project(Scene.Trajectory[Index - 1].PositionCM),
					View.Project(Scene.Trajectory[Index].PositionCM)));
		}
		MaximumHudDeviationNormalized = FMath::Max(
			MaximumHudDeviationNormalized,
			FMath::Sqrt(BestDistanceSquared));
	}
	const double MaximumHudDeviationPixels = MaximumHudDeviationNormalized
		* DiagnosticDiagramRadiusPixels * MaximumOverviewZoom;
	AddInfo(FString::Printf(
		TEXT("Terminal diagnostic: raw-to-HUD max deviation %.4f px at 512 px / 4x"),
		MaximumHudDeviationPixels));
	TestTrue(
		TEXT("Deletion-only HUD polyline stays within its screen-space error limit"),
		MaximumHudDeviationPixels <= MaximumCurveErrorPixels + 1.0e-6);

	int32 ForwardReversalCount = 0;
	double MinimumForwardDelta = TNumericLimits<double>::Max();
	double MinimumConsecutiveDirectionDot = 1.0;
	if (RawProjectedPoints.Num() >= 3)
	{
		const FVector2d OverallDirection =
			(RawProjectedPoints.Last() - RawProjectedPoints[0]).GetSafeNormal();
		FVector2d PreviousDirection = FVector2d::ZeroVector;
		for (int32 Index = 1; Index < RawProjectedPoints.Num(); ++Index)
		{
			const FVector2d Delta =
				RawProjectedPoints[Index] - RawProjectedPoints[Index - 1];
			const double ForwardDelta = Delta.Dot(OverallDirection);
			MinimumForwardDelta = FMath::Min(MinimumForwardDelta, ForwardDelta);
			if (ForwardDelta < -1.0e-9)
			{
				++ForwardReversalCount;
			}
			const FVector2d Direction = Delta.GetSafeNormal();
			if (!Direction.IsNearlyZero() && !PreviousDirection.IsNearlyZero())
			{
				MinimumConsecutiveDirectionDot = FMath::Min(
					MinimumConsecutiveDirectionDot,
					Direction.Dot(PreviousDirection));
			}
			if (!Direction.IsNearlyZero())
			{
				PreviousDirection = Direction;
			}
		}
	}
	const double MaximumLocalTurnDegrees = FMath::RadiansToDegrees(
		FMath::Acos(FMath::Clamp(
			MinimumConsecutiveDirectionDot,
			-1.0,
			1.0)));
	AddInfo(FString::Printf(
		TEXT("Terminal diagnostic: raw playback forward reversals %d, min forward delta %.9f, max local turn %.3f deg"),
		ForwardReversalCount,
		MinimumForwardDelta,
		MaximumLocalTurnDegrees));
	TestEqual(
		TEXT("Raw terminal playback never reverses toward physical contact"),
		ForwardReversalCount,
		0);
	TestTrue(
		TEXT("Raw terminal playback has no abrupt local heading change"),
		MaximumLocalTurnDegrees <= 5.0);

	FABTSM11OrbitalSceneSnapshot AdaptiveScene;
	TestTrue(
		TEXT("Third authoritative scene builds for adaptive sampling"),
		FABTSM11OrbitalSceneBuilder::Build(
			Preset,
			Result,
			AdaptiveScene,
			96));
	const int32 AdaptiveAuthoritativePointCount =
		AdaptiveScene.Trajectory.Num();
	const FABTSM11OrbitalScenePoint AdaptiveAnchor =
		AdaptiveScene.Trajectory.Last();
	FABTSM11PlaybackPlan AdaptivePlan;
	AdaptivePlan.ReleasedTrajectoryHash = AdaptiveScene.SourceTrajectoryHash;
	AdaptivePlan.PlanHash = 0xAB75A11u;
	AdaptivePlan.bPhysicalTargetHit = true;
	AdaptivePlan.bUsesVisibleTerminalTransfer = true;
	FABTSM11PlaybackPoint& AdaptivePlaybackAnchor =
		AdaptivePlan.Points.AddDefaulted_GetRef();
	AdaptivePlaybackAnchor.TimeSeconds = AdaptiveAnchor.TimeSeconds;
	AdaptivePlaybackAnchor.PositionCM = AdaptiveAnchor.PositionCM;
	AdaptivePlaybackAnchor.VelocityCMPerSec = AdaptiveAnchor.VelocityCMPerSec;
	AdaptivePlaybackAnchor.SegmentKind =
		EABTSM11PlaybackSegmentKind::PlayerAuthoritative;

	FVector3d ContactDirection =
		(AdaptiveAnchor.PositionCM - AdaptiveScene.TargetCenterCM)
		.GetSafeNormal();
	if (ContactDirection.IsNearlyZero())
	{
		ContactDirection = FVector3d::ForwardVector;
	}
	const FVector3d ContactPosition = AdaptiveScene.TargetCenterCM
		+ ContactDirection * AdaptiveScene.TargetRadiusCM;
	const FVector3d ToContact = ContactPosition - AdaptiveAnchor.PositionCM;
	FVector3d CurveSide = ToContact.GetSafeNormal()
		.Cross(FVector3d::UpVector).GetSafeNormal();
	if (CurveSide.IsNearlyZero())
	{
		CurveSide = ToContact.GetSafeNormal()
			.Cross(FVector3d::RightVector).GetSafeNormal();
	}
	const FVector3d TransferEnd = AdaptiveAnchor.PositionCM
		+ ToContact * 0.3;
	const double CurveAmplitudeCM = ToContact.Length() * 0.12;
	constexpr int32 CurvedTransferSampleCount = 120;
	constexpr double CurvedTransferDurationSeconds = 4.0;
	for (int32 Index = 1; Index <= CurvedTransferSampleCount; ++Index)
	{
		const double Alpha = static_cast<double>(Index)
			/ static_cast<double>(CurvedTransferSampleCount);
		FABTSM11PlaybackPoint& Point =
			AdaptivePlan.Points.AddDefaulted_GetRef();
		Point.TimeSeconds = AdaptiveAnchor.TimeSeconds
			+ CurvedTransferDurationSeconds * Alpha;
		Point.PositionCM = FMath::Lerp(
			AdaptiveAnchor.PositionCM,
			TransferEnd,
			Alpha)
			+ CurveSide * (CurveAmplitudeCM * FMath::Sin(UE_PI * Alpha));
		Point.VelocityCMPerSec =
			(TransferEnd - AdaptiveAnchor.PositionCM)
				/ CurvedTransferDurationSeconds
			+ CurveSide
				* (CurveAmplitudeCM * UE_PI * FMath::Cos(UE_PI * Alpha)
					/ CurvedTransferDurationSeconds);
		Point.SegmentKind =
			EABTSM11PlaybackSegmentKind::VisibleTerminalTransfer;
	}
	constexpr int32 StraightTailSampleCount = 480;
	constexpr double StraightTailDurationSeconds = 16.0;
	for (int32 Index = 1; Index <= StraightTailSampleCount; ++Index)
	{
		const double Alpha = static_cast<double>(Index)
			/ static_cast<double>(StraightTailSampleCount);
		FABTSM11PlaybackPoint& Point =
			AdaptivePlan.Points.AddDefaulted_GetRef();
		Point.TimeSeconds = AdaptiveAnchor.TimeSeconds
			+ CurvedTransferDurationSeconds
			+ StraightTailDurationSeconds * Alpha;
		Point.PositionCM = FMath::Lerp(TransferEnd, ContactPosition, Alpha);
		Point.VelocityCMPerSec =
			(ContactPosition - TransferEnd) / StraightTailDurationSeconds;
		Point.SegmentKind =
			EABTSM11PlaybackSegmentKind::CertifiedNominalTail;
	}
	AdaptivePlan.DurationSeconds = AdaptivePlan.Points.Last().TimeSeconds;
	constexpr int32 AdaptiveExtensionBudget = 24;
	TestTrue(
		TEXT("Adaptive overview sampling accepts curved transfer and straight tail"),
		FABTSM11OrbitalSceneBuilder::AppendPlaybackExtension(
			AdaptivePlan,
			AdaptiveScene,
			AdaptiveExtensionBudget));
	const int32 AdaptiveExtensionPointCount =
		AdaptiveScene.Trajectory.Num() - AdaptiveAuthoritativePointCount;
	int32 AdaptiveCurvedPointCount = 0;
	int32 AdaptiveTailPointCount = 0;
	for (int32 Index = AdaptiveAuthoritativePointCount;
		Index < AdaptiveScene.Trajectory.Num();
		++Index)
	{
		if (AdaptiveScene.Trajectory[Index].SegmentKind
			== EABTSM11PlaybackSegmentKind::VisibleTerminalTransfer)
		{
			++AdaptiveCurvedPointCount;
		}
		else if (AdaptiveScene.Trajectory[Index].SegmentKind
			== EABTSM11PlaybackSegmentKind::CertifiedNominalTail)
		{
			++AdaptiveTailPointCount;
		}
	}
	TestTrue(
		TEXT("Adaptive overview stays within its extension point budget"),
		AdaptiveExtensionPointCount <= AdaptiveExtensionBudget);
	TestTrue(
		TEXT("Curved transfer receives most of the adaptive point budget"),
		AdaptiveCurvedPointCount > AdaptiveExtensionPointCount / 2);
	TestTrue(
		TEXT("Straight nominal tail and its semantic boundary remain present"),
		AdaptiveTailPointCount >= 2);
	TestTrue(
		TEXT("Adaptive overview still ends at exact physical contact"),
		AdaptiveScene.Trajectory.Last().PositionCM.Equals(
			ContactPosition,
			1.0e-6));

	FABTSM11OrbitalSceneSnapshot SparseCurveScene;
	TestTrue(
		TEXT("Fourth authoritative scene builds for Hermite presentation sampling"),
		FABTSM11OrbitalSceneBuilder::Build(
			Preset,
			Result,
			SparseCurveScene,
			96));
	const int32 SparseAuthoritativePointCount =
		SparseCurveScene.Trajectory.Num();
	const FABTSM11OrbitalScenePoint SparseAnchor =
		SparseCurveScene.Trajectory.Last();
	FVector3d SparseContactDirection =
		(SparseAnchor.PositionCM - SparseCurveScene.TargetCenterCM)
		.GetSafeNormal();
	if (SparseContactDirection.IsNearlyZero())
	{
		SparseContactDirection = FVector3d::ForwardVector;
	}
	const FVector3d SparseContactPosition =
		SparseCurveScene.TargetCenterCM
		+ SparseContactDirection * SparseCurveScene.TargetRadiusCM;
	const FVector3d SparseToContact =
		SparseContactPosition - SparseAnchor.PositionCM;
	FVector3d SparseSide = SparseToContact.GetSafeNormal()
		.Cross(FVector3d::UpVector).GetSafeNormal();
	if (SparseSide.IsNearlyZero())
	{
		SparseSide = SparseToContact.GetSafeNormal()
			.Cross(FVector3d::RightVector).GetSafeNormal();
	}

	FABTSM11PlaybackPlan SparseCurvePlan;
	SparseCurvePlan.ReleasedTrajectoryHash =
		SparseCurveScene.SourceTrajectoryHash;
	SparseCurvePlan.PlanHash = 0xAB75A12u;
	SparseCurvePlan.bPhysicalTargetHit = true;
	SparseCurvePlan.bUsesVisibleTerminalTransfer = true;
	FABTSM11PlaybackPoint& SparsePlaybackAnchor =
		SparseCurvePlan.Points.AddDefaulted_GetRef();
	SparsePlaybackAnchor.TimeSeconds = SparseAnchor.TimeSeconds;
	SparsePlaybackAnchor.PositionCM = SparseAnchor.PositionCM;
	SparsePlaybackAnchor.VelocityCMPerSec = SparseAnchor.VelocityCMPerSec;
	SparsePlaybackAnchor.SegmentKind =
		EABTSM11PlaybackSegmentKind::PlayerAuthoritative;
	FABTSM11PlaybackPoint& SparseMidpoint =
		SparseCurvePlan.Points.AddDefaulted_GetRef();
	SparseMidpoint.TimeSeconds = SparseAnchor.TimeSeconds + 1.0;
	SparseMidpoint.PositionCM = FMath::Lerp(
		SparseAnchor.PositionCM,
		SparseContactPosition,
		0.5);
	SparseMidpoint.VelocityCMPerSec = SparseToContact * 0.5
		+ SparseSide * (SparseToContact.Length() * 0.8);
	SparseMidpoint.SegmentKind =
		EABTSM11PlaybackSegmentKind::VisibleTerminalTransfer;
	FABTSM11PlaybackPoint& SparseContact =
		SparseCurvePlan.Points.AddDefaulted_GetRef();
	SparseContact.TimeSeconds = SparseAnchor.TimeSeconds + 2.0;
	SparseContact.PositionCM = SparseContactPosition;
	SparseContact.VelocityCMPerSec = SparseToContact * 0.5
		- SparseSide * (SparseToContact.Length() * 0.8);
	SparseContact.SegmentKind =
		EABTSM11PlaybackSegmentKind::VisibleTerminalTransfer;
	SparseCurvePlan.DurationSeconds = SparseContact.TimeSeconds;
	TestTrue(
		TEXT("Sparse playback cache is expanded through playback Hermite sampling"),
		FABTSM11OrbitalSceneBuilder::AppendPlaybackExtension(
			SparseCurvePlan,
			SparseCurveScene,
			64));
	const int32 SparseExtensionPointCount =
		SparseCurveScene.Trajectory.Num() - SparseAuthoritativePointCount;
	double MaximumSparseHermiteOffsetCM = 0.0;
	for (int32 Index = SparseAuthoritativePointCount;
		Index < SparseCurveScene.Trajectory.Num();
		++Index)
	{
		MaximumSparseHermiteOffsetCM = FMath::Max(
			MaximumSparseHermiteOffsetCM,
			FMath::Abs(
				(SparseCurveScene.Trajectory[Index].PositionCM
					- SparseAnchor.PositionCM).Dot(SparseSide)));
	}
	TestTrue(
		TEXT("HUD presentation contains points between sparse playback cache samples"),
		SparseExtensionPointCount > SparseCurvePlan.Points.Num() - 1);
	TestTrue(
		TEXT("HUD presentation follows Hermite curvature instead of raw point chords"),
		MaximumSparseHermiteOffsetCM > SparseToContact.Length() * 0.01);
	TestTrue(
		TEXT("Hermite presentation still ends at exact physical contact"),
		SparseCurveScene.Trajectory.Last().PositionCM.Equals(
			SparseContactPosition,
			1.0e-6));

	FABTSM11PlaybackPlan WrongPlan = Plan;
	++WrongPlan.ReleasedTrajectoryHash;
	FABTSM11OrbitalSceneSnapshot UnchangedScene;
	TestTrue(
		TEXT("Second authoritative scene builds for mismatch rejection"),
		FABTSM11OrbitalSceneBuilder::Build(
			Preset,
			Result,
			UnchangedScene,
			96));
	const int32 UnchangedPointCount = UnchangedScene.Trajectory.Num();
	TestFalse(
		TEXT("Mismatched playback identity fails closed"),
		FABTSM11OrbitalSceneBuilder::AppendPlaybackExtension(
			WrongPlan,
			UnchangedScene,
			180));
	TestEqual(
		TEXT("Rejected extension does not mutate the authoritative scene"),
		UnchangedScene.Trajectory.Num(),
		UnchangedPointCount);
	return true;
}
#endif // WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11HudTrajectoryHitTest,
	"ABTS.M11C.HUD.Unit.TrajectoryHitTest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11HudTrajectoryHitTest::RunTest(const FString& Parameters)
{
	FABTSM11OverviewProjection Projection;
	Projection.bValid = true;
	FABTSM11OverviewHitProxy& Hidden = Projection.HitProxies.AddDefaulted_GetRef();
	Hidden.Start = FVector2d(-0.5, 0.0);
	Hidden.End = FVector2d(0.5, 0.0);
	Hidden.StartTimeSeconds = 1.0;
	Hidden.EndTimeSeconds = 2.0;
	Hidden.Leg = EABTSM11TrajectorySemanticLeg::Assist1Encounter;
	Hidden.bHiddenByBody = true;
	FABTSM11OverviewHitProxy& Visible = Projection.HitProxies.AddDefaulted_GetRef();
	Visible.Start = FVector2d(0.0, -0.5);
	Visible.End = FVector2d(0.0, 0.5);
	Visible.StartTimeSeconds = 3.0;
	Visible.EndTimeSeconds = 4.0;
	Visible.StartPhase = 0.25;
	Visible.EndPhase = 0.75;
	Visible.Leg = EABTSM11TrajectorySemanticLeg::Assist2Encounter;
	FABTSM11OverviewHitProxy& Terminal =
		Projection.HitProxies.AddDefaulted_GetRef();
	Terminal.Start = FVector2d(-0.4, 0.75);
	Terminal.End = FVector2d(0.4, 0.75);
	Terminal.StartTimeSeconds = 5.0;
	Terminal.EndTimeSeconds = 6.0;
	Terminal.Leg = EABTSM11TrajectorySemanticLeg::TargetApproach;
	Terminal.SegmentKind =
		EABTSM11PlaybackSegmentKind::VisibleTerminalTransfer;

	FABTSM11TrajectoryHit Hit;
	TestTrue(TEXT("Crossing trajectory can be selected"),
		ABTSM11HitTestOverviewTrajectory(
			Projection,
			FVector2d(200.0, 200.0),
			FVector2d(200.0, 200.0),
			160.0,
			8.0,
			Hit));
	TestEqual(TEXT("Visible segment wins an equal-distance crossing"),
		Hit.Leg, EABTSM11TrajectorySemanticLeg::Assist2Encounter);
	TestTrue(TEXT("Hit preserves continuous semantic phase"),
		FMath::IsNearlyEqual(Hit.PhaseWithinLeg, 0.5));
	TestFalse(TEXT("Outside-circle clicks are rejected"),
		ABTSM11HitTestOverviewTrajectory(
			Projection,
			FVector2d(500.0, 500.0),
			FVector2d(200.0, 200.0),
			160.0,
			8.0,
			Hit));
	TestFalse(TEXT("Presentation-only terminal transfer is not selectable"),
		ABTSM11HitTestOverviewTrajectory(
			Projection,
			FVector2d(200.0, 320.0),
			FVector2d(200.0, 200.0),
			160.0,
			8.0,
			Hit));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11HudSemanticProbeTest,
	"ABTS.M11C.HUD.Unit.SemanticProbe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11HudSemanticProbeTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM11FinaleHudDataTests;
	const FFixture& Fixture = GetFixture();
	TestTrue(TEXT("Certified fixture is valid"), Fixture.bValid);
	if (!Fixture.bValid)
	{
		return false;
	}
	const FABTSM11TrajectorySemanticSegment* Segment =
		Fixture.Scene.SemanticMap.Find(
			EABTSM11TrajectorySemanticLeg::Assist2Encounter);
	TestNotNull(TEXT("Assist-2 encounter survives trajectory decimation"), Segment);
	if (Segment == nullptr)
	{
		return false;
	}
	int32 PointA = INDEX_NONE;
	int32 PointB = INDEX_NONE;
	double Alpha = 0.0;
	TestTrue(TEXT("Semantic midpoint resolves independently of point index"),
		Fixture.Scene.SemanticMap.ResolvePoint(
			EABTSM11TrajectorySemanticLeg::Assist2Encounter,
			0.5,
			Fixture.Scene.Trajectory,
			PointA,
			PointB,
			Alpha));
	TestTrue(TEXT("Encounter midpoint maps to closest-approach neighborhood"),
		PointA <= Segment->ClosestPointIndex
			&& PointB >= Segment->ClosestPointIndex);
	FABTSM11TrajectoryProbe Probe;
	TestTrue(TEXT("A semantic hit creates a persistent probe"),
		FABTSM11TrajectoryProbeBuilder::Create(
			Fixture.Scene,
			MakeHit(EABTSM11TrajectorySemanticLeg::Assist2Encounter, 0.5),
			FVector3d::UpVector,
			FVector3d::ForwardVector,
			Probe));
	TestEqual(TEXT("Probe stores the semantic leg"),
		Probe.Leg, EABTSM11TrajectorySemanticLeg::Assist2Encounter);
	TestTrue(TEXT("Probe stores normalized progress"),
		FMath::IsNearlyEqual(Probe.PhaseWithinLeg, 0.5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11HudFrozenPipViewTest,
	"ABTS.M11C.HUD.Unit.FrozenPipView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11HudFrozenPipViewTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM11FinaleHudDataTests;
	const FFixture& Fixture = GetFixture();
	TestTrue(TEXT("Certified fixture is valid"), Fixture.bValid);
	if (!Fixture.bValid)
	{
		return false;
	}
	FABTSM11TrajectoryProbe Probe;
	TestTrue(TEXT("Frozen PIP probe builds"),
		FABTSM11TrajectoryProbeBuilder::Create(
			Fixture.Scene,
			MakeHit(EABTSM11TrajectorySemanticLeg::Assist1Encounter, 0.5),
			FVector3d::UpVector,
			FVector3d::ForwardVector,
			Probe));
	const FABTSM11FrozenPipView Frozen = Probe.FrozenPipView;
	const FABTSM11OrbitalSceneSnapshot Shifted = ShiftTrajectory(
		Fixture.Scene,
		FVector3d(2500.0, 1400.0, -600.0),
		Fixture.Scene.SourceTrajectoryHash + 7);
	FABTSM11ProbeProjection ReferenceProjection;
	FABTSM11ProbeProjection ShiftedProjection;
	TestTrue(TEXT("Reference trajectory resolves in PIP"),
		FABTSM11TrajectoryProbeResolver::Resolve(
			Fixture.Scene, Probe, ReferenceProjection));
	TestTrue(TEXT("Changed trajectory resolves in the frozen PIP"),
		FABTSM11TrajectoryProbeResolver::Resolve(
			Shifted, Probe, ShiftedProjection));
	TestTrue(TEXT("PIP frame remains byte-for-byte stable while aiming"),
		Probe.FrozenPipView.ViewCenterCM.Equals(Frozen.ViewCenterCM, 1.0e-12)
		&& Probe.FrozenPipView.ViewForward.Equals(Frozen.ViewForward, 1.0e-12)
		&& Probe.FrozenPipView.ViewUp.Equals(Frozen.ViewUp, 1.0e-12)
		&& FMath::IsNearlyEqual(Probe.FrozenPipView.HalfExtentCM, Frozen.HalfExtentCM));
	TestFalse(TEXT("Only the projected trajectory moves inside frozen PIP"),
		NearlyEqual(
			ReferenceProjection.PipPosition,
			ShiftedProjection.PipPosition,
			1.0e-8));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11HudProbeRemapTest,
	"ABTS.M11C.HUD.Unit.ProbeRemap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11HudProbeRemapTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM11FinaleHudDataTests;
	const FFixture& Fixture = GetFixture();
	TestTrue(TEXT("Certified fixture is valid"), Fixture.bValid);
	if (!Fixture.bValid)
	{
		return false;
	}
	FABTSM11TrajectoryProbe EncounterProbe;
	TestTrue(TEXT("Encounter probe builds"),
		FABTSM11TrajectoryProbeBuilder::Create(
			Fixture.Scene,
			MakeHit(EABTSM11TrajectorySemanticLeg::Assist3Encounter, 0.4),
			FVector3d::UpVector,
			FVector3d::ForwardVector,
			EncounterProbe));
	FABTSM11OrbitalSceneSnapshot MissScene = Fixture.Scene;
	MissScene.SourceTrajectoryHash += 11;
	MissScene.SemanticMap.Segments.RemoveAll(
		[](const FABTSM11TrajectorySemanticSegment& Segment)
		{
			return Segment.Leg == EABTSM11TrajectorySemanticLeg::Assist3Encounter;
		});
	FABTSM11ProbeProjection MissProjection;
	TestTrue(TEXT("Missing encounter maps to closest miss"),
		FABTSM11TrajectoryProbeResolver::Resolve(
			MissScene, EncounterProbe, MissProjection));
	TestEqual(TEXT("Closest-miss fallback is explicit"),
		MissProjection.Status, EABTSM11ProbeRemapStatus::ClosestMissFallback);

	FABTSM11TrajectoryProbe TargetProbe;
	TestTrue(TEXT("Terminal-coast probe builds"),
		FABTSM11TrajectoryProbeBuilder::Create(
			Fixture.Scene,
			MakeHit(EABTSM11TrajectorySemanticLeg::Assist3ToTarget, 0.6),
			FVector3d::UpVector,
			FVector3d::ForwardVector,
			TargetProbe));
	FABTSM11OrbitalSceneSnapshot EndedScene = Fixture.Scene;
	EndedScene.SemanticMap.Segments.RemoveAll(
		[](const FABTSM11TrajectorySemanticSegment& Segment)
		{
			return Segment.Leg == EABTSM11TrajectorySemanticLeg::Assist3ToTarget;
		});
	FABTSM11ProbeProjection EndedProjection;
	TestTrue(TEXT("Absent future leg resolves to trajectory endpoint"),
		FABTSM11TrajectoryProbeResolver::Resolve(
			EndedScene, TargetProbe, EndedProjection));
	TestEqual(TEXT("Ended-before-leg state is explicit"),
		EndedProjection.Status,
		EABTSM11ProbeRemapStatus::TrajectoryEndedBeforeLeg);

	const FABTSM11OrbitalSceneSnapshot Shifted = ShiftTrajectory(
		Fixture.Scene,
		FVector3d(1200.0, 300.0, 100.0),
		Fixture.Scene.SourceTrajectoryHash + 17);
	FABTSM11TrajectoryProbe Rebased;
	TestTrue(TEXT("Explicit rebase adopts the latest semantic trajectory"),
		FABTSM11TrajectoryProbeBuilder::Rebase(
			Shifted, EncounterProbe, FVector3d::UpVector, Rebased));
	TestEqual(TEXT("Rebase updates the reference hash"),
		Rebased.ReferenceResultHash, Shifted.SourceTrajectoryHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11HudInputCaptureTest,
	"ABTS.M11C.HUD.Unit.InputCapture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11HudInputCaptureTest::RunTest(const FString& Parameters)
{
	const FVector2D DpiMapped = ABTSM11MapViewportPointToHudCanvas(
		FVector2D(1250.0f, 750.0f),
		FVector2D::ZeroVector,
		FVector2D(2500.0f, 1500.0f),
		FVector2D(2000.0f, 1200.0f));
	TestTrue(TEXT("Viewport pixels map into HUD Canvas coordinates"),
		DpiMapped.Equals(FVector2D(1000.0f, 600.0f), 0.001f));
	const FVector2D NonUniformMapped = ABTSM11MapViewportPointToHudCanvas(
		FVector2D(960.0f, 540.0f),
		FVector2D::ZeroVector,
		FVector2D(1920.0f, 1080.0f),
		FVector2D(1280.0f, 800.0f));
	TestTrue(TEXT("Each HUD Canvas axis is mapped independently"),
		NonUniformMapped.Equals(FVector2D(640.0f, 400.0f), 0.001f));
	const FVector2D LetterboxMapped = ABTSM11MapViewportPointToHudCanvas(
		FVector2D(650.0f, 350.0f),
		FVector2D(150.0f, 100.0f),
		FVector2D(1000.0f, 500.0f),
		FVector2D(800.0f, 400.0f));
	TestTrue(TEXT("Player-view origin is removed before HUD hit testing"),
		LetterboxMapped.Equals(FVector2D(400.0f, 200.0f), 0.001f));
	TestTrue(TEXT("Invalid size fails safely without moving the pointer"),
		ABTSM11MapViewportPointToHudCanvas(
			FVector2D(11.0f, 22.0f),
			FVector2D::ZeroVector,
			FVector2D::ZeroVector,
			FVector2D(1280.0f, 720.0f)).Equals(
				FVector2D(11.0f, 22.0f), 0.001f));

	FABTSM11FinaleHudCaptureState State;
	TestTrue(TEXT("Yaw knob acquires exclusive capture"),
		State.TryBegin(EABTSM11FinaleHudCapture::AdjustYaw));
	TestFalse(TEXT("Overview rotation cannot steal an active knob capture"),
		State.TryBegin(EABTSM11FinaleHudCapture::RotateOverview));
	TestFalse(TEXT("Overview pan cannot steal an active knob capture"),
		State.TryBegin(EABTSM11FinaleHudCapture::PanOverview));
	TestFalse(TEXT("Launch is blocked during any edit capture"), State.CanLaunch());
	TestFalse(TEXT("A mismatched release cannot clear capture"),
		State.End(EABTSM11FinaleHudCapture::AdjustPitch));
	TestTrue(TEXT("Matching release clears capture"),
		State.End(EABTSM11FinaleHudCapture::AdjustYaw));
	TestTrue(TEXT("Move-mode pan acquires the shared exclusive channel"),
		State.TryBegin(EABTSM11FinaleHudCapture::PanOverview));
	TestTrue(TEXT("Move-mode pan release clears capture"),
		State.End(EABTSM11FinaleHudCapture::PanOverview));
	TestTrue(TEXT("Launch acquires the same exclusive channel"), State.TryBeginLaunch());
	State.CancelForFocusLoss();
	TestEqual(TEXT("Focus loss releases all capture"),
		State.GetCapture(), EABTSM11FinaleHudCapture::None);
	TestTrue(TEXT("Focus-loss cancellation is observable"),
		State.WasFocusLossCancellation());
	TestTrue(TEXT("Launch becomes available after cancellation"), State.CanLaunch());
	TestFalse(TEXT("Knob release never commits launch"),
		ABTSM11ShouldCommitFinaleHudLaunch(
			EABTSM11FinaleHudCapture::AdjustYaw,
			true,
			true));
	TestFalse(TEXT("Launch press dragged outside cancels"),
		ABTSM11ShouldCommitFinaleHudLaunch(
			EABTSM11FinaleHudCapture::LaunchButton,
			false,
			true));
	TestFalse(TEXT("Launch cannot commit after aiming ends"),
		ABTSM11ShouldCommitFinaleHudLaunch(
			EABTSM11FinaleHudCapture::LaunchButton,
			true,
			false));
	TestTrue(TEXT("Only an aiming launch-button release commits"),
		ABTSM11ShouldCommitFinaleHudLaunch(
			EABTSM11FinaleHudCapture::LaunchButton,
			true,
			true));
	TestTrue(TEXT("An active finale routes console presses before shared HUD consumption"),
		ABTSM11RequiresExclusiveFinaleHudPointerRouting(true));
	TestFalse(TEXT("An inactive finale leaves shared HUD consumption in control"),
		ABTSM11RequiresExclusiveFinaleHudPointerRouting(false));
	TestFalse(TEXT("Aim-only updates never recapture a frozen probe"),
		ABTSM11ShouldRefreshFinaleHudTargetCapture(
			true,
			true,
			true,
			false));
	TestTrue(TEXT("Explicit probe mutation refreshes static capture once"),
		ABTSM11ShouldRefreshFinaleHudTargetCapture(
			true,
			true,
			false,
			true));
	TestTrue(TEXT("Automatic preview captures on first publication"),
		ABTSM11ShouldRefreshFinaleHudTargetCapture(
			false,
			false,
			false,
			false));
	TestTrue(TEXT("Automatic target changes refresh without a probe"),
		ABTSM11ShouldRefreshFinaleHudTargetCapture(
			false,
			true,
			true,
			false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11HudVisualLayoutTest,
	"ABTS.M11D.HUD.Unit.VisualLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11HudVisualLayoutTest::RunTest(const FString& Parameters)
{
	const auto Overlaps = [](const FBox2D& A, const FBox2D& B)
	{
		return A.Min.X < B.Max.X && A.Max.X > B.Min.X
			&& A.Min.Y < B.Max.Y && A.Max.Y > B.Min.Y;
	};
	for (const FVector2D Viewport : {
		FVector2D(1024.0f, 768.0f),
		FVector2D(1280.0f, 720.0f),
		FVector2D(1600.0f, 900.0f),
		FVector2D(1920.0f, 1080.0f)})
	{
		FABTSM11FinaleHudVisualLayout Layout;
		TestTrue(
			*FString::Printf(TEXT("Layout builds for %.0fx%.0f"), Viewport.X, Viewport.Y),
			ABTSM11BuildFinaleHudVisualLayout(
				Viewport, 0.044f, 30.0f, 42.0f, Layout));
		TestTrue(TEXT("Layout publishes a valid result"), Layout.bValid);
		TestFalse(TEXT("Orbit and controls remain separate"),
			Overlaps(Layout.OrbitPanel, Layout.ControlDeck));
		TestFalse(TEXT("Controls and target monitor remain separate"),
			Overlaps(Layout.ControlDeck, Layout.PreviewBay));
		TestFalse(TEXT("Orbit and target monitor remain separate"),
			Overlaps(Layout.OrbitPanel, Layout.PreviewBay));
		TestTrue(TEXT("Mission strip remains in the viewport"),
			Layout.MissionStrip.Min.X >= 0.0f
			&& Layout.MissionStrip.Min.Y >= 0.0f
			&& Layout.MissionStrip.Max.X <= Viewport.X
			&& Layout.MissionStrip.Max.Y <= Viewport.Y);
		TestTrue(TEXT("All knobs remain inside the control deck"),
			Layout.ControlDeck.IsInside(Layout.KnobCenters[0])
			&& Layout.ControlDeck.IsInside(Layout.KnobCenters[1])
			&& Layout.ControlDeck.IsInside(Layout.KnobCenters[2]));
		TestTrue(TEXT("Launch button remains inside the control deck"),
			Layout.ControlDeck.IsInside(Layout.LaunchButton.Min)
			&& Layout.ControlDeck.IsInside(Layout.LaunchButton.Max));
	}

	FABTSM11FinaleHudVisualLayout Rejected;
	TestFalse(TEXT("Unsupported tiny viewports fail closed"),
		ABTSM11BuildFinaleHudVisualLayout(
			FVector2D(640.0f, 480.0f), 0.044f, 30.0f, 42.0f, Rejected));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11HudPipEdgeIndicatorTest,
	"ABTS.M11C.HUD.Unit.PipEdgeIndicator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11HudPipEdgeIndicatorTest::RunTest(const FString& Parameters)
{
	FABTSM11PipEdgeIndicator Indicator;
	TestTrue(TEXT("An in-frame point is a valid no-cue result"),
		ABTSM11BuildPipEdgeIndicator(
			FVector2d(0.25, 0.75), 0.05, Indicator));
	TestFalse(TEXT("An in-frame point does not show an edge cue"),
		Indicator.bVisible);

	TestTrue(TEXT("A right-side point produces an edge cue"),
		ABTSM11BuildPipEdgeIndicator(
			FVector2d(1.40, 0.70), 0.08, Indicator));
	TestTrue(TEXT("The right-side cue is visible"), Indicator.bVisible);
	TestTrue(TEXT("The cue anchor stays on the inset frame"),
		FMath::IsNearlyEqual(Indicator.AnchorUV.X, 0.92, 1.0e-9)
			&& Indicator.AnchorUV.Y >= 0.08
			&& Indicator.AnchorUV.Y <= 0.92);
	TestTrue(TEXT("The cue points towards the off-screen sample"),
		Indicator.DirectionUV.X > 0.0
			&& Indicator.DirectionUV.Y > 0.0);
	TestTrue(TEXT("The cue reports positive overshoot"),
		Indicator.OvershootUV > 0.0);

	TestFalse(TEXT("An invalid inset fails closed"),
		ABTSM11BuildPipEdgeIndicator(
			FVector2d(2.0, 0.5), 0.5, Indicator));
	return true;
}

#endif
