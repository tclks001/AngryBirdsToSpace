// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAGBuildingPipeline.h"

#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73DAGFailureFrontierAnalyzer.h"
#include "Building/ABTSM73DAGFailureFrontierTypes.h"
#include "Building/ABTSM73DAGFailurePatternRewriter.h"
#include "Building/ABTSM73DAGFailurePlayabilityPlanner.h"
#include "Building/ABTSM73DAG5CandidateSearch.h"
#include "Building/ABTSM73DAG5BSemanticEnvelope.h"
#include "Building/ABTSM73DAG5Types.h"
#include "Building/ABTSM73DAGGrammarExpander.h"
#include "Building/ABTSM73DAGLayoutSolver.h"
#include "Building/ABTSM73DAGModuleCompiler.h"
#include "Building/ABTSM73DAGTypes.h"
#include "Building/ABTSM73StabilityValidator.h"
#include "Building/ABTSM73StructureData.h"
#include "Misc/Crc.h"
#include "UObject/Class.h"

namespace
{
	void AppendDAG5AStructIdentity(
		FString& InOutCanonical,
		const TCHAR* Label,
		const UScriptStruct* ScriptStruct,
		const void* Value)
	{
		FString Exported;
		ScriptStruct->ExportText(
			Exported,
			Value,
			nullptr,
			nullptr,
			PPF_None,
			nullptr);
		InOutCanonical += FString::Printf(
			TEXT("|%s=%s"),
			Label,
			*Exported);
	}

	void FinalizeDAG5ACompleteChainHash(
		const FABTSM73DAG5ASettings& SearchSettings,
		const FABTSM73DAGGenerationSettings& DAGSettings,
		const FABTSM73DAGLayoutSettings& LayoutSettings,
		const FABTSM73GenerationSettings& BuildingSettings,
		const FABTSM73DAGFailureFrontierSettings& FrontierSettings,
		const FABTSM73DAGFailurePatternSettings& PatternSettings,
		const FABTSM73DAGFailurePlayabilitySettings& PlayabilitySettings,
		const FABTSM73DifficultySettings& DifficultySettings,
		const TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles,
		const FVector& LocalAttackDirection,
		const FABTSM73StructureData& Data,
		FABTSM73DAG5AResult& InOutResult)
	{
		FString Canonical = FString::Printf(
			TEXT("SchedulerGeometryHash=%lld")
			TEXT("|SelectedFrontierHash=%u")
			TEXT("|RealizedPatternHash=%u")
			TEXT("|PlayabilityHash=%u"),
			InOutResult.SearchHash,
			Data.DAGFailureFrontierAnalysis.SelectedFrontierHash,
			Data.DAGFailurePatternResult.RealizedPatternHash,
			Data.DAGFailurePlayabilityResult.PlayabilityHash);
		AppendDAG5AStructIdentity(
			Canonical,
			TEXT("Search"),
			FABTSM73DAG5ASettings::StaticStruct(),
			&SearchSettings);
		AppendDAG5AStructIdentity(
			Canonical,
			TEXT("DAG"),
			FABTSM73DAGGenerationSettings::StaticStruct(),
			&DAGSettings);
		AppendDAG5AStructIdentity(
			Canonical,
			TEXT("Layout"),
			FABTSM73DAGLayoutSettings::StaticStruct(),
			&LayoutSettings);
		AppendDAG5AStructIdentity(
			Canonical,
			TEXT("Building"),
			FABTSM73GenerationSettings::StaticStruct(),
			&BuildingSettings);
		AppendDAG5AStructIdentity(
			Canonical,
			TEXT("Frontier"),
			FABTSM73DAGFailureFrontierSettings::StaticStruct(),
			&FrontierSettings);
		AppendDAG5AStructIdentity(
			Canonical,
			TEXT("Pattern"),
			FABTSM73DAGFailurePatternSettings::StaticStruct(),
			&PatternSettings);
		AppendDAG5AStructIdentity(
			Canonical,
			TEXT("Playability"),
			FABTSM73DAGFailurePlayabilitySettings::StaticStruct(),
			&PlayabilitySettings);
		AppendDAG5AStructIdentity(
			Canonical,
			TEXT("Difficulty"),
			FABTSM73DifficultySettings::StaticStruct(),
			&DifficultySettings);
		AppendDAG5AStructIdentity(
			Canonical,
			TEXT("Attack"),
			TBaseStructure<FVector>::Get(),
			&LocalAttackDirection);
		for (int32 ProfileIndex = 0;
			ProfileIndex < MaterialProfiles.Num();
			++ProfileIndex)
		{
			const FString Label = FString::Printf(
				TEXT("Material[%d]"),
				ProfileIndex);
			AppendDAG5AStructIdentity(
				Canonical,
				*Label,
				FABTSM7MaterialProfile::StaticStruct(),
				&MaterialProfiles[ProfileIndex]);
		}
		InOutResult.SearchHash = static_cast<int64>(
			FCrc::StrCrc32(*Canonical));
	}

