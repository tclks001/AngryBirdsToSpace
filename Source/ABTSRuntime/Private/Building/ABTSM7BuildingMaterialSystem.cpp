// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM7BuildingMaterialSystem.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM7MaterialProfileLibrary.h"
#include "Building/ABTSM73StableBuildingActor.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Building/ABTSM7PenetrationValidator.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Terrain/ABTSM3Planet.h"
#include "UObject/ConstructorHelpers.h"
#include "World/ABTSCollisionChannels.h"

namespace
{
	constexpr float SharedCubeSizeCM = 100.0f;
}

AABTSM7BuildingMaterialSystem::AABTSM7BuildingMaterialSystem()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	WoodBrickHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("WoodBrickHISM"));
	StoneBrickHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("StoneBrickHISM"));
	IronBrickHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("IronBrickHISM"));
	GlassBrickHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("GlassBrickHISM"));
	CrystalBrickHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("CrystalBrickHISM"));
	for (UHierarchicalInstancedStaticMeshComponent* HISM : {WoodBrickHISM.Get(), StoneBrickHISM.Get(), IronBrickHISM.Get(), GlassBrickHISM.Get(), CrystalBrickHISM.Get()})
	{
		HISM->SetupAttachment(Root);
		HISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		HISM->SetCollisionObjectType(ABTSDeveloperObstacleChannel);
		HISM->SetCollisionResponseToAllChannels(ECR_Block);
	}
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WoodBrick(TEXT("/Game/StaticMesh/BrickMaterials/MI_Bricks_Wood.MI_Bricks_Wood"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> StoneBrick(TEXT("/Game/StaticMesh/BrickMaterials/MI_Bricks_Stone.MI_Bricks_Stone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SteelBrick(TEXT("/Game/StaticMesh/BrickMaterials/MI_Bricks_Steel.MI_Bricks_Steel"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlassBrick(TEXT("/Game/StaticMesh/BrickMaterials/MI_Bricks_Glass.MI_Bricks_Glass"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CrystalBrick(TEXT("/Game/StaticMesh/BrickMaterials/MI_Bricks_Crystal.MI_Bricks_Crystal"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SimpleCord(TEXT("/Game/StaticMesh/Cord/Simple/MI_Cord_Simple.MI_Cord_Simple"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SteelCord(TEXT("/Game/StaticMesh/Cord/Steel/MI_Cord_Steel.MI_Cord_Steel"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DynamiteMaterial(TEXT("/Game/StaticMesh/Dynamite/MI_Dynamite.MI_Dynamite"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SpringMaterialAsset(TEXT("/Game/StaticMesh/Spring/MI_Spring.MI_Spring"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> DynamiteMesh(TEXT("/Game/StaticMesh/Dynamite/SM_Dynamite.SM_Dynamite"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SpringMesh(TEXT("/Game/StaticMesh/Spring/SM_Spring.SM_Spring"));
	if (Cube.Succeeded()) SharedBrickMesh = Cube.Object;
	if (Cylinder.Succeeded()) SharedCylinderMesh = Cylinder.Object;
	if (BasicShapeMaterial.Succeeded()) FallbackMaterialParent = BasicShapeMaterial.Object;
	if (WoodBrick.Succeeded()) WoodMaterial = WoodBrick.Object;
	if (StoneBrick.Succeeded()) StoneMaterial = StoneBrick.Object;
	if (SteelBrick.Succeeded()) IronMaterial = SteelBrick.Object;
	if (GlassBrick.Succeeded()) GlassMaterial = GlassBrick.Object;
	if (CrystalBrick.Succeeded()) CrystalMaterial = CrystalBrick.Object;
	if (SimpleCord.Succeeded()) RopeMaterial = SimpleCord.Object;
	if (SteelCord.Succeeded()) ChainMaterial = SteelCord.Object;
	if (DynamiteMaterial.Succeeded()) ExplosiveMaterial = DynamiteMaterial.Object;
	if (SpringMaterialAsset.Succeeded()) SpringMaterial = SpringMaterialAsset.Object;
	if (DynamiteMesh.Succeeded()) ExplosivePresentationMesh = DynamiteMesh.Object;
	if (SpringMesh.Succeeded()) PistonPresentationMesh = SpringMesh.Object;

	MaterialProfiles = FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
}

void AABTSM7BuildingMaterialSystem::BeginPlay()
{
	Super::BeginPlay();
	for (TActorIterator<AABTSM3Planet> It(GetWorld()); It; ++It) if (It->IsPlanetReady()) { Planet = *It; break; }
	for (UHierarchicalInstancedStaticMeshComponent* HISM : {WoodBrickHISM.Get(), StoneBrickHISM.Get(), IronBrickHISM.Get(), GlassBrickHISM.Get(), CrystalBrickHISM.Get()}) HISM->SetStaticMesh(SharedBrickMesh);
	UMaterialInterface* FallbackParent = FallbackMaterialParent ? FallbackMaterialParent.Get() : UMaterial::GetDefaultMaterial(MD_Surface);
	const auto MakeFallback = [this, FallbackParent](const TCHAR* Name, const EABTSM7BuildingMaterial Type)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(FallbackParent, this, FName(Name));
		MID->SetVectorParameterValue(TEXT("Color"), GetProfile(Type).FallbackColor);
		MID->SetVectorParameterValue(TEXT("BaseColor"), GetProfile(Type).FallbackColor);
		return MID;
	};
	WoodFallbackMaterial = MakeFallback(TEXT("M7WoodFallback"), EABTSM7BuildingMaterial::Wood);
	StoneFallbackMaterial = MakeFallback(TEXT("M7StoneFallback"), EABTSM7BuildingMaterial::Stone);
	IronFallbackMaterial = MakeFallback(TEXT("M7IronFallback"), EABTSM7BuildingMaterial::Iron);
	GlassFallbackMaterial = MakeFallback(TEXT("M7GlassFallback"), EABTSM7BuildingMaterial::Glass);
	CrystalFallbackMaterial = MakeFallback(TEXT("M7CrystalFallback"), EABTSM7BuildingMaterial::Crystal);
	WoodBrickHISM->SetMaterial(0, GetMaterial(EABTSM7BuildingMaterial::Wood));
	StoneBrickHISM->SetMaterial(0, GetMaterial(EABTSM7BuildingMaterial::Stone));
	IronBrickHISM->SetMaterial(0, GetMaterial(EABTSM7BuildingMaterial::Iron));
	GlassBrickHISM->SetMaterial(0, GetMaterial(EABTSM7BuildingMaterial::Glass));
	CrystalBrickHISM->SetMaterial(0, GetMaterial(EABTSM7BuildingMaterial::Crystal));
	ApplyHISMPhysicalMaterial(*WoodBrickHISM, EABTSM7BuildingMaterial::Wood, TEXT("ABTSWoodBrickPhysics"));
	ApplyHISMPhysicalMaterial(*StoneBrickHISM, EABTSM7BuildingMaterial::Stone, TEXT("ABTSStoneBrickPhysics"));
	ApplyHISMPhysicalMaterial(*IronBrickHISM, EABTSM7BuildingMaterial::Iron, TEXT("ABTSIronBrickPhysics"));
	ApplyHISMPhysicalMaterial(*GlassBrickHISM, EABTSM7BuildingMaterial::Glass, TEXT("ABTSGlassBrickPhysics"));
	ApplyHISMPhysicalMaterial(*CrystalBrickHISM, EABTSM7BuildingMaterial::Crystal, TEXT("ABTSCrystalBrickPhysics"));
	if (bSpawnTestSetAtStart) SpawnTestSet();
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M7] MaterialSystem ready Planet=%d TestSet=%d Profiles=%d"), Planet.IsValid() ? 1 : 0, bSpawnTestSetAtStart ? 1 : 0, MaterialProfiles.Num());
}

int32 AABTSM7BuildingMaterialSystem::AddBrick(const FABTSM7BrickSpec& Spec, const FTransform& WorldTransform)
{
	UHierarchicalInstancedStaticMeshComponent* HISM = GetBrickHISM(Spec.Material);
	if (!HISM || !SharedBrickMesh) return INDEX_NONE;
	FTransform InstanceTransform = WorldTransform;
	const FVector SafeDimensions(
		FMath::Max(1.0f, Spec.DimensionsCM.X),
		FMath::Max(1.0f, Spec.DimensionsCM.Y),
		FMath::Max(1.0f, Spec.DimensionsCM.Z));
	InstanceTransform.SetScale3D(WorldTransform.GetScale3D() * (SafeDimensions / SharedCubeSizeCM));
	return HISM->AddInstance(InstanceTransform, true);
}

AABTSM7BuildingModule* AABTSM7BuildingMaterialSystem::SpawnBrickModule(
	const FABTSM7BrickSpec& Spec,
	const FTransform& WorldTransform)
{
	return SpawnBrickModuleInternal(Spec, WorldTransform, true);
}

AABTSM7BuildingModule* AABTSM7BuildingMaterialSystem::SpawnStaticBrickModule(
	const FABTSM7BrickSpec& Spec,
	const FTransform& WorldTransform)
{
	return SpawnBrickModuleInternal(Spec, WorldTransform, false);
}

AABTSM7BuildingModule* AABTSM7BuildingMaterialSystem::SpawnBrickModuleInternal(
	const FABTSM7BrickSpec& Spec,
	const FTransform& WorldTransform,
	const bool bRegisterForLaunchPhysics)
{
	if (GetWorld() == nullptr || SharedBrickMesh == nullptr) return nullptr;
	FTransform BrickTransform = WorldTransform;
	const FVector SafeDimensions(
		FMath::Max(1.0f, Spec.DimensionsCM.X),
		FMath::Max(1.0f, Spec.DimensionsCM.Y),
		FMath::Max(1.0f, Spec.DimensionsCM.Z));
	BrickTransform.SetScale3D(WorldTransform.GetScale3D() * (SafeDimensions / SharedCubeSizeCM));
	AABTSM7BuildingModule* Module =
		GetWorld()->SpawnActorDeferred<AABTSM7BuildingModule>(
			AABTSM7BuildingModule::StaticClass(), BrickTransform,
			this, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Module == nullptr) return nullptr;
	Module->ConfigureBrickBeforeFinishSpawning(
		SharedBrickMesh, GetMaterial(Spec.Material), Spec.Material);
	// Mesh and scale are immutable frozen geometry, so install them before the
	// component enters the scene. Apply the physical material only after normal
	// registration: BodyInstance mass initialization depends on that ordering.
	UGameplayStatics::FinishSpawningActor(Module, BrickTransform);
	Module->ConfigureImpactPhysics(GetProfile(Spec.Material));
	if (bRegisterForLaunchPhysics)
	{
		Modules.Add(Module);
	}
	return Module;
}

AABTSM7BuildingModule* AABTSM7BuildingMaterialSystem::SpawnSuspension(const FABTSM7SuspensionSpec& Spec, const FTransform& WorldTransform)
{
	if (Spec.Kind != EABTSM7ModuleKind::Rope && Spec.Kind != EABTSM7ModuleKind::IronChain) return nullptr;
	FActorSpawnParameters Params; Params.Owner = this; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM7BuildingModule* Module = GetWorld()->SpawnActor<AABTSM7BuildingModule>(AABTSM7BuildingModule::StaticClass(), WorldTransform, Params);
	if (!Module) return nullptr;
	const EABTSM7BuildingMaterial Material = Spec.Kind == EABTSM7ModuleKind::Rope ? EABTSM7BuildingMaterial::Wood : EABTSM7BuildingMaterial::Iron;
	Module->ConfigureCylinder(SharedCylinderMesh, Spec.Kind == EABTSM7ModuleKind::Rope ? RopeMaterial.Get() : ChainMaterial.Get(), Spec.Kind, Material, Spec.LengthCM, Spec.RadiusCM * 2.0f, WorldTransform);
	Module->ConfigureImpactPhysics(GetProfile(Material));
	Modules.Add(Module);
	return Module;
}

AABTSM7BuildingModule* AABTSM7BuildingMaterialSystem::SpawnDevice(const FABTSM7DeviceSpec& Spec, const FTransform& WorldTransform)
{
	return SpawnDeviceWithOverrides(Spec, WorldTransform, nullptr, nullptr);
}

AABTSM7BuildingModule* AABTSM7BuildingMaterialSystem::SpawnDeviceWithOverrides(
	const FABTSM7DeviceSpec& Spec,
	const FTransform& WorldTransform,
	UStaticMesh* OverrideMesh,
	UMaterialInterface* OverrideMaterial)
{
	if (Spec.Kind != EABTSM7ModuleKind::ExplosiveBarrel && Spec.Kind != EABTSM7ModuleKind::SpringPiston) return nullptr;
	FActorSpawnParameters Params; Params.Owner = this; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM7BuildingModule* Module = GetWorld()->SpawnActor<AABTSM7BuildingModule>(AABTSM7BuildingModule::StaticClass(), WorldTransform, Params);
	if (!Module) return nullptr;
	UStaticMesh* RuntimeMesh = OverrideMesh ? OverrideMesh : SharedCylinderMesh.Get();
	UMaterialInterface* RuntimeMaterial = OverrideMaterial
		? OverrideMaterial
		: (Spec.Kind == EABTSM7ModuleKind::ExplosiveBarrel ? ExplosiveMaterial.Get() : SpringMaterial.Get());
	if (RuntimeMaterial == nullptr) RuntimeMaterial = GetMaterial(EABTSM7BuildingMaterial::Iron);
	FTransform ModuleTransform = WorldTransform;
	const FVector ShapeScale = ModuleTransform.GetScale3D().GetAbs();
	ModuleTransform.SetScale3D(FVector::OneVector);
	Module->ConfigureCylinder(RuntimeMesh, RuntimeMaterial, Spec.Kind, EABTSM7BuildingMaterial::Iron, Spec.LengthCM, Spec.DiameterCM, ModuleTransform, ShapeScale);
	Module->ConfigureImpactPhysics(GetProfile(EABTSM7BuildingMaterial::Iron));
	Modules.Add(Module);
	return Module;
}

AABTSM7BuildingModule* AABTSM7BuildingMaterialSystem::SpawnVoxelDevice(
	const FABTSM7DeviceSpec& Spec,
	const FTransform& WorldTransform)
{
	return SpawnVoxelDeviceInternal(Spec, WorldTransform, true);
}

AABTSM7BuildingModule* AABTSM7BuildingMaterialSystem::SpawnStaticVoxelDevice(
	const FABTSM7DeviceSpec& Spec,
	const FTransform& WorldTransform)
{
	return SpawnVoxelDeviceInternal(Spec, WorldTransform, false);
}

AABTSM7BuildingModule* AABTSM7BuildingMaterialSystem::SpawnVoxelDeviceInternal(
	const FABTSM7DeviceSpec& Spec,
	const FTransform& WorldTransform,
	const bool bRegisterForLaunchPhysics)
{
	if (Spec.Kind != EABTSM7ModuleKind::ExplosiveBarrel
		&& Spec.Kind != EABTSM7ModuleKind::SpringPiston)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM7BuildingModule* Module = GetWorld()->SpawnActor<AABTSM7BuildingModule>(
		AABTSM7BuildingModule::StaticClass(), WorldTransform, Params);
	if (Module == nullptr)
	{
		return nullptr;
	}
	const bool bBarrel = Spec.Kind == EABTSM7ModuleKind::ExplosiveBarrel;
	UMaterialInterface* RuntimeMaterial = bBarrel
		? ExplosiveMaterial.Get() : SpringMaterial.Get();
	if (RuntimeMaterial == nullptr)
	{
		RuntimeMaterial = GetMaterial(EABTSM7BuildingMaterial::Iron);
	}
	Module->ConfigureVoxelDevice(
		SharedCylinderMesh,
		bBarrel ? ExplosivePresentationMesh.Get() : PistonPresentationMesh.Get(),
		RuntimeMaterial, Spec.Kind, Spec.LengthCM, Spec.DiameterCM,
		WorldTransform);
	Module->ConfigureImpactPhysics(GetProfile(EABTSM7BuildingMaterial::Iron));
	if (bRegisterForLaunchPhysics)
	{
		Modules.Add(Module);
	}
	return Module;
}

UHierarchicalInstancedStaticMeshComponent* AABTSM7BuildingMaterialSystem::GetBrickHISM(const EABTSM7BuildingMaterial Material) const
{
	switch (Material)
	{
	case EABTSM7BuildingMaterial::Wood: return WoodBrickHISM;
	case EABTSM7BuildingMaterial::Stone: return StoneBrickHISM;
	case EABTSM7BuildingMaterial::Iron: return IronBrickHISM;
	case EABTSM7BuildingMaterial::Glass: return GlassBrickHISM;
	case EABTSM7BuildingMaterial::Crystal: return CrystalBrickHISM;
	default: return nullptr;
	}
}

UMaterialInterface* AABTSM7BuildingMaterialSystem::GetMaterial(const EABTSM7BuildingMaterial Material) const
{
	switch (Material)
	{
	case EABTSM7BuildingMaterial::Wood: return WoodMaterial ? WoodMaterial.Get() : WoodFallbackMaterial.Get();
	case EABTSM7BuildingMaterial::Stone: return StoneMaterial ? StoneMaterial.Get() : StoneFallbackMaterial.Get();
	case EABTSM7BuildingMaterial::Iron: return IronMaterial ? IronMaterial.Get() : IronFallbackMaterial.Get();
	case EABTSM7BuildingMaterial::Glass: return GlassMaterial ? GlassMaterial.Get() : GlassFallbackMaterial.Get();
	case EABTSM7BuildingMaterial::Crystal: return CrystalMaterial ? CrystalMaterial.Get() : CrystalFallbackMaterial.Get();
	default: return nullptr;
	}
}

const FABTSM7MaterialProfile& AABTSM7BuildingMaterialSystem::GetProfile(const EABTSM7BuildingMaterial Material) const
{
	if (const FABTSM7MaterialProfile* Found = MaterialProfiles.FindByPredicate([Material](const FABTSM7MaterialProfile& P){ return P.Material == Material; })) return *Found;
	static const TArray<FABTSM7MaterialProfile> Defaults = FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
	if (const FABTSM7MaterialProfile* Found = FABTSM7MaterialProfileLibrary::FindProfile(Defaults, Material)) return *Found;
	return Defaults[0];
}

void AABTSM7BuildingMaterialSystem::CopyMaterialProfiles(TArray<FABTSM7MaterialProfile>& OutProfiles) const
{
	OutProfiles = MaterialProfiles;
}

void AABTSM7BuildingMaterialSystem::NotifyBrickRecovered(const EABTSM7BuildingMaterial Material, const int32 Quantity)
{
	if (Quantity <= 0) return;
	OnMaterialRecovered.Broadcast(Material, Quantity);
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M8][Recovery] Material=%d Quantity=%d"), static_cast<int32>(Material), Quantity);
}

