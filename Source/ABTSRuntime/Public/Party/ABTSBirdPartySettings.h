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

	/** Radial height above the controlled bird. Dominant over the backward offset to keep a top-down view. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Camera", meta = (ClampMin = "200.0", UIMax = "1600.0"))
	float CameraHeightCM = 720.0f;

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
};
