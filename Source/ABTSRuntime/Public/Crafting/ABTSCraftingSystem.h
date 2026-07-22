// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Crafting/ABTSCraftingTypes.h"
#include "GameFramework/Actor.h"
#include "Inventory/ABTSInventoryTypes.h"
#include "ABTSCraftingSystem.generated.h"

class AABTSCraftingStation;
class AABTSBirdParty;
class UABTSCraftingCatalog;
class UABTSInventoryComponent;

/** Runtime owner of the shared inventory, recipe catalog and nearby-station snapshot. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSCraftingSystem : public AActor
{
	GENERATED_BODY()

public:
	AABTSCraftingSystem();
	virtual void BeginPlay() override;

	UABTSInventoryComponent* GetInventory() const { return Inventory; }
	UABTSCraftingCatalog* GetCatalog() const { return Catalog; }
	bool IsRedBirdControlled() const;
	bool IsStationAvailable(EABTSCraftingStationType StationType) const;
	bool Craft(FName RecipeId, int32 CraftCount);
	AABTSBirdParty* FindParty() const;

private:
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M5")
	TObjectPtr<UABTSInventoryComponent> Inventory;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M5")
	TObjectPtr<UABTSCraftingCatalog> Catalog;

	mutable TWeakObjectPtr<AABTSBirdParty> Party;
};
