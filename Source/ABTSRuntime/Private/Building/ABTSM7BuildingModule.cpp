// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM7BuildingModule.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM73StableBuildingActor.h"
#include "Components/StaticMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Crc.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsObjectExternalInterface.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Physics/Experimental/PhysInterface_Chaos.h"
#include "Physics/PhysicsFiltering.h"
#include "World/ABTSCollisionChannels.h"

bool FABTSM7SiteUniformGravityPolicy::TryDerive(
	const FVector& InSiteLocationWorldCM,
	const FVector& InSupportCenterWorldCM,
	const float InGravityAccelerationCMPerSec2,
	FABTSM7SiteUniformGravityPolicy& OutPolicy)
{
	OutPolicy = FABTSM7SiteUniformGravityPolicy();
	if (InSiteLocationWorldCM.ContainsNaN()
		|| InSupportCenterWorldCM.ContainsNaN()
		|| !FMath::IsFinite(InGravityAccelerationCMPerSec2)
		|| InGravityAccelerationCMPerSec2 <= 0.0f)
	{
		return false;
	}
	const FVector DerivedSiteUp =
		(InSiteLocationWorldCM - InSupportCenterWorldCM).GetSafeNormal();
	if (DerivedSiteUp.IsNearlyZero() || DerivedSiteUp.ContainsNaN())
	{
		return false;
	}
	OutPolicy.SiteLocationWorldCM = InSiteLocationWorldCM;
	OutPolicy.SupportCenterWorldCM = InSupportCenterWorldCM;
	OutPolicy.SiteUp = DerivedSiteUp;
	OutPolicy.GravityAccelerationCMPerSec2 = InGravityAccelerationCMPerSec2;
	return OutPolicy.IsUsable();
}

bool FABTSM7SiteUniformGravityPolicy::IsUsable() const
{
	if (SiteLocationWorldCM.ContainsNaN()
		|| SupportCenterWorldCM.ContainsNaN()
		|| SiteUp.ContainsNaN()
		|| !FMath::IsFinite(GravityAccelerationCMPerSec2)
		|| GravityAccelerationCMPerSec2 <= 0.0f)
	{
		return false;
	}
	const FVector DerivedSiteUp =
		(SiteLocationWorldCM - SupportCenterWorldCM).GetSafeNormal();
	return !DerivedSiteUp.IsNearlyZero()
		&& SiteUp.IsNormalized()
		&& SiteUp.Equals(DerivedSiteUp, 1.0e-6);
}

uint32 FABTSM7SiteUniformGravityPolicy::ComputeCrc32() const
{
	if (!IsUsable())
	{
		return 0;
	}
	const FString Canonical = FString::Printf(
		TEXT("M7SiteUniformGravity:v%d:Derivation=Normalize(SiteLocationWorldCM-SupportCenterWorldCM)")
		TEXT(":Site=%d,%d,%d:Center=%d,%d,%d:Up=%d,%d,%d:Acceleration=%d"),
		SchemaVersion,
		FMath::RoundToInt(SiteLocationWorldCM.X * 1000.0),
		FMath::RoundToInt(SiteLocationWorldCM.Y * 1000.0),
		FMath::RoundToInt(SiteLocationWorldCM.Z * 1000.0),
		FMath::RoundToInt(SupportCenterWorldCM.X * 1000.0),
		FMath::RoundToInt(SupportCenterWorldCM.Y * 1000.0),
		FMath::RoundToInt(SupportCenterWorldCM.Z * 1000.0),
		FMath::RoundToInt(SiteUp.X * 1000000.0),
		FMath::RoundToInt(SiteUp.Y * 1000000.0),
		FMath::RoundToInt(SiteUp.Z * 1000000.0),
		FMath::RoundToInt(GravityAccelerationCMPerSec2 * 1000.0f));
	return FCrc::StrCrc32(*Canonical);
}

