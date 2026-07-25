// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73PostFailureValidator.h"

#include "Building/ABTSM7MaterialProfileLibrary.h"
#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73StructureData.h"

namespace
{
	double FailureProbeNodeMass(
		const FABTSM73BrickNode& Node,
		const TConstArrayView<FABTSM7MaterialProfile> Profiles)
	{
		const FABTSM7MaterialProfile* Profile = FABTSM7MaterialProfileLibrary::FindProfile(Profiles, Node.Material);
		const double Density = Profile != nullptr ? FMath::Max(0.01f, Profile->DensityGPerCubicCM) : 1.0;
		const FVector D = Node.DimensionsCM.ComponentMax(FVector(1.0f));
		return static_cast<double>(D.X) * D.Y * D.Z * Density;
	}

	float Cross2D(const FVector2D& O, const FVector2D& A, const FVector2D& B)
	{
		return (A.X - O.X) * (B.Y - O.Y) - (A.Y - O.Y) * (B.X - O.X);
	}

	TArray<FVector2D> ConvexHull(TArray<FVector2D> Points)
	{
		Points.Sort([](const FVector2D& A, const FVector2D& B)
		{
			if (!FMath::IsNearlyEqual(A.X, B.X)) return A.X < B.X;
			return A.Y < B.Y;
		});
		for (int32 Index = Points.Num() - 1; Index > 0; --Index)
		{
			if (Points[Index].Equals(Points[Index - 1], KINDA_SMALL_NUMBER)) Points.RemoveAt(Index);
		}
		if (Points.Num() <= 2) return Points;
		TArray<FVector2D> Lower;
		for (const FVector2D& Point : Points)
		{
			while (Lower.Num() >= 2 && Cross2D(Lower[Lower.Num() - 2], Lower.Last(), Point) <= KINDA_SMALL_NUMBER) Lower.Pop();
			Lower.Add(Point);
		}
		TArray<FVector2D> Upper;
		for (int32 Index = Points.Num() - 1; Index >= 0; --Index)
		{
			const FVector2D& Point = Points[Index];
			while (Upper.Num() >= 2 && Cross2D(Upper[Upper.Num() - 2], Upper.Last(), Point) <= KINDA_SMALL_NUMBER) Upper.Pop();
			Upper.Add(Point);
		}
		Lower.Pop();
		Upper.Pop();
		Lower.Append(Upper);
		return Lower;
	}

	float PointSegmentDistance(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D Segment = B - A;
		const float Denominator = Segment.SizeSquared();
		const float T = Denominator > SMALL_NUMBER
			? FMath::Clamp(FVector2D::DotProduct(P - A, Segment) / Denominator, 0.0f, 1.0f)
			: 0.0f;
		return FVector2D::Distance(P, A + Segment * T);
	}

	/** Positive inside, negative outside. */
	float InsideMargin(const FVector2D& Point, const TArray<FVector2D>& Hull)
	{
		if (Hull.Num() < 3) return -BIG_NUMBER;
		bool bInside = true;
		float MinimumDistance = BIG_NUMBER;
		for (int32 Index = 0; Index < Hull.Num(); ++Index)
		{
			const FVector2D& A = Hull[Index];
			const FVector2D& B = Hull[(Index + 1) % Hull.Num()];
			if (Cross2D(A, B, Point) < -KINDA_SMALL_NUMBER) bInside = false;
			MinimumDistance = FMath::Min(MinimumDistance, PointSegmentDistance(Point, A, B));
		}
		return bInside ? MinimumDistance : -MinimumDistance;
	}

	const FABTSM73BrickNode* FindNode(const FABTSM73StructureData& Data, const int32 NodeId)
	{
		return Data.Bricks.FindByPredicate([NodeId](const FABTSM73BrickNode& Node){ return Node.NodeId == NodeId; });
	}

