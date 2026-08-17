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

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
	constexpr float SharedCubeSizeCM = 100.0f;

	/** Stable candidate builder shared by runtime aggregation and its automation. */
	void BuildStableSpatialContactCandidatePairs(
		const TConstArrayView<FBox> SweptBounds,
		const float CellSizeCM,
		TArray<uint64>& OutPairs)
	{
		OutPairs.Reset();
		const float CellSize = FMath::Max(36.0f, CellSizeCM);
		TMap<FIntVector, TArray<int32>> SpatialBuckets;
		for (int32 ModuleIndex = 0; ModuleIndex < SweptBounds.Num(); ++ModuleIndex)
		{
			const FBox& Bounds = SweptBounds[ModuleIndex];
			const FIntVector MinimumCell(
				FMath::FloorToInt(Bounds.Min.X / CellSize),
				FMath::FloorToInt(Bounds.Min.Y / CellSize),
				FMath::FloorToInt(Bounds.Min.Z / CellSize));
			const FIntVector MaximumCell(
				FMath::FloorToInt(Bounds.Max.X / CellSize),
				FMath::FloorToInt(Bounds.Max.Y / CellSize),
				FMath::FloorToInt(Bounds.Max.Z / CellSize));
			for (int32 X = MinimumCell.X; X <= MaximumCell.X; ++X)
			{
				for (int32 Y = MinimumCell.Y; Y <= MaximumCell.Y; ++Y)
				{
					for (int32 Z = MinimumCell.Z; Z <= MaximumCell.Z; ++Z)
					{
						SpatialBuckets.FindOrAdd(FIntVector(X, Y, Z)).Add(ModuleIndex);
					}
				}
			}
		}
		TArray<FIntVector> SortedCells;
		SpatialBuckets.GetKeys(SortedCells);
		SortedCells.Sort([](const FIntVector& Left, const FIntVector& Right)
		{
			return Left.X != Right.X ? Left.X < Right.X
				: (Left.Y != Right.Y ? Left.Y < Right.Y : Left.Z < Right.Z);
		});
		TSet<uint64> UniquePairKeys;
		for (const FIntVector& Cell : SortedCells)
		{
			const TArray<int32>& Bucket = SpatialBuckets.FindChecked(Cell);
			for (int32 LeftIndex = 0; LeftIndex < Bucket.Num(); ++LeftIndex)
			{
				for (int32 RightIndex = LeftIndex + 1; RightIndex < Bucket.Num(); ++RightIndex)
				{
					const int32 First = FMath::Min(Bucket[LeftIndex], Bucket[RightIndex]);
					const int32 Second = FMath::Max(Bucket[LeftIndex], Bucket[RightIndex]);
					UniquePairKeys.Add((static_cast<uint64>(First) << 32)
						| static_cast<uint32>(Second));
				}
			}
		}
		OutPairs = UniquePairKeys.Array();
		OutPairs.Sort();
	}

	void BuildGroundReachableFreezeOrder(
		const TConstArrayView<TArray<int32>> SupportChildren,
		const TConstArrayView<int32> GroundRoots,
		TArray<int32>& OutFreezeOrder)
	{
		OutFreezeOrder.Reset();
		TBitArray<> Reachable(false, SupportChildren.Num());
		for (const int32 RootIndex : GroundRoots)
		{
			if (SupportChildren.IsValidIndex(RootIndex)
				&& !Reachable[RootIndex])
			{
				Reachable[RootIndex] = true;
				OutFreezeOrder.Add(RootIndex);
			}
		}
		for (int32 Head = 0; Head < OutFreezeOrder.Num(); ++Head)
		{
			for (const int32 ChildIndex : SupportChildren[OutFreezeOrder[Head]])
			{
				if (SupportChildren.IsValidIndex(ChildIndex)
					&& !Reachable[ChildIndex])
				{
					Reachable[ChildIndex] = true;
					OutFreezeOrder.Add(ChildIndex);
				}
			}
		}
	}

	FBox MakeSweptBounds(const FBox& CurrentBounds, const FVector& Velocity,
		const float DeltaSeconds)
	{
		const FVector Delta = Velocity * FMath::Max(0.0f, DeltaSeconds);
		FBox Swept = CurrentBounds;
		Swept += CurrentBounds.Min - Delta;
		Swept += CurrentBounds.Max - Delta;
		return Swept;
	}

	bool PassesSweptNarrowContactGate(const FBox& LeftCurrentBounds,
		const FVector& LeftVelocity, const FBox& RightCurrentBounds,
		const FVector& RightVelocity, const float DeltaSeconds)
	{
		const float Duration = FMath::Max(0.0f, DeltaSeconds);
		const FVector LeftStart = LeftCurrentBounds.GetCenter()
			- LeftVelocity * Duration;
		const FVector RightStart = RightCurrentBounds.GetCenter()
			- RightVelocity * Duration;
		const FVector RelativeStart = LeftStart - RightStart;
		const FVector RelativeVelocity = LeftVelocity - RightVelocity;
		const float RelativeSpeedSquared = RelativeVelocity.SizeSquared();
		const float ClosestTime = RelativeSpeedSquared > KINDA_SMALL_NUMBER
			? FMath::Clamp(-FVector::DotProduct(RelativeStart, RelativeVelocity)
				/ RelativeSpeedSquared, 0.0f, Duration)
			: 0.0f;
		const FVector LeftCenter = LeftStart + LeftVelocity * ClosestTime;
		const FVector RightCenter = RightStart + RightVelocity * ClosestTime;
		const FBox LeftAtClosest(LeftCenter - LeftCurrentBounds.GetExtent(),
			LeftCenter + LeftCurrentBounds.GetExtent());
		const FBox RightAtClosest(RightCenter - RightCurrentBounds.GetExtent(),
			RightCenter + RightCurrentBounds.GetExtent());
		return LeftAtClosest.Intersect(RightAtClosest);
	}

	void BuildCyclicPairWindow(const int32 PairCount, const int32 RequestedBudget,
		int32& InOutCursor, TArray<int32>& OutPairIndices)
	{
		OutPairIndices.Reset();
		if (PairCount <= 0) return;
		const int32 Budget = FMath::Min(FMath::Max(1, RequestedBudget), PairCount);
		const int32 Start = InOutCursor % PairCount;
		for (int32 Offset = 0; Offset < Budget; ++Offset)
		{
			OutPairIndices.Add((Start + Offset) % PairCount);
		}
		InOutCursor = (Start + Budget) % PairCount;
	}
}

