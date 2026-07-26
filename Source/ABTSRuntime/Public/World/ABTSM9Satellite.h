// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Planet/ABTSM2Planet.h"
#include "ABTSM9Satellite.generated.h"

class AABTSM3Planet;
class UMaterialInterface;

/** A small, non-SDF procedural sphere whose placement is derived from the final Task's CellTopo seed. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM9Satellite : public AABTSM2Planet
{
	GENERATED_BODY()

public:
	AABTSM9Satellite();
	virtual void BeginPlay() override;

	void ConfigureFromPrimaryPlanet(
		AABTSM3Planet& PrimaryPlanet,
		int32 InAnchorCellId,
		float InRadiusCM,
		float InCenterClearanceCM,
		float InSurfaceGravityAccelerationCMPerSec2);

	/** Inverse-square acceleration toward the satellite centre, with the configured value measured at its surface. */
	FVector GetGravityAccelerationAt(const FVector& WorldLocation) const;
	int32 GetAnchorCellId() const { return AnchorCellId; }
	float GetSurfaceGravityAccelerationCMPerSec2() const { return SurfaceGravityAccelerationCMPerSec2; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M9|Gravity")
	bool bGravityEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M9|Gravity", meta = (ClampMin = "0.0", UIMax = "3000.0"))
	float SurfaceGravityAccelerationCMPerSec2 = 245.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M9")
	int32 AnchorCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M9")
	float CenterClearanceCM = 1250.0f;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> GrayMaterial;
};
