// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Party/ABTSBirdTypes.h"
#include "Building/ABTSM7BuildingTypes.h"
#include "ABTSM7BuildingMaterialSystem.generated.h"

class AABTSM3Planet;
class AABTSM7BuildingModule;
class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UPrimitiveComponent;
class UStaticMesh;
class UPhysicalMaterial;
struct FABTSM7PenetrationValidationStats;

/** Emitted only when an actual M7 brick is removed from the world. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FABTSM7MaterialRecoveredNative, EABTSM7BuildingMaterial /* Material */, int32 /* Quantity */);

/** M7 material library. Building layout/generation is deliberately deferred. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM7BuildingMaterialSystem : public AActor
{
	GENERATED_BODY()

public:
	AABTSM7BuildingMaterialSystem();
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable, Category = "ABTS|M7|Brick")
	int32 AddBrick(const FABTSM7BrickSpec& Spec, const FTransform& WorldTransform);
	/** Creates a static per-brick Actor used by M7.3 validated structures; launch physics activates it with every other module. */
	AABTSM7BuildingModule* SpawnBrickModule(const FABTSM7BrickSpec& Spec, const FTransform& WorldTransform);

	UFUNCTION(BlueprintCallable, Category = "ABTS|M7|Suspension")
	AABTSM7BuildingModule* SpawnSuspension(const FABTSM7SuspensionSpec& Spec, const FTransform& WorldTransform);

	UFUNCTION(BlueprintCallable, Category = "ABTS|M7|Device")
	AABTSM7BuildingModule* SpawnDevice(const FABTSM7DeviceSpec& Spec, const FTransform& WorldTransform);
	AABTSM7BuildingModule* SpawnDeviceWithOverrides(const FABTSM7DeviceSpec& Spec, const FTransform& WorldTransform, UStaticMesh* OverrideMesh, UMaterialInterface* OverrideMaterial);
	/** Stage-5.5 device path: exact logical collision proxy plus authored visual child. */
	AABTSM7BuildingModule* SpawnVoxelDevice(
		const FABTSM7DeviceSpec& Spec, const FTransform& WorldTransform);
	/** Static-world variant: owned by the caller and excluded from global launch activation. */
	AABTSM7BuildingModule* SpawnStaticVoxelDevice(
		const FABTSM7DeviceSpec& Spec, const FTransform& WorldTransform);

	bool OwnsPrimitive(const UPrimitiveComponent* Component) const;
	bool HandleBirdImpact(UPrimitiveComponent* Component, int32 InstanceIndex, float NormalSpeedCMPerSec, const FVector& IncomingVelocity, EABTSBirdId BirdId);
	void HandleModuleChainImpact(AABTSM7BuildingModule& Source, const FHitResult& Hit, float NormalSpeedCMPerSec);
	void ApplyRadialBlast(const FVector& Origin, float DestroyRadiusCM, float ImpulseRadiusCM, float ImpulseSpeedCMPerSec);
	void ApplyDirectionalBlast(const FVector& Origin, const FVector& Axis, float DestroyLengthCM, float ImpulseLengthCM, float EffectRadiusCM, float ImpulseSpeedCMPerSec);
	/** Promotes all building HISM instances and enables gravity on every module for the launch phase. */
	void BeginLaunchPhysics(bool bPlanar, const FVector& GravityReference, float GravityAcceleration, float ContactDamageGraceSeconds = -1.0f);
	/** Runs the same pre-Chaos contact repair used by launch physics on a caller-owned module subset. */
	FABTSM7PenetrationValidationStats ValidateAndRepairPendingModules(
		const TArray<AABTSM7BuildingModule*>& PendingModules) const;
	/** Adds currently simulated M7 bodies to a read-only launch settlement sample. */
	void AppendDynamicPhysicsBodies(TArray<UPrimitiveComponent*>& OutBodies) const;
	float GetLastPhysicsActivityTimeSeconds() const { return LastPhysicsActivityTimeSeconds; }
	uint32 GetLastLaunchChaosBodyProfileHash() const { return LastLaunchChaosBodyProfileHash; }
	uint32 GetLastLaunchChaosWorldProfileHash() const { return LastLaunchChaosWorldProfileHash; }
	/** Extends the damage grace on all currently dynamic modules without changing their gravity or launch configuration. */
	void SetDynamicContactDamageGraceSeconds(float Seconds);
	void FreezeDynamicModules();
	void ConfigureTestSet(bool bEnable, const FTransform& SpawnTransform);
	/** Copies the authoritative runtime tuning for deterministic M7.3 analysis without exposing Actor state. */
	void CopyMaterialProfiles(TArray<FABTSM7MaterialProfile>& OutProfiles) const;

	/** M8 subscribes here to turn destroyed building bricks into shared-inventory materials. */
	FABTSM7MaterialRecoveredNative OnMaterialRecovered;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M7|Brick")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> WoodBrickHISM;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M7|Brick")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> StoneBrickHISM;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M7|Brick")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> IronBrickHISM;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M7|Brick")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> GlassBrickHISM;

