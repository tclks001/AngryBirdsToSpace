// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Terrain/ABTSM3StylizedMaterialAdapter.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "PCG/ABTSM3TaskGraphTypes.h"
#include "Planet/ABTSM2Planet.h"
#include "ProceduralMeshComponent.h"
#include "Rendering/ABTSStylizedMaterialContract.h"
#include "Rendering/ABTSStylizedRenderingControl.h"
#include "Terrain/ABTSM3Planet.h"
#include "Terrain/ABTSM3TerrainMaterialBridge.h"
#include "Terrain/ABTSM3TerrainVisualField.h"

namespace ABTSM3StylizedMaterialTests
{
	bool HasScalarParameter(
		const UMaterialInterface& Material,
		const FName& Name)
	{
		float Value = 0.0f;
		return Material.GetScalarParameterValue(
			FHashedMaterialParameterInfo(Name),
			Value);
	}

	bool HasVectorParameter(
		const UMaterialInterface& Material,
		const FName& Name)
	{
		FLinearColor Value = FLinearColor::Black;
		return Material.GetVectorParameterValue(
			FHashedMaterialParameterInfo(Name),
			Value);
	}

	void BuildSingleCellFixture(
		TArray<FABTSM2Cell>& OutCells,
		TArray<FABTSM3CellState>& OutCellStates,
		TArray<FABTSM3CellEdgeState>& OutEdgeStates,
		FABTSM3TerrainVisualField& OutVisualField)
	{
		OutCells.SetNum(1);
		OutCells[0].UnitCenter = FVector::UpVector;
		OutCellStates.SetNum(1);
		OutCellStates[0].TerrainType = EABTSM3TerrainType::Forest;
		OutCellStates[0].LogicalHeight01 = 0.25f;
		OutEdgeStates.Reset();
		OutVisualField.Initialize(
			10000.0f,
			900.0f,
			80.0f,
			160.0f,
			240.0f,
			160.0f,
			OutCells,
			OutCellStates,
			OutEdgeStates,
			80.0f,
			180.0f,
			70.0f,
			125.0f,
			190.0f);
	}

