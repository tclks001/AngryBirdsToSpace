// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ABTSM1HUD.generated.h"

/** Asset-free M1 HUD. It is intentionally replaced by a dedicated UI module in a later milestone. */
UCLASS()
class ABTSRUNTIME_API AABTSM1HUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
