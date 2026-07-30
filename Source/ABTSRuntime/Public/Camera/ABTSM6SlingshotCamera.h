// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "ABTSM6SlingshotCamera.generated.h"

class AABTSM25BirdCharacter;
class AABTSM2Planet;
class AABTSM9Satellite;

UENUM(BlueprintType)
enum class EABTSM9SatelliteFlightCameraPhase : uint8
{
	PrimaryFollow,
	SatelliteApproach,
	SatelliteOrbit,
	E5Approach,
	E5Impact
};

/** Roll-locked aim and projectile-follow camera used only during one M6 launch. */
UCLASS()
class ABTSRUNTIME_API AABTSM6SlingshotCamera : public ACameraActor
{
	GENERATED_BODY()

public:
	AABTSM6SlingshotCamera();
	virtual void Tick(float DeltaSeconds) override;
	void SetAimFrame(const FVector& InCenter, const FVector& InForward, const FVector& InUp);
	/** Copies the authored camera-class defaults used by player input and calibration certification. */
	bool CopyAimFraming(
		float& OutDistanceCM,
		float& OutPitchDegrees,
		float& OutTargetForwardDistanceCM,
		float& OutTargetHeightCM) const;
	/** Returns the exact plane/basis consumed by UpdateAimFromCursor. */
	bool BuildAimInputPlaneBasis(
		const FVector& InCenter,
		const FVector& InForward,
		const FVector& InUp,
		FVector& OutPlaneNormal,
		FVector& OutInPlaneAxis,
		FVector& OutOutOfPlaneAxis) const;
	void FollowBird(AABTSM25BirdCharacter* InBird, AABTSM2Planet* InPlanet);
	void FollowBirdPlanar(AABTSM25BirdCharacter* InBird, const FVector& InPlanarUp);
	void ConfigureSatelliteFlightPresentation(
		AABTSM9Satellite* InSatellite,
		AActor* InE5Target);
	void ClearSatelliteFlightPresentation();
	void NotifySatelliteE5Hit();
	void BeginReturnToPrimaryFrame();
	EABTSM9SatelliteFlightCameraPhase GetSatelliteFlightPhase() const
	{
		return SatelliteFlightPhase;
	}

private:
	void UpdateAim(float DeltaSeconds);
	void UpdateFollow(float DeltaSeconds);
	bool UpdateSatelliteFollow(
		AABTSM25BirdCharacter& TargetBird,
		float DeltaSeconds);
	void SetSatelliteFlightPhase(
		EABTSM9SatelliteFlightCameraPhase NewPhase);
	bool BuildAimView(
		const FVector& InCenter,
		const FVector& InForward,
		const FVector& InUp,
		FVector& OutLocation,
		FVector& OutLook,
		FVector& OutScreenUp) const;

	TWeakObjectPtr<AABTSM25BirdCharacter> Bird;
	TWeakObjectPtr<AABTSM2Planet> Planet;
	FVector AimCenter = FVector::ZeroVector;
	FVector AimForward = FVector::ForwardVector;
	FVector AimUp = FVector::UpVector;
	FVector PlanarFollowUp = FVector::UpVector;
	bool bPlanarFollow = false;
	bool bFollowBird = false;
	TWeakObjectPtr<AABTSM9Satellite> Satellite;
	TWeakObjectPtr<AActor> E5Target;
	EABTSM9SatelliteFlightCameraPhase SatelliteFlightPhase =
		EABTSM9SatelliteFlightCameraPhase::PrimaryFollow;
	FVector SatelliteOrbitViewNormal = FVector::ZeroVector;
	bool bSatelliteE5Hit = false;
	/** Return flight must remain in the primary frame until the next launch. */
	bool bForcePrimaryFrameUntilNextFollow = false;

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

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "2.0", ClampMax = "10.0"))
	float SatelliteApproachEnterRadiusMultiplier = 4.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "2.0", ClampMax = "12.0"))
	float SatelliteApproachExitRadiusMultiplier = 4.8f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "1.1", ClampMax = "5.0"))
	float SatelliteOrbitEnterRadiusMultiplier = 2.3f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "1.1", ClampMax = "6.0"))
	float SatelliteOrbitExitRadiusMultiplier = 2.7f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "100.0", ClampMax = "10000.0", Units = "cm"))
	float SatelliteSideViewDistanceCM = 2600.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "0.0", ClampMax = "3000.0", Units = "cm"))
	float SatelliteSideViewHeightCM = 260.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "100.0", ClampMax = "10000.0", Units = "cm"))
	float SatelliteE5ApproachDistanceCM = 1900.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "100.0", ClampMax = "12000.0", Units = "cm"))
	float SatelliteE5ApproachExitDistanceCM = 2300.0f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SatelliteFocusBias = 0.42f;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9|Satellite Flight",
		meta = (ClampMin = "0.1", ClampMax = "20.0"))
	float SatelliteFollowBlendSpeed = 3.5f;
};

