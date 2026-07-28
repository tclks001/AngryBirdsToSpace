// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCG/ABTSM3TaskGraphTypes.h"
#include "Planet/ABTSM2Planet.h"
#include "Terrain/ABTSM3TerrainVisualField.h"
#include "World/ABTSM110FinaleTypes.h"
#include "ABTSM3Planet.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class UABTSM3TerrainMaterialBridge;

USTRUCT(BlueprintType)
struct FABTSM3SurfacePhysicsProfile
{
	GENERATED_BODY()

	FABTSM3SurfacePhysicsProfile() = default;
	FABTSM3SurfacePhysicsProfile(const float InGroundDragPerSecond, const float InRestitution)
		: GroundDragPerSecond(InGroundDragPerSecond), Restitution(InRestitution) {}

	/** Linear tangential resistance consumed by the kinematic force mover. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float GroundDragPerSecond = 5.3f;

	/** Fraction of incoming normal speed returned after a sufficiently hard surface impact. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Restitution = 0.02f;
};

USTRUCT(BlueprintType)
struct FABTSM3SurfacePhysicsSample
{
	GENERATED_BODY()

	FABTSM3SurfaceSDFSample SDF;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics")
	float GroundDragPerSecond = 5.3f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics")
	float Restitution = 0.02f;
};

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

	UFUNCTION(BlueprintPure, Category = "ABTS|M3|PCG")
	const TArray<FABTSM3TaskLink>& GetGeneratedTaskLinks() const { return GeneratedTaskLinks; }

	UFUNCTION(BlueprintPure, Category = "ABTS|M3|PCG")
	const TArray<FABTSM3CellEdgeState>& GetGeneratedEdgeStates() const { return GeneratedEdgeStates; }

	/** Reserved interface for M4 modular building generation. M3 only returns validated spawn sites. */
	UFUNCTION(BlueprintPure, Category = "ABTS|M3|Building")
	const TArray<FABTSM3BuildingSpawnSite>& GetBuildingSpawnSites() const { return BuildingSpawnSites; }

	/** Deterministic terminal frame authored by M3 and consumed by M11 local-layout presets. */
	const FABTSM110FinaleLocalFrame& GetFinaleLaunchFrame() const { return FinaleLaunchFrame; }

	UFUNCTION(BlueprintPure, Category = "ABTS|M3|Surface")
	bool QuerySurface(const FVector& UnitDirection, FVector& OutWorldPosition, FVector& OutWorldNormal, float& OutSurfaceRadius, int32& OutCellId) const;

	/** CPU reconstruction of the terrain material's SDF weights for collision response. */
	bool QuerySurfacePhysics(const FVector& UnitDirection, FABTSM3SurfacePhysicsSample& OutSample) const;

	/** CPU counterpart of the final land + road + river SDF color used by M10. */
	bool QueryScoutMapTerrainColor(
		const FVector& UnitDirection,
		FLinearColor& OutColor,
		int32 StartCellHint = 0,
		int32* OutCellId = nullptr) const;

	/** Character-center spawn transform at the Start Task's first logical road Cell. */
	UFUNCTION(BlueprintPure, Category = "ABTS|M3|Spawn")
	bool GetInitialRoadSpawnTransform(float SurfaceOffsetCM, FTransform& OutWorldTransform, int32& OutCellId) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	int32 WorldSeed = 312503;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	FABTSM3PCGConfig PCGConfig;

