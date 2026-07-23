// Copyright Epic Games, Inc. All Rights Reserved.

#include "Slingshot/ABTSM6DestructibleProxy.h"

#include "Components/StaticMeshComponent.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"

AABTSM6DestructibleProxy::AABTSM6DestructibleProxy()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DynamicVisual"));
	SetRootComponent(Visual);
	Visual->SetCollisionProfileName(TEXT("PhysicsActor"));
	Visual->SetNotifyRigidBodyCollision(true);
	Visual->SetGenerateOverlapEvents(false);
	Visual->OnComponentHit.AddDynamic(this, &AABTSM6DestructibleProxy::HandleHit);
}

void AABTSM6DestructibleProxy::ActivateProxy(
	UStaticMesh* Mesh,
	const FTransform& Transform,
	const EABTSM6ImpactMaterial InMaterial,
	const FVector& InitialImpulse,
	const FVector& InPlanetCenter,
	const float InGravityAcceleration)
{
	ImpactMaterial = InMaterial;
	PlanetCenter = InPlanetCenter;
	GravityAccelerationCMPerSec2 = FMath::Max(0.0f, InGravityAcceleration);
	Visual->SetStaticMesh(Mesh);
	SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	Visual->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Visual->SetSimulatePhysics(true);
	Visual->SetEnableGravity(false);
	Visual->WakeAllRigidBodies();
	Visual->AddImpulse(InitialImpulse, NAME_None, true);
	bActiveDynamic = true;
}

void AABTSM6DestructibleProxy::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bActiveDynamic || !Visual->IsSimulatingPhysics()) return;
	const FVector Direction = (PlanetCenter - Visual->GetComponentLocation()).GetSafeNormal();
	Visual->AddForce(Direction * GravityAccelerationCMPerSec2, NAME_None, true);
}

void AABTSM6DestructibleProxy::Freeze()
{
	bActiveDynamic = false;
	Visual->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Visual->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	Visual->SetSimulatePhysics(false);
	// Frozen means static, not non-physical.  QueryOnly removes the shape from
	// Chaos, allowing both walking birds and future projectiles to pass through.
	Visual->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Visual->SetCollisionObjectType(ECC_WorldStatic);
}

void AABTSM6DestructibleProxy::Reactivate(const FVector& Impulse)
{
	if (Visual == nullptr) return;
	Visual->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Visual->SetCollisionObjectType(ECC_PhysicsBody);
	Visual->SetSimulatePhysics(true);
	Visual->SetEnableGravity(false);
	Visual->WakeAllRigidBodies();
	Visual->AddImpulse(Impulse, NAME_None, true);
	bActiveDynamic = true;
}

void AABTSM6DestructibleProxy::Shatter()
{
	bActiveDynamic = false;
	Destroy();
}

void AABTSM6DestructibleProxy::HandleHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	FVector NormalImpulse,
	const FHitResult& Hit)
{
	if (!bActiveDynamic) return;
	if (AABTSM6SlingshotSystem* System = Cast<AABTSM6SlingshotSystem>(GetOwner()))
	{
		const float NormalSpeed = FMath::Abs(FVector::DotProduct(Visual->GetPhysicsLinearVelocity(), Hit.ImpactNormal));
		System->HandleProxyImpact(*this, Hit, NormalSpeed);
	}
}

