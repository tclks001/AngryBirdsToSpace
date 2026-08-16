// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Building/ABTSM7BuildingTypes.h"
#include "ABTSM7BuildingModule.generated.h"

class AABTSM7BuildingMaterialSystem;
class AABTSM73StableBuildingActor;
class UPhysicalMaterial;
class UStaticMeshComponent;

/**
 * M7 consumer policy for one frozen tangent-site building. The direction is
 * derived only from immutable placement inputs; callers may not substitute a
 * global up vector or infer it from an arbitrary module position.
 */
struct ABTSRUNTIME_API FABTSM7SiteUniformGravityPolicy final
{
	static constexpr int32 SchemaVersion = 1;

	FVector SiteLocationWorldCM = FVector::ZeroVector;
	FVector SupportCenterWorldCM = FVector::ZeroVector;
	FVector SiteUp = FVector::ZeroVector;
	float GravityAccelerationCMPerSec2 = 0.0f;

	static bool TryDerive(
		const FVector& SiteLocationWorldCM,
		const FVector& SupportCenterWorldCM,
		float GravityAccelerationCMPerSec2,
		FABTSM7SiteUniformGravityPolicy& OutPolicy);
	bool IsUsable() const;
	uint32 ComputeCrc32() const;
	FString ToLogString() const;
};

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
	/** Installs immutable brick geometry before deferred spawn registration. */
	void ConfigureBrickBeforeFinishSpawning(UStaticMesh* Mesh, UMaterialInterface* Material, EABTSM7BuildingMaterial InMaterial);
	void ConfigureCylinder(UStaticMesh* Mesh, UMaterialInterface* Material, EABTSM7ModuleKind InKind, EABTSM7BuildingMaterial InMaterial, float LengthCM, float DiameterCM, const FTransform& WorldTransform, const FVector& AdditionalLocalScale = FVector::OneVector);
	/** Uses an exact engine-cylinder collision proxy and a no-collision authored presentation mesh. */
	void ConfigureVoxelDevice(UStaticMesh* CollisionMesh, UStaticMesh* PresentationMesh,
		UMaterialInterface* Material, EABTSM7ModuleKind InKind,
		float LengthCM, float DiameterCM, const FTransform& WorldTransform);
	void ConfigureImpactPhysics(const FABTSM7MaterialProfile& Profile);
	/** Applies a per-body Chaos quality override for multi-contact generated-building stacks. */
	void ConfigureChaosSolverIterations(int32 PositionIterations, int32 VelocityIterations);
	/** Sets the grace and, for a live body, rebases its current damage-enable deadline. */
	void SetContactDamageGraceSeconds(float Seconds);
	void ActivateDynamic(const FVector& Impulse, const FVector& InPlanetCenter, float GravityAcceleration);
	void ActivateDynamicPlanar(const FVector& Impulse, const FVector& InGravityUp, float GravityAcceleration);
	/** Activates with one exact tangent-site policy shared by production and Chaos fixtures. */
	bool ActivateDynamicSiteUniform(
		const FVector& Impulse,
		const FABTSM7SiteUniformGravityPolicy& Policy);
	/** Fail-closed audit of the live Chaos body and every shape filter. */
	bool VerifyChaosDeveloperObstacleCollisionIdentity(
		FString& OutError) const;
	/** Joins an authored child shape into this module's initial rigid body. */
	bool TryWeldStaticChild(AABTSM7BuildingModule& Child);
	/** Applies acceleration without invalidating Chaos sleep; false means no usable physics body. */
	static bool TryApplyNonInvalidatingAcceleration(
		UStaticMeshComponent& Component,
		const FVector& AccelerationCMPerSec2);
	void Freeze();
	/** Returns true only for the first successful break request. */
	bool BreakModule();
	bool ApplyImpactDamage(float DamageGain);
	/** Adds gameplay impulse without replacing an already active gravity identity. */
	bool ApplyDynamicImpactImpulse(const FVector& Impulse);
	/** Associates a real production module with its owning frozen building. */
	void ConfigureDamageLifecycleOwner(
		AABTSM73StableBuildingActor* InOwner,
		int32 InFrozenBrickId,
		bool bInCrystalLifecycleTarget);

	EABTSM7ModuleKind GetModuleKind() const { return ModuleKind; }
	EABTSM7BuildingMaterial GetBuildingMaterial() const { return BuildingMaterial; }
	UStaticMeshComponent* GetMeshComponent() const { return Visual; }
	bool IsDynamic() const { return bDynamic; }
	bool UsesSiteUniformGravity() const { return bSiteUniformGravity; }
	bool IsBroken() const { return bBroken; }
	bool IsCompoundChild() const { return bCompoundChild; }
	AABTSM73StableBuildingActor* GetDamageLifecycleOwner() const
	{
		return DamageLifecycleOwner.Get();
	}
	bool IsCrystalLifecycleTarget() const
	{
		return bCrystalLifecycleTarget;
	}
	int32 GetDamageLifecycleBrickId() const
	{
		return DamageLifecycleBrickId;
	}
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
	bool bCompoundChild = false;
	TWeakObjectPtr<AABTSM7BuildingModule> CompoundRoot;
	TArray<TWeakObjectPtr<AABTSM7BuildingModule>> CompoundChildren;
	TWeakObjectPtr<AABTSM73StableBuildingActor> DamageLifecycleOwner;
	int32 DamageLifecycleBrickId = INDEX_NONE;
	bool bPlanarGravity = false;
	bool bSiteUniformGravity = false;
	bool bCrystalLifecycleTarget = false;
	FVector PlanarGravityUp = FVector::UpVector;
	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> ImpactPhysicalMaterial;
	float CurrentDamage = 0.0f;
	float BreakDamage = 100.0f;
	float LastDamageImpactSeconds = -BIG_NUMBER;
	float ContactDamageGraceSeconds = 0.20f;
	float ContactDamageEnabledTimeSeconds = 0.0f;
};
