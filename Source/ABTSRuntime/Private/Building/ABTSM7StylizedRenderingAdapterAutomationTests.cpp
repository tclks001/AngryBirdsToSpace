// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Building/ABTSM7StylizedRenderingAdapter.h"

#include "Building/ABTSM7BuildingModule.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Misc/AutomationTest.h"
#include "Rendering/ABTSStylizedMaterialContract.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM7StylizedRenderingAdapterTest,
	"ABTS.M7.StylizedRendering.Adapter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM7StylizedRenderingAdapterTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using Family = EABTSStylizedMaterialFamily;
	FABTSM7StylizedMaterialSet LoadedMaterials;
	FString LoadFailure;
	TestTrue(TEXT("All fixed M7 T3-B candidates load"),
		FABTSM7StylizedRenderingAdapter::TryLoadMaterialSet(
			LoadedMaterials, &LoadFailure));
	TestTrue(TEXT("The loaded M7 T3-B set is complete"), LoadedMaterials.IsComplete());
	TestEqual(TEXT("Wood uses its fixed M7 soft path"),
		GetPathNameSafe(LoadedMaterials.Wood),
		FString(TEXT("/Game/M7/Toon/Buildings/MI_ABTS_M7_Toon_Wood.MI_ABTS_M7_Toon_Wood")));
	TestEqual(TEXT("Stone uses its fixed M7 soft path"),
		GetPathNameSafe(LoadedMaterials.Stone),
		FString(TEXT("/Game/M7/Toon/Buildings/MI_ABTS_M7_Toon_Stone.MI_ABTS_M7_Toon_Stone")));
	TestEqual(TEXT("Steel uses its fixed M7 soft path"),
		GetPathNameSafe(LoadedMaterials.Steel),
		FString(TEXT("/Game/M7/Toon/Buildings/MI_ABTS_M7_Toon_Steel.MI_ABTS_M7_Toon_Steel")));
	TestEqual(TEXT("Glass uses its fixed M7 soft path"),
		GetPathNameSafe(LoadedMaterials.Glass),
		FString(TEXT("/Game/M7/Toon/Buildings/MI_ABTS_M7_Toon_Glass.MI_ABTS_M7_Toon_Glass")));
	for (UMaterialInterface* Candidate : {
		LoadedMaterials.Wood, LoadedMaterials.Stone,
		LoadedMaterials.Steel, LoadedMaterials.Glass})
	{
		TestTrue(TEXT("Every M7 toon building material has its Shipping HISM permutation"),
			Candidate != nullptr
				&& Candidate->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));
		const UMaterialInstance* Instance = Cast<UMaterialInstance>(Candidate);
		TestNotNull(TEXT("Every M7 T3-B candidate is a real material instance"), Instance);
		TestEqual(TEXT("Every M7 T3-B candidate uses the approved Toon parent"),
			GetPathNameSafe(Instance ? Instance->Parent : nullptr),
			FString(TEXT("/Game/Toon/Shared/Masters/M_ABTS_Toon_SlingshotSolid.M_ABTS_Toon_SlingshotSolid")));
		for (const FName ParameterName : {
			FABTSStylizedMaterialContract::GetStyleEnabledParameterName(),
			FABTSStylizedMaterialContract::GetRoughnessFloorParameterName(),
			FABTSStylizedMaterialContract::GetRoughnessScaleParameterName(),
			FABTSStylizedMaterialContract::GetSpecularScaleParameterName(),
			FABTSStylizedMaterialContract::GetMetallicScaleParameterName(),
			FABTSStylizedMaterialContract::GetRimStrengthParameterName(),
			FABTSStylizedMaterialContract::GetRimPowerParameterName()})
		{
			float Value = 0.0f;
			TestTrue(*FString::Printf(TEXT("M7 T3-B candidate exposes %s"),
				*ParameterName.ToString()), Candidate->GetScalarParameterValue(
				FMaterialParameterInfo(ParameterName), Value));
		}
		FLinearColor Tint;
		TestTrue(TEXT("M7 T3-B candidate exposes BaseColorTint"),
			Candidate->GetVectorParameterValue(FMaterialParameterInfo(
				FABTSStylizedMaterialContract::GetBaseColorTintParameterName()), Tint));
	}
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
	TStrongObjectPtr<UStaticMeshComponent> Component(
		NewObject<UStaticMeshComponent>(GetTransientPackage()));
	Component->SetMaterial(0, Original.Get());
	FABTSStylizedMaterialSlotBinding Binding;
	Binding.Component = Component.Get();
	Binding.MaterialSlotIndex = 0;
	Binding.StylizedMaterial = LoadedMaterials.Glass;
	Binding.Family = Family::M7Glass;
	FABTSStylizedMaterialOverrideRegistry Registry;
	Registry.Apply(TArray<FABTSStylizedMaterialSlotBinding>{Binding}, true);
	TestEqual(TEXT("Style On installs fixed M7 glass candidate without a MID"),
		Component->GetMaterial(0), LoadedMaterials.Glass);
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
	TestEqual(TEXT("Device presentation publishes BuildingBody in the release slice"),
		FABTSM7StylizedRenderingAdapter::ResolveObjectClass(*Device),
		EABTSStylizedObjectClass::BuildingBody);
	TestNotNull(TEXT("Promoted module exposes one presentation primitive"),
		Device->GetStylizedPresentationPrimitive());
	TestEqual(TEXT("Crystal deliberately reuses ordinary M7Glass"),
		LoadedMaterials.Get(EABTSM7BuildingMaterial::Crystal), LoadedMaterials.Glass);
	TestEqual(TEXT("A missing material set emits no mutations or bindings"),
		FABTSM7StylizedMaterialSet().IsComplete(), false);

	Registry.Apply(TArray<FABTSStylizedMaterialSlotBinding>{Binding}, true);
	Component->DestroyComponent();
	Registry.Apply({}, true);
	TestEqual(TEXT("Destroyed presentation cleanup leaves no registry state"),
		Registry.Num(), 0);
	return true;
}

#endif
