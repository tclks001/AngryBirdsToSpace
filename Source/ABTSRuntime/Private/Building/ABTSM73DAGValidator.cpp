// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAGValidator.h"

#include "Building/ABTSM73DAGTypes.h"

namespace
{
	bool VisitExpressionNode(
		const int32 NodeId,
		const FABTSM73DAGGenerationResult& Result,
		TSet<int32>& Visiting,
		TSet<int32>& Visited,
		FString& OutError)
	{
		if (!Result.ExpressionNodes.IsValidIndex(NodeId))
		{
			OutError = FString::Printf(TEXT("ExpressionNodeMissing:%d"), NodeId);
			return false;
		}
		if (Visited.Contains(NodeId)) return true;
		if (Visiting.Contains(NodeId))
		{
			OutError = FString::Printf(TEXT("ExpressionCycle:%d"), NodeId);
			return false;
		}

		Visiting.Add(NodeId);
		const FABTSM73DAGExpressionNode& Node = Result.ExpressionNodes[NodeId];
		const bool bAtom = Node.Operator == EABTSM73DAGOperator::Atom;
		if (bAtom && !Node.ChildNodeIds.IsEmpty())
		{
			OutError = FString::Printf(TEXT("AtomHasChildren:%d"), NodeId);
			return false;
		}
		if (!bAtom && Node.ChildNodeIds.Num() < 2)
		{
			OutError = FString::Printf(TEXT("OperatorArityTooSmall:%d"), NodeId);
			return false;
		}

		for (const int32 ChildId : Node.ChildNodeIds)
		{
			if (!Result.ExpressionNodes.IsValidIndex(ChildId)
				|| Result.ExpressionNodes[ChildId].ParentNodeId != NodeId)
			{
				OutError = FString::Printf(TEXT("ExpressionParentMismatch:%d:%d"), NodeId, ChildId);
				return false;
			}
			if (!VisitExpressionNode(ChildId, Result, Visiting, Visited, OutError)) return false;
		}
		Visiting.Remove(NodeId);
		Visited.Add(NodeId);
		return true;
	}

	uint64 MakeValidationEdgeKey(const int32 SupportNodeId, const int32 LoadNodeId)
	{
		return (static_cast<uint64>(static_cast<uint32>(SupportNodeId)) << 32)
			| static_cast<uint32>(LoadNodeId);
	}
}

