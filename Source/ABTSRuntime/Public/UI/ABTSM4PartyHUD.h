// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ABTSM4PartyHUD.generated.h"

class AABTSBirdParty;

/** Asset-optional fixed-order four-bird portrait HUD. */
UCLASS()
class ABTSRUNTIME_API AABTSM4PartyHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
	virtual void NotifyHitBoxClick(FName BoxName) override;

private:
	AABTSBirdParty* FindParty();
	FName MakeBirdHitBoxName(int32 BirdIndex) const;

	TWeakObjectPtr<AABTSBirdParty> Party;
};
