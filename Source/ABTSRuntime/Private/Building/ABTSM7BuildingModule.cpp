// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM7BuildingModule.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM73StableBuildingActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Crc.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsObjectExternalInterface.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Physics/Experimental/PhysInterface_Chaos.h"
#include "Physics/PhysicsFiltering.h"
#include "ProceduralMeshComponent.h"
#include "Terrain/ABTSM3Planet.h"
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

FABTSM7ChaosBodyProfile FABTSM7ChaosBodyProfile::DestructionCandidate()
{
	FABTSM7ChaosBodyProfile Profile = Production();
	Profile.PositionSolverIterations = 4;
	Profile.VelocitySolverIterations = 1;
	return Profile;
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

void FABTSM7DeferredImpactCollisionPolicy::ApplyTo(
	UStaticMeshComponent& Component)
{
	Component.SetUseCCD(true);
	Component.SetMaxDepenetrationVelocity(
		NAME_None, MinimumInitialOverlapDepenetrationCMPerSec);
}

bool FABTSM7DeferredImpactCollisionPolicy::VerifyDynamic(
	UStaticMeshComponent& Component, FString& OutError)
{
	OutError.Reset();
	const FBodyInstance* BodyInstance = Component.GetBodyInstance();
	const UPhysicsSettings* PhysicsSettings = UPhysicsSettings::Get();
	if (Component.GetCollisionEnabled() != ECollisionEnabled::QueryAndPhysics
		|| Component.GetCollisionObjectType() != ABTSDeveloperObstacleChannel
		|| !Component.IsSimulatingPhysics()
		|| BodyInstance == nullptr || !BodyInstance->bUseCCD
		|| Component.GetMaxDepenetrationVelocity(NAME_None)
			< MinimumInitialOverlapDepenetrationCMPerSec
		|| PhysicsSettings == nullptr
		|| PhysicsSettings->ContactOffsetMultiplier <= 0.0f
		|| PhysicsSettings->MinContactOffset <= 0.0f
		|| PhysicsSettings->MaxContactOffset
			< PhysicsSettings->MinContactOffset)
	{
		OutError = FString::Printf(
			TEXT("DeferredImpactCollisionPolicyInvalid:Collision=%d:Object=%d:Sim=%d:CCD=%d:MaxDepen=%.3f:Contact=%s"),
			static_cast<int32>(Component.GetCollisionEnabled()),
			static_cast<int32>(Component.GetCollisionObjectType()),
			Component.IsSimulatingPhysics() ? 1 : 0,
			BodyInstance != nullptr && BodyInstance->bUseCCD ? 1 : 0,
			Component.GetMaxDepenetrationVelocity(NAME_None),
			PhysicsSettings != nullptr ? TEXT("Valid") : TEXT("Missing"));
		return false;
	}
	return true;
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
	// Bird/device impacts are routed explicitly by their owners. Enabling a
	// delegate on every promoted brick turns one dense Chaos contact island into
	// an uncontrolled callback storm, so peer contact damage is centralized in
	// the material system instead.
	Visual->SetNotifyRigidBodyCollision(false);
	Visual->SetGenerateOverlapEvents(false);
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

void AABTSM7BuildingModule::ConfigureChaosBodyProfile(
	const FABTSM7ChaosBodyProfile& Profile)
{
	Profile.ApplyTo(*Visual);
}

bool AABTSM7BuildingModule::ApplyImpactDamage(const float DamageGain)
{
	if (bBroken || bRecycled) return false;
	CurrentDamage = FMath::Max(0.0f, CurrentDamage + DamageGain);
	return CurrentDamage >= BreakDamage;
}

void AABTSM7BuildingModule::ActivateDynamic(const FVector& Impulse, const FVector& InPlanetCenter, const float GravityAcceleration)
{
	bOverflowKinematic = false;
	bOverflowPendingBreak = false;
	bOverflowKinematicSettled = false;
	OverflowKinematicGroundedFrames = 0;
	OverflowKinematicLinearVelocity = FVector::ZeroVector;
	OverflowKinematicAngularVelocityDegrees = FVector::ZeroVector;
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
	FABTSM7DeferredImpactCollisionPolicy::ApplyTo(*Visual);
	Visual->SetEnableGravity(false);
	Visual->WakeAllRigidBodies();
	Visual->AddImpulse(Impulse, NAME_None, true);
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	ContactDamageEnabledTimeSeconds = Now + ContactDamageGraceSeconds;
	LastDamageImpactSeconds = -BIG_NUMBER;
	bDynamic = true;
	bTerrainPenetrationReported = false;
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

bool AABTSM7BuildingModule::ReactivatePreservingSiteUniformGravity(
	const FVector& Impulse)
{
	if (!bSiteUniformGravity || PlanarGravityUp.IsNearlyZero()
		|| GravityAccelerationCMPerSec2 <= 0.0f || bCompoundChild)
	{
		return false;
	}
	const FVector SavedSiteUp = PlanarGravityUp;
	const float SavedAcceleration = GravityAccelerationCMPerSec2;
	ActivateDynamicPlanar(Impulse, SavedSiteUp, SavedAcceleration);
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
	if (!FABTSM7DeferredImpactCollisionPolicy::VerifyDynamic(
		*Visual, OutError))
	{
		return false;
	}
	UE_LOG(LogABTSRuntime, Verbose,
		TEXT("[ABTS][M7][SiteUniformLaunch][CollisionIdentity]")
		TEXT(" Module=%s ComponentObjectType=%d Shapes=%d")
		TEXT(" ShapeFilterChannel=%d CCD=1 MaxDepenetration=%.3f")
		TEXT(" ContactOffsetPolicy=Valid Accepted=1"),
		*GetName(), static_cast<int32>(Visual->GetCollisionObjectType()),
		ShapeCount, static_cast<int32>(ABTSDeveloperObstacleChannel),
		Visual->GetMaxDepenetrationVelocity(NAME_None));
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
	bTerrainPenetrationReported = false;
	Visual->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Visual->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	Visual->SetSimulatePhysics(false);
	Visual->SetCollisionObjectType(ABTSDeveloperObstacleChannel);
	Visual->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void AABTSM7BuildingModule::BeginOverflowKinematic(
	const FVector& InitialLinearVelocity,
	const FVector& InitialAngularVelocityDegrees,
	const FVector& InSiteUp,
	const float InGravityAcceleration)
{
	if (bBroken || bRecycled || !IsValid(Visual)) return;
	bDynamic = false;
	bOverflowKinematicSettled = false;
	OverflowKinematicGroundedFrames = 0;
	bSiteUniformGravity = true;
	bPlanarGravity = true;
	PlanarGravityUp = InSiteUp.GetSafeNormal();
	if (PlanarGravityUp.IsNearlyZero()) PlanarGravityUp = FVector::UpVector;
	GravityAccelerationCMPerSec2 = FMath::Max(0.0f, InGravityAcceleration);
	bOverflowKinematic = true;
	bOverflowPendingBreak = false;
	bOverflowKinematicSettled = false;
	OverflowKinematicGroundedFrames = 0;
	OverflowKinematicLinearVelocity = InitialLinearVelocity;
	OverflowKinematicAngularVelocityDegrees = InitialAngularVelocityDegrees;
	Visual->SetSimulatePhysics(false);
	Visual->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Visual->SetEnableGravity(false);
	Visual->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Visual->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	Visual->SetVisibility(true, true);
	SetActorHiddenInGame(false);
}

FVector AABTSM7BuildingModule::PredictOverflowKinematicLocation(
	const float FixedDeltaSeconds) const
{
	if (bOverflowKinematicSettled)
	{
		return GetActorLocation();
	}
	const float DeltaSeconds = FMath::Max(0.0f, FixedDeltaSeconds);
	const FVector PredictedVelocity = OverflowKinematicLinearVelocity
		- PlanarGravityUp * GravityAccelerationCMPerSec2 * DeltaSeconds;
	return GetActorLocation() + PredictedVelocity * DeltaSeconds;
}

void AABTSM7BuildingModule::TickOverflowKinematic(const float FixedDeltaSeconds,
	const FVector* GroundContactLocation)
{
	if (!bOverflowKinematic || bBroken || bRecycled || !IsValid(Visual)) return;
	if (bOverflowKinematicSettled) return;
	const float DeltaSeconds = FMath::Max(0.0f, FixedDeltaSeconds);
	OverflowKinematicLinearVelocity -= PlanarGravityUp
		* GravityAccelerationCMPerSec2 * DeltaSeconds;
	FQuat NewRotation = GetActorQuat();
	const float AngularSpeed = OverflowKinematicAngularVelocityDegrees.Size();
	if (AngularSpeed > KINDA_SMALL_NUMBER)
	{
		const FQuat DeltaRotation(OverflowKinematicAngularVelocityDegrees / AngularSpeed,
			FMath::DegreesToRadians(AngularSpeed * DeltaSeconds));
		NewRotation = (DeltaRotation * NewRotation).GetNormalized();
	}
	const bool bGroundedContact = GroundContactLocation != nullptr;
	const FVector NewLocation = bGroundedContact ? *GroundContactLocation
		: GetActorLocation() + OverflowKinematicLinearVelocity * DeltaSeconds;
	if (bGroundedContact)
	{
		// A fallback brick may settle only at a verified terrain/foundation
		// contact.  It remains visible and independently addressable; this is
		// deliberately not a suspended static proxy.
		OverflowKinematicLinearVelocity = FVector::ZeroVector;
		OverflowKinematicAngularVelocityDegrees = FVector::ZeroVector;
		constexpr int32 RequiredGroundedFrames = 30;
		++OverflowKinematicGroundedFrames;
		bOverflowKinematicSettled =
			OverflowKinematicGroundedFrames >= RequiredGroundedFrames;
	}
	else
	{
		OverflowKinematicGroundedFrames = 0;
	}
	SetActorLocationAndRotation(NewLocation, NewRotation,
		false, nullptr, ETeleportType::TeleportPhysics);
}

void AABTSM7BuildingModule::AddOverflowKinematicImpact(
	const FVector& VelocityDelta,
	const FVector& AngularVelocityDeltaDegrees,
	const float DamageGain)
{
	if (!bOverflowKinematic || bBroken || bRecycled) return;
	bOverflowKinematicSettled = false;
	OverflowKinematicGroundedFrames = 0;
	OverflowKinematicLinearVelocity += VelocityDelta;
	OverflowKinematicAngularVelocityDegrees += AngularVelocityDeltaDegrees;
	if (ApplyImpactDamage(DamageGain))
	{
		// Retain the brick until it receives an exact body; never hide/destroy it.
		bOverflowPendingBreak = true;
	}
}

bool AABTSM7BuildingModule::FreezeSettledOverflowKinematic()
{
	if (!bOverflowKinematic || !bOverflowKinematicSettled || bBroken
		|| bRecycled || !IsValid(Visual))
	{
		return false;
	}
	// Settling is awarded only by the actor's swept terrain/foundation/settled
	// support check after thirty fixed 60 Hz contacts.  This never turns an
	// airborne low-velocity brick into a static collider.
	bOverflowKinematic = false;
	bOverflowPendingBreak = false;
	bDynamic = false;
	Visual->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Visual->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	Visual->SetSimulatePhysics(false);
	Visual->SetCollisionObjectType(ABTSDeveloperObstacleChannel);
	Visual->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	return true;
}

bool AABTSM7BuildingModule::CanFreezeAsGroundedRoot() const
{
	if (!bDynamic || !IsValid(Visual) || Visual->IsAnyRigidBodyAwake())
	{
		return false;
	}
	FString PenetrationDiagnostic;
	if (DetectSleepingTerrainPenetration(PenetrationDiagnostic))
	{
		return false;
	}
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}
	const FVector GravityUp = bSiteUniformGravity
		? PlanarGravityUp.GetSafeNormal()
		: FVector::UpVector;
	const FVector SafeUp = GravityUp.IsNearlyZero()
		? FVector::UpVector : GravityUp;
	const FBoxSphereBounds Bounds = Visual->Bounds;
	const float DownExtent = FMath::Abs(SafeUp.X) * Bounds.BoxExtent.X
		+ FMath::Abs(SafeUp.Y) * Bounds.BoxExtent.Y
		+ FMath::Abs(SafeUp.Z) * Bounds.BoxExtent.Z;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(M7GroundedFreeze), false, this);
	FHitResult GroundHit;
	const FVector Start = Bounds.Origin - SafeUp * FMath::Max(0.0f, DownExtent - 2.0f);
	const bool bHasGround = World->LineTraceSingleByChannel(
		GroundHit, Start, Start - SafeUp * 8.0f, ABTSDeveloperObstacleChannel,
		QueryParams);
	const AABTSM73StableBuildingActor* OwnerBuilding =
		DamageLifecycleOwner.Get();
	const bool bGroundIsFrozenBuildingSupport = OwnerBuilding != nullptr
		&& OwnerBuilding->IsJuryDemoFixedSixGroundSupportPrimitive(
			GroundHit.GetComponent());
	const bool bGroundIsPlanetTerrain =
		Cast<AABTSM3Planet>(GroundHit.GetActor()) != nullptr;
	if (!bHasGround || (!bGroundIsFrozenBuildingSupport && !bGroundIsPlanetTerrain))
	{
		return false;
	}
	return true;
}

bool AABTSM7BuildingModule::FreezeIfSafelyGrounded()
{
	if (!CanFreezeAsGroundedRoot())
	{
		return false;
	}
	Freeze();
	return true;
}

FVector AABTSM7BuildingModule::GetCurrentGravityUp() const
{
	if (bSiteUniformGravity && !PlanarGravityUp.IsNearlyZero())
	{
		return PlanarGravityUp.GetSafeNormal();
	}
	if (!bPlanarGravity)
	{
		const FVector Radial = GetActorLocation() - PlanetCenter;
		if (!Radial.IsNearlyZero()) return Radial.GetSafeNormal();
	}
	return FVector::UpVector;
}

void AABTSM7BuildingModule::RecycleUnsupportedDebris()
{
	if (!IsValid(Visual) || bBroken || bRecycled)
	{
		return;
	}
	// This is intentionally not Freeze(): an unresolved, airborne body must not
	// become a visible static obstacle when gameplay returns to Walk.
	bDynamic = false;
	bSiteUniformGravity = false;
	bOverflowKinematic = false;
	bRecycled = true;
	Visual->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Visual->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	Visual->SetSimulatePhysics(false);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetVisibility(false, true);
	SetActorHiddenInGame(true);
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
	if (bBroken || bRecycled) return false;
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
	if (!Visual->IsAnyRigidBodyAwake())
	{
		FString PenetrationDiagnostic;
		if (!bTerrainPenetrationReported
			&& DetectSleepingTerrainPenetration(PenetrationDiagnostic))
		{
			bTerrainPenetrationReported = true;
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M7][DeferredImpactTerrain] Module=%s")
				TEXT(" SleepingPenetration=1 FailClosed=1 Detail=%s"),
				*GetName(), *PenetrationDiagnostic);
			// No transform correction: re-wake only so the real terrain collision
			// can resolve the contact through Chaos.
			Visual->WakeAllRigidBodies();
		}
		return;
	}
	const FVector GravityDirection = bPlanarGravity
		? -PlanarGravityUp
		: (PlanetCenter - GetActorLocation()).GetSafeNormal();
	TryApplyNonInvalidatingAcceleration(
		*Visual, GravityDirection * GravityAccelerationCMPerSec2);
}

