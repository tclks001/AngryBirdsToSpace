// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSRuntime.h"
#include "Building/ABTSM7MaterialProfileLibrary.h"
#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73DAG5BSemanticEnvelope.h"
#include "Building/ABTSM73DAG5Types.h"
#include "Building/ABTSM73DAGBuildingPipeline.h"
#include "Building/ABTSM73DAGFailureFrontierTypes.h"
#include "Building/ABTSM73DAGLayoutSolver.h"
#include "Building/ABTSM73DAGModuleCompiler.h"
#include "Building/ABTSM73DAGTypes.h"
#include "Building/ABTSM73StabilityValidator.h"
#include "Building/ABTSM73StructureData.h"
#include "Game/ABTSM7GameMode.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace ABTSM73DAG5BTests
{
	struct FFixture
	{
		FABTSM73DAG5ASettings SearchSettings;
		FABTSM73DAG5BSettings SemanticSettings;
		FABTSM73DAGGenerationSettings DAGSettings;
		FABTSM73DAGLayoutSettings LayoutSettings;
		FABTSM73GenerationSettings BuildingSettings;
		FABTSM73DAGFailureFrontierSettings FrontierSettings;
		FABTSM73DAGFailurePatternSettings PatternSettings;
		FABTSM73DAGFailurePlayabilitySettings PlayabilitySettings;
		FABTSM73DifficultySettings DifficultySettings;
		TArray<FABTSM7MaterialProfile> MaterialProfiles =
			FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();

		FFixture()
		{
			SemanticSettings.bEnableSemanticEnvelope = true;
			DAGSettings.BuildingSeed = 735201;
			DAGSettings.MaxEstimatedBrickCount = 100;
			DAGSettings.ReservedWeaknessBrickCount = 0;
			BuildingSettings.GenerationAlgorithm =
				EABTSM73GenerationAlgorithm::RecursiveSupportDAG;
			BuildingSettings.bGenerateStructuralWeakness = false;
			BuildingSettings.MaxBrickCount = 100;
		}
	};

	bool Build(
		const FFixture& Fixture,
		FABTSM73DAG5BResult& OutResult,
		FABTSM73StructureData& OutData,
		FString& OutError)
	{
		FABTSM73DAGBuildingPipeline Pipeline;
		return Pipeline.BuildWithFailurePattern(
			Fixture.SemanticSettings,
			Fixture.DAGSettings,
			Fixture.LayoutSettings,
			Fixture.BuildingSettings,
			Fixture.FrontierSettings,
			Fixture.PatternSettings,
			Fixture.PlayabilitySettings,
			Fixture.DifficultySettings,
			Fixture.MaterialProfiles,
			FVector::ForwardVector,
			OutResult,
			OutData,
			OutError);
	}

	bool BuildFrontEnd(
		const FFixture& Fixture,
		FABTSM73DAGGenerationResult& OutGraph,
		FABTSM73DAGSpatialLayout& OutInitialLayout,
		FABTSM73SemanticEnvelope& OutEnvelope,
		FABTSM73DAG5BResult& OutResult,
		FString& OutError)
	{
		FABTSM73DAG5BSemanticEnvelopeBuilder Builder;
		return Builder.Build(
			Fixture.SemanticSettings,
			Fixture.DAGSettings,
			Fixture.LayoutSettings,
			OutGraph,
			OutInitialLayout,
			OutEnvelope,
			OutResult,
			OutError);
	}

	bool IsDefaultEnvelope(const FABTSM73SemanticEnvelope& Envelope)
	{
		return !Envelope.bAccepted
			&& Envelope.EnvelopeVersion == 0
			&& Envelope.ShapeFamily
				== EABTSM73DAG5BShapeFamily::Auto
			&& Envelope.GridSize == FIntVector::ZeroValue
			&& !Envelope.LocalBounds.IsValid
			&& Envelope.FeatureMask == EABTSM73DAG5BFeature::None
			&& Envelope.ShapeScopes.IsEmpty()
			&& Envelope.MacroConstraints.IsEmpty()
			&& Envelope.SupportPorts.IsEmpty()
			&& Envelope.WeaknessSockets.IsEmpty()
			&& Envelope.Cells.IsEmpty()
			&& Envelope.ShapeDerivationTrace.IsEmpty()
			&& Envelope.WFCCollapseTrace.IsEmpty()
			&& Envelope.ShapeHash == 0
			&& Envelope.WFCHash == 0
			&& Envelope.EnvelopeHash == 0
			&& Envelope.RejectReason.IsEmpty();
	}

	bool IsDefaultDAG5BResult(const FABTSM73DAG5BResult& Result)
	{
		return !Result.bEnabled
			&& !Result.bAccepted
			&& Result.ShapeFamily
				== EABTSM73DAG5BShapeFamily::Auto
			&& Result.FeatureMask == EABTSM73DAG5BFeature::None
			&& Result.PropagationOperationCount == 0
			&& Result.BacktrackStepCount == 0
			&& Result.CollapsedNonAnchorCellCount == 0
			&& Result.WFCDerivedMustVoidCount == 0
			&& Result.SemanticRegionMappingCount == 0
			&& Result.WFCMappedBrickCount == 0
			&& Result.ShapeHash == 0
			&& Result.WFCHash == 0
			&& Result.EnvelopeHash == 0
			&& Result.ResultHash == 0
			&& !Result.Audit.bAccepted
			&& Result.Audit.MustOccupyCount == 0
			&& Result.Audit.MustVoidCount == 0
			&& Result.Audit.MustVoidViolationCount == 0
			&& Result.Audit.UncoveredMustOccupyCount == 0
			&& Result.Audit.OutOfEnvelopeBrickCount == 0
			&& Result.Audit.OutOfShapeScopeBrickCount == 0
			&& Result.Audit.AuditHash == 0
			&& Result.Audit.RejectReason.IsEmpty()
			&& Result.RejectReason.IsEmpty();
	}

	bool IsDefaultDAG3Results(const FABTSM73StructureData& Data)
	{
		const FABTSM73DAGFailureFrontierAnalysis& Frontier =
			Data.DAGFailureFrontierAnalysis;
		const FABTSM73DAGFailurePatternResult& Pattern =
			Data.DAGFailurePatternResult;
		const FABTSM73DAGFailurePlayabilityResult& Playability =
			Data.DAGFailurePlayabilityResult;
		return !Frontier.bEnabled
			&& !Frontier.bAccepted
			&& Frontier.AcceptedCandidateCount == 0
			&& Frontier.SelectedCandidateIndex == INDEX_NONE
			&& Frontier.SelectedFrontierHash == 0
			&& Frontier.Candidates.IsEmpty()
			&& Frontier.RejectReason.IsEmpty()
			&& !Pattern.bEnabled
			&& !Pattern.bApplied
			&& Pattern.Pattern == EABTSM73DAGFailurePattern::Auto
			&& Pattern.ExpectedMotion == EABTSM73DAGFailureMotion::None
			&& Pattern.SourceFrontierHash == 0
			&& Pattern.RealizedPatternHash == 0
			&& Pattern.SupportMacroNodeId == INDEX_NONE
			&& Pattern.LoadMacroNodeId == INDEX_NONE
			&& Pattern.SupportPlateNodeId == INDEX_NONE
			&& Pattern.LoadPlateNodeId == INDEX_NONE
			&& Pattern.RewriteAttemptCount == 0
			&& Pattern.RemovedColumnCount == 0
			&& Pattern.WeakNodeIds.IsEmpty()
			&& Pattern.RemainingSupportNodeIds.IsEmpty()
			&& Pattern.AffectedMainBodyNodeIds.IsEmpty()
			&& Pattern.ExpectedFailureDirectionLocal.IsNearlyZero()
			&& FMath::IsNearlyZero(Pattern.InitialSupportMarginCM)
			&& FMath::IsNearlyZero(Pattern.PostFailureTipMarginCM)
			&& FMath::IsNearlyEqual(Pattern.ReseatRisk, 1.0f)
			&& FMath::IsNearlyZero(Pattern.OffsetSeamShiftCM)
			&& Pattern.BypassSupportEdgeCount == 0
			&& Pattern.RejectReason.IsEmpty()
			&& !Playability.bEnabled
			&& !Playability.bPlayable
			&& !Playability.bMaterialProfileValidated
			&& Playability.Pattern == EABTSM73DAGFailurePattern::Auto
			&& Playability.ExpectedMotion
				== EABTSM73DAGFailureMotion::None
			&& Playability.Material == EABTSM7BuildingMaterial::Wood
			&& Playability.WeakNodeIds.IsEmpty()
			&& Playability.AffectedNodeIds.IsEmpty()
			&& Playability.AcceptedAttackDirectionLocal.IsNearlyZero()
			&& Playability.AttackImpactPointLocal.IsNearlyZero()
			&& FMath::IsNearlyZero(Playability.AttackExposure)
			&& FMath::IsNearlyZero(Playability.MinAttackClearanceCM)
			&& FMath::IsNearlyZero(Playability.FreeDropDistanceCM)
			&& FMath::IsNearlyZero(Playability.FreeTipAngleDegrees)
			&& FMath::IsNearlyZero(Playability.FreeSlideDistanceCM)
			&& FMath::IsNearlyZero(
				Playability.MaterialKnockSpeedCMPerSec)
			&& FMath::IsNearlyZero(
				Playability.MaterialBreakSpeedCMPerSec)
			&& FMath::IsNearlyZero(
				Playability.MaterialDynamicFriction)
			&& FMath::IsNearlyZero(
				Playability.MaterialStaticFriction)
			&& FMath::IsNearlyZero(
				Playability.MaterialRestitution)
			&& FMath::IsNearlyZero(
				Playability.MaterialDensityGPerCubicCM)
			&& FMath::IsNearlyZero(
				Playability.MaterialDamageAtBreakSpeed)
			&& FMath::IsNearlyZero(Playability.MaterialBreakDamage)
			&& FMath::IsNearlyZero(
				Playability.MaterialPushVelocityTransfer)
			&& FMath::IsNearlyZero(Playability.LocalBreakEffort)
			&& Playability.EstimatedHits == 0
			&& Playability.AttackSampleCount == 0
			&& Playability.MotionSweepSampleCount == 0
			&& Playability.BlockingNodeId == INDEX_NONE
			&& Playability.PlayabilityHash == 0
			&& Playability.RejectReason.IsEmpty();
	}

	bool IsEmptyStructureData(const FABTSM73StructureData& Data)
	{
		return Data.Bricks.IsEmpty()
			&& Data.SupportEdges.IsEmpty()
			&& Data.DAGPhysicalSupportMappings.IsEmpty()
			&& Data.DAG5BSemanticBrickMappings.IsEmpty()
			&& Data.GroundNodeIds.IsEmpty()
			&& Data.StructuralWeaknessIntents.IsEmpty()
			&& Data.FailureProbeResults.IsEmpty()
			&& Data.WeakPoints.IsEmpty()
			&& Data.ReinforcedNodeIds.IsEmpty()
			&& Data.GroundSupportPoints.IsEmpty()
			&& Data.GroundSamples.IsEmpty()
			&& Data.FoundationFeet.IsEmpty()
			&& !Data.LocalBounds.IsValid
			&& Data.FootprintHalfExtent.IsNearlyZero()
			&& FMath::IsNearlyZero(Data.FoundationCapBottomCM)
			&& FMath::IsNearlyZero(Data.FoundationCapTopCM)
			&& FMath::IsNearlyZero(Data.CurvatureDropCM)
			&& FMath::IsNearlyZero(Data.MaxSlopeDegrees)
			&& FMath::IsNearlyZero(Data.TerrainDeltaCM)
			&& FMath::IsNearlyZero(Data.MaxFoundationDepthCM)
			&& FMath::IsNearlyZero(Data.BestWeakPointScore)
			&& FMath::IsNearlyZero(Data.PredictedWeakCollapseRatio)
			&& FMath::IsNearlyZero(Data.PredictedNonWeakEffect)
			&& FMath::IsNearlyZero(Data.DifficultyScore)
			&& Data.EstimatedWeakPointHits == 0
			&& Data.DAGMacroNodeCount == 0
			&& Data.DAGSelectedSupportCount == 0
			&& Data.DAGMissingRequiredContactCount == 0
			&& Data.DAGUnexpectedBypassCount == 0
			&& FMath::IsNearlyZero(
				Data.DAGMinSupportContactAreaRatio)
			&& Data.DAGTopologyHash == 0
			&& IsDefaultEnvelope(Data.DAG5BSemanticEnvelope)
			&& IsDefaultDAG5BResult(Data.DAG5BResult)
			&& IsDefaultDAG3Results(Data);
	}

	FBox BrickBounds(const FABTSM73BrickNode& Brick)
	{
		return FBox(
			Brick.LocalCenter - Brick.DimensionsCM * 0.5f,
			Brick.LocalCenter + Brick.DimensionsCM * 0.5f);
	}

	const FABTSM73BrickNode* FindMacroBrick(
		const FABTSM73StructureData& Data,
		const int32 MacroNodeId)
	{
		return Data.Bricks.FindByPredicate(
			[MacroNodeId](const FABTSM73BrickNode& Brick)
			{
				return Brick.MacroNodeId == MacroNodeId;
			});
	}

	bool OverlapsWithVolume(const FBox& A, const FBox& B)
	{
		const FVector Min(
			FMath::Max(A.Min.X, B.Min.X),
			FMath::Max(A.Min.Y, B.Min.Y),
			FMath::Max(A.Min.Z, B.Min.Z));
		const FVector Max(
			FMath::Min(A.Max.X, B.Max.X),
			FMath::Min(A.Max.Y, B.Max.Y),
			FMath::Min(A.Max.Z, B.Max.Z));
		return Max.X > Min.X + KINDA_SMALL_NUMBER
			&& Max.Y > Min.Y + KINDA_SMALL_NUMBER
			&& Max.Z > Min.Z + KINDA_SMALL_NUMBER;
	}

	bool EqualGeometry(
		const FABTSM73StructureData& A,
		const FABTSM73StructureData& B)
	{
		if (A.Bricks.Num() != B.Bricks.Num()
			|| A.SupportEdges.Num() != B.SupportEdges.Num()
			|| A.DAGPhysicalSupportMappings.Num()
				!= B.DAGPhysicalSupportMappings.Num()
			|| A.GroundNodeIds != B.GroundNodeIds
			|| A.StructuralWeaknessIntents.Num()
				!= B.StructuralWeaknessIntents.Num()
			|| A.FailureProbeResults.Num()
				!= B.FailureProbeResults.Num()
			|| A.WeakPoints.Num() != B.WeakPoints.Num()
			|| A.ReinforcedNodeIds != B.ReinforcedNodeIds
			|| A.GroundSupportPoints != B.GroundSupportPoints
			|| A.GroundSamples.Num() != B.GroundSamples.Num()
			|| A.FoundationFeet.Num() != B.FoundationFeet.Num()
			|| A.DAGTopologyHash != B.DAGTopologyHash
			|| A.DAGMacroNodeCount != B.DAGMacroNodeCount
			|| A.DAGSelectedSupportCount != B.DAGSelectedSupportCount
			|| A.DAGMissingRequiredContactCount
				!= B.DAGMissingRequiredContactCount
			|| A.DAGUnexpectedBypassCount
				!= B.DAGUnexpectedBypassCount
			|| !FMath::IsNearlyEqual(
				A.DAGMinSupportContactAreaRatio,
				B.DAGMinSupportContactAreaRatio)
			|| A.LocalBounds.IsValid != B.LocalBounds.IsValid
			|| (A.LocalBounds.IsValid
				&& (!A.LocalBounds.Min.Equals(B.LocalBounds.Min)
					|| !A.LocalBounds.Max.Equals(B.LocalBounds.Max)))
			|| !A.FootprintHalfExtent.Equals(B.FootprintHalfExtent)
			|| !FMath::IsNearlyEqual(
				A.FoundationCapBottomCM,
				B.FoundationCapBottomCM)
			|| !FMath::IsNearlyEqual(
				A.FoundationCapTopCM,
				B.FoundationCapTopCM)
			|| !FMath::IsNearlyEqual(
				A.CurvatureDropCM,
				B.CurvatureDropCM)
			|| !FMath::IsNearlyEqual(
				A.MaxSlopeDegrees,
				B.MaxSlopeDegrees)
			|| !FMath::IsNearlyEqual(
				A.TerrainDeltaCM,
				B.TerrainDeltaCM)
			|| !FMath::IsNearlyEqual(
				A.MaxFoundationDepthCM,
				B.MaxFoundationDepthCM))
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Bricks.Num(); ++Index)
		{
			const FABTSM73BrickNode& Left = A.Bricks[Index];
			const FABTSM73BrickNode& Right = B.Bricks[Index];
			if (Left.NodeId != Right.NodeId
				|| Left.MacroNodeId != Right.MacroNodeId
				|| Left.Material != Right.Material
				|| Left.OriginalMaterial != Right.OriginalMaterial
				|| Left.SemanticRole != Right.SemanticRole
				|| Left.StoreyIndex != Right.StoreyIndex
				|| Left.BayIndex != Right.BayIndex
				|| Left.WeakPointRole != Right.WeakPointRole
				|| !FMath::IsNearlyEqual(
					Left.WeakPointScore,
					Right.WeakPointScore)
				|| !FMath::IsNearlyEqual(
					Left.UnsupportedMassRatio,
					Right.UnsupportedMassRatio)
				|| !FMath::IsNearlyEqual(
					Left.AttackExposure,
					Right.AttackExposure)
				|| Left.EstimatedHits != Right.EstimatedHits
				|| Left.bFailureFrontierMainBody
					!= Right.bFailureFrontierMainBody
				|| Left.bWeakPoint != Right.bWeakPoint
				|| Left.bReinforcedCriticalNode
					!= Right.bReinforcedCriticalNode
				|| !Left.LocalCenter.Equals(
					Right.LocalCenter,
					KINDA_SMALL_NUMBER)
				|| !Left.DimensionsCM.Equals(
					Right.DimensionsCM,
					KINDA_SMALL_NUMBER))
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < A.SupportEdges.Num(); ++Index)
		{
			const FABTSM73SupportEdge& Left = A.SupportEdges[Index];
			const FABTSM73SupportEdge& Right = B.SupportEdges[Index];
			if (Left.LowerNodeId != Right.LowerNodeId
				|| Left.UpperNodeId != Right.UpperNodeId
				|| !FMath::IsNearlyEqual(
					Left.ContactAreaCM2,
					Right.ContactAreaCM2))
			{
				return false;
			}
		}
		for (int32 Index = 0;
			Index < A.DAGPhysicalSupportMappings.Num();
			++Index)
		{
			const FABTSM73DAGPhysicalSupportMapping& Left =
				A.DAGPhysicalSupportMappings[Index];
			const FABTSM73DAGPhysicalSupportMapping& Right =
				B.DAGPhysicalSupportMappings[Index];
			if (Left.SupportMacroNodeId
					!= Right.SupportMacroNodeId
				|| Left.LoadMacroNodeId != Right.LoadMacroNodeId
				|| Left.SupportPlateNodeId
					!= Right.SupportPlateNodeId
				|| Left.LoadPlateNodeId != Right.LoadPlateNodeId
				|| Left.SupportPattern != Right.SupportPattern
				|| !FMath::IsNearlyEqual(
					Left.RealizedColumnWidthCM,
					Right.RealizedColumnWidthCM)
				|| Left.ColumnNodeIds != Right.ColumnNodeIds
				|| Left.ColumnRoles != Right.ColumnRoles)
			{
				return false;
			}
		}
		for (int32 Index = 0;
			Index < A.StructuralWeaknessIntents.Num();
			++Index)
		{
			const FABTSM73StructuralWeaknessIntent& Left =
				A.StructuralWeaknessIntents[Index];
			const FABTSM73StructuralWeaknessIntent& Right =
				B.StructuralWeaknessIntents[Index];
			if (Left.Pattern != Right.Pattern
				|| Left.ExpectedCollapseMode
					!= Right.ExpectedCollapseMode
				|| Left.CandidateNodeId != Right.CandidateNodeId
				|| Left.CarrierNodeId != Right.CarrierNodeId
				|| Left.BayIndex != Right.BayIndex
				|| !Left.ExpectedTipDirectionLocal.Equals(
					Right.ExpectedTipDirectionLocal)
				|| Left.DirectSupportNodeIds
					!= Right.DirectSupportNodeIds
				|| Left.PayloadNodeIds != Right.PayloadNodeIds)
			{
				return false;
			}
		}
		for (int32 Index = 0;
			Index < A.FailureProbeResults.Num();
			++Index)
		{
			const FABTSM73FailureProbeResult& Left =
				A.FailureProbeResults[Index];
			const FABTSM73FailureProbeResult& Right =
				B.FailureProbeResults[Index];
			if (Left.bValid != Right.bValid
				|| Left.bWouldReseat != Right.bWouldReseat
				|| Left.CandidateNodeId != Right.CandidateNodeId
				|| Left.CarrierNodeId != Right.CarrierNodeId
				|| Left.Pattern != Right.Pattern
				|| Left.CollapseMode != Right.CollapseMode
				|| !Left.AffectedCenterOfMassLocal.Equals(
					Right.AffectedCenterOfMassLocal)
				|| !Left.TipDirectionLocal.Equals(
					Right.TipDirectionLocal)
				|| !FMath::IsNearlyEqual(
					Left.AffectedMassRatio,
					Right.AffectedMassRatio)
				|| !FMath::IsNearlyEqual(
					Left.InitialSupportMarginCM,
					Right.InitialSupportMarginCM)
				|| !FMath::IsNearlyEqual(
					Left.TipMarginCM,
					Right.TipMarginCM)
				|| !FMath::IsNearlyEqual(
					Left.ReseatRisk,
					Right.ReseatRisk)
				|| Left.AffectedNodeIds != Right.AffectedNodeIds
				|| Left.RejectReason != Right.RejectReason)
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < A.WeakPoints.Num(); ++Index)
		{
			const FABTSM73WeakPointRecord& Left = A.WeakPoints[Index];
			const FABTSM73WeakPointRecord& Right = B.WeakPoints[Index];
			if (Left.NodeId != Right.NodeId
				|| Left.Role != Right.Role
				|| !FMath::IsNearlyEqual(
					Left.UnsupportedMassRatio,
					Right.UnsupportedMassRatio)
				|| !FMath::IsNearlyEqual(
					Left.Exposure,
					Right.Exposure)
				|| !FMath::IsNearlyEqual(
					Left.Readability,
					Right.Readability)
				|| !FMath::IsNearlyEqual(
					Left.LocalBreakEffort,
					Right.LocalBreakEffort)
				|| !FMath::IsNearlyEqual(Left.Score, Right.Score)
				|| Left.EstimatedHits != Right.EstimatedHits
				|| Left.UnsupportedNodeIds
					!= Right.UnsupportedNodeIds
				|| Left.AffectedNodeIds != Right.AffectedNodeIds
				|| Left.StructuralPattern != Right.StructuralPattern
				|| Left.CollapseMode != Right.CollapseMode
				|| !FMath::IsNearlyEqual(
					Left.InitialSupportMarginCM,
					Right.InitialSupportMarginCM)
				|| !FMath::IsNearlyEqual(
					Left.TipMarginCM,
					Right.TipMarginCM)
				|| !FMath::IsNearlyEqual(
					Left.ReseatRisk,
					Right.ReseatRisk)
				|| Left.DAGFailurePattern != Right.DAGFailurePattern
				|| Left.DAGFailureMotion != Right.DAGFailureMotion
				|| !Left.AcceptedAttackDirectionLocal.Equals(
					Right.AcceptedAttackDirectionLocal))
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < A.GroundSamples.Num(); ++Index)
		{
			const FABTSM73GroundSample& Left = A.GroundSamples[Index];
			const FABTSM73GroundSample& Right = B.GroundSamples[Index];
			if (!Left.LocalXY.Equals(Right.LocalXY)
				|| !Left.WorldPosition.Equals(Right.WorldPosition)
				|| !Left.WorldNormal.Equals(Right.WorldNormal)
				|| !FMath::IsNearlyEqual(
					Left.LocalHeightCM,
					Right.LocalHeightCM)
				|| Left.CellId != Right.CellId
				|| Left.bBuildable != Right.bBuildable)
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < A.FoundationFeet.Num(); ++Index)
		{
			const FABTSM73FoundationFoot& Left = A.FoundationFeet[Index];
			const FABTSM73FoundationFoot& Right = B.FoundationFeet[Index];
			if (!Left.LocalXY.Equals(Right.LocalXY)
				|| !FMath::IsNearlyEqual(
					Left.GroundHeightCM,
					Right.GroundHeightCM)
				|| !FMath::IsNearlyEqual(
					Left.BottomHeightCM,
					Right.BottomHeightCM)
				|| !FMath::IsNearlyEqual(
					Left.TopHeightCM,
					Right.TopHeightCM))
			{
				return false;
			}
		}
		if (!FMath::IsNearlyEqual(
				A.BestWeakPointScore,
				B.BestWeakPointScore)
			|| !FMath::IsNearlyEqual(
				A.PredictedWeakCollapseRatio,
				B.PredictedWeakCollapseRatio)
			|| !FMath::IsNearlyEqual(
				A.PredictedNonWeakEffect,
				B.PredictedNonWeakEffect)
			|| !FMath::IsNearlyEqual(
				A.DifficultyScore,
				B.DifficultyScore)
			|| A.EstimatedWeakPointHits != B.EstimatedWeakPointHits)
		{
			return false;
		}
		return true;
	}

	uint32 GeometrySignature(const FABTSM73StructureData& Data)
	{
		TArray<FString> VisibleBricks;
		VisibleBricks.Reserve(Data.Bricks.Num());
		for (const FABTSM73BrickNode& Brick : Data.Bricks)
		{
			VisibleBricks.Add(FString::Printf(
				TEXT("%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d"),
				Brick.LocalCenter.X,
				Brick.LocalCenter.Y,
				Brick.LocalCenter.Z,
				Brick.DimensionsCM.X,
				Brick.DimensionsCM.Y,
				Brick.DimensionsCM.Z,
				static_cast<int32>(Brick.Material),
				static_cast<int32>(Brick.SemanticRole)));
		}
		VisibleBricks.Sort();
		FString Canonical;
		for (const FString& Brick : VisibleBricks)
		{
			Canonical += TEXT("|") + Brick;
		}
		return FCrc::StrCrc32(*Canonical);
	}

	uint32 PortSignature(const FABTSM73SemanticEnvelope& Envelope)
	{
		FString Canonical;
		for (const FABTSM73DAG5BSupportPortConstraint& Port :
			Envelope.SupportPorts)
		{
			Canonical += FString::Printf(
				TEXT("|%d>%d:%.2f,%.2f,%.2f,%.2f:%d,%d,%d-%d,%d,%d:%d:%u"),
				Port.SupportMacroNodeId,
				Port.LoadMacroNodeId,
				Port.AllowedColumnRegion.Min.X,
				Port.AllowedColumnRegion.Min.Y,
				Port.AllowedColumnRegion.Max.X,
				Port.AllowedColumnRegion.Max.Y,
				Port.SourceCellMin.X,
				Port.SourceCellMin.Y,
				Port.SourceCellMin.Z,
				Port.SourceCellMax.X,
				Port.SourceCellMax.Y,
				Port.SourceCellMax.Z,
				static_cast<int32>(Port.SourceSemantic),
				Port.SourceCellHash);
		}
		return FCrc::StrCrc32(*Canonical);
	}

	bool EqualSemanticMappings(
		const FABTSM73StructureData& A,
		const FABTSM73StructureData& B)
	{
		if (A.DAG5BSemanticBrickMappings.Num()
			!= B.DAG5BSemanticBrickMappings.Num())
		{
			return false;
		}
		for (int32 Index = 0;
			Index < A.DAG5BSemanticBrickMappings.Num();
			++Index)
		{
			const FABTSM73DAG5BSemanticBrickMapping& Left =
				A.DAG5BSemanticBrickMappings[Index];
			const FABTSM73DAG5BSemanticBrickMapping& Right =
				B.DAG5BSemanticBrickMappings[Index];
			if (Left.Kind != Right.Kind
				|| Left.SupportMacroNodeId
					!= Right.SupportMacroNodeId
				|| Left.LoadMacroNodeId != Right.LoadMacroNodeId
				|| Left.TargetMacroNodeId != Right.TargetMacroNodeId
				|| Left.SourceSemantic != Right.SourceSemantic
				|| Left.SourceCellMin != Right.SourceCellMin
				|| Left.SourceCellMax != Right.SourceCellMax
				|| Left.SourceCellHash != Right.SourceCellHash
				|| Left.BrickNodeIds != Right.BrickNodeIds
				|| Left.MappingHash != Right.MappingHash)
			{
				return false;
			}
		}
		return true;
	}

	EABTSM73DAG5BFeature RequiredFeatures(
		const EABTSM73DAG5BShapeFamily Family)
	{
		switch (Family)
		{
		case EABTSM73DAG5BShapeFamily::SetbackTower:
			return EABTSM73DAG5BFeature::Setback
				| EABTSM73DAG5BFeature::FootprintCentroidShift;
		case EABTSM73DAG5BShapeFamily::OffsetBridge:
			return EABTSM73DAG5BFeature::BridgeSpan
				| EABTSM73DAG5BFeature::FootprintCentroidShift;
		case EABTSM73DAG5BShapeFamily::ThroughOpeningWall:
			return EABTSM73DAG5BFeature::ThroughOpening
				| EABTSM73DAG5BFeature::NonUniformRoofline;
		case EABTSM73DAG5BShapeFamily::OneSidedHighTower:
			return EABTSM73DAG5BFeature::HeightAsymmetry
				| EABTSM73DAG5BFeature::Cantilever;
		default:
			return EABTSM73DAG5BFeature::None;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5BFourFamilyTest,
	"ABTS.M73DAG.DAG5B.FourFamilies",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5BFourFamilyTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73DAG5BTests;
	TSet<uint32> GeometrySignatures;
	TSet<uint32> EnvelopeHashes;
	for (const EABTSM73DAG5BShapeFamily Family : {
		EABTSM73DAG5BShapeFamily::SetbackTower,
		EABTSM73DAG5BShapeFamily::OffsetBridge,
		EABTSM73DAG5BShapeFamily::ThroughOpeningWall,
		EABTSM73DAG5BShapeFamily::OneSidedHighTower})
	{
		FFixture Fixture;
		Fixture.SemanticSettings.ShapeFamily = Family;
		Fixture.DAGSettings.BuildingSeed +=
			static_cast<int32>(Family) * 101;
		FABTSM73DAG5BResult Result;
		FABTSM73StructureData Data;
		FString Error;
		const bool bBuilt = Build(Fixture, Result, Data, Error);
		TestTrue(
			FString::Printf(
				TEXT("Family %d builds through Brick audit: %s"),
				static_cast<int32>(Family),
				*Error),
			bBuilt);
		if (!bBuilt) continue;
		TestTrue(TEXT("Semantic result accepted"), Result.bAccepted);
		TestEqual(TEXT("Selected family retained"), Result.ShapeFamily, Family);
		const uint32 Required =
			static_cast<uint32>(RequiredFeatures(Family));
		const uint32 Actual =
			static_cast<uint32>(Result.FeatureMask);
		TestEqual(
			TEXT("Required visual features are present"),
			Actual & Required,
			Required);
		TestTrue(
			TEXT("Each family has at least two visual features"),
			FMath::CountBits(Actual) >= 2);
		TestTrue(
			TEXT("Shape Grammar emitted a derivation trace"),
			!Data.DAG5BSemanticEnvelope.ShapeDerivationTrace.IsEmpty());
		TestTrue(
			TEXT("WFC made non-anchor collapse decisions"),
			Result.CollapsedNonAnchorCellCount > 0);
		TestTrue(
			TEXT("WFC emitted a collapse trace"),
			!Data.DAG5BSemanticEnvelope.WFCCollapseTrace.IsEmpty());
		TestTrue(TEXT("MustOccupy audit exists"),
			Result.Audit.MustOccupyCount > 0);
		TestTrue(TEXT("MustVoid audit exists"),
			Result.Audit.MustVoidCount > 0);
		TestEqual(TEXT("MustVoid remains empty"),
			Result.Audit.MustVoidViolationCount, 0);
		TestEqual(TEXT("MustOccupy is covered"),
			Result.Audit.UncoveredMustOccupyCount, 0);
		TestEqual(TEXT("All bricks remain in Shape Grammar scopes"),
			Result.Audit.OutOfShapeScopeBrickCount, 0);
		TestTrue(TEXT("WFC emits physical open-air contracts"),
			Result.WFCDerivedMustVoidCount > 0);
		TestTrue(TEXT("Semantic regions map to physical bricks"),
			Result.SemanticRegionMappingCount
				> Data.DAGPhysicalSupportMappings.Num());
		TestTrue(TEXT("At least one non-column Shape/WFC region is lowered"),
			Data.DAG5BSemanticBrickMappings.ContainsByPredicate(
				[](const FABTSM73DAG5BSemanticBrickMapping& Mapping)
				{
					return Mapping.Kind
						== EABTSM73DAG5BSemanticMappingKind::ShapeMacro;
				}));
		TestEqual(TEXT("All DAG contacts exist"),
			Data.DAGMissingRequiredContactCount, 0);
		TestEqual(TEXT("No physical bypass exists"),
			Data.DAGUnexpectedBypassCount, 0);
		FABTSM73StabilityValidator StabilityValidator;
		TestTrue(
			FString::Printf(
				TEXT("Family %d passes static stability: %s"),
				static_cast<int32>(Family),
				*Error),
			StabilityValidator.Validate(
				Fixture.BuildingSettings,
				Data,
				Error));
		GeometrySignatures.Add(GeometrySignature(Data));
		EnvelopeHashes.Add(Result.EnvelopeHash);
	}
	TestEqual(TEXT("Four families have four visible geometries"),
		GeometrySignatures.Num(), 4);
	TestEqual(TEXT("Four families have four semantic envelopes"),
		EnvelopeHashes.Num(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5BPhysicalSilhouetteTest,
	"ABTS.M73DAG.DAG5B.FourFamilyPhysicalSilhouettes",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5BPhysicalSilhouetteTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73DAG5BTests;
	auto BuildFamily = [this](
		const EABTSM73DAG5BShapeFamily Family,
		FABTSM73StructureData& OutData)
	{
		FFixture Fixture;
		Fixture.SemanticSettings.ShapeFamily = Family;
		Fixture.DAGSettings.BuildingSeed +=
			static_cast<int32>(Family) * 101;
		FABTSM73DAG5BResult Result;
		FString Error;
		const bool bBuilt = Build(Fixture, Result, OutData, Error);
		TestTrue(
			FString::Printf(
				TEXT("Physical silhouette family %d builds: %s"),
				static_cast<int32>(Family),
				*Error),
			bBuilt);
		return bBuilt;
	};
	auto RequireMacro = [this](
		const FABTSM73StructureData& Data,
		const int32 MacroNodeId,
		const TCHAR* Label)
	{
		const FABTSM73BrickNode* Brick =
			FindMacroBrick(Data, MacroNodeId);
		TestNotNull(Label, Brick);
		return Brick;
	};

	FABTSM73StructureData Setback;
	if (BuildFamily(
		EABTSM73DAG5BShapeFamily::SetbackTower,
		Setback))
	{
		const FABTSM73BrickNode* Base =
			RequireMacro(Setback, 0, TEXT("Setback base exists"));
		const FABTSM73BrickNode* MiddleA =
			RequireMacro(Setback, 1, TEXT("Setback middle A exists"));
		const FABTSM73BrickNode* MiddleB =
			RequireMacro(Setback, 2, TEXT("Setback middle B exists"));
		const FABTSM73BrickNode* Crown =
			RequireMacro(Setback, 3, TEXT("Setback crown exists"));
		if (Base && MiddleA && MiddleB && Crown)
		{
			const auto Area = [](const FABTSM73BrickNode* Brick)
			{
				return Brick->DimensionsCM.X * Brick->DimensionsCM.Y;
			};
			TestTrue(TEXT("Final plates form three visible setbacks"),
				Area(Base) > Area(MiddleA)
					&& Area(MiddleA) > Area(MiddleB)
					&& Area(MiddleB) > Area(Crown));
			TestTrue(TEXT("Final crown shifts relative to final base"),
				FMath::Abs(
					Crown->LocalCenter.X - Base->LocalCenter.X)
					> Base->DimensionsCM.X * 0.04f);
		}
	}

	FABTSM73StructureData OffsetBridge;
	if (BuildFamily(
		EABTSM73DAG5BShapeFamily::OffsetBridge,
		OffsetBridge))
	{
		const FABTSM73BrickNode* Left =
			RequireMacro(OffsetBridge, 2, TEXT("Offset left tower exists"));
		const FABTSM73BrickNode* Right =
			RequireMacro(OffsetBridge, 3, TEXT("Offset right tower exists"));
		const FABTSM73BrickNode* Bridge =
			RequireMacro(OffsetBridge, 4, TEXT("Offset bridge exists"));
		const FABTSM73BrickNode* Crown =
			RequireMacro(OffsetBridge, 5, TEXT("Offset crown exists"));
		if (Left && Right && Bridge && Crown)
		{
			const FBox LeftBox = BrickBounds(*Left);
			const FBox RightBox = BrickBounds(*Right);
			const FBox BridgeBox = BrickBounds(*Bridge);
			TestTrue(TEXT("Final tower plates are horizontally separated"),
				LeftBox.Max.X < RightBox.Min.X);
			TestTrue(TEXT("Final bridge plate spans both tower centers"),
				BridgeBox.Min.X <= Left->LocalCenter.X
					&& BridgeBox.Max.X >= Right->LocalCenter.X);
			TestTrue(TEXT("Final bridge is physically above both towers"),
				BridgeBox.Min.Z >= LeftBox.Max.Z
					&& BridgeBox.Min.Z >= RightBox.Max.Z);
			TestTrue(TEXT("Final crown is offset from the bridge"),
				FMath::Abs(
					Crown->LocalCenter.X - Bridge->LocalCenter.X)
					> Bridge->DimensionsCM.X * 0.02f);
		}
	}

	FABTSM73StructureData ThroughOpening;
	if (BuildFamily(
		EABTSM73DAG5BShapeFamily::ThroughOpeningWall,
		ThroughOpening))
	{
		const FABTSM73BrickNode* Left =
			RequireMacro(ThroughOpening, 2, TEXT("Opening left pier exists"));
		const FABTSM73BrickNode* Right =
			RequireMacro(ThroughOpening, 3, TEXT("Opening right pier exists"));
		const FABTSM73BrickNode* Lintel =
			RequireMacro(ThroughOpening, 4, TEXT("Opening lintel exists"));
		const FABTSM73BrickNode* LowCrown =
			RequireMacro(ThroughOpening, 6, TEXT("Low crown exists"));
		const FABTSM73BrickNode* HighCrown =
			RequireMacro(ThroughOpening, 7, TEXT("High crown exists"));
		if (Left && Right && Lintel && LowCrown && HighCrown)
		{
			const FBox LeftBox = BrickBounds(*Left);
			const FBox RightBox = BrickBounds(*Right);
			const FBox LintelBox = BrickBounds(*Lintel);
			TestTrue(TEXT("Final wall has two separated physical piers"),
				LeftBox.Max.X < RightBox.Min.X);
			TestTrue(TEXT("Final lintel spans the physical opening"),
				LintelBox.Min.X <= Left->LocalCenter.X
					&& LintelBox.Max.X >= Right->LocalCenter.X);
			TestTrue(TEXT("Final opening remains below its lintel"),
				LintelBox.Min.Z >= FMath::Max(
					LeftBox.Max.Z,
					RightBox.Max.Z));
			TestTrue(TEXT("Final roof line is physically asymmetric"),
				BrickBounds(*HighCrown).Max.Z
					> BrickBounds(*LowCrown).Max.Z);
		}
		const FABTSM73DAG5BSemanticCellRecord* DoorContract =
			ThroughOpening.DAG5BSemanticEnvelope.Cells.FindByPredicate(
				[](const FABTSM73DAG5BSemanticCellRecord& Cell)
				{
					return Cell.Semantic
							== EABTSM73DAG5BSemanticCell::DoorVoid
						&& Cell.Occupancy
							== EABTSM73DAG5BOccupancy::MustVoid;
				});
		TestNotNull(TEXT("Through-opening has a physical DoorVoid"), DoorContract);
		if (DoorContract)
		{
			TestFalse(
				TEXT("No final Brick occupies the physical DoorVoid"),
				ThroughOpening.Bricks.ContainsByPredicate(
					[DoorContract](const FABTSM73BrickNode& Brick)
					{
						return OverlapsWithVolume(
							DoorContract->LocalBounds,
							BrickBounds(Brick));
					}));
		}
	}

	FABTSM73StructureData OneSided;
	if (BuildFamily(
		EABTSM73DAG5BShapeFamily::OneSidedHighTower,
		OneSided))
	{
		const FABTSM73BrickNode* Low =
			RequireMacro(OneSided, 1, TEXT("Low tower exists"));
		const FABTSM73BrickNode* HighSupport =
			RequireMacro(OneSided, 3, TEXT("High tower exists"));
		const FABTSM73BrickNode* Crown =
			RequireMacro(OneSided, 4, TEXT("Cantilever crown exists"));
		if (Low && HighSupport && Crown)
		{
			TestTrue(TEXT("Final high side is at least two levels taller"),
				BrickBounds(*Crown).Max.Z
					> BrickBounds(*Low).Max.Z
						+ OneSided.DAG5BSemanticEnvelope.LocalBounds
							.GetSize().Z * 0.25f);
			const FBox HighBox = BrickBounds(*HighSupport);
			const FBox CrownBox = BrickBounds(*Crown);
			TestTrue(TEXT("Final crown visibly overhangs its high support"),
				CrownBox.Min.X < HighBox.Min.X
					|| CrownBox.Max.X > HighBox.Max.X);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5BDeterminismTest,
	"ABTS.M73DAG.DAG5B.FullChainDeterminism",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5BDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73DAG5BTests;
	for (const EABTSM73DAG5BShapeFamily Family : {
		EABTSM73DAG5BShapeFamily::SetbackTower,
		EABTSM73DAG5BShapeFamily::OffsetBridge,
		EABTSM73DAG5BShapeFamily::ThroughOpeningWall,
		EABTSM73DAG5BShapeFamily::OneSidedHighTower})
	{
		FFixture Fixture;
		Fixture.SemanticSettings.ShapeFamily = Family;
		FABTSM73DAG5BResult FirstResult;
		FABTSM73DAG5BResult RepeatResult;
		FABTSM73StructureData FirstData;
		FABTSM73StructureData RepeatData;
		FString FirstError;
		FString RepeatError;
		const bool bFirst = Build(
			Fixture,
			FirstResult,
			FirstData,
			FirstError);
		const bool bRepeat = Build(
			Fixture,
			RepeatResult,
			RepeatData,
			RepeatError);
		TestTrue(TEXT("First deterministic build succeeds"), bFirst);
		TestTrue(TEXT("Repeat deterministic build succeeds"), bRepeat);
		if (!bFirst || !bRepeat) continue;
		TestEqual(TEXT("Shape hash repeats"),
			RepeatResult.ShapeHash, FirstResult.ShapeHash);
		TestEqual(TEXT("WFC hash repeats"),
			RepeatResult.WFCHash, FirstResult.WFCHash);
		TestEqual(TEXT("Envelope hash repeats"),
			RepeatResult.EnvelopeHash, FirstResult.EnvelopeHash);
		TestEqual(TEXT("Audit hash repeats"),
			RepeatResult.Audit.AuditHash,
			FirstResult.Audit.AuditHash);
		TestEqual(TEXT("Propagation operation count repeats"),
			RepeatResult.PropagationOperationCount,
			FirstResult.PropagationOperationCount);
		TestEqual(TEXT("Backtrack step count repeats"),
			RepeatResult.BacktrackStepCount,
			FirstResult.BacktrackStepCount);
		TestEqual(TEXT("Non-anchor collapse count repeats"),
			RepeatResult.CollapsedNonAnchorCellCount,
			FirstResult.CollapsedNonAnchorCellCount);
		TestEqual(TEXT("WFC-derived MustVoid count repeats"),
			RepeatResult.WFCDerivedMustVoidCount,
			FirstResult.WFCDerivedMustVoidCount);
		TestEqual(TEXT("Semantic mapping count repeats"),
			RepeatResult.SemanticRegionMappingCount,
			FirstResult.SemanticRegionMappingCount);
		TestEqual(TEXT("WFC-mapped Brick count repeats"),
			RepeatResult.WFCMappedBrickCount,
			FirstResult.WFCMappedBrickCount);
		TestEqual(TEXT("Result hash repeats"),
			RepeatResult.ResultHash, FirstResult.ResultHash);
		TestTrue(TEXT("Brick geometry repeats"),
			EqualGeometry(FirstData, RepeatData));
		TestTrue(TEXT("Shape derivation trace repeats"),
			RepeatData.DAG5BSemanticEnvelope.ShapeDerivationTrace
				== FirstData.DAG5BSemanticEnvelope.ShapeDerivationTrace);
		TestTrue(TEXT("WFC collapse trace repeats"),
			RepeatData.DAG5BSemanticEnvelope.WFCCollapseTrace
				== FirstData.DAG5BSemanticEnvelope.WFCCollapseTrace);
		TestEqual(TEXT("Support port signature repeats"),
			PortSignature(RepeatData.DAG5BSemanticEnvelope),
			PortSignature(FirstData.DAG5BSemanticEnvelope));
		TestTrue(TEXT("Semantic brick mappings repeat"),
			EqualSemanticMappings(FirstData, RepeatData));
		TestEqual(TEXT("Semantic cell count repeats"),
			RepeatData.DAG5BSemanticEnvelope.Cells.Num(),
			FirstData.DAG5BSemanticEnvelope.Cells.Num());
		for (int32 CellIndex = 0;
			CellIndex
				< FirstData.DAG5BSemanticEnvelope.Cells.Num()
				&& RepeatData.DAG5BSemanticEnvelope.Cells.IsValidIndex(
					CellIndex);
			++CellIndex)
		{
			const FABTSM73DAG5BSemanticCellRecord& Left =
				FirstData.DAG5BSemanticEnvelope.Cells[CellIndex];
			const FABTSM73DAG5BSemanticCellRecord& Right =
				RepeatData.DAG5BSemanticEnvelope.Cells[CellIndex];
			TestTrue(
				TEXT("Semantic cell artifact repeats"),
				Left.Coordinate == Right.Coordinate
					&& Left.LocalBounds.Min.Equals(
						Right.LocalBounds.Min)
					&& Left.LocalBounds.Max.Equals(
						Right.LocalBounds.Max)
					&& Left.RequiredSolidBounds.Min.Equals(
						Right.RequiredSolidBounds.Min)
					&& Left.RequiredSolidBounds.Max.Equals(
						Right.RequiredSolidBounds.Max)
					&& Left.Semantic == Right.Semantic
					&& Left.Occupancy == Right.Occupancy
					&& Left.Ports == Right.Ports
					&& Left.RequiredMacroNodeId
						== Right.RequiredMacroNodeId
					&& Left.bHardAnchor == Right.bHardAnchor
					&& Left.DerivationPath
						== Right.DerivationPath);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5BEnvelopeFaultInjectionTest,
	"ABTS.M73DAG.DAG5B.EnvelopeFaultInjection",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5BEnvelopeFaultInjectionTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73DAG5BTests;
	FFixture Fixture;
	Fixture.SemanticSettings.ShapeFamily =
		EABTSM73DAG5BShapeFamily::ThroughOpeningWall;
	FABTSM73DAG5BResult Result;
	FABTSM73StructureData Data;
	FString Error;
	TestTrue(TEXT("Fault fixture builds"),
		Build(Fixture, Result, Data, Error));
	if (Data.Bricks.IsEmpty()) return false;

	FABTSM73DAG5BEnvelopeAuditor Auditor;
	FABTSM73DAG5BAuditResult FaultAudit;
	const FABTSM73DAG5BSemanticCellRecord* ExistingVoid =
		Data.DAG5BSemanticEnvelope.Cells.FindByPredicate(
			[](const FABTSM73DAG5BSemanticCellRecord& Cell)
			{
				return Cell.Occupancy
					== EABTSM73DAG5BOccupancy::MustVoid;
			});
	TestNotNull(TEXT("Fault fixture has a sealed MustVoid"), ExistingVoid);
	FABTSM73StructureData VoidFault = Data;
	if (ExistingVoid != nullptr)
	{
		FABTSM73BrickNode& Intruder =
			VoidFault.Bricks.AddDefaulted_GetRef();
		Intruder.NodeId = VoidFault.Bricks.Num() - 1;
		Intruder.LocalCenter = ExistingVoid->LocalBounds.GetCenter();
		Intruder.DimensionsCM =
			ExistingVoid->LocalBounds.GetExtent();
	}
	TestFalse(TEXT("Occupied MustVoid fails closed"),
		Auditor.Audit(
			VoidFault.DAG5BSemanticEnvelope,
			VoidFault,
			FaultAudit,
			Error));
	TestTrue(TEXT("MustVoid failure is diagnosed"),
		FaultAudit.MustVoidViolationCount > 0);

	const FABTSM73DAG5BSemanticCellRecord* FloorContract =
		Data.DAG5BSemanticEnvelope.Cells.FindByPredicate(
			[](const FABTSM73DAG5BSemanticCellRecord& Cell)
			{
				return Cell.Occupancy
						== EABTSM73DAG5BOccupancy::MustOccupy
					&& Cell.Semantic
						== EABTSM73DAG5BSemanticCell::FloorCarrier;
			});
	TestNotNull(TEXT("Fixture has an unmapped FloorCarrier contract"),
		FloorContract);
	FABTSM73StructureData OccupyFault = Data;
	if (FloorContract != nullptr)
	{
		FABTSM73BrickNode* FloorBrick =
			OccupyFault.Bricks.FindByPredicate(
				[FloorContract](const FABTSM73BrickNode& Brick)
				{
					return Brick.MacroNodeId
						== FloorContract->RequiredMacroNodeId;
				});
		TestNotNull(TEXT("FloorCarrier physical Brick exists"), FloorBrick);
		if (FloorBrick != nullptr)
		{
			FloorBrick->DimensionsCM = FVector(2.0f);
		}
	}
	TestFalse(TEXT("Uncovered MustOccupy fails closed"),
		Auditor.Audit(
			OccupyFault.DAG5BSemanticEnvelope,
			OccupyFault,
			FaultAudit,
			Error));
	TestTrue(TEXT("MustOccupy failure is diagnosed"),
		FaultAudit.UncoveredMustOccupyCount > 0);

	TestFalse(TEXT("Fault fixture has support ports"),
		Data.DAG5BSemanticEnvelope.SupportPorts.IsEmpty());
	if (Data.DAG5BSemanticEnvelope.SupportPorts.IsEmpty()) return false;
	FABTSM73SemanticEnvelope ProvenanceFault =
		Data.DAG5BSemanticEnvelope;
	ProvenanceFault.SupportPorts[0].AllowedColumnRegion.Min.X += 1.0f;
	FABTSM73DAG5BSemanticEnvelopeBuilder ContractBuilder;
	TestFalse(TEXT("Shrunk port cannot forge raw WFC provenance"),
		ContractBuilder.ValidateSupportPortProvenance(
			ProvenanceFault,
			ProvenanceFault.SupportPorts[0],
			Error));
	TestTrue(TEXT("Direct provenance validation is explicit"),
		Error.Contains(TEXT("DAG5BSupportPortProvenanceMismatch")));
	TestFalse(TEXT("Forged WFC support-port provenance fails closed"),
		Auditor.Audit(ProvenanceFault, Data, FaultAudit, Error));
	TestTrue(TEXT("Sealed envelope identity rejects port mutation"),
		Error.Contains(TEXT("DAG5BEnvelopeIdentityMismatch")));

	FABTSM73SemanticEnvelope ScopeIdentityFault =
		Data.DAG5BSemanticEnvelope;
	ScopeIdentityFault.ShapeScopes[0].NormalizedBounds.Max.X -= 0.01f;
	TestFalse(TEXT("Shape scope mutation invalidates the sealed envelope"),
		ContractBuilder.ValidateEnvelopeIdentity(
			ScopeIdentityFault,
			Error));
	TestTrue(TEXT("Shape scope identity failure is explicit"),
		Error.Contains(TEXT("DAG5BEnvelopeIdentityMismatch")));

	FABTSM73StructureData PortEdgeFault = Data;
	const FABTSM73DAG5BSemanticBrickMapping* SupportMapping =
		PortEdgeFault.DAG5BSemanticBrickMappings.FindByPredicate(
			[](const FABTSM73DAG5BSemanticBrickMapping& Mapping)
			{
				return Mapping.Kind
					== EABTSM73DAG5BSemanticMappingKind::SupportPort;
			});
	TestNotNull(TEXT("Fault fixture has a SupportPort mapping"),
		SupportMapping);
	if (SupportMapping == nullptr) return false;
	TestFalse(TEXT("SupportPort mapping has physical columns"),
		SupportMapping->BrickNodeIds.IsEmpty());
	if (SupportMapping->BrickNodeIds.IsEmpty()) return false;
	FABTSM73BrickNode& Column =
		PortEdgeFault.Bricks[SupportMapping->BrickNodeIds[0]];
	Column.DimensionsCM.X += 120.0f;
	TestFalse(
		TEXT("Column center inside but AABB edge outside port fails"),
		Auditor.Audit(
			PortEdgeFault.DAG5BSemanticEnvelope,
			PortEdgeFault,
			FaultAudit,
			Error));
	TestTrue(TEXT("Port AABB failure is explicit"),
		Error.Contains(
			TEXT("DAG5BAuditSemanticMappingGeometryMismatch")));

	FABTSM73StructureData ShapeMappingFault = Data;
	const FABTSM73DAG5BSemanticBrickMapping* ShapeMapping =
		ShapeMappingFault.DAG5BSemanticBrickMappings.FindByPredicate(
			[](const FABTSM73DAG5BSemanticBrickMapping& Mapping)
			{
				return Mapping.Kind
					== EABTSM73DAG5BSemanticMappingKind::ShapeMacro;
			});
	TestNotNull(TEXT("Fault fixture has a ShapeMacro mapping"),
		ShapeMapping);
	if (ShapeMapping == nullptr) return false;
	TestEqual(TEXT("ShapeMacro mapping binds exactly one plate"),
		ShapeMapping->BrickNodeIds.Num(), 1);
	if (ShapeMapping->BrickNodeIds.Num() != 1) return false;
	ShapeMappingFault.Bricks[ShapeMapping->BrickNodeIds[0]]
		.LocalCenter.Z +=
			ShapeMappingFault.DAG5BSemanticEnvelope.LocalBounds
				.GetSize().Z;
	TestFalse(TEXT("Non-intersecting WFC/Shape mapping fails"),
		Auditor.Audit(
			ShapeMappingFault.DAG5BSemanticEnvelope,
			ShapeMappingFault,
			FaultAudit,
			Error));
	TestTrue(
		FString::Printf(
			TEXT("ShapeMacro spatial mismatch is explicit: %s"),
			*Error),
		Error.Contains(TEXT("DAG5BAuditShapeMacroMappingMismatch")));

	FABTSM73StructureData OwnScopeFault = Data;
	const FABTSM73DAG5BSemanticCellRecord* OwnScopeContract =
		Data.DAG5BSemanticEnvelope.Cells.FindByPredicate(
			[](const FABTSM73DAG5BSemanticCellRecord& Cell)
			{
				return Cell.Occupancy
						== EABTSM73DAG5BOccupancy::MustOccupy
					&& Cell.Semantic
						== EABTSM73DAG5BSemanticCell::FloorCarrier;
			});
	TestNotNull(TEXT("Own-scope fault has a FloorCarrier"),
		OwnScopeContract);
	if (OwnScopeContract != nullptr)
	{
		FABTSM73BrickNode* OwnScopeBrick =
			OwnScopeFault.Bricks.FindByPredicate(
				[OwnScopeContract](const FABTSM73BrickNode& Brick)
				{
					return Brick.MacroNodeId
						== OwnScopeContract->RequiredMacroNodeId;
				});
		TestNotNull(TEXT("Own-scope physical Brick exists"), OwnScopeBrick);
		if (OwnScopeBrick != nullptr)
		{
			OwnScopeBrick->LocalCenter.X +=
				OwnScopeBrick->DimensionsCM.X;
			TestFalse(TEXT("Plate inside another scope cannot bypass its own"),
				Auditor.Audit(
					OwnScopeFault.DAG5BSemanticEnvelope,
					OwnScopeFault,
					FaultAudit,
					Error));
			TestTrue(TEXT("Own Shape scope failure is diagnosed"),
				FaultAudit.OutOfShapeScopeBrickCount > 0);
		}
	}

	FABTSM73StructureData EnvelopeEdgeFault = Data;
	FABTSM73BrickNode& Outside =
		EnvelopeEdgeFault.Bricks.AddDefaulted_GetRef();
	Outside.NodeId = EnvelopeEdgeFault.Bricks.Num() - 1;
	Outside.LocalCenter = FVector(
		Data.DAG5BSemanticEnvelope.LocalBounds.Max.X - 1.0f,
		0.0f,
		Data.DAG5BSemanticEnvelope.LocalBounds.Max.Z * 0.5f);
	Outside.DimensionsCM = FVector(20.0f);
	TestFalse(TEXT("Brick corner outside total envelope fails"),
		Auditor.Audit(
			EnvelopeEdgeFault.DAG5BSemanticEnvelope,
			EnvelopeEdgeFault,
			FaultAudit,
			Error));
	TestTrue(TEXT("Full Brick AABB outside is diagnosed"),
		FaultAudit.OutOfEnvelopeBrickCount > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5BBudgetAtomicityTest,
	"ABTS.M73DAG.DAG5B.BudgetAtomicity",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5BBudgetAtomicityTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73DAG5BTests;
	FFixture Fixture;
	Fixture.SemanticSettings.MaxWFCPropagationOperations = 64;
	FABTSM73DAG5BResult Result;
	FABTSM73StructureData Data;
	FABTSM73BrickNode& Sentinel = Data.Bricks.AddDefaulted_GetRef();
	Sentinel.NodeId = 99;
	FString Error;
	TestFalse(TEXT("Insufficient WFC budget rejects"),
		Build(Fixture, Result, Data, Error));
	TestTrue(TEXT("Budget failure is explicit"),
		Error.Contains(TEXT("DAG5BWFCPropagationBudgetExceeded")));
	TestTrue(TEXT("Failure publishes no Legacy or partial bricks"),
		IsEmptyStructureData(Data));

	Fixture = FFixture();
	Fixture.DAGSettings.MaxEstimatedBrickCount = 1;
	TestFalse(TEXT("Estimated DAG brick budget rejects"),
		Build(Fixture, Result, Data, Error));
	TestTrue(TEXT("Estimated budget failure is explicit"),
		Error.Contains(TEXT("DAG5BEstimatedBrickBudgetExceeded")));
	TestTrue(TEXT("Estimated budget failure is atomic"),
		IsEmptyStructureData(Data));

	Fixture = FFixture();
	Fixture.SemanticSettings.GridSizeX = 4;
	TestFalse(TEXT("Invalid semantic settings reject"),
		Build(Fixture, Result, Data, Error));
	TestEqual(TEXT("Invalid settings have a stable code"),
		Error, FString(TEXT("DAG5BSettingsInvalid")));
	TestTrue(TEXT("Settings failure is atomic"),
		IsEmptyStructureData(Data));

	Fixture = FFixture();
	Fixture.SemanticSettings.SetbackRatio =
		std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("Non-finite semantic ratio rejects"),
		Build(Fixture, Result, Data, Error));
	TestEqual(TEXT("Non-finite settings have a stable code"),
		Error, FString(TEXT("DAG5BSettingsInvalid")));
	TestTrue(TEXT("Non-finite failure is atomic"),
		IsEmptyStructureData(Data));

	Fixture = FFixture();
	Fixture.PlayabilitySettings.bEnablePlayabilityRouting = true;
	FABTSM73BrickNode& DownstreamSentinel =
		Data.Bricks.AddDefaulted_GetRef();
	DownstreamSentinel.NodeId = 101;
	TestFalse(TEXT("Downstream DAG3-C prerequisite rejects"),
		Build(Fixture, Result, Data, Error));
	TestEqual(TEXT("Downstream rejection has a stable code"),
		Error,
		FString(TEXT("DAG3CRequiresAnalysisRewriteAndGeneralizedCut")));
	TestTrue(TEXT("DAG5-B was compiled before the downstream gate"),
		Result.ShapeHash != 0
			&& Result.WFCHash != 0
			&& Result.Audit.bAccepted);
	TestFalse(TEXT("Downstream failure revokes accepted stage metadata"),
		Result.bAccepted);
	TestEqual(TEXT("Downstream failure is recorded on the candidate"),
		Result.RejectReason, Error);
	TestTrue(TEXT("Downstream failure publishes no baseline structure"),
		IsEmptyStructureData(Data));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5BBacktrackBudgetTest,
	"ABTS.M73DAG.DAG5B.WFCBacktrackBudget",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5BBacktrackBudgetTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73DAG5BTests;
	FFixture Fixture;
	Fixture.DAGSettings.BuildingSeed = 720022;
	Fixture.SemanticSettings.ShapeFamily =
		EABTSM73DAG5BShapeFamily::ThroughOpeningWall;
	FABTSM73DAG5BResult Result;
	FABTSM73StructureData Data;
	FString Error;
	const bool bBuilt = Build(Fixture, Result, Data, Error);
	TestTrue(
		FString::Printf(
			TEXT("Frozen WFC backtracking fixture builds: %s"),
			*Error),
		bBuilt);
	TestEqual(TEXT("Frozen fixture takes three deterministic backtracks"),
		Result.BacktrackStepCount, 3);

	Fixture.SemanticSettings.MaxWFCBacktrackSteps = 2;
	FABTSM73DAG5BResult RejectedResult;
	FABTSM73StructureData RejectedData;
	FString RejectedError;
	TestFalse(TEXT("One-less WFC backtrack budget rejects"),
		Build(
			Fixture,
			RejectedResult,
			RejectedData,
			RejectedError));
	TestTrue(TEXT("Backtrack budget rejection is explicit"),
		RejectedError.Contains(
			TEXT("DAG5BWFCBacktrackBudgetExceeded")));
	TestTrue(TEXT("Backtrack budget rejection is atomic"),
		IsEmptyStructureData(RejectedData));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5BDisabledCompatibilityTest,
	"ABTS.M73DAG.DAG5B.DisabledCompatibility",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5BDisabledCompatibilityTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73DAG5BTests;
	FFixture Fixture;
	Fixture.SemanticSettings.bEnableSemanticEnvelope = false;
	FABTSM73DAGBuildingPipeline Pipeline;
	FABTSM73StructureData Baseline;
	FABTSM73StructureData Disabled;
	FABTSM73DAG5BResult DisabledResult;
	FString BaselineError;
	FString DisabledError;
	const bool bBaseline = Pipeline.BuildWithFailurePattern(
		Fixture.DAGSettings,
		Fixture.LayoutSettings,
		Fixture.BuildingSettings,
		Fixture.FrontierSettings,
		Fixture.PatternSettings,
		Fixture.PlayabilitySettings,
		Fixture.DifficultySettings,
		Fixture.MaterialProfiles,
		FVector::ForwardVector,
		Baseline,
		BaselineError);
	const bool bDisabled = Pipeline.BuildWithFailurePattern(
		Fixture.SemanticSettings,
		Fixture.DAGSettings,
		Fixture.LayoutSettings,
		Fixture.BuildingSettings,
		Fixture.FrontierSettings,
		Fixture.PatternSettings,
		Fixture.PlayabilitySettings,
		Fixture.DifficultySettings,
		Fixture.MaterialProfiles,
		FVector::ForwardVector,
		DisabledResult,
		Disabled,
		DisabledError);
	TestEqual(TEXT("Disabled B preserves success result"),
		bDisabled, bBaseline);
	TestEqual(TEXT("Disabled B preserves error"),
		DisabledError, BaselineError);
	TestTrue(TEXT("Disabled B preserves full geometry"),
		EqualGeometry(Baseline, Disabled));
	TestFalse(TEXT("Disabled B result remains disabled"),
		DisabledResult.bEnabled);

	struct FGoldenCase
	{
		EABTSM3TaskType TaskType;
		EABTSM7BuildingMaterial Material;
		int32 Seed;
		int32 BrickCount;
		uint32 TopologyHash;
	};
	const FGoldenCase GoldenCases[] = {
		{EABTSM3TaskType::Workshop,
			EABTSM7BuildingMaterial::Wood,
			1034266606,
			13,
			2796521057u},
		{EABTSM3TaskType::TargetBuilding,
			EABTSM7BuildingMaterial::Stone,
			1034264727,
			17,
			1424001057u},
		{EABTSM3TaskType::FurnaceRuins,
			EABTSM7BuildingMaterial::Iron,
			1034267999,
			13,
			2796521057u}
	};
	for (const FGoldenCase& Golden : GoldenCases)
	{
		FABTSM7TaskGraphBuildingProfile Profile =
			FABTSM7TaskGraphDAG23ProfileResolver::MakeDefaultProfile(
				Golden.TaskType,
				Golden.Material);
		Profile.GenerationSettings.BuildingSeed = Golden.Seed;
		Profile.DAGGenerationSettings.BuildingSeed = Golden.Seed;
		FABTSM73DAG5BSettings DisabledSemanticSettings;
		FABTSM73DAG5BResult GoldenResult;
		FABTSM73StructureData GoldenData;
		FString GoldenError;
		const bool bGoldenBuilt = Pipeline.BuildWithFailurePattern(
			DisabledSemanticSettings,
			Profile.DAGGenerationSettings,
			Profile.DAGLayoutSettings,
			Profile.GenerationSettings,
			Profile.DAGFailureFrontierSettings,
			Profile.DAGFailurePatternSettings,
			Profile.DAGFailurePlayabilitySettings,
			Profile.DifficultySettings,
			Fixture.MaterialProfiles,
			FVector::ForwardVector,
			GoldenResult,
			GoldenData,
			GoldenError);
		TestTrue(
			FString::Printf(
				TEXT("Disabled B frozen production case builds: %s"),
				*GoldenError),
			bGoldenBuilt);
		if (!bGoldenBuilt) continue;
		TestEqual(TEXT("Frozen legacy brick count"),
			GoldenData.Bricks.Num(), Golden.BrickCount);
		TestEqual(TEXT("Frozen legacy topology hash"),
			GoldenData.DAGTopologyHash, Golden.TopologyHash);
		TestFalse(TEXT("Frozen case keeps B disabled"),
			GoldenResult.bEnabled);
		TestTrue(TEXT("Frozen case emits no semantic envelope"),
			GoldenData.DAG5BSemanticEnvelope.Cells.IsEmpty()
				&& GoldenData.DAG5BSemanticEnvelope.SupportPorts.IsEmpty()
				&& GoldenData.DAG5BSemanticEnvelope.EnvelopeHash == 0
				&& GoldenData.DAG5BSemanticBrickMappings.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5BSearchBypassTest,
	"ABTS.M73DAG.DAG5B.SearchBypassesLegacyScope",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5BSearchBypassTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73DAG5BTests;
	FFixture Fixture;
	Fixture.SemanticSettings.ShapeFamily =
		EABTSM73DAG5BShapeFamily::SetbackTower;
	Fixture.SearchSettings.bEnableFeasibilitySearch = true;
	Fixture.SearchSettings.MaxCandidateAttempts = 4;
	// This is deliberately impossible for the legacy recursive preflight.
	// DAG5-B owns its candidate source, so these settings must not veto it.
	Fixture.DAGSettings.MinExpansionDepth = 6;
	Fixture.DAGSettings.MaxExpansionDepth = 6;
	Fixture.DAGSettings.ExpansionStepBudget = 0;
	FABTSM73DAGBuildingPipeline Pipeline;
	FABTSM73DAG5AResult SearchResult;
	FABTSM73DAG5BResult SemanticResult;
	FABTSM73StructureData Data;
	FString Error;
	const bool bBuilt = Pipeline.BuildWithFeasibilitySearch(
		Fixture.SearchSettings,
		Fixture.SemanticSettings,
		Fixture.DAGSettings,
		Fixture.LayoutSettings,
		Fixture.BuildingSettings,
		Fixture.FrontierSettings,
		Fixture.PatternSettings,
		Fixture.PlayabilitySettings,
		Fixture.DifficultySettings,
		Fixture.MaterialProfiles,
		FVector::ForwardVector,
		SearchResult,
		SemanticResult,
		Data,
		Error);
	TestTrue(
		FString::Printf(
			TEXT("Semantic search bypasses old Scope preflight: %s"),
			*Error),
		bBuilt);
	TestTrue(TEXT("Search accepts a semantic candidate"),
		SearchResult.bAccepted);
	TestTrue(TEXT("Semantic candidate is accepted"),
		SemanticResult.bAccepted);
	TestEqual(TEXT("No legacy Scope preflight rejection occurs"),
		SearchResult.ScopePreflightRejectCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5BSpatialCausalityTest,
	"ABTS.M73DAG.DAG5B.SpatialWFCPortDAGCausality",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5BSpatialCausalityTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73DAG5BTests;
	FFixture Fixture;
	Fixture.SemanticSettings.ShapeFamily =
		EABTSM73DAG5BShapeFamily::OffsetBridge;
	FABTSM73DAGGenerationResult Graph;
	FABTSM73DAGSpatialLayout InitialLayout;
	FABTSM73SemanticEnvelope Envelope;
	FABTSM73DAG5BResult Result;
	FString Error;
	TestTrue(TEXT("Semantic front end builds"),
		BuildFrontEnd(
			Fixture,
			Graph,
			InitialLayout,
			Envelope,
			Result,
			Error));
	if (!Envelope.bAccepted) return false;
	TestEqual(TEXT("Every graph edge emits one support port"),
		Envelope.SupportPorts.Num(), Graph.SupportEdges.Num());

	FABTSM73DAGLayoutSolver Solver;
	FABTSM73DAGSpatialLayout Layout;
	TestTrue(
		FString::Printf(
			TEXT("DAG2.3 consumes WFC support ports: %s"),
			*Error),
		Solver.SolveSemantic(
			Graph,
			Fixture.LayoutSettings,
			InitialLayout,
			Envelope,
			Layout,
			Error));
	if (!Layout.bAccepted) return false;
	for (const FABTSM73DAGSelectedSupport& Support :
		Layout.SelectedSupports)
	{
		const FABTSM73DAG5BSupportPortConstraint* Port =
			Envelope.SupportPorts.FindByPredicate(
				[&Support](
					const FABTSM73DAG5BSupportPortConstraint& Candidate)
				{
					return Candidate.SupportMacroNodeId
							== Support.SupportMacroNodeId
						&& Candidate.LoadMacroNodeId
							== Support.LoadMacroNodeId;
				});
		TestNotNull(TEXT("Selected support retains its WFC port"), Port);
		if (Port == nullptr) continue;
		for (const FVector2D& Center :
			Support.RealizedColumnCenters)
		{
			const FVector2D Half(
				Support.RealizedColumnWidthCM * 0.5f);
			TestTrue(TEXT("Full column footprint is inside WFC port"),
				Port->AllowedColumnRegion.IsInsideOrOn(Center - Half)
					&& Port->AllowedColumnRegion.IsInsideOrOn(
						Center + Half));
		}
	}

	FABTSM73DAG5BSemanticEnvelopeBuilder Builder;
	TestTrue(TEXT("Final layout binds to immutable WFC contract"),
		Builder.BindPhysicalContract(Layout, Envelope, Error));
	FABTSM73DAGModuleCompiler Compiler;
	FABTSM73StructureData Data;
	TestTrue(
		FString::Printf(
			TEXT("Semantic mapping compiles and audits: %s"),
			*Error),
		Compiler.CompileSemantic(
			Fixture.BuildingSettings,
			Graph,
			Fixture.LayoutSettings,
			Layout,
			Envelope,
			Result,
			Data,
			Error));
	if (Data.Bricks.IsEmpty()) return false;
	int32 SupportMappingCount = 0;
	int32 ShapeMappingCount = 0;
	for (const FABTSM73DAG5BSemanticBrickMapping& Mapping :
		Data.DAG5BSemanticBrickMappings)
	{
		SupportMappingCount += Mapping.Kind
				== EABTSM73DAG5BSemanticMappingKind::SupportPort
			? 1
			: 0;
		ShapeMappingCount += Mapping.Kind
				== EABTSM73DAG5BSemanticMappingKind::ShapeMacro
			? 1
			: 0;
	}
	TestEqual(TEXT("Each physical interface has one WFC mapping"),
		SupportMappingCount,
		Data.DAGPhysicalSupportMappings.Num());
	TestTrue(TEXT("Visible non-column semantics are also lowered"),
		ShapeMappingCount > 0);

	FABTSM73SemanticEnvelope MissingPort = Envelope;
	MissingPort.SupportPorts.RemoveAt(0);
	FABTSM73DAGSpatialLayout RejectedLayout;
	TestFalse(TEXT("Missing WFC port rejects before compile"),
		Solver.SolveSemantic(
			Graph,
			Fixture.LayoutSettings,
			InitialLayout,
			MissingPort,
			RejectedLayout,
			Error));
	TestTrue(TEXT("Missing sealed port has a stable diagnosis"),
		Error.StartsWith(TEXT("DAG5BEnvelopeIdentityMismatch")));

	FABTSM73SemanticEnvelope ForgedPort = Envelope;
	ForgedPort.SupportPorts[0].SourceCellHash ^= 1u;
	TestFalse(TEXT("Direct WFC provenance validation rejects forgery"),
		Builder.ValidateSupportPortProvenance(
			ForgedPort,
			ForgedPort.SupportPorts[0],
			Error));
	TestTrue(TEXT("Direct provenance has a stable diagnosis"),
		Error.StartsWith(TEXT("DAG5BSupportPortProvenanceMismatch")));
	TestFalse(TEXT("Forged WFC provenance rejects before DAG solve"),
		Solver.SolveSemantic(
			Graph,
			Fixture.LayoutSettings,
			InitialLayout,
			ForgedPort,
			RejectedLayout,
			Error));
	TestTrue(TEXT("Forged sealed envelope has a stable diagnosis"),
		Error.StartsWith(TEXT("DAG5BEnvelopeIdentityMismatch")));

	FABTSM73SemanticEnvelope ColumnVoid = Envelope;
	TestTrue(TEXT("Semantic layout has at least one selected support"),
		!Layout.SelectedSupports.IsEmpty());
	if (Layout.SelectedSupports.IsEmpty()) return false;
	const FABTSM73DAGSelectedSupport& FirstSupport =
		Layout.SelectedSupports[0];
	const FABTSM73DAGMacroLayout* Lower =
		Layout.MacroLayouts.FindByPredicate(
			[&FirstSupport](const FABTSM73DAGMacroLayout& Macro)
			{
				return Macro.MacroNodeId
					== FirstSupport.SupportMacroNodeId;
			});
	const FABTSM73DAGMacroLayout* Upper =
		Layout.MacroLayouts.FindByPredicate(
			[&FirstSupport](const FABTSM73DAGMacroLayout& Macro)
			{
				return Macro.MacroNodeId
					== FirstSupport.LoadMacroNodeId;
			});
	TestNotNull(TEXT("Selected support lower macro exists"), Lower);
	TestNotNull(TEXT("Selected support upper macro exists"), Upper);
	if (Lower != nullptr && Upper != nullptr)
	{
		const FVector2D Center =
			FirstSupport.RealizedColumnCenters[0];
		const float Bottom =
			Lower->PlateCenter.Z
				+ Lower->PlateDimensionsCM.Z * 0.5f;
		const float Top =
			Upper->PlateCenter.Z
				- Upper->PlateDimensionsCM.Z * 0.5f;
		FABTSM73DAG5BSemanticCellRecord& Fault =
			ColumnVoid.Cells.AddDefaulted_GetRef();
		Fault.Occupancy = EABTSM73DAG5BOccupancy::MustVoid;
		Fault.Semantic = EABTSM73DAG5BSemanticCell::DoorVoid;
		Fault.LocalBounds = FBox(
			FVector(Center.X - 4.0f, Center.Y - 4.0f,
				FMath::Lerp(Bottom, Top, 0.45f)),
			FVector(Center.X + 4.0f, Center.Y + 4.0f,
				FMath::Lerp(Bottom, Top, 0.55f)));
		Fault.DerivationPath = TEXT("Test/InjectedColumnVoid");
		TestFalse(TEXT("MustVoid through a selected column rejects"),
			Builder.BindPhysicalContract(Layout, ColumnVoid, Error));
		TestTrue(TEXT("Injected void invalidates the sealed envelope"),
			Error.StartsWith(TEXT("DAG5BEnvelopeIdentityMismatch")));
		FABTSM73StructureData RejectedData;
		FABTSM73DAG5BResult RejectedResult = Result;
		TestFalse(TEXT("Compiler cannot bypass final contract binding"),
			Compiler.CompileSemantic(
				Fixture.BuildingSettings,
				Graph,
				Fixture.LayoutSettings,
				Layout,
				ColumnVoid,
				RejectedResult,
				RejectedData,
				Error));
		TestTrue(TEXT("Direct compile failure remains atomic"),
			IsEmptyStructureData(RejectedData));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5BWFCInfluenceTest,
	"ABTS.M73DAG.DAG5B.WFCInfluencesPhysicalAssembly",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5BWFCInfluenceTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73DAG5BTests;
	FFixture FirstFixture;
	FirstFixture.SemanticSettings.ShapeFamily =
		EABTSM73DAG5BShapeFamily::SetbackTower;
	FirstFixture.DAGSettings.BuildingSeed = 710000;
	FFixture SecondFixture = FirstFixture;
	SecondFixture.DAGSettings.BuildingSeed = 710001;
	FABTSM73DAG5BResult FirstResult;
	FABTSM73DAG5BResult SecondResult;
	FABTSM73StructureData FirstData;
	FABTSM73StructureData SecondData;
	FString FirstError;
	FString SecondError;
	const bool bFirst = Build(
		FirstFixture,
		FirstResult,
		FirstData,
		FirstError);
	const bool bSecond = Build(
		SecondFixture,
		SecondResult,
		SecondData,
		SecondError);
	TestTrue(
		FString::Printf(TEXT("Frozen WFC candidate A builds: %s"), *FirstError),
		bFirst);
	TestTrue(
		FString::Printf(TEXT("Frozen WFC candidate B builds: %s"), *SecondError),
		bSecond);
	if (!bFirst || !bSecond) return false;
	TestEqual(TEXT("Frozen pair retains one Shape Grammar silhouette"),
		SecondResult.ShapeHash, FirstResult.ShapeHash);
	TestNotEqual(TEXT("Frozen pair selects a different WFC solution"),
		SecondResult.WFCHash, FirstResult.WFCHash);
	TestNotEqual(TEXT("WFC difference changes physical support portals"),
		PortSignature(SecondData.DAG5BSemanticEnvelope),
		PortSignature(FirstData.DAG5BSemanticEnvelope));
	TestNotEqual(TEXT("WFC portal difference changes final Brick geometry"),
		GeometrySignature(SecondData),
		GeometrySignature(FirstData));
	bool bSameLogicalSupportMovedColumn = false;
	for (const FABTSM73DAG5BSupportPortConstraint& FirstPort :
		FirstData.DAG5BSemanticEnvelope.SupportPorts)
	{
		const FABTSM73DAG5BSupportPortConstraint* SecondPort =
			SecondData.DAG5BSemanticEnvelope.SupportPorts.FindByPredicate(
				[&FirstPort](
					const FABTSM73DAG5BSupportPortConstraint& Candidate)
				{
					return Candidate.SupportMacroNodeId
							== FirstPort.SupportMacroNodeId
						&& Candidate.LoadMacroNodeId
							== FirstPort.LoadMacroNodeId;
				});
		if (SecondPort == nullptr
			|| (FirstPort.AllowedColumnRegion.Min.Equals(
					SecondPort->AllowedColumnRegion.Min)
				&& FirstPort.AllowedColumnRegion.Max.Equals(
					SecondPort->AllowedColumnRegion.Max)))
		{
			continue;
		}
		const FABTSM73DAGPhysicalSupportMapping* FirstPhysical =
			FirstData.DAGPhysicalSupportMappings.FindByPredicate(
				[&FirstPort](
					const FABTSM73DAGPhysicalSupportMapping& Candidate)
				{
					return Candidate.SupportMacroNodeId
							== FirstPort.SupportMacroNodeId
						&& Candidate.LoadMacroNodeId
							== FirstPort.LoadMacroNodeId;
				});
		const FABTSM73DAGPhysicalSupportMapping* SecondPhysical =
			SecondData.DAGPhysicalSupportMappings.FindByPredicate(
				[&FirstPort](
					const FABTSM73DAGPhysicalSupportMapping& Candidate)
				{
					return Candidate.SupportMacroNodeId
							== FirstPort.SupportMacroNodeId
						&& Candidate.LoadMacroNodeId
							== FirstPort.LoadMacroNodeId;
				});
		if (FirstPhysical == nullptr || SecondPhysical == nullptr)
		{
			continue;
		}
		TArray<FVector> FirstCenters;
		TArray<FVector> SecondCenters;
		bool bValidCenterSets = true;
		for (const int32 NodeId : FirstPhysical->ColumnNodeIds)
		{
			if (!FirstData.Bricks.IsValidIndex(NodeId))
			{
				bValidCenterSets = false;
				break;
			}
			FirstCenters.Add(FirstData.Bricks[NodeId].LocalCenter);
		}
		for (const int32 NodeId : SecondPhysical->ColumnNodeIds)
		{
			if (!SecondData.Bricks.IsValidIndex(NodeId))
			{
				bValidCenterSets = false;
				break;
			}
			SecondCenters.Add(SecondData.Bricks[NodeId].LocalCenter);
		}
		if (!bValidCenterSets) continue;
		auto SortCenter = [](const FVector& A, const FVector& B)
		{
			if (A.X != B.X) return A.X < B.X;
			if (A.Y != B.Y) return A.Y < B.Y;
			return A.Z < B.Z;
		};
		FirstCenters.Sort(SortCenter);
		SecondCenters.Sort(SortCenter);
		if (FirstCenters.Num() != SecondCenters.Num())
		{
			bSameLogicalSupportMovedColumn = true;
		}
		else
		{
			for (int32 CenterIndex = 0;
				CenterIndex < FirstCenters.Num();
				++CenterIndex)
			{
				if (!FirstCenters[CenterIndex].Equals(
					SecondCenters[CenterIndex]))
				{
					bSameLogicalSupportMovedColumn = true;
					break;
				}
			}
		}
		if (bSameLogicalSupportMovedColumn) break;
	}
	TestTrue(
		TEXT("A changed WFC portal moves a physical column on the same logical support"),
		bSameLogicalSupportMovedColumn);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG5BSemanticRetryTest,
	"ABTS.M73DAG.DAG5B.SemanticRetryNoFallback",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG5BSemanticRetryTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73DAG5BTests;
	FFixture Fixture;
	Fixture.SemanticSettings.ShapeFamily =
		EABTSM73DAG5BShapeFamily::Auto;
	Fixture.DAGSettings.BuildingSeed = 6;
	Fixture.DAGSettings.Preset = EABTSM73DAGPreset::SingleTower;
	Fixture.SearchSettings.bEnableFeasibilitySearch = true;
	// Auto(seed=6) selects the larger ThroughOpening family, while retry seed
	// 1 selects the smaller Setback family.
	Fixture.SearchSettings.MaxCompiledBrickCount = 24;
	Fixture.SearchSettings.MaxCandidateAttempts = 1;
	FABTSM73DAGBuildingPipeline Pipeline;
	FABTSM73DAG5AResult FailedSearch;
	FABTSM73DAG5BResult FailedSemantic;
	FABTSM73StructureData FailedData;
	FABTSM73BrickNode& Sentinel =
		FailedData.Bricks.AddDefaulted_GetRef();
	Sentinel.NodeId = 77;
	FString FailedError;
	const bool bSingleAttempt = Pipeline.BuildWithFeasibilitySearch(
		Fixture.SearchSettings,
		Fixture.SemanticSettings,
		Fixture.DAGSettings,
		Fixture.LayoutSettings,
		Fixture.BuildingSettings,
		Fixture.FrontierSettings,
		Fixture.PatternSettings,
		Fixture.PlayabilitySettings,
		Fixture.DifficultySettings,
		Fixture.MaterialProfiles,
		FVector::ForwardVector,
		FailedSearch,
		FailedSemantic,
		FailedData,
		FailedError);
	TestFalse(TEXT("Semantic attempt zero is rejected by A's real Brick budget"),
		bSingleAttempt);
	TestEqual(TEXT("Single-attempt budget is honored"),
		FailedSearch.AttemptCount, 1);
	TestTrue(TEXT("Failure is a bounded no-feasible-candidate result"),
		FailedError.StartsWith(TEXT("DAG5ANoFeasibleCandidate")));
	TestEqual(TEXT("Single-attempt trace has one entry"),
		FailedSearch.Attempts.Num(), 1);
	if (FailedSearch.Attempts.Num() == 1)
	{
		TestEqual(TEXT("Attempt zero reaches the compiled Brick gate"),
			FailedSearch.Attempts[0].RejectStage,
			EABTSM73DAG5ARejectStage::CompiledBrickBudget);
	}
	TestTrue(TEXT("Rejected semantic candidate publishes no partial data"),
		IsEmptyStructureData(FailedData));
	TestFalse(TEXT("Rejected semantic candidate does not leak accepted metadata"),
		FailedSemantic.bAccepted);
	TestEqual(TEXT("Rejected semantic candidate publishes no envelope hash"),
		FailedSemantic.EnvelopeHash, 0u);

	FABTSM73DAG5ASettings RetrySettings = Fixture.SearchSettings;
	RetrySettings.MaxCandidateAttempts = 2;
	FABTSM73DAG5AResult RetrySearch;
	FABTSM73DAG5BResult RetrySemantic;
	FABTSM73StructureData RetryData;
	FString RetryError;
	const bool bRetryBuilt = Pipeline.BuildWithFeasibilitySearch(
		RetrySettings,
		Fixture.SemanticSettings,
		Fixture.DAGSettings,
		Fixture.LayoutSettings,
		Fixture.BuildingSettings,
		Fixture.FrontierSettings,
		Fixture.PatternSettings,
		Fixture.PlayabilitySettings,
		Fixture.DifficultySettings,
		Fixture.MaterialProfiles,
		FVector::ForwardVector,
		RetrySearch,
		RetrySemantic,
		RetryData,
		RetryError);
	TestTrue(
		FString::Printf(
			TEXT("A retries a later semantic candidate: %s"),
			*RetryError),
		bRetryBuilt);
	TestEqual(TEXT("Second semantic candidate is selected"),
		RetrySearch.SelectedAttemptIndex, 1);
	TestEqual(TEXT("Exactly two candidates are attempted"),
		RetrySearch.AttemptCount, 2);
	TestEqual(TEXT("Two-attempt trace has two entries"),
		RetrySearch.Attempts.Num(), 2);
	TestTrue(TEXT("Selected candidate publishes accepted semantic evidence"),
		RetrySemantic.bAccepted
			&& RetryData.DAG5BResult.bAccepted
			&& RetrySemantic.ResultHash
				== RetryData.DAG5BResult.ResultHash);
	if (RetrySearch.Attempts.Num() == 2)
	{
		TestFalse(TEXT("First semantic candidate remains rejected"),
			RetrySearch.Attempts[0].bAccepted);
		TestTrue(TEXT("Second semantic candidate is accepted"),
			RetrySearch.Attempts[1].bAccepted);
		TestNotEqual(TEXT("Retry derives a distinct deterministic Seed"),
			RetrySearch.Attempts[1].CandidateSeed,
			RetrySearch.Attempts[0].CandidateSeed);
	}

	FABTSM73DAG5BSettings ImpossibleSemantic =
		Fixture.SemanticSettings;
	ImpossibleSemantic.MaxWFCPropagationOperations = 64;
	FABTSM73DAG5ASettings NoFallbackSearch =
		Fixture.SearchSettings;
	NoFallbackSearch.MaxCompiledBrickCount = 0;
	NoFallbackSearch.MaxCandidateAttempts = 3;
	FABTSM73DAG5AResult NoFallbackSemanticSearch;
	FABTSM73DAG5BResult NoFallbackSemanticResult;
	FABTSM73StructureData NoFallbackSemanticData;
	FString NoFallbackSemanticError;
	const bool bImpossibleSemanticBuilt =
		Pipeline.BuildWithFeasibilitySearch(
			NoFallbackSearch,
			ImpossibleSemantic,
			Fixture.DAGSettings,
			Fixture.LayoutSettings,
			Fixture.BuildingSettings,
			Fixture.FrontierSettings,
			Fixture.PatternSettings,
			Fixture.PlayabilitySettings,
			Fixture.DifficultySettings,
			Fixture.MaterialProfiles,
			FVector::ForwardVector,
			NoFallbackSemanticSearch,
			NoFallbackSemanticResult,
			NoFallbackSemanticData,
			NoFallbackSemanticError);
	TestFalse(TEXT("Insufficient WFC budget fails closed"),
		bImpossibleSemanticBuilt);
	TestTrue(TEXT("All failed B attempts publish empty StructureData"),
		IsEmptyStructureData(NoFallbackSemanticData));
	TestFalse(TEXT("Failed B attempts publish no accepted semantic result"),
		NoFallbackSemanticResult.bAccepted);
	TestEqual(TEXT("A continues after non-fatal B pipeline rejection"),
		NoFallbackSemanticSearch.AttemptCount,
		NoFallbackSearch.MaxCandidateAttempts);
	TestEqual(TEXT("Every failed B candidate has an attempt trace"),
		NoFallbackSemanticSearch.Attempts.Num(),
		NoFallbackSearch.MaxCandidateAttempts);
	for (const FABTSM73DAG5AAttemptResult& Attempt :
		NoFallbackSemanticSearch.Attempts)
	{
		TestEqual(TEXT("B failure is recorded at the pipeline gate"),
			Attempt.RejectStage,
			EABTSM73DAG5ARejectStage::Pipeline);
		TestTrue(
			FString::Printf(
				TEXT("B rejection code is explicit: %s"),
				*Attempt.RejectCode),
			Attempt.RejectCode.StartsWith(TEXT("DAG5B")));
	}

	FABTSM73DAG5AResult LegacySearch;
	FABTSM73StructureData LegacyData;
	FString LegacyError;
	const bool bLegacyWouldBuild = Pipeline.BuildWithFeasibilitySearch(
		NoFallbackSearch,
		Fixture.DAGSettings,
		Fixture.LayoutSettings,
		Fixture.BuildingSettings,
		Fixture.FrontierSettings,
		Fixture.PatternSettings,
		Fixture.PlayabilitySettings,
		Fixture.DifficultySettings,
		Fixture.MaterialProfiles,
		FVector::ForwardVector,
		LegacySearch,
		LegacyData,
		LegacyError);
	TestTrue(
		FString::Printf(
		TEXT("The same bounded legacy search would build: %s"),
			*LegacyError),
		bLegacyWouldBuild);
	TestTrue(
		TEXT("Semantic failure did not silently substitute that legacy geometry"),
		NoFallbackSemanticData.Bricks.IsEmpty()
			&& !bImpossibleSemanticBuilt
			&& bLegacyWouldBuild);
	return true;
}

#endif
