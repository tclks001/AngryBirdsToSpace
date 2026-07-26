// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Slingshot/ABTSSlingshotTypes.h"
#include "ABTSSlingshotVisualTypes.generated.h"

class UMaterialInterface;
class UStaticMesh;

/** Which geometric point of a mesh is aligned to the authored slingshot anchor. */
enum class EABTSSlingshotVisualAnchor : uint8
{
	BoundsCenter,
	BoundsBottomCenter
};

/** Editor-facing mesh binding and local correction for one slingshot visual part. */
USTRUCT(BlueprintType)
struct FABTSSlingshotVisualSlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	TObjectPtr<UStaticMesh> Mesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	TObjectPtr<UMaterialInterface> Material = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	FVector LocalOffsetCM = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	FRotator LocalRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	FVector LocalScale = FVector::OneVector;
};

/** Local-space attachment layout shared by idle preview and pulled runtime visuals. */
USTRUCT(BlueprintType)
struct FABTSSlingshotConnectionLayout
{
	GENERATED_BODY()

	/** Offset from the automatically calculated top of the left/Y-negative stake. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connections")
	FVector StakeAConnectionOffsetCM = FVector::ZeroVector;

	/** Offset from the automatically calculated top of the right/Y-positive stake. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connections")
	FVector StakeBConnectionOffsetCM = FVector::ZeroVector;

	/** Idle pouch center relative to the midpoint between both stake anchors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connections")
	FVector RestPouchOffsetCM = FVector::ZeroVector;

	/** Left cord attachment point in pouch-local coordinates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connections")
	FVector PouchAConnectionOffsetCM = FVector(0.0f, -18.0f, 0.0f);

	/** Right cord attachment point in pouch-local coordinates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connections")
	FVector PouchBConnectionOffsetCM = FVector(0.0f, 18.0f, 0.0f);
};

/**
 * Complete visual/tuning contract shared by M7.1 test-stage actors and the
 * CellTopo sphere. All dimensions are final world centimetres before slot scale.
 */
USTRUCT(BlueprintType)
struct FABTSSlingshotVisualPreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry", meta = (ClampMin = "50.0"))
	float BaseStakeSpacingCM = 210.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry", meta = (ClampMin = "20.0"))
	float StakeHeightCM = 220.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry", meta = (ClampMin = "1.0"))
	float StakeDiameterCM = 28.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry", meta = (ClampMin = "1.0"))
	float CordThicknessCM = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry", meta = (ClampMin = "1.0"))
	FVector PouchSizeCM = FVector(42.0f, 60.0f, 12.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stake")
	FABTSSlingshotVisualSlot StakeVisual;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cord")
	FABTSSlingshotVisualSlot CordVisual;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pouch")
	FABTSSlingshotVisualSlot PouchVisual;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connections")
	FABTSSlingshotConnectionLayout ConnectionLayout;
};

/** Native defaults shared by all tiers. Blueprint subclasses may override any preset field afterwards. */
ABTSRUNTIME_API FABTSSlingshotVisualPreset ABTSMakeDefaultSlingshotVisualPreset(EABTSSlingshotTier Tier);

/**
 * Fits a visual mesh to an explicit world-space size and aligns one of its bounds anchors.
 * LocalOffsetCM remains a real centimetre offset and is deliberately not multiplied by mesh scale.
 */
ABTSRUNTIME_API FTransform ABTSMakeSlingshotVisualTransform(
	const UStaticMesh* Mesh,
	const FVector& TargetAnchorWorld,
	const FQuat& TargetRotation,
	const FVector& TargetSizeCM,
	const FABTSSlingshotVisualSlot& VisualSlot,
	EABTSSlingshotVisualAnchor Anchor);

/**
 * Converts an authored pouch-local attachment offset into its displayed offset.
 * Pouch LocalScale is visual scale, so its magnitude must also scale the cord anchors.
 */
ABTSRUNTIME_API FVector ABTSScaleSlingshotPouchConnectionOffset(
	const FVector& AuthoredOffsetCM,
	const FABTSSlingshotVisualSlot& PouchVisualSlot);
