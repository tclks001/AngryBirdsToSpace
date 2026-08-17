// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSM73BeamD0ProfileCatalog.h"

#include "Misc/Crc.h"

namespace ABTSM73BeamD0
{
	FABTSM73BeamD0VisualComplexityRecipe VisualRecipe(const int32 Tier)
	{
		FABTSM73BeamD0VisualComplexityRecipe Recipe;
		Recipe.MilestoneTier = Tier;
		Recipe.MaximumCandidateAttempts = 6;
		switch (Tier)
		{
		case 0:
			Recipe.MaximumCandidateAttempts = 12;
			Recipe.MinimumBrickCount = 20;
			// Skeleton-first V3 adds a non-negotiable grounded core to the
			// four-face frame. The canonical five-profile E1 range is 84..110;
			// retain a coarse guard instead of density-searching away the core.
			Recipe.MaximumBrickCount = 149;
			Recipe.BoundsScale = 0.50f;
			Recipe.BaySpanScale = 1.70f;
			Recipe.ShapeGrammarDepth = 2;
			Recipe.MotifGrammarDepth = 1;
			Recipe.TargetShapeVolumeCount = 12;
			// Keep the macro silhouette and full-height roof, but spend the easy
			// tier's scarce Brick budget on one stable core instead of duplicate
			// long-post frame bays inside the same semantic volume.
			Recipe.MaximumBaysPerVolume = 1;
			Recipe.MaximumParallelBlocksPerCourse = 2;
			Recipe.MaximumRoofCourseCount = 64;
			Recipe.SingleTerminalRoofCourseCount = 8;
			Recipe.bRequireSingleTerminalRoof = true;
			break;
		case 1:
			Recipe.MinimumBrickCount = 150;
			Recipe.MaximumBrickCount = 349;
			Recipe.BoundsScale = 0.82f;
			Recipe.BaySpanScale = 1.30f;
			Recipe.ShapeGrammarDepth = 2;
			Recipe.MotifGrammarDepth = 2;
			Recipe.TargetShapeVolumeCount = 20;
			Recipe.MaximumBaysPerVolume = 3;
			Recipe.MaximumParallelBlocksPerCourse = 2;
			Recipe.MaximumRoofCourseCount = 64;
			Recipe.SingleTerminalRoofCourseCount = 10;
			Recipe.bRequireSingleTerminalRoof = true;
			break;
		case 2:
			// RC release fallback: keep the encounter identity and gameplay
			// profile, but spend only an E2-sized independent-body budget.  The
			// six encounters interpolate inside the proven E1/E2 range instead
			// of asking Chaos to carry thousands of tiny bodies.
			Recipe.MilestoneTier = 1;
			Recipe.MinimumBrickCount = 150;
			Recipe.MaximumBrickCount = 349;
			Recipe.BoundsScale = 0.84f;
			Recipe.BaySpanScale = 1.30f;
			Recipe.ShapeGrammarDepth = 2;
			Recipe.MotifGrammarDepth = 2;
			Recipe.TargetShapeVolumeCount = 20;
			Recipe.MaximumBaysPerVolume = 3;
			Recipe.MaximumParallelBlocksPerCourse = 2;
			Recipe.MaximumRoofCourseCount = 64;
			Recipe.SingleTerminalRoofCourseCount = 10;
			Recipe.bRequireSingleTerminalRoof = true;
			break;
		case 3:
			Recipe.MilestoneTier = 1;
			Recipe.MinimumBrickCount = 180;
			Recipe.MaximumBrickCount = 349;
			Recipe.BoundsScale = 0.86f;
			Recipe.BaySpanScale = 1.30f;
			Recipe.ShapeGrammarDepth = 2;
			Recipe.MotifGrammarDepth = 2;
			Recipe.TargetShapeVolumeCount = 20;
			Recipe.MaximumBaysPerVolume = 3;
			Recipe.MaximumParallelBlocksPerCourse = 2;
			Recipe.MaximumRoofCourseCount = 64;
			Recipe.SingleTerminalRoofCourseCount = 10;
			Recipe.bRequireSingleTerminalRoof = true;
			break;
		case 4:
			Recipe.MilestoneTier = 1;
			Recipe.MinimumBrickCount = 210;
			Recipe.MaximumBrickCount = 349;
			Recipe.BoundsScale = 0.88f;
			Recipe.BaySpanScale = 1.30f;
			Recipe.ShapeGrammarDepth = 2;
			Recipe.MotifGrammarDepth = 2;
			Recipe.TargetShapeVolumeCount = 20;
			Recipe.MaximumBaysPerVolume = 3;
			Recipe.MaximumParallelBlocksPerCourse = 2;
			Recipe.MaximumRoofCourseCount = 64;
			Recipe.SingleTerminalRoofCourseCount = 10;
			Recipe.bRequireSingleTerminalRoof = true;
			break;
		default:
			Recipe.MilestoneTier = 1;
			Recipe.MinimumBrickCount = 240;
			Recipe.MaximumBrickCount = 349;
			Recipe.BoundsScale = 0.90f;
			Recipe.BaySpanScale = 1.30f;
			Recipe.ShapeGrammarDepth = 2;
			Recipe.MotifGrammarDepth = 2;
			Recipe.TargetShapeVolumeCount = 20;
			Recipe.MaximumBaysPerVolume = 3;
			Recipe.MaximumParallelBlocksPerCourse = 2;
			Recipe.MaximumRoofCourseCount = 64;
			Recipe.SingleTerminalRoofCourseCount = 10;
			Recipe.bRequireSingleTerminalRoof = true;
			break;
		}
		return Recipe;
	}

