// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/ABTSInventoryTypes.h"
#include "ABTSM51WorldSystem.generated.h"

class AABTSCraftingStation;
class AABTSCraftingSystem;
class AABTSM3Planet;
class AABTSM51PickupItem;
class AABTSM51SlingshotCord;
class AABTSM51SlingshotDirtHole;
class AABTSM51SlingshotStake;

/** CellTopo-driven M5.1 pickup, placement and slingshot assembly owner. */
UCLASS()
class ABTSRUNTIME_API AABTSM51WorldSystem : public AActor
{
	GENERATED_BODY()

public:
	AABTSM51WorldSystem();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	bool PlaceHeldToolAtAim(APlayerController& Controller);
	/** Debug placement path: a held stake can be installed at any unoccupied CellTopo cell without a DirtHole. */
	bool PlaceHeldStakeAtAim(APlayerController& Controller);
	bool InstallHeldStake(AABTSM51SlingshotDirtHole& Hole);
	bool SelectStakeForHeldCord(AABTSM51SlingshotStake& Stake);
	AABTSCraftingSystem* FindCraftingSystem() const;
	void SetDeveloperAnyCellStakePlacementEnabled(bool bEnabled) { bAllowDeveloperAnyCellStakePlacement = bEnabled; }

private:
	bool InitializeWorldContent();
	void SpawnSlingshotHoles();
	void SpawnSdfPickups();
	void CollectNearbyPickups();
	bool QueryCellTransform(int32 CellId, float SurfaceOffsetCM, FTransform& OutTransform) const;
	int32 SelectPlacementCell(const FVector& UnitDirection) const;
	int32 SelectDeveloperStakeCell(const FVector& UnitDirection) const;
	bool IsCellOccupied(int32 CellId) const;
	EABTSItemId ResolveStakeForCord(EABTSItemId CordItem) const;
	void LogPlaceFailure(const TCHAR* Reason) const;

	UPROPERTY(EditAnywhere, Category = "ABTS|M5.1|Pickup", meta = (ClampMin = "50.0", UIMax = "500.0"))
	float AutoPickupRadiusCM = 145.0f;

	/** Signed spherical SDF patch radius. Negative values are inside a resource patch. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M5.1|Pickup", meta = (ClampMin = "0.05", ClampMax = "1.2"))
	float ResourcePatchRadiusRadians = 0.34f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M5.1|Pickup", meta = (ClampMin = "1", ClampMax = "20"))
	int32 ResourceCellStride = 7;

	UPROPERTY(EditAnywhere, Category = "ABTS|M5.1|Pickup", meta = (ClampMin = "10", ClampMax = "500"))
	int32 MaxPickupActorCount = 180;

	UPROPERTY(EditAnywhere, Category = "ABTS|M5.1|Placement", meta = (ClampMin = "100.0", UIMax = "2000.0"))
	float PlacementTraceDistanceCM = 950.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M5.1|Placement", meta = (ClampMin = "100.0", UIMax = "1500.0"))
	float PlacementReachCM = 650.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M5.1|Placement", meta = (ClampMin = "0.1", ClampMax = "15.0"))
	float MaxPlacementSnapDegrees = 3.5f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M5.1|Slingshot", meta = (ClampMin = "0.001", ClampMax = "0.5"))
	float MaxStakeArcRadians = 0.12f;

	/** Allows stake placement at any unoccupied CellTopo center, including water and non-buildable cells. Cords still use MaxStakeArcRadians. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M5.1|Debug")
	bool bAllowDeveloperAnyCellStakePlacement = false;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M5.1|Classes")
	TSubclassOf<AABTSM51PickupItem> PickupClass;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M5.1|Classes")
	TSubclassOf<AABTSM51SlingshotDirtHole> DirtHoleClass;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M5.1|Classes")
	TSubclassOf<AABTSM51SlingshotStake> StakeClass;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M5.1|Classes")
	TSubclassOf<AABTSM51SlingshotCord> CordClass;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M5.1|Classes")
	TSubclassOf<AABTSCraftingStation> CraftingStationClass;

	TWeakObjectPtr<AABTSM3Planet> Planet;
	mutable TWeakObjectPtr<AABTSCraftingSystem> CraftingSystem;
	TArray<TWeakObjectPtr<AABTSM51PickupItem>> Pickups;
	TSet<int32> OccupiedCells;
	TWeakObjectPtr<AABTSM51SlingshotStake> PendingCordStake;
	bool bInitialized = false;
};
