// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "ABTSM110FinaleTypes.generated.h"

/**
 * Deterministic frame exported by M3 for the M11 finale.
 *
 * Local X is the canonical launch-forward direction, local Y runs from the
 * left space slot to the right space slot, and local Z is the primary
 * planet's radial up. M11 layout presets are authored in this local frame;
 * they must never store absolute generated-world coordinates.
 */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM110FinaleLocalFrame
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M11.0|Finale Frame")
	int32 LayoutVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M11.0|Finale Frame")
	int32 LaunchTaskId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M11.0|Finale Frame")
	int32 AnchorCellId = INDEX_NONE;

	/** Stable identity of the one certified pair of finale slots. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M11.0|Finale Frame")
	int32 SlotPairId = INDEX_NONE;

	/** Origin at the midpoint of the two space slots. X=Forward, Y=Right, Z=Up. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M11.0|Finale Frame")
	FTransform WorldTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M11.0|Finale Frame")
	FVector LeftSlotWorldLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M11.0|Finale Frame")
	FVector RightSlotWorldLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M11.0|Finale Frame")
	bool bValid = false;

	FVector GetOrigin() const { return WorldTransform.GetLocation(); }
	FVector GetForward() const { return WorldTransform.GetUnitAxis(EAxis::X); }
	FVector GetRight() const { return WorldTransform.GetUnitAxis(EAxis::Y); }
	FVector GetUp() const { return WorldTransform.GetUnitAxis(EAxis::Z); }

	FVector TransformLocalPosition(const FVector& LocalPositionCM) const;
	FVector InverseTransformPosition(const FVector& WorldPositionCM) const;
	bool IsOrthonormal(double Tolerance = 1.0e-3) const;
	bool IsUsable(double Tolerance = 1.0e-3) const;
};

/**
 * The only gravity roles accepted by the M11 data-side solver.
 *
 * M9's practice satellite deliberately has no role here. This makes its
 * exclusion from finale prediction and playback a compile-time data contract
 * instead of an Actor-filtering convention.
 */
enum class EABTSM110FinaleGravityRole : uint8
{
	Primary = 0,
	AssistPlanet1,
	AssistPlanet2,
	AssistPlanet3,
	Count
};

/** UObject-free body state consumed by the future fixed-step M11 integrator. */
struct ABTSRUNTIME_API FABTSM110FinaleGravityBody
{
	EABTSM110FinaleGravityRole Role = EABTSM110FinaleGravityRole::Primary;
	FVector3d CenterCM = FVector3d::ZeroVector;
	double GravitationalParameterCM3PerSec2 = 0.0;
	double CollisionRadiusCM = 0.0;

	bool IsValid() const;
};

/**
 * Fixed four-body scenario: the generated primary plus exactly three
 * deterministic assist planets. It has no UWorld, AActor, Chaos or satellite
 * dependency and can therefore be shared by aim preview and flight playback.
 */
struct ABTSRUNTIME_API FABTSM110FinaleGravityScenario
{
	static constexpr int32 BodyCount = static_cast<int32>(EABTSM110FinaleGravityRole::Count);

	int32 LayoutVersion = 1;
	uint32 ScenarioHash = 0;
	TStaticArray<FABTSM110FinaleGravityBody, BodyCount> Bodies;

	FABTSM110FinaleGravityScenario();

	bool IsValid(FString* OutFailure = nullptr) const;
	FVector3d GetAccelerationAt(const FVector3d& PositionCM) const;
};