	FABTSM73BeamC3CribCoreSettings StabilityCoreRecipe(
		const int32 Tier,
		const int32 MaximumFinalMemberCount)
	{
		FABTSM73BeamC3CribCoreSettings Settings;
		Settings.MaximumFinalMemberCount = MaximumFinalMemberCount;
		// Safety geometry is height-driven. Difficulty must not make an easy
		// building less stable or add decorative belts to a hard one.
		Settings.TargetBeltCount = 1;
		Settings.MaximumHostCount =
			Tier == 0 ? 1
			: Tier == 1 ? 4
			: Tier == 2 ? 16
			: Tier == 3 ? 32
			: Tier == 4 ? 64 : 96;
		Settings.MaximumNetMemberIncrease =
			Tier == 0 ? 12
			// A rooted C3 tie is one horizontal course plus the two real
			// contact splits at its Z-post endpoints. Keep E2's allowance on
			// that three-member quantum instead of forcing an unsafe whole-frame
			// deletion when the cumulative C2+C3 delta is exactly 33.
			: Tier == 1 ? 33
			: Tier == 2 ? 64
			: Tier == 3 ? 128
			: Tier == 4 ? 256 : 384;
		Settings.BeamC2MemberReserve =
			Tier == 0 ? 2
			: Tier == 1 ? 8
			: Tier == 2 ? 16
			: Tier == 3 ? 32
			: Tier == 4 ? 64 : 96;
		// Stability geometry wins over one protected interior roof lane at every
		// tier. The donor selector preserves eaves and the unique ridge, so this is
		// a bounded silhouette-neutral fallback rather than a difficulty reduction.
		Settings.bAllowRoofLaneBudgetReallocation = true;
		return Settings;
	}

	FABTSM73BeamC3V2CoupledExteriorFrameSettings CoupledExteriorFrameRecipe(
		const int32 Tier,
		const int32 MaximumFinalMemberCount)
	{
		FABTSM73BeamC3V2CoupledExteriorFrameSettings Settings;
		Settings.MaximumFinalMemberCount = MaximumFinalMemberCount;

		// These are topology identities, not search ranges. The course sequence
		// preserves complete X/Y pairs and the rail ladder is the Stage-1 contract.
		static constexpr int32 CourseCounts[] = {8, 16, 30, 44, 60, 76};
		static constexpr int32 MinimumCourseCounts[] = {8, 8, 22, 36, 52, 68};
		static constexpr int32 RailCounts[] = {2, 2, 3, 3, 4, 5};
		static constexpr int32 MaximumCellCounts[] = {1, 1, 2, 2, 3, 4};
		static constexpr int32 MaximumMacroBandCounts[] = {1, 1, 2, 2, 3, 4};
		static constexpr int32 MinimumStructuralBudgets[] = {28, 28, 82, 140, 268, 436};
		static constexpr int32 MaximumStructuralCounts[] = {28, 44, 244, 328, 900, 1904};

		const int32 ClampedTier = FMath::Clamp(Tier, 0, 5);
		Settings.CourseCount = CourseCounts[ClampedTier];
		Settings.MinimumCourseCount = MinimumCourseCounts[ClampedTier];
		Settings.RailCount = RailCounts[ClampedTier];
		Settings.MaximumCellCount = MaximumCellCounts[ClampedTier];
		Settings.MaximumMacroBandCount = MaximumMacroBandCounts[ClampedTier];
		Settings.MinimumStructuralMemberBudget =
			MinimumStructuralBudgets[ClampedTier];
		Settings.MaximumStructuralMemberCount =
			MaximumStructuralCounts[ClampedTier];

		// E1 has no lower valid topology. E2-E6 expose exactly two registered
		// reductions: four courses (and, where available, one cell) per level.
		Settings.MaximumFallbackLevel = ClampedTier == 0 ? 0 : 2;
		Settings.CourseReductionPerFallbackLevel = ClampedTier == 0 ? 0 : 4;
		Settings.CellReductionPerFallbackLevel =
			ClampedTier >= 4 ? 1 : 0;
		return Settings;
	}

	bool IsFinitePositive(const float Value)
	{
		return FMath::IsFinite(Value) && Value > 0.0f;
	}

	FABTSM73BeamD0ProfileDefinition MakeProfile(
		const FName Id,
		const EABTSM73BeamD0WeaknessIntent Weakness,
		const EABTSM73BeamD0MaterialPalette Palette,
		const EABTSM73BeamD0DeviceIntent Device,
		const EABTSM73BeamD0CollapseIntent Collapse,
		const EABTSM73DAG5BV2Archetype Archetype,
		const FVector& BoundsCM)
	{
		FABTSM73BeamD0ProfileDefinition Profile;
		Profile.GameplayProfileId = Id;
		Profile.WeaknessIntent = Weakness;
		Profile.MaterialPalette = Palette;
		Profile.DeviceIntent = Device;
		Profile.CollapseIntent = Collapse;
		Profile.Archetype = Archetype;
		Profile.BaseWidthCM = BoundsCM.X;
		Profile.BaseDepthCM = BoundsCM.Y;
		Profile.BaseHeightCM = BoundsCM.Z;
		return Profile;
	}

