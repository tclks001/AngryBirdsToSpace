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
			Recipe.MaximumBrickCount = 49;
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
			Recipe.MinimumBrickCount = 60;
			Recipe.MaximumBrickCount = 199;
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
			Recipe.MinimumBrickCount = 300;
			Recipe.MaximumBrickCount = 799;
			Recipe.BoundsScale = 0.96f;
			Recipe.BaySpanScale = 1.00f;
			Recipe.ShapeGrammarDepth = 3;
			Recipe.MotifGrammarDepth = 3;
			Recipe.TargetShapeVolumeCount = 32;
			Recipe.MaximumBaysPerVolume = 4;
			Recipe.MaximumParallelBlocksPerCourse = 3;
			Recipe.MaximumRoofCourseCount = 64;
			Recipe.bRequirePrimitiveVariety = true;
			Recipe.bRequireMotifVariety = true;
			break;
		case 3:
			Recipe.MinimumBrickCount = 800;
			Recipe.MaximumBrickCount = 1499;
			Recipe.BoundsScale = 1.10f;
			Recipe.BaySpanScale = 0.80f;
			Recipe.ShapeGrammarDepth = 4;
			Recipe.MotifGrammarDepth = 4;
			Recipe.TargetShapeVolumeCount = 48;
			Recipe.MaximumBaysPerVolume = 5;
			Recipe.MaximumParallelBlocksPerCourse = 3;
			Recipe.MaximumRoofCourseCount = 64;
			Recipe.bRequirePrimitiveVariety = true;
			Recipe.bRequireMotifVariety = true;
			break;
		case 4:
			Recipe.MaximumCandidateAttempts = 10;
			Recipe.MinimumBrickCount = 1500;
			Recipe.MaximumBrickCount = 2499;
			Recipe.BoundsScale = 1.22f;
			Recipe.BaySpanScale = 0.72f;
			Recipe.ShapeGrammarDepth = 5;
			Recipe.MotifGrammarDepth = 5;
			Recipe.TargetShapeVolumeCount = 72;
			Recipe.MaximumBaysPerVolume = 6;
			Recipe.MaximumParallelBlocksPerCourse = 3;
			Recipe.MaximumRoofCourseCount = 64;
			Recipe.bRequirePrimitiveVariety = true;
			Recipe.bRequireMotifVariety = true;
			break;
		default:
			Recipe.MaximumCandidateAttempts = 12;
			Recipe.MinimumBrickCount = 2500;
			Recipe.MaximumBrickCount = 4999;
			Recipe.BoundsScale = 1.36f;
			Recipe.BaySpanScale = 0.58f;
			Recipe.ShapeGrammarDepth = 6;
			Recipe.MotifGrammarDepth = 5;
			Recipe.TargetShapeVolumeCount = 72;
			Recipe.MaximumBaysPerVolume = 6;
			Recipe.MaximumParallelBlocksPerCourse = 4;
			Recipe.MaximumRoofCourseCount = 64;
			Recipe.bRequirePrimitiveVariety = true;
			Recipe.bRequireMotifVariety = true;
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
			TEXT("Beam=%.6f:%.6f:%d:%d:%d|")
			TEXT("Visual=%d:%d:%d:%d:%.6f:%.6f:%d:%d:%d:%d:%d:%d:%d:%d:%d|")
			TEXT("C2=%d:%.6f:%.6f:%.6f:%.6f:%.6f:%d:%d|")
			TEXT("C3=%d:%.6f:%.6f:%d:%d:%d:%d:%d:%d"),
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
			Profile.BeamSettings.BeamB.BeamA.MaxParallelBlocksPerCourse,
			Profile.BeamSettings.BeamB.BeamA.MaxFrameParallelBlocksPerCourse,
			Profile.BeamSettings.BeamB.bRequireMotifVariety ? 1 : 0,
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
		ABTSM73BeamD0::BuildDefaultDefinitions(), 9);
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
	if (GameplayProfileId == TEXT("ColumnBreak") && DifficultyTier == 3)
	{
		// Reserve a non-overlapping E4 ceiling for the certified light-frame
		// E5 step below. The fixed E4 matrix remains inside this window.
		OutProfile.VisualComplexity.MaximumBrickCount = 1299;
	}
	if (GameplayProfileId == TEXT("ColumnBreak") && DifficultyTier == 4)
	{
		// The light-frame TwinTower profile reaches the same E5 silhouette
		// milestone with fewer long bearing courses than masonry profiles.
		// Keep its per-profile step above the certified E4 fixture while
		// avoiding forced over-fragmentation solely to hit a global count.
		OutProfile.VisualComplexity.MinimumBrickCount = 1300;
		OutProfile.VisualComplexity.MaximumBrickCount = 1499;
	}
	if (GameplayProfileId == TEXT("ColumnBreak") && DifficultyTier == 5)
	{
		// E6 gains its visible step from the four-lane light frame and denser
		// bay budget. A sixth silhouette recursion or another envelope jump
		// over-fragments TwinTower closure without adding a new readable macro milestone.
		OutProfile.VisualComplexity.ShapeGrammarDepth = 4;
		OutProfile.VisualComplexity.BoundsScale = 1.22f;
		OutProfile.VisualComplexity.MinimumBrickCount = 1500;
		OutProfile.VisualComplexity.MaximumBrickCount = 3499;
	}
	if (!OutProfile.VisualComplexity.Validate(OutError))
	{
		OutProfile.RejectReason = OutError;
		return false;
	}
	OutProfile.StabilityCore = ABTSM73BeamD0::StabilityCoreRecipe(
		DifficultyTier, OutProfile.VisualComplexity.MaximumBrickCount);
	if (!OutProfile.StabilityCore.Validate(OutError))
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
	if (GameplayProfileId == TEXT("ColumnBreak") && DifficultyTier >= 4)
	{
		// TwinTower's E5/E6 milestone is the paired vertical massing itself;
		// random SupportedSpan insertion creates ground-rescue bridges that
		// conflict with the light-frame structural closure.
		Shape.BridgeChance = 0.0f;
	}
	if (DifficultyTier >= 4
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
	Settings.BeamB.BeamA.MaximumVerticalSupportSpanCM =
		OutProfile.StabilityCore.MaximumUnbracedCorePostSpanCM;
	if (GameplayProfileId == TEXT("ColumnBreak") && DifficultyTier >= 4)
	{
		// E5 needs one wider bay to leave enough final-member capacity for the
		// physical C2 support cap. E6 keeps its denser certified four-lane frame.
		Settings.BeamB.BeamA.TargetBaySpanCM = FMath::Max(
			Settings.BeamB.BeamA.TargetBaySpanCM,
			DifficultyTier == 4 ? 473.0f : 420.0f);
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

	OutProfile.ResolvedSettingsHash =
		ABTSM73BeamD0::CalculateResolvedHash(OutProfile, DeterministicSeed);
	OutProfile.bAccepted = true;
	OutProfile.RejectReason.Reset();
	OutError.Reset();
	return true;
}
