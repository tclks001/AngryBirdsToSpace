// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM51WorldActors.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Player/ABTSM51PlayerController.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	void ConfigureInteractionMesh(UStaticMeshComponent& Mesh)
	{
		Mesh.SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Mesh.SetCollisionResponseToAllChannels(ECR_Ignore);
		Mesh.SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	}

	void ConfigureVisualOnlyMesh(UStaticMeshComponent& Mesh)
	{
		Mesh.SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh.SetGenerateOverlapEvents(false);
	}

	void SetSegmentBetween(
		UStaticMeshComponent& Mesh,
		const FVector& Start,
		const FVector& End,
		const float ThicknessCM,
		const FABTSSlingshotVisualSlot& VisualSlot)
	{
		const FVector Delta = End - Start;
		const float Length = Delta.Size();
		if (Length <= SMALL_NUMBER) return;
		const FVector Direction = Delta / Length;
		const FQuat Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Direction);
		Mesh.SetWorldTransform(ABTSMakeSlingshotVisualTransform(
			Mesh.GetStaticMesh(),
			(Start + End) * 0.5f,
			Rotation,
			FVector(ThicknessCM, ThicknessCM, Length),
			VisualSlot,
			EABTSSlingshotVisualAnchor::BoundsCenter));
	}

	FQuat BuildSlingshotVisualRotation(
		const FVector& EndpointA,
		const FVector& EndpointB,
		const AABTSM51SlingshotStake* StakeA,
		const AABTSM51SlingshotStake* StakeB)
	{
		const FVector Right = (EndpointB - EndpointA).GetSafeNormal();
		FVector Up = FVector::UpVector;
		if (StakeA != nullptr && StakeB != nullptr)
		{
			Up = (StakeA->GetUnitDirection() + StakeB->GetUnitDirection()).GetSafeNormal();
		}
		Up = FVector::VectorPlaneProject(Up, Right).GetSafeNormal();
		if (Up.IsNearlyZero()) Up = FVector::VectorPlaneProject(FVector::UpVector, Right).GetSafeNormal();
		const FVector Forward = FVector::CrossProduct(Right, Up).GetSafeNormal();
		return FRotationMatrix::MakeFromXY(Forward, Right).ToQuat();
	}
}

AABTSM51PickupItem::AABTSM51PickupItem()
{
	PrimaryActorTick.bCanEverTick = false;
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PickupVisual"));
	SetRootComponent(Visual);
	ConfigureInteractionMesh(*Visual);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded()) Visual->SetStaticMesh(Sphere.Object);
	Visual->SetRelativeScale3D(FVector(0.18f));
}

void AABTSM51PickupItem::InitializePickup(const EABTSItemId InItemId, const int32 InQuantity, const int32 InCellId)
{
	ItemId = InItemId;
	Quantity = FMath::Max(1, InQuantity);
	CellId = InCellId;
}

AABTSM51SlingshotDirtHole::AABTSM51SlingshotDirtHole()
{
	PrimaryActorTick.bCanEverTick = false;
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DirtHoleVisual"));
	SetRootComponent(Visual);
	ConfigureInteractionMesh(*Visual);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded()) Visual->SetStaticMesh(Cylinder.Object);
	Visual->SetRelativeScale3D(FVector(0.55f, 0.55f, 0.06f));
}

void AABTSM51SlingshotDirtHole::InitializeHole(const int32 InCellId)
{
	CellId = InCellId;
}

void AABTSM51SlingshotDirtHole::NotifyActorOnClicked(const FKey ButtonPressed)
{
	Super::NotifyActorOnClicked(ButtonPressed);
	if (ButtonPressed != EKeys::LeftMouseButton || GetWorld() == nullptr) return;
	if (AABTSM51PlayerController* Controller = Cast<AABTSM51PlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		Controller->InteractWithDirtHole(this);
	}
}

AABTSM51SlingshotStake::AABTSM51SlingshotStake()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("StakeRoot"));
	SetRootComponent(Root);
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StakeVisual"));
	Visual->SetupAttachment(Root);
	ConfigureInteractionMesh(*Visual);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded()) Visual->SetStaticMesh(Cylinder.Object);
	Visual->SetRelativeScale3D(FVector(0.14f, 0.14f, 1.1f));
}

