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

/** Pure semantic result for one point on the same water centerline M3 presents. */
struct ABTSRUNTIME_API FABTSM8BridgePlacementGeometry
{
	FTransform BridgeTransform = FTransform::Identity;
	float AimDistanceCM = BIG_NUMBER;
	float WaterHalfWidthCM = 0.0f;
	float RiverSegmentLengthCM = 0.0f;
	bool bBarrierSegment = false;
	bool bCertifiedBridgeSite = false;
};

/** M8 runtime owner for material auto-recovery and CellTopo water-edge bridge gates. */
UCLASS()
class ABTSRUNTIME_API AABTSM8RecoveryBridgeSystem : public AActor
{
	GENERATED_BODY()

public:
	AABTSM8RecoveryBridgeSystem();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Consumes a held Bridge Kit when the cursor resolves to any unbuilt semantic water segment. */
	bool PlaceHeldBridgeAtAim(APlayerController& Controller);

	/** Resolves M3's visible flow/dual-edge water semantics into an across-river bridge frame. */
	static bool ResolveSemanticBridgeGeometry(
		const AABTSM3Planet& Planet,
		const FABTSM3CellEdgeState& EdgeState,
		const FVector& AimUnitDirection,
		FABTSM8BridgePlacementGeometry& OutGeometry);

	/** Covers the visible water from bank to bank, plus a dry-side safety margin. */
	static float ComputeWaterBarrierHalfWidthCM(
		float VisibleWaterHalfWidthCM,
		float BankSafetyMarginCM);

private:
	bool InitializeRuntime();
	void SubscribeToMaterialRecovery();
	void SpawnWaterBarriers();
	bool FindNearestUnbuiltWaterSegment(
		const FVector& UnitDirection,
		FABTSM3CellEdgeState& OutEdgeState,
		FABTSM8BridgePlacementGeometry& OutGeometry) const;
	void HandleMaterialRecovered(EABTSM7BuildingMaterial Material, int32 Quantity);
	void LogBridgePlacementFailure(
		const TCHAR* Reason,
		const FABTSM3CellEdgeState* EdgeState = nullptr,
		float AimDistanceCM = -1.0f,
		float AllowedAimDistanceCM = -1.0f,
		float PlayerDistanceCM = -1.0f,
		const FVector* AimPoint = nullptr,
		const FHitResult* Hit = nullptr) const;
#if WITH_EDITOR
	void ToggleBridgeSiteDebug();
	void RefreshBridgeSiteDebug(float DeltaSeconds);
#endif

	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Recovery")
	bool bEnableAutomaticMaterialRecovery = true;

	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Recovery", meta = (ClampMin = "1", ClampMax = "99"))
	int32 RecoveryQuantityPerDestroyedBrick = 1;

	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Water Barrier", meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float BarrierLengthMultiplier = 1.35f;

	/** Extra dry-side coverage beyond the visible river edge. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Water Barrier", meta = (ClampMin = "0.0", UIMax = "200.0", Units = "cm"))
	float BarrierHalfThicknessCM = 35.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Water Barrier", meta = (ClampMin = "100.0", UIMax = "1500.0", Units = "cm"))
	float BarrierHeightCM = 650.0f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Semantic placement uses BridgePlacementAimToleranceCM."))
	float MaxBridgePlacementSnapDegrees_DEPRECATED = 4.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Bridge", meta = (ClampMin = "0.0", UIMax = "500.0", Units = "cm"))
	float BridgePlacementAimToleranceCM = 140.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Bridge", meta = (ClampMin = "0.0", UIMax = "300.0", Units = "cm"))
	float BridgeBankOverlapCM = 80.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Bridge", meta = (ClampMin = "100.0", UIMax = "1500.0", Units = "cm"))
	float BridgePlacementReachCM = 750.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Bridge", meta = (ClampMin = "100.0", UIMax = "1200.0", Units = "cm"))
	float BridgeDeckWidthCM = 240.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Bridge", meta = (ClampMin = "10.0", UIMax = "200.0", Units = "cm"))
	float BridgeDeckThicknessCM = 40.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Bridge", meta = (ClampMin = "0.0", UIMax = "500.0", Units = "cm"))
	float BridgeDeckSurfaceOffsetCM = 85.0f;

	/** Additional opening on each side of the bridge deck for bird capsules and party formation. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Bridge", meta = (ClampMin = "0.0", UIMax = "300.0", Units = "cm"))
	float BridgeBarrierSideClearanceCM = 80.0f;

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
#if WITH_EDITOR
	bool bBridgeSiteDebugEnabled = false;
	bool bBridgeSiteDebugReadyLogged = false;
	float BridgeSiteDebugRefreshRemaining = 0.0f;
#endif
};
