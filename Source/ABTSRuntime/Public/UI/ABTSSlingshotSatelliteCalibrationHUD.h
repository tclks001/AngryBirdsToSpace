// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/ABTSM10ScoutMapHUD.h"
#include "ABTSSlingshotSatelliteCalibrationHUD.generated.h"

class AABTSSlingshotSatelliteCalibrationRig;

/** M10.1 orbital HUD plus compact calibration envelopes, hashes and launch telemetry. */
UCLASS(NotBlueprintable)
class ABTSRUNTIME_API AABTSSlingshotSatelliteCalibrationHUD
	: public AABTSM10ScoutMapHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	AABTSSlingshotSatelliteCalibrationRig* FindCalibrationRig();

	TWeakObjectPtr<AABTSSlingshotSatelliteCalibrationRig> CalibrationRig;
};
