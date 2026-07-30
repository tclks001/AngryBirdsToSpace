// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ABTSRuntime.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "PCG/ABTSM3MonthlyPresentation.h"
#include "PCG/ABTSM3R5AcceptanceManifest.h"
#include "Planet/ABTSM2Planet.h"
#include "Terrain/ABTSM3Planet.h"

namespace ABTSM3R5PresentationTests
{
constexpr float ReferencePlanetRadiusCM = 10000.0f;
constexpr int32 DisplaySeed =
	FABTSM3R5AcceptanceManifest::DisplaySeed;
constexpr int32 SweepSeedCount =
	FABTSM3R5AcceptanceManifest::SweepSeedCount;

class FTestHash64
{
public:
	void Add(const uint64 Input)
	{
		for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
		{
			Value ^= static_cast<uint8>(
				(Input >> (ByteIndex * 8)) & 0xffull);
			Value *= 1099511628211ull;
		}
	}

	uint64 Get() const
	{
		return Value;
	}

private:
	uint64 Value = 14695981039346656037ull;
};

TArray<FABTSM2Cell> BuildLogicalCells(
	const int32 Subdivision = 5)
{
	AABTSM2Planet::FUnitSphereMesh Mesh;
	AABTSM2Planet::BuildUnitIcosphere(Subdivision, Mesh);
	TArray<FABTSM2Cell> Cells;
	Cells.SetNum(Mesh.Vertices.Num());
	for (int32 CellId = 0;
		CellId < Mesh.Vertices.Num();
		++CellId)
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
		Cell.bIsPentagon =
			Cell.NeighborCellIds.Num() == 5;
	}
	return Cells;
}

const TArray<FABTSM2Cell>& GetLogicalCells()
{
	static const TArray<FABTSM2Cell> Cells =
		BuildLogicalCells();
	return Cells;
}

FABTSM3MonthlyRouteConfig MakeRouteConfig()
{
	FABTSM3MonthlyRouteConfig Config;
	Config.bEmitRouteLogs = false;
	return Config;
}

FABTSM3MonthlyEncounterSpatialConfig MakeSpatialConfig()
{
	FABTSM3MonthlyEncounterSpatialConfig Config;
	Config.bEmitSpatialLogs = false;
	return Config;
}

FABTSM3MonthlyPresentationConfig MakePresentationConfig()
{
	FABTSM3MonthlyPresentationConfig Config;
	Config.bEmitPresentationLogs = false;
	return Config;
}

bool BuildSource(
	const int32 Seed,
	const FABTSM3MonthlyRouteConfig& RouteConfig,
	const FABTSM3MonthlyEncounterSpatialConfig& SpatialConfig,
	FABTSM3MonthlyRoutePool& OutRoutePool,
	FABTSM3MonthlySpatialResult& OutSpatialResult,
	FString& OutFailure)
{
	if (!FABTSM3MonthlyRouteBuilder::Build(
			Seed,
			RouteConfig,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			FABTSM3MonthlyRoadContext(),
			OutRoutePool,
			OutFailure))
	{
		return false;
	}
	return FABTSM3MonthlyEncounterBuilder::Build(
		Seed,
		SpatialConfig,
		RouteConfig,
		GetLogicalCells(),
		ReferencePlanetRadiusCM,
		OutRoutePool,
		FABTSM3MonthlySpatialFaultInjection(),
		OutSpatialResult,
		OutFailure);
}

bool BuildPresentation(
	const int32 Seed,
	const FABTSM3MonthlyPresentationConfig& PresentationConfig,
	const FABTSM3MonthlyEncounterSpatialConfig& SpatialConfig,
	const FABTSM3MonthlyRouteConfig& RouteConfig,
	const FABTSM3MonthlyRoutePool& RoutePool,
	const FABTSM3MonthlySpatialResult& SpatialResult,
	const FABTSM3MonthlyPresentationFaultInjection&
		FaultInjection,
	FABTSM3MonthlyPresentationResult& OutResult,
	FString& OutFailure)
{
	return FABTSM3MonthlyPresentationBuilder::Build(
		Seed,
		PresentationConfig,
		SpatialConfig,
		RouteConfig,
		GetLogicalCells(),
		ReferencePlanetRadiusCM,
		RoutePool,
		SpatialResult,
		FaultInjection,
		OutResult,
		OutFailure);
}

bool ValidatePresentation(
	const FABTSM3MonthlyPresentationConfig& PresentationConfig,
	const FABTSM3MonthlyEncounterSpatialConfig& SpatialConfig,
	const FABTSM3MonthlyRouteConfig& RouteConfig,
	const FABTSM3MonthlyRoutePool& RoutePool,
	const FABTSM3MonthlySpatialResult& SpatialResult,
	const FABTSM3MonthlyPresentationFaultInjection&
		FaultInjection,
	const FABTSM3MonthlyPresentationResult& Result,
	EABTSM3MonthlyPresentationRejectReason& OutReason,
	FString& OutFailure)
{
	return FABTSM3MonthlyPresentationBuilder::Validate(
		PresentationConfig,
		SpatialConfig,
		RouteConfig,
		GetLogicalCells(),
		ReferencePlanetRadiusCM,
		RoutePool,
		SpatialResult,
		FaultInjection,
		Result,
		OutReason,
		OutFailure);
}

