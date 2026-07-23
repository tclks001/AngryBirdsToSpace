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
class UStaticMesh;

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

	UFUNCTION(BlueprintCallable, Category = "ABTS|M7|Suspension")
	AABTSM7BuildingModule* SpawnSuspension(const FABTSM7SuspensionSpec& Spec, const FTransform& WorldTransform);

	UFUNCTION(BlueprintCallable, Category = "ABTS|M7|Device")
	AABTSM7BuildingModule* SpawnDevice(const FABTSM7DeviceSpec& Spec, const FTransform& WorldTransform);

	bool OwnsPrimitive(const UPrimitiveComponent* Component) const;
	bool HandleBirdImpact(UPrimitiveComponent* Component, int32 InstanceIndex, float NormalSpeedCMPerSec, const FVector& IncomingVelocity, EABTSBirdId BirdId);
	void HandleModuleChainImpact(AABTSM7BuildingModule& Source, const FHitResult& Hit, float NormalSpeedCMPerSec);
	void ApplyRadialBlast(const FVector& Origin, float DestroyRadiusCM, float ImpulseRadiusCM, float ImpulseSpeedCMPerSec);
	void ApplyDirectionalBlast(const FVector& Origin, const FVector& Axis, float DestroyLengthCM, float ImpulseLengthCM, float EffectRadiusCM, float ImpulseSpeedCMPerSec);
	void FreezeDynamicModules();
	void ConfigureTestSet(bool bEnable, const FTransform& SpawnTransform);

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
	AABTSM7BuildingModule* PromoteBrick(UHierarchicalInstancedStaticMeshComponent& HISM, int32 InstanceIndex, EABTSM7BuildingMaterial Material, const FVector& Impulse);
	void BreakOrImpulsePrimitive(UPrimitiveComponent* Component, int32 InstanceIndex, const FVector& ImpulseDirection, float ImpulseSpeed, bool bDestroy);
	void SpawnTestSet();

	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Assets")
	TObjectPtr<UStaticMesh> SharedBrickMesh;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Assets")
	TObjectPtr<UStaticMesh> SharedCylinderMesh;
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

	TWeakObjectPtr<AABTSM3Planet> Planet;
	TArray<TWeakObjectPtr<AABTSM7BuildingModule>> Modules;
	bool bSpawnTestSetAtStart = false;
	FTransform TestSetTransform = FTransform::Identity;
};