AABTSM7BuildingMaterialSystem::AABTSM7BuildingMaterialSystem()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
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

void AABTSM7BuildingMaterialSystem::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ProcessCentralizedDynamicContactDamage();
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
	if (Module.UsesSiteUniformGravity())
	{
		if (!Module.ReactivatePreservingSiteUniformGravity(InitialImpulse))
		{
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][M7][SiteUniformLaunch] ReactivationRejected Module=%s RadialFallback=Forbidden"),
				*Module.GetName());
		}
		return;
	}
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

void AABTSM7BuildingMaterialSystem::GatherLiveModulesForStylizedAdapter(
	TArray<AABTSM7BuildingModule*>& OutModules) const
{
	OutModules.Reset();
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& WeakModule : Modules)
	{
		AABTSM7BuildingModule* Module = WeakModule.Get();
		if (IsValid(Module) && !Module->IsBroken()
			&& Module->GetOwner() == this
			&& IsValid(Module->GetStylizedPresentationPrimitive()))
		{
			OutModules.Add(Module);
		}
	}
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
	int32 SkippedSiteUniformCount = 0;
	for (int32 Index = Modules.Num() - 1; Index >= 0; --Index)
	{
		if (AABTSM7BuildingModule* Module = Modules[Index].Get())
		{
			if (Module->IsRecycled())
			{
				continue;
			}
			if (!Module->IsDynamic() && Module->UsesSiteUniformGravity())
			{
				++SkippedSiteUniformCount;
			}
			else if (!Module->IsDynamic())
			{
				PendingModules.Add(Module);
			}
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
		TEXT("[ABTS][M7][LaunchGravity] Planar=%d Promoted=%d Activated=%d SkippedFrozenSiteUniform=%d M6RadialReactivation=Forbidden GravityModel=%s GravityReference=%s Gravity=%.1f ContactGrace=%.3f ChaosBodyHash=%u ChaosWorldHash=%u %s PenetrationPairs=%d Repairs=%d LargeErrors=%d RemainingSmall=%d MaxDepth=%.4f Tolerance=%.4f Passes=%d"),
		bLaunchPhysicsPlanar ? 1 : 0,
		PromotedCount,
		PendingModules.Num(),
		SkippedSiteUniformCount,
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
	const bool bPenetrationPrevalidated,
	const FABTSM7ChaosBodyProfile* const RuntimeBodyProfile)
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
			|| Module->IsRecycled()
			|| UniqueTargets.Contains(Module))
		{
			UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7][SiteUniformLaunch] Rejected Reason=TargetOwnershipInvalid Module=%s Dynamic=%d Recycled=%d Duplicate=%d"),
			*GetNameSafe(Module),
			Module != nullptr && Module->IsDynamic() ? 1 : 0,
			Module != nullptr && Module->IsRecycled() ? 1 : 0,
			UniqueTargets.Contains(Module) ? 1 : 0);
			return false;
		}
		UniqueTargets.Add(Module);
		PendingModules.Add(Module);
	}

	const FABTSM7ChaosBodyProfile BodyProfile = RuntimeBodyProfile != nullptr
		? *RuntimeBodyProfile : FABTSM7ChaosBodyProfile::Production();
	if (!BodyProfile.IsUsable())
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7][SiteUniformLaunch] Rejected Reason=BodyProfileInvalid"));
		return false;
	}
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
		Module->ConfigureChaosBodyProfile(BodyProfile);
		Module->SetContactDamageGraceSeconds(EffectiveGraceSeconds);
		if (!Module->ActivateDynamicSiteUniform(FVector::ZeroVector, Policy))
		{
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M7][SiteUniformLaunch] Rejected Reason=ActivationRejected Module=%s"),
				*GetNameSafe(Module));
			return false;
		}
		FString CollisionIdentityError;
		if (!Module->VerifyChaosDeveloperObstacleCollisionIdentity(
			CollisionIdentityError))
		{
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M7][SiteUniformLaunch]")
				TEXT(" Rejected Reason=CollisionIdentityInvalid Module=%s Detail=%s"),
				*GetNameSafe(Module), *CollisionIdentityError);
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
		if (Module->IsRecycled()) return false;
		return ApplyImpactToModule(*Module, NormalSpeedCMPerSec, IncomingVelocity,
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
	if (Module.IsBroken() || Module.IsRecycled()
		|| !FMath::IsFinite(NormalSpeedCMPerSec)
		|| NormalSpeedCMPerSec <= 0.0f)
	{
		return false;
	}
	if (AABTSM73StableBuildingActor* Building =
		Module.GetDamageLifecycleOwner())
	{
		const float InitialImpactRadiusCM =
			Module.GetModuleKind() == EABTSM7ModuleKind::ExplosiveBarrel
				? BarrelImpulseRadiusCM
				: Module.GetModuleKind() == EABTSM7ModuleKind::SpringPiston
					? PistonEffectRadiusCM : 0.0f;
		FString ActivationError;
		if (!Building->ActivateDeferredJuryDemoFixedSixChaosForFirstHit(
			Module, ActivationError, InitialImpactRadiusCM))
		{
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M7][FixedSixDeferredChaos][FirstHitRejected]")
				TEXT(" Module=%s Reason=%s DamageDropped=1"),
				*Module.GetName(), *ActivationError);
			return false;
		}
	}
	const FABTSM7MaterialProfile& Profile =
		GetProfile(Module.GetBuildingMaterial());
	const float Scale = GetBirdThresholdScale(BirdId);
	const float Knock = Profile.KnockSpeedCMPerSec * Scale;
	const float Break = Profile.BreakSpeedCMPerSec * Scale;
	if (Module.IsOverflowKinematic())
	{
		FString PromotionError;
		if (AABTSM73StableBuildingActor* Building =
			Module.GetDamageLifecycleOwner(); Building != nullptr
			&& Building->PromoteJuryDemoFixedSixOverflowForDirectImpact(
				Module, PromotionError))
		{
			// Exact body acquired; continue below with the original one-shot hit.
		}
		else
		{
			// QueryOnly queue bricks remain explicitly routed by this hit path.
			// If all reserved slots are exhausted, reject the bird interaction
			// rather than silently letting it tunnel through a fake collision.
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M7][OverflowKinematicQueue][DirectRejected]")
				TEXT(" Module=%s Reason=%s FailClosed=1"),
				*Module.GetName(), *PromotionError);
			return false;
		}
	}
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
	// This compatibility entry point is intentionally a queue, not immediate
	// per-brick damage.  Modules no longer subscribe to OnComponentHit; both
	// legacy contact notifications and the spatial pass below feed one bounded,
	// deterministic transaction at the same cadence.
	QueueCentralizedContactDamage(Source, NormalSpeedCMPerSec, SourceVelocity);

	UPrimitiveComponent* TargetComponent = Hit.GetComponent();
	AABTSM7BuildingModule* TargetModule = Cast<AABTSM7BuildingModule>(
		TargetComponent != nullptr ? TargetComponent->GetOwner() : nullptr);
	if (TargetModule != nullptr && TargetModule != &Source
		&& !TargetModule->IsDynamic() && OwnsPrimitive(TargetComponent))
	{
		QueueCentralizedContactDamage(*TargetModule, NormalSpeedCMPerSec,
			SourceVelocity);
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

void AABTSM7BuildingMaterialSystem::QueueCentralizedContactDamage(
	AABTSM7BuildingModule& Module,
	const float NormalSpeedCMPerSec,
	const FVector& IncomingVelocity)
{
	if (Module.IsBroken() || Module.IsRecycled()
		|| NormalSpeedCMPerSec < 300.0f)
	{
		return;
	}
	for (FCachedModuleContactDamage& Pending : PendingCentralizedContactDamage)
	{
		if (Pending.Module.Get() == &Module)
		{
			if (NormalSpeedCMPerSec > Pending.MaximumNormalSpeedCMPerSec)
			{
				Pending.MaximumNormalSpeedCMPerSec = NormalSpeedCMPerSec;
				Pending.IncomingVelocity = IncomingVelocity;
			}
			return;
		}
	}
	FCachedModuleContactDamage& Pending =
		PendingCentralizedContactDamage.Emplace_GetRef();
	Pending.Module = &Module;
	Pending.MaximumNormalSpeedCMPerSec = NormalSpeedCMPerSec;
	Pending.IncomingVelocity = IncomingVelocity;
}

void AABTSM7BuildingMaterialSystem::ProcessCentralizedDynamicContactDamage()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();
	if (Now - LastCentralizedContactDamageSeconds
		< FMath::Max(0.02f, CentralizedContactDamageIntervalSeconds))
	{
		return;
	}
	LastCentralizedContactDamageSeconds = Now;

	TArray<AABTSM7BuildingModule*> DynamicModules;
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& WeakModule : Modules)
	{
		AABTSM7BuildingModule* Module = WeakModule.Get();
		if (Module != nullptr && Module->IsDynamic() && !Module->IsBroken()
			&& !Module->IsRecycled()
			&& Module->GetMeshComponent() != nullptr
			&& Module->GetMeshComponent()->IsSimulatingPhysics())
		{
			DynamicModules.Add(Module);
		}
	}
	DynamicModules.Sort([](const AABTSM7BuildingModule& Left,
		const AABTSM7BuildingModule& Right)
	{
		const int32 LeftId = Left.GetDamageLifecycleBrickId();
		const int32 RightId = Right.GetDamageLifecycleBrickId();
		return LeftId != RightId ? LeftId < RightId
			: Left.GetFName().LexicalLess(Right.GetFName());
	});
	// Build a stable spatial hash from swept, expanded AABBs. This is O(active
	// + nearPairs), not O(active^2): a 720 cm beam spans only a handful of the
	// 180 cm cells, and every candidate pair is de-duplicated by its stable
	// sorted brick-id key before we apply the bounded transaction.
	const float CellSize = FMath::Max(36.0f, CentralizedContactCellSizeCM);
	const float SweepSeconds = FMath::Max(0.02f,
		CentralizedContactDamageIntervalSeconds);
	TArray<FBox> SweptBounds;
	SweptBounds.Reserve(DynamicModules.Num());
	for (int32 ModuleIndex = 0; ModuleIndex < DynamicModules.Num(); ++ModuleIndex)
	{
		UStaticMeshComponent* Body = DynamicModules[ModuleIndex]->GetMeshComponent();
		const FBox CurrentBounds = Body->Bounds.GetBox().ExpandBy(3.0f);
		SweptBounds.Add(MakeSweptBounds(CurrentBounds,
			Body->GetPhysicsLinearVelocity(), SweepSeconds));
	}
	TArray<uint64> CandidatePairs;
	BuildStableSpatialContactCandidatePairs(SweptBounds, CellSize,
		CandidatePairs);
	const int32 PairCount = CandidatePairs.Num();
	const int32 PairBudget = FMath::Min(FMath::Max(1,
		CentralizedContactPairBudget), PairCount);
	if (PairCount > 0)
	{
		int32 AcceptedDamagePairs = 0;
		TArray<int32> PairWindow;
		BuildCyclicPairWindow(PairCount, PairBudget,
			CentralizedContactPairCursor, PairWindow);
		for (const int32 PairIndex : PairWindow)
		{
			const uint64 PairKey = CandidatePairs[PairIndex];
			AABTSM7BuildingModule& Left = *DynamicModules[
				static_cast<int32>(PairKey >> 32)];
			AABTSM7BuildingModule& Right = *DynamicModules[
				static_cast<int32>(PairKey & 0xffffffffu)];
			if (!SweptBounds[static_cast<int32>(PairKey >> 32)].Intersect(
				SweptBounds[static_cast<int32>(PairKey & 0xffffffffu)]))
			{
				continue;
			}
			const FVector LeftVelocity = Left.GetMeshComponent()->GetPhysicsLinearVelocity();
			const FVector RightVelocity = Right.GetMeshComponent()->GetPhysicsLinearVelocity();
			const FBox LeftCurrentBounds = Left.GetMeshComponent()->Bounds.GetBox()
				.ExpandBy(3.0f);
			const FBox RightCurrentBounds = Right.GetMeshComponent()->Bounds.GetBox()
				.ExpandBy(3.0f);
			if (!PassesSweptNarrowContactGate(LeftCurrentBounds, LeftVelocity,
				RightCurrentBounds, RightVelocity, SweepSeconds))
			{
				continue;
			}
			const float RelativeSpeed = (LeftVelocity - RightVelocity).Size();
			if (RelativeSpeed >= 300.0f)
			{
				++AcceptedDamagePairs;
				QueueCentralizedContactDamage(Left, RelativeSpeed, RightVelocity);
				QueueCentralizedContactDamage(Right, RelativeSpeed, LeftVelocity);
			}
		}
		UE_LOG(LogABTSRuntime, Verbose,
			TEXT("[ABTS][M7][CentralContact.Total]")
			TEXT(" M7SpatialCandidatePairs=%d M7AcceptedDamagePairs=%d Budget=%d"),
			PairCount, AcceptedDamagePairs, PairBudget);
	}

	PendingCentralizedContactDamage.Sort([](
		const FCachedModuleContactDamage& Left,
		const FCachedModuleContactDamage& Right)
	{
		const AABTSM7BuildingModule* LeftModule = Left.Module.Get();
		const AABTSM7BuildingModule* RightModule = Right.Module.Get();
		const int32 LeftId = LeftModule != nullptr
			? LeftModule->GetDamageLifecycleBrickId() : MAX_int32;
		const int32 RightId = RightModule != nullptr
			? RightModule->GetDamageLifecycleBrickId() : MAX_int32;
		return LeftId != RightId ? LeftId < RightId
			: GetNameSafe(LeftModule) < GetNameSafe(RightModule);
	});
	int32 AppliedCount = 0;
	for (const FCachedModuleContactDamage& Pending : PendingCentralizedContactDamage)
	{
		if (AABTSM7BuildingModule* Module = Pending.Module.Get();
			Module != nullptr && !Module->IsBroken() && !Module->IsRecycled())
		{
			ApplyImpactToModule(*Module, Pending.MaximumNormalSpeedCMPerSec,
				Pending.IncomingVelocity, EABTSBirdId::Red,
				EABTSM73E1DamageCause::ModuleContact,
				/*bApplyGameplayTransferImpulse=*/false);
			++AppliedCount;
		}
	}
	if (AppliedCount > 0)
	{
		UE_LOG(LogABTSRuntime, Verbose,
			TEXT("[ABTS][M7][CentralizedContactDamage] Interval=%.3f Pairs=%d Budget=%d Applied=%d Deterministic=1"),
			CentralizedContactDamageIntervalSeconds, PairCount, PairBudget,
			AppliedCount);
	}
	PendingCentralizedContactDamage.Reset();
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
	// Fixed-six modules are caller-held static actors, so they are intentionally
	// absent from Modules until their first gameplay promotion. Discover one
	// deterministic seed per touched building before applying the radial effect;
	// the building expands that seed to its exact no-floating support closure.
	TMap<AABTSM73StableBuildingActor*, AABTSM7BuildingModule*> BlastSeeds;
	for (TActorIterator<AABTSM7BuildingModule> It(GetWorld()); It; ++It)
	{
		AABTSM7BuildingModule* Candidate = *It;
		AABTSM73StableBuildingActor* Building = Candidate != nullptr
			? Candidate->GetDamageLifecycleOwner() : nullptr;
		if (Candidate == nullptr || Candidate->GetOwner() != this
			|| Building == nullptr || Candidate->IsBroken() || Candidate->IsRecycled()
			|| FVector::DistSquared(Candidate->GetActorLocation(), Origin)
				> FMath::Square(ImpulseRadiusCM))
		{
			continue;
		}
		AABTSM7BuildingModule*& Existing = BlastSeeds.FindOrAdd(Building);
		if (Existing == nullptr
			|| FVector::DistSquared(Candidate->GetActorLocation(), Origin)
				< FVector::DistSquared(Existing->GetActorLocation(), Origin)
			|| (FMath::IsNearlyEqual(
				FVector::DistSquared(Candidate->GetActorLocation(), Origin),
				FVector::DistSquared(Existing->GetActorLocation(), Origin), 0.001)
				&& Candidate->GetDamageLifecycleBrickId()
					< Existing->GetDamageLifecycleBrickId()))
		{
			Existing = Candidate;
		}
	}
	TArray<TPair<AABTSM73StableBuildingActor*, AABTSM7BuildingModule*>>
		SortedBlastSeeds;
	for (const TPair<AABTSM73StableBuildingActor*, AABTSM7BuildingModule*>& Pair : BlastSeeds)
	{
		SortedBlastSeeds.Add(Pair);
	}
	SortedBlastSeeds.Sort([](const auto& Left, const auto& Right)
	{
		const int32 LeftId = Left.Value != nullptr
			? Left.Value->GetDamageLifecycleBrickId() : MAX_int32;
		const int32 RightId = Right.Value != nullptr
			? Right.Value->GetDamageLifecycleBrickId() : MAX_int32;
		return LeftId != RightId ? LeftId < RightId
			: GetNameSafe(Left.Key) < GetNameSafe(Right.Key);
	});
	TSet<const AABTSM73StableBuildingActor*> RejectedBlastBuildings;
	int32 PromotedBlastBuildingCount = 0;
	for (const TPair<AABTSM73StableBuildingActor*, AABTSM7BuildingModule*>& Pair : SortedBlastSeeds)
	{
		FString ClosureError;
		if (!Pair.Key->ActivateJuryDemoFixedSixImpactSupportClosure(
			*Pair.Value, ImpulseRadiusCM, ClosureError))
		{
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M7][FixedSixSupportClosure][BlastRejected] Building=%s Seed=%d Reason=%s"),
				*GetNameSafe(Pair.Key), Pair.Value->GetDamageLifecycleBrickId(),
				*ClosureError);
			RejectedBlastBuildings.Add(Pair.Key);
		}
		else
		{
			++PromotedBlastBuildingCount;
		}
	}
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7][FixedSixSupportClosure][BlastDiscovery]")
		TEXT(" Radius=%.1f Discovered=%d Promoted=%d Rejected=%d SameTransaction=1"),
		ImpulseRadiusCM, SortedBlastSeeds.Num(), PromotedBlastBuildingCount,
		RejectedBlastBuildings.Num());
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
		if (Module->IsRecycled()) continue;
		if (RejectedBlastBuildings.Contains(Module->GetDamageLifecycleOwner()))
		{
			// A rejected closure cannot fall through to partial damage on a
			// still-static building; that would violate no-floating fail-closed.
			continue;
		}
		const FVector Delta = Module->GetActorLocation() - Origin;
		if (Delta.Size() <= ImpulseRadiusCM)
		{
			const float ImpactSpeed = ImpulseSpeedCMPerSec * (1.0f
				- Delta.Size() / FMath::Max(1.0f, ImpulseRadiusCM));
			if (Module->IsOverflowKinematic())
			{
				const FABTSM7MaterialProfile& Profile =
					GetProfile(Module->GetBuildingMaterial());
				Module->AddOverflowKinematicImpact(Delta.GetSafeNormal() * ImpactSpeed,
					Delta.GetSafeNormal() * 120.0f,
					ComputeDamageGain(Profile, ImpactSpeed,
						Profile.BreakSpeedCMPerSec));
				continue;
			}
			BreakOrImpulsePrimitive(Module->GetMeshComponent(), INDEX_NONE, Delta,
				ImpactSpeed, Delta.Size() <= DestroyRadiusCM);
		}
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
		if (Module->IsRecycled()) continue;
		const FVector Delta = Module->GetActorLocation() - Origin;
		const float Axial = FVector::DotProduct(Delta, UnitAxis);
		if (FMath::Abs(Axial) > ImpulseLengthCM || FVector::VectorPlaneProject(Delta, UnitAxis).Size() > EffectRadiusCM) continue;
		const FVector Direction = UnitAxis * (Axial >= 0.0f ? 1.0f : -1.0f);
		const float ImpactSpeed = ImpulseSpeedCMPerSec * (1.0f
			- FMath::Abs(Axial) / FMath::Max(1.0f, ImpulseLengthCM));
		if (Module->IsOverflowKinematic())
		{
			const FABTSM7MaterialProfile& Profile =
				GetProfile(Module->GetBuildingMaterial());
			Module->AddOverflowKinematicImpact(Direction * ImpactSpeed,
				Direction * 120.0f, ComputeDamageGain(Profile, ImpactSpeed,
					Profile.BreakSpeedCMPerSec));
			continue;
		}
		BreakOrImpulsePrimitive(Module->GetMeshComponent(), INDEX_NONE, Direction,
			ImpactSpeed, FMath::Abs(Axial) <= DestroyLengthCM);
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

