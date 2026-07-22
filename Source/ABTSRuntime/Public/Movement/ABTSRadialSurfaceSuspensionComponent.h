// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ABTSRadialSurfaceSuspensionComponent.generated.h"

class AABTSM2Planet;
class ACharacter;

/** One read-only suspension sample consumed by the force movement integrator. */
struct FABTSRadialSuspensionSample
{
	FVector RadialUp = FVector::UpVector;
	FVector SurfaceNormal = FVector::UpVector;
	float DesiredCenterRadiusCM = 0.0f;
	float HeightAboveTargetCM = 0.0f;
	float RadialSpeedCMPerSec = 0.0f;
	float GroundClearanceCM = 5.0f;
	float MinimumGroundNormalUpDot = 0.2f;
	float OutwardSupportAccelerationCMPerSec2 = 0.0f;
	bool bSupportActive = false;
	bool bGrounded = false;
};

/**
 * Converts the CellTopo-derived continuous surface query into a damped radial
 * suspension force. It never moves the owner and never reads render triangles.
 */
UCLASS(ClassGroup = (ABTS), meta = (BlueprintSpawnableComponent))
class ABTSRUNTIME_API UABTSRadialSurfaceSuspensionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UABTSRadialSurfaceSuspensionComponent();

	FABTSRadialSuspensionSample Evaluate(
		const AABTSM2Planet& Planet,
		const ACharacter& Character,
		const FVector& Velocity,
		float GravityAccelerationCMPerSec2,
		float DeltaTime);

	void NotifyJump();
	void ResetSuspensionState();
	bool IsGrounded() const { return bGrounded; }

private:
	/** Collision skin above the queried surface. Keeps a contact capsule out of the complex terrain triangles. */
	UPROPERTY(EditAnywhere, Category = "ABTS|Force Suspension|Ground", meta = (ClampMin = "0.0", UIMax = "20.0"))
	float GroundClearanceCM = 8.0f;

	/** Lowest supported dot(SurfaceNormal, RadialUp), preventing steep visual triangles from producing an unbounded capsule offset. */
	UPROPERTY(EditAnywhere, Category = "ABTS|Force Suspension|Ground", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float MinimumGroundNormalUpDot = 0.2f;

	/** Maximum height above the target radius at which the suspension may capture a falling character. */
	UPROPERTY(EditAnywhere, Category = "ABTS|Force Suspension|Ground", meta = (ClampMin = "1.0", UIMax = "200.0"))
	float SupportCaptureDistanceCM = 45.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|Force Suspension|Ground", meta = (ClampMin = "0.0", UIMax = "50.0"))
	float GroundedEnterDistanceCM = 10.0f;

	/** Grounded-state hysteresis. Must normally be larger than GroundedEnterDistanceCM. */
	UPROPERTY(EditAnywhere, Category = "ABTS|Force Suspension|Ground", meta = (ClampMin = "0.0", UIMax = "100.0"))
	float GroundedExitDistanceCM = 24.0f;

	/** Natural frequency of the radial spring. Higher values track terrain faster but require smaller simulation steps. */
	UPROPERTY(EditAnywhere, Category = "ABTS|Force Suspension|Spring", meta = (ClampMin = "0.1", ClampMax = "20.0", UIMax = "10.0"))
	float SpringFrequencyHz = 4.0f;

	/** 1.0 is critical damping; lower values bounce and higher values respond more slowly. */
	UPROPERTY(EditAnywhere, Category = "ABTS|Force Suspension|Spring", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float SpringDampingRatio = 1.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|Force Suspension|Spring", meta = (ClampMin = "100.0", UIMax = "20000.0"))
	float MaxSupportAccelerationCMPerSec2 = 7000.0f;

	/** Prevents the suspension from immediately catching the character after a jump impulse. */
	UPROPERTY(EditAnywhere, Category = "ABTS|Force Suspension|Jump", meta = (ClampMin = "0.0", UIMax = "0.5"))
	float JumpSupportDisableSeconds = 0.14f;

	bool bGrounded = false;
	float SupportDisabledRemainingSeconds = 0.0f;
};
