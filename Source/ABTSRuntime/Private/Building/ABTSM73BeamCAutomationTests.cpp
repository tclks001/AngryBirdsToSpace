// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Building/ABTSM73BeamAGenerator.h"
#include "Building/ABTSM73BeamBGenerator.h"
#include "Building/ABTSM73BeamCGenerator.h"
#include "Building/ABTSM73DAG5BShapeGrammarV2.h"
#include "Misc/AutomationTest.h"

namespace ABTSM73BeamCTests
{
	FABTSM73BeamCPreviewSettings SettingsForSeed(const int32 Seed)
	{
		FABTSM73BeamCPreviewSettings Settings;
		Settings.BeamB.BeamA.Silhouette.BuildingSeed = Seed;
		Settings.BeamB.BeamA.Silhouette.Archetype =
			EABTSM73DAG5BV2Archetype::BridgedArcology;
		Settings.BeamB.BeamA.Silhouette.MinGrammarDepth = 2;
		Settings.BeamB.BeamA.Silhouette.MaxGrammarDepth = 4;
		Settings.BeamB.BeamA.TargetBaySpanCM = 480.0f;
		Settings.BeamB.GrammarDepth = 2;
		return Settings;
	}

	bool GeneratePipeline(
		const FABTSM73BeamCPreviewSettings& Settings,
		FABTSM73BeamBGenerationResult& OutBeamB,
		FABTSM73BeamCGenerationResult& OutBeamC,
		FString& OutError)
	{
		FABTSM73DAG5BV2GenerationResult Silhouette;
		FABTSM73DAG5BShapeGrammarV2 ShapeGenerator;
		if (!ShapeGenerator.Generate(
			Settings.BeamB.BeamA.Silhouette, Silhouette, OutError))
		{
			return false;
		}
		FABTSM73BeamAGenerationResult BeamA;
		FABTSM73BeamAGenerator BeamAGenerator;
		if (!BeamAGenerator.Generate(
			Settings.BeamB.BeamA, Silhouette, BeamA, OutError))
		{
			return false;
		}
		FABTSM73BeamBGenerator BeamBGenerator;
		if (!BeamBGenerator.Generate(
			Settings.BeamB, Silhouette, BeamA, OutBeamB, OutError))
		{
			return false;
		}
		FABTSM73BeamCGenerator BeamCGenerator;
		return BeamCGenerator.Generate(
			Settings, OutBeamB.ClosedAssembly, OutBeamC, OutError);
	}

	int32 AddJoint(FABTSM73BeamAGenerationResult& Assembly, const FVector& Position)
	{
		FABTSM73BeamAJoint Joint;
		Joint.JointId = Assembly.Joints.Num();
		Joint.LocalPosition = Position;
		Assembly.Joints.Add(Joint);
		return Joint.JointId;
	}

	int32 AddMember(
		FABTSM73BeamAGenerationResult& Assembly,
		const FVector& Start,
		const FVector& End,
		const EABTSM73BeamAFrameAxis Axis)
	{
		FABTSM73BeamAMember Member;
		Member.MemberId = Assembly.Members.Num();
		Member.JointA = AddJoint(Assembly, Start);
		Member.JointB = AddJoint(Assembly, End);
		Member.Axis = Axis;
		Member.Role = Axis == EABTSM73BeamAFrameAxis::Z
			? EABTSM73BeamAMemberRole::Post
			: EABTSM73BeamAMemberRole::PrimaryBeam;
		Member.LengthCM = FVector::Distance(Start, End);
		Assembly.Members.Add(Member);
		return Member.MemberId;
	}

	void AddBearing(
		FABTSM73BeamAGenerationResult& Assembly,
		const int32 LowerMemberId,
		const int32 UpperMemberId,
		const FVector& Position,
		const float AreaCM2 = 1296.0f)
	{
		FABTSM73BeamABearingContact Contact;
		Contact.ContactId = Assembly.BearingContacts.Num();
		Contact.LowerMemberId = LowerMemberId;
		Contact.UpperMemberId = UpperMemberId;
		Contact.LocalPosition = Position;
		Contact.ContactAreaCM2 = AreaCM2;
		Assembly.BearingContacts.Add(Contact);
	}

	FABTSM73BeamAGenerationResult MakeAcceptedAssembly()
	{
		FABTSM73BeamAGenerationResult Assembly;
		Assembly.Summary.bAccepted = true;
		return Assembly;
	}