int32 AABTSM7BuildingMaterialSystem::FreezeAllDynamicModulesForWalkReturn()
{
	int32 FrozenCount = 0;
	int32 RecycledAirborneCount = 0;
	int32 SimulatingBefore = 0;
	TArray<AABTSM7BuildingModule*> SleepingModules;
	for (int32 Index = Modules.Num() - 1; Index >= 0; --Index)
	{
		AABTSM7BuildingModule* Module = Modules[Index].Get();
		if (Module == nullptr)
		{
			Modules.RemoveAtSwap(Index);
			continue;
		}
		if (!Module->IsDynamic())
		{
			continue;
		}
		UStaticMeshComponent* Body = Module->GetMeshComponent();
		SimulatingBefore += Body != nullptr && Body->IsSimulatingPhysics() ? 1 : 0;
		if (Body != nullptr && Body->IsSimulatingPhysics()
			&& !Body->IsAnyRigidBodyAwake())
		{
			SleepingModules.Add(Module);
		}
	}
	SleepingModules.Sort([](const AABTSM7BuildingModule& Left,
		const AABTSM7BuildingModule& Right)
	{
		const int32 LeftId = Left.GetDamageLifecycleBrickId();
		const int32 RightId = Right.GetDamageLifecycleBrickId();
		return LeftId != RightId ? LeftId < RightId
			: Left.GetFName().LexicalLess(Right.GetFName());
	});
	TMap<const AABTSM7BuildingModule*, int32> SleepingIndex;
	for (int32 Index = 0; Index < SleepingModules.Num(); ++Index)
	{
		SleepingIndex.Add(SleepingModules[Index], Index);
	}
	TArray<TArray<int32>> SupportChildren;
	SupportChildren.SetNum(SleepingModules.Num());
	TArray<int32> GroundRoots;
	int32 SupportEdgeCount = 0;
	UWorld* World = GetWorld();
	for (int32 UpperIndex = 0; UpperIndex < SleepingModules.Num(); ++UpperIndex)
	{
		AABTSM7BuildingModule& Upper = *SleepingModules[UpperIndex];
		if (Upper.CanFreezeAsGroundedRoot())
		{
			GroundRoots.Add(UpperIndex);
			continue;
		}
		if (World == nullptr) continue;
		UStaticMeshComponent* UpperBody = Upper.GetMeshComponent();
		const FVector Up = Upper.GetCurrentGravityUp();
		const FBoxSphereBounds Bounds = UpperBody->Bounds;
		const float DownExtent = FMath::Abs(Up.X) * Bounds.BoxExtent.X
			+ FMath::Abs(Up.Y) * Bounds.BoxExtent.Y
			+ FMath::Abs(Up.Z) * Bounds.BoxExtent.Z;
		FCollisionQueryParams QueryParams(
			SCENE_QUERY_STAT(M7SettledSupportClosure), false, &Upper);
		FHitResult SupportHit;
		const FVector Start = Bounds.Origin - Up * FMath::Max(0.0f,
			DownExtent - 1.0f);
		if (!World->LineTraceSingleByChannel(SupportHit, Start,
			Start - Up * 10.0f, ABTSDeveloperObstacleChannel, QueryParams))
		{
			continue;
		}
		AABTSM7BuildingModule* Lower = Cast<AABTSM7BuildingModule>(
			SupportHit.GetActor());
		const int32* LowerIndex = Lower != nullptr ? SleepingIndex.Find(Lower) : nullptr;
		if (LowerIndex != nullptr
			&& Lower->GetDamageLifecycleOwner()
				== Upper.GetDamageLifecycleOwner()
			&& FVector::DotProduct(Lower->GetActorLocation()
				- Upper.GetActorLocation(), Up) < -1.0f)
		{
			SupportChildren[*LowerIndex].Add(UpperIndex);
			++SupportEdgeCount;
		}
	}
	TArray<int32> FreezeOrder;
	BuildGroundReachableFreezeOrder(SupportChildren, GroundRoots, FreezeOrder);
	for (const int32 Index : FreezeOrder)
	{
		SleepingModules[Index]->Freeze();
		++FrozenCount;
	}
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& WeakModule : Modules)
	{
		AABTSM7BuildingModule* Module = WeakModule.Get();
		if (Module == nullptr || !Module->IsDynamic()) continue;
		// The only remaining dynamics are awake or have no contact path to a
		// certified root. Recycle them rather than creating suspended statics.
		Module->RecycleUnsupportedDebris();
		++RecycledAirborneCount;
	}

	int32 SimulatingAfter = 0;
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& WeakModule : Modules)
	{
		const AABTSM7BuildingModule* Module = WeakModule.Get();
		const UStaticMeshComponent* Body =
			Module != nullptr ? Module->GetMeshComponent() : nullptr;
		SimulatingAfter += Body != nullptr && Body->IsSimulatingPhysics() ? 1 : 0;
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7][WalkReturnFreeze] FrozenGroundReachable=%d GroundRoots=%d SupportEdges=%d RecycledAirborneOrAwake=%d SimulatingBefore=%d SimulatingAfter=%d NoFloatingStatic=1"),
		FrozenCount,
		GroundRoots.Num(),
		SupportEdgeCount,
		RecycledAirborneCount,
		SimulatingBefore,
		SimulatingAfter);
	ensureAlwaysMsgf(SimulatingAfter == 0,
		TEXT("Walk return left %d M7 module components simulating"),
		SimulatingAfter);
	return FrozenCount;
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

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM7CentralizedContactSpatialHashTest,
	"ABTS.M7.CentralizedContactDamage.SpatialHash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM7CentralizedContactSpatialHashTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	// A 1,000 cm/s brick is outside the current overlap but its 0.1-second
	// swept bounds reach the target. This is the case a low-frequency polling
	// implementation must not silently miss after per-brick callbacks are off.
	TArray<FBox> SweptBounds;
	SweptBounds.Add(FBox(FVector(-18.0f), FVector(118.0f, 18.0f, 18.0f)));
	SweptBounds.Add(FBox(FVector(100.0f, -18.0f, -18.0f),
		FVector(136.0f, 18.0f, 18.0f)));
	SweptBounds.Add(FBox(FVector(1000.0f), FVector(1036.0f)));
	TArray<uint64> FirstPairs;
	TArray<uint64> SecondPairs;
	BuildStableSpatialContactCandidatePairs(SweptBounds, 180.0f, FirstPairs);
	BuildStableSpatialContactCandidatePairs(SweptBounds, 180.0f, SecondPairs);
	TestEqual(TEXT("High-speed swept contact has exactly one near candidate"),
		FirstPairs.Num(), 1);
	TestEqual(TEXT("Stable spatial hash emits the same candidate sequence"),
		FirstPairs, SecondPairs);
	TestEqual(TEXT("Candidate key retains the sorted two brick ordinals"),
		FirstPairs.IsEmpty() ? 0ull : FirstPairs[0], 1ull);
	const FBox CrossingMover(FVector(82.0f, -18.0f, -18.0f),
		FVector(118.0f, 18.0f, 18.0f));
	const FBox CrossingTarget(FVector(42.0f, -18.0f, -18.0f),
		FVector(78.0f, 18.0f, 18.0f));
	TestTrue(TEXT("High-speed crossing passes the deterministic narrow-contact gate"),
		PassesSweptNarrowContactGate(CrossingMover, FVector(1000.0f, 0.0f, 0.0f),
			CrossingTarget, FVector::ZeroVector, 0.10f));
	const FBox ParallelNearMiss(FVector(42.0f, 82.0f, -18.0f),
		FVector(78.0f, 118.0f, 18.0f));
	TestFalse(TEXT("Parallel near-miss does not create ghost contact damage"),
		PassesSweptNarrowContactGate(CrossingMover, FVector(1000.0f, 0.0f, 0.0f),
			ParallelNearMiss, FVector(1000.0f, 0.0f, 0.0f), 0.10f));
	int32 PairCursor = 0;
	TSet<int32> VisitedPairIndices;
	for (int32 Window = 0; Window < 8; ++Window)
	{
		TArray<int32> PairWindow;
		BuildCyclicPairWindow(2048, 256, PairCursor, PairWindow);
		for (const int32 PairIndex : PairWindow)
		{
			VisitedPairIndices.Add(PairIndex);
		}
	}
	TestEqual(TEXT("Dense pair budget eventually visits every stable candidate"),
		VisitedPairIndices.Num(), 2048);
	TArray<TArray<int32>> StackSupportChildren;
	StackSupportChildren.SetNum(4);
	StackSupportChildren[0].Add(1);
	StackSupportChildren[1].Add(2);
	TArray<int32> StackRoots = {0};
	TArray<int32> FreezeOrder;
	BuildGroundReachableFreezeOrder(StackSupportChildren, StackRoots,
		FreezeOrder);
	TestEqual(TEXT("Three-brick settled stack preserves every grounded brick"),
		FreezeOrder.Num(), 3);
	TestFalse(TEXT("Disconnected sleeping brick is excluded and therefore recycled"),
		FreezeOrder.Contains(3));
	TestEqual(TEXT("Walk transaction identifies exactly one unsupported body for recycling"),
		StackSupportChildren.Num() - FreezeOrder.Num(), 1);
	return true;
}

#endif
