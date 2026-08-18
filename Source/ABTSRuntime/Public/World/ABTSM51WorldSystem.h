// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/ABTSInventoryTypes.h"
#include "World/ABTSM51OrdinarySlingshotSlotSnapshot.h"
#include "World/ABTSM51PreviewFinaleFrame.h"
#include "ABTSM51WorldSystem.generated.h"

class AABTSCraftingStation;
class AABTSCraftingSystem;
class AABTSM3Planet;
class AABTSM51PickupItem;
class AABTSM51SlingshotCord;
class AABTSM51SlingshotDirtHole;
class AABTSM51SlingshotStake;
class APawn;
class UABTSInventoryComponent;

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
	/** PIE visual-calibration helper. Atomically spawns all four raw pickups outside auto-pickup range. */
	bool SpawnPickupShowcase(float RequestedDistanceCM = 450.0f);
	/** Explicit-pawn core used by the console entry and isolated runtime validation. */
	bool SpawnPickupShowcaseAroundPawn(const APawn& Pawn, float RequestedDistanceCM = 450.0f);
	/** Debug placement path: a held stake can be installed at any unoccupied CellTopo cell without a DirtHole. */
	bool PlaceHeldStakeAtAim(APlayerController& Controller);
	bool InstallHeldStake(AABTSM51SlingshotDirtHole& Hole);
	bool SelectStakeForHeldCord(AABTSM51SlingshotStake& Stake);
	AABTSCraftingSystem* FindCraftingSystem() const;
	bool GetFinaleSpaceSlots(AABTSM51SlingshotDirtHole*& OutLeft, AABTSM51SlingshotDirtHole*& OutRight) const;
	void SetDeveloperAnyCellStakePlacementEnabled(bool bEnabled)
	{
#if UE_BUILD_SHIPPING
		bAllowDeveloperAnyCellStakePlacement = false;
		(void)bEnabled;
#else
		bAllowDeveloperAnyCellStakePlacement = bEnabled;
#endif
	}

	/**
	 * Pre-BeginPlay injection point for the future M3R-4/R-6 accepted layout.
	 *
	 * Current production entry intentionally does not call this because R3.1
	 * still owns multiple unaccepted candidates. A failed request is retained as
	 * fail-closed state and never silently falls back to compatibility slots.
	 */
	bool ConfigureAcceptedOrdinarySlingshotSlotSnapshot(
		const FABTSM51OrdinarySlingshotSlotSnapshot& InSnapshot);

	/** Pre-BeginPlay injection for an explicitly selected Preview/Test candidate. */
	bool ConfigurePreviewOrdinarySlingshotSlotSnapshot(
		const FABTSM51OrdinarySlingshotSlotSnapshot& InSnapshot);

	/** Pre-BeginPlay injection of the M3R-5.2 Preview/Test finale frame. */
	bool ConfigurePreviewFinaleFrame(
		const FABTSM51PreviewFinaleFrameContext& InContext);

	/** Null unless a valid Preview/Test frame was explicitly configured. */
	const FABTSM51PreviewFinaleFrameContext*
		GetPreviewFinaleFrameContext() const;

	/** The exact frame used to spawn the finale pair, or null after rejection. */
	const FABTSM110FinaleLocalFrame* GetActiveFinaleFrame() const;

	/** Active ordinary connection limit, or zero after a rejected snapshot request. */
	int32 GetActiveOrdinaryMaxCordLengthCM() const;
	EABTSM51OrdinarySlingshotSlotSnapshotAuthority
		GetOrdinarySlotSnapshotAuthority() const
	{
		return OrdinarySlotSnapshotAuthority;
	}
	bool IsWorldContentInitialized() const { return bInitialized; }
	bool IsWorldInitializationRejected() const
	{
		return bInitializationRejected;
	}
	int32 GetOrdinarySlotCount() const { return OrdinarySlots.Num(); }
	int32 GetPickupCount() const { return Pickups.Num(); }
	FString BuildReleaseDiagnosticSummary() const;

private:
	bool InitializeWorldContent();
	bool SpawnSlingshotHoles();
	void SpawnSdfPickups();
	void CollectNearbyPickups();
	bool TryConnectCord(
		AABTSM51SlingshotStake& First,
		AABTSM51SlingshotStake& Second,
		EABTSItemId HeldCord,
		EABTSSlingshotTier Tier,
		UABTSInventoryComponent& Inventory);
	bool QueryCellTransform(int32 CellId, float SurfaceOffsetCM, FTransform& OutTransform) const;
	int32 SelectPlacementCell(const FVector& UnitDirection) const;
	int32 SelectDeveloperStakeCell(const FVector& UnitDirection) const;
	bool IsCellOccupied(int32 CellId) const;
	void LogPlaceFailure(const TCHAR* Reason) const;
	bool ConfigureOrdinarySlingshotSlotSnapshot(
		const FABTSM51OrdinarySlingshotSlotSnapshot& InSnapshot,
		EABTSM51OrdinarySlingshotSlotSnapshotAuthority InAuthority);
	const FABTSM110FinaleLocalFrame* ResolveFinaleFrame() const;

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

	/**
	 * Retained only for serialized Blueprint compatibility. Ordinary assembly
	 * now uses CompatibilityMaxCordLengthCM or the accepted snapshot value.
	 */
	UPROPERTY(EditAnywhere, Category = "ABTS|M5.1|Slingshot",
		meta = (DeprecatedProperty, DeprecationMessage = "Use MaxCordLengthCM in the accepted slot snapshot."))
	float MaxStakeArcRadians = 0.12f;

	/** Distance fallback for the current TaskGraph compatibility world. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M5.1|Slingshot",
		meta = (ClampMin = "100", ClampMax = "4000", Units = "cm"))
	int32 CompatibilityMaxCordLengthCM = 1200;

	/** Additional separation required from third stakes and existing cords. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M5.1|Slingshot",
		meta = (ClampMin = "0.0", ClampMax = "100.0", Units = "cm"))
	float CordConnectionClearanceCM = 8.0f;

	/** Allows stake placement at any unoccupied CellTopo center, including water and non-buildable cells. */
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
	TArray<TWeakObjectPtr<AABTSM51SlingshotDirtHole>> OrdinarySlots;
	TSet<int32> OccupiedCells;
	TWeakObjectPtr<AABTSM51SlingshotStake> PendingCordStake;
	TWeakObjectPtr<AABTSM51SlingshotDirtHole> FinaleLeftSlot;
	TWeakObjectPtr<AABTSM51SlingshotDirtHole> FinaleRightSlot;
	FABTSM51OrdinarySlingshotSlotSnapshot OrdinarySlotSnapshot;
	EABTSM51OrdinarySlingshotSlotSnapshotAuthority
		OrdinarySlotSnapshotAuthority =
			EABTSM51OrdinarySlingshotSlotSnapshotAuthority::None;
	bool bOrdinarySlotSnapshotRequested = false;
	bool bOrdinarySlotSnapshotValid = false;
	FABTSM51PreviewFinaleFrameContext PreviewFinaleFrameContext;
	bool bPreviewFinaleFrameRequested = false;
	bool bPreviewFinaleFrameValid = false;
	bool bSlingshotHolesSpawned = false;
	bool bInitializationRejected = false;
	bool bInitialized = false;
};
