// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAG5CandidateSearch.h"

#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73DAGGrammarExpander.h"
#include "Building/ABTSM73DAGTypes.h"
#include "Building/ABTSM73StabilityValidator.h"
#include "Building/ABTSM73StructureData.h"
#include "Misc/Crc.h"

namespace
{
	constexpr uint32 DAG5ACandidateSeedSalt = 0xD735A001u;

	struct FSeedCapacity
	{
		int32 InitialExpressionNodeCount = 0;
		int32 InitialTerminalCount = 0;
	};

	FSeedCapacity GetSeedCapacity(const EABTSM73DAGPreset Preset)
	{
		switch (Preset)
		{
		case EABTSM73DAGPreset::SingleTower:
			return {5, 4};
		case EABTSM73DAGPreset::Arch:
			return {5, 3};
		case EABTSM73DAGPreset::TwinTowerBridge:
			return {8, 5};
		default:
			return {};
		}
	}

	FString RejectCodeFromReason(const FString& Reason)
	{
		FString Code;
		FString Remainder;
		return Reason.Split(TEXT(":"), &Code, &Remainder)
			? Code
			: Reason;
	}

	void GatherAssociativeChildren(
		const int32 NodeId,
		const EABTSM73DAGOperator Operator,
		const EABTSM73DAGParallelPolicy ParallelPolicy,
		const FABTSM73DAGGenerationResult& Graph,
		TArray<int32>& OutChildren)
	{
		if (!Graph.ExpressionNodes.IsValidIndex(NodeId))
		{
			return;
		}
		const FABTSM73DAGExpressionNode& Node = Graph.ExpressionNodes[NodeId];
		const bool bSameOperator = Node.Operator == Operator
			&& (Operator != EABTSM73DAGOperator::Parallel
				|| Node.ParallelPolicy == ParallelPolicy);
		if (!bSameOperator)
		{
			OutChildren.Add(NodeId);
			return;
		}
		for (const int32 ChildId : Node.ChildNodeIds)
		{
			if (!Graph.ExpressionNodes.IsValidIndex(ChildId))
			{
				continue;
			}
			const FABTSM73DAGExpressionNode& Child =
				Graph.ExpressionNodes[ChildId];
			const bool bFlattenChild = Child.Operator == Operator
				&& (Operator != EABTSM73DAGOperator::Parallel
					|| Child.ParallelPolicy == ParallelPolicy);
			if (bFlattenChild)
			{
				GatherAssociativeChildren(
					ChildId,
					Operator,
					ParallelPolicy,
					Graph,
					OutChildren);
			}
			else
			{
				OutChildren.Add(ChildId);
			}
		}
	}