	void FinalizeDAG5BCompleteChainHash(
		const FABTSM73DAG5BSettings& Settings,
		const FABTSM73DAG5BResult& SemanticResult,
		FABTSM73DAG5AResult& InOutSearchResult)
	{
		FString Canonical = FString::Printf(
			TEXT("DAG5A=%lld|Accepted=%d|Family=%d|Features=%u")
			TEXT("|Shape=%u|WFC=%u|Envelope=%u|Audit=%u|Result=%u"),
			InOutSearchResult.SearchHash,
			SemanticResult.bAccepted ? 1 : 0,
			static_cast<int32>(SemanticResult.ShapeFamily),
			static_cast<uint32>(SemanticResult.FeatureMask),
			SemanticResult.ShapeHash,
			SemanticResult.WFCHash,
			SemanticResult.EnvelopeHash,
			SemanticResult.Audit.AuditHash,
			SemanticResult.ResultHash);
		AppendDAG5AStructIdentity(
			Canonical,
			TEXT("Semantic"),
			FABTSM73DAG5BSettings::StaticStruct(),
			&Settings);
		InOutSearchResult.SearchHash = static_cast<int64>(
			FCrc::StrCrc32(*Canonical));
	}
}

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
	return BuildWithFailurePattern(
		DAGSettings,
		LayoutSettings,
		BuildingSettings,
		FrontierSettings,
		PatternSettings,
		FABTSM73DAGFailurePlayabilitySettings(),
		DifficultySettings,
		MaterialProfiles,
		FVector::ForwardVector,
		OutData,
		OutError);
}

bool FABTSM73DAGBuildingPipeline::BuildWithFailurePattern(
	const FABTSM73DAGGenerationSettings& DAGSettings,
	const FABTSM73DAGLayoutSettings& LayoutSettings,
	const FABTSM73GenerationSettings& BuildingSettings,
	const FABTSM73DAGFailureFrontierSettings& FrontierSettings,
	const FABTSM73DAGFailurePatternSettings& PatternSettings,
	const FABTSM73DAGFailurePlayabilitySettings& PlayabilitySettings,
	const FABTSM73DifficultySettings& DifficultySettings,
	const TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles,
	const FVector& LocalAttackDirection,
	FABTSM73StructureData& OutData,
	FString& OutError) const
{
	FABTSM73DAG5BSettings DisabledSemanticSettings;
	FABTSM73DAG5BResult DisabledSemanticResult;
	return BuildWithFailurePattern(
		DisabledSemanticSettings,
		DAGSettings,
		LayoutSettings,
		BuildingSettings,
		FrontierSettings,
		PatternSettings,
		PlayabilitySettings,
		DifficultySettings,
		MaterialProfiles,
		LocalAttackDirection,
		DisabledSemanticResult,
		OutData,
		OutError);
}

