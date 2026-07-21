// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ABTSM1PlayerController.generated.h"

/** Owns M1's local-player startup policy; gameplay input remains with the pawn. */
UCLASS()
class ABTSRUNTIME_API AABTSM1PlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
};
