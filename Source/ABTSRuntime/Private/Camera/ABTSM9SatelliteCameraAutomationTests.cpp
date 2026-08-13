// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/ABTSM6SlingshotCamera.h"
#include "Slingshot/ABTSM6Types.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM9SatelliteFlightCameraIntentTest,
	"ABTS.M9.Camera.IntentContract",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM9SatelliteFlightCameraIntentTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FABTSM6TrajectoryPreview Preview;
	Preview.WorldPoints = { FVector::ZeroVector, FVector(100.0, 0.0, 0.0) };
	Preview.EncounterSatelliteRadiusCM = 1250.0f;

	TestEqual(
		TEXT("Distance alone cannot opt into a satellite camera"),
		AABTSM6SlingshotCamera::ClassifySatelliteFlightIntent(Preview),
		EABTSM9SatelliteFlightCameraIntent::None);

	Preview.bHasSatelliteEncounter = true;
	Preview.TerminalType = EABTSM6TrajectoryTerminalType::PrimarySurface;
	TestEqual(
		TEXT("A non-E5 satellite assist remains subtle"),
		AABTSM6SlingshotCamera::ClassifySatelliteFlightIntent(Preview),
		EABTSM9SatelliteFlightCameraIntent::SubtleAssist);

	Preview.TerminalType = EABTSM6TrajectoryTerminalType::SatelliteBody;
	TestEqual(
		TEXT("A predicted body contact earns a surface landing frame, not E5 framing"),
		AABTSM6SlingshotCamera::ClassifySatelliteFlightIntent(Preview),
		EABTSM9SatelliteFlightCameraIntent::SurfaceLanding);

	Preview.TerminalType = EABTSM6TrajectoryTerminalType::SatelliteE5;
	TestEqual(
		TEXT("Only an E5 terminal locks the strike camera"),
		AABTSM6SlingshotCamera::ClassifySatelliteFlightIntent(Preview),
		EABTSM9SatelliteFlightCameraIntent::CinematicE5);

	TestEqual(
		TEXT("Distance without an inward contact ray cannot flip the frame"),
		AABTSM6SlingshotCamera::ComputeSatelliteSurfaceFrameTarget(
			EABTSM9SatelliteFlightCameraIntent::CinematicE5,
			100.0f,
			1250.0f,
			-1.0f,
			false),
		0.0f);
	TestTrue(
		TEXT("An imminent predicted contact starts a continuous moon-frame hand-off"),
		AABTSM6SlingshotCamera::ComputeSatelliteSurfaceFrameTarget(
			EABTSM9SatelliteFlightCameraIntent::CinematicE5,
			600.0f,
			1250.0f,
			0.35f,
			false) > 0.5f);
	TestEqual(
		TEXT("Authoritative contact guarantees eventual moon-frame convergence"),
		AABTSM6SlingshotCamera::ComputeSatelliteSurfaceFrameTarget(
			EABTSM9SatelliteFlightCameraIntent::None,
			0.0f,
			1250.0f,
			-1.0f,
			true),
		1.0f);

	TestEqual(
		TEXT("A non-landing assist has no camera authority beyond its exit envelope"),
		AABTSM6SlingshotCamera::ComputeSatelliteSubtleAssistDistanceWeight(
			7900.0f,
			3375.0f,
			7875.0f),
		0.0f);
	TestEqual(
		TEXT("A close lunar fly-by receives full subtle composition weight"),
		AABTSM6SlingshotCamera::ComputeSatelliteSubtleAssistDistanceWeight(
			3000.0f,
			3375.0f,
			7875.0f),
		1.0f);
	const float MidAssistWeight =
		AABTSM6SlingshotCamera::ComputeSatelliteSubtleAssistDistanceWeight(
			5625.0f,
			3375.0f,
			7875.0f);
	TestTrue(
		TEXT("The fly-by influence envelope is continuous between its endpoints"),
		MidAssistWeight > 0.0f && MidAssistWeight < 1.0f);

	const FVector AntipodalBlend =
		AABTSM6SlingshotCamera::BlendSurfaceUpStable(
			FVector::UpVector,
			-FVector::UpVector,
			FVector::ForwardVector,
			0.5f);
	TestTrue(
		TEXT("Near-antipodal primary/moon up blending remains finite and normalized"),
		!AntipodalBlend.ContainsNaN()
			&& FMath::IsNearlyEqual(AntipodalBlend.Size(), 1.0f, 0.001f));

	const FVector LimitedSurfaceUp =
		AABTSM6SlingshotCamera::LimitSurfaceUpAngularStep(
			FVector::UpVector,
			FVector::ForwardVector,
			FVector::RightVector,
			2.0f);
	const float LimitedStepDegrees = FMath::RadiansToDegrees(
		FMath::Acos(FMath::Clamp(
			FVector::DotProduct(FVector::UpVector, LimitedSurfaceUp),
			-1.0f,
			1.0f)));
	TestTrue(
		TEXT("One moon-frame update cannot cut farther than its angular budget"),
		FMath::IsNearlyEqual(LimitedStepDegrees, 2.0f, 0.01f));

	const FQuat LimitedCameraRotation =
		AABTSM6SlingshotCamera::LimitCameraRotationAngularStep(
			FQuat::Identity,
			FQuat(FVector::UpVector, FMath::DegreesToRadians(90.0f)),
			1.5f);
	TestTrue(
		TEXT("One satellite phase update cannot become a rotation cut"),
		FMath::IsNearlyEqual(
			FMath::RadiansToDegrees(
				FQuat::Identity.AngularDistance(LimitedCameraRotation)),
			1.5f,
			0.01f));

	return true;
}

#endif
