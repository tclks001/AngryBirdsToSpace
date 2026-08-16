// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ABTSRadialForceMovementComponent.generated.h"

class AABTSM2Planet;
class ACharacter;
class UABTSRadialSurfaceSuspensionComponent;
struct FABTSRadialSuspensionSample;

DECLARE_MULTICAST_DELEGATE_ThreeParams(FABTSBlockingImpactDelegate, const FHitResult&, float, const FVector&);

/** Force-integrated player movement with CellTopo-derived radial suspension. */
UCLASS(ClassGroup = (ABTS), meta = (BlueprintSpawnableComponent))
class ABTSRUNTIME_API UABTSRadialForceMovementComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UABTSRadialForceMovementComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void SetMoveInput(const FVector& Direction, float Scale);
	/** Debug-only walking multiplier. It affects ground control only, never M6 ballistic velocity. */
	void SetDeveloperWalkingSpeedMultiplier(float InMultiplier);
	void QueueJump();
	void ConfigureCollisionGroundingExperiment(bool bEnabled, float MaxGroundAngleDegrees);
	void ResetMotionState();
	/** Clears follower/player steering momentum without invalidating the current ground-contact cache. */
	void ClearControlHandoffState();
	void ClearControlHandoffInput();
	void ClearControlHandoffVelocity();
	/** Places a newly controlled follower on its CellTopo-derived surface and establishes stable contact. */
	bool StabilizeForGroundedControlHandoff();
	void GrantControlHandoffJumpGrace(float Seconds);
	bool IsGrounded() const;
	void BeginBallisticFlight(const FVector& InitialVelocity, float InFlightAirDragPerSecond);
	void EndBallisticFlight(bool bResetVelocity);
	void SetVelocity(const FVector& InVelocity) { Velocity = InVelocity; }
	const FVector& GetVelocity() const { return Velocity; }
	const FVector& GetPendingMoveVector() const { return PendingMoveVector; }
	float GetJumpBufferRemainingSeconds() const { return JumpBufferRemainingSeconds; }
	float GetControlHandoffJumpGraceRemainingSeconds() const { return ControlHandoffJumpGraceRemainingSeconds; }
	bool IsBallisticFlight() const { return bBallisticFlight; }
	FABTSBlockingImpactDelegate& OnBlockingImpact() { return BlockingImpact; }

