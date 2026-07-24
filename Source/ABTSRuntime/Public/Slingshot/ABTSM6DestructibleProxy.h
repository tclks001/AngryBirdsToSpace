// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Slingshot/ABTSM6Types.h"
#include "ABTSM6DestructibleProxy.generated.h"

class AABTSM6SlingshotSystem;
class UPhysicalMaterial;
class UStaticMesh;
class UStaticMeshComponent;

/** Temporary dynamic replacement for one knocked HISM instance. */
UCLASS()
class ABTSRUNTIME_API AABTSM6DestructibleProxy : public AActor
{
	GENERATED_BODY()

public:
	AABTSM6DestructibleProxy();
	virtual void Tick(float DeltaSeconds) override;
	void ActivateProxy(UStaticMesh* Mesh, const FTransform& Transform, EABTSM6ImpactMaterial InMaterial, const FABTSM6MaterialImpactProfile& PhysicsProfile, const FVector& InitialImpulse, const FVector& InPlanetCenter, float InGravityAcceleration, float InitialDamage = 0.0f);
	void ActivateProxyPlanar(UStaticMesh* Mesh, const FTransform& Transform, EABTSM6ImpactMaterial InMaterial, const FABTSM6MaterialImpactProfile& PhysicsProfile, const FVector& InitialImpulse, const FVector& InGravityUp, float InGravityAcceleration, float InitialDamage = 0.0f);
	/** Ignores activation overlap/depenetration contacts for a short period. */
	void SetContactDamageGraceSeconds(float Seconds) { ContactDamageGraceSeconds = FMath::Max(0.0f, Seconds); }
	/** Turns a previously frozen tilted instance back into a moving Chaos body. */
	void Reactivate(const FVector& Impulse);
	void Freeze();
	void Shatter();
	bool ApplyImpactDamage(float DamageGain);
	EABTSM6ImpactMaterial GetImpactMaterial() const { return ImpactMaterial; }
	UStaticMeshComponent* GetMeshComponent() const { return Visual; }
	float GetCurrentDamage() const { return CurrentDamage; }
	float GetBreakDamage() const { return BreakDamage; }

private:
	UFUNCTION()
	void HandleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Visual;
	EABTSM6ImpactMaterial ImpactMaterial = EABTSM6ImpactMaterial::Wood;
	FVector PlanetCenter = FVector::ZeroVector;
	float GravityAccelerationCMPerSec2 = 980.0f;
	bool bActiveDynamic = false;
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

