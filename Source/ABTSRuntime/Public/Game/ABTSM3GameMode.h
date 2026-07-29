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
	void TryCompleteM3R0Smoke();
	void FinishM3R0Smoke(bool bPassed, const FString& Failure);
	void TryCompleteM3R1Smoke();
	void FinishM3R1Smoke(bool bPassed, const FString& Failure);
	void TryCompleteM3R2Smoke();
	void FinishM3R2Smoke(bool bPassed, const FString& Failure);
	void TryCompleteM3R3Smoke();
	void FinishM3R3Smoke(bool bPassed, const FString& Failure);

	FTimerHandle InitialRoadSpawnTimer;
	FTimerHandle M3R0SmokeTimer;
	FTimerHandle M3R1SmokeTimer;
	FTimerHandle M3R2SmokeTimer;
	FTimerHandle M3R3SmokeTimer;
	int32 InitialRoadSpawnAttempts = 0;
	double M3R0SmokeStartSeconds = 0.0;
	double M3R1SmokeStartSeconds = 0.0;
	double M3R2SmokeStartSeconds = 0.0;
	double M3R3SmokeStartSeconds = 0.0;
	bool bInitialPlayerPlaced = false;
};
