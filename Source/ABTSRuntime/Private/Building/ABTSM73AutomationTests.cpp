// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73StabilityValidator.h"
#include "Building/ABTSM73StructureBuilder.h"
#include "Building/ABTSM73StructureData.h"
#include "Building/ABTSM73WeakPointPlanner.h"
#include "Building/ABTSM7MaterialProfileLibrary.h"
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
		Settings.bGenerateStructuralWeakness = false;
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

	// Guard the deliberate 50-body budget behavior. These exact counts explain
	// why a rejected preview disappears instead of indicating an HISM/render bug.
	FABTSM73GenerationSettings BudgetSettings;
	BudgetSettings.Levels = 5;
	BudgetSettings.MaxBrickCount = 50;
	FABTSM73StructureData BudgetData;
	FString BudgetError;
	BudgetSettings.Silhouette = EABTSM73Silhouette::Gatehouse;
	TestFalse(TEXT("Five-level Gatehouse exceeds the default budget"), Builder.Build(BudgetSettings, BudgetData, BudgetError));
	TestEqual(TEXT("Gatehouse budget diagnostic is stable"), BudgetError, FString(TEXT("BrickBudgetExceeded:51:50")));
	BudgetSettings.Silhouette = EABTSM73Silhouette::TwinTowerBridge;
	BudgetError.Reset();
	TestFalse(TEXT("Five-level corridor bridge exceeds the default budget"), Builder.Build(BudgetSettings, BudgetData, BudgetError));
	TestEqual(TEXT("Twin bridge budget diagnostic is stable"), BudgetError, FString(TEXT("BrickBudgetExceeded:53:50")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73WeakPointPlannerTest,
	"ABTS.M73B.WeakPointPlanner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73WeakPointPlannerTest::RunTest(const FString& Parameters)
{
	const TArray<FABTSM7MaterialProfile> Profiles = FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
	const auto Effort = [&Profiles](const EABTSM7BuildingMaterial Material)
	{
		const FABTSM7MaterialProfile* Profile = FABTSM7MaterialProfileLibrary::FindProfile(Profiles, Material);
		return Profile != nullptr ? FABTSM7MaterialProfileLibrary::ComputeBreakEffort(*Profile) : BIG_NUMBER;
	};
	TestTrue(TEXT("Default profile effort Glass < Wood"), Effort(EABTSM7BuildingMaterial::Glass) < Effort(EABTSM7BuildingMaterial::Wood));
	TestTrue(TEXT("Default profile effort Wood < Stone"), Effort(EABTSM7BuildingMaterial::Wood) < Effort(EABTSM7BuildingMaterial::Stone));
	TestTrue(TEXT("Default profile effort Stone < Iron"), Effort(EABTSM7BuildingMaterial::Stone) < Effort(EABTSM7BuildingMaterial::Iron));

	FABTSM73StructureBuilder Builder;
	FABTSM73StabilityValidator Validator;
	FABTSM73WeakPointPlanner Planner;
	FABTSM73DifficultySettings Difficulty;
	for (const EABTSM73Silhouette Silhouette : {
		EABTSM73Silhouette::SingleTower,
		EABTSM73Silhouette::Gatehouse,
		EABTSM73Silhouette::TwinTowerBridge})
	{
		FABTSM73GenerationSettings Settings;
		Settings.Silhouette = Silhouette;
		FABTSM73StructureData Data;
		FString Error;
		TestTrue(TEXT("Base structure builds for weak-point planning"), Builder.Build(Settings, Data, Error));
		const bool bPlanned = Planner.Plan(Difficulty, Profiles, FVector::ForwardVector, Settings.BuildingSeed, Data, Error);
		TestTrue(FString::Printf(TEXT("Silhouette %d weak-point plan: %s"), static_cast<int32>(Silhouette), *Error), bPlanned);
		if (!bPlanned) continue;
		TestEqual(TEXT("Requested weak-point count selected"), Data.WeakPoints.Num(), Difficulty.WeakPointCount);
		TestTrue(TEXT("Planner reports a finite score"), FMath::IsFinite(Data.DifficultyScore));
		TestTrue(TEXT("Planner reports structural weak effect"), Data.PredictedWeakCollapseRatio >= Difficulty.MinWeakCollapseRatio);
		TestTrue(TEXT("Ordinary graph probe stays inside resistance window"), Data.PredictedNonWeakEffect <= Difficulty.MaxNonWeakEffect + KINDA_SMALL_NUMBER);
		const float WeakEffect = Data.PredictedWeakCollapseRatio / FMath::Max(1, Data.EstimatedWeakPointHits);
		AddInfo(FString::Printf(
			TEXT("Silhouette=%d WeakNode=%d WeakCollapse=%.4f NonWeakEffect=%.4f Hits=%d Reinforced=%d Difficulty=%.4f"),
			static_cast<int32>(Silhouette), Data.WeakPoints.IsEmpty() ? INDEX_NONE : Data.WeakPoints[0].NodeId,
			Data.PredictedWeakCollapseRatio, Data.PredictedNonWeakEffect, Data.EstimatedWeakPointHits,
			Data.ReinforcedNodeIds.Num(), Data.DifficultyScore));
		TestTrue(TEXT("Weak hit has configured advantage over ordinary hit"),
			Data.PredictedNonWeakEffect <= KINDA_SMALL_NUMBER
			|| WeakEffect + KINDA_SMALL_NUMBER >= Data.PredictedNonWeakEffect * Difficulty.MinWeakPointAdvantage);
		for (const FABTSM73WeakPointRecord& WeakPoint : Data.WeakPoints)
		{
			const FABTSM73BrickNode* Node = Data.Bricks.FindByPredicate([&WeakPoint](const FABTSM73BrickNode& Candidate)
			{
				return Candidate.NodeId == WeakPoint.NodeId;
			});
			TestNotNull(TEXT("Weak-point node id resolves"), Node);
			if (Node == nullptr) continue;
			TestTrue(TEXT("Weak-point metadata reaches the brick node"), Node->bWeakPoint);
			TestEqual(TEXT("Default one-tier weak point uses actual easiest profile"), Node->Material, EABTSM7BuildingMaterial::Glass);
			TestTrue(TEXT("Weak point is visible from attack direction"), WeakPoint.Exposure >= Difficulty.MinWeakPointExposure);
			TestTrue(TEXT("Weak point destabilizes a non-empty authored load set"), !WeakPoint.AffectedNodeIds.IsEmpty());
		}
		Error.Reset();
		const bool bStillStable = Validator.Validate(Settings, Data, Error);
		TestTrue(FString::Printf(TEXT("Material reassignment remains statically valid: %s"), *Error), bStillStable);

		FABTSM73StructureData DeterministicData;
		TestTrue(TEXT("Determinism rebuild succeeds"), Builder.Build(Settings, DeterministicData, Error));
		TestTrue(TEXT("Determinism replan succeeds"), Planner.Plan(Difficulty, Profiles, FVector::ForwardVector,
			Settings.BuildingSeed, DeterministicData, Error));
		TestEqual(TEXT("Deterministic weak-point count"), DeterministicData.WeakPoints.Num(), Data.WeakPoints.Num());
		for (int32 Index = 0; Index < Data.Bricks.Num() && DeterministicData.Bricks.IsValidIndex(Index); ++Index)
		{
			TestEqual(TEXT("Deterministic node id"), DeterministicData.Bricks[Index].NodeId, Data.Bricks[Index].NodeId);
			TestEqual(TEXT("Deterministic material assignment"), DeterministicData.Bricks[Index].Material, Data.Bricks[Index].Material);
			TestEqual(TEXT("Deterministic weak-point flag"), DeterministicData.Bricks[Index].bWeakPoint, Data.Bricks[Index].bWeakPoint);
		}
	}

	FABTSM73GenerationSettings ImpossibleGeneration;
	FABTSM73StructureData ImpossibleData;
	FString ImpossibleError;
	TestTrue(TEXT("Impossible-window base structure builds"), Builder.Build(ImpossibleGeneration, ImpossibleData, ImpossibleError));
	FABTSM73DifficultySettings ImpossibleDifficulty;
	ImpossibleDifficulty.MinWeakCollapseRatio = 0.95f;
	ImpossibleDifficulty.TargetWeakCollapseRatio = 0.98f;
	ImpossibleDifficulty.MaxSingleWeakCollapseRatio = 1.0f;
	TestFalse(TEXT("Impossible difficulty window is rejected transactionally"), Planner.Plan(
		ImpossibleDifficulty, Profiles, FVector::ForwardVector, ImpossibleGeneration.BuildingSeed, ImpossibleData, ImpossibleError));
	TestTrue(TEXT("Rejected plan has a diagnostic"), !ImpossibleError.IsEmpty());
	TestTrue(TEXT("Rejected plan leaves no partial weak-point records"), ImpossibleData.WeakPoints.IsEmpty());
	for (const FABTSM73BrickNode& Node : ImpossibleData.Bricks)
	{
		TestEqual(TEXT("Rejected plan leaves original material"), Node.Material, Node.OriginalMaterial);
		TestFalse(TEXT("Rejected plan leaves no weak-point flag"), Node.bWeakPoint);
	}
	return true;
}

#endif
