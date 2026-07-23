// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "ABTSM6SlingshotCamera.generated.h"

class AABTSM25BirdCharacter;
class AABTSM2Planet;

/** Roll-locked aim and projectile-follow camera used only during one M6 launch. */
UCLASS()
class ABTSRUNTIME_API AABTSM6SlingshotCamera : public ACameraActor
{
	GENERATED_BODY()

public:
	AABTSM6SlingshotCamera();
	virtual void Tick(float DeltaSeconds) override;
	void SetAimFrame(const FVector& InCenter, const FVector& InForward, const FVector& InUp);
	void FollowBird(AABTSM25BirdCharacter* InBird, AABTSM2Planet* InPlanet);

private:
	void UpdateAim(float DeltaSeconds);
	void UpdateFollow(float DeltaSeconds);

	TWeakObjectPtr<AABTSM25BirdCharacter> Bird;
	TWeakObjectPtr<AABTSM2Planet> Planet;
	FVector AimCenter = FVector::ZeroVector;
	FVector AimForward = FVector::ForwardVector;
	FVector AimUp = FVector::UpVector;
	bool bFollowBird = false;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Aim")
	float AimDistanceCM = 1150.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Aim")
	float AimCameraHeightCM = 150.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Aim")
	float AimLookHeightCM = 245.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Flight")
	float FlightDistanceCM = 920.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Flight")
	float FlightHeightCM = 310.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Flight")
	float FollowSpeed = 7.0f;
};

