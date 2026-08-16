// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ABTSM73BeamC3V2CoupledExteriorFrameGenerator.h"
#include "ABTSM73BeamAGenerator.h"
#include "ABTSM73BeamD1BrickCompiler.h"
#include "ABTSM73BeamD1BrickCompilerInternal.h"

#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"

namespace ABTSM73BeamC3V2CoupledExteriorFrameTests
{
	using namespace ABTSM73BeamC3V2;

	constexpr int32 ExpectedMemberCount = 28;
	constexpr int32 ExpectedCoreRailCount = 8;
	constexpr int32 ExpectedThroughOutriggerCount = 8;
	constexpr int32 ExpectedFacadeRailCount = 4;
	constexpr int32 ExpectedExteriorPostCount = 8;
	constexpr int32 ExpectedBodyCourseBearingCount = 28;
	constexpr int32 ExpectedFacadeSandwichBearingCount = 16;
	constexpr int32 ExpectedExteriorPostBearingCount = 8;
	constexpr int32 ExpectedBearingCount = 52;

	FABTSM73BeamC3V2CoupledExteriorFrameSettings MakeFixtureSettings()
	{
		FABTSM73BeamC3V2CoupledExteriorFrameSettings Settings;
		Settings.bEnabled = true;
		Settings.CourseCount = 8;
		Settings.RailCount = 2;
		Settings.MaximumCellCount = 1;
		Settings.MaximumMacroBandCount = 1;
		Settings.MaximumMemberLengthCM = 720.0f;
		Settings.MaximumPostSegmentSpanCM = 720.0f;
		Settings.MinimumCourseCount = 8;
		Settings.MinimumStructuralMemberBudget = ExpectedMemberCount;
		Settings.MaximumStructuralMemberCount = ExpectedMemberCount;
		Settings.MaximumFinalMemberCount = 49;
		Settings.MaximumFallbackLevel = 0;
		Settings.CourseReductionPerFallbackLevel = 0;
		Settings.CellReductionPerFallbackLevel = 0;
		Settings.MinimumMacroBandStrideCourses = 6;
		Settings.MaximumMacroBandStrideCourses = 22;
		return Settings;
	}

	FABTSM73BeamAPreviewSettings MakeBeamASettings()
	{
		FABTSM73BeamAPreviewSettings Settings;
		Settings.BlockCrossSectionCM = 36.0f;
		return Settings;
	}

	FABTSM73BeamCPreviewSettings MakeBeamCSettings(
		const FABTSM73BeamAPreviewSettings& BeamASettings)
	{
		FABTSM73BeamCPreviewSettings Settings;
		Settings.BeamB.BeamA = BeamASettings;
		return Settings;
	}

	FBox MakeFixtureBounds(const double SizeX = 648.0)
	{
		return FBox(
			FVector(-SizeX * 0.5, -324.0, 0.0),
			FVector(SizeX * 0.5, 324.0, 288.0));
	}

	double PositiveIntervalOverlap(
		const double MinimumA,
		const double MaximumA,
		const double MinimumB,
		const double MaximumB)
	{
		return FMath::Min(MaximumA, MaximumB)
			- FMath::Max(MinimumA, MinimumB);
	}

	bool HasPositiveVolumeOverlap(const FBox& A, const FBox& B)
	{
		return PositiveIntervalOverlap(A.Min.X, A.Max.X, B.Min.X, B.Max.X)
			> KINDA_SMALL_NUMBER
			&& PositiveIntervalOverlap(A.Min.Y, A.Max.Y, B.Min.Y, B.Max.Y)
				> KINDA_SMALL_NUMBER
			&& PositiveIntervalOverlap(A.Min.Z, A.Max.Z, B.Min.Z, B.Max.Z)
				> KINDA_SMALL_NUMBER;
	}

	FBox BoundsForMember(
		const FABTSM73BeamAMember& Member,
		const FABTSM73BeamAGenerationResult& Assembly)
	{
		if (!Assembly.Joints.IsValidIndex(Member.JointA)
			|| !Assembly.Joints.IsValidIndex(Member.JointB))
		{
			return FBox(EForceInit::ForceInit);
		}
		const FVector Start = Assembly.Joints[Member.JointA].LocalPosition;
		const FVector End = Assembly.Joints[Member.JointB].LocalPosition;
		const FVector Center = (Start + End) * 0.5;
		FVector Extent(18.0);
		Extent[static_cast<int32>(Member.Axis)] = Member.LengthCM * 0.5;
		return FBox(Center - Extent, Center + Extent);
	}

	int32 FindPlannedMemberIndex(
		const FABTSM73BeamAMember& Member,
		const FABTSM73BeamAGenerationResult& Assembly,
		const FCoupledExteriorFrameResult& Result)
	{
		const FBox Bounds = BoundsForMember(Member, Assembly);
		for (int32 PlannedIndex = 0;
			PlannedIndex < Result.PlannedMembers.Num(); ++PlannedIndex)
		{
			const FCoupledExteriorFramePlannedMember& Planned =
				Result.PlannedMembers[PlannedIndex];
			if (Planned.Axis == Member.Axis
				&& Planned.Role == Member.Role
				&& Planned.LocalBounds.Min.Equals(Bounds.Min, KINDA_SMALL_NUMBER)
				&& Planned.LocalBounds.Max.Equals(Bounds.Max, KINDA_SMALL_NUMBER))
			{
				return PlannedIndex;
			}
		}
		return INDEX_NONE;
	}

	bool BuildFixture(
		FABTSM73BeamAGenerationResult& OutAssembly,
		FCoupledExteriorFrameResult& OutResult,
		FString& OutError)
	{
		return FABTSM73BeamC3V2CoupledExteriorFrameGenerator().BuildSingleCell(
			MakeFixtureSettings(), MakeBeamASettings(), MakeFixtureBounds(),
			OutAssembly, OutResult, OutError, 0, 0);
	}

