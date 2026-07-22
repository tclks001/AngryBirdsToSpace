// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "ABTSMovementModeSelector.generated.h"

/** Optional per-level editor selector. If present, it overrides the Pawn class default movement mode. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSMovementModeSelector : public AActor
{
	GENERATED_BODY()

public:
	AABTSMovementModeSelector();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|Movement")
	EABTSBirdMovementMode MovementMode = EABTSBirdMovementMode::ForceSuspension;
};
