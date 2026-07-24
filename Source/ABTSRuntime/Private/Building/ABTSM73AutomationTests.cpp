// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73StabilityValidator.h"
#include "Building/ABTSM73StructureBuilder.h"
#include "Building/ABTSM73StructureData.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DefaultStructuresTest,
	"ABTS.M73A.DefaultStructuresAreStaticallyStable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DefaultStructuresTest::RunTest(const FString& Parameters)
{
	FABTSM73StructureBuilder Builder;
	FABTSM73StabilityValidator Validator;
	for (const EABTSM73Silhouette Silhouette : {
		EABTSM73Silhouette::SingleTower,
		EABTSM73Silhouette::Gatehouse,
		EABTSM73Silhouette::TwinTowerBridge})
	{
		FABTSM73GenerationSettings Settings;
		Settings.Silhouette = Silhouette;
		FABTSM73StructureData Data;
		FString Error;
		const bool bBuilt = Builder.Build(Settings, Data, Error);
		TestTrue(FString::Printf(TEXT("Silhouette %d builds: %s"), static_cast<int32>(Silhouette), *Error), bBuilt);
		if (!bBuilt) continue;
		TestTrue(TEXT("Brick budget respected"), Data.Bricks.Num() <= Settings.MaxBrickCount);
		TestTrue(TEXT("Ground nodes generated"), !Data.GroundNodeIds.IsEmpty());
		TestTrue(TEXT("Support edges generated"), !Data.SupportEdges.IsEmpty());
		const bool bStable = Validator.Validate(Settings, Data, Error);
		TestTrue(FString::Printf(TEXT("Silhouette %d validates: %s"), static_cast<int32>(Silhouette), *Error), bStable);
	}
	return true;
}

#endif

