// Copyright Epic Games, Inc. All Rights Reserved.

#include "Slingshot/ABTSM6DestructibleProxy.h"

#include "Components/StaticMeshComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
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
	const FABTSM6MaterialImpactProfile& PhysicsProfile,
	const FVector& InitialImpulse,
	const FVector& InPlanetCenter,
	const float InGravityAcceleration,
	const float InitialDamage)
{
	bPlanarGravity = false;
	ImpactMaterial = InMaterial;
	PlanetCenter = InPlanetCenter;
	GravityAccelerationCMPerSec2 = FMath::Max(0.0f, InGravityAcceleration);
	CurrentDamage = FMath::Max(0.0f, InitialDamage);
	BreakDamage = FMath::Max(1.0f, PhysicsProfile.BreakDamage);
	ImpactPhysicalMaterial = NewObject<UPhysicalMaterial>(this, NAME_None, RF_Transient);
	ImpactPhysicalMaterial->Friction = PhysicsProfile.DynamicFriction;
	ImpactPhysicalMaterial->StaticFriction = PhysicsProfile.StaticFriction;
	ImpactPhysicalMaterial->Restitution = PhysicsProfile.ObjectRestitution;
	ImpactPhysicalMaterial->Density = PhysicsProfile.DensityGPerCubicCM;
	ImpactPhysicalMaterial->bOverrideFrictionCombineMode = true;
	ImpactPhysicalMaterial->FrictionCombineMode = EFrictionCombineMode::Average;
	ImpactPhysicalMaterial->bOverrideRestitutionCombineMode = true;
	ImpactPhysicalMaterial->RestitutionCombineMode = EFrictionCombineMode::Average;
	Visual->SetStaticMesh(Mesh);
	Visual->SetPhysMaterialOverride(ImpactPhysicalMaterial);
	SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	Visual->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Visual->SetSimulatePhysics(true);
	Visual->SetEnableGravity(false);
	Visual->WakeAllRigidBodies();
	Visual->AddImpulse(InitialImpulse, NAME_None, true);
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	ContactDamageEnabledTimeSeconds = Now + ContactDamageGraceSeconds;
	LastDamageImpactSeconds = -BIG_NUMBER;
	bActiveDynamic = true;
}

void AABTSM6DestructibleProxy::ActivateProxyPlanar(
	UStaticMesh* Mesh,
	const FTransform& Transform,
	const EABTSM6ImpactMaterial InMaterial,
	const FABTSM6MaterialImpactProfile& PhysicsProfile,
	const FVector& InitialImpulse,
	const FVector& InGravityUp,
	const float InGravityAcceleration,
	const float InitialDamage)
{
	ActivateProxy(Mesh, Transform, InMaterial, PhysicsProfile, InitialImpulse, FVector::ZeroVector, InGravityAcceleration, InitialDamage);
	bPlanarGravity = true;
	PlanarGravityUp = InGravityUp.GetSafeNormal();
	if (PlanarGravityUp.IsNearlyZero()) PlanarGravityUp = FVector::UpVector;
}

bool AABTSM6DestructibleProxy::ApplyImpactDamage(const float DamageGain)
{
	CurrentDamage = FMath::Max(0.0f, CurrentDamage + DamageGain);
	return CurrentDamage >= BreakDamage;
}

void AABTSM6DestructibleProxy::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bActiveDynamic || !Visual->IsSimulatingPhysics()) return;
	const FVector Direction = bPlanarGravity ? -PlanarGravityUp : (PlanetCenter - Visual->GetComponentLocation()).GetSafeNormal();
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
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	ContactDamageEnabledTimeSeconds = Now + ContactDamageGraceSeconds;
	LastDamageImpactSeconds = -BIG_NUMBER;
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
		const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		if (Now < ContactDamageEnabledTimeSeconds) return;
		if (Now - LastDamageImpactSeconds < 0.08f) return;
		const FVector OtherVelocity = OtherComponent && OtherComponent->IsSimulatingPhysics()
			? OtherComponent->GetPhysicsLinearVelocityAtPoint(Hit.ImpactPoint) : FVector::ZeroVector;
		const FVector RelativeVelocity = Visual->GetPhysicsLinearVelocityAtPoint(Hit.ImpactPoint) - OtherVelocity;
		// Hit.ImpactNormal points toward this body. A negative relative normal
		// velocity means the bodies are approaching. Positive values are
		// separation/depenetration and must never become collision damage.
		const float NormalSpeed = FMath::Max(0.0f, -FVector::DotProduct(RelativeVelocity, Hit.ImpactNormal));
		if (NormalSpeed <= KINDA_SMALL_NUMBER) return;
		LastDamageImpactSeconds = Now;
		System->HandleProxyImpact(*this, Hit, NormalSpeed);
	}
}