bool AABTSM7BuildingModule::DetectSleepingTerrainPenetration(
	FString& OutDiagnostic) const
{
	OutDiagnostic.Reset();
	if (!IsValid(Visual) || GetWorld() == nullptr
		|| Visual->Bounds.SphereRadius <= 0.0f)
	{
		return false;
	}
	constexpr float AllowedPenetrationCM = 3.0f;
	const FVector Location = Visual->GetComponentLocation();
	for (TActorIterator<AABTSM3Planet> It(GetWorld()); It; ++It)
	{
		const AABTSM3Planet* Planet = *It;
		if (!IsValid(Planet) || !IsValid(Planet->ContinuousSurface.Get()))
		{
			continue;
		}
		const FVector Radial = Location - Planet->GetPlanetCenterWorld();
		const float CenterRadius = Radial.Size();
		const FVector Direction = Radial.GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			continue;
		}
		const FVector Extent = Visual->Bounds.BoxExtent;
		const float RadialExtent = FMath::Abs(Direction.X) * Extent.X
			+ FMath::Abs(Direction.Y) * Extent.Y
			+ FMath::Abs(Direction.Z) * Extent.Z;
		const float TerrainRadius = Planet->GetSurfaceRadiusAtDirection(Direction);
		const float BottomRadius = CenterRadius - RadialExtent;
		if (BottomRadius < TerrainRadius - AllowedPenetrationCM)
		{
			OutDiagnostic = FString::Printf(
				TEXT("Planet=%s BottomRadius=%.3f TerrainRadius=%.3f Depth=%.3f"),
				*GetNameSafe(Planet), BottomRadius, TerrainRadius,
				TerrainRadius - BottomRadius);
			return true;
		}
	}
	return false;
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
