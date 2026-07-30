// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ABTSRuntime.h"
#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"
#include "PCG/ABTSM3MonthlySchema.h"
#include "PCG/ABTSM3R0AcceptanceManifest.h"
#include "PCG/ABTSM3R1AcceptanceManifest.h"
#include "PCG/ABTSM3TaskGraphGenerator.h"
#include "Planet/ABTSM2Planet.h"
#include "UObject/Class.h"

#include <limits>

namespace ABTSM3R1SchemaTests
{
struct FGeneratedWorld
{
	TArray<FABTSM3TaskNode> Tasks;
	TArray<FABTSM3TaskLink> Links;
	TArray<FABTSM3CellState> CellStates;
	TArray<FABTSM3CellEdgeState> EdgeStates;
	FABTSM3PCGSummary Summary;
};

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

	const auto AddNeighbour = [&Cells](
		const int32 CellA,
		const int32 CellB)
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

const TArray<FABTSM2Cell>& GetLogicalCells()
{
	static const TArray<FABTSM2Cell> Cells = BuildLogicalCells();
	return Cells;
}

bool GenerateWorld(const int32 Seed, FGeneratedWorld& OutWorld)
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

bool BuildSchema(
	const int32 Seed,
	const FGeneratedWorld& World,
	const EABTSM3GenerationMode Mode,
	FABTSM3MonthlyWorldSchema& OutSchema,
	FString& OutFailure,
	const bool bBuildObservation = true)
{
	FABTSM3MonthlySchemaConfig Config;
	Config.Mode = Mode;
	Config.bBuildObservation = bBuildObservation;
	Config.bEmitLayerLogs = false;
	return FABTSM3MonthlySchemaBuilder::Build(
		Seed,
		Config,
		World.Tasks,
		World.Links,
		World.CellStates,
		World.Summary,
		OutSchema,
		OutFailure);
}

template <typename StructType>
bool StructEqual(const StructType& A, const StructType& B)
{
	return StructType::StaticStruct()->CompareScriptStruct(
		&A,
		&B,
		PPF_None);
}

template <typename StructType>
bool StructArrayEqual(
	const TArray<StructType>& A,
	const TArray<StructType>& B)
{
	if (A.Num() != B.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < A.Num(); ++Index)
	{
		if (!StructEqual(A[Index], B[Index]))
		{
			return false;
		}
	}
	return true;
}

bool GeneratedWorldEqual(const FGeneratedWorld& A, const FGeneratedWorld& B)
{
	return StructArrayEqual(A.Tasks, B.Tasks)
		&& StructArrayEqual(A.Links, B.Links)
		&& StructArrayEqual(A.CellStates, B.CellStates)
		&& StructArrayEqual(A.EdgeStates, B.EdgeStates)
		&& StructEqual(A.Summary, B.Summary);
}

void RefreshLayoutHash(FABTSM3MonthlyWorldSchema& Schema)
{
	Schema.Identity.SchemaLayoutHash = static_cast<int64>(
		FABTSM3MonthlySchemaBuilder::ComputeLayoutHash(Schema));
}

bool ValidateSchema(
	const FABTSM3MonthlyWorldSchema& Schema,
	EABTSM3SchemaRejectReason& OutReason)
{
	FString Failure;
	return FABTSM3MonthlySchemaBuilder::Validate(
		Schema,
		OutReason,
		Failure);
}

bool BuildDisplayMonthlySchema(
	FGeneratedWorld& OutWorld,
	FABTSM3MonthlyWorldSchema& OutSchema,
	FString& OutFailure)
{
	return GenerateWorld(FABTSM3R1AcceptanceManifest::DisplaySeed, OutWorld)
		&& BuildSchema(
			FABTSM3R1AcceptanceManifest::DisplaySeed,
			OutWorld,
			EABTSM3GenerationMode::MonthlyDevelopment,
			OutSchema,
			OutFailure);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R1SchemaDefaultsAndDomainsTest,
	"ABTS.M3.Monthly.Schema.01DefaultsAndDomains",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R1SchemaDefaultsAndDomainsTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R1SchemaTests;
	(void)Parameters;

	const FABTSM3MonthlySchemaConfig Defaults;
	TestEqual(
		TEXT("R-1 defaults to the explicit compatibility mode"),
		static_cast<int32>(Defaults.Mode),
		static_cast<int32>(EABTSM3GenerationMode::CompatibilityOracle));
	TestEqual(
		TEXT("The first monthly layout policy is distinct from Policy1"),
		Defaults.MonthlyLayoutPolicyVersion,
		FABTSM3MonthlySchemaBuilder::FirstMonthlyLayoutPolicyVersion);
	TestTrue(
		TEXT("Schema observation is enabled by default"),
		Defaults.bBuildObservation);
	TestEqual(
		TEXT("Compatibility resolves Gen3/Policy1"),
		FABTSM3MonthlySchemaBuilder::ResolveLayoutPolicyVersion(Defaults),
		1);

	FABTSM3MonthlySchemaConfig MonthlyConfig = Defaults;
	MonthlyConfig.Mode = EABTSM3GenerationMode::MonthlyDevelopment;
	TestEqual(
		TEXT("Monthly development resolves the new policy identity"),
		FABTSM3MonthlySchemaBuilder::ResolveLayoutPolicyVersion(
			MonthlyConfig),
		2);

	FGeneratedWorld World;
	FABTSM3MonthlyWorldSchema Schema;
	FString Failure;
	TestTrue(
		TEXT("Display fixture builds a complete monthly schema"),
		BuildDisplayMonthlySchema(World, Schema, Failure));
	if (!Failure.IsEmpty())
	{
		AddError(FString::Printf(TEXT("Schema build failure: %s"), *Failure));
	}
	TestFalse(
		TEXT("R-1 never publishes a monthly world"),
		Schema.Quality.bMonthlyWorldAccepted);
	TestEqual(
		TEXT("R-1 quality remains explicitly NotEvaluated"),
		static_cast<int32>(Schema.Quality.RejectReason),
		static_cast<int32>(EABTSM3SchemaRejectReason::NotEvaluated));

	FGeneratedWorld InvalidWorld = World;
	InvalidWorld.Tasks[0].FlowS =
		std::numeric_limits<float>::quiet_NaN();
	FABTSM3MonthlyWorldSchema InvalidSchema;
	Failure.Reset();
	TestFalse(
		TEXT("Non-finite route data fails closed"),
		BuildSchema(
			FABTSM3R1AcceptanceManifest::DisplaySeed,
			InvalidWorld,
			EABTSM3GenerationMode::MonthlyDevelopment,
			InvalidSchema,
			Failure));
	TestEqual(
		TEXT("Non-finite input reports InvalidRange"),
		static_cast<int32>(InvalidSchema.Quality.RejectReason),
		static_cast<int32>(EABTSM3SchemaRejectReason::InvalidRange));

	const auto TestInvalidConfig = [this, &World](
		const TCHAR* Label,
		TFunctionRef<void(FABTSM3MonthlySchemaConfig&)> Mutator)
	{
		FABTSM3MonthlySchemaConfig Config;
		Mutator(Config);
		FABTSM3MonthlyWorldSchema RejectedSchema;
		FString RejectedFailure;
		TestFalse(
			Label,
			FABTSM3MonthlySchemaBuilder::Build(
				FABTSM3R1AcceptanceManifest::DisplaySeed,
				Config,
				World.Tasks,
				World.Links,
				World.CellStates,
				World.Summary,
				RejectedSchema,
				RejectedFailure));
		TestEqual(
			TEXT("Invalid config reports InvalidModeIdentity"),
			static_cast<int32>(RejectedSchema.Quality.RejectReason),
			static_cast<int32>(
				EABTSM3SchemaRejectReason::InvalidModeIdentity));
	};
	TestInvalidConfig(
		TEXT("Monthly policy above the serialized uint8 domain fails closed"),
		[](FABTSM3MonthlySchemaConfig& Config)
		{
			Config.MonthlyLayoutPolicyVersion = 256;
		});
	TestInvalidConfig(
		TEXT("Negative M6 solver identity fails closed"),
		[](FABTSM3MonthlySchemaConfig& Config)
		{
			Config.M6SolverVersion = -1;
		});
	TestInvalidConfig(
		TEXT("Negative M9 solver identity fails closed"),
		[](FABTSM3MonthlySchemaConfig& Config)
		{
			Config.M9SolverVersion = -1;
		});

	FABTSM3MonthlyWorldSchema EmptyCompatibility;
	Failure.Reset();
	TestTrue(
		TEXT("Disabled compatibility observation remains a valid empty seam"),
		BuildSchema(
			FABTSM3R1AcceptanceManifest::DisplaySeed,
			World,
			EABTSM3GenerationMode::CompatibilityOracle,
			EmptyCompatibility,
			Failure,
			false));
	TestTrue(
		TEXT("Disabled observation emits no monthly arrays"),
		EmptyCompatibility.RouteBeats.IsEmpty()
			&& EmptyCompatibility.Encounters.IsEmpty()
			&& EmptyCompatibility.Pockets.IsEmpty()
			&& EmptyCompatibility.BiomeDistricts.IsEmpty()
			&& EmptyCompatibility.PlayableEnvelopes.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R1SchemaStableEnumsTest,
	"ABTS.M3.Monthly.Schema.02StableEnums",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R1SchemaStableEnumsTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TestEqual(TEXT("Compatibility mode value is frozen"),
		static_cast<int32>(EABTSM3GenerationMode::CompatibilityOracle), 0);
	TestEqual(TEXT("Monthly mode value is frozen"),
		static_cast<int32>(EABTSM3GenerationMode::MonthlyDevelopment), 1);
	TestEqual(TEXT("Final route beat role is frozen"),
		static_cast<int32>(EABTSM3RouteBeatRole::Finale), 7);
	TestEqual(TEXT("Last encounter role is frozen"),
		static_cast<int32>(EABTSM3EncounterRole::RewardCache), 3);
	TestEqual(TEXT("Last building purpose is frozen"),
		static_cast<int32>(EABTSM3BuildingPurpose::FinaleSupport), 5);
	TestEqual(TEXT("Last pocket role is frozen"),
		static_cast<int32>(EABTSM3PocketRole::Exit), 6);
	TestEqual(TEXT("Last schema reject reason is frozen"),
		static_cast<int32>(EABTSM3SchemaRejectReason::LayoutHashMismatch),
		9);
	TestEqual(TEXT("Compatibility mode diagnostic name is stable"),
		FString(FABTSM3MonthlySchemaBuilder::GetGenerationModeName(
			EABTSM3GenerationMode::CompatibilityOracle)),
		FString(TEXT("CompatibilityOracle")));
	TestEqual(TEXT("Unknown mode is rejected by its diagnostic mapper"),
		FString(FABTSM3MonthlySchemaBuilder::GetGenerationModeName(
			static_cast<EABTSM3GenerationMode>(255))),
		FString(TEXT("Invalid")));
	TestEqual(TEXT("NotEvaluated reject name is stable"),
		FString(FABTSM3MonthlySchemaBuilder::GetRejectReasonName(
			EABTSM3SchemaRejectReason::NotEvaluated)),
		FString(TEXT("NotEvaluated")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R1SchemaReferenceIntegrityTest,
	"ABTS.M3.Monthly.Schema.03ReferenceIntegrity",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R1SchemaReferenceIntegrityTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R1SchemaTests;
	(void)Parameters;

	FGeneratedWorld World;
	FABTSM3MonthlyWorldSchema Schema;
	FString Failure;
	TestTrue(
		TEXT("Reference fixture builds"),
		BuildDisplayMonthlySchema(World, Schema, Failure));
	EABTSM3SchemaRejectReason Reason =
		EABTSM3SchemaRejectReason::None;
	TestTrue(
		TEXT("Complete schema references validate"),
		ValidateSchema(Schema, Reason));
	TestEqual(
		TEXT("Display schema observes all mission tasks as independent beats"),
		Schema.RouteBeats.Num(),
		FABTSM3R1AcceptanceManifest::DisplayRouteBeatCount);
	TestEqual(
		TEXT("Display schema observes the three compatibility encounters"),
		Schema.Encounters.Num(),
		FABTSM3R1AcceptanceManifest::DisplayEncounterCount);
	TestEqual(
		TEXT("Every compatibility encounter has seven stable pocket identities"),
		Schema.Pockets.Num(),
		FABTSM3R1AcceptanceManifest::DisplayPocketCount);
	TestEqual(
		TEXT("Compatibility observation exposes one real route-candidate domain"),
		Schema.Quality.RouteCandidateCount,
		1);
	for (const FABTSM3RouteBeatPlan& Beat : Schema.RouteBeats)
	{
		TestNotEqual(
			TEXT("Stable Beat identity is independent from array order"),
			Beat.BeatId,
			Beat.OrderIndex);
	}
	for (const FABTSM3EncounterContract& Encounter : Schema.Encounters)
	{
		TestNotEqual(
			TEXT("Stable Encounter identity is independent from array order"),
			Encounter.EncounterId,
			Encounter.OrderIndex);
	}

	FABTSM3MonthlyWorldSchema Dangling = Schema;
	Dangling.Encounters[0].TargetAnchorPocketId = 987654;
	RefreshLayoutHash(Dangling);
	TestFalse(
		TEXT("A dangling pocket reference fails closed"),
		ValidateSchema(Dangling, Reason));
	TestEqual(
		TEXT("Dangling reference has a stable reason"),
		static_cast<int32>(Reason),
		static_cast<int32>(EABTSM3SchemaRejectReason::InvalidReference));

	FABTSM3MonthlyWorldSchema WrongPocketOwner = Schema;
	WrongPocketOwner.Pockets[0].EncounterId =
		WrongPocketOwner.Encounters[1].EncounterId;
	RefreshLayoutHash(WrongPocketOwner);
	TestFalse(
		TEXT("A pocket owned by the wrong Encounter fails closed"),
		ValidateSchema(WrongPocketOwner, Reason));

	FABTSM3MonthlyWorldSchema DanglingBiome = Schema;
	DanglingBiome.RouteBeats[0].BiomeDistrictId = 987654;
	RefreshLayoutHash(DanglingBiome);
	TestFalse(
		TEXT("A dangling Beat-to-Biome reference fails closed"),
		ValidateSchema(DanglingBiome, Reason));

	FABTSM3MonthlyWorldSchema WrongBeatBiome = Schema;
	WrongBeatBiome.RouteBeats[0].BiomeDistrictId =
		WrongBeatBiome.BiomeDistricts[
			WrongBeatBiome.RouteBeats[0].BiomeDistrictId
				== WrongBeatBiome.BiomeDistricts[0].BiomeDistrictId
					? 1
					: 0].BiomeDistrictId;
	RefreshLayoutHash(WrongBeatBiome);
	TestFalse(
		TEXT("A Beat portal mislabeled with another existing Biome fails closed"),
		ValidateSchema(WrongBeatBiome, Reason));

	FABTSM3MonthlyWorldSchema WrongEnvelopeBiome = Schema;
	WrongEnvelopeBiome.PlayableEnvelopes[0].Cells[0].BiomeDistrictId =
		WrongEnvelopeBiome.BiomeDistricts[
			WrongEnvelopeBiome.PlayableEnvelopes[0].Cells[0].BiomeDistrictId
				== WrongEnvelopeBiome.BiomeDistricts[0].BiomeDistrictId
					? 1
					: 0].BiomeDistrictId;
	RefreshLayoutHash(WrongEnvelopeBiome);
	TestFalse(
		TEXT("An Envelope Cell mislabeled with another existing Biome fails closed"),
		ValidateSchema(WrongEnvelopeBiome, Reason));

	FABTSM3MonthlyWorldSchema InvalidPortal = Schema;
	InvalidPortal.RouteBeats[0].RoadPortalCellId =
		InvalidPortal.Quality.SourceCellCount;
	RefreshLayoutHash(InvalidPortal);
	TestFalse(
		TEXT("A RoadPortal outside the source Cell domain fails closed"),
		ValidateSchema(InvalidPortal, Reason));

	FABTSM3MonthlyWorldSchema InvalidProgressKey = Schema;
	InvalidProgressKey.Encounters[0].RequiredKeys.Add(
		static_cast<EABTSM3ProgressKey>(255));
	RefreshLayoutHash(InvalidProgressKey);
	TestFalse(
		TEXT("An unknown progress-key enum fails closed"),
		ValidateSchema(InvalidProgressKey, Reason));

	FABTSM3MonthlyWorldSchema InvalidBiomeArchetype = Schema;
	InvalidBiomeArchetype.BiomeDistricts[0].Archetype =
		static_cast<EABTSM3BiomeArchetype>(255);
	RefreshLayoutHash(InvalidBiomeArchetype);
	TestFalse(
		TEXT("An unknown Biome archetype fails closed"),
		ValidateSchema(InvalidBiomeArchetype, Reason));

	FABTSM3MonthlyWorldSchema EmptyMonthly = Schema;
	EmptyMonthly.RouteBeats.Reset();
	EmptyMonthly.Encounters.Reset();
	EmptyMonthly.Pockets.Reset();
	EmptyMonthly.BiomeDistricts.Reset();
	EmptyMonthly.PlayableEnvelopes.Reset();
	EmptyMonthly.Quality.BeatCount = 0;
	EmptyMonthly.Quality.EncounterCount = 0;
	EmptyMonthly.Quality.PocketCount = 0;
	EmptyMonthly.Quality.BiomeDistrictCount = 0;
	EmptyMonthly.Quality.PlayableEnvelopeCount = 0;
	EmptyMonthly.Quality.PlayableCellAssignmentCount = 0;
	EmptyMonthly.Quality.ActiveRoleCellCount = 0;
	EmptyMonthly.Quality.DeepWildCellCount = 0;
	RefreshLayoutHash(EmptyMonthly);
	TestFalse(
		TEXT("Monthly mode may not publish an empty schema"),
		ValidateSchema(EmptyMonthly, Reason));
	TestEqual(
		TEXT("Empty monthly schema fails at the mode boundary"),
		static_cast<int32>(Reason),
		static_cast<int32>(
			EABTSM3SchemaRejectReason::InvalidModeIdentity));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R1SchemaStrictOrderingTest,
	"ABTS.M3.Monthly.Schema.04StrictOrdering",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R1SchemaStrictOrderingTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R1SchemaTests;
	(void)Parameters;

	FGeneratedWorld World;
	FABTSM3MonthlyWorldSchema Baseline;
	FString Failure;
	TestTrue(
		TEXT("Ordering fixture builds"),
		BuildDisplayMonthlySchema(World, Baseline, Failure));

	FGeneratedWorld ReorderedWorld = World;
	Algo::Reverse(ReorderedWorld.Tasks);
	Algo::Reverse(ReorderedWorld.Links);
	FABTSM3MonthlyWorldSchema ReorderedSchema;
	TestTrue(
		TEXT("Builder accepts reordered source arrays"),
		BuildSchema(
			FABTSM3R1AcceptanceManifest::DisplaySeed,
			ReorderedWorld,
			EABTSM3GenerationMode::MonthlyDevelopment,
			ReorderedSchema,
			Failure));
	TestTrue(
		TEXT("Builder canonicalizes source insertion order"),
		StructEqual(Baseline, ReorderedSchema));

	EABTSM3SchemaRejectReason Reason =
		EABTSM3SchemaRejectReason::None;
	FABTSM3MonthlyWorldSchema Swapped = Baseline;
	Swapped.RouteBeats.Swap(0, 1);
	RefreshLayoutHash(Swapped);
	TestFalse(
		TEXT("Published Beat arrays may not be reordered"),
		ValidateSchema(Swapped, Reason));
	TestEqual(
		TEXT("Beat order drift has a stable reason"),
		static_cast<int32>(Reason),
		static_cast<int32>(
			EABTSM3SchemaRejectReason::NonDeterministicOrder));

	FABTSM3MonthlyWorldSchema Duplicate = Baseline;
	Duplicate.Pockets[1].PocketId = Duplicate.Pockets[0].PocketId;
	RefreshLayoutHash(Duplicate);
	TestFalse(
		TEXT("Duplicate pocket IDs fail closed"),
		ValidateSchema(Duplicate, Reason));
	TestEqual(
		TEXT("Duplicate IDs have a stable reason"),
		static_cast<int32>(Reason),
		static_cast<int32>(
			EABTSM3SchemaRejectReason::DuplicateStableId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R1SchemaDeterminismTest,
	"ABTS.M3.Monthly.Schema.05Determinism",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R1SchemaDeterminismTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R1SchemaTests;
	(void)Parameters;

	int32 PassedSeeds = 0;
	for (const int32 Seed :
		FABTSM3R1AcceptanceManifest::GetSchemaFixtureSeeds())
	{
		FGeneratedWorld World;
		const bool bGenerated = GenerateWorld(Seed, World);
		TestTrue(
			*FString::Printf(TEXT("Seed %d generates"), Seed),
			bGenerated);
		if (!bGenerated)
		{
			continue;
		}

		FABTSM3MonthlyWorldSchema First;
		FABTSM3MonthlyWorldSchema Second;
		FString FirstFailure;
		FString SecondFailure;
		const bool bFirstBuilt = BuildSchema(
			Seed,
			World,
			EABTSM3GenerationMode::MonthlyDevelopment,
			First,
			FirstFailure);
		const bool bSecondBuilt = BuildSchema(
			Seed,
			World,
			EABTSM3GenerationMode::MonthlyDevelopment,
			Second,
			SecondFailure);
		TestTrue(
			*FString::Printf(TEXT("Seed %d first schema builds"), Seed),
			bFirstBuilt);
		TestTrue(
			*FString::Printf(TEXT("Seed %d second schema builds"), Seed),
			bSecondBuilt);
		TestTrue(
			*FString::Printf(TEXT("Seed %d schema is deeply deterministic"), Seed),
			StructEqual(First, Second));
		TestEqual(
			*FString::Printf(TEXT("Seed %d failure identity is deterministic"), Seed),
			FirstFailure,
			SecondFailure);
		PassedSeeds += bFirstBuilt && bSecondBuilt
			&& StructEqual(First, Second) ? 1 : 0;
	}
	TestEqual(
		TEXT("All frozen schema fixture Seeds are deterministic"),
		PassedSeeds,
		FABTSM3R1AcceptanceManifest::GetSchemaFixtureSeeds().Num());
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R1][SchemaDeterminism] SeedManifestHash=%016llX Terminal=%d Passed=%d Failed=%d"),
		static_cast<unsigned long long>(
			FABTSM3R1AcceptanceManifest::
				ComputeSchemaFixtureSeedManifestHash()),
		FABTSM3R1AcceptanceManifest::GetSchemaFixtureSeeds().Num(),
		PassedSeeds,
		FABTSM3R1AcceptanceManifest::GetSchemaFixtureSeeds().Num()
			- PassedSeeds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R1SchemaConfigHashCoverageTest,
	"ABTS.M3.Monthly.Schema.06ConfigHashCoverage",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R1SchemaConfigHashCoverageTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	constexpr int64 SourceConfigHash = 2795535429ll;
	FABTSM3MonthlySchemaConfig Baseline;
	const uint64 BaselineHash =
		FABTSM3MonthlySchemaBuilder::ComputeConfigHash(
			Baseline,
			SourceConfigHash);
	TestNotEqual(TEXT("Schema config hash is nonzero"), BaselineHash, 0ull);

	const auto TestMutation = [this, &Baseline, BaselineHash](
		const TCHAR* Label,
		TFunctionRef<void(FABTSM3MonthlySchemaConfig&)> Mutator)
	{
		FABTSM3MonthlySchemaConfig Changed = Baseline;
		Mutator(Changed);
		TestNotEqual(
			Label,
			FABTSM3MonthlySchemaBuilder::ComputeConfigHash(
				Changed,
				2795535429ll),
			BaselineHash);
	};
	TestMutation(TEXT("Mode enters config identity"),
		[](FABTSM3MonthlySchemaConfig& C)
		{
			C.Mode = EABTSM3GenerationMode::MonthlyDevelopment;
		});
	TestMutation(TEXT("Monthly policy enters config identity"),
		[](FABTSM3MonthlySchemaConfig& C)
		{
			++C.MonthlyLayoutPolicyVersion;
		});
	TestMutation(TEXT("Route catalog enters config identity"),
		[](FABTSM3MonthlySchemaConfig& C)
		{
			C.RouteTemplateCatalogHash = 1;
		});
	TestMutation(TEXT("Encounter catalog enters config identity"),
		[](FABTSM3MonthlySchemaConfig& C)
		{
			C.EncounterTemplateCatalogHash = 1;
		});
	TestMutation(TEXT("Biome catalog enters config identity"),
		[](FABTSM3MonthlySchemaConfig& C)
		{
			C.BiomeTemplateCatalogHash = 1;
		});
	TestMutation(TEXT("M7 profile catalog enters config identity"),
		[](FABTSM3MonthlySchemaConfig& C)
		{
			C.M7ProfileCatalogHash = 1;
		});
	TestMutation(TEXT("M6 solver enters config identity"),
		[](FABTSM3MonthlySchemaConfig& C)
		{
			C.M6SolverVersion = 1;
		});
	TestMutation(TEXT("M9 solver enters config identity"),
		[](FABTSM3MonthlySchemaConfig& C)
		{
			C.M9SolverVersion = 1;
		});
	TestMutation(TEXT("Observation mode enters schema identity"),
		[](FABTSM3MonthlySchemaConfig& C)
		{
			C.bBuildObservation = false;
		});

	FABTSM3MonthlySchemaConfig LogOnly = Baseline;
	LogOnly.bEmitLayerLogs = !LogOnly.bEmitLayerLogs;
	TestEqual(
		TEXT("Diagnostic logging is excluded from config identity"),
		FABTSM3MonthlySchemaBuilder::ComputeConfigHash(
			LogOnly,
			SourceConfigHash),
		BaselineHash);
	TestNotEqual(
		TEXT("Source compatibility config identity enters schema config hash"),
		FABTSM3MonthlySchemaBuilder::ComputeConfigHash(
			Baseline,
			SourceConfigHash + 1),
		BaselineHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R1SchemaLayoutHashCoverageTest,
	"ABTS.M3.Monthly.Schema.07LayoutHashCoverage",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R1SchemaLayoutHashCoverageTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R1SchemaTests;
	(void)Parameters;

	FGeneratedWorld World;
	FABTSM3MonthlyWorldSchema Baseline;
	FString Failure;
	TestTrue(
		TEXT("Hash coverage fixture builds"),
		BuildDisplayMonthlySchema(World, Baseline, Failure));
	const uint64 BaselineHash =
		FABTSM3MonthlySchemaBuilder::ComputeLayoutHash(Baseline);

	const auto TestMutation = [this, &Baseline, BaselineHash](
		const TCHAR* Label,
		TFunctionRef<void(FABTSM3MonthlyWorldSchema&)> Mutator)
	{
		FABTSM3MonthlyWorldSchema Changed = Baseline;
		Mutator(Changed);
		TestNotEqual(
			Label,
			FABTSM3MonthlySchemaBuilder::ComputeLayoutHash(Changed),
			BaselineHash);
	};
	TestMutation(TEXT("Route Beat fields enter LayoutHash"),
		[](FABTSM3MonthlyWorldSchema& S)
		{
			S.RouteBeats[0].WitnessId = 1;
		});
	TestMutation(TEXT("Encounter fields enter LayoutHash"),
		[](FABTSM3MonthlyWorldSchema& S)
		{
			S.Encounters[0].BallisticWitnessId = 1;
		});
	TestMutation(TEXT("Resolved profile identity enters LayoutHash"),
		[](FABTSM3MonthlyWorldSchema& S)
		{
			S.Encounters[0].ResolvedM7ProfileId =
				FName(TEXT("Profile.Test"));
		});
	TestMutation(TEXT("Pocket fields enter LayoutHash"),
		[](FABTSM3MonthlyWorldSchema& S)
		{
			S.Pockets[0].Resolution = EABTSM3SchemaResolution::Finalized;
		});
	TestMutation(TEXT("Biome fields enter LayoutHash"),
		[](FABTSM3MonthlyWorldSchema& S)
		{
			S.BiomeDistricts[0].bBackground =
				!S.BiomeDistricts[0].bBackground;
		});
	TestMutation(TEXT("Envelope roles enter LayoutHash"),
		[](FABTSM3MonthlyWorldSchema& S)
		{
			S.PlayableEnvelopes[0].Cells[0].ActiveRoleMask ^=
				static_cast<int32>(EABTSM3ActiveRole::Resource);
		});
	TestMutation(TEXT("Deterministic quality scores enter LayoutHash"),
		[](FABTSM3MonthlyWorldSchema& S)
		{
			++S.Quality.OverallScore;
		});
	TestMutation(TEXT("Schema config identity enters LayoutHash"),
		[](FABTSM3MonthlyWorldSchema& S)
		{
			++S.Identity.SchemaConfigHash;
		});

	FABTSM3MonthlyWorldSchema SelfHashChanged = Baseline;
	SelfHashChanged.Identity.SchemaLayoutHash = 0;
	TestEqual(
		TEXT("LayoutHash excludes its own storage field"),
		FABTSM3MonthlySchemaBuilder::ComputeLayoutHash(SelfHashChanged),
		BaselineHash);

	FABTSM3MonthlySchemaDebugData DebugA;
	FABTSM3MonthlySchemaDebugData DebugB;
	FABTSM3MonthlySchemaBuilder::BuildDebugData(Baseline, DebugA);
	DebugB = DebugA;
	DebugB.TargetCellIds.Add(987654);
	TestEqual(
		TEXT("Editor-only debug data is outside LayoutHash"),
		FABTSM3MonthlySchemaBuilder::ComputeLayoutHash(Baseline),
		BaselineHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R1SchemaCompatibilityOracleTest,
	"ABTS.M3.Monthly.Schema.08CompatibilityOracle21",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R1SchemaCompatibilityOracleTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3R1SchemaTests;
	(void)Parameters;

	FString ManifestFailure;
	TestTrue(
		TEXT("M3R-1 acceptance manifest self-validates"),
		FABTSM3R1AcceptanceManifest::Validate(ManifestFailure));
	if (!ManifestFailure.IsEmpty())
	{
		AddError(FString::Printf(
			TEXT("M3R-1 manifest failure: %s ComputedManifestHash=%016llX ComputedCompatibilitySeedHash=%016llX ComputedFixtureSeedHash=%016llX ComputedOracleHash=%016llX"),
			*ManifestFailure,
			static_cast<unsigned long long>(
				FABTSM3R1AcceptanceManifest::ComputeManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R1AcceptanceManifest::
					ComputeCompatibilitySeedManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R1AcceptanceManifest::
					ComputeSchemaFixtureSeedManifestHash()),
			static_cast<unsigned long long>(
				FABTSM3R1AcceptanceManifest::
					ComputeCompatibilityOracleHash())));
	}
	TestEqual(
		TEXT("R-1 explicitly depends on the frozen R-0 manifest"),
		FABTSM3R1AcceptanceManifest::RequiredR0ManifestHash,
		FABTSM3R0AcceptanceManifest::ComputeManifestHash());

	int32 PassedSeeds = 0;
	const TConstArrayView<FABTSM3R1CompatibilityOracle> Oracles =
		FABTSM3R1AcceptanceManifest::GetCompatibilityOracles();
	for (int32 SeedIndex = 0; SeedIndex < Oracles.Num(); ++SeedIndex)
	{
		const FABTSM3R1CompatibilityOracle& Oracle =
			Oracles[SeedIndex];
		FGeneratedWorld Legacy;
		FGeneratedWorld ExplicitCompatibility;
		const bool bLegacyGenerated =
			GenerateWorld(Oracle.Seed, Legacy);
		const bool bExplicitGenerated =
			GenerateWorld(Oracle.Seed, ExplicitCompatibility);
		TestTrue(
			*FString::Printf(
				TEXT("Seed %d legacy generation succeeds"),
				Oracle.Seed),
			bLegacyGenerated);
		TestTrue(
			*FString::Printf(
				TEXT("Seed %d explicit compatibility source succeeds"),
				Oracle.Seed),
			bExplicitGenerated);
		if (!bLegacyGenerated || !bExplicitGenerated)
		{
			continue;
		}
		const FGeneratedWorld BeforeSchema = ExplicitCompatibility;
		FABTSM3MonthlyWorldSchema CompatibilitySchema;
		FString SchemaFailure;
		const bool bSchemaBuilt = BuildSchema(
			Oracle.Seed,
			ExplicitCompatibility,
			EABTSM3GenerationMode::CompatibilityOracle,
			CompatibilitySchema,
			SchemaFailure);
		TestTrue(
			*FString::Printf(
				TEXT("Seed %d compatibility schema builds"),
				Oracle.Seed),
			bSchemaBuilt);
		TestTrue(
			*FString::Printf(
				TEXT("Seed %d schema projection does not mutate its source"),
				Oracle.Seed),
			GeneratedWorldEqual(
				BeforeSchema,
				ExplicitCompatibility));
		TestTrue(
			*FString::Printf(
				TEXT("Seed %d explicit seam deeply equals legacy output"),
				Oracle.Seed),
			GeneratedWorldEqual(Legacy, ExplicitCompatibility));
		TestEqual(
			*FString::Printf(
				TEXT("Seed %d preserves ConfigHash"),
				Oracle.Seed),
			Legacy.Summary.ConfigHash,
			Oracle.ConfigHash);
		TestEqual(
			*FString::Printf(
				TEXT("Seed %d preserves LayoutHash"),
				Oracle.Seed),
			Legacy.Summary.LayoutHash,
			Oracle.LayoutHash);
		TestEqual(
			*FString::Printf(
				TEXT("Seed %d preserves accepted Attempt"),
				Oracle.Seed),
			Legacy.Summary.AttemptIndex,
			Oracle.AttemptIndex);
		const uint64 SnapshotHash =
			FABTSM3R1AcceptanceManifest::
				ComputeCompatibilitySnapshotHash(
					Legacy.Tasks,
					Legacy.Links,
					Legacy.CellStates,
					Legacy.EdgeStates,
					Legacy.Summary);
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M3R1][CompatibilitySnapshot] Seed=%d SnapshotHash=%016llX FrozenHash=%016llX"),
			Oracle.Seed,
			static_cast<unsigned long long>(SnapshotHash),
			static_cast<unsigned long long>(Oracle.SnapshotHash));
		TestEqual(
			*FString::Printf(
				TEXT("Seed %d preserves the full compatibility snapshot"),
				Oracle.Seed),
			SnapshotHash,
			Oracle.SnapshotHash);

		FABTSM3MonthlyWorldSchema EmptySchema;
		const bool bEmptySchemaBuilt = BuildSchema(
			Oracle.Seed,
			ExplicitCompatibility,
			EABTSM3GenerationMode::CompatibilityOracle,
			EmptySchema,
			SchemaFailure,
			false);
		TestTrue(
			*FString::Printf(
				TEXT("Seed %d supports disabled observation"),
				Oracle.Seed),
			bEmptySchemaBuilt);
		TestTrue(
			*FString::Printf(
				TEXT("Seed %d disabled observation is empty"),
				Oracle.Seed),
			EmptySchema.RouteBeats.IsEmpty()
				&& EmptySchema.Encounters.IsEmpty()
				&& EmptySchema.Pockets.IsEmpty()
				&& EmptySchema.BiomeDistricts.IsEmpty()
				&& EmptySchema.PlayableEnvelopes.IsEmpty());
		PassedSeeds += bSchemaBuilt
			&& bEmptySchemaBuilt
			&& GeneratedWorldEqual(Legacy, ExplicitCompatibility)
			&& Legacy.Summary.ConfigHash == Oracle.ConfigHash
			&& Legacy.Summary.LayoutHash == Oracle.LayoutHash
			&& Legacy.Summary.AttemptIndex == Oracle.AttemptIndex
			&& SnapshotHash == Oracle.SnapshotHash
				? 1
				: 0;

		if (Oracle.Seed == FABTSM3R1AcceptanceManifest::DisplaySeed)
		{
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][M3R1][DisplaySchemaIdentity] Seed=%d SchemaConfigHash=%016llX SchemaLayoutHash=%016llX Beats=%d Encounters=%d Pockets=%d Biomes=%d Envelopes=%d"),
				Oracle.Seed,
				static_cast<unsigned long long>(
					static_cast<uint64>(
						CompatibilitySchema.Identity.SchemaConfigHash)),
				static_cast<unsigned long long>(
					static_cast<uint64>(
						CompatibilitySchema.Identity.SchemaLayoutHash)),
				CompatibilitySchema.RouteBeats.Num(),
				CompatibilitySchema.Encounters.Num(),
				CompatibilitySchema.Pockets.Num(),
				CompatibilitySchema.BiomeDistricts.Num(),
				CompatibilitySchema.PlayableEnvelopes.Num());
			TestEqual(
				TEXT("Display SchemaConfigHash is frozen"),
				static_cast<uint64>(
					CompatibilitySchema.Identity.SchemaConfigHash),
				FABTSM3R1AcceptanceManifest::
					FrozenDisplaySchemaConfigHash);
			TestEqual(
				TEXT("Display SchemaLayoutHash is frozen"),
				static_cast<uint64>(
					CompatibilitySchema.Identity.SchemaLayoutHash),
				FABTSM3R1AcceptanceManifest::
					FrozenDisplaySchemaLayoutHash);
		}
	}
	TestEqual(
		TEXT("All compatibility Oracle Seeds pass"),
		PassedSeeds,
		Oracles.Num());
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R1][SchemaCertification] ManifestHash=%016llX SeedManifestHash=%016llX OracleHash=%016llX Terminal=%d Passed=%d Failed=%d"),
		static_cast<unsigned long long>(
			FABTSM3R1AcceptanceManifest::ComputeManifestHash()),
		static_cast<unsigned long long>(
			FABTSM3R1AcceptanceManifest::
				ComputeCompatibilitySeedManifestHash()),
		static_cast<unsigned long long>(
			FABTSM3R1AcceptanceManifest::
				ComputeCompatibilityOracleHash()),
		Oracles.Num(),
		PassedSeeds,
		Oracles.Num() - PassedSeeds);
	return true;
}

#endif
