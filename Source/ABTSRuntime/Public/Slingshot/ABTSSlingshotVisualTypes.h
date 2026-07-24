// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ABTSSlingshotVisualTypes.generated.h"

class UMaterialInterface;
class UStaticMesh;

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
