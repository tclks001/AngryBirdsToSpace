// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Party/ABTSBirdTypes.h"
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
	void SetBirdIdentity(EABTSBirdId InBirdId, EABTSBirdSlingshotCapability InCapability, bool bInPlayerControlled);
	void SetPartyCollisionIsolation(bool bIsolateFromParty);
	void SetPartyControlled(bool bInPlayerControlled);
	void ApplyPartyMoveInput(const FVector& Direction, float Scale);
	void ApplyPartyJump();
	bool IsRadiallyGrounded() const;
	EABTSBirdId GetBirdId() const { return BirdId; }
	EABTSBirdSlingshotCapability GetSlingshotCapability() const { return SlingshotCapability; }
	UFUNCTION(BlueprintPure, Category = "ABTS|M4|Slingshot")
	bool CanUseSlingshotCapability(EABTSBirdSlingshotCapability RequiredCapability) const;
	bool IsPartyControlled() const { return bPartyControlled; }

	UFUNCTION(BlueprintPure, Category = "ABTS|Movement")
	EABTSBirdMovementMode GetSelectedMovementMode() const { return MovementMode; }

private:
	void ConfigureMovementMode();
	void ApplyMoveInput(const FVector& Direction, float Scale);
	void MoveWithRadialPhysicsForward(float Value);
	void MoveWithRadialPhysicsRight(float Value);
	void TurnWithRadialPhysics(float Value);
	void LookWithRadialPhysics(float Value);
	void BeginRadialJump();

	/** Select on the C++ class defaults or a Blueprint child before starting PIE. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|Movement", meta = (AllowPrivateAccess = "true"))
	EABTSBirdMovementMode MovementMode = EABTSBirdMovementMode::ForceSuspension;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Bird", meta = (AllowPrivateAccess = "true"))
	EABTSBirdId BirdId = EABTSBirdId::Red;

	/** Reserved launch eligibility queried by the future slingshot system. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Slingshot", meta = (AllowPrivateAccess = "true"))
	EABTSBirdSlingshotCapability SlingshotCapability = EABTSBirdSlingshotCapability::Simple;

	bool bPartyControlled = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|Movement|Legacy", meta = (AllowPrivateAccess = "true", NoEditInline))
	TObjectPtr<UABTSM25RadialMovementComponent> RadialMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|Movement|Force", meta = (AllowPrivateAccess = "true", NoEditInline))
	TObjectPtr<UABTSRadialForceMovementComponent> ForceMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|Movement|Force", meta = (AllowPrivateAccess = "true", NoEditInline))
	TObjectPtr<UABTSRadialSurfaceSuspensionComponent> SurfaceSuspension;
};
