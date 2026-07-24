// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestStage/ABTSM71TestStageActors.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Components/ArrowComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"
#include "World/ABTSM51WorldActors.h"

namespace
{
	constexpr float BasicShapeSizeCM = 100.0f;

	FVector DivideScaleSafely(const FVector& Scale, const FVector& Divisor)
	{
		return FVector(
			FMath::IsNearlyZero(Divisor.X) ? 1.0f : Scale.X / Divisor.X,
			FMath::IsNearlyZero(Divisor.Y) ? 1.0f : Scale.Y / Divisor.Y,
			FMath::IsNearlyZero(Divisor.Z) ? 1.0f : Scale.Z / Divisor.Z);
	}
}

AABTSM71PhysicsTestStage::AABTSM71PhysicsTestStage()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Floor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Floor"));
	Floor->SetupAttachment(Root);
	Floor->SetMobility(EComponentMobility::Static);
	Floor->SetCollisionProfileName(TEXT("BlockAll"));
	Floor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Floor->SetCollisionObjectType(ECC_WorldStatic);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded()) Floor->SetStaticMesh(Cube.Object);
}

void AABTSM71PhysicsTestStage::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	const float SafeThickness = FMath::Max(10.0f, FloorThicknessCM);
	Floor->SetRelativeLocation(FVector(0.0f, 0.0f, -SafeThickness * 0.5f));
	Floor->SetRelativeScale3D(FVector(
		FMath::Max(100.0f, FloorSizeCM.X) / BasicShapeSizeCM,
		FMath::Max(100.0f, FloorSizeCM.Y) / BasicShapeSizeCM,
		SafeThickness / BasicShapeSizeCM));
	if (FloorMaterial) Floor->SetMaterial(0, FloorMaterial);
}

AABTSM71PlayerStart::AABTSM71PlayerStart(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PlayerStartTag = TEXT("ABTS_M71_PlayerStart");
}

AABTSM71PlaceableHISMActor::AABTSM71PlaceableHISMActor()
{
	PrimaryActorTick.bCanEverTick = false;
	HISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("PlaceableHISM"));
	SetRootComponent(HISM);
	HISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	HISM->SetCollisionObjectType(ECC_WorldStatic);
	HISM->SetCollisionResponseToAllChannels(ECR_Block);
	HISM->SetGenerateOverlapEvents(false);
}

void AABTSM71PlaceableHISMActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	HISM->SetStaticMesh(InstanceMesh);
	if (InstanceMaterial) HISM->SetMaterial(0, InstanceMaterial);
	HISM->ClearInstances();
	if (InstanceMesh) HISM->AddInstance(FTransform::Identity);
}

AABTSM71TreeHISMActor::AABTSM71TreeHISMActor()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded()) InstanceMesh = Cylinder.Object;
	SetActorScale3D(FVector(0.65f, 0.65f, 3.2f));
}

AABTSM71RockHISMActor::AABTSM71RockHISMActor()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded()) InstanceMesh = Sphere.Object;
	SetActorScale3D(FVector(1.2f, 0.9f, 0.75f));
}

AABTSM71PlaceableBrickActor::AABTSM71PlaceableBrickActor()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded()) InstanceMesh = Cube.Object;
	SetActorScale3D(FVector(2.0f, 0.8f, 0.6f));
}

void AABTSM71PlaceableBrickActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (!InstanceMaterial)
	{
		UMaterialInstanceDynamic* Fallback = UMaterialInstanceDynamic::Create(
			HISM->GetMaterial(0), this, TEXT("M71BrickFallback"));
		if (Fallback)
		{
			Fallback->SetVectorParameterValue(TEXT("Color"), FallbackColor);
			Fallback->SetVectorParameterValue(TEXT("BaseColor"), FallbackColor);
			HISM->SetMaterial(0, Fallback);
		}
	}
}

AABTSM71WoodBrickActor::AABTSM71WoodBrickActor()
{
	BuildingMaterial = EABTSM7BuildingMaterial::Wood;
	FallbackColor = FLinearColor(0.38f, 0.13f, 0.035f);
}

