// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ABTSM25GameMode.generated.h"

/** M2.5 entry point: keeps M2 planet ownership while replacing the pawn with radial physics. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM25GameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AABTSM25GameMode();

	virtual void BeginPlay() override;
};
