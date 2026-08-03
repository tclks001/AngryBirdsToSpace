// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BeamCGenerator.h"

#include "Algo/Sort.h"
#include "Misc/Crc.h"

namespace ABTSM73BeamC
{
	struct FSupportStation
	{
		double Coordinate = 0.0;
		TArray<int32> EdgeIndices;
		double AreaCM2 = 0.0;
	};

	bool Reject(
		FABTSM73BeamCGenerationResult& Result,
		FString& OutError,
		const FString& Reason)
	{
		Result.Summary.bAccepted = false;
		Result.Summary.RejectReason = Reason;
		OutError = Reason;
		return false;
	}

	FVector MemberStart(
		const FABTSM73BeamAMember& Member,
		const FABTSM73BeamAGenerationResult& Assembly)
	{
		return Assembly.Joints[Member.JointA].LocalPosition;
	}

	FVector MemberEnd(
		const FABTSM73BeamAMember& Member,
		const FABTSM73BeamAGenerationResult& Assembly)
	{
		return Assembly.Joints[Member.JointB].LocalPosition;
	}

	FVector MemberMidpoint(
		const FABTSM73BeamAMember& Member,
		const FABTSM73BeamAGenerationResult& Assembly)
	{
		return (MemberStart(Member, Assembly)
			+ MemberEnd(Member, Assembly)) * 0.5;
	}

	bool IsGroundMember(
		const FABTSM73BeamAMember& Member,
		const FABTSM73BeamAGenerationResult& Assembly,
		const FABTSM73BeamCPreviewSettings& Settings)
	{
		const FVector A = MemberStart(Member, Assembly);
		const FVector B = MemberEnd(Member, Assembly);
		const double HalfSection = Settings.BeamB.BeamA.BlockCrossSectionCM * 0.5;
		return FMath::Min(A.Z, B.Z) - HalfSection
			<= Settings.BeamB.BeamA.JointMergeToleranceCM;
	}

	void SplitStationShare(
		const FSupportStation& Station,
		const double StationShare,
		TArray<FABTSM73BeamCLoadEdge>& Edges)
	{
		const double SafeArea = FMath::Max(Station.AreaCM2, UE_DOUBLE_SMALL_NUMBER);
		for (const int32 EdgeIndex : Station.EdgeIndices)
		{
			FABTSM73BeamCLoadEdge& Edge = Edges[EdgeIndex];
			Edge.LoadShare = static_cast<float>(StationShare
				* Edge.ContactAreaCM2 / SafeArea);
		}
	}

	uint32 HashResult(const FABTSM73BeamCGenerationResult& Result)
	{
		FString Signature;
		Signature.Reserve(Result.Nodes.Num() * 96 + Result.Edges.Num() * 96);
		for (const FABTSM73BeamCLoadNode& Node : Result.Nodes)
		{
			Signature += FString::Printf(
				TEXT("N:%d:%d:%d:%.6f:%.6f:%.6f:%.6f:%.6f:%.6f|"),
				Node.MemberId, static_cast<int32>(Node.Axis), Node.bGround ? 1 : 0,
				Node.SelfLoadKG, Node.AccumulatedLoadKG,
				Node.EffectiveSpanCM, Node.CantileverRatio,
				Node.SpanUtilization, Node.ColumnSlenderness);
		}
		for (const FABTSM73BeamCLoadEdge& Edge : Result.Edges)
		{
			Signature += FString::Printf(
				TEXT("E:%d:%d:%d:%d:%.6f:%.6f:%.6f|"),
				Edge.EdgeId, Edge.BearingContactId,
				Edge.UpperMemberId, Edge.LowerMemberId,
				Edge.ContactAreaCM2, Edge.LoadShare, Edge.ReactionLoadKG);
		}
		for (const int32 MemberId : Result.TopologicalMemberOrder)
		{
			Signature += FString::Printf(TEXT("T:%d|"), MemberId);
		}
		return FCrc::StrCrc32(*Signature);
	}
}

