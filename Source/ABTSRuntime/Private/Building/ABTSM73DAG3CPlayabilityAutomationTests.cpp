// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSRuntime.h"
#include "Building/ABTSM7MaterialProfileLibrary.h"
#include "Building/ABTSM73DAGBuildingPipeline.h"
#include "Building/ABTSM73DAGFailurePlayabilityPlanner.h"
#include "Building/ABTSM73DAGFailurePatternRewriter.h"
#include "Building/ABTSM73DAGGrammarExpander.h"
#include "Building/ABTSM73DAGLayoutSolver.h"
#include "Building/ABTSM73DAGModuleCompiler.h"
#include "Building/ABTSM73StructureData.h"
#include "Game/ABTSM7GameMode.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	int32 AddDAG3CTestNode(
		FABTSM73StructureData& Data,
		const FVector& Center,
		const FVector& Dimensions,
		const EABTSM7BuildingMaterial Material,
		const EABTSM73BrickSemanticRole Role)
	{
		FABTSM73BrickNode& Node = Data.Bricks.AddDefaulted_GetRef();
		Node.NodeId = Data.Bricks.Num() - 1;
		Node.MacroNodeId = Node.NodeId;
		Node.LocalCenter = Center;
		Node.DimensionsCM = Dimensions;
		Node.Material = Material;
		Node.OriginalMaterial = Material;
		Node.SemanticRole = Role;
		Node.bFailureFrontierMainBody = true;
		return Node.NodeId;
	}

	void AddDAG3CTestEdge(
		FABTSM73StructureData& Data,
		const int32 LowerNodeId,
		const int32 UpperNodeId)
	{
		FABTSM73SupportEdge& Edge =
			Data.SupportEdges.AddDefaulted_GetRef();
		Edge.LowerNodeId = LowerNodeId;
		Edge.UpperNodeId = UpperNodeId;
		Edge.ContactAreaCM2 = 400.0f;
	}

	FABTSM73StructureData MakeDAG3CFixture(
		const EABTSM73DAGFailurePattern Pattern,
		const EABTSM7BuildingMaterial Material)
	{
		FABTSM73StructureData Data;
		const int32 SupportPlate = AddDAG3CTestNode(
			Data,
			FVector(0.0f, 0.0f, 0.0f),
			FVector(200.0f, 160.0f, 20.0f),
			Material,
			EABTSM73BrickSemanticRole::Deck);
		const int32 Weak = AddDAG3CTestNode(
			Data,
			FVector(-80.0f, 0.0f, 100.0f),
			FVector(30.0f, 30.0f, 120.0f),
			Material,
			EABTSM73BrickSemanticRole::WeakSupport);
		int32 Pivot = INDEX_NONE;
		if (Pattern != EABTSM73DAGFailurePattern::InternalSingleSupport)
		{
			Pivot = AddDAG3CTestNode(
				Data,
				FVector(80.0f, 0.0f, 100.0f),
				FVector(30.0f, 30.0f, 120.0f),
				Material,
				EABTSM73BrickSemanticRole::Column);
		}
		const int32 LoadPlate = AddDAG3CTestNode(
			Data,
			FVector(0.0f, 0.0f, 170.0f),
			FVector(200.0f, 120.0f, 20.0f),
			Material,
			EABTSM73BrickSemanticRole::Deck);
		const int32 Payload = AddDAG3CTestNode(
			Data,
			FVector(0.0f, 0.0f, 250.0f),
			FVector(100.0f, 80.0f, 80.0f),
			Material,
			EABTSM73BrickSemanticRole::Payload);

		Data.GroundNodeIds.Add(SupportPlate);
		AddDAG3CTestEdge(Data, SupportPlate, Weak);
		AddDAG3CTestEdge(Data, Weak, LoadPlate);
		if (Pivot != INDEX_NONE)
		{
			AddDAG3CTestEdge(Data, SupportPlate, Pivot);
			AddDAG3CTestEdge(Data, Pivot, LoadPlate);
		}
		AddDAG3CTestEdge(Data, LoadPlate, Payload);

		Data.DAGFailureFrontierAnalysis.bEnabled = true;
		Data.DAGFailureFrontierAnalysis.bAccepted = true;
		Data.DAGFailureFrontierAnalysis.AcceptedCandidateCount = 1;
		Data.DAGFailureFrontierAnalysis.SelectedFrontierHash =
			0xD3C01001u + static_cast<uint32>(Pattern);

		FABTSM73DAGFailurePatternResult& Result =
			Data.DAGFailurePatternResult;
		Result.bEnabled = true;
		Result.bApplied = true;
		Result.Pattern = Pattern;
		Result.ExpectedMotion =
			Pattern == EABTSM73DAGFailurePattern::InternalSingleSupport
				? EABTSM73DAGFailureMotion::Drop
				: Pattern
					== EABTSM73DAGFailurePattern::InternalOffsetSeam
					? EABTSM73DAGFailureMotion::SlideThenTip
					: EABTSM73DAGFailureMotion::Tip;
		Result.SourceFrontierHash = Data.DAGFailureFrontierAnalysis
			.SelectedFrontierHash;
		Result.RealizedPatternHash =
			0xD3C02001u + static_cast<uint32>(Pattern);
		Result.SupportPlateNodeId = SupportPlate;
		Result.LoadPlateNodeId = LoadPlate;
		Result.WeakNodeIds.Add(Weak);
		if (Pivot != INDEX_NONE)
		{
			Result.RemainingSupportNodeIds.Add(Pivot);
		}
		Result.AffectedMainBodyNodeIds = {LoadPlate, Payload};
		Result.ExpectedFailureDirectionLocal = -FVector::ForwardVector;
		Result.InitialSupportMarginCM = 8.0f;
		Result.PostFailureTipMarginCM =
			Pivot == INDEX_NONE ? 0.0f : 12.0f;
		Result.ReseatRisk = 0.20f;
		Result.OffsetSeamShiftCM =
			Pattern == EABTSM73DAGFailurePattern::InternalOffsetSeam
				? 40.0f
				: 0.0f;
		return Data;
	}

	FABTSM73DAGFailurePlayabilitySettings MakeDAG3CSettings()
	{
		FABTSM73DAGFailurePlayabilitySettings Settings;
		Settings.bEnablePlayabilityRouting = true;
		Settings.ProjectileRadiusCM = 42.0f;
		Settings.AttackApproachDistanceCM = 900.0f;
		Settings.AttackFaceGridResolution = 3;
		Settings.MinAttackExposure = 0.10f;
		Settings.MinFreeDropDistanceCM = 30.0f;
		Settings.MinFreeTipAngleDegrees = 6.0f;
		Settings.MinFreeSlideDistanceCM = 24.0f;
		Settings.TranslationSweepStepCM = 3.0f;
		Settings.TipSweepStepDegrees = 1.0f;
		Settings.CollisionToleranceCM = 1.0f;
		Settings.MaxMotionSweepSampleCount = 256;
		return Settings;
	}

	bool PlanDAG3CFixture(
		const FABTSM73DAGFailurePlayabilitySettings& Settings,
		const TConstArrayView<FABTSM7MaterialProfile> Profiles,
		const FVector& AttackDirection,
		FABTSM73StructureData& Data,
		FABTSM73DAGFailurePlayabilityResult& OutResult,
		FString& OutError,
		const TOptional<EABTSM7BuildingMaterial> ExpectedMaterial =
			TOptional<EABTSM7BuildingMaterial>(),
		const FABTSM73DifficultySettings* DifficultyOverride = nullptr)
	{
		const FABTSM73DifficultySettings DefaultDifficulty;
		const FABTSM73DifficultySettings& Difficulty =
			DifficultyOverride != nullptr
				? *DifficultyOverride
				: DefaultDifficulty;
		FABTSM73DAGFailurePlayabilityPlanner Planner;
		return Planner.Plan(
			Settings,
			Difficulty,
			ExpectedMaterial.IsSet()
				? ExpectedMaterial.GetValue()
				: Data.Bricks.IsEmpty()
					? EABTSM7BuildingMaterial::Wood
					: Data.Bricks[0].OriginalMaterial,
			Profiles,
			AttackDirection,
			Data,
			OutResult,
			OutError);
	}

	bool HasDirtyWeakState(const FABTSM73StructureData& Data)
	{
		if (!Data.WeakPoints.IsEmpty()
			|| Data.DAGFailurePlayabilityResult.bPlayable)
		{
			return true;
		}
		for (const FABTSM73BrickNode& Node : Data.Bricks)
		{
			if (Node.bWeakPoint
				|| Node.WeakPointRole != EABTSM73WeakPointRole::None
				|| Node.Material != Node.OriginalMaterial)
			{
				return true;
			}
		}
		return false;
	}

	bool IsAtomicRejectedResult(
		const FABTSM73DAGFailurePlayabilityResult& Result)
	{
		return Result.bEnabled
			&& !Result.bPlayable
			&& !Result.bMaterialProfileValidated
			&& Result.WeakNodeIds.IsEmpty()
			&& Result.AffectedNodeIds.IsEmpty()
			&& Result.AcceptedAttackDirectionLocal.IsZero()
			&& Result.AttackImpactPointLocal.IsZero()
			&& Result.AttackSampleCount == 0
			&& Result.MotionSweepSampleCount == 0
			&& Result.BlockingNodeId == INDEX_NONE
			&& Result.PlayabilityHash == 0
			&& !Result.RejectReason.IsEmpty();
	}

	FABTSM7TaskGraphBuildingProfile MakeDAG3CPipelineProfile(
		const EABTSM73DAGFailurePattern Pattern)
	{
		FABTSM7TaskGraphBuildingProfile Profile =
			FABTSM7TaskGraphDAG23ProfileResolver::MakeDefaultProfile(
				EABTSM3TaskType::Workshop,
				EABTSM7BuildingMaterial::Wood);
		Profile.GenerationSettings.BuildingSeed = 1034266606;
		Profile.DAGGenerationSettings.BuildingSeed = 1034266606;
		Profile.DAGFailureFrontierSettings.bEnableAnalysis = true;
		Profile.DAGFailureFrontierSettings
			.bEnableGeneralizedSmallCutSearch = true;
		Profile.DAGFailurePatternSettings.bEnableGeometryRewrite = true;
		Profile.DAGFailurePatternSettings.Pattern = Pattern;
		Profile.DAGFailurePlayabilitySettings.bEnablePlayabilityRouting =
			true;
		return Profile;
	}

	bool BuildDAG3CPipelineProfile(
		const FABTSM7TaskGraphBuildingProfile& Profile,
		const FVector& AttackDirection,
		FABTSM73StructureData& OutData,
		FString& OutError)
	{
		const TArray<FABTSM7MaterialProfile> Profiles =
			FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
		FABTSM73DAGBuildingPipeline Pipeline;
		return Pipeline.BuildWithFailurePattern(
			Profile.DAGGenerationSettings,
			Profile.DAGLayoutSettings,
			Profile.GenerationSettings,
			Profile.DAGFailureFrontierSettings,
			Profile.DAGFailurePatternSettings,
			Profile.DAGFailurePlayabilitySettings,
			Profile.DifficultySettings,
			Profiles,
			AttackDirection,
			OutData,
			OutError);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3CAttackCorridorTest,
	"ABTS.M73DAG3.C.AttackCorridorAndOcclusion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3CAttackCorridorTest::RunTest(
	const FString& Parameters)
{
	const TArray<FABTSM7MaterialProfile> Profiles =
		FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
	const FABTSM73DAGFailurePlayabilitySettings Settings =
		MakeDAG3CSettings();

	FABTSM73StructureData Clear = MakeDAG3CFixture(
		EABTSM73DAGFailurePattern::InternalAsymmetricDualSupport,
		EABTSM7BuildingMaterial::Wood);
	FABTSM73DAGFailurePlayabilityResult ClearResult;
	FString Error;
	TestTrue(
		FString::Printf(TEXT("Clear bird corridor certifies: %s"), *Error),
		PlanDAG3CFixture(
			Settings,
			Profiles,
			FVector::ForwardVector,
			Clear,
			ClearResult,
			Error));
	TestTrue(TEXT("Clear corridor is playable"), ClearResult.bPlayable);
	TestTrue(TEXT("Clear corridor exposes attack samples"),
		ClearResult.AttackExposure >= Settings.MinAttackExposure);
	TestTrue(TEXT("Clear corridor records positive clearance"),
		ClearResult.MinAttackClearanceCM > 0.0f);

	FABTSM73StructureData Reversed = MakeDAG3CFixture(
		EABTSM73DAGFailurePattern::InternalAsymmetricDualSupport,
		EABTSM7BuildingMaterial::Wood);
	FABTSM73DAGFailurePlayabilityResult ReversedResult;
	Error.Reset();
	TestFalse(
		TEXT("Attack from the failure side's reverse is rejected"),
		PlanDAG3CFixture(
			Settings,
			Profiles,
			-FVector::ForwardVector,
			Reversed,
			ReversedResult,
			Error));
	TestEqual(
		TEXT("Reverse attack has an explicit direction rejection"),
		Error,
		FString(TEXT("DAG3CAttackDirectionOpposesFailure")));
	TestFalse(TEXT("Reverse rejection is atomic"),
		HasDirtyWeakState(Reversed));

	FABTSM73StructureData Narrow = MakeDAG3CFixture(
		EABTSM73DAGFailurePattern::InternalAsymmetricDualSupport,
		EABTSM7BuildingMaterial::Wood);
	// A zero-radius ray at Z=100 passes through the 70 cm opening. A 42 cm
	// radius bird cannot fit through the same opening.
	AddDAG3CTestNode(
		Narrow,
		FVector(-300.0f, 0.0f, 10.0f),
		FVector(80.0f, 300.0f, 110.0f),
		EABTSM7BuildingMaterial::Wood,
		EABTSM73BrickSemanticRole::Connector);
	AddDAG3CTestNode(
		Narrow,
		FVector(-300.0f, 0.0f, 190.0f),
		FVector(80.0f, 300.0f, 110.0f),
		EABTSM7BuildingMaterial::Wood,
		EABTSM73BrickSemanticRole::Connector);
	FABTSM73DAGFailurePlayabilityResult NarrowResult;
	Error.Reset();
	TestFalse(
		TEXT("A point-visible but bird-width narrow corridor is rejected"),
		PlanDAG3CFixture(
			Settings,
			Profiles,
			FVector::ForwardVector,
			Narrow,
			NarrowResult,
			Error));
	TestTrue(
		TEXT("Narrow corridor reports attack blockage"),
		Error.StartsWith(TEXT("DAG3CAttackCorridorBlocked"))
			|| Error.StartsWith(TEXT("DAG3CAttackExposureTooSmall")));
	TestFalse(TEXT("Narrow corridor rejection is atomic"),
		HasDirtyWeakState(Narrow));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3CMotionSweepTest,
	"ABTS.M73DAG3.C.MotionSweepClearance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3CMotionSweepTest::RunTest(
	const FString& Parameters)
{
	const TArray<FABTSM7MaterialProfile> Profiles =
		FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
	const FABTSM73DAGFailurePlayabilitySettings Settings =
		MakeDAG3CSettings();
	const EABTSM73DAGFailurePattern Patterns[] = {
		EABTSM73DAGFailurePattern::InternalSingleSupport,
		EABTSM73DAGFailurePattern::InternalAsymmetricDualSupport,
		EABTSM73DAGFailurePattern::InternalOffsetSeam
	};
	for (const EABTSM73DAGFailurePattern Pattern : Patterns)
	{
		FABTSM73StructureData Data =
			MakeDAG3CFixture(Pattern, EABTSM7BuildingMaterial::Wood);
		FABTSM73DAGFailurePlayabilityResult Result;
		FString Error;
		TestTrue(
			FString::Printf(
				TEXT("Pattern %d has certified static motion space: %s"),
				static_cast<int32>(Pattern),
				*Error),
			PlanDAG3CFixture(
				Settings,
				Profiles,
				FVector::ForwardVector,
				Data,
				Result,
				Error));
		TestTrue(TEXT("Motion certification consumes bounded samples"),
			Result.MotionSweepSampleCount > 0
				&& Result.MotionSweepSampleCount
					<= Settings.MaxMotionSweepSampleCount);
		if (Pattern
			== EABTSM73DAGFailurePattern::InternalSingleSupport)
		{
			TestTrue(TEXT("Single certifies the minimum free Drop"),
				Result.FreeDropDistanceCM
					>= Settings.MinFreeDropDistanceCM);
		}
		else
		{
			TestTrue(TEXT("Pivot patterns certify the minimum Tip angle"),
				Result.FreeTipAngleDegrees
					>= Settings.MinFreeTipAngleDegrees);
			if (Pattern
				== EABTSM73DAGFailurePattern::InternalOffsetSeam)
			{
				TestTrue(TEXT("Seam certifies the minimum free Slide"),
					Result.FreeSlideDistanceCM
						>= Settings.MinFreeSlideDistanceCM);
			}
		}
	}

	struct FBlockedCase
	{
		EABTSM73DAGFailurePattern Pattern;
		FVector Center;
		FVector Dimensions;
		const TCHAR* ExpectedMotion;
	};
	const FBlockedCase BlockedCases[] = {
		{
			EABTSM73DAGFailurePattern::InternalSingleSupport,
			FVector(0.0f, 0.0f, 140.0f),
			FVector(40.0f, 40.0f, 20.0f),
			TEXT("Drop")
		},
		{
			EABTSM73DAGFailurePattern::InternalAsymmetricDualSupport,
			FVector(-20.0f, 0.0f, 143.0f),
			FVector(30.0f, 30.0f, 20.0f),
			TEXT("Tip")
		},
		{
			EABTSM73DAGFailurePattern::InternalOffsetSeam,
			FVector(-115.0f, 0.0f, 170.0f),
			FVector(10.0f, 40.0f, 16.0f),
			TEXT("Slide")
		}
	};
	for (const FBlockedCase& Blocked : BlockedCases)
	{
		FABTSM73StructureData Data = MakeDAG3CFixture(
			Blocked.Pattern,
			EABTSM7BuildingMaterial::Wood);
		AddDAG3CTestNode(
			Data,
			Blocked.Center,
			Blocked.Dimensions,
			EABTSM7BuildingMaterial::Wood,
			EABTSM73BrickSemanticRole::Connector);
		FABTSM73DAGFailurePlayabilityResult Result;
		FString Error;
		TestFalse(
			FString::Printf(
				TEXT("%s obstruction rejects the candidate"),
				Blocked.ExpectedMotion),
			PlanDAG3CFixture(
				Settings,
				Profiles,
				FVector::ForwardVector,
				Data,
				Result,
				Error));
		TestTrue(
			FString::Printf(
				TEXT("%s obstruction reports the motion stage"),
				Blocked.ExpectedMotion),
			Error.Contains(Blocked.ExpectedMotion));
		TestFalse(TEXT("Motion rejection leaves no partial weak state"),
			HasDirtyWeakState(Data));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3CMaterialMatrixTest,
	"ABTS.M73DAG3.C.MaterialProfileMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3CMaterialMatrixTest::RunTest(
	const FString& Parameters)
{
	const TArray<FABTSM7MaterialProfile> Profiles =
		FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
	const FABTSM73DAGFailurePlayabilitySettings Settings =
		MakeDAG3CSettings();
	const EABTSM73DAGFailurePattern Patterns[] = {
		EABTSM73DAGFailurePattern::InternalSingleSupport,
		EABTSM73DAGFailurePattern::InternalAsymmetricDualSupport,
		EABTSM73DAGFailurePattern::InternalOffsetSeam
	};
	const EABTSM7BuildingMaterial Materials[] = {
		EABTSM7BuildingMaterial::Wood,
		EABTSM7BuildingMaterial::Stone,
		EABTSM7BuildingMaterial::Iron,
		EABTSM7BuildingMaterial::Glass
	};
	TMap<EABTSM7BuildingMaterial, float> EffortByMaterial;
	for (const EABTSM73DAGFailurePattern Pattern : Patterns)
	{
		for (const EABTSM7BuildingMaterial Material : Materials)
		{
			FABTSM73StructureData Data =
				MakeDAG3CFixture(Pattern, Material);
			FABTSM73DAGFailurePlayabilityResult Result;
			FString Error;
			TestTrue(
				FString::Printf(
					TEXT("Pattern/material %d/%d certifies: %s"),
					static_cast<int32>(Pattern),
					static_cast<int32>(Material),
					*Error),
				PlanDAG3CFixture(
					Settings,
					Profiles,
					FVector::ForwardVector,
					Data,
					Result,
					Error));
			TestEqual(TEXT("Exactly one authoritative WeakPoint is bound"),
				Data.WeakPoints.Num(), 1);
			if (Data.WeakPoints.Num() == 1
				&& Data.DAGFailurePatternResult.WeakNodeIds.Num() == 1)
			{
				TestEqual(TEXT("Bound WeakPoint matches B's W"),
					Data.WeakPoints[0].NodeId,
					Data.DAGFailurePatternResult.WeakNodeIds[0]);
				TestEqual(TEXT("Weak record stores the DAG Pattern"),
					Data.WeakPoints[0].DAGFailurePattern, Pattern);
			}
			TestEqual(TEXT("C result keeps the actual material"),
				Result.Material, Material);
			for (const FABTSM73BrickNode& Node : Data.Bricks)
			{
				TestTrue(TEXT("DAG3-C never substitutes a weak material"),
					Node.Material == Material
						&& Node.OriginalMaterial == Material);
			}
			EffortByMaterial.FindOrAdd(Material) =
				Result.LocalBreakEffort;
		}
	}
	TestTrue(TEXT("Glass break effort is below Wood"),
		EffortByMaterial.FindRef(EABTSM7BuildingMaterial::Glass)
			< EffortByMaterial.FindRef(EABTSM7BuildingMaterial::Wood));
	TestTrue(TEXT("Wood break effort is below Stone"),
		EffortByMaterial.FindRef(EABTSM7BuildingMaterial::Wood)
			< EffortByMaterial.FindRef(EABTSM7BuildingMaterial::Stone));
	TestTrue(TEXT("Stone break effort is below Iron"),
		EffortByMaterial.FindRef(EABTSM7BuildingMaterial::Stone)
			< EffortByMaterial.FindRef(EABTSM7BuildingMaterial::Iron));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3CAtomicFailureTest,
	"ABTS.M73DAG3.C.AtomicFailureAndProfileOptIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3CAtomicFailureTest::RunTest(
	const FString& Parameters)
{
	const TArray<FABTSM7MaterialProfile> Profiles =
		FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
	FABTSM73StructureData Disabled = MakeDAG3CFixture(
		EABTSM73DAGFailurePattern::InternalSingleSupport,
		EABTSM7BuildingMaterial::Wood);
	const FABTSM73StructureData DisabledBaseline = Disabled;
	FABTSM73DAGFailurePlayabilitySettings DisabledSettings;
	FABTSM73DAGFailurePlayabilityResult DisabledResult;
	FString Error;
	TestTrue(TEXT("Disabled DAG3-C is a successful no-op"),
		PlanDAG3CFixture(
			DisabledSettings,
			Profiles,
			FVector::ForwardVector,
			Disabled,
			DisabledResult,
			Error));
	TestFalse(TEXT("Disabled result is not enabled"),
		DisabledResult.bEnabled);
	TestEqual(TEXT("Disabled no-op preserves brick count"),
		Disabled.Bricks.Num(), DisabledBaseline.Bricks.Num());
	TestEqual(TEXT("Disabled no-op preserves B geometry hash"),
		Disabled.DAGFailurePatternResult.RealizedPatternHash,
		DisabledBaseline.DAGFailurePatternResult.RealizedPatternHash);
	TestFalse(TEXT("Disabled no-op binds no WeakPoint"),
		HasDirtyWeakState(Disabled));

	FABTSM73StructureData MissingPrerequisite = MakeDAG3CFixture(
		EABTSM73DAGFailurePattern::InternalSingleSupport,
		EABTSM7BuildingMaterial::Wood);
	MissingPrerequisite.DAGFailurePatternResult.bApplied = false;
	FABTSM73DAGFailurePlayabilityResult MissingResult;
	Error.Reset();
	TestFalse(TEXT("C without an applied B pattern fails closed"),
		PlanDAG3CFixture(
			MakeDAG3CSettings(),
			Profiles,
			FVector::ForwardVector,
			MissingPrerequisite,
			MissingResult,
			Error));
	TestEqual(TEXT("Missing prerequisite is explicit"),
		Error, FString(TEXT("DAG3CPrerequisiteMissing")));
	TestFalse(TEXT("Prerequisite rejection is atomic"),
		HasDirtyWeakState(MissingPrerequisite));
	TestTrue(TEXT("Prerequisite rejection returns no partial metrics"),
		IsAtomicRejectedResult(MissingResult));

	TArray<FABTSM7MaterialProfile> MissingProfiles = Profiles;
	MissingProfiles.RemoveAll([](const FABTSM7MaterialProfile& Profile)
	{
		return Profile.Material == EABTSM7BuildingMaterial::Wood;
	});
	FABTSM73StructureData MissingProfile = MakeDAG3CFixture(
		EABTSM73DAGFailurePattern::InternalSingleSupport,
		EABTSM7BuildingMaterial::Wood);
	FABTSM73DAGFailurePlayabilityResult MissingProfileResult;
	Error.Reset();
	TestFalse(TEXT("Missing actual material Profile fails closed"),
		PlanDAG3CFixture(
			MakeDAG3CSettings(),
			MissingProfiles,
			FVector::ForwardVector,
			MissingProfile,
			MissingProfileResult,
			Error));
	TestEqual(TEXT("Missing Profile is explicit"),
		Error,
		FString(TEXT("DAG3CMaterialProfileMissingOrInvalid")));
	TestFalse(TEXT("Material rejection is atomic"),
		HasDirtyWeakState(MissingProfile));
	TestTrue(TEXT("Material rejection returns no partial metrics"),
		IsAtomicRejectedResult(MissingProfileResult));

	FABTSM73StructureData PrimaryMismatch = MakeDAG3CFixture(
		EABTSM73DAGFailurePattern::InternalSingleSupport,
		EABTSM7BuildingMaterial::Wood);
	FABTSM73DAGFailurePlayabilityResult PrimaryMismatchResult;
	Error.Reset();
	TestFalse(TEXT("Primary material mismatch fails closed"),
		PlanDAG3CFixture(
			MakeDAG3CSettings(),
			Profiles,
			FVector::ForwardVector,
			PrimaryMismatch,
			PrimaryMismatchResult,
			Error,
			EABTSM7BuildingMaterial::Stone));
	TestEqual(TEXT("Primary material mismatch is explicit"),
		Error, FString(TEXT("DAG3CPrimaryMaterialMismatch")));
	TestFalse(TEXT("Primary material mismatch leaves data untouched"),
		HasDirtyWeakState(PrimaryMismatch));
	TestTrue(TEXT("Primary mismatch returns no partial metrics"),
		IsAtomicRejectedResult(PrimaryMismatchResult));

	FABTSM73DAGFailurePlayabilitySettings InvalidSettings =
		MakeDAG3CSettings();
	InvalidSettings.TranslationSweepStepCM = 0.1f;
	FABTSM73StructureData InvalidSettingsData = MakeDAG3CFixture(
		EABTSM73DAGFailurePattern::InternalSingleSupport,
		EABTSM7BuildingMaterial::Wood);
	FABTSM73DAGFailurePlayabilityResult InvalidSettingsResult;
	Error.Reset();
	TestFalse(TEXT("Programmatic settings outside UPROPERTY bounds reject"),
		PlanDAG3CFixture(
			InvalidSettings,
			Profiles,
			FVector::ForwardVector,
			InvalidSettingsData,
			InvalidSettingsResult,
			Error));
	TestEqual(TEXT("Invalid settings rejection is explicit"),
		Error, FString(TEXT("DAG3CSettingsInvalid")));
	TestTrue(TEXT("Invalid settings return no partial metrics"),
		IsAtomicRejectedResult(InvalidSettingsResult));

	FABTSM73DifficultySettings InvalidDifficulty;
	InvalidDifficulty.TargetWeakCollapseRatio =
		InvalidDifficulty.MaxSingleWeakCollapseRatio + 0.1f;
	FABTSM73StructureData InvalidDifficultyData = MakeDAG3CFixture(
		EABTSM73DAGFailurePattern::InternalSingleSupport,
		EABTSM7BuildingMaterial::Wood);
	FABTSM73DAGFailurePlayabilityResult InvalidDifficultyResult;
	Error.Reset();
	TestFalse(TEXT("Invalid difficulty ordering rejects"),
		PlanDAG3CFixture(
			MakeDAG3CSettings(),
			Profiles,
			FVector::ForwardVector,
			InvalidDifficultyData,
			InvalidDifficultyResult,
			Error,
			TOptional<EABTSM7BuildingMaterial>(),
			&InvalidDifficulty));
	TestEqual(TEXT("Invalid difficulty rejection is explicit"),
		Error, FString(TEXT("DAG3CDifficultySettingsInvalid")));
	TestTrue(TEXT("Invalid difficulty returns no partial metrics"),
		IsAtomicRejectedResult(InvalidDifficultyResult));

	FABTSM73StructureData First = MakeDAG3CFixture(
		EABTSM73DAGFailurePattern::InternalOffsetSeam,
		EABTSM7BuildingMaterial::Stone);
	FABTSM73StructureData Second = First;
	FABTSM73DAGFailurePlayabilityResult FirstResult;
	FABTSM73DAGFailurePlayabilityResult SecondResult;
	FString FirstError;
	FString SecondError;
	TestTrue(TEXT("First deterministic certification succeeds"),
		PlanDAG3CFixture(
			MakeDAG3CSettings(),
			Profiles,
			FVector::ForwardVector,
			First,
			FirstResult,
			FirstError));
	TestTrue(TEXT("Repeated deterministic certification succeeds"),
		PlanDAG3CFixture(
			MakeDAG3CSettings(),
			Profiles,
			FVector::ForwardVector,
			Second,
			SecondResult,
			SecondError));
	TestNotEqual(TEXT("Playability identity is non-zero"),
		FirstResult.PlayabilityHash, 0u);
	TestEqual(TEXT("Repeated playability identity is exact"),
		FirstResult.PlayabilityHash, SecondResult.PlayabilityHash);
	TestEqual(TEXT("Repeated weak binding count is exact"),
		First.WeakPoints.Num(), Second.WeakPoints.Num());
	if (First.WeakPoints.Num() == 1 && Second.WeakPoints.Num() == 1)
	{
		TestEqual(TEXT("Repeated weak binding is exact"),
			First.WeakPoints[0].NodeId, Second.WeakPoints[0].NodeId);
	}

	TArray<FABTSM7MaterialProfile> ModifiedProfiles = Profiles;
	FABTSM7MaterialProfile* ModifiedStone =
		ModifiedProfiles.FindByPredicate(
			[](const FABTSM7MaterialProfile& Profile)
			{
				return Profile.Material
					== EABTSM7BuildingMaterial::Stone;
			});
	TestNotNull(TEXT("Stone profile exists for identity sensitivity"),
		ModifiedStone);
	if (ModifiedStone != nullptr)
	{
		ModifiedStone->BreakSpeedCMPerSec += 1.0f;
		FABTSM73StructureData Modified = MakeDAG3CFixture(
			EABTSM73DAGFailurePattern::InternalOffsetSeam,
			EABTSM7BuildingMaterial::Stone);
		FABTSM73DAGFailurePlayabilityResult ModifiedResult;
		FString ModifiedError;
		TestTrue(TEXT("Modified valid Profile still certifies"),
			PlanDAG3CFixture(
				MakeDAG3CSettings(),
				ModifiedProfiles,
				FVector::ForwardVector,
				Modified,
				ModifiedResult,
				ModifiedError));
		TestNotEqual(TEXT("Profile physics change alters C identity"),
			ModifiedResult.PlayabilityHash,
			FirstResult.PlayabilityHash);
		TestNotEqual(TEXT("Certified result stores actual break speed"),
			ModifiedResult.MaterialBreakSpeedCMPerSec,
			FirstResult.MaterialBreakSpeedCMPerSec);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3CPipelineIntegrationTest,
	"ABTS.M73DAG3.C.Pipeline.PatternsBudgetAndGeneralizedBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3CPipelineIntegrationTest::RunTest(
	const FString& Parameters)
{
	const EABTSM73DAGFailurePattern Patterns[] = {
		EABTSM73DAGFailurePattern::InternalSingleSupport,
		EABTSM73DAGFailurePattern::InternalAsymmetricDualSupport,
		EABTSM73DAGFailurePattern::InternalOffsetSeam
	};
	for (const EABTSM73DAGFailurePattern Pattern : Patterns)
	{
		const FABTSM7TaskGraphBuildingProfile Profile =
			MakeDAG3CPipelineProfile(Pattern);
		FABTSM73StructureData Data;
		FString Error;
		if (!TestTrue(
			FString::Printf(
				TEXT("Full A/B/C pipeline certifies Pattern %d: %s"),
				static_cast<int32>(Pattern),
				*Error),
			BuildDAG3CPipelineProfile(
				Profile,
				FVector::ForwardVector,
				Data,
				Error)))
		{
			continue;
		}
		TestTrue(TEXT("Pipeline commits the requested B pattern"),
			Data.DAGFailurePatternResult.bApplied
				&& Data.DAGFailurePatternResult.Pattern == Pattern);
		TestTrue(TEXT("Pipeline commits a playable C result"),
			Data.DAGFailurePlayabilityResult.bEnabled
				&& Data.DAGFailurePlayabilityResult.bPlayable
				&& Data.DAGFailurePlayabilityResult.PlayabilityHash != 0);
		TestEqual(TEXT("Pipeline binds one authoritative WeakPoint"),
			Data.WeakPoints.Num(), 1);
		TestTrue(TEXT("Pipeline stays inside the rewrite attempt budget"),
			Data.DAGFailurePatternResult.RewriteAttemptCount > 0
				&& Data.DAGFailurePatternResult.RewriteAttemptCount
					<= Profile.DAGFailurePatternSettings
						.MaxRewriteAttemptCount);
		TestTrue(TEXT("C binds the actual full material Profile"),
			Data.DAGFailurePlayabilityResult.bMaterialProfileValidated
				&& Data.DAGFailurePlayabilityResult.Material
					== Profile.GenerationSettings.PrimaryMaterial
				&& Data.DAGFailurePlayabilityResult
					.MaterialBreakSpeedCMPerSec > 0.0f
				&& Data.DAGFailurePlayabilityResult
					.MaterialDensityGPerCubicCM > 0.0f);
		for (const FABTSM73BrickNode& Node : Data.Bricks)
		{
			TestTrue(TEXT("Full pipeline never substitutes material"),
				Node.Material == Profile.GenerationSettings.PrimaryMaterial
					&& Node.OriginalMaterial
						== Profile.GenerationSettings.PrimaryMaterial);
		}
		if (Pattern != EABTSM73DAGFailurePattern::InternalSingleSupport)
		{
			const FVector FailureDirection =
				Data.DAGFailurePatternResult
					.ExpectedFailureDirectionLocal.GetSafeNormal();
			TestTrue(TEXT("Dual/Seam weak side faces the incoming bird"),
				FVector::DotProduct(
					-FVector::ForwardVector,
					FailureDirection) >= 0.25f);
		}
	}

	FABTSM7TaskGraphBuildingProfile BudgetProfile =
		MakeDAG3CPipelineProfile(
			EABTSM73DAGFailurePattern::InternalOffsetSeam);
	BudgetProfile.DAGFailurePatternSettings.MaxRewriteAttemptCount = 1;
	BudgetProfile.DAGFailurePlayabilitySettings.MinFreeSlideDistanceCM =
		300.0f;
	BudgetProfile.DAGFailurePlayabilitySettings.TranslationSweepStepCM =
		0.5f;
	BudgetProfile.DAGFailurePlayabilitySettings.MaxMotionSweepSampleCount =
		8;
	FABTSM73StructureData BudgetData;
	FString BudgetError;
	TestFalse(TEXT("C failure consumes the shared rewrite attempt budget"),
		BuildDAG3CPipelineProfile(
			BudgetProfile,
			FVector::ForwardVector,
			BudgetData,
			BudgetError));
	TestEqual(TEXT("C attempt exhaustion reports the exact shared bound"),
		BudgetError,
		FString(
			TEXT("DAG3CNoPlayablePattern:"
				"DAG3BRewriteAttemptBudgetExceeded:1:1")));
	TestFalse(TEXT("Attempt exhaustion commits no B or C state"),
		BudgetData.DAGFailurePatternResult.bApplied
			|| BudgetData.DAGFailurePlayabilityResult.bPlayable
			|| !BudgetData.WeakPoints.IsEmpty());

	FABTSM7TaskGraphBuildingProfile BoundaryProfile =
		MakeDAG3CPipelineProfile(
			EABTSM73DAGFailurePattern::InternalAsymmetricDualSupport);
	FABTSM73DAGGrammarExpander Expander;
	FABTSM73DAGGenerationResult Graph;
	FString Error;
	if (!TestTrue(TEXT("Boundary fixture expands the recursive DAG"),
		Expander.Generate(
			BoundaryProfile.DAGGenerationSettings,
			Graph,
			Error)))
	{
		return false;
	}
	FABTSM73DAGLayoutSolver LayoutSolver;
	FABTSM73DAGSpatialLayout Layout;
	if (!TestTrue(TEXT("Boundary fixture solves its baseline layout"),
		LayoutSolver.Solve(
			Graph,
			BoundaryProfile.DAGLayoutSettings,
			Layout,
			Error)))
	{
		return false;
	}
	FABTSM73DAGModuleCompiler Compiler;
	FABTSM73StructureData BaselineData;
	if (!TestTrue(TEXT("Boundary fixture compiles physical mappings"),
		Compiler.Compile(
			BoundaryProfile.GenerationSettings,
			Graph,
			BoundaryProfile.DAGLayoutSettings,
			Layout,
			BaselineData,
			Error)))
	{
		return false;
	}
	if (!TestTrue(TEXT("Boundary fixture has multiple macro interfaces"),
		BaselineData.DAGPhysicalSupportMappings.Num() >= 2))
	{
		return false;
	}
	const FABTSM73DAGPhysicalSupportMapping& MappingA =
		BaselineData.DAGPhysicalSupportMappings[0];
	const FABTSM73DAGPhysicalSupportMapping& MappingB =
		BaselineData.DAGPhysicalSupportMappings[1];
	if (!TestTrue(TEXT("Boundary mappings have physical columns"),
		!MappingA.ColumnNodeIds.IsEmpty()
			&& !MappingB.ColumnNodeIds.IsEmpty()))
	{
		return false;
	}
	FABTSM73DAGFailureFrontierCandidate CrossInterface;
	CrossInterface.bAccepted = true;
	CrossInterface.Kind =
		EABTSM73DAGFailureCandidateKind::BoundedSmallNodeCut;
	CrossInterface.CandidateNodeIds = {
		MappingA.ColumnNodeIds[0],
		MappingB.ColumnNodeIds[0]
	};
	FABTSM73DAGFailureEdgeRef& EdgeA =
		CrossInterface.CandidateEdges.AddDefaulted_GetRef();
	EdgeA.LowerNodeId = MappingA.ColumnNodeIds[0];
	EdgeA.UpperNodeId = MappingA.LoadPlateNodeId;
	FABTSM73DAGFailureEdgeRef& EdgeB =
		CrossInterface.CandidateEdges.AddDefaulted_GetRef();
	EdgeB.LowerNodeId = MappingB.ColumnNodeIds[0];
	EdgeB.UpperNodeId = MappingB.LoadPlateNodeId;
	CrossInterface.ProtectedRootNodeIds = {
		MappingA.LoadPlateNodeId,
		MappingB.LoadPlateNodeId
	};
	CrossInterface.FrontierHash = 0xD3C0B001u;
	BaselineData.DAGFailureFrontierAnalysis.bEnabled = true;
	BaselineData.DAGFailureFrontierAnalysis.bAccepted = true;
	BaselineData.DAGFailureFrontierAnalysis.Candidates.Add(
		CrossInterface);

	FABTSM73DAGFailurePatternRewriter Rewriter;
	FABTSM73DAGFailureRewriteIntent Intent;
	Error.Reset();
	TestFalse(TEXT("Cross-interface generalized cut fails closed"),
		Rewriter.MakeIntent(
			BoundaryProfile.DAGFailurePatternSettings,
			BoundaryProfile.DifficultySettings,
			CrossInterface,
			Graph,
			Layout,
			BaselineData,
			EABTSM73DAGFailurePattern::InternalAsymmetricDualSupport,
			Intent,
			Error));
	TestEqual(TEXT("Cross-interface rejection is explicit"),
		Error, FString(TEXT("DAG3CGeneralizedCutNotRewritable")));

	FABTSM73DAGFailureFrontierCandidate Tampered = CrossInterface;
	Tampered.CandidateEdges[0].UpperNodeId =
		MappingB.LoadPlateNodeId;
	Error.Reset();
	TestFalse(TEXT("Candidate-edge tampering invalidates source identity"),
		Rewriter.MakeIntent(
			BoundaryProfile.DAGFailurePatternSettings,
			BoundaryProfile.DifficultySettings,
			Tampered,
			Graph,
			Layout,
			BaselineData,
			EABTSM73DAGFailurePattern::InternalAsymmetricDualSupport,
			Intent,
			Error));
	TestEqual(TEXT("Stale edge identity rejection is explicit"),
		Error, FString(TEXT("DAG3BStaleSourceFrontier")));
	return true;
}

#endif
