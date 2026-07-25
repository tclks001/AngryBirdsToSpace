// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAGContactGraphBuilder.h"

#include "Building/ABTSM73DAGTypes.h"
#include "Building/ABTSM73StructureData.h"

namespace
{
	FBox NodeBox(const FABTSM73BrickNode& Node)
	{
		return FBox(Node.LocalCenter - Node.DimensionsCM * 0.5f, Node.LocalCenter + Node.DimensionsCM * 0.5f);
	}

	uint64 MakeEdgeKey(const int32 LowerNodeId, const int32 UpperNodeId)
	{
		return (static_cast<uint64>(static_cast<uint32>(LowerNodeId)) << 32) | static_cast<uint32>(UpperNodeId);
	}

	bool HasEdge(const TSet<uint64>& EdgeKeys, const int32 LowerNodeId, const int32 UpperNodeId)
	{
		return EdgeKeys.Contains(MakeEdgeKey(LowerNodeId, UpperNodeId));
	}
}

bool FABTSM73DAGContactGraphBuilder::RebuildAndAudit(
	const FABTSM73DAGLayoutSettings& Settings,
	FABTSM73StructureData& InOutData,
	FString& OutError) const
{
	OutError.Reset();
	InOutData.SupportEdges.Reset();
	InOutData.GroundNodeIds.Reset();
	InOutData.GroundSupportPoints.Reset();
	InOutData.LocalBounds = FBox(EForceInit::ForceInit);
	InOutData.DAGMissingRequiredContactCount = 0;
	InOutData.DAGUnexpectedBypassCount = 0;
	if (InOutData.Bricks.IsEmpty())
	{
		OutError = TEXT("DAGContactNoBricks");
		return false;
	}

	for (const FABTSM73BrickNode& Node : InOutData.Bricks)
	{
		const FBox Box = NodeBox(Node);
		InOutData.LocalBounds += Box;
		if (FMath::Abs(Box.Min.Z) <= Settings.ContactToleranceCM)
		{
			InOutData.GroundNodeIds.Add(Node.NodeId);
			InOutData.GroundSupportPoints.Add(FVector2D(Node.LocalCenter.X, Node.LocalCenter.Y));
		}
	}
	InOutData.FootprintHalfExtent = FVector2D(
		FMath::Max(FMath::Abs(InOutData.LocalBounds.Min.X), FMath::Abs(InOutData.LocalBounds.Max.X)),
		FMath::Max(FMath::Abs(InOutData.LocalBounds.Min.Y), FMath::Abs(InOutData.LocalBounds.Max.Y)));

	TSet<uint64> RealizedEdges;
	for (int32 LowerIndex = 0; LowerIndex < InOutData.Bricks.Num(); ++LowerIndex)
	{
		const FABTSM73BrickNode& Lower = InOutData.Bricks[LowerIndex];
		const FBox LowerBox = NodeBox(Lower);
		for (int32 UpperIndex = 0; UpperIndex < InOutData.Bricks.Num(); ++UpperIndex)
		{
			if (LowerIndex == UpperIndex) continue;
			const FABTSM73BrickNode& Upper = InOutData.Bricks[UpperIndex];
			const FBox UpperBox = NodeBox(Upper);
			if (FMath::Abs(UpperBox.Min.Z - LowerBox.Max.Z) > Settings.ContactToleranceCM) continue;
			const float XOverlap = FMath::Min(LowerBox.Max.X, UpperBox.Max.X) - FMath::Max(LowerBox.Min.X, UpperBox.Min.X);
			const float YOverlap = FMath::Min(LowerBox.Max.Y, UpperBox.Max.Y) - FMath::Max(LowerBox.Min.Y, UpperBox.Min.Y);
			if (XOverlap <= KINDA_SMALL_NUMBER || YOverlap <= KINDA_SMALL_NUMBER) continue;
			FABTSM73SupportEdge& Edge = InOutData.SupportEdges.AddDefaulted_GetRef();
			Edge.LowerNodeId = Lower.NodeId;
			Edge.UpperNodeId = Upper.NodeId;
			Edge.ContactAreaCM2 = XOverlap * YOverlap;
			RealizedEdges.Add(MakeEdgeKey(Lower.NodeId, Upper.NodeId));
		}
	}

	TSet<uint64> ExpectedEdges;
	for (const FABTSM73DAGPhysicalSupportMapping& Mapping : InOutData.DAGPhysicalSupportMappings)
	{
		for (const int32 ColumnNodeId : Mapping.ColumnNodeIds)
		{
			ExpectedEdges.Add(MakeEdgeKey(Mapping.SupportPlateNodeId, ColumnNodeId));
			ExpectedEdges.Add(MakeEdgeKey(ColumnNodeId, Mapping.LoadPlateNodeId));
			if (!HasEdge(RealizedEdges, Mapping.SupportPlateNodeId, ColumnNodeId)
				|| !HasEdge(RealizedEdges, ColumnNodeId, Mapping.LoadPlateNodeId))
			{
				++InOutData.DAGMissingRequiredContactCount;
			}
		}
	}
	for (const uint64 EdgeKey : RealizedEdges)
	{
		if (!ExpectedEdges.Contains(EdgeKey)) ++InOutData.DAGUnexpectedBypassCount;
	}
	if (InOutData.DAGMissingRequiredContactCount > 0)
	{
		OutError = FString::Printf(TEXT("DAGMissingRequiredContact:%d"), InOutData.DAGMissingRequiredContactCount);
		return false;
	}
	if (InOutData.DAGUnexpectedBypassCount > 0)
	{
		OutError = FString::Printf(TEXT("DAGUnexpectedBypass:%d"), InOutData.DAGUnexpectedBypassCount);
		return false;
	}
	return true;
}
