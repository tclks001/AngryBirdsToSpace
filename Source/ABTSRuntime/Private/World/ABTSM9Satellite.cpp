// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM9Satellite.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Terrain/ABTSM3Planet.h"
#include "UObject/ConstructorHelpers.h"

AABTSM9Satellite::AABTSM9Satellite()
{
	// M9 intentionally owns a small independent CellTopo. It never contributes
	// terrain tasks, SDF, HISM resources, roads or buildings to the primary planet.
	LogicalSubdivision = 2;
	SurfaceSubdivision = 4;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BasicShapeMaterial.Succeeded()) GrayMaterial = BasicShapeMaterial.Object;
}

void AABTSM9Satellite::ConfigureFromPrimaryPlanet(
	AABTSM3Planet& PrimaryPlanet,
	const int32 InAnchorCellId,
	const float InRadiusCM,
	const float InCenterClearanceCM,
	const float InSurfaceGravityAccelerationCMPerSec2)
{
	if (!PrimaryPlanet.LogicalCells.IsValidIndex(InAnchorCellId)) return;
	const FVector Direction = PrimaryPlanet.LogicalCells[InAnchorCellId].UnitCenter;
	FVector SurfacePosition;
	FVector SurfaceNormal;
	float SurfaceRadius = 0.0f;
	int32 ResolvedCell = INDEX_NONE;
	if (!PrimaryPlanet.QuerySurface(Direction, SurfacePosition, SurfaceNormal, SurfaceRadius, ResolvedCell)) return;
	AnchorCellId = InAnchorCellId;
	CenterClearanceCM = FMath::Max(0.0f, InCenterClearanceCM);
	PlanetRadiusCM = FMath::Max(100.0f, InRadiusCM);
	SurfaceGravityAccelerationCMPerSec2 = FMath::Max(0.0f, InSurfaceGravityAccelerationCMPerSec2);
	SetActorLocation(SurfacePosition + SurfaceNormal.GetSafeNormal() * CenterClearanceCM, false, nullptr, ETeleportType::TeleportPhysics);
}

void AABTSM9Satellite::BeginPlay()
{
	Super::BeginPlay();
	UMaterialInterface* Parent = GrayMaterial ? GrayMaterial.Get() : UMaterial::GetDefaultMaterial(MD_Surface);
	UMaterialInstanceDynamic* GrayMID = UMaterialInstanceDynamic::Create(Parent, this, TEXT("M9SatelliteGray"));
	GrayMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.42f, 0.42f, 0.44f, 1.0f));
	GrayMID->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.42f, 0.42f, 0.44f, 1.0f));
	ContinuousSurface->SetMaterial(0, GrayMID);
}

FVector AABTSM9Satellite::GetGravityAccelerationAt(const FVector& WorldLocation) const
{
	if (!bGravityEnabled || SurfaceGravityAccelerationCMPerSec2 <= 0.0f) return FVector::ZeroVector;
	const FVector ToCenter = GetPlanetCenterWorld() - WorldLocation;
	const float DistanceCM = FMath::Max(ToCenter.Size(), FMath::Max(PlanetRadiusCM, 1.0f));
	return ToCenter / DistanceCM * (SurfaceGravityAccelerationCMPerSec2 * FMath::Square(PlanetRadiusCM / DistanceCM));
}