AABTSM71StoneBrickActor::AABTSM71StoneBrickActor()
{
	BuildingMaterial = EABTSM7BuildingMaterial::Stone;
	FallbackColor = FLinearColor(0.32f, 0.34f, 0.38f);
}

AABTSM71IronBrickActor::AABTSM71IronBrickActor()
{
	BuildingMaterial = EABTSM7BuildingMaterial::Iron;
	FallbackColor = FLinearColor(0.12f, 0.16f, 0.20f);
}

AABTSM71GlassBrickActor::AABTSM71GlassBrickActor()
{
	BuildingMaterial = EABTSM7BuildingMaterial::Glass;
	FallbackColor = FLinearColor(0.20f, 0.62f, 0.78f, 0.42f);
}

AABTSM71PlaceableDeviceActor::AABTSM71PlaceableDeviceActor()
{
	PrimaryActorTick.bCanEverTick = false;
	DevicePreview = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DevicePreview"));
	SetRootComponent(DevicePreview);
	DevicePreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DevicePreview->SetGenerateOverlapEvents(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded()) DeviceMesh = Cylinder.Object;
}

void AABTSM71PlaceableDeviceActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	DevicePreview->SetStaticMesh(DeviceMesh);
	if (DeviceMaterial) DevicePreview->SetMaterial(0, DeviceMaterial);
	DevicePreview->SetRelativeScale3D(FVector(
		FMath::Max(1.0f, DiameterCM) / BasicShapeSizeCM,
		FMath::Max(1.0f, DiameterCM) / BasicShapeSizeCM,
		FMath::Max(1.0f, LengthCM) / BasicShapeSizeCM));
}

void AABTSM71PlaceableDeviceActor::BeginPlay()
{
	Super::BeginPlay();
	TryFindRuntimeSystem();
}

void AABTSM71PlaceableDeviceActor::InitializeRuntimeDevice(AABTSM7BuildingMaterialSystem* MaterialSystem)
{
	if (RuntimeDevice.IsValid() || MaterialSystem == nullptr || GetWorld() == nullptr) return;
	RuntimeSystem = MaterialSystem;
	FABTSM7DeviceSpec Spec;
	Spec.Kind = DeviceKind;
	Spec.LengthCM = FMath::Max(1.0f, LengthCM);
	Spec.DiameterCM = FMath::Max(1.0f, DiameterCM);
	// DevicePreview is currently the actor root, so OnConstruction's preview-size
	// scale is also present in GetActorTransform(). The runtime module applies the
	// same Length/Diameter scale in ConfigureCylinder; remove only that preview
	// scale here so the physical shape receives the authored dimensions once.
	const FVector PreviewShapeScale(
		Spec.DiameterCM / BasicShapeSizeCM,
		Spec.DiameterCM / BasicShapeSizeCM,
		Spec.LengthCM / BasicShapeSizeCM);
	FTransform RuntimeTransform = GetActorTransform();
	const FVector RuntimeAdditionalScale = DivideScaleSafely(RuntimeTransform.GetScale3D(), PreviewShapeScale);
	RuntimeTransform.SetScale3D(RuntimeAdditionalScale);
	AABTSM7BuildingModule* Module = MaterialSystem->SpawnDeviceWithOverrides(
		Spec, RuntimeTransform, DeviceMesh, DeviceMaterial);
	RuntimeDevice = Module;
	if (Module)
	{
		DevicePreview->SetHiddenInGame(true);
		UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M7.1][Device] Spawned Kind=%d Actor=%s Length=%.1f Diameter=%.1f PreviewScale=(%.2f,%.2f,%.2f) RuntimeAdditionalScale=(%.2f,%.2f,%.2f)"),
			static_cast<int32>(DeviceKind), *GetName(), Spec.LengthCM, Spec.DiameterCM,
			PreviewShapeScale.X, PreviewShapeScale.Y, PreviewShapeScale.Z,
			RuntimeAdditionalScale.X, RuntimeAdditionalScale.Y, RuntimeAdditionalScale.Z);
	}
}

