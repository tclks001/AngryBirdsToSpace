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
			&& A.NegativeSupportVolumeId == B.NegativeSupportVolumeId
			&& A.PositiveSupportVolumeId == B.PositiveSupportVolumeId
			&& A.SpanAxisIndex == B.SpanAxisIndex
			&& FMath::IsNearlyEqual(
				A.SpanOpeningMinCM, B.SpanOpeningMinCM, 0.001)
			&& FMath::IsNearlyEqual(
				A.SpanOpeningMaxCM, B.SpanOpeningMaxCM, 0.001)
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

	bool GroundFootprintsTouch(
		const FABTSM73DAG5BV2Volume& A,
		const FABTSM73DAG5BV2Volume& B)
	{
		const double XOverlap = OverlapLength(
			A.LocalBounds.Min.X,
			A.LocalBounds.Max.X,
			B.LocalBounds.Min.X,
			B.LocalBounds.Max.X);
		const double YOverlap = OverlapLength(
			A.LocalBounds.Min.Y,
			A.LocalBounds.Max.Y,
			B.LocalBounds.Min.Y,
			B.LocalBounds.Max.Y);
		const double XGap = FMath::Max(
			0.0,
			FMath::Max(
				A.LocalBounds.Min.X - B.LocalBounds.Max.X,
				B.LocalBounds.Min.X - A.LocalBounds.Max.X));
		const double YGap = FMath::Max(
			0.0,
			FMath::Max(
				A.LocalBounds.Min.Y - B.LocalBounds.Max.Y,
				B.LocalBounds.Min.Y - A.LocalBounds.Max.Y));
		return (XOverlap > 1.0 && YGap <= 1.0)
			|| (YOverlap > 1.0 && XGap <= 1.0)
			|| (XOverlap > 1.0 && YOverlap > 1.0);
	}

	int32 CountGroundComponents(
		const TArray<FABTSM73DAG5BV2Volume>& Volumes)
	{
		TArray<int32> GroundIndices;
		for (int32 Index = 0; Index < Volumes.Num(); ++Index)
		{
			if (Volumes[Index].LocalBounds.Min.Z <= 1.0
				&& Volumes[Index].Role
					!= EABTSM73DAG5BV2VolumeRole::SupportedSpan)
			{
				GroundIndices.Add(Index);
			}
		}
		TArray<bool> Visited;
		Visited.Init(false, GroundIndices.Num());
		int32 Count = 0;
		for (int32 Start = 0; Start < GroundIndices.Num(); ++Start)
		{
			if (Visited[Start])
			{
				continue;
			}
			++Count;
			TArray<int32> Queue{Start};
			Visited[Start] = true;
			for (int32 Head = 0; Head < Queue.Num(); ++Head)
			{
				for (int32 Other = 0; Other < GroundIndices.Num(); ++Other)
				{
					if (!Visited[Other]
						&& GroundFootprintsTouch(
							Volumes[GroundIndices[Queue[Head]]],
							Volumes[GroundIndices[Other]]))
					{
						Visited[Other] = true;
						Queue.Add(Other);
					}
				}
			}
		}
		return Count;
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
	bool bSawMergedRoofTerminal = false;
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
			bSawMergedRoofTerminal |=
				Result.Summary.MergedRoofSourceCount > 0;
			TestTrue(TEXT("Generation retains a semantic roof terminal"),
				Result.Summary.RoofTerminalCount > 0);
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
					const FVector RoofSize = Volume.LocalBounds.GetSize();
					const double CourseCount =
						RoofSize.Z / Settings.RoofCourseHeightCM;
					TestTrue(TEXT("Roof height is quantized to whole Brick courses"),
						FMath::IsNearlyEqual(
							CourseCount,
							static_cast<double>(FMath::RoundToInt(CourseCount)),
							0.01));
					if (Volume.Primitive
						!= EABTSM73DAG5BV2Primitive::Pyramid)
					{
						const EABTSM73DAG5BV2Primitive ExpectedPrism =
							RoofSize.X >= RoofSize.Y
								? EABTSM73DAG5BV2Primitive::TriangularPrismY
								: EABTSM73DAG5BV2Primitive::TriangularPrismX;
						TestEqual(TEXT("Prism ridge follows the terminal long axis"),
							Volume.Primitive, ExpectedPrism);
					}
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
	TestTrue(TEXT("Seed matrix exercises pre-WFC roof aggregation"),
		bSawMergedRoofTerminal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5BV2SupportedSpanContractTest,
	"ABTS.M73DAG.DAG5Bv2.SupportedSpanContract",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5BV2SupportedSpanContractTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73DAG5BV2Tests;
	int32 SpanCount = 0;
	for (int32 ArchetypeValue = static_cast<int32>(
		EABTSM73DAG5BV2Archetype::TerracedCitadel);
		ArchetypeValue <= static_cast<int32>(
			EABTSM73DAG5BV2Archetype::SpiredCampus); ++ArchetypeValue)
	{
		FABTSM73DAG5BV2PreviewSettings Settings = MakeSettings();
		Settings.Archetype =
			static_cast<EABTSM73DAG5BV2Archetype>(ArchetypeValue);
		Settings.BuildingSeed = 950000 + ArchetypeValue * 307;
		FABTSM73DAG5BV2GenerationResult Result;
		FString Error;
		if (!Generate(Settings, Result, Error))
		{
			AddError(FString::Printf(TEXT("Generation failed: %s"), *Error));
			return false;
		}
		int32 ResultSpanCount = 0;
		for (const FABTSM73DAG5BV2Volume& Span : Result.Volumes)
		{
			if (Span.Role != EABTSM73DAG5BV2VolumeRole::SupportedSpan)
			{
				continue;
			}
			++SpanCount;
			++ResultSpanCount;
			TestTrue(TEXT("Span is intentionally elevated"),
				Span.LocalBounds.Min.Z > 1.0);
			TestTrue(TEXT("Span axis is horizontal"),
				Span.SpanAxisIndex == 0 || Span.SpanAxisIndex == 1);
			TestTrue(TEXT("Negative support identity is valid"),
				Result.Volumes.IsValidIndex(Span.NegativeSupportVolumeId));
			TestTrue(TEXT("Positive support identity is valid"),
				Result.Volumes.IsValidIndex(Span.PositiveSupportVolumeId));
			TestNotEqual(TEXT("Endpoint supports are distinct"),
				Span.NegativeSupportVolumeId,
				Span.PositiveSupportVolumeId);
			TestTrue(TEXT("Supported span has a non-empty clear opening"),
				Span.SpanOpeningMaxCM > Span.SpanOpeningMinCM);
			if (!Result.Volumes.IsValidIndex(Span.NegativeSupportVolumeId)
				|| !Result.Volumes.IsValidIndex(Span.PositiveSupportVolumeId)
				|| (Span.SpanAxisIndex != 0 && Span.SpanAxisIndex != 1))
			{
				continue;
			}
			const double Center =
				Span.LocalBounds.GetCenter()[Span.SpanAxisIndex];
			TestTrue(TEXT("Negative support is on the negative side"),
				Result.Volumes[Span.NegativeSupportVolumeId]
					.LocalBounds.GetCenter()[Span.SpanAxisIndex] < Center);
			TestTrue(TEXT("Positive support is on the positive side"),
				Result.Volumes[Span.PositiveSupportVolumeId]
					.LocalBounds.GetCenter()[Span.SpanAxisIndex] > Center);
		}
		TestEqual(TEXT("Summary reports supported spans"),
			Result.Summary.SupportedSpanCount, ResultSpanCount);
	}
	TestTrue(TEXT("Archetype matrix contains an intentional span"),
		SpanCount > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5BV2CoupledGroundLayerTest,
	"ABTS.M73DAG.DAG5Bv2.CoupledGroundLayer",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5BV2CoupledGroundLayerTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73DAG5BV2Tests;
	int32 HighestSemanticSeamArchetypeCount = 0;
	for (int32 ArchetypeValue = static_cast<int32>(
		EABTSM73DAG5BV2Archetype::TerracedCitadel);
		ArchetypeValue <= static_cast<int32>(
			EABTSM73DAG5BV2Archetype::SpiredCampus); ++ArchetypeValue)
	{
		FABTSM73DAG5BV2PreviewSettings Settings = MakeSettings();
		Settings.Archetype =
			static_cast<EABTSM73DAG5BV2Archetype>(ArchetypeValue);
		Settings.BuildingSeed = 970000 + ArchetypeValue * 401;
		FABTSM73DAG5BV2GenerationResult Result;
		FString Error;
		const bool bGenerated = Generate(Settings, Result, Error);
		TestTrue(
			FString::Printf(
				TEXT("Archetype %d coupled silhouette succeeds: %s"),
				ArchetypeValue,
				*Error),
			bGenerated);
		if (!bGenerated)
		{
			continue;
		}

		int32 CoupledGroundCellCount = 0;
		bool bUsesHighestSemanticSeam = false;
		for (const FString& Trace : Result.GrammarTrace)
		{
			bUsesHighestSemanticSeam |=
				Trace.Contains(TEXT("Selection(HighestLegal)"));
		}
		HighestSemanticSeamArchetypeCount += bUsesHighestSemanticSeam ? 1 : 0;
		for (const FABTSM73DAG5BV2Volume& Volume : Result.Volumes)
		{
			TestEqual(
				FString::Printf(TEXT("Volume %d keeps dense stable identity"),
					Volume.VolumeId),
				Volume.VolumeId,
				static_cast<int32>(&Volume - Result.Volumes.GetData()));
			TestTrue(
				FString::Printf(TEXT("Volume %d remains positive after semantic recut"),
					Volume.VolumeId),
				Volume.LocalBounds.IsValid
					&& Volume.LocalBounds.GetSize().GetMin() > 1.0);
			CoupledGroundCellCount +=
				Volume.DerivationPath.StartsWith(TEXT("CoupledGround/"))
					? 1
					: 0;
			if (Volume.DerivationPath.StartsWith(TEXT("CoupledGround/")))
			{
				const double CoursePairs = Volume.LocalBounds.Max.Z / 72.0;
				TestTrue(
					FString::Printf(
						TEXT("Coupled ground %d top is a 72 cm course pair"),
						Volume.VolumeId),
					FMath::IsNearlyEqual(
						CoursePairs, FMath::RoundToDouble(CoursePairs), 0.001));
			}
		}
		TestTrue(
			FString::Printf(
				TEXT("Archetype %d publishes coupled ground cells"),
				ArchetypeValue),
			CoupledGroundCellCount > 0);
		const int32 GroundComponentCount =
			CountGroundComponents(Result.Volumes);
		if (Result.Summary.SupportedSpanCount == 0)
		{
			TestEqual(
				FString::Printf(
					TEXT("Archetype %d has one coupled ground component"),
					ArchetypeValue),
				GroundComponentCount,
				1);
		}
		else
		{
			TestTrue(
				FString::Printf(
					TEXT("Archetype %d retains an intentional ground seam"),
					ArchetypeValue),
				GroundComponentCount > 1);
		}

		for (const FABTSM73DAG5BV2Volume& Span : Result.Volumes)
		{
			if (Span.Role != EABTSM73DAG5BV2VolumeRole::SupportedSpan)
			{
				continue;
			}
			for (const FABTSM73DAG5BV2Volume& Ground : Result.Volumes)
			{
				if (Ground.LocalBounds.Min.Z > 1.0
					|| Ground.Role
						== EABTSM73DAG5BV2VolumeRole::SupportedSpan)
				{
					continue;
				}
				const int32 Axis = Span.SpanAxisIndex;
				const int32 Perpendicular = Axis == 0 ? 1 : 0;
				const double OpeningOverlap = OverlapLength(
					Span.SpanOpeningMinCM,
					Span.SpanOpeningMaxCM,
					Ground.LocalBounds.Min[Axis],
					Ground.LocalBounds.Max[Axis]);
				const double WidthOverlap = OverlapLength(
					Span.LocalBounds.Min[Perpendicular],
					Span.LocalBounds.Max[Perpendicular],
					Ground.LocalBounds.Min[Perpendicular],
					Ground.LocalBounds.Max[Perpendicular]);
				TestFalse(
					FString::Printf(
						TEXT("Span %d protected undercroft remains empty"),
						Span.VolumeId),
					OpeningOverlap > 1.0 && WidthOverlap > 1.0);
			}
		}
	}
	TestTrue(TEXT("Archetype matrix exercises a raised highest semantic seam"),
		HighestSemanticSeamArchetypeCount > 0);
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
