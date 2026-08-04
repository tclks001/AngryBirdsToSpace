// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73BeamCPreviewTypes.h"

/** Gameplay-facing failure family selected before real Brick compilation. */
enum class EABTSM73BeamD0WeaknessIntent : uint8
{
	ColumnBreak,
	SeamRelease,
	TipOver,
	DropTrigger,
	SlideRelease
};

/** Material-role palette intent. Beam-D1 maps this to real material assets. */
enum class EABTSM73BeamD0MaterialPalette : uint8
{
	LightFrameFragileJoint,
	MasonryWithWoodSeam,
	IronFrameGlassTrigger,
	SuspendedStonePod
};

/** Optional gameplay device intent. Beam-D1/D2 owns the physical realization. */
enum class EABTSM73BeamD0DeviceIntent : uint8
{
	None,
	BreakableSeam,
	HangingMass
};

/** Expected first-order collapse signature used by later certification. */
enum class EABTSM73BeamD0CollapseIntent : uint8
{
	ProgressiveFold,
	DirectedTip,
	SupportedDrop,
	LateralSlide,
	BridgeRelease
};

struct FABTSM73BeamD0DifficultyMetrics
{
	float TargetDamageCost = 0.0f;
	float WeaknessExposureRatio = 0.0f;
	float AimToleranceDegrees = 0.0f;
	int32 SupportRedundancy = 0;
	float WeaknessRewardMultiplier = 0.0f;
	int32 SolutionSteps = 1;
};

/** Visual-only tier recipe. Beam-D2 gameplay difficulty does not consume it. */
struct FABTSM73BeamD0VisualComplexityRecipe
{
	int32 MilestoneTier = INDEX_NONE;
	int32 MinimumBrickCount = 0;
	int32 MaximumBrickCount = 0;
	int32 MaximumCandidateAttempts = 0;
	float BoundsScale = 1.0f;
	float BaySpanScale = 1.0f;
	int32 ShapeGrammarDepth = 2;
	int32 MotifGrammarDepth = 1;
	int32 TargetShapeVolumeCount = 12;
	int32 MaximumBaysPerVolume = 2;
	int32 MaximumParallelBlocksPerCourse = 2;
	int32 MaximumRoofCourseCount = 8;
	int32 SingleTerminalRoofCourseCount = 0;
	bool bRequirePrimitiveVariety = false;
	bool bRequireSingleTerminalRoof = false;
	bool bRequireMotifVariety = false;

	bool Validate(FString& OutError) const;
};

/** Monotonic per-profile curve. It is catalog data, never a TaskGraph input. */
struct FABTSM73BeamD0DifficultyCurve
{
	int32 MinimumTier = 0;
	int32 MaximumTier = 5;
	float BaseTargetDamageCost = 1.0f;
	float TargetDamageCostPerTier = 0.45f;
	float BaseWeaknessExposureRatio = 0.72f;
	float WeaknessExposureDropPerTier = 0.07f;
	float MinimumWeaknessExposureRatio = 0.24f;
	float BaseAimToleranceDegrees = 18.0f;
	float AimToleranceDropPerTier = 1.8f;
	float MinimumAimToleranceDegrees = 7.0f;
	int32 BaseSupportRedundancy = 1;
	int32 TiersPerRedundancyStep = 2;
	float BaseWeaknessRewardMultiplier = 1.0f;
	float WeaknessRewardPerTier = 0.15f;

	bool Validate(FString& OutError) const;
	bool Evaluate(
		int32 DifficultyTier,
		FABTSM73BeamD0DifficultyMetrics& OutMetrics,
		FString& OutError) const;
};

/** One semantic gameplay profile and all internal generation policy it owns. */
struct FABTSM73BeamD0ProfileDefinition
{
	FName GameplayProfileId;
	EABTSM73BeamD0WeaknessIntent WeaknessIntent =
		EABTSM73BeamD0WeaknessIntent::ColumnBreak;
	EABTSM73BeamD0MaterialPalette MaterialPalette =
		EABTSM73BeamD0MaterialPalette::LightFrameFragileJoint;
	EABTSM73BeamD0DeviceIntent DeviceIntent =
		EABTSM73BeamD0DeviceIntent::None;
	EABTSM73BeamD0CollapseIntent CollapseIntent =
		EABTSM73BeamD0CollapseIntent::ProgressiveFold;
	EABTSM73DAG5BV2Archetype Archetype =
		EABTSM73DAG5BV2Archetype::TerracedCitadel;

	float BaseWidthCM = 1800.0f;
	float BaseDepthCM = 1000.0f;
	float BaseHeightCM = 2400.0f;
	float BaseTargetBaySpanCM = 480.0f;
	int32 BaseMaximumGrammarDepth = 4;
	int32 BaseMotifGrammarDepth = 2;
	int32 BaseParallelBlockCount = 2;
	float StackWeight = 3.0f;
	float HorizontalSplitWeight = 2.4f;
	float SetbackWeight = 2.8f;
	float BridgeChance = 0.0f;
	float BoxWeight = 1.0f;
	float PrismWeight = 1.35f;
	float PyramidWeight = 1.15f;
	FABTSM73BeamD0DifficultyCurve DifficultyCurve;
};

/** Exact resolved identity and private upstream settings for one building. */
struct FABTSM73BeamD0ResolvedProfile
{
	bool bAccepted = false;
	FName GameplayProfileId;
	int32 DifficultyTier = INDEX_NONE;
	FName ResolvedM7ProfileId;
	int64 ProfileCatalogHash = 0;
	int64 ResolvedSettingsHash = 0;
	EABTSM73BeamD0WeaknessIntent WeaknessIntent =
		EABTSM73BeamD0WeaknessIntent::ColumnBreak;
	EABTSM73BeamD0MaterialPalette MaterialPalette =
		EABTSM73BeamD0MaterialPalette::LightFrameFragileJoint;
	EABTSM73BeamD0DeviceIntent DeviceIntent =
		EABTSM73BeamD0DeviceIntent::None;
	EABTSM73BeamD0CollapseIntent CollapseIntent =
		EABTSM73BeamD0CollapseIntent::ProgressiveFold;
	FABTSM73BeamD0DifficultyMetrics Difficulty;
	FABTSM73BeamD0VisualComplexityRecipe VisualComplexity;
	FABTSM73BeamCPreviewSettings BeamSettings;
	FString RejectReason;
};

/**
 * M7-private catalog and single settings resolver. The shared M3 contract is
 * deliberately not referenced here; Beam-E owns that versioned integration.
 */
class FABTSM73BeamD0ProfileCatalog
{
public:
	explicit FABTSM73BeamD0ProfileCatalog(
		TArray<FABTSM73BeamD0ProfileDefinition> InDefinitions,
		int32 InCatalogVersion = 1);

	static const FABTSM73BeamD0ProfileCatalog& GetDefault();

	bool Validate(FString& OutError) const;
	bool Resolve(
		FName GameplayProfileId,
		int32 DifficultyTier,
		int32 DeterministicSeed,
		FABTSM73BeamD0ResolvedProfile& OutProfile,
		FString& OutError) const;

	int64 GetCatalogHash() const { return CatalogHash; }
	int32 GetCatalogVersion() const { return CatalogVersion; }
	const TArray<FABTSM73BeamD0ProfileDefinition>& GetDefinitions() const
	{
		return Definitions;
	}

private:
	TArray<FABTSM73BeamD0ProfileDefinition> Definitions;
	int32 CatalogVersion = 1;
	int64 CatalogHash = 0;
};
