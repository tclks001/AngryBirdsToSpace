// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Game/ABTSM9GameMode.h"
#include "World/ABTSM10ScoutMapTypes.h"
#include "ABTSM10GameMode.generated.h"

class AABTSM10ScoutMapSystem;

/** M10 entry adds Twig scouting and the fixed spherical minimap reveal. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM10GameMode : public AABTSM9GameMode
{
	GENERATED_BODY()

public:
	AABTSM10GameMode();

protected:
	virtual void OnInitialPlayerPlaced(ACharacter& Character, const FTransform& SpawnTransform, int32 SpawnCellId) override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M10")
	TSubclassOf<AABTSM10ScoutMapSystem> ScoutMapSystemClass;

	/** Texture, pixel size, refresh cadence and spherical coverage authoring surface. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M10|Scout Map")
	FABTSM10ScoutMapSettings ScoutMapSettings;
};

