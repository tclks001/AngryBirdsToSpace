// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ABTSM1GameMode.generated.h"

/** M1's isolated gameplay entry point. Future gameplay systems are added as separate modules. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM1GameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AABTSM1GameMode();

	virtual void BeginPlay() override;
};