	/** CellTopo anchor driven construction pads consumed by the M7 TaskGraph building spawner. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M7|Spherical Buildings")
	FABTSM3BuildingPadSettings BuildingPadSettings;

	/** World-space spacing of the one terminal Space-slingshot slot pair. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M11.0|Finale Closure",
		meta = (ClampMin = "100.0", ClampMax = "600.0", UIMin = "160.0", UIMax = "360.0", Units = "cm"))
	float FinaleSpaceSlotSeparationCM = 210.0f;

	/** Small lift that keeps the slot interaction mesh above the certified pad. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M11.0|Finale Closure",
		meta = (ClampMin = "0.0", ClampMax = "40.0", UIMax = "12.0", Units = "cm"))
	float FinaleSpaceSlotSurfaceOffsetCM = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Terrain", meta = (ClampMin = "0.0"))
	float MacroHeightScaleCM = 900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Terrain", meta = (ClampMin = "0.0"))
	float TaskWaterDepthCM = 80.0f;

	/**
	 * Diagnostic flat-surface experiment. Keeps TaskGraph terrain types, roads,
	 * rivers, materials and HISM placement, but removes all radial height
	 * variation (including the river depression) from the rendered/query surface.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Experiment")
	bool bDisableTerrainHeightVariationExperiment = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Terrain", meta = (ClampMin = "1.0"))
	float TerrainBlendWidthCM = 240.0f;

	/** Geometric transition width; independent from TerrainBlendWidthCM, which controls material color only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Terrain", meta = (ClampMin = "1.0"))
	float HeightBlendWidthCM = 160.0f;

	/** World-space radius of the central-difference stencil used to smooth terrain vertex normals. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Terrain", meta = (ClampMin = "10.0", ClampMax = "800.0", UIMin = "40.0", UIMax = "400.0"))
	float SurfaceNormalSmoothingDistanceCM = 160.0f;

	/** Independent from visual color blending, so art changes do not silently change locomotion. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics", meta = (ClampMin = "1.0", ClampMax = "1200.0"))
	float SurfacePhysicsBlendWidthCM = 240.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics")
	FABTSM3SurfacePhysicsProfile PlainPhysics = {5.3f, 0.02f};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics")
	FABTSM3SurfacePhysicsProfile ForestPhysics = {6.2f, 0.01f};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics")
	FABTSM3SurfacePhysicsProfile HighlandPhysics = {5.8f, 0.04f};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics")
	FABTSM3SurfacePhysicsProfile MountainPhysics = {4.1f, 0.16f};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics")
	FABTSM3SurfacePhysicsProfile RoadPhysics = {3.0f, 0.04f};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics")
	FABTSM3SurfacePhysicsProfile RiverPhysics = {11.0f, 0.0f};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Material")
	FLinearColor RoadColor = FLinearColor(0.22f, 0.12f, 0.045f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Material", meta = (ClampMin = "10.0", ClampMax = "800.0"))
	float TrailVisualHalfWidthCM = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Material", meta = (ClampMin = "10.0", ClampMax = "1200.0"))
	float MainRoadVisualHalfWidthCM = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Material")
	FLinearColor RiverColor = FLinearColor(0.03f, 0.20f, 0.36f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Material", meta = (ClampMin = "10.0", ClampMax = "600.0"))
	float StreamVisualHalfWidthCM = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Material", meta = (ClampMin = "10.0", ClampMax = "800.0"))
	float ShallowRiverVisualHalfWidthCM = 125.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Material", meta = (ClampMin = "10.0", ClampMax = "1200.0"))
	float DeepRiverVisualHalfWidthCM = 190.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Material")
	TObjectPtr<UMaterialInterface> TerrainMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|HISM")
	TObjectPtr<UStaticMesh> ForestInstanceMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|HISM")
	TObjectPtr<UStaticMesh> RockInstanceMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|HISM", meta = (ClampMin = "0", ClampMax = "8"))
	int32 InstancesPerCell = 2;

	/** 0 keeps trees purely radial; 1 fully follows the rendered terrain normal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|HISM", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "0.5"))
	float ForestSurfaceNormalBlend = 0.2f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|HISM")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> ForestHISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|HISM")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> RockHISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	TArray<FABTSM3TaskNode> GeneratedTasks;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	TArray<FABTSM3TaskLink> GeneratedTaskLinks;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	TArray<FABTSM3CellState> GeneratedCellStates;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	TArray<FABTSM3CellEdgeState> GeneratedEdgeStates;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	FABTSM3PCGSummary PCGSummary;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Building")
	TArray<FABTSM3BuildingSpawnSite> BuildingSpawnSites;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M11.0|Finale Frame")
	FABTSM110FinaleLocalFrame FinaleLaunchFrame;

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
