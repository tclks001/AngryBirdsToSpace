// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73WeakPointPlanner.h"

#include "Building/ABTSM7MaterialProfileLibrary.h"
#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73StructureData.h"

namespace
{
	constexpr int32 ExposureGridSize = 5;
	constexpr float OcclusionToleranceCM = 0.5f;

	struct FMaterialRank
	{
		EABTSM7BuildingMaterial Material = EABTSM7BuildingMaterial::Wood;
		float BreakEffort = 1.0f;
		int32 HitTier = 1;
	};

	struct FWeakCandidate
	{
		int32 NodeId = INDEX_NONE;
		EABTSM73WeakPointRole Role = EABTSM73WeakPointRole::None;
		float UnsupportedMassRatio = 0.0f;
		float Exposure = 0.0f;
		float Readability = 0.0f;
		float LocalBreakEffort = 1.0f;
		float Score = 0.0f;
		uint32 TieBreaker = 0;
		TArray<int32> UnsupportedNodeIds;
	};

	const EABTSM7BuildingMaterial AllMaterials[] = {
		EABTSM7BuildingMaterial::Wood,
		EABTSM7BuildingMaterial::Stone,
		EABTSM7BuildingMaterial::Iron,
		EABTSM7BuildingMaterial::Glass
	};

	void BuildResolvedProfiles(
		const TConstArrayView<FABTSM7MaterialProfile> Input,
		TArray<FABTSM7MaterialProfile>& OutProfiles)
	{
		const TArray<FABTSM7MaterialProfile> Defaults = FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
		OutProfiles.Reset(UE_ARRAY_COUNT(AllMaterials));
		for (const EABTSM7BuildingMaterial Material : AllMaterials)
		{
			const FABTSM7MaterialProfile* Profile = FABTSM7MaterialProfileLibrary::FindProfile(Input, Material);
			if (Profile == nullptr) Profile = FABTSM7MaterialProfileLibrary::FindProfile(Defaults, Material);
			if (Profile != nullptr) OutProfiles.Add(*Profile);
		}
	}

	void BuildMaterialRanks(
		const TConstArrayView<FABTSM7MaterialProfile> Profiles,
		TArray<FMaterialRank>& OutRanks)
	{
		OutRanks.Reset(UE_ARRAY_COUNT(AllMaterials));
		for (const EABTSM7BuildingMaterial Material : AllMaterials)
		{
			if (const FABTSM7MaterialProfile* Profile = FABTSM7MaterialProfileLibrary::FindProfile(Profiles, Material))
			{
				FMaterialRank& Rank = OutRanks.AddDefaulted_GetRef();
				Rank.Material = Material;
				Rank.BreakEffort = FABTSM7MaterialProfileLibrary::ComputeBreakEffort(*Profile);
			}
		}
		OutRanks.Sort([](const FMaterialRank& A, const FMaterialRank& B)
		{
			if (!FMath::IsNearlyEqual(A.BreakEffort, B.BreakEffort)) return A.BreakEffort < B.BreakEffort;
			return static_cast<uint8>(A.Material) < static_cast<uint8>(B.Material);
		});
		for (int32 Index = 0; Index < OutRanks.Num(); ++Index) OutRanks[Index].HitTier = Index + 1;
	}

	const FMaterialRank* FindMaterialRank(
		const TConstArrayView<FMaterialRank> Ranks,
		const EABTSM7BuildingMaterial Material)
	{
		for (const FMaterialRank& Rank : Ranks) if (Rank.Material == Material) return &Rank;
		return nullptr;
	}

	double NodeMass(
		const FABTSM73BrickNode& Node,
		const TConstArrayView<FABTSM7MaterialProfile> Profiles)
	{
		const FABTSM7MaterialProfile* Profile = FABTSM7MaterialProfileLibrary::FindProfile(Profiles, Node.Material);
		const double Density = Profile != nullptr ? FMath::Max(0.01f, Profile->DensityGPerCubicCM) : 1.0;
		const FVector Dimensions = Node.DimensionsCM.ComponentMax(FVector(1.0f));
		return static_cast<double>(Dimensions.X) * Dimensions.Y * Dimensions.Z * Density;
	}