bool ResultsEqual(
	const FABTSM3MonthlyPresentationResult& A,
	const FABTSM3MonthlyPresentationResult& B)
{
	return FABTSM3MonthlyPresentationResult::StaticStruct()
		->CompareScriptStruct(&A, &B, PPF_None);
}

TArray<int32> GetSweepSeeds()
{
	TArray<int32> Seeds;
	Seeds.Reserve(SweepSeedCount);
	Seeds.Add(DisplaySeed);
	for (int32 Seed = 0; Seed <= 98; ++Seed)
	{
		Seeds.Add(Seed);
	}
	return Seeds;
}

uint64 ComputeSeedManifestHash(
	const TArray<int32>& Seeds)
{
	FString Payload;
	for (int32 Index = 0; Index < Seeds.Num(); ++Index)
	{
		if (Index > 0)
		{
			Payload.AppendChar(TEXT(','));
		}
		Payload.Append(FString::FromInt(Seeds[Index]));
	}
	FTCHARToUTF8 Utf8(*Payload);
	uint64 Hash = 14695981039346656037ull;
	for (int32 Index = 0; Index < Utf8.Length(); ++Index)
	{
		Hash ^= static_cast<uint8>(Utf8.Get()[Index]);
		Hash *= 1099511628211ull;
	}
	return Hash;
}

struct FVisualFragmentMetrics
{
	int32 MinComponentCellCount = MAX_int32;
	int32 BoundaryPermille = 1000;
};

bool ComputeVisualFragmentMetrics(
	const FABTSM3MonthlyCandidatePresentation& Candidate,
	FVisualFragmentMetrics& OutMetrics)
{
	OutMetrics = FVisualFragmentMetrics();
	const TArray<FABTSM2Cell>& LogicalCells =
		GetLogicalCells();
	if (Candidate.Cells.Num() != LogicalCells.Num())
	{
		return false;
	}

	TBitArray<> Visited(false, Candidate.Cells.Num());
	TArray<int32> Queue;
	Queue.Reserve(Candidate.Cells.Num());
	int32 TotalNeighborEdges = 0;
	int32 BoundaryNeighborEdges = 0;
	for (int32 StartCellId = 0;
		StartCellId < Candidate.Cells.Num();
		++StartCellId)
	{
		for (const int32 NeighborId :
			LogicalCells[StartCellId].NeighborCellIds)
		{
			if (NeighborId <= StartCellId
				|| !Candidate.Cells.IsValidIndex(NeighborId))
			{
				continue;
			}
			++TotalNeighborEdges;
			BoundaryNeighborEdges +=
				Candidate.Cells[StartCellId]
					.DisplayBiomeArchetype
					!= Candidate.Cells[NeighborId]
						.DisplayBiomeArchetype
				? 1
				: 0;
		}
		if (Visited[StartCellId])
		{
			continue;
		}
		const EABTSM3BiomeArchetype ComponentBiome =
			Candidate.Cells[StartCellId]
				.DisplayBiomeArchetype;
		Queue.Reset();
		Queue.Add(StartCellId);
		Visited[StartCellId] = true;
		int32 ReadIndex = 0;
		while (ReadIndex < Queue.Num())
		{
			const int32 CellId = Queue[ReadIndex++];
			for (const int32 NeighborId :
				LogicalCells[CellId].NeighborCellIds)
			{
				if (!Candidate.Cells.IsValidIndex(NeighborId)
					|| Visited[NeighborId]
					|| Candidate.Cells[NeighborId]
							.DisplayBiomeArchetype
						!= ComponentBiome)
				{
					continue;
				}
				Visited[NeighborId] = true;
				Queue.Add(NeighborId);
			}
		}
		OutMetrics.MinComponentCellCount = FMath::Min(
			OutMetrics.MinComponentCellCount,
			Queue.Num());
	}
	if (TotalNeighborEdges <= 0
		|| OutMetrics.MinComponentCellCount == MAX_int32)
	{
		return false;
	}
	OutMetrics.BoundaryPermille =
		BoundaryNeighborEdges * 1000
		/ TotalNeighborEdges;
	return true;
}