	TArray<FABTSM73BeamD0ProfileDefinition> BuildDefaultDefinitions()
	{
		TArray<FABTSM73BeamD0ProfileDefinition> Result;

		FABTSM73BeamD0ProfileDefinition ColumnBreak = MakeProfile(
			TEXT("ColumnBreak"),
			EABTSM73BeamD0WeaknessIntent::ColumnBreak,
			EABTSM73BeamD0MaterialPalette::LightFrameFragileJoint,
			EABTSM73BeamD0DeviceIntent::None,
			EABTSM73BeamD0CollapseIntent::ProgressiveFold,
			EABTSM73DAG5BV2Archetype::TwinTowerComplex,
			FVector(1900.0f, 1000.0f, 2500.0f));
		ColumnBreak.BaseParallelBlockCount = 2;
		ColumnBreak.StackWeight = 3.4f;
		ColumnBreak.HorizontalSplitWeight = 2.2f;
		ColumnBreak.SetbackWeight = 2.3f;
		ColumnBreak.BridgeChance = 0.15f;
		ColumnBreak.DifficultyCurve.BaseTargetDamageCost = 1.4f;
		Result.Add(ColumnBreak);

		FABTSM73BeamD0ProfileDefinition SeamRelease = MakeProfile(
			TEXT("SeamRelease"),
			EABTSM73BeamD0WeaknessIntent::SeamRelease,
			EABTSM73BeamD0MaterialPalette::MasonryWithWoodSeam,
			EABTSM73BeamD0DeviceIntent::BreakableSeam,
			EABTSM73BeamD0CollapseIntent::BridgeRelease,
			EABTSM73DAG5BV2Archetype::BridgedArcology,
			FVector(2400.0f, 1150.0f, 2400.0f));
		SeamRelease.BaseTargetBaySpanCM = 520.0f;
		SeamRelease.BaseParallelBlockCount = 3;
		SeamRelease.HorizontalSplitWeight = 3.1f;
		SeamRelease.SetbackWeight = 2.0f;
		SeamRelease.BridgeChance = 0.90f;
		SeamRelease.DifficultyCurve.BaseWeaknessExposureRatio = 0.66f;
		Result.Add(SeamRelease);

		FABTSM73BeamD0ProfileDefinition TipOver = MakeProfile(
			TEXT("TipOver"),
			EABTSM73BeamD0WeaknessIntent::TipOver,
			EABTSM73BeamD0MaterialPalette::IronFrameGlassTrigger,
			EABTSM73BeamD0DeviceIntent::None,
			EABTSM73BeamD0CollapseIntent::DirectedTip,
			EABTSM73DAG5BV2Archetype::SpiredCampus,
			FVector(1600.0f, 900.0f, 2850.0f));
		TipOver.BaseTargetBaySpanCM = 440.0f;
		TipOver.BaseParallelBlockCount = 2;
		TipOver.StackWeight = 3.8f;
		TipOver.HorizontalSplitWeight = 1.7f;
		TipOver.SetbackWeight = 3.4f;
		TipOver.BridgeChance = 0.05f;
		TipOver.PrismWeight = 1.7f;
		TipOver.PyramidWeight = 1.5f;
		TipOver.DifficultyCurve.BaseAimToleranceDegrees = 15.0f;
		Result.Add(TipOver);

		FABTSM73BeamD0ProfileDefinition DropTrigger = MakeProfile(
			TEXT("DropTrigger"),
			EABTSM73BeamD0WeaknessIntent::DropTrigger,
			EABTSM73BeamD0MaterialPalette::SuspendedStonePod,
			EABTSM73BeamD0DeviceIntent::HangingMass,
			EABTSM73BeamD0CollapseIntent::SupportedDrop,
			EABTSM73DAG5BV2Archetype::TerracedCitadel,
			FVector(2050.0f, 1200.0f, 2600.0f));
		DropTrigger.BaseTargetBaySpanCM = 500.0f;
		DropTrigger.BaseParallelBlockCount = 3;
		DropTrigger.StackWeight = 3.2f;
		DropTrigger.HorizontalSplitWeight = 2.8f;
		DropTrigger.SetbackWeight = 2.7f;
		DropTrigger.BridgeChance = 0.35f;
		DropTrigger.DifficultyCurve.BaseWeaknessRewardMultiplier = 1.2f;
		Result.Add(DropTrigger);

		FABTSM73BeamD0ProfileDefinition SlideRelease = MakeProfile(
			TEXT("SlideRelease"),
			EABTSM73BeamD0WeaknessIntent::SlideRelease,
			EABTSM73BeamD0MaterialPalette::MasonryWithWoodSeam,
			EABTSM73BeamD0DeviceIntent::BreakableSeam,
			EABTSM73BeamD0CollapseIntent::LateralSlide,
			EABTSM73DAG5BV2Archetype::TerracedCitadel,
			FVector(2250.0f, 1000.0f, 2250.0f));
		SlideRelease.BaseTargetBaySpanCM = 540.0f;
		SlideRelease.BaseParallelBlockCount = 2;
		SlideRelease.StackWeight = 2.5f;
		SlideRelease.HorizontalSplitWeight = 3.3f;
		SlideRelease.SetbackWeight = 2.1f;
		SlideRelease.BridgeChance = 0.25f;
		SlideRelease.DifficultyCurve.BaseTargetDamageCost = 1.2f;
		Result.Add(SlideRelease);

		return Result;
	}

	FString CurveCanonical(const FABTSM73BeamD0DifficultyCurve& Curve)
	{
		return FString::Printf(
			TEXT("Tier=%d:%d|Damage=%.6f:%.6f|Exposure=%.6f:%.6f:%.6f|")
			TEXT("Aim=%.6f:%.6f:%.6f|Redundancy=%d:%d|Reward=%.6f:%.6f"),
			Curve.MinimumTier,
			Curve.MaximumTier,
			Curve.BaseTargetDamageCost,
			Curve.TargetDamageCostPerTier,
			Curve.BaseWeaknessExposureRatio,
			Curve.WeaknessExposureDropPerTier,
			Curve.MinimumWeaknessExposureRatio,
			Curve.BaseAimToleranceDegrees,
			Curve.AimToleranceDropPerTier,
			Curve.MinimumAimToleranceDegrees,
			Curve.BaseSupportRedundancy,
			Curve.TiersPerRedundancyStep,
			Curve.BaseWeaknessRewardMultiplier,
			Curve.WeaknessRewardPerTier);
	}

	FString DefinitionCanonical(const FABTSM73BeamD0ProfileDefinition& Profile)
	{
		return FString::Printf(
			TEXT("Id=%s|W=%d|M=%d|D=%d|C=%d|A=%d|Bounds=%.6f:%.6f:%.6f|")
			TEXT("Bay=%.6f|Depth=%d:%d|Parallel=%d|Rules=%.6f:%.6f:%.6f:%.6f|")
			TEXT("Primitive=%.6f:%.6f:%.6f|%s"),
			*Profile.GameplayProfileId.ToString(),
			static_cast<int32>(Profile.WeaknessIntent),
			static_cast<int32>(Profile.MaterialPalette),
			static_cast<int32>(Profile.DeviceIntent),
			static_cast<int32>(Profile.CollapseIntent),
			static_cast<int32>(Profile.Archetype),
			Profile.BaseWidthCM,
			Profile.BaseDepthCM,
			Profile.BaseHeightCM,
			Profile.BaseTargetBaySpanCM,
			Profile.BaseMaximumGrammarDepth,
			Profile.BaseMotifGrammarDepth,
			Profile.BaseParallelBlockCount,
			Profile.StackWeight,
			Profile.HorizontalSplitWeight,
			Profile.SetbackWeight,
			Profile.BridgeChance,
			Profile.BoxWeight,
			Profile.PrismWeight,
			Profile.PyramidWeight,
			*CurveCanonical(Profile.DifficultyCurve));
	}

	int64 CalculateCatalogHash(
		const int32 CatalogVersion,
		const TArray<FABTSM73BeamD0ProfileDefinition>& Definitions)
	{
		FString Canonical = FString::Printf(TEXT("BeamD0CatalogV=%d"), CatalogVersion);
		for (const FABTSM73BeamD0ProfileDefinition& Definition : Definitions)
		{
			Canonical += TEXT("|");
			Canonical += DefinitionCanonical(Definition);
		}
		return static_cast<int64>(FCrc::StrCrc32(*Canonical));
	}

