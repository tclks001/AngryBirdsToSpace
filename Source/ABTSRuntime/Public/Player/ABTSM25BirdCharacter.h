// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Party/ABTSBirdTypes.h"
#include "Player/ABTSM2BirdCharacter.h"
#include "Presentation/ABTSBirdAnimationPresentationComponent.h"
#include "ABTSM25BirdCharacter.generated.h"

class UABTSM25RadialMovementComponent;
class UABTSRadialForceMovementComponent;
class UABTSRadialSurfaceSuspensionComponent;
class UABTSChaosBirdMovementComponent;
class UPrimitiveComponent;
class UMaterialInterface;

/** Editor-selectable player movement implementation. */
UENUM(BlueprintType)
enum class EABTSBirdMovementMode : uint8
{
	ForceSuspension UMETA(DisplayName = "Force + Radial Suspension (Recommended)"),
	LegacySweep UMETA(DisplayName = "Legacy Kinematic Sweep"),
	ChaosRigidBody UMETA(DisplayName = "Chaos Rigid Body")
};

/** M2.5 playable bird: input shell for radial gravity, collision and jump. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM25BirdCharacter : public AABTSM2BirdCharacter
{
	GENERATED_BODY()

public:
	AABTSM25BirdCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** Clears both implementations so a spawn teleport cannot retain stale velocity. */
	void ResetRadialMovementState();
	/** Ensures M4 writes follower steering before either movement implementation consumes it. */
	void AddPartyTickPrerequisite(AActor* PartyActor);
	/** Used by party control changes; unlike ResetRadialMovementState it preserves grounding. */
	void ClearControlHandoffState();
	/** M4 diagnostic handoff: clear both mover implementations completely, then rebuild only the selected mover's stable ground contact. */
	bool ResetForControlHandoffCacheExperiment();
	/** M4 input-routing diagnostic entry points. They intentionally use the same movement logic as the pawn bindings. */
	void HandleControllerRoutedMoveForward(float Value);
	void HandleControllerRoutedMoveRight(float Value);
	void HandleControllerRoutedJump();
	void BeginControlHandoffDiagnostics(float Seconds = 4.0f);
	bool IsControlHandoffDiagnosticsActive() const { return ControlDiagnosticRemainingSeconds > 0.0f; }
	void SetBirdIdentity(EABTSBirdId InBirdId, EABTSBirdSlingshotCapability InCapability, bool bInPlayerControlled);
	void SetPartyCollisionIsolation(bool bIsolateFromParty);
	/** Makes this bird ignore ABTSDeveloperObstacle while preserving terrain collision and applies a walking-only speed multiplier. */
	void SetDeveloperWalkEnabled(bool bEnabled, float SpeedMultiplier);
	void SetPartyControlled(bool bInPlayerControlled);
	void ApplyPartyMoveInput(const FVector& Direction, float Scale);
	void ApplyPartyJump();
	bool IsRadiallyGrounded() const;
	EABTSBirdId GetBirdId() const { return BirdId; }
	EABTSBirdSlingshotCapability GetSlingshotCapability() const { return SlingshotCapability; }
	UFUNCTION(BlueprintPure, Category = "ABTS|M4|Slingshot")
	bool CanUseSlingshotCapability(EABTSBirdSlingshotCapability RequiredCapability) const;
	bool IsPartyControlled() const { return bPartyControlled; }
	void EnterSlingshotPouch(const FVector& WorldLocation, const FQuat& WorldRotation);
	void LaunchFromSlingshot(const FVector& InitialVelocity, float FlightAirDragPerSecond);
	void BeginSlingshotReturn();
	void FinishSlingshotReturn();
	void SetSlingshotVelocity(const FVector& InVelocity);
	FVector GetSlingshotVelocity() const;
	/** Presentation-only radial frame used during the primary-to-satellite hand-off. */
	void SetSlingshotPresentationUp(
		const FVector& WorldUp,
		float DeltaSeconds,
		bool bLockFacingReversal = false);
	void ClearSlingshotPresentationUp();
	void NotifySlingshotPresentationImpact();
	/** Conservative radius consumed by every slingshot predictor and swept target test. */
	float GetSlingshotTrajectoryCollisionRadiusCM() const;
	bool IsSlingshotFlightActive() const;
	UABTSRadialForceMovementComponent* GetForceMovementComponent() const { return ForceMovement; }
	UABTSChaosBirdMovementComponent* GetChaosMovementComponent() const { return ChaosMovement; }
	UPrimitiveComponent* GetChaosPhysicsBody() const;
	/** Forces the isolated M7.1 floor to use Chaos plus constant planar gravity. */
	void EnablePlanarChaosMovement(const FVector& PlaneOrigin, const FVector& PlaneUp);

	UFUNCTION(BlueprintPure, Category = "ABTS|Movement")
	EABTSBirdMovementMode GetSelectedMovementMode() const { return MovementMode; }

	/** Requests a pose-only one-shot. The animation cannot change movement, collision or Gameplay state. */
	UFUNCTION(BlueprintCallable, Category = "ABTS|Bird|Presentation")
	void RequestBirdPresentationAction(EABTSBirdPresentationAction Action);

