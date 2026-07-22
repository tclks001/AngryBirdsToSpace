// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Game/ABTSM3GameMode.h"
#include "ABTSM4GameMode.generated.h"

class AABTSBirdParty;

/** M4 entry adds the fixed four-bird party, switching, following and persistent HUD. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM4GameMode : public AABTSM3GameMode
{
	GENERATED_BODY()

public:
	AABTSM4GameMode();

protected:
	virtual void OnInitialPlayerPlaced(ACharacter& Character, const FTransform& SpawnTransform, int32 SpawnCellId) override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M4")
	TSubclassOf<AABTSBirdParty> BirdPartyClass;
};
