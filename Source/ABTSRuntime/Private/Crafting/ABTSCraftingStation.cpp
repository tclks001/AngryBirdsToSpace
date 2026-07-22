// Copyright Epic Games, Inc. All Rights Reserved.

#include "Crafting/ABTSCraftingStation.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Player/ABTSM5PlayerController.h"
#include "UObject/ConstructorHelpers.h"

AABTSCraftingStation::AABTSCraftingStation()
{
	PrimaryActorTick.bCanEverTick = false;
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StationVisual"));
	SetRootComponent(Visual);
	// M5 stations are interaction placeholders, not gameplay obstacles. The
	// force-driven bird mover sweeps on the Pawn channel; a BlockAllDynamic
	// cube traps the capsule in repeated substep collisions. Keep only the
	// Visibility query required by PlayerController click events.
	Visual->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Visual->SetCollisionResponseToAllChannels(ECR_Ignore);
	Visual->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded()) Visual->SetStaticMesh(Cube.Object);
	Visual->SetRelativeScale3D(FVector(0.8f, 0.8f, 0.45f));
}

bool AABTSCraftingStation::IsWithinUseRange(const FVector& WorldLocation) const
{
	return FVector::DistSquared(WorldLocation, GetActorLocation()) <= FMath::Square(UseRangeCM);
}

void AABTSCraftingStation::NotifyActorOnClicked(const FKey ButtonPressed)
{
	Super::NotifyActorOnClicked(ButtonPressed);
	if (ButtonPressed != EKeys::LeftMouseButton || GetWorld() == nullptr) return;
	if (AABTSM5PlayerController* Controller = Cast<AABTSM5PlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		Controller->OpenCraftingFromStation(this);
	}
}