bool CheckCandidate(
	FAutomationTestBase& Test,
	const FABTSM3MonthlyPresentationConfig& Config,
	const FABTSM3MonthlySpatialCandidate& Source,
	const FABTSM3MonthlyCandidatePresentation& Candidate,
	const FString& Prefix)
{
	bool bPassed = true;
	bPassed &= Test.TestEqual(
		Prefix + TEXT(".SourceId"),
		Candidate.SourceRouteCandidateId,
		Source.SourceRouteCandidateId);
	bPassed &= Test.TestEqual(
		Prefix + TEXT(".SourceHash"),
		Candidate.SourceSpatialCandidateHash,
		Source.SpatialCandidateHash);
	bPassed &= Test.TestEqual(
		Prefix + TEXT(".CellCount"),
		Candidate.Cells.Num(),
		Source.Cells.Num());
	bPassed &= Test.TestEqual(
		Prefix + TEXT(".EnvelopeCount"),
		Candidate.Envelopes.Num(),
		Source.PlayableEnvelopes.Num());
	bPassed &= Test.TestEqual(
		Prefix + TEXT(".Playable"),
		Candidate.PlayableCellCount,
		Source.PlayableCellCount);
	bPassed &= Test.TestEqual(
		Prefix + TEXT(".Active"),
		Candidate.ActiveRoleCellCount,
		Source.ActiveRoleCellCount);
	bPassed &= Test.TestEqual(
		Prefix + TEXT(".DeepWild"),
		Candidate.DeepWildCellCount,
		Source.DeepWildCellCount);
	bPassed &= Test.TestTrue(
		Prefix + TEXT(".Coverage"),
		Candidate.ActiveRoleCoveragePermille
			>= Config.MinActiveRoleCoveragePermille
		&& Candidate.DeepWildPermille
			<= Config.MaxDeepWildPermille);
	bPassed &= Test.TestTrue(
		Prefix + TEXT(".Themes"),
		Candidate.BiomeArchetypeCount
			>= Config.MinBiomeArchetypeCount);
	bPassed &= Test.TestEqual(
		Prefix + TEXT(".Singletons"),
		Candidate.SingleCellBiomeComponentCount,
		0);
	FVisualFragmentMetrics FragmentMetrics;
	bPassed &= Test.TestTrue(
		Prefix + TEXT(".FragmentMetrics"),
		ComputeVisualFragmentMetrics(
			Candidate,
			FragmentMetrics));
	bPassed &= Test.TestTrue(
		Prefix + TEXT(".MinimumVisualArea"),
		FragmentMetrics.MinComponentCellCount
			>= FABTSM3R5AcceptanceManifest::
				MinVisualBiomeComponentCells);
	bPassed &= Test.TestEqual(
		Prefix + TEXT(".MinimumVisualAreaMetric"),
		Candidate.MinVisualBiomeComponentCellCount,
		FragmentMetrics.MinComponentCellCount);
	bPassed &= Test.TestTrue(
		Prefix + TEXT(".WholePlanetBoundaryCoarseScreen"),
		FragmentMetrics.BoundaryPermille
			<= FABTSM3R5AcceptanceManifest::
				MaxVisualBiomeBoundaryPermille);
	bPassed &= Test.TestEqual(
		Prefix + TEXT(".BoundaryMetric"),
		Candidate.VisualBiomeBoundaryPermille,
		FragmentMetrics.BoundaryPermille);
	bPassed &= Test.TestTrue(
		Prefix + TEXT(".Rhythm"),
		Candidate.MinVisualBeatLengthCM
			>= Config.MinVisualBeatLengthCM
		&& Candidate.MaxVisualBeatLengthCM
			<= Config.MaxVisualBeatLengthCM);
	bPassed &= Test.TestTrue(
		Prefix + TEXT(".DecorBudget"),
		Candidate.PlannedDecorationInstanceCount
			<= Config.MaxDecorInstancesPerCandidate);
	TArray<TArray<int32>> ExpectedEnvelopeIdsByCell;
	ExpectedEnvelopeIdsByCell.SetNum(Source.Cells.Num());
	for (const FABTSM3PlayableEnvelope& Envelope :
		Source.PlayableEnvelopes)
	{
		for (const FABTSM3PlayableCellRole& Role :
			Envelope.Cells)
		{
			if (ExpectedEnvelopeIdsByCell.IsValidIndex(
					Role.CellId))
			{
				ExpectedEnvelopeIdsByCell[Role.CellId].Add(
					Envelope.EnvelopeId);
			}
		}
	}
	for (TArray<int32>& Membership :
		ExpectedEnvelopeIdsByCell)
	{
		Membership.Sort();
	}
	for (const FABTSM3MonthlyPresentationCell& Cell :
		Candidate.Cells)
	{
		if (Cell.CellId < 0
			|| Cell.CellId >= Candidate.Cells.Num()
			|| Cell.BiomeDistrictId == INDEX_NONE
			|| Cell.VisualBeatId == INDEX_NONE)
		{
			Test.AddError(FString::Printf(
				TEXT("%s.InvalidCell:%d"),
				*Prefix,
				Cell.CellId));
			bPassed = false;
			break;
		}
		if (!Source.Cells.IsValidIndex(Cell.CellId))
		{
			Test.AddError(FString::Printf(
				TEXT("%s.SourceCellIdentity:%d"),
				*Prefix,
				Cell.CellId));
			bPassed = false;
			break;
		}
		const FABTSM3MonthlySpatialCell& SourceCell =
			Source.Cells[Cell.CellId];
		if (Cell.BiomeDistrictId
				!= SourceCell.BiomeDistrictId
			|| Cell.PrimaryEnvelopeId
				!= SourceCell.PrimaryEnvelopeId
			|| Cell.ActiveRoleMask
				!= SourceCell.ActiveRoleMask
			|| Cell.bApprovedTransition
				!= SourceCell.bApprovedTransition
			|| Cell.bWater != SourceCell.bWater
			|| Cell.bTargetFootprint
				!= SourceCell.bTargetFootprint
			|| Cell.bNoRoad != SourceCell.bNoRoad
			|| Cell.bAttackCorridor
				!= SourceCell.bAttackCorridor
			|| Cell.EnvelopeIds
				!= ExpectedEnvelopeIdsByCell[Cell.CellId]
			|| Cell.bPlayable
				!= !Cell.EnvelopeIds.IsEmpty()
			|| Cell.bDeepWild
				!= (Cell.bPlayable
					&& Cell.ActiveRoleMask == 0
					&& !Cell.bApprovedTransition))
		{
			Test.AddError(FString::Printf(
				TEXT("%s.SourceLogicalIdentity:%d"),
				*Prefix,
				Cell.CellId));
			bPassed = false;
			break;
		}
		if (Cell.bDecorationProtected
			&& (Cell.DecorationKindMask != 0
				|| Cell.MaxDecorationInstances != 0))
		{
			Test.AddError(FString::Printf(
				TEXT("%s.ProtectedDecor:%d"),
				*Prefix,
				Cell.CellId));
			bPassed = false;
			break;
		}
		if (Cell.MaxDecorationInstances
			> Config.MaxDecorInstancesPerCell)
		{
			Test.AddError(FString::Printf(
				TEXT("%s.CellDecorBudget:%d"),
				*Prefix,
				Cell.CellId));
			bPassed = false;
			break;
		}
	}
	return bPassed;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3MonthlyPresentationCoreTest,
	"ABTS.M3.Monthly.Biome.0",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3MonthlyPresentationCoreTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R5PresentationTests;
	const FABTSM3MonthlyRouteConfig RouteConfig =
		MakeRouteConfig();
	const FABTSM3MonthlyEncounterSpatialConfig SpatialConfig =
		MakeSpatialConfig();
	const FABTSM3MonthlyPresentationConfig PresentationConfig =
		MakePresentationConfig();
	const AABTSM3Planet* PlanetCDO =
		GetDefault<AABTSM3Planet>();
	TestNotNull(
		TEXT("Native M3 planet CDO exists"),
		PlanetCDO);
	if (PlanetCDO != nullptr)
	{
		TestFalse(
			TEXT("Native preview authority defaults disabled"),
			PlanetCDO->bEnableMonthlyPresentationPreview);
		TestEqual(
			TEXT("Native preview requires exact candidate"),
			PlanetCDO->MonthlyPresentationPreviewCandidateId,
			INDEX_NONE);
	}
	FABTSM3MonthlyRoutePool RoutePool;
	FABTSM3MonthlySpatialResult SpatialResult;
	FString Failure;
	if (!BuildSource(
			DisplaySeed,
			RouteConfig,
			SpatialConfig,
			RoutePool,
			SpatialResult,
			Failure))
	{
		AddError(TEXT("Display source failed: ") + Failure);
		return false;
	}
	const int64 SourceResultHash =
		SpatialResult.SpatialResultHash;
	TArray<int64> SourceCandidateHashes;
	for (const FABTSM3MonthlySpatialCandidate& Candidate :
		SpatialResult.RetainedCandidates)
	{
		SourceCandidateHashes.Add(
			Candidate.SpatialCandidateHash);
	}

	FABTSM3MonthlyPresentationResult First;
	if (!BuildPresentation(
			DisplaySeed,
			PresentationConfig,
			SpatialConfig,
			RouteConfig,
			RoutePool,
			SpatialResult,
			FABTSM3MonthlyPresentationFaultInjection(),
			First,
			Failure))
	{
		AddError(TEXT("Display presentation failed: ") + Failure);
		return false;
	}
	EABTSM3MonthlyPresentationRejectReason RejectReason =
		EABTSM3MonthlyPresentationRejectReason::None;
	TestTrue(
		TEXT("Display validates"),
		ValidatePresentation(
			PresentationConfig,
			SpatialConfig,
			RouteConfig,
			RoutePool,
			SpatialResult,
			FABTSM3MonthlyPresentationFaultInjection(),
			First,
			RejectReason,
			Failure));
	TestTrue(
		TEXT("Presentation valid and unpublished"),
		First.bPresentationValid
			&& !First.bMonthlyWorldAccepted
			&& First.RejectReason
				== EABTSM3MonthlyPresentationRejectReason::
					None);
	TestEqual(
		TEXT("Every retained candidate has one plan"),
		First.CandidatePresentations.Num(),
		SpatialResult.RetainedCandidates.Num());
	for (int32 Index = 0;
		Index < First.CandidatePresentations.Num();
		++Index)
	{
		CheckCandidate(
			*this,
			PresentationConfig,
			SpatialResult.RetainedCandidates[Index],
			First.CandidatePresentations[Index],
			FString::Printf(
				TEXT("Candidate[%d]"),
				Index));
	}

	FABTSM3MonthlyPresentationResult Second;
	TestTrue(
		TEXT("Second build succeeds"),
		BuildPresentation(
			DisplaySeed,
			PresentationConfig,
			SpatialConfig,
			RouteConfig,
			RoutePool,
			SpatialResult,
			FABTSM3MonthlyPresentationFaultInjection(),
			Second,
			Failure));
	TestTrue(
		TEXT("Whole result deterministic"),
		ResultsEqual(First, Second));
	TestEqual(
		TEXT("R3 result hash unchanged"),
		SpatialResult.SpatialResultHash,
		SourceResultHash);
	for (int32 Index = 0;
		Index < SourceCandidateHashes.Num();
		++Index)
	{
		TestEqual(
			FString::Printf(
				TEXT("R3 candidate hash unchanged %d"),
				Index),
			SpatialResult.RetainedCandidates[Index]
				.SpatialCandidateHash,
			SourceCandidateHashes[Index]);
	}

	FABTSM3MonthlyPresentationConfig LogToggle =
		PresentationConfig;
	LogToggle.bEmitPresentationLogs = true;
	TestEqual(
		TEXT("Log toggle excluded from config hash"),
		FABTSM3MonthlyPresentationBuilder::
			ComputeConfigHash(LogToggle),
		FABTSM3MonthlyPresentationBuilder::
			ComputeConfigHash(PresentationConfig));
	FABTSM3MonthlyPresentationConfig SemanticToggle =
		PresentationConfig;
	++SemanticToggle.TargetVisualBeatLengthCM;
	TestNotEqual(
		TEXT("Semantic config changes hash"),
		FABTSM3MonthlyPresentationBuilder::
			ComputeConfigHash(SemanticToggle),
		FABTSM3MonthlyPresentationBuilder::
			ComputeConfigHash(PresentationConfig));

	FABTSM3MonthlyPresentationResult Tampered = First;
	Tampered.CandidatePresentations[0].Cells[0]
		.ThemeVariantId ^= 1;
	Tampered.CandidatePresentations[0]
		.CandidatePresentationHash =
		static_cast<int64>(
			FABTSM3MonthlyPresentationBuilder::
				ComputeCandidateHash(
					Tampered.CandidatePresentations[0]));
	Tampered.PresentationResultHash =
		static_cast<int64>(
			FABTSM3MonthlyPresentationBuilder::
				ComputeResultHash(Tampered));
	Failure.Reset();
	TestFalse(
		TEXT("Deep tamper fails canonical validation"),
		ValidatePresentation(
			PresentationConfig,
			SpatialConfig,
			RouteConfig,
			RoutePool,
			SpatialResult,
			FABTSM3MonthlyPresentationFaultInjection(),
			Tampered,
			RejectReason,
			Failure));
	TestEqual(
		TEXT("Deep tamper reports hash mismatch"),
		RejectReason,
		EABTSM3MonthlyPresentationRejectReason::
			HashMismatch);

	const FABTSM3MonthlyCandidatePresentation& Preview =
		First.CandidatePresentations[0];
	FABTSM3MonthlyPresentationDebugData DebugData;
	FABTSM3MonthlyPresentationBuilder::BuildDebugData(
		First,
		Preview.SourceRouteCandidateId,
		DebugData);
	TArray<int32> ExpectedTargetFootprintCellIds;
	TArray<int32> ExpectedAttackCorridorCellIds;
	for (const FABTSM3MonthlyPresentationCell& Cell :
		Preview.Cells)
	{
		if (Cell.bTargetFootprint)
		{
			ExpectedTargetFootprintCellIds.Add(Cell.CellId);
		}
		if (Cell.bAttackCorridor)
		{
			ExpectedAttackCorridorCellIds.Add(Cell.CellId);
		}
	}
	TestTrue(
		TEXT("F7 target-footprint debug cells are exact"),
		!ExpectedTargetFootprintCellIds.IsEmpty()
			&& DebugData.TargetFootprintCellIds
				== ExpectedTargetFootprintCellIds);
	TestTrue(
		TEXT("F7 attack-corridor debug cells are exact"),
		!ExpectedAttackCorridorCellIds.IsEmpty()
			&& DebugData.AttackCorridorCellIds
				== ExpectedAttackCorridorCellIds);
	for (const int32 CellId :
		DebugData.TargetFootprintCellIds)
	{
		TestTrue(
			FString::Printf(
				TEXT("Target cell remains decor protected %d"),
				CellId),
			DebugData.DecorationProtectedCellIds.Contains(
				CellId));
	}
	for (const int32 CellId :
		DebugData.AttackCorridorCellIds)
	{
		TestTrue(
			FString::Printf(
				TEXT("Attack corridor remains decor protected %d"),
				CellId),
			DebugData.DecorationProtectedCellIds.Contains(
				CellId));
	}
	FString ManifestFailure;
	TestTrue(
		TEXT("R5 acceptance manifest self-valid"),
		FABTSM3R5AcceptanceManifest::Validate(
			ManifestFailure));
	TestEqual(
		TEXT("Frozen display result"),
		static_cast<uint64>(
			First.PresentationResultHash),
		FABTSM3R5AcceptanceManifest::
			FrozenDisplayResultHash);
	TestEqual(
		TEXT("Frozen display preview candidate"),
		static_cast<uint64>(
			Preview.CandidatePresentationHash),
		FABTSM3R5AcceptanceManifest::
			FrozenDisplayPreviewCandidateHash);
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M3R5][DisplayProbe] Seed=%d ConfigHash=%016llX SourceSpatial=%016llX ResultHash=%016llX PreviewSourceCandidate=%d PreviewSourceSpatialCandidate=%016llX PreviewHash=%016llX CandidatePlans=%d Cells=%d Districts=%d Envelopes=%d Themes=%d Beats=%d BeatMinCM=%d BeatMaxCM=%d ActiveCoveragePermille=%d DeepWildPermille=%d MergedLogicalSingletons=%d MergedSmallFragments=%d SingletonComponents=%d MinVisualComponentCells=%d VisualBoundaryPermille=%d ProtectedCells=%d TargetFootprintCells=%d AttackCorridorCells=%d PlannedInstances=%d MonthlyAccepted=0"),
		DisplaySeed,
		static_cast<unsigned long long>(
			FABTSM3MonthlyPresentationBuilder::
				ComputeConfigHash(PresentationConfig)),
		static_cast<unsigned long long>(
			SpatialResult.SpatialResultHash),
		static_cast<unsigned long long>(
			First.PresentationResultHash),
		Preview.SourceRouteCandidateId,
		static_cast<unsigned long long>(
			Preview.SourceSpatialCandidateHash),
		static_cast<unsigned long long>(
			Preview.CandidatePresentationHash),
		First.CandidatePresentations.Num(),
		Preview.Cells.Num(),
		Preview.DistrictStyles.Num(),
		Preview.Envelopes.Num(),
		Preview.BiomeArchetypeCount,
		Preview.VisualBeats.Num(),
		Preview.MinVisualBeatLengthCM,
		Preview.MaxVisualBeatLengthCM,
		Preview.ActiveRoleCoveragePermille,
		Preview.DeepWildPermille,
		Preview.MergedLogicalSingletonCellCount,
		Preview.MergedSmallVisualFragmentCellCount,
		Preview.SingleCellBiomeComponentCount,
		Preview.MinVisualBiomeComponentCellCount,
		Preview.VisualBiomeBoundaryPermille,
		Preview.DecorationProtectedCellCount,
		DebugData.TargetFootprintCellIds.Num(),
		DebugData.AttackCorridorCellIds.Num(),
		Preview.PlannedDecorationInstanceCount);
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M3R5][ManifestProbe] SelfValid=%d Failure=%s ComputedManifestHash=%016llX"),
		ManifestFailure.IsEmpty() ? 1 : 0,
		*ManifestFailure,
		static_cast<unsigned long long>(
			FABTSM3R5AcceptanceManifest::
				ComputeManifestHash()));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3MonthlyPresentationFailureTest,
	"ABTS.M3.Monthly.BiomeFailure",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3MonthlyPresentationFailureTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R5PresentationTests;
	const FABTSM3MonthlyRouteConfig RouteConfig =
		MakeRouteConfig();
	const FABTSM3MonthlyEncounterSpatialConfig SpatialConfig =
		MakeSpatialConfig();
	const FABTSM3MonthlyPresentationConfig PresentationConfig =
		MakePresentationConfig();
	FABTSM3MonthlyRoutePool RoutePool;
	FABTSM3MonthlySpatialResult SpatialResult;
	FString Failure;
	if (!BuildSource(
			DisplaySeed,
			RouteConfig,
			SpatialConfig,
			RoutePool,
			SpatialResult,
			Failure))
	{
		AddError(TEXT("Failure source failed: ") + Failure);
		return false;
	}

	FABTSM3MonthlyPresentationConfig InvalidConfig =
		PresentationConfig;
	InvalidConfig.MinVisualBeatLengthCM =
		InvalidConfig.MaxVisualBeatLengthCM + 1;
	FABTSM3MonthlyPresentationResult Result;
	TestFalse(
		TEXT("Invalid rhythm rejected"),
		BuildPresentation(
			DisplaySeed,
			InvalidConfig,
			SpatialConfig,
			RouteConfig,
			RoutePool,
			SpatialResult,
			FABTSM3MonthlyPresentationFaultInjection(),
			Result,
			Failure));
	TestEqual(
		TEXT("Invalid rhythm reason"),
		Result.RejectReason,
		EABTSM3MonthlyPresentationRejectReason::
			InvalidConfig);

	FABTSM3MonthlySpatialResult CorruptSource =
		SpatialResult;
	CorruptSource.RetainedCandidates[0]
		.Cells[0].BiomeDistrictId = INDEX_NONE;
	CorruptSource.RetainedCandidates[0]
		.SpatialCandidateHash =
		static_cast<int64>(
			FABTSM3MonthlyEncounterBuilder::
				ComputeCandidateHash(
					CorruptSource.RetainedCandidates[0]));
	CorruptSource.SpatialResultHash =
		static_cast<int64>(
			FABTSM3MonthlyEncounterBuilder::
				ComputeResultHash(CorruptSource));
	TestFalse(
		TEXT("Re-signed corrupt source rejected"),
		BuildPresentation(
			DisplaySeed,
			PresentationConfig,
			SpatialConfig,
			RouteConfig,
			RoutePool,
			CorruptSource,
			FABTSM3MonthlyPresentationFaultInjection(),
			Result,
			Failure));
	TestEqual(
		TEXT("Corrupt source reason"),
		Result.RejectReason,
		EABTSM3MonthlyPresentationRejectReason::
			InvalidSourceSpatial);

	FABTSM3MonthlyPresentationFaultInjection Fault;
	Fault.RejectedSourceCandidateId =
		SpatialResult.RetainedCandidates[0]
			.SourceRouteCandidateId;
	TestFalse(
		TEXT("Candidate fault rejects whole pool"),
		BuildPresentation(
			DisplaySeed,
			PresentationConfig,
			SpatialConfig,
			RouteConfig,
			RoutePool,
			SpatialResult,
			Fault,
			Result,
			Failure));
	TestEqual(
		TEXT("Fault reason"),
		Result.RejectReason,
		EABTSM3MonthlyPresentationRejectReason::
			FaultInjected);
	TestEqual(
		TEXT("No partial plans survive fault"),
		Result.CandidatePresentations.Num(),
		0);

	TestFalse(
		TEXT("World seed mismatch rejected"),
		BuildPresentation(
			DisplaySeed + 1,
			PresentationConfig,
			SpatialConfig,
			RouteConfig,
			RoutePool,
			SpatialResult,
			FABTSM3MonthlyPresentationFaultInjection(),
			Result,
			Failure));
	TestEqual(
		TEXT("World seed mismatch reason"),
		Result.RejectReason,
		EABTSM3MonthlyPresentationRejectReason::
			InvalidSourceSpatial);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3MonthlyPresentationSweepTest,
	"ABTS.M3.Monthly.Biome.Sweep100",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3MonthlyPresentationSweepTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R5PresentationTests;
	const FABTSM3MonthlyRouteConfig RouteConfig =
		MakeRouteConfig();
	const FABTSM3MonthlyEncounterSpatialConfig SpatialConfig =
		MakeSpatialConfig();
	const FABTSM3MonthlyPresentationConfig PresentationConfig =
		MakePresentationConfig();
	const TArray<int32> Seeds = GetSweepSeeds();
	const uint64 SeedManifestHash =
		ComputeSeedManifestHash(Seeds);
	TestEqual(
		TEXT("Frozen seed manifest"),
		SeedManifestHash,
		FABTSM3R5AcceptanceManifest::
			FrozenSweepSeedManifestHash);

	FTestHash64 Oracle;
	TArray<double> DurationsMS;
	DurationsMS.Reserve(Seeds.Num());
	int32 TerminalCount = 0;
	int32 AcceptedCount = 0;
	int32 RejectedCount = 0;
	int32 SourceRejectedCount = 0;
	int32 CandidatePlanCount = 0;
	int32 MinActiveCoveragePermille = 1000;
	int32 MaxDeepWildPermille = 0;
	int32 MinThemes = MAX_int32;
	int32 TotalMergedLogicalSingletons = 0;
	int32 TotalMergedSmallVisualFragments = 0;
	int32 MaxSingletons = 0;
	int32 MinVisualComponentCellCount = MAX_int32;
	int32 MaxVisualBoundaryPermille = 0;
	int32 ProtectedInstanceViolations = 0;
	for (const int32 Seed : Seeds)
	{
		++TerminalCount;
		FABTSM3MonthlyRoutePool RoutePool;
		FABTSM3MonthlySpatialResult SpatialResult;
		FString Failure;
		if (!BuildSource(
				Seed,
				RouteConfig,
				SpatialConfig,
				RoutePool,
				SpatialResult,
				Failure))
		{
			++SourceRejectedCount;
			AddError(FString::Printf(
				TEXT("Seed %d source rejected: %s"),
				Seed,
				*Failure));
			Oracle.Add(static_cast<uint32>(Seed));
			Oracle.Add(0);
			continue;
		}
		const double StartSeconds =
			FPlatformTime::Seconds();
		FABTSM3MonthlyPresentationResult First;
		if (!BuildPresentation(
				Seed,
				PresentationConfig,
				SpatialConfig,
				RouteConfig,
				RoutePool,
				SpatialResult,
				FABTSM3MonthlyPresentationFaultInjection(),
				First,
				Failure))
		{
			++RejectedCount;
			AddError(FString::Printf(
				TEXT("Seed %d presentation rejected (%s): %s"),
				Seed,
				FABTSM3MonthlyPresentationBuilder::
					GetRejectReasonName(
						First.RejectReason),
				*Failure));
			Oracle.Add(static_cast<uint32>(Seed));
			Oracle.Add(
				static_cast<uint64>(
					First.RejectReason));
			continue;
		}
		DurationsMS.Add(
			(FPlatformTime::Seconds() - StartSeconds)
			* 1000.0);
		FABTSM3MonthlyPresentationResult Second;
		if (!BuildPresentation(
				Seed,
				PresentationConfig,
				SpatialConfig,
				RouteConfig,
				RoutePool,
				SpatialResult,
				FABTSM3MonthlyPresentationFaultInjection(),
				Second,
				Failure)
			|| !ResultsEqual(First, Second))
		{
			++RejectedCount;
			AddError(FString::Printf(
				TEXT("Seed %d nondeterministic"),
				Seed));
			continue;
		}
		++AcceptedCount;
		CandidatePlanCount +=
			First.CandidatePresentations.Num();
		Oracle.Add(static_cast<uint32>(Seed));
		Oracle.Add(
			static_cast<uint64>(
				SpatialResult.SpatialResultHash));
		Oracle.Add(
			static_cast<uint64>(
				First.PresentationResultHash));
		Oracle.Add(
			First.CandidatePresentations.Num());
		for (const FABTSM3MonthlyCandidatePresentation&
			Candidate : First.CandidatePresentations)
		{
			MinActiveCoveragePermille = FMath::Min(
				MinActiveCoveragePermille,
				Candidate.ActiveRoleCoveragePermille);
			MaxDeepWildPermille = FMath::Max(
				MaxDeepWildPermille,
				Candidate.DeepWildPermille);
			MinThemes = FMath::Min(
				MinThemes,
				Candidate.BiomeArchetypeCount);
			MaxSingletons = FMath::Max(
				MaxSingletons,
				Candidate
					.SingleCellBiomeComponentCount);
			FVisualFragmentMetrics FragmentMetrics;
			if (!ComputeVisualFragmentMetrics(
					Candidate,
					FragmentMetrics))
			{
				AddError(FString::Printf(
					TEXT("Seed %d candidate %d fragmentation metrics invalid"),
					Seed,
					Candidate.SourceRouteCandidateId));
				FragmentMetrics.MinComponentCellCount = 0;
				FragmentMetrics.BoundaryPermille = 1000;
			}
			if (Candidate.MinVisualBiomeComponentCellCount
					!= FragmentMetrics.MinComponentCellCount
				|| Candidate.VisualBiomeBoundaryPermille
					!= FragmentMetrics.BoundaryPermille)
			{
				AddError(FString::Printf(
					TEXT("Seed %d candidate %d fragmentation metrics mismatch"),
					Seed,
					Candidate.SourceRouteCandidateId));
			}
			MinVisualComponentCellCount = FMath::Min(
				MinVisualComponentCellCount,
				FragmentMetrics.MinComponentCellCount);
			MaxVisualBoundaryPermille = FMath::Max(
				MaxVisualBoundaryPermille,
				FragmentMetrics.BoundaryPermille);
			TotalMergedLogicalSingletons +=
				Candidate
					.MergedLogicalSingletonCellCount;
			TotalMergedSmallVisualFragments +=
				Candidate
					.MergedSmallVisualFragmentCellCount;
			for (const FABTSM3MonthlyPresentationCell&
				Cell : Candidate.Cells)
			{
				ProtectedInstanceViolations +=
					Cell.bDecorationProtected
						&& Cell.MaxDecorationInstances > 0
					? 1 : 0;
			}
			Oracle.Add(
				static_cast<uint32>(
					Candidate
						.SourceRouteCandidateId));
			Oracle.Add(
				static_cast<uint64>(
					Candidate
						.SourceSpatialCandidateHash));
			Oracle.Add(
				static_cast<uint64>(
					Candidate
						.CandidatePresentationHash));
			Oracle.Add(
				Candidate.ActiveRoleCoveragePermille);
			Oracle.Add(Candidate.DeepWildPermille);
			Oracle.Add(Candidate.BiomeArchetypeCount);
			Oracle.Add(
				Candidate.VisualBeats.Num());
			Oracle.Add(
				Candidate
					.MergedLogicalSingletonCellCount);
			Oracle.Add(
				Candidate
					.SingleCellBiomeComponentCount);
			Oracle.Add(
				Candidate
					.PlannedDecorationInstanceCount);
		}
	}
	DurationsMS.Sort();
	const double P95MS = DurationsMS.IsEmpty()
		? 0.0
		: DurationsMS[
			FMath::Clamp(
				FMath::CeilToInt(
					DurationsMS.Num() * 0.95)
					- 1,
				0,
				DurationsMS.Num() - 1)];
	const double MaxMS = DurationsMS.IsEmpty()
		? 0.0
		: DurationsMS.Last();
	const uint64 OracleHash = Oracle.Get();
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M3R5][BiomeSweep] SeedManifestHash=%016llX Terminal=%d Accepted=%d Rejected=%d SourceRejected=%d CandidatePlans=%d MinActiveCoveragePermille=%d MaxDeepWildPermille=%d MinEncounterThemes=%d MergedLogicalSingletons=%d MergedSmallVisualFragmentCells=%d MaxSingletonComponents=%d MinVisualComponentCells=%d MaxVisualBoundaryPermille=%d ProtectedInstanceViolations=%d P95MS=%.3f MaxMS=%.3f OracleHash=%016llX"),
		static_cast<unsigned long long>(
			SeedManifestHash),
		TerminalCount,
		AcceptedCount,
		RejectedCount,
		SourceRejectedCount,
		CandidatePlanCount,
		MinActiveCoveragePermille,
		MaxDeepWildPermille,
		MinThemes == MAX_int32 ? 0 : MinThemes,
		TotalMergedLogicalSingletons,
		TotalMergedSmallVisualFragments,
		MaxSingletons,
		MinVisualComponentCellCount == MAX_int32
			? 0
			: MinVisualComponentCellCount,
		MaxVisualBoundaryPermille,
		ProtectedInstanceViolations,
		P95MS,
		MaxMS,
		static_cast<unsigned long long>(OracleHash));
	TestEqual(
		TEXT("Terminal 100"),
		TerminalCount,
		SweepSeedCount);
	TestEqual(
		TEXT("Accepted 100"),
		AcceptedCount,
		SweepSeedCount);
	TestEqual(
		TEXT("Frozen candidate plan count"),
		CandidatePlanCount,
		FABTSM3R5AcceptanceManifest::
			SweepCandidatePlanCount);
	TestEqual(
		TEXT("Rejected 0"),
		RejectedCount + SourceRejectedCount,
		0);
	TestEqual(
		TEXT("No singleton components"),
		MaxSingletons,
		0);
	TestEqual(
		TEXT("Frozen logical singleton repairs"),
		TotalMergedLogicalSingletons,
		FABTSM3R5AcceptanceManifest::
			SweepMergedLogicalSingletonCount);
	TestEqual(
		TEXT("Frozen small visual fragment repairs"),
		TotalMergedSmallVisualFragments,
		FABTSM3R5AcceptanceManifest::
			SweepMergedSmallVisualFragmentCellCount);
	TestTrue(
		TEXT("Visual biome minimum area"),
		MinVisualComponentCellCount
			>= FABTSM3R5AcceptanceManifest::
				MinVisualBiomeComponentCells);
	TestTrue(
		TEXT("Whole-planet boundary coarse screen"),
		MaxVisualBoundaryPermille
			<= FABTSM3R5AcceptanceManifest::
				MaxVisualBiomeBoundaryPermille);
	TestEqual(
		TEXT("No protected instance violations"),
		ProtectedInstanceViolations,
		0);
	TestTrue(
		TEXT("Planner p95 budget"),
		P95MS <= FABTSM3R5AcceptanceManifest::
			PlannerP95BudgetMS);
	TestTrue(
		TEXT("Planner max budget"),
		MaxMS <= FABTSM3R5AcceptanceManifest::
			PlannerMaxBudgetMS);
	TestEqual(
		TEXT("Frozen sweep oracle"),
		OracleHash,
		FABTSM3R5AcceptanceManifest::
			FrozenSweepOracleHash);
	return !HasAnyErrors();
}

#endif
