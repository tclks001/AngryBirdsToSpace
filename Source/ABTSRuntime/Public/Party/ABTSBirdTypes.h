// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ABTSBirdTypes.generated.h"

class UStaticMesh;
class UTexture2D;

UENUM(BlueprintType)
enum class EABTSBirdId : uint8
{
	Red UMETA(DisplayName = "Red - 绯翼"),
	Blue UMETA(DisplayName = "Blue - 青翎"),
	Yellow UMETA(DisplayName = "Yellow - 棱喙"),
	Black UMETA(DisplayName = "Black - 玄爪")
};

UENUM(BlueprintType)
enum class EABTSBirdSlingshotCapability : uint8
{
	Simple,
	TwigScout,
	Reinforced
};

USTRUCT(BlueprintType)
struct FABTSBirdPresentationConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Bird")
	EABTSBirdId BirdId = EABTSBirdId::Red;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Bird")
	FText DisplayName;

	/** Optional model override. Null keeps the native sphere fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Bird")
	TObjectPtr<UStaticMesh> BirdMesh = nullptr;

	/** Optional portrait. Null uses FallbackColor in the HUD circle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Bird")
	TObjectPtr<UTexture2D> PortraitTexture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Bird")
	FLinearColor FallbackColor = FLinearColor::Red;

	/** Reserved for the later slingshot milestone; M4 does not execute launches. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Slingshot")
	EABTSBirdSlingshotCapability SlingshotCapability = EABTSBirdSlingshotCapability::Simple;
};

inline int32 ABTSBirdIdToIndex(const EABTSBirdId BirdId)
{
	return static_cast<int32>(BirdId);
}