FString FABTSM7SiteUniformGravityPolicy::ToLogString() const
{
	return FString::Printf(
		TEXT("Policy=SiteUniformTangentGravity Schema=%d Derivation=Normalize(SiteLocationWorldCM-SupportCenterWorldCM) Site=%s Center=%s SiteUp=%s Acceleration=%.3f Hash=%u"),
		SchemaVersion,
		*SiteLocationWorldCM.ToString(),
		*SupportCenterWorldCM.ToString(),
		*SiteUp.ToString(),
		GravityAccelerationCMPerSec2,
		ComputeCrc32());
}

FABTSM7ChaosBodyProfile FABTSM7ChaosBodyProfile::Production()
{
	return FABTSM7ChaosBodyProfile();
}

bool FABTSM7ChaosBodyProfile::IsUsable() const
{
	return PositionSolverIterations > 0 && PositionSolverIterations <= 255
		&& VelocitySolverIterations > 0 && VelocitySolverIterations <= 255
		&& LinearDamping >= 0.0f && AngularDamping >= 0.0f;
}

uint32 FABTSM7ChaosBodyProfile::ComputeCrc32() const
{
	const FString Canonical = FString::Printf(
		TEXT("M7ChaosBody:v%d:Solver=%d,%d:Damping=%d,%d"),
		SchemaVersion,
		PositionSolverIterations,
		VelocitySolverIterations,
		FMath::RoundToInt(LinearDamping * 1000.0f),
		FMath::RoundToInt(AngularDamping * 1000.0f));
	return FCrc::StrCrc32(*Canonical);
}

void FABTSM7ChaosBodyProfile::ApplyTo(UStaticMeshComponent& Component) const
{
	if (!IsUsable())
	{
		return;
	}
	FBodyInstance& BodyInstance = Component.BodyInstance;
	BodyInstance.SetPositionSolverIterationCount(
		static_cast<uint8>(PositionSolverIterations));
	BodyInstance.SetVelocitySolverIterationCount(
		static_cast<uint8>(VelocitySolverIterations));
	BodyInstance.SetOverrideIterationCounts(true);
	Component.SetLinearDamping(LinearDamping);
	Component.SetAngularDamping(AngularDamping);
}

FABTSM7ChaosWorldProfile FABTSM7ChaosWorldProfile::CaptureProduction()
{
	FABTSM7ChaosWorldProfile Profile;
	if (const UPhysicsSettings* Settings = UPhysicsSettings::Get())
	{
		Profile.bSubstepping = Settings->bSubstepping;
		Profile.bSubsteppingAsync = Settings->bSubsteppingAsync;
		Profile.bTickPhysicsAsync = Settings->bTickPhysicsAsync;
		Profile.MaxPhysicsDeltaSeconds = Settings->MaxPhysicsDeltaTime;
		Profile.MaxSubstepDeltaSeconds = Settings->MaxSubstepDeltaTime;
		Profile.MaximumSubsteps = Settings->MaxSubsteps;
		Profile.AsyncFixedDeltaSeconds = Settings->AsyncFixedTimeStepSize;
	}
	if (const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(
		TEXT("p.Chaos.Solver.Collision.PositionFrictionIterations")))
	{
		Profile.PositionFrictionIterations = Variable->GetInt();
	}
	if (const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(
		TEXT("p.Chaos.Solver.Collision.PositionShockPropagationIterations")))
	{
		Profile.PositionShockPropagationIterations = Variable->GetInt();
	}
	return Profile;
}

uint32 FABTSM7ChaosWorldProfile::ComputeCrc32() const
{
	const FString Canonical = FString::Printf(
		TEXT("M7ChaosWorld:v%d:Substep=%d,%d,%d:%d,%d:Async=%d,%d:Friction=%d:Shock=%d"),
		SchemaVersion,
		bSubstepping ? 1 : 0,
		bSubsteppingAsync ? 1 : 0,
		FMath::RoundToInt(MaxSubstepDeltaSeconds * 1000000.0f),
		MaximumSubsteps,
		FMath::RoundToInt(MaxPhysicsDeltaSeconds * 1000000.0f),
		bTickPhysicsAsync ? 1 : 0,
		FMath::RoundToInt(AsyncFixedDeltaSeconds * 1000000.0f),
		PositionFrictionIterations,
		PositionShockPropagationIterations);
	return FCrc::StrCrc32(*Canonical);
}