	bool CheckScopeCapacityRecursive(
		const int32 ExpressionNodeId,
		const FVector& ScopeSize,
		const FABTSM73DAGGenerationResult& Graph,
		const FABTSM73DAGLayoutSettings& Settings,
		FString& OutError)
	{
		if (!Graph.ExpressionNodes.IsValidIndex(ExpressionNodeId))
		{
			OutError = TEXT("DAG5AScopeExpressionInvalid");
			return false;
		}
		const FABTSM73DAGExpressionNode& Expression =
			Graph.ExpressionNodes[ExpressionNodeId];
		const float RequiredPlateExtent = Settings.bEnableAdaptiveGeometry
			? Settings.MinAdaptivePlateExtentCM
			: Settings.MinPlateExtentCM;
		if (Expression.Operator == EABTSM73DAGOperator::Atom)
		{
			if (ScopeSize.X < RequiredPlateExtent
				|| ScopeSize.Y < RequiredPlateExtent)
			{
				OutError = FString::Printf(
					TEXT("DAG5AScopeTooSmall:%.1f:%.1f:Required=%.1f"),
					ScopeSize.X,
					ScopeSize.Y,
					RequiredPlateExtent);
				return false;
			}
			return true;
		}
		TArray<int32> Children;
		GatherAssociativeChildren(
			ExpressionNodeId,
			Expression.Operator,
			Expression.ParallelPolicy,
			Graph,
			Children);
		if (Children.Num() < 2)
		{
			OutError = TEXT("DAG5AScopeOperatorArity");
			return false;
		}
		if (Expression.Operator == EABTSM73DAGOperator::Series)
		{
			for (const int32 ChildId : Children)
			{
				if (!CheckScopeCapacityRecursive(
					ChildId,
					ScopeSize,
					Graph,
					Settings,
					OutError))
				{
					return false;
				}
			}
			return true;
		}

		const bool bSplitX = !Settings.bAlternateParallelAxes
			|| (Expression.ExpansionDepth % 2 == 0);
		const float AxisLength = bSplitX ? ScopeSize.X : ScopeSize.Y;
		const float AvailableLength = AxisLength
			- Settings.ParallelGapCM * static_cast<float>(Children.Num() - 1);
		const float ChildLength =
			AvailableLength / static_cast<float>(Children.Num());
		if (ChildLength < RequiredPlateExtent)
		{
			OutError = FString::Printf(
				TEXT("DAG5AParallelScopeTooNarrow:Axis=%c:Children=%d:Child=%.1f:Required=%.1f"),
				bSplitX ? TEXT('X') : TEXT('Y'),
				Children.Num(),
				ChildLength,
				RequiredPlateExtent);
			return false;
		}
		FVector ChildScopeSize = ScopeSize;
		if (bSplitX)
		{
			ChildScopeSize.X = ChildLength;
		}
		else
		{
			ChildScopeSize.Y = ChildLength;
		}
		for (const int32 ChildId : Children)
		{
			if (!CheckScopeCapacityRecursive(
				ChildId,
				ChildScopeSize,
				Graph,
				Settings,
				OutError))
			{
				return false;
			}
		}
		return true;
	}