	void AddContactCorners(
		const FABTSM73BrickNode& Lower,
		const FABTSM73BrickNode& Upper,
		TArray<FVector2D>& OutPoints)
	{
		const float XMin = FMath::Max(Lower.LocalCenter.X - Lower.DimensionsCM.X * 0.5f,
			Upper.LocalCenter.X - Upper.DimensionsCM.X * 0.5f);
		const float XMax = FMath::Min(Lower.LocalCenter.X + Lower.DimensionsCM.X * 0.5f,
			Upper.LocalCenter.X + Upper.DimensionsCM.X * 0.5f);
		const float YMin = FMath::Max(Lower.LocalCenter.Y - Lower.DimensionsCM.Y * 0.5f,
			Upper.LocalCenter.Y - Upper.DimensionsCM.Y * 0.5f);
		const float YMax = FMath::Min(Lower.LocalCenter.Y + Lower.DimensionsCM.Y * 0.5f,
			Upper.LocalCenter.Y + Upper.DimensionsCM.Y * 0.5f);
		if (XMax <= XMin || YMax <= YMin) return;
		OutPoints.Add(FVector2D(XMin, YMin));
		OutPoints.Add(FVector2D(XMin, YMax));
		OutPoints.Add(FVector2D(XMax, YMin));
		OutPoints.Add(FVector2D(XMax, YMax));
	}

	void GatherDescendants(
		const FABTSM73StructureData& Data,
		const int32 RootNodeId,
		TArray<int32>& OutNodeIds)
	{
		TSet<int32> Visited;
		TArray<int32> Queue;
		Visited.Add(RootNodeId);
		Queue.Add(RootNodeId);
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			for (const FABTSM73SupportEdge& Edge : Data.SupportEdges)
			{
				if (Edge.LowerNodeId != Queue[Head] || Visited.Contains(Edge.UpperNodeId)) continue;
				Visited.Add(Edge.UpperNodeId);
				Queue.Add(Edge.UpperNodeId);
			}
		}
		OutNodeIds = MoveTemp(Queue);
		OutNodeIds.Sort();
	}

	bool ComputeMassProperties(
		const FABTSM73StructureData& Data,
		const TConstArrayView<FABTSM7MaterialProfile> Profiles,
		const TConstArrayView<int32> NodeIds,
		double& OutMass,
		FVector& OutCenterOfMass,
		float& OutBottomZ)
	{
		OutMass = 0.0;
		OutCenterOfMass = FVector::ZeroVector;
		OutBottomZ = BIG_NUMBER;
		for (const int32 NodeId : NodeIds)
		{
			const FABTSM73BrickNode* Node = FindNode(Data, NodeId);
			if (Node == nullptr) return false;
			const double Mass = FailureProbeNodeMass(*Node, Profiles);
			OutMass += Mass;
			OutCenterOfMass += Node->LocalCenter * Mass;
			OutBottomZ = FMath::Min(OutBottomZ, Node->LocalCenter.Z - Node->DimensionsCM.Z * 0.5f);
		}
		if (OutMass <= SMALL_NUMBER) return false;
		OutCenterOfMass /= OutMass;
		return true;
	}
}

