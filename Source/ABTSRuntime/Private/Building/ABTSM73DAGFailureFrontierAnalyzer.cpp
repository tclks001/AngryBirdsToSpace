// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAGFailureFrontierAnalyzer.h"

#include "Building/ABTSM7MaterialProfileLibrary.h"
#include "Building/ABTSM73DAGFailureFrontierTypes.h"
#include "Building/ABTSM73StructureData.h"
#include "Misc/Crc.h"

namespace
{
	struct FFrontierSeed
	{
		EABTSM73DAGFailureCandidateKind Kind = EABTSM73DAGFailureCandidateKind::DirectedNodeCut;
		TArray<int32> CandidateNodeIds;
		TArray<FABTSM73DAGFailureEdgeRef> CandidateEdges;
		TArray<int32> ProtectedRootNodeIds;
	};

	struct FResidualEdge
	{
		int32 To = INDEX_NONE;
		int32 ReverseIndex = INDEX_NONE;
		int32 Capacity = 0;
	};

	enum class EBoundedCutSearchResult : uint8
	{
		NoBoundedCut,
		Found,
		BudgetExceeded
	};

	bool IsFiniteFrontierVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}

	const FABTSM73BrickNode* FindFrontierNode(
		const FABTSM73StructureData& Data,
		const TMap<int32, int32>& NodeIndices,
		const int32 NodeId)
	{
		const int32* Index = NodeIndices.Find(NodeId);
		return Index != nullptr && Data.Bricks.IsValidIndex(*Index) ? &Data.Bricks[*Index] : nullptr;
	}

	double FrontierNodeMass(
		const FABTSM73BrickNode& Node,
		const TConstArrayView<FABTSM7MaterialProfile> Profiles)
	{
		const FABTSM7MaterialProfile* Profile =
			FABTSM7MaterialProfileLibrary::FindProfile(Profiles, Node.Material);
		const double Density = Profile != nullptr
			? FMath::Max(0.01f, Profile->DensityGPerCubicCM)
			: 1.0;
		const FVector Dimensions = Node.DimensionsCM.ComponentMax(FVector(1.0f));
		return static_cast<double>(Dimensions.X) * Dimensions.Y * Dimensions.Z * Density;
	}

	uint64 MakeFrontierEdgeKey(const int32 LowerNodeId, const int32 UpperNodeId)
	{
		return (static_cast<uint64>(static_cast<uint32>(LowerNodeId)) << 32)
			| static_cast<uint32>(UpperNodeId);
	}

	void SortUnique(TArray<int32>& Values)
	{
		Values.Sort();
		for (int32 Index = Values.Num() - 1; Index > 0; --Index)
		{
			if (Values[Index] == Values[Index - 1]) Values.RemoveAt(Index);
		}
	}

	void SortUniqueEdges(TArray<FABTSM73DAGFailureEdgeRef>& Edges)
	{
		Edges.Sort([](
			const FABTSM73DAGFailureEdgeRef& A,
			const FABTSM73DAGFailureEdgeRef& B)
		{
			if (A.LowerNodeId != B.LowerNodeId)
			{
				return A.LowerNodeId < B.LowerNodeId;
			}
			return A.UpperNodeId < B.UpperNodeId;
		});
		for (int32 Index = Edges.Num() - 1; Index > 0; --Index)
		{
			if (Edges[Index] == Edges[Index - 1]) Edges.RemoveAt(Index);
		}
	}

	FString JoinNodeIds(const TArray<int32>& NodeIds)
	{
		FString Result;
		for (int32 Index = 0; Index < NodeIds.Num(); ++Index)
		{
			if (Index > 0) Result += TEXT(",");
			Result += FString::FromInt(NodeIds[Index]);
		}
		return Result;
	}

	FString JoinEdges(const TArray<FABTSM73DAGFailureEdgeRef>& Edges)
	{
		FString Result;
		for (int32 Index = 0; Index < Edges.Num(); ++Index)
		{
			if (Index > 0) Result += TEXT(",");
			Result += FString::Printf(
				TEXT("%d>%d"),
				Edges[Index].LowerNodeId,
				Edges[Index].UpperNodeId);
		}
		return Result;
	}

	FString SeedKey(const FFrontierSeed& Seed)
	{
		if (!Seed.CandidateEdges.IsEmpty())
		{
			return FString::Printf(
				TEXT("%d|%s|%s|%s"),
				static_cast<int32>(Seed.Kind),
				*JoinNodeIds(Seed.CandidateNodeIds),
				*JoinEdges(Seed.CandidateEdges),
				*JoinNodeIds(Seed.ProtectedRootNodeIds));
		}
		return FString::Printf(
			TEXT("%d|%s|%s"),
			static_cast<int32>(Seed.Kind),
			*JoinNodeIds(Seed.CandidateNodeIds),
			*JoinNodeIds(Seed.ProtectedRootNodeIds));
	}

	void GatherDescendants(
		const TMap<int32, TArray<int32>>& Children,
		const TConstArrayView<int32> Roots,
		const TSet<int32>& RemovedNodes,
		TSet<int32>& OutDescendants)
	{
		TArray<int32> Queue;
		for (const int32 RootNodeId : Roots)
		{
			if (RemovedNodes.Contains(RootNodeId) || OutDescendants.Contains(RootNodeId)) continue;
			OutDescendants.Add(RootNodeId);
			Queue.Add(RootNodeId);
		}
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			if (const TArray<int32>* NextNodes = Children.Find(Queue[Head]))
			{
				for (const int32 NextNodeId : *NextNodes)
				{
					if (RemovedNodes.Contains(NextNodeId) || OutDescendants.Contains(NextNodeId)) continue;
					OutDescendants.Add(NextNodeId);
					Queue.Add(NextNodeId);
				}
			}
		}
	}

	void GatherReachableFromGround(
		const FABTSM73StructureData& Data,
		const TMap<int32, TArray<int32>>& Children,
		const TSet<int32>& RemovedNodes,
		TSet<int32>& OutReachable)
	{
		TArray<int32> Queue;
		for (const int32 GroundNodeId : Data.GroundNodeIds)
		{
			if (RemovedNodes.Contains(GroundNodeId) || OutReachable.Contains(GroundNodeId)) continue;
			OutReachable.Add(GroundNodeId);
			Queue.Add(GroundNodeId);
		}
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			if (const TArray<int32>* NextNodes = Children.Find(Queue[Head]))
			{
				for (const int32 NextNodeId : *NextNodes)
				{
					if (RemovedNodes.Contains(NextNodeId) || OutReachable.Contains(NextNodeId)) continue;
					OutReachable.Add(NextNodeId);
					Queue.Add(NextNodeId);
				}
			}
		}
	}

	void AddResidualEdge(
		TArray<TArray<FResidualEdge>>& Graph,
		const int32 From,
		const int32 To,
		const int32 Capacity)
	{
		const int32 ForwardIndex = Graph[From].Num();
		const int32 ReverseIndex = Graph[To].Num();
		FResidualEdge& Forward = Graph[From].AddDefaulted_GetRef();
		Forward.To = To;
		Forward.ReverseIndex = ReverseIndex;
		Forward.Capacity = Capacity;
		FResidualEdge& Reverse = Graph[To].AddDefaulted_GetRef();
		Reverse.To = From;
		Reverse.ReverseIndex = ForwardIndex;
		Reverse.Capacity = 0;
	}

	bool ConsumeFlowOperation(
		const int32 MaxFlowOperationCount,
		int32& InOutFlowOperationCount)
	{
		++InOutFlowOperationCount;
		return InOutFlowOperationCount <= MaxFlowOperationCount;
	}

	EBoundedCutSearchResult FindBoundedVertexCut(
		const FABTSM73DAGFailureFrontierSettings& Settings,
		const TArray<int32>& SortedNodeIds,
		const TArray<FABTSM73DAGFailureEdgeRef>& SortedEdges,
		const TSet<int32>& GroundNodeIds,
		const int32 ProtectedRootNodeId,
		int32& InOutFlowOperationCount,
		TArray<int32>& OutCutNodeIds)
	{
		OutCutNodeIds.Reset();
		TMap<int32, int32> NodeOrdinals;
		for (int32 Index = 0; Index < SortedNodeIds.Num(); ++Index)
		{
			NodeOrdinals.Add(SortedNodeIds[Index], Index);
		}
		const int32* RootOrdinal = NodeOrdinals.Find(ProtectedRootNodeId);
		if (RootOrdinal == nullptr || GroundNodeIds.Contains(ProtectedRootNodeId))
		{
			return EBoundedCutSearchResult::NoBoundedCut;
		}

		const int32 SourceIndex = SortedNodeIds.Num() * 2;
		const int32 SinkIndex = SourceIndex + 1;
		TArray<TArray<FResidualEdge>> ResidualGraph;
		ResidualGraph.SetNum(SinkIndex + 1);
		const int32 InfiniteCapacity = Settings.MaxCutSetSize + 1;
		for (int32 Ordinal = 0; Ordinal < SortedNodeIds.Num(); ++Ordinal)
		{
			const int32 NodeId = SortedNodeIds[Ordinal];
			const bool bUncuttable =
				GroundNodeIds.Contains(NodeId)
				|| NodeId == ProtectedRootNodeId;
			AddResidualEdge(
				ResidualGraph,
				Ordinal * 2,
				Ordinal * 2 + 1,
				bUncuttable ? InfiniteCapacity : 1);
		}
		for (const FABTSM73DAGFailureEdgeRef& Edge : SortedEdges)
		{
			const int32* LowerOrdinal = NodeOrdinals.Find(Edge.LowerNodeId);
			const int32* UpperOrdinal = NodeOrdinals.Find(Edge.UpperNodeId);
			if (LowerOrdinal == nullptr || UpperOrdinal == nullptr)
			{
				return EBoundedCutSearchResult::NoBoundedCut;
			}
			AddResidualEdge(
				ResidualGraph,
				*LowerOrdinal * 2 + 1,
				*UpperOrdinal * 2,
				InfiniteCapacity);
		}
		TArray<int32> SortedGroundNodeIds = GroundNodeIds.Array();
		SortedGroundNodeIds.Sort();
		for (const int32 GroundNodeId : SortedGroundNodeIds)
		{
			const int32* GroundOrdinal = NodeOrdinals.Find(GroundNodeId);
			if (GroundOrdinal == nullptr)
			{
				return EBoundedCutSearchResult::NoBoundedCut;
			}
			AddResidualEdge(
				ResidualGraph,
				SourceIndex,
				*GroundOrdinal * 2,
				InfiniteCapacity);
		}
		AddResidualEdge(
			ResidualGraph,
			*RootOrdinal * 2 + 1,
			SinkIndex,
			InfiniteCapacity);

		int32 TotalFlow = 0;
		for (;;)
		{
			TArray<int32> ParentNodes;
			TArray<int32> ParentEdges;
			ParentNodes.Init(INDEX_NONE, ResidualGraph.Num());
			ParentEdges.Init(INDEX_NONE, ResidualGraph.Num());
			TArray<int32> Queue;
			ParentNodes[SourceIndex] = SourceIndex;
			Queue.Add(SourceIndex);
			for (int32 Head = 0;
				Head < Queue.Num() && ParentNodes[SinkIndex] == INDEX_NONE;
				++Head)
			{
				const int32 From = Queue[Head];
				for (int32 EdgeIndex = 0;
					EdgeIndex < ResidualGraph[From].Num();
					++EdgeIndex)
				{
					if (!ConsumeFlowOperation(
						Settings.MaxFlowOperationCount,
						InOutFlowOperationCount))
					{
						return EBoundedCutSearchResult::BudgetExceeded;
					}
					const FResidualEdge& Edge =
						ResidualGraph[From][EdgeIndex];
					if (Edge.Capacity <= 0
						|| ParentNodes[Edge.To] != INDEX_NONE)
					{
						continue;
					}
					ParentNodes[Edge.To] = From;
					ParentEdges[Edge.To] = EdgeIndex;
					Queue.Add(Edge.To);
					if (Edge.To == SinkIndex) break;
				}
			}
			if (ParentNodes[SinkIndex] == INDEX_NONE) break;

			int32 Augment = InfiniteCapacity;
			for (int32 NodeIndex = SinkIndex;
				NodeIndex != SourceIndex;
				NodeIndex = ParentNodes[NodeIndex])
			{
				if (!ConsumeFlowOperation(
					Settings.MaxFlowOperationCount,
					InOutFlowOperationCount))
				{
					return EBoundedCutSearchResult::BudgetExceeded;
				}
				const int32 ParentNode = ParentNodes[NodeIndex];
				const int32 ParentEdge = ParentEdges[NodeIndex];
				Augment = FMath::Min(
					Augment,
					ResidualGraph[ParentNode][ParentEdge].Capacity);
			}
			for (int32 NodeIndex = SinkIndex;
				NodeIndex != SourceIndex;
				NodeIndex = ParentNodes[NodeIndex])
			{
				if (!ConsumeFlowOperation(
					Settings.MaxFlowOperationCount,
					InOutFlowOperationCount))
				{
					return EBoundedCutSearchResult::BudgetExceeded;
				}
				const int32 ParentNode = ParentNodes[NodeIndex];
				const int32 ParentEdge = ParentEdges[NodeIndex];
				FResidualEdge& Edge =
					ResidualGraph[ParentNode][ParentEdge];
				const int32 ReverseIndex = Edge.ReverseIndex;
				Edge.Capacity -= Augment;
				ResidualGraph[NodeIndex][ReverseIndex].Capacity += Augment;
			}
			TotalFlow += Augment;
			if (TotalFlow > Settings.MaxCutSetSize)
			{
				return EBoundedCutSearchResult::NoBoundedCut;
			}
		}
		if (TotalFlow <= 0)
		{
			return EBoundedCutSearchResult::NoBoundedCut;
		}

		TBitArray<> ResidualReachable(false, ResidualGraph.Num());
		TArray<int32> ReachabilityQueue;
		ResidualReachable[SourceIndex] = true;
		ReachabilityQueue.Add(SourceIndex);
		for (int32 Head = 0; Head < ReachabilityQueue.Num(); ++Head)
		{
			const int32 From = ReachabilityQueue[Head];
			for (const FResidualEdge& Edge : ResidualGraph[From])
			{
				if (!ConsumeFlowOperation(
					Settings.MaxFlowOperationCount,
					InOutFlowOperationCount))
				{
					return EBoundedCutSearchResult::BudgetExceeded;
				}
				if (Edge.Capacity <= 0 || ResidualReachable[Edge.To]) continue;
				ResidualReachable[Edge.To] = true;
				ReachabilityQueue.Add(Edge.To);
			}
		}
		for (int32 Ordinal = 0; Ordinal < SortedNodeIds.Num(); ++Ordinal)
		{
			const int32 NodeId = SortedNodeIds[Ordinal];
			if (GroundNodeIds.Contains(NodeId)
				|| NodeId == ProtectedRootNodeId)
			{
				continue;
			}
			if (ResidualReachable[Ordinal * 2]
				&& !ResidualReachable[Ordinal * 2 + 1])
			{
				OutCutNodeIds.Add(NodeId);
			}
		}
		SortUnique(OutCutNodeIds);
		if (OutCutNodeIds.Num() != TotalFlow
			|| OutCutNodeIds.IsEmpty()
			|| OutCutNodeIds.Num() > Settings.MaxCutSetSize)
		{
			OutCutNodeIds.Reset();
			return EBoundedCutSearchResult::NoBoundedCut;
		}
		return EBoundedCutSearchResult::Found;
	}

	void BuildPhysicalCutBoundary(
		const FABTSM73StructureData& Data,
		const TMap<int32, TArray<int32>>& Children,
		const TArray<int32>& CutNodeIds,
		TArray<FABTSM73DAGFailureEdgeRef>& OutEdges,
		TArray<int32>& OutProtectedRootNodeIds)
	{
		TSet<int32> RemovedNodes;
		for (const int32 CutNodeId : CutNodeIds)
		{
			RemovedNodes.Add(CutNodeId);
		}
		TSet<int32> Reachable;
		GatherReachableFromGround(Data, Children, RemovedNodes, Reachable);
		for (const FABTSM73SupportEdge& Edge : Data.SupportEdges)
		{
			if (!RemovedNodes.Contains(Edge.LowerNodeId)
				|| RemovedNodes.Contains(Edge.UpperNodeId)
				|| Reachable.Contains(Edge.UpperNodeId))
			{
				continue;
			}
			FABTSM73DAGFailureEdgeRef& EdgeRef =
				OutEdges.AddDefaulted_GetRef();
			EdgeRef.LowerNodeId = Edge.LowerNodeId;
			EdgeRef.UpperNodeId = Edge.UpperNodeId;
			OutProtectedRootNodeIds.Add(Edge.UpperNodeId);
		}
		SortUniqueEdges(OutEdges);
		SortUnique(OutProtectedRootNodeIds);
	}

	uint32 BuildCandidateHash(const FABTSM73DAGFailureFrontierCandidate& Candidate)
	{
		if (!Candidate.CandidateEdges.IsEmpty())
		{
			const FString Identity = FString::Printf(
				TEXT("DAG3C|K=%d|C=%s|E=%s|R=%s|A=%s|M=%s|H=%d|Mass=%d|Span=%d|Bypass=%d"),
				static_cast<int32>(Candidate.Kind),
				*JoinNodeIds(Candidate.CandidateNodeIds),
				*JoinEdges(Candidate.CandidateEdges),
				*JoinNodeIds(Candidate.ProtectedRootNodeIds),
				*JoinNodeIds(Candidate.AffectedMainBodyNodeIds),
				*JoinNodeIds(Candidate.AffectedMacroNodeIds),
				FMath::RoundToInt(Candidate.NormalizedHeight * 10000.0f),
				FMath::RoundToInt(Candidate.MainBodyAffectedMassRatio * 10000.0f),
				FMath::RoundToInt(Candidate.AffectedHeightSpanNormalized * 10000.0f),
				Candidate.BypassSupportEdgeCount);
			const uint32 Hash = FCrc::StrCrc32(*Identity);
			return Hash != 0 ? Hash : 1u;
		}
		const FString Identity = FString::Printf(
			TEXT("DAG3A|K=%d|C=%s|R=%s|A=%s|M=%s|H=%d|Mass=%d|Span=%d|Bypass=%d"),
			static_cast<int32>(Candidate.Kind),
			*JoinNodeIds(Candidate.CandidateNodeIds),
			*JoinNodeIds(Candidate.ProtectedRootNodeIds),
			*JoinNodeIds(Candidate.AffectedMainBodyNodeIds),
			*JoinNodeIds(Candidate.AffectedMacroNodeIds),
			FMath::RoundToInt(Candidate.NormalizedHeight * 10000.0f),
			FMath::RoundToInt(Candidate.MainBodyAffectedMassRatio * 10000.0f),
			FMath::RoundToInt(Candidate.AffectedHeightSpanNormalized * 10000.0f),
			Candidate.BypassSupportEdgeCount);
		const uint32 Hash = FCrc::StrCrc32(*Identity);
		return Hash != 0 ? Hash : 1u;
	}
}