float AABTSM7BuildingMaterialSystem::ComputeDamageGain(const FABTSM7MaterialProfile& Profile, const float NormalSpeedCMPerSec, const float BreakSpeedCMPerSec) const
{
	if (NormalSpeedCMPerSec < 60.0f) return 0.0f;
	return Profile.DamageAtBreakSpeed * FMath::Square(NormalSpeedCMPerSec / FMath::Max(BreakSpeedCMPerSec, 1.0f));
}

uint64 AABTSM7BuildingMaterialSystem::GetHISMDamageKey(const UHierarchicalInstancedStaticMeshComponent& HISM, const int32 InstanceIndex) const
{
	FTransform Transform;
	if (!HISM.GetInstanceTransform(InstanceIndex, Transform, true)) return 0;
	const FVector Location = Transform.GetLocation();
	uint32 Hash = PointerHash(&HISM);
	Hash = HashCombineFast(Hash, GetTypeHash(FMath::RoundToInt(Location.X / 5.0f)));
	Hash = HashCombineFast(Hash, GetTypeHash(FMath::RoundToInt(Location.Y / 5.0f)));
	Hash = HashCombineFast(Hash, GetTypeHash(FMath::RoundToInt(Location.Z / 5.0f)));
	// Instance indices are compacted by HISM removal, so do not use them as a
	// persistent damage identity. A static instance keeps its world transform.
	return static_cast<uint64>(Hash);
}

