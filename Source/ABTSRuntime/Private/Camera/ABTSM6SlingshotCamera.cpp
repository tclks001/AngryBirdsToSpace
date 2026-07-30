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

bool AABTSM6SlingshotCamera::CopyAimFraming(
	float& OutDistanceCM,
	float& OutPitchDegrees,
	float& OutTargetForwardDistanceCM,
	float& OutTargetHeightCM) const
{
	OutDistanceCM = AimDistanceCM;
	OutPitchDegrees = AimPitchDegrees;
	OutTargetForwardDistanceCM = AimTargetForwardDistanceCM;
	OutTargetHeightCM = AimTargetHeightCM;
	return FMath::IsFinite(OutDistanceCM)
		&& FMath::IsFinite(OutPitchDegrees)
		&& FMath::IsFinite(OutTargetForwardDistanceCM)
		&& FMath::IsFinite(OutTargetHeightCM)
		&& OutDistanceCM >= 100.0f
		&& OutPitchDegrees >= -10.0f
		&& OutPitchDegrees <= 75.0f
		&& OutTargetForwardDistanceCM >= 0.0f;
}

bool AABTSM6SlingshotCamera::BuildAimView(
	const FVector& InCenter,
	const FVector& InForward,
	const FVector& InUp,
	FVector& OutLocation,
	FVector& OutLook,
	FVector& OutScreenUp) const
{
	const FVector SafeUp = InUp.GetSafeNormal();
	const FVector SafeForward =
		FVector::VectorPlaneProject(
			InForward,
			SafeUp).GetSafeNormal();
	if (SafeUp.IsNearlyZero() || SafeForward.IsNearlyZero())
	{
		return false;
	}
	const float PitchRadians = FMath::DegreesToRadians(AimPitchDegrees);
	const FVector BackAndUp =
		(-SafeForward * FMath::Cos(PitchRadians)
			+ SafeUp * FMath::Sin(PitchRadians)).GetSafeNormal();
	OutLocation = InCenter + BackAndUp * AimDistanceCM;
	const FVector Target =
		InCenter
		+ SafeForward * AimTargetForwardDistanceCM
		+ SafeUp * AimTargetHeightCM;
	OutLook = (Target - OutLocation).GetSafeNormal();
	OutScreenUp =
		FVector::VectorPlaneProject(SafeUp, OutLook).GetSafeNormal();
	return !OutLook.IsNearlyZero() && !OutScreenUp.IsNearlyZero();
}

bool AABTSM6SlingshotCamera::BuildAimInputPlaneBasis(
	const FVector& InCenter,
	const FVector& InForward,
	const FVector& InUp,
	FVector& OutPlaneNormal,
	FVector& OutInPlaneAxis,
	FVector& OutOutOfPlaneAxis) const
{
	OutPlaneNormal = FVector::ZeroVector;
	OutInPlaneAxis = FVector::ZeroVector;
	OutOutOfPlaneAxis = FVector::ZeroVector;
	FVector CameraLocation;
	if (!BuildAimView(
		InCenter,
		InForward,
		InUp,
		CameraLocation,
		OutPlaneNormal,
		OutInPlaneAxis))
	{
		return false;
	}
	OutOutOfPlaneAxis =
		FVector::CrossProduct(
			OutInPlaneAxis,
			OutPlaneNormal).GetSafeNormal();
	const FVector PreferredRight =
		FVector::CrossProduct(
			InUp.GetSafeNormal(),
			InForward.GetSafeNormal()).GetSafeNormal();
	if (FVector::DotProduct(
		OutOutOfPlaneAxis,
		PreferredRight) < 0.0f)
	{
		OutOutOfPlaneAxis *= -1.0f;
	}
	return !OutOutOfPlaneAxis.IsNearlyZero();
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
	FVector DesiredLocation;
	FVector Look;
	FVector ScreenUp;
	if (!BuildAimView(
		AimCenter,
		AimForward,
		AimUp,
		DesiredLocation,
		Look,
		ScreenUp))
	{
		return;
	}
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

