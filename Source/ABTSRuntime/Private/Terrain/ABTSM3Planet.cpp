// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/ABTSM3Planet.h"

#include "ABTSRuntime.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "PCG/ABTSM3TaskGraphGenerator.h"
#include "ProceduralMeshComponent.h"
#include "Terrain/ABTSM3TerrainVisualField.h"
#include "Terrain/ABTSM3TerrainMaterialBridge.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
bool ValidateInstancedMeshMaterials(const TCHAR* Label, const UStaticMesh* Mesh)
{
	if (Mesh == nullptr) return false;

	bool bAllMaterialsValid = true;
	const TArray<FStaticMaterial>& StaticMaterials = Mesh->GetStaticMaterials();
	for (int32 SlotIndex = 0; SlotIndex < StaticMaterials.Num(); ++SlotIndex)
	{
		UMaterialInterface* Material = StaticMaterials[SlotIndex].MaterialInterface;
		const bool bUsageValid = Material != nullptr
			&& Material->CheckMaterialUsage_Concurrent(MATUSAGE_InstancedStaticMeshes);
		if (!bUsageValid)
		{
			bAllMaterialsValid = false;
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M3][HISM] %s mesh %s material slot %d (%s) is not compiled for Instanced Static Meshes."),
				Label, *GetNameSafe(Mesh), SlotIndex, *GetNameSafe(Material));
		}
	}
	return bAllMaterialsValid;
}
}

AABTSM3Planet::AABTSM3Planet()
{
	// Keep native preview meshes as a last-resort presentation fallback.  M3 must
	// remain visible even before the art meshes are assigned in the Blueprint.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ForestPreviewMesh(
		TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> RockPreviewMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));

	ForestHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("ForestHISM"));
	ForestHISM->SetupAttachment(ContinuousSurface);
	ForestHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ForestHISM->SetMobility(EComponentMobility::Movable);
	ForestHISM->SetStaticMesh(ForestPreviewMesh.Object);

	RockHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("RockHISM"));
	RockHISM->SetupAttachment(ContinuousSurface);
	RockHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RockHISM->SetMobility(EComponentMobility::Movable);
	RockHISM->SetStaticMesh(RockPreviewMesh.Object);
}

bool AABTSM3Planet::RebuildPlanet()
{
	if (!AABTSM2Planet::RebuildPlanet() || !GenerateLogicalTerrain()) return false;
	TerrainVisualField = MakeUnique<FABTSM3TerrainVisualField>();
	TerrainVisualField->Initialize(PlanetRadiusCM, MacroHeightScaleCM, TaskWaterDepthCM, HeightBlendWidthCM, TerrainBlendWidthCM, SurfaceNormalSmoothingDistanceCM,
		LogicalCells, GeneratedCellStates, GeneratedEdgeStates,
		StreamVisualHalfWidthCM, ShallowRiverVisualHalfWidthCM, DeepRiverVisualHalfWidthCM);
	BuildM3ContinuousSurface();
	bool bMaterialReady = false;
	bool bPresentationReady = true;
	if (TerrainMaterial)
	{
		TerrainMaterialBridge = NewObject<UABTSM3TerrainMaterialBridge>(this);
		bMaterialReady = TerrainMaterialBridge->Initialize(ContinuousSurface, TerrainMaterial, GetPlanetCenterWorld(), PlanetRadiusCM, TerrainBlendWidthCM,
			RoadColor, TrailVisualHalfWidthCM, MainRoadVisualHalfWidthCM,
			RiverColor, StreamVisualHalfWidthCM, ShallowRiverVisualHalfWidthCM, DeepRiverVisualHalfWidthCM,
			LogicalCells, GeneratedCellStates, GeneratedEdgeStates, *TerrainVisualField);
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
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M3] Ready=%d Seed=%d Version=%d Attempt=%d Tasks=%d Links=%d Cells=%d Edges=%d RoadCells=%d WaterCells=%d Buildings=%d ForestInstances=%d RockInstances=%d MaterialAssigned=%d MaterialReady=%d"),
		bPresentationReady ? 1 : 0, WorldSeed, PCGSummary.GeneratorVersion, PCGSummary.AttemptIndex, GeneratedTasks.Num(), GeneratedTaskLinks.Num(),
		GeneratedCellStates.Num(), GeneratedEdgeStates.Num(), RoadCells, WaterCells, BuildingSpawnSites.Num(), ForestHISM->GetInstanceCount(), RockHISM->GetInstanceCount(), TerrainMaterial ? 1 : 0, bMaterialReady ? 1 : 0);
	return bPresentationReady;
}