void AABTSM7BuildingMaterialSystem::ApplyHISMPhysicalMaterial(UHierarchicalInstancedStaticMeshComponent& HISM, const EABTSM7BuildingMaterial Material, const TCHAR* DebugName)
{
	const FABTSM7MaterialProfile& Profile = GetProfile(Material);
	UPhysicalMaterial* Physical = NewObject<UPhysicalMaterial>(this, FName(DebugName), RF_Transient);
	Physical->Friction = Profile.DynamicFriction; Physical->StaticFriction = Profile.StaticFriction; Physical->Restitution = Profile.Restitution; Physical->Density = Profile.DensityGPerCubicCM;
	Physical->bOverrideFrictionCombineMode = true; Physical->FrictionCombineMode = EFrictionCombineMode::Average;
	Physical->bOverrideRestitutionCombineMode = true; Physical->RestitutionCombineMode = EFrictionCombineMode::Average;
	HISM.SetPhysMaterialOverride(Physical);
	RuntimePhysicalMaterials.Add(Physical);
}

float AABTSM7BuildingMaterialSystem::GetBirdThresholdScale(const EABTSBirdId BirdId) const
{
	switch (BirdId)
	{
	case EABTSBirdId::Blue: return 1.10f;
	case EABTSBirdId::Yellow: return 0.82f;
	case EABTSBirdId::Black: return 0.72f;
	default: return 1.0f;
	}
}

