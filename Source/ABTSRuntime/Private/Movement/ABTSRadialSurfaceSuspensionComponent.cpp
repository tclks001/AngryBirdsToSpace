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
	Sample.bSupportActive = SupportDisabledRemainingSeconds <= 0.0f
		&& Sample.HeightAboveTargetCM <= FMath::Max(1.0f, SupportCaptureDistanceCM);

	if (Sample.bSupportActive)
	{
		const float AngularFrequency = 2.0f * PI * FMath::Max(0.1f, SpringFrequencyHz);
		const float SpringAcceleration = -FMath::Square(AngularFrequency) * Sample.HeightAboveTargetCM;
		const float DampingAcceleration = -2.0f
			* FMath::Max(0.1f, SpringDampingRatio)
			* AngularFrequency
			* Sample.RadialSpeedCMPerSec;
		// Gravity feed-forward removes the static spring sag at rest.
		Sample.OutwardSupportAccelerationCMPerSec2 = FMath::Clamp(
			FMath::Max(0.0f, GravityAccelerationCMPerSec2) + SpringAcceleration + DampingAcceleration,
			-MaxSupportAccelerationCMPerSec2,
			MaxSupportAccelerationCMPerSec2);
	}

	const bool bWasGrounded = bGrounded;
	const float GroundedDistanceCM = bWasGrounded
		? FMath::Max(GroundedEnterDistanceCM, GroundedExitDistanceCM)
		: FMath::Max(0.0f, GroundedEnterDistanceCM);
	bGrounded = Sample.bSupportActive && FMath::Abs(Sample.HeightAboveTargetCM) <= GroundedDistanceCM;
	Sample.bGrounded = bGrounded;
	if (bWasGrounded != bGrounded)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][ForceSuspension][Ground] %s HeightError=%.2f RadialSpeed=%.2f Support=%d SpringAccel=%.2f"),
			bGrounded ? TEXT("Grounded") : TEXT("Airborne"),
			Sample.HeightAboveTargetCM,
			Sample.RadialSpeedCMPerSec,
			Sample.bSupportActive ? 1 : 0,
			Sample.OutwardSupportAccelerationCMPerSec2);
	}
	return Sample;
}

void UABTSRadialSurfaceSuspensionComponent::NotifyJump()
{
	bGrounded = false;
	SupportDisabledRemainingSeconds = FMath::Max(0.0f, JumpSupportDisableSeconds);
}

void UABTSRadialSurfaceSuspensionComponent::ResetSuspensionState()
{
	bGrounded = false;
	SupportDisabledRemainingSeconds = 0.0f;
}
