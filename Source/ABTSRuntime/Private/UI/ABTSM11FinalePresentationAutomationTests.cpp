// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "UI/ABTSM11FinalePresentation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11CTargetPipPresentationTest,
	"ABTS.M11C.Unit.TargetPipPresentation",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11CTargetPipPresentationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FABTSM11FinaleLayoutPreset Preset =
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
	FABTSM11PreviewSelection FirstSelection;
	FirstSelection.Target = EABTSM11PreviewTarget::Assist2;
	FirstSelection.ClosestDistanceCM = 1000.0;
	FirstSelection.IncomingDirection =
		FVector3d(0.2, 0.7, -0.4).GetSafeNormal();
	FABTSM11PreviewSelection JitteredSelection = FirstSelection;
	JitteredSelection.ClosestDistanceCM = 100000.0;
	JitteredSelection.IncomingDirection =
		FVector3d(-0.8, 0.1, 0.5).GetSafeNormal();

	FABTSM11TargetPipView FirstView;
	FABTSM11TargetPipView JitteredView;
	TestTrue(
		TEXT("Target PIP builds from the frozen preset"),
		ABTSM11BuildTargetPipView(
			Preset,
			FirstSelection,
			384,
			240,
			FirstView));
	TestTrue(
		TEXT("Target PIP rebuild accepts a changed current result"),
		ABTSM11BuildTargetPipView(
			Preset,
			JitteredSelection,
			384,
			240,
			JitteredView));
	const FVector3d ExpectedForward = (
		Preset.CanonicalScenario.GetAssist(2).CenterCM
		- Preset.CanonicalScenario.GetAssist(1).CenterCM)
			.GetSafeNormal();
	TestTrue(
		TEXT("View direction is previous target to current target"),
		FVector3d::DotProduct(
			FirstView.Forward,
			ExpectedForward) > 0.999999);
	TestTrue(
		TEXT("Constant local +Z defines a stable orthogonal screen up"),
		FMath::Abs(FVector3d::DotProduct(
			FirstView.Forward,
			FirstView.Up)) < 1.0e-9
			&& FVector3d::DotProduct(
				FirstView.Up,
				FVector3d::UpVector) > 0.0);
	TestTrue(
		TEXT("Incidence and closest-distance jitter cannot move the capture"),
		FirstView.TargetCenterCM.Equals(
			JitteredView.TargetCenterCM,
			1.0e-9)
			&& FirstView.CameraLocationCM.Equals(
				JitteredView.CameraLocationCM,
				1.0e-9)
			&& FirstView.Forward.Equals(
				JitteredView.Forward,
				1.0e-9)
			&& FirstView.Up.Equals(
				JitteredView.Up,
				1.0e-9)
			&& FMath::IsNearlyEqual(
				FirstView.CameraDistanceCM,
				JitteredView.CameraDistanceCM,
				1.0e-9));

	FABTSM11TrajectoryResult Prediction;
	Prediction.ValidationHash = 0x11c0f001ull;
	constexpr int32 SourcePointCount = 201;
	Prediction.Points.Reserve(SourcePointCount);
	for (int32 Index = 0; Index < SourcePointCount; ++Index)
	{
		const double Alpha =
			(static_cast<double>(Index) - 100.0) / 100.0;
		FABTSM11TrajectoryPoint& Point =
			Prediction.Points.AddDefaulted_GetRef();
		Point.TimeSeconds = Index / 120.0;
		Point.PositionCM =
			FirstView.TargetCenterCM
			+ FirstView.Right
				* (Alpha * FirstView.FramingRadiusCM * 0.80)
			+ FirstView.Forward
				* (Alpha * FirstView.FramingRadiusCM * 0.10);
		Point.VelocityCMPerSec =
			FirstView.Right * 900.0
			+ FirstView.Forward * 120.0;
	}
	FABTSM11TargetPipTrajectory LocalTrajectory;
	TestTrue(
		TEXT("Current authoritative local trajectory builds"),
		ABTSM11BuildTargetPipTrajectory(
			FirstView,
			FirstSelection,
			Prediction,
			LocalTrajectory,
			21));
	TestTrue(
		TEXT("Local PIP scan publishes a bounded Canvas polyline"),
		LocalTrajectory.Points.Num() >= 3
			&& LocalTrajectory.Points.Num() <= 21);
	TestEqual(
		TEXT("True closest source point is retained"),
		LocalTrajectory.ClosestSourcePointIndex,
		100);
	int32 ClosestMarkerCount = 0;
	bool bClosestCentered = false;
	for (const FABTSM11TargetPipTrajectoryPoint& Point
		: LocalTrajectory.Points)
	{
		if (Point.bClosestApproach)
		{
			++ClosestMarkerCount;
			bClosestCentered =
				Point.bInFront
				&& Point.UV.Equals(
					FVector2D(0.5f, 0.5f),
					1.0e-4f);
		}
	}
	TestEqual(
		TEXT("Exactly one closest-approach marker is emitted"),
		ClosestMarkerCount,
		1);
	TestTrue(
		TEXT("Target and exact closest point share the stable PIP center"),
		bClosestCentered);

	FVector2D ClipStart(-1.0f, 0.5f);
	FVector2D ClipEnd(2.0f, 0.5f);
	TestTrue(
		TEXT("PIP line clipping accepts a crossing segment"),
		ABTSM11ClipPipLineToRect(
			ClipStart,
			ClipEnd,
			0.05f));
	TestTrue(
		TEXT("PIP line clipping honors its safe inset"),
		FMath::IsNearlyEqual(ClipStart.X, 0.05f, 1.0e-5f)
			&& FMath::IsNearlyEqual(
				ClipEnd.X,
				0.95f,
				1.0e-5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11CTargetWedgePresentationTest,
	"ABTS.M11C.Unit.TargetWedgePresentation",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11CTargetWedgePresentationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FVector2D Viewport(1000.0f, 600.0f);
	const FVector3d CameraLocation = FVector3d::ZeroVector;
	const FVector3d CameraForward = FVector3d::ForwardVector;
	const FVector3d CameraRight = FVector3d::RightVector;
	const FVector3d CameraUp = FVector3d::UpVector;
	const auto Project =
		[&] (const FVector3d& Target)
	{
		return ABTSM11ProjectTargetForWedge(
			Target,
			CameraLocation,
			CameraForward,
			CameraRight,
			CameraUp,
			90.0,
			Viewport);
	};

	FABTSM11TargetWedgeConfig Config;
	Config.AnchorMarginPixels = 38.0f;
	Config.ShowEdgeDistancePixels = 58.0f;
	Config.HideEdgeDistancePixels = 92.0f;
	Config.ShowHoldSeconds = 0.05;
	Config.HideHoldSeconds = 0.10;
	FABTSM11TargetWedgeTracker Tracker;
	const FABTSM11TargetWedgeOutput Centered =
		Tracker.Update(
			0.016,
			EABTSM11PreviewTarget::Assist1,
			Project(FVector3d(1000.0, 0.0, 0.0)),
			Viewport,
			Config);
	TestFalse(
		TEXT("Wedge hides when the selected target is safely in view"),
		Centered.bVisible);

	FABTSM11TargetWedgeOutput Offscreen =
		Tracker.Update(
			0.03,
			EABTSM11PreviewTarget::Assist1,
			Project(FVector3d(1000.0, 3000.0, 0.0)),
			Viewport,
			Config);
	TestFalse(
		TEXT("Wedge show transition honors temporal hysteresis"),
		Offscreen.bVisible);
	Offscreen = Tracker.Update(
		0.03,
		EABTSM11PreviewTarget::Assist1,
		Project(FVector3d(1000.0, 3000.0, 0.0)),
		Viewport,
		Config);
	TestTrue(
		TEXT("Only the current offscreen target emits a wedge"),
		Offscreen.bVisible
			&& Offscreen.Target
				== EABTSM11PreviewTarget::Assist1);
	TestTrue(
		TEXT("Wedge anchor remains inside the viewport safe margin"),
		Offscreen.Anchor.X
				<= Viewport.X - Config.AnchorMarginPixels + 0.01f
			&& Offscreen.Anchor.X
				>= Config.AnchorMarginPixels - 0.01f
			&& Offscreen.Anchor.Y
				<= Viewport.Y - Config.AnchorMarginPixels + 0.01f
			&& Offscreen.Anchor.Y
				>= Config.AnchorMarginPixels - 0.01f);
	TestTrue(
		TEXT("Right-side target produces a right-pointing wedge"),
		Offscreen.Direction.X > 0.99f);

	FABTSM11TargetWedgeOutput Returning =
		Tracker.Update(
			0.05,
			EABTSM11PreviewTarget::Assist1,
			Project(FVector3d(1000.0, 0.0, 0.0)),
			Viewport,
			Config);
	TestTrue(
		TEXT("Wedge does not disappear on the first in-view sample"),
		Returning.bVisible);
	Returning = Tracker.Update(
		0.06,
		EABTSM11PreviewTarget::Assist1,
		Project(FVector3d(1000.0, 0.0, 0.0)),
		Viewport,
		Config);
	TestFalse(
		TEXT("Wedge hides after the selected target remains in view"),
		Returning.bVisible);

	const FABTSM11TargetWedgeOutput HysteresisBand =
		Tracker.Update(
			0.50,
			EABTSM11PreviewTarget::Assist1,
			Project(FVector3d(1000.0, -850.0, 0.0)),
			Viewport,
			Config);
	TestFalse(
		TEXT("Spatial hysteresis band does not re-show a hidden wedge"),
		HysteresisBand.bVisible);
	FABTSM11TargetWedgeOutput LeftOffscreen =
		Tracker.Update(
			0.06,
			EABTSM11PreviewTarget::Assist1,
			Project(FVector3d(1000.0, -2000.0, 0.0)),
			Viewport,
			Config);
	TestTrue(
		TEXT("Leaving the safe view re-shows the same target"),
		LeftOffscreen.bVisible
			&& LeftOffscreen.Direction.X < -0.99f);

	const FABTSM11TargetWedgeProjection BehindProjection =
		Project(FVector3d(-1000.0, 0.0, 0.0));
	TestFalse(
		TEXT("Behind-camera target is never classified as in view"),
		BehindProjection.bInFront);
	const FABTSM11TargetWedgeOutput Behind =
		Tracker.Update(
			0.06,
			EABTSM11PreviewTarget::Assist2,
			BehindProjection,
			Viewport,
			Config);
	TestTrue(
		TEXT("A target change replaces, rather than adds, the wedge"),
		Behind.bVisible
			&& Behind.Target
				== EABTSM11PreviewTarget::Assist2);
	return true;
}

#endif
