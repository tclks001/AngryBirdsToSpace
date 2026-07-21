// Copyright Epic Games, Inc. All Rights Reserved.

#include "Planet/ABTSM2Planet.h"

#include "ABTSRuntime.h"
#include "ProceduralMeshComponent.h"

namespace
{
	uint64 MakeEdgeKey(const int32 A, const int32 B)
	{
		return (static_cast<uint64>(FMath::Min(A, B)) << 32) | static_cast<uint32>(FMath::Max(A, B));
	}

	int32 GetMidpoint(AABTSM2Planet::FUnitSphereMesh& Mesh, TMap<uint64, int32>& Cache, const int32 A, const int32 B)
	{
		const uint64 Key = MakeEdgeKey(A, B);
		if (const int32* Existing = Cache.Find(Key))
		{
			return *Existing;
		}

		const int32 NewIndex = Mesh.Vertices.Add((Mesh.Vertices[A] + Mesh.Vertices[B]).GetSafeNormal());
		Cache.Add(Key, NewIndex);
		return NewIndex;
	}
}

AABTSM2Planet::AABTSM2Planet()
{
	PrimaryActorTick.bCanEverTick = false;
	ContinuousSurface = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ContinuousSurface"));
	SetRootComponent(ContinuousSurface);
	ContinuousSurface->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	ContinuousSurface->bUseComplexAsSimpleCollision = true;
	ContinuousSurface->SetMobility(EComponentMobility::Movable);
}

void AABTSM2Planet::BeginPlay()
{
	Super::BeginPlay();
	RebuildPlanet();
}

bool AABTSM2Planet::RebuildPlanet()
{
	bPlanetReady = false;
	LogicalSubdivision = FMath::Clamp(LogicalSubdivision, 1, 6);
	SurfaceSubdivision = FMath::Clamp(SurfaceSubdivision, 1, 7);
	PlanetRadiusCM = FMath::Max(PlanetRadiusCM, 1000.0f);

	BuildLogicalTopology();
	BuildContinuousSurface();
	bPlanetReady = ValidateTopology();

	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M2] Planet rebuilt. CellTopoSub=%d Cells=%d SurfaceSub=%d Triangles=%d InwardTriangles=%d Ready=%d"),
		LogicalSubdivision, LogicalCells.Num(), SurfaceSubdivision, SurfaceTriangleCount, InwardSurfaceTriangleCount, bPlanetReady);
	return bPlanetReady;
}

FVector AABTSM2Planet::GetSurfaceWorldLocation(const FVector& UnitDirection, const float HeightOffsetCM) const
{
	return GetActorLocation() + UnitDirection.GetSafeNormal() * (PlanetRadiusCM + HeightOffsetCM);
}

FVector AABTSM2Planet::GetRadialUpAtWorldLocation(const FVector& WorldLocation) const
{
	const FVector Radial = WorldLocation - GetPlanetCenterWorld();
	return Radial.IsNearlyZero() ? FVector::UpVector : Radial.GetSafeNormal();
}

float AABTSM2Planet::GetSurfaceRadiusAtDirection(const FVector& UnitDirection) const
{
	return PlanetRadiusCM;
}

FVector AABTSM2Planet::GetSurfaceNormalAtDirection(const FVector& UnitDirection) const
{
	return UnitDirection.IsNearlyZero() ? FVector::UpVector : UnitDirection.GetSafeNormal();
}

FTransform AABTSM2Planet::GetNorthPoleSpawnTransform(const float HeightOffsetCM) const
{
	const FVector Up = FVector::UpVector;
	const FVector Forward = FVector::ForwardVector;
	return FTransform(FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat(), GetSurfaceWorldLocation(Up, HeightOffsetCM));
}

void AABTSM2Planet::BuildUnitIcosphere(const int32 Subdivision, FUnitSphereMesh& OutMesh)
{
	OutMesh.Vertices.Reset();
	OutMesh.Triangles.Reset();
	const float Phi = (1.0f + FMath::Sqrt(5.0f)) * 0.5f;
	const FVector RawVertices[] =
	{
		{-1.0f, Phi, 0.0f}, {1.0f, Phi, 0.0f}, {-1.0f, -Phi, 0.0f}, {1.0f, -Phi, 0.0f},
		{0.0f, -1.0f, Phi}, {0.0f, 1.0f, Phi}, {0.0f, -1.0f, -Phi}, {0.0f, 1.0f, -Phi},
		{Phi, 0.0f, -1.0f}, {Phi, 0.0f, 1.0f}, {-Phi, 0.0f, -1.0f}, {-Phi, 0.0f, 1.0f},
	};
	for (const FVector& Vertex : RawVertices)
	{
		OutMesh.Vertices.Add(Vertex.GetSafeNormal());
	}

	// The actual UProceduralMeshComponent outer-shell front-face convention matches
	// TerraCivilization::FSphereTopology::PrimalTris. With this project's vertex order,
	// its algebraic cross product points inward. PMC culling uses the index winding, while
	// the explicit vertex normals below remain radial outward for lighting.
	const int32 FaceIndices[][3] =
	{
		{0, 5, 11}, {0, 1, 5}, {0, 7, 1}, {0, 10, 7}, {0, 11, 10},
		{1, 9, 5}, {5, 4, 11}, {11, 2, 10}, {10, 6, 7}, {7, 8, 1},
		{4, 5, 9}, {2, 11, 4}, {6, 10, 2}, {8, 7, 6}, {9, 1, 8},
		{3, 4, 9}, {3, 2, 4}, {3, 6, 2}, {3, 8, 6}, {3, 9, 8},
	};
	for (const auto& Face : FaceIndices)
	{
		OutMesh.Triangles.Add(FIntVector(Face[0], Face[1], Face[2]));
	}

	for (int32 Index = 0; Index < Subdivision; ++Index)
	{
		SubdivideOnce(OutMesh);
	}
}

