// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Player/ABTSM1PlayerController.h"
#include "ABTSM4PlayerController.generated.h"

class AABTSM4PartyCamera;

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
	void SetCursorInteractionMode(bool bEnableCursor);
	void EnsurePartyCameraView();

	UPROPERTY(Transient)
	TObjectPtr<AABTSM4PartyCamera> PartyCamera;

	bool bOrbitInputHeld = false;
	bool bSavedCursorPositionValid = false;
	float SavedCursorX = 0.0f;
	float SavedCursorY = 0.0f;
	bool bGameplayInputBlocked = false;
};
