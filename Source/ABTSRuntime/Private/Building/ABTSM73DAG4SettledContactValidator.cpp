// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAG4SettledContactValidator.h"

namespace
{
	constexpr float MinimumSupportNormalDot = 0.90f;
	constexpr float PolygonTolerance = 1.0e-3f;
	constexpr float ContactHeightSampleToleranceCM = 1.5f;

	struct FSettledFace
	{
		FVector Normal = FVector::ZeroVector;
		TArray<FVector> Vertices;
		TArray<FVector2D> ProjectedHull;
	};

	struct FSettledNodeGeometry
	{
		const FABTSM73DAG4SettledNode* Node = nullptr;
		FSettledFace TopFace;
		FSettledFace BottomFace;
	};

	uint64 MakeSettledEdgeKey(
		const int32 LowerNodeId,
		const int32 UpperNodeId)
	{
		return
			(static_cast<uint64>(static_cast<uint32>(LowerNodeId)) << 32)
			| static_cast<uint32>(UpperNodeId);
	}

	bool IsFiniteSettledVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}

	bool IsFiniteSettledQuat(const FQuat& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z)
			&& FMath::IsFinite(Value.W);
	}

	float Cross2D(
		const FVector2D& Origin,
		const FVector2D& A,
		const FVector2D& B)
	{
		return (A.X - Origin.X) * (B.Y - Origin.Y)
			- (A.Y - Origin.Y) * (B.X - Origin.X);
	}

	float CrossVectors2D(const FVector2D& A, const FVector2D& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	void SortUniqueSettledIds(TArray<int32>& NodeIds)
	{
		NodeIds.Sort();
		for (int32 Index = NodeIds.Num() - 1; Index > 0; --Index)
		{
			if (NodeIds[Index] == NodeIds[Index - 1])
			{
				NodeIds.RemoveAt(Index, 1, EAllowShrinking::No);
			}
		}
	}

	TArray<FVector2D> BuildConvexHull(TArray<FVector2D> Points)
	{
		Points.Sort([](const FVector2D& A, const FVector2D& B)
		{
			return A.X != B.X ? A.X < B.X : A.Y < B.Y;
		});
		for (int32 Index = Points.Num() - 1; Index > 0; --Index)
		{
			if (Points[Index].Equals(
				Points[Index - 1],
				PolygonTolerance))
			{
				Points.RemoveAt(Index, 1, EAllowShrinking::No);
			}
		}
		if (Points.Num() <= 2)
		{
			return Points;
		}

		TArray<FVector2D> Lower;
		for (const FVector2D& Point : Points)
		{
			while (Lower.Num() >= 2
				&& Cross2D(
					Lower[Lower.Num() - 2],
					Lower.Last(),
					Point) <= PolygonTolerance)
			{
				Lower.Pop(EAllowShrinking::No);
			}
			Lower.Add(Point);
		}
		TArray<FVector2D> Upper;
		for (int32 Index = Points.Num() - 1; Index >= 0; --Index)
		{
			const FVector2D& Point = Points[Index];
			while (Upper.Num() >= 2
				&& Cross2D(
					Upper[Upper.Num() - 2],
					Upper.Last(),
					Point) <= PolygonTolerance)
			{
				Upper.Pop(EAllowShrinking::No);
			}
			Upper.Add(Point);
		}
		Lower.Pop(EAllowShrinking::No);
		Upper.Pop(EAllowShrinking::No);
		Lower.Append(Upper);
		return Lower;
	}

	float PolygonArea(const TConstArrayView<FVector2D> Polygon)
	{
		if (Polygon.Num() < 3)
		{
			return 0.0f;
		}
		double TwiceArea = 0.0;
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			const FVector2D& A = Polygon[Index];
			const FVector2D& B = Polygon[(Index + 1) % Polygon.Num()];
			TwiceArea += static_cast<double>(A.X) * B.Y
				- static_cast<double>(A.Y) * B.X;
		}
		return static_cast<float>(FMath::Abs(TwiceArea) * 0.5);
	}

	FVector2D PolygonCentroid(const TConstArrayView<FVector2D> Polygon)
	{
		if (Polygon.IsEmpty())
		{
			return FVector2D::ZeroVector;
		}
		FVector2D Sum = FVector2D::ZeroVector;
		for (const FVector2D& Point : Polygon)
		{
			Sum += Point;
		}
		return Sum / Polygon.Num();
	}

	bool IsInsideClipEdge(
		const FVector2D& Point,
		const FVector2D& A,
		const FVector2D& B)
	{
		return Cross2D(A, B, Point) >= -PolygonTolerance;
	}

	FVector2D IntersectClipLine(
		const FVector2D& Start,
		const FVector2D& End,
		const FVector2D& A,
		const FVector2D& B)
	{
		const FVector2D Segment = End - Start;
		const FVector2D ClipEdge = B - A;
		const float Denominator = CrossVectors2D(ClipEdge, Segment);
		if (FMath::Abs(Denominator) <= SMALL_NUMBER)
		{
			return (Start + End) * 0.5f;
		}
		const float Alpha = FMath::Clamp(
			CrossVectors2D(ClipEdge, A - Start) / Denominator,
			0.0f,
			1.0f);
		return Start + Segment * Alpha;
	}

	TArray<FVector2D> IntersectConvexPolygons(
		const TConstArrayView<FVector2D> SubjectPolygon,
		const TConstArrayView<FVector2D> ClipPolygon)
	{
		TArray<FVector2D> Output;
		Output.Append(SubjectPolygon.GetData(), SubjectPolygon.Num());
		if (Output.Num() < 3 || ClipPolygon.Num() < 3)
		{
			return {};
		}
		for (int32 ClipIndex = 0;
			ClipIndex < ClipPolygon.Num();
			++ClipIndex)
		{
			const FVector2D ClipA = ClipPolygon[ClipIndex];
			const FVector2D ClipB =
				ClipPolygon[(ClipIndex + 1) % ClipPolygon.Num()];
			TArray<FVector2D> Input = MoveTemp(Output);
			Output.Reset();
			if (Input.IsEmpty())
			{
				break;
			}
			FVector2D Previous = Input.Last();
			bool bPreviousInside =
				IsInsideClipEdge(Previous, ClipA, ClipB);
			for (const FVector2D& Current : Input)
			{
				const bool bCurrentInside =
					IsInsideClipEdge(Current, ClipA, ClipB);
				if (bCurrentInside != bPreviousInside)
				{
					Output.Add(IntersectClipLine(
						Previous,
						Current,
						ClipA,
						ClipB));
				}
				if (bCurrentInside)
				{
					Output.Add(Current);
				}
				Previous = Current;
				bPreviousInside = bCurrentInside;
			}
		}
		return BuildConvexHull(MoveTemp(Output));
	}

	float PointSegmentDistance(
		const FVector2D& Point,
		const FVector2D& A,
		const FVector2D& B)
	{
		const FVector2D Segment = B - A;
		const float Denominator = Segment.SizeSquared();
		const float Alpha = Denominator > SMALL_NUMBER
			? FMath::Clamp(
				FVector2D::DotProduct(Point - A, Segment) / Denominator,
				0.0f,
				1.0f)
			: 0.0f;
		return FVector2D::Distance(Point, A + Segment * Alpha);
	}

	/** Positive inside and negative outside one canonical CCW hull. */
	float SignedInsideMargin(
		const FVector2D& Point,
		const TConstArrayView<FVector2D> Hull)
	{
		if (Hull.Num() < 3)
		{
			return -BIG_NUMBER;
		}
		bool bInside = true;
		float MinimumDistance = BIG_NUMBER;
		for (int32 Index = 0; Index < Hull.Num(); ++Index)
		{
			const FVector2D& A = Hull[Index];
			const FVector2D& B = Hull[(Index + 1) % Hull.Num()];
			if (Cross2D(A, B, Point) < -PolygonTolerance)
			{
				bInside = false;
			}
			MinimumDistance = FMath::Min(
				MinimumDistance,
				PointSegmentDistance(Point, A, B));
		}
		return bInside ? MinimumDistance : -MinimumDistance;
	}

	FSettledFace BuildSupportFace(
		const FABTSM73DAG4SettledNode& Node,
		const bool bTop)
	{
		FSettledFace Face;
		const FQuat Rotation = Node.LocalTransform.GetRotation();
		const FVector Axes[] = {
			Rotation.GetAxisX(),
			Rotation.GetAxisY(),
			Rotation.GetAxisZ()
		};
		const FVector Half = Node.DimensionsCM * 0.5f;
		int32 SupportAxis = 0;
		for (int32 Axis = 1; Axis < 3; ++Axis)
		{
			if (FMath::Abs(Axes[Axis].Z)
				> FMath::Abs(Axes[SupportAxis].Z))
			{
				SupportAxis = Axis;
			}
		}
		const float UpSign = Axes[SupportAxis].Z >= 0.0f ? 1.0f : -1.0f;
		const float FaceSign = bTop ? UpSign : -UpSign;
		Face.Normal = Axes[SupportAxis] * FaceSign;

		int32 OtherAxes[2] = {INDEX_NONE, INDEX_NONE};
		int32 OtherCount = 0;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Axis != SupportAxis)
			{
				OtherAxes[OtherCount++] = Axis;
			}
		}
		const FVector Center = Node.LocalTransform.GetLocation();
		for (int32 ASign = -1; ASign <= 1; ASign += 2)
		{
			for (int32 BSign = -1; BSign <= 1; BSign += 2)
			{
				const FVector Point =
					Center
					+ Axes[SupportAxis] * Half[SupportAxis] * FaceSign
					+ Axes[OtherAxes[0]]
						* Half[OtherAxes[0]]
						* static_cast<float>(ASign)
					+ Axes[OtherAxes[1]]
						* Half[OtherAxes[1]]
						* static_cast<float>(BSign);
				Face.Vertices.Add(Point);
				Face.ProjectedHull.Add(FVector2D(Point.X, Point.Y));
			}
		}
		Face.ProjectedHull =
			BuildConvexHull(MoveTemp(Face.ProjectedHull));
		return Face;
	}

	bool PlaneHeightAt(
		const FSettledFace& Face,
		const FVector2D& Point,
		float& OutHeight)
	{
		if (Face.Vertices.IsEmpty()
			|| FMath::Abs(Face.Normal.Z) < MinimumSupportNormalDot)
		{
			return false;
		}
		const FVector& PlanePoint = Face.Vertices[0];
		OutHeight =
			PlanePoint.Z
			- (Face.Normal.X * (Point.X - PlanePoint.X)
				+ Face.Normal.Y * (Point.Y - PlanePoint.Y))
				/ Face.Normal.Z;
		return FMath::IsFinite(OutHeight);
	}

	float AverageFaceHeight(const FSettledFace& Face)
	{
		if (Face.Vertices.IsEmpty())
		{
			return 0.0f;
		}
		float Sum = 0.0f;
		for (const FVector& Vertex : Face.Vertices)
		{
			Sum += Vertex.Z;
		}
		return Sum / Face.Vertices.Num();
	}

	bool TryBuildSupportContact(
		const FABTSM73DAG4ValidationSettings& Settings,
		const FSettledNodeGeometry& A,
		const FSettledNodeGeometry& B,
		FABTSM73DAG4SettledContact& OutContact)
	{
		const FSettledNodeGeometry* Lower = &A;
		const FSettledNodeGeometry* Upper = &B;
		if (A.Node->LocalTransform.GetLocation().Z
			> B.Node->LocalTransform.GetLocation().Z)
		{
			Swap(Lower, Upper);
		}
		const float CenterHeightDelta =
			Upper->Node->LocalTransform.GetLocation().Z
			- Lower->Node->LocalTransform.GetLocation().Z;
		if (CenterHeightDelta <= PolygonTolerance
			|| Lower->TopFace.Normal.Z < MinimumSupportNormalDot
			|| Upper->BottomFace.Normal.Z > -MinimumSupportNormalDot)
		{
			return false;
		}

		TArray<FVector2D> Patch = IntersectConvexPolygons(
			Lower->TopFace.ProjectedHull,
			Upper->BottomFace.ProjectedHull);
		const float Area = PolygonArea(Patch);
		if (Area + PolygonTolerance < Settings.MinContactPatchAreaCM2)
		{
			return false;
		}

		TArray<FVector2D> GapSamples = Patch;
		GapSamples.Add(PolygonCentroid(Patch));
		float GapSum = 0.0f;
		float MaximumGap = -BIG_NUMBER;
		float MinimumGap = BIG_NUMBER;
		for (const FVector2D& Point : GapSamples)
		{
			float LowerHeight = 0.0f;
			float UpperHeight = 0.0f;
			if (!PlaneHeightAt(Lower->TopFace, Point, LowerHeight)
				|| !PlaneHeightAt(Upper->BottomFace, Point, UpperHeight))
			{
				return false;
			}
			const float Gap = UpperHeight - LowerHeight;
			GapSum += Gap;
			MaximumGap = FMath::Max(MaximumGap, Gap);
			MinimumGap = FMath::Min(MinimumGap, Gap);
		}
		if (MaximumGap > Settings.ContactGapToleranceCM
			|| MinimumGap < -Settings.ContactPenetrationToleranceCM)
		{
			return false;
		}

		OutContact = FABTSM73DAG4SettledContact();
		OutContact.LowerNodeId = Lower->Node->NodeId;
		OutContact.UpperNodeId = Upper->Node->NodeId;
		OutContact.ContactAreaCM2 = Area;
		OutContact.SignedGapCM = GapSum / GapSamples.Num();
		OutContact.PatchVertices = MoveTemp(Patch);
		return true;
	}

	bool IsGroundContact(
		const FABTSM73DAG4ValidationSettings& Settings,
		const FSettledNodeGeometry& Geometry)
	{
		if (Geometry.BottomFace.Normal.Z > -MinimumSupportNormalDot
			|| PolygonArea(Geometry.BottomFace.ProjectedHull)
				+ PolygonTolerance < Settings.MinContactPatchAreaCM2)
		{
			return false;
		}
		TArray<FVector2D> Samples = Geometry.BottomFace.ProjectedHull;
		Samples.Add(PolygonCentroid(Geometry.BottomFace.ProjectedHull));
		for (const FVector2D& Point : Samples)
		{
			float Height = 0.0f;
			if (!PlaneHeightAt(Geometry.BottomFace, Point, Height)
				|| Height > Settings.ContactGapToleranceCM
				|| Height < -Settings.ContactPenetrationToleranceCM)
			{
				return false;
			}
		}
		return true;
	}

	void GatherGroundReachable(
		const TConstArrayView<FABTSM73DAG4SettledContact> Contacts,
		const TConstArrayView<int32> GroundNodeIds,
		const TSet<int32>& RemovedNodeIds,
		TSet<int32>& OutReachable)
	{
		OutReachable.Reset();
		TArray<int32> Queue;
		for (const int32 GroundNodeId : GroundNodeIds)
		{
			if (!RemovedNodeIds.Contains(GroundNodeId)
				&& !OutReachable.Contains(GroundNodeId))
			{
				OutReachable.Add(GroundNodeId);
				Queue.Add(GroundNodeId);
			}
		}
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			for (const FABTSM73DAG4SettledContact& Contact : Contacts)
			{
				if (Contact.LowerNodeId != Queue[Head]
					|| RemovedNodeIds.Contains(Contact.UpperNodeId)
					|| OutReachable.Contains(Contact.UpperNodeId))
				{
					continue;
				}
				OutReachable.Add(Contact.UpperNodeId);
				Queue.Add(Contact.UpperNodeId);
			}
		}
	}

	bool ContainsDuplicateSettledIds(TArray<int32> NodeIds)
	{
		const int32 OriginalCount = NodeIds.Num();
		SortUniqueSettledIds(NodeIds);
		return NodeIds.Num() != OriginalCount;
	}

	bool ValidateSettings(
		const FABTSM73DAG4ValidationSettings& Settings,
		FString& OutError)
	{
		const bool bValid =
			FMath::IsFinite(Settings.ContactGapToleranceCM)
			&& Settings.ContactGapToleranceCM > 0.0f
			&& FMath::IsFinite(Settings.ContactPenetrationToleranceCM)
			&& Settings.ContactPenetrationToleranceCM > 0.0f
			&& FMath::IsFinite(Settings.MinContactPatchAreaCM2)
			&& Settings.MinContactPatchAreaCM2 > 0.0f
			&& FMath::IsFinite(Settings.MinRequiredContactAreaRetention)
			&& Settings.MinRequiredContactAreaRetention > 0.0f
			&& Settings.MinRequiredContactAreaRetention <= 1.0f
			&& FMath::IsFinite(
				Settings.MinFailureDirectionAlignment)
			&& Settings.MinFailureDirectionAlignment >= -1.0f
			&& Settings.MinFailureDirectionAlignment <= 1.0f
			&& Settings.MaxSettledBodyCount >= 1
			&& Settings.MaxContactPairQueryCount >= 1;
		if (!bValid)
		{
			OutError = TEXT("DAG4SettledSettingsInvalid");
		}
		return bValid;
	}

	bool ValidateEdgeArray(
		const TCHAR* ErrorPrefix,
		const TConstArrayView<FABTSM73SupportEdge> Edges,
		const TSet<int32>& NodeIds,
		TSet<uint64>& OutEdgeKeys,
		FString& OutError)
	{
		OutEdgeKeys.Reset();
		for (const FABTSM73SupportEdge& Edge : Edges)
		{
			if (Edge.LowerNodeId == Edge.UpperNodeId
				|| !NodeIds.Contains(Edge.LowerNodeId)
				|| !NodeIds.Contains(Edge.UpperNodeId)
				|| !FMath::IsFinite(Edge.ContactAreaCM2)
				|| Edge.ContactAreaCM2 <= 0.0f)
			{
				OutError = FString::Printf(
					TEXT("%sInvalid:%d:%d"),
					ErrorPrefix,
					Edge.LowerNodeId,
					Edge.UpperNodeId);
				return false;
			}
			const uint64 Key =
				MakeSettledEdgeKey(Edge.LowerNodeId, Edge.UpperNodeId);
			if (OutEdgeKeys.Contains(Key))
			{
				OutError = FString::Printf(
					TEXT("%sDuplicate:%d:%d"),
					ErrorPrefix,
					Edge.LowerNodeId,
					Edge.UpperNodeId);
				return false;
			}
			OutEdgeKeys.Add(Key);
		}
		return true;
	}

	uint32 EnsureNonZeroSettledHash(const uint32 Hash)
	{
		return Hash != 0 ? Hash : 1u;
	}

	int32 Quantize(const float Value, const float Scale = 1000.0f)
	{
		return FMath::RoundToInt(Value * Scale);
	}

	void AddSettledHashValue(uint32& Hash, const uint32 Value)
	{
		Hash = HashCombineFast(Hash, Value);
	}

	void AddCanonicalIdArray(
		uint32& Hash,
		const uint32 DomainTag,
		TArray<int32> NodeIds)
	{
		NodeIds.Sort();
		AddSettledHashValue(Hash, DomainTag);
		AddSettledHashValue(Hash, GetTypeHash(NodeIds.Num()));
		for (const int32 NodeId : NodeIds)
		{
			AddSettledHashValue(Hash, GetTypeHash(NodeId));
		}
	}

	uint32 BuildBaselineHash(
		TArray<FABTSM73SupportEdge> AllowedEdges,
		TArray<FABTSM73SupportEdge> RequiredEdges,
		TArray<int32> GroundNodeIds)
	{
		const auto SortEdges = [](TArray<FABTSM73SupportEdge>& Edges)
		{
			Edges.Sort([](
				const FABTSM73SupportEdge& A,
				const FABTSM73SupportEdge& B)
			{
				return A.LowerNodeId != B.LowerNodeId
					? A.LowerNodeId < B.LowerNodeId
					: A.UpperNodeId < B.UpperNodeId;
			});
		};
		SortEdges(AllowedEdges);
		SortEdges(RequiredEdges);
		SortUniqueSettledIds(GroundNodeIds);
		uint32 Hash = 0xD470BA5Eu;
		AddSettledHashValue(Hash, GetTypeHash(GroundNodeIds.Num()));
		for (const int32 GroundNodeId : GroundNodeIds)
		{
			AddSettledHashValue(Hash, 0x47000000u);
			AddSettledHashValue(Hash, GetTypeHash(GroundNodeId));
		}
		AddSettledHashValue(Hash, GetTypeHash(AllowedEdges.Num()));
		for (const FABTSM73SupportEdge& Edge : AllowedEdges)
		{
			AddSettledHashValue(Hash, 0xA1100000u);
			AddSettledHashValue(Hash, GetTypeHash(Edge.LowerNodeId));
			AddSettledHashValue(Hash, GetTypeHash(Edge.UpperNodeId));
			AddSettledHashValue(
				Hash,
				GetTypeHash(Quantize(Edge.ContactAreaCM2)));
		}
		AddSettledHashValue(Hash, GetTypeHash(RequiredEdges.Num()));
		for (const FABTSM73SupportEdge& Edge : RequiredEdges)
		{
			AddSettledHashValue(Hash, 0x5EED0000u);
			AddSettledHashValue(Hash, GetTypeHash(Edge.LowerNodeId));
			AddSettledHashValue(Hash, GetTypeHash(Edge.UpperNodeId));
			AddSettledHashValue(
				Hash,
				GetTypeHash(Quantize(Edge.ContactAreaCM2)));
		}
		return EnsureNonZeroSettledHash(Hash);
	}

	uint32 BuildSettledHash(
		const FABTSM73DAG4ValidationSettings& Settings,
		const FABTSM73DAG4SettledContactInput& Input,
		const FABTSM73DAG4SettledContactResult& Result)
	{
		TArray<const FABTSM73DAG4SettledNode*> SortedNodes;
		for (const FABTSM73DAG4SettledNode& Node : Input.Nodes)
		{
			SortedNodes.Add(&Node);
		}
		SortedNodes.Sort([](
			const FABTSM73DAG4SettledNode& A,
			const FABTSM73DAG4SettledNode& B)
		{
			return A.NodeId < B.NodeId;
		});
		uint32 Hash = 0xD475E771u;
		AddSettledHashValue(
			Hash,
			GetTypeHash(Quantize(Settings.ContactGapToleranceCM)));
		AddSettledHashValue(Hash, GetTypeHash(
			Quantize(Settings.ContactPenetrationToleranceCM)));
		AddSettledHashValue(Hash, GetTypeHash(
			Quantize(Settings.MinContactPatchAreaCM2)));
		AddSettledHashValue(Hash, GetTypeHash(
			Quantize(Settings.MinRequiredContactAreaRetention)));
		AddSettledHashValue(Hash, static_cast<uint32>(Input.Pattern));
		AddSettledHashValue(
			Hash,
			static_cast<uint32>(Input.ExpectedMotion));
		AddSettledHashValue(Hash, Input.RealizedPatternHash);
		AddSettledHashValue(Hash, GetTypeHash(Input.LoadPlateNodeId));
		AddSettledHashValue(Hash, GetTypeHash(
			Quantize(Input.ExpectedFailureDirectionLocal.X)));
		AddSettledHashValue(Hash, GetTypeHash(
			Quantize(Input.ExpectedFailureDirectionLocal.Y)));
		AddSettledHashValue(Hash, GetTypeHash(
			Quantize(Input.ExpectedFailureDirectionLocal.Z)));
		AddSettledHashValue(Hash, GetTypeHash(
			Quantize(Input.MinInitialSupportMarginCM)));
		AddSettledHashValue(Hash, GetTypeHash(
			Quantize(Input.MinPostFailureTipMarginCM)));
		AddSettledHashValue(
			Hash,
			GetTypeHash(Quantize(Input.MaxReseatRisk)));
		AddCanonicalIdArray(
			Hash,
			0x5745414Bu,
			Input.WeakNodeIds);
		AddCanonicalIdArray(
			Hash,
			0x5049564Fu,
			Input.RemainingSupportNodeIds);
		AddCanonicalIdArray(
			Hash,
			0x41464645u,
			Input.ExpectedAffectedNodeIds);
		AddCanonicalIdArray(
			Hash,
			0x4D41494Eu,
			Input.ExpectedAffectedMainBodyNodeIds);
		AddSettledHashValue(Hash, Result.BaselineContactHash);
		AddSettledHashValue(Hash, GetTypeHash(SortedNodes.Num()));
		for (const FABTSM73DAG4SettledNode* Node : SortedNodes)
		{
			AddSettledHashValue(Hash, GetTypeHash(Node->NodeId));
			AddSettledHashValue(Hash, GetTypeHash(Node->MacroNodeId));
			AddSettledHashValue(
				Hash,
				static_cast<uint32>(Node->Material));
			AddSettledHashValue(Hash, Node->bMainBody ? 1u : 0u);
			AddSettledHashValue(
				Hash,
				GetTypeHash(Quantize(Node->DimensionsCM.X)));
			AddSettledHashValue(
				Hash,
				GetTypeHash(Quantize(Node->DimensionsCM.Y)));
			AddSettledHashValue(
				Hash,
				GetTypeHash(Quantize(Node->DimensionsCM.Z)));
		}
		AddSettledHashValue(
			Hash,
			GetTypeHash(Result.GroundNodeIds.Num()));
		for (const int32 GroundNodeId : Result.GroundNodeIds)
		{
			AddSettledHashValue(Hash, 0x47000000u);
			AddSettledHashValue(Hash, GetTypeHash(GroundNodeId));
		}
		AddSettledHashValue(Hash, GetTypeHash(Result.Contacts.Num()));
		for (const FABTSM73DAG4SettledContact& Contact : Result.Contacts)
		{
			AddSettledHashValue(
				Hash,
				GetTypeHash(Contact.LowerNodeId));
			AddSettledHashValue(
				Hash,
				GetTypeHash(Contact.UpperNodeId));
		}
		return EnsureNonZeroSettledHash(Hash);
	}

	bool ComputeAffectedCenterOfMass(
		const FABTSM73DAG4SettledContactInput& Input,
		const TMap<int32, const FABTSM73DAG4SettledNode*>& NodesById,
		FVector& OutCenterOfMass)
	{
		double TotalMass = 0.0;
		OutCenterOfMass = FVector::ZeroVector;
		TArray<int32> SortedAffectedNodeIds =
			Input.ExpectedAffectedNodeIds;
		SortedAffectedNodeIds.Sort();
		for (const int32 NodeId : SortedAffectedNodeIds)
		{
			const FABTSM73DAG4SettledNode* const* Found =
				NodesById.Find(NodeId);
			if (Found == nullptr || *Found == nullptr)
			{
				return false;
			}
			const FABTSM73DAG4SettledNode& Node = **Found;
			TotalMass += Node.Mass;
			OutCenterOfMass +=
				Node.LocalTransform.GetLocation() * Node.Mass;
		}
		if (!FMath::IsFinite(TotalMass)
			|| TotalMass <= SMALL_NUMBER)
		{
			return false;
		}
		OutCenterOfMass /= TotalMass;
		return IsFiniteSettledVector(OutCenterOfMass);
	}

	bool EstimateSettledReseatRisk(
		const FABTSM73DAG4ValidationSettings& Settings,
		const FABTSM73DAG4SettledContactInput& Input,
		const TMap<int32, FSettledNodeGeometry>& GeometryById,
		const FVector2D& AffectedCenterOfMass,
		const float TipMarginCM,
		int32& InOutPairQueryCount,
		float& OutRisk,
		FString& OutError)
	{
		TSet<int32> AffectedSet;
		for (const int32 NodeId : Input.ExpectedAffectedNodeIds)
		{
			AffectedSet.Add(NodeId);
		}
		TSet<int32> FailureSet;
		for (const int32 NodeId : Input.WeakNodeIds)
		{
			FailureSet.Add(NodeId);
		}

		float FallingBottomHeight = BIG_NUMBER;
		TArray<const FSettledNodeGeometry*> LowestAffected;
		TArray<int32> SortedAffectedNodeIds =
			Input.ExpectedAffectedNodeIds;
		SortedAffectedNodeIds.Sort();
		for (const int32 NodeId : SortedAffectedNodeIds)
		{
			const FSettledNodeGeometry* Geometry = GeometryById.Find(NodeId);
			if (Geometry == nullptr)
			{
				OutError = TEXT("DAG4SettledReseatNodeMissing");
				return false;
			}
			FallingBottomHeight = FMath::Min(
				FallingBottomHeight,
				AverageFaceHeight(Geometry->BottomFace));
		}
		for (const int32 NodeId : SortedAffectedNodeIds)
		{
			const FSettledNodeGeometry& Geometry =
				GeometryById.FindChecked(NodeId);
			if (FMath::Abs(
				AverageFaceHeight(Geometry.BottomFace)
				- FallingBottomHeight) <= ContactHeightSampleToleranceCM)
			{
				LowestAffected.Add(&Geometry);
			}
		}

		struct FLandingPatch
		{
			float Height = 0.0f;
			TArray<FVector2D> Points;
		};
		TArray<FLandingPatch> LandingPatches;
		TArray<int32> SortedGeometryNodeIds;
		GeometryById.GetKeys(SortedGeometryNodeIds);
		SortedGeometryNodeIds.Sort();
		for (const FSettledNodeGeometry* Falling : LowestAffected)
		{
			for (const int32 StaticNodeId : SortedGeometryNodeIds)
			{
				if (AffectedSet.Contains(StaticNodeId)
					|| FailureSet.Contains(StaticNodeId))
				{
					continue;
				}
				++InOutPairQueryCount;
				if (InOutPairQueryCount
					> Settings.MaxContactPairQueryCount)
				{
					OutError = FString::Printf(
						TEXT("DAG4SettledPairBudgetExceeded:%d:%d"),
						InOutPairQueryCount,
						Settings.MaxContactPairQueryCount);
					return false;
				}
				const FSettledNodeGeometry& Landing =
					GeometryById.FindChecked(StaticNodeId);
				const float LandingHeight =
					AverageFaceHeight(Landing.TopFace);
				if (LandingHeight
					> FallingBottomHeight
						+ Settings.ContactGapToleranceCM)
				{
					continue;
				}
				TArray<FVector2D> Patch = IntersectConvexPolygons(
					Falling->BottomFace.ProjectedHull,
					Landing.TopFace.ProjectedHull);
				if (PolygonArea(Patch)
					+ PolygonTolerance
					< Settings.MinContactPatchAreaCM2)
				{
					continue;
				}
				FLandingPatch& Candidate = LandingPatches.AddDefaulted_GetRef();
				Candidate.Height = LandingHeight;
				Candidate.Points = MoveTemp(Patch);
			}
		}
		if (LandingPatches.IsEmpty())
		{
			OutRisk = 0.0f;
			return true;
		}

		float HighestLandingHeight = -BIG_NUMBER;
		for (const FLandingPatch& Candidate : LandingPatches)
		{
			HighestLandingHeight = FMath::Max(
				HighestLandingHeight,
				Candidate.Height);
		}
		TArray<FVector2D> HighestLandingPoints;
		for (const FLandingPatch& Candidate : LandingPatches)
		{
			if (Candidate.Height
				>= HighestLandingHeight
					- ContactHeightSampleToleranceCM)
			{
				HighestLandingPoints.Append(Candidate.Points);
			}
		}
		const TArray<FVector2D> LandingHull =
			BuildConvexHull(MoveTemp(HighestLandingPoints));
		const float AlignmentRisk =
			SignedInsideMargin(AffectedCenterOfMass, LandingHull) >= 0.0f
				? 1.0f
				: 0.0f;
		const float TipFeasibility =
			Input.ExpectedMotion == EABTSM73DAGFailureMotion::Drop
				? 0.0f
				: FMath::Clamp(
					TipMarginCM
						/ FMath::Max(
							1.0f,
							Input.MinPostFailureTipMarginCM * 2.0f),
					0.0f,
					1.0f);
		OutRisk = AlignmentRisk * (1.0f - TipFeasibility);
		return true;
	}
}

