// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PCG/ABTSM3TaskGraphGenerator.h"
#include "Planet/ABTSM2Planet.h"
#include "World/ABTSM110FinaleTypes.h"

namespace
{
	TArray<FABTSM2Cell> BuildM110LogicalCells(const int32 Subdivision)
	{
		AABTSM2Planet::FUnitSphereMesh Mesh;
		AABTSM2Planet::BuildUnitIcosphere(Subdivision, Mesh);
		TArray<FABTSM2Cell> Cells;
		Cells.SetNum(Mesh.Vertices.Num());
		for (int32 CellId = 0; CellId < Mesh.Vertices.Num(); ++CellId)
		{
			Cells[CellId].UnitCenter = Mesh.Vertices[CellId];
		}
		const auto AddNeighbour = [&Cells](const int32 A, const int32 B)
		{
			Cells[A].NeighborCellIds.AddUnique(B);
			Cells[B].NeighborCellIds.AddUnique(A);
		};
		for (const FIntVector& Triangle : Mesh.Triangles)
		{
			AddNeighbour(Triangle.X, Triangle.Y);
			AddNeighbour(Triangle.Y, Triangle.Z);
			AddNeighbour(Triangle.Z, Triangle.X);
		}
		for (FABTSM2Cell& Cell : Cells)
		{
			Cell.NeighborCellIds.Sort();
			Cell.bIsPentagon = Cell.NeighborCellIds.Num() == 5;
		}
		return Cells;
	}

	const FABTSM3TaskNode* FindM110Task(
		const TArray<FABTSM3TaskNode>& Tasks,
		const EABTSM3TaskType Type)
	{
		return Tasks.FindByPredicate([Type](const FABTSM3TaskNode& Task)
		{
			return Task.Type == Type;
		});
	}