FString FABTSM7ChaosWorldProfile::ToLogString() const
{
	return FString::Printf(
		TEXT("Substep=%d AsyncSubstep=%d MaxPhysicsDT=%.6f MaxSubstepDT=%.6f MaxSubsteps=%d AsyncTick=%d AsyncFixedDT=%.6f PositionFriction=%d PositionShock=%d"),
		bSubstepping ? 1 : 0,
		bSubsteppingAsync ? 1 : 0,
		MaxPhysicsDeltaSeconds,
		MaxSubstepDeltaSeconds,
		MaximumSubsteps,
		bTickPhysicsAsync ? 1 : 0,
		AsyncFixedDeltaSeconds,
		PositionFrictionIterations,
		PositionShockPropagationIterations);
}

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
	ConfigureBrickBeforeFinishSpawning(Mesh, Material, InMaterial);
	SetActorTransform(WorldTransform, false, nullptr, ETeleportType::TeleportPhysics);
}

void AABTSM7BuildingModule::ConfigureBrickBeforeFinishSpawning(
	UStaticMesh* Mesh,
	UMaterialInterface* Material,
	const EABTSM7BuildingMaterial InMaterial)
{
	ModuleKind = EABTSM7ModuleKind::Brick;
	BuildingMaterial = InMaterial;
	Visual->SetStaticMesh(Mesh);
	if (Material) Visual->SetMaterial(0, Material);
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
	FABTSM7ChaosBodyProfile::Production().ApplyTo(*Visual);
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
	if (bBroken) return false;
	CurrentDamage = FMath::Max(0.0f, CurrentDamage + DamageGain);
	return CurrentDamage >= BreakDamage;
}

