// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Calibration/ABTSSlingshotSatelliteCalibrationTypes.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ABTSSlingshotSatelliteCalibrationRig.generated.h"

class AABTSCalibrationTargetProxy;
class AABTSM3Planet;
class AABTSM6SlingshotSystem;
class AABTSM9Satellite;

/** Owns visible proxies, offline sweep evidence and real-launch swept hit telemetry. */
UCLASS(NotBlueprintable)
class ABTSRUNTIME_API AABTSSlingshotSatelliteCalibrationRig : public AActor
{
	GENERATED_BODY()

public:
	AABTSSlingshotSatelliteCalibrationRig();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void Configure(
		AABTSM3Planet& InPrimaryPlanet,
		AABTSM9Satellite& InSatellite,
		AABTSM6SlingshotSystem& InSlingshotSystem,
		const FVector& InCalibrationOriginWorld,
		const FVector& InStartUnitDirection,
		const FVector& InCalibrationForward,
		const FABTSSatellitePracticePreset& InPreset);

	bool IsReady() const { return bReady; }
	int32 GetTargetProxyCount() const { return TargetProxies.Num(); }
	const TArray<FABTSM6ReachEnvelope>& GetReachEnvelopes() const { return ReachEnvelopes; }
	const FABTSSatellitePracticePreset& GetPreset() const { return Preset; }
	const FABTSCalibrationGravitySnapshot& GetGravitySnapshot() const { return GravitySnapshot; }
	const FABTSCalibrationSweepSummary& GetSweepSummary() const { return SweepSummary; }
	uint64 GetLaunchProfileHash() const { return LaunchProfileHash; }
	uint64 GetGravitySnapshotHash() const { return GravitySnapshotHash; }
	uint64 GetSatellitePracticePresetHash() const { return SatellitePracticePresetHash; }
	bool HasLastLaunchTelemetry() const { return bHasLastLaunchTelemetry; }
	const FABTSM6LaunchCalibrationTelemetry& GetLastLaunchTelemetry() const { return LastLaunchTelemetry; }
	bool IsSatelliteGravityEnabled() const;

private:
	bool SpawnReachTargets();
	bool SpawnSatelliteTarget();
	AABTSCalibrationTargetProxy* SpawnTarget(
		FName TargetId,
		const FVector& WorldLocation,
		float RadiusCM,
		const FLinearColor& Color);
	void RunSweep();
	void UpdateActualLaunchTargetSweep();
	void HandleLaunchRecorded(const FABTSM6LaunchCalibrationTelemetry& Telemetry);

	TWeakObjectPtr<AABTSM3Planet> PrimaryPlanet;
	TWeakObjectPtr<AABTSM9Satellite> Satellite;
	TWeakObjectPtr<AABTSM6SlingshotSystem> SlingshotSystem;
	FVector CalibrationOriginWorld = FVector::ZeroVector;
	FVector StartUnitDirection = FVector::UpVector;
	FVector CalibrationForward = FVector::ForwardVector;
	FABTSSatellitePracticePreset Preset;
	FABTSM6LaunchProfileCatalog LaunchProfileCatalog;
	FABTSM6CalibrationLaunchFrame ReinforcedLaunchFrame;
	FABTSCalibrationGravitySnapshot GravitySnapshot;
	FABTSCalibrationSweepSummary SweepSummary;
	TArray<FABTSM6ReachEnvelope> ReachEnvelopes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AABTSCalibrationTargetProxy>> TargetProxies;

	UPROPERTY(Transient)
	TObjectPtr<AABTSCalibrationTargetProxy> SatelliteTargetProxy;

	uint64 LaunchProfileHash = 0;
	uint64 GravitySnapshotHash = 0;
	uint64 SatellitePracticePresetHash = 0;
	FABTSM6LaunchCalibrationTelemetry LastLaunchTelemetry;
	FVector PreviousActiveBirdLocation = FVector::ZeroVector;
	int32 ActiveSweepSequence = 0;
	bool bHasPreviousActiveBirdLocation = false;
	bool bHasLastLaunchTelemetry = false;
	bool bReady = false;
};
