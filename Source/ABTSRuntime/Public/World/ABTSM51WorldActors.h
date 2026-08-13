// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/ABTSInventoryTypes.h"
#include "Slingshot/ABTSSlingshotTypes.h"
#include "Slingshot/ABTSSlingshotVisualTypes.h"
#include "ABTSM51WorldActors.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class UMaterialInterface;
class UStaticMesh;

UCLASS()
class ABTSRUNTIME_API AABTSM51PickupItem : public AActor
{
	GENERATED_BODY()

public:
	AABTSM51PickupItem();
	void InitializePickup(EABTSItemId InItemId, int32 InQuantity, int32 InCellId);
	EABTSItemId GetItemId() const { return ItemId; }
	int32 GetQuantity() const { return Quantity; }
	int32 GetCellId() const { return CellId; }

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Visual;

	EABTSItemId ItemId = EABTSItemId::Branch;
	int32 Quantity = 1;
	int32 CellId = INDEX_NONE;
};

UCLASS()
class ABTSRUNTIME_API AABTSM51SlingshotDirtHole : public AActor
{
	GENERATED_BODY()

public:
	AABTSM51SlingshotDirtHole();
	void InitializeHole(int32 InCellId);
	void InitializeFinaleSpaceSlot(int32 InCellId, int32 InPairId, EABTSSlingshotSlotSide InSide);
	int32 GetCellId() const { return CellId; }
	bool IsOccupied() const { return OccupiedStake.IsValid(); }
	void SetOccupiedStake(AActor* Stake) { OccupiedStake = Stake; }
	AActor* GetOccupiedStake() const { return OccupiedStake.Get(); }
	EABTSSlingshotSlotKind GetSlotKind() const { return SlotKind; }
	EABTSSlingshotSlotSide GetSlotSide() const { return SlotSide; }
	int32 GetSlotPairId() const { return SlotPairId; }
	bool IsFinaleSpaceSlot() const { return SlotKind == EABTSSlingshotSlotKind::FinaleSpace; }
	virtual void NotifyActorOnClicked(FKey ButtonPressed) override;

private:
	void ApplySlotVisual(EABTSSlingshotSlotKind InSlotKind);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Visual;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> StandardSlotMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> StandardSlotMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> FinaleSlotMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> FinaleSlotMaterial;

	int32 CellId = INDEX_NONE;
	TWeakObjectPtr<AActor> OccupiedStake;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M11.0|Finale Slot")
	EABTSSlingshotSlotKind SlotKind = EABTSSlingshotSlotKind::Standard;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M11.0|Finale Slot")
	EABTSSlingshotSlotSide SlotSide = EABTSSlingshotSlotSide::None;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M11.0|Finale Slot")
	int32 SlotPairId = INDEX_NONE;
};

