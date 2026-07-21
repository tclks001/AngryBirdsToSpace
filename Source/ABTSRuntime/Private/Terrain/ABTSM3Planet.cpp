// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/ABTSM3Planet.h"

#include "ABTSRuntime.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "PCG/ABTSM3TaskGraphGenerator.h"
#include "ProceduralMeshComponent.h"
#include "Terrain/ABTSM3TerrainVisualField.h"
#include "Terrain/ABTSM3TerrainMaterialBridge.h"

AABTSM3Planet::AABTSM3Planet()
{
	ForestHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("ForestHISM"));
	ForestHISM->SetupAttachment(ContinuousSurface);
	ForestHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ForestHISM->SetMobility(EComponentMobility::Movable);

	RockHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("RockHISM"));
	RockHISM->SetupAttachment(ContinuousSurface);
	RockHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RockHISM->SetMobility(EComponentMobility::Movable);
}

bool AABTSM3Planet::RebuildPlanet()
{
	if (!AABTSM2Planet::RebuildPlanet() || !GenerateLogicalTerrain()) return false;
	TerrainVisualField = MakeUnique<FABTSM3TerrainVisualField>();
	TerrainVisualField->Initialize(PlanetRadiusCM, MacroHeightScaleCM, TaskWaterDepthCM, TerrainBlendWidthCM, LogicalCells, GeneratedCellStates);
	BuildM3ContinuousSurface();
	bool bMaterialReady = false;
	bool bPresentationReady = true;
	if (TerrainMaterial)
	{
		TerrainMaterialBridge = NewObject<UABTSM3TerrainMaterialBridge>(this);
		bMaterialReady = TerrainMaterialBridge->Initialize(ContinuousSurface, TerrainMaterial, GetPlanetCenterWorld(), PlanetRadiusCM, TerrainBlendWidthCM, LogicalCells, GeneratedCellStates, *TerrainVisualField);
		if (!bMaterialReady)
		{
			bPresentationReady = false;
			UE_LOG(LogABTSRuntime, Error, TEXT("[ABTS][M3] Terrain material bridge failed. Check M3 LUT parameter names and texture types."));
		}
	}
	BuildDecorInstances();
	BuildBuildingSpawnSites();

	int32 RoadCells = 0;
	int32 WaterCells = 0;
	for (const FABTSM3CellState& State : GeneratedCellStates)
	{
		RoadCells += State.bRoad ? 1 : 0;
		WaterCells += State.bWater ? 1 : 0;
	}
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M3] Ready=%d Seed=%d Tasks=%d Cells=%d RoadCells=%d WaterCells=%d Buildings=%d ForestInstances=%d RockInstances=%d MaterialAssigned=%d MaterialReady=%d"),
		bPresentationReady ? 1 : 0, WorldSeed, GeneratedTasks.Num(), GeneratedCellStates.Num(), RoadCells, WaterCells, BuildingSpawnSites.Num(), ForestHISM->GetInstanceCount(), RockHISM->GetInstanceCount(), TerrainMaterial ? 1 : 0, bMaterialReady ? 1 : 0);
	return bPresentationReady;
}

bool AABTSM3Planet::GenerateLogicalTerrain()
{
	FABTSM3TaskGraphGenerator Generator;
	return Generator.Generate(WorldSeed, LogicalCells, GeneratedTasks, GeneratedCellStates);
}

float AABTSM3Planet::GetSurfaceRadiusAtDirection(const FVector& UnitDirection) const
{
	return TerrainVisualField && TerrainVisualField->IsReady()
		? TerrainVisualField->GetSurfaceRadius(UnitDirection)
		: Super::GetSurfaceRadiusAtDirection(UnitDirection);
}

FVector AABTSM3Planet::GetSurfaceNormalAtDirection(const FVector& UnitDirection) const
{
	return TerrainVisualField && TerrainVisualField->IsReady()
		? TerrainVisualField->GetSurfaceNormal(UnitDirection)
		: Super::GetSurfaceNormalAtDirection(UnitDirection);
}

int32 AABTSM3Planet::FindNearestCell(const FVector& UnitDirection) const
{
	return TerrainVisualField && TerrainVisualField->IsReady() ? TerrainVisualField->FindNearestCell(UnitDirection) : INDEX_NONE;
}

bool AABTSM3Planet::QuerySurface(
	const FVector& UnitDirection,
	FVector& OutWorldPosition,
	FVector& OutWorldNormal,
	float& OutSurfaceRadius,
	int32& OutCellId) const
{
	if (!TerrainVisualField || !TerrainVisualField->IsReady() || UnitDirection.IsNearlyZero()) return false;
	const FVector Direction = UnitDirection.GetSafeNormal();
	OutSurfaceRadius = TerrainVisualField->GetSurfaceRadius(Direction);
	OutWorldPosition = GetPlanetCenterWorld() + Direction * OutSurfaceRadius;
	OutWorldNormal = TerrainVisualField->GetSurfaceNormal(Direction);
	OutCellId = TerrainVisualField->FindNearestCell(Direction);
	return OutCellId != INDEX_NONE;
}