bool AABTSM3Planet::GenerateLogicalTerrain()
{
	FABTSM3TaskGraphGenerator Generator;
	return Generator.Generate(WorldSeed, PCGConfig, LogicalCells, GeneratedTasks, GeneratedTaskLinks, GeneratedCellStates, GeneratedEdgeStates, PCGSummary);
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

bool AABTSM3Planet::GetInitialRoadSpawnTransform(
	const float SurfaceOffsetCM,
	FTransform& OutWorldTransform,
	int32& OutCellId) const
{
	OutWorldTransform = FTransform::Identity;
	OutCellId = INDEX_NONE;
	if (!TerrainVisualField || !TerrainVisualField->IsReady()) return false;

	const FABTSM3TaskNode* StartTask = GeneratedTasks.FindByPredicate([](const FABTSM3TaskNode& Task)
	{
		return Task.Type == EABTSM3TaskType::Start;
	});
	if (StartTask == nullptr || !LogicalCells.IsValidIndex(StartTask->SeedCellId)) return false;

	// The first Task seed is always included in the first main-road path. Keep a
	// defensive search inside the Start region for future graph templates.
	int32 SpawnCellId = StartTask->SeedCellId;
	if (!GeneratedCellStates.IsValidIndex(SpawnCellId) || !GeneratedCellStates[SpawnCellId].bRoad)
	{
		SpawnCellId = INDEX_NONE;
		for (const int32 CellId : StartTask->CellIds)
		{
			if (GeneratedCellStates.IsValidIndex(CellId) && GeneratedCellStates[CellId].bRoad)
			{
				SpawnCellId = CellId;
				break;
			}
		}
	}
	if (!LogicalCells.IsValidIndex(SpawnCellId)) return false;

	const FVector SpawnDirection = LogicalCells[SpawnCellId].UnitCenter.GetSafeNormal();
	const FVector SurfaceNormal = TerrainVisualField->GetSurfaceNormal(SpawnDirection);
	FVector RoadForward = FVector::ZeroVector;
	float BestNextTaskDot = -2.0f;
	const FABTSM3TaskNode* NextMainTask = nullptr;
	for (const int32 LinkedTaskId : StartTask->LinkedTaskIds)
	{
		const FABTSM3TaskNode* LinkedTask = GeneratedTasks.FindByPredicate([LinkedTaskId](const FABTSM3TaskNode& Task)
		{
			return Task.TaskId == LinkedTaskId;
		});
		if (LinkedTask != nullptr && LogicalCells.IsValidIndex(LinkedTask->SeedCellId))
		{
			const float Dot = FVector::DotProduct(SpawnDirection, LogicalCells[LinkedTask->SeedCellId].UnitCenter);
			if (Dot > BestNextTaskDot)
			{
				BestNextTaskDot = Dot;
				NextMainTask = LinkedTask;
			}
		}
	}
	if (NextMainTask != nullptr)
	{
		const FVector TargetDirection = LogicalCells[NextMainTask->SeedCellId].UnitCenter;
		int32 FirstRoadNeighborId = INDEX_NONE;
		float BestRoadProgress = -2.0f;
		for (const int32 NeighborId : LogicalCells[SpawnCellId].NeighborCellIds)
		{
			if (!GeneratedCellStates.IsValidIndex(NeighborId) || !GeneratedCellStates[NeighborId].bRoad) continue;
			const float Progress = FVector::DotProduct(LogicalCells[NeighborId].UnitCenter, TargetDirection);
			if (Progress > BestRoadProgress)
			{
				BestRoadProgress = Progress;
				FirstRoadNeighborId = NeighborId;
			}
		}
		if (FirstRoadNeighborId != INDEX_NONE)
		{
			RoadForward = FVector::VectorPlaneProject(LogicalCells[FirstRoadNeighborId].UnitCenter - SpawnDirection, SurfaceNormal).GetSafeNormal();
		}
	}
	if (RoadForward.IsNearlyZero())
	{
		for (const int32 NeighborId : LogicalCells[SpawnCellId].NeighborCellIds)
		{
			if (GeneratedCellStates.IsValidIndex(NeighborId) && GeneratedCellStates[NeighborId].bRoad)
			{
				RoadForward = FVector::VectorPlaneProject(LogicalCells[NeighborId].UnitCenter - SpawnDirection, SurfaceNormal).GetSafeNormal();
				if (!RoadForward.IsNearlyZero()) break;
			}
		}
	}
	if (RoadForward.IsNearlyZero())
	{
		RoadForward = FVector::VectorPlaneProject(FVector::ForwardVector, SurfaceNormal).GetSafeNormal();
		if (RoadForward.IsNearlyZero()) RoadForward = FVector::VectorPlaneProject(FVector::RightVector, SurfaceNormal).GetSafeNormal();
	}

	const float CharacterCenterRadius = TerrainVisualField->GetSurfaceRadius(SpawnDirection) + FMath::Max(0.0f, SurfaceOffsetCM);
	const FVector CharacterCenter = GetPlanetCenterWorld() + SpawnDirection * CharacterCenterRadius;
	OutWorldTransform = FTransform(FRotationMatrix::MakeFromXZ(RoadForward, SpawnDirection).ToQuat(), CharacterCenter);
	OutCellId = SpawnCellId;
	return true;
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
	float MaxSurfaceNormalTiltDegrees = 0.0f;
	int32 ExtremeSurfaceNormalCount = 0;

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
			const FVector SurfaceNormal = TerrainVisualField->GetSurfaceNormal(Unit);
			Normals.Add(SurfaceNormal);
			const float NormalTiltDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Unit, SurfaceNormal), -1.0f, 1.0f)));
			MaxSurfaceNormalTiltDegrees = FMath::Max(MaxSurfaceNormalTiltDegrees, NormalTiltDegrees);
			ExtremeSurfaceNormalCount += NormalTiltDegrees > 80.0f ? 1 : 0;
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
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M3][SurfaceNormals] SmoothingDistance=%.1f MaxTilt=%.2f ExtremeOver80=%d Vertices=%d"),
		SurfaceNormalSmoothingDistanceCM, MaxSurfaceNormalTiltDegrees, ExtremeSurfaceNormalCount, Normals.Num());
}

