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

	/**
	 * Diagnostic alternative: only a blocking collision whose normal is close to
	 * radial Up establishes grounded state. Radial-height contact no longer grants jumping.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|Movement|Experiment")
	bool bUseCollisionNormalGroundingExperiment = false;

	/** Maximum angle between the blocking-hit normal and radial Up that counts as ground. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|Movement|Experiment", meta = (ClampMin = "0.0", ClampMax = "89.0", UIMin = "0.0", UIMax = "75.0", EditCondition = "bUseCollisionNormalGroundingExperiment"))
	float CollisionGroundMaxAngleDegrees = 55.0f;
};
