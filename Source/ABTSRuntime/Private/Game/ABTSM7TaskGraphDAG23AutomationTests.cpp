// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSRuntime.h"
#include "Building/ABTSM73DAGBuildingPipeline.h"
#include "Building/ABTSM73StructureData.h"
#include "Game/ABTSM7GameMode.h"
#include "Misc/AutomationTest.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM7TaskGraphDAG23ProfileRoutingTest,
	"ABTS.M7.TaskGraphDAG23ProfileRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM7TaskGraphDAG23ProfileRoutingTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		EABTSM3TaskType TaskType;
		EABTSM7BuildingMaterial Material;
		EABTSM73DAGPreset Preset;
		int32 BuildingSeed;
		int32 MaxBrickCount;
		FVector LayoutSize;
		int32 ExpectedBrickCount;
		int32 ExpectedSupportCount;
		int32 ExpectedMacroCount;
		int32 ExpectedSparseCount;
		uint32 ExpectedTopologyHash;
	};

	const FCase Cases[] = {
		{EABTSM3TaskType::Workshop, EABTSM7BuildingMaterial::Wood,
			EABTSM73DAGPreset::SingleTower, 1034266606, 20, FVector(360.0f, 260.0f, 480.0f),
			13, 18, 4, 3, 2796521057u},
		{EABTSM3TaskType::TargetBuilding, EABTSM7BuildingMaterial::Stone,
			EABTSM73DAGPreset::TwinTowerBridge, 1034264727, 24, FVector(460.0f, 300.0f, 520.0f),
			17, 24, 5, 4, 1424001057u},
		{EABTSM3TaskType::FurnaceRuins, EABTSM7BuildingMaterial::Iron,
			EABTSM73DAGPreset::SingleTower, 1034267999, 20, FVector(400.0f, 280.0f, 480.0f),
			13, 18, 4, 3, 2796521057u}
	};

	FABTSM73DAGBuildingPipeline Pipeline;
	for (const FCase& TestCase : Cases)
	{
		FABTSM7TaskGraphBuildingProfile SerializedLegacy;
		SerializedLegacy.TaskType = TestCase.TaskType;
		SerializedLegacy.GenerationSettings.GenerationAlgorithm =
			EABTSM73GenerationAlgorithm::LegacyLayeredAB2;
		SerializedLegacy.GenerationSettings.PrimaryMaterial = TestCase.Material;
		SerializedLegacy.GenerationSettings.bGenerateStructuralWeakness = true;
		// This deliberately simulates the stale all-SingleTower DAG fields stored
		// by an older Blueprint CDO.
		SerializedLegacy.DAGGenerationSettings.Preset = EABTSM73DAGPreset::SingleTower;
		SerializedLegacy.DAGGenerationSettings.MaxExpansionDepth = 1;

		FABTSM7TaskGraphBuildingProfile Resolved;
		bool bMigratedLegacy = false;
		TestTrue(TEXT("Supported TaskGraph profile resolves"),
			FABTSM7TaskGraphDAG23ProfileResolver::ResolveRuntimeProfile(
				TestCase.TaskType, SerializedLegacy, Resolved, bMigratedLegacy));
		TestTrue(TEXT("Serialized Legacy profile is explicitly migrated"), bMigratedLegacy);
		TestEqual(TEXT("Production algorithm is RecursiveSupportDAG"),
			Resolved.GenerationSettings.GenerationAlgorithm,
			EABTSM73GenerationAlgorithm::RecursiveSupportDAG);
		TestFalse(TEXT("Retired B/B2 weakness flag is disabled"),
			Resolved.GenerationSettings.bGenerateStructuralWeakness);
		TestFalse(TEXT("Legacy migration never enables DAG3-C"),
			Resolved.DAGFailurePlayabilitySettings.bEnablePlayabilityRouting);
		TestEqual(TEXT("Task maps to its authored DAG preset"),
			Resolved.DAGGenerationSettings.Preset, TestCase.Preset);
		TestEqual(TEXT("Initial production profile has zero expansion budget"),
			Resolved.DAGGenerationSettings.ExpansionStepBudget, 0);
		TestEqual(TEXT("Initial production profile has zero expansion depth"),
			Resolved.DAGGenerationSettings.MaxExpansionDepth, 0);
		TestEqual(TEXT("No DAG-3 weakness budget is reserved"),
			Resolved.DAGGenerationSettings.ReservedWeaknessBrickCount, 0);
		TestEqual(TEXT("Estimated DAG body budget matches the runtime envelope"),
			Resolved.DAGGenerationSettings.MaxEstimatedBrickCount, TestCase.MaxBrickCount);
		TestEqual(TEXT("Required branches use conjunctive support"),
			Resolved.DAGGenerationSettings.DefaultParallelPolicy,
			EABTSM73DAGParallelPolicy::AllRequired);
		TestEqual(TEXT("Production layout uses tripod lowering"),
			Resolved.DAGLayoutSettings.SupportPattern,
			EABTSM73DAGSupportPattern::ThreeColumnTripod);
		TestEqual(TEXT("Task material survives migration"),
			Resolved.GenerationSettings.PrimaryMaterial, TestCase.Material);
		TestEqual(TEXT("Runtime physical brick envelope is explicit"),
			Resolved.GenerationSettings.MaxBrickCount, TestCase.MaxBrickCount);
		TestTrue(TEXT("DAG layout width matches the production envelope"),
			FMath::IsNearlyEqual(Resolved.DAGLayoutSettings.TargetWidthCM, TestCase.LayoutSize.X));
		TestTrue(TEXT("DAG layout depth matches the production envelope"),
			FMath::IsNearlyEqual(Resolved.DAGLayoutSettings.TargetDepthCM, TestCase.LayoutSize.Y));
		TestTrue(TEXT("DAG layout height matches the production envelope"),
			FMath::IsNearlyEqual(Resolved.DAGLayoutSettings.TargetHeightCM, TestCase.LayoutSize.Z));
		if (TestCase.TaskType == EABTSM3TaskType::FurnaceRuins)
		{
			TestTrue(TEXT("Furnace production profile keeps the iron contact-area floor"),
				Resolved.DAGLayoutSettings.MinSupportContactAreaRatio
				>= FABTSM7TaskGraphDAG23ProfileResolver::FurnaceMinSupportContactAreaRatio);
		}

		Resolved.GenerationSettings.BuildingSeed = TestCase.BuildingSeed;
		Resolved.DAGGenerationSettings.BuildingSeed = TestCase.BuildingSeed;
		FABTSM73StructureData Data;
		FString Error;
		const bool bBuilt = Pipeline.Build(
			Resolved.DAGGenerationSettings,
			Resolved.DAGLayoutSettings,
			Resolved.GenerationSettings,
			Data,
			Error);
		TestTrue(FString::Printf(TEXT("Production DAG profile builds: %s"), *Error), bBuilt);
		if (!bBuilt) continue;
		TestTrue(TEXT("Production DAG has macro nodes"), Data.DAGMacroNodeCount > 0);
		TestTrue(TEXT("Production DAG has selected supports"), Data.DAGSelectedSupportCount > 0);
		TestTrue(TEXT("Production DAG has a non-zero topology hash"), Data.DAGTopologyHash != 0);
		TestEqual(TEXT("Production DAG brick count matches the golden topology"),
			Data.Bricks.Num(), TestCase.ExpectedBrickCount);
		TestEqual(TEXT("Production DAG support count matches the golden topology"),
			Data.SupportEdges.Num(), TestCase.ExpectedSupportCount);
		TestEqual(TEXT("Production DAG macro count matches the golden topology"),
			Data.DAGMacroNodeCount, TestCase.ExpectedMacroCount);
		TestEqual(TEXT("Production DAG sparse support count matches the golden topology"),
			Data.DAGSelectedSupportCount, TestCase.ExpectedSparseCount);
		TestEqual(TEXT("Production DAG topology hash matches the golden topology"),
			Data.DAGTopologyHash, TestCase.ExpectedTopologyHash);
		TestEqual(TEXT("Production DAG has no missing required contacts"),
			Data.DAGMissingRequiredContactCount, 0);
		TestEqual(TEXT("Production DAG has no unexpected contact bypass"),
			Data.DAGUnexpectedBypassCount, 0);
		TestTrue(TEXT("Budget-zero compiled body count stays in the task envelope"),
			Data.Bricks.Num() <= TestCase.MaxBrickCount);
		if (TestCase.TaskType == EABTSM3TaskType::FurnaceRuins)
		{
			for (const FABTSM73DAGPhysicalSupportMapping& Mapping : Data.DAGPhysicalSupportMappings)
			{
				if (!Data.Bricks.IsValidIndex(Mapping.LoadPlateNodeId)) continue;
				const FABTSM73BrickNode& LoadPlate = Data.Bricks[Mapping.LoadPlateNodeId];
				const float ContactAreaRatio =
					Mapping.ColumnNodeIds.Num()
					* FMath::Square(Mapping.RealizedColumnWidthCM)
					/ (LoadPlate.DimensionsCM.X * LoadPlate.DimensionsCM.Y);
				TestTrue(TEXT("Every Furnace support realizes at least six percent contact area"),
					ContactAreaRatio + KINDA_SMALL_NUMBER
					>= FABTSM7TaskGraphDAG23ProfileResolver::FurnaceMinSupportContactAreaRatio);

				for (const int32 ColumnNodeId : Mapping.ColumnNodeIds)
				{
					if (!Data.Bricks.IsValidIndex(ColumnNodeId)) continue;
					const FABTSM73BrickNode& Column = Data.Bricks[ColumnNodeId];
					const float Slenderness = Column.DimensionsCM.Z
						/ FMath::Min(Column.DimensionsCM.X, Column.DimensionsCM.Y);
					TestTrue(TEXT("Furnace columns retain dynamic-stability slenderness headroom"),
						Slenderness <= 2.5f);
				}
			}
		}

		FABTSM73StructureData RepeatData;
		FString RepeatError;
		TestTrue(TEXT("Production DAG repeat build succeeds"),
			Pipeline.Build(
				Resolved.DAGGenerationSettings,
				Resolved.DAGLayoutSettings,
				Resolved.GenerationSettings,
				RepeatData,
				RepeatError));
		TestEqual(TEXT("Production DAG topology hash is deterministic"),
			RepeatData.DAGTopologyHash, Data.DAGTopologyHash);
		TestEqual(TEXT("Production DAG body count is deterministic"),
			RepeatData.Bricks.Num(), Data.Bricks.Num());
	}

	FABTSM7TaskGraphBuildingProfile AuthoredDAG =
		FABTSM7TaskGraphDAG23ProfileResolver::MakeDefaultProfile(
			EABTSM3TaskType::Workshop, EABTSM7BuildingMaterial::Wood);
	AuthoredDAG.DAGGenerationSettings.Preset = EABTSM73DAGPreset::Arch;
	AuthoredDAG.DAGLayoutSettings.TargetHeightCM = 333.0f;
	FABTSM7TaskGraphBuildingProfile PreservedDAG;
	bool bMigratedAuthoredDAG = true;
	TestTrue(TEXT("Explicitly authored DAG profile resolves"),
		FABTSM7TaskGraphDAG23ProfileResolver::ResolveRuntimeProfile(
			EABTSM3TaskType::Workshop, AuthoredDAG, PreservedDAG, bMigratedAuthoredDAG));
	TestFalse(TEXT("Explicitly authored DAG profile is not replaced"), bMigratedAuthoredDAG);
	TestEqual(TEXT("Explicit DAG preset remains editable"),
		PreservedDAG.DAGGenerationSettings.Preset, EABTSM73DAGPreset::Arch);
	TestTrue(TEXT("Explicit DAG layout remains editable"),
		FMath::IsNearlyEqual(PreservedDAG.DAGLayoutSettings.TargetHeightCM, 333.0f));
	TestFalse(TEXT("Default authored DAG profile keeps DAG3-C disabled"),
		PreservedDAG.DAGFailurePlayabilitySettings.bEnablePlayabilityRouting);

	FABTSM7TaskGraphBuildingProfile AuthoredDAG3C = AuthoredDAG;
	AuthoredDAG3C.DAGFailureFrontierSettings.bEnableAnalysis = true;
	AuthoredDAG3C.DAGFailureFrontierSettings.bEnableGeneralizedSmallCutSearch = true;
	AuthoredDAG3C.DAGFailurePatternSettings.bEnableGeometryRewrite = true;
	AuthoredDAG3C.DAGFailurePlayabilitySettings.bEnablePlayabilityRouting = true;
	FABTSM7TaskGraphBuildingProfile PreservedDAG3C;
	bool bMigratedDAG3C = true;
	TestTrue(TEXT("Explicit DAG3-C opt-in survives routing"),
		FABTSM7TaskGraphDAG23ProfileResolver::ResolveRuntimeProfile(
			EABTSM3TaskType::Workshop,
			AuthoredDAG3C,
			PreservedDAG3C,
			bMigratedDAG3C));
	TestFalse(TEXT("Explicit DAG3-C opt-in is not migrated"), bMigratedDAG3C);
	TestTrue(TEXT("Explicit DAG3-C opt-in is preserved"),
		PreservedDAG3C.DAGFailurePlayabilitySettings.bEnablePlayabilityRouting);

	FABTSM7TaskGraphBuildingProfile InvalidDAG3C = AuthoredDAG3C;
	InvalidDAG3C.DAGFailurePatternSettings.bEnableGeometryRewrite = false;
	FABTSM7TaskGraphBuildingProfile RejectedDAG3C;
	bool bRejectedDAG3CMigration = false;
	TestFalse(TEXT("DAG3-C without DAG3-B fails closed"),
		FABTSM7TaskGraphDAG23ProfileResolver::ResolveRuntimeProfile(
			EABTSM3TaskType::Workshop,
			InvalidDAG3C,
			RejectedDAG3C,
			bRejectedDAG3CMigration));

	FABTSM7TaskGraphBuildingProfile SerializedFurnaceDAG =
		FABTSM7TaskGraphDAG23ProfileResolver::MakeDefaultProfile(
			EABTSM3TaskType::FurnaceRuins, EABTSM7BuildingMaterial::Iron);
	SerializedFurnaceDAG.DAGLayoutSettings.MinSupportContactAreaRatio = 0.04f;
	FABTSM7TaskGraphBuildingProfile ResolvedFurnaceDAG;
	bool bMigratedFurnaceDAG = true;
	TestTrue(TEXT("Serialized explicit Furnace DAG profile resolves"),
		FABTSM7TaskGraphDAG23ProfileResolver::ResolveRuntimeProfile(
			EABTSM3TaskType::FurnaceRuins,
			SerializedFurnaceDAG,
			ResolvedFurnaceDAG,
			bMigratedFurnaceDAG));
	TestFalse(TEXT("Explicit Furnace DAG keeps its authored-profile identity"),
		bMigratedFurnaceDAG);
	TestTrue(TEXT("Runtime boundary upgrades stale Furnace contact area"),
		ResolvedFurnaceDAG.DAGLayoutSettings.MinSupportContactAreaRatio
			>= FABTSM7TaskGraphDAG23ProfileResolver::FurnaceMinSupportContactAreaRatio);

	FABTSM7TaskGraphBuildingProfile LaunchSiteProfile;
	LaunchSiteProfile.TaskType = EABTSM3TaskType::LaunchSite;
	FABTSM7TaskGraphBuildingProfile RejectedLaunchSite;
	bool bUnusedMigrationFlag = false;
	TestFalse(TEXT("LaunchSite can never resolve a building profile"),
		FABTSM7TaskGraphDAG23ProfileResolver::ResolveRuntimeProfile(
			EABTSM3TaskType::LaunchSite,
			LaunchSiteProfile,
			RejectedLaunchSite,
			bUnusedMigrationFlag));

	TestEqual(TEXT("No outstanding building validation permits WorldReady"),
		FABTSM6BuildingValidationGate::Classify(0, 0, 0),
		EABTSM6BuildingValidationGate::Ready);
	TestEqual(TEXT("Pending building validation blocks WorldReady"),
		FABTSM6BuildingValidationGate::Classify(1, 0, 0),
		EABTSM6BuildingValidationGate::Waiting);
	TestEqual(TEXT("Running building validation blocks WorldReady"),
		FABTSM6BuildingValidationGate::Classify(0, 1, 0),
		EABTSM6BuildingValidationGate::Waiting);
	TestEqual(TEXT("Rejected building validation is a fatal world gate"),
		FABTSM6BuildingValidationGate::Classify(0, 0, 1),
		EABTSM6BuildingValidationGate::Rejected);
	TestEqual(TEXT("Rejected takes priority over an unfinished validation"),
		FABTSM6BuildingValidationGate::Classify(1, 1, 1),
		EABTSM6BuildingValidationGate::Rejected);
	TestEqual(TEXT("An open required-building contract waits even with no actors yet"),
		FABTSM6BuildingValidationGate::Classify(0, 0, 0, true, false, false, 3, 0, 0, 0),
		EABTSM6BuildingValidationGate::Waiting);
	TestEqual(TEXT("A sealed required-building count mismatch is fatal"),
		FABTSM6BuildingValidationGate::Classify(0, 0, 0, true, true, false, 3, 2, 2, 0),
		EABTSM6BuildingValidationGate::Rejected);
	TestEqual(TEXT("A setup rejection is fatal even before all actors register"),
		FABTSM6BuildingValidationGate::Classify(1, 0, 0, true, false, true, 3, 2, 0, 0),
		EABTSM6BuildingValidationGate::Rejected);
	TestEqual(TEXT("A sealed exact required-building contract can become ready"),
		FABTSM6BuildingValidationGate::Classify(0, 0, 0, true, true, false, 3, 3, 3, 0),
		EABTSM6BuildingValidationGate::Ready);
	TestEqual(TEXT("A required actor cannot bypass validation as NotRequired"),
		FABTSM6BuildingValidationGate::Classify(0, 0, 0, true, true, false, 3, 3, 2, 1),
		EABTSM6BuildingValidationGate::Rejected);
	TestEqual(TEXT("Every required actor must explicitly reach Accepted"),
		FABTSM6BuildingValidationGate::Classify(0, 0, 0, true, true, false, 3, 3, 2, 0),
		EABTSM6BuildingValidationGate::Rejected);
	return true;
}

#endif
