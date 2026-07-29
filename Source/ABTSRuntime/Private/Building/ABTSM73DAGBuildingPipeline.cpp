// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAGBuildingPipeline.h"

#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73DAGFailureFrontierAnalyzer.h"
#include "Building/ABTSM73DAGFailureFrontierTypes.h"
#include "Building/ABTSM73DAGFailurePatternRewriter.h"
#include "Building/ABTSM73DAGGrammarExpander.h"
#include "Building/ABTSM73DAGLayoutSolver.h"
#include "Building/ABTSM73DAGModuleCompiler.h"
#include "Building/ABTSM73DAGTypes.h"
#include "Building/ABTSM73StabilityValidator.h"
#include "Building/ABTSM73StructureData.h"

bool FABTSM73DAGBuildingPipeline::Build(
	const FABTSM73DAGGenerationSettings& DAGSettings,
	const FABTSM73DAGLayoutSettings& LayoutSettings,
	const FABTSM73GenerationSettings& BuildingSettings,
	FABTSM73StructureData& OutData,
	FString& OutError) const
{
	FABTSM73DAGGrammarExpander Expander;
	FABTSM73DAGGenerationResult Graph;
	if (!Expander.Generate(DAGSettings, Graph, OutError)) return false;
	FABTSM73DAGLayoutSolver LayoutSolver;
	FABTSM73DAGSpatialLayout Layout;
	if (!LayoutSolver.Solve(Graph, LayoutSettings, Layout, OutError)) return false;
	FABTSM73DAGModuleCompiler Compiler;
	return Compiler.Compile(BuildingSettings, Graph, LayoutSettings, Layout, OutData, OutError);
}

