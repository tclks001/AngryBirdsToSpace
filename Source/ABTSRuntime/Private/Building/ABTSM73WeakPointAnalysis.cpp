// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73WeakPointAnalysis.h"

#include "Building/ABTSM7MaterialProfileLibrary.h"
#include "Building/ABTSM73StructureData.h"

namespace
{
	constexpr int32 ExposureGridSize = 5;
	constexpr float OcclusionToleranceCM = 0.5f;

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
}

namespace ABTSM73WeakPointAnalysis
{
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
		const TArray<int32>& NodesA = A.AffectedNodeIds.IsEmpty() ? A.UnsupportedNodeIds : A.AffectedNodeIds;
		const TArray<int32>& NodesB = B.AffectedNodeIds.IsEmpty() ? B.UnsupportedNodeIds : B.AffectedNodeIds;
		if (NodesA.IsEmpty() || NodesB.IsEmpty()) return 0.0f;
		TSet<int32> AffectedA;
		for (const int32 NodeId : NodesA) AffectedA.Add(NodeId);
		int32 Intersection = 0;
		for (const int32 NodeId : NodesB) if (AffectedA.Contains(NodeId)) ++Intersection;
		return static_cast<float>(Intersection)
			/ FMath::Max(1, FMath::Min(NodesA.Num(), NodesB.Num()));
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