bool FABTSM73DAGBuildingPipeline::BuildWithFailurePattern(
	const FABTSM73DAG5BSettings& SemanticSettings,
	const FABTSM73DAGGenerationSettings& DAGSettings,
	const FABTSM73DAGLayoutSettings& LayoutSettings,
	const FABTSM73GenerationSettings& BuildingSettings,
	const FABTSM73DAGFailureFrontierSettings& FrontierSettings,
	const FABTSM73DAGFailurePatternSettings& PatternSettings,
	const FABTSM73DAGFailurePlayabilitySettings& PlayabilitySettings,
	const FABTSM73DifficultySettings& DifficultySettings,
	const TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles,
	const FVector& LocalAttackDirection,
	FABTSM73DAG5BResult& OutSemanticResult,
	FABTSM73StructureData& OutData,
	FString& OutError) const
{
	OutData = FABTSM73StructureData();
	OutSemanticResult = FABTSM73DAG5BResult();
	OutSemanticResult.bEnabled =
		SemanticSettings.bEnableSemanticEnvelope;
	OutError.Reset();
	const bool bSemantic =
		SemanticSettings.bEnableSemanticEnvelope;
	FABTSM73DAGGenerationResult Graph;
	FABTSM73DAGSpatialLayout SemanticInitialLayout;
	FABTSM73SemanticEnvelope RawSemanticEnvelope;
	FABTSM73DAG5BSemanticEnvelopeBuilder SemanticBuilder;
	if (bSemantic)
	{
		if (!SemanticBuilder.Build(
			SemanticSettings,
			DAGSettings,
			LayoutSettings,
			Graph,
			SemanticInitialLayout,
			RawSemanticEnvelope,
			OutSemanticResult,
			OutError))
		{
			return false;
		}
		const int32 EstimatedLimit = FMath::Min(
			DAGSettings.MaxEstimatedBrickCount,
			BuildingSettings.MaxBrickCount);
		if (EstimatedLimit < 1
			|| Graph.EstimatedBrickCount > EstimatedLimit)
		{
			OutError = FString::Printf(
				TEXT("DAG5BEstimatedBrickBudgetExceeded:Estimated=%d:Limit=%d"),
				Graph.EstimatedBrickCount,
				EstimatedLimit);
			OutSemanticResult.RejectReason = OutError;
			return false;
		}
	}
	else
	{
		FABTSM73DAGGrammarExpander Expander;
		if (!Expander.Generate(DAGSettings, Graph, OutError))
		{
			return false;
		}
	}

	FABTSM73DAGLayoutSolver LayoutSolver;
	FABTSM73DAGSpatialLayout BaselineLayout;
	const bool bBaselineLayoutSolved = bSemantic
		? LayoutSolver.SolveSemantic(
			Graph,
			LayoutSettings,
			SemanticInitialLayout,
			RawSemanticEnvelope,
			BaselineLayout,
			OutError)
		: LayoutSolver.Solve(
			Graph,
			LayoutSettings,
			BaselineLayout,
			OutError);
	if (!bBaselineLayoutSolved)
	{
		if (bSemantic)
		{
			OutSemanticResult.RejectReason = OutError;
		}
		return false;
	}
	FABTSM73DAGModuleCompiler Compiler;
	FABTSM73StructureData BaselineData;
	FABTSM73SemanticEnvelope BaselineSemanticEnvelope =
		RawSemanticEnvelope;
	if (bSemantic
		&& !SemanticBuilder.BindPhysicalContract(
			BaselineLayout,
			BaselineSemanticEnvelope,
			OutError))
	{
		OutSemanticResult.RejectReason = OutError;
		return false;
	}
	if (bSemantic)
	{
		OutSemanticResult.FeatureMask =
			BaselineSemanticEnvelope.FeatureMask;
		OutSemanticResult.EnvelopeHash =
			BaselineSemanticEnvelope.EnvelopeHash;
	}
	const bool bBaselineCompiled = bSemantic
		? Compiler.CompileSemantic(
			BuildingSettings,
			Graph,
			LayoutSettings,
			BaselineLayout,
			BaselineSemanticEnvelope,
			OutSemanticResult,
			BaselineData,
			OutError)
		: Compiler.Compile(
			BuildingSettings,
			Graph,
			LayoutSettings,
			BaselineLayout,
			BaselineData,
			OutError);
	if (!bBaselineCompiled) return false;
	if (bSemantic
		&& (BaselineData.Bricks.Num()
				> BuildingSettings.MaxBrickCount
			|| BaselineData.Bricks.Num()
				> DAGSettings.MaxEstimatedBrickCount))
	{
		OutError = FString::Printf(
			TEXT("DAG5BBrickBudgetExceeded:Actual=%d:BuildingLimit=%d:DAGLimit=%d"),
			BaselineData.Bricks.Num(),
			BuildingSettings.MaxBrickCount,
			DAGSettings.MaxEstimatedBrickCount);
		OutSemanticResult.bAccepted = false;
		OutSemanticResult.RejectReason = OutError;
		return false;
	}

	BaselineData.DAGFailurePatternResult.bEnabled =
		PatternSettings.bEnableGeometryRewrite;
	BaselineData.DAGFailurePatternResult.Pattern = PatternSettings.Pattern;
	BaselineData.DAGFailurePlayabilityResult.bEnabled =
		PlayabilitySettings.bEnablePlayabilityRouting;
	auto PublishDownstreamFailure =
		[&bSemantic,
			&OutData,
			&OutSemanticResult,
			&OutError](FABTSM73StructureData& FailureData)
		{
			if (bSemantic)
			{
				// DAG5-B is a transactional candidate source. A downstream
				// DAG3 gate may inspect BaselineData for diagnostics, but a
				// false return must never publish that partial structure.
				OutData = FABTSM73StructureData();
				OutSemanticResult.bAccepted = false;
				OutSemanticResult.RejectReason = OutError;
				return;
			}
			// Preserve the established diagnostic contract byte-for-byte when
			// DAG5-B is disabled.
			OutData = MoveTemp(FailureData);
		};
	if (PlayabilitySettings.bEnablePlayabilityRouting
		&& (!FrontierSettings.bEnableAnalysis
			|| !FrontierSettings.bEnableGeneralizedSmallCutSearch
			|| !PatternSettings.bEnableGeometryRewrite))
	{
		OutError = TEXT("DAG3CRequiresAnalysisRewriteAndGeneralizedCut");
		BaselineData.DAGFailurePlayabilityResult.RejectReason = OutError;
		PublishDownstreamFailure(BaselineData);
		return false;
	}
	const FVector SafeAttackDirection = LocalAttackDirection.GetSafeNormal();
	if (PlayabilitySettings.bEnablePlayabilityRouting
		&& (SafeAttackDirection.IsNearlyZero()
			|| FMath::Abs(SafeAttackDirection.Z) > 0.95f))
	{
		OutError = TEXT("DAG3CAttackDirectionInvalid");
		BaselineData.DAGFailurePlayabilityResult.RejectReason = OutError;
		PublishDownstreamFailure(BaselineData);
		return false;
	}
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
		if (PlayabilitySettings.bEnablePlayabilityRouting)
		{
			BaselineData.DAGFailurePlayabilityResult.RejectReason = OutError;
		}
		PublishDownstreamFailure(BaselineData);
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
		PublishDownstreamFailure(BaselineData);
		return false;
	}
	if (PatternSettings.MaxRewriteAttemptCount < 1)
	{
		OutError = TEXT("DAG3BRewriteAttemptBudgetInvalid");
		BaselineData.DAGFailurePatternResult.RejectReason = OutError;
		PublishDownstreamFailure(BaselineData);
		return false;
	}

	FABTSM73DAGFailurePatternRewriter Rewriter;
	FABTSM73StabilityValidator StabilityValidator;
	int32 AttemptCount = 0;
	FString LastReject = TEXT("DAG3BNoTransactionAttempted");
	const FVector2D AttackFacingFailureDirection =
		-FVector2D(SafeAttackDirection.X, SafeAttackDirection.Y).GetSafeNormal();
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
			const int32 DirectionAttemptCount =
				PlayabilitySettings.bEnablePlayabilityRouting
					&& Pattern
						!= EABTSM73DAGFailurePattern::InternalSingleSupport
				? 2
				: 1;
			for (int32 DirectionAttempt = 0;
				DirectionAttempt < DirectionAttemptCount;
				++DirectionAttempt)
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
				TransactionError,
				PlayabilitySettings.bEnablePlayabilityRouting
					? AttackFacingFailureDirection
					: FVector2D::ZeroVector,
				DirectionAttempt > 0))
			{
				LastReject = MoveTemp(TransactionError);
				continue;
			}
			FABTSM73DAGSpatialLayout TrialLayout;
			const bool bTrialLayoutSolved = bSemantic
				? LayoutSolver.SolveSemantic(
					Graph,
					LayoutSettings,
					SemanticInitialLayout,
					RawSemanticEnvelope,
					TrialLayout,
					TransactionError,
					&Intent)
				: LayoutSolver.Solve(
					Graph,
					LayoutSettings,
					TrialLayout,
					TransactionError,
					&Intent);
			if (!bTrialLayoutSolved)
			{
				LastReject = MoveTemp(TransactionError);
				continue;
			}
			FABTSM73StructureData TrialData;
			FABTSM73SemanticEnvelope TrialSemanticEnvelope =
				RawSemanticEnvelope;
			FABTSM73DAG5BResult TrialSemanticResult =
				OutSemanticResult;
			if (bSemantic
				&& !SemanticBuilder.BindPhysicalContract(
					TrialLayout,
					TrialSemanticEnvelope,
					TransactionError))
			{
				LastReject = MoveTemp(TransactionError);
				continue;
			}
			if (bSemantic)
			{
				TrialSemanticResult.FeatureMask =
					TrialSemanticEnvelope.FeatureMask;
				TrialSemanticResult.EnvelopeHash =
					TrialSemanticEnvelope.EnvelopeHash;
			}
			const bool bTrialCompiled = bSemantic
				? Compiler.CompileSemantic(
					BuildingSettings,
					Graph,
					LayoutSettings,
					TrialLayout,
					TrialSemanticEnvelope,
					TrialSemanticResult,
					TrialData,
					TransactionError)
				: Compiler.Compile(
					BuildingSettings,
					Graph,
					LayoutSettings,
					TrialLayout,
					TrialData,
					TransactionError);
			if (!bTrialCompiled)
			{
				LastReject = MoveTemp(TransactionError);
				continue;
			}
			if (bSemantic
				&& (TrialData.Bricks.Num()
						> BuildingSettings.MaxBrickCount
					|| TrialData.Bricks.Num()
						> DAGSettings.MaxEstimatedBrickCount))
			{
				TransactionError = FString::Printf(
					TEXT("DAG5BBrickBudgetExceeded:Actual=%d:BuildingLimit=%d:DAGLimit=%d"),
					TrialData.Bricks.Num(),
					BuildingSettings.MaxBrickCount,
					DAGSettings.MaxEstimatedBrickCount);
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
			if (PlayabilitySettings.bEnablePlayabilityRouting)
			{
				FABTSM73DAGFailurePlayabilityPlanner PlayabilityPlanner;
				FABTSM73DAGFailurePlayabilityResult PlayabilityResult;
				if (!PlayabilityPlanner.Plan(
					PlayabilitySettings,
					DifficultySettings,
					BuildingSettings.PrimaryMaterial,
					MaterialProfiles,
					SafeAttackDirection,
					TrialData,
					PlayabilityResult,
					TransactionError))
				{
					LastReject = MoveTemp(TransactionError);
					continue;
				}
				TrialData.DAGFailurePlayabilityResult =
					MoveTemp(PlayabilityResult);
			}
			if (bSemantic)
			{
				OutSemanticResult = TrialSemanticResult;
			}
			OutData = MoveTemp(TrialData);
			return true;
			}
		}
		if (AttemptCount >= PatternSettings.MaxRewriteAttemptCount) break;
	}

	BaselineData.DAGFailurePatternResult.bEnabled = true;
	BaselineData.DAGFailurePatternResult.Pattern = PatternSettings.Pattern;
	BaselineData.DAGFailurePatternResult.RewriteAttemptCount = AttemptCount;
	BaselineData.DAGFailurePatternResult.SourceFrontierHash =
		BaselineData.DAGFailureFrontierAnalysis.SelectedFrontierHash;
	BaselineData.DAGFailurePatternResult.RejectReason = LastReject;
	if (PlayabilitySettings.bEnablePlayabilityRouting)
	{
		BaselineData.DAGFailurePlayabilityResult.bEnabled = true;
		BaselineData.DAGFailurePlayabilityResult.RejectReason = LastReject;
		OutError = FString::Printf(
			TEXT("DAG3CNoPlayablePattern:%s"),
			*LastReject);
	}
	else
	{
		OutError = FString::Printf(
			TEXT("DAG3BNoAcceptedPattern:%s"),
			*LastReject);
	}
	PublishDownstreamFailure(BaselineData);
	return false;
}