void AABTSM7BuildingModule::ActivateDynamic(const FVector& Impulse, const FVector& InPlanetCenter, const float GravityAcceleration)
{
	bPlanarGravity = false;
	bSiteUniformGravity = false;
	PlanetCenter = InPlanetCenter;
	GravityAccelerationCMPerSec2 = FMath::Max(0.0f, GravityAcceleration);
	Visual->SetCollisionProfileName(TEXT("PhysicsActor"));
	// PhysicsActor restores the engine's PhysicsBody ObjectType. M7's frozen
	// pads deliberately ignore only the M7 building channel, so restore that
	// channel after loading the profile while retaining its response container.
	Visual->SetCollisionObjectType(ABTSDeveloperObstacleChannel);
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

UPrimitiveComponent* AABTSM7BuildingModule::GetStylizedPresentationPrimitive() const
{
	return IsValid(DevicePresentation) ? DevicePresentation.Get() : Visual.Get();
}

bool AABTSM7BuildingModule::IsStylizedWeakPoint() const
{
	return bCrystalLifecycleTarget
		|| ModuleKind == EABTSM7ModuleKind::ExplosiveBarrel
		|| ModuleKind == EABTSM7ModuleKind::SpringPiston;
}

void AABTSM7BuildingModule::SetContactDamageGraceSeconds(
	const float Seconds)
{
	ContactDamageGraceSeconds = FMath::Max(0.0f, Seconds);
	if (bDynamic)
	{
		const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		ContactDamageEnabledTimeSeconds = Now + ContactDamageGraceSeconds;
		LastDamageImpactSeconds = -BIG_NUMBER;
	}
}

bool AABTSM7BuildingModule::ApplyDynamicImpactImpulse(
	const FVector& Impulse)
{
	if (bBroken || bCompoundChild || !bDynamic || !IsValid(Visual)
		|| !Visual->IsSimulatingPhysics() || Impulse.ContainsNaN())
	{
		return false;
	}
	Visual->WakeAllRigidBodies();
	if (!Impulse.IsNearlyZero())
	{
		Visual->AddImpulse(Impulse, NAME_None, true);
	}
	return true;
}

void AABTSM7BuildingModule::ConfigureDamageLifecycleOwner(
	AABTSM73StableBuildingActor* InOwner,
	const int32 InFrozenBrickId,
	const bool bInCrystalLifecycleTarget)
{
	DamageLifecycleOwner = InOwner;
	DamageLifecycleBrickId = InOwner != nullptr
		? InFrozenBrickId
		: INDEX_NONE;
	bCrystalLifecycleTarget = InOwner != nullptr
		&& bInCrystalLifecycleTarget;
}

bool AABTSM7BuildingModule::ActivateDynamicSiteUniform(
	const FVector& Impulse,
	const FABTSM7SiteUniformGravityPolicy& Policy)
{
	if (bCompoundChild || !Policy.IsUsable())
	{
		return false;
	}
	ActivateDynamicPlanar(
		Impulse, Policy.SiteUp, Policy.GravityAccelerationCMPerSec2);
	bSiteUniformGravity = true;
	return true;
}

bool AABTSM7BuildingModule::VerifyChaosDeveloperObstacleCollisionIdentity(
	FString& OutError) const
{
	OutError.Reset();
	if (!IsValid(Visual)
		|| !Visual->IsRegistered()
		|| Visual->GetCollisionEnabled() != ECollisionEnabled::QueryAndPhysics
		|| Visual->GetCollisionObjectType() != ABTSDeveloperObstacleChannel
		|| !Visual->IsSimulatingPhysics())
	{
		OutError = TEXT("ComponentCollisionIdentityInvalid");
		return false;
	}
	const FBodyInstance* BodyInstance = Visual->GetBodyInstance();
	if (BodyInstance == nullptr
		|| !FPhysicsInterface::IsValid(BodyInstance->GetPhysicsActor()))
	{
		OutError = TEXT("ChaosPhysicsActorMissing");
		return false;
	}

	int32 ShapeCount = 0;
	int32 MatchingShapeCount = 0;
	FPhysicsCommand::ExecuteRead(
		BodyInstance->GetPhysicsActor(),
		[BodyInstance, &ShapeCount, &MatchingShapeCount](
			const FPhysicsActorHandle& Actor)
		{
			TArray<FPhysicsShapeHandle> Shapes;
			BodyInstance->GetAllShapes_AssumesLocked(Shapes);
			ShapeCount = Shapes.Num();
			for (const FPhysicsShapeHandle& Shape : Shapes)
			{
				if (GetCollisionChannel(
					FPhysicsInterface::GetShapeFilterData(Shape))
					== ABTSDeveloperObstacleChannel)
				{
					++MatchingShapeCount;
				}
			}
		});
	if (ShapeCount <= 0 || MatchingShapeCount != ShapeCount)
	{
		OutError = FString::Printf(
			TEXT("ChaosShapeFilterChannelMismatch:Shapes=%d:Matching=%d"),
			ShapeCount, MatchingShapeCount);
		return false;
	}
	UE_LOG(LogABTSRuntime, Verbose,
		TEXT("[ABTS][M7][SiteUniformLaunch][CollisionIdentity]")
		TEXT(" Module=%s ComponentObjectType=%d Shapes=%d")
		TEXT(" ShapeFilterChannel=%d Accepted=1"),
		*GetName(), static_cast<int32>(Visual->GetCollisionObjectType()),
		ShapeCount, static_cast<int32>(ABTSDeveloperObstacleChannel));
	return true;
}

bool AABTSM7BuildingModule::TryWeldStaticChild(
	AABTSM7BuildingModule& Child)
{
	if (&Child == this || bBroken || Child.bBroken || bDynamic || Child.bDynamic
		|| bCompoundChild || Child.bCompoundChild
		|| BuildingMaterial != Child.BuildingMaterial
		|| !IsValid(Visual) || !IsValid(Child.Visual)
		|| !Visual->IsRegistered() || !Child.Visual->IsRegistered()
		|| Visual->IsWelded() || Child.Visual->IsWelded())
	{
		return false;
	}
	Child.Visual->WeldTo(Visual, NAME_None, true);
	if (!Child.Visual->IsWelded())
	{
		return false;
	}
	Child.bCompoundChild = true;
	Child.CompoundRoot = this;
	CompoundChildren.Add(&Child);
	return true;
}

void AABTSM7BuildingModule::Freeze()
{
	if (bCompoundChild) return;
	bDynamic = false;
	Visual->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Visual->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	Visual->SetSimulatePhysics(false);
	Visual->SetCollisionObjectType(ABTSDeveloperObstacleChannel);
	Visual->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

bool AABTSM7BuildingModule::TryApplyNonInvalidatingAcceleration(
	UStaticMeshComponent& Component,
	const FVector& AccelerationCMPerSec2)
{
	if (!FMath::IsFinite(AccelerationCMPerSec2.X)
		|| !FMath::IsFinite(AccelerationCMPerSec2.Y)
		|| !FMath::IsFinite(AccelerationCMPerSec2.Z))
	{
		return false;
	}
	TArray<Chaos::FPhysicsObject*> PhysicsObjects =
		Component.GetAllPhysicsObjects();
	if (PhysicsObjects.IsEmpty()) return false;
	FLockedWritePhysicsObjectExternalInterface Interface =
		FPhysicsObjectExternalInterface::LockWrite(PhysicsObjects);
	const float MassKG = Interface->GetMass(PhysicsObjects);
	if (!FMath::IsFinite(MassKG) || MassKG <= 0.0f) return false;
	Interface->AddForce(
		PhysicsObjects,
		AccelerationCMPerSec2 * MassKG,
		false);
	return true;
}

bool AABTSM7BuildingModule::BreakModule()
{
	if (bBroken) return false;
	bBroken = true;
	if (bCompoundChild)
	{
		if (AABTSM7BuildingModule* Root = CompoundRoot.Get())
		{
			Root->CompoundChildren.Remove(this);
		}
		if (Visual->IsWelded()) Visual->UnWeldFromParent();
		bCompoundChild = false;
		CompoundRoot.Reset();
	}
	else
	{
		AABTSM7BuildingMaterialSystem* MaterialSystem =
			Cast<AABTSM7BuildingMaterialSystem>(GetOwner());
		for (const TWeakObjectPtr<AABTSM7BuildingModule>& ChildPtr :
			CompoundChildren)
		{
			AABTSM7BuildingModule* Child = ChildPtr.Get();
			if (!IsValid(Child) || Child->bBroken) continue;
			if (Child->Visual->IsWelded()) Child->Visual->UnWeldFromParent();
			Child->bCompoundChild = false;
			Child->CompoundRoot.Reset();
			if (bDynamic)
			{
				if (bPlanarGravity)
				{
					Child->ActivateDynamicPlanar(FVector::ZeroVector,
						PlanarGravityUp, GravityAccelerationCMPerSec2);
					Child->bSiteUniformGravity = bSiteUniformGravity;
				}
				else
				{
					Child->ActivateDynamic(FVector::ZeroVector,
						PlanetCenter, GravityAccelerationCMPerSec2);
				}
				if (MaterialSystem != nullptr)
				{
					MaterialSystem->AdoptUnweldedCompoundChild(*Child);
				}
			}
		}
		CompoundChildren.Reset();
	}
	bDynamic = false;
	Destroy();
	return true;
}

void AABTSM7BuildingModule::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bDynamic || !Visual->IsSimulatingPhysics()) return;
	// UPrimitiveComponent::AddForce invalidates the particle and forces its
	// state back to Dynamic every frame, so a supported body can never enter
	// Chaos sleep. Skip bodies that are already asleep, and use the same
	// non-invalidating force path as a persistent environmental acceleration
	// for awake bodies. A collision/explicit wake makes the body eligible again.
	if (!Visual->IsAnyRigidBodyAwake()) return;
	const FVector GravityDirection = bPlanarGravity
		? -PlanarGravityUp
		: (PlanetCenter - GetActorLocation()).GetSafeNormal();
	TryApplyNonInvalidatingAcceleration(
		*Visual, GravityDirection * GravityAccelerationCMPerSec2);
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