	FABTSM73BeamC3V2CoupledExteriorFrameSettings MakeSharedCourseSettings(
		const int32 MaximumFinalMemberCount = 64)
	{
		FABTSM73BeamC3V2CoupledExteriorFrameSettings Settings =
			MakeFixtureSettings();
		Settings.MaximumCellCount = 2;
		Settings.MaximumStructuralMemberCount = ExpectedMemberCount * 2;
		Settings.MaximumFinalMemberCount = MaximumFinalMemberCount;
		return Settings;
	}

	TArray<FCoupledExteriorFrameCellRequest> MakeSharedCourseCells()
	{
		FCoupledExteriorFrameCellRequest Negative;
		Negative.LocalBounds = FBox(
			FVector(-360.0, -108.0, 0.0),
			FVector(-144.0, 108.0, 288.0));
		Negative.BayId = 10;
		Negative.SourceVolumeId = 10;
		Negative.RootAuthorityCrc32 = 1010;
		Negative.CoupledSpanVolumeId = 900;
		Negative.CoupledSupportBayId = 10;
		Negative.CoupledEndpointSign = -1;
		Negative.bRequireSharedCoursePair = true;
		Negative.SharedCoursePairCourseCount = 8;
		Negative.CoupledSpanAxis = EABTSM73BeamAFrameAxis::X;
		Negative.CoupledBearingPlaneCM = -144.0;
		Negative.CoupledRailCenterZCM = 162.0;
		Negative.CoupledRailStationsCM = {-54.0, 54.0};
		Negative.CoupledMinimumSharedCourseBottomZCM = 0.0;

		FCoupledExteriorFrameCellRequest Positive = Negative;
		Positive.LocalBounds = FBox(
			FVector(144.0, -108.0, 0.0),
			FVector(360.0, 108.0, 288.0));
		Positive.BayId = 20;
		Positive.SourceVolumeId = 20;
		Positive.RootAuthorityCrc32 = 2020;
		Positive.CoupledSupportBayId = 20;
		Positive.CoupledEndpointSign = 1;
		Positive.CoupledBearingPlaneCM = 144.0;
		return {Negative, Positive};
	}

