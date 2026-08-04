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
			|| A.BearingContacts.Num() != B.BearingContacts.Num()
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
				|| Left.Role != Right.Role
				|| !FMath::IsNearlyEqual(Left.LengthCM, Right.LengthCM, 0.001f))
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < A.BearingContacts.Num(); ++Index)
		{
			const FABTSM73BeamABearingContact& Left =
				A.BearingContacts[Index];
			const FABTSM73BeamABearingContact& Right =
				B.BearingContacts[Index];
			if (Left.ContactId != Right.ContactId
				|| Left.LowerMemberId != Right.LowerMemberId
				|| Left.UpperMemberId != Right.UpperMemberId
				|| Left.Type != Right.Type
				|| !Left.LocalPosition.Equals(Right.LocalPosition, 0.001)
				|| !FMath::IsNearlyEqual(
					Left.ContactAreaCM2,
					Right.ContactAreaCM2,
					0.001f))
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

	FBox MemberBounds(
		const FABTSM73BeamAMember& Member,
		const FABTSM73BeamAGenerationResult& Result,
		const double CrossSection)
	{
		const FVector Center = (
			Result.Joints[Member.JointA].LocalPosition
			+ Result.Joints[Member.JointB].LocalPosition) * 0.5;
		FVector Extent(
			CrossSection * 0.5,
			CrossSection * 0.5,
			CrossSection * 0.5);
		if (Member.Axis != EABTSM73BeamAFrameAxis::Diagonal)
		{
			Extent[static_cast<int32>(Member.Axis)] = Member.LengthCM * 0.5;
		}
		return FBox(Center - Extent, Center + Extent);
	}

	bool HasPositiveVolumePenetration(
		const FABTSM73BeamAGenerationResult& Result,
		const FABTSM73BeamAPreviewSettings& Settings)
	{
		TArray<FBox> Bounds;
		Bounds.Reserve(Result.Members.Num());
		for (const FABTSM73BeamAMember& Member : Result.Members)
		{
			Bounds.Add(MemberBounds(
				Member, Result, Settings.BlockCrossSectionCM));
		}
		const double Tolerance = Settings.JointMergeToleranceCM;
		for (int32 Left = 0; Left < Bounds.Num(); ++Left)
		{
			for (int32 Right = Left + 1; Right < Bounds.Num(); ++Right)
			{
				if (FMath::Min(Bounds[Left].Max.X, Bounds[Right].Max.X)
						- FMath::Max(Bounds[Left].Min.X, Bounds[Right].Min.X)
						> Tolerance
					&& FMath::Min(Bounds[Left].Max.Y, Bounds[Right].Max.Y)
						- FMath::Max(Bounds[Left].Min.Y, Bounds[Right].Min.Y)
						> Tolerance
					&& FMath::Min(Bounds[Left].Max.Z, Bounds[Right].Max.Z)
						- FMath::Max(Bounds[Left].Min.Z, Bounds[Right].Min.Z)
						> Tolerance)
				{
					return true;
				}
			}
		}
		return false;
	}

	bool EveryMemberReachesGround(
		const FABTSM73BeamAGenerationResult& Result,
		const FABTSM73BeamAPreviewSettings& Settings)
	{
		TArray<bool> Reachable;
		Reachable.Init(false, Result.Members.Num());
		TArray<int32> Queue;
		for (const FABTSM73BeamAMember& Member : Result.Members)
		{
			if (MemberBounds(Member, Result, Settings.BlockCrossSectionCM)
				.Min.Z <= Settings.JointMergeToleranceCM)
			{
				Reachable[Member.MemberId] = true;
				Queue.Add(Member.MemberId);
			}
		}
		for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
		{
			for (const FABTSM73BeamABearingContact& Contact :
				Result.BearingContacts)
			{
				if (Contact.LowerMemberId == Queue[QueueIndex]
					&& !Reachable[Contact.UpperMemberId])
				{
					Reachable[Contact.UpperMemberId] = true;
					Queue.Add(Contact.UpperMemberId);
				}
			}
		}
		for (const bool bReachable : Reachable)
		{
			if (!bReachable)
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
		TestEqual(TEXT("Has no diagonal members"),
			Result.Summary.DiagonalMemberCount,
			0);
		TestTrue(TEXT("Has physical bearing contacts"),
			Result.Summary.BearingContactCount > 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamAGlobalAssemblyClosureTest,
	"ABTS.M73DAG.BeamA.GlobalAssemblyClosure",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamAGlobalAssemblyClosureTest::RunTest(
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
			FString::Printf(TEXT("Archetype %d closes: %s"), Value, *Error),
			bGenerated);
		if (!bGenerated)
		{
			continue;
		}
		TestEqual(TEXT("Summary reports no penetration"),
			Result.Summary.RemainingPenetrationCount, 0);
		TestEqual(TEXT("Summary reports no unsupported member"),
			Result.Summary.UnsupportedMemberCount, 0);
		TestFalse(TEXT("Independent AABB audit finds no penetration"),
			HasPositiveVolumePenetration(Result, Settings));
		TestTrue(TEXT("Independent bearing audit reaches every member"),
			EveryMemberReachesGround(Result, Settings));
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
	for (int32 Index = 0; Index < Result.BearingContacts.Num(); ++Index)
	{
		const FABTSM73BeamABearingContact& Contact =
			Result.BearingContacts[Index];
		TestEqual(TEXT("Contact identity matches array index"),
			Contact.ContactId, Index);
		TestTrue(TEXT("Contact lower member is valid"),
			Result.Members.IsValidIndex(Contact.LowerMemberId));
		TestTrue(TEXT("Contact upper member is valid"),
			Result.Members.IsValidIndex(Contact.UpperMemberId));
		TestTrue(TEXT("Contact has positive area"),
			Contact.ContactAreaCM2 > 0.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamAStackedBlockSemanticsTest,
	"ABTS.M73DAG.BeamA.StackedBlockSemantics",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamAStackedBlockSemanticsTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamATests;
	const FABTSM73BeamAPreviewSettings Settings = MakeSettings();
	FABTSM73BeamAGenerationResult Result;
	FString Error;
	TestTrue(TEXT("Generation succeeds"), Generate(
		Settings, Result, Error));
	int32 CrossBearings = 0;
	int32 PostOnBeam = 0;
	int32 BeamOnPost = 0;
	TSet<int32> QuantizedLengths;
	for (const FABTSM73BeamAMember& Member : Result.Members)
	{
		TestTrue(TEXT("Every block keeps the minimum fixed section length"),
			Member.LengthCM >= Settings.BlockCrossSectionCM);
		TestTrue(TEXT("Only XYZ axes are emitted"),
			Member.Axis == EABTSM73BeamAFrameAxis::X
			|| Member.Axis == EABTSM73BeamAFrameAxis::Y
			|| Member.Axis == EABTSM73BeamAFrameAxis::Z);
		QuantizedLengths.Add(FMath::RoundToInt(Member.LengthCM));
	}
	for (const FABTSM73BeamABearingContact& Contact :
		Result.BearingContacts)
	{
		switch (Contact.Type)
		{
		case EABTSM73BeamABearingType::CrossBearing:
			++CrossBearings;
			break;
		case EABTSM73BeamABearingType::PostOnBeam:
			++PostOnBeam;
			break;
		case EABTSM73BeamABearingType::BeamOnPost:
			++BeamOnPost;
			break;
		case EABTSM73BeamABearingType::ParallelBearing:
		default:
			break;
		}
	}
	TestTrue(TEXT("X/Y blocks cross-support each other"), CrossBearings > 0);
	TestTrue(TEXT("Posts stand on beams"), PostOnBeam > 0);
	TestTrue(TEXT("Upper beams stand on posts"), BeamOnPost > 0);
	TestTrue(TEXT("Topology contains variable block lengths"),
		QuantizedLengths.Num() >= 3);
	TestEqual(TEXT("No diagonal output"),
		Result.Summary.DiagonalMemberCount,
		0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamAParallelCourseSpacingTest,
	"ABTS.M73DAG.BeamA.ParallelCourseSpacing",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamAParallelCourseSpacingTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamATests;
	FABTSM73BeamAPreviewSettings Settings = MakeSettings();
	Settings.MaxParallelBlocksPerCourse = 5;
	Settings.MinimumParallelBlockGapCM = 18.0f;
	Settings.TwoBlockMergeGapCM = 4.0f;
	FABTSM73BeamAGenerationResult Result;
	FString Error;
	TestTrue(TEXT("Generation succeeds"), Generate(Settings, Result, Error));
	const double MinimumCenterSpacing =
		Settings.BlockCrossSectionCM + Settings.TwoBlockMergeGapCM;
	bool bSawReducedSingleBlockCourse = false;
	bool bSawMultipleBlockCourse = false;
	for (const FABTSM73BeamAAssembly& Assembly : Result.Assemblies)
	{
		TMap<FString, int32> CourseCounts;
		for (int32 LeftIndex = 0;
			LeftIndex < Assembly.MemberIds.Num();
			++LeftIndex)
		{
			const FABTSM73BeamAMember& Left =
				Result.Members[Assembly.MemberIds[LeftIndex]];
			if (Left.Axis == EABTSM73BeamAFrameAxis::Z)
			{
				continue;
			}
			const FVector LeftA = Result.Joints[Left.JointA].LocalPosition;
			const FVector LeftB = Result.Joints[Left.JointB].LocalPosition;
			const FVector LeftCenter = (LeftA + LeftB) * 0.5;
			const FString CourseKey = FString::Printf(
				TEXT("%d:%d:%lld:%lld"),
				static_cast<int32>(Left.Axis),
				static_cast<int32>(Left.Role),
				FMath::RoundToInt64(LeftCenter.Z * 10.0),
				FMath::RoundToInt64(Left.LengthCM * 10.0));
			++CourseCounts.FindOrAdd(CourseKey);
			for (int32 RightIndex = LeftIndex + 1;
				RightIndex < Assembly.MemberIds.Num();
				++RightIndex)
			{
				const FABTSM73BeamAMember& Right =
					Result.Members[Assembly.MemberIds[RightIndex]];
				if (Right.Axis != Left.Axis || Right.Role != Left.Role)
				{
					continue;
				}
				const FVector RightA =
					Result.Joints[Right.JointA].LocalPosition;
				const FVector RightB =
					Result.Joints[Right.JointB].LocalPosition;
				const FVector RightCenter = (RightA + RightB) * 0.5;
				if (!FMath::IsNearlyEqual(LeftCenter.Z, RightCenter.Z, 0.01)
					|| !FMath::IsNearlyEqual(Left.LengthCM, Right.LengthCM, 0.01f))
				{
					continue;
				}
				const double Separation =
					Left.Axis == EABTSM73BeamAFrameAxis::X
						? FMath::Abs(LeftCenter.Y - RightCenter.Y)
						: FMath::Abs(LeftCenter.X - RightCenter.X);
				TestTrue(
					TEXT("Parallel blocks retain the final-pair merge gap"),
					Separation + 0.01 >= MinimumCenterSpacing);
			}
		}
		for (const TPair<FString, int32>& Course : CourseCounts)
		{
			TestTrue(
				TEXT("A course does not exceed the configured parallel cap"),
				Course.Value <= Settings.MaxParallelBlocksPerCourse);
			bSawReducedSingleBlockCourse |= Course.Value == 1;
			bSawMultipleBlockCourse |= Course.Value >= 2;
		}
	}
	TestTrue(TEXT("Wide courses retain multiple parallel blocks"),
		bSawMultipleBlockCourse);
	TestTrue(TEXT("Narrow courses automatically reduce to one block"),
		bSawReducedSingleBlockCourse);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamAAdjacentBayBoundarySpacingTest,
	"ABTS.M73DAG.BeamA.AdjacentBayBoundarySpacing",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamAAdjacentBayBoundarySpacingTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamATests;
	FABTSM73BeamAPreviewSettings Settings = MakeSettings();
	Settings.TargetBaySpanCM = 1000.0f;
	Settings.MaxParallelBlocksPerCourse = 2;
	Settings.MinimumParallelBlockGapCM = 12.0f;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	Silhouette.Summary.bAccepted = true;
	for (int32 VolumeIndex = 0; VolumeIndex < 2; ++VolumeIndex)
	{
		FABTSM73DAG5BV2Volume& Volume =
			Silhouette.Volumes.AddDefaulted_GetRef();
		Volume.VolumeId = VolumeIndex;
		Volume.Role = EABTSM73DAG5BV2VolumeRole::Body;
		Volume.Primitive = EABTSM73DAG5BV2Primitive::Box;
		const double MinimumY = VolumeIndex == 0 ? -240.0 : 0.0;
		const double MaximumY = VolumeIndex == 0 ? 0.0 : 240.0;
		Volume.LocalBounds = FBox(
			FVector(-300.0, MinimumY, 0.0),
			FVector(300.0, MaximumY, 360.0 + VolumeIndex * 120.0));
	}
	FABTSM73BeamAGenerator Generator;
	FABTSM73BeamAGenerationResult Result;
	FString Error;
	TestTrue(TEXT("Adjacent-volume generation succeeds"),
		Generator.Generate(Settings, Silhouette, Result, Error));
	TestEqual(TEXT("Two semantic segments remain two bays"),
		Result.Bays.Num(), 2);
	TestTrue(TEXT("The two bays are adjacent"),
		Result.Bays.Num() == 2
		&& Result.Bays[0].AdjacentBayIds.Contains(1)
		&& Result.Bays[1].AdjacentBayIds.Contains(0));

	TArray<double> BottomPrimaryY;
	TArray<FVector> LowerSecondaryCenters;
	TArray<FVector> UpperPrimaryCenters;
	TArray<FVector> PostCenters;
	TSet<int32> QuantizedPostLengths;
	for (const FABTSM73BeamAMember& Member : Result.Members)
	{
		const FVector A = Result.Joints[Member.JointA].LocalPosition;
		const FVector B = Result.Joints[Member.JointB].LocalPosition;
		const FVector Center = (A + B) * 0.5;
		if (Member.Axis == EABTSM73BeamAFrameAxis::Y
			&& Member.Role == EABTSM73BeamAMemberRole::SecondaryBeam
			&& FMath::IsNearlyEqual(
				Center.Z,
				Settings.BlockCrossSectionCM * 1.5,
				0.01))
		{
			LowerSecondaryCenters.Add(Center);
		}
		if (Member.Axis == EABTSM73BeamAFrameAxis::X
			&& Member.Role == EABTSM73BeamAMemberRole::PrimaryBeam
			&& Center.Z > Settings.BlockCrossSectionCM * 2.0)
		{
			UpperPrimaryCenters.Add(Center);
		}
		if (Member.Axis == EABTSM73BeamAFrameAxis::Z)
		{
			PostCenters.Add(Center);
			QuantizedPostLengths.Add(FMath::RoundToInt(Member.LengthCM));
		}
		if (Member.Axis != EABTSM73BeamAFrameAxis::X
			|| Member.Role != EABTSM73BeamAMemberRole::PrimaryBeam)
		{
			continue;
		}
		if (FMath::IsNearlyEqual(
			Center.Z,
			Settings.BlockCrossSectionCM * 0.5,
			0.01))
		{
			BottomPrimaryY.Add(Center.Y);
		}
	}
	BottomPrimaryY.Sort();
	TestEqual(TEXT("Each side retains two blocks"),
		BottomPrimaryY.Num(), 4);
	if (BottomPrimaryY.Num() == 4)
	{
		const double LeftInternalGap =
			BottomPrimaryY[1] - BottomPrimaryY[0]
			- Settings.BlockCrossSectionCM;
		const double SharedBoundaryGap =
			BottomPrimaryY[2] - BottomPrimaryY[1]
			- Settings.BlockCrossSectionCM;
		const double RightInternalGap =
			BottomPrimaryY[3] - BottomPrimaryY[2]
			- Settings.BlockCrossSectionCM;
		TestTrue(TEXT("The solved gap respects the configured minimum"),
			SharedBoundaryGap + 0.01
				>= Settings.MinimumParallelBlockGapCM);
		TestTrue(TEXT("The shared gap matches the left internal gap"),
			FMath::IsNearlyEqual(
				SharedBoundaryGap,
				LeftInternalGap,
				0.01));
		TestTrue(TEXT("The shared gap matches the right internal gap"),
			FMath::IsNearlyEqual(
				SharedBoundaryGap,
				RightInternalGap,
				0.01));
	}
	TestTrue(TEXT("X-Y support intersections produce posts"),
		PostCenters.Num() > 0);
	TestTrue(TEXT("Different course elevations produce variable post lengths"),
		QuantizedPostLengths.Num() >= 2);
	for (const FVector& PostCenter : PostCenters)
	{
		const bool bMatchesLowerLane = LowerSecondaryCenters.ContainsByPredicate(
			[&PostCenter](const FVector& BeamCenter)
			{
				return FMath::IsNearlyEqual(
					BeamCenter.X, PostCenter.X, 0.01);
			});
		const bool bMatchesUpperLane = UpperPrimaryCenters.ContainsByPredicate(
			[&PostCenter](const FVector& BeamCenter)
			{
				return FMath::IsNearlyEqual(
					BeamCenter.Y, PostCenter.Y, 0.01);
			});
		TestTrue(TEXT("Every post consumes a generated lower Y lane"),
			bMatchesLowerLane);
		TestTrue(TEXT("Every post consumes a generated upper X lane"),
			bMatchesUpperLane);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamAParallelZSupportPlacementTest,
	"ABTS.M73DAG.BeamA.ParallelZSupportPlacement",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamAParallelZSupportPlacementTest::RunTest(
	const FString& Parameters)
{
	FABTSM73BeamAPreviewSettings Settings;
	Settings.BlockCrossSectionCM = 36.0f;
	Settings.MinimumParallelBlockGapCM = 18.0f;
	Settings.TwoBlockMergeGapCM = 4.0f;
	Settings.MaxParallelBlocksPerCourse = 4;
	Settings.JointMergeToleranceCM = 0.5f;
	TArray<double> AlignedOffsets;
	TestTrue(TEXT("Aligned parallel lanes produce Z-support stations"),
		ABTSM73BeamA::BuildAlignedParallelSupportOffsets(
			42.0,
			42.25,
			0.0,
			420.0,
			Settings,
			AlignedOffsets));
	TestEqual(TEXT("Aligned support count respects the shared cap"),
		AlignedOffsets.Num(), Settings.MaxParallelBlocksPerCourse);
	for (int32 Index = 1; Index < AlignedOffsets.Num(); ++Index)
	{
		const double ClearGap = AlignedOffsets[Index]
			- AlignedOffsets[Index - 1]
			- Settings.BlockCrossSectionCM;
		TestTrue(TEXT("Aligned supports retain the minimum clear gap"),
			ClearGap + 0.01 >= Settings.MinimumParallelBlockGapCM);
	}

	TArray<double> MisalignedOffsets;
	TestFalse(TEXT("Misaligned parallel lanes do not force Z supports"),
		ABTSM73BeamA::BuildAlignedParallelSupportOffsets(
			42.0,
			43.0,
			0.0,
			420.0,
			Settings,
			MisalignedOffsets));
	TestTrue(TEXT("Misaligned lanes emit no support stations"),
		MisalignedOffsets.IsEmpty());

	Settings.MaxParallelBlocksPerCourse = 2;
	TArray<double> RetainedPairOffsets;
	TestTrue(TEXT("A final pair is retained above the merge threshold"),
		ABTSM73BeamA::BuildAlignedParallelSupportOffsets(
			42.0,
			42.25,
			0.0,
			82.0,
			Settings,
			RetainedPairOffsets));
	TestEqual(TEXT("The pair survives below the normal multi-block gap"),
		RetainedPairOffsets.Num(), 2);
	if (RetainedPairOffsets.Num() == 2)
	{
		const double ClearGap = RetainedPairOffsets[1]
			- RetainedPairOffsets[0]
			- Settings.BlockCrossSectionCM;
		TestTrue(TEXT("Retained pair may use less than the normal gap"),
			ClearGap < Settings.MinimumParallelBlockGapCM);
		TestTrue(TEXT("Retained pair still satisfies the merge gap"),
			ClearGap + 0.01 >= Settings.TwoBlockMergeGapCM);
	}

	TArray<double> CollapsedPairOffsets;
	TestTrue(TEXT("A sub-threshold final pair produces a centered station"),
		ABTSM73BeamA::BuildAlignedParallelSupportOffsets(
			42.0,
			42.25,
			0.0,
			74.0,
			Settings,
			CollapsedPairOffsets));
	TestEqual(TEXT("The sub-threshold pair collapses to one block"),
		CollapsedPairOffsets.Num(), 1);
	if (CollapsedPairOffsets.Num() == 1)
	{
		TestTrue(TEXT("Collapsed block remains centered"),
			FMath::IsNearlyEqual(CollapsedPairOffsets[0], 37.0, 0.01));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamAPrismLongAxisRidgeTest,
	"ABTS.M73DAG.BeamA.PrismLongAxisRidge",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamAPrismLongAxisRidgeTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamATests;
	FABTSM73BeamAPreviewSettings Settings = MakeSettings();
	Settings.TargetBaySpanCM = 240.0f;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	Silhouette.Summary.bAccepted = true;
	FABTSM73DAG5BV2Volume& Body =
		Silhouette.Volumes.AddDefaulted_GetRef();
	Body.VolumeId = 0;
	Body.Role = EABTSM73DAG5BV2VolumeRole::Foundation;
	Body.Primitive = EABTSM73DAG5BV2Primitive::Box;
	Body.LocalBounds = FBox(
		FVector(-360.0, -180.0, 0.0),
		FVector(360.0, 180.0, 360.0));
	FABTSM73DAG5BV2Volume& Roof =
		Silhouette.Volumes.AddDefaulted_GetRef();
	Roof.VolumeId = 1;
	Roof.Role = EABTSM73DAG5BV2VolumeRole::Crown;
	Roof.Primitive = EABTSM73DAG5BV2Primitive::TriangularPrismY;
	Roof.LocalBounds = FBox(
		FVector(-360.0, -180.0, 360.0),
		FVector(360.0, 180.0, 720.0));

	FABTSM73BeamAGenerator Generator;
	FABTSM73BeamAGenerationResult Result;
	FString Error;
	const bool bGenerated = Generator.Generate(
		Settings, Silhouette, Result, Error);
	TestTrue(*FString::Printf(
		TEXT("Prism ridge fixture generates: %s"), *Error), bGenerated);
	if (!bGenerated)
	{
		return false;
	}
	int32 RoofBayCount = 0;
	for (const FABTSM73BeamABay& Bay : Result.Bays)
	{
		RoofBayCount += Bay.SourceVolumeId == Roof.VolumeId ? 1 : 0;
	}
	TestEqual(TEXT("A semantic roof remains one undivided Bay"),
		RoofBayCount, 1);

	double HighestRoofZ = -TNumericLimits<double>::Max();
	for (const FABTSM73BeamAMember& Member : Result.Members)
	{
		if (Member.Role != EABTSM73BeamAMemberRole::RoofCourse)
		{
			continue;
		}
		const FVector Center =
			(Result.Joints[Member.JointA].LocalPosition
				+ Result.Joints[Member.JointB].LocalPosition) * 0.5;
		HighestRoofZ = FMath::Max(HighestRoofZ, Center.Z);
	}
	TArray<const FABTSM73BeamAMember*> RidgeMembers;
	for (const FABTSM73BeamAMember& Member : Result.Members)
	{
		if (Member.Role != EABTSM73BeamAMemberRole::RoofCourse)
		{
			continue;
		}
		const FVector Center =
			(Result.Joints[Member.JointA].LocalPosition
				+ Result.Joints[Member.JointB].LocalPosition) * 0.5;
		if (FMath::IsNearlyEqual(Center.Z, HighestRoofZ, 0.01))
		{
			RidgeMembers.Add(&Member);
		}
	}
	TestEqual(TEXT("Prism apex contains one physical ridge block"),
		RidgeMembers.Num(), 1);
	if (RidgeMembers.Num() == 1)
	{
		TestEqual(TEXT("PrismY ridge follows the long X axis"),
			RidgeMembers[0]->Axis,
			EABTSM73BeamAFrameAxis::X);
		TestTrue(TEXT("The ridge spans the semantic roof long axis"),
			RidgeMembers[0]->LengthCM >= 700.0f);
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
	TestEqual(TEXT("No partial bearing contacts"),
		Result.BearingContacts.Num(), 0);
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
