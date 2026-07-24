// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM7BuildingModule.h"

#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

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

void AABTSM7BuildingModule::ConfigureCylinder(UStaticMesh* Mesh, UMaterialInterface* Material, const EABTSM7ModuleKind InKind, const EABTSM7BuildingMaterial InMaterial, const float LengthCM, const float DiameterCM, const FTransform& WorldTransform, const FVector& AdditionalLocalScale)
{
	ModuleKind = InKind;
	BuildingMaterial = InMaterial;
	Visual->SetStaticMesh(Mesh);
	if (Material) Visual->SetMaterial(0, Material);
	SetActorTransform(WorldTransform, false, nullptr, ETeleportType::TeleportPhysics);
	// Engine Cylinder: diameter 100 cm, height 100 cm, local Z is its axis.
	Visual->SetRelativeScale3D(FVector(DiameterCM / 100.0f, DiameterCM / 100.0f, LengthCM / 100.0f) * AdditionalLocalScale.GetAbs());
}

void AABTSM7BuildingModule::ConfigureImpactPhysics(const FABTSM7MaterialProfile& Profile)
{
	BreakDamage = FMath::Max(1.0f, Profile.BreakDamage);
	ImpactPhysicalMaterial = NewObject<UPhysicalMaterial>(this, NAME_None, RF_Transient);
	ImpactPhysicalMaterial->Friction = Profile.DynamicFriction;
	ImpactPhysicalMaterial->StaticFriction = Profile.StaticFriction;
	ImpactPhysicalMaterial->Restitution = Profile.Restitution;
	ImpactPhysicalMaterial->Density = Profile.DensityGPerCubicCM;
	ImpactPhysicalMaterial->bOverrideFrictionCombineMode = true;
	ImpactPhysicalMaterial->FrictionCombineMode = EFrictionCombineMode::Average;
	ImpactPhysicalMaterial->bOverrideRestitutionCombineMode = true;
	ImpactPhysicalMaterial->RestitutionCombineMode = EFrictionCombineMode::Average;
	Visual->SetPhysMaterialOverride(ImpactPhysicalMaterial);
}

bool AABTSM7BuildingModule::ApplyImpactDamage(const float DamageGain)
{
	CurrentDamage = FMath::Max(0.0f, CurrentDamage + DamageGain);
	return CurrentDamage >= BreakDamage;
}

void AABTSM7BuildingModule::ActivateDynamic(const FVector& Impulse, const FVector& InPlanetCenter, const float GravityAcceleration)
{
	bPlanarGravity = false;
	PlanetCenter = InPlanetCenter;
	GravityAccelerationCMPerSec2 = FMath::Max(0.0f, GravityAcceleration);
	Visual->SetCollisionProfileName(TEXT("PhysicsActor"));
	Visual->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Visual->SetSimulatePhysics(true);
	Visual->SetEnableGravity(false);
	Visual->WakeAllRigidBodies();
	Visual->AddImpulse(Impulse, NAME_None, true);
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	ContactDamageEnabledTimeSeconds = Now + ContactDamageGraceSeconds;
	LastDamageImpactSeconds = -BIG_NUMBER;
	bDynamic = true;
}

void AABTSM7BuildingModule::ActivateDynamicPlanar(const FVector& Impulse, const FVector& InGravityUp, const float GravityAcceleration)
{
	ActivateDynamic(Impulse, FVector::ZeroVector, GravityAcceleration);
	bPlanarGravity = true;
	PlanarGravityUp = InGravityUp.GetSafeNormal();
	if (PlanarGravityUp.IsNearlyZero()) PlanarGravityUp = FVector::UpVector;
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
	const FVector GravityDirection = bPlanarGravity
		? -PlanarGravityUp
		: (PlanetCenter - GetActorLocation()).GetSafeNormal();
	Visual->AddForce(GravityDirection * GravityAccelerationCMPerSec2, NAME_None, true);
}

void AABTSM7BuildingModule::HandleHit(UPrimitiveComponent*, AActor*, UPrimitiveComponent* OtherComponent, FVector, const FHitResult& Hit)
{
	if (!bDynamic) return;
	if (AABTSM7BuildingMaterialSystem* System = Cast<AABTSM7BuildingMaterialSystem>(GetOwner()))
	{
		const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		if (Now < ContactDamageEnabledTimeSeconds) return;
		if (Now - LastDamageImpactSeconds < 0.08f) return;
		const FVector OtherVelocity = OtherComponent && OtherComponent->IsSimulatingPhysics()
			? OtherComponent->GetPhysicsLinearVelocityAtPoint(Hit.ImpactPoint) : FVector::ZeroVector;
		const FVector RelativeVelocity = Visual->GetPhysicsLinearVelocityAtPoint(Hit.ImpactPoint) - OtherVelocity;
		// Hit.ImpactNormal points toward this body. Only velocity into the
		// contact is damaging; positive dot means the bodies are separating,
		// which is commonly generated by Chaos initial-overlap depenetration.
		const float NormalSpeed = FMath::Max(0.0f, -FVector::DotProduct(RelativeVelocity, Hit.ImpactNormal));
		if (NormalSpeed <= KINDA_SMALL_NUMBER) return;
		LastDamageImpactSeconds = Now;
		System->HandleModuleChainImpact(*this, Hit, NormalSpeed);
	}
}