	bool CheckGlobalCapacity(
		const FABTSM73DAG5ASettings& SearchSettings,
		const FABTSM73DAGGenerationSettings& DAGSettings,
		const FABTSM73DAGLayoutSettings& LayoutSettings,
		const FABTSM73GenerationSettings& BuildingSettings,
		const bool bRunLegacyDAGPreflight,
		FABTSM73DAG5AResult& OutResult,
		FString& OutError)
	{
		const bool bFiniteGeometryInputs =
			(!bRunLegacyDAGPreflight
				|| (FMath::IsFinite(DAGSettings.SeriesRuleWeight)
					&& FMath::IsFinite(
						DAGSettings.ParallelRuleWeight)))
			&& FMath::IsFinite(LayoutSettings.TargetWidthCM)
			&& FMath::IsFinite(LayoutSettings.TargetDepthCM)
			&& FMath::IsFinite(LayoutSettings.TargetHeightCM)
			&& FMath::IsFinite(LayoutSettings.ParallelGapCM)
			&& FMath::IsFinite(LayoutSettings.SeriesGapCM)
			&& FMath::IsFinite(LayoutSettings.PlateThicknessCM)
			&& FMath::IsFinite(LayoutSettings.MinPlateExtentCM)
			&& FMath::IsFinite(LayoutSettings.PlateFootprintRatio)
			&& FMath::IsFinite(LayoutSettings.MinAdaptivePlateExtentCM)
			&& FMath::IsFinite(
				LayoutSettings.MinAdaptivePlateThicknessCM)
			&& FMath::IsFinite(LayoutSettings.ColumnWidthCM)
			&& FMath::IsFinite(LayoutSettings.MinColumnHeightCM)
			&& FMath::IsFinite(LayoutSettings.ColumnClearanceCM)
			&& FMath::IsFinite(LayoutSettings.MinAdaptiveColumnWidthCM)
			&& FMath::IsFinite(LayoutSettings.MaxAdaptiveColumnWidthCM)
			&& FMath::IsFinite(LayoutSettings.ContactToleranceCM)
			&& FMath::IsFinite(
				LayoutSettings.MinSupportContactAreaRatio);
		if (!bFiniteGeometryInputs)
		{
			OutError = TEXT("DAG5ANonFiniteGeometryInput");
			return false;
		}
		if (SearchSettings.SearchVersion < 1
			|| SearchSettings.SearchVersion > 64
			|| SearchSettings.MaxCandidateAttempts < 1
			|| SearchSettings.MaxCandidateAttempts > 64
			|| SearchSettings.MaxCompiledBrickCount < 0
			|| SearchSettings.MaxCompiledBrickCount > 256)
		{
			OutError = TEXT("DAG5ASettingsInvalid");
			return false;
		}
		if (DAGSettings.GeneratorVersion < 1
			|| DAGSettings.MaxEstimatedBrickCount < 1
			|| DAGSettings.MaxEstimatedBrickCount > 256
			|| DAGSettings.ReservedWeaknessBrickCount < 0
			|| DAGSettings.ReservedWeaknessBrickCount > 64
			|| (bRunLegacyDAGPreflight
				&& (DAGSettings.MinExpansionDepth < 0
					|| DAGSettings.MaxExpansionDepth
						< DAGSettings.MinExpansionDepth
					|| DAGSettings.MaxExpansionDepth > 6
					|| DAGSettings.ExpansionStepBudget < 0
					|| DAGSettings.ExpansionStepBudget > 32
					|| DAGSettings.MaxAbstractNodeCount < 1
					|| DAGSettings.MaxAbstractNodeCount > 256
					|| DAGSettings.SeriesRuleWeight < 0.0f
					|| DAGSettings.SeriesRuleWeight > 1.0f
					|| DAGSettings.ParallelRuleWeight < 0.0f
					|| DAGSettings.ParallelRuleWeight > 1.0f))
			|| BuildingSettings.MaxBrickCount < 1
			|| BuildingSettings.MaxBrickCount > 100)
		{
			OutError = TEXT("DAG5AOperationBudgetInvalid");
			return false;
		}
		OutResult.EffectiveCompiledBrickLimit =
			SearchSettings.MaxCompiledBrickCount > 0
			? FMath::Min(
				BuildingSettings.MaxBrickCount,
				SearchSettings.MaxCompiledBrickCount)
			: BuildingSettings.MaxBrickCount;
		if (OutResult.EffectiveCompiledBrickLimit < 1)
		{
			OutError = TEXT("DAG5ACompiledBrickLimitInvalid");
			return false;
		}
		if (!SearchSettings.bEnableCapacityPreflight)
		{
			return true;
		}
		if (!bRunLegacyDAGPreflight)
		{
			if (LayoutSettings.TargetWidthCM
					< LayoutSettings.MinAdaptivePlateExtentCM * 2.0f
				|| LayoutSettings.TargetDepthCM
					< LayoutSettings.MinAdaptivePlateExtentCM
				|| LayoutSettings.TargetHeightCM
					<= LayoutSettings.PlateThicknessCM
				|| LayoutSettings.PreferredLogicalSupportsPerLoad < 1
				|| LayoutSettings.MaxLogicalSupportsPerLoad < 1)
			{
				OutError = TEXT("DAG5BSemanticCapacityInvalid");
				return false;
			}
			return true;
		}

		const FSeedCapacity SeedCapacity = GetSeedCapacity(DAGSettings.Preset);
		if (SeedCapacity.InitialTerminalCount < 1)
		{
			OutError = TEXT("DAG5APresetInvalid");
			return false;
		}
		const int32 ExpansionMultiplier = 1 << DAGSettings.MinExpansionDepth;
		OutResult.RequiredMinimumExpansionSteps =
			SeedCapacity.InitialTerminalCount * (ExpansionMultiplier - 1);
		OutResult.RequiredMinimumTerminalCount =
			SeedCapacity.InitialTerminalCount * ExpansionMultiplier;
		const int32 RequiredExpressionNodes =
			SeedCapacity.InitialExpressionNodeCount
			+ OutResult.RequiredMinimumExpansionSteps * 2;
		if (OutResult.RequiredMinimumExpansionSteps
			> DAGSettings.ExpansionStepBudget)
		{
			OutError = FString::Printf(
				TEXT("DAG5AMinimumExpansionStepCapacityExceeded:Required=%d:Budget=%d"),
				OutResult.RequiredMinimumExpansionSteps,
				DAGSettings.ExpansionStepBudget);
			return false;
		}
		if (RequiredExpressionNodes > DAGSettings.MaxAbstractNodeCount)
		{
			OutError = FString::Printf(
				TEXT("DAG5AMinimumExpressionCapacityExceeded:Required=%d:Budget=%d"),
				RequiredExpressionNodes,
				DAGSettings.MaxAbstractNodeCount);
			return false;
		}
		if (OutResult.RequiredMinimumTerminalCount
				+ DAGSettings.ReservedWeaknessBrickCount
			> DAGSettings.MaxEstimatedBrickCount)
		{
			OutError = FString::Printf(
				TEXT("DAG5AMinimumEstimatedBrickCapacityExceeded:Required=%d:Budget=%d"),
				OutResult.RequiredMinimumTerminalCount
					+ DAGSettings.ReservedWeaknessBrickCount,
				DAGSettings.MaxEstimatedBrickCount);
			return false;
		}
		if (OutResult.RequiredMinimumTerminalCount
			> OutResult.EffectiveCompiledBrickLimit)
		{
			OutError = FString::Printf(
				TEXT("DAG5AMinimumCompiledBrickCapacityExceeded:Required=%d:Budget=%d"),
				OutResult.RequiredMinimumTerminalCount,
				OutResult.EffectiveCompiledBrickLimit);
			return false;
		}
		if (LayoutSettings.TargetWidthCM < LayoutSettings.MinPlateExtentCM
			|| LayoutSettings.TargetDepthCM < LayoutSettings.MinPlateExtentCM
			|| LayoutSettings.TargetHeightCM <= LayoutSettings.PlateThicknessCM
			|| LayoutSettings.PlateFootprintRatio <= 0.0f
			|| LayoutSettings.PlateFootprintRatio > 1.0f
			|| LayoutSettings.PreferredLogicalSupportsPerLoad < 1
			|| LayoutSettings.MaxLogicalSupportsPerLoad < 1)
		{
			OutError = TEXT("DAG5ALayoutCapacityInvalid");
			return false;
		}
		return true;
	}

