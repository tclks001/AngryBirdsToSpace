// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/ABTSM6SlingshotCamera.h"

#include "Camera/CameraComponent.h"
#include "Planet/ABTSM2Planet.h"
#include "Player/ABTSM25BirdCharacter.h"

AABTSM6SlingshotCamera::AABTSM6SlingshotCamera()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	GetCameraComponent()->SetFieldOfView(50.0f);
}

void AABTSM6SlingshotCamera::SetAimFrame(const FVector& InCenter, const FVector& InForward, const FVector& InUp)
{
	AimCenter = InCenter;
	AimUp = InUp.GetSafeNormal();
	AimForward = FVector::VectorPlaneProject(InForward, AimUp).GetSafeNormal();
	if (AimForward.IsNearlyZero()) AimForward = FVector::ForwardVector;
	bFollowBird = false;
	UpdateAim(0.0f);
}

void AABTSM6SlingshotCamera::FollowBird(AABTSM25BirdCharacter* InBird, AABTSM2Planet* InPlanet)
{
	Bird = InBird;
	Planet = InPlanet;
	bPlanarFollow = false;
	bFollowBird = true;
}

void AABTSM6SlingshotCamera::FollowBirdPlanar(AABTSM25BirdCharacter* InBird, const FVector& InPlanarUp)
{
	Bird = InBird;
	Planet.Reset();
	PlanarFollowUp = InPlanarUp.GetSafeNormal();
	if (PlanarFollowUp.IsNearlyZero()) PlanarFollowUp = FVector::UpVector;
	bPlanarFollow = true;
	bFollowBird = true;
}

void AABTSM6SlingshotCamera::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFollowBird) UpdateFollow(DeltaSeconds); else UpdateAim(DeltaSeconds);
}

void AABTSM6SlingshotCamera::UpdateAim(const float DeltaSeconds)
{
	// Use only the cord frame captured on launch-mode entry. Pulling the pouch
	// must not rotate or translate the camera around the slingshot.
	const float PitchRadians = FMath::DegreesToRadians(AimPitchDegrees);
	const FVector BackAndUp = (-AimForward * FMath::Cos(PitchRadians) + AimUp * FMath::Sin(PitchRadians)).GetSafeNormal();
	const FVector DesiredLocation = AimCenter + BackAndUp * AimDistanceCM;
	const FVector Target = AimCenter + AimForward * AimTargetForwardDistanceCM + AimUp * AimTargetHeightCM;
	const FVector Look = (Target - DesiredLocation).GetSafeNormal();
	const FVector ScreenUp = FVector::VectorPlaneProject(AimUp, Look).GetSafeNormal();
	const FQuat Rotation = FRotationMatrix::MakeFromXZ(Look, ScreenUp).ToQuat();
	SetActorLocationAndRotation(DeltaSeconds > 0.0f ? FMath::VInterpTo(GetActorLocation(), DesiredLocation, DeltaSeconds, AimCameraBlendSpeed) : DesiredLocation, Rotation);
}

void AABTSM6SlingshotCamera::UpdateFollow(const float DeltaSeconds)
{
	AABTSM25BirdCharacter* TargetBird = Bird.Get();
	AABTSM2Planet* TargetPlanet = Planet.Get();
	if (TargetBird == nullptr || (!bPlanarFollow && TargetPlanet == nullptr)) return;
	const FVector Up = bPlanarFollow ? PlanarFollowUp : TargetPlanet->GetRadialUpAtWorldLocation(TargetBird->GetActorLocation());
	FVector Forward = FVector::VectorPlaneProject(TargetBird->GetSlingshotVelocity(), Up).GetSafeNormal();
	if (Forward.IsNearlyZero()) Forward = FVector::VectorPlaneProject(TargetBird->GetActorForwardVector(), Up).GetSafeNormal();
	const FVector DesiredLocation = TargetBird->GetActorLocation() - Forward * FlightDistanceCM + Up * FlightHeightCM;
	const FVector Look = (TargetBird->GetActorLocation() + Up * 80.0f - DesiredLocation).GetSafeNormal();
	FVector ScreenUp = FVector::VectorPlaneProject(Up, Look).GetSafeNormal();
	if (ScreenUp.IsNearlyZero()) ScreenUp = Up;
	const FVector Location = FMath::VInterpTo(GetActorLocation(), DesiredLocation, DeltaSeconds, FollowSpeed);
	const FQuat Rotation = FMath::QInterpTo(GetActorQuat(), FRotationMatrix::MakeFromXZ(Look, ScreenUp).ToQuat(), DeltaSeconds, FollowSpeed);
	SetActorLocationAndRotation(Location, Rotation);
}

