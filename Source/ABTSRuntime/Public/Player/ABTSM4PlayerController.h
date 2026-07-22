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

protected:
	virtual void BeginPlay() override;

private:
	void CycleBird();
	void EnsurePartyCameraView();

	UPROPERTY(Transient)
	TObjectPtr<AABTSM4PartyCamera> PartyCamera;
};
