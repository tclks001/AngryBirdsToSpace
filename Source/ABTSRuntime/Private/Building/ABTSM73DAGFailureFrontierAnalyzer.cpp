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
		TArray<int32> ProtectedRootNodeIds;
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

	FString SeedKey(const FFrontierSeed& Seed)
	{
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

	uint32 BuildCandidateHash(const FABTSM73DAGFailureFrontierCandidate& Candidate)
	{
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
	for (FFrontierSeed& Seed : Seeds)
	{
		SortUnique(Seed.CandidateNodeIds);
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