void AABTSM51SlingshotStake::InitializeStake(
	const EABTSItemId InStakeItem,
	const int32 InCellId,
	const FVector& InUnitDirection)
{
	StakeItem = InStakeItem;
	CellId = InCellId;
	UnitDirection = InUnitDirection.GetSafeNormal();
}

void AABTSM51SlingshotStake::ConfigureVisualDimensions(
	const float DiameterCM,
	const float HeightCM,
	UMaterialInterface* Material)
{
	const float SafeDiameterCM = FMath::Max(1.0f, DiameterCM);
	const float SafeHeightCM = FMath::Max(1.0f, HeightCM);
	const FVector BaseWorld = GetActorLocation() - GetActorUpVector() * (SafeHeightCM * 0.5f);
	Visual->SetWorldTransform(ABTSMakeSlingshotVisualTransform(
		Visual->GetStaticMesh(),
		BaseWorld,
		GetActorQuat(),
		FVector(SafeDiameterCM, SafeDiameterCM, SafeHeightCM),
		FABTSSlingshotVisualSlot(),
		EABTSSlingshotVisualAnchor::BoundsBottomCenter));
	if (Material) Visual->SetMaterial(0, Material);
}

void AABTSM51SlingshotStake::ApplyVisualSlot(
	const FABTSSlingshotVisualSlot& VisualSlot,
	const float DiameterCM,
	const float HeightCM)
{
	if (VisualSlot.Mesh) Visual->SetStaticMesh(VisualSlot.Mesh);
	if (VisualSlot.Material) Visual->SetMaterial(0, VisualSlot.Material);
	const float SafeDiameterCM = FMath::Max(1.0f, DiameterCM);
	const float SafeHeightCM = FMath::Max(1.0f, HeightCM);
	const FVector BaseWorld = GetActorLocation() - GetActorUpVector() * (SafeHeightCM * 0.5f);
	Visual->SetWorldTransform(ABTSMakeSlingshotVisualTransform(
		Visual->GetStaticMesh(),
		BaseWorld,
		GetActorQuat(),
		FVector(SafeDiameterCM, SafeDiameterCM, SafeHeightCM),
		VisualSlot,
		EABTSSlingshotVisualAnchor::BoundsBottomCenter));
}

void AABTSM51SlingshotStake::NotifyActorOnClicked(const FKey ButtonPressed)
{
	Super::NotifyActorOnClicked(ButtonPressed);
	if (ButtonPressed != EKeys::LeftMouseButton || GetWorld() == nullptr) return;
	if (AABTSM51PlayerController* Controller = Cast<AABTSM51PlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		Controller->InteractWithStake(this);
	}
}

AABTSM51SlingshotCord::AABTSM51SlingshotCord()
{
	PrimaryActorTick.bCanEverTick = false;
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CordVisual"));
	SetRootComponent(Visual);
	ConfigureInteractionMesh(*Visual);
	CordSegmentA = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CordSegmentA"));
	CordSegmentB = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CordSegmentB"));
	PouchVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PouchVisual"));
	CordSegmentA->SetupAttachment(Visual);
	CordSegmentB->SetupAttachment(Visual);
	PouchVisual->SetupAttachment(Visual);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Cube.Succeeded()) Visual->SetStaticMesh(Cube.Object);
	if (Cylinder.Succeeded())
	{
		DefaultCordCylinderMesh = Cylinder.Object;
		CordSegmentA->SetStaticMesh(DefaultCordCylinderMesh);
		CordSegmentB->SetStaticMesh(DefaultCordCylinderMesh);
	}
	if (Sphere.Succeeded())
	{
		DefaultPouchSphereMesh = Sphere.Object;
		PouchVisual->SetStaticMesh(DefaultPouchSphereMesh);
	}
	ConfigureVisualOnlyMesh(*CordSegmentA);
	ConfigureVisualOnlyMesh(*CordSegmentB);
	ConfigureVisualOnlyMesh(*PouchVisual);
}

