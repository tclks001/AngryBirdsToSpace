// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Building/ABTSM73BeamAGenerator.h"
#include "Building/ABTSM73BeamBGenerator.h"
#include "Building/ABTSM73DAG5BShapeGrammarV2.h"
#include "Misc/AutomationTest.h"

namespace ABTSM73BeamBTests
{
	bool GenerateUpstream(
		const FABTSM73BeamBPreviewSettings& Settings,
		FABTSM73DAG5BV2GenerationResult& OutSilhouette,
		FABTSM73BeamAGenerationResult& OutBeamA,
		FString& OutError)
	{
		FABTSM73DAG5BShapeGrammarV2 ShapeGenerator;
		if (!ShapeGenerator.Generate(Settings.BeamA.Silhouette,
			OutSilhouette, OutError))
		{
			return false;
		}
		FABTSM73BeamAGenerator BeamAGenerator;
		return BeamAGenerator.Generate(Settings.BeamA, OutSilhouette,
			OutBeamA, OutError);
	}

	bool Generate(
		const FABTSM73BeamBPreviewSettings& Settings,
		FABTSM73BeamBGenerationResult& OutResult,
		FString& OutError)
	{
		FABTSM73DAG5BV2GenerationResult Silhouette;
		FABTSM73BeamAGenerationResult BeamA;
		if (!GenerateUpstream(Settings, Silhouette, BeamA, OutError))
		{
			return false;
		}
		FABTSM73BeamBGenerator Generator;
		return Generator.Generate(Settings, Silhouette, BeamA,
			OutResult, OutError);
	}

