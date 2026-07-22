// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Party/ABTSBirdTypes.h"
#include "ABTSBirdPartySettings.generated.h"

/** Optional level-authored presentation and tuning for the four-bird party. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSBirdPartySettings : public AActor
{
	GENERATED_BODY()

public:
	AABTSBirdPartySettings();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Birds", meta = (TitleProperty = "DisplayName"))
	TArray<FABTSBirdPresentationConfig> Birds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Formation", meta = (ClampMin = "80.0", UIMax = "400.0"))
	float QueueSpacingCM = 190.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Formation", meta = (ClampMin = "100.0", UIMax = "600.0"))
	float FollowStartDistanceCM = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Formation", meta = (ClampMin = "40.0", UIMax = "400.0"))
	float FollowStopDistanceCM = 145.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Formation", meta = (ClampMin = "20.0", UIMax = "250.0"))
	float SeparationDistanceCM = 95.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Formation", meta = (ClampMin = "300.0", UIMax = "3000.0"))
	float SevereDetachDistanceCM = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Path", meta = (ClampMin = "5.0", UIMax = "100.0"))
	float PathSampleSpacingCM = 32.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Jump", meta = (ClampMin = "10.0", UIMax = "200.0"))
	float JumpHeightTriggerCM = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Jump", meta = (ClampMin = "20.0", UIMax = "300.0"))
	float AirFollowHeightThresholdCM = 95.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Jump", meta = (ClampMin = "30.0", UIMax = "300.0"))
	float JumpTriggerDistanceCM = 105.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|HUD", meta = (ClampMin = "40.0", UIMax = "160.0"))
	float PortraitDiameterPx = 72.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|HUD", meta = (ClampMin = "0.0", UIMax = "80.0"))
	float PortraitGapPx = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|HUD", meta = (ClampMin = "0.0", UIMax = "200.0"))
	float RightMarginPx = 42.0f;

	/** Legacy fixed-camera height; retained for existing map serialization. Orbit mode uses distance and elevation below. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Camera", meta = (ClampMin = "200.0", UIMax = "1600.0"))
	float CameraHeightCM = 720.0f;

	/** Legacy fixed-camera back distance; retained for existing map serialization. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Camera", meta = (ClampMin = "0.0", UIMax = "1000.0"))
	float CameraBackDistanceCM = 320.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Camera", meta = (ClampMin = "-100.0", UIMax = "300.0"))
	float CameraLookAtHeightCM = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Camera", meta = (ClampMin = "0.1", UIMax = "20.0"))
	float CameraPositionLagSpeed = 4.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Camera", meta = (ClampMin = "0.1", UIMax = "30.0"))
	float CameraRotationLagSpeed = 7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Camera", meta = (ClampMin = "20.0", ClampMax = "100.0"))
	float CameraFieldOfViewDegrees = 52.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Camera|Orbit", meta = (ClampMin = "200.0", UIMax = "2000.0"))
	float OrbitDistanceCM = 850.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Camera|Orbit", meta = (ClampMin = "100.0", UIMax = "1500.0"))
	float MinOrbitDistanceCM = 550.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Camera|Orbit", meta = (ClampMin = "300.0", UIMax = "3000.0"))
	float MaxOrbitDistanceCM = 1300.0f;

	/** Signed degrees above the local tangent plane; +85 is top-down and -85 is bottom-up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Camera|Orbit", meta = (ClampMin = "-85.0", ClampMax = "85.0"))
	float DefaultElevationDegrees = 60.0f;

	/** Legacy serialized bound retained for old map compatibility; runtime pitch is fixed to [-85,+85]. */
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Orbit pitch now always uses the fixed -85 to +85 degree range."))
	float MinElevationDegrees = -85.0f;

	/** Legacy serialized bound retained for old map compatibility; runtime pitch is fixed to [-85,+85]. */
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Orbit pitch now always uses the fixed -85 to +85 degree range."))
	float MaxElevationDegrees = 85.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Camera|Input", meta = (ClampMin = "0.01", UIMax = "5.0"))
	float OrbitYawDegreesPerInput = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Camera|Input", meta = (ClampMin = "0.01", UIMax = "5.0"))
	float OrbitPitchDegreesPerInput = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Camera|Input", meta = (ClampMin = "10.0", UIMax = "300.0"))
	float OrbitZoomStepCM = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Camera|Switch", meta = (ClampMin = "0.0", UIMax = "2.0"))
	float CameraSwitchBlendSeconds = 0.48f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Camera|Follow", meta = (ClampMin = "0.1", UIMax = "30.0"))
	float OrbitPivotFollowSpeed = 7.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Camera|Follow", meta = (ClampMin = "0.1", UIMax = "30.0"))
	float OrbitRotationFollowSpeed = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Camera|Follow", meta = (ClampMin = "0.0", UIMax = "100.0"))
	float CameraPivotDeadZoneCM = 22.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Camera|Obstruction", meta = (ClampMin = "1.0", UIMax = "80.0"))
	float CameraProbeRadiusCM = 24.0f;

	/** Legacy hard minimum retained for map compatibility; collision-safe contraction may approach the pivot. */
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Collision safety now overrides the old minimum obstructed distance."))
	float CameraMinimumObstructedDistanceCM = 280.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Camera|Obstruction", meta = (ClampMin = "0.1", UIMax = "50.0"))
	float CameraObstructionPullInSpeed = 22.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Camera|Obstruction", meta = (ClampMin = "0.1", UIMax = "20.0"))
	float CameraObstructionRestoreSpeed = 5.0f;
};
