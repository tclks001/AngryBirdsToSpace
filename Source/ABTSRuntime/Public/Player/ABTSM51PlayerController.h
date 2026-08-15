// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Player/ABTSM5PlayerController.h"
#include "ABTSM51PlayerController.generated.h"

class AABTSM51SlingshotDirtHole;
class AABTSM51SlingshotStake;
class AABTSM51SlingshotCord;
class AABTSM51WorldSystem;

/** M5.1 controller routes left click between HUD, world actors and ground placement. */
UCLASS()
class ABTSRUNTIME_API AABTSM51PlayerController : public AABTSM5PlayerController
{
	GENERATED_BODY()

public:
	virtual void SetupInputComponent() override;
	void InteractWithDirtHole(AABTSM51SlingshotDirtHole* Hole);
	void InteractWithStake(AABTSM51SlingshotStake* Stake);
	virtual void InteractWithSlingshotCord(AABTSM51SlingshotCord* Cord) {}

protected:
	virtual void PrimaryWorldInteract();
	/** Central barrier shared by M5.1/M6/M11 before any left-click world action. */
	bool ShouldConsumePrimaryPointerForHUD() const;

private:
	AABTSM51WorldSystem* FindWorldSystem();

	TWeakObjectPtr<AABTSM51WorldSystem> WorldSystem;
};