	int64 CalculateResolvedHash(
		const FABTSM73BeamD0ResolvedProfile& Profile,
		const int32 DeterministicSeed)
	{
		const FABTSM73DAG5BV2PreviewSettings& Shape =
			Profile.BeamSettings.BeamB.BeamA.Silhouette;
		const FString Canonical = FString::Printf(
			TEXT("Catalog=%lld|Resolved=%s|Tier=%d|Seed=%d|W=%d|M=%d|D=%d|C=%d|")
			TEXT("Metrics=%.6f:%.6f:%.6f:%d:%.6f:%d|")
			TEXT("Shape=%d:%.6f:%.6f:%.6f:%d:%d:%d:%d:%.6f:")
			TEXT("%d:%.6f:%.6f:%d:%d:%.6f:%.6f:%.6f|")
			TEXT("Beam=%.6f:%.6f:%.6f:%.6f:%.6f:%d:%d:%d:%d:%d:%d:%d:%d|")
			TEXT("Visual=%d:%d:%d:%d:%.6f:%.6f:%d:%d:%d:%d:%d:%d:%d:%d:%d|")
			TEXT("C2=%d:%.6f:%.6f:%.6f:%.6f:%.6f:%d:%d|")
			TEXT("C3V2=%d:%d:%d:%d:%d:%.6f:%.6f:%d:%d:%d:%d:%d:%d:%d:%d:%d|")
			TEXT("C3V1=%d:%.6f:%.6f:%d:%d:%d:%d:%d:%d"),
			Profile.ProfileCatalogHash,
			*Profile.ResolvedM7ProfileId.ToString(),
			Profile.DifficultyTier,
			DeterministicSeed,
			static_cast<int32>(Profile.WeaknessIntent),
			static_cast<int32>(Profile.MaterialPalette),
			static_cast<int32>(Profile.DeviceIntent),
			static_cast<int32>(Profile.CollapseIntent),
			Profile.Difficulty.TargetDamageCost,
			Profile.Difficulty.WeaknessExposureRatio,
			Profile.Difficulty.AimToleranceDegrees,
			Profile.Difficulty.SupportRedundancy,
			Profile.Difficulty.WeaknessRewardMultiplier,
			Profile.Difficulty.SolutionSteps,
			static_cast<int32>(Shape.Archetype),
			Shape.TargetWidthCM,
			Shape.TargetDepthCM,
			Shape.TargetHeightCM,
			Shape.MaxGrammarDepth,
			Shape.ComplexityMilestoneTier,
			Profile.BeamSettings.BeamB.GrammarDepth,
			Shape.bRequireSingleTerminalRoof ? 1 : 0,
			Shape.SingleTerminalRoofHeightCM,
			Shape.bMergeRoofTerminals ? 1 : 0,
			Shape.RoofMergeGapCM,
			Shape.RoofCourseHeightCM,
			Shape.MinimumRoofCourseCount,
			Shape.MaximumRoofCourseCount,
			Shape.RoofHeightToShortSpanRatio,
			Shape.PyramidPreferredMaxAspectRatio,
			Shape.PrismPreferredMinAspectRatio,
			Profile.BeamSettings.BeamB.BeamA.TargetBaySpanCM,
			Profile.BeamSettings.BeamB.BeamA.MaximumVerticalSupportSpanCM,
			Profile.BeamSettings.BeamB.BeamA.BlockCrossSectionCM,
			Profile.BeamSettings.BeamB.BeamA.MinimumParallelBlockGapCM,
			Profile.BeamSettings.BeamB.BeamA.TwoBlockMergeGapCM,
			Profile.BeamSettings.BeamB.BeamA.MaxParallelBlocksPerCourse,
			Profile.BeamSettings.BeamB.BeamA.MaxFrameParallelBlocksPerCourse,
			Profile.BeamSettings.BeamB.bRequireMotifVariety ? 1 : 0,
			Profile.BeamSettings.BeamB.BeamA.MaxBayCount,
			Profile.BeamSettings.BeamB.BeamA.MaxJointCount,
			Profile.BeamSettings.BeamB.BeamA.MaxMemberCount,
			Profile.BeamSettings.BeamB.BeamA.MaxBearingContactCount,
			Profile.BeamSettings.BeamB.BeamA.MaxBearingPairChecks,
			Profile.VisualComplexity.MilestoneTier,
			Profile.VisualComplexity.MinimumBrickCount,
			Profile.VisualComplexity.MaximumBrickCount,
			Profile.VisualComplexity.MaximumCandidateAttempts,
			Profile.VisualComplexity.BoundsScale,
			Profile.VisualComplexity.BaySpanScale,
			Profile.VisualComplexity.ShapeGrammarDepth,
			Profile.VisualComplexity.MotifGrammarDepth,
			Profile.VisualComplexity.TargetShapeVolumeCount,
			Profile.VisualComplexity.MaximumBaysPerVolume,
			Profile.VisualComplexity.MaximumParallelBlocksPerCourse,
			Profile.VisualComplexity.SingleTerminalRoofCourseCount,
			Profile.VisualComplexity.bRequirePrimitiveVariety ? 1 : 0,
			Profile.VisualComplexity.bRequireSingleTerminalRoof ? 1 : 0,
			Profile.VisualComplexity.bRequireMotifVariety ? 1 : 0,
			Profile.BeamSettings.bRequireRealContactAgreement ? 1 : 0,
			Profile.BeamSettings.RealContactToleranceCM,
			Profile.BeamSettings.RealContactAreaToleranceRatio,
			Profile.BeamSettings.MinimumSingleSupportCoverageRatio,
			Profile.BeamSettings.MinimumSeparatedSupportSpanRatio,
			Profile.BeamSettings.SupportResultantMarginCM,
			Profile.BeamSettings.MaximumStructuralClosurePasses,
			Profile.BeamSettings.MaximumStructuralSupportPosts,
			Profile.CoupledExteriorFrame.bEnabled ? 1 : 0,
			Profile.CoupledExteriorFrame.CourseCount,
			Profile.CoupledExteriorFrame.RailCount,
			Profile.CoupledExteriorFrame.MaximumCellCount,
			Profile.CoupledExteriorFrame.MaximumMacroBandCount,
			Profile.CoupledExteriorFrame.MaximumMemberLengthCM,
			Profile.CoupledExteriorFrame.MaximumPostSegmentSpanCM,
			Profile.CoupledExteriorFrame.MinimumCourseCount,
			Profile.CoupledExteriorFrame.MinimumStructuralMemberBudget,
			Profile.CoupledExteriorFrame.MaximumStructuralMemberCount,
			Profile.CoupledExteriorFrame.MaximumFinalMemberCount,
			Profile.CoupledExteriorFrame.MaximumFallbackLevel,
			Profile.CoupledExteriorFrame.CourseReductionPerFallbackLevel,
			Profile.CoupledExteriorFrame.CellReductionPerFallbackLevel,
			Profile.CoupledExteriorFrame.MinimumMacroBandStrideCourses,
			Profile.CoupledExteriorFrame.MaximumMacroBandStrideCourses,
			Profile.StabilityCore.bEnabled ? 1 : 0,
			Profile.StabilityCore.MaximumUnbracedCorePostSpanCM,
			Profile.StabilityCore.MinimumCoreArmSpanCM,
			Profile.StabilityCore.TargetBeltCount,
			Profile.StabilityCore.MaximumHostCount,
			Profile.StabilityCore.MaximumNetMemberIncrease,
			Profile.StabilityCore.MaximumFinalMemberCount,
			Profile.StabilityCore.BeamC2MemberReserve,
			Profile.StabilityCore.bAllowRoofLaneBudgetReallocation ? 1 : 0);
		return static_cast<int64>(FCrc::StrCrc32(*Canonical));
	}

