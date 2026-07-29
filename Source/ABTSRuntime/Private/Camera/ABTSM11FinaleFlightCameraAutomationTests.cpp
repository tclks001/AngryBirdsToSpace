// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/ABTSM11FinaleFlightCamera.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11CFlightCameraAuthorityFrameTest,
	"ABTS.M11C.Unit.FlightCameraAuthorityFrame",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11CFlightCameraAuthorityFrameTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FVector Target(100.0, 200.0, 300.0);
	FABTSM11FinaleFlightCameraFrame Frame;
	TestTrue(
		TEXT("Initial authority tangent builds a usable frame"),
		ABTSM11FinaleFlightCameraMath::BuildDesiredFrame(
			Target,
			FVector::ForwardVector,
			FVector::UpVector,
			FVector::ZeroVector,
			FVector::ZeroVector,
			false,
			920.0,
			310.0,
			80.0,
			80.0,
			Frame));
	TestTrue(
		TEXT("Initial camera location is behind and above authority sample"),
		Frame.DesiredTransform.GetLocation().Equals(
			Target
				- FVector::ForwardVector * 920.0
				+ FVector::UpVector * 310.0,
			1.0e-3));
	TestTrue(
		TEXT("Initial transported Up is orthogonal to tangent"),
		FMath::Abs(FVector::DotProduct(
			Frame.TrajectoryForward,
			Frame.TransportedUp)) <= 1.0e-5);

	FVector PreviousForward = Frame.TrajectoryForward;
	FVector PreviousUp = Frame.TransportedUp;
	for (int32 Index = 1; Index <= 90; ++Index)
	{
		const double Alpha = static_cast<double>(Index) / 90.0;
		const double YawRadians =
			FMath::DegreesToRadians(100.0 * Alpha);
		const double PitchRadians =
			FMath::DegreesToRadians(35.0 * Alpha);
		const FVector Tangent(
			FMath::Cos(PitchRadians) * FMath::Cos(YawRadians),
			FMath::Cos(PitchRadians) * FMath::Sin(YawRadians),
			FMath::Sin(PitchRadians));
		FABTSM11FinaleFlightCameraFrame Next;
		if (!TestTrue(
			TEXT("Every curved authority sample builds a finite frame"),
			ABTSM11FinaleFlightCameraMath::BuildDesiredFrame(
				Target + Tangent * Index * 100.0,
				Tangent,
				FVector::UpVector,
				PreviousForward,
				PreviousUp,
				true,
				920.0,
				310.0,
				80.0,
				80.0,
				Next)))
		{
			return false;
		}
		TestTrue(
			TEXT("Parallel-transported Up stays orthogonal"),
			FMath::Abs(FVector::DotProduct(
				Next.TrajectoryForward,
				Next.TransportedUp)) <= 1.0e-4);
		TestTrue(
			TEXT("Parallel transport does not flip camera Up"),
			FVector::DotProduct(
				PreviousUp,
				Next.TransportedUp) > 0.0);
		PreviousForward = Next.TrajectoryForward;
		PreviousUp = Next.TransportedUp;
	}

	FABTSM11FinaleFlightCameraFrame Rejected;
	TestFalse(
		TEXT("A zero authority tangent is rejected"),
		ABTSM11FinaleFlightCameraMath::BuildDesiredFrame(
			Target,
			FVector::ZeroVector,
			FVector::UpVector,
			PreviousForward,
			PreviousUp,
			true,
			920.0,
			310.0,
			80.0,
			80.0,
			Rejected));
	return true;
}

#endif
