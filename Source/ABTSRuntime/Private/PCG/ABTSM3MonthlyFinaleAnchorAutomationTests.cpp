// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "PCG/ABTSM3MonthlyFinaleAnchor.h"
#include "PCG/ABTSM3MonthlySlingshotField.h"
#include "Planet/ABTSM2Planet.h"
#include "Terrain/ABTSM3Planet.h"

namespace ABTSM3R52FinaleAnchorTests
{
constexpr int32 DisplaySeed = 312503;
constexpr float ReferencePlanetRadiusCM = 10000.0f;

TArray<FABTSM2Cell> BuildLogicalCells()
{
	AABTSM2Planet::FUnitSphereMesh Mesh;
	AABTSM2Planet::BuildUnitIcosphere(5, Mesh);
	TArray<FABTSM2Cell> Cells;
	Cells.SetNum(Mesh.Vertices.Num());
	for (int32 CellId = 0; CellId < Mesh.Vertices.Num(); ++CellId)
	{
		Cells[CellId].UnitCenter = Mesh.Vertices[CellId];
	}
	const auto AddNeighbor = [&Cells](const int32 A, const int32 B)
	{
		Cells[A].NeighborCellIds.AddUnique(B);
		Cells[B].NeighborCellIds.AddUnique(A);
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

bool BuildSpatialFixture(
	const int32 Seed,
	FABTSM3MonthlySpatialResult& OutSpatial,
	FString& OutFailure)
{
	FABTSM3MonthlyRouteConfig RouteConfig;
	RouteConfig.bEmitRouteLogs = false;
	FABTSM3MonthlyRoutePool RoutePool;
	if (!FABTSM3MonthlyRouteBuilder::Build(
			Seed,
			RouteConfig,
			GetLogicalCells(),
			ReferencePlanetRadiusCM,
			FABTSM3MonthlyRoadContext(),
			RoutePool,
			OutFailure))
	{
		return false;
	}
	FABTSM3MonthlyEncounterSpatialConfig SpatialConfig;
	SpatialConfig.bEmitSpatialLogs = false;
	return FABTSM3MonthlyEncounterBuilder::Build(
		Seed,
		SpatialConfig,
		RouteConfig,
		GetLogicalCells(),
		ReferencePlanetRadiusCM,
		RoutePool,
		FABTSM3MonthlySpatialFaultInjection(),
		OutSpatial,
		OutFailure);
}

bool BuildJoinedFixture(
	const int32 Seed,
	FABTSM3MonthlySpatialResult& OutSpatial,
	FABTSM3MonthlyFinaleAnchorPlanResult& OutPlan,
	FABTSM3MonthlySlingshotFieldResult& OutFields,
	FString& OutFailure)
{
	if (!BuildSpatialFixture(Seed, OutSpatial, OutFailure))
	{
		return false;
	}
	FABTSM3MonthlyFinaleAnchorConfig FinaleConfig;
	FinaleConfig.bEmitFinaleAnchorLogs = false;
	if (!FABTSM3MonthlyFinaleAnchorBuilder::Build(
			Seed,
			FinaleConfig,
			GetLogicalCells(),
			OutSpatial,
			OutPlan,
			OutFailure))
	{
		return false;
	}
	FABTSM3MonthlySlingshotFieldConfig FieldConfig;
	FieldConfig.bEmitSlingshotFieldLogs = false;
	return FABTSM3MonthlySlingshotFieldBuilder::Build(
		Seed,
		FieldConfig,
		GetLogicalCells(),
		ReferencePlanetRadiusCM,
		OutSpatial,
		OutPlan,
		OutFields,
		OutFailure);
}

class FSphereSurface final : public IABTSM3MonthlyFinaleAnchorSurface
{
public:
	explicit FSphereSurface(const TArray<FABTSM2Cell>& InCells)
		: Cells(InCells)
	{
	}

	virtual FVector GetPrimaryCenterWorld() const override
	{
		return FVector(1200.0, -3400.0, 560.0);
	}

	virtual float GetPrimaryRadiusCM() const override
	{
		return ReferencePlanetRadiusCM;
	}

	virtual bool QuerySurface(
		const FVector& UnitDirection,
		FABTSM3MonthlyFinaleSurfaceSample& OutSample) const override
	{
		const FVector Direction = UnitDirection.GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			return false;
		}
		OutSample.WorldLocation = GetPrimaryCenterWorld()
			+ Direction * GetPrimaryRadiusCM();
		OutSample.WorldNormal = Direction;
		OutSample.NearestCellId = INDEX_NONE;
		double BestDot = -2.0;
		for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
		{
			const double Dot = FVector::DotProduct(
				Direction,
				Cells[CellId].UnitCenter);
			if (Dot > BestDot)
			{
				BestDot = Dot;
				OutSample.NearestCellId = CellId;
			}
		}
		return OutSample.NearestCellId != INDEX_NONE;
	}

private:
	const TArray<FABTSM2Cell>& Cells;
};

class FScopedTestWorld
{
public:
	FScopedTestWorld()
	{
		const UWorld::InitializationValues Values =
			UWorld::InitializationValues()
				.InitializeScenes(false)
				.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(false)
				.CreateNavigation(false)
				.CreateAISystem(false)
				.ShouldSimulatePhysics(false)
				.EnableTraceCollision(false)
				.SetTransactional(false)
				.CreateFXSystem(false);
		World = UWorld::CreateWorld(
			EWorldType::Game,
			false,
			TEXT("ABTSM3R52FinaleAnchorTestWorld"),
			nullptr,
			true,
			ERHIFeatureLevel::Num,
			&Values);
		if (World != nullptr && GEngine != nullptr)
		{
			WorldContext = &GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext->SetCurrentWorld(World);
		}
	}

	~FScopedTestWorld()
	{
		if (World != nullptr)
		{
			if (GEngine != nullptr && WorldContext != nullptr)
			{
				GEngine->DestroyWorldContext(World);
				WorldContext = nullptr;
			}
			World->DestroyWorld(false);
			World->RemoveFromRoot();
		}
	}

	UWorld* Get() const { return World; }

private:
	UWorld* World = nullptr;
	FWorldContext* WorldContext = nullptr;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R52FinaleAnchorPlanAndClearanceTest,
	"ABTS.M3.Monthly.FinaleAnchor.01PlanAndSlotClearance",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R52FinaleAnchorPlanAndClearanceTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	using namespace ABTSM3R52FinaleAnchorTests;
	FABTSM3MonthlySpatialResult Spatial;
	FABTSM3MonthlyFinaleAnchorPlanResult Plan;
	FABTSM3MonthlySlingshotFieldResult Fields;
	FString Failure;
	TestTrue(TEXT("Joined fixture builds"), BuildJoinedFixture(
		DisplaySeed,
		Spatial,
		Plan,
		Fields,
		Failure));
	if (!Plan.bPlanResultValid || !Fields.bSlingshotFieldResultValid)
	{
		AddError(FString::Printf(TEXT("Fixture invalid: %s"), *Failure));
		return false;
	}
	TestFalse(TEXT("Plan never accepts monthly world"), Plan.bMonthlyWorldAccepted);
	TestEqual(TEXT("Every spatial candidate has a finale plan"),
		Plan.RetainedCandidates.Num(), Spatial.RetainedCandidates.Num());
	TestEqual(TEXT("Slot result records the exact finale plan"),
		Fields.SourceFinaleAnchorPlanResultHash, Plan.ResultHash);
	for (const FABTSM3MonthlyFinaleAnchorPlanCandidate& Candidate :
		Plan.RetainedCandidates)
	{
		const FABTSM3MonthlySpatialCandidate* Source =
			Spatial.RetainedCandidates.FindByPredicate(
				[&Candidate](const FABTSM3MonthlySpatialCandidate& Value)
				{
					return Value.SourceRouteCandidateId
						== Candidate.SourceRouteCandidateId;
				});
		TestNotNull(TEXT("Plan rejoins its source candidate"), Source);
		if (Source == nullptr)
		{
			continue;
		}
		TestEqual(TEXT("Exact route terminal is retained"),
			Candidate.RoadTerminalCellId,
			Source->RecomputedRoute.OrderedRoadCellIds.Last());
		TestTrue(TEXT("Terminal window has enough candidates"),
			Candidate.TerminalCandidateCellIds.Num() >= 3);
		bool bClearanceSorted = true;
		for (int32 Index = 1;
			Index < Candidate.ClearanceCellIds.Num();
			++Index)
		{
			bClearanceSorted &=
				Candidate.ClearanceCellIds[Index - 1]
					< Candidate.ClearanceCellIds[Index];
		}
		TestTrue(TEXT("Clearance is canonical ascending unique order"),
			bClearanceSorted);
		TestEqual(TEXT("Plan candidate hash is canonical"),
			static_cast<uint64>(Candidate.CandidateHash),
			FABTSM3MonthlyFinaleAnchorBuilder::ComputeCandidateHash(Candidate));

		const FABTSM3MonthlySlingshotFieldCandidate* FieldCandidate =
			Fields.RetainedCandidates.FindByPredicate(
				[&Candidate](const FABTSM3MonthlySlingshotFieldCandidate& Value)
				{
					return Value.SourceRouteCandidateId
						== Candidate.SourceRouteCandidateId;
				});
		TestNotNull(TEXT("Slot candidate rejoins finale plan"), FieldCandidate);
		if (FieldCandidate == nullptr)
		{
			continue;
		}
		TestEqual(TEXT("Slot candidate stores finale candidate hash"),
			FieldCandidate->SourceFinaleAnchorPlanCandidateHash,
			Candidate.CandidateHash);
		TSet<int32> Clearance;
		for (const int32 CellId : Candidate.ClearanceCellIds)
		{
			Clearance.Add(CellId);
		}
		for (const FABTSM3MonthlySlingshotField& Field : FieldCandidate->Fields)
		{
			for (const int32 SlotCellId : Field.SlotCellIds)
			{
				TestFalse(TEXT("Ordinary slot never occupies finale clearance"),
					Clearance.Contains(SlotCellId));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R52FinaleAnchorSurfacePreviewTest,
	"ABTS.M3.Monthly.FinaleAnchor.02SurfacePreviewAndFailureClosure",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R52FinaleAnchorSurfacePreviewTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	using namespace ABTSM3R52FinaleAnchorTests;
	FABTSM3MonthlySpatialResult Spatial;
	FABTSM3MonthlyFinaleAnchorPlanResult Plan;
	FABTSM3MonthlySlingshotFieldResult Fields;
	FString Failure;
	if (!BuildJoinedFixture(
			DisplaySeed,
			Spatial,
			Plan,
			Fields,
			Failure))
	{
		AddError(Failure);
		return false;
	}
	FABTSM3MonthlyFinaleAnchorConfig Config;
	Config.bEmitFinaleAnchorLogs = false;
	const FSphereSurface Surface(GetLogicalCells());
	for (const FABTSM3MonthlySpatialCandidate& Candidate :
		Spatial.RetainedCandidates)
	{
		FABTSM3MonthlyFinaleAnchorPreview Preview;
		TestTrue(TEXT("Explicit candidate resolves on the real surface interface"),
			FABTSM3MonthlyFinaleAnchorBuilder::BuildPreview(
				Candidate.SourceRouteCandidateId,
				Config,
				GetLogicalCells(),
				Spatial,
				Plan,
				Surface,
				Preview,
				Failure));
		if (!Preview.bPreviewValid)
		{
			continue;
		}
		const FABTSM3MonthlyFinaleAnchorPlanCandidate* PlanCandidate =
			FABTSM3MonthlyFinaleAnchorBuilder::FindCandidate(
				Plan,
				Candidate.SourceRouteCandidateId);
		TestNotNull(TEXT("Resolved preview rejoins plan"), PlanCandidate);
		TestTrue(TEXT("Chosen anchor stays in terminal window"),
			PlanCandidate != nullptr
				&& PlanCandidate->TerminalCandidateCellIds.Contains(
					Preview.AnchorCellId));
		TestTrue(TEXT("Forward is normalized"), Preview.ForwardWorld.IsNormalized());
		TestTrue(TEXT("Right is normalized"), Preview.RightWorld.IsNormalized());
		TestTrue(TEXT("Up is normalized"), Preview.UpWorld.IsNormalized());
		TestTrue(TEXT("Frame basis is orthogonal"),
			FMath::Abs(FVector::DotProduct(
				Preview.ForwardWorld,
				Preview.RightWorld)) < 0.001
			&& FMath::Abs(FVector::DotProduct(
				Preview.ForwardWorld,
				Preview.UpWorld)) < 0.001
			&& FMath::Abs(FVector::DotProduct(
				Preview.RightWorld,
				Preview.UpWorld)) < 0.001);
		TestTrue(TEXT("Frame basis is right handed"),
			FVector::CrossProduct(
				Preview.ForwardWorld,
				Preview.RightWorld).Equals(Preview.UpWorld, 0.001));
		TestTrue(TEXT("Slot separation stays within grounding tolerance"),
			FMath::IsNearlyEqual(
				Preview.ActualSlotSeparationCM,
				Config.SlotSeparationCM,
				2.0f));
		TestEqual(TEXT("Preview hash is canonical"),
			static_cast<uint64>(Preview.PreviewHash),
			FABTSM3MonthlyFinaleAnchorBuilder::ComputePreviewHash(Preview));
	}

	FABTSM3MonthlyFinaleAnchorPreview RejectedPreview;
	TestFalse(TEXT("Implicit candidate selection is forbidden"),
		FABTSM3MonthlyFinaleAnchorBuilder::BuildPreview(
			INDEX_NONE,
			Config,
			GetLogicalCells(),
			Spatial,
			Plan,
			Surface,
			RejectedPreview,
			Failure));
	FABTSM3MonthlyFinaleAnchorPlanResult Tampered = Plan;
	Tampered.RetainedCandidates[0].ClearanceCellIds.Pop();
	EABTSM3MonthlyFinaleAnchorRejectReason Reason =
		EABTSM3MonthlyFinaleAnchorRejectReason::None;
	TestFalse(TEXT("Whole-struct tamper fails validation"),
		FABTSM3MonthlyFinaleAnchorBuilder::Validate(
			Config,
			GetLogicalCells(),
			Spatial,
			Tampered,
			Reason,
			Failure));
	TestEqual(TEXT("Tamper reports hash mismatch"),
		Reason,
		EABTSM3MonthlyFinaleAnchorRejectReason::HashMismatch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R52FinaleAnchorPlanetJoinTest,
	"ABTS.M3.Monthly.FinaleAnchor.03PlanetContinuousSurfaceJoin",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R52FinaleAnchorPlanetJoinTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	using namespace ABTSM3R52FinaleAnchorTests;
	FScopedTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("Transient world exists"), World);
	if (World == nullptr)
	{
		return false;
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM3Planet* Planet = World->SpawnActor<AABTSM3Planet>(
		AABTSM3Planet::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	TestNotNull(TEXT("M3 Planet spawns"), Planet);
	if (Planet == nullptr)
	{
		return false;
	}
	Planet->SurfaceSubdivision = 1;
	Planet->InstancesPerCell = 0;
	TestTrue(TEXT("Planet rebuilds joined finale plan"), Planet->RebuildPlanet());
	FString Failure;
	TestTrue(TEXT("Planet plan whole-struct validates"),
		Planet->ValidateMonthlyFinaleAnchorPlanResult(Failure));
	TestTrue(TEXT("Planet slot result validates with the plan"),
		Planet->ValidateMonthlySlingshotFieldResult(Failure));
	const FABTSM3MonthlyFinaleAnchorPlanResult FirstPlan =
		Planet->GetMonthlyFinaleAnchorPlanResult();
	if (FirstPlan.RetainedCandidates.IsEmpty())
	{
		AddError(TEXT("Planet produced no finale anchor plans"));
		return false;
	}
	const int32 CandidateId =
		FirstPlan.RetainedCandidates[0].SourceRouteCandidateId;
	FABTSM3MonthlyFinaleAnchorPreview FirstPreview;
	TestTrue(TEXT("Planet resolves preview on its final TerrainVisualField"),
		Planet->TryBuildMonthlyFinaleAnchorPreview(
			CandidateId,
			FirstPreview,
			Failure));
	TestTrue(TEXT("Planet preview is non-authoritative"),
		FirstPreview.bPreviewValid
			&& !FirstPreview.bMonthlyWorldAccepted);
	TestTrue(TEXT("Repeated Planet rebuild succeeds"), Planet->RebuildPlanet());
	TestTrue(TEXT("Plan is deterministic across rebuild"),
		FABTSM3MonthlyFinaleAnchorPlanResult::StaticStruct()
			->CompareScriptStruct(
				&FirstPlan,
				&Planet->GetMonthlyFinaleAnchorPlanResult(),
				PPF_None));
	FABTSM3MonthlyFinaleAnchorPreview SecondPreview;
	TestTrue(TEXT("Repeated surface preview resolves"),
		Planet->TryBuildMonthlyFinaleAnchorPreview(
			CandidateId,
			SecondPreview,
			Failure));
	TestTrue(TEXT("Surface preview is deterministic across rebuild"),
		FABTSM3MonthlyFinaleAnchorPreview::StaticStruct()
			->CompareScriptStruct(&FirstPreview, &SecondPreview, PPF_None));
	return true;
}

#endif