bool FABTSM73PostFailureValidator::EvaluateAuthoredIntent(
	const FABTSM73DifficultySettings& Settings,
	const TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles,
	const FABTSM73StructureData& Data,
	const FABTSM73StructuralWeaknessIntent& Intent,
	FABTSM73FailureProbeResult& OutResult,
	FString& OutError) const
{
	OutResult = FABTSM73FailureProbeResult();
	OutError.Reset();
	OutResult.CandidateNodeId = Intent.CandidateNodeId;
	OutResult.CarrierNodeId = Intent.CarrierNodeId;
	OutResult.Pattern = Intent.Pattern;
	OutResult.CollapseMode = Intent.ExpectedCollapseMode;
	const FABTSM73BrickNode* Carrier = FindNode(Data, Intent.CarrierNodeId);
	const FABTSM73BrickNode* Candidate = FindNode(Data, Intent.CandidateNodeId);
	if (Carrier == nullptr || Candidate == nullptr)
	{
		OutError = TEXT("B2IntentNodeMissing");
		OutResult.RejectReason = OutError;
		return false;
	}

	GatherDescendants(Data, Intent.CarrierNodeId, OutResult.AffectedNodeIds);
	double AffectedMass = 0.0;
	float AffectedBottomZ = 0.0f;
	if (!ComputeMassProperties(Data, MaterialProfiles, OutResult.AffectedNodeIds,
		AffectedMass, OutResult.AffectedCenterOfMassLocal, AffectedBottomZ))
	{
		OutError = TEXT("B2AffectedMassInvalid");
		OutResult.RejectReason = OutError;
		return false;
	}
	double TotalMass = 0.0;
	for (const FABTSM73BrickNode& Node : Data.Bricks) TotalMass += FailureProbeNodeMass(Node, MaterialProfiles);
	OutResult.AffectedMassRatio = TotalMass > SMALL_NUMBER ? static_cast<float>(AffectedMass / TotalMass) : 0.0f;

	TArray<FVector2D> FullContactPoints;
	TArray<FVector2D> RemainingContactPoints;
	for (const FABTSM73SupportEdge& Edge : Data.SupportEdges)
	{
		if (Edge.UpperNodeId != Intent.CarrierNodeId || !Intent.DirectSupportNodeIds.Contains(Edge.LowerNodeId)) continue;
		const FABTSM73BrickNode* Lower = FindNode(Data, Edge.LowerNodeId);
		if (Lower == nullptr) continue;
		AddContactCorners(*Lower, *Carrier, FullContactPoints);
		if (Lower->NodeId != Intent.CandidateNodeId) AddContactCorners(*Lower, *Carrier, RemainingContactPoints);
	}
	const TArray<FVector2D> FullHull = ConvexHull(MoveTemp(FullContactPoints));
	const TArray<FVector2D> RemainingHull = ConvexHull(MoveTemp(RemainingContactPoints));
	if (FullHull.Num() < 3)
	{
		OutError = TEXT("B2FullSupportHullDegenerate");
		OutResult.RejectReason = OutError;
		return false;
	}
	if (RemainingHull.Num() < 3)
	{
		OutError = TEXT("B2RemainingSupportHullDegenerate");
		OutResult.RejectReason = OutError;
		return false;
	}
	const FVector2D LoadCOM(OutResult.AffectedCenterOfMassLocal.X, OutResult.AffectedCenterOfMassLocal.Y);
	OutResult.InitialSupportMarginCM = InsideMargin(LoadCOM, FullHull);
	const float RemainingInsideMargin = InsideMargin(LoadCOM, RemainingHull);
	OutResult.TipMarginCM = -RemainingInsideMargin;

	FVector2D RemainingHullCenter = FVector2D::ZeroVector;
	for (const FVector2D& Point : RemainingHull) RemainingHullCenter += Point;
	RemainingHullCenter /= RemainingHull.Num();
	const FVector ExpectedTipDirection = Intent.ExpectedTipDirectionLocal.GetSafeNormal();
	FVector PredictedTipDirection = FVector(LoadCOM - RemainingHullCenter, 0.0f).GetSafeNormal();
	if (PredictedTipDirection.IsNearlyZero()) PredictedTipDirection = ExpectedTipDirection;
	OutResult.TipDirectionLocal = PredictedTipDirection;
	const float ExpectedDirectionAlignment = ExpectedTipDirection.IsNearlyZero()
		? 1.0f
		: FVector::DotProduct(PredictedTipDirection, ExpectedTipDirection);
	const float AlignmentRisk = EstimateVerticalReseatRisk(
		MaterialProfiles, Data, Intent.CandidateNodeId, OutResult.AffectedNodeIds);
	const float TipFeasibility = FMath::Clamp(
		OutResult.TipMarginCM / FMath::Max(1.0f, Settings.MinTipMarginCM * 2.0f), 0.0f, 1.0f);
	OutResult.ReseatRisk = AlignmentRisk * (1.0f - TipFeasibility);
	OutResult.bWouldReseat = OutResult.ReseatRisk > Settings.MaxReseatRisk;

	if (OutResult.InitialSupportMarginCM < Settings.MinInitialSupportMarginCM)
	{
		OutError = FString::Printf(TEXT("B2InitialSupportMarginTooSmall:%.3f:%.3f"),
			OutResult.InitialSupportMarginCM, Settings.MinInitialSupportMarginCM);
	}
	else if (OutResult.TipMarginCM < Settings.MinTipMarginCM)
	{
		OutError = FString::Printf(TEXT("B2TipMarginTooSmall:%.3f:%.3f"), OutResult.TipMarginCM, Settings.MinTipMarginCM);
	}
	else if (ExpectedDirectionAlignment < 0.25f)
	{
		OutError = FString::Printf(TEXT("B2TipDirectionMismatch:%.3f:0.250"), ExpectedDirectionAlignment);
	}
	else if (OutResult.ReseatRisk > Settings.MaxReseatRisk)
	{
		OutError = FString::Printf(TEXT("B2ReseatRiskTooHigh:%.3f:%.3f"), OutResult.ReseatRisk, Settings.MaxReseatRisk);
	}
	if (!OutError.IsEmpty())
	{
		OutResult.RejectReason = OutError;
		return false;
	}
	OutResult.bValid = true;
	return true;
}

