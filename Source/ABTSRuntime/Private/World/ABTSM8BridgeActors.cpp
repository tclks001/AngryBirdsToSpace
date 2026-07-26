// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM8BridgeActors.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "World/ABTSCollisionChannels.h"

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
	Deck = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BridgeDeck"));
	SetRootComponent(Deck);
	Deck->SetCollisionProfileName(TEXT("BlockAll"));
	Deck->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Deck->SetGenerateOverlapEvents(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded()) Deck->SetStaticMesh(Cube.Object);
}

void AABTSM8BridgeActor::InitializeBridge(const FABTSM3CellEdgeKey& InEdge, const FTransform& Transform, const FVector& DimensionsCM)
{
	EdgeKey = InEdge;
	SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	Deck->SetRelativeScale3D(DimensionsCM.ComponentMax(FVector(1.0f)) / 100.0f);
}