private:
	UHierarchicalInstancedStaticMeshComponent* GetBrickHISM(EABTSM7BuildingMaterial Material) const;
	UMaterialInterface* GetMaterial(EABTSM7BuildingMaterial Material) const;
	const FABTSM7MaterialProfile& GetProfile(EABTSM7BuildingMaterial Material) const;
	float GetBirdThresholdScale(EABTSBirdId BirdId) const;
	float ComputeDamageGain(const FABTSM7MaterialProfile& Profile, float NormalSpeedCMPerSec, float BreakSpeedCMPerSec) const;
	uint64 GetHISMDamageKey(const UHierarchicalInstancedStaticMeshComponent& HISM, int32 InstanceIndex) const;
	void ApplyHISMPhysicalMaterial(UHierarchicalInstancedStaticMeshComponent& HISM, EABTSM7BuildingMaterial Material, const TCHAR* DebugName);
	AABTSM7BuildingModule* SpawnVoxelDeviceInternal(
		const FABTSM7DeviceSpec& Spec,
		const FTransform& WorldTransform,
		bool bRegisterForLaunchPhysics);
	void ActivateModuleForLaunch(AABTSM7BuildingModule& Module, const FVector& InitialImpulse = FVector::ZeroVector);
	void MarkPhysicsActivity();
	AABTSM7BuildingModule* PromoteBrick(UHierarchicalInstancedStaticMeshComponent& HISM, int32 InstanceIndex, EABTSM7BuildingMaterial Material, const FVector& Impulse, bool bActivateImmediately = true);
	void BreakOrImpulsePrimitive(UPrimitiveComponent* Component, int32 InstanceIndex, const FVector& ImpulseDirection, float ImpulseSpeed, bool bDestroy);
	void NotifyBrickRecovered(EABTSM7BuildingMaterial Material, int32 Quantity = 1);
	void SpawnTestSet();

	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Assets")
	TObjectPtr<UStaticMesh> SharedBrickMesh;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Assets")
	TObjectPtr<UStaticMesh> SharedCylinderMesh;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Assets")
	TObjectPtr<UStaticMesh> ExplosivePresentationMesh;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Assets")
	TObjectPtr<UStaticMesh> PistonPresentationMesh;
	/** Parent used for colored no-asset fallbacks; Engine BasicShapeMaterial is the C++ default. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Assets")
	TObjectPtr<UMaterialInterface> FallbackMaterialParent;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Assets")
	TObjectPtr<UMaterialInterface> WoodMaterial;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Assets")
	TObjectPtr<UMaterialInterface> StoneMaterial;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Assets")
	TObjectPtr<UMaterialInterface> IronMaterial;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Assets")
	TObjectPtr<UMaterialInterface> GlassMaterial;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Assets")
	TObjectPtr<UMaterialInterface> RopeMaterial;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Assets")
	TObjectPtr<UMaterialInterface> ChainMaterial;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Assets")
	TObjectPtr<UMaterialInterface> ExplosiveMaterial;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Assets")
	TObjectPtr<UMaterialInterface> SpringMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> WoodFallbackMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> StoneFallbackMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> IronFallbackMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> GlassFallbackMaterial;

	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Damage")
	TArray<FABTSM7MaterialProfile> MaterialProfiles;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Damage", meta = (ClampMin = "1.0"))
	float BarrelDestroyRadiusCM = 340.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Damage", meta = (ClampMin = "1.0"))
	float BarrelImpulseRadiusCM = 760.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Damage", meta = (ClampMin = "0.0"))
	float BarrelImpulseSpeedCMPerSec = 1250.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Damage", meta = (ClampMin = "1.0"))
	float PistonDestroyLengthCM = 260.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Damage", meta = (ClampMin = "1.0"))
	float PistonImpulseLengthCM = 720.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Damage", meta = (ClampMin = "1.0"))
	float PistonEffectRadiusCM = 180.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Damage", meta = (ClampMin = "0.0"))
	float PistonImpulseSpeedCMPerSec = 1400.0f;
	/** Prevents activation overlap/depenetration contacts from becoming damage. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Damage", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	float LaunchContactDamageGraceSeconds = 0.20f;
	/** Small initial penetration is repaired before any M7 module enters Chaos. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Contact Stability", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "10.0"))
	float InitialPenetrationRepairToleranceCM = 2.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Contact Stability", meta = (ClampMin = "1", ClampMax = "32", UIMin = "1", UIMax = "16"))
	int32 InitialPenetrationRepairPasses = 8;

	TWeakObjectPtr<AABTSM3Planet> Planet;
	TArray<TWeakObjectPtr<AABTSM7BuildingModule>> Modules;
	TMap<uint64, float> HISMDamageByStableKey;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPhysicalMaterial>> RuntimePhysicalMaterials;
	bool bLaunchPhysicsPlanar = false;
	FVector LaunchGravityReference = FVector::ZeroVector;
	float LaunchGravityAccelerationCMPerSec2 = 980.0f;
	uint32 LastLaunchChaosBodyProfileHash = 0;
	uint32 LastLaunchChaosWorldProfileHash = 0;
	float LastPhysicsActivityTimeSeconds = -BIG_NUMBER;
	bool bSpawnTestSetAtStart = false;
	FTransform TestSetTransform = FTransform::Identity;
};
