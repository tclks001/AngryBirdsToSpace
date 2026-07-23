// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Game/ABTSM6GameMode.h"
#include "ABTSM7GameMode.generated.h"

class AABTSM7BuildingMaterialSystem;

/** M7 entry adds the building-material runtime without generating buildings. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM7GameMode : public AABTSM6GameMode
{
	GENERATED_BODY()

public:
	AABTSM7GameMode();

protected:
	virtual void OnInitialPlayerPlaced(ACharacter& Character, const FTransform& SpawnTransform, int32 SpawnCellId) override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M7")
	TSubclassOf<AABTSM7BuildingMaterialSystem> BuildingMaterialSystemClass;

	/** Test-only material gallery near the runtime TaskGraph spawn. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Testing")
	bool bSpawnBuildingMaterialTestSet = false;
};

