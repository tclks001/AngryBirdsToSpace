// Copyright Epic Games, Inc. All Rights Reserved.

#include "Planet/ABTSM2SphericalSurfaceComponent.h"

#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "Planet/ABTSM2Planet.h"
#include "Planet/ABTSPrimaryPlanetMovementAuthority.h"

UABTSM2SphericalSurfaceComponent::UABTSM2SphericalSurfaceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UABTSM2SphericalSurfaceComponent::SetSurfaceOffsetCM(const float InSurfaceOffsetCM)
{
	SurfaceOffsetCM = FMath::Max(0.0f, InSurfaceOffsetCM);
}

void UABTSM2SphericalSurfaceComponent::SetProjectToBaseSurface(const bool bInProjectToBaseSurface)
{
	bProjectToBaseSurface = bInProjectToBaseSurface;
}

bool UABTSM2SphericalSurfaceComponent::UpdateSurfaceFrame()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	AABTSM2Planet* ResolvedPlanet = FindPlanet();
	if (Character == nullptr || ResolvedPlanet == nullptr || !ResolvedPlanet->IsPlanetReady())
	{
		return false;
	}

	const FVector PlanetCenter = ResolvedPlanet->GetPlanetCenterWorld();
	const FVector CurrentLocation = Character->GetActorLocation();
	RadialUp = ResolvedPlanet->GetRadialUpAtWorldLocation(CurrentLocation);
	if (bProjectToBaseSurface)
	{
		const FVector SurfaceLocation = PlanetCenter + RadialUp * (ResolvedPlanet->GetPlanetRadiusCM() + SurfaceOffsetCM);
		Character->SetActorLocation(SurfaceLocation, false, nullptr, ETeleportType::TeleportPhysics);
	}

	ActorForwardTangent = ProjectToTangent(ActorForwardTangent, Character->GetActorForwardVector());
	CameraForwardTangent = ProjectToTangent(CameraForwardTangent, ActorForwardTangent);
	if (bApplyActorFrame) ApplyActorFrame(*Character);
	ApplyCameraFrame(*Character);
	bInitialized = true;
	return true;
}

void UABTSM2SphericalSurfaceComponent::SetMovementFacing(const FVector& WorldDirection)
{
	ActorForwardTangent = ProjectToTangent(WorldDirection, ActorForwardTangent);
}

void UABTSM2SphericalSurfaceComponent::AddCameraYaw(const float InputValue)
{
	if (FMath::IsNearlyZero(InputValue))
	{
		return;
	}

	CameraForwardTangent = CameraForwardTangent.RotateAngleAxis(InputValue * CameraYawDegreesPerInput, RadialUp).GetSafeNormal();
	CameraForwardTangent = ProjectToTangent(CameraForwardTangent, ActorForwardTangent);
}

void UABTSM2SphericalSurfaceComponent::AddCameraPitch(const float InputValue)
{
	CameraPitchDegrees = FMath::Clamp(CameraPitchDegrees + InputValue * CameraPitchDegreesPerInput, -70.0f, 10.0f);
}

FVector UABTSM2SphericalSurfaceComponent::GetTangentRight() const
{
	return FVector::CrossProduct(RadialUp, CameraForwardTangent).GetSafeNormal();
}

AABTSM2Planet* UABTSM2SphericalSurfaceComponent::FindPlanet()
{
	return ABTSPrimaryPlanetMovementAuthority::Resolve(GetWorld(), Planet);
}

FVector UABTSM2SphericalSurfaceComponent::ProjectToTangent(const FVector& Candidate, const FVector& Fallback) const
{
	FVector Projected = FVector::VectorPlaneProject(Candidate, RadialUp).GetSafeNormal();
	if (!Projected.IsNearlyZero())
	{
		return Projected;
	}

	Projected = FVector::VectorPlaneProject(Fallback, RadialUp).GetSafeNormal();
	if (!Projected.IsNearlyZero())
	{
		return Projected;
	}

	const FVector Reference = FMath::Abs(RadialUp.Z) < 0.95f ? FVector::UpVector : FVector::ForwardVector;
	return FVector::CrossProduct(Reference, RadialUp).GetSafeNormal();
}

void UABTSM2SphericalSurfaceComponent::ApplyActorFrame(ACharacter& Character)
{
	Character.SetActorRotation(FRotationMatrix::MakeFromXZ(ActorForwardTangent, RadialUp).Rotator());
}

void UABTSM2SphericalSurfaceComponent::ApplyCameraFrame(ACharacter& Character)
{
	AController* Controller = Character.GetController();
	if (Controller == nullptr)
	{
		return;
	}

	const FVector CameraRight = GetTangentRight();
	const FVector LookForward = CameraForwardTangent.RotateAngleAxis(CameraPitchDegrees, CameraRight).GetSafeNormal();
	Controller->SetControlRotation(FRotationMatrix::MakeFromXZ(LookForward, RadialUp).Rotator());
}
