// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Party/ABTSBirdTypes.h"
#include "ABTSM6Types.generated.h"

UENUM(BlueprintType)
enum class EABTSM6LaunchState : uint8
{
	Inactive,
	Ready,
	Pulling,
	Flying,
	Returning
};

UENUM(BlueprintType)
enum class EABTSM6ImpactMaterial : uint8
{
	Terrain,
	Wood,
	Stone,
	Building
};

USTRUCT(BlueprintType)
struct FABTSM6BirdImpactProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EABTSBirdId BirdId = EABTSBirdId::Red;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float KnockSpeedCMPerSec = 520.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float BreakSpeedCMPerSec = 1050.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RetainedTangentSpeed = 0.62f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Restitution = 0.12f;
};

USTRUCT(BlueprintType)
struct FABTSM6MaterialImpactProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EABTSM6ImpactMaterial Material = EABTSM6ImpactMaterial::Wood;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float KnockThresholdMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float BreakThresholdMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BirdSpeedRetention = 0.62f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BirdRestitution = 0.10f;
};