	bool RayBoxEntry(
		const FVector& RayOrigin,
		const FVector& RayDirection,
		const FBox& Box,
		const float MaximumDistance,
		float& OutEntry)
	{
		float Entry = 0.0f;
		float Exit = MaximumDistance;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const float Origin = RayOrigin[Axis];
			const float Direction = RayDirection[Axis];
			if (FMath::Abs(Direction) <= SMALL_NUMBER)
			{
				if (Origin < Box.Min[Axis] || Origin > Box.Max[Axis]) return false;
				continue;
			}
			float Near = (Box.Min[Axis] - Origin) / Direction;
			float Far = (Box.Max[Axis] - Origin) / Direction;
			if (Near > Far) Swap(Near, Far);
			Entry = FMath::Max(Entry, Near);
			Exit = FMath::Min(Exit, Far);
			if (Entry > Exit) return false;
		}
		OutEntry = Entry;
		return Exit >= 0.0f && Entry <= MaximumDistance;
	}

	float ProjectedHalfExtent(const FVector& HalfDimensions, const FVector& Axis)
	{
		return FMath::Abs(Axis.X) * HalfDimensions.X
			+ FMath::Abs(Axis.Y) * HalfDimensions.Y
			+ FMath::Abs(Axis.Z) * HalfDimensions.Z;
	}

	float ComputeExposure(
		const FABTSM73BrickNode& Candidate,
		const FABTSM73StructureData& Data,
		const FVector& AttackDirection)
	{
		FVector Direction = AttackDirection.GetSafeNormal();
		if (Direction.IsNearlyZero()) Direction = FVector::ForwardVector;
		FVector AxisU;
		FVector AxisV;
		Direction.FindBestAxisVectors(AxisU, AxisV);
		const FVector Half = Candidate.DimensionsCM * 0.5f;
		const float HalfU = FMath::Max(1.0f, ProjectedHalfExtent(Half, AxisU));
		const float HalfV = FMath::Max(1.0f, ProjectedHalfExtent(Half, AxisV));
		const float RayLength = FMath::Max(2000.0f, Data.LocalBounds.GetSize().Size() * 4.0f + 1000.0f);
		const FBox CandidateBox(Candidate.LocalCenter - Half, Candidate.LocalCenter + Half);
		int32 CandidateRayCount = 0;
		int32 VisibleRayCount = 0;
		for (int32 UIndex = 0; UIndex < ExposureGridSize; ++UIndex)
		{
			const float U = (((UIndex + 0.5f) / ExposureGridSize) * 2.0f - 1.0f) * HalfU;
			for (int32 VIndex = 0; VIndex < ExposureGridSize; ++VIndex)
			{
				const float V = (((VIndex + 0.5f) / ExposureGridSize) * 2.0f - 1.0f) * HalfV;
				const FVector Origin = Candidate.LocalCenter - Direction * RayLength + AxisU * U + AxisV * V;
				float CandidateEntry = 0.0f;
				if (!RayBoxEntry(Origin, Direction, CandidateBox, RayLength * 2.0f, CandidateEntry)) continue;
				++CandidateRayCount;
				bool bOccluded = false;
				for (const FABTSM73BrickNode& Blocker : Data.Bricks)
				{
					if (Blocker.NodeId == Candidate.NodeId) continue;
					const FVector BlockerHalf = Blocker.DimensionsCM * 0.5f;
					const FBox BlockerBox(Blocker.LocalCenter - BlockerHalf, Blocker.LocalCenter + BlockerHalf);
					float BlockerEntry = 0.0f;
					if (RayBoxEntry(Origin, Direction, BlockerBox, RayLength * 2.0f, BlockerEntry)
						&& BlockerEntry < CandidateEntry - OcclusionToleranceCM)
					{
						bOccluded = true;
						break;
					}
				}
				if (!bOccluded) ++VisibleRayCount;
			}
		}
		return CandidateRayCount > 0
			? static_cast<float>(VisibleRayCount) / CandidateRayCount
			: 0.0f;
	}

	EABTSM73WeakPointRole ClassifyRole(const FABTSM73BrickNode& Node, const FABTSM73StructureData& Data)
	{
		const FVector D = Node.DimensionsCM.ComponentMax(FVector(1.0f));
		const bool bHorizontal = D.Z <= FMath::Min(D.X, D.Y) * 0.80f;
		if (bHorizontal)
		{
			if (D.Y >= FMath::Max(D.X, D.Z) * 1.50f) return EABTSM73WeakPointRole::BridgeConnector;
			return EABTSM73WeakPointRole::LoadBearingDeck;
		}
		return Data.GroundNodeIds.Contains(Node.NodeId)
			? EABTSM73WeakPointRole::GroundSupport
			: EABTSM73WeakPointRole::VerticalSupport;
	}

	float RoleReadability(const EABTSM73WeakPointRole Role)
	{
		switch (Role)
		{
		case EABTSM73WeakPointRole::LoadBearingDeck: return 1.0f;
		case EABTSM73WeakPointRole::BridgeConnector: return 0.95f;
		case EABTSM73WeakPointRole::GroundSupport: return 0.88f;
		case EABTSM73WeakPointRole::VerticalSupport: return 0.84f;
		default: return 0.70f;
		}
	}

	bool BuildGraph(
		const FABTSM73StructureData& Data,
		TMap<int32, int32>& OutNodeIndices,
		TMap<int32, TArray<int32>>& OutChildren,
		FString& OutError)
	{
		OutNodeIndices.Reset();
		OutChildren.Reset();
		for (int32 Index = 0; Index < Data.Bricks.Num(); ++Index)
		{
			const int32 NodeId = Data.Bricks[Index].NodeId;
			if (NodeId == INDEX_NONE || OutNodeIndices.Contains(NodeId))
			{
				OutError = FString::Printf(TEXT("InvalidOrDuplicateNodeId:%d"), NodeId);
				return false;
			}
			OutNodeIndices.Add(NodeId, Index);
		}
		for (const FABTSM73SupportEdge& Edge : Data.SupportEdges)
		{
			if (Edge.LowerNodeId == Edge.UpperNodeId || Edge.ContactAreaCM2 <= 0.0f
				|| !OutNodeIndices.Contains(Edge.LowerNodeId) || !OutNodeIndices.Contains(Edge.UpperNodeId))
			{
				OutError = FString::Printf(TEXT("InvalidSupportEdge:%d:%d"), Edge.LowerNodeId, Edge.UpperNodeId);
				return false;
			}
			OutChildren.FindOrAdd(Edge.LowerNodeId).AddUnique(Edge.UpperNodeId);
		}
		for (const int32 GroundNodeId : Data.GroundNodeIds)
		{
			if (!OutNodeIndices.Contains(GroundNodeId))
			{
				OutError = FString::Printf(TEXT("InvalidGroundNode:%d"), GroundNodeId);
				return false;
			}
		}
		return true;
	}

	void ProbeRemoval(
		const int32 RemovedNodeId,
		const FABTSM73StructureData& Data,
		const TMap<int32, TArray<int32>>& Children,
		const TConstArrayView<FABTSM7MaterialProfile> Profiles,
		TArray<int32>& OutUnsupported,
		float& OutMassRatio)
	{
		TSet<int32> Reachable;
		TArray<int32> Queue;
		for (const int32 GroundNodeId : Data.GroundNodeIds)
		{
			if (GroundNodeId != RemovedNodeId && !Reachable.Contains(GroundNodeId))
			{
				Reachable.Add(GroundNodeId);
				Queue.Add(GroundNodeId);
			}
		}
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			if (const TArray<int32>* NextNodes = Children.Find(Queue[Head]))
			{
				for (const int32 Next : *NextNodes)
				{
					if (Next == RemovedNodeId || Reachable.Contains(Next)) continue;
					Reachable.Add(Next);
					Queue.Add(Next);
				}
			}
		}

		double TotalMass = 0.0;
		double UnsupportedMass = 0.0;
		OutUnsupported.Reset();
		for (const FABTSM73BrickNode& Node : Data.Bricks)
		{
			const double Mass = NodeMass(Node, Profiles);
			TotalMass += Mass;
			if (Node.NodeId != RemovedNodeId && !Reachable.Contains(Node.NodeId))
			{
				OutUnsupported.Add(Node.NodeId);
				UnsupportedMass += Mass;
			}
		}
		OutUnsupported.Sort();
		OutMassRatio = TotalMass > SMALL_NUMBER ? static_cast<float>(UnsupportedMass / TotalMass) : 0.0f;
	}

	float AffectedOverlap(const FWeakCandidate& A, const FWeakCandidate& B)
	{
		if (A.UnsupportedNodeIds.IsEmpty() || B.UnsupportedNodeIds.IsEmpty()) return 0.0f;
		TSet<int32> AffectedA;
		for (const int32 NodeId : A.UnsupportedNodeIds) AffectedA.Add(NodeId);
		int32 Intersection = 0;
		for (const int32 NodeId : B.UnsupportedNodeIds) if (AffectedA.Contains(NodeId)) ++Intersection;
		return static_cast<float>(Intersection)
			/ FMath::Max(1, FMath::Min(A.UnsupportedNodeIds.Num(), B.UnsupportedNodeIds.Num()));
	}

	float ComputeCandidateScore(
		const float UnsupportedRatio,
		const float Exposure,
		const float Readability,
		const int32 HitTier,
		const FABTSM73DifficultySettings& Settings)
	{
		const float TargetSpan = FMath::Max3(
			0.01f,
			Settings.TargetWeakCollapseRatio - Settings.MinWeakCollapseRatio,
			Settings.MaxSingleWeakCollapseRatio - Settings.TargetWeakCollapseRatio);
		const float TargetFit = 1.0f - FMath::Clamp(
			FMath::Abs(UnsupportedRatio - Settings.TargetWeakCollapseRatio) / TargetSpan,
			0.0f,
			1.0f);
		return FMath::Clamp(
			UnsupportedRatio * Exposure * Readability * (0.40f + 0.60f * TargetFit)
			/ FMath::Max(1, HitTier),
			0.0f,
			1.0f);
	}
}

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
	TArray<FWeakCandidate> Candidates;
	Candidates.Reserve(Working.Bricks.Num());
	for (const FABTSM73BrickNode& Node : Working.Bricks)
	{
		FWeakCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.NodeId = Node.NodeId;
		Candidate.Role = ClassifyRole(Node, Working);
		ProbeRemoval(Node.NodeId, Working, Children, Profiles, Candidate.UnsupportedNodeIds, Candidate.UnsupportedMassRatio);
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
		if (Candidate.UnsupportedMassRatio < Settings.MinWeakCollapseRatio
			|| Candidate.UnsupportedMassRatio > Settings.MaxSingleWeakCollapseRatio
			|| Candidate.Exposure < Settings.MinWeakPointExposure)
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
		OutError = FString::Printf(TEXT("InsufficientWeakPoints:%d:%d"), Selected.Num(), RequestedCount);
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

	float WeakRatioSum = 0.0f;
	float WeakExposureSum = 0.0f;
	float MaxNonWeakEffect = 0.0f;
	for (FWeakCandidate& Candidate : Candidates)
	{
		const int32* Index = NodeIndices.Find(Candidate.NodeId);
		if (Index == nullptr) continue;
		FABTSM73BrickNode& Node = Working.Bricks[*Index];
		ProbeRemoval(Node.NodeId, Working, Children, Profiles, Candidate.UnsupportedNodeIds, Candidate.UnsupportedMassRatio);
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
