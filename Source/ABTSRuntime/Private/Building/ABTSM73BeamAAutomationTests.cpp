// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BeamAGenerator.h"

#include "Building/ABTSM73DAG5BShapeGrammarV2.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ABTSM73BeamATests
{
	FABTSM73BeamAPreviewSettings MakeSettings()
	{
		FABTSM73BeamAPreviewSettings Settings;
		Settings.Silhouette.BuildingSeed = 735201;
		Settings.Silhouette.Archetype =
			EABTSM73DAG5BV2Archetype::BridgedArcology;
		Settings.Silhouette.MinGrammarDepth = 2;
		Settings.Silhouette.MaxGrammarDepth = 4;
		Settings.Silhouette.MaxVolumeCount = 96;
		Settings.Silhouette.bRequirePrimitiveVariety = true;
		Settings.TargetBaySpanCM = 480.0f;
		return Settings;
	}

	bool Generate(
		const FABTSM73BeamAPreviewSettings& Settings,
		FABTSM73BeamAGenerationResult& OutResult,
		FString& OutError)
	{
		FABTSM73DAG5BShapeGrammarV2 SilhouetteGenerator;
		FABTSM73DAG5BV2GenerationResult Silhouette;
		if (!SilhouetteGenerator.Generate(
			Settings.Silhouette,
			Silhouette,
			OutError))
		{
			return false;
		}
		FABTSM73BeamAGenerator BeamGenerator;
		return BeamGenerator.Generate(
			Settings,
			Silhouette,
			OutResult,
			OutError);
	}

	bool EqualResult(
		const FABTSM73BeamAGenerationResult& A,
		const FABTSM73BeamAGenerationResult& B)
	{
		if (A.Bays.Num() != B.Bays.Num()
			|| A.Joints.Num() != B.Joints.Num()
			|| A.Members.Num() != B.Members.Num()
			|| A.Assemblies.Num() != B.Assemblies.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Bays.Num(); ++Index)
		{
			const FABTSM73BeamABay& Left = A.Bays[Index];
			const FABTSM73BeamABay& Right = B.Bays[Index];
			if (Left.BayId != Right.BayId
				|| Left.SourceVolumeId != Right.SourceVolumeId
				|| !Left.LocalBounds.Min.Equals(
					Right.LocalBounds.Min, 0.001)
				|| !Left.LocalBounds.Max.Equals(
					Right.LocalBounds.Max, 0.001)
				|| Left.PreferredAxis != Right.PreferredAxis
				|| Left.AdjacentBayIds != Right.AdjacentBayIds)
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < A.Joints.Num(); ++Index)
		{
			const FABTSM73BeamAJoint& Left = A.Joints[Index];
			const FABTSM73BeamAJoint& Right = B.Joints[Index];
			if (Left.JointId != Right.JointId
				|| !Left.LocalPosition.Equals(Right.LocalPosition, 0.001)
				|| Left.Role != Right.Role)
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < A.Members.Num(); ++Index)
		{
			const FABTSM73BeamAMember& Left = A.Members[Index];
			const FABTSM73BeamAMember& Right = B.Members[Index];
			if (Left.MemberId != Right.MemberId
				|| Left.JointA != Right.JointA
				|| Left.JointB != Right.JointB
				|| Left.Axis != Right.Axis
				|| Left.Role != Right.Role)
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < A.Assemblies.Num(); ++Index)
		{
			const FABTSM73BeamAAssembly& Left = A.Assemblies[Index];
			const FABTSM73BeamAAssembly& Right = B.Assemblies[Index];
			if (Left.AssemblyId != Right.AssemblyId
				|| Left.BayId != Right.BayId
				|| Left.Type != Right.Type
				|| Left.JointIds != Right.JointIds
				|| Left.MemberIds != Right.MemberIds)
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamADeterminismTest,
	"ABTS.M73DAG.BeamA.Determinism",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamADeterminismTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamATests;
	const FABTSM73BeamAPreviewSettings Settings = MakeSettings();
	FABTSM73BeamAGenerationResult A;
	FABTSM73BeamAGenerationResult B;
	FString ErrorA;
	FString ErrorB;
	TestTrue(TEXT("First generation succeeds"), Generate(
		Settings, A, ErrorA));
	TestTrue(TEXT("Second generation succeeds"), Generate(
		Settings, B, ErrorB));
	TestEqual(TEXT("Reject reasons match"), ErrorA, ErrorB);
	TestEqual(
		TEXT("Bay graph hash matches"),
		A.Summary.BayGraphHash,
		B.Summary.BayGraphHash);
	TestEqual(
		TEXT("Beam graph hash matches"),
		A.Summary.BeamGraphHash,
		B.Summary.BeamGraphHash);
	TestTrue(TEXT("All IR records match"), EqualResult(A, B));

	FABTSM73BeamAPreviewSettings Variant = Settings;
	Variant.Silhouette.BuildingSeed += 1;
	FABTSM73BeamAGenerationResult C;
	FString ErrorC;
	TestTrue(TEXT("Seed variant succeeds"), Generate(
		Variant, C, ErrorC));
	TestNotEqual(
		TEXT("Seed changes Beam identity"),
		A.Summary.BeamGraphHash,
		C.Summary.BeamGraphHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamAArchetypeCoverageTest,
	"ABTS.M73DAG.BeamA.ArchetypeCoverage",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamAArchetypeCoverageTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamATests;
	for (int32 Value = static_cast<int32>(
			EABTSM73DAG5BV2Archetype::TerracedCitadel);
		Value <= static_cast<int32>(
			EABTSM73DAG5BV2Archetype::SpiredCampus);
		++Value)
	{
		FABTSM73BeamAPreviewSettings Settings = MakeSettings();
		Settings.Silhouette.Archetype =
			static_cast<EABTSM73DAG5BV2Archetype>(Value);
		Settings.Silhouette.BuildingSeed = 940000 + Value * 211;
		FABTSM73BeamAGenerationResult Result;
		FString Error;
		const bool bGenerated = Generate(Settings, Result, Error);
		TestTrue(
			FString::Printf(TEXT("Archetype %d succeeds: %s"), Value, *Error),
			bGenerated);
		if (!bGenerated)
		{
			continue;
		}
		TestTrue(TEXT("Accepted summary"), Result.Summary.bAccepted);
		TestTrue(TEXT("Silhouette expands into bays"),
			Result.Summary.BayCount >= Result.Summary.SourceVolumeCount);
		TestEqual(TEXT("One assembly per bay"),
			Result.Summary.AssemblyCount,
			Result.Summary.BayCount);
		TestTrue(TEXT("Has X members"), Result.Summary.XMemberCount > 0);
		TestTrue(TEXT("Has Y members"), Result.Summary.YMemberCount > 0);
		TestTrue(TEXT("Has Z members"), Result.Summary.ZMemberCount > 0);
		TestTrue(TEXT("Has roof/diagonal members"),
			Result.Summary.DiagonalMemberCount > 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamAReferentialIntegrityTest,
	"ABTS.M73DAG.BeamA.ReferentialIntegrity",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamAReferentialIntegrityTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamATests;
	FABTSM73BeamAGenerationResult Result;
	FString Error;
	TestTrue(TEXT("Generation succeeds"), Generate(
		MakeSettings(), Result, Error));
	for (int32 Index = 0; Index < Result.Bays.Num(); ++Index)
	{
		const FABTSM73BeamABay& Bay = Result.Bays[Index];
		TestEqual(TEXT("Bay identity matches array index"), Bay.BayId, Index);
		for (const int32 Neighbor : Bay.AdjacentBayIds)
		{
			TestTrue(TEXT("Adjacent bay id is valid"),
				Result.Bays.IsValidIndex(Neighbor));
			if (Result.Bays.IsValidIndex(Neighbor))
			{
				TestTrue(TEXT("Bay adjacency is symmetric"),
					Result.Bays[Neighbor].AdjacentBayIds.Contains(Index));
			}
		}
	}
	for (int32 Index = 0; Index < Result.Members.Num(); ++Index)
	{
		const FABTSM73BeamAMember& Member = Result.Members[Index];
		TestEqual(TEXT("Member identity matches array index"),
			Member.MemberId, Index);
		TestTrue(TEXT("Member joint A is valid"),
			Result.Joints.IsValidIndex(Member.JointA));
		TestTrue(TEXT("Member joint B is valid"),
			Result.Joints.IsValidIndex(Member.JointB));
		if (Result.Joints.IsValidIndex(Member.JointA)
			&& Result.Joints.IsValidIndex(Member.JointB))
		{
			TestFalse(TEXT("Member has non-zero length"),
				Result.Joints[Member.JointA].LocalPosition.Equals(
					Result.Joints[Member.JointB].LocalPosition, 0.01));
		}
	}
	for (int32 Index = 0; Index < Result.Assemblies.Num(); ++Index)
	{
		const FABTSM73BeamAAssembly& Assembly = Result.Assemblies[Index];
		TestEqual(TEXT("Assembly identity matches array index"),
			Assembly.AssemblyId, Index);
		TestTrue(TEXT("Assembly bay is valid"),
			Result.Bays.IsValidIndex(Assembly.BayId));
		for (const int32 JointId : Assembly.JointIds)
		{
			TestTrue(TEXT("Assembly joint is valid"),
				Result.Joints.IsValidIndex(JointId));
		}
		for (const int32 MemberId : Assembly.MemberIds)
		{
			TestTrue(TEXT("Assembly member is valid"),
				Result.Members.IsValidIndex(MemberId));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamABudgetFailureTest,
	"ABTS.M73DAG.BeamA.BudgetFailure",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamABudgetFailureTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamATests;
	FABTSM73BeamAPreviewSettings Settings = MakeSettings();
	Settings.MaxBayCount = 1;
	FABTSM73BeamAGenerationResult Result;
	FString Error;
	TestFalse(TEXT("Insufficient bay budget rejects"), Generate(
		Settings, Result, Error));
	TestEqual(TEXT("Stable rejection reason"),
		Error,
		FString(TEXT("BeamAMaxBayCountExceeded")));
	TestFalse(TEXT("Summary is not accepted"), Result.Summary.bAccepted);
	TestEqual(TEXT("No partial bays"), Result.Bays.Num(), 0);
	TestEqual(TEXT("No partial joints"), Result.Joints.Num(), 0);
	TestEqual(TEXT("No partial members"), Result.Members.Num(), 0);
	TestEqual(TEXT("No partial assemblies"), Result.Assemblies.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamAInvalidSettingsTest,
	"ABTS.M73DAG.BeamA.InvalidSettings",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamAInvalidSettingsTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamATests;
	FABTSM73BeamAPreviewSettings Settings = MakeSettings();
	Settings.TargetBaySpanCM = 0.0f;
	FABTSM73BeamAGenerationResult Result;
	FString Error;
	TestFalse(TEXT("Invalid settings reject"), Generate(
		Settings, Result, Error));
	TestEqual(TEXT("Stable rejection reason"),
		Error,
		FString(TEXT("BeamAInvalidSettings")));
	TestFalse(TEXT("Summary is not accepted"), Result.Summary.bAccepted);
	return true;
}

#endif
