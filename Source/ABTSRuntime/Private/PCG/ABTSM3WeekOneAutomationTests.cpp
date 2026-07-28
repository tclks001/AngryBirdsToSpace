// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PCG/ABTSM3TaskGraphGenerator.h"
#include "Planet/ABTSM2Planet.h"

namespace
{
struct FABTSM3WeekOneGeneratedWorld
{
	TArray<FABTSM3TaskNode> Tasks;
	TArray<FABTSM3TaskLink> Links;
	TArray<FABTSM3CellState> CellStates;
	TArray<FABTSM3CellEdgeState> EdgeStates;
	FABTSM3PCGSummary Summary;
};

TArray<FABTSM2Cell> BuildM3WeekOneLogicalCells(const int32 Subdivision = 5)
{
	AABTSM2Planet::FUnitSphereMesh Mesh;
	AABTSM2Planet::BuildUnitIcosphere(Subdivision, Mesh);

	TArray<FABTSM2Cell> Cells;
	Cells.SetNum(Mesh.Vertices.Num());
	for (int32 CellId = 0; CellId < Mesh.Vertices.Num(); ++CellId)
	{
		Cells[CellId].UnitCenter = Mesh.Vertices[CellId];
	}

	const auto AddNeighbour = [&Cells](const int32 CellA, const int32 CellB)
	{
		Cells[CellA].NeighborCellIds.AddUnique(CellB);
		Cells[CellB].NeighborCellIds.AddUnique(CellA);
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

const FABTSM3TaskNode* FindTaskByType(
	const TArray<FABTSM3TaskNode>& Tasks,
	const EABTSM3TaskType Type)
{
	return Tasks.FindByPredicate([Type](const FABTSM3TaskNode& Task)
	{
		return Task.Type == Type;
	});
}

const FABTSM3TaskNode* FindTaskById(
	const TArray<FABTSM3TaskNode>& Tasks,
	const int32 TaskId)
{
	return Tasks.FindByPredicate([TaskId](const FABTSM3TaskNode& Task)
	{
		return Task.TaskId == TaskId;
	});
}

bool IsOrdinaryBuildingTask(const EABTSM3TaskType Type)
{
	return Type == EABTSM3TaskType::Workshop
		|| Type == EABTSM3TaskType::TargetBuilding
		|| Type == EABTSM3TaskType::FurnaceRuins;
}

constexpr float ReferencePlanetRadiusCM = 10000.0f;
constexpr float ReferenceTerrainHeightScaleCM = 900.0f;

bool IsMainRouteLink(const FABTSM3TaskLink& Link)
{
	return Link.Role == EABTSM3TaskLinkRole::MainPath
		|| Link.Role == EABTSM3TaskLinkRole::LockedGate;
}

bool HasUniqueOrderedMainRoute(
	const TArray<FABTSM3TaskNode>& Tasks,
	const TArray<FABTSM3TaskLink>& Links,
	const int32 StartTaskId,
	const int32 LaunchTaskId)
{
	TSet<int32> VisitedTasks;
	TSet<int32> VisitedCells;
	int32 PreviousCellId = INDEX_NONE;
	int32 CurrentTaskId = StartTaskId;
	for (int32 Guard = 0; Guard < Tasks.Num(); ++Guard)
	{
		if (CurrentTaskId == LaunchTaskId)
		{
			return true;
		}
		if (VisitedTasks.Contains(CurrentTaskId))
		{
			return false;
		}
		VisitedTasks.Add(CurrentTaskId);
		const FABTSM3TaskLink* Link =
			Links.FindByPredicate([CurrentTaskId](const FABTSM3TaskLink& Candidate)
			{
				return Candidate.TaskA == CurrentTaskId
					&& IsMainRouteLink(Candidate);
			});
		if (Link == nullptr)
		{
			return false;
		}
		for (int32 CellIndex = 0; CellIndex < Link->CorridorCells.Num(); ++CellIndex)
		{
			const int32 CellId = Link->CorridorCells[CellIndex];
			if (CellIndex == 0 && PreviousCellId != INDEX_NONE)
			{
				if (CellId != PreviousCellId)
				{
					return false;
				}
				continue;
			}
			if (VisitedCells.Contains(CellId))
			{
				return false;
			}
			VisitedCells.Add(CellId);
			PreviousCellId = CellId;
		}
		CurrentTaskId = Link->TaskB;
	}
	return CurrentTaskId == LaunchTaskId;
}

bool TryComputeReferenceCorridorLengthCM(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<int32>& CorridorCells,
	float& OutLengthCM)
{
	OutLengthCM = 0.0f;
	if (CorridorCells.Num() < 2)
	{
		return false;
	}

	for (int32 Index = 0; Index + 1 < CorridorCells.Num(); ++Index)
	{
		const int32 CellA = CorridorCells[Index];
		const int32 CellB = CorridorCells[Index + 1];
		if (!Cells.IsValidIndex(CellA)
			|| !Cells.IsValidIndex(CellB)
			|| !Cells[CellA].NeighborCellIds.Contains(CellB))
		{
			return false;
		}

		const double Dot = FMath::Clamp(
			static_cast<double>(FVector::DotProduct(
				Cells[CellA].UnitCenter.GetSafeNormal(),
				Cells[CellB].UnitCenter.GetSafeNormal())),
			-1.0,
			1.0);
		OutLengthCM += static_cast<float>(
			FMath::Acos(Dot) * static_cast<double>(ReferencePlanetRadiusCM));
	}
	return true;
}

TArray<int32> BuildReferenceMainRouteHopDistances(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3TaskLink>& Links)
{
	TArray<int32> Distances;
	Distances.Init(MAX_int32, Cells.Num());
	TArray<int32> Queue;
	for (const FABTSM3TaskLink& Link : Links)
	{
		if (!IsMainRouteLink(Link))
		{
			continue;
		}
		for (const int32 CellId : Link.CorridorCells)
		{
			if (Cells.IsValidIndex(CellId) && Distances[CellId] != 0)
			{
				Distances[CellId] = 0;
				Queue.Add(CellId);
			}
		}
	}

	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 CellId = Queue[Head];
		for (const int32 NeighborId : Cells[CellId].NeighborCellIds)
		{
			if (!Cells.IsValidIndex(NeighborId)
				|| Distances[NeighborId] <= Distances[CellId] + 1)
			{
				continue;
			}
			Distances[NeighborId] = Distances[CellId] + 1;
			Queue.Add(NeighborId);
		}
	}
	return Distances;
}

int32 FindClosestCellBruteForce(
	const TArray<FABTSM2Cell>& Cells,
	const FVector& UnitDirection)
{
	const FVector SafeDirection = UnitDirection.GetSafeNormal();
	if (SafeDirection.IsNearlyZero())
	{
		return INDEX_NONE;
	}

	int32 BestCellId = INDEX_NONE;
	double BestDot = -2.0;
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		const double Dot = static_cast<double>(FVector::DotProduct(
			Cells[CellId].UnitCenter.GetSafeNormal(),
			SafeDirection));
		if (Dot > BestDot)
		{
			BestDot = Dot;
			BestCellId = CellId;
		}
	}
	return BestCellId;
}

bool HasReferenceTerrainLineOfSight(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3CellState>& CellStates,
	const FVector& Camera,
	const FVector& Target,
	const int32 TraceSamples)
{
	const int32 SafeSamples = FMath::Clamp(TraceSamples, 16, 128);
	for (int32 SampleIndex = 1; SampleIndex < SafeSamples; ++SampleIndex)
	{
		const float Alpha =
			static_cast<float>(SampleIndex) / static_cast<float>(SafeSamples);
		const FVector Point = FMath::Lerp(Camera, Target, Alpha);
		const float PointRadius = Point.Size();
		if (PointRadius <= UE_SMALL_NUMBER)
		{
			return false;
		}

		const int32 ClosestCellId =
			FindClosestCellBruteForce(Cells, Point / PointRadius);
		if (!CellStates.IsValidIndex(ClosestCellId))
		{
			return false;
		}
		const float SurfaceRadius = ReferencePlanetRadiusCM
			+ CellStates[ClosestCellId].LogicalHeight01
				* ReferenceTerrainHeightScaleCM;
		if (PointRadius < SurfaceRadius + 8.0f)
		{
			return false;
		}
	}
	return true;
}

bool EvaluateReferenceStartVisibility(
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3CellState>& CellStates,
	const FABTSM3TaskNode& StartTask,
	const FABTSM3TaskNode& TargetTask,
	const TArray<FABTSM3TaskLink>& Links,
	const FABTSM3PCGConfig& Config,
	const float OrbitDistanceCM)
{
	if (!Cells.IsValidIndex(StartTask.RoadPortalCellId)
		|| !Cells.IsValidIndex(TargetTask.BuildingAnchorCellId))
	{
		return false;
	}

	const FABTSM3TaskLink* const FirstMainLink =
		Links.FindByPredicate([&StartTask](const FABTSM3TaskLink& Link)
		{
			return Link.TaskA == StartTask.TaskId
				&& IsMainRouteLink(Link)
				&& Link.CorridorCells.Num() >= 2;
		});
	if (FirstMainLink == nullptr)
	{
		return false;
	}

	const FVector StartUp =
		Cells[StartTask.RoadPortalCellId].UnitCenter.GetSafeNormal();
	FVector RouteForward = FVector::ZeroVector;
	for (const int32 CorridorCellId : FirstMainLink->CorridorCells)
	{
		if (CorridorCellId == StartTask.RoadPortalCellId
			|| !Cells.IsValidIndex(CorridorCellId))
		{
			continue;
		}
		RouteForward = FVector::VectorPlaneProject(
			Cells[CorridorCellId].UnitCenter,
			StartUp).GetSafeNormal();
		if (!RouteForward.IsNearlyZero())
		{
			break;
		}
	}
	if (RouteForward.IsNearlyZero())
	{
		return false;
	}

	const float StartSurfaceRadius = ReferencePlanetRadiusCM
		+ CellStates[StartTask.RoadPortalCellId].LogicalHeight01
			* ReferenceTerrainHeightScaleCM;
	const FVector Pivot = StartUp * (
		StartSurfaceRadius
		+ FMath::Max(0.0f, Config.VisibilityCharacterCenterHeightCM)
		+ Config.VisibilityLookAtHeightCM);
	const float ElevationRadians = FMath::DegreesToRadians(
		FMath::Clamp(Config.VisibilityElevationDegrees, 20.0f, 85.0f));
	const FVector CameraOffset = (
		StartUp * FMath::Sin(ElevationRadians)
		- RouteForward * FMath::Cos(ElevationRadians)).GetSafeNormal();
	const FVector Camera =
		Pivot + CameraOffset * FMath::Max(300.0f, OrbitDistanceCM);

	const FVector TargetUp =
		Cells[TargetTask.BuildingAnchorCellId].UnitCenter.GetSafeNormal();
	const float TargetSurfaceRadius = ReferencePlanetRadiusCM
		+ CellStates[TargetTask.BuildingAnchorCellId].LogicalHeight01
			* ReferenceTerrainHeightScaleCM;
	const FVector Target = TargetUp * (
		TargetSurfaceRadius
		+ FMath::Max(100.0f, Config.VisibilityTargetHeightCM));
	return HasReferenceTerrainLineOfSight(
		Cells,
		CellStates,
		Camera,
		Target,
		Config.VisibilityTraceSamples);
}

bool GenerateM3WeekOneWorld(
	const int32 Seed,
	const FABTSM3PCGConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	FABTSM3WeekOneGeneratedWorld& OutWorld,
	const FABTSM3PCGGeometryContext& GeometryContext =
		FABTSM3PCGGeometryContext())
{
	const FABTSM3TaskGraphGenerator Generator;
	return Generator.Generate(
		Seed,
		Config,
		Cells,
		OutWorld.Tasks,
		OutWorld.Links,
		OutWorld.CellStates,
		OutWorld.EdgeStates,
		OutWorld.Summary,
		GeometryContext);
}

bool AreSummariesSemanticallyEqual(
	const FABTSM3PCGSummary& A,
	const FABTSM3PCGSummary& B)
{
	return A.GeneratorVersion == B.GeneratorVersion
		&& A.LayoutPolicyVersion == B.LayoutPolicyVersion
		&& A.ConfigHash == B.ConfigHash
		&& A.LayoutHash == B.LayoutHash
		&& A.AttemptIndex == B.AttemptIndex
		&& A.AssignedTaskCells == B.AssignedTaskCells
		&& A.RiverEdges == B.RiverEdges
		&& A.RoadEdges == B.RoadEdges
		&& A.BridgeEdge == B.BridgeEdge
		&& A.ShortcutEdge == B.ShortcutEdge
		&& A.bBridgeLockedBeforeBuild == B.bBridgeLockedBeforeBuild
		&& A.bMainPathReachableAfterBridge == B.bMainPathReachableAfterBridge
		&& A.SatelliteLaunchAngularSeparationDegrees == B.SatelliteLaunchAngularSeparationDegrees
		&& A.MainRouteLengthCM == B.MainRouteLengthCM
		&& A.MinAdjacentBuildingProgressCM == B.MinAdjacentBuildingProgressCM
		&& A.bWorkshopVisibleAtDefaultOrbit == B.bWorkshopVisibleAtDefaultOrbit
		&& A.bWorkshopVisibleAtMaxOrbit == B.bWorkshopVisibleAtMaxOrbit
		&& A.bTargetBuildingVisibleAtDefaultOrbit == B.bTargetBuildingVisibleAtDefaultOrbit
		&& A.bTargetBuildingVisibleAtMaxOrbit == B.bTargetBuildingVisibleAtMaxOrbit
		&& A.bFurnaceVisibleAtDefaultOrbit == B.bFurnaceVisibleAtDefaultOrbit
		&& A.bFurnaceVisibleAtMaxOrbit == B.bFurnaceVisibleAtMaxOrbit
		&& A.bAccepted == B.bAccepted;
}

bool AreTasksSemanticallyEqual(
	const FABTSM3TaskNode& A,
	const FABTSM3TaskNode& B)
{
	return A.TaskId == B.TaskId
		&& A.Type == B.Type
		&& A.SeedCellId == B.SeedCellId
		&& A.RoadPortalCellId == B.RoadPortalCellId
		&& A.BuildingAnchorCellId == B.BuildingAnchorCellId
		&& A.RouteProgressDistanceCM == B.RouteProgressDistanceCM
		&& A.FlowS == B.FlowS
		&& A.CellIds == B.CellIds
		&& A.LinkedTaskIds == B.LinkedTaskIds;
}

bool AreLinksSemanticallyEqual(
	const FABTSM3TaskLink& A,
	const FABTSM3TaskLink& B)
{
	return A.LinkId == B.LinkId
		&& A.TaskA == B.TaskA
		&& A.TaskB == B.TaskB
		&& A.Role == B.Role
		&& A.RequiredKey == B.RequiredKey
		&& A.CorridorCells == B.CorridorCells
		&& A.CorridorEdges == B.CorridorEdges
		&& A.CorridorLengthCM == B.CorridorLengthCM;
}

bool AreCellStatesSemanticallyEqual(
	const FABTSM3CellState& A,
	const FABTSM3CellState& B)
{
	return A.TaskId == B.TaskId
		&& A.TerrainType == B.TerrainType
		&& A.LogicalHeight01 == B.LogicalHeight01
		&& A.Moisture01 == B.Moisture01
		&& A.LogicalSlopeDegrees == B.LogicalSlopeDegrees
		&& A.RoadDistance == B.RoadDistance
		&& A.MainRoadDistance == B.MainRoadDistance
		&& A.ProgressDistance == B.ProgressDistance
		&& A.ProgressDistanceCM == B.ProgressDistanceCM
		&& A.FlowS == B.FlowS
		&& A.bRoad == B.bRoad
		&& A.bWater == B.bWater
		&& A.bBuildingAnchor == B.bBuildingAnchor
		&& A.bBuildingRoadExclusion == B.bBuildingRoadExclusion
		&& A.bBuildable == B.bBuildable;
}

bool AreEdgeStatesSemanticallyEqual(
	const FABTSM3CellEdgeState& A,
	const FABTSM3CellEdgeState& B)
{
	return A.Key == B.Key
		&& A.Transport == B.Transport
		&& A.Water == B.Water
		&& A.Crossing == B.Crossing
		&& A.RequiredKey == B.RequiredKey
		&& A.DownstreamCellId == B.DownstreamCellId
		&& A.FlowAccumulation == B.FlowAccumulation
		&& A.bBlocksOnFoot == B.bBlocksOnFoot;
}

void TestM3WeekOneWorldEquality(
	FAutomationTestBase& Test,
	const int32 Seed,
	const FABTSM3WeekOneGeneratedWorld& A,
	const FABTSM3WeekOneGeneratedWorld& B)
{
	Test.TestTrue(
		*FString::Printf(TEXT("Seed %d repeats the complete PCG summary"), Seed),
		AreSummariesSemanticallyEqual(A.Summary, B.Summary));

	Test.TestEqual(
		*FString::Printf(TEXT("Seed %d repeats the task count"), Seed),
		B.Tasks.Num(),
		A.Tasks.Num());
	for (int32 Index = 0; Index < FMath::Min(A.Tasks.Num(), B.Tasks.Num()); ++Index)
	{
		Test.TestTrue(
			*FString::Printf(TEXT("Seed %d repeats Task[%d] semantics"), Seed, Index),
			AreTasksSemanticallyEqual(A.Tasks[Index], B.Tasks[Index]));
	}

	Test.TestEqual(
		*FString::Printf(TEXT("Seed %d repeats the link count"), Seed),
		B.Links.Num(),
		A.Links.Num());
	for (int32 Index = 0; Index < FMath::Min(A.Links.Num(), B.Links.Num()); ++Index)
	{
		Test.TestTrue(
			*FString::Printf(TEXT("Seed %d repeats Link[%d] semantics"), Seed, Index),
			AreLinksSemanticallyEqual(A.Links[Index], B.Links[Index]));
	}

	Test.TestEqual(
		*FString::Printf(TEXT("Seed %d repeats the CellState count"), Seed),
		B.CellStates.Num(),
		A.CellStates.Num());
	for (int32 Index = 0; Index < FMath::Min(A.CellStates.Num(), B.CellStates.Num()); ++Index)
	{
		Test.TestTrue(
			*FString::Printf(TEXT("Seed %d repeats CellState[%d] semantics"), Seed, Index),
			AreCellStatesSemanticallyEqual(A.CellStates[Index], B.CellStates[Index]));
	}

	Test.TestEqual(
		*FString::Printf(TEXT("Seed %d repeats the edge-state count"), Seed),
		B.EdgeStates.Num(),
		A.EdgeStates.Num());
	for (int32 Index = 0; Index < FMath::Min(A.EdgeStates.Num(), B.EdgeStates.Num()); ++Index)
	{
		Test.TestTrue(
			*FString::Printf(TEXT("Seed %d repeats EdgeState[%d] semantics"), Seed, Index),
			AreEdgeStatesSemanticallyEqual(A.EdgeStates[Index], B.EdgeStates[Index]));
	}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3WeekOneSeedContractsTest,
	"ABTS.M3.WeekOne.SeedContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM3WeekOneSeedContractsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const TArray<FABTSM2Cell> Cells = BuildM3WeekOneLogicalCells();
	const FABTSM3PCGConfig Config;
	TArray<int32> Seeds;
	Seeds.Reserve(21);
	Seeds.Add(312503);
	for (int32 Seed = 0; Seed < 20; ++Seed)
	{
		Seeds.Add(Seed);
	}

	for (const int32 Seed : Seeds)
	{
		FABTSM3WeekOneGeneratedWorld World;
		const bool bGenerated = GenerateM3WeekOneWorld(Seed, Config, Cells, World);
		TestTrue(
			*FString::Printf(TEXT("Seed %d produces an accepted first-week world"), Seed),
			bGenerated);
		if (!bGenerated)
		{
			continue;
		}

		TestTrue(
			*FString::Printf(TEXT("Seed %d summary is accepted"), Seed),
			World.Summary.bAccepted);
		TestEqual(
			*FString::Printf(TEXT("Seed %d preserves the M11-compatible generator version"), Seed),
			World.Summary.GeneratorVersion,
			3);
		TestEqual(
			*FString::Printf(TEXT("Seed %d exposes the first-week layout policy version"), Seed),
			World.Summary.LayoutPolicyVersion,
			1);
		TestTrue(
			*FString::Printf(TEXT("Seed %d main route meets the configured minimum"), Seed),
			World.Summary.MainRouteLengthCM + KINDA_SMALL_NUMBER
				>= Config.MinMainRouteLengthCM);
		TestTrue(
			*FString::Printf(TEXT("Seed %d adjacent buildings meet the configured progress gap"), Seed),
			World.Summary.MinAdjacentBuildingProgressCM + KINDA_SMALL_NUMBER
				>= Config.MinAdjacentBuildingProgressCM);

		float ReferenceMainCorridorLengthCM = 0.0f;
		for (const FABTSM3TaskLink& Link : World.Links)
		{
			float ReferenceLinkLengthCM = 0.0f;
			const bool bReferenceLinkValid = TryComputeReferenceCorridorLengthCM(
				Cells,
				Link.CorridorCells,
				ReferenceLinkLengthCM);
			TestTrue(
				*FString::Printf(
					TEXT("Seed %d Link %d is an adjacent CellTopo corridor"),
					Seed,
					Link.LinkId),
				bReferenceLinkValid);
			if (bReferenceLinkValid)
			{
				TestTrue(
					*FString::Printf(
						TEXT("Seed %d Link %d records its independently reconstructed arc length"),
						Seed,
						Link.LinkId),
					FMath::IsNearlyEqual(
						ReferenceLinkLengthCM,
						Link.CorridorLengthCM,
						1.0f));
			}
			if (bReferenceLinkValid && IsMainRouteLink(Link))
			{
				ReferenceMainCorridorLengthCM += ReferenceLinkLengthCM;
			}
		}
		TestTrue(
			*FString::Printf(
				TEXT("Seed %d summary matches the independently reconstructed main-corridor length"),
				Seed),
			FMath::IsNearlyEqual(
				ReferenceMainCorridorLengthCM,
				World.Summary.MainRouteLengthCM,
				1.0f));

		const FABTSM3TaskNode* const Start =
			FindTaskByType(World.Tasks, EABTSM3TaskType::Start);
		const FABTSM3TaskNode* const Workshop =
			FindTaskByType(World.Tasks, EABTSM3TaskType::Workshop);
		const FABTSM3TaskNode* const Target =
			FindTaskByType(World.Tasks, EABTSM3TaskType::TargetBuilding);
		const FABTSM3TaskNode* const Furnace =
			FindTaskByType(World.Tasks, EABTSM3TaskType::FurnaceRuins);
		const FABTSM3TaskNode* const Launch =
			FindTaskByType(World.Tasks, EABTSM3TaskType::LaunchSite);
		const FABTSM3TaskNode* const Satellite =
			FindTaskByType(World.Tasks, EABTSM3TaskType::SatelliteWindow);
		TestNotNull(*FString::Printf(TEXT("Seed %d has Start"), Seed), Start);
		TestNotNull(*FString::Printf(TEXT("Seed %d has Workshop/B1"), Seed), Workshop);
		TestNotNull(*FString::Printf(TEXT("Seed %d has TargetBuilding/B2"), Seed), Target);
		TestNotNull(*FString::Printf(TEXT("Seed %d has FurnaceRuins/B3"), Seed), Furnace);
		TestNotNull(*FString::Printf(TEXT("Seed %d has LaunchSite"), Seed), Launch);
		TestNotNull(*FString::Printf(TEXT("Seed %d has SatelliteWindow"), Seed), Satellite);
		if (Start == nullptr
			|| Workshop == nullptr
			|| Target == nullptr
			|| Furnace == nullptr
			|| Launch == nullptr
			|| Satellite == nullptr)
		{
			continue;
		}

		TestTrue(
			*FString::Printf(TEXT("Seed %d starts at zero route progress"), Seed),
			FMath::IsNearlyZero(Start->RouteProgressDistanceCM, KINDA_SMALL_NUMBER));
		TestTrue(
			*FString::Printf(TEXT("Seed %d has ordered B1 < B2 < B3 progress"), Seed),
			Workshop->RouteProgressDistanceCM < Target->RouteProgressDistanceCM
				&& Target->RouteProgressDistanceCM < Furnace->RouteProgressDistanceCM);
		TestTrue(
			*FString::Printf(TEXT("Seed %d has a seam-only unique ordered main route"), Seed),
			HasUniqueOrderedMainRoute(
				World.Tasks,
				World.Links,
				Start->TaskId,
				Launch->TaskId));
		const float B1ToB2ProgressCM =
			Target->RouteProgressDistanceCM - Workshop->RouteProgressDistanceCM;
		const float B2ToB3ProgressCM =
			Furnace->RouteProgressDistanceCM - Target->RouteProgressDistanceCM;
		TestTrue(
			*FString::Printf(TEXT("Seed %d B1-to-B2 progress meets the configured gap"), Seed),
			B1ToB2ProgressCM + KINDA_SMALL_NUMBER
				>= Config.MinAdjacentBuildingProgressCM);
		TestTrue(
			*FString::Printf(TEXT("Seed %d B2-to-B3 progress meets the configured gap"), Seed),
			B2ToB3ProgressCM + KINDA_SMALL_NUMBER
				>= Config.MinAdjacentBuildingProgressCM);
		TestTrue(
			*FString::Printf(TEXT("Seed %d summary records the actual minimum building gap"), Seed),
			FMath::IsNearlyEqual(
				FMath::Min(B1ToB2ProgressCM, B2ToB3ProgressCM),
				World.Summary.MinAdjacentBuildingProgressCM,
				1.0f));
		TestTrue(
			*FString::Printf(TEXT("Seed %d Launch progress matches the main-route length"), Seed),
			FMath::IsNearlyEqual(
				Launch->RouteProgressDistanceCM,
				World.Summary.MainRouteLengthCM,
				1.0f));
		TestTrue(
			*FString::Printf(TEXT("Seed %d has ordered B1 < B2 < B3 FlowS"), Seed),
			Workshop->FlowS < Target->FlowS && Target->FlowS < Furnace->FlowS);

		struct FOrdinaryBuildingExpectation
		{
			const FABTSM3TaskNode* Task = nullptr;
			int32 MinimumMainRoadDistanceCells = 0;
			const TCHAR* Label = TEXT("");
		};
		const FOrdinaryBuildingExpectation OrdinaryBuildings[] = {
			{Workshop, Config.WorkshopMinMainRoadDistanceCells, TEXT("B1/Workshop")},
			{Target, Config.TargetBuildingMinMainRoadDistanceCells, TEXT("B2/TargetBuilding")},
			{Furnace, Config.FurnaceMinMainRoadDistanceCells, TEXT("B3/FurnaceRuins")}
		};
		const TArray<int32> ReferenceMainRouteHopDistances =
			BuildReferenceMainRouteHopDistances(Cells, World.Links);
		for (const FOrdinaryBuildingExpectation& Expected : OrdinaryBuildings)
		{
			const int32 AnchorCellId = Expected.Task->BuildingAnchorCellId;
			TestTrue(
				*FString::Printf(TEXT("Seed %d %s has a valid building anchor"), Seed, Expected.Label),
				World.CellStates.IsValidIndex(AnchorCellId));
			TestTrue(
				*FString::Printf(TEXT("Seed %d %s has a valid road portal"), Seed, Expected.Label),
				World.CellStates.IsValidIndex(Expected.Task->RoadPortalCellId));
			if (!World.CellStates.IsValidIndex(AnchorCellId))
			{
				continue;
			}

			const FABTSM3CellState& AnchorState = World.CellStates[AnchorCellId];
			TestTrue(
				*FString::Printf(TEXT("Seed %d %s task and CellState agree on the anchor"), Seed, Expected.Label),
				AnchorState.bBuildingAnchor
					&& AnchorState.TaskId == Expected.Task->TaskId);
			TestFalse(
				*FString::Printf(TEXT("Seed %d %s anchor is outside the road"), Seed, Expected.Label),
				AnchorState.bRoad);
			TestTrue(
				*FString::Printf(TEXT("Seed %d %s satisfies its main-road setback"), Seed, Expected.Label),
				ReferenceMainRouteHopDistances[AnchorCellId]
					>= Expected.MinimumMainRoadDistanceCells);
			TestEqual(
				*FString::Printf(
					TEXT("Seed %d %s records the independently reconstructed main-road distance"),
					Seed,
					Expected.Label),
				AnchorState.MainRoadDistance,
				ReferenceMainRouteHopDistances[AnchorCellId]);
			TestTrue(
				*FString::Printf(TEXT("Seed %d %s separates RoadPortal and BuildingAnchor"), Seed, Expected.Label),
				Expected.Task->RoadPortalCellId != AnchorCellId);
			if (World.CellStates.IsValidIndex(Expected.Task->RoadPortalCellId))
			{
				TestTrue(
					*FString::Printf(TEXT("Seed %d %s road portal lies on a road"), Seed, Expected.Label),
					World.CellStates[Expected.Task->RoadPortalCellId].bRoad);
			}
		}

		int32 OrdinaryAnchorCount = 0;
		int32 LaunchAnchorCount = 0;
		int32 SatelliteAnchorCount = 0;
		int32 RoadExclusionCellCount = 0;
		int32 RoadExclusionOverlapCount = 0;
		for (const FABTSM3CellState& State : World.CellStates)
		{
			if (State.bBuildingRoadExclusion)
			{
				++RoadExclusionCellCount;
				RoadExclusionOverlapCount += State.bRoad ? 1 : 0;
			}
			if (!State.bBuildingAnchor)
			{
				continue;
			}

			const FABTSM3TaskNode* const AnchorTask =
				FindTaskById(World.Tasks, State.TaskId);
			if (AnchorTask == nullptr)
			{
				continue;
			}
			OrdinaryAnchorCount += IsOrdinaryBuildingTask(AnchorTask->Type) ? 1 : 0;
			LaunchAnchorCount +=
				AnchorTask->Type == EABTSM3TaskType::LaunchSite ? 1 : 0;
			SatelliteAnchorCount +=
				AnchorTask->Type == EABTSM3TaskType::SatelliteWindow ? 1 : 0;
		}
		TestEqual(
			*FString::Printf(TEXT("Seed %d has exactly three ordinary building anchors"), Seed),
			OrdinaryAnchorCount,
			3);
		TestTrue(
			*FString::Printf(TEXT("Seed %d reserves ordinary building road-exclusion cells"), Seed),
			RoadExclusionCellCount > 0);
		TestEqual(
			*FString::Printf(TEXT("Seed %d keeps every building exclusion cell road-free"), Seed),
			RoadExclusionOverlapCount,
			0);
		TestEqual(
			*FString::Printf(TEXT("Seed %d has exactly one LaunchSite anchor"), Seed),
			LaunchAnchorCount,
			1);
		TestEqual(
			*FString::Printf(TEXT("Seed %d gives SatelliteWindow no building anchor"), Seed),
			SatelliteAnchorCount,
			0);
		TestTrue(
			*FString::Printf(TEXT("Seed %d LaunchSite task points at its unique anchor"), Seed),
			World.CellStates.IsValidIndex(Launch->BuildingAnchorCellId)
				&& World.CellStates[Launch->BuildingAnchorCellId].bBuildingAnchor
				&& World.CellStates[Launch->BuildingAnchorCellId].TaskId == Launch->TaskId);
		TestEqual(
			*FString::Printf(TEXT("Seed %d SatelliteWindow task has no anchor identity"), Seed),
			Satellite->BuildingAnchorCellId,
			INDEX_NONE);

		TestTrue(
			*FString::Printf(TEXT("Seed %d B1 is visible at the default orbit"), Seed),
			World.Summary.bWorkshopVisibleAtDefaultOrbit);
		TestTrue(
			*FString::Printf(TEXT("Seed %d B1 is visible at the maximum orbit"), Seed),
			World.Summary.bWorkshopVisibleAtMaxOrbit);
		TestFalse(
			*FString::Printf(TEXT("Seed %d B2 is hidden at the default orbit"), Seed),
			World.Summary.bTargetBuildingVisibleAtDefaultOrbit);
		TestFalse(
			*FString::Printf(TEXT("Seed %d B2 is hidden at the maximum orbit"), Seed),
			World.Summary.bTargetBuildingVisibleAtMaxOrbit);
		TestFalse(
			*FString::Printf(TEXT("Seed %d B3 is hidden at the default orbit"), Seed),
			World.Summary.bFurnaceVisibleAtDefaultOrbit);
		TestFalse(
			*FString::Printf(TEXT("Seed %d B3 is hidden at the maximum orbit"), Seed),
			World.Summary.bFurnaceVisibleAtMaxOrbit);
		if (Seed == 312503)
		{
			const bool bReferenceWorkshopDefault = EvaluateReferenceStartVisibility(
				Cells,
				World.CellStates,
				*Start,
				*Workshop,
				World.Links,
				Config,
				Config.VisibilityDefaultOrbitDistanceCM);
			const bool bReferenceWorkshopMax = EvaluateReferenceStartVisibility(
				Cells,
				World.CellStates,
				*Start,
				*Workshop,
				World.Links,
				Config,
				Config.VisibilityMaxOrbitDistanceCM);
			const bool bReferenceTargetDefault = EvaluateReferenceStartVisibility(
				Cells,
				World.CellStates,
				*Start,
				*Target,
				World.Links,
				Config,
				Config.VisibilityDefaultOrbitDistanceCM);
			const bool bReferenceTargetMax = EvaluateReferenceStartVisibility(
				Cells,
				World.CellStates,
				*Start,
				*Target,
				World.Links,
				Config,
				Config.VisibilityMaxOrbitDistanceCM);
			const bool bReferenceFurnaceDefault = EvaluateReferenceStartVisibility(
				Cells,
				World.CellStates,
				*Start,
				*Furnace,
				World.Links,
				Config,
				Config.VisibilityDefaultOrbitDistanceCM);
			const bool bReferenceFurnaceMax = EvaluateReferenceStartVisibility(
				Cells,
				World.CellStates,
				*Start,
				*Furnace,
				World.Links,
				Config,
				Config.VisibilityMaxOrbitDistanceCM);

			TestEqual(
				TEXT("Display seed B1 default visibility matches the brute-force reference"),
				World.Summary.bWorkshopVisibleAtDefaultOrbit,
				bReferenceWorkshopDefault);
			TestEqual(
				TEXT("Display seed B1 maximum visibility matches the brute-force reference"),
				World.Summary.bWorkshopVisibleAtMaxOrbit,
				bReferenceWorkshopMax);
			TestEqual(
				TEXT("Display seed B2 default visibility matches the brute-force reference"),
				World.Summary.bTargetBuildingVisibleAtDefaultOrbit,
				bReferenceTargetDefault);
			TestEqual(
				TEXT("Display seed B2 maximum visibility matches the brute-force reference"),
				World.Summary.bTargetBuildingVisibleAtMaxOrbit,
				bReferenceTargetMax);
			TestEqual(
				TEXT("Display seed B3 default visibility matches the brute-force reference"),
				World.Summary.bFurnaceVisibleAtDefaultOrbit,
				bReferenceFurnaceDefault);
			TestEqual(
				TEXT("Display seed B3 maximum visibility matches the brute-force reference"),
				World.Summary.bFurnaceVisibleAtMaxOrbit,
				bReferenceFurnaceMax);
			TestTrue(
				TEXT("Display seed brute-force reference keeps B1 visible at both orbit distances"),
				bReferenceWorkshopDefault && bReferenceWorkshopMax);
			TestTrue(
				TEXT("Display seed brute-force reference keeps B2/B3 hidden at both orbit distances"),
				!bReferenceTargetDefault
					&& !bReferenceTargetMax
					&& !bReferenceFurnaceDefault
					&& !bReferenceFurnaceMax);
		}

		TestTrue(
			*FString::Printf(TEXT("Seed %d satisfies the M11.0 satellite/finale separation"), Seed),
			World.Summary.SatelliteLaunchAngularSeparationDegrees
				+ KINDA_SMALL_NUMBER
				>= Config.MinSatelliteLaunchAngularSeparationDegrees);
		if (Cells.IsValidIndex(Launch->BuildingAnchorCellId)
			&& Cells.IsValidIndex(Satellite->SeedCellId))
		{
			const float ActualSeparationDegrees = FMath::RadiansToDegrees(
				FMath::Acos(FMath::Clamp(
					FVector::DotProduct(
						Cells[Launch->BuildingAnchorCellId].UnitCenter,
						Cells[Satellite->SeedCellId].UnitCenter),
					-1.0f,
					1.0f)));
			TestTrue(
				*FString::Printf(TEXT("Seed %d independently satisfies the M11.0 separation"), Seed),
				ActualSeparationDegrees + KINDA_SMALL_NUMBER
					>= Config.MinSatelliteLaunchAngularSeparationDegrees);
			TestTrue(
				*FString::Printf(TEXT("Seed %d summary records the actual M11.0 separation"), Seed),
				FMath::IsNearlyEqual(
					ActualSeparationDegrees,
					World.Summary.SatelliteLaunchAngularSeparationDegrees,
					1.0e-3f));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3WeekOneDeterminismTest,
	"ABTS.M3.WeekOne.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM3WeekOneDeterminismTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const TArray<FABTSM2Cell> Cells = BuildM3WeekOneLogicalCells();
	const FABTSM3PCGConfig Config;
	const int32 Seeds[] = {0, 7, 19, 312503};
	for (const int32 Seed : Seeds)
	{
		FABTSM3WeekOneGeneratedWorld First;
		FABTSM3WeekOneGeneratedWorld Second;
		const bool bFirstGenerated =
			GenerateM3WeekOneWorld(Seed, Config, Cells, First);
		const bool bSecondGenerated =
			GenerateM3WeekOneWorld(Seed, Config, Cells, Second);
		TestTrue(
			*FString::Printf(TEXT("Seed %d first deterministic generation succeeds"), Seed),
			bFirstGenerated);
		TestTrue(
			*FString::Printf(TEXT("Seed %d repeated deterministic generation succeeds"), Seed),
			bSecondGenerated);
		if (!bFirstGenerated || !bSecondGenerated)
		{
			continue;
		}

		TestM3WeekOneWorldEquality(*this, Seed, First, Second);
	}

	FABTSM3WeekOneGeneratedWorld Baseline;
	TestTrue(
		TEXT("Identity baseline generation succeeds"),
		GenerateM3WeekOneWorld(312503, Config, Cells, Baseline));

	FABTSM3PCGConfig ChangedConfig = Config;
	ChangedConfig.MinMainRouteLengthCM += 1.0f;
	FABTSM3WeekOneGeneratedWorld ChangedConfigWorld;
	GenerateM3WeekOneWorld(
		312503,
		ChangedConfig,
		Cells,
		ChangedConfigWorld);
	TestNotEqual(
		TEXT("ConfigHash changes when one policy input changes"),
		Baseline.Summary.ConfigHash,
		ChangedConfigWorld.Summary.ConfigHash);

	FABTSM3PCGGeometryContext ChangedGeometry;
	ChangedGeometry.RoadPadSafetyMarginCM += 1.0f;
	FABTSM3WeekOneGeneratedWorld ChangedGeometryWorld;
	GenerateM3WeekOneWorld(
		312503,
		Config,
		Cells,
		ChangedGeometryWorld,
		ChangedGeometry);
	TestNotEqual(
		TEXT("ConfigHash changes when one runtime-geometry input changes"),
		Baseline.Summary.ConfigHash,
		ChangedGeometryWorld.Summary.ConfigHash);

	TArray<FABTSM2Cell> DifferentTopology = Cells;
	DifferentTopology[0].bIsPentagon =
		!DifferentTopology[0].bIsPentagon;
	FABTSM3WeekOneGeneratedWorld DifferentTopologyWorld;
	TestTrue(
		TEXT("CellTopo identity mutation still produces a valid comparison world"),
		GenerateM3WeekOneWorld(
			312503,
			Config,
			DifferentTopology,
			DifferentTopologyWorld));
	TestNotEqual(
		TEXT("ConfigHash changes when one CellTopo input changes"),
		Baseline.Summary.ConfigHash,
		DifferentTopologyWorld.Summary.ConfigHash);
	return true;
}

#endif
