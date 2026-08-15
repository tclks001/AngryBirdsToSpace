// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/ABTSM73BeamD1Types.h"
#include "Contracts/ABTSWorldGenerationContracts.h"
#include "CoreMinimal.h"

class AABTSM73StableBuildingActor;
class AABTSM7BuildingMaterialSystem;
class UWorld;

/** One fully verified, immutable static-registration payload. */
struct FABTSM73JuryDemoFixedSixStaticEntry
{
	FName ManifestEntryId = NAME_None;
	EABTSM73BeamDemoBuilding DemoBuilding =
		EABTSM73BeamDemoBuilding::Custom;
	int32 EncounterIndex = INDEX_NONE;
	int32 DifficultyTier = INDEX_NONE;
	int32 DeterministicSeed = 0;
	FTransform WorldTransform = FTransform::Identity;
	FVector2D PadHalfExtentCM = FVector2D::ZeroVector;
	FBox LocalBounds = FBox(EForceInit::ForceInit);
	FBox EffectBounds = FBox(EForceInit::ForceInit);
	uint64 DescriptorHash = 0;
	uint64 StaticGeometryHash = 0;
	uint64 ProductionIdentityHash = 0;
	uint64 DeviceAssemblyHash = 0;
	uint64 SourceLayoutHash = 0;
	uint64 RegistrationResultHash = 0;
	bool bDynamicEnvelopeRequired = false;
	TArray<FABTSM73BeamD1BrickBinding> Bricks;
	TArray<FABTSM73BeamD1DeviceBinding> Devices;

	bool IsUsable(double Tolerance = 1.0e-3) const;
};

/** Atomic plan: no Actor or component exists until all six entries validate. */
struct FABTSM73JuryDemoFixedSixStaticPlan
{
	int32 ContractVersion = 0;
	int32 WorldSeed = 0;
	uint64 PlacementCatalogHash = 0;
	uint64 LayoutHash = 0;
	uint64 RegistrationResultHash = 0;
	TArray<FABTSM73JuryDemoFixedSixStaticEntry> Entries;

	bool IsUsable(double Tolerance = 1.0e-3) const;
};

/** Exact V2 consumer and transactional static-Actor registrar. */
class ABTSRUNTIME_API FABTSM73JuryDemoFixedSixRegistration
{
public:
	static bool BuildStaticPlan(
		const FABTSBuildingGenerationContract& Contract,
		FABTSM73JuryDemoFixedSixStaticPlan& OutPlan,
		FString& OutError);

	static bool SpawnStaticActors(
		UWorld& World,
		AABTSM7BuildingMaterialSystem& MaterialSystem,
		TSubclassOf<AABTSM73StableBuildingActor> BuildingClass,
		FABTSM73JuryDemoFixedSixStaticPlan&& Plan,
		TArray<TWeakObjectPtr<AABTSM73StableBuildingActor>>& OutActors,
		FString& OutError);
};
