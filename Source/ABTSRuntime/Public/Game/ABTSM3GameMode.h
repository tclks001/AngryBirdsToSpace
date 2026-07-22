// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ABTSM3GameMode.generated.h"

/** M3 entry keeps M2.5 radial movement while the placed M3 Planet owns PCG and presentation. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM3GameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AABTSM3GameMode();
	virtual void BeginPlay() override;


protected:
	/** Stage hook called after the initial player is placed on the generated road. */
	virtual void OnInitialPlayerPlaced(ACharacter& Character, const FTransform& SpawnTransform, int32 SpawnCellId);


private:
	void TryPlacePlayerAtInitialRoad();

	FTimerHandle InitialRoadSpawnTimer;
	int32 InitialRoadSpawnAttempts = 0;
};
