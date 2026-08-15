// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Building/ABTSM7BuildingTypes.h"
#include "ABTSM7BuildingModule.generated.h"

class AABTSM7BuildingMaterialSystem;
class UPhysicalMaterial;
class UStaticMeshComponent;

/**
 * The per-body Chaos identity shared by production modules and M7 stability
 * fixtures. Keep experiment-only tuning out of this profile: changing it
 * changes live building physics as well as the research candidate hash.
 */
struct ABTSRUNTIME_API FABTSM7ChaosBodyProfile final
{
	static constexpr int32 SchemaVersion = 1;

	int32 PositionSolverIterations = 80;
	int32 VelocitySolverIterations = 20;
	float LinearDamping = 2.0f;
	float AngularDamping = 4.0f;

	static FABTSM7ChaosBodyProfile Production();
	bool IsUsable() const;
	uint32 ComputeCrc32() const;
	void ApplyTo(UStaticMeshComponent& Component) const;
};

/** Read-only snapshot of the project/world Chaos stepping identity. */
struct ABTSRUNTIME_API FABTSM7ChaosWorldProfile final
{
	static constexpr int32 SchemaVersion = 1;

	bool bSubstepping = false;
	bool bSubsteppingAsync = false;
	bool bTickPhysicsAsync = false;
	float MaxPhysicsDeltaSeconds = 0.0f;
	float MaxSubstepDeltaSeconds = 0.0f;
	int32 MaximumSubsteps = 1;
	float AsyncFixedDeltaSeconds = 0.0f;
	int32 PositionFrictionIterations = 0;
	int32 PositionShockPropagationIterations = 0;

	static FABTSM7ChaosWorldProfile CaptureProduction();
	uint32 ComputeCrc32() const;
	FString ToLogString() const;
};

/** Parameterized non-HISM M7 module and promoted brick body. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM7BuildingModule : public AActor
{
	GENERATED_BODY()

public:
	AABTSM7BuildingModule();
	virtual void Tick(float DeltaSeconds) override;

	void ConfigureBrick(UStaticMesh* Mesh, UMaterialInterface* Material, EABTSM7BuildingMaterial InMaterial, const FTransform& WorldTransform);
	void ConfigureCylinder(UStaticMesh* Mesh, UMaterialInterface* Material, EABTSM7ModuleKind InKind, EABTSM7BuildingMaterial InMaterial, float LengthCM, float DiameterCM, const FTransform& WorldTransform, const FVector& AdditionalLocalScale = FVector::OneVector);
	/** Uses an exact engine-cylinder collision proxy and a no-collision authored presentation mesh. */
	void ConfigureVoxelDevice(UStaticMesh* CollisionMesh, UStaticMesh* PresentationMesh,
		UMaterialInterface* Material, EABTSM7ModuleKind InKind,
		float LengthCM, float DiameterCM, const FTransform& WorldTransform);
	void ConfigureImpactPhysics(const FABTSM7MaterialProfile& Profile);
	/** Applies a per-body Chaos quality override for multi-contact generated-building stacks. */
	void ConfigureChaosSolverIterations(int32 PositionIterations, int32 VelocityIterations);
	/** Ignores contact damage briefly after a static body enters Chaos. */
	void SetContactDamageGraceSeconds(float Seconds) { ContactDamageGraceSeconds = FMath::Max(0.0f, Seconds); }
	void ActivateDynamic(const FVector& Impulse, const FVector& InPlanetCenter, float GravityAcceleration);
	void ActivateDynamicPlanar(const FVector& Impulse, const FVector& InGravityUp, float GravityAcceleration);
	/** Applies acceleration without invalidating Chaos sleep; false means no usable physics body. */
	static bool TryApplyNonInvalidatingAcceleration(
		UStaticMeshComponent& Component,
		const FVector& AccelerationCMPerSec2);
	void Freeze();
	/** Returns true only for the first successful break request. */
	bool BreakModule();
	bool ApplyImpactDamage(float DamageGain);

	EABTSM7ModuleKind GetModuleKind() const { return ModuleKind; }
	EABTSM7BuildingMaterial GetBuildingMaterial() const { return BuildingMaterial; }
	UStaticMeshComponent* GetMeshComponent() const { return Visual; }
	bool IsDynamic() const { return bDynamic; }
	bool IsBroken() const { return bBroken; }
	float GetCurrentDamage() const { return CurrentDamage; }
	float GetBreakDamage() const { return BreakDamage; }

private:
	UFUNCTION()
	void HandleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M7", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> Visual;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> DevicePresentation;
	bool bBroken = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M7", meta = (AllowPrivateAccess = "true"))
	EABTSM7ModuleKind ModuleKind = EABTSM7ModuleKind::Brick;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M7", meta = (AllowPrivateAccess = "true"))
	EABTSM7BuildingMaterial BuildingMaterial = EABTSM7BuildingMaterial::Wood;

	FVector PlanetCenter = FVector::ZeroVector;
	float GravityAccelerationCMPerSec2 = 980.0f;
	bool bDynamic = false;
	bool bPlanarGravity = false;
	FVector PlanarGravityUp = FVector::UpVector;
	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> ImpactPhysicalMaterial;
	float CurrentDamage = 0.0f;
	float BreakDamage = 100.0f;
	float LastDamageImpactSeconds = -BIG_NUMBER;
	float ContactDamageGraceSeconds = 0.20f;
	float ContactDamageEnabledTimeSeconds = 0.0f;
};
