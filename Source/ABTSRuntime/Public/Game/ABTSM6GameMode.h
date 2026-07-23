// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Game/ABTSM51GameMode.h"
#include "ABTSM6GameMode.generated.h"

class AABTSM6SlingshotSystem;

/** M6 entry installs the slingshot launch and impact coordinator. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM6GameMode : public AABTSM51GameMode
{
	GENERATED_BODY()

public:
	AABTSM6GameMode();

protected:
	virtual void OnInitialPlayerPlaced(ACharacter& Character, const FTransform& SpawnTransform, int32 SpawnCellId) override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6")
	TSubclassOf<AABTSM6SlingshotSystem> SlingshotSystemClass;

	/** Test-only: creates complete simple/reinforced slingshots around the TaskGraph start Cell. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Testing")
	bool bSpawnDebugSlingshotsAtStart = false;
};
