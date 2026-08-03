// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Player/ABTSM6PlayerController.h"
#include "ABTSM11PlayerController.generated.h"

class AABTSM11FinaleInteractionSystem;
class AABTSM51SlingshotCord;
class AABTSM6SlingshotSystem;

/** M11-only Space-slingshot input router; ordinary M6 launch stays intact. */
UCLASS()
class ABTSRUNTIME_API AABTSM11PlayerController
	: public AABTSM6PlayerController
{
	GENERATED_BODY()

public:
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void FlushPressedKeys() override;
	virtual void InteractWithSlingshotCord(
		AABTSM51SlingshotCord* Cord) override;

private:
	virtual void PrimaryWorldInteract() override;
	void M11PrimaryReleased();
	void M11PrimaryDoubleClicked();
	void M11Power(float Value);
	void M11Cancel();
	void M11OrbitPressed();
	void M11OrbitReleased();
	void SetM11FinaleInputMode(bool bActive);
	void ApplyM11PointerMode(bool bFinaleActive);
	AABTSM11FinaleInteractionSystem* FindM11Interaction() const;
	AABTSM6SlingshotSystem* FindOrdinarySlingshotSystem();

	TWeakObjectPtr<AABTSM6SlingshotSystem> OrdinarySlingshotSystem;
	bool bWasM11FinaleActive = false;
	bool bM11SavedPointerEventFlags = false;
	bool bSavedClickEvents = true;
	bool bSavedMouseOverEvents = true;
	bool bM11OrbitCursorSaved = false;
	bool bRestoreM11CursorAfterOrbitRelease = false;
	float M11OrbitCursorX = 0.0f;
	float M11OrbitCursorY = 0.0f;
};
