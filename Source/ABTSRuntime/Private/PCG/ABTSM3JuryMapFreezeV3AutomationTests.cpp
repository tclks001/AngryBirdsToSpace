// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Math/RotationMatrix.h"
#include "Misc/AutomationTest.h"
#include "PCG/ABTSM3JuryMapFreezeV3.h"
#include "Terrain/ABTSM3Planet.h"

namespace ABTSM3JuryMapFreezeV3Tests
{
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
			TEXT("ABTSM3JuryMapFreezeV3TestWorld"),
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

AABTSM3Planet* SpawnPlanet(FAutomationTestBase& Test, UWorld& World)
{
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM3Planet* Planet = World.SpawnActor<AABTSM3Planet>(
		AABTSM3Planet::StaticClass(),
		FTransform::Identity,
		Parameters);
	Test.TestNotNull(TEXT("M3 MapFreezeV3 planet spawns"), Planet);
	if (Planet != nullptr)
	{
		Planet->SurfaceSubdivision = 1;
		Planet->InstancesPerCell = 0;
	}
	return Planet;
}

bool BuildFixture(
	FAutomationTestBase& Test,
	FScopedTestWorld& ScopedWorld,
	AABTSM3Planet*& OutPlanet)
{
	UWorld* World = ScopedWorld.Get();
	Test.TestNotNull(TEXT("Transient MapFreezeV3 world exists"), World);
	if (World == nullptr)
	{
		return false;
	}
	OutPlanet = SpawnPlanet(Test, *World);
	return OutPlanet != nullptr
		&& Test.TestTrue(TEXT("M3 MapFreezeV3 world rebuilds"),
			OutPlanet->RebuildPlanet());
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3JuryMapFreezeV3DeterminismTest,
	"ABTS.M3.Jury.MapFreezeV3.01DeterminismAndRoadFacing",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3JuryMapFreezeV3DeterminismTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	using namespace ABTSM3JuryMapFreezeV3Tests;
	FScopedTestWorld ScopedWorld;
	AABTSM3Planet* Planet = nullptr;
	if (!BuildFixture(*this, ScopedWorld, Planet))
	{
		return false;
	}

	const FABTSM3JuryMapFreezeV3Result First =
		Planet->GetJuryMapFreezeV3Result();
	TestTrue(TEXT("MapFreezeV3 is ready"), First.bMapFreezeReady);
	TestEqual(TEXT("MapFreezeV3 reject reason is None"),
		First.RejectReason,
		EABTSM3JuryMapFreezeV3RejectReason::None);
	TestEqual(TEXT("Exactly six V3 sites are frozen"),
		First.Placements.Num(), 6);
	TestTrue(TEXT("V3 handoff is structurally usable"),
		First.HandoffContract.IsStructurallyUsableV3());
	TestTrue(TEXT("The exact published V3 handoff is the production contract"),
		First.HandoffContract.IsUsable());
	TestEqual(TEXT("Layout hash is canonical"),
		First.LayoutHash,
		FABTSM3JuryMapFreezeV3Builder::ComputeLayoutHash(First));

	static const FName ExpectedEntries[] = {
		TEXT("E2DropTrigger"),
		TEXT("E3SlideRelease"),
		TEXT("E4TipOver"),
		TEXT("E5SeamRelease"),
		TEXT("E1ColumnBreak"),
		TEXT("E6TipOver")};
	int32 PrimaryCount = 0;
	int32 SatelliteCount = 0;
	for (int32 Index = 0; Index < First.Placements.Num(); ++Index)
	{
		const FABTSM3JuryMapFreezeV3Placement& Placement =
			First.Placements[Index];
		TestEqual(TEXT("V3 slot mapping is exact"),
			Placement.Site.ManifestEntryId,
			ExpectedEntries[Index]);
		const FVector BoundsSize = Placement.Site.LocalBounds.GetSize();
		TestTrue(TEXT("Frozen non-square footprint has Site Y as long axis"),
			BoundsSize.Y > BoundsSize.X);
		TestTrue(TEXT("Road/slingshot attack corridor faces Site X"),
			FVector::DotProduct(
				Placement.AttackCorridorWorldDirection.GetSafeNormal(),
				Placement.Site.WorldTransform.GetUnitAxis(EAxis::X))
				>= 1.0 - 1.0e-4);
		TestTrue(TEXT("Attack corridor is perpendicular to footprint long axis"),
			Placement.AttackCorridorLongAxisAbsDot <= 1.0e-3);
		if (Placement.Site.V3Envelope.SurfaceKind
			== EABTSJuryDemoFixedSixSurfaceKind::Satellite)
		{
			++SatelliteCount;
			TestEqual(TEXT("Only slot four is satellite E1"), Index, 4);
			TestTrue(TEXT("Satellite E1 owns no primary pad cells"),
				Placement.ReservedPadCellIds.IsEmpty());
		}
		else
		{
			++PrimaryCount;
			TestFalse(TEXT("Every primary building reserves a pad"),
				Placement.ReservedPadCellIds.IsEmpty());
		}
	}
	TestEqual(TEXT("Five sites are on the primary planet"), PrimaryCount, 5);
	TestEqual(TEXT("One site is on the satellite"), SatelliteCount, 1);
	int32 TerrainPadCount = 0;
	int32 PhysicalDecorOverlapCount = 0;
	int32 DynamicDecorOverlapCount = 0;
	float MaxPadResidualCM = 0.0f;
	FABTSM3JuryTerrainGradeDiagnostics GradeDiagnostics;
	FString TerrainFailure;
	TestTrue(TEXT("Production terrain conforms to the five primary V3 sites"),
		Planet->ValidateJuryFixedSixProductionClearance(
			TerrainPadCount,
			PhysicalDecorOverlapCount,
			DynamicDecorOverlapCount,
			MaxPadResidualCM,
			GradeDiagnostics,
			TerrainFailure));
	TestEqual(TEXT("Only five primary V3 sites grade the primary terrain"),
		TerrainPadCount,
		FABTSM3JuryMapFreezeV3Builder::ExpectedPrimarySiteCount);
	TestTrue(TEXT("Every V3 building base is flush with its terrain plane"),
		MaxPadResidualCM <= 0.5f);
	TestEqual(TEXT("No decor intersects V3 physical bounds"),
		PhysicalDecorOverlapCount, 0);
	TestEqual(TEXT("No decor intersects V3 effect bounds"),
		DynamicDecorOverlapCount, 0);

	FString ValidationFailure;
	TestTrue(TEXT("Whole-result canonical validation succeeds"),
		Planet->ValidateJuryMapFreezeV3Result(ValidationFailure));
	TestTrue(TEXT("Independent repeated rebuild succeeds"),
		Planet->RebuildPlanet());
	const FABTSM3JuryMapFreezeV3Result& Second =
		Planet->GetJuryMapFreezeV3Result();
	TestEqual(TEXT("Repeated run keeps the exact LayoutHash"),
		Second.LayoutHash, First.LayoutHash);
	for (int32 Index = 0;
		Index < FMath::Min(First.Placements.Num(), Second.Placements.Num());
		++Index)
	{
		TestEqual(TEXT("Repeated run keeps every PlacementHash"),
			Second.Placements[Index].Site.V3Envelope.PlacementHash,
			First.Placements[Index].Site.V3Envelope.PlacementHash);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3JuryMapFreezeV3FailureClosureTest,
	"ABTS.M3.Jury.MapFreezeV3.02AxisSurfaceSlotBoundsFailureClosure",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3JuryMapFreezeV3FailureClosureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	using namespace ABTSM3JuryMapFreezeV3Tests;
	FScopedTestWorld ScopedWorld;
	AABTSM3Planet* Planet = nullptr;
	if (!BuildFixture(*this, ScopedWorld, Planet))
	{
		return false;
	}
	const FABTSM3JuryMapFreezeV3Result& Source =
		Planet->GetJuryMapFreezeV3Result();

	auto ValidateTamper = [this, Planet](
		const TCHAR* Label,
		const FABTSM3JuryMapFreezeV3Result& Tampered)
	{
		EABTSM3JuryMapFreezeV3RejectReason Reason =
			EABTSM3JuryMapFreezeV3RejectReason::None;
		FString Failure;
		TestFalse(Label,
			Planet->ValidateJuryMapFreezeV3Snapshot(
				Tampered, Reason, Failure));
		return Reason;
	};

	FABTSM3JuryMapFreezeV3Result DoubleRotated = Source;
	FABTSM3JuryMapFreezeV3Placement& Rotated = DoubleRotated.Placements[0];
	const FVector SiteZ = Rotated.Site.WorldTransform.GetUnitAxis(EAxis::Z);
	const FQuat ExtraQuarterTurn(SiteZ, HALF_PI);
	Rotated.Site.WorldTransform.SetRotation(
		ExtraQuarterTurn * Rotated.Site.WorldTransform.GetRotation());
	Rotated.HorizontalLongAxisWorld =
		Rotated.Site.WorldTransform.GetUnitAxis(EAxis::Y);
	Rotated.AttackCorridorLongAxisAbsDot = FMath::Abs(FVector::DotProduct(
		Rotated.AttackCorridorWorldDirection.GetSafeNormal(),
		Rotated.HorizontalLongAxisWorld.GetSafeNormal()));
	Rotated.Site.V3Envelope.PlacementHash =
		FABTSM3JuryMapFreezeV3Builder::ComputePlacementHash(Rotated);
	DoubleRotated.HandoffContract.Sites[0] = Rotated.Site;
	DoubleRotated.LayoutHash =
		FABTSM3JuryMapFreezeV3Builder::ComputeLayoutHash(DoubleRotated);
	DoubleRotated.HandoffContract.LayoutHash = DoubleRotated.LayoutHash;
	TestEqual(TEXT("Double rotation is rejected by corridor/long-axis oracle"),
		ValidateTamper(TEXT("A second 90-degree rotation fails closed"),
			DoubleRotated),
		EABTSM3JuryMapFreezeV3RejectReason::AttackCorridorOrientationInvalid);

	FABTSM3JuryMapFreezeV3Result WrongSurface = Source;
	WrongSurface.Placements[0].Site.V3Envelope.SurfaceKind =
		EABTSJuryDemoFixedSixSurfaceKind::Satellite;
	WrongSurface.HandoffContract.Sites[0] = WrongSurface.Placements[0].Site;
	TestNotEqual(TEXT("Wrong primary surface fails closed"),
		ValidateTamper(TEXT("Wrong surface is rejected"), WrongSurface),
		EABTSM3JuryMapFreezeV3RejectReason::None);

	FABTSM3JuryMapFreezeV3Result WrongE1Slot = Source;
	WrongE1Slot.Placements[4].Site.ManifestEntryId = TEXT("E5SeamRelease");
	WrongE1Slot.HandoffContract.Sites[4] = WrongE1Slot.Placements[4].Site;
	TestNotEqual(TEXT("Wrong E1 slot fails closed"),
		ValidateTamper(TEXT("Wrong E1 slot is rejected"), WrongE1Slot),
		EABTSM3JuryMapFreezeV3RejectReason::None);

	FABTSM3JuryMapFreezeV3Result WrongBounds = Source;
	WrongBounds.Placements[2].Site.LocalBounds.Max.Y += 1.0;
	WrongBounds.HandoffContract.Sites[2] = WrongBounds.Placements[2].Site;
	TestNotEqual(TEXT("Bounds drift fails closed"),
		ValidateTamper(TEXT("Wrong bounds are rejected"), WrongBounds),
		EABTSM3JuryMapFreezeV3RejectReason::None);
	return true;
}

#endif