void AABTSM51SlingshotCord::InitializeCord(
	AABTSM51SlingshotStake* InStakeA,
	AABTSM51SlingshotStake* InStakeB,
	const FVector& InEndpointA,
	const FVector& InEndpointB)
{
	const EABTSSlingshotTier InferredTier = InStakeA && InStakeA->GetStakeItem() == EABTSItemId::ReinforcedStake
		? EABTSSlingshotTier::Reinforced : EABTSSlingshotTier::Simple;
	InitializeCordWithTier(InStakeA, InStakeB, InEndpointA, InEndpointB, InferredTier);
}

void AABTSM51SlingshotCord::InitializeCordWithTier(
	AABTSM51SlingshotStake* InStakeA,
	AABTSM51SlingshotStake* InStakeB,
	const FVector& InEndpointA,
	const FVector& InEndpointB,
	const EABTSSlingshotTier InTier)
{
	StakeA = InStakeA;
	StakeB = InStakeB;
	SlingshotTier = InTier;
	EndpointA = InEndpointA;
	EndpointB = InEndpointB;
	const FVector Delta = EndpointB - EndpointA;
	const float Length = Delta.Size();
	if (Length <= SMALL_NUMBER) return;
	SetActorLocation((EndpointA + EndpointB) * 0.5f);
	SetActorRotation(FRotationMatrix::MakeFromX(Delta / Length).ToQuat());
	Visual->SetRelativeScale3D(FVector(Length / 100.0f, 0.035f, 0.035f));
	ConfigureTwoCordVisuals(CordVisualSlot, PouchVisualSlot, ConnectionLayout, CordThicknessCM);
}

void AABTSM51SlingshotCord::ConfigureVisualThickness(const float ThicknessCM, UMaterialInterface* Material)
{
	const FVector CurrentScale = Visual->GetRelativeScale3D();
	const float ThicknessScale = FMath::Max(1.0f, ThicknessCM) / 100.0f;
	Visual->SetRelativeScale3D(FVector(CurrentScale.X, ThicknessScale, ThicknessScale));
	if (Material) Visual->SetMaterial(0, Material);
}

void AABTSM51SlingshotCord::ApplyVisualSlot(const FABTSSlingshotVisualSlot& VisualSlot, const float ThicknessCM)
{
	if (VisualSlot.Mesh) Visual->SetStaticMesh(VisualSlot.Mesh);
	if (VisualSlot.Material) Visual->SetMaterial(0, VisualSlot.Material);
	const FTransform BaseTransform = GetActorTransform();
	SetActorLocation(BaseTransform.TransformPosition(VisualSlot.LocalOffsetCM), false, nullptr, ETeleportType::TeleportPhysics);
	SetActorRotation((BaseTransform.GetRotation() * VisualSlot.LocalRotation.Quaternion()).Rotator(), ETeleportType::TeleportPhysics);
	const FVector CurrentScale = Visual->GetRelativeScale3D();
	Visual->SetRelativeScale3D(FVector(
		CurrentScale.X * VisualSlot.LocalScale.X,
		FMath::Max(1.0f, ThicknessCM) / 100.0f * VisualSlot.LocalScale.Y,
		FMath::Max(1.0f, ThicknessCM) / 100.0f * VisualSlot.LocalScale.Z));
}

void AABTSM51SlingshotCord::ConfigureTwoCordVisuals(
	const FABTSSlingshotVisualSlot& CordSlot,
	const FABTSSlingshotVisualSlot& PouchSlot,
	const FABTSSlingshotConnectionLayout& Layout,
	const float ThicknessCM,
	const FVector& InPouchSizeCM)
{
	CordVisualSlot = CordSlot;
	PouchVisualSlot = PouchSlot;
	ConnectionLayout = Layout;
	CordThicknessCM = FMath::Max(0.1f, ThicknessCM);
	PouchSizeCM = FVector(
		FMath::Max(1.0f, FMath::Abs(InPouchSizeCM.X)),
		FMath::Max(1.0f, FMath::Abs(InPouchSizeCM.Y)),
		FMath::Max(1.0f, FMath::Abs(InPouchSizeCM.Z)));

	CordSegmentA->SetStaticMesh(CordSlot.Mesh ? CordSlot.Mesh : DefaultCordCylinderMesh);
	CordSegmentB->SetStaticMesh(CordSlot.Mesh ? CordSlot.Mesh : DefaultCordCylinderMesh);
	PouchVisual->SetStaticMesh(PouchSlot.Mesh ? PouchSlot.Mesh : DefaultPouchSphereMesh);
	if (CordSlot.Material)
	{
		CordSegmentA->SetMaterial(0, CordSlot.Material);
		CordSegmentB->SetMaterial(0, CordSlot.Material);
	}
	if (PouchSlot.Material) PouchVisual->SetMaterial(0, PouchSlot.Material);

	// Keep the original crossbar mesh as the click/trace target only.
	Visual->SetVisibility(false, false);
	Visual->SetHiddenInGame(true, false);
	CordSegmentA->SetVisibility(true, true);
	CordSegmentB->SetVisibility(true, true);
	PouchVisual->SetVisibility(true, true);
	ResetPouchVisualToRest();
}

