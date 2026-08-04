// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/ABTSM4CameraRigModel.h"
#include "Misc/AutomationTest.h"
#include "Party/ABTSBirdPartySettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM4CameraGamepadFrameRateTest,
	"ABTS.Camera.GroundRig.GamepadFrameRateInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM4CameraGamepadFrameRateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const float Response = ABTSM4CameraRigModel::ApplyGamepadResponse(0.75f, 0.18f, 1.35f);
	const float RateDegreesPerSecond = 120.0f;
	auto IntegrateOneSecond = [&](const int32 Frames)
	{
		float Degrees = 0.0f;
		const float DeltaSeconds = 1.0f / static_cast<float>(Frames);
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Degrees += Response * RateDegreesPerSecond * DeltaSeconds;
		}
		return Degrees;
	};

	const float At30 = IntegrateOneSecond(30);
	const float At60 = IntegrateOneSecond(60);
	const float At120 = IntegrateOneSecond(120);
	TestTrue(TEXT("30 and 60 FPS integrate to the same angle"), FMath::IsNearlyEqual(At30, At60, 0.001f));
	TestTrue(TEXT("60 and 120 FPS integrate to the same angle"), FMath::IsNearlyEqual(At60, At120, 0.001f));
	TestTrue(TEXT("Dead-zone samples are zero"), FMath::IsNearlyZero(ABTSM4CameraRigModel::ApplyGamepadResponse(0.1f, 0.18f, 1.35f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM4CameraSphericalPivotAxisSeparationTest,
	"ABTS.Camera.GroundRig.SphericalPivotAxisSeparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM4CameraSphericalPivotAxisSeparationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FVector Center = FVector::ZeroVector;
	const float DeltaSeconds = 1.0f / 60.0f;
	const float FollowSpeed = 7.5f;
	const float DeadZoneCM = 22.0f;

	const FVector InitialPivot(1000.0f, 0.0f, 0.0f);
	const FVector SmallRadialStep(1010.0f, 0.0f, 0.0f);
	const FVector GroundedRadialResult = ABTSM4CameraRigModel::UpdateSphericalPivot(
		InitialPivot, SmallRadialStep, Center, DeltaSeconds, FollowSpeed, 180.0f, DeadZoneCM, true);
	TestTrue(TEXT("Grounded radial motion is not held by the tangential dead zone"),
		GroundedRadialResult.Size() > InitialPivot.Size());
	TestTrue(TEXT("Grounded radial motion remains smoothed instead of snapping to the target radius"),
		GroundedRadialResult.Size() < SmallRadialStep.Size());

	FVector AirbornePivot = InitialPivot;
	for (int32 Frame = 1; Frame <= 6; ++Frame)
	{
		const FVector AirborneTarget(1000.0f + Frame * 5.0f, 0.0f, 0.0f);
		const float PreviousRadius = AirbornePivot.Size();
		AirbornePivot = ABTSM4CameraRigModel::UpdateSphericalPivot(
			AirbornePivot, AirborneTarget, Center, DeltaSeconds, FollowSpeed, 180.0f, DeadZoneCM, false);
		TestTrue(TEXT("Every airborne ascent frame advances the radial pivot"), AirbornePivot.Size() > PreviousRadius);
		TestTrue(TEXT("Airborne radial follow does not overshoot its target"), AirbornePivot.Size() <= AirborneTarget.Size());
	}
	AirbornePivot = FVector(1060.0f, 0.0f, 0.0f);
	for (int32 Frame = 1; Frame <= 6; ++Frame)
	{
		const FVector AirborneTarget(1060.0f - Frame * 5.0f, 0.0f, 0.0f);
		const float PreviousRadius = AirbornePivot.Size();
		AirbornePivot = ABTSM4CameraRigModel::UpdateSphericalPivot(
			AirbornePivot, AirborneTarget, Center, DeltaSeconds, FollowSpeed, 180.0f, DeadZoneCM, false);
		TestTrue(TEXT("Every airborne descent frame advances the radial pivot"), AirbornePivot.Size() < PreviousRadius);
		TestTrue(TEXT("Airborne radial descent does not undershoot its target"), AirbornePivot.Size() >= AirborneTarget.Size());
	}

	auto IntegrateOneSecond = [&](const int32 Frames)
	{
		FVector Pivot = InitialPivot;
		const FVector Target(1100.0f, 0.0f, 0.0f);
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Pivot = ABTSM4CameraRigModel::UpdateSphericalPivot(
				Pivot, Target, Center, 1.0f / static_cast<float>(Frames), FollowSpeed, 180.0f, DeadZoneCM, false);
		}
		return Pivot.Size();
	};
	const float At30FPS = IntegrateOneSecond(30);
	const float At60FPS = IntegrateOneSecond(60);
	const float At120FPS = IntegrateOneSecond(120);
	TestTrue(TEXT("Radial smoothing is invariant between 30 and 60 FPS"), FMath::IsNearlyEqual(At30FPS, At60FPS, 0.001f));
	TestTrue(TEXT("Radial smoothing is invariant between 60 and 120 FPS"), FMath::IsNearlyEqual(At60FPS, At120FPS, 0.001f));

	const float SmallArcRadians = 10.0f / InitialPivot.Size();
	const FVector SmallTangentialStep = FVector(
		FMath::Cos(SmallArcRadians),
		FMath::Sin(SmallArcRadians),
		0.0f) * InitialPivot.Size();
	const FVector GroundedTangentialResult = ABTSM4CameraRigModel::UpdateSphericalPivot(
		InitialPivot, SmallTangentialStep, Center, DeltaSeconds, FollowSpeed, 180.0f, DeadZoneCM, true);
	TestTrue(TEXT("Grounded sub-threshold tangential jitter remains inside the focus window"),
		GroundedTangentialResult.Equals(InitialPivot, KINDA_SMALL_NUMBER));

	const FVector AirborneTangentialResult = ABTSM4CameraRigModel::UpdateSphericalPivot(
		InitialPivot, SmallTangentialStep, Center, DeltaSeconds, FollowSpeed, 180.0f, DeadZoneCM, false);
	TestTrue(TEXT("The same sub-threshold tangential movement follows while airborne"),
		!AirborneTangentialResult.Equals(InitialPivot, KINDA_SMALL_NUMBER));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM4CameraSweepCenterDistanceTest,
	"ABTS.Camera.GroundRig.SweepCenterDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM4CameraSweepCenterDistanceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const float DesiredDistance = 850.0f;
	const float HitDistance = 600.0f;
	const float SafetyMargin = 4.0f;
	const float SafeDistance = ABTSM4CameraRigModel::ComputeSafeSweepDistance(
		DesiredDistance, true, false, HitDistance, SafetyMargin);
	TestTrue(TEXT("Sweep center loses only the explicit safety margin"), FMath::IsNearlyEqual(SafeDistance, 596.0f));
	TestTrue(TEXT("An unobstructed sweep preserves desired distance"), FMath::IsNearlyEqual(
		ABTSM4CameraRigModel::ComputeSafeSweepDistance(DesiredDistance, false, false, 0.0f, SafetyMargin),
		DesiredDistance));
	TestTrue(TEXT("Initial penetration fails safely without forcing a comfort minimum"), FMath::IsNearlyEqual(
		ABTSM4CameraRigModel::ComputeSafeSweepDistance(DesiredDistance, true, true, 0.0f, SafetyMargin),
		1.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM4CameraObstructionHysteresisTest,
	"ABTS.Camera.GroundRig.ObstructionHysteresis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM4CameraObstructionHysteresisTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FABTSM4CameraObstructionFilter Filter;
	FABTSM4CameraObstructionFilterSettings Settings;
	Settings.EnterDelaySeconds = 0.04f;
	Settings.ExitDelaySeconds = 0.16f;
	Filter.Reset(850.0f);
	TestFalse(TEXT("Runtime obstruction avoidance is opt-in"),
		GetDefault<AABTSBirdPartySettings>()->bEnableCameraObstructionAvoidance);

	float Distance = Filter.Update(true, 500.0f, 850.0f, false, 1.0f / 60.0f, Settings);
	TestTrue(TEXT("Hard obstruction clamps in the first frame"), FMath::IsNearlyEqual(Distance, 500.0f));
	TestTrue(TEXT("First contact is enter-pending"), Filter.GetPhase() == EABTSM4CameraObstructionPhase::EnterPending);
	Filter.Update(true, 500.0f, 850.0f, false, 1.0f / 60.0f, Settings);
	Filter.Update(true, 500.0f, 850.0f, false, 1.0f / 60.0f, Settings);
	TestTrue(TEXT("Persistent contact becomes obstructed"), Filter.GetPhase() == EABTSM4CameraObstructionPhase::Obstructed);

	Distance = Filter.Update(true, 700.0f, 850.0f, true, 1.0f / 60.0f, Settings);
	TestTrue(TEXT("A swept alternate candidate expands immediately without a speed limit"), FMath::IsNearlyEqual(Distance, 700.0f));
	Distance = Filter.Update(false, 850.0f, 850.0f, false, 0.05f, Settings);
	TestTrue(TEXT("Clear-side zoom-out restores the requested distance immediately"), FMath::IsNearlyEqual(Distance, 850.0f));
	TestTrue(TEXT("Short clear interval is exit-pending"), Filter.GetPhase() == EABTSM4CameraObstructionPhase::ExitPending);

	for (int32 Step = 0; Step < 5; ++Step)
	{
		Distance = Filter.Update(false, 850.0f, 850.0f, false, 0.05f, Settings);
		TestTrue(TEXT("Clear-side distance remains at the requested zoom"), FMath::IsNearlyEqual(Distance, 850.0f));
	}
	TestTrue(TEXT("Exit delay eventually returns to clear"), Filter.GetPhase() == EABTSM4CameraObstructionPhase::Clear);

	Distance = Filter.Update(true, 480.0f, 850.0f, false, 1.0f / 120.0f, Settings);
	TestTrue(TEXT("A new obstruction always overrides recovery"), FMath::IsNearlyEqual(Distance, 480.0f));
	return true;
}

#endif