bool FABTSM73DAGBuildingPipeline::BuildWithFailurePattern(
	const FABTSM73DAGGenerationSettings& DAGSettings,
	const FABTSM73DAGLayoutSettings& LayoutSettings,
	const FABTSM73GenerationSettings& BuildingSettings,
	const FABTSM73DAGFailureFrontierSettings& FrontierSettings,
	const FABTSM73DAGFailurePatternSettings& PatternSettings,
	const FABTSM73DifficultySettings& DifficultySettings,
	const TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles,
	FABTSM73StructureData& OutData,
	FString& OutError) const
{
	OutData = FABTSM73StructureData();
	OutError.Reset();
	FABTSM73DAGGrammarExpander Expander;
	FABTSM73DAGGenerationResult Graph;
	if (!Expander.Generate(DAGSettings, Graph, OutError)) return false;

	FABTSM73DAGLayoutSolver LayoutSolver;
	FABTSM73DAGSpatialLayout BaselineLayout;
	if (!LayoutSolver.Solve(
		Graph,
		LayoutSettings,
		BaselineLayout,
		OutError))
	{
		return false;
	}
	FABTSM73DAGModuleCompiler Compiler;
	FABTSM73StructureData BaselineData;
	if (!Compiler.Compile(
		BuildingSettings,
		Graph,
		LayoutSettings,
		BaselineLayout,
		BaselineData,
		OutError))
	{
		return false;
	}

	BaselineData.DAGFailurePatternResult.bEnabled =
		PatternSettings.bEnableGeometryRewrite;
	BaselineData.DAGFailurePatternResult.Pattern = PatternSettings.Pattern;
	FABTSM73DAGFailureFrontierAnalyzer FrontierAnalyzer;
	if (!FrontierAnalyzer.Analyze(
		FrontierSettings,
		MaterialProfiles,
		BaselineData,
		BaselineData.DAGFailureFrontierAnalysis,
		OutError))
	{
		if (PatternSettings.bEnableGeometryRewrite)
		{
			BaselineData.DAGFailurePatternResult.RejectReason = OutError;
		}
		OutData = MoveTemp(BaselineData);
		return false;
	}
	if (!PatternSettings.bEnableGeometryRewrite)
	{
		OutData = MoveTemp(BaselineData);
		return true;
	}
	if (!FrontierSettings.bEnableAnalysis
		|| !BaselineData.DAGFailureFrontierAnalysis.bAccepted)
	{
		OutError = TEXT("DAG3BRequiresAcceptedFrontier");
		BaselineData.DAGFailurePatternResult.RejectReason = OutError;
		OutData = MoveTemp(BaselineData);
		return false;
	}
	if (PatternSettings.MaxRewriteAttemptCount < 1)
	{
		OutError = TEXT("DAG3BRewriteAttemptBudgetInvalid");
		BaselineData.DAGFailurePatternResult.RejectReason = OutError;
		OutData = MoveTemp(BaselineData);
		return false;
	}

	FABTSM73DAGFailurePatternRewriter Rewriter;
	FABTSM73StabilityValidator StabilityValidator;
	int32 AttemptCount = 0;
	FString LastReject = TEXT("DAG3BNoTransactionAttempted");
	for (const FABTSM73DAGFailureFrontierCandidate& SourceFrontier
		: BaselineData.DAGFailureFrontierAnalysis.Candidates)
	{
		if (!SourceFrontier.bAccepted) continue;
		TArray<EABTSM73DAGFailurePattern> Patterns;
		if (PatternSettings.Pattern != EABTSM73DAGFailurePattern::Auto)
		{
			Patterns.Add(PatternSettings.Pattern);
		}
		else
		{
			Patterns = {
				EABTSM73DAGFailurePattern::InternalSingleSupport,
				EABTSM73DAGFailurePattern::InternalAsymmetricDualSupport,
				EABTSM73DAGFailurePattern::InternalOffsetSeam
			};
			const uint32 RotationSeed =
				static_cast<uint32>(DAGSettings.BuildingSeed)
				^ SourceFrontier.FrontierHash;
			const int32 Rotation = static_cast<int32>(
				RotationSeed % static_cast<uint32>(Patterns.Num()));
			TArray<EABTSM73DAGFailurePattern> RotatedPatterns;
			RotatedPatterns.Reserve(Patterns.Num());
			for (int32 Index = 0; Index < Patterns.Num(); ++Index)
			{
				RotatedPatterns.Add(Patterns[(Index + Rotation) % Patterns.Num()]);
			}
			Patterns = MoveTemp(RotatedPatterns);
		}

		for (const EABTSM73DAGFailurePattern Pattern : Patterns)
		{
			if (AttemptCount >= PatternSettings.MaxRewriteAttemptCount)
			{
				LastReject = FString::Printf(
					TEXT("DAG3BRewriteAttemptBudgetExceeded:%d:%d"),
					AttemptCount,
					PatternSettings.MaxRewriteAttemptCount);
				break;
			}
			++AttemptCount;
			FABTSM73DAGFailureRewriteIntent Intent;
			FString TransactionError;
			if (!Rewriter.MakeIntent(
				PatternSettings,
				DifficultySettings,
				SourceFrontier,
				Graph,
				BaselineLayout,
				BaselineData,
				Pattern,
				Intent,
				TransactionError))
			{
				LastReject = MoveTemp(TransactionError);
				continue;
			}
			FABTSM73DAGSpatialLayout TrialLayout;
			if (!LayoutSolver.Solve(
				Graph,
				LayoutSettings,
				TrialLayout,
				TransactionError,
				&Intent))
			{
				LastReject = MoveTemp(TransactionError);
				continue;
			}
			FABTSM73StructureData TrialData;
			if (!Compiler.Compile(
				BuildingSettings,
				Graph,
				LayoutSettings,
				TrialLayout,
				TrialData,
				TransactionError))
			{
				LastReject = MoveTemp(TransactionError);
				continue;
			}
			if (!StabilityValidator.Validate(
				BuildingSettings,
				TrialData,
				TransactionError))
			{
				LastReject = FString::Printf(
					TEXT("DAG3BIntactStabilityRejected:%s"),
					*TransactionError);
				continue;
			}
			FABTSM73DAGFailureFrontierAnalysis RealizedAnalysis;
			if (!FrontierAnalyzer.Analyze(
				FrontierSettings,
				MaterialProfiles,
				TrialData,
				RealizedAnalysis,
				TransactionError))
			{
				LastReject = MoveTemp(TransactionError);
				continue;
			}
			FABTSM73DAGFailurePatternResult PatternResult;
			if (!Rewriter.ValidateRealizedPattern(
				Intent,
				PatternSettings,
				BuildingSettings,
				DifficultySettings,
				MaterialProfiles,
				TrialData,
				PatternResult,
				TransactionError))
			{
				LastReject = MoveTemp(TransactionError);
				continue;
			}
			TArray<int32> RealizedFrontierNodeIds =
				PatternResult.WeakNodeIds;
			RealizedFrontierNodeIds.Append(
				PatternResult.RemainingSupportNodeIds);
			RealizedFrontierNodeIds.Sort();
			int32 BoundCandidateIndex = INDEX_NONE;
			for (int32 CandidateIndex = 0;
				CandidateIndex < RealizedAnalysis.Candidates.Num();
				++CandidateIndex)
			{
				const FABTSM73DAGFailureFrontierCandidate& Candidate =
					RealizedAnalysis.Candidates[CandidateIndex];
				if (Candidate.bAccepted
					&& Candidate.Kind
						== EABTSM73DAGFailureCandidateKind::SupportInterfaceCutSet
					&& Candidate.CandidateNodeIds == RealizedFrontierNodeIds
					&& Candidate.ProtectedRootNodeIds.Contains(
						PatternResult.LoadPlateNodeId))
				{
					BoundCandidateIndex = CandidateIndex;
					break;
				}
			}
			if (BoundCandidateIndex == INDEX_NONE)
			{
				LastReject = TEXT("DAG3BRealizedFrontierNotAccepted");
				continue;
			}
			RealizedAnalysis.SelectedCandidateIndex = BoundCandidateIndex;
			RealizedAnalysis.SelectedFrontierHash =
				RealizedAnalysis.Candidates[BoundCandidateIndex].FrontierHash;
			RealizedAnalysis.bAccepted = true;
			PatternResult.RewriteAttemptCount = AttemptCount;
			TrialData.DAGFailureFrontierAnalysis = MoveTemp(RealizedAnalysis);
			TrialData.DAGFailurePatternResult = MoveTemp(PatternResult);
			OutData = MoveTemp(TrialData);
			return true;
		}
		if (AttemptCount >= PatternSettings.MaxRewriteAttemptCount) break;
	}

	BaselineData.DAGFailurePatternResult.bEnabled = true;
	BaselineData.DAGFailurePatternResult.Pattern = PatternSettings.Pattern;
	BaselineData.DAGFailurePatternResult.RewriteAttemptCount = AttemptCount;
	BaselineData.DAGFailurePatternResult.SourceFrontierHash =
		BaselineData.DAGFailureFrontierAnalysis.SelectedFrontierHash;
	BaselineData.DAGFailurePatternResult.RejectReason = LastReject;
	OutError = FString::Printf(
		TEXT("DAG3BNoAcceptedPattern:%s"),
		*LastReject);
	OutData = MoveTemp(BaselineData);
	return false;
}
