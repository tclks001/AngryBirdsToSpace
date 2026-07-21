// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Player/ABTSM2BirdCharacter.h"
#include "ABTSM25BirdCharacter.generated.h"

class UABTSM25RadialMovementComponent;

/** M2.5 playable bird: input shell for radial gravity, collision and jump. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM25BirdCharacter : public AABTSM2BirdCharacter
{
	GENERATED_BODY()

public:
	AABTSM25BirdCharacter();

	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
	void MoveWithRadialPhysicsForward(float Value);
	void MoveWithRadialPhysicsRight(float Value);
	void TurnWithRadialPhysics(float Value);
	void LookWithRadialPhysics(float Value);
	void BeginRadialJump();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M2.5", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UABTSM25RadialMovementComponent> RadialMovement;
};
