// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PCG/ABTSM3TaskGraphTypes.h"
#include "ABTSM8BridgeActors.generated.h"

class UBoxComponent;
class USceneComponent;
class UStaticMeshComponent;

/** Invisible collision segment generated from one CellTopo edge that blocks walking through water. */
UCLASS()
class ABTSRUNTIME_API AABTSM8WaterBarrierActor : public AActor
{
	GENERATED_BODY()

public:
	AABTSM8WaterBarrierActor();
	void InitializeBarrier(const FABTSM3CellEdgeKey& InEdge, const FTransform& Transform, const FVector& HalfExtentCM);
	void OpenPassage();
	const FABTSM3CellEdgeKey& GetEdgeKey() const { return EdgeKey; }

private:
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M8|Water")
	TObjectPtr<UBoxComponent> Collision;

	FABTSM3CellEdgeKey EdgeKey;
};

/** A simple collision bridge. Its dimensions and transform are resolved from the same CellTopo edge as its air-wall opening. */
UCLASS()
class ABTSRUNTIME_API AABTSM8BridgeActor : public AActor
{
	GENERATED_BODY()

public:
	AABTSM8BridgeActor();
	void InitializeBridge(const FABTSM3CellEdgeKey& InEdge, const FTransform& Transform, const FVector& DimensionsCM);
	void RefreshVisualTuning();
	const FABTSM3CellEdgeKey& GetEdgeKey() const { return EdgeKey; }
	const UStaticMeshComponent* GetDeckComponent() const { return Deck; }
	const UBoxComponent* GetCollisionComponent() const { return Collision; }

private:
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M8|Bridge")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M8|Bridge")
	TObjectPtr<UBoxComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M8|Bridge")
	TObjectPtr<UStaticMeshComponent> Deck;

	FABTSM3CellEdgeKey EdgeKey;
	FVector BaseDimensionsCM = FVector(100.0f);
};