float FABTSM73PostFailureValidator::EstimateVerticalReseatRisk(
	const TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles,
	const FABTSM73StructureData& Data,
	const int32 RemovedNodeId,
	const TConstArrayView<int32> FallingNodeIds) const
{
	if (FallingNodeIds.IsEmpty()) return 0.0f;
	double FallingMass = 0.0;
	FVector FallingCOM = FVector::ZeroVector;
	float FallingBottomZ = 0.0f;
	if (!ComputeMassProperties(Data, MaterialProfiles, FallingNodeIds, FallingMass, FallingCOM, FallingBottomZ)) return 1.0f;
	TSet<int32> FallingSet;
	for (const int32 NodeId : FallingNodeIds) FallingSet.Add(NodeId);
	TArray<const FABTSM73BrickNode*> LowestFallingNodes;
	for (const int32 NodeId : FallingNodeIds)
	{
		const FABTSM73BrickNode* Node = FindNode(Data, NodeId);
		if (Node == nullptr) continue;
		const float BottomZ = Node->LocalCenter.Z - Node->DimensionsCM.Z * 0.5f;
		if (FMath::Abs(BottomZ - FallingBottomZ) <= 1.5f) LowestFallingNodes.Add(Node);
	}
	float HighestLandingZ = -BIG_NUMBER;
	for (const FABTSM73BrickNode* FallingNode : LowestFallingNodes)
	{
		for (const FABTSM73BrickNode& Node : Data.Bricks)
		{
			if (Node.NodeId == RemovedNodeId || FallingSet.Contains(Node.NodeId)) continue;
			const float TopZ = Node.LocalCenter.Z + Node.DimensionsCM.Z * 0.5f;
			if (TopZ > FallingBottomZ + KINDA_SMALL_NUMBER) continue;
			const float XOverlap = FMath::Min(
				FallingNode->LocalCenter.X + FallingNode->DimensionsCM.X * 0.5f,
				Node.LocalCenter.X + Node.DimensionsCM.X * 0.5f)
				- FMath::Max(FallingNode->LocalCenter.X - FallingNode->DimensionsCM.X * 0.5f,
					Node.LocalCenter.X - Node.DimensionsCM.X * 0.5f);
			const float YOverlap = FMath::Min(
				FallingNode->LocalCenter.Y + FallingNode->DimensionsCM.Y * 0.5f,
				Node.LocalCenter.Y + Node.DimensionsCM.Y * 0.5f)
				- FMath::Max(FallingNode->LocalCenter.Y - FallingNode->DimensionsCM.Y * 0.5f,
					Node.LocalCenter.Y - Node.DimensionsCM.Y * 0.5f);
			if (XOverlap > KINDA_SMALL_NUMBER && YOverlap > KINDA_SMALL_NUMBER)
			{
				HighestLandingZ = FMath::Max(HighestLandingZ, TopZ);
			}
		}
	}
	if (HighestLandingZ <= -BIG_NUMBER) return 0.0f;
	TArray<FVector2D> ContactPoints;
	for (const FABTSM73BrickNode* FallingNode : LowestFallingNodes)
	{
		for (const FABTSM73BrickNode& Node : Data.Bricks)
		{
			if (Node.NodeId == RemovedNodeId || FallingSet.Contains(Node.NodeId)) continue;
			const float TopZ = Node.LocalCenter.Z + Node.DimensionsCM.Z * 0.5f;
			if (!FMath::IsNearlyEqual(TopZ, HighestLandingZ, 1.5f)) continue;
			AddContactCorners(Node, *FallingNode, ContactPoints);
		}
	}
	const TArray<FVector2D> LandingHull = ConvexHull(MoveTemp(ContactPoints));
	return InsideMargin(FVector2D(FallingCOM.X, FallingCOM.Y), LandingHull) >= 0.0f ? 1.0f : 0.0f;
}