	bool BuildSharedCourseFixture(
		const FABTSM73BeamC3V2CoupledExteriorFrameSettings& Settings,
		const TArray<FCoupledExteriorFrameCellRequest>& Cells,
		FABTSM73BeamAGenerationResult& OutAssembly,
		FCoupledExteriorFrameResult& OutResult,
		FString& OutError)
	{
		return FABTSM73BeamC3V2CoupledExteriorFrameGenerator().Generate(
			Settings, MakeBeamASettings(), Cells, OutAssembly, OutResult, OutError);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V2GroundedCoupledFrameTopologyAndDimensionsTest,
	"ABTS.M73DAG.BeamC3V2.GroundedCoupledFrame.TopologyAndDimensions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V2GroundedCoupledFrameTopologyAndDimensionsTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V2;
	using namespace ABTSM73BeamC3V2CoupledExteriorFrameTests;

	FABTSM73BeamAGenerationResult Assembly;
	FCoupledExteriorFrameResult Result;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("Fixture builds: %s"), *Error),
		BuildFixture(Assembly, Result, Error)))
	{
		AddError(Error);
		return false;
	}

	TestTrue(TEXT("Fixture is accepted"), Result.Summary.bAccepted);
	TestTrue(TEXT("Fixture geometry is certified"),
		Result.Summary.bGeometryCertified);
	TestEqual(TEXT("One semantic cell is generated"),
		Result.Summary.CellCount, 1);
	TestEqual(TEXT("One six-course macro band is generated"),
		Result.Summary.MacroBandCount, 1);
	TestEqual(TEXT("The fixture contains exactly 28 members"),
		Assembly.Members.Num(), ExpectedMemberCount);
	TestEqual(TEXT("The plan also contains exactly 28 members"),
		Result.PlannedMembers.Num(), ExpectedMemberCount);
	TestEqual(TEXT("Eight ordinary core rails remain"),
		Result.Summary.CoreRailCount, ExpectedCoreRailCount);
	TestEqual(TEXT("Eight body rails extend through the envelope"),
		Result.Summary.ThroughOutriggerCount, ExpectedThroughOutriggerCount);
	TestEqual(TEXT("Four facade rails are sandwiched"),
		Result.Summary.FacadeRailCount, ExpectedFacadeRailCount);
	TestEqual(TEXT("Eight exterior posts reach the ground"),
		Result.Summary.ExteriorPostCount, ExpectedExteriorPostCount);
	TestEqual(TEXT("All four exterior faces are grounded"),
		static_cast<int32>(Result.Summary.GroundedFaceMask),
		static_cast<int32>(AllFaces));
	TestEqual(TEXT("One cell plan is retained for final certification"),
		Result.CellPlans.Num(), 1);
	if (Result.CellPlans.Num() == 1)
	{
		const FCoupledExteriorFrameCellPlan& CellPlan = Result.CellPlans[0];
		TestEqual(TEXT("Cell course count is frozen at C8"),
			CellPlan.CourseCount, 8);
		TestEqual(TEXT("Cell rail count is frozen at n2"),
			CellPlan.RailCount, 2);
		TestEqual(TEXT("Cell has exactly one macro band"),
			CellPlan.MacroBandCount, 1);
		TestEqual(TEXT("The first macro band starts at course 2"),
			CellPlan.MacroBandStartCourses.Num() == 1
				? CellPlan.MacroBandStartCourses[0] : INDEX_NONE,
			2);
		TestEqual(TEXT("Cell envelope is 648 cm in X"),
			CellPlan.Cell.LocalBounds.GetSize().X, 648.0);
		TestEqual(TEXT("Cell envelope is 648 cm in Y"),
			CellPlan.Cell.LocalBounds.GetSize().Y, 648.0);
		TestEqual(TEXT("Cell envelope is 288 cm tall"),
			CellPlan.Cell.LocalBounds.GetSize().Z, 288.0);
		TestEqual(TEXT("The cell itself records all four grounded faces"),
			static_cast<int32>(CellPlan.GroundedFaceMask),
			static_cast<int32>(AllFaces));
	}

	uint8 ReconstructedGroundedFaceMask = 0;
	for (int32 MemberIndex = 0;
		MemberIndex < Result.PlannedMembers.Num(); ++MemberIndex)
	{
		const FCoupledExteriorFramePlannedMember& Member =
			Result.PlannedMembers[MemberIndex];
		const FVector Size = Member.LocalBounds.GetSize();
		const int32 AxisIndex = static_cast<int32>(Member.Axis);
		TestTrue(*FString::Printf(TEXT("Member %d has a finite positive span"),
			MemberIndex),
			Member.LocalBounds.IsValid
				&& FMath::IsFinite(Member.LocalStart.X)
				&& FMath::IsFinite(Member.LocalStart.Y)
				&& FMath::IsFinite(Member.LocalStart.Z)
				&& FMath::IsFinite(Member.LocalEnd.X)
				&& FMath::IsFinite(Member.LocalEnd.Y)
				&& FMath::IsFinite(Member.LocalEnd.Z)
				&& FVector::Distance(Member.LocalStart, Member.LocalEnd) > 0.0);
		TestTrue(*FString::Printf(TEXT("Member %d is no longer than 720 cm"),
			MemberIndex),
			FVector::Distance(Member.LocalStart, Member.LocalEnd) <= 720.0
				+ KINDA_SMALL_NUMBER);
		for (int32 Dimension = 0; Dimension < 3; ++Dimension)
		{
			if (Dimension != AxisIndex)
			{
				TestTrue(*FString::Printf(
					TEXT("Member %d retains the fixed 36 cm section"), MemberIndex),
					FMath::IsNearlyEqual(Size[Dimension], 36.0,
						KINDA_SMALL_NUMBER));
			}
		}
		if (Member.Kind == ECoupledExteriorFrameMemberKind::ExteriorPost
			&& FMath::IsNearlyZero(Member.LocalBounds.Min.Z,
				KINDA_SMALL_NUMBER))
		{
			ReconstructedGroundedFaceMask |= Member.FaceMask;
		}
		for (int32 OtherIndex = MemberIndex + 1;
			OtherIndex < Result.PlannedMembers.Num(); ++OtherIndex)
		{
			TestFalse(*FString::Printf(
				TEXT("Planned members %d and %d do not penetrate"),
				MemberIndex, OtherIndex),
				HasPositiveVolumeOverlap(
					Member.LocalBounds,
					Result.PlannedMembers[OtherIndex].LocalBounds));
		}
	}
	TestEqual(TEXT("Grounded post roles reconstruct face mask 0xF"),
		static_cast<int32>(ReconstructedGroundedFaceMask),
		static_cast<int32>(AllFaces));
	TestNotEqual(TEXT("The plan hash is populated"), Result.Summary.PlanCrc32, 0u);

	FABTSM73BeamAGenerationResult ReplayAssembly;
	FCoupledExteriorFrameResult ReplayResult;
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Exact replay builds: %s"), *Error),
		BuildFixture(ReplayAssembly, ReplayResult, Error));
	TestEqual(TEXT("Exact replay preserves the plan hash"),
		ReplayResult.Summary.PlanCrc32, Result.Summary.PlanCrc32);
	TestEqual(TEXT("Exact replay preserves the planned geometry hash"),
		ReplayResult.CellPlans.Num() == 1
			? ReplayResult.CellPlans[0].GeometryCrc32 : 0u,
		Result.CellPlans.Num() == 1 ? Result.CellPlans[0].GeometryCrc32 : 0u);
	TestEqual(TEXT("Exact replay preserves final member count"),
		ReplayAssembly.Members.Num(), Assembly.Members.Num());

	FABTSM73BeamC3V2CoupledExteriorFrameSettings TwoCellSettings =
		MakeFixtureSettings();
	TwoCellSettings.MaximumCellCount = 2;
	TwoCellSettings.MaximumStructuralMemberCount = ExpectedMemberCount * 2;
	TwoCellSettings.MaximumFinalMemberCount = ExpectedMemberCount * 2 + 8;
	FCoupledExteriorFrameCellRequest NegativeCell;
	NegativeCell.LocalBounds = MakeFixtureBounds();
	NegativeCell.BayId = 10;
	NegativeCell.SourceVolumeId = 10;
	FCoupledExteriorFrameCellRequest PositiveCell = NegativeCell;
	PositiveCell.LocalBounds = FBox(
		NegativeCell.LocalBounds.Min + FVector(720.0, 0.0, 0.0),
		NegativeCell.LocalBounds.Max + FVector(720.0, 0.0, 0.0));
	PositiveCell.BayId = 20;
	PositiveCell.SourceVolumeId = 20;
	TArray<FCoupledExteriorFrameCellRequest> TwoCells = {
		NegativeCell, PositiveCell};
	FABTSM73BeamAGenerationResult TwoCellAssembly;
	FCoupledExteriorFrameResult TwoCellResult;
	Error.Reset();
	FABTSM73BeamC3V2CoupledExteriorFrameGenerator Generator;
	TestTrue(*FString::Printf(TEXT("Two independent cells build: %s"), *Error),
		Generator.Generate(TwoCellSettings, MakeBeamASettings(), TwoCells,
			TwoCellAssembly, TwoCellResult, Error));
	TestEqual(TEXT("A 64-member admission budget selects both requested cells"),
		TwoCellResult.Summary.CellCount, 2);
	TestEqual(TEXT("Two cells publish exactly 56 structural members"),
		TwoCellAssembly.Members.Num(), ExpectedMemberCount * 2);
	TestEqual(TEXT("Two-cell admission reserves eight closure members"),
		TwoCellResult.Summary.ClosureReserveMemberCount, 8);
	TestEqual(TEXT("No optional cell is budget-limited at 64 members"),
		TwoCellResult.Summary.BudgetLimitedCellCount, 0);

	FABTSM73BeamC3V2CoupledExteriorFrameSettings LimitedSettings =
		TwoCellSettings;
	LimitedSettings.MaximumFinalMemberCount = ExpectedMemberCount * 2;
	FABTSM73BeamAGenerationResult LimitedAssembly;
	FCoupledExteriorFrameResult LimitedResult;
	Error.Reset();
	TestTrue(*FString::Printf(
		TEXT("A budget-limited optional cell falls back transactionally: %s"),
		*Error), Generator.Generate(LimitedSettings, MakeBeamASettings(), TwoCells,
			LimitedAssembly, LimitedResult, Error));
	TestEqual(TEXT("The mandatory anchor remains selected"),
		LimitedResult.Summary.CellCount, 1);
	TestEqual(TEXT("The optional endpoint cell is explicitly budget-limited"),
		LimitedResult.Summary.BudgetLimitedCellCount, 1);
	TestEqual(TEXT("Budget fallback publishes only one complete frame"),
		LimitedAssembly.Members.Num(), ExpectedMemberCount);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V2SeamReleaseE6SharedCourseTest,
	"ABTS.M73DAG.BeamC3V2.GroundedCoupledFrame.SeamReleaseE6SharedCourse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V2SeamReleaseE6SharedCourseTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V2;
	using namespace ABTSM73BeamC3V2CoupledExteriorFrameTests;

	constexpr int32 ExpectedSharedMemberCount = ExpectedMemberCount * 2;
	constexpr int32 ExpectedSharedBearingCount = ExpectedBearingCount * 2;
	const FABTSM73BeamC3V2CoupledExteriorFrameSettings Settings =
		MakeSharedCourseSettings();
	const TArray<FCoupledExteriorFrameCellRequest> Cells =
		MakeSharedCourseCells();
	FABTSM73BeamAGenerationResult Assembly;
	FCoupledExteriorFrameResult Result;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("Atomic shared pair builds: %s"), *Error),
		BuildSharedCourseFixture(Settings, Cells, Assembly, Result, Error)))
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("Shared pair geometry is certified"),
		Result.Summary.bSharedCoursePairCertified);
	TestEqual(TEXT("Exactly two grounded cores are admitted"),
		Result.Summary.CellCount, 2);
	TestEqual(TEXT("One shared course contains exactly R2 continuous rails"),
		Result.Summary.SharedCourseCount, 2);
	TestEqual(TEXT("Shared-course rail identity is explicit"),
		Result.Summary.SharedCourseRailCount, 2);
	TestEqual(TEXT("Minimum-segment pair has exactly 56 planned members"),
		Result.PlannedMembers.Num(), ExpectedSharedMemberCount);
	TestEqual(TEXT("Minimum-segment pair publishes exactly 56 members"),
		Assembly.Members.Num(), ExpectedSharedMemberCount);
	TestEqual(TEXT("The shared pair preserves the two-cell bearing count"),
		Assembly.BearingContacts.Num(), ExpectedSharedBearingCount);
	TestEqual(TEXT("Non-through core-rail count is unchanged"),
		Result.Summary.CoreRailCount, 16);
	TestEqual(TEXT("Two redundant far-side stubs are absorbed"),
		Result.Summary.ThroughOutriggerCount, 14);
	TestEqual(TEXT("Facade count is unchanged"),
		Result.Summary.FacadeRailCount, 8);
	TestEqual(TEXT("Post count is unchanged"),
		Result.Summary.ExteriorPostCount, 16);
	TestNotEqual(TEXT("Shared-course hash is populated"),
		Result.Summary.SharedCourseCrc32, 0u);

	int32 SharedCount = 0;
	for (const FCoupledExteriorFramePlannedMember& Member : Result.PlannedMembers)
	{
		if (Member.Kind != ECoupledExteriorFrameMemberKind::SharedCourse)
		{
			continue;
		}
		++SharedCount;
		TestEqual(TEXT("Shared course keeps the SeamRelease bridge role"),
			Member.Role, EABTSM73BeamAMemberRole::BridgeRail);
		TestEqual(TEXT("The registered X shared through course is course 4"),
			Member.CourseIndex, 4);
		TestEqual(TEXT("Shared course remains at the registered course height"),
			Member.LocalStart.Z, 162.0);
		TestEqual(TEXT("Shared member absorbs one far-side stub"),
			FVector::Distance(Member.LocalStart, Member.LocalEnd), 576.0);
	}
	TestEqual(TEXT("Exactly two planned members carry SharedCourse kind"),
		SharedCount, 2);

	FABTSM73BeamCGenerationResult BeamC;
	FABTSM73BeamCGenerator BeamCGenerator;
	Error.Reset();
	if (!TestTrue(*FString::Printf(TEXT("Beam-C accepts shared pair: %s"), *Error),
		BeamCGenerator.Generate(MakeBeamCSettings(MakeBeamASettings()),
			Assembly, BeamC, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("Every shared-pair member enters the DAG"),
		BeamC.Nodes.Num(), ExpectedSharedMemberCount);
	TestEqual(TEXT("Every shared-pair bearing enters the DAG"),
		BeamC.Edges.Num(), ExpectedSharedBearingCount);
	TestEqual(TEXT("Shared-pair DAG has no unreachable member"),
		BeamC.Summary.GroundUnreachableNodeCount, 0);
	Error.Reset();
	FABTSM73BeamC3V2CoupledExteriorFrameGenerator Generator;
	TestTrue(*FString::Printf(TEXT("Final shared certificate accepts DAG: %s"),
		*Error), Generator.CertifyFinalAssembly(
		Settings, MakeBeamASettings(), Assembly, BeamC, Result, Error));
	TestTrue(TEXT("Final shared result is DAG-certified"),
		Result.Summary.bDAGCertified);

	TArray<FCoupledExteriorFrameCellRequest> ReversedCells = Cells;
	Algo::Reverse(ReversedCells);
	for (FCoupledExteriorFrameCellRequest& Cell : ReversedCells)
	{
		Algo::Reverse(Cell.CoupledRailStationsCM);
	}
	FABTSM73BeamAGenerationResult ReplayAssembly;
	FCoupledExteriorFrameResult ReplayResult;
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Reversed input order replays: %s"), *Error),
		BuildSharedCourseFixture(
			Settings, ReversedCells, ReplayAssembly, ReplayResult, Error));
	TestEqual(TEXT("Input order does not change the plan hash"),
		ReplayResult.Summary.PlanCrc32, Result.Summary.PlanCrc32);
	TestEqual(TEXT("Input order does not change shared geometry hash"),
		ReplayResult.Summary.SharedCourseCrc32,
		Result.Summary.SharedCourseCrc32);

	TArray<FCoupledExteriorFrameCellRequest> TooWideCells = Cells;
		TooWideCells[1].LocalBounds = FBox(
			TooWideCells[1].LocalBounds.Min + FVector(324.0, 0.0, 0.0),
			TooWideCells[1].LocalBounds.Max + FVector(324.0, 0.0, 0.0));
		TooWideCells[1].CoupledBearingPlaneCM += 324.0;
	FABTSM73BeamAGenerationResult TooWideAssembly;
	FCoupledExteriorFrameResult TooWideResult;
	Error.Reset();
	TestFalse(TEXT("A 756 cm shared member fails closed"),
		BuildSharedCourseFixture(
			Settings, TooWideCells, TooWideAssembly, TooWideResult, Error));
	TestTrue(TEXT("Span failure is explicit"),
		Error.Contains(TEXT("BeamC3V2SharedCourseSpanExceeded")));
	TestEqual(TEXT("Span failure publishes no member"),
		TooWideAssembly.Members.Num(), 0);
	TestEqual(TEXT("Span failure publishes no cell plan"),
		TooWideResult.CellPlans.Num(), 0);

	FABTSM73BeamAGenerationResult BudgetAssembly;
	FCoupledExteriorFrameResult BudgetResult;
	Error.Reset();
	TestFalse(TEXT("A one-short atomic pair budget fails closed"),
		BuildSharedCourseFixture(MakeSharedCourseSettings(63), Cells,
			BudgetAssembly, BudgetResult, Error));
	TestTrue(TEXT("Atomic budget failure is explicit"),
		Error.Contains(TEXT("BeamC3V2FinalBudgetInsufficient")));
	TestEqual(TEXT("Atomic budget failure cannot retain one core"),
		BudgetAssembly.Members.Num(), 0);
	TestEqual(TEXT("Atomic budget failure cannot retain one cell plan"),
		BudgetResult.CellPlans.Num(), 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V2GroundedCoupledFrameDAGContractTest,
	"ABTS.M73DAG.BeamC3V2.GroundedCoupledFrame.DAGContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V2GroundedCoupledFrameDAGContractTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V2;
	using namespace ABTSM73BeamC3V2CoupledExteriorFrameTests;

	FABTSM73BeamAGenerationResult Assembly;
	FCoupledExteriorFrameResult Result;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("Fixture builds: %s"), *Error),
		BuildFixture(Assembly, Result, Error)))
	{
		AddError(Error);
		return false;
	}

	TestEqual(TEXT("The role contract requires exactly 52 contacts"),
		Result.Summary.RequiredBearingContactCount, ExpectedBearingCount);
	TestEqual(TEXT("Beam-A reconstructs exactly 52 real bearing contacts"),
		Assembly.BearingContacts.Num(), ExpectedBearingCount);

	TArray<ECoupledExteriorFrameMemberKind> KindByMemberId;
	KindByMemberId.Init(
		ECoupledExteriorFrameMemberKind::CoreRail, Assembly.Members.Num());
	TArray<bool> bMatchedMember;
	bMatchedMember.Init(false, Assembly.Members.Num());
	for (const FABTSM73BeamAMember& Member : Assembly.Members)
	{
		const int32 PlannedIndex = FindPlannedMemberIndex(Member, Assembly, Result);
		TestTrue(*FString::Printf(TEXT("Closed member %d retains a planned role"),
			Member.MemberId), PlannedIndex != INDEX_NONE);
		if (PlannedIndex != INDEX_NONE
			&& KindByMemberId.IsValidIndex(Member.MemberId))
		{
			KindByMemberId[Member.MemberId] =
				Result.PlannedMembers[PlannedIndex].Kind;
			bMatchedMember[Member.MemberId] = true;
		}
	}

	int32 BodyCourseContacts = 0;
	int32 FacadeSandwichContacts = 0;
	int32 ExteriorPostContacts = 0;
	for (const FABTSM73BeamABearingContact& Contact : Assembly.BearingContacts)
	{
		if (!bMatchedMember.IsValidIndex(Contact.LowerMemberId)
			|| !bMatchedMember.IsValidIndex(Contact.UpperMemberId)
			|| !bMatchedMember[Contact.LowerMemberId]
			|| !bMatchedMember[Contact.UpperMemberId])
		{
			AddError(TEXT("Bearing contact references an unmatched member"));
			continue;
		}
		const ECoupledExteriorFrameMemberKind LowerKind =
			KindByMemberId[Contact.LowerMemberId];
		const ECoupledExteriorFrameMemberKind UpperKind =
			KindByMemberId[Contact.UpperMemberId];
		if (LowerKind == ECoupledExteriorFrameMemberKind::ExteriorPost
			|| UpperKind == ECoupledExteriorFrameMemberKind::ExteriorPost)
		{
			++ExteriorPostContacts;
		}
		else if (LowerKind == ECoupledExteriorFrameMemberKind::FacadeRail
			|| UpperKind == ECoupledExteriorFrameMemberKind::FacadeRail)
		{
			++FacadeSandwichContacts;
		}
		else
		{
			++BodyCourseContacts;
		}
	}
	TestEqual(TEXT("Seven body interfaces contribute 7*2*2 contacts"),
		BodyCourseContacts, ExpectedBodyCourseBearingCount);
	TestEqual(TEXT("Four facade rails contribute sixteen sandwich contacts"),
		FacadeSandwichContacts, ExpectedFacadeSandwichBearingCount);
	TestEqual(TEXT("Eight grounded exterior posts each contact one rail"),
		ExteriorPostContacts, ExpectedExteriorPostBearingCount);
	TestEqual(TEXT("The role-level contact partition is exact"),
		BodyCourseContacts + FacadeSandwichContacts + ExteriorPostContacts,
		ExpectedBearingCount);

	const FABTSM73BeamAPreviewSettings BeamASettings = MakeBeamASettings();
	const FABTSM73BeamCPreviewSettings BeamCSettings =
		MakeBeamCSettings(BeamASettings);
	FABTSM73BeamCGenerationResult BeamC;
	Error.Reset();
	FABTSM73BeamCGenerator BeamCGenerator;
	if (!TestTrue(*FString::Printf(TEXT("Beam-C accepts the fixture: %s"),
		*Error), BeamCGenerator.Generate(
			BeamCSettings, Assembly, BeamC, Error)))
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("The load DAG is accepted"), BeamC.Summary.bAccepted);
	TestEqual(TEXT("Every member appears exactly once in the load DAG"),
		BeamC.Nodes.Num(), ExpectedMemberCount);
	TestEqual(TEXT("Every real bearing appears exactly once in the load DAG"),
		BeamC.Edges.Num(), ExpectedBearingCount);
	TestEqual(TEXT("The load DAG contains no cycle nodes"),
		BeamC.Summary.CycleNodeCount, 0);
	TestEqual(TEXT("Every load node reaches ground"),
		BeamC.Summary.GroundUnreachableNodeCount, 0);
	for (const FABTSM73BeamCLoadNode& Node : BeamC.Nodes)
	{
		TestTrue(*FString::Printf(TEXT("Member %d is ground reachable"),
			Node.MemberId), Node.bGroundReachable);
	}

	Error.Reset();
	FABTSM73BeamC3V2CoupledExteriorFrameGenerator FrameGenerator;
	TestTrue(*FString::Printf(TEXT("Final V2 certification accepts the DAG: %s"),
		*Error), FrameGenerator.CertifyFinalAssembly(
			MakeFixtureSettings(), BeamASettings, Assembly, BeamC, Result, Error));
	TestTrue(TEXT("The accepted fixture carries final DAG evidence"),
		Result.Summary.bDAGCertified);
	TestNotEqual(TEXT("The final DAG evidence hash is populated"),
		Result.Summary.DAGEvidenceCrc32, 0u);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V2FinalAllZSpanFailClosedTest,
	"ABTS.M73DAG.BeamC3V2.GroundedCoupledFrame.FinalAllZSpanFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V2FinalAllZSpanFailClosedTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V2CoupledExteriorFrameTests;

	FABTSM73BeamAGenerationResult Assembly;
	ABTSM73BeamC3V2::FCoupledExteriorFrameResult Result;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("Fixture builds: %s"), *Error),
		BuildFixture(Assembly, Result, Error)))
	{
		return false;
	}

	// A final, non-plan Z member is deliberately placed outside the cell so
	// geometry matching still succeeds. Beam-C can route its self load directly
	// to ground, but V2 must not publish that DAG when the segment exceeds the
	// registered 720 cm structural limit.
	const int32 BottomJointId = Assembly.Joints.Num();
	FABTSM73BeamAJoint& Bottom = Assembly.Joints.AddDefaulted_GetRef();
	Bottom.JointId = BottomJointId;
	Bottom.LocalPosition = FVector(1000.0, 0.0, 18.0);
	Bottom.Role = EABTSM73BeamAJointRole::GroundFoot;
	const int32 TopJointId = Assembly.Joints.Num();
	FABTSM73BeamAJoint& Top = Assembly.Joints.AddDefaulted_GetRef();
	Top.JointId = TopJointId;
	Top.LocalPosition = FVector(1000.0, 0.0, 774.0);
	Top.Role = EABTSM73BeamAJointRole::ColumnHead;
	const int32 RogueMemberId = Assembly.Members.Num();
	FABTSM73BeamAMember& Rogue = Assembly.Members.AddDefaulted_GetRef();
	Rogue.MemberId = RogueMemberId;
	Rogue.JointA = BottomJointId;
	Rogue.JointB = TopJointId;
	Rogue.Axis = EABTSM73BeamAFrameAxis::Z;
	Rogue.Role = EABTSM73BeamAMemberRole::BridgePost;
	Rogue.LengthCM = 756.0f;
	if (!Assembly.Assemblies.IsEmpty())
	{
		Assembly.Assemblies[0].JointIds.Add(BottomJointId);
		Assembly.Assemblies[0].JointIds.Add(TopJointId);
		Assembly.Assemblies[0].MemberIds.Add(RogueMemberId);
	}

	FABTSM73BeamCGenerationResult BeamC;
	FABTSM73BeamCGenerator BeamCGenerator;
	Error.Reset();
	if (!TestTrue(*FString::Printf(
		TEXT("Beam-C accepts the rooted rogue fixture: %s"), *Error),
		BeamCGenerator.Generate(
			MakeBeamCSettings(MakeBeamASettings()), Assembly, BeamC, Error)))
	{
		return false;
	}

	Error.Reset();
	FABTSM73BeamC3V2CoupledExteriorFrameGenerator FrameGenerator;
	TestFalse(TEXT("Final V2 certification rejects every overlong Z segment"),
		FrameGenerator.CertifyFinalAssembly(
			MakeFixtureSettings(), MakeBeamASettings(), Assembly,
			BeamC, Result, Error));
	TestTrue(*FString::Printf(TEXT("Stable all-Z rejection: %s"), *Error),
		Error.StartsWith(TEXT("BeamC3V2FinalAllZSpanExceeded:756.00>720.00")));
	TestFalse(TEXT("Rejected all-Z evidence cannot remain DAG-certified"),
		Result.Summary.bDAGCertified);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V2GroundedCoupledFrameFailClosedTest,
	"ABTS.M73DAG.BeamC3V2.GroundedCoupledFrame.FailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V2GroundedCoupledFrameFailClosedTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V2;
	using namespace ABTSM73BeamC3V2CoupledExteriorFrameTests;

	const FABTSM73BeamC3V2CoupledExteriorFrameGenerator Generator;
	const FABTSM73BeamC3V2CoupledExteriorFrameSettings Settings =
		MakeFixtureSettings();

	FABTSM73BeamAPreviewSettings OneShortBeamA = MakeBeamASettings();
	OneShortBeamA.MaxMemberCount = ExpectedMemberCount - 1;
	FABTSM73BeamAGenerationResult BudgetAssembly;
	FCoupledExteriorFrameResult BudgetResult;
	FString Error;
	TestFalse(TEXT("A 27-member output budget rejects the 28-member fixture"),
		Generator.BuildSingleCell(Settings, OneShortBeamA, MakeFixtureBounds(),
			BudgetAssembly, BudgetResult, Error));
	TestTrue(TEXT("The one-short rejection identifies the exact budget"),
		Error.Contains(TEXT("BeamC3V2FinalBudgetInsufficient:28>27")));
	TestEqual(TEXT("Budget rejection publishes no members"),
		BudgetAssembly.Members.Num(), 0);
	TestEqual(TEXT("Budget rejection publishes no joints"),
		BudgetAssembly.Joints.Num(), 0);
	TestEqual(TEXT("Budget rejection publishes no assemblies"),
		BudgetAssembly.Assemblies.Num(), 0);
	TestEqual(TEXT("Budget rejection publishes no planned members"),
		BudgetResult.PlannedMembers.Num(), 0);
	TestEqual(TEXT("Budget rejection publishes no cell plans"),
		BudgetResult.CellPlans.Num(), 0);
	TestFalse(TEXT("Budget rejection cannot be accepted"),
		BudgetResult.Summary.bAccepted);

	FABTSM73BeamAGenerationResult SpanAssembly;
	FCoupledExteriorFrameResult SpanResult;
	Error.Reset();
	TestFalse(TEXT("A 756 cm member envelope violates the 720 cm gate"),
		Generator.BuildSingleCell(Settings, MakeBeamASettings(),
			MakeFixtureBounds(756.0), SpanAssembly, SpanResult, Error));
	TestTrue(TEXT("The illegal span is rejected before publication"),
		Error.Contains(TEXT("BeamC3V2CellEnvelopeUnsupported")));
	TestEqual(TEXT("Span rejection publishes no members"),
		SpanAssembly.Members.Num(), 0);
	TestEqual(TEXT("Span rejection publishes no joints"),
		SpanAssembly.Joints.Num(), 0);
	TestEqual(TEXT("Span rejection publishes no assemblies"),
		SpanAssembly.Assemblies.Num(), 0);
	TestEqual(TEXT("Span rejection publishes no planned members"),
		SpanResult.PlannedMembers.Num(), 0);
	TestEqual(TEXT("Span rejection publishes no cell plans"),
		SpanResult.CellPlans.Num(), 0);
	TestFalse(TEXT("Span rejection cannot be accepted"),
		SpanResult.Summary.bAccepted);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V2SeamReleaseE6SharedCourseProductionSeamTest,
	"ABTS.M73DAG.BeamC3V2.SeamReleaseE6SharedCourseProductionSeam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V2SeamReleaseE6SharedCourseProductionSeamTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V2;

	constexpr int32 CandidateSeed = 972217611;
	FABTSM73BeamD0ResolvedProfile Profile;
	FString Error;
	const bool bResolved = FABTSM73BeamD0ProfileCatalog::GetDefault().Resolve(
		TEXT("SeamRelease"), 5, CandidateSeed, Profile, Error);
	if (!TestTrue(*FString::Printf(TEXT("E6 candidate resolves: %s"), *Error),
		bResolved))
	{
		return false;
	}

	FABTSM73DAG5BV2GenerationResult Silhouette;
	FABTSM73DAG5BShapeGrammarV2 ShapeGenerator;
	Error.Reset();
	const bool bShape = ShapeGenerator.Generate(
		Profile.BeamSettings.BeamB.BeamA.Silhouette, Silhouette, Error);
	if (!TestTrue(*FString::Printf(TEXT("E6 silhouette builds: %s"), *Error),
		bShape))
	{
		return false;
	}

	FABTSM73BeamAGenerationResult BeamA;
	FABTSM73BeamAGenerator BeamAGenerator;
	Error.Reset();
	const bool bBeamA = BeamAGenerator.Generate(
		Profile.BeamSettings.BeamB.BeamA, Silhouette, BeamA, Error);
	if (!TestTrue(*FString::Printf(TEXT("E6 Beam-A builds: %s"), *Error),
		bBeamA))
	{
		return false;
	}

	FABTSM73BeamBGenerationResult BeamB;
	FABTSM73BeamBGenerator BeamBGenerator;
	Error.Reset();
	const bool bBeamB = BeamBGenerator.Generate(
		Profile.BeamSettings.BeamB, Silhouette, BeamA, BeamB, Error);
	if (!TestTrue(*FString::Printf(TEXT("E6 Beam-B builds: %s"), *Error),
		bBeamB))
	{
		return false;
	}

	TArray<FCoupledExteriorFrameCellRequest> Cells;
	Error.Reset();
	const bool bDerived = ABTSM73BeamD1::DeriveCoupledExteriorFrameCells(
		Profile.CoupledExteriorFrame, Profile.BeamSettings.BeamB.BeamA,
		Silhouette, BeamB, 1, true, Cells, Error);
	if (!TestTrue(*FString::Printf(
		TEXT("E6 derives its atomic endpoint pair: %s"), *Error), bDerived))
	{
		return false;
	}
	TestEqual(TEXT("E6 derives exactly two endpoint cells"), Cells.Num(), 2);
	for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
	{
		TestEqual(*FString::Printf(
			TEXT("E6 cell %d stops below the roof at 52 courses"), CellIndex),
			Cells[CellIndex].SharedCoursePairCourseCount, 52);
		TestEqual(*FString::Printf(
			TEXT("E6 cell %d is 1872 cm tall"), CellIndex),
			Cells[CellIndex].LocalBounds.GetSize().Z, 1872.0);
		TestEqual(*FString::Printf(
			TEXT("E6 cell %d consumes the two Beam-B envelope rails"), CellIndex),
			Cells[CellIndex].CoupledRailStationsCM.Num(), 2);
	}
	TestTrue(TEXT("E6 endpoint cells agree on the bridge rail height"),
		FMath::IsNearlyEqual(Cells[0].CoupledRailCenterZCM,
			Cells[1].CoupledRailCenterZCM,
			Profile.BeamSettings.BeamB.BeamA.JointMergeToleranceCM));
	TestTrue(TEXT("E6 endpoint cells agree on both bridge envelope stations"),
		FMath::IsNearlyEqual(Cells[0].CoupledRailStationsCM[0],
			Cells[1].CoupledRailStationsCM[0],
			Profile.BeamSettings.BeamB.BeamA.JointMergeToleranceCM)
		&& FMath::IsNearlyEqual(Cells[0].CoupledRailStationsCM[1],
			Cells[1].CoupledRailStationsCM[1],
			Profile.BeamSettings.BeamB.BeamA.JointMergeToleranceCM));

	FCoupledExteriorFrameResult Result;
	FABTSM73BeamC3V2CoupledExteriorFrameGenerator FrameGenerator;
	Error.Reset();
	const bool bGenerated = FrameGenerator.Generate(
		Profile.CoupledExteriorFrame, Profile.BeamSettings.BeamB.BeamA,
		Cells, BeamB.ClosedAssembly, Result, Error);
	if (!TestTrue(*FString::Printf(
		TEXT("E6 production shared course generates: %s"), *Error), bGenerated))
	{
		return false;
	}
	TestTrue(TEXT("E6 production shared course is geometry-certified"),
		Result.Summary.bGeometryCertified);
	TestTrue(TEXT("E6 production shared pair is certified"),
		Result.Summary.bSharedCoursePairCertified);
	TestEqual(TEXT("E6 atomic pair uses the budget-derived R4 contract"),
		Result.CellPlans.IsEmpty() ? 0 : Result.CellPlans[0].RailCount, 4);
	TestEqual(TEXT("E6 publishes one shared member per Beam-B envelope rail"),
		Result.Summary.SharedCourseCount, 2);
	TestEqual(TEXT("E6 keeps R4 cores plus the four-member shared cover"),
		Result.PlannedMembers.Num(), 540);
	int32 SharedCourseCount = 0;
	TSet<int32> SharedRailIndices;
	for (const FCoupledExteriorFramePlannedMember& Member : Result.PlannedMembers)
	{
		if (Member.Kind != ECoupledExteriorFrameMemberKind::SharedCourse)
		{
			continue;
		}
		++SharedCourseCount;
		SharedRailIndices.Add(Member.RailIndex);
		TestEqual(TEXT("E6 bridge-aligned shared course is course 44"),
			Member.CourseIndex, 44);
		TestEqual(TEXT("E6 shared course stays on the exact C3 lattice"),
			Member.LocalStart.Z, 1602.0);
	}
	TestEqual(TEXT("E6 shares exactly the two real Beam-B rails"),
		SharedCourseCount, 2);
	TestEqual(TEXT("E6 shared rails retain two deterministic identities"),
		SharedRailIndices.Num(), 2);

	FABTSM73BeamCGenerationResult BeamC;
	FABTSM73BeamCGenerator BeamCGenerator;
	Error.Reset();
	const bool bBeamC = BeamCGenerator.GenerateWithStructuralClosure(
		Profile.BeamSettings, BeamB.ClosedAssembly, BeamC, Error,
		Profile.CoupledExteriorFrame.MaximumFinalMemberCount, true);
	if (!TestTrue(*FString::Printf(
		TEXT("E6 production shared course closes Beam-C: %s"), *Error), bBeamC))
	{
		return false;
	}
	Error.Reset();
	const bool bCertified = FrameGenerator.CertifyFinalAssembly(
		Profile.CoupledExteriorFrame, Profile.BeamSettings.BeamB.BeamA,
		BeamB.ClosedAssembly, BeamC, Result, Error);
	if (!TestTrue(*FString::Printf(
		TEXT("E6 final shared pair remains DAG-certified: %s"), *Error),
		bCertified))
	{
		return false;
	}

	FABTSM73BeamD1GenerationResult Compiled;
	FABTSM73BeamD1BrickCompiler Compiler;
	Error.Reset();
	const bool bCompiled = Compiler.CompileResolved(
		Profile, BeamB, BeamC, Compiled, Error);
	if (!TestTrue(*FString::Printf(
		TEXT("E6 exact shared-pair candidate compiles to bricks: %s"), *Error),
		bCompiled))
	{
		return false;
	}
	TestTrue(TEXT("E6 exact candidate reaches its registered brick window"),
		Compiled.Bricks.Num() >= Profile.VisualComplexity.MinimumBrickCount
			&& Compiled.Bricks.Num() <= Profile.VisualComplexity.MaximumBrickCount);
	return !HasAnyErrors();
}

#endif