bool AABTSM7BuildingMaterialSystem::OwnsPrimitive(const UPrimitiveComponent* Component) const
{
	if (!Component) return false;
	if (Component == WoodBrickHISM || Component == StoneBrickHISM || Component == IronBrickHISM || Component == GlassBrickHISM || Component == CrystalBrickHISM) return true;
	return Cast<AABTSM7BuildingModule>(Component->GetOwner()) != nullptr;
}

AABTSM7BuildingModule* AABTSM7BuildingMaterialSystem::PromoteBrick(UHierarchicalInstancedStaticMeshComponent& HISM, const int32 InstanceIndex, const EABTSM7BuildingMaterial Material, const FVector& Impulse, const bool bActivateImmediately)
{
	FTransform Transform;
	if (!HISM.GetInstanceTransform(InstanceIndex, Transform, true)) return nullptr;
	HISM.RemoveInstance(InstanceIndex);
	FActorSpawnParameters Params; Params.Owner = this; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM7BuildingModule* Module = GetWorld()->SpawnActor<AABTSM7BuildingModule>(AABTSM7BuildingModule::StaticClass(), Transform, Params);
	if (!Module) return nullptr;
	Module->ConfigureBrick(SharedBrickMesh, GetMaterial(Material), Material, Transform);
	Module->ConfigureImpactPhysics(GetProfile(Material));
	if (bActivateImmediately) ActivateModuleForLaunch(*Module, Impulse);
	Modules.Add(Module);
	return Module;
}

void AABTSM7BuildingMaterialSystem::ActivateModuleForLaunch(AABTSM7BuildingModule& Module, const FVector& InitialImpulse)
{
	MarkPhysicsActivity();
	Module.SetContactDamageGraceSeconds(LaunchContactDamageGraceSeconds);
	if (bLaunchPhysicsPlanar)
	{
		Module.ActivateDynamicPlanar(InitialImpulse, LaunchGravityReference, LaunchGravityAccelerationCMPerSec2);
	}
	else
	{
		const FVector Center = !LaunchGravityReference.IsNearlyZero()
			? LaunchGravityReference
			: (Planet.IsValid() ? Planet->GetPlanetCenterWorld() : FVector::ZeroVector);
		Module.ActivateDynamic(InitialImpulse, Center, LaunchGravityAccelerationCMPerSec2);
	}
}

FABTSM7PenetrationValidationStats AABTSM7BuildingMaterialSystem::ValidateAndRepairPendingModules(
	const TArray<AABTSM7BuildingModule*>& PendingModules) const
{
	return GetWorld() != nullptr
		? FABTSM7PenetrationValidator::ValidateAndRepair(
			*GetWorld(), PendingModules, InitialPenetrationRepairToleranceCM, InitialPenetrationRepairPasses)
		: FABTSM7PenetrationValidationStats();
}

FABTSM7PenetrationValidationStats
AABTSM7BuildingMaterialSystem::ValidatePendingModuleInterpenetration(
	const TArray<AABTSM7BuildingModule*>& PendingModules) const
{
	return GetWorld() != nullptr
		? FABTSM7PenetrationValidator::ValidateAndRepair(
			*GetWorld(), PendingModules,
			/*RepairToleranceCM=*/0.0f,
			/*MaximumRepairPasses=*/1,
			/*bPendingModulesOnly=*/true)
		: FABTSM7PenetrationValidationStats();
}

