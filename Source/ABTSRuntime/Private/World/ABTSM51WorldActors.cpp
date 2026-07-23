// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM51WorldActors.h"

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
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StakeVisual"));
	SetRootComponent(Visual);
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
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded()) Visual->SetStaticMesh(Cube.Object);
}

void AABTSM51SlingshotCord::InitializeCord(
	AABTSM51SlingshotStake* InStakeA,
	AABTSM51SlingshotStake* InStakeB,
	const FVector& InEndpointA,
	const FVector& InEndpointB)
{
	StakeA = InStakeA;
	StakeB = InStakeB;
	EndpointA = InEndpointA;
	EndpointB = InEndpointB;
	const FVector Delta = EndpointB - EndpointA;
	const float Length = Delta.Size();
	if (Length <= SMALL_NUMBER) return;
	SetActorLocation((EndpointA + EndpointB) * 0.5f);
	SetActorRotation(FRotationMatrix::MakeFromX(Delta / Length).ToQuat());
	Visual->SetRelativeScale3D(FVector(Length / 100.0f, 0.035f, 0.035f));
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
