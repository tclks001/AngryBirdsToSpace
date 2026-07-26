// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ABTSChaosBirdMovementComponent.generated.h"

class AABTSM2Planet;
class UPrimitiveComponent;

DECLARE_MULTICAST_DELEGATE_ThreeParams(FABTSChaosBlockingImpactDelegate, const FHitResult&, float, const FVector&);

/**
 * Optional M2.5 movement path driven by a Chaos rigid body. The body owns
 * collision/contact; this component only supplies radial gravity and a
 * tangential velocity servo.
 */
UCLASS(ClassGroup = (ABTS), meta = (BlueprintSpawnableComponent))
class ABTSRUNTIME_API UABTSChaosBirdMovementComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UABTSChaosBirdMovementComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void SetChaosEnabled(bool bEnabled);
	/** Debug-only walking multiplier. It affects tangential steering, never launch velocity. */
	void SetDeveloperWalkingSpeedMultiplier(float InMultiplier);
	/** Enables constant-direction gravity for the standalone M7.1 floor. */
	void ConfigurePlanarTestMode(bool bEnabled, const FVector& InPlaneOrigin, const FVector& InPlaneUp);
	FVector GetMovementUpAt(const FVector& WorldLocation) const;
	bool IsPlanarTestMode() const { return bPlanarTestMode; }
	void ConfigureCollisionGrounding(float MaxGroundAngleDegrees);
	void SetMoveInput(const FVector& Direction, float Scale);
	void QueueJump();
	void ResetMotionState();
	void ClearControlHandoffState();
	void ClearControlHandoffVelocity();
	bool IsGrounded() const { return bGrounded; }
	const FVector& GetPendingMoveVector() const { return PendingMoveVector; }
	void BeginBallisticFlight(const FVector& InitialVelocity, float InAirDragPerSecond);
	void EndBallisticFlight(bool bResetVelocity);
	void SetVelocity(const FVector& InVelocity);
	FVector GetVelocity() const;
	bool IsBallisticFlight() const { return bBallisticFlight; }
	FABTSChaosBlockingImpactDelegate& OnBlockingImpact() { return BlockingImpact; }

private:
	UFUNCTION()
	void HandlePhysicsHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

	AABTSM2Planet* FindPlanet();
	UPrimitiveComponent* ResolveBody() const;
	void ApplyRadialForces(float DeltaTime);
	void TryGroundFromHit(const FHitResult& Hit);

	UPROPERTY(EditAnywhere, Category = "ABTS|Chaos Movement|Gravity", meta = (ClampMin = "0.0", UIMax = "3000.0"))
	float GravityAccelerationCMPerSec2 = 980.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|Chaos Movement|Movement", meta = (ClampMin = "0.0", UIMax = "2000.0"))
	float MaxGroundSpeedCMPerSec = 680.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|Chaos Movement|Movement", meta = (ClampMin = "0.0", UIMax = "10000.0"))
	float GroundAccelerationCMPerSec2 = 3600.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|Chaos Movement|Movement", meta = (ClampMin = "0.0", UIMax = "10000.0"))
	float GroundBrakingCMPerSec2 = 4200.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|Chaos Movement|Movement", meta = (ClampMin = "0.0", UIMax = "20.0"))
	float AirDragPerSecond = 0.32f;

	UPROPERTY(EditAnywhere, Category = "ABTS|Chaos Movement|Jump", meta = (ClampMin = "0.0", UIMax = "2000.0"))
	float JumpSpeedCMPerSec = 620.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|Chaos Movement|Contact", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float CollisionGroundMaxAngleDegrees = 55.0f;

	TWeakObjectPtr<AABTSM2Planet> Planet;
	FVector PlanarOrigin = FVector::ZeroVector;
	FVector PlanarUp = FVector::UpVector;
	bool bPlanarTestMode = false;
	FVector PendingMoveVector = FVector::ZeroVector;
	bool bChaosEnabled = false;
	bool bGrounded = false;
	bool bJumpQueued = false;
	bool bBallisticFlight = false;
	float LastGroundContactAgeSeconds = BIG_NUMBER;
	float BallisticAirDragPerSecond = 0.08f;
	float DeveloperWalkingSpeedMultiplier = 1.0f;
	FVector PreviousPhysicsVelocity = FVector::ZeroVector;
	FABTSChaosBlockingImpactDelegate BlockingImpact;
};
