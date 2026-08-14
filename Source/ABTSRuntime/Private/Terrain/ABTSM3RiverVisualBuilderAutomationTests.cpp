// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Terrain/ABTSM3RiverVisualBuilder.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Terrain/ABTSM3Planet.h"

namespace
{
class FScopedABTSM3RiverSmoothingWorld
{
public:
	FScopedABTSM3RiverSmoothingWorld()
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
			TEXT("ABTSM3RiverSmoothingWorld"),
			nullptr,
			true,
			ERHIFeatureLevel::Num,
			&Values);
	}

	~FScopedABTSM3RiverSmoothingWorld()
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
	FABTSM3BarrierRiverGreatCircleSmoothingTest,
	"ABTS.M3.RiverVisual.BarrierGreatCircleSmoothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM3BarrierRiverGreatCircleSmoothingTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FScopedABTSM3RiverSmoothingWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("M3 river smoothing test World is created"), World);
	if (World == nullptr) return false;

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM3Planet* Planet = World->SpawnActor<AABTSM3Planet>(
		AABTSM3Planet::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	TestNotNull(TEXT("M3 river smoothing test Planet spawns"), Planet);
	if (Planet == nullptr) return false;
	Planet->SurfaceSubdivision = 1;
	Planet->InstancesPerCell = 0;
	TestTrue(TEXT("M3 river smoothing test Planet rebuilds"), Planet->RebuildPlanet());
	if (!Planet->IsPlanetReady()) return false;

	TArray<FABTSM3RiverVisualSegment> Segments;
	FABTSM3RiverVisualBuilder::BuildSegments(
		Planet->LogicalCells,
		Planet->GetGeneratedEdgeStates(),
		Planet->StreamVisualHalfWidthCM,
		Planet->ShallowRiverVisualHalfWidthCM,
		Planet->DeepRiverVisualHalfWidthCM,
		Segments);

	TMap<FABTSM3CellEdgeKey, int32> SegmentIndexByEdge;
	for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
	{
		const FABTSM3CellEdgeKey Key = Segments[SegmentIndex].SourceEdgeKey;
		TestFalse(
			*FString::Printf(
				TEXT("Water edge (%d,%d) maps to one visual segment"),
				Key.CellA,
				Key.CellB),
			SegmentIndexByEdge.Contains(Key));
		SegmentIndexByEdge.Add(Key, SegmentIndex);
	}

	int32 WaterEdgeCount = 0;
	int32 BarrierEdgeCount = 0;
	int32 ProjectedBarrierCount = 0;
	for (const FABTSM3CellEdgeState& Edge : Planet->GetGeneratedEdgeStates())
	{
		if (Edge.Water == EABTSM3WaterEdgeType::None) continue;
		++WaterEdgeCount;
		const int32* SegmentIndex = SegmentIndexByEdge.Find(Edge.Key);
		TestNotNull(
			*FString::Printf(
				TEXT("Water edge (%d,%d) retains its source mapping"),
				Edge.Key.CellA,
				Edge.Key.CellB),
			SegmentIndex);
		if (SegmentIndex == nullptr) continue;

		const FABTSM3RiverVisualSegment& Segment = Segments[*SegmentIndex];
		const bool bBarrierEdge =
			Edge.DownstreamCellId == INDEX_NONE || Edge.bBlocksOnFoot;
		if (!bBarrierEdge)
		{
			TestFalse(
				TEXT("Natural downstream flow is not projected onto the blocking cut"),
				Segment.bBarrierCenterlineProjected);
			continue;
		}

		++BarrierEdgeCount;
		const FVector PlaneNormal = Edge.WaterBarrierPlaneNormal.GetSafeNormal();
		TestFalse(TEXT("Blocking water edge records its great-circle plane"), PlaneNormal.IsNearlyZero());
		TestTrue(TEXT("Blocking water edge uses projected centerline geometry"), Segment.bBarrierCenterlineProjected);
		if (!Segment.bBarrierCenterlineProjected || PlaneNormal.IsNearlyZero()) continue;
		++ProjectedBarrierCount;

		TestTrue(
			TEXT("Projected barrier start lies on the ideal great circle"),
			FMath::Abs(FVector::DotProduct(Segment.StartUnit, PlaneNormal)) < 1.0e-4f);
		TestTrue(
			TEXT("Projected barrier end lies on the ideal great circle"),
			FMath::Abs(FVector::DotProduct(Segment.EndUnit, PlaneNormal)) < 1.0e-4f);

		const FVector MidDirection =
			(Segment.StartUnit + Segment.EndUnit).GetSafeNormal();
		const FVector SegmentTangent = FVector::VectorPlaneProject(
			Segment.EndUnit - Segment.StartUnit,
			MidDirection).GetSafeNormal();
		const FVector GreatCircleTangent = FVector::CrossProduct(
			PlaneNormal,
			MidDirection).GetSafeNormal();
		TestTrue(
			TEXT("Projected barrier tangent follows the low-frequency great circle"),
			FMath::Abs(FVector::DotProduct(SegmentTangent, GreatCircleTangent)) > 0.9999f);

		TArray<FABTSM3CellEdgeState> SingleEdge;
		SingleEdge.Add(Edge);
		TArray<FABTSM3RiverVisualSegment> SingleEdgeSegments;
		FABTSM3RiverVisualBuilder::BuildSegments(
			Planet->LogicalCells,
			SingleEdge,
			Planet->StreamVisualHalfWidthCM,
			Planet->ShallowRiverVisualHalfWidthCM,
			Planet->DeepRiverVisualHalfWidthCM,
			SingleEdgeSegments);
		TestEqual(TEXT("Single-edge semantic consumer receives one segment"), SingleEdgeSegments.Num(), 1);
		if (SingleEdgeSegments.Num() == 1)
		{
			TestTrue(
				TEXT("Single-edge consumer receives the same smoothed start"),
				SingleEdgeSegments[0].StartUnit.Equals(Segment.StartUnit, 1.0e-6f));
			TestTrue(
				TEXT("Single-edge consumer receives the same smoothed end"),
				SingleEdgeSegments[0].EndUnit.Equals(Segment.EndUnit, 1.0e-6f));
		}
	}

	TestTrue(TEXT("Generated world contains water edges"), WaterEdgeCount > 0);
	TestTrue(TEXT("Generated world contains blocking river edges"), BarrierEdgeCount > 0);
	TestEqual(
		TEXT("Every blocking river edge is projected onto the shared great circle"),
		ProjectedBarrierCount,
		BarrierEdgeCount);
	TestEqual(
		TEXT("Every water edge retains a unique visual segment mapping"),
		SegmentIndexByEdge.Num(),
		WaterEdgeCount);
	return true;
}

#endif
