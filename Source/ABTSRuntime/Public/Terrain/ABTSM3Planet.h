// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCG/ABTSM3TaskGraphTypes.h"
#include "Planet/ABTSM2Planet.h"
#include "Terrain/ABTSM3TerrainVisualField.h"
#include "ABTSM3Planet.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class UABTSM3TerrainMaterialBridge;

/** M3 presentation planet. CellTopo and TaskGraph remain the only gameplay sources. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM3Planet : public AABTSM2Planet
{
	GENERATED_BODY()

public:
	AABTSM3Planet();

	virtual bool RebuildPlanet() override;
	virtual float GetSurfaceRadiusAtDirection(const FVector& UnitDirection) const override;
	virtual FVector GetSurfaceNormalAtDirection(const FVector& UnitDirection) const override;

	UFUNCTION(BlueprintPure, Category = "ABTS|M3|PCG")
	const TArray<FABTSM3CellState>& GetGeneratedCellStates() const { return GeneratedCellStates; }

	UFUNCTION(BlueprintPure, Category = "ABTS|M3|PCG")
	const TArray<FABTSM3TaskNode>& GetGeneratedTasks() const { return GeneratedTasks; }

	/** Reserved interface for M4 modular building generation. M3 only returns validated spawn sites. */
	UFUNCTION(BlueprintPure, Category = "ABTS|M3|Building")
	const TArray<FABTSM3BuildingSpawnSite>& GetBuildingSpawnSites() const { return BuildingSpawnSites; }

	UFUNCTION(BlueprintPure, Category = "ABTS|M3|Surface")
	bool QuerySurface(const FVector& UnitDirection, FVector& OutWorldPosition, FVector& OutWorldNormal, float& OutSurfaceRadius, int32& OutCellId) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	int32 WorldSeed = 312503;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Terrain", meta = (ClampMin = "0.0"))
	float MacroHeightScaleCM = 900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Terrain", meta = (ClampMin = "0.0"))
	float TaskWaterDepthCM = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Terrain", meta = (ClampMin = "1.0"))
	float TerrainBlendWidthCM = 240.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Material")
	TObjectPtr<UMaterialInterface> TerrainMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|HISM")
	TObjectPtr<UStaticMesh> ForestInstanceMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|HISM")
	TObjectPtr<UStaticMesh> RockInstanceMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|HISM", meta = (ClampMin = "0", ClampMax = "8"))
	int32 InstancesPerCell = 2;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|HISM")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> ForestHISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|HISM")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> RockHISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	TArray<FABTSM3TaskNode> GeneratedTasks;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	TArray<FABTSM3CellState> GeneratedCellStates;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Building")
	TArray<FABTSM3BuildingSpawnSite> BuildingSpawnSites;

private:
	bool GenerateLogicalTerrain();
	void BuildM3ContinuousSurface();
	void BuildDecorInstances();
	void BuildBuildingSpawnSites();
	int32 FindNearestCell(const FVector& UnitDirection) const;

	TUniquePtr<FABTSM3TerrainVisualField> TerrainVisualField;

	UPROPERTY(Transient)
	TObjectPtr<UABTSM3TerrainMaterialBridge> TerrainMaterialBridge;
};
