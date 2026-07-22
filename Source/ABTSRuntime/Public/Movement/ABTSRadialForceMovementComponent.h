// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ABTSRadialForceMovementComponent.generated.h"

class AABTSM2Planet;
class ACharacter;
class UABTSRadialSurfaceSuspensionComponent;

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
	void QueueJump();
	void ResetMotionState();
	bool IsGrounded() const;

private:
	AABTSM2Planet* FindPlanet();
	UABTSRadialSurfaceSuspensionComponent* FindSuspension();
	void SimulateSubstep(ACharacter& Character, AABTSM2Planet& ResolvedPlanet, float DeltaTime);
	void MoveIgnoringTerrain(ACharacter& Character, const AABTSM2Planet& ResolvedPlanet, const FVector& RequestedDelta);
	void ResolveBlockingHit(const FHitResult& Hit);

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

	/** Air drag is intentionally weak and never damps the radial jump velocity. */
	UPROPERTY(EditAnywhere, Category = "ABTS|Force Movement|Drag", meta = (ClampMin = "0.0", UIMax = "5.0"))
	float AirTangentDragPerSecond = 0.18f;

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
};
