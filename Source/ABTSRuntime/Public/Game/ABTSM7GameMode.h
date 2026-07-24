// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Game/ABTSM6GameMode.h"
#include "ABTSM7GameMode.generated.h"

class AABTSM7BuildingMaterialSystem;
class AABTSM73StableBuildingActor;

/** M7 entry owns the material runtime and an optional M7.3-A first-anchor building test. */
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

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M7.3-A")
	TSubclassOf<AABTSM73StableBuildingActor> StableBuildingClass;

	/** Test bridge to M3: spawn one M7.3-A structure at the first TaskGraph building anchor. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-A")
	bool bSpawnStableBuildingAtFirstAnchor = false;

	/** Test-only material gallery near the runtime TaskGraph spawn. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Testing")
	bool bSpawnBuildingMaterialTestSet = false;
};