private:
	void ConfigureMovementMode();
	void ApplyMoveInput(const FVector& Direction, float Scale);
	void MoveWithRadialPhysicsForward(float Value);
	void MoveWithRadialPhysicsRight(float Value);
	void TurnWithRadialPhysics(float Value);
	void LookWithRadialPhysics(float Value);
	void BeginRadialJump();
	void ProcessMoveWithRadialPhysicsForward(float Value, bool bControllerRouted);
	void ProcessMoveWithRadialPhysicsRight(float Value, bool bControllerRouted);
	void ProcessRadialJump(bool bControllerRouted);
	bool IsControllerRoutedMovementInputExperimentEnabled() const;
	bool IsClearMotionBeforePlayerJumpExperimentEnabled() const;
	void ApplyClearMotionBeforeJumpExperiment();
	void UpdateChaosVisualFrame(float DeltaSeconds);
	void UpdateSlingshotPresentationFrame(float DeltaSeconds);
	void UpdateBirdAnimationPresentation(float DeltaSeconds);
	void ApplyCuteBirdMaterials();
	FVector GetPresentationVelocity() const;
	void ConfigureChaosPhysicsBody(bool bEnable);
	void SetLocomotionCollisionEnabled(ECollisionEnabled::Type CollisionEnabled);
	void LogControlDiagnosticSnapshot();

	/** Select on the C++ class defaults or a Blueprint child before starting PIE. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|Movement", meta = (AllowPrivateAccess = "true"))
	EABTSBirdMovementMode MovementMode = EABTSBirdMovementMode::ForceSuspension;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Bird", meta = (AllowPrivateAccess = "true"))
	EABTSBirdId BirdId = EABTSBirdId::Red;

	/** Reserved launch eligibility queried by the future slingshot system. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Slingshot", meta = (AllowPrivateAccess = "true"))
	EABTSBirdSlingshotCapability SlingshotCapability = EABTSBirdSlingshotCapability::Simple;

	bool bPartyControlled = true;
	float ControlDiagnosticRemainingSeconds = 0.0f;
	float ControlDiagnosticLogAccumulator = 0.0f;
	ECollisionEnabled::Type SavedCapsuleCollision = ECollisionEnabled::QueryAndPhysics;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|Movement|Legacy", meta = (AllowPrivateAccess = "true", NoEditInline))
	TObjectPtr<UABTSM25RadialMovementComponent> RadialMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|Movement|Force", meta = (AllowPrivateAccess = "true", NoEditInline))
	TObjectPtr<UABTSRadialForceMovementComponent> ForceMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|Movement|Force", meta = (AllowPrivateAccess = "true", NoEditInline))
	TObjectPtr<UABTSRadialSurfaceSuspensionComponent> SurfaceSuspension;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|Movement|Chaos", meta = (AllowPrivateAccess = "true", NoEditInline))
	TObjectPtr<UABTSChaosBirdMovementComponent> ChaosMovement;

	/** Dedicated StaticMesh sphere used as the Chaos root body; never used by the legacy movers. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|Movement|Chaos", meta = (AllowPrivateAccess = "true", NoEditInline))
	TObjectPtr<UStaticMeshComponent> ChaosPhysicsSphere;

	float SavedChaosCapsuleRadius = 42.0f;
	float SavedChaosCapsuleHalfHeight = 60.0f;
	ECollisionEnabled::Type SavedChaosBodyCollision = ECollisionEnabled::QueryAndPhysics;
	bool bPlanarChaosMode = false;
	bool bDeveloperWalkEnabled = false;
	bool bSlingshotPresentationUpActive = false;
	bool bSlingshotPresentationFrameInitialized = false;
	bool bSlingshotPresentationLockFacingReversal = false;
	FVector SlingshotPresentationUp = FVector::UpVector;
	FQuat SlingshotPresentationFrame = FQuat::Identity;
	FVector StableChaosPresentationForward = FVector::ZeroVector;
	bool bChaosVisualRotationInitialized = false;
	FQuat ChaosVisualRotation = FQuat::Identity;
	float SlingshotImpactFacingLockRemainingSeconds = 0.0f;
	/** Runtime-only helper avoids adding another serialized native Blueprint subobject. */
	UPROPERTY(Transient)
	TObjectPtr<UABTSBirdAnimationPresentationComponent> BirdAnimationPresentation;

	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> CuteBirdColorMaterials[4];
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> CuteBirdFaceMaterials[4];
};
