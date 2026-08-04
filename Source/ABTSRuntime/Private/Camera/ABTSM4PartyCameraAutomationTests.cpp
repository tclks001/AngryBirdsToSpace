// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/ABTSM4CameraRigModel.h"
#include "Misc/AutomationTest.h"

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
	Settings.RestoreSpeedCMPerSecond = 500.0f;
	Settings.EscapeExpansionSpeedCMPerSecond = 900.0f;
	Filter.Reset(850.0f);

	float Distance = Filter.Update(true, 500.0f, 850.0f, false, 1.0f / 60.0f, Settings);
	TestTrue(TEXT("Hard obstruction clamps in the first frame"), FMath::IsNearlyEqual(Distance, 500.0f));
	TestTrue(TEXT("First contact is enter-pending"), Filter.GetPhase() == EABTSM4CameraObstructionPhase::EnterPending);
	Filter.Update(true, 500.0f, 850.0f, false, 1.0f / 60.0f, Settings);
	Filter.Update(true, 500.0f, 850.0f, false, 1.0f / 60.0f, Settings);
	TestTrue(TEXT("Persistent contact becomes obstructed"), Filter.GetPhase() == EABTSM4CameraObstructionPhase::Obstructed);

	Distance = Filter.Update(true, 700.0f, 850.0f, true, 1.0f / 60.0f, Settings);
	TestTrue(TEXT("A swept alternate candidate expands at a bounded rate"), Distance > 500.0f && Distance < 700.0f);
	const float HeldDistance = Distance;
	Distance = Filter.Update(false, 850.0f, 850.0f, false, 0.05f, Settings);
	TestTrue(TEXT("Short clear interval holds the previous distance"), FMath::IsNearlyEqual(Distance, HeldDistance));
	TestTrue(TEXT("Short clear interval is exit-pending"), Filter.GetPhase() == EABTSM4CameraObstructionPhase::ExitPending);

	float PreviousDistance = Distance;
	for (int32 Step = 0; Step < 5; ++Step)
	{
		Distance = Filter.Update(false, 850.0f, 850.0f, false, 0.05f, Settings);
		TestTrue(TEXT("Clear-side recovery is monotonic"), Distance + KINDA_SMALL_NUMBER >= PreviousDistance);
		PreviousDistance = Distance;
	}
	TestTrue(TEXT("Exit delay eventually returns to clear"), Filter.GetPhase() == EABTSM4CameraObstructionPhase::Clear);

	Distance = Filter.Update(true, 480.0f, 850.0f, false, 1.0f / 120.0f, Settings);
	TestTrue(TEXT("A new obstruction always overrides recovery"), FMath::IsNearlyEqual(Distance, 480.0f));
	return true;
}

#endif
