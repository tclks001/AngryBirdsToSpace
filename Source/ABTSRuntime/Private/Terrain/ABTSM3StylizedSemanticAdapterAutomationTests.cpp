// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Terrain/ABTSM3StylizedSemanticAdapter.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "ProceduralMeshComponent.h"
#include "Terrain/ABTSM3Planet.h"
#include "World/ABTSM9Satellite.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3StylizedSemanticMappingTest,
	"ABTS.M3.StylizedSemantics.MappingDeterminismAndFailClosed",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3StylizedSemanticMappingTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	AABTSM3Planet* Planet = NewObject<AABTSM3Planet>();
	TestNotNull(TEXT("Transient M3 planet exists"), Planet);
	if (Planet == nullptr
		|| Planet->ContinuousSurface == nullptr
		|| Planet->ForestHISM == nullptr
		|| Planet->RockHISM == nullptr)
	{
		return false;
	}

	Planet->ForestHISM->AddInstance(FTransform::Identity);
	Planet->ForestHISM->AddInstance(FTransform(FVector(100.0f, 0.0f, 0.0f)));
	Planet->RockHISM->AddInstance(FTransform::Identity);
	Planet->RockHISM->AddInstance(FTransform(FVector(0.0f, 100.0f, 0.0f)));
	Planet->RockHISM->AddInstance(FTransform(FVector(0.0f, 0.0f, 100.0f)));

	struct FPrimitiveDepthState
	{
		bool bRenderCustomDepth = false;
		int32 StencilValue = 0;
	};
	const FPrimitiveDepthState SurfaceDepthState =
	{
		Planet->ContinuousSurface->bRenderCustomDepth != 0,
		Planet->ContinuousSurface->CustomDepthStencilValue
	};
	const FPrimitiveDepthState ForestDepthState =
	{
		Planet->ForestHISM->bRenderCustomDepth != 0,
		Planet->ForestHISM->CustomDepthStencilValue
	};
	const FPrimitiveDepthState RockDepthState =
	{
		Planet->RockHISM->bRenderCustomDepth != 0,
		Planet->RockHISM->CustomDepthStencilValue
	};
	const int32 WorldSeedBefore = Planet->WorldSeed;
	const int64 LayoutHashBefore = Planet->PCGSummary.LayoutHash;
	const int64 PreviewHashBefore =
		Planet->GetMonthlySatellitePreviewResult().ResultHash;
	const bool bPreviewAuthorityBefore =
		Planet->IsMonthlyPresentationPreviewActive();

	TArray<FABTSM3StylizedSemanticBinding> First;
	TArray<FABTSM3StylizedSemanticBinding> Second;
	FABTSM3StylizedSemanticAdapter::GatherPrimaryPlanetSemantics(
		*Planet,
		First);
	FABTSM3StylizedSemanticAdapter::GatherPrimaryPlanetSemantics(
		*Planet,
		Second);
	TestEqual(TEXT("Planet publishes one surface and two component batches"),
		First.Num(),
		3);
	TestEqual(TEXT("Repeated semantic query has stable cardinality"),
		Second.Num(),
		First.Num());
	for (int32 Index = 0; Index < First.Num() && Index < Second.Num(); ++Index)
	{
		TestTrue(TEXT("Every planet semantic is valid"), First[Index].IsValid());
		TestTrue(TEXT("Repeated semantic query keeps the component"),
			First[Index].Component == Second[Index].Component);
		TestEqual(TEXT("Repeated semantic query keeps the class"),
			First[Index].ObjectClass,
			Second[Index].ObjectClass);
		TestEqual(TEXT("Repeated semantic query keeps the source"),
			First[Index].Source,
			Second[Index].Source);
		TestEqual(TEXT("Repeated semantic query keeps the represented count"),
			First[Index].RepresentedInstanceCount,
			Second[Index].RepresentedInstanceCount);
	}
	if (First.Num() == 3)
	{
		TestEqual(TEXT("Continuous terrain, roads and water publish WorldSurface"),
			First[0].ObjectClass,
			EABTSStylizedObjectClass::WorldSurface);
		TestEqual(TEXT("Forest HISM publishes BackgroundProp"),
			First[1].ObjectClass,
			EABTSStylizedObjectClass::BackgroundProp);
		TestEqual(TEXT("Rock HISM publishes BackgroundProp"),
			First[2].ObjectClass,
			EABTSStylizedObjectClass::BackgroundProp);
		TestEqual(TEXT("Forest is one component-batch registration"),
			First[1].Granularity,
			EABTSM3StylizedSemanticGranularity::ComponentBatch);
		TestEqual(TEXT("Rock is one component-batch registration"),
			First[2].Granularity,
			EABTSM3StylizedSemanticGranularity::ComponentBatch);
		TestEqual(TEXT("Forest batch reports instances without per-instance entries"),
			First[1].RepresentedInstanceCount,
			2);
		TestEqual(TEXT("Rock batch reports instances without per-instance entries"),
			First[2].RepresentedInstanceCount,
			3);
	}

	UStaticMeshComponent* UnknownComponent =
		NewObject<UStaticMeshComponent>(Planet);
	FABTSM3StylizedSemanticBinding UnknownBinding;
	TestFalse(TEXT("Unknown planet component fails closed"),
		UnknownComponent != nullptr
			&& FABTSM3StylizedSemanticAdapter::TryResolveComponent(
				*Planet,
				*UnknownComponent,
				UnknownBinding));
	TestFalse(TEXT("Failed component resolution returns no semantic"),
		UnknownBinding.IsValid());

	AABTSM9Satellite* Satellite = NewObject<AABTSM9Satellite>();
	TArray<FABTSM3StylizedSemanticBinding> SatelliteBindings;
	FABTSM3StylizedSemanticAdapter::GatherSatelliteSemantics(
		*Satellite,
		SatelliteBindings);
	TestEqual(TEXT("M9 satellite publishes one actor semantic"),
		SatelliteBindings.Num(),
		1);
	if (SatelliteBindings.Num() == 1)
	{
		TestEqual(TEXT("M9 satellite is a SatelliteTarget"),
			SatelliteBindings[0].ObjectClass,
			EABTSStylizedObjectClass::SatelliteTarget);
		TestEqual(TEXT("M9 satellite semantic stays actor-granular"),
			SatelliteBindings[0].Granularity,
			EABTSM3StylizedSemanticGranularity::Actor);
	}
	FABTSM3StylizedSemanticBinding CrossAuthorityBinding;
	TestFalse(TEXT("M9 actor under an unrelated authority fails closed"),
		FABTSM3StylizedSemanticAdapter::TryResolveActor(
			*Planet,
			*Satellite,
			CrossAuthorityBinding));

	TestEqual(TEXT("Semantic query preserves world seed"),
		Planet->WorldSeed,
		WorldSeedBefore);
	TestEqual(TEXT("Semantic query preserves compatibility layout hash"),
		Planet->PCGSummary.LayoutHash,
		LayoutHashBefore);
	TestEqual(TEXT("Semantic query preserves satellite preview hash"),
		Planet->GetMonthlySatellitePreviewResult().ResultHash,
		PreviewHashBefore);
	TestEqual(TEXT("Semantic query preserves preview authority"),
		Planet->IsMonthlyPresentationPreviewActive(),
		bPreviewAuthorityBefore);
	TestEqual(TEXT("Surface custom-depth enablement is unchanged"),
		Planet->ContinuousSurface->bRenderCustomDepth != 0,
		SurfaceDepthState.bRenderCustomDepth);
	TestEqual(TEXT("Surface stencil state is unchanged"),
		Planet->ContinuousSurface->CustomDepthStencilValue,
		SurfaceDepthState.StencilValue);
	TestEqual(TEXT("Forest custom-depth enablement is unchanged"),
		Planet->ForestHISM->bRenderCustomDepth != 0,
		ForestDepthState.bRenderCustomDepth);
	TestEqual(TEXT("Forest stencil state is unchanged"),
		Planet->ForestHISM->CustomDepthStencilValue,
		ForestDepthState.StencilValue);
	TestEqual(TEXT("Rock custom-depth enablement is unchanged"),
		Planet->RockHISM->bRenderCustomDepth != 0,
		RockDepthState.bRenderCustomDepth);
	TestEqual(TEXT("Rock stencil state is unchanged"),
		Planet->RockHISM->CustomDepthStencilValue,
		RockDepthState.StencilValue);
	return true;
}

#endif