void AABTSM7BuildingMaterialSystem::BeginLaunchPhysics(
	const bool bPlanar,
	const FVector& GravityReference,
	const float GravityAcceleration,
	const float ContactDamageGraceSeconds)
{
	bLaunchPhysicsPlanar = bPlanar;
	LaunchGravityReference = GravityReference;
	LaunchGravityAccelerationCMPerSec2 = FMath::Max(0.0f, GravityAcceleration);
	const FABTSM7ChaosBodyProfile BodyProfile =
		FABTSM7ChaosBodyProfile::Production();
	const FABTSM7ChaosWorldProfile WorldProfile =
		FABTSM7ChaosWorldProfile::CaptureProduction();
	LastLaunchChaosBodyProfileHash = BodyProfile.ComputeCrc32();
	LastLaunchChaosWorldProfileHash = WorldProfile.ComputeCrc32();
	if (ContactDamageGraceSeconds >= 0.0f)
	{
		LaunchContactDamageGraceSeconds = ContactDamageGraceSeconds;
	}

	int32 PromotedCount = 0;
	const auto PromoteAll = [this, &PromotedCount](UHierarchicalInstancedStaticMeshComponent* HISM, const EABTSM7BuildingMaterial Material)
	{
		if (HISM == nullptr) return;
		for (int32 Index = HISM->GetInstanceCount() - 1; Index >= 0; --Index)
		{
			if (PromoteBrick(*HISM, Index, Material, FVector::ZeroVector, false)) ++PromotedCount;
		}
	};
	PromoteAll(WoodBrickHISM, EABTSM7BuildingMaterial::Wood);
	PromoteAll(StoneBrickHISM, EABTSM7BuildingMaterial::Stone);
	PromoteAll(IronBrickHISM, EABTSM7BuildingMaterial::Iron);
	PromoteAll(GlassBrickHISM, EABTSM7BuildingMaterial::Glass);
	PromoteAll(CrystalBrickHISM, EABTSM7BuildingMaterial::Crystal);

	TArray<AABTSM7BuildingModule*> PendingModules;
	for (int32 Index = Modules.Num() - 1; Index >= 0; --Index)
	{
		if (AABTSM7BuildingModule* Module = Modules[Index].Get())
		{
			if (!Module->IsDynamic()) PendingModules.Add(Module);
		}
		else
		{
			Modules.RemoveAtSwap(Index);
		}
	}
	const FABTSM7PenetrationValidationStats Validation = ValidateAndRepairPendingModules(PendingModules);
	for (AABTSM7BuildingModule* Module : PendingModules)
	{
		if (IsValid(Module) && !Module->IsDynamic()) ActivateModuleForLaunch(*Module);
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7][LaunchGravity] Planar=%d Promoted=%d Activated=%d GravityModel=%s GravityReference=%s Gravity=%.1f ContactGrace=%.3f ChaosBodyHash=%u ChaosWorldHash=%u %s PenetrationPairs=%d Repairs=%d LargeErrors=%d RemainingSmall=%d MaxDepth=%.4f Tolerance=%.4f Passes=%d"),
		bLaunchPhysicsPlanar ? 1 : 0,
		PromotedCount,
		PendingModules.Num(),
		bLaunchPhysicsPlanar
			? TEXT("PlanarConstantAcceleration")
			: TEXT("RadialConstantAcceleration"),
		*LaunchGravityReference.ToString(),
		LaunchGravityAccelerationCMPerSec2,
		LaunchContactDamageGraceSeconds,
		LastLaunchChaosBodyProfileHash,
		LastLaunchChaosWorldProfileHash,
		*WorldProfile.ToLogString(),
		Validation.DetectedPairCount,
		Validation.RepairCount,
		Validation.LargeErrorPairCount,
		Validation.RemainingSmallPairCount,
		Validation.MaximumDetectedDepthCM,
		InitialPenetrationRepairToleranceCM,
		InitialPenetrationRepairPasses);
}

bool AABTSM7BuildingMaterialSystem::BeginSiteUniformLaunchPhysics(
	const TConstArrayView<AABTSM7BuildingModule*> TargetModules,
	const FVector& SiteLocationWorldCM,
	const FVector& SupportCenterWorldCM,
	const float GravityAcceleration,
	const float ContactDamageGraceSeconds,
	const bool bPenetrationPrevalidated)
{
	LastSiteUniformGravityPolicyHash = 0;
	LastSiteUniformGravityUp = FVector::ZeroVector;
	FABTSM7SiteUniformGravityPolicy Policy;
	if (!FABTSM7SiteUniformGravityPolicy::TryDerive(
		SiteLocationWorldCM, SupportCenterWorldCM,
		GravityAcceleration, Policy)
		|| TargetModules.IsEmpty())
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7][SiteUniformLaunch] Rejected Reason=PolicyOrTargetsInvalid Targets=%d Site=%s Center=%s Gravity=%.3f"),
			TargetModules.Num(), *SiteLocationWorldCM.ToString(),
			*SupportCenterWorldCM.ToString(), GravityAcceleration);
		return false;
	}

	TSet<const AABTSM7BuildingModule*> UniqueTargets;
	TArray<AABTSM7BuildingModule*> PendingModules;
	PendingModules.Reserve(TargetModules.Num());
	for (AABTSM7BuildingModule* Module : TargetModules)
	{
		if (!IsValid(Module)
			|| Module->GetOwner() != this
			|| Module->IsDynamic()
			|| UniqueTargets.Contains(Module))
		{
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M7][SiteUniformLaunch] Rejected Reason=TargetOwnershipInvalid Module=%s Dynamic=%d Duplicate=%d"),
				*GetNameSafe(Module),
				Module != nullptr && Module->IsDynamic() ? 1 : 0,
				UniqueTargets.Contains(Module) ? 1 : 0);
			return false;
		}
		UniqueTargets.Add(Module);
		PendingModules.Add(Module);
	}

	const FABTSM7ChaosBodyProfile BodyProfile =
		FABTSM7ChaosBodyProfile::Production();
	const FABTSM7ChaosWorldProfile WorldProfile =
		FABTSM7ChaosWorldProfile::CaptureProduction();
	LastLaunchChaosBodyProfileHash = BodyProfile.ComputeCrc32();
	LastLaunchChaosWorldProfileHash = WorldProfile.ComputeCrc32();
	LastSiteUniformGravityPolicyHash = Policy.ComputeCrc32();
	LastSiteUniformGravityUp = Policy.SiteUp;
	const float EffectiveGraceSeconds = ContactDamageGraceSeconds >= 0.0f
		? ContactDamageGraceSeconds
		: LaunchContactDamageGraceSeconds;
	const FABTSM7PenetrationValidationStats Validation =
		bPenetrationPrevalidated
			? FABTSM7PenetrationValidationStats()
			: ValidateAndRepairPendingModules(PendingModules);
	for (AABTSM7BuildingModule* Module : PendingModules)
	{
		if (!Modules.ContainsByPredicate(
			[Module](const TWeakObjectPtr<AABTSM7BuildingModule>& Candidate)
			{
				return Candidate.Get() == Module;
			}))
		{
			// Static-registration devices/caps are deliberately excluded from
			// the global launch queue; an explicit per-site launch adopts only
			// the caller-provided building subset into runtime ownership.
			Modules.Add(Module);
		}
		Module->SetContactDamageGraceSeconds(EffectiveGraceSeconds);
		if (!Module->ActivateDynamicSiteUniform(FVector::ZeroVector, Policy))
		{
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M7][SiteUniformLaunch] Rejected Reason=ActivationRejected Module=%s"),
				*GetNameSafe(Module));
			return false;
		}
	}
	MarkPhysicsActivity();
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7][SiteUniformLaunch] Accepted=1 Activated=%d %s ContactGrace=%.3f ChaosBodyHash=%u ChaosWorldHash=%u %s PenetrationPrevalidated=%d PenetrationPairs=%d Repairs=%d LargeErrors=%d RemainingSmall=%d MaxDepth=%.4f Tolerance=%.4f Passes=%d"),
		PendingModules.Num(), *Policy.ToLogString(), EffectiveGraceSeconds,
		LastLaunchChaosBodyProfileHash, LastLaunchChaosWorldProfileHash,
		*WorldProfile.ToLogString(), bPenetrationPrevalidated ? 1 : 0,
		Validation.DetectedPairCount,
		Validation.RepairCount, Validation.LargeErrorPairCount,
		Validation.RemainingSmallPairCount,
		Validation.MaximumDetectedDepthCM,
		InitialPenetrationRepairToleranceCM,
		InitialPenetrationRepairPasses);
	return true;
}

