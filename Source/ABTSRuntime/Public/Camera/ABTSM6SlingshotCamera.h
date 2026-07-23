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

	/** Distance from the fixed slingshot-frame focus to the launch camera. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Aim", meta = (ClampMin = "100.0", UIMin = "300.0", UIMax = "3000.0"))
	float AimDistanceCM = 1150.0f;
	/** Fixed upward viewing pitch relative to the slingshot tangent plane. Does not follow pouch/aim direction. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Aim", meta = (ClampMin = "-10.0", ClampMax = "75.0", UIMin = "0.0", UIMax = "45.0"))
	float AimPitchDegrees = 18.0f;
	/** Fixed forward offset of the look target, measured along the slingshot launch normal. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Aim", meta = (ClampMin = "0.0", UIMin = "100.0", UIMax = "3000.0"))
	float AimTargetForwardDistanceCM = 900.0f;
	/** Fixed radial lift of the look target; changes framing without changing launch orientation. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Aim", meta = (UIMin = "-500.0", UIMax = "1000.0"))
	float AimTargetHeightCM = 245.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Aim", meta = (ClampMin = "0.0", UIMin = "1.0", UIMax = "20.0"))
	float AimCameraBlendSpeed = 10.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Flight")
	float FlightDistanceCM = 920.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Flight")
	float FlightHeightCM = 310.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Flight")
	float FollowSpeed = 7.0f;
};

