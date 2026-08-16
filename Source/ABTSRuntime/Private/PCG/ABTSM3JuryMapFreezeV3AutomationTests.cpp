// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ABTSRuntime.h"
#include "Building/ABTSM73BuildingFreezeV3.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Math/RotationMatrix.h"
#include "Misc/AutomationTest.h"
#include "PCG/ABTSM3JuryMapFreezeV3.h"
#include "PCG/ABTSM3MonthlySatellitePreview.h"
#include "Terrain/ABTSM3Planet.h"

namespace ABTSM3JuryMapFreezeV3Tests
{
uint64 ComputeProposedV3RegistrationHash(
	const FABTSM3JuryMapFreezeV3Result& Result)
{
	constexpr uint64 OffsetBasis = 1469598103934665603ull;
	const auto AddUInt64 = [](uint64& Hash, const uint64 Value)
	{
		for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
		{
			Hash ^= (Value >> (ByteIndex * 8)) & 0xffull;
			Hash *= 1099511628211ull;
		}
	};
	TArray<FABTSM73BuildingFreezeV3Descriptor> Descriptors;
	uint64 CatalogHash = 0;
	FString Failure;
	if (!FABTSM73BuildingFreezeV3::DeriveAndValidateCatalog(
			Descriptors, CatalogHash, Failure)
		|| Descriptors.Num() != Result.Placements.Num()
		|| CatalogHash != Result.HandoffContract.PlacementCatalogHash)
	{
		return 0;
	}
	uint64 Hash = OffsetBasis;
	AddUInt64(Hash, Result.LayoutHash);
	AddUInt64(Hash, CatalogHash);
	for (int32 Index = 0; Index < Descriptors.Num(); ++Index)
	{
		const FABTSM73BuildingFreezeV3Descriptor& Descriptor =
			Descriptors[Index];
		const FABTSJuryDemoFixedSixV3Envelope& Envelope =
			Result.Placements[Index].Site.V3Envelope;
		AddUInt64(Hash, Descriptor.DescriptorHash);
		AddUInt64(Hash, Descriptor.StaticGeometryHash);
		AddUInt64(Hash, Descriptor.ProductionHash);
		AddUInt64(Hash, Descriptor.SourceDeviceAssemblyHash);
		AddUInt64(Hash, Envelope.PlacementHash);
		if (Descriptor.PhysicsAssemblyHash != 0)
		{
			AddUInt64(Hash, Descriptor.PhysicsAssemblyHash);
		}
	}
	return Hash;
}

class FScopedContinuousSurfaceExactOracle
{
public:
	FScopedContinuousSurfaceExactOracle()
	{
		Variable = IConsoleManager::Get().FindConsoleVariable(
			TEXT("abts.M3.ContinuousSurfaceExactOracle"));
		if (Variable != nullptr)
		{
			PreviousValue = Variable->GetInt();
			Variable->Set(1, ECVF_SetByCode);
		}
	}

	~FScopedContinuousSurfaceExactOracle()
	{
		if (Variable != nullptr)
		{
			Variable->Set(PreviousValue, ECVF_SetByCode);
		}
	}

