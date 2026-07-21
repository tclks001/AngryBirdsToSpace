// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ABTSM2SphericalSurfaceComponent.generated.h"

class AABTSM2Planet;
class ACharacter;

/**
 * Keeps a character on an M2 sphere through deterministic surface projection.
 * This is not gravity or physics; it only supplies the radial frame required by M2.
 */
UCLASS(ClassGroup = (ABTS), meta = (BlueprintSpawnableComponent))
class ABTSRUNTIME_API UABTSM2SphericalSurfaceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UABTSM2SphericalSurfaceComponent();

	void SetSurfaceOffsetCM(float InSurfaceOffsetCM);
	void SetProjectToBaseSurface(bool bInProjectToBaseSurface);
	bool UpdateSurfaceFrame();
	void SetMovementFacing(const FVector& WorldDirection);
	void AddCameraYaw(float InputValue);
	void AddCameraPitch(float InputValue);

	FVector GetTangentForward() const { return CameraForwardTangent; }
	FVector GetTangentRight() const;
	FVector GetDownDirection() const { return -RadialUp; }
	bool IsSurfaceFrameReady() const { return Planet != nullptr && !RadialUp.IsNearlyZero(); }

private:
	AABTSM2Planet* FindPlanet();
	FVector ProjectToTangent(const FVector& Candidate, const FVector& Fallback) const;
	void ApplyActorFrame(ACharacter& Character);
	void ApplyCameraFrame(ACharacter& Character);

	UPROPERTY(EditAnywhere, Category = "ABTS|M2|Surface", meta = (ClampMin = "0.0"))
	float SurfaceOffsetCM = 60.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M2|Camera", meta = (ClampMin = "-70.0", ClampMax = "10.0"))
	float InitialCameraPitchDegrees = -18.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M2|Camera", meta = (ClampMin = "0.1"))
	float CameraYawDegreesPerInput = 1.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M2|Camera", meta = (ClampMin = "0.1"))
	float CameraPitchDegreesPerInput = 0.7f;

	TWeakObjectPtr<AABTSM2Planet> Planet;
	FVector RadialUp = FVector::UpVector;
	FVector ActorForwardTangent = FVector::ForwardVector;
	FVector CameraForwardTangent = FVector::ForwardVector;
	float CameraPitchDegrees = -18.0f;
	bool bInitialized = false;
	bool bProjectToBaseSurface = true;
};
