// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ABTSM2Planet.generated.h"

class UProceduralMeshComponent;

/** A CellTopo entry. It is the future owner of all M3+ gameplay data; the render mesh owns no logic state. */
USTRUCT(BlueprintType)
struct FABTSM2Cell
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M2")
	FVector UnitCenter = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M2")
	TArray<int32> NeighborCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M2")
	bool bIsPentagon = false;
};

/** M2's independent logical sphere and collision surface. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM2Planet : public AActor
{
	GENERATED_BODY()

public:
	AABTSM2Planet();

	virtual void BeginPlay() override;

	/** Explicit rebuild entry for runtime tests. M2 does not rebuild in OnConstruction. */
	UFUNCTION(BlueprintCallable, Category = "ABTS|M2")
	bool RebuildPlanet();

	UFUNCTION(BlueprintPure, Category = "ABTS|M2")
	FVector GetSurfaceWorldLocation(const FVector& UnitDirection, float HeightOffsetCM = 0.0f) const;

	/** The world-space center used by all M2 radial frame calculations. */
	UFUNCTION(BlueprintPure, Category = "ABTS|M2")
	FVector GetPlanetCenterWorld() const { return GetActorLocation(); }

	UFUNCTION(BlueprintPure, Category = "ABTS|M2")
	float GetPlanetRadiusCM() const { return PlanetRadiusCM; }

	UFUNCTION(BlueprintPure, Category = "ABTS|M2")
	FVector GetRadialUpAtWorldLocation(const FVector& WorldLocation) const;

	UFUNCTION(BlueprintPure, Category = "ABTS|M2")
	FTransform GetNorthPoleSpawnTransform(float HeightOffsetCM = 140.0f) const;

	UFUNCTION(BlueprintPure, Category = "ABTS|M2")
	int32 GetLogicalCellCount() const { return LogicalCells.Num(); }

	UFUNCTION(BlueprintPure, Category = "ABTS|M2")
	int32 GetSurfaceTriangleCount() const { return SurfaceTriangleCount; }

	/** Zero is required: every triangle uses the front-face winding expected by ProceduralMeshComponent. */
	UFUNCTION(BlueprintPure, Category = "ABTS|M2")
	int32 GetInwardSurfaceTriangleCount() const { return InwardSurfaceTriangleCount; }

	UFUNCTION(BlueprintPure, Category = "ABTS|M2")
	bool IsPlanetReady() const { return bPlanetReady; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M2")
	TObjectPtr<UProceduralMeshComponent> ContinuousSurface;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M2|Topology", meta = (ClampMin = "1", ClampMax = "6"))
	int32 LogicalSubdivision = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M2|Surface", meta = (ClampMin = "1", ClampMax = "7"))
	int32 SurfaceSubdivision = 7;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M2|Surface", meta = (ClampMin = "1000.0"))
	float PlanetRadiusCM = 10000.0f;

	/** M3+ should access CellTopo through explicit queries rather than mutate it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M2")
	TArray<FABTSM2Cell> LogicalCells;

	// Public solely so the implementation's file-local mesh helper can build the transient mesh.
	struct FUnitSphereMesh
	{
		TArray<FVector> Vertices;
		TArray<FIntVector> Triangles;
	};

private:

	static void BuildUnitIcosphere(int32 Subdivision, FUnitSphereMesh& OutMesh);
	static void SubdivideOnce(FUnitSphereMesh& Mesh);
	static int32 CountWrongProceduralMeshWinding(const FUnitSphereMesh& Mesh);
	void BuildLogicalTopology();
	void BuildContinuousSurface();
	bool ValidateTopology() const;

	int32 SurfaceTriangleCount = 0;
	int32 InwardSurfaceTriangleCount = 0;
	bool bPlanetReady = false;
};
