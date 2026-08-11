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

#endif

