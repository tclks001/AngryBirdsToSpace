// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Building/ABTSM7BuildingTypes.h"
#include "ABTSM7BuildingModule.generated.h"

class AABTSM7BuildingMaterialSystem;
class UPhysicalMaterial;
class UStaticMeshComponent;

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
	void ConfigureImpactPhysics(const FABTSM7MaterialProfile& Profile);
	/** Applies a per-body Chaos quality override for multi-contact generated-building stacks. */
	void ConfigureChaosSolverIterations(int32 PositionIterations, int32 VelocityIterations);
	/** Ignores contact damage briefly after a static body enters Chaos. */
	void SetContactDamageGraceSeconds(float Seconds) { ContactDamageGraceSeconds = FMath::Max(0.0f, Seconds); }
	void ActivateDynamic(const FVector& Impulse, const FVector& InPlanetCenter, float GravityAcceleration);
	void ActivateDynamicPlanar(const FVector& Impulse, const FVector& InGravityUp, float GravityAcceleration);
	void Freeze();
	void BreakModule();
	bool ApplyImpactDamage(float DamageGain);

	EABTSM7ModuleKind GetModuleKind() const { return ModuleKind; }
	EABTSM7BuildingMaterial GetBuildingMaterial() const { return BuildingMaterial; }
	UStaticMeshComponent* GetMeshComponent() const { return Visual; }
	bool IsDynamic() const { return bDynamic; }
	float GetCurrentDamage() const { return CurrentDamage; }
	float GetBreakDamage() const { return BreakDamage; }

private:
	UFUNCTION()
	void HandleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M7", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> Visual;

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