	FABTSM73BeamBPreviewSettings SettingsForSeed(const int32 Seed)
	{
		FABTSM73BeamBPreviewSettings Settings;
		Settings.BeamA.Silhouette.BuildingSeed = Seed;
		Settings.BeamA.Silhouette.Archetype =
			EABTSM73DAG5BV2Archetype::BridgedArcology;
		Settings.BeamA.Silhouette.MinGrammarDepth = 2;
		Settings.BeamA.Silhouette.MaxGrammarDepth = 4;
		Settings.BeamA.TargetBaySpanCM = 480.0f;
		Settings.GrammarDepth = 2;
		return Settings;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamBDeterminismTest,
	"ABTS.M73DAG.BeamB.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamBDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamBTests;
	const FABTSM73BeamBPreviewSettings Settings = SettingsForSeed(735201);
	FABTSM73BeamBGenerationResult A;
	FABTSM73BeamBGenerationResult B;
	FString Error;
	TestTrue(TEXT("First generation succeeds"), Generate(Settings, A, Error));
	TestTrue(TEXT("Second generation succeeds"), Generate(Settings, B, Error));
	TestEqual(TEXT("Motif hash is deterministic"),
		A.Summary.MotifWFCHash, B.Summary.MotifWFCHash);
	TestEqual(TEXT("Grammar hash is deterministic"),
		A.Summary.GraphGrammarHash, B.Summary.GraphGrammarHash);
	TestEqual(TEXT("Result hash is deterministic"),
		A.Summary.ResultHash, B.Summary.ResultHash);
	TestEqual(TEXT("Placement count is deterministic"),
		A.Placements.Num(), B.Placements.Num());
	TestEqual(TEXT("Member count is deterministic"),
		A.PlannedMembers.Num(), B.PlannedMembers.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamBMotifCoverageTest,
	"ABTS.M73DAG.BeamB.MotifCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamBMotifCoverageTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamBTests;
	TSet<EABTSM73BeamBMotif> Seen;
	bool bSawBridgeVolume = false;
	bool bBridgeForced = true;
	const EABTSM73DAG5BV2Archetype Archetypes[] = {
		EABTSM73DAG5BV2Archetype::TerracedCitadel,
		EABTSM73DAG5BV2Archetype::TwinTowerComplex,
		EABTSM73DAG5BV2Archetype::BridgedArcology,
		EABTSM73DAG5BV2Archetype::SpiredCampus};
	for (const EABTSM73DAG5BV2Archetype Archetype : Archetypes)
	{
			const int32 Value = static_cast<int32>(Archetype);
			FABTSM73BeamBPreviewSettings Settings = SettingsForSeed(
				940000 + Value * 211);
			Settings.BeamA.Silhouette.Archetype = Archetype;
			FABTSM73DAG5BV2GenerationResult Silhouette;
			FABTSM73BeamAGenerationResult BeamA;
			FABTSM73BeamBGenerationResult Result;
			FString Error;
			if (!GenerateUpstream(Settings, Silhouette, BeamA, Error))
			{
				AddError(FString::Printf(TEXT("Upstream failed: %s"), *Error));
				return false;
			}
			FABTSM73BeamBGenerator Generator;
			if (!Generator.Generate(Settings, Silhouette, BeamA, Result, Error))
			{
				AddError(FString::Printf(TEXT("Beam-B failed: %s"), *Error));
				return false;
			}
			for (const FABTSM73BeamBPlacement& Placement : Result.Placements)
			{
				Seen.Add(Placement.Motif);
				const FABTSM73BeamABay& Bay = BeamA.Bays[Placement.BayId];
				const FABTSM73DAG5BV2Volume* Volume =
					Silhouette.Volumes.FindByPredicate(
						[&](const FABTSM73DAG5BV2Volume& Candidate)
						{
							return Candidate.VolumeId == Bay.SourceVolumeId;
						});
				if (Volume != nullptr
					&& Volume->Role == EABTSM73DAG5BV2VolumeRole::Bridge)
				{
					bSawBridgeVolume = true;
					bBridgeForced &= Placement.Motif
						== EABTSM73BeamBMotif::BridgeBay;
				}
			}
	}
	TestTrue(TEXT("Seed matrix covers at least six motif families"),
		Seen.Num() >= 6);
	TestTrue(TEXT("Matrix contains a bridge volume"), bSawBridgeVolume);
	TestTrue(TEXT("Bridge volumes force BridgeBay"), bBridgeForced);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamBPortCompatibilityTest,
	"ABTS.M73DAG.BeamB.PortCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamBPortCompatibilityTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamBTests;
	for (int32 Value = static_cast<int32>(
		EABTSM73DAG5BV2Archetype::TerracedCitadel);
		Value <= static_cast<int32>(
			EABTSM73DAG5BV2Archetype::SpiredCampus); ++Value)
	{
		FABTSM73BeamBPreviewSettings Settings = SettingsForSeed(
			940000 + Value * 211);
		Settings.BeamA.Silhouette.Archetype =
			static_cast<EABTSM73DAG5BV2Archetype>(Value);
		FABTSM73BeamBGenerationResult Result;
		FString Error;
		TestTrue(FString::Printf(TEXT("Archetype %d accepts: %s"),
			Value, *Error), Generate(Settings, Result, Error));
		TestEqual(TEXT("No port violations"),
			Result.Summary.PortViolationCount, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamBGrammarDepthTest,
	"ABTS.M73DAG.BeamB.GrammarDepthAddsTopology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamBGrammarDepthTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamBTests;
	FABTSM73BeamBPreviewSettings ShallowSettings = SettingsForSeed(735201);
	ShallowSettings.GrammarDepth = 1;
	FABTSM73BeamBPreviewSettings DeepSettings = ShallowSettings;
	DeepSettings.GrammarDepth = 4;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FABTSM73BeamAGenerationResult BeamA;
	FString Error;
	TestTrue(TEXT("Upstream accepts"), GenerateUpstream(
		ShallowSettings, Silhouette, BeamA, Error));
	FABTSM73BeamBGenerator Generator;
	FABTSM73BeamBGenerationResult Shallow;
	FABTSM73BeamBGenerationResult Deep;
	TestTrue(TEXT("Shallow accepts"), Generator.Generate(
		ShallowSettings, Silhouette, BeamA, Shallow, Error));
	TestTrue(TEXT("Deep accepts"), Generator.Generate(
		DeepSettings, Silhouette, BeamA, Deep, Error));
	TestEqual(TEXT("Motif collapse remains stable across grammar depth"),
		Shallow.Summary.MotifWFCHash, Deep.Summary.MotifWFCHash);
	TestTrue(TEXT("Deep grammar adds rule steps"),
		Deep.GrammarSteps.Num() > Shallow.GrammarSteps.Num());
	TestTrue(TEXT("Deep grammar adds planned topology"),
		Deep.PlannedMembers.Num() > Shallow.PlannedMembers.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamBBoundsBudgetTest,
	"ABTS.M73DAG.BeamB.BoundsAndBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamBBoundsBudgetTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamBTests;
	FABTSM73BeamBPreviewSettings Settings = SettingsForSeed(735201);
	FABTSM73BeamBGenerationResult Accepted;
	FString Error;
	TestTrue(TEXT("Normal settings accept"), Generate(Settings, Accepted, Error));
	TestEqual(TEXT("No member leaves its Bay"),
		Accepted.Summary.OutOfBoundsMemberCount, 0);

	FABTSM73DAG5BV2GenerationResult Silhouette;
	FABTSM73BeamAGenerationResult BeamA;
	TestTrue(TEXT("Upstream accepts"), GenerateUpstream(
		Settings, Silhouette, BeamA, Error));
	Settings.MaxPlannedMemberCount = 4;
	FABTSM73BeamBGenerationResult Rejected;
	FABTSM73BeamBGenerator Generator;
	TestFalse(TEXT("Insufficient member budget rejects"), Generator.Generate(
		Settings, Silhouette, BeamA, Rejected, Error));
	TestEqual(TEXT("Budget failure is stable"), Error,
		FString(TEXT("BeamBPlannedMemberBudgetExceeded")));
	TestTrue(TEXT("Rejected result does not leak a partial graph"),
		Rejected.PlannedMembers.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamBInvalidSettingsTest,
	"ABTS.M73DAG.BeamB.InvalidSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamBInvalidSettingsTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamBTests;
	FABTSM73BeamBPreviewSettings Settings = SettingsForSeed(735201);
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FABTSM73BeamAGenerationResult BeamA;
	FString Error;
	TestTrue(TEXT("Upstream accepts"), GenerateUpstream(
		Settings, Silhouette, BeamA, Error));
	Settings.GrammarDepth = 0;
	FABTSM73BeamBGenerationResult Result;
	FABTSM73BeamBGenerator Generator;
	TestFalse(TEXT("Invalid depth rejects"), Generator.Generate(
		Settings, Silhouette, BeamA, Result, Error));
	TestEqual(TEXT("Invalid reason is stable"), Error,
		FString(TEXT("BeamBInvalidSettings")));
	return true;
}

#endif
