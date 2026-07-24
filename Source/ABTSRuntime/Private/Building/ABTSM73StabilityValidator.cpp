// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73StabilityValidator.h"

#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73StructureData.h"

namespace
{
	FBox NodeBox(const FABTSM73BrickNode& Node)
	{
		return FBox(Node.LocalCenter - Node.DimensionsCM * 0.5f, Node.LocalCenter + Node.DimensionsCM * 0.5f);
	}

	float PositiveOverlap(const float MinA, const float MaxA, const float MinB, const float MaxB)
	{
		return FMath::Min(MaxA, MaxB) - FMath::Max(MinA, MinB);
	}
}

bool FABTSM73StabilityValidator::HasGroundPath(
	const int32 NodeId,
	const FABTSM73StructureData& Data,
	TSet<int32>& Visiting,
	TMap<int32, bool>& Cache)
{
	if (Data.GroundNodeIds.Contains(NodeId)) return true;
	if (const bool* Cached = Cache.Find(NodeId)) return *Cached;
	if (Visiting.Contains(NodeId)) return false;
	Visiting.Add(NodeId);
	bool bHasPath = false;
	for (const FABTSM73SupportEdge& Edge : Data.SupportEdges)
	{
		if (Edge.UpperNodeId == NodeId && HasGroundPath(Edge.LowerNodeId, Data, Visiting, Cache))
		{
			bHasPath = true;
			break;
		}
	}
	Visiting.Remove(NodeId);
	Cache.Add(NodeId, bHasPath);
	return bHasPath;
}

bool FABTSM73StabilityValidator::Validate(
	const FABTSM73GenerationSettings& Settings,
	const FABTSM73StructureData& Data,
	FString& OutError) const
{
	OutError.Reset();
	if (Data.Bricks.IsEmpty()) { OutError = TEXT("NoBrickNodes"); return false; }
	if (Data.GroundNodeIds.Num() < 2) { OutError = TEXT("InsufficientGroundNodes"); return false; }

	constexpr float PenetrationToleranceCM = 0.25f;
	for (int32 A = 0; A < Data.Bricks.Num(); ++A)
	{
		const FBox BoxA = NodeBox(Data.Bricks[A]);
		for (int32 B = A + 1; B < Data.Bricks.Num(); ++B)
		{
			const FBox BoxB = NodeBox(Data.Bricks[B]);
			const float X = PositiveOverlap(BoxA.Min.X, BoxA.Max.X, BoxB.Min.X, BoxB.Max.X);
			const float Y = PositiveOverlap(BoxA.Min.Y, BoxA.Max.Y, BoxB.Min.Y, BoxB.Max.Y);
			const float Z = PositiveOverlap(BoxA.Min.Z, BoxA.Max.Z, BoxB.Min.Z, BoxB.Max.Z);
			if (X > PenetrationToleranceCM && Y > PenetrationToleranceCM && Z > PenetrationToleranceCM)
			{
				OutError = FString::Printf(TEXT("BrickPenetration:%d:%d:%.2f"), A, B, FMath::Min3(X, Y, Z));
				return false;
			}
		}
	}

	TMap<int32, bool> GroundPathCache;
	for (const FABTSM73BrickNode& Node : Data.Bricks)
	{
		TSet<int32> Visiting;
		if (!HasGroundPath(Node.NodeId, Data, Visiting, GroundPathCache))
		{
			OutError = FString::Printf(TEXT("NoGroundPath:%d"), Node.NodeId);
			return false;
		}

		if (Data.GroundNodeIds.Contains(Node.NodeId)) continue;
		float MinX = BIG_NUMBER;
		float MinY = BIG_NUMBER;
		float MaxX = -BIG_NUMBER;
		float MaxY = -BIG_NUMBER;
		float ContactArea = 0.0f;
		for (const FABTSM73SupportEdge& Edge : Data.SupportEdges)
		{
			if (Edge.UpperNodeId != Node.NodeId || !Data.Bricks.IsValidIndex(Edge.LowerNodeId)) continue;
			const FABTSM73BrickNode& Lower = Data.Bricks[Edge.LowerNodeId];
			const float XMin = FMath::Max(Node.LocalCenter.X - Node.DimensionsCM.X * 0.5f, Lower.LocalCenter.X - Lower.DimensionsCM.X * 0.5f);
			const float XMax = FMath::Min(Node.LocalCenter.X + Node.DimensionsCM.X * 0.5f, Lower.LocalCenter.X + Lower.DimensionsCM.X * 0.5f);
			const float YMin = FMath::Max(Node.LocalCenter.Y - Node.DimensionsCM.Y * 0.5f, Lower.LocalCenter.Y - Lower.DimensionsCM.Y * 0.5f);
			const float YMax = FMath::Min(Node.LocalCenter.Y + Node.DimensionsCM.Y * 0.5f, Lower.LocalCenter.Y + Lower.DimensionsCM.Y * 0.5f);
			if (XMax <= XMin || YMax <= YMin) continue;
			MinX = FMath::Min(MinX, XMin); MaxX = FMath::Max(MaxX, XMax);
			MinY = FMath::Min(MinY, YMin); MaxY = FMath::Max(MaxY, YMax);
			ContactArea += Edge.ContactAreaCM2;
		}
		const float BottomArea = FMath::Max(1.0f, Node.DimensionsCM.X * Node.DimensionsCM.Y);
		if (ContactArea / BottomArea < Settings.MinContactAreaRatio)
		{
			OutError = FString::Printf(TEXT("ContactAreaTooSmall:%d:%.3f"), Node.NodeId, ContactArea / BottomArea);
			return false;
		}
		if (Node.LocalCenter.X < MinX - KINDA_SMALL_NUMBER || Node.LocalCenter.X > MaxX + KINDA_SMALL_NUMBER
			|| Node.LocalCenter.Y < MinY - KINDA_SMALL_NUMBER || Node.LocalCenter.Y > MaxY + KINDA_SMALL_NUMBER)
		{
			OutError = FString::Printf(TEXT("COMOutsideSupport:%d"), Node.NodeId);
			return false;
		}
	}
	return true;
}