void AABTSM3Planet::BuildDecorInstances()
{
	ForestHISM->ClearInstances();
	RockHISM->ClearInstances();

	// An artist may configure either the Actor properties or the HISM component
	// templates in BP_ABTSM3Planet.  A null Actor property must not erase a mesh
	// already assigned on the component.
	if (ForestInstanceMesh)
	{
		ForestHISM->SetStaticMesh(ForestInstanceMesh);
	}
	if (RockInstanceMesh)
	{
		RockHISM->SetStaticMesh(RockInstanceMesh);
	}

	UStaticMesh* ResolvedForestMesh = ForestHISM->GetStaticMesh();
	UStaticMesh* ResolvedRockMesh = RockHISM->GetStaticMesh();
	// Existing Blueprints created before native preview meshes were introduced
	// can serialize an explicit null component mesh.  Resolve that migration case
	// at runtime so the HISM path is immediately testable without an asset edit.
	if (ResolvedForestMesh == nullptr)
	{
		ResolvedForestMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
		ForestHISM->SetStaticMesh(ResolvedForestMesh);
	}
	if (ResolvedRockMesh == nullptr)
	{
		ResolvedRockMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		RockHISM->SetStaticMesh(ResolvedRockMesh);
	}
	const bool bForestMaterialsValid = ValidateInstancedMeshMaterials(TEXT("Forest"), ResolvedForestMesh);
	const bool bRockMaterialsValid = ValidateInstancedMeshMaterials(TEXT("Rock"), ResolvedRockMesh);

	if (InstancesPerCell <= 0)
	{
		UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M3][HISM] InstancesPerCell=%d; decoration generation is disabled."), InstancesPerCell);
		return;
	}

	int32 EligibleForestCells = 0;
	int32 EligibleRockCells = 0;
	float MaxForestSurfaceTiltDegrees = 0.0f;
	float MaxForestAppliedTiltDegrees = 0.0f;

	for (int32 CellId = 0; CellId < LogicalCells.Num(); ++CellId)
	{
		if (!GeneratedCellStates.IsValidIndex(CellId)) continue;
		const FABTSM3CellState& State = GeneratedCellStates[CellId];
		UHierarchicalInstancedStaticMeshComponent* TargetHISM = nullptr;
		if (State.bRoad || State.bBuildingAnchor || State.bWater) continue;
		if (State.TerrainType == EABTSM3TerrainType::Forest && ResolvedForestMesh)
		{
			TargetHISM = ForestHISM;
			++EligibleForestCells;
		}
		if (State.TerrainType == EABTSM3TerrainType::Mountain && ResolvedRockMesh)
		{
			TargetHISM = RockHISM;
			++EligibleRockCells;
		}
		if (TargetHISM == nullptr) continue;

		FRandomStream Stream(HashCombineFast(GetTypeHash(WorldSeed), GetTypeHash(CellId)));
		const FVector Center = LogicalCells[CellId].UnitCenter;
		for (int32 Slot = 0; Slot < InstancesPerCell; ++Slot)
		{
			const int32 NeighborId = LogicalCells[CellId].NeighborCellIds[Stream.RandRange(0, LogicalCells[CellId].NeighborCellIds.Num() - 1)];
			const FVector Direction = FMath::Lerp(Center, LogicalCells[NeighborId].UnitCenter, Stream.FRandRange(0.0f, 0.42f)).GetSafeNormal();
			const float Radius = TerrainVisualField->GetSurfaceRadius(Direction) - 8.0f;
			const FVector RadialUp = Direction;
			FVector SurfaceUp = TerrainVisualField->GetSurfaceNormal(Direction).GetSafeNormal();
			if (FVector::DotProduct(SurfaceUp, RadialUp) < 0.0f) SurfaceUp *= -1.0f;
			FVector Up = SurfaceUp;
			if (TargetHISM == ForestHISM)
			{
				// A tree should visually grow away from the planet, not lie along every
				// local terrain ripple. Keep radial Up dominant and use the surface
				// normal only as a controlled slope response.
				const float SurfaceBlend = FMath::Clamp(ForestSurfaceNormalBlend, 0.0f, 1.0f);
				Up = FMath::Lerp(RadialUp, SurfaceUp, SurfaceBlend).GetSafeNormal();
				if (Up.IsNearlyZero()) Up = RadialUp;
				const float SurfaceTiltDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(RadialUp, SurfaceUp), -1.0f, 1.0f)));
				const float AppliedTiltDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(RadialUp, Up), -1.0f, 1.0f)));
				MaxForestSurfaceTiltDegrees = FMath::Max(MaxForestSurfaceTiltDegrees, SurfaceTiltDegrees);
				MaxForestAppliedTiltDegrees = FMath::Max(MaxForestAppliedTiltDegrees, AppliedTiltDegrees);
			}
			FVector Forward = FVector::VectorPlaneProject(LogicalCells[NeighborId].UnitCenter - Center, Up).GetSafeNormal();
			if (Forward.IsNearlyZero()) Forward = FVector::VectorPlaneProject(FVector::ForwardVector, Up).GetSafeNormal();
			if (Forward.IsNearlyZero()) Forward = FVector::VectorPlaneProject(FVector::RightVector, Up).GetSafeNormal();
			const FQuat Rotation = FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat();
			const float Scale = Stream.FRandRange(0.75f, 1.25f);
			TargetHISM->AddInstance(FTransform(Rotation, Direction * Radius, FVector(Scale)), false);
		}
	}

	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3][HISM] ForestMesh=%s RockMesh=%s ForestMaterialsValid=%d RockMaterialsValid=%d EligibleForestCells=%d EligibleRockCells=%d ForestInstances=%d RockInstances=%d ForestNormalBlend=%.2f MaxSurfaceTilt=%.2f MaxAppliedTilt=%.2f"),
		*GetNameSafe(ResolvedForestMesh), *GetNameSafe(ResolvedRockMesh), bForestMaterialsValid ? 1 : 0, bRockMaterialsValid ? 1 : 0, EligibleForestCells, EligibleRockCells,
		ForestHISM->GetInstanceCount(), RockHISM->GetInstanceCount(), FMath::Clamp(ForestSurfaceNormalBlend, 0.0f, 1.0f),
		MaxForestSurfaceTiltDegrees, MaxForestAppliedTiltDegrees);
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