void AABTSM7BuildingMaterialSystem::AdoptUnweldedCompoundChild(
	AABTSM7BuildingModule& Module)
{
	if (Module.GetOwner() != this)
	{
		return;
	}
	if (!Modules.ContainsByPredicate(
		[&Module](const TWeakObjectPtr<AABTSM7BuildingModule>& Candidate)
		{
			return Candidate.Get() == &Module;
		}))
	{
		Modules.Add(&Module);
	}
}

bool AABTSM7BuildingMaterialSystem::HandleBirdImpact(UPrimitiveComponent* Component, const int32 InstanceIndex, const float NormalSpeedCMPerSec, const FVector& IncomingVelocity, const EABTSBirdId BirdId)
{
	if (!OwnsPrimitive(Component)) return false;
	EABTSM7BuildingMaterial Material = EABTSM7BuildingMaterial::Wood;
	if (Component == StoneBrickHISM) Material = EABTSM7BuildingMaterial::Stone;
	else if (Component == IronBrickHISM) Material = EABTSM7BuildingMaterial::Iron;
	else if (Component == GlassBrickHISM) Material = EABTSM7BuildingMaterial::Glass;
	else if (Component == CrystalBrickHISM) Material = EABTSM7BuildingMaterial::Crystal;
	else if (const AABTSM7BuildingModule* Module = Cast<AABTSM7BuildingModule>(Component->GetOwner())) Material = Module->GetBuildingMaterial();
	const FABTSM7MaterialProfile& Profile = GetProfile(Material);
	const float Scale = GetBirdThresholdScale(BirdId);
	const float Knock = Profile.KnockSpeedCMPerSec * Scale;
	const float Break = Profile.BreakSpeedCMPerSec * Scale;
	if (UHierarchicalInstancedStaticMeshComponent* HISM = Cast<UHierarchicalInstancedStaticMeshComponent>(Component))
	{
		if (InstanceIndex < 0) return true;
		const uint64 DamageKey = GetHISMDamageKey(*HISM, InstanceIndex);
		const float DamageAfter = HISMDamageByStableKey.FindRef(DamageKey) + ComputeDamageGain(Profile, NormalSpeedCMPerSec, Break);
		HISMDamageByStableKey.Add(DamageKey, DamageAfter);
		if (DamageAfter >= Profile.BreakDamage)
		{
			if (HISM->RemoveInstance(InstanceIndex)) NotifyBrickRecovered(Material);
			HISMDamageByStableKey.Remove(DamageKey);
			UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M7][DamageBreak] Material=%d Damage=%.1f/%.1f"), static_cast<int32>(Material), DamageAfter, Profile.BreakDamage);
		}
		else if (NormalSpeedCMPerSec >= Knock)
		{
			if (AABTSM7BuildingModule* Module = PromoteBrick(*HISM, InstanceIndex, Material, IncomingVelocity.GetSafeNormal() * NormalSpeedCMPerSec * Profile.PushVelocityTransfer))
			{
				Module->ApplyImpactDamage(DamageAfter);
				HISMDamageByStableKey.Remove(DamageKey);
			}
		}
	}
	else if (AABTSM7BuildingModule* Module = Cast<AABTSM7BuildingModule>(Component->GetOwner()))
	{
		ApplyImpactToModule(*Module, NormalSpeedCMPerSec, IncomingVelocity,
			BirdId, EABTSM73E1DamageCause::BirdImpact,
			/*bApplyGameplayTransferImpulse=*/true);
	}
	return true;
}

bool AABTSM7BuildingMaterialSystem::ApplyImpactToModule(
	AABTSM7BuildingModule& Module,
	const float NormalSpeedCMPerSec,
	const FVector& IncomingVelocity,
	const EABTSBirdId BirdId,
	const EABTSM73E1DamageCause Cause,
	const bool bApplyGameplayTransferImpulse)
{
	if (Module.IsBroken() || !FMath::IsFinite(NormalSpeedCMPerSec)
		|| NormalSpeedCMPerSec <= 0.0f)
	{
		return false;
	}
	const FABTSM7MaterialProfile& Profile =
		GetProfile(Module.GetBuildingMaterial());
	const float Scale = GetBirdThresholdScale(BirdId);
	const float Knock = Profile.KnockSpeedCMPerSec * Scale;
	const float Break = Profile.BreakSpeedCMPerSec * Scale;
	const bool bDamageBreak = Module.ApplyImpactDamage(
		ComputeDamageGain(Profile, NormalSpeedCMPerSec, Break));
	const bool bShouldBreak = bDamageBreak
		|| NormalSpeedCMPerSec >= Break * 1.35f;
	if (bShouldBreak)
	{
		const EABTSM7ModuleKind Kind = Module.GetModuleKind();
		const EABTSM7BuildingMaterial Material = Module.GetBuildingMaterial();
		const FVector Origin = Module.GetActorLocation();
		const FVector Axis = Module.GetActorUpVector();
		// BreakModule marks the Actor pending-destroy. Record the already proven,
		// game-thread break decision while the exact RuntimeModules identity is
		// still queryable; BreakModule is idempotent and cannot reject here after
		// the entry bBroken guard above.
		if (AABTSM73StableBuildingActor* Building =
			Module.GetDamageLifecycleOwner())
		{
			Building->NotifyJuryDemoE1ModuleDamage(
				Module, Cause, true, NormalSpeedCMPerSec);
		}
		if (Module.BreakModule())
		{
			if (Kind == EABTSM7ModuleKind::Brick)
			{
				NotifyBrickRecovered(Material);
			}
			if (Kind == EABTSM7ModuleKind::ExplosiveBarrel)
			{
				ApplyRadialBlast(Origin, BarrelDestroyRadiusCM,
					BarrelImpulseRadiusCM, BarrelImpulseSpeedCMPerSec);
			}
			else if (Kind == EABTSM7ModuleKind::SpringPiston)
			{
				ApplyDirectionalBlast(Origin, Axis,
					PistonDestroyLengthCM, PistonImpulseLengthCM,
					PistonEffectRadiusCM, PistonImpulseSpeedCMPerSec);
			}
			return true;
		}
	}

	if (AABTSM73StableBuildingActor* Building =
		Module.GetDamageLifecycleOwner())
	{
		Building->NotifyJuryDemoE1ModuleDamage(
			Module, Cause, false, NormalSpeedCMPerSec);
	}
	if (NormalSpeedCMPerSec >= Knock && bApplyGameplayTransferImpulse)
	{
		const FVector TransferImpulse = IncomingVelocity.GetSafeNormal()
			* NormalSpeedCMPerSec * Profile.PushVelocityTransfer;
		if (Module.IsDynamic())
		{
			// Preserve SiteUniformTangentGravity and the wake/sleep identity.
			Module.ApplyDynamicImpactImpulse(TransferImpulse);
		}
		else
		{
			ActivateModuleForLaunch(Module, TransferImpulse);
		}
	}
	return true;
}