bool FABTSM73DAGBuildingPipeline::BuildWithFeasibilitySearch(
	const FABTSM73DAG5ASettings& SearchSettings,
	const FABTSM73DAGGenerationSettings& DAGSettings,
	const FABTSM73DAGLayoutSettings& LayoutSettings,
	const FABTSM73GenerationSettings& BuildingSettings,
	const FABTSM73DAGFailureFrontierSettings& FrontierSettings,
	const FABTSM73DAGFailurePatternSettings& PatternSettings,
	const FABTSM73DAGFailurePlayabilitySettings& PlayabilitySettings,
	const FABTSM73DifficultySettings& DifficultySettings,
	const TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles,
	const FVector& LocalAttackDirection,
	FABTSM73DAG5AResult& OutSearchResult,
	FABTSM73StructureData& OutData,
	FString& OutError) const
{
	if (!SearchSettings.bEnableFeasibilitySearch)
	{
		OutSearchResult = FABTSM73DAG5AResult();
		return BuildWithFailurePattern(
			DAGSettings,
			LayoutSettings,
			BuildingSettings,
			FrontierSettings,
			PatternSettings,
			PlayabilitySettings,
			DifficultySettings,
			MaterialProfiles,
			LocalAttackDirection,
			OutData,
			OutError);
	}
	FABTSM73DAG5CandidateSearch Search;
	FABTSM73DAGGenerationSettings SelectedDAGSettings;
	const bool bBuilt = Search.Build(
		SearchSettings,
		DAGSettings,
		LayoutSettings,
		BuildingSettings,
		[this,
			&LayoutSettings,
			&BuildingSettings,
			&FrontierSettings,
			&PatternSettings,
			&PlayabilitySettings,
			&DifficultySettings,
			MaterialProfiles,
			LocalAttackDirection](
				const FABTSM73DAGGenerationSettings& CandidateSettings,
				FABTSM73StructureData& CandidateData,
				FString& CandidateError)
		{
			return BuildWithFailurePattern(
				CandidateSettings,
				LayoutSettings,
				BuildingSettings,
				FrontierSettings,
				PatternSettings,
				PlayabilitySettings,
				DifficultySettings,
				MaterialProfiles,
				LocalAttackDirection,
				CandidateData,
				CandidateError);
		},
		SelectedDAGSettings,
		OutData,
		OutSearchResult,
		OutError);
	FinalizeDAG5ACompleteChainHash(
		SearchSettings,
		DAGSettings,
		LayoutSettings,
		BuildingSettings,
		FrontierSettings,
		PatternSettings,
		PlayabilitySettings,
		DifficultySettings,
		MaterialProfiles,
		LocalAttackDirection,
		OutData,
		OutSearchResult);
	return bBuilt;
}

