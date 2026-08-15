// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73StableBuildingActor.h"

#include "Building/ABTSM7BuildingModule.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"

bool AABTSM73StableBuildingActor::QueryLivePresentationAnchor(
	FVector& OutWorldLocation,
	int32& OutLiveModuleCount) const
{
	OutWorldLocation = FVector::ZeroVector;
	OutLiveModuleCount = 0;
	if (!bRuntimeSpawned || !GenerationSummary.bAccepted) return false;

	FVector AccumulatedLocation = FVector::ZeroVector;
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& WeakModule : RuntimeModules)
	{
		const AABTSM7BuildingModule* Module = WeakModule.Get();
		if (Module == nullptr || Module->IsActorBeingDestroyed()) continue;
		const UStaticMeshComponent* Mesh = Module->GetMeshComponent();
		AccumulatedLocation += Mesh
			? Mesh->GetComponentLocation()
			: Module->GetActorLocation();
		++OutLiveModuleCount;
	}
	if (OutLiveModuleCount <= 0) return false;
	OutWorldLocation = AccumulatedLocation
		/ static_cast<float>(OutLiveModuleCount);
	return !OutWorldLocation.ContainsNaN();
}

bool AABTSM73StableBuildingActor::OwnsRuntimePrimitive(
	const UPrimitiveComponent* Component) const
{
	if (Component == nullptr || !bRuntimeSpawned
		|| !GenerationSummary.bAccepted)
	{
		return false;
	}
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& WeakModule : RuntimeModules)
	{
		const AABTSM7BuildingModule* Module = WeakModule.Get();
		if (Module == nullptr || Module->IsActorBeingDestroyed()) continue;
		if (Component == Module->GetMeshComponent()
			|| Component->GetOwner() == Module)
		{
			return true;
		}
	}
	return false;
}

bool AABTSM73StableBuildingActor::QueryScoutMapMarkerLocation(
	const AABTSM3Planet* ExpectedPlanet,
	FVector& OutWorldLocation,
	int32& OutLiveModuleCount) const
{
	if (ExpectedPlanet == nullptr || ConfiguredPlanet.Get() != ExpectedPlanet)
	{
		OutWorldLocation = FVector::ZeroVector;
		OutLiveModuleCount = 0;
		return false;
	}
	return QueryLivePresentationAnchor(
		OutWorldLocation,
		OutLiveModuleCount);
}
