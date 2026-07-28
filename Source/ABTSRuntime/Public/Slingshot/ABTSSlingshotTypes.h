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

/** Logical installation contract of a terrain slot, independent from its visual mesh. */
UENUM(BlueprintType)
enum class EABTSSlingshotSlotKind : uint8
{
	Standard UMETA(DisplayName = "Standard Slingshot Slot"),
	FinaleSpace UMETA(DisplayName = "Finale Space Slingshot Slot")
};

/** Stable identity inside the unique terminal slot pair. */
UENUM(BlueprintType)
enum class EABTSSlingshotSlotSide : uint8
{
	None,
	Left,
	Right
};