	int32 MakeCandidateSeed(
		const FABTSM73DAG5ASettings& SearchSettings,
		const FABTSM73DAGGenerationSettings& BaseSettings,
		const int32 AttemptIndex)
	{
		if (AttemptIndex == 0)
		{
			return BaseSettings.BuildingSeed;
		}
		uint32 Hash = HashCombineFast(
			GetTypeHash(BaseSettings.BuildingSeed),
			GetTypeHash(BaseSettings.GeneratorVersion));
		Hash = HashCombineFast(
			Hash,
			GetTypeHash(static_cast<uint8>(BaseSettings.Preset)));
		Hash = HashCombineFast(Hash, GetTypeHash(SearchSettings.SearchVersion));
		Hash = HashCombineFast(Hash, GetTypeHash(AttemptIndex));
		Hash = HashCombineFast(Hash, DAG5ACandidateSeedSalt);
		const int32 CandidateSeed =
			static_cast<int32>(Hash & 0x7fffffffu);
		return CandidateSeed > 0 ? CandidateSeed : AttemptIndex;
	}

	bool IsFatalCandidateError(const FString& RejectCode)
	{
		return RejectCode == TEXT("GeneratorVersionInvalid")
			|| RejectCode == TEXT("ExpansionDepthRangeInvalid")
			|| RejectCode == TEXT("GenerationBudgetInvalid")
			|| RejectCode == TEXT("RuleWeightNegative")
			|| RejectCode == TEXT("NoEnabledExpansionRule")
			|| RejectCode == TEXT("DAGLayoutSettingsInvalid")
			|| RejectCode == TEXT("DAG5BSettingsInvalid")
			|| RejectCode == TEXT("DAG5BShapeFamilyInvalid")
			|| RejectCode == TEXT("DAG3CRequiresAnalysisRewriteAndGeneralizedCut")
			|| RejectCode == TEXT("DAG3CAttackDirectionInvalid")
			|| RejectCode == TEXT("DAG3BRewriteAttemptBudgetInvalid");
	}