void AABTSM71PlaceableDeviceActor::TryFindRuntimeSystem()
{
	if (RuntimeDevice.IsValid() || GetWorld() == nullptr) return;
	for (TActorIterator<AABTSM7BuildingMaterialSystem> It(GetWorld()); It; ++It)
	{
		InitializeRuntimeDevice(*It);
		return;
	}
	if (++SystemSearchAttempts < 40)
	{
		GetWorldTimerManager().SetTimer(SystemSearchTimer, this, &AABTSM71PlaceableDeviceActor::TryFindRuntimeSystem, 0.1f, false);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M7.1][Device] No material system for %s"), *GetName());
	}
}

AABTSM71ExplosiveBarrelActor::AABTSM71ExplosiveBarrelActor()
{
	SetDeviceKind(EABTSM7ModuleKind::ExplosiveBarrel);
}

AABTSM71SpringPistonActor::AABTSM71SpringPistonActor()
{
	SetDeviceKind(EABTSM7ModuleKind::SpringPiston);
}

AABTSM71PlaceableSlingshotActor::AABTSM71PlaceableSlingshotActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	StakePreviewA = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StakePreviewA"));
	StakePreviewB = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StakePreviewB"));
	CordPreview = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CordPreview"));
	CordPreviewB = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CordPreviewB"));
	PouchPreview = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PouchPreview"));
	LaunchDirection = CreateDefaultSubobject<UArrowComponent>(TEXT("LaunchDirection"));
	for (UStaticMeshComponent* Component : {StakePreviewA.Get(), StakePreviewB.Get(), CordPreview.Get(), CordPreviewB.Get(), PouchPreview.Get()})
	{
		Component->SetupAttachment(Root);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetHiddenInGame(true);
	}
	LaunchDirection->SetupAttachment(Root);
	LaunchDirection->SetArrowColor(FColor::Green);
	LaunchDirection->ArrowSize = 2.0f;
	LaunchDirection->SetHiddenInGame(true);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Cylinder.Succeeded())
	{
		DefaultStakeMesh = Cylinder.Object;
		StakePreviewA->SetStaticMesh(DefaultStakeMesh);
		StakePreviewB->SetStaticMesh(DefaultStakeMesh);
	}
	if (Cube.Succeeded())
	{
		DefaultCordMesh = Cube.Object;
		CordPreview->SetStaticMesh(DefaultCordMesh);
		CordPreviewB->SetStaticMesh(DefaultCordMesh);
	}
	if (Sphere.Succeeded())
	{
		DefaultPouchMesh = Sphere.Object;
		PouchPreview->SetStaticMesh(DefaultPouchMesh);
	}
}

void AABTSM71PlaceableSlingshotActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdatePreview();
}

