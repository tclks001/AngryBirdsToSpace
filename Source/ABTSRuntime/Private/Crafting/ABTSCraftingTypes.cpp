// Copyright Epic Games, Inc. All Rights Reserved.

#include "Crafting/ABTSCraftingTypes.h"

FText ABTSGetCraftingStationDisplayName(const EABTSCraftingStationType StationType)
{
	switch (StationType)
	{
	case EABTSCraftingStationType::Workbench: return FText::FromString(TEXT("工作台"));
	case EABTSCraftingStationType::Furnace: return FText::FromString(TEXT("熔炉"));
	default: return FText::FromString(TEXT("徒手"));
	}
}

FString ABTSGetCraftingStationFallbackLabel(const EABTSCraftingStationType StationType)
{
	switch (StationType)
	{
	case EABTSCraftingStationType::Workbench: return TEXT("Workbench");
	case EABTSCraftingStationType::Furnace: return TEXT("Furnace");
	default: return TEXT("Handcraft");
	}
}