void AABTSM2Planet::SubdivideOnce(FUnitSphereMesh& Mesh)
{
	TMap<uint64, int32> MidpointCache;
	MidpointCache.Reserve(Mesh.Triangles.Num() * 2);
	TArray<FIntVector> NewTriangles;
	NewTriangles.Reserve(Mesh.Triangles.Num() * 4);
	for (const FIntVector& Triangle : Mesh.Triangles)
	{
		const int32 AB = GetMidpoint(Mesh, MidpointCache, Triangle.X, Triangle.Y);
		const int32 BC = GetMidpoint(Mesh, MidpointCache, Triangle.Y, Triangle.Z);
		const int32 CA = GetMidpoint(Mesh, MidpointCache, Triangle.Z, Triangle.X);
		NewTriangles.Emplace(Triangle.X, AB, CA);
		NewTriangles.Emplace(Triangle.Y, BC, AB);
		NewTriangles.Emplace(Triangle.Z, CA, BC);
		NewTriangles.Emplace(AB, BC, CA);
	}
	Mesh.Triangles = MoveTemp(NewTriangles);
}

int32 AABTSM2Planet::CountWrongProceduralMeshWinding(const FUnitSphereMesh& Mesh)
{
	int32 InwardCount = 0;
	for (const FIntVector& Triangle : Mesh.Triangles)
	{
		const FVector& A = Mesh.Vertices[Triangle.X];
		const FVector& B = Mesh.Vertices[Triangle.Y];
		const FVector& C = Mesh.Vertices[Triangle.Z];
		const FVector FaceNormal = FVector::CrossProduct(B - A, C - A);
		if (FVector::DotProduct(FaceNormal, A + B + C) >= 0.0f)
		{
			++InwardCount;
		}
	}
	return InwardCount;
}

void AABTSM2Planet::BuildLogicalTopology()
{
	FUnitSphereMesh LogicMesh;
	BuildUnitIcosphere(LogicalSubdivision, LogicMesh);
	LogicalCells.SetNum(LogicMesh.Vertices.Num());
	for (int32 CellId = 0; CellId < LogicMesh.Vertices.Num(); ++CellId)
	{
		LogicalCells[CellId].UnitCenter = LogicMesh.Vertices[CellId];
		LogicalCells[CellId].NeighborCellIds.Reset();
	}

	auto AddNeighbor = [this](const int32 A, const int32 B)
	{
		LogicalCells[A].NeighborCellIds.AddUnique(B);
		LogicalCells[B].NeighborCellIds.AddUnique(A);
	};
	for (const FIntVector& Triangle : LogicMesh.Triangles)
	{
		AddNeighbor(Triangle.X, Triangle.Y);
		AddNeighbor(Triangle.Y, Triangle.Z);
		AddNeighbor(Triangle.Z, Triangle.X);
	}
	for (FABTSM2Cell& Cell : LogicalCells)
	{
		Cell.NeighborCellIds.Sort();
		Cell.bIsPentagon = Cell.NeighborCellIds.Num() == 5;
	}
}

void AABTSM2Planet::BuildContinuousSurface()
{
	FUnitSphereMesh RenderMesh;
	BuildUnitIcosphere(SurfaceSubdivision, RenderMesh);
	InwardSurfaceTriangleCount = CountWrongProceduralMeshWinding(RenderMesh);
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	Vertices.Reserve(RenderMesh.Vertices.Num());
	Normals.Reserve(RenderMesh.Vertices.Num());
	UVs.Reserve(RenderMesh.Vertices.Num());
	Tangents.Reserve(RenderMesh.Vertices.Num());
	Triangles.Reserve(RenderMesh.Triangles.Num() * 3);

	for (const FVector& UnitVertex : RenderMesh.Vertices)
	{
		const FVector Normal = UnitVertex.GetSafeNormal();
		Vertices.Add(Normal * PlanetRadiusCM);
		Normals.Add(Normal);
		UVs.Emplace(FMath::Atan2(Normal.Y, Normal.X) / (2.0f * PI) + 0.5f, FMath::Asin(Normal.Z) / PI + 0.5f);
		FVector TangentX = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
		if (TangentX.IsNearlyZero()) TangentX = FVector::ForwardVector;
		Tangents.Emplace(TangentX, false);
	}

	for (const FIntVector& Triangle : RenderMesh.Triangles)
	{
		Triangles.Add(Triangle.X);
		Triangles.Add(Triangle.Y);
		Triangles.Add(Triangle.Z);
	}

	ContinuousSurface->ClearAllMeshSections();
	ContinuousSurface->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, true);
	SurfaceTriangleCount = RenderMesh.Triangles.Num();
}

bool AABTSM2Planet::ValidateTopology() const
{
	const int32 ExpectedCells = 10 * (1 << (2 * LogicalSubdivision)) + 2;
	const int32 ExpectedTriangles = 20 * (1 << (2 * SurfaceSubdivision));
	int32 PentagonCount = 0;
	for (const FABTSM2Cell& Cell : LogicalCells)
	{
		PentagonCount += Cell.bIsPentagon ? 1 : 0;
		if (Cell.NeighborCellIds.Num() != 5 && Cell.NeighborCellIds.Num() != 6)
		{
			return false;
		}
	}
	return LogicalCells.Num() == ExpectedCells
		&& PentagonCount == 12
		&& SurfaceTriangleCount == ExpectedTriangles
		&& InwardSurfaceTriangleCount == 0;
}
