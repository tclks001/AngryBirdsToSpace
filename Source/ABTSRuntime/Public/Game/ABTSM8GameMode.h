// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Game/ABTSM7GameMode.h"
#include "ABTSM8GameMode.generated.h"

class AABTSM8RecoveryBridgeSystem;

/** M8 entry adds automatic building-material recovery and CellTopo bridge traversal gates. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM8GameMode : public AABTSM7GameMode
{
	GENERATED_BODY()

public:
	AABTSM8GameMode();

protected:
	virtual void OnInitialPlayerPlaced(ACharacter& Character, const FTransform& SpawnTransform, int32 SpawnCellId) override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M8")
	TSubclassOf<AABTSM8RecoveryBridgeSystem> RecoveryBridgeSystemClass;

	/** PIE-only convenience: all known items start at this amount, with Bridge Kit seeded into the hotbar first. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Debug")
	bool bSeedDebugMaximumInventory = false;

	UPROPERTY(EditAnywhere, Category = "ABTS|M8|Debug", meta = (ClampMin = "1", ClampMax = "999"))
	int32 DebugMaximumInventoryQuantity = 99;
};
