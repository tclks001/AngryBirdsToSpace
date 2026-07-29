// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSRuntime.h"
#include "Building/ABTSM7MaterialProfileLibrary.h"
#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73DAG5Types.h"
#include "Building/ABTSM73DAGBuildingPipeline.h"
#include "Building/ABTSM73DAGFailureFrontierTypes.h"
#include "Building/ABTSM73DAGTypes.h"
#include "Building/ABTSM73StructureData.h"
#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FDAG5ATestFixture
	{
		FABTSM73DAGGenerationSettings DAGSettings;
		FABTSM73DAGLayoutSettings LayoutSettings;
		FABTSM73GenerationSettings BuildingSettings;
		FABTSM73DAGFailureFrontierSettings FrontierSettings;
		FABTSM73DAGFailurePatternSettings PatternSettings;
		FABTSM73DAGFailurePlayabilitySettings PlayabilitySettings;
		FABTSM73DifficultySettings DifficultySettings;
		TArray<FABTSM7MaterialProfile> MaterialProfiles =
			FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();

		FDAG5ATestFixture()
		{
			DAGSettings.Preset = EABTSM73DAGPreset::SingleTower;
			DAGSettings.BuildingSeed = 735001;
			DAGSettings.MinExpansionDepth = 0;
			DAGSettings.MaxExpansionDepth = 0;
			DAGSettings.ExpansionStepBudget = 0;
			DAGSettings.MaxAbstractNodeCount = 64;
			DAGSettings.MaxEstimatedBrickCount = 64;
			DAGSettings.ReservedWeaknessBrickCount = 0;

			BuildingSettings.GenerationAlgorithm =
				EABTSM73GenerationAlgorithm::RecursiveSupportDAG;
			BuildingSettings.bGenerateStructuralWeakness = false;
			BuildingSettings.MaxBrickCount = 64;
		}
	};

	bool RunDAG5ASearch(
		const FDAG5ATestFixture& Fixture,
		const FABTSM73DAG5ASettings& SearchSettings,
		FABTSM73DAG5AResult& OutResult,
		FABTSM73StructureData& OutData,
		FString& OutError,
		const FVector& LocalAttackDirection = FVector::ForwardVector)
	{
		FABTSM73DAGBuildingPipeline Pipeline;
		return Pipeline.BuildWithFeasibilitySearch(
			SearchSettings,
			Fixture.DAGSettings,
			Fixture.LayoutSettings,
			Fixture.BuildingSettings,
			Fixture.FrontierSettings,
			Fixture.PatternSettings,
			Fixture.PlayabilitySettings,
			Fixture.DifficultySettings,
			Fixture.MaterialProfiles,
			LocalAttackDirection,
			OutResult,
			OutData,
			OutError);
	}

	bool IsDAG5AAtomicEmpty(const FABTSM73StructureData& Data)
	{
		return Data.Bricks.IsEmpty()
			&& Data.SupportEdges.IsEmpty()
			&& Data.DAGPhysicalSupportMappings.IsEmpty()
			&& Data.GroundNodeIds.IsEmpty()
			&& Data.StructuralWeaknessIntents.IsEmpty()
			&& Data.FailureProbeResults.IsEmpty()
			&& Data.WeakPoints.IsEmpty()
			&& Data.ReinforcedNodeIds.IsEmpty()
			&& Data.GroundSupportPoints.IsEmpty()
			&& Data.GroundSamples.IsEmpty()
			&& Data.FoundationFeet.IsEmpty()
			&& Data.DAGMacroNodeCount == 0
			&& Data.DAGSelectedSupportCount == 0
			&& Data.DAGTopologyHash == 0;
	}

	bool EqualDAG5ABrick(
		const FABTSM73BrickNode& A,
		const FABTSM73BrickNode& B)
	{
		return A.NodeId == B.NodeId
			&& A.MacroNodeId == B.MacroNodeId
			&& A.Material == B.Material
			&& A.OriginalMaterial == B.OriginalMaterial
			&& A.LocalCenter.Equals(B.LocalCenter, KINDA_SMALL_NUMBER)
			&& A.DimensionsCM.Equals(B.DimensionsCM, KINDA_SMALL_NUMBER)
			&& A.SemanticRole == B.SemanticRole
			&& A.StoreyIndex == B.StoreyIndex
			&& A.BayIndex == B.BayIndex
			&& A.WeakPointRole == B.WeakPointRole
			&& FMath::IsNearlyEqual(
				A.WeakPointScore,
				B.WeakPointScore,
				KINDA_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(
				A.UnsupportedMassRatio,
				B.UnsupportedMassRatio,
				KINDA_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(
				A.AttackExposure,
				B.AttackExposure,
				KINDA_SMALL_NUMBER)
			&& A.EstimatedHits == B.EstimatedHits
			&& A.bFailureFrontierMainBody == B.bFailureFrontierMainBody
			&& A.bWeakPoint == B.bWeakPoint
			&& A.bReinforcedCriticalNode == B.bReinforcedCriticalNode;
	}

	bool EqualDAG5AMapping(
		const FABTSM73DAGPhysicalSupportMapping& A,
		const FABTSM73DAGPhysicalSupportMapping& B)
	{
		return A.SupportMacroNodeId == B.SupportMacroNodeId
			&& A.LoadMacroNodeId == B.LoadMacroNodeId
			&& A.SupportPlateNodeId == B.SupportPlateNodeId
			&& A.LoadPlateNodeId == B.LoadPlateNodeId
			&& A.SupportPattern == B.SupportPattern
			&& FMath::IsNearlyEqual(
				A.RealizedColumnWidthCM,
				B.RealizedColumnWidthCM,
				KINDA_SMALL_NUMBER)
			&& A.ColumnNodeIds == B.ColumnNodeIds
			&& A.ColumnRoles == B.ColumnRoles;
	}

	bool EqualDAG5AGeometry(
		const FABTSM73StructureData& A,
		const FABTSM73StructureData& B)
	{
		if (A.DAGTopologyHash != B.DAGTopologyHash
			|| A.DAGMacroNodeCount != B.DAGMacroNodeCount
			|| A.DAGSelectedSupportCount != B.DAGSelectedSupportCount
			|| A.DAGMissingRequiredContactCount
				!= B.DAGMissingRequiredContactCount
			|| A.DAGUnexpectedBypassCount != B.DAGUnexpectedBypassCount
			|| A.Bricks.Num() != B.Bricks.Num()
			|| A.SupportEdges.Num() != B.SupportEdges.Num()
			|| A.DAGPhysicalSupportMappings.Num()
				!= B.DAGPhysicalSupportMappings.Num()
			|| A.GroundNodeIds != B.GroundNodeIds
			|| A.GroundSupportPoints != B.GroundSupportPoints
			|| !A.LocalBounds.Min.Equals(
				B.LocalBounds.Min,
				KINDA_SMALL_NUMBER)
			|| !A.LocalBounds.Max.Equals(
				B.LocalBounds.Max,
				KINDA_SMALL_NUMBER)
			|| !A.FootprintHalfExtent.Equals(
				B.FootprintHalfExtent,
				KINDA_SMALL_NUMBER)
			|| !FMath::IsNearlyEqual(
				A.DAGMinSupportContactAreaRatio,
				B.DAGMinSupportContactAreaRatio,
				KINDA_SMALL_NUMBER))
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Bricks.Num(); ++Index)
		{
			if (!EqualDAG5ABrick(A.Bricks[Index], B.Bricks[Index]))
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < A.SupportEdges.Num(); ++Index)
		{
			const FABTSM73SupportEdge& EdgeA = A.SupportEdges[Index];
			const FABTSM73SupportEdge& EdgeB = B.SupportEdges[Index];
			if (EdgeA.LowerNodeId != EdgeB.LowerNodeId
				|| EdgeA.UpperNodeId != EdgeB.UpperNodeId
				|| !FMath::IsNearlyEqual(
					EdgeA.ContactAreaCM2,
					EdgeB.ContactAreaCM2,
					KINDA_SMALL_NUMBER))
			{
				return false;
			}
		}
		for (int32 Index = 0;
			Index < A.DAGPhysicalSupportMappings.Num();
			++Index)
		{
			if (!EqualDAG5AMapping(
				A.DAGPhysicalSupportMappings[Index],
				B.DAGPhysicalSupportMappings[Index]))
			{
				return false;
			}
		}
		return true;
	}

	bool EqualDAG5AAttempts(
		const FABTSM73DAG5AResult& A,
		const FABTSM73DAG5AResult& B)
	{
		if (A.Attempts.Num() != B.Attempts.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Attempts.Num(); ++Index)
		{
			const FABTSM73DAG5AAttemptResult& AttemptA = A.Attempts[Index];
			const FABTSM73DAG5AAttemptResult& AttemptB = B.Attempts[Index];
			if (AttemptA.AttemptIndex != AttemptB.AttemptIndex
				|| AttemptA.CandidateSeed != AttemptB.CandidateSeed
				|| AttemptA.TopologyHash != AttemptB.TopologyHash
				|| AttemptA.CompiledBrickCount != AttemptB.CompiledBrickCount
				|| AttemptA.bAccepted != AttemptB.bAccepted
				|| AttemptA.RejectStage != AttemptB.RejectStage
				|| AttemptA.RejectCode != AttemptB.RejectCode
				|| AttemptA.RejectReason != AttemptB.RejectReason)
			{
				return false;
			}
		}
		return true;
	}

	void AddDAG5ASentinel(FABTSM73StructureData& Data)
	{
		FABTSM73BrickNode& Node = Data.Bricks.AddDefaulted_GetRef();
		Node.NodeId = 42;
		Data.DAGMacroNodeCount = 1;
		Data.DAGTopologyHash = 1;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5AAttemptZeroDeterminismTest,
	"ABTS.M73DAG.DAG5A.AttemptZeroDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5AAttemptZeroDeterminismTest::RunTest(
	const FString& Parameters)
{
	const FDAG5ATestFixture Fixture;
	FABTSM73DAG5ASettings SearchSettings;
	SearchSettings.bEnableFeasibilitySearch = true;
	SearchSettings.MaxCandidateAttempts = 4;
	SearchSettings.MaxCompiledBrickCount = 13;

	FABTSM73DAG5AResult FirstResult;
	FABTSM73StructureData FirstData;
	FString FirstError;
	const bool bFirstBuilt = RunDAG5ASearch(
		Fixture,
		SearchSettings,
		FirstResult,
		FirstData,
		FirstError);
	TestTrue(
		FString::Printf(
			TEXT("DAG5-A attempt zero is feasible: %s"),
			*FirstError),
		bFirstBuilt);
	TestTrue(TEXT("DAG5-A accepts the feasible candidate"), FirstResult.bAccepted);
	TestEqual(TEXT("DAG5-A accepts attempt zero"), FirstResult.SelectedAttemptIndex, 0);
	TestEqual(TEXT("DAG5-A stops after the accepted attempt"), FirstResult.AttemptCount, 1);
	TestEqual(TEXT("SingleTower depth zero compiles exactly 13 bricks"), FirstData.Bricks.Num(), 13);
	TestEqual(TEXT("DAG5-A records the caller's input seed"),
		FirstResult.InputSeed, Fixture.DAGSettings.BuildingSeed);
	TestTrue(TEXT("DAG5-A records exactly one attempt"), FirstResult.Attempts.Num() == 1);
	if (FirstResult.Attempts.Num() == 1)
	{
		TestEqual(TEXT("Attempt zero preserves the input seed exactly"),
			FirstResult.Attempts[0].CandidateSeed,
			Fixture.DAGSettings.BuildingSeed);
	}
	TestTrue(
		TEXT("Accepted attempt count remains within the hard budget"),
		FirstResult.AttemptCount <= SearchSettings.MaxCandidateAttempts);

	FABTSM73DAGBuildingPipeline Pipeline;
	FABTSM73StructureData OneShotData;
	FString OneShotError;
	const bool bOneShotBuilt = Pipeline.BuildWithFailurePattern(
		Fixture.DAGSettings,
		Fixture.LayoutSettings,
		Fixture.BuildingSettings,
		Fixture.FrontierSettings,
		Fixture.PatternSettings,
		Fixture.PlayabilitySettings,
		Fixture.DifficultySettings,
		Fixture.MaterialProfiles,
		FVector::ForwardVector,
		OneShotData,
		OneShotError);
	TestTrue(
		FString::Printf(
			TEXT("Legacy one-shot candidate succeeds: %s"),
			*OneShotError),
		bOneShotBuilt);
	TestTrue(
		TEXT("Attempt-zero search is fully geometry-identical to old one-shot"),
		EqualDAG5AGeometry(OneShotData, FirstData));

	FABTSM73DAG5AResult RepeatResult;
	FABTSM73StructureData RepeatData;
	FString RepeatError;
	const bool bRepeatBuilt = RunDAG5ASearch(
		Fixture,
		SearchSettings,
		RepeatResult,
		RepeatData,
		RepeatError);
	TestTrue(
		FString::Printf(
			TEXT("DAG5-A deterministic repeat succeeds: %s"),
			*RepeatError),
		bRepeatBuilt);
	TestEqual(
		TEXT("DAG5-A repeat search hash is stable"),
		RepeatResult.SearchHash,
		FirstResult.SearchHash);
	TestEqual(
		TEXT("DAG5-A repeat selects the same seed"),
		RepeatResult.SelectedCandidateSeed,
		FirstResult.SelectedCandidateSeed);
	TestTrue(
		TEXT("DAG5-A repeat preserves the complete attempt trace"),
		EqualDAG5AAttempts(FirstResult, RepeatResult));
	TestTrue(
		TEXT("DAG5-A repeat preserves compiled geometry"),
		EqualDAG5AGeometry(FirstData, RepeatData));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5AMinimumDepthCapacityTest,
	"ABTS.M73DAG.DAG5A.MinimumDepthCapacityPreflight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5AMinimumDepthCapacityTest::RunTest(
	const FString& Parameters)
{
	FDAG5ATestFixture Fixture;
	Fixture.DAGSettings.MinExpansionDepth = 2;
	Fixture.DAGSettings.MaxExpansionDepth = 2;
	Fixture.DAGSettings.ExpansionStepBudget = 11;

	FABTSM73DAG5ASettings SearchSettings;
	SearchSettings.bEnableFeasibilitySearch = true;
	SearchSettings.MaxCandidateAttempts = 7;

	FABTSM73DAG5AResult FirstResult;
	FABTSM73StructureData FirstData;
	AddDAG5ASentinel(FirstData);
	FString FirstError;
	const bool bFirstBuilt = RunDAG5ASearch(
		Fixture,
		SearchSettings,
		FirstResult,
		FirstData,
		FirstError);
	TestFalse(TEXT("Impossible minimum depth is rejected before search"), bFirstBuilt);
	TestTrue(
		TEXT("Minimum-depth rejection uses the stable preflight prefix"),
		FirstError.StartsWith(
			TEXT("DAG5APreflightRejected:DAG5AMinimumExpansionStepCapacityExceeded")));
	TestEqual(TEXT("Capacity rejection attempts no candidates"), FirstResult.AttemptCount, 0);
	TestEqual(TEXT("Depth two requires 12 expansion steps"), FirstResult.RequiredMinimumExpansionSteps, 12);
	TestTrue(TEXT("Preflight failure clears sentinel output atomically"), IsDAG5AAtomicEmpty(FirstData));
	TestTrue(
		TEXT("Preflight attempt count remains within the hard budget"),
		FirstResult.AttemptCount <= SearchSettings.MaxCandidateAttempts);

	FABTSM73DAG5AResult RepeatResult;
	FABTSM73StructureData RepeatData;
	FString RepeatError;
	RunDAG5ASearch(
		Fixture,
		SearchSettings,
		RepeatResult,
		RepeatData,
		RepeatError);
	TestEqual(
		TEXT("Preflight rejection hash is deterministic"),
		RepeatResult.SearchHash,
		FirstResult.SearchHash);
	TestTrue(TEXT("Repeat preflight rejection remains atomic"), IsDAG5AAtomicEmpty(RepeatData));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5AInvalidOperationBudgetTest,
	"ABTS.M73DAG.DAG5A.InvalidOperationBudgetsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5AInvalidOperationBudgetTest::RunTest(
	const FString& Parameters)
{
	const FDAG5ATestFixture DefaultFixture;
	FABTSM73DAG5ASettings AttemptSettings;
	AttemptSettings.bEnableFeasibilitySearch = true;
	AttemptSettings.MaxCandidateAttempts = 65;

	FABTSM73DAG5AResult AttemptResult;
	FABTSM73StructureData AttemptData;
	AddDAG5ASentinel(AttemptData);
	FString AttemptError;
	const bool bAttemptBuilt = RunDAG5ASearch(
		DefaultFixture,
		AttemptSettings,
		AttemptResult,
		AttemptData,
		AttemptError);
	TestFalse(TEXT("A 65-candidate search budget is rejected"), bAttemptBuilt);
	TestTrue(TEXT("Attempt-budget rejection has the stable settings code"),
		AttemptError.StartsWith(
			TEXT("DAG5APreflightRejected:DAG5ASettingsInvalid")));
	TestEqual(TEXT("Invalid attempt budget executes zero attempts"),
		AttemptResult.AttemptCount, 0);
	TestTrue(TEXT("Invalid attempt budget clears output atomically"),
		IsDAG5AAtomicEmpty(AttemptData));

	FDAG5ATestFixture DAGBudgetFixture;
	DAGBudgetFixture.DAGSettings.ExpansionStepBudget = 33;
	FABTSM73DAG5ASettings DAGBudgetSettings;
	DAGBudgetSettings.bEnableFeasibilitySearch = true;
	DAGBudgetSettings.MaxCandidateAttempts = 4;
	FABTSM73DAG5AResult DAGBudgetResult;
	FABTSM73StructureData DAGBudgetData;
	AddDAG5ASentinel(DAGBudgetData);
	FString DAGBudgetError;
	const bool bDAGBudgetBuilt = RunDAG5ASearch(
		DAGBudgetFixture,
		DAGBudgetSettings,
		DAGBudgetResult,
		DAGBudgetData,
		DAGBudgetError);
	TestFalse(TEXT("An oversized DAG operation budget is rejected"), bDAGBudgetBuilt);
	TestTrue(TEXT("DAG operation rejection has the stable budget code"),
		DAGBudgetError.StartsWith(
			TEXT("DAG5APreflightRejected:DAG5AOperationBudgetInvalid")));
	TestEqual(TEXT("Invalid DAG operation budget executes zero attempts"),
		DAGBudgetResult.AttemptCount, 0);
	TestTrue(TEXT("Invalid DAG operation budget clears output atomically"),
		IsDAG5AAtomicEmpty(DAGBudgetData));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5AAdaptiveRootCapacityTest,
	"ABTS.M73DAG.DAG5A.AdaptiveRootCapacityPreflight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5AAdaptiveRootCapacityTest::RunTest(
	const FString& Parameters)
{
	FDAG5ATestFixture Fixture;
	Fixture.LayoutSettings.bEnableAdaptiveGeometry = true;
	Fixture.LayoutSettings.MinAdaptivePlateExtentCM = 42.0f;
	Fixture.LayoutSettings.MinPlateExtentCM = 90.0f;
	Fixture.LayoutSettings.TargetWidthCM = 66.0f;

	FABTSM73DAG5ASettings SearchSettings;
	SearchSettings.bEnableFeasibilitySearch = true;
	SearchSettings.MaxCandidateAttempts = 4;
	FABTSM73DAG5AResult Result;
	FABTSM73StructureData Data;
	AddDAG5ASentinel(Data);
	FString Error;
	const bool bBuilt = RunDAG5ASearch(
		Fixture,
		SearchSettings,
		Result,
		Data,
		Error);
	TestFalse(
		TEXT("Adaptive leaf geometry does not relax the root plate capacity"),
		bBuilt);
	TestTrue(TEXT("Adaptive root rejection has the stable layout code"),
		Error.StartsWith(
			TEXT("DAG5APreflightRejected:DAG5ALayoutCapacityInvalid")));
	TestEqual(TEXT("Adaptive root rejection executes zero attempts"),
		Result.AttemptCount, 0);
	TestTrue(TEXT("Adaptive root rejection clears output atomically"),
		IsDAG5AAtomicEmpty(Data));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5ANonFiniteInputTest,
	"ABTS.M73DAG.DAG5A.NonFiniteInputsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5ANonFiniteInputTest::RunTest(
	const FString& Parameters)
{
	const float QuietNaN = std::numeric_limits<float>::quiet_NaN();
	const float Infinity = std::numeric_limits<float>::infinity();
	for (int32 CaseIndex = 0; CaseIndex < 4; ++CaseIndex)
	{
		FDAG5ATestFixture Fixture;
		FString CaseName;
		switch (CaseIndex)
		{
		case 0:
			Fixture.DAGSettings.SeriesRuleWeight = QuietNaN;
			CaseName = TEXT("NaN rule weight");
			break;
		case 1:
			Fixture.DAGSettings.ParallelRuleWeight = Infinity;
			CaseName = TEXT("Infinite rule weight");
			break;
		case 2:
			Fixture.LayoutSettings.TargetWidthCM = QuietNaN;
			CaseName = TEXT("NaN layout width");
			break;
		default:
			Fixture.LayoutSettings.TargetHeightCM = Infinity;
			CaseName = TEXT("Infinite layout height");
			break;
		}

		FABTSM73DAG5ASettings SearchSettings;
		SearchSettings.bEnableFeasibilitySearch = true;
		SearchSettings.MaxCandidateAttempts = 4;
		FABTSM73DAG5AResult Result;
		FABTSM73StructureData Data;
		AddDAG5ASentinel(Data);
		FString Error;
		const bool bBuilt = RunDAG5ASearch(
			Fixture,
			SearchSettings,
			Result,
			Data,
			Error);
		TestFalse(
			FString::Printf(TEXT("%s is rejected"), *CaseName),
			bBuilt);
		TestTrue(
			FString::Printf(TEXT("%s fails in global preflight"), *CaseName),
			Error.StartsWith(TEXT("DAG5APreflightRejected:")));
		TestEqual(
			FString::Printf(TEXT("%s executes zero attempts"), *CaseName),
			Result.AttemptCount,
			0);
		TestTrue(
			FString::Printf(TEXT("%s clears output atomically"), *CaseName),
			IsDAG5AAtomicEmpty(Data));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5ACapacityPreflightDisabledTest,
	"ABTS.M73DAG.DAG5A.CapacityPreflightDisabledEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5ACapacityPreflightDisabledTest::RunTest(
	const FString& Parameters)
{
	const FDAG5ATestFixture Fixture;
	FABTSM73DAG5ASettings SearchSettings;
	SearchSettings.bEnableFeasibilitySearch = true;
	SearchSettings.bEnableCapacityPreflight = false;
	SearchSettings.MaxCandidateAttempts = 1;
	SearchSettings.MaxCompiledBrickCount = 13;
	FABTSM73DAG5AResult Result;
	FABTSM73StructureData Data;
	FString Error;
	const bool bBuilt = RunDAG5ASearch(
		Fixture,
		SearchSettings,
		Result,
		Data,
		Error);
	TestTrue(
		FString::Printf(
			TEXT("Capacity-preflight-disabled candidate still builds: %s"),
			*Error),
		bBuilt);
	TestTrue(TEXT("Capacity-preflight-disabled search is accepted"),
		Result.bAccepted);
	TestFalse(TEXT("Result does not claim an unexecuted capacity preflight"),
		Result.bCapacityPreflightPassed);
	TestEqual(TEXT("Disabled capacity preflight still uses one bounded attempt"),
		Result.AttemptCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5ACompiledBrickBoundaryTest,
	"ABTS.M73DAG.DAG5A.CompiledBrickBudgetBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5ACompiledBrickBoundaryTest::RunTest(
	const FString& Parameters)
{
	const FDAG5ATestFixture Fixture;
	FABTSM73DAG5ASettings RejectSettings;
	RejectSettings.bEnableFeasibilitySearch = true;
	RejectSettings.MaxCandidateAttempts = 1;
	RejectSettings.MaxCompiledBrickCount = 12;

	FABTSM73DAG5AResult RejectResult;
	FABTSM73StructureData RejectData;
	AddDAG5ASentinel(RejectData);
	FString RejectError;
	const bool bRejectedBuild = RunDAG5ASearch(
		Fixture,
		RejectSettings,
		RejectResult,
		RejectData,
		RejectError);
	TestFalse(TEXT("SingleTower depth zero rejects a 12-brick physical limit"), bRejectedBuild);
	TestEqual(TEXT("The 12-brick boundary performs one bounded attempt"), RejectResult.AttemptCount, 1);
	TestEqual(TEXT("The rejected candidate still reports its 13 physical bricks"),
		RejectResult.Attempts.Num() == 1
			? RejectResult.Attempts[0].CompiledBrickCount
			: INDEX_NONE,
		13);
	TestEqual(TEXT("The boundary rejection occurs after compilation"),
		RejectResult.Attempts.Num() == 1
			? RejectResult.Attempts[0].RejectStage
			: EABTSM73DAG5ARejectStage::None,
		EABTSM73DAG5ARejectStage::CompiledBrickBudget);
	TestTrue(TEXT("A compiled-budget failure leaves no partial structure"), IsDAG5AAtomicEmpty(RejectData));
	TestTrue(
		TEXT("Compiled-budget rejection does not exceed its attempt budget"),
		RejectResult.AttemptCount <= RejectSettings.MaxCandidateAttempts);

	FABTSM73DAG5AResult RejectRepeatResult;
	FABTSM73StructureData RejectRepeatData;
	FString RejectRepeatError;
	RunDAG5ASearch(
		Fixture,
		RejectSettings,
		RejectRepeatResult,
		RejectRepeatData,
		RejectRepeatError);
	TestEqual(
		TEXT("The 12-brick rejection hash is stable"),
		RejectRepeatResult.SearchHash,
		RejectResult.SearchHash);

	FABTSM73DAG5ASettings AcceptSettings = RejectSettings;
	AcceptSettings.MaxCompiledBrickCount = 13;
	FABTSM73DAG5AResult AcceptResult;
	FABTSM73StructureData AcceptData;
	FString AcceptError;
	const bool bAcceptedBuild = RunDAG5ASearch(
		Fixture,
		AcceptSettings,
		AcceptResult,
		AcceptData,
		AcceptError);
	TestTrue(
		FString::Printf(
			TEXT("SingleTower depth zero accepts the exact 13-brick limit: %s"),
			*AcceptError),
		bAcceptedBuild);
	TestEqual(TEXT("Exact physical limit retains all 13 bricks"), AcceptData.Bricks.Num(), 13);
	TestEqual(TEXT("Exact physical limit accepts attempt zero"), AcceptResult.SelectedAttemptIndex, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5AParallelScopeBoundTest,
	"ABTS.M73DAG.DAG5A.ParallelScopeBoundedRejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5AParallelScopeBoundTest::RunTest(
	const FString& Parameters)
{
	FDAG5ATestFixture Fixture;
	Fixture.DAGSettings.MinExpansionDepth = 1;
	Fixture.DAGSettings.MaxExpansionDepth = 1;
	Fixture.DAGSettings.ExpansionStepBudget = 4;
	Fixture.DAGSettings.SeriesRuleWeight = 0.0f;
	Fixture.DAGSettings.ParallelRuleWeight = 1.0f;
	Fixture.LayoutSettings.TargetWidthCM = 160.0f;
	Fixture.LayoutSettings.TargetDepthCM = 160.0f;
	Fixture.LayoutSettings.MinAdaptivePlateExtentCM = 70.0f;

	FABTSM73DAG5ASettings SearchSettings;
	SearchSettings.bEnableFeasibilitySearch = true;
	SearchSettings.MaxCandidateAttempts = 3;
	SearchSettings.MaxCompiledBrickCount = 64;

	FABTSM73DAG5AResult FirstResult;
	FABTSM73StructureData FirstData;
	AddDAG5ASentinel(FirstData);
	FString FirstError;
	const bool bFirstBuilt = RunDAG5ASearch(
		Fixture,
		SearchSettings,
		FirstResult,
		FirstData,
		FirstError);
	TestFalse(TEXT("Pure Parallel grammar rejects an undersized Scope"), bFirstBuilt);
	TestEqual(TEXT("Pure Parallel rejection consumes exactly its bounded attempts"),
		FirstResult.AttemptCount, SearchSettings.MaxCandidateAttempts);
	TestEqual(TEXT("Scope preflight rejects every candidate"),
		FirstResult.ScopePreflightRejectCount, SearchSettings.MaxCandidateAttempts);
	TestEqual(TEXT("No scope-rejected candidate reaches compilation"),
		FirstResult.CompiledCandidateCount, 0);
	TestTrue(TEXT("Bounded Scope rejection leaves no partial structure"), IsDAG5AAtomicEmpty(FirstData));
	for (const FABTSM73DAG5AAttemptResult& Attempt : FirstResult.Attempts)
	{
		TestEqual(
			TEXT("Every pure Parallel attempt rejects at ScopeCapacity"),
			Attempt.RejectStage,
			EABTSM73DAG5ARejectStage::ScopeCapacity);
		TestTrue(
			TEXT("Every pure Parallel attempt reports the stable narrow-Scope code"),
			Attempt.RejectCode == TEXT("DAG5AParallelScopeTooNarrow"));
	}

	FABTSM73DAG5AResult RepeatResult;
	FABTSM73StructureData RepeatData;
	FString RepeatError;
	RunDAG5ASearch(
		Fixture,
		SearchSettings,
		RepeatResult,
		RepeatData,
		RepeatError);
	TestEqual(
		TEXT("Bounded Scope rejection hash is deterministic"),
		RepeatResult.SearchHash,
		FirstResult.SearchHash);
	TestTrue(
		TEXT("Bounded Scope repeat preserves its attempt trace"),
		EqualDAG5AAttempts(FirstResult, RepeatResult));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5ADeterministicBacktrackingTest,
	"ABTS.M73DAG.DAG5A.DeterministicBacktracking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5ADeterministicBacktrackingTest::RunTest(
	const FString& Parameters)
{
	FDAG5ATestFixture Fixture;
	Fixture.DAGSettings.MinExpansionDepth = 1;
	Fixture.DAGSettings.MaxExpansionDepth = 1;
	Fixture.DAGSettings.ExpansionStepBudget = 4;
	Fixture.LayoutSettings.TargetWidthCM = 160.0f;
	Fixture.LayoutSettings.TargetDepthCM = 160.0f;
	Fixture.LayoutSettings.MinAdaptivePlateExtentCM = 70.0f;

	FABTSM73DAG5ASettings SearchSettings;
	SearchSettings.bEnableFeasibilitySearch = true;
	SearchSettings.MaxCandidateAttempts = 8;
	SearchSettings.MaxCompiledBrickCount = 64;

	Fixture.DAGSettings.BuildingSeed = 1;
	FABTSM73DAG5AResult FoundResult;
	FABTSM73StructureData FoundData;
	FString FoundError;
	const bool bFoundBuilt = RunDAG5ASearch(
		Fixture,
		SearchSettings,
		FoundResult,
		FoundData,
		FoundError);
	TestTrue(
		FString::Printf(
			TEXT("Fixed backtracking fixture succeeds: %s"),
			*FoundError),
		bFoundBuilt);
	TestEqual(TEXT("Backtracking golden selects attempt one"),
		FoundResult.SelectedAttemptIndex, 1);
	TestEqual(TEXT("Backtracking golden selects its stable derived seed"),
		FoundResult.SelectedCandidateSeed, 525865732);
	if (!bFoundBuilt || FoundResult.Attempts.Num() < 2)
	{
		return true;
	}
	TestTrue(TEXT("Backtracking returns non-empty accepted geometry"), !FoundData.Bricks.IsEmpty());
	TestTrue(TEXT("Backtracking records a rejected first attempt"), !FoundResult.Attempts[0].bAccepted);
	TestTrue(TEXT("Backtracking records the selected attempt as accepted"),
		FoundResult.Attempts.IsValidIndex(FoundResult.SelectedAttemptIndex)
			&& FoundResult.Attempts[FoundResult.SelectedAttemptIndex].bAccepted);
	TestEqual(TEXT("Search stops immediately after the accepted attempt"),
		FoundResult.AttemptCount, FoundResult.SelectedAttemptIndex + 1);
	TestTrue(TEXT("Backtracking remains inside its hard attempt budget"),
		FoundResult.AttemptCount <= SearchSettings.MaxCandidateAttempts);

	FABTSM73DAG5AResult RepeatResult;
	FABTSM73StructureData RepeatData;
	FString RepeatError;
	const bool bRepeatBuilt = RunDAG5ASearch(
		Fixture,
		SearchSettings,
		RepeatResult,
		RepeatData,
		RepeatError);
	TestTrue(
		FString::Printf(
			TEXT("Discovered backtracking seed reproduces: %s"),
			*RepeatError),
		bRepeatBuilt);
	TestEqual(TEXT("Backtracking search hash is stable"),
		RepeatResult.SearchHash, FoundResult.SearchHash);
	TestEqual(TEXT("Backtracking selects the same attempt"),
		RepeatResult.SelectedAttemptIndex, FoundResult.SelectedAttemptIndex);
	TestEqual(TEXT("Backtracking selects the same candidate seed"),
		RepeatResult.SelectedCandidateSeed, FoundResult.SelectedCandidateSeed);
	TestTrue(TEXT("Backtracking attempt trace is stable"),
		EqualDAG5AAttempts(FoundResult, RepeatResult));
	TestTrue(TEXT("Backtracking compiled geometry is stable"),
		EqualDAG5AGeometry(FoundData, RepeatData));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5ASearchHashIdentityTest,
	"ABTS.M73DAG.DAG5A.SearchHashInputIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5ASearchHashIdentityTest::RunTest(
	const FString& Parameters)
{
	const FDAG5ATestFixture BaseFixture;
	FABTSM73DAG5ASettings BaseSearchSettings;
	BaseSearchSettings.bEnableFeasibilitySearch = true;
	BaseSearchSettings.MaxCandidateAttempts = 1;
	BaseSearchSettings.MaxCompiledBrickCount = 13;
	FABTSM73DAG5AResult BaseResult;
	FABTSM73StructureData BaseData;
	FString BaseError;
	const bool bBaseBuilt = RunDAG5ASearch(
		BaseFixture,
		BaseSearchSettings,
		BaseResult,
		BaseData,
		BaseError);
	TestTrue(
		FString::Printf(TEXT("Hash baseline builds: %s"), *BaseError),
		bBaseBuilt);
	TestNotEqual(TEXT("Accepted hash baseline is non-zero"),
		BaseResult.SearchHash, static_cast<int64>(0));

	FABTSM73DAG5ASettings SearchVariantSettings = BaseSearchSettings;
	SearchVariantSettings.SearchVersion += 1;
	FABTSM73DAG5AResult SearchVariantResult;
	FABTSM73StructureData SearchVariantData;
	FString SearchVariantError;
	const bool bSearchVariantBuilt = RunDAG5ASearch(
		BaseFixture,
		SearchVariantSettings,
		SearchVariantResult,
		SearchVariantData,
		SearchVariantError);
	TestTrue(TEXT("Search-settings hash variant builds"), bSearchVariantBuilt);
	TestNotEqual(TEXT("Changing Search input changes SearchHash"),
		SearchVariantResult.SearchHash, BaseResult.SearchHash);

	FDAG5ATestFixture LayoutVariant = BaseFixture;
	LayoutVariant.LayoutSettings.TargetHeightCM += 10.0f;
	FABTSM73DAG5AResult LayoutVariantResult;
	FABTSM73StructureData LayoutVariantData;
	FString LayoutVariantError;
	const bool bLayoutVariantBuilt = RunDAG5ASearch(
		LayoutVariant,
		BaseSearchSettings,
		LayoutVariantResult,
		LayoutVariantData,
		LayoutVariantError);
	TestTrue(TEXT("Layout hash variant builds"), bLayoutVariantBuilt);
	TestNotEqual(TEXT("Changing Layout input changes SearchHash"),
		LayoutVariantResult.SearchHash, BaseResult.SearchHash);

	FDAG5ATestFixture FrontierVariant = BaseFixture;
	FrontierVariant.FrontierSettings.MaxCandidateCount -= 1;
	FABTSM73DAG5AResult FrontierVariantResult;
	FABTSM73StructureData FrontierVariantData;
	FString FrontierVariantError;
	const bool bFrontierVariantBuilt = RunDAG5ASearch(
		FrontierVariant,
		BaseSearchSettings,
		FrontierVariantResult,
		FrontierVariantData,
		FrontierVariantError);
	TestTrue(TEXT("Frontier hash variant builds"), bFrontierVariantBuilt);
	TestTrue(TEXT("Disabled Frontier input leaves geometry unchanged"),
		EqualDAG5AGeometry(BaseData, FrontierVariantData));
	TestNotEqual(TEXT("Changing Frontier input changes SearchHash"),
		FrontierVariantResult.SearchHash, BaseResult.SearchHash);

	FDAG5ATestFixture MaterialVariant = BaseFixture;
	TestTrue(TEXT("Default material library is available"),
		!MaterialVariant.MaterialProfiles.IsEmpty());
	if (!MaterialVariant.MaterialProfiles.IsEmpty())
	{
		MaterialVariant.MaterialProfiles[0].DensityGPerCubicCM += 0.01f;
	}
	FABTSM73DAG5AResult MaterialVariantResult;
	FABTSM73StructureData MaterialVariantData;
	FString MaterialVariantError;
	const bool bMaterialVariantBuilt = RunDAG5ASearch(
		MaterialVariant,
		BaseSearchSettings,
		MaterialVariantResult,
		MaterialVariantData,
		MaterialVariantError);
	TestTrue(TEXT("Material hash variant builds"), bMaterialVariantBuilt);
	TestTrue(TEXT("Disabled material consumers leave geometry unchanged"),
		EqualDAG5AGeometry(BaseData, MaterialVariantData));
	TestNotEqual(TEXT("Changing Material input changes SearchHash"),
		MaterialVariantResult.SearchHash, BaseResult.SearchHash);

	FABTSM73DAG5AResult AttackVariantResult;
	FABTSM73StructureData AttackVariantData;
	FString AttackVariantError;
	const bool bAttackVariantBuilt = RunDAG5ASearch(
		BaseFixture,
		BaseSearchSettings,
		AttackVariantResult,
		AttackVariantData,
		AttackVariantError,
		FVector::RightVector);
	TestTrue(TEXT("Attack-direction hash variant builds"), bAttackVariantBuilt);
	TestTrue(TEXT("Disabled playability leaves attack-variant geometry unchanged"),
		EqualDAG5AGeometry(BaseData, AttackVariantData));
	TestNotEqual(TEXT("Changing Attack input changes SearchHash"),
		AttackVariantResult.SearchHash, BaseResult.SearchHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5ADisabledCompatibilityTest,
	"ABTS.M73DAG.DAG5A.DisabledLegacyPipelineParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5ADisabledCompatibilityTest::RunTest(
	const FString& Parameters)
{
	const FDAG5ATestFixture Fixture;
	const FABTSM73DAG5ASettings DefaultSearchSettings;
	TestFalse(
		TEXT("DAG5-A remains opt-in by default"),
		DefaultSearchSettings.bEnableFeasibilitySearch);

	FABTSM73DAGBuildingPipeline Pipeline;
	FABTSM73StructureData DirectData;
	FString DirectError;
	const bool bDirectBuilt = Pipeline.Build(
		Fixture.DAGSettings,
		Fixture.LayoutSettings,
		Fixture.BuildingSettings,
		DirectData,
		DirectError);
	TestTrue(
		FString::Printf(
			TEXT("The legacy one-shot Build path remains valid: %s"),
			*DirectError),
		bDirectBuilt);

	FABTSM73StructureData DisabledStagesData;
	FString DisabledStagesError;
	const bool bDisabledStagesBuilt = Pipeline.BuildWithFailurePattern(
		Fixture.DAGSettings,
		Fixture.LayoutSettings,
		Fixture.BuildingSettings,
		Fixture.FrontierSettings,
		Fixture.PatternSettings,
		Fixture.PlayabilitySettings,
		Fixture.DifficultySettings,
		Fixture.MaterialProfiles,
		FVector::ForwardVector,
		DisabledStagesData,
		DisabledStagesError);
	TestTrue(
		FString::Printf(
			TEXT("The existing disabled-stage pipeline remains valid: %s"),
			*DisabledStagesError),
		bDisabledStagesBuilt);
	TestTrue(
		TEXT("Disabled optional stages preserve the old one-shot geometry"),
		EqualDAG5AGeometry(DirectData, DisabledStagesData));

	FABTSM73DAG5AResult DisabledSearchResult;
	FABTSM73StructureData DisabledSearchData;
	FString DisabledSearchError;
	const bool bDisabledSearchBuilt = RunDAG5ASearch(
		Fixture,
		DefaultSearchSettings,
		DisabledSearchResult,
		DisabledSearchData,
		DisabledSearchError);
	TestTrue(
		FString::Printf(
			TEXT("Disabled DAG5-A dispatches to the legacy one-shot path: %s"),
			*DisabledSearchError),
		bDisabledSearchBuilt);
	TestEqual(TEXT("Disabled search performs no attempts"),
		DisabledSearchResult.AttemptCount, 0);
	TestFalse(TEXT("Disabled dispatch remains marked as not enabled"),
		DisabledSearchResult.bEnabled);
	TestTrue(TEXT("Disabled dispatch is geometry-identical to old Build"),
		EqualDAG5AGeometry(DirectData, DisabledSearchData));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