	FABTSM73BeamAGenerationResult MakeTwoSupportAssembly()
	{
		FABTSM73BeamAGenerationResult Assembly = MakeAcceptedAssembly();
		const int32 LeftGround = AddMember(Assembly,
			FVector(0.0, -18.0, 18.0), FVector(0.0, 18.0, 18.0),
			EABTSM73BeamAFrameAxis::Y);
		const int32 RightGround = AddMember(Assembly,
			FVector(300.0, -18.0, 18.0), FVector(300.0, 18.0, 18.0),
			EABTSM73BeamAFrameAxis::Y);
		const int32 Beam = AddMember(Assembly,
			FVector(0.0, 0.0, 54.0), FVector(300.0, 0.0, 54.0),
			EABTSM73BeamAFrameAxis::X);
		const int32 UpperPost = AddMember(Assembly,
			FVector(225.0, 0.0, 90.0), FVector(225.0, 0.0, 390.0),
			EABTSM73BeamAFrameAxis::Z);
		AddBearing(Assembly, LeftGround, Beam, FVector(0.0, 0.0, 36.0));
		AddBearing(Assembly, RightGround, Beam, FVector(300.0, 0.0, 36.0));
		AddBearing(Assembly, Beam, UpperPost, FVector(225.0, 0.0, 72.0));
		return Assembly;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamCDeterminismTest,
	"ABTS.M73DAG.BeamC.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamCDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamCTests;
	FABTSM73BeamCPreviewSettings Settings = SettingsForSeed(940211);
	Settings.BeamB.BeamA.Silhouette.Archetype =
		EABTSM73DAG5BV2Archetype::TerracedCitadel;
	FABTSM73BeamBGenerationResult BeamBA;
	FABTSM73BeamBGenerationResult BeamBB;
	FABTSM73BeamCGenerationResult A;
	FABTSM73BeamCGenerationResult B;
	FString Error;
	TestTrue(TEXT("First pipeline succeeds"),
		GeneratePipeline(Settings, BeamBA, A, Error));
	TestTrue(TEXT("Second pipeline succeeds"),
		GeneratePipeline(Settings, BeamBB, B, Error));
	TestEqual(TEXT("Load DAG hash is deterministic"),
		A.Summary.LoadDAGHash, B.Summary.LoadDAGHash);
	TestEqual(TEXT("Node count is deterministic"), A.Nodes.Num(), B.Nodes.Num());
	TestEqual(TEXT("Edge count is deterministic"), A.Edges.Num(), B.Edges.Num());
	TestTrue(TEXT("Topological order is deterministic"),
		A.TopologicalMemberOrder == B.TopologicalMemberOrder);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamCMultiSupportReactionTest,
	"ABTS.M73DAG.BeamC.MultiSupportReaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamCMultiSupportReactionTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamCTests;
	FABTSM73BeamCPreviewSettings Settings;
	Settings.bRequireBidirectionalLateralTies = false;
	FABTSM73BeamCGenerationResult Result;
	FString Error;
	FABTSM73BeamCGenerator Generator;
	TestTrue(TEXT("Two-support assembly succeeds"),
		Generator.Generate(Settings, MakeTwoSupportAssembly(), Result, Error));
	if (Result.Edges.Num() >= 2)
	{
		const double Left = Result.Edges[0].LoadShare;
		const double Right = Result.Edges[1].LoadShare;
		TestTrue(TEXT("Reaction shares sum to one"),
			FMath::IsNearlyEqual(Left + Right, 1.0, 1.0e-4));
		TestTrue(TEXT("Offset upper load biases right reaction"), Right > Left);
		TestTrue(TEXT("First moment is reproduced"),
			FMath::IsNearlyEqual(Right * 300.0,
				Result.Nodes[2].LoadResultant.X, 0.1));
	}
	TestTrue(TEXT("All self load reaches ground"),
		FMath::IsNearlyEqual(Result.Summary.TotalSelfLoadKG,
			Result.Summary.TotalGroundReactionKG, 0.05f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamCCycleRejectTest,
	"ABTS.M73DAG.BeamC.CycleReject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamCCycleRejectTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamCTests;
	FABTSM73BeamAGenerationResult Assembly = MakeAcceptedAssembly();
	const int32 A = AddMember(Assembly, FVector(0, 0, 18), FVector(100, 0, 18),
		EABTSM73BeamAFrameAxis::X);
	const int32 B = AddMember(Assembly, FVector(0, 0, 54), FVector(100, 0, 54),
		EABTSM73BeamAFrameAxis::X);
	AddBearing(Assembly, A, B, FVector(25, 0, 36));
	AddBearing(Assembly, B, A, FVector(75, 0, 36));
	FABTSM73BeamCGenerationResult Result;
	FString Error;
	FABTSM73BeamCGenerator Generator;
	TestFalse(TEXT("Reciprocal support is rejected"),
		Generator.Generate(FABTSM73BeamCPreviewSettings(), Assembly, Result, Error));
	TestEqual(TEXT("Stable cycle reason"), Error, FString(TEXT("BeamCLoadDAGCycle")));
	TestTrue(TEXT("Cycle nodes are reported"), Result.Summary.CycleNodeCount > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamCGroundRejectTest,
	"ABTS.M73DAG.BeamC.GroundReject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamCGroundRejectTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamCTests;
	FABTSM73BeamAGenerationResult Assembly = MakeAcceptedAssembly();
	AddMember(Assembly, FVector(0, 0, 200), FVector(300, 0, 200),
		EABTSM73BeamAFrameAxis::X);
	FABTSM73BeamCGenerationResult Result;
	FString Error;
	FABTSM73BeamCGenerator Generator;
	TestFalse(TEXT("Floating member is rejected"),
		Generator.Generate(FABTSM73BeamCPreviewSettings(), Assembly, Result, Error));
	TestEqual(TEXT("Stable ground reason"), Error,
		FString(TEXT("BeamCGroundUnreachable")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamCBearingAreaRejectTest,
	"ABTS.M73DAG.BeamC.BearingAreaReject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamCBearingAreaRejectTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamCTests;
	FABTSM73BeamAGenerationResult Assembly = MakeAcceptedAssembly();
	const int32 Ground = AddMember(Assembly, FVector(0, 0, 18), FVector(100, 0, 18),
		EABTSM73BeamAFrameAxis::X);
	const int32 Upper = AddMember(Assembly, FVector(0, 0, 54), FVector(100, 0, 54),
		EABTSM73BeamAFrameAxis::X);
	AddBearing(Assembly, Ground, Upper, FVector(50, 0, 36), 1.0f);
	FABTSM73BeamCGenerationResult Result;
	FString Error;
	FABTSM73BeamCGenerator Generator;
	TestFalse(TEXT("Tiny bearing is rejected"),
		Generator.Generate(FABTSM73BeamCPreviewSettings(), Assembly, Result, Error));
	TestEqual(TEXT("Stable bearing reason"), Error,
		FString(TEXT("BeamCBearingAreaInsufficient")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamCSpanRejectTest,
	"ABTS.M73DAG.BeamC.SpanReject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamCSpanRejectTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamCTests;
	FABTSM73BeamAGenerationResult Assembly = MakeAcceptedAssembly();
	const int32 Left = AddMember(Assembly, FVector(0, -18, 18), FVector(0, 18, 18),
		EABTSM73BeamAFrameAxis::Y);
	const int32 Right = AddMember(Assembly, FVector(1000, -18, 18), FVector(1000, 18, 18),
		EABTSM73BeamAFrameAxis::Y);
	const int32 Beam = AddMember(Assembly, FVector(0, 0, 54), FVector(1000, 0, 54),
		EABTSM73BeamAFrameAxis::X);
	AddBearing(Assembly, Left, Beam, FVector(0, 0, 36));
	AddBearing(Assembly, Right, Beam, FVector(1000, 0, 36));
	FABTSM73BeamCPreviewSettings Settings;
	Settings.bRequireBidirectionalLateralTies = false;
	Settings.MaximumUnsupportedSpanCM = 100.0f;
	FABTSM73BeamCGenerationResult Result;
	FString Error;
	FABTSM73BeamCGenerator Generator;
	TestFalse(TEXT("Overspan beam is rejected"),
		Generator.Generate(Settings, Assembly, Result, Error));
	TestEqual(TEXT("Stable span reason"), Error,
		FString(TEXT("BeamCSpanLimitExceeded")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamCSlendernessRejectTest,
	"ABTS.M73DAG.BeamC.SlendernessReject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamCSlendernessRejectTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamCTests;
	FABTSM73BeamAGenerationResult Assembly = MakeAcceptedAssembly();
	const int32 Ground = AddMember(Assembly, FVector(-50, 0, 18), FVector(50, 0, 18),
		EABTSM73BeamAFrameAxis::X);
	const int32 Post = AddMember(Assembly, FVector(0, 0, 54), FVector(0, 0, 4054),
		EABTSM73BeamAFrameAxis::Z);
	AddBearing(Assembly, Ground, Post, FVector(0, 0, 36));
	FABTSM73BeamCPreviewSettings Settings;
	Settings.bRequireBidirectionalLateralTies = false;
	Settings.MaximumColumnSlenderness = 10.0f;
	FABTSM73BeamCGenerationResult Result;
	FString Error;
	FABTSM73BeamCGenerator Generator;
	TestFalse(TEXT("Slender post is rejected"),
		Generator.Generate(Settings, Assembly, Result, Error));
	TestEqual(TEXT("Stable slenderness reason"), Error,
		FString(TEXT("BeamCColumnSlendernessExceeded")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamCArchetypeMatrixTest,
	"ABTS.M73DAG.BeamC.ArchetypeMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamCArchetypeMatrixTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamCTests;
	const EABTSM73DAG5BV2Archetype Archetypes[] = {
		EABTSM73DAG5BV2Archetype::TerracedCitadel,
		EABTSM73DAG5BV2Archetype::TwinTowerComplex,
		EABTSM73DAG5BV2Archetype::BridgedArcology,
		EABTSM73DAG5BV2Archetype::SpiredCampus};
	for (const EABTSM73DAG5BV2Archetype Archetype : Archetypes)
	{
		FABTSM73BeamCPreviewSettings Settings = SettingsForSeed(
			940000 + static_cast<int32>(Archetype) * 211);
		Settings.BeamB.BeamA.Silhouette.Archetype = Archetype;
		FABTSM73BeamBGenerationResult BeamB;
		FABTSM73BeamCGenerationResult BeamC;
		FString Error;
		if (!GeneratePipeline(Settings, BeamB, BeamC, Error))
		{
			AddError(FString::Printf(TEXT("Archetype %d failed: %s"),
				static_cast<int32>(Archetype), *Error));
			continue;
		}
		TestTrue(TEXT("Load DAG accepted"), BeamC.Summary.bAccepted);
		TestEqual(TEXT("One load node per closed member"),
			BeamC.Nodes.Num(), BeamB.ClosedAssembly.Members.Num());
		TestEqual(TEXT("One load edge per explicit bearing"),
			BeamC.Edges.Num(), BeamB.ClosedAssembly.BearingContacts.Num());
		TestEqual(TEXT("All nodes topologically ordered"),
			BeamC.TopologicalMemberOrder.Num(), BeamC.Nodes.Num());
		TestEqual(TEXT("No unreachable nodes"),
			BeamC.Summary.GroundUnreachableNodeCount, 0);
		TestEqual(TEXT("No reaction violations"),
			BeamC.Summary.ReactionBalanceViolationCount, 0);
		TestTrue(TEXT("Load is conserved"), FMath::IsNearlyEqual(
			BeamC.Summary.TotalSelfLoadKG,
			BeamC.Summary.TotalGroundReactionKG, 0.1f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamCLateralMechanismRejectTest,
	"ABTS.M73DAG.BeamC.LateralMechanismReject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamCLateralMechanismRejectTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamCTests;
	FABTSM73BeamAGenerationResult Assembly = MakeAcceptedAssembly();
	const int32 Ground = AddMember(Assembly, FVector(-50, 0, 18), FVector(50, 0, 18),
		EABTSM73BeamAFrameAxis::X);
	const int32 Post = AddMember(Assembly, FVector(0, 0, 54), FVector(0, 0, 254),
		EABTSM73BeamAFrameAxis::Z);
	AddBearing(Assembly, Ground, Post, FVector(0, 0, 36));
	FABTSM73BeamCGenerationResult Result;
	FString Error;
	FABTSM73BeamCGenerator Generator;
	TestFalse(TEXT("One-axis frame is rejected"),
		Generator.Generate(FABTSM73BeamCPreviewSettings(), Assembly, Result, Error));
	TestEqual(TEXT("Stable lateral reason"), Error,
		FString(TEXT("BeamCLateralMechanism")));
	return true;
}

#endif
