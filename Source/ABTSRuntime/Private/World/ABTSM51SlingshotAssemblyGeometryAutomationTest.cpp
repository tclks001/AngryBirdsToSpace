// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Slingshot/ABTSM6CordConnectionRules.h"

#include <limits>

namespace
{
FABTSM6CordConnectionQuery MakeClearGeometryQuery()
{
	FABTSM6CordConnectionQuery Query;
	Query.EndpointA = FVector(0.0, 0.0, 0.0);
	Query.EndpointB = FVector(100.0, 0.0, 0.0);
	Query.MaxCordLengthCM = 100.0f;
	Query.CandidateCordRadiusCM = 2.0f;
	Query.ClearanceCM = 1.0f;
	return Query;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM51SlingshotAssemblyGeometryTest,
	"ABTS.M51.SlingshotAssembly.Geometry",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM51SlingshotAssemblyGeometryTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const auto ExpectReason = [this](
		const TCHAR* Label,
		const FABTSM6CordConnectionQuery& Query,
		const EABTSM6CordConnectionRejectReason Expected)
	{
		const FABTSM6CordConnectionResult Result =
			FABTSM6CordConnectionRules::Evaluate(Query);
		TestEqual(
			Label,
			static_cast<int32>(Result.RejectReason),
			static_cast<int32>(Expected));
	};

	const FABTSM6CordConnectionQuery Clear =
		MakeClearGeometryQuery();
	ExpectReason(
		TEXT("A clear segment exactly at the maximum length is accepted"),
		Clear,
		EABTSM6CordConnectionRejectReason::None);

	FABTSM6CordConnectionQuery TooLong = Clear;
	TooLong.EndpointB.X += 0.1;
	ExpectReason(
		TEXT("A segment beyond the maximum length is rejected"),
		TooLong,
		EABTSM6CordConnectionRejectReason::ExceedsMaximumLength);

	FABTSM6CordConnectionQuery StakeBlocked = Clear;
	StakeBlocked.StakeObstacles.Add(
		{FVector(50.0, -10.0, 0.0),
		 FVector(50.0, 10.0, 0.0),
		 5.0f});
	ExpectReason(
		TEXT("A third stake crossing the candidate is rejected"),
		StakeBlocked,
		EABTSM6CordConnectionRejectReason::StakeObstacleBlocked);

	FABTSM6CordConnectionQuery StakeAtBoundary = Clear;
	StakeAtBoundary.StakeObstacles.Add(
		{FVector(50.0, 8.0, -10.0),
		 FVector(50.0, 8.0, 10.0),
		 5.0f});
	ExpectReason(
		TEXT("A third stake touching the clearance boundary is rejected"),
		StakeAtBoundary,
		EABTSM6CordConnectionRejectReason::StakeObstacleBlocked);

	FABTSM6CordConnectionQuery StakeNearMiss = Clear;
	StakeNearMiss.StakeObstacles.Add(
		{FVector(50.0, 8.01, -10.0),
		 FVector(50.0, 8.01, 10.0),
		 5.0f});
	ExpectReason(
		TEXT("A deterministic stake near miss outside clearance is accepted"),
		StakeNearMiss,
		EABTSM6CordConnectionRejectReason::None);

	FABTSM6CordConnectionQuery HeightSeparated = Clear;
	HeightSeparated.EndpointA.Z = 100.0;
	HeightSeparated.EndpointB.Z = 100.0;
	HeightSeparated.StakeObstacles.Add(
		{FVector(50.0, 0.0, 0.0),
		 FVector(50.0, 0.0, 80.0),
		 5.0f});
	ExpectReason(
		TEXT("Three-dimensional height separation is respected"),
		HeightSeparated,
		EABTSM6CordConnectionRejectReason::None);

	FABTSM6CordConnectionQuery CordCrossing = Clear;
	CordCrossing.CordObstacles.Add(
		{FVector(50.0, -10.0, 0.0),
		 FVector(50.0, 10.0, 0.0),
		 2.0f});
	ExpectReason(
		TEXT("An existing crossing cord is rejected"),
		CordCrossing,
		EABTSM6CordConnectionRejectReason::CordObstacleBlocked);

	FABTSM6CordConnectionQuery CordBoundary = Clear;
	CordBoundary.CordObstacles.Add(
		{FVector(0.0, 5.0, 0.0),
		 FVector(100.0, 5.0, 0.0),
		 2.0f});
	ExpectReason(
		TEXT("An existing cord touching the clearance boundary is rejected"),
		CordBoundary,
		EABTSM6CordConnectionRejectReason::CordObstacleBlocked);

	FABTSM6CordConnectionQuery CordNearMiss = Clear;
	CordNearMiss.CordObstacles.Add(
		{FVector(0.0, 5.01, 0.0),
		 FVector(100.0, 5.01, 0.0),
		 2.0f});
	ExpectReason(
		TEXT("An existing cord just outside clearance is accepted"),
		CordNearMiss,
		EABTSM6CordConnectionRejectReason::None);

	FABTSM6CordConnectionQuery NonFinite = Clear;
	NonFinite.EndpointA.X =
		std::numeric_limits<double>::quiet_NaN();
	ExpectReason(
		TEXT("NaN input fails closed"),
		NonFinite,
		EABTSM6CordConnectionRejectReason::InvalidInput);

	FABTSM6CordConnectionQuery Infinite = Clear;
	Infinite.MaxCordLengthCM =
		std::numeric_limits<float>::infinity();
	ExpectReason(
		TEXT("Infinite scalar input fails closed"),
		Infinite,
		EABTSM6CordConnectionRejectReason::InvalidInput);

	FABTSM6CordConnectionQuery Degenerate = Clear;
	Degenerate.EndpointB = Degenerate.EndpointA;
	ExpectReason(
		TEXT("A degenerate candidate segment is rejected"),
		Degenerate,
		EABTSM6CordConnectionRejectReason::DegenerateCandidate);

	FABTSM6CordConnectionQuery DegenerateObstacle = Clear;
	DegenerateObstacle.CordObstacles.Add(
		{FVector(10.0, 10.0, 10.0),
		 FVector(10.0, 10.0, 10.0),
		 2.0f});
	ExpectReason(
		TEXT("A degenerate existing cord fails closed"),
		DegenerateObstacle,
		EABTSM6CordConnectionRejectReason::InvalidCordObstacle);
	return true;
}

#endif
