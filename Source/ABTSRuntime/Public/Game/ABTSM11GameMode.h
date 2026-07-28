// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Game/ABTSM10GameMode.h"
#include "ABTSM11GameMode.generated.h"

class AABTSM11FinaleSystem;
class AABTSM11FinaleInteractionSystem;

/**
 * M11 entry point. M10 remains intact; this subclass adds the M11-B layout
 * authority and M11-C interaction after the accepted World places its player.
 */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM11GameMode : public AABTSM10GameMode
{
	GENERATED_BODY()

public:
	AABTSM11GameMode();

	AABTSM11FinaleSystem* GetFinaleSystem() const
	{
		return FinaleSystem;
	}
	AABTSM11FinaleInteractionSystem* GetFinaleInteractionSystem() const
	{
		return FinaleInteractionSystem;
	}

protected:
	virtual void OnInitialPlayerPlaced(
		ACharacter& Character,
		const FTransform& SpawnTransform,
		int32 SpawnCellId) override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C")
	TSubclassOf<AABTSM11FinaleInteractionSystem>
		FinaleInteractionSystemClass;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM11FinaleSystem> FinaleSystem;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM11FinaleInteractionSystem>
		FinaleInteractionSystem;
};