bool FABTSM73DAGValidator::Validate(
	const FABTSM73DAGGenerationSettings& Settings,
	const FABTSM73DAGGenerationResult& Result,
	FString& OutError) const
{
	OutError.Reset();
	if (!Result.ExpressionNodes.IsValidIndex(Result.RootExpressionNodeId))
	{
		OutError = TEXT("RootExpressionMissing");
		return false;
	}
	if (Result.ExpressionNodes[Result.RootExpressionNodeId].ParentNodeId != INDEX_NONE)
	{
		OutError = TEXT("RootExpressionHasParent");
		return false;
	}
	if (Result.ExpressionNodes.Num() > Settings.MaxAbstractNodeCount)
	{
		OutError = TEXT("AbstractNodeBudgetExceeded");
		return false;
	}

	TSet<int32> Visiting;
	TSet<int32> Visited;
	if (!VisitExpressionNode(Result.RootExpressionNodeId, Result, Visiting, Visited, OutError)) return false;
	if (Visited.Num() != Result.ExpressionNodes.Num())
	{
		OutError = FString::Printf(TEXT("UnreachableExpressionNodes:%d:%d"), Visited.Num(), Result.ExpressionNodes.Num());
		return false;
	}

	int32 AtomCount = 0;
	for (int32 NodeIndex = 0; NodeIndex < Result.ExpressionNodes.Num(); ++NodeIndex)
	{
		const FABTSM73DAGExpressionNode& Node = Result.ExpressionNodes[NodeIndex];
		if (Node.NodeId != NodeIndex)
		{
			OutError = TEXT("ExpressionNodeIdsNotDense");
			return false;
		}
		if (Node.Operator == EABTSM73DAGOperator::Atom) ++AtomCount;
	}
	if (AtomCount != Result.MacroNodes.Num())
	{
		OutError = FString::Printf(TEXT("AtomMacroCountMismatch:%d:%d"), AtomCount, Result.MacroNodes.Num());
		return false;
	}
	if (Result.EstimatedBrickCount != Result.MacroNodes.Num() + Settings.ReservedWeaknessBrickCount)
	{
		OutError = TEXT("EstimatedBrickCountMismatch");
		return false;
	}
	if (Result.EstimatedBrickCount > Settings.MaxEstimatedBrickCount)
	{
		OutError = TEXT("EstimatedBrickBudgetExceeded");
		return false;
	}
	if (Result.MacroNodes.IsEmpty() || Result.GroundNodeIds.IsEmpty() || Result.TopLoadNodeIds.IsEmpty())
	{
		OutError = TEXT("CompiledDAGMissingFrontier");
		return false;
	}

	TArray<int32> InDegrees;
	TArray<TArray<int32>> Outgoing;
	InDegrees.Init(0, Result.MacroNodes.Num());
	Outgoing.SetNum(Result.MacroNodes.Num());
	TSet<uint64> UniqueEdges;
	for (const FABTSM73DAGSupportEdge& Edge : Result.SupportEdges)
	{
		if (!Result.MacroNodes.IsValidIndex(Edge.SupportNodeId)
			|| !Result.MacroNodes.IsValidIndex(Edge.LoadNodeId)
			|| Edge.SupportNodeId == Edge.LoadNodeId)
		{
			OutError = TEXT("InvalidSupportEdge");
			return false;
		}
		const uint64 EdgeKey = MakeValidationEdgeKey(Edge.SupportNodeId, Edge.LoadNodeId);
		if (UniqueEdges.Contains(EdgeKey))
		{
			OutError = TEXT("DuplicateSupportEdge");
			return false;
		}
		UniqueEdges.Add(EdgeKey);
		++InDegrees[Edge.LoadNodeId];
		Outgoing[Edge.SupportNodeId].Add(Edge.LoadNodeId);
	}

	TArray<int32> Queue;
	for (int32 NodeId = 0; NodeId < InDegrees.Num(); ++NodeId)
	{
		if (InDegrees[NodeId] == 0) Queue.Add(NodeId);
	}
	int32 VisitedMacroCount = 0;
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 NodeId = Queue[Head];
		++VisitedMacroCount;
		for (const int32 LoadId : Outgoing[NodeId])
		{
			if (--InDegrees[LoadId] == 0) Queue.Add(LoadId);
		}
	}
	if (VisitedMacroCount != Result.MacroNodes.Num())
	{
		OutError = TEXT("CompiledSupportGraphCycle");
		return false;
	}

	TSet<int32> ReachableFromGround;
	TArray<int32> ReachabilityQueue;
	for (const int32 GroundNodeId : Result.GroundNodeIds)
	{
		if (!Result.MacroNodes.IsValidIndex(GroundNodeId))
		{
			OutError = TEXT("InvalidGroundNode");
			return false;
		}
		ReachableFromGround.Add(GroundNodeId);
		ReachabilityQueue.Add(GroundNodeId);
	}
	for (int32 Head = 0; Head < ReachabilityQueue.Num(); ++Head)
	{
		for (const int32 LoadId : Outgoing[ReachabilityQueue[Head]])
		{
			if (ReachableFromGround.Contains(LoadId)) continue;
			ReachableFromGround.Add(LoadId);
			ReachabilityQueue.Add(LoadId);
		}
	}
	if (ReachableFromGround.Num() != Result.MacroNodes.Num())
	{
		OutError = FString::Printf(TEXT("NoGroundPath:%d:%d"), ReachableFromGround.Num(), Result.MacroNodes.Num());
		return false;
	}
	for (const int32 TopNodeId : Result.TopLoadNodeIds)
	{
		if (!Result.MacroNodes.IsValidIndex(TopNodeId) || !Outgoing[TopNodeId].IsEmpty())
		{
			OutError = TEXT("InvalidTopLoadNode");
			return false;
		}
	}
	if (Result.CanonicalExpression.IsEmpty())
	{
		OutError = TEXT("CanonicalTopologyMissing");
		return false;
	}
	return true;
}