bool FABTSM73DAGBuildingPipeline::BuildWithFeasibilitySearch(
	const FABTSM73DAG5ASettings& SearchSettings,
	const FABTSM73DAG5BSettings& SemanticSettings,
	const FABTSM73DAGGenerationSettings& DAGSettings,
	const FABTSM73DAGLayoutSettings& LayoutSettings,
	const FABTSM73GenerationSettings& BuildingSettings,
	const FABTSM73DAGFailureFrontierSettings& FrontierSettings,
	const FABTSM73DAGFailurePatternSettings& PatternSettings,
	const FABTSM73DAGFailurePlayabilitySettings& PlayabilitySettings,
	const FABTSM73DifficultySettings& DifficultySettings,
	const TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles,
	const FVector& LocalAttackDirection,
	FABTSM73DAG5AResult& OutSearchResult,
	FABTSM73DAG5BResult& OutSemanticResult,
	FABTSM73StructureData& OutData,
	FString& OutError) const
{
	OutSemanticResult = FABTSM73DAG5BResult();
	if (!SemanticSettings.bEnableSemanticEnvelope)
	{
		return BuildWithFeasibilitySearch(
			SearchSettings,
			DAGSettings,
			LayoutSettings,
			BuildingSettings,
			FrontierSettings,
			PatternSettings,
			PlayabilitySettings,
			DifficultySettings,
			MaterialProfiles,
			LocalAttackDirection,
			OutSearchResult,
			OutData,
			OutError);
	}
	OutSemanticResult.bEnabled = true;
	if (!SearchSettings.bEnableFeasibilitySearch)
	{
		OutSearchResult = FABTSM73DAG5AResult();
		return BuildWithFailurePattern(
			SemanticSettings,
			DAGSettings,
			LayoutSettings,
			BuildingSettings,
			FrontierSettings,
			PatternSettings,
			PlayabilitySettings,
			DifficultySettings,
			MaterialProfiles,
			LocalAttackDirection,
			OutSemanticResult,
			OutData,
			OutError);
	}

	FABTSM73DAG5CandidateSearch Search;
	FABTSM73DAGGenerationSettings SelectedDAGSettings;
	const bool bBuilt = Search.Build(
		SearchSettings,
		DAGSettings,
		LayoutSettings,
		BuildingSettings,
		[this,
			&SemanticSettings,
			&LayoutSettings,
			&BuildingSettings,
			&FrontierSettings,
			&PatternSettings,
			&PlayabilitySettings,
			&DifficultySettings,
			MaterialProfiles,
			LocalAttackDirection,
			&OutSemanticResult](
				const FABTSM73DAGGenerationSettings& CandidateSettings,
				FABTSM73StructureData& CandidateData,
				FString& CandidateError)
		{
			FABTSM73DAG5BResult CandidateSemanticResult;
			const bool bCandidateBuilt = BuildWithFailurePattern(
				SemanticSettings,
				CandidateSettings,
				LayoutSettings,
				BuildingSettings,
				FrontierSettings,
				PatternSettings,
				PlayabilitySettings,
				DifficultySettings,
				MaterialProfiles,
				LocalAttackDirection,
				CandidateSemanticResult,
				CandidateData,
				CandidateError);
			OutSemanticResult = CandidateSemanticResult;
			return bCandidateBuilt;
		},
		SelectedDAGSettings,
		OutData,
		OutSearchResult,
		OutError,
		false);
	if (bBuilt)
	{
		OutSemanticResult = OutData.DAG5BResult;
	}
	else
	{
		// Candidate callbacks use a scratch semantic result. A candidate may
		// complete B and then be rejected by A's compiled-brick budget, so do
		// not publish that unselected candidate as an accepted stage result.
		OutSemanticResult = FABTSM73DAG5BResult();
		OutSemanticResult.bEnabled = true;
		OutSemanticResult.RejectReason = OutError;
	}
	FinalizeDAG5ACompleteChainHash(
		SearchSettings,
		DAGSettings,
		LayoutSettings,
		BuildingSettings,
		FrontierSettings,
		PatternSettings,
		PlayabilitySettings,
		DifficultySettings,
		MaterialProfiles,
		LocalAttackDirection,
		OutData,
		OutSearchResult);
	FinalizeDAG5BCompleteChainHash(
		SemanticSettings,
		OutSemanticResult,
		OutSearchResult);
	return bBuilt;
}
