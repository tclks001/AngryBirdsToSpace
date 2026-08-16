// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM7StylizedRenderingAdapter.h"

#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Materials/MaterialInterface.h"

namespace ABTSM7StylizedRenderingAdapterPrivate
{
	void AddMaterialBindings(
		UPrimitiveComponent* Component,
		const EABTSM7BuildingMaterial Material,
		const FABTSM7StylizedMaterialSet& Materials,
		TArray<FABTSStylizedMaterialSlotBinding>& OutBindings)
	{
		UMaterialInterface* Stylized = Materials.Get(Material);
		const EABTSStylizedMaterialFamily Family =
			FABTSM7StylizedRenderingAdapter::ResolveMaterialFamily(Material);
		if (!IsValid(Component) || !IsValid(Stylized)
			|| Family == EABTSStylizedMaterialFamily::None)
		{
			return;
		}
		for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
		{
			FABTSStylizedMaterialSlotBinding Binding;
			Binding.Component = Component;
			Binding.MaterialSlotIndex = Slot;
			Binding.StylizedMaterial = Stylized;
			Binding.Family = Family;
			if (Binding.IsValid())
			{
				OutBindings.Add(Binding);
			}
		}
	}

	void AddSemantic(
		const AABTSM7BuildingMaterialSystem& Authority,
		const AActor& Actor,
		const UPrimitiveComponent& Component,
		const EABTSM7BuildingMaterial Material,
		const EABTSStylizedObjectClass ObjectClass,
		const bool bDynamic,
		TArray<FABTSM7StylizedSemanticBinding>& OutBindings)
	{
		FABTSM7StylizedSemanticBinding Binding;
		Binding.SemanticAuthority = &Authority;
		Binding.Actor = &Actor;
		Binding.Component = &Component;
		Binding.Material = Material;
		Binding.ObjectClass = ObjectClass;
		Binding.bDynamic = bDynamic;
		if (Binding.IsValid())
		{
			OutBindings.Add(Binding);
		}
	}
}

UMaterialInterface* FABTSM7StylizedMaterialSet::Get(
	const EABTSM7BuildingMaterial Material) const
{
	switch (Material)
	{
	case EABTSM7BuildingMaterial::Wood: return Wood;
	case EABTSM7BuildingMaterial::Stone: return Stone;
	case EABTSM7BuildingMaterial::Iron: return Steel;
	case EABTSM7BuildingMaterial::Glass:
	case EABTSM7BuildingMaterial::Crystal: return Glass;
	default: return nullptr;
	}
}

bool FABTSM7StylizedSemanticBinding::IsValid() const
{
	return SemanticAuthority != nullptr && Actor != nullptr && Component != nullptr
		&& FABTSStylizedRenderingContract::IsObjectClassValid(ObjectClass);
}

uint8 FABTSM7StylizedSemanticBinding::ResolveStencilValueForRenderer() const
{
	return IsValid()
		? FABTSStylizedRenderingContract::ResolveStencilValueForRenderer(ObjectClass)
		: 0;
}

EABTSStylizedMaterialFamily FABTSM7StylizedRenderingAdapter::ResolveMaterialFamily(
	const EABTSM7BuildingMaterial Material)
{
	switch (Material)
	{
	case EABTSM7BuildingMaterial::Wood: return EABTSStylizedMaterialFamily::M7Wood;
	case EABTSM7BuildingMaterial::Stone: return EABTSStylizedMaterialFamily::M7Stone;
	case EABTSM7BuildingMaterial::Iron: return EABTSStylizedMaterialFamily::M7Steel;
	case EABTSM7BuildingMaterial::Glass:
	case EABTSM7BuildingMaterial::Crystal: return EABTSStylizedMaterialFamily::M7Glass;
	default: return EABTSStylizedMaterialFamily::None;
	}
}

EABTSStylizedObjectClass FABTSM7StylizedRenderingAdapter::ResolveObjectClass(
	const AABTSM7BuildingModule& Module)
{
	return Module.IsStylizedWeakPoint()
		? EABTSStylizedObjectClass::BuildingWeakPoint
		: EABTSStylizedObjectClass::BuildingBody;
}

