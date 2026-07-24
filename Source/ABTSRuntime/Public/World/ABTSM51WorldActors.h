// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/ABTSInventoryTypes.h"
#include "Slingshot/ABTSSlingshotTypes.h"
#include "Slingshot/ABTSSlingshotVisualTypes.h"
#include "ABTSM51WorldActors.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;

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
	int32 GetCellId() const { return CellId; }
	bool IsOccupied() const { return OccupiedStake.IsValid(); }
	void SetOccupiedStake(AActor* Stake) { OccupiedStake = Stake; }
	virtual void NotifyActorOnClicked(FKey ButtonPressed) override;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Visual;

	int32 CellId = INDEX_NONE;
	TWeakObjectPtr<AActor> OccupiedStake;
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
	bool HasCord() const { return bHasCord; }
	void SetHasCord(bool bValue) { bHasCord = bValue; }
	virtual void NotifyActorOnClicked(FKey ButtonPressed) override;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Visual;

	EABTSItemId StakeItem = EABTSItemId::SimpleStake;
	int32 CellId = INDEX_NONE;
	FVector UnitDirection = FVector::UpVector;
	bool bHasCord = false;
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
	void ConfigureTwoCordVisuals(const FABTSSlingshotVisualSlot& CordSlot, const FABTSSlingshotVisualSlot& PouchSlot, const FABTSSlingshotConnectionLayout& Layout, float ThicknessCM);
	void UpdatePulledPouchVisual(const FVector& WorldLocation, const FQuat& WorldRotation);
	void ResetPouchVisualToRest();
	FTransform GetRestPouchTransform() const;
	FVector GetEndpointA() const { return EndpointA; }
	FVector GetEndpointB() const { return EndpointB; }
	EABTSItemId GetStakeItem() const;
	EABTSSlingshotTier GetSlingshotTier() const { return SlingshotTier; }
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
	TObjectPtr<UStaticMesh> DefaultCordCylinderMesh;
	TObjectPtr<UStaticMesh> DefaultPouchSphereMesh;
};