void AABTSM7BuildingMaterialSystem::HandleModuleChainImpact(AABTSM7BuildingModule& Source, const FHitResult& Hit, const float NormalSpeedCMPerSec)
{
	if (NormalSpeedCMPerSec < 300.0f) return;
	const FVector SourceVelocity = Source.GetMeshComponent() != nullptr
		? Source.GetMeshComponent()->GetPhysicsLinearVelocityAtPoint(
			Hit.ImpactPoint)
		: FVector::ZeroVector;
	// The source must take real collision damage too. This is what permits a
	// displaced Crystal to break after falling onto terrain instead of remaining
	// immortal because the terrain is not an M7-owned primitive.
	ApplyImpactToModule(Source, NormalSpeedCMPerSec, SourceVelocity,
		EABTSBirdId::Red, EABTSM73E1DamageCause::ModuleContact,
		/*bApplyGameplayTransferImpulse=*/false);

	UPrimitiveComponent* TargetComponent = Hit.GetComponent();
	AABTSM7BuildingModule* TargetModule = Cast<AABTSM7BuildingModule>(
		TargetComponent != nullptr ? TargetComponent->GetOwner() : nullptr);
	if (TargetModule != nullptr && TargetModule != &Source
		&& !TargetModule->IsDynamic() && OwnsPrimitive(TargetComponent))
	{
		// Dynamic M7 peers receive their own OnComponentHit callback. Only a
		// static peer needs target-side forwarding, otherwise one contact would
		// be counted twice for each body.
		ApplyImpactToModule(*TargetModule, NormalSpeedCMPerSec,
			SourceVelocity, EABTSBirdId::Red,
			EABTSM73E1DamageCause::ModuleContact,
			/*bApplyGameplayTransferImpulse=*/false);
	}
	else if (TargetModule == nullptr
		&& Cast<UHierarchicalInstancedStaticMeshComponent>(TargetComponent)
		&& OwnsPrimitive(TargetComponent))
	{
		// Preserve the legacy static-HISM chain path outside promoted Fixed-Six.
		HandleBirdImpact(TargetComponent, Hit.Item, NormalSpeedCMPerSec,
			SourceVelocity, EABTSBirdId::Red);
	}
}

void AABTSM7BuildingMaterialSystem::BreakOrImpulsePrimitive(UPrimitiveComponent* Component, const int32 InstanceIndex, const FVector& ImpulseDirection, const float ImpulseSpeed, const bool bDestroy)
{
	if (UHierarchicalInstancedStaticMeshComponent* HISM = Cast<UHierarchicalInstancedStaticMeshComponent>(Component))
	{
		EABTSM7BuildingMaterial Material = EABTSM7BuildingMaterial::Wood;
		if (HISM == StoneBrickHISM) Material = EABTSM7BuildingMaterial::Stone;
		else if (HISM == IronBrickHISM) Material = EABTSM7BuildingMaterial::Iron;
		else if (HISM == GlassBrickHISM) Material = EABTSM7BuildingMaterial::Glass;
		else if (HISM == CrystalBrickHISM) Material = EABTSM7BuildingMaterial::Crystal;
		if (bDestroy)
		{
			if (HISM->RemoveInstance(InstanceIndex)) NotifyBrickRecovered(Material);
		}
		else PromoteBrick(*HISM, InstanceIndex, Material, ImpulseDirection.GetSafeNormal() * ImpulseSpeed);
	}
	else if (AABTSM7BuildingModule* Module = Cast<AABTSM7BuildingModule>(Component ? Component->GetOwner() : nullptr))
	{
		if (bDestroy)
		{
			const EABTSM7ModuleKind Kind = Module->GetModuleKind();
			const EABTSM7BuildingMaterial Material = Module->GetBuildingMaterial();
			if (Module->BreakModule() && Kind == EABTSM7ModuleKind::Brick)
			{
				NotifyBrickRecovered(Material);
			}
		}
		else ActivateModuleForLaunch(*Module, ImpulseDirection.GetSafeNormal() * ImpulseSpeed);
	}
}

void AABTSM7BuildingMaterialSystem::ApplyRadialBlast(const FVector& Origin, const float DestroyRadiusCM, const float ImpulseRadiusCM, const float ImpulseSpeedCMPerSec)
{
	MarkPhysicsActivity();
	for (UHierarchicalInstancedStaticMeshComponent* HISM : {WoodBrickHISM.Get(), StoneBrickHISM.Get(), IronBrickHISM.Get(), GlassBrickHISM.Get(), CrystalBrickHISM.Get()})
	{
		TArray<int32> Indices = HISM->GetInstancesOverlappingSphere(Origin, ImpulseRadiusCM, true);
		Indices.Sort(TGreater<int32>());
		for (const int32 Index : Indices)
		{
			FTransform Transform; if (!HISM->GetInstanceTransform(Index, Transform, true)) continue;
			const FVector Delta = Transform.GetLocation() - Origin;
			BreakOrImpulsePrimitive(HISM, Index, Delta, ImpulseSpeedCMPerSec * (1.0f - Delta.Size() / FMath::Max(ImpulseRadiusCM, 1.0f)), Delta.Size() <= DestroyRadiusCM);
		}
	}
	TArray<TWeakObjectPtr<AABTSM7BuildingModule>> Snapshot = Modules;
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& Weak : Snapshot) if (AABTSM7BuildingModule* Module = Weak.Get())
	{
		const FVector Delta = Module->GetActorLocation() - Origin;
		if (Delta.Size() <= ImpulseRadiusCM) BreakOrImpulsePrimitive(Module->GetMeshComponent(), INDEX_NONE, Delta, ImpulseSpeedCMPerSec * (1.0f - Delta.Size() / ImpulseRadiusCM), Delta.Size() <= DestroyRadiusCM);
	}
}

