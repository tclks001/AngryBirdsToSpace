// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ABTSRuntime.h"
#include "Algo/Sort.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "PCG/ABTSM3MonthlyRoute.h"
#include "PCG/ABTSM3R1AcceptanceManifest.h"
#include "PCG/ABTSM3R2AcceptanceManifest.h"
#include "PCG/ABTSM3TaskGraphGenerator.h"
#include "Planet/ABTSM2Planet.h"

namespace ABTSM3R2RouteTests
{
constexpr float ReferencePlanetRadiusCM = 10000.0f;
constexpr int32 DisplaySeed = 312503;

TArray<FABTSM2Cell> BuildLogicalCells(const int32 Subdivision = 5)
{
	AABTSM2Planet::FUnitSphereMesh Mesh;
	AABTSM2Planet::BuildUnitIcosphere(Subdivision, Mesh);
	TArray<FABTSM2Cell> Cells;
	Cells.SetNum(Mesh.Vertices.Num());
	for (int32 CellId = 0; CellId < Mesh.Vertices.Num(); ++CellId)
	{
		Cells[CellId].UnitCenter = Mesh.Vertices[CellId];
	}
	const auto AddNeighbor = [&Cells](
		const int32 CellA,
		const int32 CellB)
	{
		Cells[CellA].NeighborCellIds.AddUnique(CellB);
		Cells[CellB].NeighborCellIds.AddUnique(CellA);
	};
	for (const FIntVector& Triangle : Mesh.Triangles)
	{
		AddNeighbor(Triangle.X, Triangle.Y);
		AddNeighbor(Triangle.Y, Triangle.Z);
		AddNeighbor(Triangle.Z, Triangle.X);
	}
	for (FABTSM2Cell& Cell : Cells)
	{
		Cell.NeighborCellIds.Sort();
		Cell.bIsPentagon = Cell.NeighborCellIds.Num() == 5;
	}
	return Cells;
}

const TArray<FABTSM2Cell>& GetLogicalCells()
{
	static const TArray<FABTSM2Cell> Cells = BuildLogicalCells();
	return Cells;
}

FABTSM3MonthlyRouteConfig MakeQuietConfig()
{
	FABTSM3MonthlyRouteConfig Config;
	Config.bEmitRouteLogs = false;
	return Config;
}

bool BuildRoutePool(
	const int32 Seed,
	FABTSM3MonthlyRoutePool& OutPool,
	FString& OutFailure,
	const FABTSM3MonthlyRoadContext& Context =
		FABTSM3MonthlyRoadContext(),
	const FABTSM3MonthlyRouteConfig& Config = MakeQuietConfig(),
	const TArray<FABTSM2Cell>* OverrideCells = nullptr)
{
	const TArray<FABTSM2Cell>& Cells =
		OverrideCells != nullptr ? *OverrideCells : GetLogicalCells();
	return FABTSM3MonthlyRouteBuilder::Build(
		Seed,
		Config,
		Cells,
		ReferencePlanetRadiusCM,
		Context,
		OutPool,
		OutFailure);
}

bool RoutePoolEqual(
	const FABTSM3MonthlyRoutePool& A,
	const FABTSM3MonthlyRoutePool& B)
{
	return FABTSM3MonthlyRoutePool::StaticStruct()
		->CompareScriptStruct(&A, &B, PPF_None);
}

bool ValidateCandidateTopology(
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlyRouteConfig& Config,
	const FABTSM3MonthlyRouteCandidate& Candidate)
{
	if (!Candidate.bHardPass
		|| Candidate.OrderedRoadCellIds.Num() < 2
		|| Candidate.ProgressDistanceCM.Num()
			!= Candidate.OrderedRoadCellIds.Num()
		|| Candidate.BeatPoints.Num() != Config.RouteBeatPointCount)
	{
		return false;
	}
	TSet<int32> UniqueCells;
	for (int32 Index = 0;
		Index < Candidate.OrderedRoadCellIds.Num();
		++Index)
	{
		const int32 CellId = Candidate.OrderedRoadCellIds[Index];
		if (!Cells.IsValidIndex(CellId)
			|| UniqueCells.Contains(CellId))
		{
			return false;
		}
		UniqueCells.Add(CellId);
		if (Index > 0
			&& (!Cells[Candidate.OrderedRoadCellIds[Index - 1]]
					.NeighborCellIds.Contains(CellId)
				|| Candidate.ProgressDistanceCM[Index]
					<= Candidate.ProgressDistanceCM[Index - 1]))
		{
			return false;
		}
	}
	return Candidate.Metrics.RouteLengthCM
			>= Config.Acceptance.MinRouteLengthCM
		&& Candidate.Metrics.RouteLengthCM
			<= Config.Acceptance.MaxRouteLengthCM
		&& Candidate.Metrics.ScenicBendCount
			>= Config.Acceptance.MinScenicBendCount
		&& Candidate.Metrics.MaxStraightCM
			<= Config.Acceptance.MaxStraightCM
		&& Candidate.Metrics.MinSelfApproachCells
			>= Config.Acceptance.MinSelfApproachCells
		&& Candidate.BeatPoints[0].FlowQ == 0
		&& Candidate.BeatPoints.Last().FlowQ
			== FABTSM3MonthlyRouteBuilder::FlowQuantization;
}

uint64 MixManifestHash(uint64 Hash, const uint64 Value)
{
	for (int32 Shift = 0; Shift < 64; Shift += 8)
	{
		Hash ^= static_cast<uint8>((Value >> Shift) & 0xffull);
		Hash *= 1099511628211ull;
	}
	return Hash;
}

TArray<int32> BuildSweepSeeds()
{
	return TArray<int32>(
		FABTSM3R2AcceptanceManifest::GetSweepSeeds());
}

struct FGeneratedCompatibilityWorld
{
	TArray<FABTSM3TaskNode> Tasks;
	TArray<FABTSM3TaskLink> Links;
	TArray<FABTSM3CellState> CellStates;
	TArray<FABTSM3CellEdgeState> EdgeStates;
	FABTSM3PCGSummary Summary;
};

bool GenerateCompatibilityWorld(
	const int32 Seed,
	FGeneratedCompatibilityWorld& OutWorld)
{
	const FABTSM3TaskGraphGenerator Generator;
	return Generator.Generate(
		Seed,
		FABTSM3PCGConfig(),
		GetLogicalCells(),
		OutWorld.Tasks,
		OutWorld.Links,
		OutWorld.CellStates,
		OutWorld.EdgeStates,
		OutWorld.Summary);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R2RouteDefaultsAndHashDomainsTest,
	"ABTS.M3.Monthly.RouteCore.01DefaultsAndHashDomains",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R2RouteDefaultsAndHashDomainsTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R2RouteTests;
	const TArray<FABTSM2Cell>& Cells = GetLogicalCells();
	const FABTSM3MonthlyRouteConfig DefaultConfig;
	FString ManifestFailure;
	const bool bManifestValid =
		FABTSM3R2AcceptanceManifest::Validate(ManifestFailure);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R2][AcceptanceManifest] SelfValid=%d Failure=%s SeedHash=%016llX ProfileHash=%016llX ManifestHash=%016llX"),
		bManifestValid ? 1 : 0,
		*ManifestFailure,
		static_cast<unsigned long long>(
			FABTSM3R2AcceptanceManifest::
				ComputeSweepSeedManifestHash()),
		static_cast<unsigned long long>(
			FABTSM3R2AcceptanceManifest::
				ComputeAcceptanceProfileHash()),
		static_cast<unsigned long long>(
			FABTSM3R2AcceptanceManifest::ComputeManifestHash()));
	TestTrue(TEXT("Frozen acceptance manifest"), bManifestValid);
	TestEqual(TEXT("Frozen seed manifest hash"),
		FABTSM3R2AcceptanceManifest::
			ComputeSweepSeedManifestHash(),
		FABTSM3R2AcceptanceManifest::
			FrozenSweepSeedManifestHash);
	TestEqual(TEXT("Frozen acceptance profile hash"),
		FABTSM3R2AcceptanceManifest::
			ComputeAcceptanceProfileHash(),
		FABTSM3R2AcceptanceManifest::
			FrozenAcceptanceProfileHash);
	TestEqual(TEXT("Normal candidate slots"),
		DefaultConfig.NormalCandidateSlots, 8);
	TestEqual(TEXT("Retained candidate cap"),
		DefaultConfig.MaxRetainedCandidates, 3);
	TestEqual(TEXT("Minimum route length"),
		DefaultConfig.Acceptance.MinRouteLengthCM, 28000);
	TestEqual(TEXT("Target route length"),
		DefaultConfig.Acceptance.TargetRouteLengthCM, 32000);
	TestEqual(TEXT("Maximum route length"),
		DefaultConfig.Acceptance.MaxRouteLengthCM, 36000);
	TestEqual(TEXT("Minimum scenic bends"),
		DefaultConfig.Acceptance.MinScenicBendCount, 3);
	TestEqual(TEXT("Maximum straight run"),
		DefaultConfig.Acceptance.MaxStraightCM, 5500);
	TestEqual(TEXT("Minimum self approach"),
		DefaultConfig.Acceptance.MinSelfApproachCells, 4);

	const uint64 TopologyHash =
		FABTSM3MonthlyRouteBuilder::ComputeTopologyHash(Cells);
	const uint64 ConfigHash =
		FABTSM3MonthlyRouteBuilder::ComputeConfigHash(
			DefaultConfig,
			ReferencePlanetRadiusCM,
			TopologyHash);
	FABTSM3MonthlyRouteConfig LoggingMutation = DefaultConfig;
	LoggingMutation.bEmitRouteLogs =
		!LoggingMutation.bEmitRouteLogs;
	TestEqual(TEXT("Logging excluded from config hash"),
		FABTSM3MonthlyRouteBuilder::ComputeConfigHash(
			LoggingMutation,
			ReferencePlanetRadiusCM,
			TopologyHash),
		ConfigHash);
	FABTSM3MonthlyRouteConfig CostMutation = DefaultConfig;
	++CostMutation.Costs.CorridorRing3Penalty;
	TestNotEqual(TEXT("Cost mutation changes config hash"),
		FABTSM3MonthlyRouteBuilder::ComputeConfigHash(
			CostMutation,
			ReferencePlanetRadiusCM,
			TopologyHash),
		ConfigHash);
	FABTSM3MonthlyRouteConfig ProfileMutation = DefaultConfig;
	++ProfileMutation.Acceptance.MaxStraightCM;
	TestNotEqual(TEXT("Acceptance mutation changes config hash"),
		FABTSM3MonthlyRouteBuilder::ComputeConfigHash(
			ProfileMutation,
			ReferencePlanetRadiusCM,
			TopologyHash),
		ConfigHash);

	FABTSM3MonthlyRouteConfig Disabled = MakeQuietConfig();
	Disabled.bBuildRouteObservation = false;
	FABTSM3MonthlyRoutePool DisabledPool;
	FString Failure;
	TestTrue(TEXT("Disabled observation builds empty result"),
		BuildRoutePool(
			DisplaySeed,
			DisabledPool,
			Failure,
			FABTSM3MonthlyRoadContext(),
			Disabled));
	TestTrue(TEXT("Disabled result remains route-pool valid"),
		DisabledPool.bRoutePoolValid);
	TestEqual(TEXT("Disabled result is not evaluated"),
		DisabledPool.RejectReason,
		EABTSM3MonthlyRouteRejectReason::NotEvaluated);
	TestEqual(TEXT("Disabled result has no candidates"),
		DisabledPool.RetainedCandidates.Num(), 0);

	FABTSM3MonthlyRouteConfig Invalid = MakeQuietConfig();
	Invalid.NormalCandidateSlots = 3;
	FABTSM3MonthlyRoutePool InvalidPool;
	TestFalse(TEXT("Invalid domain fails closed"),
		BuildRoutePool(
			DisplaySeed,
			InvalidPool,
			Failure,
			FABTSM3MonthlyRoadContext(),
			Invalid));
	TestEqual(TEXT("Invalid domain reason"),
		InvalidPool.RejectReason,
		EABTSM3MonthlyRouteRejectReason::InvalidConfig);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R2RouteGeometryAndRotationTest,
	"ABTS.M3.Monthly.RouteCore.02GeometryAndRotation",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R2RouteGeometryAndRotationTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R2RouteTests;
	const FABTSM3MonthlyRouteConfig Config = MakeQuietConfig();
	FABTSM3MonthlyRoutePool Pool;
	FString Failure;
	TestTrue(TEXT("Display route builds"),
		BuildRoutePool(DisplaySeed, Pool, Failure));
	TestTrue(TEXT("Display route uses normal candidates"),
		!Pool.bUsedRouteFallback);
	TestEqual(TEXT("Display route retains top three"),
		Pool.RetainedCandidates.Num(), 3);
	TestEqual(TEXT("Display pool identity"),
		static_cast<uint64>(Pool.RouteCandidatePoolHash),
		FABTSM3R2AcceptanceManifest::FrozenDisplayPoolHash);
	TestEqual(TEXT("Display snapshot identity"),
		FABTSM3MonthlyRouteBuilder::ComputePoolSnapshotHash(
			Pool),
		FABTSM3R2AcceptanceManifest::
			FrozenDisplaySnapshotHash);
	if (!Pool.RetainedCandidates.IsEmpty())
	{
		const FABTSM3MonthlyRouteCandidate& Best =
			Pool.RetainedCandidates[0];
		TestEqual(TEXT("Display best route length"),
			Best.Metrics.RouteLengthCM,
			FABTSM3R2AcceptanceManifest::
				DisplayBestRouteLengthCM);
		TestEqual(TEXT("Display best scenic bends"),
			Best.Metrics.ScenicBendCount,
			FABTSM3R2AcceptanceManifest::
				DisplayBestScenicBendCount);
		TestEqual(TEXT("Display best maximum straight"),
			Best.Metrics.MaxStraightCM,
			FABTSM3R2AcceptanceManifest::
				DisplayBestMaxStraightCM);
		TestEqual(TEXT("Display best self approach"),
			Best.Metrics.MinSelfApproachCells,
			FABTSM3R2AcceptanceManifest::
				DisplayBestSelfApproachCells);
		TestEqual(TEXT("Display best score"),
			Best.RouteScore,
			FABTSM3R2AcceptanceManifest::DisplayBestScore);
	}
	for (const FABTSM3MonthlyRouteCandidate& Candidate :
		Pool.RetainedCandidates)
	{
		TestTrue(
			FString::Printf(
				TEXT("Candidate %d topology and gates"),
				Candidate.CandidateId),
			ValidateCandidateTopology(
				GetLogicalCells(),
				Config,
				Candidate));
	}

	TArray<FABTSM2Cell> RotatedCells = GetLogicalCells();
	const FQuat Rotation(
		FVector(0.37, -0.51, 0.78).GetSafeNormal(),
		1.137);
	for (FABTSM2Cell& Cell : RotatedCells)
	{
		Cell.UnitCenter =
			Rotation.RotateVector(Cell.UnitCenter).GetSafeNormal();
	}
	FABTSM3MonthlyRoutePool RotatedPool;
	TestTrue(TEXT("Rotated topology route builds"),
		BuildRoutePool(
			DisplaySeed,
			RotatedPool,
			Failure,
			FABTSM3MonthlyRoadContext(),
			Config,
			&RotatedCells));
	TestEqual(TEXT("Rotation preserves retained count"),
		RotatedPool.RetainedCandidates.Num(),
		Pool.RetainedCandidates.Num());
	for (int32 CandidateIndex = 0;
		CandidateIndex < Pool.RetainedCandidates.Num();
		++CandidateIndex)
	{
		TestEqual(TEXT("Rotation preserves candidate hash"),
			RotatedPool.RetainedCandidates[CandidateIndex].
				CandidateHash,
			Pool.RetainedCandidates[CandidateIndex].CandidateHash);
	}
	TestNotEqual(TEXT("Rotation changes topology identity"),
		RotatedPool.TopologyHash,
		Pool.TopologyHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R2RouteContextAndCorridorTest,
	"ABTS.M3.Monthly.RouteCore.03ContextAndCorridor",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R2RouteContextAndCorridorTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R2RouteTests;
	FABTSM3MonthlyRoutePool NeutralPool;
	FString Failure;
	TestTrue(TEXT("Neutral pool builds"),
		BuildRoutePool(DisplaySeed, NeutralPool, Failure));
	if (NeutralPool.RetainedCandidates.IsEmpty())
	{
		AddError(TEXT("Neutral pool has no retained candidate."));
		return false;
	}
	const FABTSM3MonthlyRouteCandidate& Best =
		NeutralPool.RetainedCandidates[0];
	const int32 MidCell =
		Best.OrderedRoadCellIds[Best.OrderedRoadCellIds.Num() / 2];
	FABTSM3MonthlyRoadContext Context;
	Context.Cells.SetNum(GetLogicalCells().Num());
	Context.Cells[MidCell].bHardBlocked = true;
	FABTSM3MonthlyRoutePool BlockedPool;
	TestTrue(TEXT("Single hard block still finds a route pool"),
		BuildRoutePool(
			DisplaySeed,
			BlockedPool,
			Failure,
			Context));
	for (const FABTSM3MonthlyRouteCandidate& Candidate :
		BlockedPool.RetainedCandidates)
	{
		TestFalse(TEXT("Hard-blocked cell is never crossed"),
			Candidate.OrderedRoadCellIds.Contains(MidCell));
	}
	TestNotEqual(TEXT("Context identity changes"),
		BlockedPool.RoadContextHash,
		NeutralPool.RoadContextHash);
	TestNotEqual(TEXT("Context changes pool identity"),
		BlockedPool.RouteCandidatePoolHash,
		NeutralPool.RouteCandidatePoolHash);

	FABTSM3MonthlyRoadContext IllegalStartContext;
	IllegalStartContext.Cells.SetNum(GetLogicalCells().Num());
	const int32 StartCellId = Best.ControlCellIds[0];
	IllegalStartContext.Cells[StartCellId].bWater = true;
	IllegalStartContext.Cells[StartCellId].
		bLegalWaterCrossing = false;
	FABTSM3MonthlyRoutePool IllegalStartPool;
	TestTrue(TEXT("Illegal normal start falls back deterministically"),
		BuildRoutePool(
			DisplaySeed,
			IllegalStartPool,
			Failure,
			IllegalStartContext));
	TestTrue(TEXT("Illegal start cannot pass a normal route"),
		IllegalStartPool.bUsedRouteFallback
			&& IllegalStartPool.NormalHardPassCount == 0);
	for (int32 ReportIndex = 0;
		ReportIndex
			< FABTSM3MonthlyRouteConfig().
				NormalCandidateSlots;
		++ReportIndex)
	{
		TestEqual(TEXT("Illegal start reject reason"),
			IllegalStartPool.AttemptReports[ReportIndex].
				RejectReason,
			EABTSM3MonthlyRouteRejectReason::HardBlocked);
	}

	FABTSM3MonthlyRoadContext InvalidCostContext;
	InvalidCostContext.Cells.SetNum(GetLogicalCells().Num());
	InvalidCostContext.Cells[StartCellId].TerrainCost = -1;
	FABTSM3MonthlyRoutePool InvalidCostPool;
	TestFalse(TEXT("Negative terrain cost fails closed"),
		BuildRoutePool(
			DisplaySeed,
			InvalidCostPool,
			Failure,
			InvalidCostContext));
	TestEqual(TEXT("Negative context reject reason"),
		InvalidCostPool.RejectReason,
		EABTSM3MonthlyRouteRejectReason::InvalidContext);

	FABTSM3MonthlyRoadContext SoftContext;
	SoftContext.Cells.SetNum(GetLogicalCells().Num());
	for (int32 Index = 1;
		Index + 1 < Best.OrderedRoadCellIds.Num();
		Index += 7)
	{
		FABTSM3MonthlyRouteCellContext& Cell =
			SoftContext.Cells[Best.OrderedRoadCellIds[Index]];
		Cell.bWater = true;
		Cell.bLegalWaterCrossing = true;
		Cell.bSoftEncounterReserved = true;
		Cell.TerrainCost = 150;
		Cell.SlopeCost = 75;
		Cell.ReuseBias = -500;
	}
	FABTSM3MonthlyRoutePool SoftPool;
	TestTrue(TEXT("Soft context remains solvable"),
		BuildRoutePool(
			DisplaySeed,
			SoftPool,
			Failure,
			SoftContext));
	TestTrue(TEXT("Soft context never publishes a world"),
		!SoftPool.bMonthlyWorldAccepted);
	EABTSM3MonthlyRouteRejectReason ValidationReason =
		EABTSM3MonthlyRouteRejectReason::None;
	TestFalse(TEXT("Context mismatch is rejected"),
		FABTSM3MonthlyRouteBuilder::Validate(
			MakeQuietConfig(),
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			FABTSM3MonthlyRoadContext(),
			SoftPool,
			ValidationReason,
			Failure));
	TestEqual(TEXT("Context mismatch reason"),
		ValidationReason,
		EABTSM3MonthlyRouteRejectReason::HashMismatch);

	FABTSM3MonthlyRoadContext ReuseContext;
	ReuseContext.Cells.SetNum(GetLogicalCells().Num());
	for (const int32 RouteCellId : Best.OrderedRoadCellIds)
	{
		ReuseContext.Cells[RouteCellId].ReuseBias = -200;
	}
	FABTSM3MonthlyRoutePool ReusePool;
	TestTrue(TEXT("Capped reuse context remains solvable"),
		BuildRoutePool(
			DisplaySeed,
			ReusePool,
			Failure,
			ReuseContext));
	bool bObservedReuseReward = false;
	const FABTSM3MonthlyRouteConfig ReuseConfig =
		MakeQuietConfig();
	for (const FABTSM3MonthlyRouteCandidate& Candidate :
		ReusePool.RetainedCandidates)
	{
		TestTrue(TEXT("Candidate reuse reward obeys total cap"),
			Candidate.Metrics.AppliedReuseBonus
				<= ReuseConfig.Costs.
					MaxReuseBonusPerCandidate);
		bObservedReuseReward |=
			Candidate.Metrics.AppliedReuseBonus > 0;
	}
	TestTrue(TEXT("Reuse reward participates in road search"),
		bObservedReuseReward);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R2RouteDeepDeterminismTest,
	"ABTS.M3.Monthly.RouteCore.04DeepDeterminism",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R2RouteDeepDeterminismTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R2RouteTests;
	FABTSM3MonthlyRoutePool First;
	FABTSM3MonthlyRoutePool Second;
	FString Failure;
	TestTrue(TEXT("First deterministic build"),
		BuildRoutePool(DisplaySeed, First, Failure));
	TestTrue(TEXT("Second deterministic build"),
		BuildRoutePool(DisplaySeed, Second, Failure));
	TestTrue(TEXT("Full pool deep equality"),
		RoutePoolEqual(First, Second));
	TestEqual(TEXT("Pool hash deterministic"),
		First.RouteCandidatePoolHash,
		Second.RouteCandidatePoolHash);
	TestEqual(TEXT("Snapshot hash deterministic"),
		FABTSM3MonthlyRouteBuilder::ComputePoolSnapshotHash(First),
		FABTSM3MonthlyRouteBuilder::ComputePoolSnapshotHash(Second));

	const FABTSM3MonthlyRouteConfig Config = MakeQuietConfig();
	EABTSM3MonthlyRouteRejectReason Reason =
		EABTSM3MonthlyRouteRejectReason::None;
	TestTrue(TEXT("Pool validator accepts canonical result"),
		FABTSM3MonthlyRouteBuilder::Validate(
			Config,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			FABTSM3MonthlyRoadContext(),
			First,
			Reason,
			Failure));
	FABTSM3MonthlyRoutePool CorridorMutation = First;
	if (!CorridorMutation.RetainedCandidates.IsEmpty())
	{
		FABTSM3MonthlyRouteCandidate& Mutated =
			CorridorMutation.RetainedCandidates[0];
		Mutated.CorridorCellIds.RemoveSingle(
			Mutated.OrderedRoadCellIds[
				Mutated.OrderedRoadCellIds.Num() / 2]);
		Mutated.CandidateHash = static_cast<int64>(
			FABTSM3MonthlyRouteBuilder::
				ComputeCandidateHash(Mutated));
		CorridorMutation.RouteCandidatePoolHash =
			static_cast<int64>(
				FABTSM3MonthlyRouteBuilder::
					ComputePoolHash(CorridorMutation));
		TestFalse(TEXT("Corridor omission fails semantic validation"),
			FABTSM3MonthlyRouteBuilder::Validate(
				Config,
				GetLogicalCells(),
				ReferencePlanetRadiusCM,
				FABTSM3MonthlyRoadContext(),
				CorridorMutation,
				Reason,
				Failure));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R2RouteHashMutationTest,
	"ABTS.M3.Monthly.RouteCore.05HashMutation",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R2RouteHashMutationTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R2RouteTests;
	FABTSM3MonthlyRoutePool Pool;
	FString Failure;
	TestTrue(TEXT("Hash fixture builds"),
		BuildRoutePool(DisplaySeed, Pool, Failure));
	if (Pool.RetainedCandidates.IsEmpty())
	{
		AddError(TEXT("Hash fixture has no retained candidate."));
		return false;
	}
	FABTSM3MonthlyRouteCandidate Mutated =
		Pool.RetainedCandidates[0];
	const uint64 FrozenCandidateHash =
		FABTSM3MonthlyRouteBuilder::ComputeCandidateHash(Mutated);
	++Mutated.Metrics.MaxStraightCM;
	TestNotEqual(TEXT("Metric mutation changes candidate hash"),
		FABTSM3MonthlyRouteBuilder::ComputeCandidateHash(Mutated),
		FrozenCandidateHash);
	Mutated = Pool.RetainedCandidates[0];
	Swap(
		Mutated.CorridorCellIds[0],
		Mutated.CorridorCellIds[1]);
	TestNotEqual(TEXT("Ordered corridor mutation changes hash"),
		FABTSM3MonthlyRouteBuilder::ComputeCandidateHash(Mutated),
		FrozenCandidateHash);

	FABTSM3MonthlyRoutePool MutatedPool = Pool;
	const uint64 FrozenPoolHash =
		FABTSM3MonthlyRouteBuilder::ComputePoolHash(MutatedPool);
	MutatedPool.AttemptReports[0].ExpandedStates++;
	TestNotEqual(TEXT("Attempt report mutation changes pool hash"),
		FABTSM3MonthlyRouteBuilder::ComputePoolHash(MutatedPool),
		FrozenPoolHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R2RouteCompatibilityBoundaryTest,
	"ABTS.M3.Monthly.RouteCore.06CompatibilityBoundary",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R2RouteCompatibilityBoundaryTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R2RouteTests;
	const TConstArrayView<FABTSM3R1CompatibilityOracle> Oracles =
		FABTSM3R1AcceptanceManifest::GetCompatibilityOracles();
	int32 Passed = 0;
	for (const FABTSM3R1CompatibilityOracle& Oracle : Oracles)
	{
		FGeneratedCompatibilityWorld World;
		if (!GenerateCompatibilityWorld(Oracle.Seed, World))
		{
			AddError(FString::Printf(
				TEXT("Compatibility generation failed for Seed=%d"),
				Oracle.Seed));
			continue;
		}
		const uint64 BeforeSnapshot =
			FABTSM3R1AcceptanceManifest::
				ComputeCompatibilitySnapshotHash(
					World.Tasks,
					World.Links,
					World.CellStates,
					World.EdgeStates,
					World.Summary);
		FABTSM3MonthlyRoutePool RoutePool;
		FString Failure;
		const bool bRouteBuilt =
			BuildRoutePool(Oracle.Seed, RoutePool, Failure);
		const uint64 AfterSnapshot =
			FABTSM3R1AcceptanceManifest::
				ComputeCompatibilitySnapshotHash(
					World.Tasks,
					World.Links,
					World.CellStates,
					World.EdgeStates,
					World.Summary);
		const bool bPassed = bRouteBuilt
			&& BeforeSnapshot == Oracle.SnapshotHash
			&& AfterSnapshot == Oracle.SnapshotHash
			&& !RoutePool.bMonthlyWorldAccepted;
		if (!bPassed)
		{
			AddError(FString::Printf(
				TEXT("Compatibility boundary failed Seed=%d Failure=%s"),
				Oracle.Seed,
				*Failure));
			continue;
		}
		++Passed;
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R2][CompatibilityOracle] Terminal=%d Passed=%d Failed=%d"),
		Oracles.Num(),
		Passed,
		Oracles.Num() - Passed);
	TestEqual(TEXT("All compatibility oracles remain unchanged"),
		Passed, Oracles.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R2RouteSweep200Test,
	"ABTS.M3.Monthly.RouteCore.07RouteSweep200",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R2RouteSweep200Test::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R2RouteTests;
	const TArray<int32> Seeds = BuildSweepSeeds();
	FABTSM3MonthlyRoutePool WarmupPool;
	FString WarmupFailure;
	TestTrue(TEXT("Route timing warm-up"),
		BuildRoutePool(DisplaySeed, WarmupPool, WarmupFailure));
	TArray<double> DurationsMS;
	DurationsMS.Reserve(Seeds.Num());
	int32 NormalAccepted = 0;
	int32 RouteFallback = 0;
	int32 Rejected = 0;
	int32 MaxExpanded = 0;
	int32 MaxRelaxations = 0;
	int32 MaxBacktracks = 0;
	uint64 OracleHash = 14695981039346656037ull;
	for (const int32 Seed : Seeds)
	{
		FABTSM3MonthlyRoutePool First;
		FString Failure;
		const double StartSeconds = FPlatformTime::Seconds();
		const bool bBuilt = BuildRoutePool(Seed, First, Failure);
		DurationsMS.Add(
			(FPlatformTime::Seconds() - StartSeconds) * 1000.0);
		FABTSM3MonthlyRoutePool Second;
		const bool bRepeated =
			BuildRoutePool(Seed, Second, Failure);
		if (!bBuilt
			|| !bRepeated
			|| !RoutePoolEqual(First, Second)
			|| First.bUsedRouteFallback
			|| First.NormalHardPassCount <= 0
			|| First.RetainedCandidates.IsEmpty())
		{
			++Rejected;
			if (First.bUsedRouteFallback)
			{
				++RouteFallback;
			}
			AddError(FString::Printf(
				TEXT("Route sweep rejected Seed=%d Reason=%s Failure=%s"),
				Seed,
				FABTSM3MonthlyRouteBuilder::GetRejectReasonName(
					First.RejectReason),
				*Failure));
			continue;
		}
		++NormalAccepted;
		OracleHash = MixManifestHash(
			OracleHash,
			static_cast<uint32>(Seed));
		OracleHash = MixManifestHash(
			OracleHash,
			static_cast<uint64>(First.RouteCandidatePoolHash));
		OracleHash = MixManifestHash(
			OracleHash,
			FABTSM3MonthlyRouteBuilder::ComputePoolSnapshotHash(
				First));
		OracleHash = MixManifestHash(
			OracleHash,
			static_cast<uint32>(
				First.RetainedCandidates.Num()));
		for (const FABTSM3MonthlyRouteAttemptReport& Report :
			First.AttemptReports)
		{
			MaxExpanded = FMath::Max(
				MaxExpanded,
				Report.ExpandedStates);
			MaxRelaxations = FMath::Max(
				MaxRelaxations,
				Report.Relaxations);
			MaxBacktracks = FMath::Max(
				MaxBacktracks,
				Report.Backtracks);
		}
	}
	DurationsMS.Sort();
	const int32 P95Index = FMath::Clamp(
		FMath::CeilToInt(DurationsMS.Num() * 0.95) - 1,
		0,
		DurationsMS.Num() - 1);
	const double P95MS =
		DurationsMS.IsValidIndex(P95Index)
		? DurationsMS[P95Index]
		: 0.0;
	const double MaxMS =
		DurationsMS.IsEmpty() ? 0.0 : DurationsMS.Last();
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R2][RouteSweep] Terminal=%d NormalAccepted=%d RouteFallback=%d Rejected=%d P95MS=%.3f MaxMS=%.3f MaxExpanded=%d MaxRelaxations=%d MaxBacktracks=%d OracleHash=%016llX"),
		Seeds.Num(),
		NormalAccepted,
		RouteFallback,
		Rejected,
		P95MS,
		MaxMS,
		MaxExpanded,
		MaxRelaxations,
		MaxBacktracks,
		static_cast<unsigned long long>(OracleHash));
	TestEqual(TEXT("Sweep terminal count"), Seeds.Num(), 200);
	TestEqual(TEXT("All seeds normally accepted"),
		NormalAccepted, 200);
	TestEqual(TEXT("No route fallback"), RouteFallback, 0);
	TestEqual(TEXT("No rejected seed"), Rejected, 0);
	TestTrue(TEXT("Route P95 budget"), P95MS <= 200.0);
	TestTrue(TEXT("Route maximum budget"), MaxMS <= 1000.0);
	const FABTSM3MonthlyRouteConfig BudgetConfig =
		MakeQuietConfig();
	TestTrue(TEXT("Expanded-state hard cap"),
		MaxExpanded
			<= BudgetConfig.MaxExpandedStatesPerCandidate);
	TestTrue(TEXT("Relaxation hard cap"),
		MaxRelaxations
			<= BudgetConfig.MaxRelaxationsPerCandidate);
	TestTrue(TEXT("Backtrack hard cap"),
		MaxBacktracks
			<= BudgetConfig.MaxCandidateBacktracksPerSeed);
	TestEqual(TEXT("Frozen route oracle"),
		OracleHash,
		FABTSM3R2AcceptanceManifest::FrozenRouteOracleHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R2RouteFailureFallbackTest,
	"ABTS.M3.Monthly.RouteFailure.01AllNormalCandidates",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R2RouteFailureFallbackTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R2RouteTests;
	FABTSM3MonthlyRoadContext BlockedContext;
	BlockedContext.Cells.SetNum(GetLogicalCells().Num());
	for (FABTSM3MonthlyRouteCellContext& Cell :
		BlockedContext.Cells)
	{
		Cell.bHardBlocked = true;
	}
	FABTSM3MonthlyRoutePool First;
	FABTSM3MonthlyRoutePool Second;
	FString Failure;
	const bool bFirst = BuildRoutePool(
		DisplaySeed,
		First,
		Failure,
		BlockedContext);
	const bool bSecond = BuildRoutePool(
		DisplaySeed,
		Second,
		Failure,
		BlockedContext);
	const bool bPassed = bFirst
		&& bSecond
		&& RoutePoolEqual(First, Second)
		&& First.bRoutePoolValid
		&& First.bUsedRouteFallback
		&& First.NormalHardPassCount == 0
		&& First.RetainedCandidates.Num() == 1
		&& First.RetainedCandidates[0].Origin
			== EABTSM3MonthlyRouteOrigin::MonthlyRouteFallback
		&& First.RetainedCandidates[0].bHardPass
		&& !First.bMonthlyWorldAccepted;
	int32 NormalRejects = 0;
	int32 HardBlockedRejects = 0;
	for (const FABTSM3MonthlyRouteAttemptReport& Report :
		First.AttemptReports)
	{
		if (Report.Origin
			!= EABTSM3MonthlyRouteOrigin::Normal)
		{
			continue;
		}
		++NormalRejects;
		HardBlockedRejects += Report.RejectReason
				== EABTSM3MonthlyRouteRejectReason::HardBlocked
			? 1
			: 0;
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R2][RouteFailureCertification] Terminal=1 Passed=%d Failed=%d NormalRejects=%d HardBlocked=%d FallbackAttempts=%d FallbackHash=%016llX PoolHash=%016llX SnapshotHash=%016llX"),
		bPassed ? 1 : 0,
		bPassed ? 0 : 1,
		NormalRejects,
		HardBlockedRejects,
		First.AttemptReports.Num()
			- First.AttemptedCandidateCount,
		First.RetainedCandidates.IsEmpty()
			? 0ull
			: static_cast<unsigned long long>(
				First.RetainedCandidates[0].CandidateHash),
		static_cast<unsigned long long>(
			First.RouteCandidatePoolHash),
		static_cast<unsigned long long>(
			FABTSM3MonthlyRouteBuilder::ComputePoolSnapshotHash(
				First)));
	TestTrue(TEXT("Deterministic route fallback"), bPassed);
	TestEqual(TEXT("All normal attempts are reported"),
		NormalRejects,
		MakeQuietConfig().NormalCandidateSlots);
	TestEqual(TEXT("All injected rejects are hard blocks"),
		HardBlockedRejects,
		NormalRejects);
	TestEqual(TEXT("Successful fallback attempt is reported"),
		First.AttemptReports.Num()
			- First.AttemptedCandidateCount,
		1);
	TestEqual(TEXT("Frozen fallback candidate identity"),
		static_cast<uint64>(
			First.RetainedCandidates.IsEmpty()
				? 0
				: First.RetainedCandidates[0].CandidateHash),
		FABTSM3R2AcceptanceManifest::FrozenFailureFallbackHash);
	TestEqual(TEXT("Frozen fallback pool identity"),
		static_cast<uint64>(First.RouteCandidatePoolHash),
		FABTSM3R2AcceptanceManifest::FrozenFailurePoolHash);
	TestEqual(TEXT("Frozen fallback snapshot identity"),
		FABTSM3MonthlyRouteBuilder::ComputePoolSnapshotHash(
			First),
		FABTSM3R2AcceptanceManifest::FrozenFailureSnapshotHash);
	return true;
}

#endif
