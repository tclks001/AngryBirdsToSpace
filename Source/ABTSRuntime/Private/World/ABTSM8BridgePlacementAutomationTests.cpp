// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "World/ABTSM8RecoveryBridgeSystem.h"

#include "Building/ABTSM7BuildingTypes.h"
#include "Engine/World.h"
#include "Inventory/ABTSInventoryTypes.h"
#include "Misc/AutomationTest.h"
#include "Terrain/ABTSM3Planet.h"
#include "Terrain/ABTSM3RiverVisualBuilder.h"
#include "World/ABTSM8BridgeActors.h"

namespace
{
class FScopedABTSM8BridgeGeometryWorld
{
public:
	FScopedABTSM8BridgeGeometryWorld()
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
			TEXT("ABTSM8BridgeGeometryWorld"),
			nullptr,
			true,
			ERHIFeatureLevel::Num,
			&Values);
	}

	~FScopedABTSM8BridgeGeometryWorld()
	{
		if (World != nullptr)
		{
			World->DestroyWorld(false);
			World->RemoveFromRoot();
		}
	}

	UWorld* Get() const { return World; }

private:
	UWorld* World = nullptr;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM8SemanticWaterBridgeGeometryTest,
	"ABTS.M8.BridgePlacement.SemanticWaterGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM8SemanticWaterBridgeGeometryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FScopedABTSM8BridgeGeometryWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("M8 bridge geometry test World is created"), World);
	if (World == nullptr) return false;

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM3Planet* Planet = World->SpawnActor<AABTSM3Planet>(
		AABTSM3Planet::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	TestNotNull(TEXT("M8 bridge geometry test Planet spawns"), Planet);
	if (Planet == nullptr) return false;
	Planet->SurfaceSubdivision = 1;
	Planet->InstancesPerCell = 0;
	TestTrue(TEXT("M8 bridge geometry test Planet rebuilds"), Planet->RebuildPlanet());
	if (!Planet->IsPlanetReady()) return false;

	int32 WaterEdgeCount = 0;
	int32 ResolvedCount = 0;
	int32 FlowSegmentCount = 0;
	int32 BarrierSegmentCount = 0;
	int32 CertifiedSiteCount = 0;
	for (const FABTSM3CellEdgeState& EdgeState : Planet->GetGeneratedEdgeStates())
	{
		if (EdgeState.Water == EABTSM3WaterEdgeType::None)
		{
			continue;
		}
		++WaterEdgeCount;

		TArray<FABTSM3CellEdgeState> SingleEdge;
		SingleEdge.Add(EdgeState);
		TArray<FABTSM3RiverVisualSegment> Segments;
		FABTSM3RiverVisualBuilder::BuildSegments(
			Planet->LogicalCells,
			SingleEdge,
			Planet->StreamVisualHalfWidthCM,
			Planet->ShallowRiverVisualHalfWidthCM,
			Planet->DeepRiverVisualHalfWidthCM,
			Segments);
		if (Segments.Num() != 1)
		{
			AddError(FString::Printf(
				TEXT("Water edge (%d,%d) has no unique visual segment"),
				EdgeState.Key.CellA,
				EdgeState.Key.CellB));
			continue;
		}

		const FABTSM3RiverVisualSegment& Segment = Segments[0];
		const FVector AimDirection = (Segment.StartUnit + Segment.EndUnit).GetSafeNormal();
		FABTSM8BridgePlacementGeometry Geometry;
		if (!AABTSM8RecoveryBridgeSystem::ResolveSemanticBridgeGeometry(
			*Planet,
			EdgeState,
			AimDirection,
			Geometry))
		{
			AddError(FString::Printf(
				TEXT("Water edge (%d,%d) semantic geometry does not resolve"),
				EdgeState.Key.CellA,
				EdgeState.Key.CellB));
			continue;
		}
		++ResolvedCount;
		FlowSegmentCount += Geometry.bBarrierSegment ? 0 : 1;
		BarrierSegmentCount += Geometry.bBarrierSegment ? 1 : 0;
		CertifiedSiteCount += Geometry.bCertifiedBridgeSite ? 1 : 0;

		const FVector Up = Geometry.BridgeTransform.GetUnitAxis(EAxis::Z);
		const FVector AcrossRiver = Geometry.BridgeTransform.GetUnitAxis(EAxis::X);
		const FVector RiverTangent = FVector::VectorPlaneProject(
			Segment.EndUnit - Segment.StartUnit,
			Up).GetSafeNormal();
		TestTrue(
			FString::Printf(
				TEXT("Bridge crosses visual water edge (%d,%d) at right angles"),
				EdgeState.Key.CellA,
				EdgeState.Key.CellB),
			FMath::Abs(FVector::DotProduct(AcrossRiver, RiverTangent)) < 0.01f);
		TestEqual(
			FString::Printf(
				TEXT("Aim distance uses M3 visible segment (%d,%d)"),
				EdgeState.Key.CellA,
				EdgeState.Key.CellB),
			Geometry.AimDistanceCM,
			FABTSM3RiverVisualBuilder::GetDistanceToSegmentCM(
				AimDirection,
				Segment,
				Planet->GetPlanetRadiusCM()),
			0.01f);
		TestEqual(
			FString::Printf(
				TEXT("Barrier length follows visible water segment (%d,%d)"),
				EdgeState.Key.CellA,
				EdgeState.Key.CellB),
			Geometry.RiverSegmentLengthCM,
			static_cast<float>(FMath::Max(
				100.0f,
				Planet->GetSurfaceRadiusAtDirection(AimDirection)
					* FMath::Acos(FMath::Clamp(
						FVector::DotProduct(Segment.StartUnit, Segment.EndUnit),
						-1.0f,
						1.0f)))),
			0.1f);
	}

	TestTrue(TEXT("Generated world contains semantic water edges"), WaterEdgeCount > 0);
	TestEqual(TEXT("Every visible water edge resolves for bridge placement"), ResolvedCount, WaterEdgeCount);
	TestTrue(TEXT("Generated world exercises flow centerline semantics"), FlowSegmentCount > 0);
	TestTrue(TEXT("Generated world exercises barrier dual-edge semantics"), BarrierSegmentCount > 0);
	TestTrue(TEXT("Certified BridgeSite remains available as a recommended crossing"), CertifiedSiteCount > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM8WaterBarrierCoverageTest,
	"ABTS.M8.BridgePlacement.WaterBarrierCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM8WaterBarrierCoverageTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FScopedABTSM8BridgeGeometryWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("M8 barrier coverage test World is created"), World);
	if (World == nullptr) return false;

	const float VisibleWaterHalfWidthCM = 190.0f;
	const float BankSafetyMarginCM = 35.0f;
	const float BarrierHalfWidthCM =
		AABTSM8RecoveryBridgeSystem::ComputeWaterBarrierHalfWidthCM(
			VisibleWaterHalfWidthCM,
			BankSafetyMarginCM);
	TestEqual(
		TEXT("Barrier reaches from the centerline beyond the visible bank"),
		BarrierHalfWidthCM,
		225.0f,
		0.01f);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM8WaterBarrierActor* CenterBarrier =
		World->SpawnActor<AABTSM8WaterBarrierActor>(
			AABTSM8WaterBarrierActor::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	TestNotNull(TEXT("Center water barrier spawns"), CenterBarrier);
	if (CenterBarrier == nullptr) return false;

	const FVector BarrierHalfExtentCM(500.0f, BarrierHalfWidthCM, 325.0f);
	const FTransform CenterBarrierTransform(
		FQuat::Identity,
		FVector(0.0f, 0.0f, 325.0f));
	CenterBarrier->InitializeBarrier(
		FABTSM3CellEdgeKey(1, 2),
		CenterBarrierTransform,
		BarrierHalfExtentCM);
	TestEqual(
		TEXT("Spawned wall uses the full visual-water-plus-margin half width"),
		CenterBarrier->GetBaseHalfExtentCM().Y,
		225.0,
		0.01);

	const FTransform BridgeTransform(
		FQuat(FVector::UpVector, HALF_PI),
		FVector(0.0f, 0.0f, 85.0f));
	const FVector BridgeDimensionsCM(600.0f, 240.0f, 40.0f);
	const float PassageSideClearanceCM = 80.0f;
	TestTrue(
		TEXT("Bridge carves the center barrier"),
		CenterBarrier->OpenPassage(
			BridgeTransform,
			BridgeDimensionsCM,
			PassageSideClearanceCM));
	TestEqual(
		TEXT("Centered bridge leaves two blocking wall pieces"),
		CenterBarrier->GetBlockingPieceCount(),
		2);
	TestFalse(
		TEXT("Bridge center is fully open"),
		CenterBarrier->IsBlockingAtLocalAlongDistance(0.0f));
	TestFalse(
		TEXT("Bridge deck edge plus bird clearance is open"),
		CenterBarrier->IsBlockingAtLocalAlongDistance(199.0f));
	TestTrue(
		TEXT("Water remains blocked immediately outside bridge clearance"),
		CenterBarrier->IsBlockingAtLocalAlongDistance(201.0f));

	AABTSM8WaterBarrierActor* AdjacentBarrier =
		World->SpawnActor<AABTSM8WaterBarrierActor>(
			AABTSM8WaterBarrierActor::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	TestNotNull(TEXT("Adjacent water barrier spawns"), AdjacentBarrier);
	if (AdjacentBarrier == nullptr) return false;
	AdjacentBarrier->InitializeBarrier(
		FABTSM3CellEdgeKey(2, 3),
		FTransform(FQuat::Identity, FVector(350.0f, 0.0f, 325.0f)),
		BarrierHalfExtentCM);
	TestTrue(
		TEXT("Bridge also carves an overlapping neighboring wall segment"),
		AdjacentBarrier->OpenPassage(
			BridgeTransform,
			BridgeDimensionsCM,
			PassageSideClearanceCM));
	TestFalse(
		TEXT("Neighbor overlap above the bridge is open"),
		AdjacentBarrier->IsBlockingAtLocalAlongDistance(-300.0f));
	TestTrue(
		TEXT("Neighbor water outside the bridge remains blocked"),
		AdjacentBarrier->IsBlockingAtLocalAlongDistance(0.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM8RecoveredMaterialMappingTest,
	"ABTS.M8.Recovery.BuildingMaterialMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM8RecoveredMaterialMappingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	struct FExpectedMapping
	{
		EABTSM7BuildingMaterial Material;
		EABTSItemId Item;
	};
	const FExpectedMapping ExpectedMappings[] = {
		{EABTSM7BuildingMaterial::Wood, EABTSItemId::Wood},
		{EABTSM7BuildingMaterial::Stone, EABTSItemId::Stone},
		{EABTSM7BuildingMaterial::Iron, EABTSItemId::MetalParts},
		{EABTSM7BuildingMaterial::Glass, EABTSItemId::Glass},
		{EABTSM7BuildingMaterial::Crystal, EABTSItemId::CrystalCore},
	};
	for (const FExpectedMapping& Expected : ExpectedMappings)
	{
		EABTSItemId Item = EABTSItemId::Branch;
		TestTrue(
			TEXT("Known building material maps to a shared inventory item"),
			AABTSM8RecoveryBridgeSystem::TryMapRecoveredMaterialToItem(
				Expected.Material,
				Item));
		TestEqual(TEXT("Recovered item mapping is stable"), Item, Expected.Item);
	}

	EABTSItemId UnknownItem = EABTSItemId::Branch;
	TestFalse(
		TEXT("Unknown building material fails closed"),
		AABTSM8RecoveryBridgeSystem::TryMapRecoveredMaterialToItem(
			static_cast<EABTSM7BuildingMaterial>(255),
			UnknownItem));
	return true;
}

#endif
