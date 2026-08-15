// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM7BuildingModule.h"

#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodyInstance.h"
#include "World/ABTSCollisionChannels.h"

AABTSM7BuildingModule::AABTSM7BuildingModule()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ModuleVisual"));
	SetRootComponent(Visual);
	Visual->SetCollisionProfileName(TEXT("BlockAll"));
	Visual->SetCollisionObjectType(ABTSDeveloperObstacleChannel);
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

void AABTSM7BuildingModule::ConfigureVoxelDevice(
	UStaticMesh* CollisionMesh,
	UStaticMesh* PresentationMesh,
	UMaterialInterface* Material,
	const EABTSM7ModuleKind InKind,
	const float LengthCM,
	const float DiameterCM,
	const FTransform& WorldTransform)
{
	ConfigureCylinder(CollisionMesh, Material, InKind,
		EABTSM7BuildingMaterial::Iron, LengthCM, DiameterCM, WorldTransform);
	if (PresentationMesh == nullptr)
	{
		return;
	}
	if (DevicePresentation == nullptr)
	{
		DevicePresentation = NewObject<UStaticMeshComponent>(
			this, TEXT("VoxelDevicePresentation"), RF_Transient);
		DevicePresentation->SetupAttachment(Visual);
		DevicePresentation->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		DevicePresentation->SetGenerateOverlapEvents(false);
		DevicePresentation->SetCanEverAffectNavigation(false);
		DevicePresentation->RegisterComponent();
	}
	DevicePresentation->SetStaticMesh(PresentationMesh);
	if (Material != nullptr)
	{
		DevicePresentation->SetMaterial(0, Material);
	}

	const FBox NativeBounds = PresentationMesh->GetBoundingBox();
	const FVector NativeSize = NativeBounds.GetSize();
	const double NativeRadialSize = FMath::Max(
		static_cast<double>(NativeSize.X), static_cast<double>(NativeSize.Y));
	if (NativeRadialSize <= UE_DOUBLE_SMALL_NUMBER
		|| NativeSize.Z <= UE_DOUBLE_SMALL_NUMBER)
	{
		return;
	}
	const FVector DesiredScale(
		DiameterCM / NativeRadialSize,
		DiameterCM / NativeRadialSize,
		LengthCM / NativeSize.Z);
	const FVector ProxyScale = Visual->GetRelativeScale3D().GetAbs();
	const FVector RelativeScale(
		DesiredScale.X / FMath::Max(ProxyScale.X, UE_DOUBLE_SMALL_NUMBER),
		DesiredScale.Y / FMath::Max(ProxyScale.Y, UE_DOUBLE_SMALL_NUMBER),
		DesiredScale.Z / FMath::Max(ProxyScale.Z, UE_DOUBLE_SMALL_NUMBER));
	const FVector DesiredOffset = -NativeBounds.GetCenter() * DesiredScale;
	const FVector RelativeOffset(
		DesiredOffset.X / FMath::Max(ProxyScale.X, UE_DOUBLE_SMALL_NUMBER),
		DesiredOffset.Y / FMath::Max(ProxyScale.Y, UE_DOUBLE_SMALL_NUMBER),
		DesiredOffset.Z / FMath::Max(ProxyScale.Z, UE_DOUBLE_SMALL_NUMBER));
	DevicePresentation->SetRelativeTransform(FTransform(
		FQuat::Identity, RelativeOffset, RelativeScale));
	// The shared engine cylinder remains the exact Chaos authority but is not
	// part of presentation. Do not hide the root component: component visibility
	// is inherited by attached children and would also hide the authored device.
	// Disable only the proxy primitive's render passes instead.
	Visual->SetRenderInMainPass(false);
	Visual->SetRenderInDepthPass(false);
	Visual->SetCastShadow(false);
	DevicePresentation->SetVisibility(true, false);
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

void AABTSM7BuildingModule::ConfigureChaosSolverIterations(
	const int32 PositionIterations,
	const int32 VelocityIterations)
{
	// DAG2.3 can produce tall stacks with several simultaneous load-bearing
	// contacts. At 30 Hz the project-wide 8/2 Chaos defaults accumulate enough
	// lateral contact error to translate an otherwise upright Arch as a unit.
	// This per-body override is called only for M7.3 generated-building modules,
	// preserving the strict idle gate without changing unrelated world physics.
	FBodyInstance& BodyInstance = Visual->BodyInstance;
	BodyInstance.SetPositionSolverIterationCount(
		static_cast<uint8>(FMath::Clamp(PositionIterations, 1, 255)));
	BodyInstance.SetVelocitySolverIterationCount(
		static_cast<uint8>(FMath::Clamp(VelocityIterations, 1, 255)));
	BodyInstance.SetOverrideIterationCounts(true);
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
	Visual->SetCollisionObjectType(ABTSDeveloperObstacleChannel);
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
