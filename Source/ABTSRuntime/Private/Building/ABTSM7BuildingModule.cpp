// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM7BuildingModule.h"

#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Components/StaticMeshComponent.h"

AABTSM7BuildingModule::AABTSM7BuildingModule()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ModuleVisual"));
	SetRootComponent(Visual);
	Visual->SetCollisionProfileName(TEXT("BlockAll"));
	Visual->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Visual->SetNotifyRigidBodyCollision(true);
	Visual->SetGenerateOverlapEvents(false);
	Visual->OnComponentHit.AddDynamic(this, &AABTSM7BuildingModule::HandleHit);
}

void AABTSM7BuildingModule::ConfigureBrick(UStaticMesh* Mesh, UMaterialInterface* Material, const EABTSM7BuildingMaterial InMaterial, const FTransform& WorldTransform)
{
	ModuleKind = EABTSM7ModuleKind::Brick;
	BuildingMaterial = InMaterial;
	Visual->SetStaticMesh(Mesh);
	if (Material) Visual->SetMaterial(0, Material);
	SetActorTransform(WorldTransform, false, nullptr, ETeleportType::TeleportPhysics);
}

void AABTSM7BuildingModule::ConfigureCylinder(UStaticMesh* Mesh, UMaterialInterface* Material, const EABTSM7ModuleKind InKind, const EABTSM7BuildingMaterial InMaterial, const float LengthCM, const float DiameterCM, const FTransform& WorldTransform)
{
	ModuleKind = InKind;
	BuildingMaterial = InMaterial;
	Visual->SetStaticMesh(Mesh);
	if (Material) Visual->SetMaterial(0, Material);
	SetActorTransform(WorldTransform, false, nullptr, ETeleportType::TeleportPhysics);
	// Engine Cylinder: diameter 100 cm, height 100 cm, local Z is its axis.
	Visual->SetRelativeScale3D(FVector(DiameterCM / 100.0f, DiameterCM / 100.0f, LengthCM / 100.0f));
}

void AABTSM7BuildingModule::ActivateDynamic(const FVector& Impulse, const FVector& InPlanetCenter, const float GravityAcceleration)
{
	PlanetCenter = InPlanetCenter;
	GravityAccelerationCMPerSec2 = FMath::Max(0.0f, GravityAcceleration);
	Visual->SetCollisionProfileName(TEXT("PhysicsActor"));
	Visual->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Visual->SetSimulatePhysics(true);
	Visual->SetEnableGravity(false);
	Visual->WakeAllRigidBodies();
	Visual->AddImpulse(Impulse, NAME_None, true);
	bDynamic = true;
}

void AABTSM7BuildingModule::Freeze()
{
	bDynamic = false;
	Visual->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Visual->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	Visual->SetSimulatePhysics(false);
	Visual->SetCollisionObjectType(ECC_WorldStatic);
	Visual->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void AABTSM7BuildingModule::BreakModule()
{
	bDynamic = false;
	Destroy();
}

void AABTSM7BuildingModule::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bDynamic || !Visual->IsSimulatingPhysics()) return;
	Visual->AddForce((PlanetCenter - GetActorLocation()).GetSafeNormal() * GravityAccelerationCMPerSec2, NAME_None, true);
}

void AABTSM7BuildingModule::HandleHit(UPrimitiveComponent*, AActor*, UPrimitiveComponent*, FVector, const FHitResult& Hit)
{
	if (!bDynamic) return;
	if (AABTSM7BuildingMaterialSystem* System = Cast<AABTSM7BuildingMaterialSystem>(GetOwner()))
	{
		const float NormalSpeed = FMath::Abs(FVector::DotProduct(Visual->GetPhysicsLinearVelocity(), Hit.ImpactNormal));
		System->HandleModuleChainImpact(*this, Hit, NormalSpeed);
	}
}

