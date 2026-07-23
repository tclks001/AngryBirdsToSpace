// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ABTSSlingshotTypes.generated.h"

/** Gameplay tier of a complete slingshot, independent from its temporary inventory parts. */
UENUM(BlueprintType)
enum class EABTSSlingshotTier : uint8
{
	Twig UMETA(DisplayName = "Twig Slingshot"),
	Simple UMETA(DisplayName = "Simple Slingshot"),
	Reinforced UMETA(DisplayName = "Reinforced Slingshot"),
	Space UMETA(DisplayName = "Space Slingshot")
};