void AABTSM71PlaceableSlingshotActor::UpdatePreview()
{
	const float YScaleOnly = FMath::Max(0.01f, FMath::Abs(GetActorScale3D().Y));
	const float HalfSpacing = BaseStakeSpacingCM * YScaleOnly * 0.5f;
	const FVector BaseAWorld = GetActorLocation() + GetActorQuat().RotateVector(FVector(0.0f, -HalfSpacing, 0.0f));
	const FVector BaseBWorld = GetActorLocation() + GetActorQuat().RotateVector(FVector(0.0f, HalfSpacing, 0.0f));
	StakePreviewA->SetStaticMesh(StakeVisual.Mesh ? StakeVisual.Mesh : DefaultStakeMesh);
	StakePreviewB->SetStaticMesh(StakeVisual.Mesh ? StakeVisual.Mesh : DefaultStakeMesh);
	const FVector StakeTargetSize(FMath::Max(1.0f, StakeDiameterCM), FMath::Max(1.0f, StakeDiameterCM), FMath::Max(1.0f, StakeHeightCM));
	StakePreviewA->SetWorldTransform(ABTSMakeSlingshotVisualTransform(
		StakePreviewA->GetStaticMesh(), BaseAWorld, GetActorQuat(), StakeTargetSize, StakeVisual,
		EABTSSlingshotVisualAnchor::BoundsBottomCenter));
	StakePreviewB->SetWorldTransform(ABTSMakeSlingshotVisualTransform(
		StakePreviewB->GetStaticMesh(), BaseBWorld, GetActorQuat(), StakeTargetSize, StakeVisual,
		EABTSSlingshotVisualAnchor::BoundsBottomCenter));
	const FVector EndpointA = GetActorLocation() + GetActorQuat().RotateVector(FVector(0.0f, -HalfSpacing, StakeHeightCM) + ConnectionLayout.StakeAConnectionOffsetCM);
	const FVector EndpointB = GetActorLocation() + GetActorQuat().RotateVector(FVector(0.0f, HalfSpacing, StakeHeightCM) + ConnectionLayout.StakeBConnectionOffsetCM);
	const FVector PouchCenter = (EndpointA + EndpointB) * 0.5f + GetActorQuat().RotateVector(ConnectionLayout.RestPouchOffsetCM);
	const FVector PouchAnchorA = PouchCenter + GetActorQuat().RotateVector(ConnectionLayout.PouchAConnectionOffsetCM);
	const FVector PouchAnchorB = PouchCenter + GetActorQuat().RotateVector(ConnectionLayout.PouchBConnectionOffsetCM);
	CordPreview->SetStaticMesh(CordVisual.Mesh ? CordVisual.Mesh : DefaultCordMesh);
	CordPreviewB->SetStaticMesh(CordVisual.Mesh ? CordVisual.Mesh : DefaultCordMesh);
	auto SetPreviewSegment = [this](UStaticMeshComponent* Component, const FVector& Start, const FVector& End)
	{
		const FVector Delta = End - Start;
		const float Length = Delta.Size();
		if (Length <= SMALL_NUMBER) return;
		const FQuat BaseRotation = FQuat::FindBetweenNormals(FVector::UpVector, Delta / Length);
		Component->SetWorldTransform(ABTSMakeSlingshotVisualTransform(
			Component->GetStaticMesh(),
			(Start + End) * 0.5f,
			BaseRotation,
			FVector(CordThicknessCM, CordThicknessCM, Length),
			CordVisual,
			EABTSSlingshotVisualAnchor::BoundsCenter));
	};
	SetPreviewSegment(CordPreview, EndpointA, PouchAnchorA);
	SetPreviewSegment(CordPreviewB, EndpointB, PouchAnchorB);
	PouchPreview->SetStaticMesh(PouchVisual.Mesh ? PouchVisual.Mesh : DefaultPouchMesh);
	PouchPreview->SetVisibility(PouchPreview->GetStaticMesh() != nullptr);
	PouchPreview->SetWorldTransform(ABTSMakeSlingshotVisualTransform(
		PouchPreview->GetStaticMesh(), PouchCenter, GetActorQuat(), PouchSizeCM, PouchVisual,
		EABTSSlingshotVisualAnchor::BoundsCenter));
	if (StakeVisual.Material)
	{
		StakePreviewA->SetMaterial(0, StakeVisual.Material);
		StakePreviewB->SetMaterial(0, StakeVisual.Material);
	}
	if (CordVisual.Material)
	{
		CordPreview->SetMaterial(0, CordVisual.Material);
		CordPreviewB->SetMaterial(0, CordVisual.Material);
	}
	if (PouchVisual.Material) PouchPreview->SetMaterial(0, PouchVisual.Material);
}

void AABTSM71PlaceableSlingshotActor::BeginPlay()
{
	Super::BeginPlay();
	SpawnRuntimeSlingshot();
}

