// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Calibration/ABTSSlingshotSatelliteCalibrationTypes.h"
#include "CoreMinimal.h"
#include "Game/ABTSM6GameMode.h"
#include "World/ABTSM10ScoutMapTypes.h"
#include "ABTSSlingshotSatelliteCalibrationGameMode.generated.h"

class AABTSM10ScoutMapSystem;
class AABTSM9Satellite;
class AABTSSlingshotSatelliteCalibrationRig;

/**
 * Isolated native calibration entry. It deliberately bypasses the M7/M8/M9
 * GameMode chain and composes only M6 launch, one practice satellite and M10.1 UI.
 */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSSlingshotSatelliteCalibrationGameMode
	: public AABTSM6GameMode
{
	GENERATED_BODY()

public:
	AABTSSlingshotSatelliteCalibrationGameMode();

protected:
	virtual void OnInitialPlayerPlaced(
		ACharacter& Character,
		const FTransform& SpawnTransform,
		int32 SpawnCellId) override;

private:
	void TryCompleteCalibrationSmoke();
	void TryStartSatelliteCameraCapture();
	void FinishCalibrationSmoke(bool bPassed, const FString& Reason);

	UPROPERTY(EditAnywhere, Category = "ABTS|Calibration")
	FABTSM6LaunchProfileCatalog LaunchProfileCatalog;

	UPROPERTY(EditAnywhere, Category = "ABTS|Calibration")
	FABTSSatellitePracticePreset PracticePreset;

	UPROPERTY(EditAnywhere, Category = "ABTS|Calibration")
	FABTSM10ScoutMapSettings ScoutMapSettings;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|Calibration|Classes")
	TSubclassOf<AABTSM9Satellite> SatelliteClass;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|Calibration|Classes")
	TSubclassOf<AABTSSlingshotSatelliteCalibrationRig> CalibrationRigClass;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|Calibration|Classes")
	TSubclassOf<AABTSM10ScoutMapSystem> ScoutMapSystemClass;

	TWeakObjectPtr<AABTSSlingshotSatelliteCalibrationRig> RuntimeCalibrationRig;
	TWeakObjectPtr<AABTSM10ScoutMapSystem> RuntimeCalibrationScoutMapSystem;
	int32 RuntimeCalibrationSlingshotCount = 0;
	double CalibrationSmokeStartSeconds = 0.0;
	bool bCalibrationSmokeRequested = false;
	bool bCalibrationSmokeFinished = false;
	bool bSatelliteCameraCaptureRequested = false;
};
