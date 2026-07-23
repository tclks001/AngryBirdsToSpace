// Copyright Epic Games, Inc. All Rights Reserved.

#include "Movement/ABTSRadialSurfaceSuspensionComponent.h"

#include "ABTSRuntime.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Planet/ABTSM2Planet.h"

UABTSRadialSurfaceSuspensionComponent::UABTSRadialSurfaceSuspensionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

FABTSRadialSuspensionSample UABTSRadialSurfaceSuspensionComponent::Evaluate(
	const AABTSM2Planet& Planet,
	const ACharacter& Character,
	const FVector& Velocity,
	const float GravityAccelerationCMPerSec2,
	const float DeltaTime)
{
	FABTSRadialSuspensionSample Sample;
	const FVector PlanetCenter = Planet.GetPlanetCenterWorld();
	const FVector CharacterLocation = Character.GetActorLocation();
	Sample.RadialUp = Planet.GetRadialUpAtWorldLocation(CharacterLocation);
	Sample.SurfaceNormal = Planet.GetSurfaceNormalAtDirection(Sample.RadialUp).GetSafeNormal();
	if (Sample.SurfaceNormal.IsNearlyZero() || FVector::DotProduct(Sample.SurfaceNormal, Sample.RadialUp) < 0.0f)
	{
		Sample.SurfaceNormal = Sample.RadialUp;
	}

	const UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
	const float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	const float CylinderHalfHeight = FMath::Max(0.0f, CapsuleHalfHeight - CapsuleRadius);
	const float NormalUpDot = FMath::Max(
		FVector::DotProduct(Sample.SurfaceNormal, Sample.RadialUp),
		FMath::Clamp(MinimumGroundNormalUpDot, 0.05f, 1.0f));
	// The capsule remains radially aligned while the M3 terrain is tilted. A
	// HalfHeight-only offset embeds its lower side in uphill triangles. This is
	// the same support geometry used by the legacy sweep mover.
	const float GroundCenterOffsetCM = CylinderHalfHeight + CapsuleRadius / NormalUpDot;
	Sample.GroundClearanceCM = FMath::Max(5.0f, GroundClearanceCM);
	Sample.MinimumGroundNormalUpDot = FMath::Clamp(MinimumGroundNormalUpDot, 0.05f, 1.0f);
	Sample.DesiredCenterRadiusCM = Planet.GetSurfaceRadiusAtDirection(Sample.RadialUp)
		+ GroundCenterOffsetCM
		+ Sample.GroundClearanceCM;
	const float CurrentRadiusCM = FVector::Distance(CharacterLocation, PlanetCenter);
	Sample.HeightAboveTargetCM = CurrentRadiusCM - Sample.DesiredCenterRadiusCM;
	Sample.RadialSpeedCMPerSec = FVector::DotProduct(Velocity, Sample.RadialUp);

	SupportDisabledRemainingSeconds = FMath::Max(0.0f, SupportDisabledRemainingSeconds - FMath::Max(0.0f, DeltaTime));
	// A fixed timeout is insufficient on slopes: support can re-enable while the
	// bird is still moving outward, and its critically damped spring then erases
	// the jump before the capsule has left the capture band. Keep support fully
	// detached until both the minimum timeout elapsed and outward ascent ended.
	if (bJumpDetachActive
		&& SupportDisabledRemainingSeconds <= 0.0f
		&& Sample.RadialSpeedCMPerSec <= 0.0f)
	{
		bJumpDetachActive = false;
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][ForceSuspension][JumpDetach] Released. HeightError=%.2f RadialSpeed=%.2f"),
			Sample.HeightAboveTargetCM,
			Sample.RadialSpeedCMPerSec);
	}
	Sample.bSupportActive = !bJumpDetachActive
		&& SupportDisabledRemainingSeconds <= 0.0f
		&& Sample.HeightAboveTargetCM <= FMath::Max(1.0f, SupportCaptureDistanceCM);

	if (Sample.bSupportActive)
	{
		const float AngularFrequency = 2.0f * PI * FMath::Max(0.1f, SpringFrequencyHz);
		const float SpringAcceleration = -FMath::Square(AngularFrequency) * Sample.HeightAboveTargetCM;
		const float DampingAcceleration = -2.0f
			* FMath::Max(0.1f, SpringDampingRatio)
			* AngularFrequency
			* Sample.RadialSpeedCMPerSec;
		// Ground support is unilateral: it may push the bird away from the
		// surface, but it must never pull it toward the surface. Gravity already
		// supplies the inward force. Allowing a negative support acceleration
		// created a pull-in/depenetrate loop that repeatedly flipped Grounded.
		Sample.OutwardSupportAccelerationCMPerSec2 = FMath::Clamp(
			FMath::Max(0.0f, GravityAccelerationCMPerSec2) + SpringAcceleration + DampingAcceleration,
			0.0f,
			MaxSupportAccelerationCMPerSec2);
	}

	const bool bWasGrounded = bGrounded;
	const float GroundedDistanceCM = bWasGrounded
		? FMath::Max(GroundedEnterDistanceCM, GroundedExitDistanceCM)
		: FMath::Max(0.0f, GroundedEnterDistanceCM);
	const bool bLaunchingAway = Sample.RadialSpeedCMPerSec > FMath::Max(0.0f, UngroundSpeedCMPerSec);
	// Outward speed may prevent an airborne body from entering contact, but it
	// must not eject an already grounded body. Explicit jumps use NotifyJump;
	// ordinary support overshoot is resolved by the ground velocity constraint.
	const bool bMayEnterOrRemainGrounded = bWasGrounded || !bLaunchingAway;
	bGrounded = Sample.bSupportActive
		&& FMath::Abs(Sample.HeightAboveTargetCM) <= GroundedDistanceCM
		&& bMayEnterOrRemainGrounded;
	Sample.bGrounded = bGrounded;
	if (bWasGrounded != bGrounded)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][ForceSuspension][Ground] %s HeightError=%.2f RadialSpeed=%.2f Support=%d LaunchingAway=%d Unground=%.1f SpringAccel=%.2f"),
			bGrounded ? TEXT("Grounded") : TEXT("Airborne"),
			Sample.HeightAboveTargetCM,
			Sample.RadialSpeedCMPerSec,
			Sample.bSupportActive ? 1 : 0,
			bLaunchingAway ? 1 : 0,
			UngroundSpeedCMPerSec,
			Sample.OutwardSupportAccelerationCMPerSec2);
	}
	return Sample;
}

void UABTSRadialSurfaceSuspensionComponent::NotifyJump()
{
	bGrounded = false;
	bJumpDetachActive = true;
	SupportDisabledRemainingSeconds = FMath::Max(0.0f, JumpSupportDisableSeconds);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][ForceSuspension][JumpDetach] Began. MinimumSeconds=%.3f"),
		SupportDisabledRemainingSeconds);
}

void UABTSRadialSurfaceSuspensionComponent::ResetSuspensionState()
{
	bGrounded = false;
	bJumpDetachActive = false;
	SupportDisabledRemainingSeconds = 0.0f;
}
