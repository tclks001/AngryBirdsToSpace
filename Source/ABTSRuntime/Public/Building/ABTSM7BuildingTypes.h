// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ABTSM7BuildingTypes.generated.h"

UENUM(BlueprintType)
enum class EABTSM7BuildingMaterial : uint8
{
	Wood,
	Stone,
	Iron,
	Glass,
	/** Collectible emissive reward brick. Appended to preserve existing serialized values. */
	Crystal
};

UENUM(BlueprintType)
enum class EABTSM7ModuleKind : uint8
{
	Brick,
	Rope,
	IronChain,
	ExplosiveBarrel,
	SpringPiston
};

USTRUCT(BlueprintType)
struct FABTSM7MaterialProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EABTSM7BuildingMaterial Material = EABTSM7BuildingMaterial::Wood;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float KnockSpeedCMPerSec = 520.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float BreakSpeedCMPerSec = 1050.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics", meta = (ClampMin = "0.0"))
	float DynamicFriction = 0.62f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics", meta = (ClampMin = "0.0"))
	float StaticFriction = 0.78f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Restitution = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics", meta = (ClampMin = "0.01"))
	float DensityGPerCubicCM = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0"))
	float DamageAtBreakSpeed = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "1.0"))
	float BreakDamage = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float PushVelocityTransfer = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FLinearColor FallbackColor = FLinearColor(0.42f, 0.18f, 0.05f, 1.0f);
};

USTRUCT(BlueprintType)
struct FABTSM7BrickSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EABTSM7BuildingMaterial Material = EABTSM7BuildingMaterial::Wood;

	/** Final world-space dimensions. Engine Cube is only the shared source mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1.0"))
	FVector DimensionsCM = FVector(200.0f, 80.0f, 60.0f);
};

USTRUCT(BlueprintType)
struct FABTSM7SuspensionSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EABTSM7ModuleKind Kind = EABTSM7ModuleKind::Rope;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1.0"))
	float LengthCM = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1.0"))
	float RadiusCM = 10.0f;
};

USTRUCT(BlueprintType)
struct FABTSM7DeviceSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EABTSM7ModuleKind Kind = EABTSM7ModuleKind::ExplosiveBarrel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1.0"))
	float LengthCM = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1.0"))
	float DiameterCM = 90.0f;
};