	void FinalizeSearchHash(
		const FABTSM73DAG5ASettings& SearchSettings,
		const FABTSM73DAGGenerationSettings& DAGSettings,
		const FABTSM73DAGLayoutSettings& LayoutSettings,
		const FABTSM73GenerationSettings& BuildingSettings,
		const FABTSM73StructureData& Data,
		FABTSM73DAG5AResult& OutResult)
	{
		FString Canonical = FString::Printf(
			TEXT("V=%d|Attempts=%d|PreflightSetting=%d|SearchLimit=%d")
			TEXT("|Input=%d|Generator=%d|Preset=%d|MinDepth=%d|MaxDepth=%d|Steps=%d|Nodes=%d|Estimated=%d|Reserved=%d|Series=%.6f|Parallel=%.6f|Policy=%d")
			TEXT("|Width=%.3f|Depth=%.3f|Height=%.3f|ParallelGap=%.3f|Alternate=%d|MinPlate=%.3f|Adaptive=%d|MinAdaptive=%.3f|Pattern=%d|Preferred=%d|MaxSupports=%d|Contact=%.6f")
			TEXT("|BuildingLimit=%d|Material=%d|Limit=%d|PreflightPassed=%d|Accepted=%d|Reject=%s"),
			SearchSettings.SearchVersion,
			SearchSettings.MaxCandidateAttempts,
			SearchSettings.bEnableCapacityPreflight ? 1 : 0,
			SearchSettings.MaxCompiledBrickCount,
			DAGSettings.BuildingSeed,
			DAGSettings.GeneratorVersion,
			static_cast<int32>(DAGSettings.Preset),
			DAGSettings.MinExpansionDepth,
			DAGSettings.MaxExpansionDepth,
			DAGSettings.ExpansionStepBudget,
			DAGSettings.MaxAbstractNodeCount,
			DAGSettings.MaxEstimatedBrickCount,
			DAGSettings.ReservedWeaknessBrickCount,
			DAGSettings.SeriesRuleWeight,
			DAGSettings.ParallelRuleWeight,
			static_cast<int32>(DAGSettings.DefaultParallelPolicy),
			LayoutSettings.TargetWidthCM,
			LayoutSettings.TargetDepthCM,
			LayoutSettings.TargetHeightCM,
			LayoutSettings.ParallelGapCM,
			LayoutSettings.bAlternateParallelAxes ? 1 : 0,
			LayoutSettings.MinPlateExtentCM,
			LayoutSettings.bEnableAdaptiveGeometry ? 1 : 0,
			LayoutSettings.MinAdaptivePlateExtentCM,
			static_cast<int32>(LayoutSettings.SupportPattern),
			LayoutSettings.PreferredLogicalSupportsPerLoad,
			LayoutSettings.MaxLogicalSupportsPerLoad,
			LayoutSettings.MinSupportContactAreaRatio,
			BuildingSettings.MaxBrickCount,
			static_cast<int32>(BuildingSettings.PrimaryMaterial),
			OutResult.EffectiveCompiledBrickLimit,
			OutResult.bCapacityPreflightPassed ? 1 : 0,
			OutResult.bAccepted ? 1 : 0,
			*OutResult.RejectReason);
		for (const FABTSM73DAG5AAttemptResult& Attempt : OutResult.Attempts)
		{
			Canonical += FString::Printf(
				TEXT("|A=%d,S=%d,T=%lld,B=%d,OK=%d,Stage=%d,Code=%s,Reason=%s"),
				Attempt.AttemptIndex,
				Attempt.CandidateSeed,
				Attempt.TopologyHash,
				Attempt.CompiledBrickCount,
				Attempt.bAccepted ? 1 : 0,
				static_cast<int32>(Attempt.RejectStage),
				*Attempt.RejectCode,
				*Attempt.RejectReason);
		}
		for (const FABTSM73BrickNode& Brick : Data.Bricks)
		{
			Canonical += FString::Printf(
				TEXT("|Brick=%d,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f"),
				Brick.NodeId,
				Brick.MacroNodeId,
				static_cast<int32>(Brick.Material),
				static_cast<int32>(Brick.SemanticRole),
				Brick.LocalCenter.X,
				Brick.LocalCenter.Y,
				Brick.LocalCenter.Z,
				Brick.DimensionsCM.X,
				Brick.DimensionsCM.Y,
				Brick.DimensionsCM.Z);
		}
		for (const FABTSM73SupportEdge& Edge : Data.SupportEdges)
		{
			Canonical += FString::Printf(
				TEXT("|Edge=%d,%d,%.3f"),
				Edge.LowerNodeId,
				Edge.UpperNodeId,
				Edge.ContactAreaCM2);
		}
		OutResult.SearchHash = static_cast<int64>(
			FCrc::StrCrc32(*Canonical));
	}