bool FABTSM73DAGFailureFrontierAnalyzer::Analyze(
	const FABTSM73DAGFailureFrontierSettings& Settings,
	const TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles,
	const FABTSM73StructureData& Data,
	FABTSM73DAGFailureFrontierAnalysis& OutAnalysis,
	FString& OutError) const
{
	OutAnalysis = FABTSM73DAGFailureFrontierAnalysis();
	OutAnalysis.bEnabled = Settings.bEnableAnalysis;
	OutError.Reset();
	if (!Settings.bEnableAnalysis)
	{
		return true;
	}
	if (Settings.MaxCutSetSize < 1 || Settings.MaxCutSetSize > 4
		|| Settings.MaxCandidateCount < 1 || Settings.MaxCandidateCount > 256
		|| (Settings.bEnableGeneralizedSmallCutSearch
			&& (Settings.MaxFlowOperationCount < 64
				|| Settings.MaxFlowOperationCount > 65536))
		|| !FMath::IsFinite(Settings.MinNormalizedHeight)
		|| !FMath::IsFinite(Settings.MaxNormalizedHeight)
		|| !FMath::IsFinite(Settings.MinMainBodyAffectedMassRatio)
		|| !FMath::IsFinite(Settings.TargetMainBodyAffectedMassRatio)
		|| !FMath::IsFinite(Settings.MaxMainBodyAffectedMassRatio)
		|| !FMath::IsFinite(Settings.MinAffectedHeightSpanNormalized)
		|| Settings.MinNormalizedHeight < 0.0f
		|| Settings.MaxNormalizedHeight < Settings.MinNormalizedHeight
		|| Settings.MaxNormalizedHeight > 1.0f
		|| Settings.MinMainBodyAffectedMassRatio < 0.0f
		|| Settings.TargetMainBodyAffectedMassRatio < Settings.MinMainBodyAffectedMassRatio
		|| Settings.MaxMainBodyAffectedMassRatio < Settings.TargetMainBodyAffectedMassRatio
		|| Settings.MaxMainBodyAffectedMassRatio > 1.0f
		|| Settings.MinAffectedHeightSpanNormalized < 0.0f
		|| Settings.MinAffectedHeightSpanNormalized > 1.0f
		|| Settings.MinAffectedMacroNodeCount < 1
		|| Settings.MinAffectedMacroNodeCount > 64
		|| Settings.MaxBypassSupportEdgeCount < 0
		|| Settings.MaxBypassSupportEdgeCount > 16)
	{
		OutError = TEXT("DAG3SettingsInvalid");
		OutAnalysis.RejectReason = OutError;
		return false;
	}
	if (Data.Bricks.IsEmpty() || Data.GroundNodeIds.IsEmpty())
	{
		OutError = TEXT("DAG3StructureMissing");
		OutAnalysis.RejectReason = OutError;
		return false;
	}

	TMap<int32, int32> NodeIndices;
	TMap<int32, TArray<int32>> Children;
	TSet<uint64> UniqueEdges;
	FBox Bounds(EForceInit::ForceInit);
	for (int32 Index = 0; Index < Data.Bricks.Num(); ++Index)
	{
		const FABTSM73BrickNode& Node = Data.Bricks[Index];
		if (Node.NodeId == INDEX_NONE || NodeIndices.Contains(Node.NodeId))
		{
			OutError = FString::Printf(TEXT("DAG3InvalidOrDuplicateNode:%d"), Node.NodeId);
			OutAnalysis.RejectReason = OutError;
			return false;
		}
		if (!IsFiniteFrontierVector(Node.LocalCenter)
			|| !IsFiniteFrontierVector(Node.DimensionsCM)
			|| Node.DimensionsCM.X <= 0.0f
			|| Node.DimensionsCM.Y <= 0.0f
			|| Node.DimensionsCM.Z <= 0.0f)
		{
			OutError = FString::Printf(
				TEXT("DAG3NodeGeometryInvalid:%d"),
				Node.NodeId);
			OutAnalysis.RejectReason = OutError;
			return false;
		}
		NodeIndices.Add(Node.NodeId, Index);
		Bounds += FBox(
			Node.LocalCenter - Node.DimensionsCM * 0.5f,
			Node.LocalCenter + Node.DimensionsCM * 0.5f);
	}
	for (const FABTSM73SupportEdge& Edge : Data.SupportEdges)
	{
		if (Edge.LowerNodeId == Edge.UpperNodeId
			|| !FMath::IsFinite(Edge.ContactAreaCM2)
			|| Edge.ContactAreaCM2 <= 0.0f
			|| !NodeIndices.Contains(Edge.LowerNodeId)
			|| !NodeIndices.Contains(Edge.UpperNodeId))
		{
			OutError = TEXT("DAG3SupportEdgeInvalid");
			OutAnalysis.RejectReason = OutError;
			return false;
		}
		const uint64 EdgeKey = MakeFrontierEdgeKey(Edge.LowerNodeId, Edge.UpperNodeId);
		if (UniqueEdges.Contains(EdgeKey))
		{
			OutError = TEXT("DAG3DuplicateSupportEdge");
			OutAnalysis.RejectReason = OutError;
			return false;
		}
		UniqueEdges.Add(EdgeKey);
		Children.FindOrAdd(Edge.LowerNodeId).Add(Edge.UpperNodeId);
	}
	for (TPair<int32, TArray<int32>>& Pair : Children) SortUnique(Pair.Value);

	TMap<int32, int32> IncomingEdgeCounts;
	for (const FABTSM73BrickNode& Node : Data.Bricks)
	{
		IncomingEdgeCounts.Add(Node.NodeId, 0);
	}
	for (const FABTSM73SupportEdge& Edge : Data.SupportEdges)
	{
		++IncomingEdgeCounts.FindChecked(Edge.UpperNodeId);
	}
	const TMap<int32, int32> StableIncomingEdgeCounts = IncomingEdgeCounts;
	TArray<int32> TopologyQueue;
	for (const TPair<int32, int32>& Pair : IncomingEdgeCounts)
	{
		if (Pair.Value == 0) TopologyQueue.Add(Pair.Key);
	}
	TopologyQueue.Sort();
	int32 TopologyVisitedCount = 0;
	for (int32 Head = 0; Head < TopologyQueue.Num(); ++Head)
	{
		++TopologyVisitedCount;
		if (const TArray<int32>* NextNodes = Children.Find(TopologyQueue[Head]))
		{
			for (const int32 NextNodeId : *NextNodes)
			{
				int32& IncomingCount = IncomingEdgeCounts.FindChecked(NextNodeId);
				--IncomingCount;
				if (IncomingCount == 0) TopologyQueue.Add(NextNodeId);
			}
		}
	}
	if (TopologyVisitedCount != Data.Bricks.Num())
	{
		OutError = TEXT("DAG3SupportGraphCycle");
		OutAnalysis.RejectReason = OutError;
		return false;
	}

	for (const int32 GroundNodeId : Data.GroundNodeIds)
	{
		if (!NodeIndices.Contains(GroundNodeId))
		{
			OutError = FString::Printf(TEXT("DAG3GroundNodeInvalid:%d"), GroundNodeId);
			OutAnalysis.RejectReason = OutError;
			return false;
		}
	}
	TSet<int32> BaselineReachable;
	const TSet<int32> NoRemovedNodes;
	GatherReachableFromGround(
		Data,
		Children,
		NoRemovedNodes,
		BaselineReachable);
	if (BaselineReachable.Num() != Data.Bricks.Num())
	{
		OutError = FString::Printf(
			TEXT("DAG3BaselineNoGroundPath:%d:%d"),
			BaselineReachable.Num(),
			Data.Bricks.Num());
		OutAnalysis.RejectReason = OutError;
		return false;
	}
	if (!Bounds.IsValid || Bounds.GetSize().Z <= KINDA_SMALL_NUMBER)
	{
		OutError = TEXT("DAG3BoundsInvalid");
		OutAnalysis.RejectReason = OutError;
		return false;
	}

	TArray<FFrontierSeed> Seeds;
	for (const FABTSM73BrickNode& Node : Data.Bricks)
	{
		if (Data.GroundNodeIds.Contains(Node.NodeId)) continue;
		const TArray<int32>* NodeChildren = Children.Find(Node.NodeId);
		if (NodeChildren == nullptr || NodeChildren->IsEmpty()) continue;
		FFrontierSeed& Seed = Seeds.AddDefaulted_GetRef();
		Seed.Kind = EABTSM73DAGFailureCandidateKind::DirectedNodeCut;
		Seed.CandidateNodeIds.Add(Node.NodeId);
		Seed.ProtectedRootNodeIds = *NodeChildren;
	}
	TMap<int32, TArray<int32>> InterfaceColumnsByLoadPlate;
	for (const FABTSM73DAGPhysicalSupportMapping& Mapping : Data.DAGPhysicalSupportMappings)
	{
		TArray<int32> MappingColumnNodeIds = Mapping.ColumnNodeIds;
		SortUnique(MappingColumnNodeIds);
		bool bMappingValid =
			NodeIndices.Contains(Mapping.SupportPlateNodeId)
			&& NodeIndices.Contains(Mapping.LoadPlateNodeId)
			&& !MappingColumnNodeIds.IsEmpty()
			&& MappingColumnNodeIds.Num() == Mapping.ColumnNodeIds.Num();
		for (const int32 ColumnNodeId : MappingColumnNodeIds)
		{
			bMappingValid = bMappingValid
				&& NodeIndices.Contains(ColumnNodeId)
				&& UniqueEdges.Contains(MakeFrontierEdgeKey(
					Mapping.SupportPlateNodeId,
					ColumnNodeId))
				&& UniqueEdges.Contains(MakeFrontierEdgeKey(
					ColumnNodeId,
					Mapping.LoadPlateNodeId));
		}
		if (!bMappingValid)
		{
			OutError = TEXT("DAG3SupportMappingInvalid");
			OutAnalysis.RejectReason = OutError;
			return false;
		}

		InterfaceColumnsByLoadPlate.FindOrAdd(
			Mapping.LoadPlateNodeId).Append(MappingColumnNodeIds);
		if (MappingColumnNodeIds.Num() <= Settings.MaxCutSetSize)
		{
			FFrontierSeed& Seed = Seeds.AddDefaulted_GetRef();
			Seed.Kind = EABTSM73DAGFailureCandidateKind::SupportInterfaceCutSet;
			Seed.CandidateNodeIds = MoveTemp(MappingColumnNodeIds);
			Seed.ProtectedRootNodeIds.Add(Mapping.LoadPlateNodeId);
		}
	}
	for (TPair<int32, TArray<int32>>& Pair : InterfaceColumnsByLoadPlate)
	{
		SortUnique(Pair.Value);
		if (Pair.Value.Num() > Settings.MaxCutSetSize) continue;
		FFrontierSeed& Seed = Seeds.AddDefaulted_GetRef();
		Seed.Kind = EABTSM73DAGFailureCandidateKind::SupportInterfaceCutSet;
		Seed.CandidateNodeIds = Pair.Value;
		Seed.ProtectedRootNodeIds.Add(Pair.Key);
	}
	if (Settings.bEnableGeneralizedSmallCutSearch)
	{
		for (FFrontierSeed& Seed : Seeds)
		{
			SortUnique(Seed.CandidateNodeIds);
			SortUniqueEdges(Seed.CandidateEdges);
			SortUnique(Seed.ProtectedRootNodeIds);
		}
		Seeds.Sort([](const FFrontierSeed& A, const FFrontierSeed& B)
		{
			return SeedKey(A) < SeedKey(B);
		});
		for (int32 Index = Seeds.Num() - 1; Index > 0; --Index)
		{
			if (SeedKey(Seeds[Index]) == SeedKey(Seeds[Index - 1]))
			{
				Seeds.RemoveAt(Index);
			}
		}
		if (Seeds.Num() > Settings.MaxCandidateCount)
		{
			OutError = FString::Printf(
				TEXT("DAG3CandidateBudgetExceeded:%d:%d"),
				Seeds.Num(),
				Settings.MaxCandidateCount);
			OutAnalysis.RejectReason = OutError;
			return false;
		}
		auto TryAddGeneralizedSeed = [&Seeds, &Settings](
			FFrontierSeed&& NewSeed)
		{
			SortUnique(NewSeed.CandidateNodeIds);
			SortUniqueEdges(NewSeed.CandidateEdges);
			SortUnique(NewSeed.ProtectedRootNodeIds);
			const FString NewKey = SeedKey(NewSeed);
			if (Seeds.ContainsByPredicate([&NewKey](const FFrontierSeed& Seed)
				{
					return SeedKey(Seed) == NewKey;
				}))
			{
				return true;
			}
			if (Seeds.Num() >= Settings.MaxCandidateCount)
			{
				return false;
			}
			Seeds.Add(MoveTemp(NewSeed));
			return true;
		};

		TArray<int32> SortedNodeIds;
		SortedNodeIds.Reserve(Data.Bricks.Num());
		for (const FABTSM73BrickNode& Node : Data.Bricks)
		{
			SortedNodeIds.Add(Node.NodeId);
		}
		SortedNodeIds.Sort();
		TArray<FABTSM73DAGFailureEdgeRef> SortedEdges;
		SortedEdges.Reserve(Data.SupportEdges.Num());
		for (const FABTSM73SupportEdge& Edge : Data.SupportEdges)
		{
			FABTSM73DAGFailureEdgeRef& EdgeRef =
				SortedEdges.AddDefaulted_GetRef();
			EdgeRef.LowerNodeId = Edge.LowerNodeId;
			EdgeRef.UpperNodeId = Edge.UpperNodeId;
		}
		SortUniqueEdges(SortedEdges);
		TSet<int32> GroundNodeSet;
		for (const int32 GroundNodeId : Data.GroundNodeIds)
		{
			GroundNodeSet.Add(GroundNodeId);
		}

		// A single edge is only physically realizable as its Lower brick when
		// that brick exclusively carries an Upper node with no alternate input.
		TSet<int32> DirectEdgeProtectedRootNodeIds;
		for (const FABTSM73DAGFailureEdgeRef& Edge : SortedEdges)
		{
			const TArray<int32>* LowerChildren =
				Children.Find(Edge.LowerNodeId);
			const int32* UpperIncomingCount =
				StableIncomingEdgeCounts.Find(Edge.UpperNodeId);
			if (GroundNodeSet.Contains(Edge.LowerNodeId)
				|| GroundNodeSet.Contains(Edge.UpperNodeId)
				|| LowerChildren == nullptr
				|| LowerChildren->Num() != 1
				|| (*LowerChildren)[0] != Edge.UpperNodeId
				|| UpperIncomingCount == nullptr
				|| *UpperIncomingCount != 1)
			{
				continue;
			}
			FFrontierSeed Seed;
			Seed.Kind =
				EABTSM73DAGFailureCandidateKind::DirectedEdgeCut;
			Seed.CandidateNodeIds.Add(Edge.LowerNodeId);
			Seed.CandidateEdges.Add(Edge);
			Seed.ProtectedRootNodeIds.Add(Edge.UpperNodeId);
			DirectEdgeProtectedRootNodeIds.Add(Edge.UpperNodeId);
			if (!TryAddGeneralizedSeed(MoveTemp(Seed)))
			{
				OutError = FString::Printf(
					TEXT("DAG3CandidateBudgetExceeded:%d:%d"),
					Seeds.Num() + 1,
					Settings.MaxCandidateCount);
				OutAnalysis.RejectReason = OutError;
				return false;
			}
		}

		TArray<int32> ProtectedRootNodeIds;
		for (const int32 NodeId : SortedNodeIds)
		{
			const int32* IncomingCount =
				StableIncomingEdgeCounts.Find(NodeId);
			if (!GroundNodeSet.Contains(NodeId)
				&& IncomingCount != nullptr
				&& (*IncomingCount > 1
					|| (*IncomingCount == 1
						&& !DirectEdgeProtectedRootNodeIds.Contains(NodeId))))
			{
				ProtectedRootNodeIds.Add(NodeId);
			}
		}
		int32 FlowOperationCount = 0;
		for (const int32 ProtectedRootNodeId : ProtectedRootNodeIds)
		{
			TArray<int32> CutNodeIds;
			const EBoundedCutSearchResult CutResult =
				FindBoundedVertexCut(
					Settings,
					SortedNodeIds,
					SortedEdges,
					GroundNodeSet,
					ProtectedRootNodeId,
					FlowOperationCount,
					CutNodeIds);
			if (CutResult == EBoundedCutSearchResult::BudgetExceeded)
			{
				OutError = FString::Printf(
					TEXT("DAG3GeneralizedCutFlowBudgetExceeded:%d:%d"),
					FlowOperationCount,
					Settings.MaxFlowOperationCount);
				OutAnalysis.RejectReason = OutError;
				return false;
			}
			if (CutResult != EBoundedCutSearchResult::Found) continue;

			FFrontierSeed Seed;
			Seed.Kind =
				EABTSM73DAGFailureCandidateKind::BoundedSmallNodeCut;
			Seed.CandidateNodeIds = MoveTemp(CutNodeIds);
			BuildPhysicalCutBoundary(
				Data,
				Children,
				Seed.CandidateNodeIds,
				Seed.CandidateEdges,
				Seed.ProtectedRootNodeIds);
			if (Seed.CandidateEdges.IsEmpty()
				|| Seed.ProtectedRootNodeIds.IsEmpty())
			{
				continue;
			}
			if (!TryAddGeneralizedSeed(MoveTemp(Seed)))
			{
				OutError = FString::Printf(
					TEXT("DAG3CandidateBudgetExceeded:%d:%d"),
					Seeds.Num() + 1,
					Settings.MaxCandidateCount);
				OutAnalysis.RejectReason = OutError;
				return false;
			}
		}
	}
	for (FFrontierSeed& Seed : Seeds)
	{
		SortUnique(Seed.CandidateNodeIds);
		SortUniqueEdges(Seed.CandidateEdges);
		SortUnique(Seed.ProtectedRootNodeIds);
	}
	Seeds.Sort([](const FFrontierSeed& A, const FFrontierSeed& B)
	{
		return SeedKey(A) < SeedKey(B);
	});
	for (int32 Index = Seeds.Num() - 1; Index > 0; --Index)
	{
		if (SeedKey(Seeds[Index]) == SeedKey(Seeds[Index - 1])) Seeds.RemoveAt(Index);
	}
	if (Seeds.Num() > Settings.MaxCandidateCount)
	{
		OutError = FString::Printf(
			TEXT("DAG3CandidateBudgetExceeded:%d:%d"),
			Seeds.Num(),
			Settings.MaxCandidateCount);
		OutAnalysis.RejectReason = OutError;
		return false;
	}

	TArray<int32> MainBodyNodeIds;
	FBox MainBodyBounds(EForceInit::ForceInit);
	for (const FABTSM73BrickNode& Node : Data.Bricks)
	{
		if (Node.bFailureFrontierMainBody)
		{
			const FABTSM7MaterialProfile* Profile =
				FABTSM7MaterialProfileLibrary::FindProfile(MaterialProfiles, Node.Material);
			if (Profile == nullptr)
			{
				OutError = FString::Printf(
					TEXT("DAG3MaterialProfileMissing:%d"),
					static_cast<int32>(Node.Material));
				OutAnalysis.RejectReason = OutError;
				return false;
			}
			if (!FMath::IsFinite(Profile->DensityGPerCubicCM)
				|| Profile->DensityGPerCubicCM <= 0.0f)
			{
				OutError = FString::Printf(
					TEXT("DAG3MaterialProfileInvalid:%d"),
					static_cast<int32>(Node.Material));
				OutAnalysis.RejectReason = OutError;
				return false;
			}
			MainBodyNodeIds.Add(Node.NodeId);
			MainBodyBounds += FBox(
				Node.LocalCenter - Node.DimensionsCM * 0.5f,
				Node.LocalCenter + Node.DimensionsCM * 0.5f);
		}
	}
	SortUnique(MainBodyNodeIds);
	double TotalMainBodyMass = 0.0;
	for (const int32 NodeId : MainBodyNodeIds)
	{
		const FABTSM73BrickNode* Node = FindFrontierNode(Data, NodeIndices, NodeId);
		if (Node != nullptr) TotalMainBodyMass += FrontierNodeMass(*Node, MaterialProfiles);
	}
	if (TotalMainBodyMass <= SMALL_NUMBER)
	{
		OutError = TEXT("DAG3MainBodyMassMissing");
		OutAnalysis.RejectReason = OutError;
		return false;
	}
	if (!MainBodyBounds.IsValid
		|| MainBodyBounds.GetSize().Z <= KINDA_SMALL_NUMBER)
	{
		OutError = TEXT("DAG3MainBodyBoundsInvalid");
		OutAnalysis.RejectReason = OutError;
		return false;
	}

	const float StructureHeight = MainBodyBounds.GetSize().Z;
	for (const FFrontierSeed& Seed : Seeds)
	{
		FABTSM73DAGFailureFrontierCandidate& Candidate =
			OutAnalysis.Candidates.AddDefaulted_GetRef();
		Candidate.Kind = Seed.Kind;
		Candidate.CandidateNodeIds = Seed.CandidateNodeIds;
		Candidate.CandidateEdges = Seed.CandidateEdges;
		Candidate.ProtectedRootNodeIds = Seed.ProtectedRootNodeIds;

		TSet<int32> RemovedNodes;
		float HeightSum = 0.0f;
		bool bSeedNodesValid = true;
		for (const int32 CandidateNodeId : Candidate.CandidateNodeIds)
		{
			const FABTSM73BrickNode* Node = FindFrontierNode(Data, NodeIndices, CandidateNodeId);
			if (Node == nullptr)
			{
				bSeedNodesValid = false;
				break;
			}
			RemovedNodes.Add(CandidateNodeId);
			HeightSum += Node->LocalCenter.Z;
		}
		if (!bSeedNodesValid || Candidate.CandidateNodeIds.IsEmpty())
		{
			Candidate.RejectReason = TEXT("DAG3CandidateNodesInvalid");
			Candidate.FrontierHash = BuildCandidateHash(Candidate);
			continue;
		}
		Candidate.NormalizedHeight = FMath::Clamp(
			(HeightSum / Candidate.CandidateNodeIds.Num() - MainBodyBounds.Min.Z)
				/ StructureHeight,
			0.0f,
			1.0f);

		TSet<int32> ExpectedAffected;
		GatherDescendants(Children, Candidate.ProtectedRootNodeIds, RemovedNodes, ExpectedAffected);
		Candidate.ExpectedAffectedNodeIds = ExpectedAffected.Array();
		SortUnique(Candidate.ExpectedAffectedNodeIds);

		TSet<int32> Reachable;
		GatherReachableFromGround(Data, Children, RemovedNodes, Reachable);
		TSet<int32> ActualAffected;
		for (const int32 NodeId : Candidate.ExpectedAffectedNodeIds)
		{
			if (!Reachable.Contains(NodeId)) ActualAffected.Add(NodeId);
		}
		Candidate.bDirectedDominator =
			!Candidate.ExpectedAffectedNodeIds.IsEmpty()
			&& ActualAffected.Num() == Candidate.ExpectedAffectedNodeIds.Num();

		TArray<int32> ActualAffectedNodeIds = ActualAffected.Array();
		SortUnique(ActualAffectedNodeIds);
		TSet<int32> AffectedMacros;
		double AffectedMainBodyMass = 0.0;
		FBox AffectedBounds(EForceInit::ForceInit);
		for (const int32 NodeId : ActualAffectedNodeIds)
		{
			const FABTSM73BrickNode* Node = FindFrontierNode(Data, NodeIndices, NodeId);
			if (Node == nullptr || !Node->bFailureFrontierMainBody) continue;
			Candidate.AffectedMainBodyNodeIds.Add(NodeId);
			AffectedMainBodyMass += FrontierNodeMass(*Node, MaterialProfiles);
			AffectedBounds += FBox(
				Node->LocalCenter - Node->DimensionsCM * 0.5f,
				Node->LocalCenter + Node->DimensionsCM * 0.5f);
			if (Node->MacroNodeId != INDEX_NONE) AffectedMacros.Add(Node->MacroNodeId);
		}
		SortUnique(Candidate.AffectedMainBodyNodeIds);
		Candidate.AffectedMacroNodeIds = AffectedMacros.Array();
		SortUnique(Candidate.AffectedMacroNodeIds);
		Candidate.MainBodyAffectedMassRatio =
			static_cast<float>(AffectedMainBodyMass / TotalMainBodyMass);
		Candidate.AffectedHeightSpanNormalized = AffectedBounds.IsValid
			? AffectedBounds.GetSize().Z / StructureHeight
			: 0.0f;

		for (const FABTSM73SupportEdge& Edge : Data.SupportEdges)
		{
			if (RemovedNodes.Contains(Edge.LowerNodeId) || RemovedNodes.Contains(Edge.UpperNodeId)) continue;
			if (!ExpectedAffected.Contains(Edge.LowerNodeId)
				&& ExpectedAffected.Contains(Edge.UpperNodeId)
				&& Reachable.Contains(Edge.LowerNodeId)
				&& Reachable.Contains(Edge.UpperNodeId))
			{
				++Candidate.BypassSupportEdgeCount;
			}
		}

		if (Candidate.NormalizedHeight < Settings.MinNormalizedHeight
			|| Candidate.NormalizedHeight > Settings.MaxNormalizedHeight)
		{
			Candidate.RejectReason = FString::Printf(
				TEXT("DAG3HeightOutsideRange:%.4f"),
				Candidate.NormalizedHeight);
		}
		else if (Candidate.ExpectedAffectedNodeIds.IsEmpty())
		{
			Candidate.RejectReason = TEXT("DAG3AffectedClosureEmpty");
		}
		else if (Settings.bRequireCompleteDirectedCut && !Candidate.bDirectedDominator)
		{
			Candidate.RejectReason = TEXT("DAG3DirectedCutIncomplete");
		}
		else if (Candidate.BypassSupportEdgeCount > Settings.MaxBypassSupportEdgeCount)
		{
			Candidate.RejectReason = FString::Printf(
				TEXT("DAG3FrontierBypass:%d:%d"),
				Candidate.BypassSupportEdgeCount,
				Settings.MaxBypassSupportEdgeCount);
		}
		else if (Candidate.AffectedMainBodyNodeIds.IsEmpty())
		{
			Candidate.RejectReason = TEXT("DAG3AffectedMainBodyEmpty");
		}
		else if (Candidate.MainBodyAffectedMassRatio < Settings.MinMainBodyAffectedMassRatio
			|| Candidate.MainBodyAffectedMassRatio > Settings.MaxMainBodyAffectedMassRatio)
		{
			Candidate.RejectReason = FString::Printf(
				TEXT("DAG3MainBodyMassOutsideRange:%.4f"),
				Candidate.MainBodyAffectedMassRatio);
		}
		else if (Candidate.AffectedHeightSpanNormalized
			< Settings.MinAffectedHeightSpanNormalized)
		{
			Candidate.RejectReason = FString::Printf(
				TEXT("DAG3AffectedHeightSpanTooSmall:%.4f"),
				Candidate.AffectedHeightSpanNormalized);
		}
		else if (Candidate.AffectedMacroNodeIds.Num()
			< Settings.MinAffectedMacroNodeCount)
		{
			Candidate.RejectReason = FString::Printf(
				TEXT("DAG3AffectedMacroCountTooSmall:%d:%d"),
				Candidate.AffectedMacroNodeIds.Num(),
				Settings.MinAffectedMacroNodeCount);
		}
		else
		{
			Candidate.bAccepted = true;
			++OutAnalysis.AcceptedCandidateCount;
		}
		Candidate.FrontierHash = BuildCandidateHash(Candidate);
	}

	OutAnalysis.Candidates.Sort([&Settings](
		const FABTSM73DAGFailureFrontierCandidate& A,
		const FABTSM73DAGFailureFrontierCandidate& B)
	{
		if (A.bAccepted != B.bAccepted) return A.bAccepted;
		const int32 DistanceA = FMath::RoundToInt(FMath::Abs(
			A.MainBodyAffectedMassRatio - Settings.TargetMainBodyAffectedMassRatio) * 10000.0f);
		const int32 DistanceB = FMath::RoundToInt(FMath::Abs(
			B.MainBodyAffectedMassRatio - Settings.TargetMainBodyAffectedMassRatio) * 10000.0f);
		if (DistanceA != DistanceB) return DistanceA < DistanceB;
		if (A.CandidateNodeIds.Num() != B.CandidateNodeIds.Num())
		{
			return A.CandidateNodeIds.Num() < B.CandidateNodeIds.Num();
		}
		if (A.Kind != B.Kind)
		{
			return static_cast<uint8>(A.Kind) < static_cast<uint8>(B.Kind);
		}
		return A.FrontierHash < B.FrontierHash;
	});
	if (OutAnalysis.AcceptedCandidateCount <= 0)
	{
		OutError = TEXT("DAG3NoAcceptedFailureFrontier");
		OutAnalysis.RejectReason = OutError;
		return false;
	}
	OutAnalysis.SelectedCandidateIndex = 0;
	OutAnalysis.SelectedFrontierHash = OutAnalysis.Candidates[0].FrontierHash;
	OutAnalysis.bAccepted = true;
	return true;
}