	bool ValidateDefinition(
		const FABTSM73BeamD0ProfileDefinition& Profile,
		FString& OutError)
	{
		if (Profile.GameplayProfileId.IsNone()
			|| !IsFinitePositive(Profile.BaseWidthCM)
			|| !IsFinitePositive(Profile.BaseDepthCM)
			|| !IsFinitePositive(Profile.BaseHeightCM)
			|| !IsFinitePositive(Profile.BaseTargetBaySpanCM)
			|| Profile.BaseMaximumGrammarDepth < 2
			|| Profile.BaseMaximumGrammarDepth > 6
			|| Profile.BaseMotifGrammarDepth < 1
			|| Profile.BaseMotifGrammarDepth > 6
			|| Profile.BaseParallelBlockCount < 2
			|| Profile.BaseParallelBlockCount > 16
			|| !FMath::IsFinite(Profile.StackWeight)
			|| !FMath::IsFinite(Profile.HorizontalSplitWeight)
			|| !FMath::IsFinite(Profile.SetbackWeight)
			|| !FMath::IsFinite(Profile.BridgeChance)
			|| !FMath::IsFinite(Profile.BoxWeight)
			|| !FMath::IsFinite(Profile.PrismWeight)
			|| !FMath::IsFinite(Profile.PyramidWeight)
			|| Profile.StackWeight < 0.0f
			|| Profile.HorizontalSplitWeight < 0.0f
			|| Profile.SetbackWeight < 0.0f
			|| Profile.BridgeChance < 0.0f
			|| Profile.BridgeChance > 1.0f
			|| Profile.BoxWeight < 0.0f
			|| Profile.PrismWeight < 0.0f
			|| Profile.PyramidWeight < 0.0f
			|| Profile.BoxWeight + Profile.PrismWeight + Profile.PyramidWeight <= 0.0f
			|| Profile.Archetype == EABTSM73DAG5BV2Archetype::Auto
			|| static_cast<uint8>(Profile.Archetype)
				> static_cast<uint8>(EABTSM73DAG5BV2Archetype::SpiredCampus)
			|| static_cast<uint8>(Profile.WeaknessIntent)
				> static_cast<uint8>(EABTSM73BeamD0WeaknessIntent::SlideRelease)
			|| static_cast<uint8>(Profile.MaterialPalette)
				> static_cast<uint8>(EABTSM73BeamD0MaterialPalette::SuspendedStonePod)
			|| static_cast<uint8>(Profile.DeviceIntent)
				> static_cast<uint8>(EABTSM73BeamD0DeviceIntent::HangingMass)
			|| static_cast<uint8>(Profile.CollapseIntent)
				> static_cast<uint8>(EABTSM73BeamD0CollapseIntent::BridgeRelease))
		{
			OutError = TEXT("BeamD0InvalidProfileDefinition");
			return false;
		}
		return Profile.DifficultyCurve.Validate(OutError);
	}
}

bool FABTSM73BeamC3V2CoupledExteriorFrameSettings::Validate(
	FString& OutError) const
{
	const int32 MaximumFallbackCourseReduction =
		MaximumFallbackLevel * CourseReductionPerFallbackLevel;
	const int32 MaximumFallbackCellReduction =
		MaximumFallbackLevel * CellReductionPerFallbackLevel;
	const int32 RequiredMacroBandCount =
		1 + (FMath::Max(0, CourseCount - 22) + 21) / 22;
	const int32 MinimumMacroBandCount =
		1 + (FMath::Max(0, MinimumCourseCount - 22) + 21) / 22;
	const int32 MinimumStructuralCount =
		RailCount * MinimumCourseCount
			+ 4 * MinimumMacroBandCount * (RailCount + 1);
	const int32 MaximumStructuralCount = MaximumCellCount
		* (RailCount * CourseCount
			+ 4 * MaximumMacroBandCount * (RailCount + 1));
	if (CourseCount < 8
		|| CourseCount % 2 != 0
		|| RailCount < 2 || RailCount > 5
		|| MaximumCellCount < 1 || MaximumCellCount > 16
		|| MaximumMacroBandCount < 1
		|| MaximumMacroBandCount > 32
		|| MaximumMacroBandCount != RequiredMacroBandCount
		|| !ABTSM73BeamD0::IsFinitePositive(MaximumMemberLengthCM)
		|| !ABTSM73BeamD0::IsFinitePositive(MaximumPostSegmentSpanCM)
		|| MaximumMemberLengthCM > 720.0f
		|| MaximumPostSegmentSpanCM > MaximumMemberLengthCM
		|| MinimumCourseCount < 8
		|| MinimumCourseCount > CourseCount
		|| MinimumCourseCount % 2 != 0
		|| MinimumStructuralMemberBudget != MinimumStructuralCount
		|| MaximumStructuralMemberCount != MaximumStructuralCount
		|| MaximumFinalMemberCount < MaximumStructuralMemberCount
		|| MaximumFallbackLevel < 0 || MaximumFallbackLevel > 2
		|| CourseReductionPerFallbackLevel < 0
		|| CourseReductionPerFallbackLevel % 2 != 0
		|| CellReductionPerFallbackLevel < 0
		|| CourseCount - MaximumFallbackCourseReduction
			< MinimumCourseCount
		|| MaximumCellCount - MaximumFallbackCellReduction < 1
		|| MinimumMacroBandStrideCourses < 6
		|| MinimumMacroBandStrideCourses % 2 != 0
		|| MaximumMacroBandStrideCourses < MinimumMacroBandStrideCourses
		|| MaximumMacroBandStrideCourses > 22
		|| MaximumMacroBandStrideCourses % 2 != 0)
	{
		OutError = TEXT("BeamD0InvalidCoupledExteriorFrameRecipe");
		return false;
	}
	OutError.Reset();
	return true;
}

