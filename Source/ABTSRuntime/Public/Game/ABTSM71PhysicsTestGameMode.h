// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ABTSM71PhysicsTestGameMode.generated.h"

class AABTSBirdParty;
class AABTSM6SlingshotSystem;
class AABTSM7BuildingMaterialSystem;

/** Standalone planar M7.1 entry: no Planet, TaskGraph, PCG, pickups or runtime world placement. */
UCLASS(BlueprintType, meta = (DisplayName = "M7.1 Planar Physics Test GameMode"))
class ABTSRUNTIME_API AABTSM71PhysicsTestGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AABTSM71PhysicsTestGameMode();
	virtual void BeginPlay() override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M7.1|Classes")
	TSubclassOf<AABTSBirdParty> BirdPartyClass;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M7.1|Classes")
	TSubclassOf<AABTSM6SlingshotSystem> SlingshotSystemClass;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M7.1|Classes")
	TSubclassOf<AABTSM7BuildingMaterialSystem> BuildingMaterialSystemClass;
};