	int32 FindM110BuildingAnchor(
		const TArray<FABTSM3CellState>& CellStates,
		const FABTSM3TaskNode& Task)
	{
		for (int32 CellId = 0; CellId < CellStates.Num(); ++CellId)
		{
			if (CellStates[CellId].bBuildingAnchor && CellStates[CellId].TaskId == Task.TaskId)
			{
				return CellId;
			}
		}
		return INDEX_NONE;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM110FinaleFrameTest,
	"ABTS.M110.FinaleFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM110FinaleFrameTest::RunTest(const FString& Parameters)
{
	FABTSM110FinaleLocalFrame Frame;
	Frame.LaunchTaskId = 6;
	Frame.AnchorCellId = 42;
	Frame.SlotPairId = 1234;
	Frame.WorldTransform = FTransform(FQuat::Identity, FVector(100.0, 200.0, 300.0));
	Frame.LeftSlotWorldLocation = FVector(100.0, 100.0, 300.0);
	Frame.RightSlotWorldLocation = FVector(100.0, 300.0, 300.0);
	Frame.bValid = true;

	TestTrue(TEXT("Canonical frame is usable"), Frame.IsUsable());
	FABTSM110FinaleLocalFrame GroundedFrame = Frame;
	GroundedFrame.LeftSlotWorldLocation.Z -= 25.0;
	GroundedFrame.RightSlotWorldLocation.Z += 25.0;
	TestTrue(TEXT("Independently grounded pair keeps a usable planar Y axis"),
		GroundedFrame.IsUsable());
	FABTSM110FinaleLocalFrame ExcessiveTiltFrame = Frame;
	ExcessiveTiltFrame.LeftSlotWorldLocation = FVector(100.0, 190.0, 100.0);
	ExcessiveTiltFrame.RightSlotWorldLocation = FVector(100.0, 210.0, 500.0);
	TestFalse(TEXT("Excessively radial slot pair fails closed"),
		ExcessiveTiltFrame.IsUsable());
	const FVector Local(12.0, -30.0, 8.0);
	TestTrue(TEXT("Local/world transform round-trips"),
		Frame.InverseTransformPosition(Frame.TransformLocalPosition(Local)).Equals(Local, 1.0e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM110FourBodyContractTest,
	"ABTS.M110.FourBodyContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM110FourBodyContractTest::RunTest(const FString& Parameters)
{
	FABTSM110FinaleGravityScenario Scenario;
	Scenario.LayoutVersion = 1;
	Scenario.ScenarioHash = 0x1100u;
	for (int32 Index = 0; Index < FABTSM110FinaleGravityScenario::BodyCount; ++Index)
	{
		FABTSM110FinaleGravityBody& Body = Scenario.Bodies[Index];
		Body.CenterCM = FVector3d(static_cast<double>(Index) * 10000.0, 0.0, 0.0);
		Body.GravitationalParameterCM3PerSec2 = 1.0e8 + static_cast<double>(Index);
		Body.CollisionRadiusCM = 100.0;
	}

	FString Failure;
	TestTrue(TEXT("Primary plus three ordered assist planets form a valid scenario"), Scenario.IsValid(&Failure));
	TestEqual(TEXT("The data-side contract always contains exactly four gravity bodies"),
		FABTSM110FinaleGravityScenario::BodyCount, 4);
	TestFalse(TEXT("Acceleration remains finite"),
		Scenario.GetAccelerationAt(FVector3d(0.0, 1000.0, 0.0)).ContainsNaN());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM110TaskGraphFinaleSeparationTest,
	"ABTS.M110.TaskGraphFinaleSeparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM110TaskGraphFinaleSeparationTest::RunTest(const FString& Parameters)
{
	const TArray<FABTSM2Cell> Cells = BuildM110LogicalCells(5);
	const FABTSM3PCGConfig Config;
	const FABTSM3TaskGraphGenerator Generator;
	TArray<int32> Seeds;
	for (int32 Seed = 0; Seed < 100; ++Seed)
	{
		Seeds.Add(Seed);
	}
	Seeds.Add(312503);
	Seeds.Add(20260727);
	Seeds.Add(8675309);

	for (const int32 Seed : Seeds)
	{
		TArray<FABTSM3TaskNode> Tasks;
		TArray<FABTSM3TaskLink> Links;
		TArray<FABTSM3CellState> CellStates;
		TArray<FABTSM3CellEdgeState> EdgeStates;
		FABTSM3PCGSummary Summary;
		const bool bGenerated = Generator.Generate(
			Seed,
			Config,
			Cells,
			Tasks,
			Links,
			CellStates,
			EdgeStates,
			Summary);
		TestTrue(FString::Printf(TEXT("Seed %d produces an accepted complete world"), Seed), bGenerated);
		if (!bGenerated)
		{
			continue;
		}

		const FABTSM3TaskNode* LaunchTask = FindM110Task(Tasks, EABTSM3TaskType::LaunchSite);
		const FABTSM3TaskNode* SatelliteTask = FindM110Task(Tasks, EABTSM3TaskType::SatelliteWindow);
		TestNotNull(FString::Printf(TEXT("Seed %d has one LaunchSite"), Seed), LaunchTask);
		TestNotNull(FString::Printf(TEXT("Seed %d has one SatelliteWindow"), Seed), SatelliteTask);
		if (LaunchTask == nullptr || SatelliteTask == nullptr)
		{
			continue;
		}

		const int32 LaunchAnchorCellId = FindM110BuildingAnchor(CellStates, *LaunchTask);
		TestTrue(
			FString::Printf(TEXT("Seed %d has a certified LaunchSite anchor"), Seed),
			Cells.IsValidIndex(LaunchAnchorCellId));
		TestTrue(
			FString::Printf(TEXT("Seed %d has a valid SatelliteWindow seed"), Seed),
			Cells.IsValidIndex(SatelliteTask->SeedCellId));
		if (!Cells.IsValidIndex(LaunchAnchorCellId) || !Cells.IsValidIndex(SatelliteTask->SeedCellId))
		{
			continue;
		}

		const float ActualSeparationDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(
				Cells[LaunchAnchorCellId].UnitCenter,
				Cells[SatelliteTask->SeedCellId].UnitCenter),
			-1.0f,
			1.0f)));
		TestTrue(
			FString::Printf(TEXT("Seed %d satisfies the finale/satellite angular separation"), Seed),
			ActualSeparationDegrees + KINDA_SMALL_NUMBER >= Config.MinSatelliteLaunchAngularSeparationDegrees);
		TestTrue(
			FString::Printf(TEXT("Seed %d summary records the validated separation"), Seed),
			FMath::IsNearlyEqual(
				ActualSeparationDegrees,
				Summary.SatelliteLaunchAngularSeparationDegrees,
				1.0e-3f));

		TArray<FABTSM3TaskNode> RepeatTasks;
		TArray<FABTSM3TaskLink> RepeatLinks;
		TArray<FABTSM3CellState> RepeatCellStates;
		TArray<FABTSM3CellEdgeState> RepeatEdgeStates;
		FABTSM3PCGSummary RepeatSummary;
		const bool bRepeatGenerated = Generator.Generate(
			Seed,
			Config,
			Cells,
			RepeatTasks,
			RepeatLinks,
			RepeatCellStates,
			RepeatEdgeStates,
			RepeatSummary);
		TestTrue(FString::Printf(TEXT("Seed %d deterministically regenerates"), Seed), bRepeatGenerated);
		if (!bRepeatGenerated)
		{
			continue;
		}
		TestEqual(
			FString::Printf(TEXT("Seed %d repeats the same accepted attempt"), Seed),
			RepeatSummary.AttemptIndex,
			Summary.AttemptIndex);
		TestEqual(
			FString::Printf(TEXT("Seed %d repeats the same task count"), Seed),
			RepeatTasks.Num(),
			Tasks.Num());
		for (int32 TaskIndex = 0; TaskIndex < FMath::Min(Tasks.Num(), RepeatTasks.Num()); ++TaskIndex)
		{
			TestEqual(
				FString::Printf(TEXT("Seed %d repeats Task %d seed Cell"), Seed, TaskIndex),
				RepeatTasks[TaskIndex].SeedCellId,
				Tasks[TaskIndex].SeedCellId);
		}
		TestTrue(
			FString::Printf(TEXT("Seed %d repeats the validated separation"), Seed),
			FMath::IsNearlyEqual(
				RepeatSummary.SatelliteLaunchAngularSeparationDegrees,
				Summary.SatelliteLaunchAngularSeparationDegrees,
				1.0e-3f));
	}
	return true;
}

#endif