	bool IsAvailable() const { return Variable != nullptr; }

private:
	IConsoleVariable* Variable = nullptr;
	int32 PreviousValue = 0;
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
	FScopedContinuousSurfaceExactOracle ExactOracle;
	TestTrue(TEXT("Continuous-surface exact oracle CVar is registered"),
		ExactOracle.IsAvailable());
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
	// Exact IsUsable() sealing is owned by Integration because M3 can publish a
	// canonical refreeze candidate before the shared production hash is moved.
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
		const bool bIsSatellite = Placement.Site.V3Envelope.SurfaceKind
			== EABTSJuryDemoFixedSixSurfaceKind::Satellite;
		if (bIsSatellite)
		{
			TestTrue(TEXT("Satellite E1 footprint is square in Site X/Y"),
				FMath::IsNearlyEqual(BoundsSize.X, BoundsSize.Y, 1.0e-4));
		}
		else
		{
			TestTrue(TEXT("Frozen non-square primary footprint has Site Y as long axis"),
				BoundsSize.Y > BoundsSize.X);
		}
		TestTrue(TEXT("Road/slingshot attack corridor faces Site X"),
			FVector::DotProduct(
				Placement.AttackCorridorWorldDirection.GetSafeNormal(),
				Placement.Site.WorldTransform.GetUnitAxis(EAxis::X))
				>= 1.0 - 1.0e-4);
		TestTrue(TEXT("Attack corridor is perpendicular to footprint long axis"),
			Placement.AttackCorridorLongAxisAbsDot <= 1.0e-3);
		if (bIsSatellite)
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
	const FABTSM3MonthlySatellitePreviewCandidate* FrozenSatelliteCandidate =
		FABTSM3MonthlySatellitePreviewBuilder::FindCandidate(
			Planet->GetMonthlySatellitePreviewResult(),
			First.SourceCandidateId);
	TestNotNull(TEXT("V3 E1 joins the selected final-surface satellite preview"),
		FrozenSatelliteCandidate);
	if (FrozenSatelliteCandidate != nullptr
		&& First.Placements.IsValidIndex(4))
	{
		TestEqual(TEXT("Final V3 target authority is the frozen E1 building modules"),
			FrozenSatelliteCandidate->TargetAuthority,
			EABTSM3MonthlySatelliteTargetAuthority::FrozenE1BuildingModules);
		TestTrue(TEXT("Final V3 target is production-trajectory certified"),
			FrozenSatelliteCandidate->bProductionTargetTrajectoryCertified
				&& FrozenSatelliteCandidate->ProductionTargetTrajectoryHash != 0);
		TestTrue(TEXT("V3 E1 support center is the final-surface satellite center"),
			First.Placements[4].Site.V3Envelope.SupportCenterWorldCM.Equals(
				FrozenSatelliteCandidate->SatelliteCenterWorld,
				0.1));
		TestTrue(TEXT("Preview Site carrier is the exact frozen E1 placement"),
			First.Placements[4].Site.WorldTransform.Equals(
				FrozenSatelliteCandidate->SatelliteSiteWorldTransform,
				0.001f));
		FABTSM73BuildingFreezeV3Descriptor E1Descriptor;
		FString E1Failure;
		TestTrue(TEXT("Published frozen E1 descriptor resolves for target oracle"),
			FABTSM73BuildingFreezeV3::DeriveAndValidate(
				EABTSM73BeamDemoBuilding::E1ColumnBreak,
				E1Descriptor,
				E1Failure));
		const FABTSM73BeamD1BrickBinding* TargetModule =
			E1Descriptor.Bricks.FindByPredicate(
				[FrozenSatelliteCandidate](const FABTSM73BeamD1BrickBinding& Brick)
				{
					return Brick.BrickId
						== FrozenSatelliteCandidate->ProductionTargetModuleId;
				});
		TestNotNull(TEXT("Frozen module target resolves from the public descriptor"),
			TargetModule);
		if (TargetModule != nullptr)
		{
			TestTrue(TEXT("Frozen module world target follows the published brick transform"),
				FrozenSatelliteCandidate->E5TargetWorldTransform.Equals(
					TargetModule->LocalTransform
						* First.Placements[4].Site.WorldTransform,
					0.001f));
			TestTrue(TEXT("Frozen module extent follows the published brick dimensions"),
				FrozenSatelliteCandidate->E5TargetHalfExtentCM.Equals(
					TargetModule->BrickSpec.DimensionsCM * 0.5f,
					0.001f));
		}
	}
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
	const uint64 ProposedRegistrationHash =
		ComputeProposedV3RegistrationHash(First);
	TestNotEqual(TEXT("Proposed V3 registration hash is computable"),
		ProposedRegistrationHash, 0ull);
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M3Jury][PendingIntegrationSeal] SatelliteCandidate=%016llX SatelliteResult=%016llX Layout=%016llX Registration=%016llX Catalog=%016llX SharedMutation=0"),
		static_cast<unsigned long long>(
			First.SourceSatellitePreviewCandidateHash),
		static_cast<unsigned long long>(
			First.SourceSatellitePreviewResultHash),
		static_cast<unsigned long long>(First.LayoutHash),
		static_cast<unsigned long long>(ProposedRegistrationHash),
		static_cast<unsigned long long>(
			First.HandoffContract.PlacementCatalogHash));
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