	bool ReadScalar(
		FAutomationTestBase& Test,
		const UABTSM3TerrainMaterialBridge& Bridge,
		const FName& Name,
		float& OutValue)
	{
		const bool bFound = Bridge.TryGetScalarParameterValue(Name, OutValue);
		Test.TestTrue(
			FString::Printf(TEXT("MID keeps scalar parameter %s"), *Name.ToString()),
			bFound);
		return bFound;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3StylizedSurfaceMaterialTest,
	"ABTS.M3.StylizedMaterials.SurfaceContractRuntimeSwitchAndFailSoft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM3StylizedSurfaceMaterialTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace ABTSM3StylizedMaterialTests;
	TestEqual(
		TEXT("M3Surface is owned by M3"),
		FABTSStylizedMaterialContract::ResolveOwner(
			EABTSStylizedMaterialFamily::M3Surface),
		EABTSStylizedMaterialOwner::M3);
	TestEqual(
		TEXT("M3Surface is an in-place parameter family"),
		FABTSStylizedMaterialContract::ResolveAdoptionMode(
			EABTSStylizedMaterialFamily::M3Surface),
		EABTSStylizedMaterialAdoptionMode::InPlaceStyleParameter);

	UMaterialInterface* TerrainMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/Materials/M_ABTS_M3_SDFTerrain.M_ABTS_M3_SDFTerrain"));
	TestNotNull(TEXT("M3 SDF terrain material loads"), TerrainMaterial);
	if (TerrainMaterial == nullptr)
	{
		return false;
	}

	const FName ScalarParameters[] =
	{
		FABTSStylizedMaterialContract::GetStyleEnabledParameterName(),
		FABTSStylizedMaterialContract::GetRoughnessFloorParameterName(),
		FABTSStylizedMaterialContract::GetRoughnessScaleParameterName(),
		FABTSStylizedMaterialContract::GetSpecularScaleParameterName(),
		FABTSStylizedMaterialContract::GetMetallicScaleParameterName(),
		FABTSStylizedMaterialContract::GetRimStrengthParameterName(),
		FABTSStylizedMaterialContract::GetRimPowerParameterName()
	};
	for (const FName& Name : ScalarParameters)
	{
		TestTrue(
			FString::Printf(TEXT("Terrain material exposes %s"), *Name.ToString()),
			HasScalarParameter(*TerrainMaterial, Name));
	}
	TestTrue(
		TEXT("Terrain material exposes ABTS_BaseColorTint"),
		HasVectorParameter(
			*TerrainMaterial,
			FABTSStylizedMaterialContract::GetBaseColorTintParameterName()));

	TArray<FABTSM2Cell> Cells;
	TArray<FABTSM3CellState> CellStates;
	TArray<FABTSM3CellEdgeState> EdgeStates;
	FABTSM3TerrainVisualField VisualField;
	BuildSingleCellFixture(Cells, CellStates, EdgeStates, VisualField);
	TestTrue(TEXT("Single-cell visual fixture is ready"), VisualField.IsReady());

	UProceduralMeshComponent* Surface =
		NewObject<UProceduralMeshComponent>(GetTransientPackage());
	UABTSM3TerrainMaterialBridge* Bridge =
		NewObject<UABTSM3TerrainMaterialBridge>(GetTransientPackage());
	TestNotNull(TEXT("Transient terrain surface exists"), Surface);
	TestNotNull(TEXT("Transient terrain bridge exists"), Bridge);
	if (Surface == nullptr || Bridge == nullptr)
	{
		return false;
	}

	const bool bStyleInitiallyEnabled =
		FABTSStylizedRenderingControl::IsEnabled();
	FABTSStylizedRenderingControl::SetEnabled(false);
	const bool bInitialized = Bridge->Initialize(
		Surface,
		TerrainMaterial,
		FVector(100.0f, 200.0f, 300.0f),
		10000.0f,
		240.0f,
		FLinearColor(0.22f, 0.12f, 0.045f),
		80.0f,
		180.0f,
		FLinearColor(0.03f, 0.20f, 0.36f),
		70.0f,
		125.0f,
		190.0f,
		Cells,
		CellStates,
		EdgeStates,
		VisualField);
	FABTSStylizedRenderingControl::SetEnabled(bStyleInitiallyEnabled);
	TestTrue(TEXT("Terrain bridge initializes with the T3-A1 asset"), bInitialized);
	TestTrue(
		TEXT("Terrain style contract is available"),
		Bridge->IsStylizedSurfaceContractAvailable());
	TestTrue(
		TEXT("The original terrain MID remains the assigned material"),
		Surface->GetMaterial(0) == Bridge->GetTerrainMIDForDiagnostics());

	float StyleEnabled = -1.0f;
	ReadScalar(
		*this,
		*Bridge,
		FABTSStylizedMaterialContract::GetStyleEnabledParameterName(),
		StyleEnabled);
	TestTrue(TEXT("Style Off initializes as zero"),
		FMath::IsNearlyZero(StyleEnabled));
	TestTrue(TEXT("Runtime Style On updates the same MID"),
		Bridge->ApplyStylizedSurfaceParameters(true));
	ReadScalar(
		*this,
		*Bridge,
		FABTSStylizedMaterialContract::GetStyleEnabledParameterName(),
		StyleEnabled);
	TestTrue(TEXT("Runtime Style On writes one"),
		FMath::IsNearlyEqual(StyleEnabled, 1.0f));
	TestTrue(
		TEXT("Runtime update never replaces the terrain MID"),
		Surface->GetMaterial(0) == Bridge->GetTerrainMIDForDiagnostics());

	const FName TextureParameters[] =
	{
		TEXT("M3_CellDirectionLUT"),
		TEXT("M3_CellVisualLUT"),
		TEXT("M3_BoundarySegmentLUT"),
		TEXT("M3_RoadSegmentLUT"),
		TEXT("M3_RiverSegmentLUT")
	};
	for (const FName& Name : TextureParameters)
	{
		UTexture* Texture = nullptr;
		TestTrue(
			FString::Printf(TEXT("Same MID consumes %s"), *Name.ToString()),
			Bridge->TryGetTextureParameterValue(Name, Texture)
				&& Texture != nullptr);
	}
	float ScalarValue = 0.0f;
	ReadScalar(*this, *Bridge, TEXT("M3_CellCount"), ScalarValue);
	TestTrue(TEXT("M3_CellCount remains injected"),
		FMath::IsNearlyEqual(ScalarValue, 1.0f));
	ReadScalar(*this, *Bridge, TEXT("M3_BoundarySlots"), ScalarValue);
	TestTrue(TEXT("M3_BoundarySlots remains injected"),
		FMath::IsNearlyEqual(ScalarValue, 32.0f));
	ReadScalar(*this, *Bridge, TEXT("M3_RoadSegmentCount"), ScalarValue);
	TestTrue(TEXT("M3_RoadSegmentCount remains injected"),
		FMath::IsNearlyEqual(ScalarValue, 16.0f));
	ReadScalar(*this, *Bridge, TEXT("M3_RiverSegmentCount"), ScalarValue);
	TestTrue(TEXT("M3_RiverSegmentCount remains injected"),
		FMath::IsNearlyEqual(ScalarValue, 24.0f));
	ReadScalar(*this, *Bridge, TEXT("M3_PlanetRadiusCM"), ScalarValue);
	TestTrue(TEXT("M3_PlanetRadiusCM remains injected"),
		FMath::IsNearlyEqual(ScalarValue, 10000.0f));
	ReadScalar(*this, *Bridge, TEXT("M3_BlendWidthCM"), ScalarValue);
	TestTrue(TEXT("M3_BlendWidthCM remains injected"),
		FMath::IsNearlyEqual(ScalarValue, 240.0f));
	FLinearColor PlanetCenter = FLinearColor::Black;
	FLinearColor RoadColor = FLinearColor::Black;
	FLinearColor RiverColor = FLinearColor::Black;
	TestTrue(TEXT("Same MID consumes M3_PlanetCenter"),
		Bridge->TryGetVectorParameterValue(
			TEXT("M3_PlanetCenter"),
			PlanetCenter));
	TestTrue(TEXT("M3_PlanetCenter remains injected"),
		PlanetCenter.Equals(FLinearColor(100.0f, 200.0f, 300.0f)));
	TestTrue(TEXT("Same MID consumes M3_RoadColor"),
		Bridge->TryGetVectorParameterValue(TEXT("M3_RoadColor"), RoadColor));
	TestTrue(TEXT("M3_RoadColor remains injected"),
		RoadColor.Equals(FLinearColor(0.22f, 0.12f, 0.045f)));
	TestTrue(TEXT("Same MID consumes M3_RiverColor"),
		Bridge->TryGetVectorParameterValue(TEXT("M3_RiverColor"), RiverColor));
	TestTrue(TEXT("M3_RiverColor remains injected"),
		RiverColor.Equals(FLinearColor(0.03f, 0.20f, 0.36f)));
	FLinearColor BaseColorTint = FLinearColor::Black;
	TestTrue(TEXT("Same MID consumes ABTS_BaseColorTint"),
		Bridge->TryGetVectorParameterValue(
			FABTSStylizedMaterialContract::GetBaseColorTintParameterName(),
			BaseColorTint));
	TestTrue(TEXT("Default tint preserves LUT/road/river colors"),
		BaseColorTint.Equals(FLinearColor::White));

	TestTrue(TEXT("Runtime Style Off updates the same MID"),
		Bridge->ApplyStylizedSurfaceParameters(false));
	ReadScalar(
		*this,
		*Bridge,
		FABTSStylizedMaterialContract::GetStyleEnabledParameterName(),
		StyleEnabled);
	TestTrue(TEXT("Runtime Style Off restores zero"),
		FMath::IsNearlyZero(StyleEnabled));

	AABTSM3Planet* BuiltPlanet = NewObject<AABTSM3Planet>();
	TestNotNull(TEXT("Built-planet style fixture exists"), BuiltPlanet);
	if (BuiltPlanet != nullptr)
	{
		BuiltPlanet->SurfaceSubdivision = 1;
		BuiltPlanet->InstancesPerCell = 1;
		BuiltPlanet->TerrainMaterial = TerrainMaterial;
		TestTrue(TEXT("Built-planet style fixture rebuilds"),
			BuiltPlanet->RebuildPlanet());
		const bool bPlanetReadyBefore = BuiltPlanet->IsPlanetReady();
		const bool bPresentationReadyBefore =
			BuiltPlanet->IsM3PresentationReady();
		const int64 LayoutHashBefore = BuiltPlanet->PCGSummary.LayoutHash;
		const int64 SatellitePreviewHashBefore =
			BuiltPlanet->GetMonthlySatellitePreviewResult().ResultHash;
		const int32 TaskCountBefore = BuiltPlanet->GetGeneratedTasks().Num();
		const int32 CellCountBefore = BuiltPlanet->GetGeneratedCellStates().Num();
		const int32 ForestCountBefore =
			BuiltPlanet->ForestHISM->GetInstanceCount();
		const int32 RockCountBefore =
			BuiltPlanet->RockHISM->GetInstanceCount();

		TestTrue(TEXT("Planet runtime entry applies Style On"),
			BuiltPlanet->ApplyStylizedSurfaceStyle(true));
		float PlanetStyleEnabled = -1.0f;
		TestTrue(TEXT("Planet runtime entry exposes Style On state"),
			BuiltPlanet->TryGetStylizedSurfaceStyleEnabled(
				PlanetStyleEnabled));
		TestTrue(TEXT("Planet runtime entry writes one"),
			FMath::IsNearlyEqual(PlanetStyleEnabled, 1.0f));
		TestTrue(TEXT("Planet runtime entry applies Style Off"),
			BuiltPlanet->ApplyStylizedSurfaceStyle(false));
		TestTrue(TEXT("Planet runtime entry exposes Style Off state"),
			BuiltPlanet->TryGetStylizedSurfaceStyleEnabled(
				PlanetStyleEnabled));
		TestTrue(TEXT("Planet runtime entry restores zero"),
			FMath::IsNearlyZero(PlanetStyleEnabled));

		TestEqual(TEXT("Style switch preserves PlanetReady"),
			BuiltPlanet->IsPlanetReady(),
			bPlanetReadyBefore);
		TestEqual(TEXT("Style switch preserves M3 presentation readiness"),
			BuiltPlanet->IsM3PresentationReady(),
			bPresentationReadyBefore);
		TestEqual(TEXT("Style switch preserves terrain/PCG layout hash"),
			BuiltPlanet->PCGSummary.LayoutHash,
			LayoutHashBefore);
		TestEqual(TEXT("Style switch preserves satellite result hash"),
			BuiltPlanet->GetMonthlySatellitePreviewResult().ResultHash,
			SatellitePreviewHashBefore);
		TestEqual(TEXT("Style switch preserves TaskGraph count"),
			BuiltPlanet->GetGeneratedTasks().Num(),
			TaskCountBefore);
		TestEqual(TEXT("Style switch preserves terrain cell count"),
			BuiltPlanet->GetGeneratedCellStates().Num(),
			CellCountBefore);
		TestEqual(TEXT("Style switch preserves forest instance count"),
			BuiltPlanet->ForestHISM->GetInstanceCount(),
			ForestCountBefore);
		TestEqual(TEXT("Style switch preserves rock instance count"),
			BuiltPlanet->RockHISM->GetInstanceCount(),
			RockCountBefore);
	}

	UABTSM3TerrainMaterialBridge* FallbackBridge =
		NewObject<UABTSM3TerrainMaterialBridge>(GetTransientPackage());
	TestFalse(
		TEXT("Unavailable terrain MID does not claim style readiness"),
		FallbackBridge != nullptr
			&& FallbackBridge->IsStylizedSurfaceContractAvailable());
	TestFalse(
		TEXT("Unavailable terrain style state rejects Style On without mutation"),
		FallbackBridge != nullptr
			&& FallbackBridge->ApplyStylizedSurfaceParameters(true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3StylizedBackgroundPropBindingTest,
	"ABTS.M3.StylizedMaterials.BackgroundPropBindingsDeterminismAndFailSoft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM3StylizedBackgroundPropBindingTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TestEqual(
		TEXT("M3BackgroundProp is owned by M3"),
		FABTSStylizedMaterialContract::ResolveOwner(
			EABTSStylizedMaterialFamily::M3BackgroundProp),
		EABTSStylizedMaterialOwner::M3);
	TestEqual(
		TEXT("M3BackgroundProp uses reversible slot override"),
		FABTSStylizedMaterialContract::ResolveAdoptionMode(
			EABTSStylizedMaterialFamily::M3BackgroundProp),
		EABTSStylizedMaterialAdoptionMode::ReversibleSlotOverride);

	AABTSM3Planet* Planet = NewObject<AABTSM3Planet>();
	TestNotNull(TEXT("Transient M3 planet exists"), Planet);
	if (Planet == nullptr
		|| Planet->ForestHISM == nullptr
		|| Planet->RockHISM == nullptr)
	{
		return false;
	}
	TestNotNull(TEXT("M3-owned forest style material loads"),
		Planet->ForestStylizedMaterial.Get());
	TestNotNull(TEXT("M3-owned rock style material loads"),
		Planet->RockStylizedMaterial.Get());
	if (Planet->ForestStylizedMaterial == nullptr
		|| Planet->RockStylizedMaterial == nullptr)
	{
		return false;
	}
	const FName BackgroundScalarParameters[] =
	{
		FABTSStylizedMaterialContract::GetStyleEnabledParameterName(),
		FABTSStylizedMaterialContract::GetRoughnessFloorParameterName(),
		FABTSStylizedMaterialContract::GetRoughnessScaleParameterName(),
		FABTSStylizedMaterialContract::GetSpecularScaleParameterName(),
		FABTSStylizedMaterialContract::GetMetallicScaleParameterName(),
		FABTSStylizedMaterialContract::GetRimStrengthParameterName(),
		FABTSStylizedMaterialContract::GetRimPowerParameterName()
	};
	UMaterialInterface* BackgroundMaterials[] =
	{
		Planet->ForestStylizedMaterial.Get(),
		Planet->RockStylizedMaterial.Get()
	};
	const FABTSStylizedSurfaceParameters BackgroundDefaults =
		FABTSStylizedMaterialContract::ResolveDefaultParameters(
			EABTSStylizedMaterialFamily::M3BackgroundProp);
	for (UMaterialInterface* Material : BackgroundMaterials)
	{
		for (const FName& Name : BackgroundScalarParameters)
		{
			TestTrue(
				FString::Printf(
					TEXT("%s exposes %s"),
					*GetNameSafe(Material),
					*Name.ToString()),
				ABTSM3StylizedMaterialTests::HasScalarParameter(
					*Material,
					Name));
		}
		TestTrue(
			FString::Printf(
				TEXT("%s exposes ABTS_BaseColorTint"),
				*GetNameSafe(Material)),
			ABTSM3StylizedMaterialTests::HasVectorParameter(
				*Material,
				FABTSStylizedMaterialContract::GetBaseColorTintParameterName()));
		float MaterialValue = 0.0f;
		TestTrue(TEXT("Background style asset defaults Style On"),
			Material->GetScalarParameterValue(
				FHashedMaterialParameterInfo(
					FABTSStylizedMaterialContract::GetStyleEnabledParameterName()),
				MaterialValue)
			&& FMath::IsNearlyEqual(MaterialValue, 1.0f));
		TestTrue(TEXT("Background style asset uses the public roughness floor"),
			Material->GetScalarParameterValue(
				FHashedMaterialParameterInfo(
					FABTSStylizedMaterialContract::GetRoughnessFloorParameterName()),
				MaterialValue)
			&& FMath::IsNearlyEqual(
				MaterialValue,
				BackgroundDefaults.RoughnessFloor));
		TestTrue(TEXT("Background style asset uses the public specular scale"),
			Material->GetScalarParameterValue(
				FHashedMaterialParameterInfo(
					FABTSStylizedMaterialContract::GetSpecularScaleParameterName()),
				MaterialValue)
			&& FMath::IsNearlyEqual(
				MaterialValue,
				BackgroundDefaults.SpecularScale));
	}

	Planet->ForestHISM->AddInstance(FTransform::Identity);
	Planet->ForestHISM->AddInstance(
		FTransform(FRotator(0.0, 20.0, 0.0), FVector(100.0, 0.0, 0.0)));
	Planet->RockHISM->AddInstance(
		FTransform(FRotator(0.0, 0.0, 15.0), FVector(0.0, 100.0, 0.0)));
	const int32 ForestCountBefore = Planet->ForestHISM->GetInstanceCount();
	const int32 RockCountBefore = Planet->RockHISM->GetInstanceCount();
	FTransform ForestTransformBefore;
	FTransform RockTransformBefore;
	Planet->ForestHISM->GetInstanceTransform(
		1,
		ForestTransformBefore,
		false);
	Planet->RockHISM->GetInstanceTransform(
		0,
		RockTransformBefore,
		false);
	int32 ForestStartCullBefore = 0;
	int32 ForestEndCullBefore = 0;
	int32 RockStartCullBefore = 0;
	int32 RockEndCullBefore = 0;
	Planet->ForestHISM->GetCullDistances(
		ForestStartCullBefore,
		ForestEndCullBefore);
	Planet->RockHISM->GetCullDistances(
		RockStartCullBefore,
		RockEndCullBefore);
	const ECollisionEnabled::Type ForestCollisionBefore =
		Planet->ForestHISM->GetCollisionEnabled();
	const ECollisionEnabled::Type RockCollisionBefore =
		Planet->RockHISM->GetCollisionEnabled();
	UMaterialInterface* ForestOriginalMaterial =
		Planet->ForestHISM->GetMaterial(0);
	UMaterialInterface* RockOriginalMaterial =
		Planet->RockHISM->GetMaterial(0);
	const int32 WorldSeedBefore = Planet->WorldSeed;
	const int64 LayoutHashBefore = Planet->PCGSummary.LayoutHash;
	const int32 TaskCountBefore = Planet->GetGeneratedTasks().Num();

	TArray<FABTSStylizedMaterialSlotBinding> First;
	TArray<FABTSStylizedMaterialSlotBinding> Second;
	FABTSM3StylizedMaterialAdapter::GatherBackgroundPropMaterialBindings(
		*Planet,
		First);
	FABTSM3StylizedMaterialAdapter::GatherBackgroundPropMaterialBindings(
		*Planet,
		Second);
	TestEqual(TEXT("Forest and rock publish two component-slot bindings"),
		First.Num(),
		2);
	TestEqual(TEXT("Repeated input keeps binding count"),
		Second.Num(),
		First.Num());
	for (int32 Index = 0; Index < First.Num() && Index < Second.Num(); ++Index)
	{
		TestTrue(TEXT("Published binding is valid"), First[Index].IsValid());
		TestTrue(TEXT("Repeated input keeps component identity"),
			First[Index].Component == Second[Index].Component);
		TestEqual(TEXT("Repeated input keeps slot identity"),
			First[Index].MaterialSlotIndex,
			Second[Index].MaterialSlotIndex);
		TestTrue(TEXT("Repeated input keeps style material identity"),
			First[Index].StylizedMaterial == Second[Index].StylizedMaterial);
		TestEqual(TEXT("Binding uses M3BackgroundProp"),
			First[Index].Family,
			EABTSStylizedMaterialFamily::M3BackgroundProp);
	}
	if (First.Num() == 2)
	{
		TestTrue(TEXT("Deterministic binding order starts with forest"),
			First[0].Component == Planet->ForestHISM);
		TestTrue(TEXT("Deterministic binding order ends with rock"),
			First[1].Component == Planet->RockHISM);
		TestEqual(TEXT("Forest binding uses slot zero"),
			First[0].MaterialSlotIndex,
			0);
		TestEqual(TEXT("Rock binding uses slot zero"),
			First[1].MaterialSlotIndex,
			0);
	}

	TestEqual(TEXT("Adapter preserves forest instance count"),
		Planet->ForestHISM->GetInstanceCount(),
		ForestCountBefore);
	TestEqual(TEXT("Adapter preserves rock instance count"),
		Planet->RockHISM->GetInstanceCount(),
		RockCountBefore);
	FTransform ForestTransformAfter;
	FTransform RockTransformAfter;
	Planet->ForestHISM->GetInstanceTransform(1, ForestTransformAfter, false);
	Planet->RockHISM->GetInstanceTransform(0, RockTransformAfter, false);
	TestTrue(TEXT("Adapter preserves forest transforms"),
		ForestTransformBefore.Equals(ForestTransformAfter));
	TestTrue(TEXT("Adapter preserves rock transforms"),
		RockTransformBefore.Equals(RockTransformAfter));
	int32 ForestStartCullAfter = 0;
	int32 ForestEndCullAfter = 0;
	int32 RockStartCullAfter = 0;
	int32 RockEndCullAfter = 0;
	Planet->ForestHISM->GetCullDistances(
		ForestStartCullAfter,
		ForestEndCullAfter);
	Planet->RockHISM->GetCullDistances(
		RockStartCullAfter,
		RockEndCullAfter);
	TestEqual(TEXT("Adapter preserves forest start cull distance"),
		ForestStartCullAfter,
		ForestStartCullBefore);
	TestEqual(TEXT("Adapter preserves forest end cull distance"),
		ForestEndCullAfter,
		ForestEndCullBefore);
	TestEqual(TEXT("Adapter preserves rock start cull distance"),
		RockStartCullAfter,
		RockStartCullBefore);
	TestEqual(TEXT("Adapter preserves rock end cull distance"),
		RockEndCullAfter,
		RockEndCullBefore);
	TestEqual(TEXT("Adapter preserves forest collision"),
		Planet->ForestHISM->GetCollisionEnabled(),
		ForestCollisionBefore);
	TestEqual(TEXT("Adapter preserves rock collision"),
		Planet->RockHISM->GetCollisionEnabled(),
		RockCollisionBefore);
	TestTrue(TEXT("Adapter does not apply the forest slot"),
		Planet->ForestHISM->GetMaterial(0) == ForestOriginalMaterial);
	TestTrue(TEXT("Adapter does not apply the rock slot"),
		Planet->RockHISM->GetMaterial(0) == RockOriginalMaterial);
	TestEqual(TEXT("Adapter preserves world seed"),
		Planet->WorldSeed,
		WorldSeedBefore);
	TestEqual(TEXT("Adapter preserves PCG layout hash"),
		Planet->PCGSummary.LayoutHash,
		LayoutHashBefore);
	TestEqual(TEXT("Adapter preserves TaskGraph count"),
		Planet->GetGeneratedTasks().Num(),
		TaskCountBefore);

	Planet->ForestStylizedMaterial = nullptr;
	TArray<FABTSStylizedMaterialSlotBinding> MissingForest;
	FABTSM3StylizedMaterialAdapter::GatherBackgroundPropMaterialBindings(
		*Planet,
		MissingForest);
	TestEqual(TEXT("Missing forest style asset publishes only rock"),
		MissingForest.Num(),
		1);
	if (MissingForest.Num() == 1)
	{
		TestTrue(TEXT("Remaining binding is the rock component"),
			MissingForest[0].Component == Planet->RockHISM);
	}
	Planet->RockStylizedMaterial = nullptr;
	TArray<FABTSStylizedMaterialSlotBinding> MissingAll;
	FABTSM3StylizedMaterialAdapter::GatherBackgroundPropMaterialBindings(
		*Planet,
		MissingAll);
	TestTrue(TEXT("Missing style assets publish no illegal binding"),
		MissingAll.IsEmpty());
	return true;
}

#endif
