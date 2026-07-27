// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Party/ABTSBirdTypes.h"
#include "Slingshot/ABTSSlingshotTypes.h"
#include "ABTSM6Types.generated.h"

UENUM(BlueprintType)
enum class EABTSM6LaunchState : uint8
{
	Inactive = 0,
	Ready = 1,
	Pulling = 2,
	Flying = 3,
	Returning = 4,
	Settling = 5
};

UENUM(BlueprintType)
enum class EABTSM6ImpactMaterial : uint8
{
	Terrain,
	Wood,
	Stone,
	Iron,
	Glass,
	Building
};

/** Immutable copy of one M6 aim prediction, consumed by M10 without re-integrating physics in the HUD. */
USTRUCT(BlueprintType)
struct FABTSM6TrajectoryPreview
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	EABTSSlingshotTier SlingshotTier = EABTSSlingshotTier::Simple;

	UPROPERTY(BlueprintReadOnly)
	TArray<FVector> WorldPoints;

	UPROPERTY(BlueprintReadOnly)
	FVector InitialWorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector InitialWorldVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	bool bHasPrimarySurfaceLanding = false;

	/** Presentation-surface point, not the predicted bird-centre contact point. */
	UPROPERTY(BlueprintReadOnly)
	FVector PrimarySurfaceLandingWorld = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector PrimarySurfaceLandingVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	int32 LandingCellId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly)
	float LandingTimeSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float PredictedPathLengthCM = 0.0f;
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

	/** Chaos dynamic friction for promoted trees/rocks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics", meta = (ClampMin = "0.0"))
	float DynamicFriction = 0.62f;

	/** Chaos static friction for a stopped or frozen promoted object. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics", meta = (ClampMin = "0.0"))
	float StaticFriction = 0.78f;

	/** Target-side restitution used by Chaos after a HISM instance becomes a rigid body. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ObjectRestitution = 0.08f;

	/** Density in g/cm3, consumed by the temporary Chaos proxy mass calculation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics", meta = (ClampMin = "0.01"))
	float DensityGPerCubicCM = 0.65f;

	/** Damage gained by a centered impact at this material's break-speed threshold. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0"))
	float DamageAtBreakSpeed = 100.0f;

	/** Total accumulated damage required to shatter the target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "1.0"))
	float BreakDamage = 100.0f;

	/** Fraction of the arriving velocity used to push a newly promoted rigid body. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float PushVelocityTransfer = 0.72f;
};

