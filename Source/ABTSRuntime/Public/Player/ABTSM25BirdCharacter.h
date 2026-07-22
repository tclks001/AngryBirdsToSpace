// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Player/ABTSM2BirdCharacter.h"
#include "ABTSM25BirdCharacter.generated.h"

class UABTSM25RadialMovementComponent;
class UABTSRadialForceMovementComponent;
class UABTSRadialSurfaceSuspensionComponent;

/** Editor-selectable player movement implementation. */
UENUM(BlueprintType)
enum class EABTSBirdMovementMode : uint8
{
	ForceSuspension UMETA(DisplayName = "Force + Radial Suspension (Recommended)"),
	LegacySweep UMETA(DisplayName = "Legacy Kinematic Sweep")
};

/** M2.5 playable bird: input shell for radial gravity, collision and jump. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM25BirdCharacter : public AABTSM2BirdCharacter
{
	GENERATED_BODY()

public:
	AABTSM25BirdCharacter();

	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** Clears both implementations so a spawn teleport cannot retain stale velocity. */
	void ResetRadialMovementState();

	UFUNCTION(BlueprintPure, Category = "ABTS|Movement")
	EABTSBirdMovementMode GetSelectedMovementMode() const { return MovementMode; }

private:
	void ConfigureMovementMode();
	void MoveWithRadialPhysicsForward(float Value);
	void MoveWithRadialPhysicsRight(float Value);
	void TurnWithRadialPhysics(float Value);
	void LookWithRadialPhysics(float Value);
	void BeginRadialJump();

	/** Select on the C++ class defaults or a Blueprint child before starting PIE. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|Movement", meta = (AllowPrivateAccess = "true"))
	EABTSBirdMovementMode MovementMode = EABTSBirdMovementMode::ForceSuspension;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|Movement|Legacy", meta = (AllowPrivateAccess = "true", NoEditInline))
	TObjectPtr<UABTSM25RadialMovementComponent> RadialMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|Movement|Force", meta = (AllowPrivateAccess = "true", NoEditInline))
	TObjectPtr<UABTSRadialForceMovementComponent> ForceMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|Movement|Force", meta = (AllowPrivateAccess = "true", NoEditInline))
	TObjectPtr<UABTSRadialSurfaceSuspensionComponent> SurfaceSuspension;
};
