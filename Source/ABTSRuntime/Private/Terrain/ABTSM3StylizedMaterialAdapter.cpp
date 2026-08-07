// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/ABTSM3StylizedMaterialAdapter.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Terrain/ABTSM3Planet.h"

namespace ABTSM3StylizedMaterialAdapterPrivate
{
	void AddBindingIfValid(
		UHierarchicalInstancedStaticMeshComponent* Component,
		UMaterialInterface* StylizedMaterial,
		TArray<FABTSStylizedMaterialSlotBinding>& OutBindings)
	{
		constexpr int32 MaterialSlotIndex = 0;
		if (!IsValid(Component)
			|| !IsValid(StylizedMaterial)
			|| Component->GetNumMaterials() <= MaterialSlotIndex)
		{
			return;
		}

		FABTSStylizedMaterialSlotBinding Binding;
		Binding.Component = Component;
		Binding.MaterialSlotIndex = MaterialSlotIndex;
		Binding.StylizedMaterial = StylizedMaterial;
		Binding.Family = EABTSStylizedMaterialFamily::M3BackgroundProp;
		if (Binding.IsValid())
		{
			OutBindings.Add(Binding);
		}
	}
}

void FABTSM3StylizedMaterialAdapter::GatherBackgroundPropMaterialBindings(
	const AABTSM3Planet& Planet,
	TArray<FABTSStylizedMaterialSlotBinding>& OutBindings)
{
	OutBindings.Reset();
	if (FABTSStylizedMaterialContract::ResolveOwner(
			EABTSStylizedMaterialFamily::M3BackgroundProp)
			!= EABTSStylizedMaterialOwner::M3
		|| FABTSStylizedMaterialContract::ResolveAdoptionMode(
			EABTSStylizedMaterialFamily::M3BackgroundProp)
			!= EABTSStylizedMaterialAdoptionMode::ReversibleSlotOverride)
	{
		return;
	}

	// Stable order is part of the adapter identity: forest batch, then rock batch.
	ABTSM3StylizedMaterialAdapterPrivate::AddBindingIfValid(
		Planet.ForestHISM,
		Planet.ForestStylizedMaterial,
		OutBindings);
	ABTSM3StylizedMaterialAdapterPrivate::AddBindingIfValid(
		Planet.RockHISM,
		Planet.RockStylizedMaterial,
		OutBindings);
}
