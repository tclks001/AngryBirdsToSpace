// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "World/ABTSM11FinaleFormation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11M6FormationArcLengthTest,
	"ABTS.M11C.Unit.M6FormationArcLength",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11M6FormationArcLengthTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FABTSM11PlaybackPlan Plan;
	for (int32 Index = 0; Index <= 2; ++Index)
	{
		FABTSM11PlaybackPoint& Point = Plan.Points.AddDefaulted_GetRef();
		Point.TimeSeconds = Index * 5.0;
		Point.PositionCM = FVector3d(Index * 500.0, 0.0, 0.0);
		Point.VelocityCMPerSec = FVector3d(100.0, 0.0, 0.0);
	}
	Plan.DurationSeconds = 10.0;
	FABTSM11FinaleFormationPath Path;
	FString Failure;
	TestTrue(TEXT("Linear released path builds"), Path.Build(Plan, &Failure));
	TestEqual(TEXT("No formation build failure"), Failure, FString());
	TestTrue(
		TEXT("Dense path preserves linear arc length"),
		FMath::IsNearlyEqual(Path.TotalArcLengthCM, 1000.0, 1.0e-6));
	double ArcAtTime = -1.0;
	TestTrue(
		TEXT("Time resolves to arc length"),
		Path.ResolveArcLengthAtTime(6.25, ArcAtTime));
	TestTrue(
		TEXT("Resolved arc is deterministic"),
		FMath::IsNearlyEqual(ArcAtTime, 625.0, 1.0e-6));
	FVector3d Position;
	FVector3d Velocity;
	TestTrue(
		TEXT("Follower arc sample resolves"),
		Path.SampleAtArcLength(ArcAtTime - 260.0, Position, Velocity));
	TestTrue(
		TEXT("Follower remains exactly one frozen spacing behind"),
		Position.Equals(FVector3d(365.0, 0.0, 0.0), 1.0e-6));
	TestTrue(
		TEXT("Follower tangent remains authority tangent"),
		Velocity.Equals(FVector3d(100.0, 0.0, 0.0), 1.0e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11M6FormationViewRotationTest,
	"ABTS.M11C.Unit.M6FormationViewRotation",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11M6FormationViewRotationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FVector Velocity(3.0, 4.0, 5.0);
	const FVector ViewUp(-0.25, 0.50, 1.0);
	const FVector ViewRight(1.0, 0.0, 0.0);
	FQuat ActorRotation;
	TestTrue(
		TEXT("Velocity/view rotation resolves"),
		ABTSM11FinaleFormationMath::BuildVelocityViewRotation(
			Velocity,
			ViewUp,
			ViewRight,
			FQuat::Identity,
			ActorRotation));
	const FVector ExpectedForward = Velocity.GetSafeNormal();
	const FVector ExpectedUp = FVector::VectorPlaneProject(
		ViewUp,
		ExpectedForward).GetSafeNormal();
	TestTrue(
		TEXT("Actor forward follows trajectory velocity"),
		ActorRotation.GetForwardVector().Equals(ExpectedForward, 1.0e-5));
	TestTrue(
		TEXT("Actor up follows projected current view up"),
		ActorRotation.GetUpVector().Equals(ExpectedUp, 1.0e-5));

	// The production bird mesh imports facing +Y and applies this authored
	// relative correction. Keeping it on the component maps visible front to
	// Actor +X without double-applying the correction in finale code.
	const FQuat DefaultVisualAxisCorrection =
		FRotator(0.0, -90.0, 0.0).Quaternion();
	const FVector VisibleModelForward = ActorRotation.RotateVector(
		DefaultVisualAxisCorrection.RotateVector(FVector::RightVector));
	const FVector VisibleModelUp = ActorRotation.RotateVector(
		DefaultVisualAxisCorrection.RotateVector(FVector::UpVector));
	TestTrue(
		TEXT("Default mesh correction faces visible bird along velocity"),
		VisibleModelForward.Equals(ExpectedForward, 1.0e-5));
	TestTrue(
		TEXT("Default mesh correction preserves view-relative up"),
		VisibleModelUp.Equals(ExpectedUp, 1.0e-5));

	FQuat DegenerateRotation;
	TestTrue(
		TEXT("View-up parallel to velocity uses stable fallback"),
		ABTSM11FinaleFormationMath::BuildVelocityViewRotation(
			FVector::UpVector,
			FVector::UpVector,
			FVector::RightVector,
			ActorRotation,
			DegenerateRotation));
	TestTrue(
		TEXT("Degenerate fallback still preserves velocity forward"),
		DegenerateRotation.GetForwardVector().Equals(
			FVector::UpVector,
			1.0e-5));
	return true;
}

#endif