bool FABTSM73BeamD0VisualComplexityRecipe::Validate(FString& OutError) const
{
	if (MilestoneTier < 0 || MilestoneTier > 5
		|| MinimumBrickCount < 1
		|| MaximumBrickCount < MinimumBrickCount
		|| MaximumCandidateAttempts < 1
		|| !ABTSM73BeamD0::IsFinitePositive(BoundsScale)
		|| !ABTSM73BeamD0::IsFinitePositive(BaySpanScale)
		|| ShapeGrammarDepth < 1 || ShapeGrammarDepth > 6
		|| MotifGrammarDepth < 1 || MotifGrammarDepth > 6
		|| TargetShapeVolumeCount < 3 || TargetShapeVolumeCount > 256
		|| MaximumBaysPerVolume < 1 || MaximumBaysPerVolume > 16
		|| MaximumParallelBlocksPerCourse < 2
		|| MaximumParallelBlocksPerCourse > 16
		|| MaximumRoofCourseCount < 2 || MaximumRoofCourseCount > 64
		|| (bRequireSingleTerminalRoof
			&& (SingleTerminalRoofCourseCount < 8
				|| SingleTerminalRoofCourseCount > MaximumRoofCourseCount))
		|| (!bRequireSingleTerminalRoof
			&& SingleTerminalRoofCourseCount != 0))
	{
		OutError = TEXT("BeamD0InvalidVisualComplexityRecipe");
		return false;
	}
	OutError.Reset();
	return true;
}

bool FABTSM73BeamD0DifficultyCurve::Validate(FString& OutError) const
{
	if (MinimumTier < 0
		|| MaximumTier < MinimumTier
		|| MaximumTier > 32
		|| !ABTSM73BeamD0::IsFinitePositive(BaseTargetDamageCost)
		|| !FMath::IsFinite(TargetDamageCostPerTier)
		|| TargetDamageCostPerTier < 0.0f
		|| !ABTSM73BeamD0::IsFinitePositive(BaseWeaknessExposureRatio)
		|| BaseWeaknessExposureRatio > 1.0f
		|| !FMath::IsFinite(WeaknessExposureDropPerTier)
		|| WeaknessExposureDropPerTier < 0.0f
		|| !ABTSM73BeamD0::IsFinitePositive(MinimumWeaknessExposureRatio)
		|| MinimumWeaknessExposureRatio > BaseWeaknessExposureRatio
		|| !ABTSM73BeamD0::IsFinitePositive(BaseAimToleranceDegrees)
		|| !FMath::IsFinite(AimToleranceDropPerTier)
		|| AimToleranceDropPerTier < 0.0f
		|| !ABTSM73BeamD0::IsFinitePositive(MinimumAimToleranceDegrees)
		|| MinimumAimToleranceDegrees > BaseAimToleranceDegrees
		|| BaseSupportRedundancy < 1
		|| TiersPerRedundancyStep < 1
		|| !ABTSM73BeamD0::IsFinitePositive(BaseWeaknessRewardMultiplier)
		|| !FMath::IsFinite(WeaknessRewardPerTier)
		|| WeaknessRewardPerTier < 0.0f)
	{
		OutError = TEXT("BeamD0InvalidDifficultyCurve");
		return false;
	}
	OutError.Reset();
	return true;
}

bool FABTSM73BeamD0DifficultyCurve::Evaluate(
	const int32 DifficultyTier,
	FABTSM73BeamD0DifficultyMetrics& OutMetrics,
	FString& OutError) const
{
	OutMetrics = FABTSM73BeamD0DifficultyMetrics();
	if (!Validate(OutError))
	{
		return false;
	}
	if (DifficultyTier < MinimumTier || DifficultyTier > MaximumTier)
	{
		OutError = TEXT("BeamD0DifficultyTierOutOfRange");
		return false;
	}

	const int32 RelativeTier = DifficultyTier - MinimumTier;
	OutMetrics.TargetDamageCost =
		BaseTargetDamageCost + TargetDamageCostPerTier * RelativeTier;
	OutMetrics.WeaknessExposureRatio = FMath::Max(
		MinimumWeaknessExposureRatio,
		BaseWeaknessExposureRatio - WeaknessExposureDropPerTier * RelativeTier);
	OutMetrics.AimToleranceDegrees = FMath::Max(
		MinimumAimToleranceDegrees,
		BaseAimToleranceDegrees - AimToleranceDropPerTier * RelativeTier);
	OutMetrics.SupportRedundancy =
		BaseSupportRedundancy + RelativeTier / TiersPerRedundancyStep;
	OutMetrics.WeaknessRewardMultiplier =
		BaseWeaknessRewardMultiplier + WeaknessRewardPerTier * RelativeTier;
	OutMetrics.SolutionSteps = 1;
	OutError.Reset();
	return true;
}

FABTSM73BeamD0ProfileCatalog::FABTSM73BeamD0ProfileCatalog(
	TArray<FABTSM73BeamD0ProfileDefinition> InDefinitions,
	const int32 InCatalogVersion)
	: Definitions(MoveTemp(InDefinitions))
	, CatalogVersion(InCatalogVersion)
{
	Definitions.Sort([](
		const FABTSM73BeamD0ProfileDefinition& A,
		const FABTSM73BeamD0ProfileDefinition& B)
	{
		return A.GameplayProfileId.ToString() < B.GameplayProfileId.ToString();
	});
	CatalogHash = ABTSM73BeamD0::CalculateCatalogHash(CatalogVersion, Definitions);
}