private:
	AABTSM2Planet* FindPlanet();
	UABTSRadialSurfaceSuspensionComponent* FindSuspension();
	void SimulateSubstep(ACharacter& Character, AABTSM2Planet& ResolvedPlanet, float DeltaTime);
	bool EnsureGroundClearance(ACharacter& Character, const AABTSM2Planet& ResolvedPlanet, const FABTSRadialSuspensionSample& Surface);
	FVector BuildGroundFollowingDelta(const ACharacter& Character, const AABTSM2Planet& ResolvedPlanet, const FABTSRadialSuspensionSample& Surface, const FVector& RequestedDelta) const;
	void MoveWithCollision(ACharacter& Character, const AABTSM2Planet& ResolvedPlanet, const FVector& RequestedDelta);
	void ResolveBlockingHit(const FHitResult& Hit, const AABTSM2Planet& ResolvedPlanet);
	void TryEstablishCollisionGround(const FHitResult& Hit, const AABTSM2Planet& ResolvedPlanet);

	/** Virtual mass keeps force values meaningful; acceleration remains mass-independent. */
	UPROPERTY(EditAnywhere, Category = "ABTS|Force Movement|Force", meta = (ClampMin = "0.1", UIMax = "200.0"))
	float VirtualMassKG = 20.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|Force Movement|Force", meta = (ClampMin = "0.0", UIMax = "3000.0"))
	float GravityAccelerationCMPerSec2 = 980.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|Force Movement|Force", meta = (ClampMin = "0.0", UIMax = "8000.0"))
	float GroundMoveAccelerationCMPerSec2 = 3600.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|Force Movement|Force", meta = (ClampMin = "0.0", UIMax = "1.0"))
	float AirControlScale = 0.28f;

	/** Linear tangent drag. 3600 / 5.3 gives a terminal ground speed near 680 cm/s. */
	UPROPERTY(EditAnywhere, Category = "ABTS|Force Movement|Drag", meta = (ClampMin = "0.0", UIMax = "20.0"))
	float GroundDragPerSecond = 5.3f;

	/** Extra static/rolling resistance when grounded with no command; prevents directional momentum from masquerading as input. */
	UPROPERTY(EditAnywhere, Category = "ABTS|Force Movement|Drag", meta = (ClampMin = "0.0", UIMax = "40.0"))
	float GroundIdleBrakePerSecond = 14.0f;

	/** Air drag is intentionally weak and never damps the radial jump velocity. */
	UPROPERTY(EditAnywhere, Category = "ABTS|Force Movement|Drag", meta = (ClampMin = "0.0", UIMax = "5.0"))
	float AirTangentDragPerSecond = 0.18f;

	/** Always opposes the complete velocity vector, including airborne radial motion. */
	UPROPERTY(EditAnywhere, Category = "ABTS|Force Movement|Drag", meta = (ClampMin = "0.0", UIMax = "5.0"))
	float AirDragPerSecond = 0.32f;

	/** Minimum incoming normal speed required before a terrain profile may bounce the bird. */
	UPROPERTY(EditAnywhere, Category = "ABTS|Force Movement|Contact", meta = (ClampMin = "0.0", UIMax = "3000.0"))
	float BounceSpeedThresholdCMPerSec = 260.0f;

	/** Ground height following is disabled while falling faster than this; landing must come from the capsule sweep. */
	UPROPERTY(EditAnywhere, Category = "ABTS|Force Movement|Contact", meta = (ClampMin = "0.0", UIMax = "500.0"))
	float MaxGroundFollowDescentSpeedCMPerSec = 35.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|Force Movement|Speed", meta = (ClampMin = "1.0", UIMax = "2000.0"))
	float DesignMaxGroundSpeedCMPerSec = 680.0f;

	/** Safety brake above the drag-defined terminal speed; this is not a hard velocity clamp. */
	UPROPERTY(EditAnywhere, Category = "ABTS|Force Movement|Speed", meta = (ClampMin = "0.0", UIMax = "50.0"))
	float OverspeedDragPerSecond = 12.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|Force Movement|Jump", meta = (ClampMin = "0.0", UIMax = "2000.0"))
	float JumpSpeedCMPerSec = 620.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|Force Movement|Jump", meta = (ClampMin = "0.0", UIMax = "0.5"))
	float JumpBufferSeconds = 0.15f;

	/** Local deterministic substep. 1/120 s is stable for the default 4 Hz suspension spring. */
	UPROPERTY(EditAnywhere, Category = "ABTS|Force Movement|Simulation", meta = (ClampMin = "0.001", ClampMax = "0.033333"))
	float MaxSimulationStepSeconds = 0.008333f;

	UPROPERTY(EditAnywhere, Category = "ABTS|Force Movement|Simulation", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxSimulationSubsteps = 8;

	TWeakObjectPtr<AABTSM2Planet> Planet;
	TWeakObjectPtr<UABTSRadialSurfaceSuspensionComponent> Suspension;
	FVector Velocity = FVector::ZeroVector;
	FVector PendingMoveVector = FVector::ZeroVector;
	float JumpBufferRemainingSeconds = 0.0f;
	bool bLoggedNoDependencies = false;
	bool bLoggedGroundSatelliteGravitySuppressed = false;
	bool bBallisticFlight = false;
	float BallisticFlightAirDragPerSecond = 0.08f;
	float ControlHandoffJumpGraceRemainingSeconds = 0.0f;
	bool bUseCollisionNormalGroundingExperiment = false;
	bool bCollisionGrounded = false;
	float CollisionGroundMaxAngleDegrees = 55.0f;
	float DeveloperWalkingSpeedMultiplier = 1.0f;
	FABTSBlockingImpactDelegate BlockingImpact;
};
