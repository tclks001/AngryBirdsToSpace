// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ABTSM2GameMode.generated.h"

/** Dedicated M2 entry. It preserves M1's pawn while keeping planet ownership outside the character. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM2GameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AABTSM2GameMode();
	virtual void BeginPlay() override;
};
