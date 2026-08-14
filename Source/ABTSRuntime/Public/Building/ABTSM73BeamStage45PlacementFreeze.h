// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73BeamD1Types.h"

/**
 * Integration-facing, immutable placement facts for one frozen Stage-4 jury building.
 *
 * Coordinates are centimetres in the Stage-4 generator's local frame. The placement
 * pivot is the generator origin; +X is Forward, +Y is Right and +Z is Up. Bounds and
 * hashes describe active static geometry only, excluding Stage-3 members suppressed by
 * the Stage-4 Facade-to-Top replacement.
 */
struct ABTSRUNTIME_API FABTSM73BeamStage45PlacementDescriptor
{
	int32 SchemaVersion = 0;
	int32 SourceManifestVersion = 0;
	int64 SourceManifestHash = 0;
	EABTSM73BeamDemoBuilding ManifestEntryId = EABTSM73BeamDemoBuilding::Custom;
	FName StableId;
	FName GameplayProfileId;
	int32 DifficultyTier = INDEX_NONE;
	int32 BuildingSeed = 0;

	int64 ProfileCatalogHash = 0;
	int64 ResolvedSettingsHash = 0;
	int64 GrammarHash = 0;
	int64 WFCHash = 0;
	int64 Stage4PlanHash = 0;
	uint64 StaticStructureHash = 0;
	uint64 StaticGeometryHash = 0;
	int32 ActiveMemberCount = 0;

	FBox LocalBounds = FBox(EForceInit::ForceInit);
	FVector2D FootprintMinCM = FVector2D::ZeroVector;
	FVector2D FootprintMaxCM = FVector2D::ZeroVector;
	FVector PlacementPivotLocalCM = FVector::ZeroVector;
	double GroundPlaneZCM = 0.0;
	double PivotToGroundOffsetCM = 0.0;
	FVector LocalForwardAxis = FVector::ForwardVector;
	FVector LocalRightAxis = FVector::RightVector;
	FVector LocalUpAxis = FVector::UpVector;
	FVector2D RequiredPadHalfExtentCM = FVector2D::ZeroVector;
	double PadSafetyMarginCM = 0.0;

	uint64 DescriptorHash = 0;
};

/** Stage 4.5 publisher and geometry-derived freeze verifier. */
class ABTSRUNTIME_API FABTSM73BeamStage45PlacementFreeze
{
public:
	static constexpr int32 SchemaVersion = 1;
	static constexpr double PadSafetyMarginCM = 36.0;
	static constexpr int32 ExpectedEntryCount = 6;
	static constexpr int32 FrozenSourceManifestVersion = 1;
	static constexpr int64 FrozenSourceManifestHash = 2324068295;
	static constexpr uint64 FrozenCatalogHash = 13889440156022460967ull;

	/** Committed values which Integration/M3 may consume after merging this branch. */
	static const TArray<FABTSM73BeamStage45PlacementDescriptor>& GetFrozenDescriptors();
	static bool ResolveFrozen(
		EABTSM73BeamDemoBuilding Id,
		FABTSM73BeamStage45PlacementDescriptor& OutDescriptor,
		FString& OutError);
	static uint64 CalculateFrozenCatalogHash();

	/** Rebuilds Stage 4 and derives placement facts from actual active geometry. */
	static bool DeriveAndValidate(
		EABTSM73BeamDemoBuilding Id,
		FABTSM73BeamStage45PlacementDescriptor& OutDescriptor,
		FString& OutError);
	static bool DeriveAndValidateCatalog(
		TArray<FABTSM73BeamStage45PlacementDescriptor>& OutDescriptors,
		uint64& OutCatalogHash,
		FString& OutError);
};
