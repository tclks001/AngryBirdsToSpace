// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Player/ABTSM51PlayerController.h"
#include "ABTSM6PlayerController.generated.h"

class AABTSM51SlingshotCord;
class AABTSM6SlingshotSystem;

/** M6 mouse pull/release and manual black-bird detonation router. */
UCLASS()
class ABTSRUNTIME_API AABTSM6PlayerController : public AABTSM51PlayerController
{
	GENERATED_BODY()

public:
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void InteractWithSlingshotCord(AABTSM51SlingshotCord* Cord) override;
	void SetLaunchModeInputBlocked(bool bBlocked) { SetGameplayInputBlocked(bBlocked); }

private:
	virtual void PrimaryWorldInteract() override;
	void M6PrimaryReleased();
	void M6AdjustPower(float Value);
	AABTSM6SlingshotSystem* FindSlingshotSystem();

	TWeakObjectPtr<AABTSM6SlingshotSystem> SlingshotSystem;
};