	void RejectAttempt(
		FABTSM73DAG5AAttemptResult& Attempt,
		const EABTSM73DAG5ARejectStage Stage,
		const FString& Reason)
	{
		Attempt.bAccepted = false;
		Attempt.RejectStage = Stage;
		Attempt.RejectReason = Reason;
		Attempt.RejectCode = RejectCodeFromReason(Reason);
	}
}

bool FABTSM73DAG5CandidateSearch::Build(
	const FABTSM73DAG5ASettings& SearchSettings,
	const FABTSM73DAGGenerationSettings& BaseDAGSettings,
	const FABTSM73DAGLayoutSettings& LayoutSettings,
	const FABTSM73GenerationSettings& BuildingSettings,
	const FCandidateBuilder CandidateBuilder,
	FABTSM73DAGGenerationSettings& OutSelectedDAGSettings,
	FABTSM73StructureData& OutData,
	FABTSM73DAG5AResult& OutResult,
	FString& OutError,
	const bool bRunLegacyDAGPreflight) const
{
	OutSelectedDAGSettings = BaseDAGSettings;
	OutData = FABTSM73StructureData();
	OutResult = FABTSM73DAG5AResult();
	OutResult.bEnabled = SearchSettings.bEnableFeasibilitySearch;
	OutResult.InputSeed = BaseDAGSettings.BuildingSeed;
	OutError.Reset();
	if (!SearchSettings.bEnableFeasibilitySearch)
	{
		OutError = TEXT("DAG5ASearchNotEnabled");
		OutResult.RejectReason = OutError;
		FinalizeSearchHash(
			SearchSettings,
			BaseDAGSettings,
			LayoutSettings,
			BuildingSettings,
			OutData,
			OutResult);
		return false;
	}

	if (!CheckGlobalCapacity(
		SearchSettings,
		BaseDAGSettings,
		LayoutSettings,
		BuildingSettings,
		bRunLegacyDAGPreflight,
		OutResult,
		OutError))
	{
		OutResult.RejectReason = FString::Printf(
			TEXT("DAG5APreflightRejected:%s"),
			*OutError);
		OutError = OutResult.RejectReason;
		FinalizeSearchHash(
			SearchSettings,
			BaseDAGSettings,
			LayoutSettings,
			BuildingSettings,
			OutData,
			OutResult);
		return false;
	}
	OutResult.bCapacityPreflightPassed =
		SearchSettings.bEnableCapacityPreflight;

	FString LastRejectCode = TEXT("DAG5ANoCandidateAttempted");
	for (int32 AttemptIndex = 0;
		AttemptIndex < SearchSettings.MaxCandidateAttempts;
		++AttemptIndex)
	{
		FABTSM73DAGGenerationSettings CandidateSettings =
			BaseDAGSettings;
		CandidateSettings.BuildingSeed = MakeCandidateSeed(
			SearchSettings,
			BaseDAGSettings,
			AttemptIndex);
		FABTSM73DAG5AAttemptResult& Attempt =
			OutResult.Attempts.AddDefaulted_GetRef();
		Attempt.AttemptIndex = AttemptIndex;
		Attempt.CandidateSeed = CandidateSettings.BuildingSeed;
		++OutResult.AttemptCount;

		FString CandidateError;
		if (bRunLegacyDAGPreflight)
		{
			FABTSM73DAGGrammarExpander Expander;
			FABTSM73DAGGenerationResult Graph;
			if (!Expander.Generate(
				CandidateSettings,
				Graph,
				CandidateError))
			{
				RejectAttempt(
					Attempt,
					EABTSM73DAG5ARejectStage::Grammar,
					CandidateError);
				LastRejectCode = Attempt.RejectCode;
				if (IsFatalCandidateError(Attempt.RejectCode))
				{
					break;
				}
				continue;
			}
			Attempt.TopologyHash = static_cast<int64>(
				Graph.CanonicalTopologyHash);
			if (SearchSettings.bEnableCapacityPreflight
				&& !CheckScopeCapacityRecursive(
					Graph.RootExpressionNodeId,
					FVector(
						LayoutSettings.TargetWidthCM,
						LayoutSettings.TargetDepthCM,
						LayoutSettings.TargetHeightCM),
					Graph,
					LayoutSettings,
					CandidateError))
			{
				RejectAttempt(
					Attempt,
					EABTSM73DAG5ARejectStage::ScopeCapacity,
					CandidateError);
				++OutResult.ScopePreflightRejectCount;
				LastRejectCode = Attempt.RejectCode;
				continue;
			}
		}

		FABTSM73StructureData CandidateData;
		if (!CandidateBuilder(
			CandidateSettings,
			CandidateData,
			CandidateError))
		{
			RejectAttempt(
				Attempt,
				EABTSM73DAG5ARejectStage::Pipeline,
				CandidateError);
			LastRejectCode = Attempt.RejectCode;
			if (IsFatalCandidateError(Attempt.RejectCode))
			{
				break;
			}
			continue;
		}
		++OutResult.CompiledCandidateCount;
		if (!bRunLegacyDAGPreflight)
		{
			Attempt.TopologyHash = static_cast<int64>(
				CandidateData.DAGTopologyHash);
		}
		Attempt.CompiledBrickCount = CandidateData.Bricks.Num();
		if (Attempt.CompiledBrickCount
			> OutResult.EffectiveCompiledBrickLimit)
		{
			CandidateError = FString::Printf(
				TEXT("DAG5ABrickBudgetExceeded:Actual=%d:Limit=%d"),
				Attempt.CompiledBrickCount,
				OutResult.EffectiveCompiledBrickLimit);
			RejectAttempt(
				Attempt,
				EABTSM73DAG5ARejectStage::CompiledBrickBudget,
				CandidateError);
			LastRejectCode = Attempt.RejectCode;
			continue;
		}

		FABTSM73StabilityValidator StabilityValidator;
		if (!StabilityValidator.Validate(
			BuildingSettings,
			CandidateData,
			CandidateError))
		{
			CandidateError = FString::Printf(
				TEXT("DAG5AStaticStabilityRejected:%s"),
				*CandidateError);
			RejectAttempt(
				Attempt,
				EABTSM73DAG5ARejectStage::StaticStability,
				CandidateError);
			LastRejectCode = Attempt.RejectCode;
			continue;
		}

		Attempt.bAccepted = true;
		Attempt.RejectStage = EABTSM73DAG5ARejectStage::None;
		Attempt.RejectCode.Reset();
		Attempt.RejectReason.Reset();
		OutResult.bAccepted = true;
		OutResult.SelectedAttemptIndex = AttemptIndex;
		OutResult.SelectedCandidateSeed =
			CandidateSettings.BuildingSeed;
		OutResult.CompiledBrickCount =
			CandidateData.Bricks.Num();
		OutSelectedDAGSettings = CandidateSettings;
		OutData = MoveTemp(CandidateData);
		FinalizeSearchHash(
			SearchSettings,
			BaseDAGSettings,
			LayoutSettings,
			BuildingSettings,
			OutData,
			OutResult);
		return true;
	}

	OutResult.RejectReason = FString::Printf(
		TEXT("DAG5ANoFeasibleCandidate:Attempts=%d:Last=%s"),
		OutResult.AttemptCount,
		*LastRejectCode);
	OutError = OutResult.RejectReason;
	OutData = FABTSM73StructureData();
	FinalizeSearchHash(
		SearchSettings,
		BaseDAGSettings,
		LayoutSettings,
		BuildingSettings,
		OutData,
		OutResult);
	return false;
}
