// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73WeakPointPlanner.h"

#include "Building/ABTSM73WeakPointAnalysis.h"
#include "Building/ABTSM7MaterialProfileLibrary.h"
#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73PostFailureValidator.h"
#include "Building/ABTSM73StructureData.h"


using namespace ABTSM73WeakPointAnalysis;

bool FABTSM73WeakPointPlanner::Plan(
	const FABTSM73DifficultySettings& Settings,
	const TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles,
	const FVector& LocalAttackDirection,
	const int32 BuildingSeed,
	FABTSM73StructureData& InOutData,
	FString& OutError) const
{
	OutError.Reset();
	FABTSM73StructureData Working = InOutData;
	Working.WeakPoints.Reset();
	Working.FailureProbeResults.Reset();
	Working.ReinforcedNodeIds.Reset();
	Working.BestWeakPointScore = 0.0f;
	Working.PredictedWeakCollapseRatio = 0.0f;
	Working.PredictedNonWeakEffect = 0.0f;
	Working.DifficultyScore = 0.0f;
	Working.EstimatedWeakPointHits = 0;
	for (FABTSM73BrickNode& Node : Working.Bricks)
	{
		Node.Material = Node.OriginalMaterial;
		Node.WeakPointRole = EABTSM73WeakPointRole::None;
		Node.WeakPointScore = 0.0f;
		Node.UnsupportedMassRatio = 0.0f;
		Node.AttackExposure = 0.0f;
		Node.EstimatedHits = 0;
		Node.bWeakPoint = false;
		Node.bReinforcedCriticalNode = false;
	}
	if (!Settings.bEnableWeakPointPlanning)
	{
		InOutData = MoveTemp(Working);
		return true;
	}

	TArray<FABTSM7MaterialProfile> Profiles;
	BuildResolvedProfiles(MaterialProfiles, Profiles);
	TArray<FMaterialRank> MaterialRanks;
	BuildMaterialRanks(Profiles, MaterialRanks);
	if (MaterialRanks.Num() != UE_ARRAY_COUNT(AllMaterials))
	{
		OutError = TEXT("IncompleteMaterialProfiles");
		return false;
	}
	const int32 TargetTier = FMath::Clamp(Settings.TargetBirdHits, 1, MaterialRanks.Num());
	const EABTSM7BuildingMaterial PlannedWeakMaterial = Settings.bAutoSelectWeakPointMaterial
		? MaterialRanks[TargetTier - 1].Material
		: Settings.WeakPointMaterial;
	const FMaterialRank* WeakMaterialRank = FindMaterialRank(MaterialRanks, PlannedWeakMaterial);
	if (WeakMaterialRank == nullptr)
	{
		OutError = TEXT("WeakPointMaterialProfileMissing");
		return false;
	}

	TMap<int32, int32> NodeIndices;
	TMap<int32, TArray<int32>> Children;
	if (!BuildGraph(Working, NodeIndices, Children, OutError)) return false;
	FABTSM73PostFailureValidator PostFailureValidator;
	TMap<int32, FABTSM73FailureProbeResult> AuthoredProbeByCandidate;
	for (const FABTSM73StructuralWeaknessIntent& Intent : Working.StructuralWeaknessIntents)
	{
		FABTSM73FailureProbeResult Result;
		FString ProbeError;
		if (!PostFailureValidator.EvaluateAuthoredIntent(
			Settings, Profiles, Working, Intent, Result, ProbeError))
		{
			if (Settings.bRequireAuthoredStructuralWeakness)
			{
				OutError = FString::Printf(TEXT("B2FailureProbeRejected:%d:%s"), Intent.CandidateNodeId, *ProbeError);
				return false;
			}
			continue;
		}
		Working.FailureProbeResults.Add(Result);
		AuthoredProbeByCandidate.Add(Intent.CandidateNodeId, MoveTemp(Result));
	}
	if (Settings.bRequireAuthoredStructuralWeakness && AuthoredProbeByCandidate.IsEmpty())
	{
		OutError = TEXT("B2NoValidAuthoredWeakness");
		return false;
	}
	TArray<FWeakCandidate> Candidates;
	Candidates.Reserve(Working.Bricks.Num());
	for (const FABTSM73BrickNode& Node : Working.Bricks)
	{
		FWeakCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.NodeId = Node.NodeId;
		Candidate.Role = ClassifyRole(Node, Working);
		ProbeRemoval(Node.NodeId, Working, Children, Profiles, Candidate.UnsupportedNodeIds, Candidate.UnsupportedMassRatio);
		if (const FABTSM73FailureProbeResult* Authored = AuthoredProbeByCandidate.Find(Node.NodeId))
		{
			Candidate.bAuthoredStructuralWeakness = true;
			Candidate.StructuralPattern = Authored->Pattern;
			Candidate.CollapseMode = Authored->CollapseMode;
			Candidate.InitialSupportMarginCM = Authored->InitialSupportMarginCM;
			Candidate.TipMarginCM = Authored->TipMarginCM;
			Candidate.ReseatRisk = Authored->ReseatRisk;
			Candidate.UnsupportedMassRatio = Authored->AffectedMassRatio;
			Candidate.AffectedNodeIds = Authored->AffectedNodeIds;
		}
		else if (!Candidate.UnsupportedNodeIds.IsEmpty())
		{
			Candidate.ReseatRisk = PostFailureValidator.EstimateVerticalReseatRisk(
				Profiles, Working, Node.NodeId, Candidate.UnsupportedNodeIds);
			Candidate.UnsupportedMassRatio *= 1.0f - Candidate.ReseatRisk;
		}
		Candidate.Exposure = ComputeExposure(Node, Working, LocalAttackDirection);
		Candidate.Readability = RoleReadability(Candidate.Role)
			* (Node.Material == PlannedWeakMaterial ? 0.80f : 1.0f);
		Candidate.LocalBreakEffort = WeakMaterialRank->BreakEffort;
		Candidate.Score = ComputeCandidateScore(
			Candidate.UnsupportedMassRatio,
			Candidate.Exposure,
			Candidate.Readability,
			WeakMaterialRank->HitTier,
			Settings);
		if (Candidate.bAuthoredStructuralWeakness)
		{
			const float TipFeasibility = FMath::Clamp(
				Candidate.TipMarginCM / FMath::Max(1.0f, Settings.MinTipMarginCM * 2.0f), 0.0f, 1.0f);
			Candidate.Score *= TipFeasibility * (1.0f - Candidate.ReseatRisk);
		}
		Candidate.TieBreaker = HashCombineFast(GetTypeHash(BuildingSeed), GetTypeHash(Node.NodeId));
	}
	Candidates.Sort([](const FWeakCandidate& A, const FWeakCandidate& B)
	{
		if (!FMath::IsNearlyEqual(A.Score, B.Score, KINDA_SMALL_NUMBER)) return A.Score > B.Score;
		return A.TieBreaker < B.TieBreaker;
	});

	TArray<FWeakCandidate> Selected;
	const int32 RequestedCount = FMath::Clamp(Settings.WeakPointCount, 1, 3);
	for (const FWeakCandidate& Candidate : Candidates)
	{
		if (Settings.bRequireAuthoredStructuralWeakness && !Candidate.bAuthoredStructuralWeakness) continue;
		if (Candidate.UnsupportedMassRatio < Settings.MinWeakCollapseRatio
			|| Candidate.UnsupportedMassRatio > Settings.MaxSingleWeakCollapseRatio
			|| Candidate.Exposure < Settings.MinWeakPointExposure
			|| (Candidate.bAuthoredStructuralWeakness
				&& (Candidate.TipMarginCM < Settings.MinTipMarginCM || Candidate.ReseatRisk > Settings.MaxReseatRisk)))
		{
			continue;
		}
		const int32* CandidateIndex = NodeIndices.Find(Candidate.NodeId);
		if (CandidateIndex == nullptr) continue;
		bool bConflicts = false;
		for (const FWeakCandidate& Existing : Selected)
		{
			const int32* ExistingIndex = NodeIndices.Find(Existing.NodeId);
			if (ExistingIndex == nullptr) continue;
			const float Separation = FVector::Distance(
				Working.Bricks[*CandidateIndex].LocalCenter,
				Working.Bricks[*ExistingIndex].LocalCenter);
			if (Separation < Settings.MinWeakPointSeparationCM
				|| AffectedOverlap(Candidate, Existing) > Settings.MaxWeakPointAffectedOverlap)
			{
				bConflicts = true;
				break;
			}
		}
		if (bConflicts) continue;
		Selected.Add(Candidate);
		if (Selected.Num() >= RequestedCount) break;
	}
	if (Selected.Num() < RequestedCount)
	{
		const FWeakCandidate* Authored = Candidates.FindByPredicate([](const FWeakCandidate& Candidate)
		{
			return Candidate.bAuthoredStructuralWeakness;
		});
		OutError = Authored != nullptr
			? FString::Printf(TEXT("InsufficientWeakPoints:%d:%d:B2Ratio=%.4f:Exposure=%.3f:Tip=%.2f:Reseat=%.3f:Score=%.4f"),
				Selected.Num(), RequestedCount, Authored->UnsupportedMassRatio, Authored->Exposure,
				Authored->TipMarginCM, Authored->ReseatRisk, Authored->Score)
			: FString::Printf(TEXT("InsufficientWeakPoints:%d:%d"), Selected.Num(), RequestedCount);
		return false;
	}

	TSet<int32> SelectedIds;
	for (const FWeakCandidate& Candidate : Selected)
	{
		SelectedIds.Add(Candidate.NodeId);
		if (const int32* Index = NodeIndices.Find(Candidate.NodeId))
		{
			FABTSM73BrickNode& Node = Working.Bricks[*Index];
			Node.Material = PlannedWeakMaterial;
			Node.bWeakPoint = true;
			Node.WeakPointRole = Candidate.Role;
		}
	}

	if (Settings.bReinforceNonWeakCriticalNodes && Settings.MaxReinforcedNodeCount > 0)
	{
		TArray<FWeakCandidate> ReinforcementCandidates = Candidates;
		ReinforcementCandidates.Sort([](const FWeakCandidate& A, const FWeakCandidate& B)
		{
			if (!FMath::IsNearlyEqual(A.UnsupportedMassRatio, B.UnsupportedMassRatio))
				return A.UnsupportedMassRatio > B.UnsupportedMassRatio;
			return A.NodeId < B.NodeId;
		});
		for (const FWeakCandidate& Candidate : ReinforcementCandidates)
		{
			if (Working.ReinforcedNodeIds.Num() >= Settings.MaxReinforcedNodeCount) break;
			if (SelectedIds.Contains(Candidate.NodeId)
				|| Candidate.UnsupportedMassRatio < Settings.ReinforcementImpactThreshold) continue;
			if (const int32* Index = NodeIndices.Find(Candidate.NodeId))
			{
				FABTSM73BrickNode& Node = Working.Bricks[*Index];
				Node.Material = Settings.ReinforcementMaterial;
				Node.bReinforcedCriticalNode = true;
				Working.ReinforcedNodeIds.Add(Node.NodeId);
			}
		}
	}

	Working.FailureProbeResults.Reset();
	TMap<int32, FABTSM73FailureProbeResult> FinalAuthoredProbeByCandidate;
	for (const FABTSM73StructuralWeaknessIntent& Intent : Working.StructuralWeaknessIntents)
	{
		FABTSM73FailureProbeResult Result;
		FString ProbeError;
		if (!PostFailureValidator.EvaluateAuthoredIntent(Settings, Profiles, Working, Intent, Result, ProbeError))
		{
			const bool bSelectedAuthoredCandidate = SelectedIds.Contains(Intent.CandidateNodeId);
			if (Settings.bRequireAuthoredStructuralWeakness || bSelectedAuthoredCandidate)
			{
				OutError = FString::Printf(TEXT("B2FinalFailureProbeRejected:%d:%s"), Intent.CandidateNodeId, *ProbeError);
				return false;
			}
			continue;
		}
		Working.FailureProbeResults.Add(Result);
		FinalAuthoredProbeByCandidate.Add(Intent.CandidateNodeId, MoveTemp(Result));
	}

	float WeakRatioSum = 0.0f;
	float WeakExposureSum = 0.0f;
	float MaxNonWeakEffect = 0.0f;
	for (FWeakCandidate& Candidate : Candidates)
	{
		const int32* Index = NodeIndices.Find(Candidate.NodeId);
		if (Index == nullptr) continue;
		FABTSM73BrickNode& Node = Working.Bricks[*Index];
		ProbeRemoval(Node.NodeId, Working, Children, Profiles, Candidate.UnsupportedNodeIds, Candidate.UnsupportedMassRatio);
		Candidate.AffectedNodeIds.Reset();
		if (const FABTSM73FailureProbeResult* Authored = FinalAuthoredProbeByCandidate.Find(Node.NodeId))
		{
			Candidate.bAuthoredStructuralWeakness = true;
			Candidate.StructuralPattern = Authored->Pattern;
			Candidate.CollapseMode = Authored->CollapseMode;
			Candidate.InitialSupportMarginCM = Authored->InitialSupportMarginCM;
			Candidate.TipMarginCM = Authored->TipMarginCM;
			Candidate.ReseatRisk = Authored->ReseatRisk;
			Candidate.UnsupportedMassRatio = Authored->AffectedMassRatio;
			Candidate.AffectedNodeIds = Authored->AffectedNodeIds;
		}
		else if (!Candidate.UnsupportedNodeIds.IsEmpty())
		{
			Candidate.ReseatRisk = PostFailureValidator.EstimateVerticalReseatRisk(
				Profiles, Working, Node.NodeId, Candidate.UnsupportedNodeIds);
			Candidate.UnsupportedMassRatio *= 1.0f - Candidate.ReseatRisk;
		}
		const FMaterialRank* CurrentRank = FindMaterialRank(MaterialRanks, Node.Material);
		const int32 HitTier = CurrentRank != nullptr ? CurrentRank->HitTier : 1;
		Node.UnsupportedMassRatio = Candidate.UnsupportedMassRatio;
		Node.AttackExposure = Candidate.Exposure;
		Node.EstimatedHits = HitTier;
		if (Node.bWeakPoint)
		{
			Candidate.Score = ComputeCandidateScore(
				Candidate.UnsupportedMassRatio,
				Candidate.Exposure,
				Candidate.Readability,
				HitTier,
				Settings);
			if (Candidate.bAuthoredStructuralWeakness)
			{
				const float TipFeasibility = FMath::Clamp(
					Candidate.TipMarginCM / FMath::Max(1.0f, Settings.MinTipMarginCM * 2.0f), 0.0f, 1.0f);
				Candidate.Score *= TipFeasibility * (1.0f - Candidate.ReseatRisk);
			}
			Node.WeakPointScore = Candidate.Score;
			FABTSM73WeakPointRecord& Record = Working.WeakPoints.AddDefaulted_GetRef();
			Record.NodeId = Candidate.NodeId;
			Record.Role = Candidate.Role;
			Record.UnsupportedMassRatio = Candidate.UnsupportedMassRatio;
			Record.Exposure = Candidate.Exposure;
			Record.Readability = Candidate.Readability;
			Record.LocalBreakEffort = CurrentRank != nullptr ? CurrentRank->BreakEffort : 1.0f;
			Record.Score = Candidate.Score;
			Record.EstimatedHits = HitTier;
			Record.UnsupportedNodeIds = Candidate.UnsupportedNodeIds;
			Record.AffectedNodeIds = Candidate.AffectedNodeIds.IsEmpty()
				? Candidate.UnsupportedNodeIds : Candidate.AffectedNodeIds;
			Record.StructuralPattern = Candidate.StructuralPattern;
			Record.CollapseMode = Candidate.CollapseMode;
			Record.InitialSupportMarginCM = Candidate.InitialSupportMarginCM;
			Record.TipMarginCM = Candidate.TipMarginCM;
			Record.ReseatRisk = Candidate.ReseatRisk;
			WeakRatioSum += Candidate.UnsupportedMassRatio;
			WeakExposureSum += Candidate.Exposure;
			Working.BestWeakPointScore = FMath::Max(Working.BestWeakPointScore, Candidate.Score);
			Working.EstimatedWeakPointHits = FMath::Max(Working.EstimatedWeakPointHits, HitTier);
		}
		else
		{
			MaxNonWeakEffect = FMath::Max(MaxNonWeakEffect, Candidate.UnsupportedMassRatio / FMath::Max(1, HitTier));
		}
	}
	Working.WeakPoints.Sort([](const FABTSM73WeakPointRecord& A, const FABTSM73WeakPointRecord& B)
	{
		if (!FMath::IsNearlyEqual(A.Score, B.Score)) return A.Score > B.Score;
		return A.NodeId < B.NodeId;
	});
	Working.PredictedWeakCollapseRatio = WeakRatioSum / FMath::Max(1, Working.WeakPoints.Num());
	Working.PredictedNonWeakEffect = MaxNonWeakEffect;
	const float AverageExposure = WeakExposureSum / FMath::Max(1, Working.WeakPoints.Num());
	const float CollapseSpan = FMath::Max(0.01f, Settings.MaxSingleWeakCollapseRatio - Settings.MinWeakCollapseRatio);
	const float CollapseFit = 1.0f - FMath::Clamp(
		FMath::Abs(Working.PredictedWeakCollapseRatio - Settings.TargetWeakCollapseRatio) / CollapseSpan,
		0.0f,
		1.0f);
	const float WeakEffect = Working.PredictedWeakCollapseRatio / FMath::Max(1, Working.EstimatedWeakPointHits);
	const float Advantage = WeakEffect / FMath::Max(0.001f, MaxNonWeakEffect);
	const float AdvantageScore = FMath::Clamp(Advantage / FMath::Max(1.0f, Settings.MinWeakPointAdvantage), 0.0f, 1.0f);
	const float ResistanceScore = 1.0f - FMath::Clamp(
		MaxNonWeakEffect / FMath::Max(0.001f, Settings.MaxNonWeakEffect),
		0.0f,
		1.0f);
	Working.DifficultyScore = FMath::Clamp(
		0.40f * CollapseFit + 0.25f * AverageExposure + 0.20f * AdvantageScore + 0.15f * ResistanceScore,
		0.0f,
		1.0f);

	if (Settings.bRejectOutsideDifficultyWindow)
	{
		for (const FABTSM73WeakPointRecord& Record : Working.WeakPoints)
		{
			if (Record.UnsupportedMassRatio < Settings.MinWeakCollapseRatio
				|| Record.UnsupportedMassRatio > Settings.MaxSingleWeakCollapseRatio)
			{
				OutError = FString::Printf(TEXT("WeakPointOutsideWindow:%d:%.4f"), Record.NodeId, Record.UnsupportedMassRatio);
				return false;
			}
		}
		if (MaxNonWeakEffect > Settings.MaxNonWeakEffect + KINDA_SMALL_NUMBER)
		{
			OutError = FString::Printf(TEXT("NonWeakTooFragile:%.4f:%.4f"), MaxNonWeakEffect, Settings.MaxNonWeakEffect);
			return false;
		}
		if (MaxNonWeakEffect > KINDA_SMALL_NUMBER && Advantage < Settings.MinWeakPointAdvantage)
		{
			OutError = FString::Printf(TEXT("WeakPointAdvantageTooLow:%.3f:%.3f"), Advantage, Settings.MinWeakPointAdvantage);
			return false;
		}
	}

	InOutData = MoveTemp(Working);
	return true;
}
