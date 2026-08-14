// Copyright Epic Games, Inc. All Rights Reserved.

#include "Crafting/ABTSCraftingStation.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInterface.h"
#include "Player/ABTSM5PlayerController.h"
#include "UObject/ConstructorHelpers.h"
#include "World/ABTSVisualTuning.h"

AABTSCraftingStation::AABTSCraftingStation()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("StationRoot"));
	SetRootComponent(Root);
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StationVisual"));
	Visual->SetupAttachment(Root);
	// M5 stations are interaction placeholders, not gameplay obstacles. The
	// force-driven bird mover sweeps on the Pawn channel; a BlockAllDynamic
	// cube traps the capsule in repeated substep collisions. Keep only the
	// Visibility query required by PlayerController click events.
	Visual->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Visual->SetCollisionResponseToAllChannels(ECR_Ignore);
	Visual->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Workbench(TEXT("/Game/StaticMesh/Workbench/SM_Workbench.SM_Workbench"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WorkbenchMaterial(TEXT("/Game/StaticMesh/Workbench/MI_Workbench.MI_Workbench"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Furnace(TEXT("/Game/StaticMesh/Furnace/SM_Furnace.SM_Furnace"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FurnaceMaterial(TEXT("/Game/StaticMesh/Furnace/MI_Furnace.MI_Furnace"));
	WorkbenchMeshAsset = Workbench.Object;
	WorkbenchMaterialAsset = WorkbenchMaterial.Object;
	FurnaceMeshAsset = Furnace.Object;
	FurnaceMaterialAsset = FurnaceMaterial.Object;
	SetStationType(StationType);
}

void AABTSCraftingStation::SetStationType(const EABTSCraftingStationType InStationType)
{
	StationType = InStationType;
	if (Visual == nullptr) return;

	switch (InStationType)
	{
	case EABTSCraftingStationType::Workbench:
		Visual->SetStaticMesh(WorkbenchMeshAsset);
		Visual->SetMaterial(0, WorkbenchMaterialAsset);
		break;
	case EABTSCraftingStationType::Furnace:
		Visual->SetStaticMesh(FurnaceMeshAsset);
		Visual->SetMaterial(0, FurnaceMaterialAsset);
		break;
	default:
		break;
	}
	RefreshVisualTuning();
}

void AABTSCraftingStation::RefreshVisualTuning()
{
	if (Visual == nullptr) return;
	const EABTSVisualTuningTarget Target =
		StationType == EABTSCraftingStationType::Furnace
			? EABTSVisualTuningTarget::Furnace
			: EABTSVisualTuningTarget::Workbench;
	const FABTSVisualTuningValue& Tuning = ABTSGetVisualTuning(Target);
	Visual->SetRelativeScale3D(
		FVector(0.8f, 0.8f, 0.45f) * Tuning.ScaleMultiplier);
	Visual->SetRelativeLocation(FVector(0.0f, 0.0f, Tuning.LocalZOffsetCM));
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
