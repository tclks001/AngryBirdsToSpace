// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Game/ABTSM4GameMode.h"
#include "ABTSM5GameMode.generated.h"

class AABTSCraftingSystem;

/** M5 entry adds shared inventory, crafting rules and the inventory/crafting HUD. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM5GameMode : public AABTSM4GameMode
{
	GENERATED_BODY()

public:
	AABTSM5GameMode();

protected:
	virtual void OnInitialPlayerPlaced(ACharacter& Character, const FTransform& SpawnTransform, int32 SpawnCellId) override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M5")
	TSubclassOf<AABTSCraftingSystem> CraftingSystemClass;

};
