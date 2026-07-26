// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Player/ABTSM4PlayerController.h"
#include "ABTSM5PlayerController.generated.h"

class AABTSCraftingStation;
class AABTSCraftingSystem;

/** M5 controller owns the modal crafting state while preserving the M4 camera. */
UCLASS()
class ABTSRUNTIME_API AABTSM5PlayerController : public AABTSM4PlayerController
{
	GENERATED_BODY()

public:
	virtual void SetupInputComponent() override;
	void OpenCraftingInterface();
	void CloseCraftingInterface();
	void ToggleCraftingInterface();
	void OpenCraftingFromStation(const AABTSCraftingStation* Station);
	bool IsCraftingInterfaceOpen() const { return bCraftingInterfaceOpen; }
	AABTSCraftingSystem* FindCraftingSystem();

private:
	void ScrollCraftingInventory(float Value);

	bool bCraftingInterfaceOpen = false;
	TWeakObjectPtr<AABTSCraftingSystem> CraftingSystem;
};