FTransform AABTSM51SlingshotCord::GetRestPouchTransform() const
{
	const FQuat LayoutRotation = BuildSlingshotVisualRotation(EndpointA, EndpointB, StakeA.Get(), StakeB.Get());
	const FVector StakeAnchorA = EndpointA + LayoutRotation.RotateVector(ConnectionLayout.StakeAConnectionOffsetCM);
	const FVector StakeAnchorB = EndpointB + LayoutRotation.RotateVector(ConnectionLayout.StakeBConnectionOffsetCM);
	const FVector Center = (StakeAnchorA + StakeAnchorB) * 0.5f
		+ LayoutRotation.RotateVector(ConnectionLayout.RestPouchOffsetCM);
	return FTransform(LayoutRotation, Center, FVector::OneVector);
}

void AABTSM51SlingshotCord::ResetPouchVisualToRest()
{
	UpdatePulledPouchVisual(GetRestPouchTransform().GetLocation(), GetRestPouchTransform().GetRotation());
}

void AABTSM51SlingshotCord::UpdatePulledPouchVisual(const FVector& WorldLocation, const FQuat& WorldRotation)
{
	if (CordSegmentA == nullptr || CordSegmentB == nullptr || PouchVisual == nullptr) return;
	const FQuat LayoutRotation = BuildSlingshotVisualRotation(EndpointA, EndpointB, StakeA.Get(), StakeB.Get());
	const FVector StakeAnchorA = EndpointA + LayoutRotation.RotateVector(ConnectionLayout.StakeAConnectionOffsetCM);
	const FVector StakeAnchorB = EndpointB + LayoutRotation.RotateVector(ConnectionLayout.StakeBConnectionOffsetCM);
	const FVector PouchAnchorA = WorldLocation + WorldRotation.RotateVector(ConnectionLayout.PouchAConnectionOffsetCM);
	const FVector PouchAnchorB = WorldLocation + WorldRotation.RotateVector(ConnectionLayout.PouchBConnectionOffsetCM);
	SetSegmentBetween(*CordSegmentA, StakeAnchorA, PouchAnchorA, CordThicknessCM, CordVisualSlot);
	SetSegmentBetween(*CordSegmentB, StakeAnchorB, PouchAnchorB, CordThicknessCM, CordVisualSlot);

	PouchVisual->SetWorldTransform(ABTSMakeSlingshotVisualTransform(
		PouchVisual->GetStaticMesh(),
		WorldLocation,
		WorldRotation,
		PouchSizeCM,
		PouchVisualSlot,
		EABTSSlingshotVisualAnchor::BoundsCenter));
	CordSegmentA->SetVisibility(true, true);
	CordSegmentB->SetVisibility(true, true);
	PouchVisual->SetVisibility(true, true);
}

EABTSItemId AABTSM51SlingshotCord::GetStakeItem() const
{
	return StakeA.IsValid() ? StakeA->GetStakeItem() : EABTSItemId::SimpleStake;
}

void AABTSM51SlingshotCord::NotifyActorOnClicked(const FKey ButtonPressed)
{
	Super::NotifyActorOnClicked(ButtonPressed);
	if (ButtonPressed != EKeys::LeftMouseButton || GetWorld() == nullptr) return;
	if (AABTSM51PlayerController* Controller = Cast<AABTSM51PlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		Controller->InteractWithSlingshotCord(this);
	}
}
