// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Game/ABTSM10GameMode.h"
#include "ABTSM11GameMode.generated.h"

class AABTSM11FinaleSystem;

/**
 * M11 entry point. M10 remains intact; this subclass adds one M11-B finale
 * runtime system after M3 has placed the initial player in the accepted world.
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

protected:
	virtual void OnInitialPlayerPlaced(
		ACharacter& Character,
		const FTransform& SpawnTransform,
		int32 SpawnCellId) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<AABTSM11FinaleSystem> FinaleSystem;
};