void AABTSM7BuildingMaterialSystem::ApplyDirectionalBlast(const FVector& Origin, const FVector& Axis, const float DestroyLengthCM, const float ImpulseLengthCM, const float EffectRadiusCM, const float ImpulseSpeedCMPerSec)
{
	MarkPhysicsActivity();
	const FVector UnitAxis = Axis.GetSafeNormal();
	for (UHierarchicalInstancedStaticMeshComponent* HISM : {WoodBrickHISM.Get(), StoneBrickHISM.Get(), IronBrickHISM.Get(), GlassBrickHISM.Get(), CrystalBrickHISM.Get()})
	{
		TArray<int32> Indices = HISM->GetInstancesOverlappingSphere(Origin, ImpulseLengthCM + EffectRadiusCM, true);
		Indices.Sort(TGreater<int32>());
		for (const int32 Index : Indices)
		{
			FTransform Transform; if (!HISM->GetInstanceTransform(Index, Transform, true)) continue;
			const FVector Delta = Transform.GetLocation() - Origin;
			const float Axial = FVector::DotProduct(Delta, UnitAxis);
			if (FMath::Abs(Axial) > ImpulseLengthCM || FVector::VectorPlaneProject(Delta, UnitAxis).Size() > EffectRadiusCM) continue;
			const FVector Direction = UnitAxis * (Axial >= 0.0f ? 1.0f : -1.0f);
			BreakOrImpulsePrimitive(HISM, Index, Direction, ImpulseSpeedCMPerSec * (1.0f - FMath::Abs(Axial) / ImpulseLengthCM), FMath::Abs(Axial) <= DestroyLengthCM);
		}
	}
	TArray<TWeakObjectPtr<AABTSM7BuildingModule>> Snapshot = Modules;
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& Weak : Snapshot) if (AABTSM7BuildingModule* Module = Weak.Get())
	{
		const FVector Delta = Module->GetActorLocation() - Origin;
		const float Axial = FVector::DotProduct(Delta, UnitAxis);
		if (FMath::Abs(Axial) > ImpulseLengthCM || FVector::VectorPlaneProject(Delta, UnitAxis).Size() > EffectRadiusCM) continue;
		const FVector Direction = UnitAxis * (Axial >= 0.0f ? 1.0f : -1.0f);
		BreakOrImpulsePrimitive(Module->GetMeshComponent(), INDEX_NONE, Direction, ImpulseSpeedCMPerSec * (1.0f - FMath::Abs(Axial) / ImpulseLengthCM), FMath::Abs(Axial) <= DestroyLengthCM);
	}
}

void AABTSM7BuildingMaterialSystem::FreezeDynamicModules()
{
	int32 PreservedSiteUniformCount = 0;
	for (int32 Index = Modules.Num() - 1; Index >= 0; --Index)
	{
		if (AABTSM7BuildingModule* Module = Modules[Index].Get())
		{
			if (Module->UsesSiteUniformGravity())
			{
				++PreservedSiteUniformCount;
				continue;
			}
			Module->Freeze();
		}
		else
		{
			Modules.RemoveAtSwap(Index);
		}
	}
	if (PreservedSiteUniformCount > 0)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7][SiteUniformLaunch] M6GlobalFreezeSkipped=%d")
			TEXT(" Reason=ProductionSiteUniformBodiesRemainWakeable"),
			PreservedSiteUniformCount);
	}
}

void AABTSM7BuildingMaterialSystem::SetDynamicContactDamageGraceSeconds(const float Seconds)
{
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& WeakModule : Modules)
	{
		if (AABTSM7BuildingModule* Module = WeakModule.Get(); Module && Module->IsDynamic())
		{
			Module->SetContactDamageGraceSeconds(Seconds);
		}
	}
}

void AABTSM7BuildingMaterialSystem::AppendDynamicPhysicsBodies(TArray<UPrimitiveComponent*>& OutBodies) const
{
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& WeakModule : Modules)
	{
		const AABTSM7BuildingModule* Module = WeakModule.Get();
		if (Module == nullptr || !Module->IsDynamic()) continue;
		UStaticMeshComponent* Body = Module->GetMeshComponent();
		if (Body != nullptr && Body->IsSimulatingPhysics()) OutBodies.Add(Body);
	}
}

void AABTSM7BuildingMaterialSystem::MarkPhysicsActivity()
{
	if (const UWorld* World = GetWorld()) LastPhysicsActivityTimeSeconds = World->GetTimeSeconds();
}

void AABTSM7BuildingMaterialSystem::ConfigureTestSet(const bool bEnable, const FTransform& SpawnTransform)
{
	bSpawnTestSetAtStart = bEnable;
	TestSetTransform = SpawnTransform;
}

void AABTSM7BuildingMaterialSystem::SpawnTestSet()
{
	const FVector Up = TestSetTransform.GetUnitAxis(EAxis::Z);
	const FVector Right = TestSetTransform.GetUnitAxis(EAxis::Y);
	const FVector Forward = TestSetTransform.GetUnitAxis(EAxis::X);
	const FVector Base = TestSetTransform.GetLocation() + Forward * 850.0f + Up * 80.0f;
	const EABTSM7BuildingMaterial Materials[] = {EABTSM7BuildingMaterial::Wood, EABTSM7BuildingMaterial::Stone, EABTSM7BuildingMaterial::Iron, EABTSM7BuildingMaterial::Glass, EABTSM7BuildingMaterial::Crystal};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Materials); ++Index)
	{
		FABTSM7BrickSpec Spec; Spec.Material = Materials[Index]; Spec.DimensionsCM = FVector(220.0f, 90.0f, 70.0f + Index * 15.0f);
		AddBrick(Spec, FTransform(TestSetTransform.GetRotation(), Base + Right * ((Index - 2.0f) * 260.0f)));
	}
	FABTSM7SuspensionSpec Rope; Rope.Kind = EABTSM7ModuleKind::Rope; Rope.LengthCM = 320.0f; Rope.RadiusCM = 10.0f;
	FABTSM7SuspensionSpec Chain = Rope; Chain.Kind = EABTSM7ModuleKind::IronChain; Chain.RadiusCM = 14.0f;
	SpawnSuspension(Rope, FTransform(TestSetTransform.GetRotation(), Base + Forward * 300.0f - Right * 180.0f + Up * 150.0f));
	SpawnSuspension(Chain, FTransform(TestSetTransform.GetRotation(), Base + Forward * 300.0f + Right * 180.0f + Up * 150.0f));
	FABTSM7DeviceSpec Barrel; Barrel.Kind = EABTSM7ModuleKind::ExplosiveBarrel;
	FABTSM7DeviceSpec Piston = Barrel; Piston.Kind = EABTSM7ModuleKind::SpringPiston; Piston.LengthCM = 220.0f; Piston.DiameterCM = 75.0f;
	SpawnDevice(Barrel, FTransform(TestSetTransform.GetRotation(), Base + Forward * 600.0f - Right * 170.0f));
	SpawnDevice(Piston, FTransform(TestSetTransform.GetRotation(), Base + Forward * 600.0f + Right * 170.0f));
}
