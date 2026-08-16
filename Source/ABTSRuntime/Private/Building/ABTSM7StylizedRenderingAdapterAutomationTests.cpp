// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Building/ABTSM7StylizedRenderingAdapter.h"

#include "Building/ABTSM7BuildingModule.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM7StylizedRenderingAdapterTest,
	"ABTS.M7.StylizedRendering.Adapter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM7StylizedRenderingAdapterTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using Family = EABTSStylizedMaterialFamily;
	for (const TPair<EABTSM7BuildingMaterial, Family> Pair : {
		TPair<EABTSM7BuildingMaterial, Family>(EABTSM7BuildingMaterial::Wood, Family::M7Wood),
		TPair<EABTSM7BuildingMaterial, Family>(EABTSM7BuildingMaterial::Stone, Family::M7Stone),
		TPair<EABTSM7BuildingMaterial, Family>(EABTSM7BuildingMaterial::Iron, Family::M7Steel),
		TPair<EABTSM7BuildingMaterial, Family>(EABTSM7BuildingMaterial::Glass, Family::M7Glass),
		TPair<EABTSM7BuildingMaterial, Family>(EABTSM7BuildingMaterial::Crystal, Family::M7Glass)})
	{
		TestEqual(TEXT("M7 material family mapping is stable"),
			FABTSM7StylizedRenderingAdapter::ResolveMaterialFamily(Pair.Key),
			Pair.Value);
		TestEqual(TEXT("M7 materials use reversible slot overrides"),
			FABTSStylizedMaterialContract::ResolveAdoptionMode(Pair.Value),
			EABTSStylizedMaterialAdoptionMode::ReversibleSlotOverride);
	}

	TStrongObjectPtr<UMaterial> Original(NewObject<UMaterial>());
	TStrongObjectPtr<UMaterial> Stylized(NewObject<UMaterial>());
	TStrongObjectPtr<UStaticMeshComponent> Component(
		NewObject<UStaticMeshComponent>(GetTransientPackage()));
	Component->SetMaterial(0, Original.Get());
	FABTSStylizedMaterialSlotBinding Binding;
	Binding.Component = Component.Get();
	Binding.MaterialSlotIndex = 0;
	Binding.StylizedMaterial = Stylized.Get();
	Binding.Family = Family::M7Glass;
	FABTSStylizedMaterialOverrideRegistry Registry;
	Registry.Apply(TArray<FABTSStylizedMaterialSlotBinding>{Binding}, true);
	TestEqual(TEXT("Style On installs supplied M7 candidate without a MID"),
		Component->GetMaterial(0), static_cast<UMaterialInterface*>(Stylized.Get()));
	Registry.Apply({}, false);
	TestEqual(TEXT("Style Off restores exact original M7 material"),
		Component->GetMaterial(0), static_cast<UMaterialInterface*>(Original.Get()));
	TestEqual(TEXT("Style Off releases saved override ownership"), Registry.Num(), 0);

	AABTSM7BuildingModule* Device =
		NewObject<AABTSM7BuildingModule>(GetTransientPackage());
	Device->ConfigureCylinder(nullptr, Original.Get(),
		EABTSM7ModuleKind::ExplosiveBarrel,
		EABTSM7BuildingMaterial::Iron, 100.0f, 50.0f,
		FTransform::Identity);
	TestEqual(TEXT("Device presentation publishes BuildingWeakPoint"),
		FABTSM7StylizedRenderingAdapter::ResolveObjectClass(*Device),
		EABTSStylizedObjectClass::BuildingWeakPoint);
	TestNotNull(TEXT("Promoted module exposes one presentation primitive"),
		Device->GetStylizedPresentationPrimitive());

	Registry.Apply(TArray<FABTSStylizedMaterialSlotBinding>{Binding}, true);
	Component->DestroyComponent();
	Registry.Apply({}, true);
	TestEqual(TEXT("Destroyed presentation cleanup leaves no registry state"),
		Registry.Num(), 0);
	return true;
}

#endif
