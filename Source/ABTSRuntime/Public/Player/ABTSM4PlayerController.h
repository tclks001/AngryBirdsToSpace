// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Player/ABTSM1PlayerController.h"
#include "ABTSM4PlayerController.generated.h"

class AABTSM4PartyCamera;
class AABTSBirdParty;

/** M4 local controller: persistent clickable party HUD and Tab cycling. */
UCLASS()
class ABTSRUNTIME_API AABTSM4PlayerController : public AABTSM1PlayerController
{
	GENERATED_BODY()

public:
	AABTSM4PlayerController();

	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;
	bool GetCameraRelativeMovementBasis(const FVector& WorldLocation, FVector& OutForward, FVector& OutRight) const;
	/** True only while the level's M4 experiment bypasses the possessed pawn input stack for WASD and Space. */
	bool IsControllerRoutedMovementInputExperimentEnabled() const;
	/** True while the jump-time drift intervention is enabled on the level's party settings. */
	bool IsClearMotionBeforePlayerJumpExperimentEnabled() const;
	void RestorePartyCameraView() { EnsurePartyCameraView(); }
	/** Shared release-cinematic gate; preserves the existing gameplay input latch. */
	void SetCinematicInputBlocked(bool bBlocked) { SetGameplayInputBlocked(bBlocked); }
	bool IsCinematicInputBlocked() const { return IsGameplayInputBlocked(); }
	/** Allows a paused release cinematic to keep camera-manager evaluation alive. */
	void SetCinematicFullTickWhenPaused(bool bEnabled)
	{
		bShouldPerformFullTickWhenPaused = bEnabled;
	}
	bool IsCinematicFullTickWhenPaused() const
	{
		return bShouldPerformFullTickWhenPaused;
	}

protected:
	virtual void BeginPlay() override;
	void SetGameplayInputBlocked(bool bBlocked);
	bool IsGameplayInputBlocked() const { return bGameplayInputBlocked; }

private:
	void CycleBird();
	void BeginOrbitInput();
	void EndOrbitInput();
	void ApplyOrbitYaw(float Value);
	void ApplyOrbitPitch(float Value);
	void ApplyCameraZoom(float Value);
	void RecenterCamera();
	void RouteMoveForwardToControlledBird(float Value);
	void RouteMoveRightToControlledBird(float Value);
	void RouteJumpToControlledBird();
	void SetCursorInteractionMode(bool bEnableCursor);
	void EnsurePartyCameraView();
	AABTSBirdParty* FindParty() const;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM4PartyCamera> PartyCamera;

	bool bOrbitInputHeld = false;
	bool bSavedCursorPositionValid = false;
	float SavedCursorX = 0.0f;
	float SavedCursorY = 0.0f;
	bool bGameplayInputBlocked = false;
};
