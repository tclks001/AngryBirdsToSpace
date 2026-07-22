// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ABTSM25RadialMovementComponent.generated.h"

class AABTSM2Planet;

/** Kinematic radial gravity, sweep collision and jump for M2.5. */
UCLASS(ClassGroup = (ABTS), meta = (BlueprintSpawnableComponent))
class ABTSRUNTIME_API UABTSM25RadialMovementComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UABTSM25RadialMovementComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void SetMoveInput(const FVector& Direction, float Scale);
	void QueueJump();
	void ResetMotionState();
	bool IsGrounded() const { return bGrounded; }

private:
	AABTSM2Planet* FindPlanet();
	void IntegrateMotion(float DeltaTime);
	void ResolveBaseSphereContact();
	void ResolveBlockingHit(const FHitResult& Hit);

	UPROPERTY(EditAnywhere, Category = "ABTS|M2.5|Gravity", meta = (ClampMin = "0.0"))
	float GravityAccelerationCMPerSec2 = 980.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M2.5|Movement", meta = (ClampMin = "0.0"))
	float GroundAccelerationCMPerSec2 = 3600.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M2.5|Movement", meta = (ClampMin = "0.0"))
	float MaxGroundSpeedCMPerSec = 680.0f;

	/** Tangential deceleration while grounded and receiving no movement input. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M2.5|Movement", meta = (ClampMin = "0.0"))
	float GroundBrakingCMPerSec2 = 4200.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M2.5|Movement", meta = (ClampMin = "0.0"))
	float AirControlScale = 0.35f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M2.5|Jump", meta = (ClampMin = "0.0"))
	float JumpSpeedCMPerSec = 620.0f;

	/** Keeps a recent jump press alive while contact state settles between ticks. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M2.5|Jump", meta = (ClampMin = "0.0"))
	float JumpBufferSeconds = 0.15f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M2.5|Collision", meta = (ClampMin = "0.0"))
	float GroundSnapToleranceCM = 8.0f;

	/** Outward radial speed required to leave the grounded state. Avoids false airborne states while moving along a curved surface. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M2.5|Collision", meta = (ClampMin = "0.0"))
	float UngroundSpeedCMPerSec = 120.0f;

	TWeakObjectPtr<AABTSM2Planet> Planet;
	FVector Velocity = FVector::ZeroVector;
	FVector PendingMoveVector = FVector::ZeroVector;
	bool bGrounded = false;
	bool bLoggedNoReadyPlanet = false;
	float JumpBufferRemainingSeconds = 0.0f;
};
