// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Movement/ABTSRadialForceMovementComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSGroundLocomotionSatelliteGravityGateTest,
	"ABTS.M4.GroundLocomotion.SatelliteGravityStateGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSGroundLocomotionSatelliteGravityGateTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FVector RawSatelliteAcceleration(123.25, -456.5, 789.75);
	TestTrue(
		TEXT("Ground locomotion consumes no satellite acceleration"),
		UABTSRadialForceMovementComponent::ResolveSatelliteAccelerationForMovement(
			false,
			RawSatelliteAcceleration) == FVector::ZeroVector);
	TestTrue(
		TEXT("Ballistic slingshot flight preserves the exact satellite acceleration"),
		UABTSRadialForceMovementComponent::ResolveSatelliteAccelerationForMovement(
			true,
			RawSatelliteAcceleration) == RawSatelliteAcceleration);
	return true;
}

#endif