void AABTSM71PlaceableSlingshotActor::SpawnRuntimeSlingshot()
{
	if (GetWorld() == nullptr || RuntimeCord.IsValid()) return;
	const FVector Up = GetActorUpVector().GetSafeNormal();
	const float HalfSpacing = BaseStakeSpacingCM * FMath::Max(0.01f, FMath::Abs(GetActorScale3D().Y)) * 0.5f;
	const FVector EndpointA = GetActorLocation() + GetActorQuat().RotateVector(FVector(0.0f, -HalfSpacing, StakeHeightCM));
	const FVector EndpointB = GetActorLocation() + GetActorQuat().RotateVector(FVector(0.0f, HalfSpacing, StakeHeightCM));
	const FVector StakeLocationA = EndpointA - Up * StakeHeightCM * 0.5f;
	const FVector StakeLocationB = EndpointB - Up * StakeHeightCM * 0.5f;
	const FQuat Rotation = GetActorQuat();
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM51SlingshotStake* StakeA = GetWorld()->SpawnActor<AABTSM51SlingshotStake>(AABTSM51SlingshotStake::StaticClass(), FTransform(Rotation, StakeLocationA), Params);
	AABTSM51SlingshotStake* StakeB = GetWorld()->SpawnActor<AABTSM51SlingshotStake>(AABTSM51SlingshotStake::StaticClass(), FTransform(Rotation, StakeLocationB), Params);
	if (StakeA == nullptr || StakeB == nullptr) return;
	const EABTSItemId StakeItem = SlingshotTier == EABTSSlingshotTier::Twig || SlingshotTier == EABTSSlingshotTier::Simple
		? EABTSItemId::SimpleStake : EABTSItemId::ReinforcedStake;
	StakeA->InitializeStake(StakeItem, INDEX_NONE, Up);
	StakeB->InitializeStake(StakeItem, INDEX_NONE, Up);
	StakeA->ApplyVisualSlot(StakeVisual, StakeDiameterCM, StakeHeightCM);
	StakeB->ApplyVisualSlot(StakeVisual, StakeDiameterCM, StakeHeightCM);
	StakeA->SetHasCord(true);
	StakeB->SetHasCord(true);
	AABTSM51SlingshotCord* Cord = GetWorld()->SpawnActor<AABTSM51SlingshotCord>(AABTSM51SlingshotCord::StaticClass(), FTransform::Identity, Params);
	if (Cord == nullptr) return;
	Cord->InitializeCordWithTier(StakeA, StakeB, EndpointA, EndpointB, SlingshotTier);
	Cord->ConfigureTwoCordVisuals(CordVisual, PouchVisual, ConnectionLayout, CordThicknessCM, PouchSizeCM);
	RuntimeStakeA = StakeA;
	RuntimeStakeB = StakeB;
	RuntimeCord = Cord;
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M7.1][Slingshot] Spawned Tier=%d Direction=(%.2f,%.2f,%.2f) Spacing=%.1f"),
		static_cast<int32>(SlingshotTier), GetActorForwardVector().X, GetActorForwardVector().Y, GetActorForwardVector().Z, HalfSpacing * 2.0f);
}

AABTSM71TwigSlingshotActor::AABTSM71TwigSlingshotActor() { SetSlingshotTier(EABTSSlingshotTier::Twig); }
AABTSM71SimpleSlingshotActor::AABTSM71SimpleSlingshotActor() { SetSlingshotTier(EABTSSlingshotTier::Simple); }
AABTSM71ReinforcedSlingshotActor::AABTSM71ReinforcedSlingshotActor() { SetSlingshotTier(EABTSSlingshotTier::Reinforced); }
AABTSM71SpaceSlingshotActor::AABTSM71SpaceSlingshotActor() { SetSlingshotTier(EABTSSlingshotTier::Space); }

AABTSM71ModularBuildingAnchor::AABTSM71ModularBuildingAnchor()
{
	PrimaryActorTick.bCanEverTick = false;
	ForwardArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("BuildingForward"));
	SetRootComponent(ForwardArrow);
	ForwardArrow->SetArrowColor(FColor::Cyan);
	ForwardArrow->ArrowSize = 2.5f;
}
