// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM8BridgeActors.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "World/ABTSCollisionChannels.h"
#include "World/ABTSVisualTuning.h"

AABTSM8WaterBarrierActor::AABTSM8WaterBarrierActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("WaterBarrierCollision"));
	SetRootComponent(Collision);
	Collision->SetCollisionProfileName(TEXT("BlockAll"));
	Collision->SetCollisionObjectType(ABTSDeveloperObstacleChannel);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Collision->SetGenerateOverlapEvents(false);
}

void AABTSM8WaterBarrierActor::InitializeBarrier(const FABTSM3CellEdgeKey& InEdge, const FTransform& Transform, const FVector& HalfExtentCM)
{
	EdgeKey = InEdge;
	Collision->SetCollisionObjectType(ABTSDeveloperObstacleChannel);
	SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	Collision->SetBoxExtent(HalfExtentCM.ComponentMax(FVector(1.0f)), true);
}

void AABTSM8WaterBarrierActor::OpenPassage()
{
	Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetActorHiddenInGame(true);
}

AABTSM8BridgeActor::AABTSM8BridgeActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("BridgeRoot"));
	SetRootComponent(Root);
	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("BridgeCollision"));
	Collision->SetupAttachment(Root);
	Collision->SetCollisionProfileName(TEXT("BlockAll"));
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Collision->SetGenerateOverlapEvents(false);
	Deck = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BridgeDeck"));
	Deck->SetupAttachment(Root);
	Deck->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Deck->SetGenerateOverlapEvents(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BridgeMesh(
		TEXT("/Game/StaticMesh/Bridge/SM_Bridge.SM_Bridge"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BridgeMaterial(
		TEXT("/Game/StaticMesh/Bridge/MI_Bridge.MI_Bridge"));
	if (BridgeMesh.Succeeded()) Deck->SetStaticMesh(BridgeMesh.Object);
	if (BridgeMaterial.Succeeded()) Deck->SetMaterial(0, BridgeMaterial.Object);
	RefreshVisualTuning();
}

void AABTSM8BridgeActor::InitializeBridge(const FABTSM3CellEdgeKey& InEdge, const FTransform& Transform, const FVector& DimensionsCM)
{
	EdgeKey = InEdge;
	SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	BaseDimensionsCM = DimensionsCM.ComponentMax(FVector(1.0f));
	Collision->SetBoxExtent(BaseDimensionsCM * 0.5f, true);
	RefreshVisualTuning();
}

void AABTSM8BridgeActor::RefreshVisualTuning()
{
	if (Deck == nullptr) return;
	const FABTSVisualTuningValue& Tuning = ABTSGetVisualTuning(
		EABTSVisualTuningTarget::Bridge);
	FVector MeshSizeCM(100.0f);
	FVector MeshBoundsOrigin = FVector::ZeroVector;
	if (const UStaticMesh* Mesh = Deck->GetStaticMesh())
	{
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		MeshSizeCM = (Bounds.BoxExtent * 2.0f).ComponentMax(FVector(1.0f));
		MeshBoundsOrigin = Bounds.Origin;
	}
	// The authored bridge's X axis is its span axis. Fit only that axis and
	// carry the resulting scalar to Y/Z so the mesh keeps its authored shape.
	const float TunedUniformScale =
		(BaseDimensionsCM.X / MeshSizeCM.X) * Tuning.ScaleMultiplier;
	const FVector TunedScale(TunedUniformScale);
	Deck->SetRelativeScale3D(TunedScale);
	Deck->SetRelativeLocation(
		-MeshBoundsOrigin * TunedScale
			+ FVector(0.0f, 0.0f, Tuning.LocalZOffsetCM));
}
