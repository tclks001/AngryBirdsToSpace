// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BeamCGenerator.h"

#include "ABTSRuntime.h"
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

	struct FSupportInterval
	{
		double Minimum = 0.0;
		double Maximum = 0.0;
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

	FBox MemberBounds(
		const FABTSM73BeamAMember& Member,
		const FABTSM73BeamAGenerationResult& Assembly,
		const double CrossSectionCM)
	{
		const FVector Center = MemberMidpoint(Member, Assembly);
		FVector Extent(CrossSectionCM * 0.5);
		const int32 AxisIndex = static_cast<int32>(Member.Axis);
		if (AxisIndex >= 0 && AxisIndex <= 2)
		{
			Extent[AxisIndex] = Member.LengthCM * 0.5;
		}
		return FBox(Center - Extent, Center + Extent);
	}

	double OverlapLength(
		const double AMin,
		const double AMax,
		const double BMin,
		const double BMax)
	{
		return FMath::Max(0.0, FMath::Min(AMax, BMax)
			- FMath::Max(AMin, BMin));
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
				TEXT("N:%d:%d:%d:%.6f:%.6f:%.6f:%.6f:%d:%.6f:%.6f:%.6f:%.6f|"),
				Node.MemberId, static_cast<int32>(Node.Axis), Node.bGround ? 1 : 0,
				Node.SelfLoadKG, Node.AccumulatedLoadKG,
				Node.EffectiveSpanCM, Node.CantileverRatio,
				Node.RealSupportIntervalCount,
				Node.RealSupportCoverageRatio, Node.RealSupportSpanRatio,
				Node.SpanUtilization, Node.ColumnSlenderness);
		}
		for (const FABTSM73BeamCLoadEdge& Edge : Result.Edges)
		{
			Signature += FString::Printf(
				TEXT("E:%d:%d:%d:%d:%.6f:%.6f:%.6f:%.6f:%.6f:%.6f:%.6f|"),
				Edge.EdgeId, Edge.BearingContactId,
				Edge.UpperMemberId, Edge.LowerMemberId,
				Edge.ContactAreaCM2,
				Edge.ContactMinXY.X, Edge.ContactMinXY.Y,
				Edge.ContactMaxXY.X, Edge.ContactMaxXY.Y,
				Edge.LoadShare, Edge.ReactionLoadKG);
		}
		for (const int32 MemberId : Result.TopologicalMemberOrder)
		{
			Signature += FString::Printf(TEXT("T:%d|"), MemberId);
		}
		return FCrc::StrCrc32(*Signature);
	}

	struct FStructuralSupportProposal
	{
		int32 AssemblyId = INDEX_NONE;
		FVector2D Station = FVector2D::ZeroVector;
		double BottomZ = 0.0;
		double TopZ = 0.0;
	};

	bool AddStructuralSupportPosts(
		const FABTSM73BeamCPreviewSettings& Settings,
		const FABTSM73BeamCGenerationResult& Analysis,
		FABTSM73BeamAGenerationResult& Assembly,
		const int32 RemainingPostBudget,
		int32& OutAddedCount,
		FString& OutError)
	{
		OutAddedCount = 0;
		const double Section = Settings.BeamB.BeamA.BlockCrossSectionCM;
		const double HalfSection = Section * 0.5;
		const double Tolerance = Settings.BeamB.BeamA.JointMergeToleranceCM;
		TArray<FBox> Bounds;
		Bounds.Reserve(Assembly.Members.Num());
		for (const FABTSM73BeamAMember& Member : Assembly.Members)
		{
			Bounds.Add(MemberBounds(Member, Assembly, Section));
		}

		auto OwnerForMember = [&Assembly](const int32 MemberId)
		{
			for (const FABTSM73BeamAAssembly& Owner : Assembly.Assemblies)
			{
				if (Owner.MemberIds.Contains(MemberId))
				{
					return Owner.AssemblyId;
				}
			}
			return static_cast<int32>(INDEX_NONE);
		};
		auto IsReserved = [&Assembly, Tolerance](
			const FVector2D& Station,
			const double BottomZ,
			const double TopZ)
		{
			for (const FABTSM73BeamASupportVoid& SupportVoid :
				Assembly.ReservedSupportVoids)
			{
				const FBox& Void = SupportVoid.Bounds;
				if (FMath::Min(TopZ, Void.Max.Z)
						- FMath::Max(BottomZ, Void.Min.Z) > Tolerance
					&& Station.X > Void.Min.X + Tolerance
					&& Station.X < Void.Max.X - Tolerance
					&& Station.Y > Void.Min.Y + Tolerance
					&& Station.Y < Void.Max.Y - Tolerance)
				{
					return true;
				}
			}
			return false;
		};

		TArray<FStructuralSupportProposal> Proposals;
		for (const FABTSM73BeamCLoadNode& Node : Analysis.Nodes)
		{
			if (Node.bSupportResultantValid && Node.bSupportSpreadValid)
			{
				continue;
			}
			if (!Assembly.Members.IsValidIndex(Node.MemberId)
				|| !Bounds.IsValidIndex(Node.MemberId))
			{
				OutError = TEXT("BeamCStructuralClosureInvalidMember");
				return false;
			}
			const FABTSM73BeamAMember& Upper = Assembly.Members[Node.MemberId];
			if (Upper.Axis != EABTSM73BeamAFrameAxis::X
				&& Upper.Axis != EABTSM73BeamAFrameAxis::Y)
			{
				continue;
			}
			const FBox& UpperBounds = Bounds[Node.MemberId];
			const int32 AxisIndex = Upper.Axis == EABTSM73BeamAFrameAxis::X ? 0 : 1;
			const int32 CrossIndex = AxisIndex == 0 ? 1 : 0;
			TArray<FVector2D> DesiredStations;
			if (!Node.bSupportResultantValid)
			{
				DesiredStations.Add(FVector2D(
					Node.LoadResultant.X, Node.LoadResultant.Y));
			}
			if (!Node.bSupportSpreadValid)
			{
				const double UsableMinimum = UpperBounds.Min[AxisIndex] + HalfSection;
				const double UsableMaximum = UpperBounds.Max[AxisIndex] - HalfSection;
				for (const double Alpha : {0.25, 0.75})
				{
					FVector2D Station(
						UpperBounds.GetCenter().X,
						UpperBounds.GetCenter().Y);
					Station[AxisIndex] = FMath::Lerp(
						UsableMinimum, UsableMaximum, Alpha);
					Station[CrossIndex] = FMath::Clamp(
						Node.LoadResultant[CrossIndex],
						UpperBounds.Min[CrossIndex] + HalfSection,
						UpperBounds.Max[CrossIndex] - HalfSection);
					DesiredStations.Add(Station);
				}
			}

			const int32 OwnerId = OwnerForMember(Node.MemberId);
			if (!Assembly.Assemblies.IsValidIndex(OwnerId))
			{
				OutError = TEXT("BeamCStructuralClosureOwnerMissing");
				return false;
			}
			for (FVector2D Desired : DesiredStations)
			{
				Desired.X = FMath::Clamp(Desired.X,
					UpperBounds.Min.X + HalfSection,
					UpperBounds.Max.X - HalfSection);
				Desired.Y = FMath::Clamp(Desired.Y,
					UpperBounds.Min.Y + HalfSection,
					UpperBounds.Max.Y - HalfSection);
				for (const FABTSM73BeamASupportVoid& SupportVoid :
					Assembly.ReservedSupportVoids)
				{
					const FBox& Void = SupportVoid.Bounds;
					if (Desired.X <= Void.Min.X + Tolerance
						|| Desired.X >= Void.Max.X - Tolerance
						|| Desired.Y <= Void.Min.Y + Tolerance
						|| Desired.Y >= Void.Max.Y - Tolerance
						|| UpperBounds.Min.Z <= Void.Min.Z + Tolerance)
					{
						continue;
					}
					const int32 VoidAxis = SupportVoid.SpanAxisIndex == 1 ? 1 : 0;
					const double NegativeStation =
						Void.Min[VoidAxis] - HalfSection;
					const double PositiveStation =
						Void.Max[VoidAxis] + HalfSection;
					const double MinimumStation =
						UpperBounds.Min[VoidAxis] + HalfSection;
					const double MaximumStation =
						UpperBounds.Max[VoidAxis] - HalfSection;
					const bool bNegativeAvailable =
						NegativeStation >= MinimumStation - Tolerance;
					const bool bPositiveAvailable =
						PositiveStation <= MaximumStation + Tolerance;
					if (bNegativeAvailable || bPositiveAvailable)
					{
						const bool bUseNegative = bNegativeAvailable
							&& (!bPositiveAvailable
								|| FMath::Abs(Desired[VoidAxis] - NegativeStation)
									<= FMath::Abs(
										Desired[VoidAxis] - PositiveStation));
						Desired[VoidAxis] = bUseNegative
							? NegativeStation : PositiveStation;
					}
				}
				double BestTopZ = 0.0;
				double BestDistance = TNumericLimits<double>::Max();
				FVector2D BestStation = Desired;
				for (const FABTSM73BeamAMember& Lower : Assembly.Members)
				{
					if (Lower.MemberId == Node.MemberId
						|| Lower.Axis == EABTSM73BeamAFrameAxis::Z
						|| Lower.Axis == EABTSM73BeamAFrameAxis::Diagonal)
					{
						continue;
					}
					const FBox& LowerBounds = Bounds[Lower.MemberId];
					if (LowerBounds.Max.Z > UpperBounds.Min.Z - Section + Tolerance)
					{
						continue;
					}
					const double MinX = FMath::Max(
						UpperBounds.Min.X, LowerBounds.Min.X) + HalfSection;
					const double MaxX = FMath::Min(
						UpperBounds.Max.X, LowerBounds.Max.X) - HalfSection;
					const double MinY = FMath::Max(
						UpperBounds.Min.Y, LowerBounds.Min.Y) + HalfSection;
					const double MaxY = FMath::Min(
						UpperBounds.Max.Y, LowerBounds.Max.Y) - HalfSection;
					if (MinX > MaxX + Tolerance || MinY > MaxY + Tolerance)
					{
						continue;
					}
					const FVector2D Candidate(
						FMath::Clamp(Desired.X, MinX, MaxX),
						FMath::Clamp(Desired.Y, MinY, MaxY));
					const double Distance = FVector2D::DistSquared(Candidate, Desired);
					// A remote lower beam must not pull multiple requested lanes
					// onto one narrow station. If the desired full-footprint lane
					// is unavailable, retain that lane and continue to ground.
					if (Distance > FMath::Square(Tolerance))
					{
						continue;
					}
					if (LowerBounds.Max.Z > BestTopZ + Tolerance
						|| (FMath::IsNearlyEqual(LowerBounds.Max.Z, BestTopZ, Tolerance)
							&& Distance < BestDistance))
					{
						BestTopZ = LowerBounds.Max.Z;
						BestDistance = Distance;
						BestStation = Candidate;
					}
				}
				const double TopZ = UpperBounds.Min.Z;
				if (TopZ - BestTopZ + Tolerance < Section
					|| IsReserved(BestStation, BestTopZ, TopZ))
				{
					continue;
				}
				const bool bAlreadySupported = Assembly.Members.ContainsByPredicate(
					[&Bounds, &BestStation, TopZ, Tolerance](
						const FABTSM73BeamAMember& Existing)
					{
						if (Existing.Axis != EABTSM73BeamAFrameAxis::Z)
						{
							return false;
						}
						const FBox& ExistingBounds = Bounds[Existing.MemberId];
						return FVector2D::Distance(
							FVector2D(ExistingBounds.GetCenter().X,
								ExistingBounds.GetCenter().Y), BestStation) <= Tolerance
							&& FMath::Abs(ExistingBounds.Max.Z - TopZ) <= Tolerance;
					});
				if (bAlreadySupported || Proposals.ContainsByPredicate(
					[&BestStation, TopZ, Tolerance](
						const FStructuralSupportProposal& Existing)
					{
						return Existing.Station.Equals(BestStation, Tolerance)
							&& FMath::Abs(Existing.TopZ - TopZ) <= Tolerance;
					}))
				{
					continue;
				}
				if (Proposals.Num() >= RemainingPostBudget)
				{
					OutError = TEXT("BeamCStructuralSupportBudgetExceeded");
					return false;
				}
				FStructuralSupportProposal& Proposal =
					Proposals.AddDefaulted_GetRef();
				Proposal.AssemblyId = OwnerId;
				Proposal.Station = BestStation;
				Proposal.BottomZ = BestTopZ;
				Proposal.TopZ = TopZ;
			}
		}

		for (const FStructuralSupportProposal& Proposal : Proposals)
		{
			if (Assembly.Members.Num() >= Settings.BeamB.BeamA.MaxMemberCount
				|| Assembly.Joints.Num() + 2 > Settings.BeamB.BeamA.MaxJointCount)
			{
				OutError = TEXT("BeamCStructuralSupportMemberBudgetExceeded");
				return false;
			}
			const int32 JointA = Assembly.Joints.Num();
			FABTSM73BeamAJoint& A = Assembly.Joints.AddDefaulted_GetRef();
			A.JointId = JointA;
			A.LocalPosition = FVector(
				Proposal.Station.X, Proposal.Station.Y, Proposal.BottomZ);
			A.Role = EABTSM73BeamAJointRole::GroundFoot;
			const int32 JointB = Assembly.Joints.Num();
			FABTSM73BeamAJoint& B = Assembly.Joints.AddDefaulted_GetRef();
			B.JointId = JointB;
			B.LocalPosition = FVector(
				Proposal.Station.X, Proposal.Station.Y, Proposal.TopZ);
			B.Role = EABTSM73BeamAJointRole::ColumnHead;
			FABTSM73BeamAMember& Post = Assembly.Members.AddDefaulted_GetRef();
			Post.MemberId = Assembly.Members.Num() - 1;
			Post.JointA = JointA;
			Post.JointB = JointB;
			Post.Axis = EABTSM73BeamAFrameAxis::Z;
			Post.Role = EABTSM73BeamAMemberRole::Post;
			Post.LengthCM = Proposal.TopZ - Proposal.BottomZ;
			Assembly.Assemblies[Proposal.AssemblyId].MemberIds.Add(Post.MemberId);
			++OutAddedCount;
		}
		if (OutAddedCount == 0)
		{
			for (const FABTSM73BeamCLoadNode& Node : Analysis.Nodes)
			{
				if (Node.bSupportResultantValid && Node.bSupportSpreadValid)
				{
					continue;
				}
				UE_LOG(LogABTSRuntime, Warning,
					TEXT("[ABTS][M7.3-Beam-C2][ClosureStalled]")
					TEXT(" Member=%d Axis=%d Supports=%d Resultant=%s")
					TEXT(" ResultantValid=%d SpreadValid=%d Coverage=%.3f Span=%.3f"),
					Node.MemberId, static_cast<int32>(Node.Axis), Node.SupportCount,
					*Node.LoadResultant.ToCompactString(),
					Node.bSupportResultantValid ? 1 : 0,
					Node.bSupportSpreadValid ? 1 : 0,
					Node.RealSupportCoverageRatio,
					Node.RealSupportSpanRatio);
			}
		}
		return OutAddedCount > 0;
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
		|| Settings.SpanStiffnessScale <= 0.0f
		|| Settings.MinimumBearingAreaRatio <= 0.0f
		|| Settings.MinimumBearingAreaRatio > 1.0f
		|| Settings.RealContactToleranceCM <= 0.0f
		|| Settings.RealContactAreaToleranceRatio <= 0.0f
		|| Settings.MaximumSingleSupportMemberLengthRatio < 1.0f
		|| Settings.MinimumSingleSupportCoverageRatio <= 0.0f
		|| Settings.MinimumSingleSupportCoverageRatio > 1.0f
		|| Settings.MinimumSeparatedSupportSpanRatio <= 0.0f
		|| Settings.MinimumSeparatedSupportSpanRatio > 1.0f
		|| Settings.SupportResultantMarginCM < 0.0f
		|| Settings.MaximumStructuralClosurePasses <= 0
		|| Settings.MaximumStructuralSupportPosts <= 0)
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
	TArray<FBox> MemberBoxes;
	MemberBoxes.Reserve(NodeCount);
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
		MemberBoxes.Add(MemberBounds(
			Member, ClosedAssembly,
			Settings.BeamB.BeamA.BlockCrossSectionCM));
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
		const FBox& LowerBounds = MemberBoxes[Contact.LowerMemberId];
		const FBox& UpperBounds = MemberBoxes[Contact.UpperMemberId];
		const double XMinimum = FMath::Max(
			LowerBounds.Min.X, UpperBounds.Min.X);
		const double XMaximum = FMath::Min(
			LowerBounds.Max.X, UpperBounds.Max.X);
		const double YMinimum = FMath::Max(
			LowerBounds.Min.Y, UpperBounds.Min.Y);
		const double YMaximum = FMath::Min(
			LowerBounds.Max.Y, UpperBounds.Max.Y);
		const double XOverlap = OverlapLength(
			LowerBounds.Min.X, LowerBounds.Max.X,
			UpperBounds.Min.X, UpperBounds.Max.X);
		const double YOverlap = OverlapLength(
			LowerBounds.Min.Y, LowerBounds.Max.Y,
			UpperBounds.Min.Y, UpperBounds.Max.Y);
		const double ActualArea = XOverlap * YOverlap;
		const FVector ActualPosition(
			(XMinimum + XMaximum) * 0.5,
			(YMinimum + YMaximum) * 0.5,
			LowerBounds.Max.Z);
		const double AreaTolerance = FMath::Max(
			UE_DOUBLE_SMALL_NUMBER,
			ActualArea * Settings.RealContactAreaToleranceRatio);
		const bool bRealContact =
			FMath::Abs(LowerBounds.Max.Z - UpperBounds.Min.Z)
				<= Settings.RealContactToleranceCM
			&& XOverlap > 0.0 && YOverlap > 0.0
			&& FVector::Distance(ActualPosition, Contact.LocalPosition)
				<= Settings.RealContactToleranceCM
			&& FMath::Abs(ActualArea - Contact.ContactAreaCM2)
				<= AreaTolerance;
		if (!bRealContact)
		{
			if (OutResult.Summary.RealContactMismatchCount < 8)
			{
				UE_LOG(LogABTSRuntime, Warning,
					TEXT("[ABTS][M7.3-Beam-C2][RealContactMismatch]")
					TEXT(" Contact=%d Lower=%d Upper=%d DZ=%.4f DPos=%.4f")
					TEXT(" DeclaredArea=%.4f ActualArea=%.4f AreaTolerance=%.4f")
					TEXT(" XOverlap=%.4f YOverlap=%.4f"),
					Contact.ContactId,
					Contact.LowerMemberId,
					Contact.UpperMemberId,
					FMath::Abs(LowerBounds.Max.Z - UpperBounds.Min.Z),
					FVector::Distance(ActualPosition, Contact.LocalPosition),
					Contact.ContactAreaCM2,
					ActualArea,
					AreaTolerance,
					XOverlap,
					YOverlap);
			}
			++OutResult.Summary.RealContactMismatchCount;
		}
		if (XOverlap > 0.0 && YOverlap > 0.0)
		{
			Edge.ContactMinXY = FVector2D(XMinimum, YMinimum);
			Edge.ContactMaxXY = FVector2D(XMaximum, YMaximum);
		}
		else
		{
			const double HalfFallback = FMath::Sqrt(FMath::Max(
				static_cast<double>(Contact.ContactAreaCM2),
				UE_DOUBLE_SMALL_NUMBER)) * 0.5;
			Edge.ContactMinXY = FVector2D(
				Contact.LocalPosition.X - HalfFallback,
				Contact.LocalPosition.Y - HalfFallback);
			Edge.ContactMaxXY = FVector2D(
				Contact.LocalPosition.X + HalfFallback,
				Contact.LocalPosition.Y + HalfFallback);
		}
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
	if (Settings.bRequireRealContactAgreement
		&& OutResult.Summary.RealContactMismatchCount > 0)
	{
		return Reject(OutResult, OutError, TEXT("BeamCRealContactMismatch"));
	}
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
			const int32 ResultantAxisIndex =
				Member.Axis == EABTSM73BeamAFrameAxis::X ? 0 : 1;
			double SupportMinimum = TNumericLimits<double>::Max();
			double SupportMaximum = -TNumericLimits<double>::Max();
			for (const int32 EdgeIndex : SupportEdges)
			{
				const FABTSM73BeamCLoadEdge& Edge = OutResult.Edges[EdgeIndex];
				SupportMinimum = FMath::Min(SupportMinimum,
					ResultantAxisIndex == 0
						? Edge.ContactMinXY.X : Edge.ContactMinXY.Y);
				SupportMaximum = FMath::Max(SupportMaximum,
					ResultantAxisIndex == 0
						? Edge.ContactMaxXY.X : Edge.ContactMaxXY.Y);
			}
			const double ResultantCoordinate =
				Node.LoadResultant[ResultantAxisIndex];
			if (ResultantCoordinate
					< SupportMinimum + Settings.SupportResultantMarginCM
				|| ResultantCoordinate
					> SupportMaximum - Settings.SupportResultantMarginCM)
			{
				Node.bSupportResultantValid = false;
				++OutResult.Summary.SupportResultantViolationCount;
			}
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
			TArray<FSupportInterval> SupportIntervals;
			SupportIntervals.Reserve(SupportEdges.Num());
			for (const int32 EdgeIndex : SupportEdges)
			{
				const FABTSM73BeamCLoadEdge& Edge = OutResult.Edges[EdgeIndex];
				FSupportInterval& Interval =
					SupportIntervals.AddDefaulted_GetRef();
				Interval.Minimum = AxisIndex == 0
					? Edge.ContactMinXY.X : Edge.ContactMinXY.Y;
				Interval.Maximum = AxisIndex == 0
					? Edge.ContactMaxXY.X : Edge.ContactMaxXY.Y;
			}
			SupportIntervals.Sort([](
				const FSupportInterval& A,
				const FSupportInterval& B)
			{
				return A.Minimum < B.Minimum
					|| (A.Minimum == B.Minimum && A.Maximum < B.Maximum);
			});
			TArray<FSupportInterval> MergedIntervals;
			for (const FSupportInterval& Interval : SupportIntervals)
			{
				if (MergedIntervals.IsEmpty()
					|| Interval.Minimum
						> MergedIntervals.Last().Maximum + StationTolerance)
				{
					MergedIntervals.Add(Interval);
				}
				else
				{
					MergedIntervals.Last().Maximum = FMath::Max(
						MergedIntervals.Last().Maximum, Interval.Maximum);
				}
			}
			double TotalSupportLength = 0.0;
			for (const FSupportInterval& Interval : MergedIntervals)
			{
				TotalSupportLength += FMath::Max(
					0.0, Interval.Maximum - Interval.Minimum);
			}
			const double SupportSpan = MergedIntervals.IsEmpty()
				? 0.0
				: MergedIntervals.Last().Maximum
					- MergedIntervals[0].Minimum;
			Node.RealSupportIntervalCount = MergedIntervals.Num();
			Node.RealSupportCoverageRatio = static_cast<float>(
				TotalSupportLength
				/ FMath::Max(static_cast<double>(Member.LengthCM), 1.0));
			Node.RealSupportSpanRatio = static_cast<float>(
				SupportSpan
				/ FMath::Max(static_cast<double>(Member.LengthCM), 1.0));
			const double LongMemberThreshold =
				Settings.BeamB.BeamA.BlockCrossSectionCM
					* Settings.MaximumSingleSupportMemberLengthRatio;
			if (Member.LengthCM > LongMemberThreshold)
			{
				const bool bHasHorizontalBearing = SupportEdges.ContainsByPredicate(
					[&OutResult, &ClosedAssembly](const int32 EdgeIndex)
					{
						const int32 LowerId = OutResult.Edges[EdgeIndex].LowerMemberId;
						return ClosedAssembly.Members.IsValidIndex(LowerId)
							&& ClosedAssembly.Members[LowerId].Axis
								!= EABTSM73BeamAFrameAxis::Z
							&& ClosedAssembly.Members[LowerId].Axis
								!= EABTSM73BeamAFrameAxis::Diagonal;
					});
				const bool bContinuousBearing = MergedIntervals.Num() == 1
					&& (Node.RealSupportCoverageRatio
							>= Settings.MinimumSingleSupportCoverageRatio
						|| (bHasHorizontalBearing
							&& Node.bSupportResultantValid));
				const bool bSeparatedBearing = MergedIntervals.Num() >= 2
					&& Node.RealSupportSpanRatio
						>= Settings.MinimumSeparatedSupportSpanRatio;
				if (!bContinuousBearing && !bSeparatedBearing)
				{
					Node.bSupportSpreadValid = false;
					++OutResult.Summary.SupportSpreadViolationCount;
				}
			}
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
			double EffectiveSpan = MergedIntervals.IsEmpty()
				? Member.LengthCM
				: FMath::Max(
					FMath::Max(0.0, MergedIntervals[0].Minimum - Minimum),
					FMath::Max(0.0, Maximum - MergedIntervals.Last().Maximum));
			for (int32 IntervalIndex = 0;
				IntervalIndex + 1 < MergedIntervals.Num(); ++IntervalIndex)
			{
				EffectiveSpan = FMath::Max(EffectiveSpan,
					MergedIntervals[IntervalIndex + 1].Minimum
						- MergedIntervals[IntervalIndex].Maximum);
			}
			const double Overhang = MergedIntervals.IsEmpty()
				? Member.LengthCM
				: FMath::Max(
					FMath::Max(0.0, MergedIntervals[0].Minimum - Minimum),
					FMath::Max(0.0, Maximum - MergedIntervals.Last().Maximum));
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
	if (OutResult.Summary.SupportResultantViolationCount > 0)
	{
		return Reject(OutResult, OutError,
			TEXT("BeamCSupportResultantOutsideHull"));
	}
	if (OutResult.Summary.SupportSpreadViolationCount > 0)
	{
		return Reject(OutResult, OutError,
			TEXT("BeamCSupportSpreadInsufficient"));
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

bool FABTSM73BeamCGenerator::GenerateWithStructuralClosure(
	const FABTSM73BeamCPreviewSettings& Settings,
	FABTSM73BeamAGenerationResult& InOutClosedAssembly,
	FABTSM73BeamCGenerationResult& OutResult,
	FString& OutError) const
{
	using namespace ABTSM73BeamC;
	int32 TotalAddedPosts = 0;
	for (int32 Pass = 0; Pass <= Settings.MaximumStructuralClosurePasses; ++Pass)
	{
		if (Generate(Settings, InOutClosedAssembly, OutResult, OutError))
		{
			OutResult.Summary.StructuralClosurePassCount = Pass;
			OutResult.Summary.AddedStructuralSupportPostCount = TotalAddedPosts;
			return true;
		}
		const bool bRepairable =
			OutResult.Summary.SupportResultantViolationCount > 0
			|| OutResult.Summary.SupportSpreadViolationCount > 0;
		const bool bResultantOnlyAfterRepair = Pass > 0
			&& OutResult.Summary.SupportResultantViolationCount > 0
			&& OutResult.Summary.SupportSpreadViolationCount == 0
			&& OutResult.Summary.ReactionBalanceViolationCount == 0
			&& OutResult.Summary.SpanViolationCount == 0
			&& OutResult.Summary.CantileverViolationCount == 0
			&& OutResult.Summary.ColumnSlendernessViolationCount == 0
			&& OutResult.Summary.LateralMechanismViolationCount == 0;
		if (bResultantOnlyAfterRepair)
		{
			OutResult.Summary.StructuralClosurePassCount = Pass;
			OutResult.Summary.AddedStructuralSupportPostCount = TotalAddedPosts;
			OutResult.Summary.SupportResultantAdvisoryCount =
				OutResult.Summary.SupportResultantViolationCount;
			OutResult.Summary.SupportResultantViolationCount = 0;
			OutResult.Summary.bAccepted = true;
			OutResult.Summary.RejectReason.Reset();
			OutError.Reset();
			OutResult.Summary.LoadDAGHash = static_cast<int64>(
				HashResult(OutResult));
			return true;
		}
		if (!bRepairable || Pass == Settings.MaximumStructuralClosurePasses)
		{
			OutResult.Summary.StructuralClosurePassCount = Pass;
			OutResult.Summary.AddedStructuralSupportPostCount = TotalAddedPosts;
			if (bRepairable)
			{
				for (const FABTSM73BeamCLoadNode& Node : OutResult.Nodes)
				{
					if (Node.bSupportResultantValid && Node.bSupportSpreadValid)
					{
						continue;
					}
					UE_LOG(LogABTSRuntime, Warning,
						TEXT("[ABTS][M7.3-Beam-C2][ClosureExhausted]")
						TEXT(" Pass=%d Added=%d Member=%d Axis=%d Supports=%d")
						TEXT(" Resultant=%s ResultantValid=%d SpreadValid=%d")
						TEXT(" Coverage=%.3f Span=%.3f"),
						Pass, TotalAddedPosts, Node.MemberId,
						static_cast<int32>(Node.Axis), Node.SupportCount,
						*Node.LoadResultant.ToCompactString(),
						Node.bSupportResultantValid ? 1 : 0,
						Node.bSupportSpreadValid ? 1 : 0,
						Node.RealSupportCoverageRatio,
						Node.RealSupportSpanRatio);
				}
			}
			return false;
		}
		int32 AddedThisPass = 0;
		FString RepairError;
		if (!AddStructuralSupportPosts(
			Settings, OutResult, InOutClosedAssembly,
			Settings.MaximumStructuralSupportPosts - TotalAddedPosts,
			AddedThisPass, RepairError))
		{
			OutError = RepairError.IsEmpty()
				? TEXT("BeamCStructuralClosureStalled") : RepairError;
			return false;
		}
		TotalAddedPosts += AddedThisPass;
		if (!ABTSM73BeamA::CloseGeneratedAssembly(
			Settings.BeamB.BeamA, InOutClosedAssembly, RepairError))
		{
			OutError = FString::Printf(
				TEXT("BeamCStructuralReclose:%s"), *RepairError);
			return false;
		}
	}
	return false;
}
