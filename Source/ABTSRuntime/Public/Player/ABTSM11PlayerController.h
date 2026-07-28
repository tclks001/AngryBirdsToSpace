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
	virtual void InteractWithSlingshotCord(
		AABTSM51SlingshotCord* Cord) override;

private:
	virtual void PrimaryWorldInteract() override;
	void M11PrimaryReleased();
	void M11Yaw(float Value);
	void M11Pitch(float Value);
	void M11Power(float Value);
	void M11Cancel();
	AABTSM11FinaleInteractionSystem* FindM11Interaction() const;
	AABTSM6SlingshotSystem* FindOrdinarySlingshotSystem();

	TWeakObjectPtr<AABTSM6SlingshotSystem> OrdinarySlingshotSystem;
	bool bWasM11FinaleActive = false;
};
