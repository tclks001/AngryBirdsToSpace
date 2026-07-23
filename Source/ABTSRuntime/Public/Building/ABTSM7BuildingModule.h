// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Building/ABTSM7BuildingTypes.h"
#include "ABTSM7BuildingModule.generated.h"

class AABTSM7BuildingMaterialSystem;
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
	void ConfigureCylinder(UStaticMesh* Mesh, UMaterialInterface* Material, EABTSM7ModuleKind InKind, EABTSM7BuildingMaterial InMaterial, float LengthCM, float DiameterCM, const FTransform& WorldTransform);
	void ActivateDynamic(const FVector& Impulse, const FVector& InPlanetCenter, float GravityAcceleration);
	void Freeze();
	void BreakModule();

	EABTSM7ModuleKind GetModuleKind() const { return ModuleKind; }
	EABTSM7BuildingMaterial GetBuildingMaterial() const { return BuildingMaterial; }
	UStaticMeshComponent* GetMeshComponent() const { return Visual; }
	bool IsDynamic() const { return bDynamic; }

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
};

