// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Game/ABTSM8GameMode.h"
#include "PCG/ABTSM3TaskGraphTypes.h"
#include "ABTSM9GameMode.generated.h"

class AABTSM9Satellite;

/** M9 entry spawns one final-task-anchored satellite and exposes its size/clearance/gravity ratios. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM9GameMode : public AABTSM8GameMode
{
	GENERATED_BODY()

public:
	AABTSM9GameMode();

protected:
	virtual void OnInitialPlayerPlaced(ACharacter& Character, const FTransform& SpawnTransform, int32 SpawnCellId) override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M9")
	TSubclassOf<AABTSM9Satellite> SatelliteClass;

	/** The default LaunchSite is the final main-path Task in the current TaskGraph. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M9|Placement")
	EABTSM3TaskType FinalAnchorTaskType = EABTSM3TaskType::LaunchSite;

	UPROPERTY(EditAnywhere, Category = "ABTS|M9|Placement", meta = (ClampMin = "0.02", ClampMax = "0.5"))
	float SatelliteRadiusPrimaryRatio = 0.125f;

	/** Satellite centre's clearance above the primary terrain at the final Task seed. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M9|Placement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SatelliteCenterClearancePrimaryRadiusRatio = 0.125f;

	/** Satellite acceleration at its own surface relative to primary surface gravity (980cm/s²). */
	UPROPERTY(EditAnywhere, Category = "ABTS|M9|Gravity", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float SatelliteSurfaceGravityPrimaryRatio = 0.25f;

	/** Birds ignore HISM, buildings and river barriers, while the continuous terrain remains solid. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M9|Debug")
	bool bEnableDeveloperWalk = false;

	UPROPERTY(EditAnywhere, Category = "ABTS|M9|Debug", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float DeveloperWalkSpeedMultiplier = 4.0f;

	/** Allows a held slingshot stake to be placed on any unoccupied CellTopo cell without a DirtHole. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M9|Debug")
	bool bAllowDeveloperAnyCellSlingshotStakePlacement = false;
};