const FABTSM73BeamD0ProfileCatalog& FABTSM73BeamD0ProfileCatalog::GetDefault()
{
	static const FABTSM73BeamD0ProfileCatalog Catalog(
		ABTSM73BeamD0::BuildDefaultDefinitions(), 11);
	return Catalog;
}

bool FABTSM73BeamD0ProfileCatalog::Validate(FString& OutError) const
{
	if (CatalogVersion < 1 || Definitions.IsEmpty())
	{
		OutError = TEXT("BeamD0CatalogEmpty");
		return false;
	}

	TSet<FName> SeenIds;
	for (const FABTSM73BeamD0ProfileDefinition& Definition : Definitions)
	{
		if (!ABTSM73BeamD0::ValidateDefinition(Definition, OutError))
		{
			return false;
		}
		if (SeenIds.Contains(Definition.GameplayProfileId))
		{
			OutError = TEXT("BeamD0DuplicateGameplayProfileId");
			return false;
		}
		SeenIds.Add(Definition.GameplayProfileId);
	}

	if (CatalogHash == 0)
	{
		OutError = TEXT("BeamD0InvalidCatalogHash");
		return false;
	}
	OutError.Reset();
	return true;
}

bool FABTSM73BeamD0ProfileCatalog::Resolve(
	const FName GameplayProfileId,
	const int32 DifficultyTier,
	const int32 DeterministicSeed,
	FABTSM73BeamD0ResolvedProfile& OutProfile,
	FString& OutError) const
{
	OutProfile = FABTSM73BeamD0ResolvedProfile();
	if (!Validate(OutError))
	{
		OutProfile.RejectReason = OutError;
		return false;
	}
	if (GameplayProfileId.IsNone())
	{
		OutError = TEXT("BeamD0UnknownGameplayProfileId");
		OutProfile.RejectReason = OutError;
		return false;
	}

	const FABTSM73BeamD0ProfileDefinition* Definition = Definitions.FindByPredicate(
		[GameplayProfileId](const FABTSM73BeamD0ProfileDefinition& Candidate)
		{
			return Candidate.GameplayProfileId == GameplayProfileId;
		});
	if (Definition == nullptr)
	{
		OutError = TEXT("BeamD0UnknownGameplayProfileId");
		OutProfile.RejectReason = OutError;
		return false;
	}

	if (!Definition->DifficultyCurve.Evaluate(
		DifficultyTier, OutProfile.Difficulty, OutError))
	{
		OutProfile.RejectReason = OutError;
		return false;
	}
	OutProfile.VisualComplexity = ABTSM73BeamD0::VisualRecipe(DifficultyTier);
	if (GameplayProfileId == TEXT("DropTrigger") && DifficultyTier == 0)
	{
		// The suspended-pod base envelope is materially larger than the other
		// E1 profiles. Reserve the eight-course roof and recover the 20-49
		// Brick window by contracting only its internal macro envelope.
		OutProfile.VisualComplexity.BoundsScale = 0.42f;
	}
	if (GameplayProfileId == TEXT("ColumnBreak")
		&& OutProfile.VisualComplexity.MilestoneTier == 3)
	{
		// V3 skeleton-first canonical counts are separated by hundreds of
		// members (E4/E5/E6 = 1348/1951/2515). Keep coarse hundred-scale
		// guards; never fit a boundary to one emitted count by +/- one.
		OutProfile.VisualComplexity.MaximumBrickCount = 1599;
	}
	if (GameplayProfileId == TEXT("ColumnBreak")
		&& OutProfile.VisualComplexity.MilestoneTier == 4)
	{
		OutProfile.VisualComplexity.MinimumBrickCount = 1600;
		OutProfile.VisualComplexity.MaximumBrickCount = 2199;
	}
	if (GameplayProfileId == TEXT("ColumnBreak")
		&& OutProfile.VisualComplexity.MilestoneTier == 5)
	{
		// E6 gains its visible step from the four-lane light frame and denser
		// bay budget. A sixth silhouette recursion or another envelope jump
		// over-fragments TwinTower closure without adding a new readable macro milestone.
		OutProfile.VisualComplexity.ShapeGrammarDepth = 4;
		OutProfile.VisualComplexity.BoundsScale = 1.22f;
		OutProfile.VisualComplexity.MinimumBrickCount = 2200;
		OutProfile.VisualComplexity.MaximumBrickCount = 3499;
	}
	if (!OutProfile.VisualComplexity.Validate(OutError))
	{
		OutProfile.RejectReason = OutError;
		return false;
	}
	OutProfile.StabilityCore = ABTSM73BeamD0::StabilityCoreRecipe(
		OutProfile.VisualComplexity.MilestoneTier,
		OutProfile.VisualComplexity.MaximumBrickCount);
	if (!OutProfile.StabilityCore.Validate(OutError))
	{
		OutProfile.RejectReason = OutError;
		return false;
	}
	OutProfile.CoupledExteriorFrame =
		ABTSM73BeamD0::CoupledExteriorFrameRecipe(
			OutProfile.VisualComplexity.MilestoneTier,
			OutProfile.VisualComplexity.MaximumBrickCount);
	if (!OutProfile.CoupledExteriorFrame.Validate(OutError))
	{
		OutProfile.RejectReason = OutError;
		return false;
	}

	OutProfile.GameplayProfileId = GameplayProfileId;
	OutProfile.DifficultyTier = DifficultyTier;
	OutProfile.ResolvedM7ProfileId = FName(*FString::Printf(
		TEXT("%s_T%d"), *GameplayProfileId.ToString(), DifficultyTier));
	OutProfile.ProfileCatalogHash = CatalogHash;
	OutProfile.WeaknessIntent = Definition->WeaknessIntent;
	OutProfile.MaterialPalette = Definition->MaterialPalette;
	OutProfile.DeviceIntent = Definition->DeviceIntent;
	OutProfile.CollapseIntent = Definition->CollapseIntent;

	FABTSM73BeamCPreviewSettings& Settings = OutProfile.BeamSettings;
	FABTSM73DAG5BV2PreviewSettings& Shape =
		Settings.BeamB.BeamA.Silhouette;
	Shape.BuildingSeed = DeterministicSeed;
	Shape.Archetype = Definition->Archetype;
	const FABTSM73BeamD0VisualComplexityRecipe& Visual =
		OutProfile.VisualComplexity;
	const float TierScale = Visual.BoundsScale;
	Shape.TargetWidthCM = Definition->BaseWidthCM * TierScale;
	Shape.TargetDepthCM = Definition->BaseDepthCM * TierScale;
	Shape.TargetHeightCM = Definition->BaseHeightCM * TierScale;
	Shape.MinGrammarDepth = Visual.ShapeGrammarDepth;
	Shape.MaxGrammarDepth = Visual.ShapeGrammarDepth;
	Shape.ComplexityMilestoneTier = Visual.MilestoneTier;
	Shape.TargetVolumeCount = Visual.TargetShapeVolumeCount;
	Shape.StackWeight = Definition->StackWeight;
	Shape.HorizontalSplitWeight = Definition->HorizontalSplitWeight;
	Shape.SetbackWeight = Definition->SetbackWeight;
	Shape.BridgeChance = Definition->BridgeChance;
	if (GameplayProfileId == TEXT("ColumnBreak")
		&& Visual.MilestoneTier >= 4)
	{
		// TwinTower's E5/E6 milestone is the paired vertical massing itself;
		// random SupportedSpan insertion creates ground-rescue bridges that
		// conflict with the light-frame structural closure.
		Shape.BridgeChance = 0.0f;
	}
	if (Visual.MilestoneTier >= 4
		&& Definition->Archetype
			== EABTSM73DAG5BV2Archetype::BridgedArcology)
	{
		// E5/E6 formally require at least one SupportedSpan for these
		// archetype, so the resolver must make that milestone reachable
		// instead of asking candidate search to win against a near-zero rate.
		Shape.BridgeChance = FMath::Max(Shape.BridgeChance, 0.35f);
	}
	Shape.BoxWeight = Definition->BoxWeight;
	Shape.PrismWeight = Definition->PrismWeight;
	Shape.PyramidWeight = Definition->PyramidWeight;
	Shape.bRequirePrimitiveVariety = Visual.bRequirePrimitiveVariety;
	Shape.bRequireSingleTerminalRoof =
		Visual.bRequireSingleTerminalRoof;
	Shape.RoofCourseHeightCM =
		Settings.BeamB.BeamA.BlockCrossSectionCM;
	Shape.MinimumRoofCourseCount = Visual.bRequireSingleTerminalRoof
		? Visual.SingleTerminalRoofCourseCount : 8;
	Shape.MaximumRoofCourseCount = Visual.MaximumRoofCourseCount;
	Shape.RoofHeightToShortSpanRatio = 0.90f;
	Shape.RoofMergeGapCM = FMath::Max(
		Shape.MinVolumeSpanCM,
		FMath::Max(Shape.TargetWidthCM, Shape.TargetDepthCM)
			* Shape.SplitGapRatio * 1.25f);
	if (Visual.bRequireSingleTerminalRoof)
	{
		Shape.SingleTerminalRoofHeightCM =
			Settings.BeamB.BeamA.BlockCrossSectionCM
				* Visual.SingleTerminalRoofCourseCount;
	}

	Settings.BeamB.BeamA.TargetBaySpanCM = FMath::Max(
		220.0f, Definition->BaseTargetBaySpanCM * Visual.BaySpanScale);
	// Density prototype: keep the 36 cm section and the final two-lane merge
	// contract, but require a 40 cm clear gap before a third/fourth parallel
	// lane is admitted. The first 72 cm probe proved the budget margin but
	// collapsed three-lane sections to two lanes and stalled Beam-C closure;
	// this registered value only compresses the dense four-lane case while
	// preserving the intermediate three-lane bearing pattern. It is one global
	// production value, not a per-Profile search range or Candidate fallback.
	Settings.BeamB.BeamA.MinimumParallelBlockGapCM = 40.0f;
	Settings.BeamB.BeamA.MaximumVerticalSupportSpanCM =
		OutProfile.StabilityCore.MaximumUnbracedCorePostSpanCM;
	if (GameplayProfileId == TEXT("ColumnBreak")
		&& Visual.MilestoneTier >= 4)
	{
		// E5 needs one wider bay to leave enough final-member capacity for the
		// physical C2 support cap. E6 keeps its denser certified four-lane frame.
		Settings.BeamB.BeamA.TargetBaySpanCM = FMath::Max(
			Settings.BeamB.BeamA.TargetBaySpanCM,
			Visual.MilestoneTier == 4 ? 473.0f : 420.0f);
	}
	Settings.BeamB.BeamA.MaxBaysPerVolume = Visual.MaximumBaysPerVolume;
	Settings.BeamB.BeamA.MaxParallelBlocksPerCourse =
		Visual.MaximumParallelBlocksPerCourse;
	Settings.BeamB.BeamA.MaxFrameParallelBlocksPerCourse =
		DifficultyTier == 0
			? 1
			: Visual.MaximumParallelBlocksPerCourse;
	Settings.BeamB.BeamA.MaxRoofCourseCount =
		Visual.MaximumRoofCourseCount;
	Settings.BeamB.GrammarDepth = Visual.MotifGrammarDepth;
	Settings.BeamB.bRequireMotifVariety = Visual.bRequireMotifVariety;
	Settings.BeamB.bAllowCantilever = false;
	Settings.BeamB.bAllowBracedBay = false;

	// The Stage-1 shell is downstream of Beam-B, but Beam-B must first be able
	// to publish its high-tier IR. These deterministic caps remove the E6
	// 8192-joint hard stop without changing the 36 cm section or visual window.
	if (Visual.MilestoneTier >= 4)
	{
		FABTSM73BeamAPreviewSettings& BeamA = Settings.BeamB.BeamA;
		BeamA.MaxJointCount = Visual.MilestoneTier == 4 ? 16384 : 32768;
		BeamA.MaxMemberCount = Visual.MilestoneTier == 4 ? 32768 : 65536;
		BeamA.MaxBearingContactCount = Visual.MilestoneTier == 4 ? 32768 : 65536;
		BeamA.MaxBearingPairChecks =
			Visual.MilestoneTier == 4 ? 524288 : 1048576;
	}

	OutProfile.ResolvedSettingsHash =
		ABTSM73BeamD0::CalculateResolvedHash(OutProfile, DeterministicSeed);
	OutProfile.bAccepted = true;
	OutProfile.RejectReason.Reset();
	OutError.Reset();
	return true;
}