bool FABTSM73BeamCGenerator::Generate(
	const FABTSM73BeamCPreviewSettings& Settings,
	const FABTSM73BeamAGenerationResult& ClosedAssembly,
	FABTSM73BeamCGenerationResult& OutResult,
	FString& OutError) const
{
	using namespace ABTSM73BeamC;
	OutResult = FABTSM73BeamCGenerationResult();
	OutError.Reset();

	if (!ClosedAssembly.Summary.bAccepted)
	{
		return Reject(OutResult, OutError, TEXT("BeamCUpstreamRejected"));
	}
	if (Settings.MemberLinearDensityKGPerCM <= 0.0f
		|| Settings.ReferenceLoadKG <= 0.0f
		|| Settings.ReferenceSpanCM <= 0.0f
		|| Settings.SpanStiffnessScale <= 0.0f)
	{
		return Reject(OutResult, OutError, TEXT("BeamCInvalidSettings"));
	}
	if (ClosedAssembly.Members.IsEmpty())
	{
		return Reject(OutResult, OutError, TEXT("BeamCEmptyAssembly"));
	}
	if (ClosedAssembly.Members.Num() > Settings.MaximumLoadNodeCount)
	{
		return Reject(OutResult, OutError, TEXT("BeamCNodeBudgetExceeded"));
	}
	if (ClosedAssembly.BearingContacts.Num() > Settings.MaximumLoadEdgeCount)
	{
		return Reject(OutResult, OutError, TEXT("BeamCEdgeBudgetExceeded"));
	}

	const int32 NodeCount = ClosedAssembly.Members.Num();
	OutResult.Nodes.SetNum(NodeCount);
	TArray<FVector> FirstMoments;
	FirstMoments.SetNumZeroed(NodeCount);
	int32 XCount = 0;
	int32 YCount = 0;
	int32 ZCount = 0;
	for (int32 MemberIndex = 0; MemberIndex < NodeCount; ++MemberIndex)
	{
		const FABTSM73BeamAMember& Member = ClosedAssembly.Members[MemberIndex];
		if (Member.MemberId != MemberIndex
			|| !ClosedAssembly.Joints.IsValidIndex(Member.JointA)
			|| !ClosedAssembly.Joints.IsValidIndex(Member.JointB)
			|| Member.LengthCM <= 0.0f)
		{
			return Reject(OutResult, OutError, TEXT("BeamCInvalidMember"));
		}
		FABTSM73BeamCLoadNode& Node = OutResult.Nodes[MemberIndex];
		Node.MemberId = Member.MemberId;
		Node.Axis = Member.Axis;
		Node.bGround = IsGroundMember(Member, ClosedAssembly, Settings);
		Node.SelfLoadKG = Member.LengthCM * Settings.MemberLinearDensityKGPerCM;
		Node.AccumulatedLoadKG = Node.SelfLoadKG;
		Node.LoadResultant = MemberMidpoint(Member, ClosedAssembly);
		FirstMoments[MemberIndex] = Node.LoadResultant * Node.AccumulatedLoadKG;
		OutResult.Summary.TotalSelfLoadKG += Node.SelfLoadKG;
		OutResult.Summary.GroundNodeCount += Node.bGround ? 1 : 0;
		XCount += Member.Axis == EABTSM73BeamAFrameAxis::X ? 1 : 0;
		YCount += Member.Axis == EABTSM73BeamAFrameAxis::Y ? 1 : 0;
		ZCount += Member.Axis == EABTSM73BeamAFrameAxis::Z ? 1 : 0;
	}
	OutResult.Summary.LoadNodeCount = NodeCount;

	TArray<TArray<int32>> OutgoingEdges;
	TArray<TArray<int32>> ReverseReachability;
	OutgoingEdges.SetNum(NodeCount);
	ReverseReachability.SetNum(NodeCount);
	TArray<int32> InDegree;
	InDegree.Init(0, NodeCount);
	const double MinimumBearingArea =
		FMath::Square(static_cast<double>(Settings.BeamB.BeamA.BlockCrossSectionCM))
		* Settings.MinimumBearingAreaRatio;
	for (const FABTSM73BeamABearingContact& Contact : ClosedAssembly.BearingContacts)
	{
		if (!OutResult.Nodes.IsValidIndex(Contact.UpperMemberId)
			|| !OutResult.Nodes.IsValidIndex(Contact.LowerMemberId)
			|| Contact.UpperMemberId == Contact.LowerMemberId
			|| Contact.ContactAreaCM2 <= 0.0f)
		{
			return Reject(OutResult, OutError, TEXT("BeamCInvalidBearingContact"));
		}
		FABTSM73BeamCLoadEdge Edge;
		Edge.EdgeId = OutResult.Edges.Num();
		Edge.BearingContactId = Contact.ContactId;
		Edge.UpperMemberId = Contact.UpperMemberId;
		Edge.LowerMemberId = Contact.LowerMemberId;
		Edge.ContactPosition = Contact.LocalPosition;
		Edge.ContactAreaCM2 = Contact.ContactAreaCM2;
		if (Contact.ContactAreaCM2 + UE_DOUBLE_SMALL_NUMBER < MinimumBearingArea)
		{
			++OutResult.Summary.BearingAreaViolationCount;
		}
		OutgoingEdges[Edge.UpperMemberId].Add(Edge.EdgeId);
		ReverseReachability[Edge.LowerMemberId].Add(Edge.UpperMemberId);
		++InDegree[Edge.LowerMemberId];
		OutResult.Edges.Add(Edge);
	}
	OutResult.Summary.LoadEdgeCount = OutResult.Edges.Num();
	if (OutResult.Summary.BearingAreaViolationCount > 0)
	{
		return Reject(OutResult, OutError, TEXT("BeamCBearingAreaInsufficient"));
	}

	int32 TopologyOperations = 0;
	TArray<int32> Ready;
	for (int32 NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex)
	{
		if (InDegree[NodeIndex] == 0)
		{
			Ready.Add(NodeIndex);
		}
	}
	Ready.Sort();
	while (!Ready.IsEmpty())
	{
		const int32 MemberId = Ready[0];
		Ready.RemoveAt(0, EAllowShrinking::No);
		OutResult.TopologicalMemberOrder.Add(MemberId);
		for (const int32 EdgeIndex : OutgoingEdges[MemberId])
		{
			if (++TopologyOperations > Settings.MaximumTopologyOperationCount)
			{
				return Reject(OutResult, OutError,
					TEXT("BeamCTopologyBudgetExceeded"));
			}
			const int32 LowerId = OutResult.Edges[EdgeIndex].LowerMemberId;
			if (--InDegree[LowerId] == 0)
			{
				Ready.Add(LowerId);
				Ready.Sort();
			}
		}
	}
	if (OutResult.TopologicalMemberOrder.Num() != NodeCount)
	{
		OutResult.Summary.CycleNodeCount =
			NodeCount - OutResult.TopologicalMemberOrder.Num();
		return Reject(OutResult, OutError, TEXT("BeamCLoadDAGCycle"));
	}

	TArray<int32> GroundQueue;
	for (FABTSM73BeamCLoadNode& Node : OutResult.Nodes)
	{
		if (Node.bGround)
		{
			Node.bGroundReachable = true;
			GroundQueue.Add(Node.MemberId);
		}
	}
	for (int32 QueueIndex = 0; QueueIndex < GroundQueue.Num(); ++QueueIndex)
	{
		const int32 LowerId = GroundQueue[QueueIndex];
		for (const int32 UpperId : ReverseReachability[LowerId])
		{
			if (++TopologyOperations > Settings.MaximumTopologyOperationCount)
			{
				return Reject(OutResult, OutError,
					TEXT("BeamCTopologyBudgetExceeded"));
			}
			if (!OutResult.Nodes[UpperId].bGroundReachable)
			{
				OutResult.Nodes[UpperId].bGroundReachable = true;
				GroundQueue.Add(UpperId);
			}
		}
	}
	for (const FABTSM73BeamCLoadNode& Node : OutResult.Nodes)
	{
		OutResult.Summary.GroundUnreachableNodeCount +=
			Node.bGroundReachable ? 0 : 1;
	}
	if (OutResult.Summary.GroundUnreachableNodeCount > 0)
	{
		return Reject(OutResult, OutError, TEXT("BeamCGroundUnreachable"));
	}

	const double StationTolerance =
		FMath::Max(0.01, static_cast<double>(Settings.BeamB.BeamA.JointMergeToleranceCM));
	for (const int32 MemberId : OutResult.TopologicalMemberOrder)
	{
		FABTSM73BeamCLoadNode& Node = OutResult.Nodes[MemberId];
		const FABTSM73BeamAMember& Member = ClosedAssembly.Members[MemberId];
		Node.LoadResultant = FirstMoments[MemberId]
			/ FMath::Max(static_cast<double>(Node.AccumulatedLoadKG), UE_DOUBLE_SMALL_NUMBER);
		if (Member.Axis == EABTSM73BeamAFrameAxis::Z)
		{
			Node.ColumnSlenderness = Member.LengthCM
				/ FMath::Max(Settings.BeamB.BeamA.BlockCrossSectionCM, 1.0f);
			OutResult.Summary.MaximumObservedColumnSlenderness = FMath::Max(
				OutResult.Summary.MaximumObservedColumnSlenderness,
				Node.ColumnSlenderness);
			if (Node.ColumnSlenderness > Settings.MaximumColumnSlenderness)
			{
				++OutResult.Summary.ColumnSlendernessViolationCount;
			}
		}
		if (Node.bGround)
		{
			continue;
		}
		const TArray<int32>& SupportEdges = OutgoingEdges[MemberId];
		Node.SupportCount = SupportEdges.Num();
		if (SupportEdges.IsEmpty())
		{
			++OutResult.Summary.ReactionBalanceViolationCount;
			continue;
		}

		if (Member.Axis == EABTSM73BeamAFrameAxis::X
			|| Member.Axis == EABTSM73BeamAFrameAxis::Y)
		{
			const int32 AxisIndex = Member.Axis == EABTSM73BeamAFrameAxis::X ? 0 : 1;
			TArray<FSupportStation> Stations;
			for (const int32 EdgeIndex : SupportEdges)
			{
				const double Coordinate = OutResult.Edges[EdgeIndex].ContactPosition[AxisIndex];
				FSupportStation* Existing = Stations.FindByPredicate(
					[Coordinate, StationTolerance](const FSupportStation& Station)
					{
						return FMath::Abs(Station.Coordinate - Coordinate) <= StationTolerance;
					});
				if (Existing == nullptr)
				{
					FSupportStation& NewStation = Stations.AddDefaulted_GetRef();
					NewStation.Coordinate = Coordinate;
					Existing = &NewStation;
				}
				Existing->EdgeIndices.Add(EdgeIndex);
				Existing->AreaCM2 += OutResult.Edges[EdgeIndex].ContactAreaCM2;
			}
			Stations.Sort([](const FSupportStation& A, const FSupportStation& B)
			{
				return A.Coordinate < B.Coordinate;
			});
			const double ResultantCoordinate = Node.LoadResultant[AxisIndex];
			if (Stations.Num() == 1 || ResultantCoordinate <= Stations[0].Coordinate)
			{
				SplitStationShare(Stations[0], 1.0, OutResult.Edges);
			}
			else if (ResultantCoordinate >= Stations.Last().Coordinate)
			{
				SplitStationShare(Stations.Last(), 1.0, OutResult.Edges);
			}
			else
			{
				for (int32 StationIndex = 0; StationIndex + 1 < Stations.Num(); ++StationIndex)
				{
					const FSupportStation& Left = Stations[StationIndex];
					const FSupportStation& Right = Stations[StationIndex + 1];
					if (ResultantCoordinate >= Left.Coordinate
						&& ResultantCoordinate <= Right.Coordinate)
					{
						const double Denominator = FMath::Max(
							Right.Coordinate - Left.Coordinate, StationTolerance);
						const double RightShare =
							(ResultantCoordinate - Left.Coordinate) / Denominator;
						SplitStationShare(Left, 1.0 - RightShare, OutResult.Edges);
						SplitStationShare(Right, RightShare, OutResult.Edges);
						break;
					}
				}
			}

			const FVector A = MemberStart(Member, ClosedAssembly);
			const FVector B = MemberEnd(Member, ClosedAssembly);
			const double Minimum = FMath::Min(A[AxisIndex], B[AxisIndex]);
			const double Maximum = FMath::Max(A[AxisIndex], B[AxisIndex]);
			double EffectiveSpan = 0.0;
			for (int32 StationIndex = 0; StationIndex + 1 < Stations.Num(); ++StationIndex)
			{
				EffectiveSpan = FMath::Max(EffectiveSpan,
					Stations[StationIndex + 1].Coordinate - Stations[StationIndex].Coordinate);
			}
			if (Stations.Num() == 1)
			{
				EffectiveSpan = Member.LengthCM;
			}
			const double Overhang = FMath::Max(
				FMath::Max(0.0, Stations[0].Coordinate - Minimum),
				FMath::Max(0.0, Maximum - Stations.Last().Coordinate));
			Node.EffectiveSpanCM = static_cast<float>(EffectiveSpan);
			Node.CantileverRatio = static_cast<float>(
				Overhang / FMath::Max(static_cast<double>(Member.LengthCM), 1.0));
			const double LoadRatio = Node.AccumulatedLoadKG / Settings.ReferenceLoadKG;
			const double SpanRatio = EffectiveSpan / Settings.ReferenceSpanCM;
			Node.SpanUtilization = static_cast<float>(
				LoadRatio * FMath::Square(SpanRatio) / Settings.SpanStiffnessScale);
			OutResult.Summary.MaximumObservedSpanUtilization = FMath::Max(
				OutResult.Summary.MaximumObservedSpanUtilization,
				Node.SpanUtilization);
			if (EffectiveSpan > Settings.MaximumUnsupportedSpanCM
				|| Node.SpanUtilization > Settings.MaximumSpanUtilization)
			{
				++OutResult.Summary.SpanViolationCount;
			}
			if (Node.CantileverRatio > Settings.MaximumCantileverRatio)
			{
				++OutResult.Summary.CantileverViolationCount;
			}
		}
		else
		{
			double TotalArea = 0.0;
			for (const int32 EdgeIndex : SupportEdges)
			{
				TotalArea += OutResult.Edges[EdgeIndex].ContactAreaCM2;
			}
			for (const int32 EdgeIndex : SupportEdges)
			{
				OutResult.Edges[EdgeIndex].LoadShare = static_cast<float>(
					OutResult.Edges[EdgeIndex].ContactAreaCM2
					/ FMath::Max(TotalArea, UE_DOUBLE_SMALL_NUMBER));
			}
		}

		double ShareSum = 0.0;
		double ReactionSum = 0.0;
		for (const int32 EdgeIndex : SupportEdges)
		{
			FABTSM73BeamCLoadEdge& Edge = OutResult.Edges[EdgeIndex];
			Edge.ReactionLoadKG = Edge.LoadShare * Node.AccumulatedLoadKG;
			ShareSum += Edge.LoadShare;
			ReactionSum += Edge.ReactionLoadKG;
			FABTSM73BeamCLoadNode& LowerNode = OutResult.Nodes[Edge.LowerMemberId];
			LowerNode.AccumulatedLoadKG += Edge.ReactionLoadKG;
			FirstMoments[Edge.LowerMemberId] +=
				Edge.ContactPosition * Edge.ReactionLoadKG;
		}
		const double ReactionTolerance = FMath::Max(
			0.01, static_cast<double>(Node.AccumulatedLoadKG) * 1.0e-4);
		if (!FMath::IsNearlyEqual(ShareSum, 1.0, 1.0e-4)
			|| !FMath::IsNearlyEqual(ReactionSum,
				static_cast<double>(Node.AccumulatedLoadKG), ReactionTolerance))
		{
			++OutResult.Summary.ReactionBalanceViolationCount;
		}
	}

	for (const FABTSM73BeamCLoadNode& Node : OutResult.Nodes)
	{
		if (Node.bGround)
		{
			OutResult.Summary.TotalGroundReactionKG += Node.AccumulatedLoadKG;
		}
	}
	const double GroundTolerance = FMath::Max(
		0.05, static_cast<double>(OutResult.Summary.TotalSelfLoadKG) * 1.0e-4);
	if (!FMath::IsNearlyEqual(
		static_cast<double>(OutResult.Summary.TotalSelfLoadKG),
		static_cast<double>(OutResult.Summary.TotalGroundReactionKG),
		GroundTolerance))
	{
		++OutResult.Summary.ReactionBalanceViolationCount;
	}
	if (Settings.bRequireBidirectionalLateralTies
		&& ZCount > 0 && (XCount == 0 || YCount == 0))
	{
		++OutResult.Summary.LateralMechanismViolationCount;
	}

	if (OutResult.Summary.ReactionBalanceViolationCount > 0)
	{
		return Reject(OutResult, OutError, TEXT("BeamCReactionBalanceFailed"));
	}
	if (OutResult.Summary.SpanViolationCount > 0)
	{
		return Reject(OutResult, OutError, TEXT("BeamCSpanLimitExceeded"));
	}
	if (OutResult.Summary.CantileverViolationCount > 0)
	{
		return Reject(OutResult, OutError, TEXT("BeamCCantileverLimitExceeded"));
	}
	if (OutResult.Summary.ColumnSlendernessViolationCount > 0)
	{
		return Reject(OutResult, OutError,
			TEXT("BeamCColumnSlendernessExceeded"));
	}
	if (OutResult.Summary.LateralMechanismViolationCount > 0)
	{
		return Reject(OutResult, OutError, TEXT("BeamCLateralMechanism"));
	}

	OutResult.Summary.bAccepted = true;
	OutResult.Summary.LoadDAGHash = static_cast<int64>(HashResult(OutResult));
	return true;
}
