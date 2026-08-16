// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM7StylizedRenderingAdapter.h"

#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/SoftObjectPtr.h"

namespace ABTSM7StylizedRenderingAdapterPrivate
{
	constexpr const TCHAR* WoodPath = TEXT("/Game/M7/Toon/Buildings/MI_ABTS_M7_Toon_Wood.MI_ABTS_M7_Toon_Wood");
	constexpr const TCHAR* StonePath = TEXT("/Game/M7/Toon/Buildings/MI_ABTS_M7_Toon_Stone.MI_ABTS_M7_Toon_Stone");
	constexpr const TCHAR* SteelPath = TEXT("/Game/M7/Toon/Buildings/MI_ABTS_M7_Toon_Steel.MI_ABTS_M7_Toon_Steel");
	constexpr const TCHAR* GlassPath = TEXT("/Game/M7/Toon/Buildings/MI_ABTS_M7_Toon_Glass.MI_ABTS_M7_Toon_Glass");
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

bool FABTSM7StylizedMaterialSet::IsComplete() const
{
	return IsValid(Wood) && IsValid(Stone) && IsValid(Steel) && IsValid(Glass);
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
	// 8/18 release deliberately has no gameplay weak-point visual semantics.
	return EABTSStylizedObjectClass::BuildingBody;
}

bool FABTSM7StylizedRenderingAdapter::TryLoadMaterialSet(
	FABTSM7StylizedMaterialSet& OutMaterials,
	FString* OutFailureReason)
{
	OutMaterials = FABTSM7StylizedMaterialSet();
	if (OutFailureReason != nullptr)
	{
		OutFailureReason->Reset();
	}
	using namespace ABTSM7StylizedRenderingAdapterPrivate;
	const TSoftObjectPtr<UMaterialInterface> WoodCandidate{FSoftObjectPath(WoodPath)};
	const TSoftObjectPtr<UMaterialInterface> StoneCandidate{FSoftObjectPath(StonePath)};
	const TSoftObjectPtr<UMaterialInterface> SteelCandidate{FSoftObjectPath(SteelPath)};
	const TSoftObjectPtr<UMaterialInterface> GlassCandidate{FSoftObjectPath(GlassPath)};
	FABTSM7StylizedMaterialSet Candidate;
	Candidate.Wood = WoodCandidate.LoadSynchronous();
	Candidate.Stone = StoneCandidate.LoadSynchronous();
	Candidate.Steel = SteelCandidate.LoadSynchronous();
	Candidate.Glass = GlassCandidate.LoadSynchronous();
	if (!Candidate.IsComplete())
	{
		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = FString::Printf(
				TEXT("MissingFixedCandidate Wood=%d Stone=%d Steel=%d Glass=%d"),
				IsValid(Candidate.Wood) ? 1 : 0, IsValid(Candidate.Stone) ? 1 : 0,
				IsValid(Candidate.Steel) ? 1 : 0, IsValid(Candidate.Glass) ? 1 : 0);
		}
		return false;
	}
	OutMaterials = Candidate;
	return true;
}

void FABTSM7StylizedRenderingAdapter::GatherMaterialBindings(
	const AABTSM7BuildingMaterialSystem& MaterialSystem,
	const FABTSM7StylizedMaterialSet& Materials,
	TArray<FABTSStylizedMaterialSlotBinding>& OutBindings,
	FABTSM7StylizedAdapterReadiness* OutReadiness)
{
	OutBindings.Reset();
	if (OutReadiness != nullptr)
	{
		*OutReadiness = FABTSM7StylizedAdapterReadiness();
		OutReadiness->bSemanticReady = true;
		OutReadiness->bMaterialSetReady = Materials.IsComplete();
	}
	if (!Materials.IsComplete())
	{
		return;
	}
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
	if (OutReadiness != nullptr)
	{
		OutReadiness->PublishedSlotCount = OutBindings.Num();
		// Application is intentionally owned by Integration's reversible registry.
		OutReadiness->AppliedSlotCount = 0;
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
				EABTSStylizedObjectClass::BuildingBody, false, OutBindings);
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