void FABTSM7StylizedRenderingAdapter::GatherMaterialBindings(
	const AABTSM7BuildingMaterialSystem& MaterialSystem,
	const FABTSM7StylizedMaterialSet& Materials,
	TArray<FABTSStylizedMaterialSlotBinding>& OutBindings)
{
	OutBindings.Reset();
	using namespace ABTSM7StylizedRenderingAdapterPrivate;
	for (const TPair<UHierarchicalInstancedStaticMeshComponent*, EABTSM7BuildingMaterial> Pair : {
		TPair<UHierarchicalInstancedStaticMeshComponent*, EABTSM7BuildingMaterial>(MaterialSystem.WoodBrickHISM.Get(), EABTSM7BuildingMaterial::Wood),
		TPair<UHierarchicalInstancedStaticMeshComponent*, EABTSM7BuildingMaterial>(MaterialSystem.StoneBrickHISM.Get(), EABTSM7BuildingMaterial::Stone),
		TPair<UHierarchicalInstancedStaticMeshComponent*, EABTSM7BuildingMaterial>(MaterialSystem.IronBrickHISM.Get(), EABTSM7BuildingMaterial::Iron),
		TPair<UHierarchicalInstancedStaticMeshComponent*, EABTSM7BuildingMaterial>(MaterialSystem.GlassBrickHISM.Get(), EABTSM7BuildingMaterial::Glass),
		TPair<UHierarchicalInstancedStaticMeshComponent*, EABTSM7BuildingMaterial>(MaterialSystem.CrystalBrickHISM.Get(), EABTSM7BuildingMaterial::Crystal)})
	{
		AddMaterialBindings(Pair.Key, Pair.Value, Materials, OutBindings);
	}
	TArray<AABTSM7BuildingModule*> Modules;
	MaterialSystem.GatherLiveModulesForStylizedAdapter(Modules);
	for (AABTSM7BuildingModule* Module : Modules)
	{
		AddMaterialBindings(Module->GetStylizedPresentationPrimitive(),
			Module->GetBuildingMaterial(), Materials, OutBindings);
	}
}

void FABTSM7StylizedRenderingAdapter::GatherSemanticBindings(
	const AABTSM7BuildingMaterialSystem& MaterialSystem,
	TArray<FABTSM7StylizedSemanticBinding>& OutBindings)
{
	OutBindings.Reset();
	using namespace ABTSM7StylizedRenderingAdapterPrivate;
	for (const TPair<UHierarchicalInstancedStaticMeshComponent*, EABTSM7BuildingMaterial> Pair : {
		TPair<UHierarchicalInstancedStaticMeshComponent*, EABTSM7BuildingMaterial>(MaterialSystem.WoodBrickHISM.Get(), EABTSM7BuildingMaterial::Wood),
		TPair<UHierarchicalInstancedStaticMeshComponent*, EABTSM7BuildingMaterial>(MaterialSystem.StoneBrickHISM.Get(), EABTSM7BuildingMaterial::Stone),
		TPair<UHierarchicalInstancedStaticMeshComponent*, EABTSM7BuildingMaterial>(MaterialSystem.IronBrickHISM.Get(), EABTSM7BuildingMaterial::Iron),
		TPair<UHierarchicalInstancedStaticMeshComponent*, EABTSM7BuildingMaterial>(MaterialSystem.GlassBrickHISM.Get(), EABTSM7BuildingMaterial::Glass),
		TPair<UHierarchicalInstancedStaticMeshComponent*, EABTSM7BuildingMaterial>(MaterialSystem.CrystalBrickHISM.Get(), EABTSM7BuildingMaterial::Crystal)})
	{
		if (IsValid(Pair.Key))
		{
			AddSemantic(MaterialSystem, MaterialSystem, *Pair.Key, Pair.Value,
				Pair.Value == EABTSM7BuildingMaterial::Crystal
					? EABTSStylizedObjectClass::BuildingWeakPoint
					: EABTSStylizedObjectClass::BuildingBody,
				false, OutBindings);
		}
	}
	TArray<AABTSM7BuildingModule*> Modules;
	MaterialSystem.GatherLiveModulesForStylizedAdapter(Modules);
	for (AABTSM7BuildingModule* Module : Modules)
	{
		if (UPrimitiveComponent* Component = Module->GetStylizedPresentationPrimitive())
		{
			AddSemantic(MaterialSystem, *Module, *Component,
				Module->GetBuildingMaterial(), ResolveObjectClass(*Module),
				Module->IsDynamic(), OutBindings);
		}
	}
}
