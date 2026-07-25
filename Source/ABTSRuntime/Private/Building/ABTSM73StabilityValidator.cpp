// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73StabilityValidator.h"

#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73StructureData.h"

namespace
{
	FBox StabilityNodeBox(const FABTSM73BrickNode& Node)
	{
		return FBox(Node.LocalCenter - Node.DimensionsCM * 0.5f, Node.LocalCenter + Node.DimensionsCM * 0.5f);
	}

	float PositiveOverlap(const float MinA, const float MaxA, const float MinB, const float MaxB)
	{
		return FMath::Min(MaxA, MaxB) - FMath::Max(MinA, MinB);
	}

	float StabilityCross2D(const FVector2D& Origin, const FVector2D& A, const FVector2D& B)
	{
		return (A.X - Origin.X) * (B.Y - Origin.Y) - (A.Y - Origin.Y) * (B.X - Origin.X);
	}

	TArray<FVector2D> BuildConvexHull(TArray<FVector2D> Points)
	{
		Points.Sort([](const FVector2D& A, const FVector2D& B)
		{
			return !FMath::IsNearlyEqual(A.X, B.X) ? A.X < B.X : A.Y < B.Y;
		});
		TArray<FVector2D> UniquePoints;
		for (const FVector2D& Point : Points)
		{
			if (UniquePoints.IsEmpty() || !UniquePoints.Last().Equals(Point, KINDA_SMALL_NUMBER))
			{
				UniquePoints.Add(Point);
			}
		}
		Points = MoveTemp(UniquePoints);
		if (Points.Num() <= 2) return Points;
		TArray<FVector2D> Hull;
		for (const FVector2D& Point : Points)
		{
			while (Hull.Num() >= 2 && StabilityCross2D(Hull[Hull.Num() - 2], Hull.Last(), Point) <= KINDA_SMALL_NUMBER)
			{
				Hull.Pop();
			}
			Hull.Add(Point);
		}
		const int32 LowerCount = Hull.Num();
		for (int32 Index = Points.Num() - 2; Index >= 0; --Index)
		{
			const FVector2D& Point = Points[Index];
			while (Hull.Num() > LowerCount && StabilityCross2D(Hull[Hull.Num() - 2], Hull.Last(), Point) <= KINDA_SMALL_NUMBER)
			{
				Hull.Pop();
			}
			Hull.Add(Point);
		}
		Hull.Pop();
		return Hull;
	}

	bool IsInsideConvexHull(const FVector2D& Point, const TArray<FVector2D>& Hull)
	{
		if (Hull.Num() < 3) return false;
		bool bHasPositive = false;
		bool bHasNegative = false;
		for (int32 Index = 0; Index < Hull.Num(); ++Index)
		{
			const float Side = StabilityCross2D(Hull[Index], Hull[(Index + 1) % Hull.Num()], Point);
			bHasPositive |= Side > KINDA_SMALL_NUMBER;
			bHasNegative |= Side < -KINDA_SMALL_NUMBER;
			if (bHasPositive && bHasNegative) return false;
		}
		return true;
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
	// DAG-2 can lower a single wide Foundation plate. Legacy uses multiple ground columns,
	// but the physical FoundationCap already supplies the external ground support.
	if (Data.GroundNodeIds.IsEmpty()) { OutError = TEXT("NoGroundNodes"); return false; }

	constexpr float PenetrationToleranceCM = 0.25f;
	for (int32 A = 0; A < Data.Bricks.Num(); ++A)
	{
		const FBox BoxA = StabilityNodeBox(Data.Bricks[A]);
		for (int32 B = A + 1; B < Data.Bricks.Num(); ++B)
		{
			const FBox BoxB = StabilityNodeBox(Data.Bricks[B]);
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
	TMap<int32, const FABTSM73BrickNode*> NodesById;
	for (const FABTSM73BrickNode& Node : Data.Bricks) NodesById.Add(Node.NodeId, &Node);
	for (const FABTSM73BrickNode& Node : Data.Bricks)
	{
		TSet<int32> Visiting;
		if (!HasGroundPath(Node.NodeId, Data, Visiting, GroundPathCache))
		{
			OutError = FString::Printf(TEXT("NoGroundPath:%d"), Node.NodeId);
			return false;
		}

		if (Data.GroundNodeIds.Contains(Node.NodeId)) continue;
		TArray<FVector2D> ContactCorners;
		float ContactArea = 0.0f;
		for (const FABTSM73SupportEdge& Edge : Data.SupportEdges)
		{
			if (Edge.UpperNodeId != Node.NodeId) continue;
			const FABTSM73BrickNode* const* LowerPtr = NodesById.Find(Edge.LowerNodeId);
			if (LowerPtr == nullptr || *LowerPtr == nullptr) continue;
			const FABTSM73BrickNode& Lower = **LowerPtr;
			const float XMin = FMath::Max(Node.LocalCenter.X - Node.DimensionsCM.X * 0.5f, Lower.LocalCenter.X - Lower.DimensionsCM.X * 0.5f);
			const float XMax = FMath::Min(Node.LocalCenter.X + Node.DimensionsCM.X * 0.5f, Lower.LocalCenter.X + Lower.DimensionsCM.X * 0.5f);
			const float YMin = FMath::Max(Node.LocalCenter.Y - Node.DimensionsCM.Y * 0.5f, Lower.LocalCenter.Y - Lower.DimensionsCM.Y * 0.5f);
			const float YMax = FMath::Min(Node.LocalCenter.Y + Node.DimensionsCM.Y * 0.5f, Lower.LocalCenter.Y + Lower.DimensionsCM.Y * 0.5f);
			if (XMax <= XMin || YMax <= YMin) continue;
			ContactCorners.Add(FVector2D(XMin, YMin));
			ContactCorners.Add(FVector2D(XMax, YMin));
			ContactCorners.Add(FVector2D(XMin, YMax));
			ContactCorners.Add(FVector2D(XMax, YMax));
			ContactArea += Edge.ContactAreaCM2;
		}
		const float BottomArea = FMath::Max(1.0f, Node.DimensionsCM.X * Node.DimensionsCM.Y);
		const float RequiredContactAreaRatio = Data.DAGPhysicalSupportMappings.IsEmpty()
			? Settings.MinContactAreaRatio
			: Data.DAGMinSupportContactAreaRatio;
		if (ContactArea / BottomArea < RequiredContactAreaRatio)
		{
			OutError = FString::Printf(TEXT("ContactAreaTooSmall:%d:%.3f"), Node.NodeId, ContactArea / BottomArea);
			return false;
		}
		if (!IsInsideConvexHull(FVector2D(Node.LocalCenter.X, Node.LocalCenter.Y), BuildConvexHull(MoveTemp(ContactCorners))))
		{
			OutError = FString::Printf(TEXT("COMOutsideSupportHull:%d"), Node.NodeId);
			return false;
		}
	}
	return true;
}
