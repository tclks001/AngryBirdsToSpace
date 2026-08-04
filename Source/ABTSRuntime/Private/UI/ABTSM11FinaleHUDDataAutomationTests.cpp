// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "UI/ABTSM11FinaleHUDData.h"
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
	return true;
}

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