UCLASS()
class ABTSRUNTIME_API AABTSM51SlingshotStake : public AActor
{
	GENERATED_BODY()

public:
	AABTSM51SlingshotStake();
	void InitializeStake(EABTSItemId InStakeItem, int32 InCellId, const FVector& InUnitDirection);
	void ConfigureVisualDimensions(float DiameterCM, float HeightCM, UMaterialInterface* Material = nullptr);
	void ApplyVisualSlot(const FABTSSlingshotVisualSlot& VisualSlot, float DiameterCM, float HeightCM);
	EABTSItemId GetStakeItem() const { return StakeItem; }
	int32 GetCellId() const { return CellId; }
	const FVector& GetUnitDirection() const { return UnitDirection; }
	void SetInstalledSlotIdentity(EABTSSlingshotSlotKind InSlotKind, int32 InSlotPairId, EABTSSlingshotSlotSide InSlotSide);
	EABTSSlingshotSlotKind GetInstalledSlotKind() const { return InstalledSlotKind; }
	EABTSSlingshotSlotSide GetInstalledSlotSide() const { return InstalledSlotSide; }
	int32 GetInstalledSlotPairId() const { return InstalledSlotPairId; }
	FVector GetVisualTopWorldLocation() const;
	FVector GetVisualBottomWorldLocation() const;
	const UStaticMeshComponent* GetVisualComponent() const { return Visual; }
	float GetStakeObstructionRadiusCM() const { return StakeObstructionRadiusCM; }
	bool HasCord() const { return bHasCord; }
	void SetHasCord(bool bValue) { bHasCord = bValue; }
	virtual void NotifyActorOnClicked(FKey ButtonPressed) override;

private:
	/** Stable gameplay transform at the stake centre; visual pivot correction must not move it. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Visual;

	EABTSItemId StakeItem = EABTSItemId::SimpleStake;
	int32 CellId = INDEX_NONE;
	FVector UnitDirection = FVector::UpVector;
	float VisualHeightCM = 220.0f;
	float StakeObstructionRadiusCM = 14.0f;
	bool bHasCord = false;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M11.0|Finale Slot")
	EABTSSlingshotSlotKind InstalledSlotKind = EABTSSlingshotSlotKind::Standard;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M11.0|Finale Slot")
	EABTSSlingshotSlotSide InstalledSlotSide = EABTSSlingshotSlotSide::None;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M11.0|Finale Slot")
	int32 InstalledSlotPairId = INDEX_NONE;
};

UCLASS()
class ABTSRUNTIME_API AABTSM51SlingshotCord : public AActor
{
	GENERATED_BODY()

public:
	AABTSM51SlingshotCord();
	void InitializeCord(AABTSM51SlingshotStake* InStakeA, AABTSM51SlingshotStake* InStakeB, const FVector& InEndpointA, const FVector& InEndpointB);
	void InitializeCordWithTier(AABTSM51SlingshotStake* InStakeA, AABTSM51SlingshotStake* InStakeB, const FVector& InEndpointA, const FVector& InEndpointB, EABTSSlingshotTier InTier);
	void ConfigureVisualThickness(float ThicknessCM, UMaterialInterface* Material = nullptr);
	void ApplyVisualSlot(const FABTSSlingshotVisualSlot& VisualSlot, float ThicknessCM);
	void SetPouchVisualSlot(const FABTSSlingshotVisualSlot& InVisualSlot) { PouchVisualSlot = InVisualSlot; }
	const FABTSSlingshotVisualSlot& GetPouchVisualSlot() const { return PouchVisualSlot; }
	void ConfigureTwoCordVisuals(
		const FABTSSlingshotVisualSlot& CordSlot,
		const FABTSSlingshotVisualSlot& PouchSlot,
		const FABTSSlingshotConnectionLayout& Layout,
		float ThicknessCM,
		const FVector& InPouchSizeCM = FVector(42.0f, 60.0f, 12.0f));
	void UpdatePulledPouchVisual(const FVector& WorldLocation, const FQuat& WorldRotation);
	void ResetPouchVisualToRest();
	FTransform GetRestPouchTransform() const;
	FVector GetEndpointA() const { return EndpointA; }
	FVector GetEndpointB() const { return EndpointB; }
	float GetCordObstructionRadiusCM() const { return CordObstructionRadiusCM; }
	float GetCordThicknessCM() const { return CordThicknessCM; }
	const FVector& GetPouchSizeCM() const { return PouchSizeCM; }
	const FABTSSlingshotConnectionLayout& GetConnectionLayout() const { return ConnectionLayout; }
	const UStaticMeshComponent* GetCordSegmentAComponent() const { return CordSegmentA; }
	const UStaticMeshComponent* GetCordSegmentBComponent() const { return CordSegmentB; }
	const UStaticMeshComponent* GetPouchVisualComponent() const { return PouchVisual; }
	EABTSItemId GetStakeItem() const;
	EABTSSlingshotTier GetSlingshotTier() const { return SlingshotTier; }
	AABTSM51SlingshotStake* GetStakeA() const { return StakeA.Get(); }
	AABTSM51SlingshotStake* GetStakeB() const { return StakeB.Get(); }
	bool IsFinaleSpaceSlingshot() const;
	int32 GetFinaleSlotPairId() const;
	virtual void NotifyActorOnClicked(FKey ButtonPressed) override;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Visual;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> CordSegmentA;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> CordSegmentB;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> PouchVisual;

	TWeakObjectPtr<AABTSM51SlingshotStake> StakeA;
	TWeakObjectPtr<AABTSM51SlingshotStake> StakeB;
	FVector EndpointA = FVector::ZeroVector;
	FVector EndpointB = FVector::ZeroVector;
	EABTSSlingshotTier SlingshotTier = EABTSSlingshotTier::Simple;
	FABTSSlingshotVisualSlot PouchVisualSlot;
	FABTSSlingshotVisualSlot CordVisualSlot;
	FABTSSlingshotConnectionLayout ConnectionLayout;
	float CordThicknessCM = 3.5f;
	float CordObstructionRadiusCM = 1.75f;
	FVector PouchSizeCM = FVector(42.0f, 60.0f, 12.0f);
	TObjectPtr<UStaticMesh> DefaultCordCylinderMesh;
	TObjectPtr<UStaticMesh> DefaultPouchSphereMesh;
};
