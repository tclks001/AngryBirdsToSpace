// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAG5BShapeGrammarV2.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ABTSM73DAG5BV2Tests
{
	FABTSM73DAG5BV2PreviewSettings MakeSettings()
	{
		FABTSM73DAG5BV2PreviewSettings Settings;
		Settings.BuildingSeed = 735201;
		Settings.Archetype =
			EABTSM73DAG5BV2Archetype::BridgedArcology;
		Settings.MinGrammarDepth = 2;
		Settings.MaxGrammarDepth = 4;
		Settings.MaxVolumeCount = 96;
		Settings.bRequirePrimitiveVariety = true;
		return Settings;
	}

	bool Generate(
		const FABTSM73DAG5BV2PreviewSettings& Settings,
		FABTSM73DAG5BV2GenerationResult& OutResult,
		FString& OutError)
	{
		FABTSM73DAG5BShapeGrammarV2 Generator;
		return Generator.Generate(Settings, OutResult, OutError);
	}

	bool EqualVolume(
		const FABTSM73DAG5BV2Volume& A,
		const FABTSM73DAG5BV2Volume& B)
	{
		return A.VolumeId == B.VolumeId
			&& A.GrammarDepth == B.GrammarDepth
			&& A.LocalBounds.Min.Equals(B.LocalBounds.Min, 0.001)
			&& A.LocalBounds.Max.Equals(B.LocalBounds.Max, 0.001)
			&& A.Role == B.Role
			&& A.Primitive == B.Primitive
			&& A.DerivationPath == B.DerivationPath;
	}

	float OverlapLength(
		const double AMin,
		const double AMax,
		const double BMin,
		const double BMax)
	{
		return FMath::Max(
			0.0,
			FMath::Min(AMax, BMax) - FMath::Max(AMin, BMin));
	}

	bool HasDirectUpperVolume(
		const int32 LowerIndex,
		const TArray<FABTSM73DAG5BV2Volume>& Volumes)
	{
		const FBox& Lower = Volumes[LowerIndex].LocalBounds;
		for (int32 UpperIndex = 0; UpperIndex < Volumes.Num(); ++UpperIndex)
		{
			if (UpperIndex == LowerIndex)
			{
				continue;
			}
			const FBox& Upper = Volumes[UpperIndex].LocalBounds;
			if (FMath::Abs(Lower.Max.Z - Upper.Min.Z) <= 1.0
				&& OverlapLength(
					Lower.Min.X,
					Lower.Max.X,
					Upper.Min.X,
					Upper.Max.X) > 1.0
				&& OverlapLength(
					Lower.Min.Y,
					Lower.Max.Y,
					Upper.Min.Y,
					Upper.Max.Y) > 1.0)
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5BV2DeterminismTest,
	"ABTS.M73DAG.DAG5Bv2.Determinism",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5BV2DeterminismTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73DAG5BV2Tests;
	const FABTSM73DAG5BV2PreviewSettings Settings = MakeSettings();
	FABTSM73DAG5BV2GenerationResult A;
	FABTSM73DAG5BV2GenerationResult B;
	FString ErrorA;
	FString ErrorB;
	TestTrue(TEXT("First generation succeeds"), Generate(
		Settings,
		A,
		ErrorA));
	TestTrue(TEXT("Second generation succeeds"), Generate(
		Settings,
		B,
		ErrorB));
	TestEqual(TEXT("Reject reasons"), ErrorA, ErrorB);
	TestEqual(TEXT("Grammar hash"), A.Summary.GrammarHash, B.Summary.GrammarHash);
	TestEqual(TEXT("WFC hash"), A.Summary.WFCHash, B.Summary.WFCHash);
	TestEqual(TEXT("Result hash"), A.Summary.ResultHash, B.Summary.ResultHash);
	TestEqual(TEXT("Volume count"), A.Volumes.Num(), B.Volumes.Num());
	if (A.Volumes.Num() == B.Volumes.Num())
	{
		for (int32 Index = 0; Index < A.Volumes.Num(); ++Index)
		{
			TestTrue(
				FString::Printf(TEXT("Volume %d matches"), Index),
				EqualVolume(A.Volumes[Index], B.Volumes[Index]));
		}
	}

	FABTSM73DAG5BV2PreviewSettings Variant = Settings;
	Variant.BuildingSeed += 1;
	FABTSM73DAG5BV2GenerationResult C;
	FString ErrorC;
	TestTrue(TEXT("Variant generation succeeds"), Generate(
		Variant,
		C,
		ErrorC));
	TestNotEqual(
		TEXT("Seed changes complete identity"),
		A.Summary.ResultHash,
		C.Summary.ResultHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5BV2DepthGrowthTest,
	"ABTS.M73DAG.DAG5Bv2.DepthGrowth",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5BV2DepthGrowthTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73DAG5BV2Tests;
	FABTSM73DAG5BV2PreviewSettings Shallow = MakeSettings();
	Shallow.MinGrammarDepth = 1;
	Shallow.MaxGrammarDepth = 1;
	Shallow.TerminalWeight = 0.0f;
	FABTSM73DAG5BV2PreviewSettings Deep = Shallow;
	Deep.MinGrammarDepth = 4;
	Deep.MaxGrammarDepth = 4;

	FABTSM73DAG5BV2GenerationResult ShallowResult;
	FABTSM73DAG5BV2GenerationResult DeepResult;
	FString ShallowError;
	FString DeepError;
	TestTrue(TEXT("Shallow generation succeeds"), Generate(
		Shallow,
		ShallowResult,
		ShallowError));
	TestTrue(TEXT("Deep generation succeeds"), Generate(
		Deep,
		DeepResult,
		DeepError));
	TestTrue(
		TEXT("Deep grammar emits more volumes"),
		DeepResult.Volumes.Num() > ShallowResult.Volumes.Num());
	TestTrue(
		TEXT("Deep grammar executes more rules"),
		DeepResult.Summary.GrammarStepCount
			> ShallowResult.Summary.GrammarStepCount);
	TestTrue(
		TEXT("Deep generation respects hard volume budget"),
		DeepResult.Volumes.Num() <= Deep.MaxVolumeCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5BV2ArchetypeCoverageTest,
	"ABTS.M73DAG.DAG5Bv2.ArchetypeCoverage",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5BV2ArchetypeCoverageTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73DAG5BV2Tests;
	for (int32 Value =
			static_cast<int32>(
				EABTSM73DAG5BV2Archetype::TerracedCitadel);
			Value <= static_cast<int32>(
				EABTSM73DAG5BV2Archetype::SpiredCampus);
			++Value)
	{
		FABTSM73DAG5BV2PreviewSettings Settings = MakeSettings();
		Settings.Archetype =
			static_cast<EABTSM73DAG5BV2Archetype>(Value);
		Settings.BuildingSeed = 820000 + Value * 173;
		FABTSM73DAG5BV2GenerationResult Result;
		FString Error;
		const bool bGenerated = Generate(Settings, Result, Error);
		TestTrue(
			FString::Printf(TEXT("Archetype %d succeeds: %s"), Value, *Error),
			bGenerated);
		if (!bGenerated)
		{
			continue;
		}
		TestEqual(
			FString::Printf(TEXT("Archetype %d identity"), Value),
			static_cast<int32>(Result.Summary.ResolvedArchetype),
			Value);
		TestTrue(
			FString::Printf(TEXT("Archetype %d has box"), Value),
			Result.Summary.BoxCount > 0);
		TestTrue(
			FString::Printf(TEXT("Archetype %d has prism"), Value),
			Result.Summary.PrismCount > 0);
		TestTrue(
			FString::Printf(TEXT("Archetype %d has pyramid"), Value),
			Result.Summary.PyramidCount > 0);
		TestTrue(
			FString::Printf(TEXT("Archetype %d is complex"), Value),
			Result.Volumes.Num() >= 8);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5BV2BoundsAndBudgetTest,
	"ABTS.M73DAG.DAG5Bv2.BoundsAndBudget",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5BV2BoundsAndBudgetTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73DAG5BV2Tests;
	FABTSM73DAG5BV2PreviewSettings Settings = MakeSettings();
	Settings.MaxVolumeCount = 24;
	FABTSM73DAG5BV2GenerationResult Result;
	FString Error;
	TestTrue(TEXT("Bounded generation succeeds"), Generate(
		Settings,
		Result,
		Error));
	TestTrue(
		TEXT("Volume budget is respected"),
		Result.Volumes.Num() <= Settings.MaxVolumeCount);
	const FBox Target(
		FVector(
			-Settings.TargetWidthCM * 0.5,
			-Settings.TargetDepthCM * 0.5,
			0.0),
		FVector(
			Settings.TargetWidthCM * 0.5,
			Settings.TargetDepthCM * 0.5,
			Settings.TargetHeightCM));
	for (const FABTSM73DAG5BV2Volume& Volume : Result.Volumes)
	{
		TestTrue(
			FString::Printf(
				TEXT("Volume %d min inside target"),
				Volume.VolumeId),
			Target.IsInsideOrOn(Volume.LocalBounds.Min));
		TestTrue(
			FString::Printf(
				TEXT("Volume %d max inside target"),
				Volume.VolumeId),
			Target.IsInsideOrOn(Volume.LocalBounds.Max));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5BV2RoofPrimitiveTerminalTest,
	"ABTS.M73DAG.DAG5Bv2.RoofPrimitiveTerminal",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5BV2RoofPrimitiveTerminalTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73DAG5BV2Tests;
	for (int32 ArchetypeValue =
			static_cast<int32>(
				EABTSM73DAG5BV2Archetype::TerracedCitadel);
			ArchetypeValue <= static_cast<int32>(
				EABTSM73DAG5BV2Archetype::SpiredCampus);
			++ArchetypeValue)
	{
		for (int32 SeedOffset = 0; SeedOffset < 8; ++SeedOffset)
		{
			FABTSM73DAG5BV2PreviewSettings Settings = MakeSettings();
			Settings.Archetype =
				static_cast<EABTSM73DAG5BV2Archetype>(ArchetypeValue);
			Settings.BuildingSeed =
				910000 + ArchetypeValue * 1000 + SeedOffset * 37;
			FABTSM73DAG5BV2GenerationResult Result;
			FString Error;
			const bool bGenerated = Generate(Settings, Result, Error);
			TestTrue(
				FString::Printf(
					TEXT("Archetype %d seed %d succeeds: %s"),
					ArchetypeValue,
					Settings.BuildingSeed,
					*Error),
				bGenerated);
			if (!bGenerated)
			{
				continue;
			}
			for (int32 Index = 0; Index < Result.Volumes.Num(); ++Index)
			{
				const FABTSM73DAG5BV2Volume& Volume =
					Result.Volumes[Index];
				const bool bRoofPrimitive =
					Volume.Primitive
						== EABTSM73DAG5BV2Primitive::TriangularPrismX
					|| Volume.Primitive
						== EABTSM73DAG5BV2Primitive::TriangularPrismY
					|| Volume.Primitive
						== EABTSM73DAG5BV2Primitive::Pyramid;
				if (bRoofPrimitive)
				{
					TestFalse(
						FString::Printf(
							TEXT(
								"Archetype %d seed %d roof volume %d "
								"is terminal"),
							ArchetypeValue,
							Settings.BuildingSeed,
							Volume.VolumeId),
						HasDirectUpperVolume(Index, Result.Volumes));
				}
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5BV2InvalidSettingsTest,
	"ABTS.M73DAG.DAG5Bv2.InvalidSettings",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5BV2InvalidSettingsTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73DAG5BV2Tests;
	FABTSM73DAG5BV2PreviewSettings Settings = MakeSettings();
	Settings.BoxWeight = 0.0f;
	Settings.PrismWeight = 0.0f;
	Settings.PyramidWeight = 0.0f;
	FABTSM73DAG5BV2GenerationResult Result;
	FString Error;
	TestFalse(TEXT("Zero WFC weights fail closed"), Generate(
		Settings,
		Result,
		Error));
	TestEqual(
		TEXT("Stable rejection reason"),
		Error,
		FString(TEXT("DAG5BV2InvalidSettings")));
	TestFalse(TEXT("No accepted partial result"), Result.Summary.bAccepted);
	TestEqual(TEXT("No partial volumes"), Result.Volumes.Num(), 0);
	return true;
}

#endif
