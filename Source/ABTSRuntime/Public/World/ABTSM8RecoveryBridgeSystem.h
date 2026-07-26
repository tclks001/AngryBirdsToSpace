// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PCG/ABTSM3TaskGraphTypes.h"
#include "ABTSM8RecoveryBridgeSystem.generated.h"

class AABTSCraftingSystem;
class AABTSM3Planet;
class AABTSM7BuildingMaterialSystem;
class AABTSM8BridgeActor;
class AABTSM8WaterBarrierActor;
class APlayerController;
enum class EABTSM7BuildingMaterial : uint8;

/** M8 runtime owner for material auto-recovery and CellTopo water-edge bridge gates. */
UCLASS()
class ABTSRUNTIME_API AABTSM8RecoveryBridgeSystem : public AActor
{
	GENERATED_BODY()

public:
	AABTSM8RecoveryBridgeSystem();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Consumes a held Bridge Kit only when the cursor resolves to the unbuilt CellTopo BridgeSite edge. */
	bool PlaceHeldBridgeAtAim(APlayerController& Controller);

private:
	bool InitializeRuntime();
	void SubscribeToMaterialRecovery();
	void SpawnWaterBarriers();
	bool BuildEdgeFrame(const FABTSM3CellEdgeKey& Edge, FTransform& OutTransform, float& OutCellSpanCM) const;
	bool FindNearestUnbuiltBridgeSite(const FVector& UnitDirection, FABTSM3CellEdgeKey& OutEdge, float& OutAngularDistanceDegrees) const;
	void HandleMaterialRecovered(EABTSM7BuildingMaterial Material, int32 Quantity);
	void LogBridgePlacementFailure(const TCHAR* Reason) const;

	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Recovery")
	bool bEnableAutomaticMaterialRecovery = true;

	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Recovery", meta = (ClampMin = "1", ClampMax = "99"))
	int32 RecoveryQuantityPerDestroyedBrick = 1;

	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Water Barrier", meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float BarrierLengthMultiplier = 1.35f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Water Barrier", meta = (ClampMin = "10.0", UIMax = "200.0", Units = "cm"))
	float BarrierHalfThicknessCM = 35.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Water Barrier", meta = (ClampMin = "100.0", UIMax = "1500.0", Units = "cm"))
	float BarrierHeightCM = 650.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Bridge", meta = (ClampMin = "1.0", ClampMax = "15.0"))
	float MaxBridgePlacementSnapDegrees = 4.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Bridge", meta = (ClampMin = "100.0", UIMax = "1500.0", Units = "cm"))
	float BridgePlacementReachCM = 750.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Bridge", meta = (ClampMin = "100.0", UIMax = "1200.0", Units = "cm"))
	float BridgeDeckWidthCM = 240.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Bridge", meta = (ClampMin = "10.0", UIMax = "200.0", Units = "cm"))
	float BridgeDeckThicknessCM = 40.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Bridge", meta = (ClampMin = "0.0", UIMax = "500.0", Units = "cm"))
	float BridgeDeckSurfaceOffsetCM = 85.0f;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M8|Classes")
	TSubclassOf<AABTSM8WaterBarrierActor> WaterBarrierClass;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M8|Classes")
	TSubclassOf<AABTSM8BridgeActor> BridgeClass;

	TWeakObjectPtr<AABTSM3Planet> Planet;
	TWeakObjectPtr<AABTSCraftingSystem> CraftingSystem;
	TWeakObjectPtr<AABTSM7BuildingMaterialSystem> MaterialSystem;
	TMap<FABTSM3CellEdgeKey, TWeakObjectPtr<AABTSM8WaterBarrierActor>> WaterBarriers;
	TSet<FABTSM3CellEdgeKey> BuiltBridgeEdges;
	bool bRuntimeInitialized = false;
};