void AABTSM3Planet::BuildM3ContinuousSurface()
{
	FUnitSphereMesh Mesh;
	BuildUnitIcosphere(SurfaceSubdivision, Mesh);
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0, UV1, UV2, UV3;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	Vertices.Reserve(Mesh.Triangles.Num() * 3);
	Triangles.Reserve(Mesh.Triangles.Num() * 3);
	Normals.Reserve(Mesh.Triangles.Num() * 3);
	Colors.Reserve(Mesh.Triangles.Num() * 3);

	for (const FIntVector& Triangle : Mesh.Triangles)
	{
		const int32 CandidateCellIds[3] = {
			FindNearestCell(Mesh.Vertices[Triangle.X]),
			FindNearestCell(Mesh.Vertices[Triangle.Y]),
			FindNearestCell(Mesh.Vertices[Triangle.Z])};
		const auto EncodeCellId = [](const int32 CellId)
		{
			return FVector2D(static_cast<float>(CellId >> 8), static_cast<float>(CellId & 0xff));
		};
		const FVector2D EncodedA = EncodeCellId(CandidateCellIds[0]);
		const FVector2D EncodedB = EncodeCellId(CandidateCellIds[1]);
		const FVector2D EncodedC = EncodeCellId(CandidateCellIds[2]);
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FVector Unit = Mesh.Vertices[Triangle[Corner]].GetSafeNormal();
			const int32 CellId = FindNearestCell(Unit);
			const int32 BaseIndex = Vertices.Num();
			Vertices.Add(Unit * TerrainVisualField->GetSurfaceRadius(Unit));
			Normals.Add(TerrainVisualField->GetSurfaceNormal(Unit));
			// UV0/1/2 are constant per triangle: three material candidate CellIds.
			// UV3 is one-hot so the material can reconstruct barycentric role if needed.
			UV0.Add(EncodedA);
			UV1.Add(EncodedB);
			UV2.Add(EncodedC);
			UV3.Emplace(Corner == 0 ? 1.0f : 0.0f, Corner == 1 ? 1.0f : 0.0f);
			Colors.Add(TerrainVisualField->GetDebugTerrainColor(Unit));
			Triangles.Add(BaseIndex);
		}
	}

	ContinuousSurface->ClearAllMeshSections();
	ContinuousSurface->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UV0, UV1, UV2, UV3, Colors, Tangents, true, false);
	if (TerrainMaterial) ContinuousSurface->SetMaterial(0, TerrainMaterial);
}

void AABTSM3Planet::BuildDecorInstances()
{
	ForestHISM->ClearInstances();
	RockHISM->ClearInstances();
	ForestHISM->SetStaticMesh(ForestInstanceMesh);
	RockHISM->SetStaticMesh(RockInstanceMesh);
	if (InstancesPerCell <= 0) return;

	for (int32 CellId = 0; CellId < LogicalCells.Num(); ++CellId)
	{
		const FABTSM3CellState& State = GeneratedCellStates[CellId];
		UHierarchicalInstancedStaticMeshComponent* TargetHISM = nullptr;
		if (State.TerrainType == EABTSM3TerrainType::Forest && ForestInstanceMesh) TargetHISM = ForestHISM;
		if (State.TerrainType == EABTSM3TerrainType::Mountain && RockInstanceMesh) TargetHISM = RockHISM;
		if (TargetHISM == nullptr || State.bRoad || State.bBuildingAnchor || State.bWater) continue;

		FRandomStream Stream(HashCombineFast(GetTypeHash(WorldSeed), GetTypeHash(CellId)));
		const FVector Center = LogicalCells[CellId].UnitCenter;
		for (int32 Slot = 0; Slot < InstancesPerCell; ++Slot)
		{
			const int32 NeighborId = LogicalCells[CellId].NeighborCellIds[Stream.RandRange(0, LogicalCells[CellId].NeighborCellIds.Num() - 1)];
			const FVector Direction = FMath::Lerp(Center, LogicalCells[NeighborId].UnitCenter, Stream.FRandRange(0.0f, 0.42f)).GetSafeNormal();
			const float Radius = TerrainVisualField->GetSurfaceRadius(Direction) - 8.0f;
			const FVector Up = TerrainVisualField->GetSurfaceNormal(Direction);
			FVector Forward = FVector::VectorPlaneProject(LogicalCells[NeighborId].UnitCenter - Center, Up).GetSafeNormal();
			if (Forward.IsNearlyZero()) Forward = FVector::ForwardVector;
			const FQuat Rotation = FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat();
			const float Scale = Stream.FRandRange(0.75f, 1.25f);
			TargetHISM->AddInstance(FTransform(Rotation, Direction * Radius, FVector(Scale)), false);
		}
	}
}

void AABTSM3Planet::BuildBuildingSpawnSites()
{
	BuildingSpawnSites.Reset();
	for (int32 CellId = 0; CellId < GeneratedCellStates.Num(); ++CellId)
	{
		if (!GeneratedCellStates[CellId].bBuildingAnchor) continue;
		const FVector Direction = LogicalCells[CellId].UnitCenter;
		const FVector Normal = TerrainVisualField->GetSurfaceNormal(Direction);
		FVector Forward = FVector::VectorPlaneProject(FVector::ForwardVector, Normal).GetSafeNormal();
		if (Forward.IsNearlyZero()) Forward = FVector::VectorPlaneProject(FVector::RightVector, Normal).GetSafeNormal();
		FABTSM3BuildingSpawnSite& Site = BuildingSpawnSites.AddDefaulted_GetRef();
		Site.CellId = CellId;
		Site.TaskType = GeneratedTasks[GeneratedCellStates[CellId].TaskId].Type;
		Site.WorldTransform = FTransform(FRotationMatrix::MakeFromXZ(Forward, Normal).ToQuat(), GetPlanetCenterWorld() + Direction * TerrainVisualField->GetSurfaceRadius(Direction));
		Site.MaxSlopeDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Direction, Normal), -1.0f, 1.0f)));
	}
}