bool FABTSM73DAG4SettledContactValidator::RebuildAndValidate(
	const FABTSM73DAG4ValidationSettings& Settings,
	const FABTSM73DAG4SettledContactInput& Input,
	FABTSM73DAG4SettledContactResult& OutResult,
	FString& OutError) const
{
	OutResult = FABTSM73DAG4SettledContactResult();
	OutError.Reset();
	if (!Settings.bEnableSettledChaosValidation)
	{
		return true;
	}

	auto Reject = [&OutResult, &OutError](const FString& Reason)
	{
		OutResult = FABTSM73DAG4SettledContactResult();
		OutResult.RejectReason = Reason;
		OutError = Reason;
		return false;
	};

	FString ValidationError;
	if (!ValidateSettings(Settings, ValidationError))
	{
		return Reject(ValidationError);
	}
	if (Input.Nodes.IsEmpty()
		|| Input.Nodes.Num() > Settings.MaxSettledBodyCount)
	{
		return Reject(FString::Printf(
			TEXT("DAG4SettledBodyBudgetInvalid:%d:%d"),
			Input.Nodes.Num(),
			Settings.MaxSettledBodyCount));
	}
	const int64 RequiredPairQueries =
		static_cast<int64>(Input.Nodes.Num())
		* (Input.Nodes.Num() - 1)
		/ 2;
	if (RequiredPairQueries > Settings.MaxContactPairQueryCount)
	{
		return Reject(FString::Printf(
			TEXT("DAG4SettledPairBudgetExceeded:%lld:%d"),
			RequiredPairQueries,
			Settings.MaxContactPairQueryCount));
	}

	TMap<int32, const FABTSM73DAG4SettledNode*> NodesById;
	TMap<int32, FSettledNodeGeometry> GeometryById;
	TSet<int32> NodeIds;
	for (const FABTSM73DAG4SettledNode& Node : Input.Nodes)
	{
		const FVector Scale = Node.LocalTransform.GetScale3D();
		const FQuat Rotation = Node.LocalTransform.GetRotation();
		if (Node.NodeId < 0
			|| Node.MacroNodeId < INDEX_NONE
			|| NodeIds.Contains(Node.NodeId)
			|| static_cast<uint8>(Node.Material)
				> static_cast<uint8>(EABTSM7BuildingMaterial::Glass)
			|| !IsFiniteSettledVector(Node.DimensionsCM)
			|| Node.DimensionsCM.GetMin() <= 0.0f
			|| Node.LocalTransform.ContainsNaN()
			|| !IsFiniteSettledVector(Node.LocalTransform.GetLocation())
			|| !IsFiniteSettledQuat(Rotation)
			|| !FMath::IsNearlyEqual(Rotation.SizeSquared(), 1.0f, 1.0e-3f)
			|| !IsFiniteSettledVector(Scale)
			|| !Scale.Equals(FVector::OneVector, 1.0e-3f)
			|| !FMath::IsFinite(Node.Mass)
			|| Node.Mass <= 0.0)
		{
			return Reject(FString::Printf(
				TEXT("DAG4SettledNodeInvalid:%d"),
				Node.NodeId));
		}
		NodeIds.Add(Node.NodeId);
		NodesById.Add(Node.NodeId, &Node);
		FSettledNodeGeometry& Geometry =
			GeometryById.Add(Node.NodeId);
		Geometry.Node = &Node;
		Geometry.TopFace = BuildSupportFace(Node, true);
		Geometry.BottomFace = BuildSupportFace(Node, false);
		if (Geometry.TopFace.ProjectedHull.Num() < 3
			|| Geometry.BottomFace.ProjectedHull.Num() < 3)
		{
			return Reject(FString::Printf(
				TEXT("DAG4SettledNodeFaceDegenerate:%d"),
				Node.NodeId));
		}
	}

	if (Input.BaselineAllowedContacts.IsEmpty()
		|| Input.RequiredContacts.IsEmpty()
		|| Input.BaselineGroundNodeIds.IsEmpty()
		|| ContainsDuplicateSettledIds(Input.BaselineGroundNodeIds)
		|| Input.WeakNodeIds.Num() != 1
		|| ContainsDuplicateSettledIds(Input.WeakNodeIds)
		|| ContainsDuplicateSettledIds(Input.RemainingSupportNodeIds)
		|| Input.ExpectedAffectedNodeIds.IsEmpty()
		|| ContainsDuplicateSettledIds(Input.ExpectedAffectedNodeIds)
		|| Input.ExpectedAffectedMainBodyNodeIds.IsEmpty()
		|| ContainsDuplicateSettledIds(
			Input.ExpectedAffectedMainBodyNodeIds)
		|| !NodeIds.Contains(Input.LoadPlateNodeId)
		|| !Input.ExpectedAffectedNodeIds.Contains(
			Input.LoadPlateNodeId)
		|| Input.RealizedPatternHash == 0
		|| !IsFiniteSettledVector(
			Input.ExpectedFailureDirectionLocal)
		|| !FMath::IsFinite(Input.MinInitialSupportMarginCM)
		|| Input.MinInitialSupportMarginCM < 0.0f
		|| !FMath::IsFinite(Input.MinPostFailureTipMarginCM)
		|| Input.MinPostFailureTipMarginCM < 0.0f
		|| !FMath::IsFinite(Input.MaxReseatRisk)
		|| Input.MaxReseatRisk < 0.0f
		|| Input.MaxReseatRisk > 1.0f)
	{
		return Reject(TEXT("DAG4SettledPatternInputInvalid"));
	}
	for (const int32 GroundNodeId : Input.BaselineGroundNodeIds)
	{
		if (!NodeIds.Contains(GroundNodeId))
		{
			return Reject(FString::Printf(
				TEXT("DAG4SettledGroundNodeMissing:%d"),
				GroundNodeId));
		}
	}
	TSet<int32> FailureNodeIds;
	for (const int32 WeakNodeId : Input.WeakNodeIds)
	{
		if (!NodeIds.Contains(WeakNodeId))
		{
			return Reject(FString::Printf(
				TEXT("DAG4SettledWeakNodeMissing:%d"),
				WeakNodeId));
		}
		FailureNodeIds.Add(WeakNodeId);
	}
	for (const int32 PivotNodeId : Input.RemainingSupportNodeIds)
	{
		if (!NodeIds.Contains(PivotNodeId)
			|| FailureNodeIds.Contains(PivotNodeId))
		{
			return Reject(FString::Printf(
				TEXT("DAG4SettledPivotNodeInvalid:%d"),
				PivotNodeId));
		}
		FailureNodeIds.Add(PivotNodeId);
	}
	for (const int32 AffectedNodeId : Input.ExpectedAffectedNodeIds)
	{
		if (!NodeIds.Contains(AffectedNodeId)
			|| FailureNodeIds.Contains(AffectedNodeId))
		{
			return Reject(FString::Printf(
				TEXT("DAG4SettledAffectedNodeInvalid:%d"),
				AffectedNodeId));
		}
	}
	for (const int32 AffectedMainBodyNodeId
		: Input.ExpectedAffectedMainBodyNodeIds)
	{
		const FABTSM73DAG4SettledNode* const* AffectedNode =
			NodesById.Find(AffectedMainBodyNodeId);
		if (AffectedNode == nullptr
			|| *AffectedNode == nullptr
			|| !(*AffectedNode)->bMainBody
			|| !Input.ExpectedAffectedNodeIds.Contains(
				AffectedMainBodyNodeId))
		{
			return Reject(FString::Printf(
				TEXT("DAG4SettledAffectedMainBodyNodeInvalid:%d"),
				AffectedMainBodyNodeId));
		}
	}

	const bool bSingle =
		Input.Pattern
			== EABTSM73DAGFailurePattern::InternalSingleSupport
		&& Input.ExpectedMotion == EABTSM73DAGFailureMotion::Drop
		&& Input.RemainingSupportNodeIds.IsEmpty();
	const bool bDual =
		Input.Pattern
			== EABTSM73DAGFailurePattern::InternalAsymmetricDualSupport
		&& Input.ExpectedMotion == EABTSM73DAGFailureMotion::Tip
		&& Input.RemainingSupportNodeIds.Num() == 1;
	const bool bSeam =
		Input.Pattern
			== EABTSM73DAGFailurePattern::InternalOffsetSeam
		&& Input.ExpectedMotion
			== EABTSM73DAGFailureMotion::SlideThenTip
		&& Input.RemainingSupportNodeIds.Num() == 1;
	if ((!bSingle && !bDual && !bSeam)
		|| ((!bSingle)
			&& FVector(
				Input.ExpectedFailureDirectionLocal.X,
				Input.ExpectedFailureDirectionLocal.Y,
				0.0f).IsNearlyZero()))
	{
		return Reject(TEXT("DAG4SettledPatternMotionMismatch"));
	}

	TSet<uint64> AllowedEdgeKeys;
	TSet<uint64> RequiredEdgeKeys;
	if (!ValidateEdgeArray(
			TEXT("DAG4SettledAllowedContact"),
			Input.BaselineAllowedContacts,
			NodeIds,
			AllowedEdgeKeys,
			ValidationError)
		|| !ValidateEdgeArray(
			TEXT("DAG4SettledRequiredContact"),
			Input.RequiredContacts,
			NodeIds,
			RequiredEdgeKeys,
			ValidationError))
	{
		return Reject(ValidationError);
	}
	for (const uint64 RequiredKey : RequiredEdgeKeys)
	{
		if (!AllowedEdgeKeys.Contains(RequiredKey))
		{
			return Reject(TEXT(
				"DAG4SettledRequiredContactNotBaselineAllowed"));
		}
	}

	FABTSM73DAG4SettledContactResult Working;
	Working.BaselineContactHash = BuildBaselineHash(
		Input.BaselineAllowedContacts,
		Input.RequiredContacts,
		Input.BaselineGroundNodeIds);

	TArray<int32> SortedNodeIds = NodeIds.Array();
	SortedNodeIds.Sort();
	for (const int32 NodeId : SortedNodeIds)
	{
		if (IsGroundContact(
			Settings,
			GeometryById.FindChecked(NodeId)))
		{
			Working.GroundNodeIds.Add(NodeId);
		}
	}
	SortUniqueSettledIds(Working.GroundNodeIds);
	for (const int32 BaselineGroundNodeId : Input.BaselineGroundNodeIds)
	{
		if (!Working.GroundNodeIds.Contains(BaselineGroundNodeId))
		{
			return Reject(FString::Printf(
				TEXT("DAG4SettledGroundContactMissing:%d"),
				BaselineGroundNodeId));
		}
	}

	for (int32 AIndex = 0; AIndex < SortedNodeIds.Num(); ++AIndex)
	{
		for (int32 BIndex = AIndex + 1;
			BIndex < SortedNodeIds.Num();
			++BIndex)
		{
			++Working.PairQueryCount;
			if (Working.PairQueryCount
				> Settings.MaxContactPairQueryCount)
			{
				return Reject(FString::Printf(
					TEXT("DAG4SettledPairBudgetExceeded:%d:%d"),
					Working.PairQueryCount,
					Settings.MaxContactPairQueryCount));
			}
			FABTSM73DAG4SettledContact Contact;
			if (TryBuildSupportContact(
				Settings,
				GeometryById.FindChecked(SortedNodeIds[AIndex]),
				GeometryById.FindChecked(SortedNodeIds[BIndex]),
				Contact))
			{
				Working.Contacts.Add(MoveTemp(Contact));
			}
		}
	}
	Working.Contacts.Sort([](
		const FABTSM73DAG4SettledContact& A,
		const FABTSM73DAG4SettledContact& B)
	{
		return A.LowerNodeId != B.LowerNodeId
			? A.LowerNodeId < B.LowerNodeId
			: A.UpperNodeId < B.UpperNodeId;
	});

	TMap<uint64, const FABTSM73DAG4SettledContact*> ContactsByKey;
	for (const FABTSM73DAG4SettledContact& Contact : Working.Contacts)
	{
		const uint64 Key = MakeSettledEdgeKey(
			Contact.LowerNodeId,
			Contact.UpperNodeId);
		ContactsByKey.Add(Key, &Contact);
		if (!AllowedEdgeKeys.Contains(Key))
		{
			Working.NewContactLowerNodeIds.Add(Contact.LowerNodeId);
			Working.NewContactUpperNodeIds.Add(Contact.UpperNodeId);
		}
	}
	for (const FABTSM73SupportEdge& Required : Input.RequiredContacts)
	{
		const FABTSM73DAG4SettledContact* const* Found =
			ContactsByKey.Find(MakeSettledEdgeKey(
				Required.LowerNodeId,
				Required.UpperNodeId));
		const float RequiredArea = FMath::Max(
			Settings.MinContactPatchAreaCM2,
			Required.ContactAreaCM2
				* Settings.MinRequiredContactAreaRetention);
		if (Found == nullptr
			|| *Found == nullptr
			|| (*Found)->ContactAreaCM2
				+ PolygonTolerance < RequiredArea)
		{
			Working.MissingRequiredLowerNodeIds.Add(
				Required.LowerNodeId);
			Working.MissingRequiredUpperNodeIds.Add(
				Required.UpperNodeId);
		}
	}
	if (!Working.MissingRequiredLowerNodeIds.IsEmpty())
	{
		return Reject(FString::Printf(
			TEXT("DAG4SettledRequiredContactMissing:%d"),
			Working.MissingRequiredLowerNodeIds.Num()));
	}

	TSet<int32> NoRemovedNodes;
	TSet<int32> IntactReachable;
	GatherGroundReachable(
		Working.Contacts,
		Working.GroundNodeIds,
		NoRemovedNodes,
		IntactReachable);
	for (const FABTSM73DAG4SettledNode& Node : Input.Nodes)
	{
		if (Node.bMainBody && !IntactReachable.Contains(Node.NodeId))
		{
			return Reject(FString::Printf(
				TEXT("DAG4SettledIntactGroundPathMissing:%d"),
				Node.NodeId));
		}
	}

	TSet<int32> FrontierRemoved = FailureNodeIds;
	TSet<int32> ReachableWithoutFrontier;
	GatherGroundReachable(
		Working.Contacts,
		Working.GroundNodeIds,
		FrontierRemoved,
		ReachableWithoutFrontier);
	for (const int32 AffectedNodeId : Input.ExpectedAffectedNodeIds)
	{
		if (ReachableWithoutFrontier.Contains(AffectedNodeId))
		{
			Working.FrontierBypassNodeIds.Add(AffectedNodeId);
		}
	}
	SortUniqueSettledIds(Working.FrontierBypassNodeIds);
	if (!Working.FrontierBypassNodeIds.IsEmpty())
	{
		return Reject(FString::Printf(
			TEXT("DAG4SettledFrontierBypass:%d"),
			Working.FrontierBypassNodeIds.Num()));
	}

	TSet<int32> WeakRemoved;
	for (const int32 WeakNodeId : Input.WeakNodeIds)
	{
		WeakRemoved.Add(WeakNodeId);
	}
	TSet<int32> ReachableWithoutWeak;
	GatherGroundReachable(
		Working.Contacts,
		Working.GroundNodeIds,
		WeakRemoved,
		ReachableWithoutWeak);
	for (const int32 NodeId : SortedNodeIds)
	{
		if (!WeakRemoved.Contains(NodeId)
			&& !ReachableWithoutWeak.Contains(NodeId))
		{
			Working.DisconnectedAfterWeakNodeIds.Add(NodeId);
		}
	}
	if (bSingle)
	{
		if (ReachableWithoutWeak.Contains(Input.LoadPlateNodeId))
		{
			return Reject(TEXT(
				"DAG4SettledSingleWeakRemovalStillGrounded"));
		}
		for (const int32 AffectedNodeId : Input.ExpectedAffectedNodeIds)
		{
			if (ReachableWithoutWeak.Contains(AffectedNodeId))
			{
				return Reject(FString::Printf(
					TEXT("DAG4SettledSingleAffectedStillGrounded:%d"),
					AffectedNodeId));
			}
		}
	}
	else
	{
		if (!ReachableWithoutWeak.Contains(Input.LoadPlateNodeId))
		{
			return Reject(TEXT(
				"DAG4SettledPivotLoadPlateDisconnected"));
		}
		for (const int32 PivotNodeId : Input.RemainingSupportNodeIds)
		{
			if (!ReachableWithoutWeak.Contains(PivotNodeId))
			{
				return Reject(FString::Printf(
					TEXT("DAG4SettledPivotDisconnected:%d"),
					PivotNodeId));
			}
		}
	}

	FVector AffectedCenterOfMass3D = FVector::ZeroVector;
	if (!ComputeAffectedCenterOfMass(
		Input,
		NodesById,
		AffectedCenterOfMass3D))
	{
		return Reject(TEXT("DAG4SettledAffectedMassInvalid"));
	}
	const FVector2D AffectedCenterOfMass(
		AffectedCenterOfMass3D.X,
		AffectedCenterOfMass3D.Y);

	TArray<FVector2D> FullSupportPoints;
	TArray<FVector2D> RemainingSupportPoints;
	TSet<int32> WeakSet;
	for (const int32 WeakNodeId : Input.WeakNodeIds)
	{
		WeakSet.Add(WeakNodeId);
	}
	TSet<int32> PivotSet;
	for (const int32 PivotNodeId : Input.RemainingSupportNodeIds)
	{
		PivotSet.Add(PivotNodeId);
	}
	for (const FABTSM73DAG4SettledContact& Contact : Working.Contacts)
	{
		if (Contact.UpperNodeId != Input.LoadPlateNodeId)
		{
			continue;
		}
		if (WeakSet.Contains(Contact.LowerNodeId)
			|| PivotSet.Contains(Contact.LowerNodeId))
		{
			FullSupportPoints.Append(Contact.PatchVertices);
		}
		if (PivotSet.Contains(Contact.LowerNodeId))
		{
			RemainingSupportPoints.Append(Contact.PatchVertices);
		}
	}
	const TArray<FVector2D> FullSupportHull =
		BuildConvexHull(MoveTemp(FullSupportPoints));
	if (FullSupportHull.Num() < 3)
	{
		return Reject(TEXT("DAG4SettledFullSupportHullDegenerate"));
	}
	Working.InitialSupportMarginCM =
		SignedInsideMargin(AffectedCenterOfMass, FullSupportHull);
	if (Working.InitialSupportMarginCM
		+ PolygonTolerance < Input.MinInitialSupportMarginCM)
	{
		return Reject(FString::Printf(
			TEXT("DAG4SettledInitialSupportMarginTooSmall:%.3f:%.3f"),
			Working.InitialSupportMarginCM,
			Input.MinInitialSupportMarginCM));
	}

	if (!bSingle)
	{
		const TArray<FVector2D> RemainingSupportHull =
			BuildConvexHull(MoveTemp(RemainingSupportPoints));
		if (RemainingSupportHull.Num() < 3)
		{
			return Reject(TEXT(
				"DAG4SettledRemainingSupportHullDegenerate"));
		}
		const float RemainingInsideMargin =
			SignedInsideMargin(
				AffectedCenterOfMass,
				RemainingSupportHull);
		Working.PostFailureTipMarginCM = -RemainingInsideMargin;
		if (Working.PostFailureTipMarginCM
			+ PolygonTolerance
			< Input.MinPostFailureTipMarginCM)
		{
			return Reject(FString::Printf(
				TEXT("DAG4SettledPostFailureTipMarginTooSmall:%.3f:%.3f"),
				Working.PostFailureTipMarginCM,
				Input.MinPostFailureTipMarginCM));
		}
		const FVector2D RemainingCenter =
			PolygonCentroid(RemainingSupportHull);
		const FVector PredictedDirection =
			FVector(
				AffectedCenterOfMass - RemainingCenter,
				0.0f).GetSafeNormal();
		const FVector ExpectedDirection =
			FVector(
				Input.ExpectedFailureDirectionLocal.X,
				Input.ExpectedFailureDirectionLocal.Y,
				0.0f).GetSafeNormal();
		const float Alignment = FVector::DotProduct(
			PredictedDirection,
			ExpectedDirection);
		if (PredictedDirection.IsNearlyZero()
			|| Alignment
				< Settings.MinFailureDirectionAlignment)
		{
			return Reject(FString::Printf(
				TEXT("DAG4SettledFailureDirectionMismatch:%.3f:%.3f"),
				Alignment,
				Settings.MinFailureDirectionAlignment));
		}
	}
	else
	{
		Working.PostFailureTipMarginCM = 0.0f;
	}

	if (!EstimateSettledReseatRisk(
		Settings,
		Input,
		GeometryById,
		AffectedCenterOfMass,
		Working.PostFailureTipMarginCM,
		Working.PairQueryCount,
		Working.ReseatRisk,
		ValidationError))
	{
		return Reject(ValidationError);
	}
	// A Single/Drop pattern is intentionally allowed to enter the dynamic
	// rollout even when a static vertical projection predicts a possible
	// landing patch. The Chaos weak trial is the authority on whether that
	// landing still produces enough visible structural progress. Dual/Seam
	// retain the static reseat gate because their certified motion depends on
	// preserving the pivot-driven tip/slide corridor.
	if (!bSingle
		&& Working.ReseatRisk
		> Input.MaxReseatRisk + PolygonTolerance)
	{
		return Reject(FString::Printf(
			TEXT("DAG4SettledReseatRiskTooHigh:%.3f:%.3f"),
			Working.ReseatRisk,
			Input.MaxReseatRisk));
	}

	Working.SettledContactHash =
		BuildSettledHash(Settings, Input, Working);
	OutResult = MoveTemp(Working);
	return true;
}
