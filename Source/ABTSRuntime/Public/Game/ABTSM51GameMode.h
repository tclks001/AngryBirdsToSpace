// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Game/ABTSM5GameMode.h"
#include "ABTSM51GameMode.generated.h"

class AABTSM51WorldSystem;

/** M5.1 entry adds CellTopo-driven pickups, placement and slingshot assembly. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM51GameMode : public AABTSM5GameMode
{
	GENERATED_BODY()

public:
	AABTSM51GameMode();

protected:
	virtual void OnInitialPlayerPlaced(ACharacter& Character, const FTransform& SpawnTransform, int32 SpawnCellId) override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M5.1")
	TSubclassOf<AABTSM51WorldSystem> WorldSystemClass;

	/**
	 * Preview/Test only. Production stays on the compatibility or future
	 * AcceptedMonthly path unless an authored test GameMode enables this flag.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M5.1|Ordinary Slots|Preview Test")
	bool bEnableOrdinarySlingshotSlotPreview = false;

	/** Exact M3 SourceRouteCandidateId; array order is never an authority. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M5.1|Ordinary Slots|Preview Test",
		meta = (EditCondition = "bEnableOrdinarySlingshotSlotPreview"))
	int32 OrdinarySlingshotSlotPreviewCandidateId = INDEX_NONE;
};

