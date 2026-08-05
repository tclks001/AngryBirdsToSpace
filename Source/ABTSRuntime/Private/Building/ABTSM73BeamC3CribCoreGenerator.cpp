// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSM73BeamC3CribCoreGenerator.h"

#include "ABTSRuntime.h"
#include "Misc/Crc.h"

namespace ABTSM73BeamC3
{
	struct FVerticalInterval
	{
		double MinimumZ = 0.0;
		double MaximumZ = 0.0;
		/** Z members physically connected into this interval. */
		TSet<int32> MemberIds;
	};

	struct FPostStation
	{
		FVector2D Position = FVector2D::ZeroVector;
		TArray<int32> MemberIds;
		TArray<FVerticalInterval> ContinuousIntervals;
		TSet<int32> BayIds;
		TSet<int32> SourceVolumeIds;
	};

	struct FCoreHost
	{
		int32 OriginStation = INDEX_NONE;
		int32 XStation = INDEX_NONE;
		int32 YStation = INDEX_NONE;
		int32 DiagonalStation = INDEX_NONE;
		int32 BayId = INDEX_NONE;
		int32 SourceVolumeId = INDEX_NONE;
		double MinimumZ = 0.0;
		double MaximumZ = 0.0;
		double Score = -TNumericLimits<double>::Max();
		FString Signature;
	};

	struct FUnbracedZViolation
	{
		float SpanCM = 0.0f;
		FVector2D Station = FVector2D::ZeroVector;
		double MinimumZ = 0.0;
		double MaximumZ = 0.0;
		EABTSM73BeamAFrameAxis MissingBraceAxis =
			EABTSM73BeamAFrameAxis::Z;
		int32 MemberId = INDEX_NONE;
	};

	FVector MemberCenter(
		const FABTSM73BeamAMember& Member,
		const FABTSM73BeamAGenerationResult& Assembly)
	{
		return (Assembly.Joints[Member.JointA].LocalPosition
			+ Assembly.Joints[Member.JointB].LocalPosition) * 0.5;
	}

	FBox MemberBounds(
		const FABTSM73BeamAMember& Member,
		const FABTSM73BeamAGenerationResult& Assembly,
		const double Section)
	{
		FVector Extent(Section * 0.5);
		Extent[static_cast<int32>(Member.Axis)] = Member.LengthCM * 0.5;
		const FVector Center = MemberCenter(Member, Assembly);
		return FBox(Center - Extent, Center + Extent);
	}

	int32 CountReusableCoreCourses(
		const FABTSM73BeamAGenerationResult& Assembly,
		const FVector2D& Origin,
		const FVector2D& XStation,
		const FVector2D& YStation,
		const double MidZ,
		const double Section,
		const double Tolerance)
	{
		const double XMinimum = FMath::Min(Origin.X, XStation.X);
		const double XMaximum = FMath::Max(Origin.X, XStation.X);
		const double YMinimum = FMath::Min(Origin.Y, YStation.Y);
		const double YMaximum = FMath::Max(Origin.Y, YStation.Y);
		const double XZ = MidZ - Section * 0.5;
		const double YZ = MidZ + Section * 0.5;
		const double ConstantCoordinates[4] = {
			Origin.Y, YStation.Y, Origin.X, XStation.X};
		const double CenterZs[4] = {XZ, XZ, YZ, YZ};
		const EABTSM73BeamAFrameAxis Axes[4] = {
			EABTSM73BeamAFrameAxis::X, EABTSM73BeamAFrameAxis::X,
			EABTSM73BeamAFrameAxis::Y, EABTSM73BeamAFrameAxis::Y};
		int32 ReusedSides = 0;
		for (int32 Side = 0; Side < 4; ++Side)
		{
			const int32 AxisIndex = static_cast<int32>(Axes[Side]);
			const int32 OtherAxisIndex =
				Axes[Side] == EABTSM73BeamAFrameAxis::X ? 1 : 0;
			const double SpanMinimum =
				Axes[Side] == EABTSM73BeamAFrameAxis::X
					? XMinimum : YMinimum;
			const double SpanMaximum =
				Axes[Side] == EABTSM73BeamAFrameAxis::X
					? XMaximum : YMaximum;
			const bool bCovered = Assembly.Members.ContainsByPredicate(
				[&Assembly, Axes, Side, OtherAxisIndex, AxisIndex,
					ConstantCoordinates, CenterZs, SpanMinimum, SpanMaximum,
					Section, Tolerance](const FABTSM73BeamAMember& Member)
				{
					if (Member.Axis != Axes[Side])
					{
						return false;
					}
					const FVector Center = MemberCenter(Member, Assembly);
					const FBox Bounds = MemberBounds(Member, Assembly, Section);
					return FMath::Abs(
							Center[OtherAxisIndex] - ConstantCoordinates[Side])
							<= Tolerance
						&& FMath::Abs(Center.Z - CenterZs[Side]) <= Tolerance
						&& Bounds.Min[AxisIndex] <= SpanMinimum + Tolerance
						&& Bounds.Max[AxisIndex] >= SpanMaximum - Tolerance;
				});
			ReusedSides += bCovered ? 1 : 0;
		}
		return ReusedSides;
	}

	int32 BestReusableCoreCourseCount(
		const FABTSM73BeamAGenerationResult& Assembly,
		const FVector2D& Origin,
		const FVector2D& XStation,
		const FVector2D& YStation,
		const double MinimumZ,
		const double MaximumZ,
		const double Section,
		const double Tolerance)
	{
		int32 Best = 0;
		const double QuantizationStep = FMath::Max(Tolerance, 0.01);
		TSet<int64> CandidateHeightKeys;
		TArray<double> CandidateMidZs;
		for (const FABTSM73BeamAMember& Member : Assembly.Members)
		{
			if (Member.Axis != EABTSM73BeamAFrameAxis::X
				&& Member.Axis != EABTSM73BeamAFrameAxis::Y)
			{
				continue;
			}
			const double CenterZ = MemberCenter(Member, Assembly).Z;
			const double MidZ = Member.Axis == EABTSM73BeamAFrameAxis::X
				? CenterZ + Section * 0.5
				: CenterZ - Section * 0.5;
			if (MidZ <= MinimumZ + Section * 2.0
				|| MidZ >= MaximumZ - Section * 2.0)
			{
				continue;
			}
			const int64 HeightKey = FMath::RoundToInt64(
				MidZ / QuantizationStep);
			if (!CandidateHeightKeys.Contains(HeightKey))
			{
				CandidateHeightKeys.Add(HeightKey);
				CandidateMidZs.Add(MidZ);
			}
		}
		CandidateMidZs.Sort();
		for (const double MidZ : CandidateMidZs)
		{
			Best = FMath::Max(Best, CountReusableCoreCourses(
				Assembly, Origin, XStation, YStation,
				MidZ, Section, Tolerance));
			if (Best == 4)
			{
				break;
			}
		}
		return Best;
	}

	void BuildPostStations(
		const FABTSM73BeamAGenerationResult& Assembly,
		const double Tolerance,
		const double Section,
		TArray<FPostStation>& OutStations)
	{
		TMap<int32, TSet<int32>> BayIdsByMember;
		TMap<int32, TSet<int32>> SourceVolumeIdsByMember;
		for (const FABTSM73BeamAAssembly& MemberAssembly : Assembly.Assemblies)
		{
			const int32 BayId = MemberAssembly.BayId;
			const int32 SourceVolumeId = Assembly.Bays.IsValidIndex(BayId)
				? Assembly.Bays[BayId].SourceVolumeId : INDEX_NONE;
			for (const int32 MemberId : MemberAssembly.MemberIds)
			{
				BayIdsByMember.FindOrAdd(MemberId).Add(BayId);
				SourceVolumeIdsByMember.FindOrAdd(MemberId).Add(SourceVolumeId);
			}
		}
		OutStations.Reset();
		for (const FABTSM73BeamAMember& Member : Assembly.Members)
		{
			if (Member.Axis != EABTSM73BeamAFrameAxis::Z
				|| !Assembly.Joints.IsValidIndex(Member.JointA)
				|| !Assembly.Joints.IsValidIndex(Member.JointB))
			{
				continue;
			}
			const FVector Center = MemberCenter(Member, Assembly);
			int32 StationIndex = OutStations.IndexOfByPredicate(
				[&Center, Tolerance](const FPostStation& Station)
				{
					return FMath::Abs(Station.Position.X - Center.X) <= Tolerance
						&& FMath::Abs(Station.Position.Y - Center.Y) <= Tolerance;
				});
			if (StationIndex == INDEX_NONE)
			{
				StationIndex = OutStations.AddDefaulted();
				OutStations[StationIndex].Position = FVector2D(Center.X, Center.Y);
			}
			FPostStation& Station = OutStations[StationIndex];
			const FVector A = Assembly.Joints[Member.JointA].LocalPosition;
			const FVector B = Assembly.Joints[Member.JointB].LocalPosition;
			Station.MemberIds.Add(Member.MemberId);
			FVerticalInterval& Interval =
				Station.ContinuousIntervals.AddDefaulted_GetRef();
			Interval.MinimumZ = FMath::Min(A.Z, B.Z);
			Interval.MaximumZ = FMath::Max(A.Z, B.Z);
			Interval.MemberIds.Add(Member.MemberId);
			if (const TSet<int32>* BayIds = BayIdsByMember.Find(Member.MemberId))
			{
				Station.BayIds.Append(*BayIds);
			}
			if (const TSet<int32>* SourceIds =
				SourceVolumeIdsByMember.Find(Member.MemberId))
			{
				Station.SourceVolumeIds.Append(*SourceIds);
			}
		}
		for (FPostStation& Station : OutStations)
		{
			auto HasPhysicalVerticalBridge = [&Assembly, &Station, Section,
				Tolerance](const FVerticalInterval& Lower,
					const FVerticalInterval& Upper)
			{
				// A floor may separate two Z pieces by one or two orthogonal
				// courses. Treat the station as continuous only when the actual
				// bearing graph connects the lower post to the upper post through
				// those courses. A distance-only merge admits an empty gap and can
				// make a visually floating corner look reusable to core selection.
				TSet<int32> Frontier = Lower.MemberIds;
				TSet<int32> Visited = Frontier;
				for (int32 Depth = 0; Depth < 3 && !Frontier.IsEmpty(); ++Depth)
				{
					TSet<int32> Next;
					for (const FABTSM73BeamABearingContact& Contact :
						Assembly.BearingContacts)
					{
						if (!Frontier.Contains(Contact.LowerMemberId)
							|| FVector2D::Distance(
								FVector2D(Contact.LocalPosition.X,
									Contact.LocalPosition.Y),
								Station.Position) > Section + Tolerance)
						{
							continue;
						}
						const int32 UpperMemberId = Contact.UpperMemberId;
						if (Upper.MemberIds.Contains(UpperMemberId))
						{
							return true;
						}
						if (!Assembly.Members.IsValidIndex(UpperMemberId))
						{
							continue;
						}
						const EABTSM73BeamAFrameAxis Axis =
							Assembly.Members[UpperMemberId].Axis;
						if ((Axis == EABTSM73BeamAFrameAxis::X
								|| Axis == EABTSM73BeamAFrameAxis::Y)
							&& !Visited.Contains(UpperMemberId))
						{
							Visited.Add(UpperMemberId);
							Next.Add(UpperMemberId);
						}
					}
					Frontier = MoveTemp(Next);
				}
				return false;
			};
			Station.ContinuousIntervals.Sort(
				[](const FVerticalInterval& A, const FVerticalInterval& B)
				{
					return A.MinimumZ < B.MinimumZ;
				});
			TArray<FVerticalInterval> Merged;
			for (const FVerticalInterval& Interval : Station.ContinuousIntervals)
			{
				const bool bOverlapsOrTouches = !Merged.IsEmpty()
					&& Interval.MinimumZ
						<= Merged.Last().MaximumZ + Tolerance;
				const bool bPhysicallyBridged = !Merged.IsEmpty()
					&& Interval.MinimumZ
						<= Merged.Last().MaximumZ + Section * 2.0 + Tolerance
					&& HasPhysicalVerticalBridge(Merged.Last(), Interval);
				if (Merged.IsEmpty()
					|| (!bOverlapsOrTouches && !bPhysicallyBridged))
				{
					Merged.Add(Interval);
				}
				else
				{
					Merged.Last().MaximumZ = FMath::Max(
						Merged.Last().MaximumZ, Interval.MaximumZ);
					Merged.Last().MemberIds.Append(Interval.MemberIds);
				}
			}
			Station.ContinuousIntervals = MoveTemp(Merged);
		}
		OutStations.Sort([](const FPostStation& A, const FPostStation& B)
		{
			if (!FMath::IsNearlyEqual(A.Position.X, B.Position.X))
			{
				return A.Position.X < B.Position.X;
			}
			return A.Position.Y < B.Position.Y;
		});
	}

	bool FindCommonContinuousInterval(
		const FPostStation& A,
		const FPostStation& B,
		const FPostStation& C,
		const FPostStation& D,
		const double MinimumHeight,
		double& OutMinimumZ,
		double& OutMaximumZ)
	{
		bool bFound = false;
		double BestHeight = -1.0;
		for (const FVerticalInterval& IA : A.ContinuousIntervals)
		for (const FVerticalInterval& IB : B.ContinuousIntervals)
		for (const FVerticalInterval& IC : C.ContinuousIntervals)
		for (const FVerticalInterval& ID : D.ContinuousIntervals)
		{
			const double MinimumZ = FMath::Max(
				FMath::Max(IA.MinimumZ, IB.MinimumZ),
				FMath::Max(IC.MinimumZ, ID.MinimumZ));
			const double MaximumZ = FMath::Min(
				FMath::Min(IA.MaximumZ, IB.MaximumZ),
				FMath::Min(IC.MaximumZ, ID.MaximumZ));
			const double Height = MaximumZ - MinimumZ;
			if (Height >= MinimumHeight && Height > BestHeight)
			{
				bFound = true;
				BestHeight = Height;
				OutMinimumZ = MinimumZ;
				OutMaximumZ = MaximumZ;
			}
		}
		return bFound;
	}

	bool SelectCoreHost(
		const FABTSM73BeamC3CribCoreSettings& Settings,
		const FABTSM73BeamAPreviewSettings& BeamASettings,
		const FABTSM73BeamAGenerationResult& Assembly,
		const TArray<FPostStation>& Stations,
		const FUnbracedZViolation* TargetViolation,
		const TSet<FString>& ExcludedHostSignatures,
		FCoreHost& OutHost)
	{
		// TryAppendHost calls this repeatedly after excluding a rejected
		// rectangle.  Do not retain the previous winner: its non-default score
		// would prevent every lower-scoring remaining candidate from replacing it
		// and its still-valid indices would make the caller's retry loop spin
		// forever on the already excluded host.
		OutHost = FCoreHost();
		FBox AssemblyBounds(EForceInit::ForceInit);
		for (const FABTSM73BeamAJoint& Joint : Assembly.Joints)
		{
			AssemblyBounds += Joint.LocalPosition;
		}
		const FVector2D Center(
			AssemblyBounds.GetCenter().X, AssemblyBounds.GetCenter().Y);
		const double AlignmentTolerance = FMath::Max(
			BeamASettings.JointMergeToleranceCM,
			BeamASettings.BlockCrossSectionCM * 0.15);
		const double StationCellSize = FMath::Max(AlignmentTolerance, 0.01);
		TMap<FIntPoint, TArray<int32>> StationIndicesByCell;
		for (int32 StationIndex = 0; StationIndex < Stations.Num(); ++StationIndex)
		{
			const FVector2D& Position = Stations[StationIndex].Position;
			StationIndicesByCell.FindOrAdd(FIntPoint(
				FMath::FloorToInt(Position.X / StationCellSize),
				FMath::FloorToInt(Position.Y / StationCellSize))).Add(StationIndex);
		}
		auto FindStationIndex = [&Stations, &StationIndicesByCell,
			AlignmentTolerance, StationCellSize](const FVector2D& Position)
		{
			const FIntPoint CenterCell(
				FMath::FloorToInt(Position.X / StationCellSize),
				FMath::FloorToInt(Position.Y / StationCellSize));
			int32 BestIndex = INDEX_NONE;
			for (int32 DX = -1; DX <= 1; ++DX)
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				if (const TArray<int32>* CellIndices =
					StationIndicesByCell.Find(CenterCell + FIntPoint(DX, DY)))
				{
					for (const int32 CandidateIndex : *CellIndices)
					{
						if (Stations[CandidateIndex].Position.Equals(
							Position, AlignmentTolerance)
							&& (BestIndex == INDEX_NONE || CandidateIndex < BestIndex))
						{
							BestIndex = CandidateIndex;
						}
					}
				}
			}
			return BestIndex;
		};
		auto CommonId = [](const TSet<int32>& A, const TSet<int32>& B,
			const TSet<int32>& C, const TSet<int32>& D) -> int32
		{
			TArray<int32> Sorted = A.Array();
			Sorted.Sort();
			for (const int32 Id : Sorted)
			{
				if (Id != INDEX_NONE && B.Contains(Id)
					&& C.Contains(Id) && D.Contains(Id))
				{
					return Id;
				}
			}
			return INDEX_NONE;
		};
		for (int32 OriginIndex = 0; OriginIndex < Stations.Num(); ++OriginIndex)
		{
			const FPostStation& Origin = Stations[OriginIndex];
			TArray<int32> XCandidateIndices;
			TArray<int32> YCandidateIndices;
			for (int32 CandidateIndex = 0;
				CandidateIndex < Stations.Num(); ++CandidateIndex)
			{
				if (CandidateIndex == OriginIndex)
				{
					continue;
				}
				const FPostStation& Candidate = Stations[CandidateIndex];
				if (FMath::Abs(Candidate.Position.Y - Origin.Position.Y)
						<= AlignmentTolerance
					&& FMath::Abs(Candidate.Position.X - Origin.Position.X)
						>= Settings.MinimumCoreArmSpanCM)
				{
					XCandidateIndices.Add(CandidateIndex);
				}
				if (FMath::Abs(Candidate.Position.X - Origin.Position.X)
						<= AlignmentTolerance
					&& FMath::Abs(Candidate.Position.Y - Origin.Position.Y)
						>= Settings.MinimumCoreArmSpanCM)
				{
					YCandidateIndices.Add(CandidateIndex);
				}
			}
			for (const int32 XIndex : XCandidateIndices)
			{
				const FPostStation& XStation = Stations[XIndex];
				const double XSpan = FMath::Abs(
					XStation.Position.X - Origin.Position.X);
				for (const int32 YIndex : YCandidateIndices)
				{
					const FPostStation& YStation = Stations[YIndex];
					const double YSpan = FMath::Abs(
						YStation.Position.Y - Origin.Position.Y);
					if (YIndex == XIndex)
					{
						continue;
					}
					const FVector2D DiagonalPosition(
						XStation.Position.X, YStation.Position.Y);
					if (TargetViolation != nullptr
						&& TargetViolation->SpanCM
							> Settings.MaximumUnbracedCorePostSpanCM
						&& !Origin.Position.Equals(
							TargetViolation->Station, AlignmentTolerance)
						&& !XStation.Position.Equals(
							TargetViolation->Station, AlignmentTolerance)
						&& !YStation.Position.Equals(
							TargetViolation->Station, AlignmentTolerance)
						&& !DiagonalPosition.Equals(
							TargetViolation->Station, AlignmentTolerance))
					{
						continue;
					}
					const int32 DiagonalIndex =
						FindStationIndex(DiagonalPosition);
					if (DiagonalIndex == INDEX_NONE
						|| DiagonalIndex == OriginIndex
						|| DiagonalIndex == XIndex || DiagonalIndex == YIndex)
					{
						continue;
					}
					const FPostStation& Diagonal = Stations[DiagonalIndex];
					int32 CommonBayId = CommonId(
						Origin.BayIds, XStation.BayIds,
						YStation.BayIds, Diagonal.BayIds);
					const int32 CommonSourceVolumeId = CommonId(
						Origin.SourceVolumeIds, XStation.SourceVolumeIds,
						YStation.SourceVolumeIds, Diagonal.SourceVolumeIds);
					if (CommonSourceVolumeId == INDEX_NONE)
					{
						continue;
					}
					const double HostMinX = FMath::Min(
						Origin.Position.X, XStation.Position.X);
					const double HostMaxX = FMath::Max(
						Origin.Position.X, XStation.Position.X);
					const double HostMinY = FMath::Min(
						Origin.Position.Y, YStation.Position.Y);
					const double HostMaxY = FMath::Max(
						Origin.Position.Y, YStation.Position.Y);
					const FString HostSignature = FString::Printf(
						TEXT("%d:%.3f:%.3f:%.3f:%.3f"), CommonSourceVolumeId,
						HostMinX, HostMaxX, HostMinY, HostMaxY);
					if (ExcludedHostSignatures.Contains(HostSignature))
					{
						continue;
					}
					if (CommonBayId == INDEX_NONE)
					{
						TArray<int32> OriginBayIds = Origin.BayIds.Array();
						OriginBayIds.Sort();
						for (const int32 CandidateBayId : OriginBayIds)
						{
							if (Assembly.Bays.IsValidIndex(CandidateBayId)
								&& Assembly.Bays[CandidateBayId].SourceVolumeId
									== CommonSourceVolumeId)
							{
								CommonBayId = CandidateBayId;
								break;
							}
						}
					}
					if (CommonBayId == INDEX_NONE)
					{
						continue;
					}
					double MinimumZ = 0.0;
					double MaximumZ = 0.0;
					if (!FindCommonContinuousInterval(
						Origin, XStation, YStation, Diagonal,
						BeamASettings.BlockCrossSectionCM * 5.0,
						MinimumZ, MaximumZ))
					{
						continue;
					}
					const double Height = MaximumZ - MinimumZ;
					const int32 ReusableCourseCount =
						BestReusableCoreCourseCount(
							Assembly, Origin.Position, XStation.Position,
							YStation.Position, MinimumZ, MaximumZ,
							BeamASettings.BlockCrossSectionCM,
							AlignmentTolerance);
					const double DistanceToCenter = FVector2D::Distance(
						Origin.Position, Center);
					// Prefer a tall, broad ring inside one semantic source volume. The
					// four sides then brace both the corner posts and any intermediate
					// edge stations instead of protecting only a tiny decorative core.
					const double Score = ReusableCourseCount * 1000000.0
						+ Height * 100.0
						+ (XSpan + YSpan) * 2.0
						- FMath::Abs(XSpan - YSpan) * 0.5
						- DistanceToCenter;
					if (Score > OutHost.Score)
					{
						OutHost.OriginStation = OriginIndex;
						OutHost.XStation = XIndex;
						OutHost.YStation = YIndex;
						OutHost.DiagonalStation = DiagonalIndex;
						OutHost.BayId = CommonBayId;
						OutHost.SourceVolumeId = CommonSourceVolumeId;
						OutHost.MinimumZ = MinimumZ;
						OutHost.MaximumZ = MaximumZ;
						OutHost.Score = Score;
						OutHost.Signature = HostSignature;
					}
				}
			}
		}
		return OutHost.OriginStation != INDEX_NONE
			&& OutHost.DiagonalStation != INDEX_NONE;
	}

	int32 AddJoint(
		FABTSM73BeamAGenerationResult& Assembly,
		const FVector& Position,
		const EABTSM73BeamAJointRole Role)
	{
		FABTSM73BeamAJoint& Joint = Assembly.Joints.AddDefaulted_GetRef();
		Joint.JointId = Assembly.Joints.Num() - 1;
		Joint.LocalPosition = Position;
		Joint.Role = Role;
		return Joint.JointId;
	}

	int32 AddCoreMember(
		FABTSM73BeamAGenerationResult& Assembly,
		FABTSM73BeamAAssembly& CoreAssembly,
		const FVector& Start,
		const FVector& End,
		const EABTSM73BeamAFrameAxis Axis)
	{
		FABTSM73BeamAMember& Member = Assembly.Members.AddDefaulted_GetRef();
		Member.MemberId = Assembly.Members.Num() - 1;
		Member.JointA = AddJoint(
			Assembly, Start, EABTSM73BeamAJointRole::CrossBearing);
		Member.JointB = AddJoint(
			Assembly, End, EABTSM73BeamAJointRole::CrossBearing);
		Member.Axis = Axis;
		Member.Role = EABTSM73BeamAMemberRole::CoreCourse;
		Member.LengthCM = FVector::Distance(Start, End);
		CoreAssembly.MemberIds.Add(Member.MemberId);
		CoreAssembly.JointIds.Add(Member.JointA);
		CoreAssembly.JointIds.Add(Member.JointB);
		return Member.MemberId;
	}

	int32 AddSyntheticCorePost(
		FABTSM73BeamAGenerationResult& Assembly,
		FABTSM73BeamAAssembly& CoreAssembly,
		const FVector2D& Station,
		const double MinimumZ,
		const double MaximumZ)
	{
		if (MaximumZ <= MinimumZ)
		{
			return INDEX_NONE;
		}
		FABTSM73BeamAMember& Member = Assembly.Members.AddDefaulted_GetRef();
		Member.MemberId = Assembly.Members.Num() - 1;
		Member.JointA = AddJoint(Assembly,
			FVector(Station.X, Station.Y, MinimumZ),
			MinimumZ <= 1.0
				? EABTSM73BeamAJointRole::GroundFoot
				: EABTSM73BeamAJointRole::BeamEnd);
		Member.JointB = AddJoint(Assembly,
			FVector(Station.X, Station.Y, MaximumZ),
			EABTSM73BeamAJointRole::ColumnHead);
		Member.Axis = EABTSM73BeamAFrameAxis::Z;
		Member.Role = EABTSM73BeamAMemberRole::CorePost;
		Member.LengthCM = static_cast<float>(MaximumZ - MinimumZ);
		CoreAssembly.MemberIds.Add(Member.MemberId);
		CoreAssembly.JointIds.Add(Member.JointA);
		CoreAssembly.JointIds.Add(Member.JointB);
		return Member.MemberId;
	}

	int32 AddOrReuseCoreMember(
		FABTSM73BeamAGenerationResult& Assembly,
		FABTSM73BeamAAssembly& CoreAssembly,
		const FVector& Start,
		const FVector& End,
		const EABTSM73BeamAFrameAxis Axis,
		const double Section,
		const double Tolerance,
		bool& bOutInserted)
	{
		const int32 AxisIndex = static_cast<int32>(Axis);
		const FVector DesiredCenter = (Start + End) * 0.5;
		const double RequiredMinimum =
			FMath::Min(Start[AxisIndex], End[AxisIndex]) + Section * 0.5;
		const double RequiredMaximum =
			FMath::Max(Start[AxisIndex], End[AxisIndex]) - Section * 0.5;
		for (FABTSM73BeamAMember& Member : Assembly.Members)
		{
			if (Member.Axis != Axis)
			{
				continue;
			}
			const FVector ExistingCenter = MemberCenter(Member, Assembly);
			bool bAligned = true;
			for (int32 Coordinate = 0; Coordinate < 3; ++Coordinate)
			{
				if (Coordinate != AxisIndex
					&& FMath::Abs(ExistingCenter[Coordinate]
						- DesiredCenter[Coordinate]) > Tolerance)
				{
					bAligned = false;
					break;
				}
			}
			const FBox ExistingBounds = MemberBounds(Member, Assembly, Section);
			if (bAligned
				&& ExistingBounds.Min[AxisIndex] <= RequiredMinimum + Tolerance
				&& ExistingBounds.Max[AxisIndex] >= RequiredMaximum - Tolerance)
			{
				Member.Role = EABTSM73BeamAMemberRole::CoreCourse;
				CoreAssembly.MemberIds.AddUnique(Member.MemberId);
				CoreAssembly.JointIds.AddUnique(Member.JointA);
				CoreAssembly.JointIds.AddUnique(Member.JointB);
				bOutInserted = false;
				return Member.MemberId;
			}
		}
		bOutInserted = true;
		return AddCoreMember(Assembly, CoreAssembly, Start, End, Axis);
	}

	FBox CourseBounds(
		const FVector& Start,
		const FVector& End,
		const EABTSM73BeamAFrameAxis Axis,
		const double Section)
	{
		const FVector Center = (Start + End) * 0.5;
		FVector Extent(Section * 0.5);
		Extent[static_cast<int32>(Axis)] = FVector::Distance(Start, End) * 0.5;
		return FBox(Center - Extent, Center + Extent);
	}

	bool HasPositiveOverlap(
		const FBox& A,
		const FBox& B,
		const double Tolerance)
	{
		return FMath::Min(A.Max.X, B.Max.X)
			- FMath::Max(A.Min.X, B.Min.X) > Tolerance
			&& FMath::Min(A.Max.Y, B.Max.Y)
			- FMath::Max(A.Min.Y, B.Min.Y) > Tolerance
			&& FMath::Min(A.Max.Z, B.Max.Z)
			- FMath::Max(A.Min.Z, B.Min.Z) > Tolerance;
	}

	bool IntersectsReservedSupportVoid(
		const FABTSM73BeamAGenerationResult& Assembly,
		const FBox& Bounds,
		const int32 SourceVolumeId,
		const double Tolerance)
	{
		for (const FABTSM73BeamASupportVoid& SupportVoid :
			Assembly.ReservedSupportVoids)
		{
			if ((SupportVoid.SpanSourceVolumeId == INDEX_NONE
					|| SupportVoid.SpanSourceVolumeId == SourceVolumeId)
				&& HasPositiveOverlap(Bounds, SupportVoid.Bounds, Tolerance))
			{
				return true;
			}
		}
		return false;
	}

	bool ConflictsWithExistingHorizontal(
		const FABTSM73BeamAGenerationResult& Assembly,
		const FBox& CandidateBounds,
		const EABTSM73BeamAFrameAxis CandidateAxis,
		const double Section,
		const double Tolerance)
	{
		const FVector CandidateCenter = CandidateBounds.GetCenter();
		const int32 AxisIndex = static_cast<int32>(CandidateAxis);
		for (const FABTSM73BeamAMember& Member : Assembly.Members)
		{
			if (Member.Axis == EABTSM73BeamAFrameAxis::Z
				|| Member.Axis == EABTSM73BeamAFrameAxis::Diagonal)
			{
				continue;
			}
			const FBox ExistingBounds = MemberBounds(Member, Assembly, Section);
			if (!HasPositiveOverlap(CandidateBounds, ExistingBounds, Tolerance))
			{
				continue;
			}
			const FVector ExistingCenter = ExistingBounds.GetCenter();
			bool bCollinear = Member.Axis == CandidateAxis;
			for (int32 Coordinate = 0; Coordinate < 3 && bCollinear; ++Coordinate)
			{
				if (Coordinate != AxisIndex
					&& FMath::Abs(CandidateCenter[Coordinate]
						- ExistingCenter[Coordinate]) > Tolerance)
				{
					bCollinear = false;
				}
			}
			if (!bCollinear)
			{
				return true;
			}
		}
		return false;
	}

	bool HasCorePostFaceClearance(
		const FABTSM73BeamAGenerationResult& Assembly,
		const FVector2D& Station,
		const double XCourseBottomZ,
		const double YCourseTopZ,
		const double Section,
		const double Tolerance)
	{
		// SplitPostsAtHorizontalCourses removes every occupied Z slab from a
		// vertical post. Preserve one full post section immediately below the X
		// course and immediately above the staggered Y course, otherwise closure
		// can produce a mathematically continuous station with no real post face
		// bearing at that corner.
		const FBox LowerGuard(
			FVector(Station.X - Section * 0.5,
				Station.Y - Section * 0.5, XCourseBottomZ - Section),
			FVector(Station.X + Section * 0.5,
				Station.Y + Section * 0.5, XCourseBottomZ));
		const FBox UpperGuard(
			FVector(Station.X - Section * 0.5,
				Station.Y - Section * 0.5, YCourseTopZ),
			FVector(Station.X + Section * 0.5,
				Station.Y + Section * 0.5, YCourseTopZ + Section));
		for (const FABTSM73BeamAMember& Member : Assembly.Members)
		{
			if (Member.Axis == EABTSM73BeamAFrameAxis::Z
				|| Member.Axis == EABTSM73BeamAFrameAxis::Diagonal)
			{
				continue;
			}
			const FBox Bounds = MemberBounds(Member, Assembly, Section);
			if (HasPositiveOverlap(Bounds, LowerGuard, Tolerance)
				|| HasPositiveOverlap(Bounds, UpperGuard, Tolerance))
			{
				return false;
			}
		}
		return true;
	}

	double FindCourseCenterZ(
		const double DesiredMidZ,
		const double MinimumZ,
		const double MaximumZ,
		const double Section)
	{
		for (int32 Step = 0; Step <= 64; ++Step)
		{
			const int32 Sign = Step == 0 ? 0 : (Step % 2 == 1 ? 1 : -1);
			const int32 Magnitude = (Step + 1) / 2;
			const double MidZ = DesiredMidZ + Sign * Magnitude * Section * 2.0;
			const double XCenterZ = MidZ - Section * 0.5;
			const double YCenterZ = MidZ + Section * 0.5;
			if (XCenterZ - Section * 0.5 <= MinimumZ + Section
				|| YCenterZ + Section * 0.5 >= MaximumZ - Section)
			{
				continue;
			}
			return MidZ;
		}
		return TNumericLimits<double>::Max();
	}

	void RemoveMembers(
		FABTSM73BeamAGenerationResult& Assembly,
		const TSet<int32>& RemovedIds)
	{
		if (RemovedIds.IsEmpty())
		{
			return;
		}
		TArray<int32> OldToNew;
		OldToNew.Init(INDEX_NONE, Assembly.Members.Num());
		TArray<FABTSM73BeamAMember> Kept;
		Kept.Reserve(Assembly.Members.Num() - RemovedIds.Num());
		for (const FABTSM73BeamAMember& Member : Assembly.Members)
		{
			if (RemovedIds.Contains(Member.MemberId))
			{
				continue;
			}
			FABTSM73BeamAMember Copy = Member;
			Copy.MemberId = Kept.Num();
			OldToNew[Member.MemberId] = Copy.MemberId;
			Kept.Add(Copy);
		}
		Assembly.Members = MoveTemp(Kept);
		Assembly.BearingContacts.Reset();
		for (FABTSM73BeamAAssembly& MemberAssembly : Assembly.Assemblies)
		{
			TArray<int32> Remapped;
			for (const int32 OldId : MemberAssembly.MemberIds)
			{
				if (OldToNew.IsValidIndex(OldId) && OldToNew[OldId] != INDEX_NONE)
				{
					Remapped.AddUnique(OldToNew[OldId]);
				}
			}
			MemberAssembly.MemberIds = MoveTemp(Remapped);
		}
	}

	int32 ReallocateRoofLanes(
		FABTSM73BeamAGenerationResult& Assembly,
		const int32 RequiredRemovalCount,
		const double Tolerance)
	{
		if (RequiredRemovalCount <= 0)
		{
			return 0;
		}
		TMap<int32, TArray<int32>> AssemblyIdsByMember;
		for (const FABTSM73BeamAAssembly& MemberAssembly : Assembly.Assemblies)
		{
			for (const int32 MemberId : MemberAssembly.MemberIds)
			{
				AssemblyIdsByMember.FindOrAdd(MemberId).Add(MemberAssembly.AssemblyId);
			}
		}
		TMap<FString, TArray<int32>> Groups;
		for (const FABTSM73BeamAMember& Member : Assembly.Members)
		{
			if (Member.Role != EABTSM73BeamAMemberRole::RoofCourse
				|| Member.Axis == EABTSM73BeamAFrameAxis::Z)
			{
				continue;
			}
			const TArray<int32>* Owners = AssemblyIdsByMember.Find(Member.MemberId);
			const int32 Owner = Owners != nullptr && !Owners->IsEmpty()
				? (*Owners)[0] : INDEX_NONE;
			const FVector Center = MemberCenter(Member, Assembly);
			const int32 QuantizedZ = FMath::RoundToInt(Center.Z / FMath::Max(0.01, Tolerance));
			const FString Key = FString::Printf(TEXT("%d:%d:%d"),
				Owner, static_cast<int32>(Member.Axis), QuantizedZ);
			Groups.FindOrAdd(Key).Add(Member.MemberId);
		}

		TSet<int32> Removed;
		TArray<FString> Keys;
		Groups.GetKeys(Keys);
		Keys.Sort();
		for (const FString& Key : Keys)
		{
			TArray<int32>& Group = Groups.FindChecked(Key);
			// A two-lane roof course consists of its two silhouette eaves, not
			// an outer lane plus a donor. Only interior lanes of a 3+ lane
			// course may fund C3; minimum/maximum eaves and a single-member
			// ridge are immutable visual contract geometry.
			if (Group.Num() <= 2)
			{
				continue;
			}
			Group.Sort([&Assembly](const int32 A, const int32 B)
			{
				const FABTSM73BeamAMember& MA = Assembly.Members[A];
				const FABTSM73BeamAMember& MB = Assembly.Members[B];
				const FVector CA = MemberCenter(Assembly.Members[A], Assembly);
				const FVector CB = MemberCenter(Assembly.Members[B], Assembly);
				const int32 Perpendicular =
					MA.Axis == EABTSM73BeamAFrameAxis::X ? 1 : 0;
				return !FMath::IsNearlyEqual(CA[Perpendicular], CB[Perpendicular])
					? CA[Perpendicular] < CB[Perpendicular]
					: (MA.MemberId < MB.MemberId);
			});
			for (int32 Index = 1;
				Index < Group.Num() - 1 && Removed.Num() < RequiredRemovalCount;
				++Index)
			{
				Removed.Add(Group[Index]);
			}
			if (Removed.Num() >= RequiredRemovalCount)
			{
				break;
			}
		}
		RemoveMembers(Assembly, Removed);
		return Removed.Num();
	}

	int32 ReallocateOneSafeHostFrame(
		FABTSM73BeamAGenerationResult& Assembly,
		const TArray<FABTSM73BeamC3CribCoreHostPlan>& HostPlans,
		const TSet<int32>& ProtectedSupportConeMemberIds,
		const TSet<int32>& ExcludedAssemblyIds,
		int32& OutDonorAssemblyId)
	{
		OutDonorAssemblyId = INDEX_NONE;
		if (HostPlans.IsEmpty())
		{
			return 0;
		}
		TSet<int32> HostBayIds;
		TSet<int32> HostSourceVolumeIds;
		for (const FABTSM73BeamC3CribCoreHostPlan& Plan : HostPlans)
		{
			HostBayIds.Add(Plan.BayId);
			HostSourceVolumeIds.Add(Plan.SourceVolumeId);
		}
		TMap<int32, TArray<int32>> OwnersByMember;
		for (const FABTSM73BeamAAssembly& MemberAssembly : Assembly.Assemblies)
		{
			for (const int32 MemberId : MemberAssembly.MemberIds)
			{
				OwnersByMember.FindOrAdd(MemberId).AddUnique(
					MemberAssembly.AssemblyId);
			}
		}
		struct FFrameDonor
		{
			int32 AssemblyId = INDEX_NONE;
			int32 SourcePriority = 1;
			TArray<int32> MemberIds;
		};
		TArray<FFrameDonor> Donors;
		for (const FABTSM73BeamAAssembly& MemberAssembly : Assembly.Assemblies)
		{
			const bool bReplaceableFrame =
				MemberAssembly.Type == EABTSM73BeamAAssemblyType::PostAndLintelBay
				|| MemberAssembly.Type == EABTSM73BeamAAssemblyType::CrossBeamBay
				|| MemberAssembly.Type == EABTSM73BeamAAssemblyType::StackedFrameBay;
			const int32 SourceVolumeId =
				Assembly.Bays.IsValidIndex(MemberAssembly.BayId)
					? Assembly.Bays[MemberAssembly.BayId].SourceVolumeId
					: INDEX_NONE;
			const bool bSameStructuralSource =
				HostSourceVolumeIds.Contains(SourceVolumeId);
			if (!bReplaceableFrame
				|| (!HostBayIds.Contains(MemberAssembly.BayId)
					&& !bSameStructuralSource)
				|| ExcludedAssemblyIds.Contains(MemberAssembly.AssemblyId))
			{
				continue;
			}
			FFrameDonor Donor;
			Donor.AssemblyId = MemberAssembly.AssemblyId;
			Donor.SourcePriority = HostBayIds.Contains(MemberAssembly.BayId)
				? 0 : 1;
			for (const int32 MemberId : MemberAssembly.MemberIds)
			{
				if (!Assembly.Members.IsValidIndex(MemberId))
				{
					continue;
				}
				const FABTSM73BeamAMember& Member = Assembly.Members[MemberId];
				if (ProtectedSupportConeMemberIds.Contains(MemberId)
					|| Member.Role == EABTSM73BeamAMemberRole::CoreCourse
					|| Member.Role == EABTSM73BeamAMemberRole::CorePost
					|| Member.Role == EABTSM73BeamAMemberRole::RoofCourse
					|| Member.Role == EABTSM73BeamAMemberRole::BridgeSeat
					|| Member.Role == EABTSM73BeamAMemberRole::BridgeRail
					|| Member.Role == EABTSM73BeamAMemberRole::BridgePost)
				{
					continue;
				}
				bool bSharedWithProtectedAssembly = false;
				for (const int32 OwnerId : OwnersByMember.FindRef(MemberId))
				{
					if (!Assembly.Assemblies.IsValidIndex(OwnerId)
						|| OwnerId == MemberAssembly.AssemblyId)
					{
						continue;
					}
					const FABTSM73BeamAAssembly& Other = Assembly.Assemblies[OwnerId];
					bSharedWithProtectedAssembly |=
						Other.Type == EABTSM73BeamAAssemblyType::RoofFrameBay
						|| Other.Type == EABTSM73BeamAAssemblyType::LayeredRoofBay
						|| Other.Type == EABTSM73BeamAAssemblyType::BridgeFrameBay
						|| Other.Type == EABTSM73BeamAAssemblyType::CribCore;
				}
				if (!bSharedWithProtectedAssembly)
				{
					Donor.MemberIds.AddUnique(MemberId);
				}
				else
				{
					continue;
				}
			}
			// The crib deliberately replaces part of an ordinary frame while
			// retaining any reused CorePost and its complete lower support cone.
			// The remaining fragment is accepted only after global closure and full
			// C3 re-certification in the caller's donor transaction.
			if (!Donor.MemberIds.IsEmpty())
			{
				Donors.Add(MoveTemp(Donor));
			}
		}
		Donors.Sort([](const FFrameDonor& A, const FFrameDonor& B)
		{
			if (A.SourcePriority != B.SourcePriority)
			{
				return A.SourcePriority < B.SourcePriority;
			}
			return A.MemberIds.Num() != B.MemberIds.Num()
				? A.MemberIds.Num() < B.MemberIds.Num()
				: A.AssemblyId < B.AssemblyId;
		});
		if (Donors.IsEmpty())
		{
			return 0;
		}
		const FFrameDonor& Donor = Donors[0];
		TSet<int32> Removed;
		Removed.Append(Donor.MemberIds);
		OutDonorAssemblyId = Donor.AssemblyId;
		RemoveMembers(Assembly, Removed);
		return Removed.Num();
	}

	struct FCoreTopologyEvidence
	{
		int32 CourseCount = 0;
		int32 CornerBearingCount = 0;
		int32 PostSupportContactCount = 0;
		TSet<int32> CertifiedCourseMemberIds;
	};

	struct FCertifiedC3BraceEvidence
	{
		FCoreTopologyEvidence Topology;
		TArray<TSet<int32>> HostCourseMemberIds;
		TSet<int32> CertifiedCourseMemberIds;
		/** Closed crib/floor-diaphragm courses that restrain both X and Y. */
		TSet<int32> BiaxialCourseMemberIds;
		TMap<int32, TSet<int32>> CertifiedSourceIdsByCourse;
		TSet<int32> RootedExistingCourseMemberIds;
		int32 RootedExistingCourseCount = 0;
	};

	int32 FindCoreCourse(
		const FABTSM73BeamAGenerationResult& Assembly,
		const EABTSM73BeamAFrameAxis Axis,
		const double ConstantCoordinate,
		const double CenterZ,
		const double SpanMinimum,
		const double SpanMaximum,
		const double Section,
		const double Tolerance)
	{
		const int32 AxisIndex = static_cast<int32>(Axis);
		const int32 OtherAxisIndex = Axis == EABTSM73BeamAFrameAxis::X ? 1 : 0;
		for (const FABTSM73BeamAMember& Member : Assembly.Members)
		{
			if (Member.Role != EABTSM73BeamAMemberRole::CoreCourse
				|| Member.Axis != Axis)
			{
				continue;
			}
			const FVector Center = MemberCenter(Member, Assembly);
			const FBox Bounds = MemberBounds(Member, Assembly, Section);
			if (FMath::Abs(Center[OtherAxisIndex] - ConstantCoordinate) <= Tolerance
				&& FMath::Abs(Center.Z - CenterZ) <= Tolerance
				&& Bounds.Min[AxisIndex] <= SpanMinimum + Tolerance
				&& Bounds.Max[AxisIndex] >= SpanMaximum - Tolerance)
			{
				return Member.MemberId;
			}
		}
		return INDEX_NONE;
	}

	bool HasContactNear(
		const FABTSM73BeamAGenerationResult& Assembly,
		const int32 A,
		const int32 B,
		const FVector2D& Position,
		const double Tolerance)
	{
		return Assembly.BearingContacts.ContainsByPredicate(
			[A, B, &Position, Tolerance](const FABTSM73BeamABearingContact& Contact)
			{
				const bool bPair =
					(Contact.LowerMemberId == A && Contact.UpperMemberId == B)
					|| (Contact.LowerMemberId == B && Contact.UpperMemberId == A);
				return bPair
					&& FVector2D::Distance(
						FVector2D(Contact.LocalPosition.X, Contact.LocalPosition.Y),
						Position) <= Tolerance * 2.0;
			});
	}

	bool HasCorePostContact(
		const FABTSM73BeamAGenerationResult& Assembly,
		const int32 CourseMemberId,
		const FVector2D& Station,
		const bool bPostBelow,
		const double CourseFaceZ,
		const double Section,
		const double Tolerance)
	{
		for (const FABTSM73BeamABearingContact& Contact : Assembly.BearingContacts)
		{
			int32 OtherMemberId = INDEX_NONE;
			if (Contact.LowerMemberId == CourseMemberId)
			{
				OtherMemberId = Contact.UpperMemberId;
			}
			else if (Contact.UpperMemberId == CourseMemberId)
			{
				OtherMemberId = Contact.LowerMemberId;
			}
			if (!Assembly.Members.IsValidIndex(OtherMemberId))
			{
				continue;
			}
			const FABTSM73BeamAMember& Other = Assembly.Members[OtherMemberId];
			if (Other.Axis != EABTSM73BeamAFrameAxis::Z
				|| Other.Role != EABTSM73BeamAMemberRole::CorePost)
			{
				continue;
			}
			const FBox Bounds = MemberBounds(Other, Assembly, Section);
			const double FaceZ = bPostBelow ? Bounds.Max.Z : Bounds.Min.Z;
			const bool bFootprintCoversStation =
				Bounds.Min.X <= Station.X + Tolerance
				&& Bounds.Max.X >= Station.X - Tolerance
				&& Bounds.Min.Y <= Station.Y + Tolerance
				&& Bounds.Max.Y >= Station.Y - Tolerance;
			const bool bContactNearStation = FVector2D::Distance(
				FVector2D(Contact.LocalPosition.X, Contact.LocalPosition.Y),
				Station) <= Section + Tolerance;
			if (bFootprintCoversStation && bContactNearStation
				&& FMath::Abs(FaceZ - CourseFaceZ) <= Tolerance * 2.0)
			{
				return true;
			}
		}
		return false;
	}

	bool RestoreHostPlanPostBearingFaces(
		FABTSM73BeamAGenerationResult& Assembly,
		const TArray<FABTSM73BeamC3CribCoreHostPlan>& HostPlans,
		const FABTSM73BeamAPreviewSettings& BeamASettings,
		const double Section,
		const double Tolerance,
		int32& InOutInsertedSyntheticPostCount,
		FString& OutError)
	{
		for (const FABTSM73BeamC3CribCoreHostPlan& Plan : HostPlans)
		{
			for (const FVector2D& Station : Plan.StationPositions)
			{
				for (FABTSM73BeamAMember& Member : Assembly.Members)
				{
					if (Member.Axis != EABTSM73BeamAFrameAxis::Z)
					{
						continue;
					}
					const FBox Bounds = MemberBounds(Member, Assembly, Section);
					if (FMath::Abs(Bounds.GetCenter().X - Station.X) <= Tolerance
						&& FMath::Abs(Bounds.GetCenter().Y - Station.Y) <= Tolerance
						&& Bounds.Max.Z >= Plan.MinimumZ - Tolerance
						&& Bounds.Min.Z <= Plan.MaximumZ + Tolerance)
					{
						Member.Role = EABTSM73BeamAMemberRole::CorePost;
					}
				}
			}
		}
		if (!ABTSM73BeamA::RebuildBearingContacts(
			BeamASettings, Assembly, OutError))
		{
			return false;
		}

		bool bInsertedContactFace = false;
		auto IsVerticalSegmentClearOfHorizontalMembers = [
			&Assembly, Section, Tolerance](
				const FVector2D& Station,
				const double MinimumZ,
				const double MaximumZ)
		{
			const FBox CandidateBounds(
				FVector(Station.X - Section * 0.5,
					Station.Y - Section * 0.5, MinimumZ),
				FVector(Station.X + Section * 0.5,
					Station.Y + Section * 0.5, MaximumZ));
			for (const FABTSM73BeamAMember& Member : Assembly.Members)
			{
				if (Member.Axis == EABTSM73BeamAFrameAxis::Z
					|| Member.Axis == EABTSM73BeamAFrameAxis::Diagonal)
				{
					continue;
				}
				if (HasPositiveOverlap(
					CandidateBounds,
					MemberBounds(Member, Assembly, Section),
					Tolerance))
				{
					return false;
				}
			}
			return true;
		};
		for (const FABTSM73BeamC3CribCoreHostPlan& Plan : HostPlans)
		{
			if (Plan.StationPositions.Num() != 4)
			{
				continue;
			}
			FABTSM73BeamAAssembly* ContactAssembly = nullptr;
			auto GetContactAssembly = [&]() -> FABTSM73BeamAAssembly&
			{
				if (ContactAssembly == nullptr)
				{
					ContactAssembly = &Assembly.Assemblies.AddDefaulted_GetRef();
					ContactAssembly->AssemblyId = Assembly.Assemblies.Num() - 1;
					ContactAssembly->BayId = Plan.BayId;
					ContactAssembly->Type = EABTSM73BeamAAssemblyType::CribCore;
				}
				return *ContactAssembly;
			};
			const double XMinimum = FMath::Min(
				Plan.StationPositions[0].X, Plan.StationPositions[1].X);
			const double XMaximum = FMath::Max(
				Plan.StationPositions[0].X, Plan.StationPositions[1].X);
			const double YMinimum = FMath::Min(
				Plan.StationPositions[0].Y, Plan.StationPositions[2].Y);
			const double YMaximum = FMath::Max(
				Plan.StationPositions[0].Y, Plan.StationPositions[2].Y);
			for (const double BeltMidZ : Plan.BeltMidZs)
			{
				const double XZ = BeltMidZ - Section * 0.5;
				const double YZ = BeltMidZ + Section * 0.5;
				const int32 X0 = FindCoreCourse(Assembly,
					EABTSM73BeamAFrameAxis::X, Plan.StationPositions[0].Y,
					XZ, XMinimum, XMaximum, Section, Tolerance);
				const int32 X1 = FindCoreCourse(Assembly,
					EABTSM73BeamAFrameAxis::X, Plan.StationPositions[2].Y,
					XZ, XMinimum, XMaximum, Section, Tolerance);
				const int32 Y0 = FindCoreCourse(Assembly,
					EABTSM73BeamAFrameAxis::Y, Plan.StationPositions[0].X,
					YZ, YMinimum, YMaximum, Section, Tolerance);
				const int32 Y1 = FindCoreCourse(Assembly,
					EABTSM73BeamAFrameAxis::Y, Plan.StationPositions[1].X,
					YZ, YMinimum, YMaximum, Section, Tolerance);
				if (X0 == INDEX_NONE || X1 == INDEX_NONE
					|| Y0 == INDEX_NONE || Y1 == INDEX_NONE)
				{
					continue;
				}
				const int32 XCourses[4] = {X0, X0, X1, X1};
				const int32 YCourses[4] = {Y0, Y1, Y0, Y1};
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					const FVector2D& Station = Plan.StationPositions[Corner];
					const auto IsSamePostStation = [&Station, Tolerance](
						const FBox& Bounds)
					{
						const FVector Center = Bounds.GetCenter();
						return FMath::Abs(Center.X - Station.X) <= Tolerance
							&& FMath::Abs(Center.Y - Station.Y) <= Tolerance;
					};
					if (!HasCorePostContact(Assembly, XCourses[Corner], Station,
						true, BeltMidZ - Section, Section, Tolerance))
					{
						const double DesiredMaximumZ = BeltMidZ - Section;
						double NearestMaximumZ = -TNumericLimits<double>::Max();
						for (const FABTSM73BeamAMember& Member : Assembly.Members)
						{
							if (Member.Axis != EABTSM73BeamAFrameAxis::Z)
							{
								continue;
							}
							const FBox Bounds = MemberBounds(Member, Assembly, Section);
							if (IsSamePostStation(Bounds)
								&& Bounds.Max.Z <= DesiredMaximumZ + Tolerance)
							{
								NearestMaximumZ = FMath::Max(
									NearestMaximumZ, Bounds.Max.Z);
							}
						}
						if (FMath::IsFinite(NearestMaximumZ)
							&& DesiredMaximumZ > NearestMaximumZ + Tolerance
							&& DesiredMaximumZ - NearestMaximumZ
								<= Section * 2.0 + Tolerance)
						{
							// BuildPostStations deliberately treats a course-sized gap as
							// one structural station.  Restore the whole missing run when
							// it is wider than one Brick; for a smaller gap, overlap the
							// existing post with one full-section face so closure can merge
							// it without ever creating a sub-block member.
							const double ContactFaceMinimumZ =
								DesiredMaximumZ - NearestMaximumZ
									> Section + Tolerance
									? NearestMaximumZ
									: DesiredMaximumZ - Section;
							if (!IsVerticalSegmentClearOfHorizontalMembers(
								Station, ContactFaceMinimumZ, DesiredMaximumZ))
							{
								OutError = TEXT("BeamC3PostFaceBlocked:Lower");
								return false;
							}
							AddSyntheticCorePost(Assembly, GetContactAssembly(),
								Station, ContactFaceMinimumZ, DesiredMaximumZ);
							++InOutInsertedSyntheticPostCount;
							bInsertedContactFace = true;
						}
					}
					if (!HasCorePostContact(Assembly, YCourses[Corner], Station,
						false, BeltMidZ + Section, Section, Tolerance))
					{
						const double DesiredMinimumZ = BeltMidZ + Section;
						double NearestMinimumZ = TNumericLimits<double>::Max();
						for (const FABTSM73BeamAMember& Member : Assembly.Members)
						{
							if (Member.Axis != EABTSM73BeamAFrameAxis::Z)
							{
								continue;
							}
							const FBox Bounds = MemberBounds(Member, Assembly, Section);
							if (IsSamePostStation(Bounds)
								&& Bounds.Min.Z >= DesiredMinimumZ - Tolerance)
							{
								NearestMinimumZ = FMath::Min(
									NearestMinimumZ, Bounds.Min.Z);
							}
						}
						if (FMath::IsFinite(NearestMinimumZ)
							&& NearestMinimumZ > DesiredMinimumZ + Tolerance
							&& NearestMinimumZ - DesiredMinimumZ
								<= Section * 2.0 + Tolerance)
						{
							const double ContactFaceMaximumZ =
								NearestMinimumZ - DesiredMinimumZ
									> Section + Tolerance
									? NearestMinimumZ
									: DesiredMinimumZ + Section;
							if (!IsVerticalSegmentClearOfHorizontalMembers(
								Station, DesiredMinimumZ, ContactFaceMaximumZ))
							{
								OutError = TEXT("BeamC3PostFaceBlocked:Upper");
								return false;
							}
							AddSyntheticCorePost(Assembly, GetContactAssembly(),
								Station, DesiredMinimumZ, ContactFaceMaximumZ);
							++InOutInsertedSyntheticPostCount;
							bInsertedContactFace = true;
						}
					}
				}
			}
		}
		return !bInsertedContactFace
			|| ABTSM73BeamA::RebuildBearingContacts(
				BeamASettings, Assembly, OutError);
	}

	bool ValidateClosedCoreTopology(
		const FABTSM73BeamAGenerationResult& Assembly,
		const TArray<FVector2D>& Stations,
		const TArray<double>& BeltMidZs,
		const double Section,
		const double Tolerance,
		FCoreTopologyEvidence& OutEvidence,
		FString& OutError)
	{
		OutEvidence = FCoreTopologyEvidence();
		if (Stations.Num() != 4 || BeltMidZs.IsEmpty())
		{
			OutError = TEXT("BeamC3CoreTopologyIncomplete:MissingPlan");
			return false;
		}
		const double XMinimum = FMath::Min(Stations[0].X, Stations[1].X);
		const double XMaximum = FMath::Max(Stations[0].X, Stations[1].X);
		const double YMinimum = FMath::Min(Stations[0].Y, Stations[2].Y);
		const double YMaximum = FMath::Max(Stations[0].Y, Stations[2].Y);
		for (int32 BeltIndex = 0; BeltIndex < BeltMidZs.Num(); ++BeltIndex)
		{
			const double MidZ = BeltMidZs[BeltIndex];
			const double XZ = MidZ - Section * 0.5;
			const double YZ = MidZ + Section * 0.5;
			const int32 X0 = FindCoreCourse(Assembly,
				EABTSM73BeamAFrameAxis::X, Stations[0].Y, XZ,
				XMinimum, XMaximum, Section, Tolerance);
			const int32 X1 = FindCoreCourse(Assembly,
				EABTSM73BeamAFrameAxis::X, Stations[2].Y, XZ,
				XMinimum, XMaximum, Section, Tolerance);
			const int32 Y0 = FindCoreCourse(Assembly,
				EABTSM73BeamAFrameAxis::Y, Stations[0].X, YZ,
				YMinimum, YMaximum, Section, Tolerance);
			const int32 Y1 = FindCoreCourse(Assembly,
				EABTSM73BeamAFrameAxis::Y, Stations[1].X, YZ,
				YMinimum, YMaximum, Section, Tolerance);
			if (X0 == INDEX_NONE || X1 == INDEX_NONE
				|| Y0 == INDEX_NONE || Y1 == INDEX_NONE
				|| X0 == X1 || Y0 == Y1)
			{
				OutError = FString::Printf(
					TEXT("BeamC3CoreTopologyIncomplete:MissingCourse:Belt=%d:X=%d,%d:Y=%d,%d"),
					BeltIndex, X0, X1, Y0, Y1);
				return false;
			}
			OutEvidence.CourseCount += 4;
			OutEvidence.CertifiedCourseMemberIds.Add(X0);
			OutEvidence.CertifiedCourseMemberIds.Add(X1);
			OutEvidence.CertifiedCourseMemberIds.Add(Y0);
			OutEvidence.CertifiedCourseMemberIds.Add(Y1);
			const int32 XCourses[4] = {X0, X0, X1, X1};
			const int32 YCourses[4] = {Y0, Y1, Y0, Y1};
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				if (!HasContactNear(Assembly, XCourses[Corner], YCourses[Corner],
					Stations[Corner], Tolerance))
				{
					OutError = FString::Printf(
						TEXT("BeamC3CoreTopologyIncomplete:MissingCornerBearing:Belt=%d:Corner=%d:X=%d:Y=%d"),
						BeltIndex, Corner, XCourses[Corner], YCourses[Corner]);
					return false;
				}
				++OutEvidence.CornerBearingCount;
				const bool bHasPostBelow = HasCorePostContact(
					Assembly, XCourses[Corner], Stations[Corner],
					true, XZ - Section * 0.5, Section, Tolerance);
				const bool bHasPostAbove = HasCorePostContact(
					Assembly, YCourses[Corner], Stations[Corner],
					false, YZ + Section * 0.5, Section, Tolerance);
				if (!bHasPostBelow || !bHasPostAbove)
				{
					FString PostEvidence;
					int32 PostEvidenceCount = 0;
					for (const FABTSM73BeamAMember& Member : Assembly.Members)
					{
						if (Member.Axis != EABTSM73BeamAFrameAxis::Z)
						{
							continue;
						}
						const FBox Bounds = MemberBounds(Member, Assembly, Section);
						if (Bounds.Min.X <= Stations[Corner].X + Tolerance
							&& Bounds.Max.X >= Stations[Corner].X - Tolerance
							&& Bounds.Min.Y <= Stations[Corner].Y + Tolerance
							&& Bounds.Max.Y >= Stations[Corner].Y - Tolerance
							&& PostEvidenceCount < 12)
						{
							PostEvidence += FString::Printf(
								TEXT("M%dR%d[%.1f..%.1f],"), Member.MemberId,
								static_cast<int32>(Member.Role),
								Bounds.Min.Z, Bounds.Max.Z);
							++PostEvidenceCount;
						}
					}
					FString ContactEvidence;
					int32 ContactEvidenceCount = 0;
					for (const FABTSM73BeamABearingContact& Contact :
						Assembly.BearingContacts)
					{
						if ((Contact.LowerMemberId == XCourses[Corner]
								|| Contact.UpperMemberId == XCourses[Corner]
								|| Contact.LowerMemberId == YCourses[Corner]
								|| Contact.UpperMemberId == YCourses[Corner])
							&& FVector2D::Distance(
								FVector2D(Contact.LocalPosition.X,
									Contact.LocalPosition.Y), Stations[Corner])
								<= Section + Tolerance
							&& ContactEvidenceCount < 12)
						{
							ContactEvidence += FString::Printf(
								TEXT("%d>%d@%.1f,"), Contact.LowerMemberId,
								Contact.UpperMemberId, Contact.LocalPosition.Z);
							++ContactEvidenceCount;
						}
					}
					OutError = FString::Printf(
						TEXT("BeamC3CoreTopologyIncomplete:MissingPostBearing:Belt=%d:Corner=%d:Below=%d:Above=%d:X=%d:Y=%d:Station=%.1f,%.1f:Faces=%.1f,%.1f:Posts=%s:Contacts=%s"),
						BeltIndex, Corner, bHasPostBelow ? 1 : 0,
						bHasPostAbove ? 1 : 0,
						XCourses[Corner], YCourses[Corner],
						Stations[Corner].X, Stations[Corner].Y,
						XZ - Section * 0.5, YZ + Section * 0.5,
						*PostEvidence, *ContactEvidence);
					return false;
				}
				OutEvidence.PostSupportContactCount += 2;
			}
		}
		OutError.Reset();
		return true;
	}

	bool ValidateHostPlanVerticalRange(
		const FABTSM73BeamC3CribCoreHostPlan& Plan,
		const int32 HostIndex,
		const double Section,
		const double Tolerance,
		FString& OutError)
	{
		if (!FMath::IsFinite(Plan.MinimumZ)
			|| !FMath::IsFinite(Plan.MaximumZ)
			|| Plan.MaximumZ <= Plan.MinimumZ + Tolerance)
		{
			OutError = FString::Printf(
				TEXT("BeamC3CoreTopologyIncomplete:InvalidVerticalRange:Host=%d"),
				HostIndex);
			return false;
		}
		for (int32 BeltIndex = 0; BeltIndex < Plan.BeltMidZs.Num(); ++BeltIndex)
		{
			const double MidZ = Plan.BeltMidZs[BeltIndex];
			if (!FMath::IsFinite(MidZ)
				|| MidZ - Section < Plan.MinimumZ + Section - Tolerance
				|| MidZ + Section > Plan.MaximumZ - Section + Tolerance)
			{
				OutError = FString::Printf(
					TEXT("BeamC3CoreTopologyIncomplete:BeltOutsideVerticalRange:Host=%d:Belt=%d"),
					HostIndex, BeltIndex);
				return false;
			}
		}
		return true;
	}

	bool MemberFootprintCoversStation(
		const FABTSM73BeamAGenerationResult& Assembly,
		const int32 MemberId,
		const FVector2D& Station,
		const double Section,
		const double Tolerance)
	{
		if (!Assembly.Members.IsValidIndex(MemberId))
		{
			return false;
		}
		const FABTSM73BeamAMember& Member = Assembly.Members[MemberId];
		if (Member.Axis == EABTSM73BeamAFrameAxis::Diagonal)
		{
			return false;
		}
		const FBox Bounds = MemberBounds(Member, Assembly, Section);
		return Bounds.Min.X <= Station.X + Tolerance
			&& Bounds.Max.X >= Station.X - Tolerance
			&& Bounds.Min.Y <= Station.Y + Tolerance
			&& Bounds.Max.Y >= Station.Y - Tolerance;
	}

	bool HasVerticalRootPathAtAnchor(
		const FABTSM73BeamAGenerationResult& Assembly,
		const FVector2D& AnchorStation,
		const int32 TieCourseId,
		const FABTSM73BeamC3CribCoreHostPlan& HostPlan,
		const TSet<int32>& HostCourseMemberIds,
		const double Section,
		const double Tolerance)
	{
		if (!Assembly.Members.IsValidIndex(TieCourseId)
			|| HostCourseMemberIds.IsEmpty())
		{
			return false;
		}
		TArray<int32> Pending;
		TSet<int32> Visited;
		Pending.Add(TieCourseId);
		Visited.Add(TieCourseId);
		for (int32 PendingIndex = 0; PendingIndex < Pending.Num(); ++PendingIndex)
		{
			const int32 CurrentId = Pending[PendingIndex];
			if (HostCourseMemberIds.Contains(CurrentId))
			{
				return true;
			}
			for (const FABTSM73BeamABearingContact& Contact :
				Assembly.BearingContacts)
			{
				if (Contact.LocalPosition.Z < HostPlan.MinimumZ - Tolerance
					|| Contact.LocalPosition.Z > HostPlan.MaximumZ + Tolerance
					|| FVector2D::Distance(
						FVector2D(Contact.LocalPosition.X, Contact.LocalPosition.Y),
						AnchorStation) > Section + Tolerance)
				{
					continue;
				}
				int32 OtherId = INDEX_NONE;
				if (Contact.LowerMemberId == CurrentId)
				{
					OtherId = Contact.UpperMemberId;
				}
				else if (Contact.UpperMemberId == CurrentId)
				{
					OtherId = Contact.LowerMemberId;
				}
				if (OtherId == INDEX_NONE || Visited.Contains(OtherId)
					|| !MemberFootprintCoversStation(
						Assembly, CurrentId, AnchorStation, Section, Tolerance)
					|| !MemberFootprintCoversStation(
						Assembly, OtherId, AnchorStation, Section, Tolerance))
				{
					continue;
				}
				Visited.Add(OtherId);
				Pending.Add(OtherId);
			}
		}
		return false;
	}

	bool ValidateTargetedTieTopology(
		const FABTSM73BeamAGenerationResult& Assembly,
		const TArray<FABTSM73BeamC3CribCoreHostPlan>& HostPlans,
		const TArray<FABTSM73BeamC3TargetedTiePlan>& TiePlans,
		const TArray<TSet<int32>>& HostCourseMemberIds,
		const TSet<int32>& RootedAnchorCourseMemberIds,
		const double Section,
		const double Tolerance,
		TSet<int32>& OutTieCourseMemberIds,
		FString& OutError)
	{
		OutTieCourseMemberIds.Reset();
		for (int32 TieIndex = 0; TieIndex < TiePlans.Num(); ++TieIndex)
		{
			const FABTSM73BeamC3TargetedTiePlan& Plan = TiePlans[TieIndex];
			if (!Plan.bAnchorIsRootedCourse
				&& (!HostPlans.IsValidIndex(Plan.AnchorHostPlanIndex)
				|| !HostCourseMemberIds.IsValidIndex(Plan.AnchorHostPlanIndex)
				|| !HostPlans[Plan.AnchorHostPlanIndex].StationPositions.ContainsByPredicate(
					[&Plan, Tolerance](const FVector2D& Station)
					{
						return Station.Equals(Plan.AnchorStation, Tolerance);
					})))
			{
				OutError = FString::Printf(
					TEXT("BeamC3TargetedTieTopologyIncomplete:MissingAnchor:Tie=%d"),
					TieIndex);
				return false;
			}
			const double CourseBottomZ = Plan.CourseCenterZ - Section * 0.5;
			const double CourseTopZ = Plan.CourseCenterZ + Section * 0.5;
			const bool bOutsideVerticalRange = Plan.bAnchorIsRootedCourse
				? CourseBottomZ < Plan.MinimumZ + Section - Tolerance
					|| CourseTopZ > Plan.MaximumZ - Section + Tolerance
				: CourseBottomZ
						< HostPlans[Plan.AnchorHostPlanIndex].MinimumZ
							+ Section - Tolerance
					|| CourseTopZ
						> HostPlans[Plan.AnchorHostPlanIndex].MaximumZ
							- Section + Tolerance;
			if (!FMath::IsFinite(Plan.CourseCenterZ) || bOutsideVerticalRange)
			{
				OutError = FString::Printf(
					TEXT("BeamC3TargetedTieTopologyIncomplete:AnchorOutsideHostRange:Tie=%d"),
					TieIndex);
				return false;
			}
			if (Plan.Axis != EABTSM73BeamAFrameAxis::X
				&& Plan.Axis != EABTSM73BeamAFrameAxis::Y)
			{
				OutError = FString::Printf(
					TEXT("BeamC3TargetedTieTopologyIncomplete:InvalidAxis:Tie=%d"),
					TieIndex);
				return false;
			}
			const int32 AxisIndex = static_cast<int32>(Plan.Axis);
			const int32 OtherAxisIndex = AxisIndex == 0 ? 1 : 0;
			if (FMath::Abs(Plan.AnchorStation[OtherAxisIndex]
					- Plan.TargetStation[OtherAxisIndex]) > Tolerance)
			{
				OutError = FString::Printf(
					TEXT("BeamC3TargetedTieTopologyIncomplete:MisalignedPlan:Tie=%d"),
					TieIndex);
				return false;
			}
			const double SpanMinimum = FMath::Min(
				Plan.AnchorStation[AxisIndex], Plan.TargetStation[AxisIndex]);
			const double SpanMaximum = FMath::Max(
				Plan.AnchorStation[AxisIndex], Plan.TargetStation[AxisIndex]);
			const int32 CourseId = FindCoreCourse(
				Assembly, Plan.Axis, Plan.AnchorStation[OtherAxisIndex],
				Plan.CourseCenterZ, SpanMinimum, SpanMaximum, Section, Tolerance);
			if (CourseId == INDEX_NONE)
			{
				OutError = FString::Printf(
					TEXT("BeamC3TargetedTieTopologyIncomplete:MissingCourse:Tie=%d"),
					TieIndex);
				return false;
			}
			const FVector2D Stations[2] = {
				Plan.AnchorStation, Plan.TargetStation};
			const int32 FirstPostEnd = Plan.bAnchorIsRootedCourse ? 1 : 0;
			for (int32 EndIndex = FirstPostEnd; EndIndex < 2; ++EndIndex)
			{
				const bool bBelow = HasCorePostContact(
					Assembly, CourseId, Stations[EndIndex], true,
					CourseBottomZ, Section, Tolerance);
				const bool bAbove = HasCorePostContact(
					Assembly, CourseId, Stations[EndIndex], false,
					CourseTopZ, Section, Tolerance);
				if (!bBelow || !bAbove)
				{
					OutError = FString::Printf(
						TEXT("BeamC3TargetedTieTopologyIncomplete:MissingEndBearing:Tie=%d:End=%d:Below=%d:Above=%d"),
						TieIndex, EndIndex, bBelow ? 1 : 0, bAbove ? 1 : 0);
					return false;
				}
			}
			if (Plan.bAnchorIsRootedCourse)
			{
				const bool bHasRootedCourseBearing =
					Assembly.BearingContacts.ContainsByPredicate(
						[&Assembly, &Plan, &RootedAnchorCourseMemberIds,
							CourseId, Section, Tolerance](
								const FABTSM73BeamABearingContact& Contact)
						{
							const int32 OtherId = Contact.LowerMemberId == CourseId
								? Contact.UpperMemberId
								: Contact.UpperMemberId == CourseId
									? Contact.LowerMemberId : INDEX_NONE;
							if (!RootedAnchorCourseMemberIds.Contains(OtherId)
								|| !Assembly.Members.IsValidIndex(OtherId)
								|| Assembly.Members[OtherId].Axis == Plan.Axis
								|| Contact.ContactAreaCM2 + Tolerance
									< Section * Section * 0.5)
							{
								return false;
							}
							return FVector2D::Distance(
								FVector2D(Contact.LocalPosition.X,
									Contact.LocalPosition.Y),
								Plan.AnchorStation) <= Section + Tolerance;
						});
				if (!bHasRootedCourseBearing)
				{
					OutError = FString::Printf(
						TEXT("BeamC3TargetedTieTopologyIncomplete:RootedCourseBearingMissing:Tie=%d"),
						TieIndex);
					return false;
				}
			}
			else if (!HasVerticalRootPathAtAnchor(
				Assembly, Plan.AnchorStation, CourseId,
				HostPlans[Plan.AnchorHostPlanIndex],
				HostCourseMemberIds[Plan.AnchorHostPlanIndex],
				Section, Tolerance))
			{
				OutError = FString::Printf(
					TEXT("BeamC3TargetedTieTopologyIncomplete:AnchorVerticalPathMissing:Tie=%d"),
					TieIndex);
				return false;
			}
			OutTieCourseMemberIds.Add(CourseId);
		}
		OutError.Reset();
		return true;
	}

	int32 AppendRootedExistingFrameCourses(
		const FABTSM73BeamAGenerationResult& Assembly,
		const TArray<FABTSM73BeamC3CribCoreHostPlan>& HostPlans,
		const TArray<TSet<int32>>& HostCourseMemberIds,
		const double Section,
		const double Tolerance,
		TSet<int32>& InOutCertifiedCourseMemberIds,
		TMap<int32, TSet<int32>>& InOutCertifiedSourceIdsByCourse)
	{
		// A closed crib can share its gravity/friction restraint with an existing
		// interleaved floor frame, but a generic connected component is too broad:
		// traversing through ordinary Z posts would certify an entire tower from one
		// incidental contact. Root only (a) exact closed-core courses and (b)
		// ordinary horizontal portals that bear on at least two distinct, vertically
		// rooted core corners. Expansion then stays horizontal, in one floor band,
		// in one semantic volume, and must contain both X and Y courses. This is the
		// physical "crib + floor diaphragm" path; no role retag or hidden joint is
		// introduced and final certification derives it again from current contacts.
		TArray<TSet<int32>> SourceIdsByMember;
		SourceIdsByMember.SetNum(Assembly.Members.Num());
		for (const FABTSM73BeamAAssembly& MemberAssembly : Assembly.Assemblies)
		{
			if (!Assembly.Bays.IsValidIndex(MemberAssembly.BayId))
			{
				continue;
			}
			const int32 SourceVolumeId =
				Assembly.Bays[MemberAssembly.BayId].SourceVolumeId;
			if (SourceVolumeId == INDEX_NONE)
			{
				continue;
			}
			for (const int32 MemberId : MemberAssembly.MemberIds)
			{
				if (SourceIdsByMember.IsValidIndex(MemberId))
				{
					SourceIdsByMember[MemberId].Add(SourceVolumeId);
				}
			}
		}

		TArray<TArray<int32>> AllContactAdjacency;
		TArray<TArray<int32>> HorizontalContactAdjacency;
		AllContactAdjacency.SetNum(Assembly.Members.Num());
		HorizontalContactAdjacency.SetNum(Assembly.Members.Num());
		const double MinimumRootContactAreaCM2 =
			Section * Section * 0.5;
		for (const FABTSM73BeamABearingContact& Contact : Assembly.BearingContacts)
		{
			if (!AllContactAdjacency.IsValidIndex(Contact.LowerMemberId)
				|| !AllContactAdjacency.IsValidIndex(Contact.UpperMemberId)
				|| Contact.ContactAreaCM2 + Tolerance
					< MinimumRootContactAreaCM2)
			{
				continue;
			}
			AllContactAdjacency[Contact.LowerMemberId].AddUnique(
				Contact.UpperMemberId);
			AllContactAdjacency[Contact.UpperMemberId].AddUnique(
				Contact.LowerMemberId);
			const EABTSM73BeamAFrameAxis LowerAxis =
				Assembly.Members[Contact.LowerMemberId].Axis;
			const EABTSM73BeamAFrameAxis UpperAxis =
				Assembly.Members[Contact.UpperMemberId].Axis;
			const bool bLowerHorizontal = LowerAxis == EABTSM73BeamAFrameAxis::X
				|| LowerAxis == EABTSM73BeamAFrameAxis::Y;
			const bool bUpperHorizontal = UpperAxis == EABTSM73BeamAFrameAxis::X
				|| UpperAxis == EABTSM73BeamAFrameAxis::Y;
			if (bLowerHorizontal && bUpperHorizontal)
			{
				HorizontalContactAdjacency[Contact.LowerMemberId].AddUnique(
					Contact.UpperMemberId);
				HorizontalContactAdjacency[Contact.UpperMemberId].AddUnique(
					Contact.LowerMemberId);
			}
		}

		TMap<int32, TSet<uint64>> RootAnchorKeysByMember;
		TMap<uint64, int32> SourceByRootAnchorKey;
		TArray<int32> RootSourceVolumeIds;
		auto CoreCourseAnchorKey = [](const int32 HostIndex, const int32 CourseId)
		{
			return (uint64{1} << 63)
				| (static_cast<uint64>(static_cast<uint32>(HostIndex)) << 32)
				| static_cast<uint32>(CourseId);
		};
		auto CorePostAnchorKey = [](const int32 HostIndex, const int32 StationIndex)
		{
			return (static_cast<uint64>(static_cast<uint32>(HostIndex)) << 32)
				| static_cast<uint32>(StationIndex);
		};
		for (int32 HostIndex = 0; HostIndex < HostPlans.Num(); ++HostIndex)
		{
			if (!HostCourseMemberIds.IsValidIndex(HostIndex)
				|| HostPlans[HostIndex].SourceVolumeId == INDEX_NONE)
			{
				continue;
			}
			const FABTSM73BeamC3CribCoreHostPlan& HostPlan = HostPlans[HostIndex];
			RootSourceVolumeIds.AddUnique(HostPlan.SourceVolumeId);
			for (const int32 CourseId : HostCourseMemberIds[HostIndex])
			{
				const uint64 AnchorKey = CoreCourseAnchorKey(HostIndex, CourseId);
				RootAnchorKeysByMember.FindOrAdd(CourseId).Add(AnchorKey);
				SourceByRootAnchorKey.Add(AnchorKey, HostPlan.SourceVolumeId);
			}

			// Derive the complete rooted CorePost chain at each corner from exact
			// courses plus real contacts. Role alone is deliberately insufficient.
			TSet<int32> Reached;
			TArray<int32> Pending = HostCourseMemberIds[HostIndex].Array();
			Reached.Append(HostCourseMemberIds[HostIndex]);
			for (int32 PendingIndex = 0; PendingIndex < Pending.Num(); ++PendingIndex)
			{
				const int32 CurrentId = Pending[PendingIndex];
				if (!AllContactAdjacency.IsValidIndex(CurrentId))
				{
					continue;
				}
				for (const int32 NeighborId : AllContactAdjacency[CurrentId])
				{
					if (!Assembly.Members.IsValidIndex(NeighborId)
						|| Reached.Contains(NeighborId)
						|| Assembly.Members[NeighborId].Axis
							!= EABTSM73BeamAFrameAxis::Z
						|| Assembly.Members[NeighborId].Role
							!= EABTSM73BeamAMemberRole::CorePost
						|| !SourceIdsByMember[NeighborId].Contains(
							HostPlan.SourceVolumeId))
					{
						continue;
					}
					const FVector Center = MemberCenter(
						Assembly.Members[NeighborId], Assembly);
					const int32 StationIndex =
						HostPlan.StationPositions.IndexOfByPredicate(
							[&Center, Tolerance](const FVector2D& Station)
							{
								return Station.Equals(
									FVector2D(Center.X, Center.Y), Tolerance);
							});
					if (StationIndex == INDEX_NONE)
					{
						continue;
					}
					const FBox PostBounds = MemberBounds(
						Assembly.Members[NeighborId], Assembly, Section);
					if (PostBounds.Min.Z < HostPlan.MinimumZ - Tolerance
						|| PostBounds.Max.Z > HostPlan.MaximumZ + Tolerance)
					{
						continue;
					}
					Reached.Add(NeighborId);
					Pending.Add(NeighborId);
					const uint64 AnchorKey =
						CorePostAnchorKey(HostIndex, StationIndex);
					RootAnchorKeysByMember.FindOrAdd(NeighborId).Add(AnchorKey);
					SourceByRootAnchorKey.Add(AnchorKey, HostPlan.SourceVolumeId);
				}
			}
		}

		const int32 BeforeCount = InOutCertifiedCourseMemberIds.Num();
		const double MaximumBandDrift = Section * 2.5 + Tolerance;
		RootSourceVolumeIds.Sort();
		TSet<uint64> ProcessedMemberKeys;
		for (const int32 SourceVolumeId : RootSourceVolumeIds)
		{
			for (const FABTSM73BeamAMember& SeedMember : Assembly.Members)
			{
				const bool bHorizontal =
					SeedMember.Axis == EABTSM73BeamAFrameAxis::X
					|| SeedMember.Axis == EABTSM73BeamAFrameAxis::Y;
				const uint64 MemberKey =
					(static_cast<uint64>(static_cast<uint32>(SourceVolumeId)) << 32)
					| static_cast<uint32>(SeedMember.MemberId);
				if (!bHorizontal
					|| InOutCertifiedCourseMemberIds.Contains(SeedMember.MemberId)
					|| !SourceIdsByMember[SeedMember.MemberId].Contains(SourceVolumeId)
					|| ProcessedMemberKeys.Contains(MemberKey))
				{
					continue;
				}

				const double AnchorZ = MemberCenter(SeedMember, Assembly).Z;
				TSet<int32> Component;
				Component.Add(SeedMember.MemberId);
				TArray<int32> Pending = {SeedMember.MemberId};
				ProcessedMemberKeys.Add(MemberKey);
				for (int32 PendingIndex = 0;
					PendingIndex < Pending.Num(); ++PendingIndex)
				{
					const int32 CurrentId = Pending[PendingIndex];
					for (const int32 NeighborId :
						HorizontalContactAdjacency[CurrentId])
					{
						const uint64 NeighborKey =
							(static_cast<uint64>(static_cast<uint32>(SourceVolumeId)) << 32)
							| static_cast<uint32>(NeighborId);
						if (!Assembly.Members.IsValidIndex(NeighborId)
							|| InOutCertifiedCourseMemberIds.Contains(NeighborId)
							|| Component.Contains(NeighborId)
							|| !SourceIdsByMember[NeighborId].Contains(SourceVolumeId)
							|| FMath::Abs(MemberCenter(
								Assembly.Members[NeighborId], Assembly).Z - AnchorZ)
								> MaximumBandDrift)
						{
							continue;
						}
						Component.Add(NeighborId);
						Pending.Add(NeighborId);
						ProcessedMemberKeys.Add(NeighborKey);
					}
				}

				TMap<int32, TSet<uint64>> DirectAnchorsByMember;
				for (const int32 MemberId : Component)
				{
					for (const int32 NeighborId : AllContactAdjacency[MemberId])
					{
						if (const TSet<uint64>* RootKeys =
							RootAnchorKeysByMember.Find(NeighborId))
						{
							for (const uint64 RootKey : *RootKeys)
							{
								if (SourceByRootAnchorKey.FindRef(RootKey)
									== SourceVolumeId)
								{
									DirectAnchorsByMember.FindOrAdd(
										MemberId).Add(RootKey);
								}
							}
						}
					}
				}

				// Remove unanchored leaf branches. The retained members form the
				// load-sharing backbone between independent core contacts; a decorative
				// one-edge cantilever cannot become brace evidence merely because some
				// other part of the component contains X and Y members.
				TSet<int32> Retained = Component;
				TMap<int32, int32> DegreeByMember;
				TArray<int32> LeafQueue;
				for (const int32 MemberId : Component)
				{
					int32 Degree = 0;
					for (const int32 NeighborId :
						HorizontalContactAdjacency[MemberId])
					{
						Degree += Component.Contains(NeighborId) ? 1 : 0;
					}
					DegreeByMember.Add(MemberId, Degree);
					if (Degree <= 1 && !DirectAnchorsByMember.Contains(MemberId))
					{
						LeafQueue.Add(MemberId);
					}
				}
				for (int32 LeafIndex = 0; LeafIndex < LeafQueue.Num(); ++LeafIndex)
				{
					const int32 LeafId = LeafQueue[LeafIndex];
					if (!Retained.Remove(LeafId))
					{
						continue;
					}
					for (const int32 NeighborId :
						HorizontalContactAdjacency[LeafId])
					{
						if (!Retained.Contains(NeighborId))
						{
							continue;
						}
						int32& Degree = DegreeByMember.FindChecked(NeighborId);
						--Degree;
						if (Degree <= 1
							&& !DirectAnchorsByMember.Contains(NeighborId))
						{
							LeafQueue.Add(NeighborId);
						}
					}
				}

				TSet<uint64> RootAnchors;
				bool bHasX = false;
				bool bHasY = false;
				for (const int32 MemberId : Retained)
				{
					if (const TSet<uint64>* Anchors =
						DirectAnchorsByMember.Find(MemberId))
					{
						RootAnchors.Append(*Anchors);
					}
					bHasX |= Assembly.Members[MemberId].Axis
						== EABTSM73BeamAFrameAxis::X;
					bHasY |= Assembly.Members[MemberId].Axis
						== EABTSM73BeamAFrameAxis::Y;
				}
				if (RootAnchors.Num() < 2 || !bHasX || !bHasY)
				{
					continue;
				}
				for (const int32 MemberId : Retained)
				{
					InOutCertifiedCourseMemberIds.Add(MemberId);
					InOutCertifiedSourceIdsByCourse.FindOrAdd(
						MemberId).Add(SourceVolumeId);
				}
			}
		}
		return InOutCertifiedCourseMemberIds.Num() - BeforeCount;
	}

	bool BuildCertifiedC3BraceEvidence(
		const FABTSM73BeamAGenerationResult& Assembly,
		const TArray<FABTSM73BeamC3CribCoreHostPlan>& HostPlans,
		const TArray<FABTSM73BeamC3TargetedTiePlan>& TiePlans,
		const double Section,
		const double Tolerance,
		FCertifiedC3BraceEvidence& OutEvidence,
		FString& OutError)
	{
		OutEvidence = FCertifiedC3BraceEvidence();
		TArray<TSet<int32>> SourceIdsByMember;
		SourceIdsByMember.SetNum(Assembly.Members.Num());
		for (const FABTSM73BeamAAssembly& MemberAssembly : Assembly.Assemblies)
		{
			if (!Assembly.Bays.IsValidIndex(MemberAssembly.BayId))
			{
				continue;
			}
			const int32 SourceVolumeId =
				Assembly.Bays[MemberAssembly.BayId].SourceVolumeId;
			for (const int32 MemberId : MemberAssembly.MemberIds)
			{
				if (SourceIdsByMember.IsValidIndex(MemberId))
				{
					SourceIdsByMember[MemberId].Add(SourceVolumeId);
				}
			}
		}
		OutEvidence.HostCourseMemberIds.Reserve(HostPlans.Num());
		for (int32 HostIndex = 0; HostIndex < HostPlans.Num(); ++HostIndex)
		{
			const FABTSM73BeamC3CribCoreHostPlan& Plan = HostPlans[HostIndex];
			if (!ValidateHostPlanVerticalRange(
				Plan, HostIndex, Section, Tolerance, OutError))
			{
				return false;
			}
			FCoreTopologyEvidence HostEvidence;
			if (!ValidateClosedCoreTopology(
				Assembly, Plan.StationPositions, Plan.BeltMidZs,
				Section, Tolerance, HostEvidence, OutError))
			{
				OutError = FString::Printf(TEXT("%s:Host=%d"),
					*OutError, HostIndex);
				return false;
			}
			OutEvidence.Topology.CourseCount += HostEvidence.CourseCount;
			OutEvidence.Topology.CornerBearingCount +=
				HostEvidence.CornerBearingCount;
			OutEvidence.Topology.PostSupportContactCount +=
				HostEvidence.PostSupportContactCount;
			OutEvidence.Topology.CertifiedCourseMemberIds.Append(
				HostEvidence.CertifiedCourseMemberIds);
			OutEvidence.HostCourseMemberIds.Add(
				HostEvidence.CertifiedCourseMemberIds);
			OutEvidence.CertifiedCourseMemberIds.Append(
				HostEvidence.CertifiedCourseMemberIds);
			OutEvidence.BiaxialCourseMemberIds.Append(
				HostEvidence.CertifiedCourseMemberIds);
			for (const int32 CourseId : HostEvidence.CertifiedCourseMemberIds)
			{
				if (!SourceIdsByMember.IsValidIndex(CourseId)
					|| !SourceIdsByMember[CourseId].Contains(Plan.SourceVolumeId))
				{
					OutError = FString::Printf(
						TEXT("BeamC3CoreTopologySourceMismatch:Host=%d:Course=%d:Source=%d"),
						HostIndex, CourseId, Plan.SourceVolumeId);
					return false;
				}
				OutEvidence.CertifiedSourceIdsByCourse.FindOrAdd(CourseId).Add(
					Plan.SourceVolumeId);
			}
		}
		// Root ordinary same-source floor diaphragms before validating portal
		// ties.  A portal is allowed to terminate on this already-certified
		// physical network, but it cannot make its own unrooted anchor valid.
		const TSet<int32> ExplicitHostCourseMemberIds =
			OutEvidence.CertifiedCourseMemberIds;
		AppendRootedExistingFrameCourses(
			Assembly, HostPlans, OutEvidence.HostCourseMemberIds,
			Section, Tolerance, OutEvidence.CertifiedCourseMemberIds,
			OutEvidence.CertifiedSourceIdsByCourse);
		const TSet<int32> RootedAnchorCourseMemberIds =
			OutEvidence.CertifiedCourseMemberIds;
		TSet<int32> TieCourseMemberIds;
		if (!ValidateTargetedTieTopology(
			Assembly, HostPlans, TiePlans, OutEvidence.HostCourseMemberIds,
			RootedAnchorCourseMemberIds,
			Section, Tolerance, TieCourseMemberIds, OutError))
		{
			return false;
		}
		OutEvidence.CertifiedCourseMemberIds.Append(TieCourseMemberIds);
		for (const FABTSM73BeamC3TargetedTiePlan& Plan : TiePlans)
		{
			const int32 AxisIndex = static_cast<int32>(Plan.Axis);
			const int32 OtherAxisIndex = AxisIndex == 0 ? 1 : 0;
			const double SpanMinimum = FMath::Min(
				Plan.AnchorStation[AxisIndex], Plan.TargetStation[AxisIndex]);
			const double SpanMaximum = FMath::Max(
				Plan.AnchorStation[AxisIndex], Plan.TargetStation[AxisIndex]);
			const int32 CourseId = FindCoreCourse(
				Assembly, Plan.Axis, Plan.AnchorStation[OtherAxisIndex],
				Plan.CourseCenterZ, SpanMinimum, SpanMaximum, Section, Tolerance);
			if (TieCourseMemberIds.Contains(CourseId))
			{
				if (!SourceIdsByMember.IsValidIndex(CourseId)
					|| !SourceIdsByMember[CourseId].Contains(Plan.SourceVolumeId))
				{
					OutError = FString::Printf(
						TEXT("BeamC3TargetedTieSourceMismatch:Course=%d:Source=%d"),
						CourseId, Plan.SourceVolumeId);
					return false;
				}
				OutEvidence.CertifiedSourceIdsByCourse.FindOrAdd(CourseId).Add(
					Plan.SourceVolumeId);
				if (Plan.bAnchorIsRootedCourse)
				{
					OutEvidence.BiaxialCourseMemberIds.Add(CourseId);
				}
			}
		}
		TSet<int32> ExplicitCourseMemberIds = ExplicitHostCourseMemberIds;
		ExplicitCourseMemberIds.Append(TieCourseMemberIds);
		// A direct host tie can itself close another same-source floor loop.
		// Re-run the strict rooted-floor derivation once after adding explicit
		// ties, then classify every non-host/non-tie course as reused evidence.
		AppendRootedExistingFrameCourses(
			Assembly, HostPlans, OutEvidence.HostCourseMemberIds,
			Section, Tolerance, OutEvidence.CertifiedCourseMemberIds,
			OutEvidence.CertifiedSourceIdsByCourse);
		for (const int32 CourseId : OutEvidence.CertifiedCourseMemberIds)
		{
			if (!ExplicitCourseMemberIds.Contains(CourseId))
			{
				OutEvidence.RootedExistingCourseMemberIds.Add(CourseId);
				OutEvidence.BiaxialCourseMemberIds.Add(CourseId);
			}
		}
		OutEvidence.RootedExistingCourseCount =
			OutEvidence.RootedExistingCourseMemberIds.Num();
		OutError.Reset();
		return true;
	}

	FUnbracedZViolation MaximumUnbracedZViolation(
		const FABTSM73BeamAGenerationResult& Assembly,
		const TSet<int32>& CertifiedCourseMemberIds,
		const double Section,
		const double Tolerance,
		const TMap<int32, TSet<int32>>* CertifiedSourceIdsByCourse = nullptr,
		const TSet<int32>* BiaxialCourseMemberIds = nullptr)
	{
		TArray<FPostStation> Stations;
		BuildPostStations(Assembly, Tolerance, Section, Stations);
		TMap<int32, int32> StationByMember;
		for (int32 StationIndex = 0; StationIndex < Stations.Num(); ++StationIndex)
		{
			for (const int32 MemberId : Stations[StationIndex].MemberIds)
			{
				StationByMember.Add(MemberId, StationIndex);
			}
		}
		struct FBraceEvent
		{
			double Z = 0.0;
			EABTSM73BeamAFrameAxis Axis = EABTSM73BeamAFrameAxis::X;
		};
		TMap<int32, TArray<FBraceEvent>> EventsByStation;
		for (const FABTSM73BeamABearingContact& Contact : Assembly.BearingContacts)
		{
			if (!Assembly.Members.IsValidIndex(Contact.LowerMemberId)
				|| !Assembly.Members.IsValidIndex(Contact.UpperMemberId))
			{
				continue;
			}
			const FABTSM73BeamAMember& Lower =
				Assembly.Members[Contact.LowerMemberId];
			const FABTSM73BeamAMember& Upper =
				Assembly.Members[Contact.UpperMemberId];
			const FABTSM73BeamAMember* Post = nullptr;
			const FABTSM73BeamAMember* Course = nullptr;
			if (Lower.Axis == EABTSM73BeamAFrameAxis::Z
				&& (Upper.Axis == EABTSM73BeamAFrameAxis::X
					|| Upper.Axis == EABTSM73BeamAFrameAxis::Y))
			{
				Post = &Lower;
				Course = &Upper;
			}
			else if (Upper.Axis == EABTSM73BeamAFrameAxis::Z
				&& (Lower.Axis == EABTSM73BeamAFrameAxis::X
					|| Lower.Axis == EABTSM73BeamAFrameAxis::Y))
			{
				Post = &Upper;
				Course = &Lower;
			}
			if (Post == nullptr || Course == nullptr)
			{
				continue;
			}
			if (!CertifiedCourseMemberIds.Contains(Course->MemberId))
			{
				continue;
			}
			if (const int32* StationIndex = StationByMember.Find(Post->MemberId))
			{
				if (CertifiedSourceIdsByCourse != nullptr)
				{
					const TSet<int32>* CourseSourceIds =
						CertifiedSourceIdsByCourse->Find(Course->MemberId);
					bool bSharesStructuralSource = false;
					if (CourseSourceIds != nullptr)
					{
						for (const int32 SourceVolumeId : *CourseSourceIds)
						{
							bSharesStructuralSource |=
								Stations[*StationIndex].SourceVolumeIds.Contains(
									SourceVolumeId);
						}
					}
					if (!bSharesStructuralSource)
					{
						continue;
					}
				}
				FBraceEvent& Event =
					EventsByStation.FindOrAdd(*StationIndex).AddDefaulted_GetRef();
				Event.Z = Contact.LocalPosition.Z;
				Event.Axis = Course->Axis;
				if (BiaxialCourseMemberIds != nullptr
					&& BiaxialCourseMemberIds->Contains(Course->MemberId))
				{
					FBraceEvent& TransverseEvent = EventsByStation.FindOrAdd(
						*StationIndex).AddDefaulted_GetRef();
					TransverseEvent.Z = Contact.LocalPosition.Z;
					TransverseEvent.Axis = Course->Axis
						== EABTSM73BeamAFrameAxis::X
							? EABTSM73BeamAFrameAxis::Y
							: EABTSM73BeamAFrameAxis::X;
				}
			}
		}

		FUnbracedZViolation Violation;
		auto ConsiderViolation = [&Violation](
			const double Span,
			const FPostStation& Station,
			const double MinimumZ,
			const double MaximumZ,
			const EABTSM73BeamAFrameAxis MissingAxis,
			const int32 MemberId)
		{
			if (Span <= Violation.SpanCM)
			{
				return;
			}
			Violation.SpanCM = static_cast<float>(Span);
			Violation.Station = Station.Position;
			Violation.MinimumZ = MinimumZ;
			Violation.MaximumZ = MaximumZ;
			Violation.MissingBraceAxis = MissingAxis;
			Violation.MemberId = MemberId;
		};
		for (int32 StationIndex = 0; StationIndex < Stations.Num(); ++StationIndex)
		{
			const FPostStation& Station = Stations[StationIndex];
			for (const int32 MemberId : Station.MemberIds)
			{
				if (Assembly.Members.IsValidIndex(MemberId))
				{
					const FABTSM73BeamAMember& Member = Assembly.Members[MemberId];
					const FVector A = Assembly.Joints[Member.JointA].LocalPosition;
					const FVector B = Assembly.Joints[Member.JointB].LocalPosition;
					ConsiderViolation(Member.LengthCM, Station,
						FMath::Min(A.Z, B.Z), FMath::Max(A.Z, B.Z),
						EABTSM73BeamAFrameAxis::Z, MemberId);
				}
			}
			const TArray<FBraceEvent> Events = EventsByStation.FindRef(StationIndex);
			for (const FVerticalInterval& Interval : Station.ContinuousIntervals)
			{
				TArray<double> XPlanes = {Interval.MinimumZ, Interval.MaximumZ};
				TArray<double> YPlanes = XPlanes;
				for (const FBraceEvent& Event : Events)
				{
					if (Event.Z > Interval.MinimumZ + Tolerance
						&& Event.Z < Interval.MaximumZ - Tolerance)
					{
						if (Event.Axis == EABTSM73BeamAFrameAxis::X)
						{
							XPlanes.Add(Event.Z);
						}
						else if (Event.Axis == EABTSM73BeamAFrameAxis::Y)
						{
							YPlanes.Add(Event.Z);
						}
					}
				}
				XPlanes.Sort();
				YPlanes.Sort();
				const TArray<TPair<const TArray<double>*, EABTSM73BeamAFrameAxis>>
					PlaneSets = {
						{&XPlanes, EABTSM73BeamAFrameAxis::X},
						{&YPlanes, EABTSM73BeamAFrameAxis::Y}};
				for (const TPair<const TArray<double>*, EABTSM73BeamAFrameAxis>&
					PlaneSet : PlaneSets)
				{
					for (int32 Index = 1; Index < PlaneSet.Key->Num(); ++Index)
					{
						ConsiderViolation(
							(*PlaneSet.Key)[Index] - (*PlaneSet.Key)[Index - 1],
							Station,
							(*PlaneSet.Key)[Index - 1], (*PlaneSet.Key)[Index],
							PlaneSet.Value, INDEX_NONE);
					}
				}
			}
		}
		return Violation;
	}

	float MaximumUnbracedZSpan(
		const FABTSM73BeamAGenerationResult& Assembly,
		const TSet<int32>& CertifiedCourseMemberIds,
		const double Section,
		const double Tolerance)
	{
		return MaximumUnbracedZViolation(
			Assembly, CertifiedCourseMemberIds, Section, Tolerance).SpanCM;
	}

	float MaximumUnbracedSpanAtStationAxis(
		const FABTSM73BeamAGenerationResult& Assembly,
		const FVector2D& TargetPosition,
		const EABTSM73BeamAFrameAxis Axis,
		const TSet<int32>& CertifiedCourseMemberIds,
		const TSet<int32>& BiaxialCourseMemberIds,
		const TMap<int32, TSet<int32>>& CertifiedSourceIdsByCourse,
		const double Section,
		const double Tolerance,
		bool& bOutFound)
	{
		bOutFound = false;
		if (Axis != EABTSM73BeamAFrameAxis::X
			&& Axis != EABTSM73BeamAFrameAxis::Y)
		{
			return 0.0f;
		}
		TArray<FPostStation> Stations;
		BuildPostStations(Assembly, Tolerance, Section, Stations);
		const double StationTolerance = FMath::Max(Tolerance, Section);
		int32 StationIndex = INDEX_NONE;
		double BestStationDistance = TNumericLimits<double>::Max();
		for (int32 CandidateIndex = 0;
			CandidateIndex < Stations.Num(); ++CandidateIndex)
		{
			const double Distance = FVector2D::Distance(
				Stations[CandidateIndex].Position, TargetPosition);
			if (Distance <= StationTolerance
				&& Distance < BestStationDistance)
			{
				StationIndex = CandidateIndex;
				BestStationDistance = Distance;
			}
		}
		if (StationIndex == INDEX_NONE)
		{
			return 0.0f;
		}
		bOutFound = true;
		const FPostStation& Station = Stations[StationIndex];
		TSet<int32> PostMemberIds;
		PostMemberIds.Append(Station.MemberIds);
		TArray<double> Events;
		for (const FABTSM73BeamABearingContact& Contact : Assembly.BearingContacts)
		{
			int32 PostId = INDEX_NONE;
			int32 CourseId = INDEX_NONE;
			if (PostMemberIds.Contains(Contact.LowerMemberId))
			{
				PostId = Contact.LowerMemberId;
				CourseId = Contact.UpperMemberId;
			}
			else if (PostMemberIds.Contains(Contact.UpperMemberId))
			{
				PostId = Contact.UpperMemberId;
				CourseId = Contact.LowerMemberId;
			}
			if (PostId == INDEX_NONE || !Assembly.Members.IsValidIndex(CourseId)
				|| (Assembly.Members[CourseId].Axis != Axis
					&& !BiaxialCourseMemberIds.Contains(CourseId))
				|| !CertifiedCourseMemberIds.Contains(CourseId))
			{
				continue;
			}
			const TSet<int32>* CourseSourceIds =
				CertifiedSourceIdsByCourse.Find(CourseId);
			bool bSharesStructuralSource = false;
			if (CourseSourceIds != nullptr)
			{
				for (const int32 SourceVolumeId : *CourseSourceIds)
				{
					bSharesStructuralSource |=
						Station.SourceVolumeIds.Contains(SourceVolumeId);
				}
			}
			if (!bSharesStructuralSource)
			{
				continue;
			}
			Events.Add(Contact.LocalPosition.Z);
		}
		float Maximum = 0.0f;
		for (const FVerticalInterval& Interval : Station.ContinuousIntervals)
		{
			TArray<double> Planes = {Interval.MinimumZ, Interval.MaximumZ};
			for (const double EventZ : Events)
			{
				if (EventZ > Interval.MinimumZ + Tolerance
					&& EventZ < Interval.MaximumZ - Tolerance)
				{
					Planes.Add(EventZ);
				}
			}
			Planes.Sort();
			for (int32 Index = 1; Index < Planes.Num(); ++Index)
			{
				Maximum = FMath::Max(Maximum,
					static_cast<float>(Planes[Index] - Planes[Index - 1]));
			}
		}
		return Maximum;
	}

	float MaximumUnbracedSpanWithinIntervalAtStationAxis(
		const FABTSM73BeamAGenerationResult& Assembly,
		const FVector2D& TargetPosition,
		const EABTSM73BeamAFrameAxis Axis,
		const double TargetMinimumZ,
		const double TargetMaximumZ,
		const TSet<int32>& CertifiedCourseMemberIds,
		const TSet<int32>& BiaxialCourseMemberIds,
		const TMap<int32, TSet<int32>>& CertifiedSourceIdsByCourse,
		const double Section,
		const double Tolerance,
		bool& bOutFound)
	{
		bOutFound = false;
		if ((Axis != EABTSM73BeamAFrameAxis::X
				&& Axis != EABTSM73BeamAFrameAxis::Y)
			|| TargetMaximumZ <= TargetMinimumZ + Tolerance)
		{
			return 0.0f;
		}

		TArray<FPostStation> Stations;
		BuildPostStations(Assembly, Tolerance, Section, Stations);
		const double StationTolerance = FMath::Max(Tolerance, Section);
		const FPostStation* Station = nullptr;
		double BestStationDistance = TNumericLimits<double>::Max();
		for (const FPostStation& Candidate : Stations)
		{
			const double Distance = FVector2D::Distance(
				Candidate.Position, TargetPosition);
			if (Distance <= StationTolerance && Distance < BestStationDistance)
			{
				Station = &Candidate;
				BestStationDistance = Distance;
			}
		}
		if (Station == nullptr)
		{
			return 0.0f;
		}

		TSet<int32> PostMemberIds;
		PostMemberIds.Append(Station->MemberIds);
		TArray<double> Events;
		for (const FABTSM73BeamABearingContact& Contact : Assembly.BearingContacts)
		{
			int32 CourseId = INDEX_NONE;
			if (PostMemberIds.Contains(Contact.LowerMemberId))
			{
				CourseId = Contact.UpperMemberId;
			}
			else if (PostMemberIds.Contains(Contact.UpperMemberId))
			{
				CourseId = Contact.LowerMemberId;
			}
			if (!Assembly.Members.IsValidIndex(CourseId)
				|| (Assembly.Members[CourseId].Axis != Axis
					&& !BiaxialCourseMemberIds.Contains(CourseId))
				|| !CertifiedCourseMemberIds.Contains(CourseId))
			{
				continue;
			}
			const TSet<int32>* CourseSourceIds =
				CertifiedSourceIdsByCourse.Find(CourseId);
			bool bSharesStructuralSource = false;
			if (CourseSourceIds != nullptr)
			{
				for (const int32 SourceVolumeId : *CourseSourceIds)
				{
					bSharesStructuralSource |=
						Station->SourceVolumeIds.Contains(SourceVolumeId);
				}
			}
			if (!bSharesStructuralSource)
			{
				continue;
			}
			Events.Add(Contact.LocalPosition.Z);
		}

		float Maximum = 0.0f;
		for (const FVerticalInterval& Interval : Station->ContinuousIntervals)
		{
			const double MinimumZ = FMath::Max(
				Interval.MinimumZ, TargetMinimumZ);
			const double MaximumZ = FMath::Min(
				Interval.MaximumZ, TargetMaximumZ);
			if (MaximumZ <= MinimumZ + Tolerance)
			{
				continue;
			}
			bOutFound = true;
			TArray<double> Planes = {MinimumZ, MaximumZ};
			for (const double EventZ : Events)
			{
				if (EventZ > MinimumZ + Tolerance
					&& EventZ < MaximumZ - Tolerance)
				{
					Planes.Add(EventZ);
				}
			}
			Planes.Sort();
			for (int32 Index = 1; Index < Planes.Num(); ++Index)
			{
				Maximum = FMath::Max(Maximum,
					static_cast<float>(Planes[Index] - Planes[Index - 1]));
			}
		}
		return Maximum;
	}

	int64 CorePlanHash(
		const FABTSM73BeamAGenerationResult& Assembly,
		const TArray<FABTSM73BeamC3CribCoreHostPlan>& HostPlans,
		const TArray<FABTSM73BeamC3TargetedTiePlan>& TiePlans)
	{
		FString Signature;
		for (const FABTSM73BeamAMember& Member : Assembly.Members)
		{
			if (Member.Role != EABTSM73BeamAMemberRole::CoreCourse
				&& Member.Role != EABTSM73BeamAMemberRole::CorePost)
			{
				continue;
			}
			const FVector Center = MemberCenter(Member, Assembly);
			Signature += FString::Printf(TEXT("%d:%d:%.3f:%.3f:%.3f:%.3f|"),
				Member.MemberId, static_cast<int32>(Member.Axis),
				Center.X, Center.Y, Center.Z, Member.LengthCM);
		}
		for (const FABTSM73BeamC3CribCoreHostPlan& Plan : HostPlans)
		{
			Signature += FString::Printf(TEXT("H:%d:%d:%.3f:%.3f|"),
				Plan.BayId, Plan.SourceVolumeId, Plan.MinimumZ, Plan.MaximumZ);
			for (const FVector2D& Station : Plan.StationPositions)
			{
				Signature += FString::Printf(
					TEXT("HS:%.3f:%.3f|"), Station.X, Station.Y);
			}
			for (const double MidZ : Plan.BeltMidZs)
			{
				Signature += FString::Printf(TEXT("HB:%.3f|"), MidZ);
			}
		}
		for (const FABTSM73BeamC3TargetedTiePlan& Plan : TiePlans)
		{
			Signature += FString::Printf(
				TEXT("T:%d:%d:%.3f:%.3f:%.3f:%.3f:%.3f:%.3f:%.3f:%d:%d:%d|"),
				Plan.AnchorHostPlanIndex, static_cast<int32>(Plan.Axis),
				Plan.AnchorStation.X, Plan.AnchorStation.Y,
				Plan.TargetStation.X, Plan.TargetStation.Y,
				Plan.CourseCenterZ, Plan.MinimumZ, Plan.MaximumZ,
				Plan.BayId, Plan.SourceVolumeId,
				Plan.bAnchorIsRootedCourse ? 1 : 0);
		}
		return static_cast<int64>(FCrc::StrCrc32(*Signature));
	}

	int64 RootedEvidenceHash(
		const FABTSM73BeamAGenerationResult& Assembly,
		const FCertifiedC3BraceEvidence& Evidence)
	{
		TArray<FString> Entries;
		for (const int32 CourseId : Evidence.RootedExistingCourseMemberIds)
		{
			if (!Assembly.Members.IsValidIndex(CourseId))
			{
				continue;
			}
			const FABTSM73BeamAMember& Course = Assembly.Members[CourseId];
			const FVector Center = MemberCenter(Course, Assembly);
			TArray<int32> SourceIds =
				Evidence.CertifiedSourceIdsByCourse.FindRef(CourseId).Array();
			SourceIds.Sort();
			FString SourceSignature;
			for (const int32 SourceVolumeId : SourceIds)
			{
				SourceSignature += FString::Printf(TEXT("%d,"), SourceVolumeId);
			}
			Entries.Add(FString::Printf(
				TEXT("%d:%.3f:%.3f:%.3f:%.3f:%s"),
				static_cast<int32>(Course.Axis), Center.X, Center.Y, Center.Z,
				Course.LengthCM, *SourceSignature));
		}
		Entries.Sort();
		FString Signature(TEXT("ROOTED|"));
		for (const FString& Entry : Entries)
		{
			Signature += Entry;
			Signature += TEXT("|");
		}
		return static_cast<int64>(FCrc::StrCrc32(*Signature));
	}
}

bool FABTSM73BeamC3CribCoreSettings::Validate(FString& OutError) const
{
	if (!FMath::IsFinite(MaximumUnbracedCorePostSpanCM)
		|| !FMath::IsFinite(MinimumCoreArmSpanCM)
		|| MaximumUnbracedCorePostSpanCM <= 0.0f
		|| MinimumCoreArmSpanCM <= 0.0f
		|| TargetBeltCount < 1
		|| MaximumHostCount < 1
		|| MaximumNetMemberIncrease < 0
		|| MaximumFinalMemberCount < 1
		|| BeamC2MemberReserve < 0
		|| BeamC2MemberReserve >= MaximumFinalMemberCount)
	{
		OutError = TEXT("BeamC3InvalidSettings");
		return false;
	}
	OutError.Reset();
	return true;
}

bool FABTSM73BeamC3CribCoreGenerator::Generate(
	const FABTSM73BeamC3CribCoreSettings& Settings,
	const FABTSM73BeamAPreviewSettings& BeamASettings,
	FABTSM73BeamAGenerationResult& InOutAssembly,
	FABTSM73BeamC3CribCoreResult& OutResult,
	FString& OutError,
	const FABTSM73BeamC3CribCoreResult* ExistingCertifiedPlan) const
{
	using namespace ABTSM73BeamC3;
	OutResult = FABTSM73BeamC3CribCoreResult();
	auto Reject = [&OutResult, &OutError](const FString& Reason)
	{
		OutResult.Summary.bAccepted = false;
		OutResult.Summary.RejectReason = Reason;
		OutError = Reason;
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M7.3-Beam-C3][Rejected] Reason=%s"), *Reason);
		return false;
	};
	if (!Settings.Validate(OutError))
	{
		return Reject(OutError);
	}
	if (!Settings.bEnabled)
	{
		OutResult.Summary.bAccepted = true;
		OutError.Reset();
		return true;
	}
	if (!InOutAssembly.Summary.bAccepted || InOutAssembly.Members.IsEmpty()
		|| InOutAssembly.Assemblies.IsEmpty())
	{
		return Reject(TEXT("BeamC3UpstreamAssemblyRejected"));
	}

	const int32 OriginalMemberCount = InOutAssembly.Members.Num();
	FABTSM73BeamAGenerationResult Scratch = InOutAssembly;
	const double Section = BeamASettings.BlockCrossSectionCM;
	const double Tolerance = FMath::Max(
		0.01, static_cast<double>(BeamASettings.JointMergeToleranceCM));
	const TSet<int32> NoCertifiedCourses;
	const FUnbracedZViolation BeforeViolation = MaximumUnbracedZViolation(
		Scratch, NoCertifiedCourses, Section, Tolerance);
	const float MaximumBefore = BeforeViolation.SpanCM;
	TArray<FPostStation> Stations;
	BuildPostStations(Scratch, Tolerance, Section, Stations);
	FCoreHost Host;

	int32 InsertedCourseCount = 0;
	int32 InsertedSyntheticPostCount = 0;
	int32 ReusedHostPostMemberCount = 0;
	TArray<FABTSM73BeamC3CribCoreHostPlan> HostPlans;
	TArray<FABTSM73BeamC3TargetedTiePlan> TiePlans;
	const bool bContinueExistingPlan = ExistingCertifiedPlan != nullptr
		&& !ExistingCertifiedPlan->HostPlans.IsEmpty();
	if (bContinueExistingPlan)
	{
		HostPlans = ExistingCertifiedPlan->HostPlans;
		TiePlans = ExistingCertifiedPlan->TiePlans;
	}
	FCertifiedC3BraceEvidence CertifiedEvidence;
	auto AppendHost = [&](const TArray<FPostStation>& AvailableStations,
		const FCoreHost& SelectedHost,
		FABTSM73BeamC3CribCoreHostPlan& OutPlan,
		FString& AppendError)
	{
		const FPostStation& Origin = AvailableStations[SelectedHost.OriginStation];
		const FPostStation& XStation = AvailableStations[SelectedHost.XStation];
		const FPostStation& YStation = AvailableStations[SelectedHost.YStation];
		const FPostStation& Diagonal =
			AvailableStations[SelectedHost.DiagonalStation];
		const double HostHeight = SelectedHost.MaximumZ - SelectedHost.MinimumZ;
		const double EffectiveSpanTarget = FMath::Max(
			Section * 4.0,
			static_cast<double>(Settings.MaximumUnbracedCorePostSpanCM));
		const int32 BeltCount = FMath::Max(Settings.TargetBeltCount,
			FMath::Max(0,
				FMath::CeilToInt(HostHeight / EffectiveSpanTarget) - 1));
		if (BeltCount < 1)
		{
			AppendError = TEXT("BeamC3CoreIncomplete");
			return false;
		}
		FABTSM73BeamAAssembly& CoreAssembly =
			Scratch.Assemblies.AddDefaulted_GetRef();
		CoreAssembly.AssemblyId = Scratch.Assemblies.Num() - 1;
		CoreAssembly.BayId = SelectedHost.BayId;
		CoreAssembly.Type = EABTSM73BeamAAssemblyType::CribCore;
		OutPlan.StationPositions = {
			Origin.Position, XStation.Position, YStation.Position, Diagonal.Position};
		OutPlan.MinimumZ = SelectedHost.MinimumZ;
		OutPlan.MaximumZ = SelectedHost.MaximumZ;
		OutPlan.BayId = SelectedHost.BayId;
		OutPlan.SourceVolumeId = SelectedHost.SourceVolumeId;
		// Mark the four selected physical lanes before Beam-A closure.  Its
		// collinear merge is allowed to combine nearby overlapping posts; the
		// CorePost priority then keeps the exact certified station instead of
		// retaining an adjacent ordinary lane and making the host disappear.
		const FPostStation* HostStations[4] = {
			&Origin, &XStation, &YStation, &Diagonal};
		for (const FPostStation* HostStation : HostStations)
		{
			for (const int32 MemberId : HostStation->MemberIds)
			{
				if (Scratch.Members.IsValidIndex(MemberId)
					&& Scratch.Members[MemberId].Axis
						== EABTSM73BeamAFrameAxis::Z)
				{
					Scratch.Members[MemberId].Role =
						EABTSM73BeamAMemberRole::CorePost;
				}
			}
		}
		ReusedHostPostMemberCount += Origin.MemberIds.Num()
			+ XStation.MemberIds.Num() + YStation.MemberIds.Num()
			+ Diagonal.MemberIds.Num();
		for (int32 BeltIndex = 0; BeltIndex < BeltCount; ++BeltIndex)
		{
			const double DesiredMidZ = FMath::Lerp(
				SelectedHost.MinimumZ, SelectedHost.MaximumZ,
				static_cast<double>(BeltIndex + 1) / (BeltCount + 1));
			double MidZ = TNumericLimits<double>::Max();
			const double XMin = FMath::Min(Origin.Position.X, XStation.Position.X)
				- Section * 0.5;
			const double XMax = FMath::Max(Origin.Position.X, XStation.Position.X)
				+ Section * 0.5;
			const double YMin = FMath::Min(Origin.Position.Y, YStation.Position.Y)
				- Section * 0.5;
			const double YMax = FMath::Max(Origin.Position.Y, YStation.Position.Y)
				+ Section * 0.5;
			TArray<double> CandidateMidZs;
			TSet<int64> CandidateMidZKeys;
			const double HeightQuantizationStep = FMath::Max(Tolerance, 0.01);
			auto AddCandidateMidZ = [&CandidateMidZs, &CandidateMidZKeys,
				HeightQuantizationStep](
				const double CandidateMidZ)
			{
				const int64 HeightKey = FMath::RoundToInt64(
					CandidateMidZ / HeightQuantizationStep);
				if (!CandidateMidZKeys.Contains(HeightKey))
				{
					CandidateMidZKeys.Add(HeightKey);
					CandidateMidZs.Add(CandidateMidZ);
				}
			};
			// Existing floor courses are the cheapest authoritative belt heights.
			// Trying them first lets a low-tier core reuse a complete side or only
			// fill its missing mates instead of splitting every host post at a new Z.
			for (const FABTSM73BeamAMember& Existing : Scratch.Members)
			{
				if (Existing.Axis != EABTSM73BeamAFrameAxis::X
					&& Existing.Axis != EABTSM73BeamAFrameAxis::Y)
				{
					continue;
				}
				const double ExistingZ = MemberCenter(Existing, Scratch).Z;
				AddCandidateMidZ(Existing.Axis == EABTSM73BeamAFrameAxis::X
					? ExistingZ + Section * 0.5
					: ExistingZ - Section * 0.5);
			}
			for (int32 Step = 0; Step <= 64; ++Step)
			{
				const int32 Sign = Step == 0 ? 0 : (Step % 2 == 1 ? 1 : -1);
				const int32 Magnitude = (Step + 1) / 2;
				// Initial C3 keeps its two-section cadence for stable hashes and
				// low-tier budgets. A post-C2 local repair must also search the
				// alternate section phase; otherwise a dense floor stack with the
				// same two-section cadence can reject every legal belt height.
				const double HeightStep = bContinueExistingPlan
					? Section : Section * 2.0;
				AddCandidateMidZ(DesiredMidZ
					+ Sign * Magnitude * HeightStep);
			}
			// Score only after both reusable floor heights and deterministic
			// fallback heights have been collected.  Scoring the array before the
			// fallback loop leaves a sparse fixture with 65 reported candidates but
			// no candidate to evaluate.
			struct FScoredBeltHeight
			{
				double MidZ = 0.0;
				int32 ReuseCount = 0;
			};
			TArray<FScoredBeltHeight> ScoredCandidateMidZs;
			ScoredCandidateMidZs.Reserve(CandidateMidZs.Num());
			for (const double CandidateMidZ : CandidateMidZs)
			{
				FScoredBeltHeight& Scored =
					ScoredCandidateMidZs.AddDefaulted_GetRef();
				Scored.MidZ = CandidateMidZ;
				Scored.ReuseCount = CountReusableCoreCourses(
					Scratch, Origin.Position, XStation.Position,
					YStation.Position, CandidateMidZ, Section, Tolerance);
			}
			ScoredCandidateMidZs.Sort([DesiredMidZ](
				const FScoredBeltHeight& A, const FScoredBeltHeight& B)
			{
				if (A.ReuseCount != B.ReuseCount)
				{
					return A.ReuseCount > B.ReuseCount;
				}
				const double DistanceA = FMath::Abs(A.MidZ - DesiredMidZ);
				const double DistanceB = FMath::Abs(B.MidZ - DesiredMidZ);
				return DistanceA != DistanceB
					? DistanceA < DistanceB : A.MidZ < B.MidZ;
			});
			int32 RangeRejectedCount = 0;
			int32 ReservedVoidRejectedCount = 0;
			int32 HorizontalRejectedCount = 0;
			int32 ClearanceRejectedCount = 0;
			for (const FScoredBeltHeight& ScoredCandidate : ScoredCandidateMidZs)
			{
				const double CandidateMidZ = ScoredCandidate.MidZ;
				const double XZ = CandidateMidZ - Section * 0.5;
				const double YZ = CandidateMidZ + Section * 0.5;
				if (XZ - Section * 0.5 <= SelectedHost.MinimumZ + Section
					|| YZ + Section * 0.5 >= SelectedHost.MaximumZ - Section)
				{
					++RangeRejectedCount;
					continue;
				}
				const FBox CandidateBounds[4] = {
					CourseBounds(FVector(XMin, Origin.Position.Y, XZ),
						FVector(XMax, Origin.Position.Y, XZ),
						EABTSM73BeamAFrameAxis::X, Section),
					CourseBounds(FVector(XMin, YStation.Position.Y, XZ),
						FVector(XMax, YStation.Position.Y, XZ),
						EABTSM73BeamAFrameAxis::X, Section),
					CourseBounds(FVector(Origin.Position.X, YMin, YZ),
						FVector(Origin.Position.X, YMax, YZ),
						EABTSM73BeamAFrameAxis::Y, Section),
					CourseBounds(FVector(XStation.Position.X, YMin, YZ),
						FVector(XStation.Position.X, YMax, YZ),
						EABTSM73BeamAFrameAxis::Y, Section)};
				const EABTSM73BeamAFrameAxis Axes[4] = {
					EABTSM73BeamAFrameAxis::X, EABTSM73BeamAFrameAxis::X,
					EABTSM73BeamAFrameAxis::Y, EABTSM73BeamAFrameAxis::Y};
				bool bReservedVoidConflict = false;
				bool bHorizontalConflict = false;
				for (int32 CourseIndex = 0; CourseIndex < 4; ++CourseIndex)
				{
					bReservedVoidConflict |= IntersectsReservedSupportVoid(
						Scratch, CandidateBounds[CourseIndex],
						SelectedHost.SourceVolumeId, Tolerance);
					bHorizontalConflict |= ConflictsWithExistingHorizontal(
						Scratch, CandidateBounds[CourseIndex], Axes[CourseIndex],
						Section, Tolerance);
				}
				const FVector2D CoreStations[4] = {
					Origin.Position, XStation.Position,
					YStation.Position, Diagonal.Position};
				bool bClearanceConflict = false;
				for (const FVector2D& Station : CoreStations)
				{
				// Closure removes any horizontal slab from a vertical post. A core
				// belt is valid only when one complete post section remains below
				// its X courses and above its Y courses. This is equally mandatory
				// during post-C2 repair: skipping it creates a graph-valid plan whose
				// synthetic bearing posts physically penetrate ordinary floor beams.
				bClearanceConflict |= !HasCorePostFaceClearance(
					Scratch, Station,
					XZ - Section * 0.5,
					YZ + Section * 0.5,
					Section, Tolerance);
				}
				ReservedVoidRejectedCount += bReservedVoidConflict ? 1 : 0;
				HorizontalRejectedCount += bHorizontalConflict ? 1 : 0;
				ClearanceRejectedCount += bClearanceConflict ? 1 : 0;
				if (!bReservedVoidConflict && !bHorizontalConflict
					&& !bClearanceConflict)
				{
					MidZ = CandidateMidZ;
					break;
				}
			}
			if (!FMath::IsFinite(MidZ)
				|| MidZ == TNumericLimits<double>::Max())
			{
				AppendError = FString::Printf(
					TEXT("BeamC3NoLegalBeltHeight:Belt=%d:Candidates=%d:")
					TEXT("Range=%d:Void=%d:Horizontal=%d:Clearance=%d:"),
					BeltIndex, CandidateMidZs.Num(), RangeRejectedCount,
					ReservedVoidRejectedCount, HorizontalRejectedCount,
					ClearanceRejectedCount);
				return false;
			}
			const double XZ = MidZ - Section * 0.5;
			const double YZ = MidZ + Section * 0.5;
			bool bInserted = false;
			AddOrReuseCoreMember(Scratch, CoreAssembly,
				FVector(XMin, Origin.Position.Y, XZ),
				FVector(XMax, Origin.Position.Y, XZ),
				EABTSM73BeamAFrameAxis::X, Section, Tolerance, bInserted);
			InsertedCourseCount += bInserted ? 1 : 0;
			AddOrReuseCoreMember(Scratch, CoreAssembly,
				FVector(XMin, YStation.Position.Y, XZ),
				FVector(XMax, YStation.Position.Y, XZ),
				EABTSM73BeamAFrameAxis::X, Section, Tolerance, bInserted);
			InsertedCourseCount += bInserted ? 1 : 0;
			AddOrReuseCoreMember(Scratch, CoreAssembly,
				FVector(Origin.Position.X, YMin, YZ),
				FVector(Origin.Position.X, YMax, YZ),
				EABTSM73BeamAFrameAxis::Y, Section, Tolerance, bInserted);
			InsertedCourseCount += bInserted ? 1 : 0;
			AddOrReuseCoreMember(Scratch, CoreAssembly,
				FVector(XStation.Position.X, YMin, YZ),
				FVector(XStation.Position.X, YMax, YZ),
				EABTSM73BeamAFrameAxis::Y, Section, Tolerance, bInserted);
			InsertedCourseCount += bInserted ? 1 : 0;
			OutPlan.BeltMidZs.Add(MidZ);
		}
		AppendError.Reset();
		return true;
	};
	TSet<FString> UsedHostSignatures;
	TSet<FString> AcceptedHostSignatures;
	TSet<FString> TriedSyntheticHostSignatures;
	for (const FABTSM73BeamC3CribCoreHostPlan& ExistingPlan : HostPlans)
	{
		if (ExistingPlan.StationPositions.IsEmpty())
		{
			continue;
		}
		double HostMinX = TNumericLimits<double>::Max();
		double HostMaxX = -TNumericLimits<double>::Max();
		double HostMinY = TNumericLimits<double>::Max();
		double HostMaxY = -TNumericLimits<double>::Max();
		for (const FVector2D& Position : ExistingPlan.StationPositions)
		{
			HostMinX = FMath::Min(HostMinX, Position.X);
			HostMaxX = FMath::Max(HostMaxX, Position.X);
			HostMinY = FMath::Min(HostMinY, Position.Y);
			HostMaxY = FMath::Max(HostMaxY, Position.Y);
		}
		const FString AcceptedSignature = FString::Printf(
			TEXT("%d:%.3f:%.3f:%.3f:%.3f"), ExistingPlan.SourceVolumeId,
			HostMinX, HostMaxX, HostMinY, HostMaxY);
		UsedHostSignatures.Add(AcceptedSignature);
		AcceptedHostSignatures.Add(AcceptedSignature);
	}
	auto TryMaterializeSyntheticHost = [&](
		const TArray<FPostStation>& AvailableStations,
		const FUnbracedZViolation& TargetViolation,
		FCoreHost& OutHost,
		FABTSM73BeamC3CribCoreHostPlan& OutPlan,
		FString& AttemptError)
	{
		const double AlignmentTolerance = FMath::Max(
			Tolerance, Section * 0.15);
		const int32 TargetStationIndex =
			AvailableStations.IndexOfByPredicate(
				[&TargetViolation, AlignmentTolerance](
					const FPostStation& Station)
				{
					return Station.Position.Equals(
						TargetViolation.Station, AlignmentTolerance);
				});
		if (TargetStationIndex == INDEX_NONE)
		{
			AttemptError = TEXT("BeamC3SyntheticTargetStationMissing");
			return false;
		}
		const FPostStation& TargetStation =
			AvailableStations[TargetStationIndex];
		const FVerticalInterval* TargetInterval =
			TargetStation.ContinuousIntervals.FindByPredicate(
				[&TargetViolation, Tolerance](const FVerticalInterval& Interval)
				{
					return Interval.MinimumZ
						<= TargetViolation.MinimumZ + Tolerance
						&& Interval.MaximumZ
						>= TargetViolation.MaximumZ - Tolerance;
				});
		if (TargetInterval == nullptr)
		{
			AttemptError = TEXT("BeamC3SyntheticTargetIntervalMissing");
			return false;
		}

		TArray<int32> CandidateBayIds = TargetStation.BayIds.Array();
		if (TargetViolation.MemberId != INDEX_NONE)
		{
			for (const FABTSM73BeamAAssembly& MemberAssembly : Scratch.Assemblies)
			{
				if (MemberAssembly.MemberIds.Contains(TargetViolation.MemberId))
				{
					CandidateBayIds.AddUnique(MemberAssembly.BayId);
				}
			}
		}
		if (CandidateBayIds.IsEmpty())
		{
			for (const int32 SourceVolumeId : TargetStation.SourceVolumeIds)
			{
				for (const FABTSM73BeamABay& Bay : Scratch.Bays)
				{
					if (Bay.SourceVolumeId == SourceVolumeId)
					{
						CandidateBayIds.AddUnique(Bay.BayId);
					}
				}
			}
		}
		CandidateBayIds.Sort();
		if (CandidateBayIds.IsEmpty())
		{
			AttemptError = TEXT("BeamC3SyntheticHostNoOwningBay");
			return false;
		}

		struct FSyntheticCandidate
		{
			TArray<FVector2D> Stations;
			int32 BayId = INDEX_NONE;
			int32 SourceVolumeId = INDEX_NONE;
			double MinimumZ = 0.0;
			double MaximumZ = 0.0;
			int32 NewPostCount = 4;
			double ArmScore = 0.0;
			FString Signature;
		};
		TArray<FSyntheticCandidate> Candidates;
		auto StationCovers = [&AvailableStations, AlignmentTolerance, Tolerance](
			const FVector2D& Position,
			const double MinimumZ,
			const double MaximumZ) -> bool
		{
			const FPostStation* Existing = AvailableStations.FindByPredicate(
				[&Position, AlignmentTolerance](const FPostStation& Station)
				{
					return Station.Position.Equals(Position, AlignmentTolerance);
				});
			return Existing != nullptr
				&& Existing->ContinuousIntervals.ContainsByPredicate(
					[MinimumZ, MaximumZ, Tolerance](
						const FVerticalInterval& Interval)
					{
						return Interval.MinimumZ <= MinimumZ + Tolerance
							&& Interval.MaximumZ >= MaximumZ - Tolerance;
					});
		};
		for (const int32 BayId : CandidateBayIds)
		{
			if (!Scratch.Bays.IsValidIndex(BayId))
			{
				continue;
			}
			const FABTSM73BeamABay& Bay = Scratch.Bays[BayId];
			if (Bay.SourceVolumeId == INDEX_NONE)
			{
				continue;
			}
			// A continuous post chain may cross two semantic volumes even though
			// its physical XY station is singular. C3 is an assembly-level safety
			// rewrite, so its compact fallback rectangle may use the full building
			// envelope while retaining the target Bay as deterministic ownership.
			FBox SourceBounds(EForceInit::ForceInit);
			for (const FABTSM73BeamABay& SourceBay : Scratch.Bays)
			{
				SourceBounds += SourceBay.LocalBounds;
			}
			if (!SourceBounds.IsValid)
			{
				continue;
			}
			const double MinimumX = SourceBounds.Min.X + Section * 0.5;
			const double MaximumX = SourceBounds.Max.X - Section * 0.5;
			const double MinimumY = SourceBounds.Min.Y + Section * 0.5;
			const double MaximumY = SourceBounds.Max.Y - Section * 0.5;
			const FVector2D Target = TargetStation.Position;
			const double SyntheticMinimumArmSpan = bContinueExistingPlan
				? FMath::Min(static_cast<double>(Settings.MinimumCoreArmSpanCM),
					Section * 2.0)
				: static_cast<double>(Settings.MinimumCoreArmSpanCM);
			if (Target.X < MinimumX - AlignmentTolerance
				|| Target.X > MaximumX + AlignmentTolerance
				|| Target.Y < MinimumY - AlignmentTolerance
				|| Target.Y > MaximumY + AlignmentTolerance
				|| TargetInterval->MinimumZ
					< SourceBounds.Min.Z - AlignmentTolerance
				|| TargetInterval->MaximumZ
					> SourceBounds.Max.Z + AlignmentTolerance)
			{
				continue;
			}

			TArray<double> XCoordinates;
			TArray<double> YCoordinates;
			auto AddCoordinate = [AlignmentTolerance](
				TArray<double>& Coordinates, const double Value)
			{
				if (!Coordinates.ContainsByPredicate(
					[Value, AlignmentTolerance](const double Existing)
					{
						return FMath::Abs(Existing - Value)
							<= AlignmentTolerance;
					}))
				{
					Coordinates.Add(Value);
				}
			};
			for (const FPostStation& Station : AvailableStations)
			{
				const bool bSameSource =
					Station.SourceVolumeIds.Contains(Bay.SourceVolumeId);
				const bool bCovers = Station.ContinuousIntervals.ContainsByPredicate(
					[&TargetInterval, Tolerance](
						const FVerticalInterval& Interval)
					{
						return Interval.MinimumZ
							<= TargetInterval->MinimumZ + Tolerance
							&& Interval.MaximumZ
							>= TargetInterval->MaximumZ - Tolerance;
					});
				if (!bSameSource || !bCovers)
				{
					continue;
				}
				if (FMath::Abs(Station.Position.Y - Target.Y)
						<= AlignmentTolerance
					&& FMath::Abs(Station.Position.X - Target.X)
						>= SyntheticMinimumArmSpan
					&& Station.Position.X >= MinimumX - AlignmentTolerance
					&& Station.Position.X <= MaximumX + AlignmentTolerance)
				{
					AddCoordinate(XCoordinates, Station.Position.X);
				}
				if (FMath::Abs(Station.Position.X - Target.X)
						<= AlignmentTolerance
					&& FMath::Abs(Station.Position.Y - Target.Y)
						>= SyntheticMinimumArmSpan
					&& Station.Position.Y >= MinimumY - AlignmentTolerance
					&& Station.Position.Y <= MaximumY + AlignmentTolerance)
				{
					AddCoordinate(YCoordinates, Station.Position.Y);
				}
			}
			const double SyntheticXs[6] = {
				Target.X - SyntheticMinimumArmSpan,
				Target.X + SyntheticMinimumArmSpan,
				Target.X - Settings.MinimumCoreArmSpanCM,
				Target.X + Settings.MinimumCoreArmSpanCM,
				MinimumX, MaximumX};
			const double SyntheticYs[6] = {
				Target.Y - SyntheticMinimumArmSpan,
				Target.Y + SyntheticMinimumArmSpan,
				Target.Y - Settings.MinimumCoreArmSpanCM,
				Target.Y + Settings.MinimumCoreArmSpanCM,
				MinimumY, MaximumY};
			for (const double X : SyntheticXs)
			{
				if (X >= MinimumX - AlignmentTolerance
					&& X <= MaximumX + AlignmentTolerance
					&& FMath::Abs(X - Target.X)
						>= SyntheticMinimumArmSpan)
				{
					AddCoordinate(XCoordinates, X);
				}
			}
			for (const double Y : SyntheticYs)
			{
				if (Y >= MinimumY - AlignmentTolerance
					&& Y <= MaximumY + AlignmentTolerance
					&& FMath::Abs(Y - Target.Y)
						>= SyntheticMinimumArmSpan)
				{
					AddCoordinate(YCoordinates, Y);
				}
			}
			XCoordinates.Sort();
			YCoordinates.Sort();
			for (const double X : XCoordinates)
			for (const double Y : YCoordinates)
			{
				FSyntheticCandidate Candidate;
				Candidate.Stations = {
					Target, FVector2D(X, Target.Y),
					FVector2D(Target.X, Y), FVector2D(X, Y)};
				Candidate.BayId = BayId;
				Candidate.SourceVolumeId = Bay.SourceVolumeId;
				Candidate.MinimumZ = TargetInterval->MinimumZ;
				Candidate.MaximumZ = TargetInterval->MaximumZ;
				Candidate.ArmScore = FMath::Abs(X - Target.X)
					+ FMath::Abs(Y - Target.Y);
				const double HostMinX = FMath::Min(Target.X, X);
				const double HostMaxX = FMath::Max(Target.X, X);
				const double HostMinY = FMath::Min(Target.Y, Y);
				const double HostMaxY = FMath::Max(Target.Y, Y);
				Candidate.Signature = FString::Printf(
					TEXT("%d:%.3f:%.3f:%.3f:%.3f"), Bay.SourceVolumeId,
					HostMinX, HostMaxX, HostMinY, HostMaxY);
				// Ordinary host search records rejected rectangles so it can make
				// progress. A synthetic host with the same footprint is materially
				// different because it fills all four post intervals; only a core
				// that was actually accepted may suppress this fallback.
				const bool bSuppressSyntheticCandidate = bContinueExistingPlan
					? AcceptedHostSignatures.Contains(Candidate.Signature)
						|| TriedSyntheticHostSignatures.Contains(Candidate.Signature)
					: UsedHostSignatures.Contains(Candidate.Signature);
				if (bSuppressSyntheticCandidate)
				{
					continue;
				}
				bool bReserved = false;
				Candidate.NewPostCount = 0;
				for (const FVector2D& Position : Candidate.Stations)
				{
					if (StationCovers(Position,
						Candidate.MinimumZ, Candidate.MaximumZ))
					{
						continue;
					}
					++Candidate.NewPostCount;
					const FBox PostBounds(
						FVector(Position.X - Section * 0.5,
							Position.Y - Section * 0.5, Candidate.MinimumZ),
						FVector(Position.X + Section * 0.5,
							Position.Y + Section * 0.5, Candidate.MaximumZ));
					bReserved |= IntersectsReservedSupportVoid(
						Scratch, PostBounds, Candidate.SourceVolumeId, Tolerance);
				}
				if (!bReserved)
				{
					Candidates.Add(MoveTemp(Candidate));
				}
			}
		}
		Candidates.Sort([](const FSyntheticCandidate& A,
			const FSyntheticCandidate& B)
		{
			if (A.NewPostCount != B.NewPostCount)
			{
				return A.NewPostCount < B.NewPostCount;
			}
			if (A.ArmScore != B.ArmScore)
			{
				return A.ArmScore < B.ArmScore;
			}
			if (A.BayId != B.BayId)
			{
				return A.BayId < B.BayId;
			}
			return A.Signature < B.Signature;
		});
		if (Candidates.IsEmpty())
		{
			AttemptError = TEXT("BeamC3SyntheticHostNoOwningBay");
			return false;
		}

		FString LastError = TEXT("BeamC3SyntheticHostReservedVoidConflict");
		for (const FSyntheticCandidate& Candidate : Candidates)
		{
			if (bContinueExistingPlan)
			{
				TriedSyntheticHostSignatures.Add(Candidate.Signature);
			}
			FABTSM73BeamAGenerationResult BeforeAttempt = Scratch;
			const int32 BeforeInsertedCourses = InsertedCourseCount;
			const int32 BeforeInsertedPosts = InsertedSyntheticPostCount;
			const int32 BeforeReusedPosts = ReusedHostPostMemberCount;
			FABTSM73BeamAAssembly& PostAssembly =
				Scratch.Assemblies.AddDefaulted_GetRef();
			PostAssembly.AssemblyId = Scratch.Assemblies.Num() - 1;
			PostAssembly.BayId = Candidate.BayId;
			PostAssembly.Type = EABTSM73BeamAAssemblyType::CribCore;
			for (const FVector2D& Position : Candidate.Stations)
			{
				// Materialize the complete four-post interval even at reusable
				// stations. Authoritative closure merges overlapping collinear
				// pieces, while the full temporary post closes small course-sized
				// gaps that BuildPostStations intentionally treats as continuous.
				// Without this, a reused corner can have a mathematical interval
				// but no real post face touching one side of the new belt.
				if (AddSyntheticCorePost(Scratch, PostAssembly, Position,
					Candidate.MinimumZ, Candidate.MaximumZ) == INDEX_NONE)
				{
					LastError = TEXT("BeamC3SyntheticHostPostFailed");
					break;
				}
				++InsertedSyntheticPostCount;
			}
			if (InsertedSyntheticPostCount - BeforeInsertedPosts
				!= Candidate.Stations.Num())
			{
				Scratch = MoveTemp(BeforeAttempt);
				InsertedCourseCount = BeforeInsertedCourses;
				InsertedSyntheticPostCount = BeforeInsertedPosts;
				ReusedHostPostMemberCount = BeforeReusedPosts;
				continue;
			}
			TArray<FPostStation> MaterializedStations;
			BuildPostStations(Scratch, Tolerance, Section, MaterializedStations);
			FCoreHost SyntheticHost;
			int32* Indices[4] = {
				&SyntheticHost.OriginStation, &SyntheticHost.XStation,
				&SyntheticHost.YStation, &SyntheticHost.DiagonalStation};
			bool bAllStationsFound = true;
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				*Indices[Corner] = MaterializedStations.IndexOfByPredicate(
					[&Candidate, Corner, AlignmentTolerance](
						const FPostStation& Station)
					{
						return Station.Position.Equals(
							Candidate.Stations[Corner], AlignmentTolerance);
					});
				bAllStationsFound &= *Indices[Corner] != INDEX_NONE;
			}
			if (bAllStationsFound)
			{
				SyntheticHost.BayId = Candidate.BayId;
				SyntheticHost.SourceVolumeId = Candidate.SourceVolumeId;
				SyntheticHost.MinimumZ = Candidate.MinimumZ;
				SyntheticHost.MaximumZ = Candidate.MaximumZ;
				SyntheticHost.Signature = Candidate.Signature;
				FABTSM73BeamC3CribCoreHostPlan CandidatePlan;
				FString CandidateError;
				if (AppendHost(MaterializedStations, SyntheticHost,
					CandidatePlan, CandidateError))
				{
					UsedHostSignatures.Add(Candidate.Signature);
					OutHost = SyntheticHost;
					OutPlan = MoveTemp(CandidatePlan);
					AttemptError.Reset();
					return true;
				}
				LastError = CandidateError;
			}
			else
			{
				LastError = TEXT("BeamC3SyntheticTargetStationMissing");
			}
			Scratch = MoveTemp(BeforeAttempt);
			InsertedCourseCount = BeforeInsertedCourses;
			InsertedSyntheticPostCount = BeforeInsertedPosts;
			ReusedHostPostMemberCount = BeforeReusedPosts;
		}
		AttemptError = LastError;
		return false;
	};
	auto TryAppendHost = [&](const TArray<FPostStation>& AvailableStations,
		const FUnbracedZViolation& TargetViolation,
		FCoreHost& OutHost,
		FABTSM73BeamC3CribCoreHostPlan& OutPlan,
		FString& AttemptError)
	{
		FString LastHostError = TEXT("BeamC3NoClosedCoreHost");
		while (SelectCoreHost(Settings, BeamASettings, Scratch,
			AvailableStations, &TargetViolation,
			UsedHostSignatures, OutHost))
		{
			UsedHostSignatures.Add(OutHost.Signature);
			FABTSM73BeamAGenerationResult BeforeAttempt = Scratch;
			const int32 BeforeInserted = InsertedCourseCount;
			const int32 BeforeInsertedPosts = InsertedSyntheticPostCount;
			const int32 BeforeReusedPosts = ReusedHostPostMemberCount;
			FABTSM73BeamC3CribCoreHostPlan CandidatePlan;
			FString CandidateError;
			if (AppendHost(AvailableStations, OutHost,
				CandidatePlan, CandidateError))
			{
				// This host already consists of real Z stations.  Duplicating each
				// complete vertical interval makes a dense high-tier floor stack cross
				// hundreds of new post/horizontal pairs before global closure can merge
				// them.  Keep the existing columns and let
				// RestoreHostPlanPostBearingFaces add only a missing one-section contact
				// face after the new crib courses have been closed.
				OutPlan = MoveTemp(CandidatePlan);
				AttemptError.Reset();
				return true;
			}
			LastHostError = CandidateError;
			Scratch = MoveTemp(BeforeAttempt);
			InsertedCourseCount = BeforeInserted;
			InsertedSyntheticPostCount = BeforeInsertedPosts;
			ReusedHostPostMemberCount = BeforeReusedPosts;
		}
		if (TryMaterializeSyntheticHost(AvailableStations,
			TargetViolation, OutHost, OutPlan, AttemptError))
		{
			return true;
		}
		AttemptError = FString::Printf(
			TEXT("BeamC3NoLegalClosedCoreHost:%s:%s"),
			*LastHostError, *AttemptError);
		return false;
	};
	auto TryAddTargetedTie = [&](const TArray<FPostStation>& AvailableStations,
		const FUnbracedZViolation& TargetViolation,
		FString& AttemptError)
	{
		const double AlignmentTolerance = FMath::Max(Tolerance, Section * 0.15);
		int32 TargetIndex = INDEX_NONE;
		double BestTargetDistance = TNumericLimits<double>::Max();
		for (int32 Index = 0; Index < AvailableStations.Num(); ++Index)
		{
			const double Distance = FVector2D::Distance(
				AvailableStations[Index].Position, TargetViolation.Station);
			if (Distance < BestTargetDistance && Distance <= Section)
			{
				TargetIndex = Index;
				BestTargetDistance = Distance;
			}
		}
		if (TargetIndex == INDEX_NONE)
		{
			AttemptError = TEXT("BeamC3TargetedTieStationMissing");
			return false;
		}
		EABTSM73BeamAFrameAxis BraceAxis = TargetViolation.MissingBraceAxis;
		bool bFoundX = false;
		bool bFoundY = false;
		const float XSpan = MaximumUnbracedSpanAtStationAxis(
			Scratch, TargetViolation.Station, EABTSM73BeamAFrameAxis::X,
			CertifiedEvidence.CertifiedCourseMemberIds,
			CertifiedEvidence.BiaxialCourseMemberIds,
			CertifiedEvidence.CertifiedSourceIdsByCourse,
			Section, Tolerance, bFoundX);
		const float YSpan = MaximumUnbracedSpanAtStationAxis(
			Scratch, TargetViolation.Station, EABTSM73BeamAFrameAxis::Y,
			CertifiedEvidence.CertifiedCourseMemberIds,
			CertifiedEvidence.BiaxialCourseMemberIds,
			CertifiedEvidence.CertifiedSourceIdsByCourse,
			Section, Tolerance, bFoundY);
		if (BraceAxis != EABTSM73BeamAFrameAxis::X
			&& BraceAxis != EABTSM73BeamAFrameAxis::Y)
		{
			BraceAxis = XSpan >= YSpan
				? EABTSM73BeamAFrameAxis::X
				: EABTSM73BeamAFrameAxis::Y;
		}
		bool bFoundTargetInterval = BraceAxis == EABTSM73BeamAFrameAxis::X
			? bFoundX : bFoundY;
		float BeforeTargetSpan = BraceAxis == EABTSM73BeamAFrameAxis::X
			? XSpan : YSpan;
		int32 PreferredAlignedAnchorCount = 0;
		int32 PreferredRangeRejectedCount = 0;
		int32 PreferredTargetCoverageRejectedCount = 0;
		int32 PreferredAddedCount = 0;
		int32 RootedPortalAddedCount = 0;
		if (bContinueExistingPlan)
		{
			BeforeTargetSpan = MaximumUnbracedSpanWithinIntervalAtStationAxis(
				Scratch, TargetViolation.Station, BraceAxis,
				TargetViolation.MinimumZ, TargetViolation.MaximumZ,
				CertifiedEvidence.CertifiedCourseMemberIds,
				CertifiedEvidence.BiaxialCourseMemberIds,
				CertifiedEvidence.CertifiedSourceIdsByCourse,
				Section, Tolerance, bFoundTargetInterval);
		}
		if ((!bFoundX && BraceAxis == EABTSM73BeamAFrameAxis::X)
			|| (!bFoundY && BraceAxis == EABTSM73BeamAFrameAxis::Y)
			|| !bFoundTargetInterval
			|| BeforeTargetSpan <= Settings.MaximumUnbracedCorePostSpanCM + Tolerance)
		{
			AttemptError = TEXT("BeamC3TargetedTieNoAxisViolation");
			return false;
		}

		const FPostStation& TargetStation = AvailableStations[TargetIndex];
		const int32 AxisIndex = static_cast<int32>(BraceAxis);
		const int32 OtherAxisIndex = BraceAxis == EABTSM73BeamAFrameAxis::X ? 1 : 0;
		struct FTieCandidate
		{
			FVector2D EndStation = FVector2D::ZeroVector;
			double MinimumZ = 0.0;
			double MaximumZ = 0.0;
			int32 BayId = INDEX_NONE;
			int32 SourceVolumeId = INDEX_NONE;
			int32 AnchorHostPlanIndex = INDEX_NONE;
			double Distance = 0.0;
			double PreferredContactZ = 0.0;
			bool bHasPreferredContactZ = false;
			bool bRootedCoursePortal = false;
			FString Signature;
		};
		TArray<FTieCandidate> Candidates;
		auto AddCandidate = [&](const FVector2D& EndPosition,
			const double MinimumZ, const double MaximumZ,
			const int32 BayId, const int32 AnchorHostPlanIndex)
		{
			// A station can contain several disjoint continuous post intervals.
			// An incremental C2 repair must brace the exact interval reported by
			// the final audit, not a shorter interval from another floor that
			// happens to share the same XY station.
			const double ClippedMinimumZ = bContinueExistingPlan
				? FMath::Max(MinimumZ, TargetViolation.MinimumZ)
				: MinimumZ;
			const double ClippedMaximumZ = bContinueExistingPlan
				? FMath::Min(MaximumZ, TargetViolation.MaximumZ)
				: MaximumZ;
			if (!Scratch.Bays.IsValidIndex(BayId)
				|| ClippedMaximumZ - ClippedMinimumZ < Section * 4.0)
			{
				return;
			}
			const int32 ExistingIndex = Candidates.IndexOfByPredicate(
				[&EndPosition, BayId, AnchorHostPlanIndex, AlignmentTolerance](
					const FTieCandidate& Existing)
				{
					return Existing.BayId == BayId
						&& Existing.AnchorHostPlanIndex == AnchorHostPlanIndex
						&& Existing.EndStation.Equals(
							EndPosition, AlignmentTolerance);
				});
			if (ExistingIndex != INDEX_NONE)
			{
				FTieCandidate& Existing = Candidates[ExistingIndex];
				const double TargetMidZ =
					(TargetViolation.MinimumZ + TargetViolation.MaximumZ) * 0.5;
				const bool bExistingContainsTarget =
					TargetMidZ >= Existing.MinimumZ
						&& TargetMidZ <= Existing.MaximumZ;
				const bool bNewContainsTarget =
					TargetMidZ >= ClippedMinimumZ
						&& TargetMidZ <= ClippedMaximumZ;
				const double ExistingHeight =
					Existing.MaximumZ - Existing.MinimumZ;
				const double NewHeight = ClippedMaximumZ - ClippedMinimumZ;
				if (!bNewContainsTarget
					|| (bExistingContainsTarget
						&& NewHeight <= ExistingHeight + Tolerance))
				{
					return;
				}
				Existing.MinimumZ = ClippedMinimumZ;
				Existing.MaximumZ = ClippedMaximumZ;
				Existing.Signature = FString::Printf(
					TEXT("%d:%d:%.3f:%.3f:%.3f:%.3f"),
					static_cast<int32>(BraceAxis), BayId,
					EndPosition.X, EndPosition.Y,
					ClippedMinimumZ, ClippedMaximumZ);
				return;
			}
			FTieCandidate Candidate;
			Candidate.EndStation = EndPosition;
			Candidate.MinimumZ = ClippedMinimumZ;
			Candidate.MaximumZ = ClippedMaximumZ;
			Candidate.BayId = BayId;
			Candidate.SourceVolumeId = Scratch.Bays[BayId].SourceVolumeId;
			Candidate.AnchorHostPlanIndex = AnchorHostPlanIndex;
			Candidate.Distance = FMath::Abs(
				EndPosition[AxisIndex] - TargetStation.Position[AxisIndex]);
			Candidate.Signature = FString::Printf(TEXT("%d:%d:%.3f:%.3f:%.3f:%.3f"),
				static_cast<int32>(BraceAxis), BayId,
				EndPosition.X, EndPosition.Y,
				ClippedMinimumZ, ClippedMaximumZ);
			Candidates.Add(MoveTemp(Candidate));
		};
		for (int32 OtherIndex = 0; OtherIndex < AvailableStations.Num(); ++OtherIndex)
		{
			if (OtherIndex == TargetIndex)
			{
				continue;
			}
			const FPostStation& Other = AvailableStations[OtherIndex];
			const int32 AnchorHostPlanIndex = HostPlans.IndexOfByPredicate(
				[&Other, AlignmentTolerance](
					const FABTSM73BeamC3CribCoreHostPlan& Plan)
				{
					return Plan.StationPositions.ContainsByPredicate(
						[&Other, AlignmentTolerance](const FVector2D& Station)
						{
							return Station.Equals(
								Other.Position, AlignmentTolerance);
						});
				});
			if (FMath::Abs(Other.Position[OtherAxisIndex]
					- TargetStation.Position[OtherAxisIndex]) > AlignmentTolerance
				|| FMath::Abs(Other.Position[AxisIndex]
					- TargetStation.Position[AxisIndex])
					< Settings.MinimumCoreArmSpanCM
				|| AnchorHostPlanIndex == INDEX_NONE)
			{
				continue;
			}
			TArray<int32> SharedBays;
			for (const int32 BayId : TargetStation.BayIds)
			{
				if (Other.BayIds.Contains(BayId))
				{
					SharedBays.Add(BayId);
				}
			}
			SharedBays.Sort();
			if (SharedBays.IsEmpty())
			{
				// A target post can be owned by an adjacent Bay even before C2 runs.
				// The tie is an assembly-level course, so a Bay seam is not a physical
				// discontinuity.  Permit that seam at every tier while retaining the
				// stricter same-SourceVolume contract and all reserved-void,
				// real-contact, rooted-path and span-reduction audits below.
				const int32 AnchorBayId =
					HostPlans[AnchorHostPlanIndex].BayId;
				if (Scratch.Bays.IsValidIndex(AnchorBayId))
				{
					const int32 SourceVolumeId =
						Scratch.Bays[AnchorBayId].SourceVolumeId;
					if (SourceVolumeId != INDEX_NONE
						&& TargetStation.SourceVolumeIds.Contains(SourceVolumeId)
						&& Other.SourceVolumeIds.Contains(SourceVolumeId))
					{
						SharedBays.Add(AnchorBayId);
					}
				}
			}
			for (const FVerticalInterval& TargetInterval :
				TargetStation.ContinuousIntervals)
			{
				for (const FVerticalInterval& OtherInterval : Other.ContinuousIntervals)
				{
					const FABTSM73BeamC3CribCoreHostPlan& AnchorHost =
						HostPlans[AnchorHostPlanIndex];
					const double AnchorMinimumZ = FMath::Max(
						OtherInterval.MinimumZ, AnchorHost.MinimumZ);
					const double AnchorMaximumZ = FMath::Min(
						OtherInterval.MaximumZ, AnchorHost.MaximumZ);
					const double MinimumZ = FMath::Max(
						TargetInterval.MinimumZ, AnchorMinimumZ);
					const double MaximumZ = FMath::Min(
						TargetInterval.MaximumZ, AnchorMaximumZ);
					for (const int32 BayId : SharedBays)
					{
						AddCandidate(Other.Position, MinimumZ, MaximumZ,
							BayId, AnchorHostPlanIndex);
					}
				}
			}
		}
		if (bContinueExistingPlan)
		{
			// A post introduced by C2 often starts above every original host's
			// broad continuous interval, even though an already certified host belt
			// lies inside the violating Z range. Anchor directly to that real belt
			// height and materialize only the short post faces needed at both ends.
			// This is the zero-displacement, low-Brick alternative to inventing a
			// second closed core inside a dense floor stack.
			for (int32 HostIndex = 0; HostIndex < HostPlans.Num(); ++HostIndex)
			{
				const FABTSM73BeamC3CribCoreHostPlan& AnchorHost =
					HostPlans[HostIndex];
				if (!Scratch.Bays.IsValidIndex(AnchorHost.BayId))
				{
					continue;
				}
				for (const FVector2D& AnchorStation : AnchorHost.StationPositions)
				{
					if (FMath::Abs(AnchorStation[OtherAxisIndex]
							- TargetStation.Position[OtherAxisIndex])
							> AlignmentTolerance
						|| FMath::Abs(AnchorStation[AxisIndex]
							- TargetStation.Position[AxisIndex])
							< Settings.MinimumCoreArmSpanCM)
					{
						continue;
					}
					++PreferredAlignedAnchorCount;
					for (const double BeltMidZ : AnchorHost.BeltMidZs)
					{
						const double ContactZ =
							BraceAxis == EABTSM73BeamAFrameAxis::X
								? BeltMidZ - Section : BeltMidZ;
						const double CourseBottomZ = ContactZ;
						const double CourseTopZ = ContactZ + Section;
						if (CourseBottomZ
								< AnchorHost.MinimumZ + Section - Tolerance
							|| CourseTopZ
								> AnchorHost.MaximumZ - Section + Tolerance
							|| ContactZ <= TargetViolation.MinimumZ + Tolerance
							|| ContactZ >= TargetViolation.MaximumZ - Tolerance)
						{
							++PreferredRangeRejectedCount;
							continue;
						}
						const double MinimumZ = ContactZ - Section;
						const double MaximumZ = ContactZ + Section * 3.0;
						const bool bTargetCoversFaces =
							TargetStation.ContinuousIntervals.ContainsByPredicate(
								[MinimumZ, MaximumZ, Tolerance](
									const FVerticalInterval& Interval)
								{
									return Interval.MinimumZ <= MinimumZ + Tolerance
										&& Interval.MaximumZ
											>= MaximumZ - Tolerance;
								});
						if (!bTargetCoversFaces)
						{
							++PreferredTargetCoverageRejectedCount;
							continue;
						}
						const bool bDuplicate = Candidates.ContainsByPredicate(
							[&AnchorStation, ContactZ, HostIndex,
								AlignmentTolerance, Tolerance](
									const FTieCandidate& Existing)
							{
								return Existing.AnchorHostPlanIndex == HostIndex
									&& Existing.EndStation.Equals(
										AnchorStation, AlignmentTolerance)
									&& Existing.bHasPreferredContactZ
									&& FMath::Abs(Existing.PreferredContactZ - ContactZ)
										<= Tolerance;
							});
						if (bDuplicate)
						{
							continue;
						}
						FTieCandidate Candidate;
						Candidate.EndStation = AnchorStation;
						Candidate.MinimumZ = MinimumZ;
						Candidate.MaximumZ = MaximumZ;
						Candidate.BayId = AnchorHost.BayId;
						Candidate.SourceVolumeId = AnchorHost.SourceVolumeId;
						Candidate.AnchorHostPlanIndex = HostIndex;
						Candidate.Distance = FMath::Abs(
							AnchorStation[AxisIndex]
								- TargetStation.Position[AxisIndex]);
						Candidate.PreferredContactZ = ContactZ;
						Candidate.bHasPreferredContactZ = true;
						Candidate.Signature = FString::Printf(
							TEXT("B:%d:%d:%.3f:%.3f:%.3f"),
							static_cast<int32>(BraceAxis), HostIndex,
							AnchorStation.X, AnchorStation.Y, ContactZ);
						Candidates.Add(MoveTemp(Candidate));
						++PreferredAddedCount;
					}
				}
			}
		}
		// A same-source rooted floor course is a cheaper physical anchor than a
		// second four-post host. Project the violating station along its missing
		// brace axis onto a perpendicular, already-rooted course and stack the new
		// tie exactly one Brick above or below it. The resulting cross bearing is
		// explicit and local; no cross-SourceVolume BFS or hidden constraint is
		// introduced. This path is especially important for E1/E2 budgets.
		TArray<int32> RootedPortalCourseIds =
			CertifiedEvidence.CertifiedCourseMemberIds.Array();
		RootedPortalCourseIds.Sort();
		for (const int32 RootedCourseId : RootedPortalCourseIds)
		{
			if (!Scratch.Members.IsValidIndex(RootedCourseId))
			{
				continue;
			}
			const FABTSM73BeamAMember& RootedCourse =
				Scratch.Members[RootedCourseId];
			const EABTSM73BeamAFrameAxis RequiredAnchorAxis =
				BraceAxis == EABTSM73BeamAFrameAxis::X
					? EABTSM73BeamAFrameAxis::Y
					: EABTSM73BeamAFrameAxis::X;
			if (RootedCourse.Axis != RequiredAnchorAxis)
			{
				continue;
			}
			const FBox RootedBounds = MemberBounds(
				RootedCourse, Scratch, Section);
			if (TargetStation.Position[OtherAxisIndex]
					< RootedBounds.Min[OtherAxisIndex] - Tolerance
				|| TargetStation.Position[OtherAxisIndex]
					> RootedBounds.Max[OtherAxisIndex] + Tolerance)
			{
				continue;
			}
			FVector2D AnchorPosition = TargetStation.Position;
			AnchorPosition[AxisIndex] = RootedBounds.GetCenter()[AxisIndex];
			const double PortalDistance = FMath::Abs(
				AnchorPosition[AxisIndex]
					- TargetStation.Position[AxisIndex]);
			if (PortalDistance + Tolerance < Settings.MinimumCoreArmSpanCM)
			{
				continue;
			}
			TArray<int32> RootedSources =
				CertifiedEvidence.CertifiedSourceIdsByCourse.FindRef(
					RootedCourseId).Array();
			RootedSources.Sort();
			for (const int32 SourceVolumeId : RootedSources)
			{
				if (!TargetStation.SourceVolumeIds.Contains(SourceVolumeId))
				{
					continue;
				}
				TArray<int32> TargetBayIds = TargetStation.BayIds.Array();
				TargetBayIds.Sort();
				const int32* TargetBayIdPtr = TargetBayIds.FindByPredicate(
					[&Scratch, SourceVolumeId](const int32 BayId)
					{
						return Scratch.Bays.IsValidIndex(BayId)
							&& Scratch.Bays[BayId].SourceVolumeId
								== SourceVolumeId;
					});
				if (TargetBayIdPtr == nullptr)
				{
					continue;
				}
				const int32 TargetBayId = *TargetBayIdPtr;
				for (const FVerticalInterval& TargetInterval :
					TargetStation.ContinuousIntervals)
				{
					const double MinimumZ = bContinueExistingPlan
						? FMath::Max(TargetInterval.MinimumZ,
							TargetViolation.MinimumZ)
						: TargetInterval.MinimumZ;
					const double MaximumZ = bContinueExistingPlan
						? FMath::Min(TargetInterval.MaximumZ,
							TargetViolation.MaximumZ)
						: TargetInterval.MaximumZ;
					if (MaximumZ - MinimumZ < Section * 4.0)
					{
						continue;
					}
					const double ContactBottoms[2] = {
						RootedBounds.Max.Z, RootedBounds.Min.Z - Section};
					for (const double ContactBottomZ : ContactBottoms)
					{
						if (ContactBottomZ
								< MinimumZ + Section - Tolerance
							|| ContactBottomZ + Section
								> MaximumZ - Section + Tolerance
							|| ContactBottomZ
								<= TargetViolation.MinimumZ + Tolerance
							|| ContactBottomZ
								>= TargetViolation.MaximumZ - Tolerance)
						{
							continue;
						}
						const FString Signature = FString::Printf(
							TEXT("P:%d:%d:%d:%.3f:%.3f:%.3f"),
							static_cast<int32>(BraceAxis), TargetBayId,
							RootedCourseId, AnchorPosition.X,
							AnchorPosition.Y, ContactBottomZ);
						if (Candidates.ContainsByPredicate(
							[&Signature](const FTieCandidate& Existing)
							{
								return Existing.Signature == Signature;
							}))
						{
							continue;
						}
						FTieCandidate Candidate;
						Candidate.EndStation = AnchorPosition;
						Candidate.MinimumZ = MinimumZ;
						Candidate.MaximumZ = MaximumZ;
						Candidate.BayId = TargetBayId;
						Candidate.SourceVolumeId = SourceVolumeId;
						Candidate.Distance = PortalDistance;
						Candidate.PreferredContactZ = ContactBottomZ;
						Candidate.bHasPreferredContactZ = true;
						Candidate.bRootedCoursePortal = true;
						Candidate.Signature = Signature;
						Candidates.Add(MoveTemp(Candidate));
						++RootedPortalAddedCount;
					}
				}
			}
		}
		Candidates.Sort([](const FTieCandidate& A, const FTieCandidate& B)
		{
			const int32 ARank = A.bRootedCoursePortal ? 1
				: A.bHasPreferredContactZ ? 0 : 2;
			const int32 BRank = B.bRootedCoursePortal ? 1
				: B.bHasPreferredContactZ ? 0 : 2;
			if (ARank != BRank)
			{
				return ARank < BRank;
			}
			if (A.Distance != B.Distance)
			{
				return A.Distance < B.Distance;
			}
			return A.Signature < B.Signature;
		});
		if (Candidates.IsEmpty())
		{
			AttemptError = bContinueExistingPlan
				? FString::Printf(
					TEXT("BeamC3TargetedTieNoEndpoint:Aligned=%d:Range=%d:")
					TEXT("TargetCoverage=%d:Preferred=%d:Portal=%d"),
					PreferredAlignedAnchorCount, PreferredRangeRejectedCount,
					PreferredTargetCoverageRejectedCount, PreferredAddedCount,
					RootedPortalAddedCount)
				: TEXT("BeamC3TargetedTieNoEndpoint");
			return false;
		}

		FString LastError = TEXT("BeamC3TargetedTieNoLegalHeight");
		int32 HeightBoundsRejectedCount = 0;
		int32 ReservedVoidRejectedCount = 0;
		int32 HorizontalConflictRejectedCount = 0;
		int32 TargetClearanceRejectedCount = 0;
		int32 AnchorClearanceRejectedCount = 0;
		int32 ClosureRejectedCount = 0;
		int32 EvidenceRejectedCount = 0;
		int32 NoProgressRejectedCount = 0;
		// The same physical anchor used to be repeated by every overlapping
		// vertical-interval pair, and every duplicate triggered up to 33 global
		// closure attempts. Keep the closest deterministic endpoints and sample a
		// bounded symmetric height set. This is a search bound only: every accepted
		// tie still passes closure, rooted-topology certification and span reduction.
		constexpr int32 MaximumEndpointCandidates = 12;
		constexpr int32 MaximumHeightSearchStep = 24;
		const int32 CandidateCount = FMath::Min(
			Candidates.Num(), MaximumEndpointCandidates);
		for (int32 CandidateIndex = 0;
			CandidateIndex < CandidateCount; ++CandidateIndex)
		{
			const FTieCandidate& Candidate = Candidates[CandidateIndex];
			const double DesiredContactZ = Candidate.bHasPreferredContactZ
				? Candidate.PreferredContactZ
				: FMath::Clamp(
					(TargetViolation.MinimumZ + TargetViolation.MaximumZ) * 0.5,
					Candidate.MinimumZ + Section,
					Candidate.MaximumZ - Section * 2.0);
			const int32 HeightSearchStepCount = Candidate.bRootedCoursePortal
				? 0 : MaximumHeightSearchStep;
			for (int32 Step = 0; Step <= HeightSearchStepCount; ++Step)
			{
				const int32 Sign = Step == 0 ? 0 : (Step % 2 == 1 ? 1 : -1);
				const int32 Magnitude = (Step + 1) / 2;
				const double ContactZ = DesiredContactZ
					+ Sign * Magnitude * Section;
				// A four-section overlap has one valid bearing arrangement: one
				// complete post section below the course, the course itself, and at
				// least one complete post section above it. The candidate contract
				// deliberately accepts that minimum overlap, so do not reject its
				// exact boundary solution here. Closure and certified bearing
				// evidence remain the authoritative acceptance gates.
				if (ContactZ < Candidate.MinimumZ + Section - Tolerance
					|| ContactZ + Section > Candidate.MaximumZ - Section + Tolerance
					|| ContactZ <= TargetViolation.MinimumZ + Tolerance
					|| ContactZ >= TargetViolation.MaximumZ - Tolerance)
				{
					++HeightBoundsRejectedCount;
					continue;
				}
				FVector Start(TargetStation.Position.X,
					TargetStation.Position.Y, ContactZ + Section * 0.5);
				FVector End(Candidate.EndStation.X,
					Candidate.EndStation.Y, ContactZ + Section * 0.5);
				const double MinimumAxis = FMath::Min(
					Start[AxisIndex], End[AxisIndex]) - Section * 0.5;
				const double MaximumAxis = FMath::Max(
					Start[AxisIndex], End[AxisIndex]) + Section * 0.5;
				Start[AxisIndex] = MinimumAxis;
				End[AxisIndex] = MaximumAxis;
				Start[OtherAxisIndex] = TargetStation.Position[OtherAxisIndex];
				End[OtherAxisIndex] = TargetStation.Position[OtherAxisIndex];
				const FBox Bounds = CourseBounds(Start, End, BraceAxis, Section);
				const bool bVoidConflict = IntersectsReservedSupportVoid(
					Scratch, Bounds, Candidate.SourceVolumeId, Tolerance);
				const bool bHorizontalConflict = ConflictsWithExistingHorizontal(
					Scratch, Bounds, BraceAxis, Section, Tolerance);
				const bool bTargetClear = bContinueExistingPlan
					|| HasCorePostFaceClearance(Scratch,
						TargetStation.Position, ContactZ,
						ContactZ + Section, Section, Tolerance);
				const bool bAnchorClear = Candidate.bRootedCoursePortal
					|| bContinueExistingPlan
					|| HasCorePostFaceClearance(Scratch,
						Candidate.EndStation, ContactZ,
						ContactZ + Section, Section, Tolerance);
				if (bVoidConflict || bHorizontalConflict
					|| !bTargetClear || !bAnchorClear)
				{
					ReservedVoidRejectedCount += bVoidConflict ? 1 : 0;
					HorizontalConflictRejectedCount += bHorizontalConflict ? 1 : 0;
					TargetClearanceRejectedCount += !bTargetClear ? 1 : 0;
					AnchorClearanceRejectedCount += !bAnchorClear ? 1 : 0;
					continue;
				}
				FABTSM73BeamAGenerationResult BeforeAttempt = Scratch;
				const int32 BeforeInsertedCourses = InsertedCourseCount;
				const int32 BeforeInsertedPosts = InsertedSyntheticPostCount;
				FABTSM73BeamAAssembly& TieAssembly =
					Scratch.Assemblies.AddDefaulted_GetRef();
				TieAssembly.AssemblyId = Scratch.Assemblies.Num() - 1;
				TieAssembly.BayId = Candidate.BayId;
				TieAssembly.Type = EABTSM73BeamAAssemblyType::CribCore;
				bool bInserted = false;
				AddOrReuseCoreMember(Scratch, TieAssembly,
					Start, End, BraceAxis, Section, Tolerance, bInserted);
				InsertedCourseCount += bInserted ? 1 : 0;
				AddSyntheticCorePost(Scratch, TieAssembly,
					TargetStation.Position,
					Candidate.MinimumZ, Candidate.MaximumZ);
				++InsertedSyntheticPostCount;
				if (!Candidate.bRootedCoursePortal)
				{
					AddSyntheticCorePost(Scratch, TieAssembly,
						Candidate.EndStation,
						Candidate.MinimumZ, Candidate.MaximumZ);
					++InsertedSyntheticPostCount;
				}
				FString TieClosureError;
				if (!ABTSM73BeamA::CloseGeneratedAssembly(
					BeamASettings, Scratch, TieClosureError))
				{
					++ClosureRejectedCount;
					Scratch = MoveTemp(BeforeAttempt);
					InsertedCourseCount = BeforeInsertedCourses;
					InsertedSyntheticPostCount = BeforeInsertedPosts;
					LastError = FString::Printf(
						TEXT("BeamC3TargetedTieClosureFailed:%s"),
						*TieClosureError);
					continue;
				}
				if (bContinueExistingPlan)
				{
					const FVector2D TieStations[2] = {
						TargetStation.Position, Candidate.EndStation};
					const int32 TieStationCount =
						Candidate.bRootedCoursePortal ? 1 : 2;
					for (FABTSM73BeamAMember& Member : Scratch.Members)
					{
						if (Member.Axis != EABTSM73BeamAFrameAxis::Z)
						{
							continue;
						}
						const FBox PostBounds = MemberBounds(Member, Scratch, Section);
						for (int32 TieStationIndex = 0;
							TieStationIndex < TieStationCount; ++TieStationIndex)
						{
							const FVector2D& Station =
								TieStations[TieStationIndex];
							if (FMath::Abs(PostBounds.GetCenter().X - Station.X) <= Tolerance
								&& FMath::Abs(PostBounds.GetCenter().Y - Station.Y) <= Tolerance
								&& PostBounds.Max.Z >= Candidate.MinimumZ - Tolerance
								&& PostBounds.Min.Z <= Candidate.MaximumZ + Tolerance)
							{
								Member.Role = EABTSM73BeamAMemberRole::CorePost;
								break;
							}
						}
					}
					if (!RestoreHostPlanPostBearingFaces(
						Scratch, HostPlans, BeamASettings, Section, Tolerance,
						InsertedSyntheticPostCount, TieClosureError))
					{
						++EvidenceRejectedCount;
						Scratch = MoveTemp(BeforeAttempt);
						InsertedCourseCount = BeforeInsertedCourses;
						InsertedSyntheticPostCount = BeforeInsertedPosts;
						LastError = FString::Printf(
							TEXT("BeamC3TargetedTieContactRestoreFailed:%s"),
							*TieClosureError);
						continue;
					}
				}
				TArray<FABTSM73BeamC3TargetedTiePlan> ProposedTiePlans = TiePlans;
				FABTSM73BeamC3TargetedTiePlan& ProposedPlan =
					ProposedTiePlans.AddDefaulted_GetRef();
				ProposedPlan.AnchorStation = Candidate.EndStation;
				ProposedPlan.TargetStation = TargetStation.Position;
				ProposedPlan.Axis = BraceAxis;
				ProposedPlan.CourseCenterZ = ContactZ + Section * 0.5;
				ProposedPlan.MinimumZ = Candidate.MinimumZ;
				ProposedPlan.MaximumZ = Candidate.MaximumZ;
				ProposedPlan.bAnchorIsRootedCourse =
					Candidate.bRootedCoursePortal;
				ProposedPlan.AnchorHostPlanIndex = Candidate.AnchorHostPlanIndex;
				ProposedPlan.BayId = Candidate.BayId;
				ProposedPlan.SourceVolumeId = Candidate.SourceVolumeId;
				FCertifiedC3BraceEvidence ProposedEvidence;
				FString ProposedEvidenceError;
				if (!BuildCertifiedC3BraceEvidence(
					Scratch, HostPlans, ProposedTiePlans, Section, Tolerance,
					ProposedEvidence, ProposedEvidenceError))
				{
					++EvidenceRejectedCount;
					Scratch = MoveTemp(BeforeAttempt);
					InsertedCourseCount = BeforeInsertedCourses;
					InsertedSyntheticPostCount = BeforeInsertedPosts;
					LastError = ProposedEvidenceError;
					continue;
				}
				bool bFoundAfter = false;
				const float AfterTargetSpan = bContinueExistingPlan
					? MaximumUnbracedSpanWithinIntervalAtStationAxis(
						Scratch, TargetViolation.Station, BraceAxis,
						TargetViolation.MinimumZ, TargetViolation.MaximumZ,
						ProposedEvidence.CertifiedCourseMemberIds,
						ProposedEvidence.BiaxialCourseMemberIds,
						ProposedEvidence.CertifiedSourceIdsByCourse,
						Section, Tolerance, bFoundAfter)
					: MaximumUnbracedSpanAtStationAxis(
						Scratch, TargetViolation.Station, BraceAxis,
						ProposedEvidence.CertifiedCourseMemberIds,
						ProposedEvidence.BiaxialCourseMemberIds,
						ProposedEvidence.CertifiedSourceIdsByCourse,
						Section, Tolerance, bFoundAfter);
				if (!bFoundAfter
					|| AfterTargetSpan >= BeforeTargetSpan - Tolerance)
				{
					++NoProgressRejectedCount;
					Scratch = MoveTemp(BeforeAttempt);
					InsertedCourseCount = BeforeInsertedCourses;
					InsertedSyntheticPostCount = BeforeInsertedPosts;
					LastError = TEXT("BeamC3TargetedTieNoSpanImprovement");
					continue;
				}
				TiePlans = MoveTemp(ProposedTiePlans);
				CertifiedEvidence = MoveTemp(ProposedEvidence);
				AttemptError.Reset();
				return true;
			}
		}
		AttemptError = FString::Printf(
			TEXT("%s:Candidates=%d:Height=%d:Void=%d:Horizontal=%d:")
			TEXT("TargetClear=%d:AnchorClear=%d:Closure=%d:Evidence=%d:Progress=%d:Portal=%d"),
			*LastError, CandidateCount, HeightBoundsRejectedCount,
			ReservedVoidRejectedCount, HorizontalConflictRejectedCount,
			TargetClearanceRejectedCount, AnchorClearanceRejectedCount,
			ClosureRejectedCount, EvidenceRejectedCount,
			NoProgressRejectedCount, RootedPortalAddedCount);
		return false;
	};
	FString ClosureError;
	auto RestoreExistingPlanGeometry = [&]()
	{
		bool bInsertedGeometry = false;
		auto AddPlanAssembly = [&](const int32 BayId)
			-> FABTSM73BeamAAssembly&
		{
			FABTSM73BeamAAssembly& PlanAssembly =
				Scratch.Assemblies.AddDefaulted_GetRef();
			PlanAssembly.AssemblyId = Scratch.Assemblies.Num() - 1;
			PlanAssembly.BayId = BayId;
			PlanAssembly.Type = EABTSM73BeamAAssemblyType::CribCore;
			return PlanAssembly;
		};
		auto EnsurePost = [&](FABTSM73BeamAAssembly& PlanAssembly,
			const FVector2D& Position,
			const double MinimumZ,
			const double MaximumZ)
		{
			TArray<FPostStation> CurrentStations;
			BuildPostStations(Scratch, Tolerance, Section, CurrentStations);
			const FPostStation* ExistingStation =
				CurrentStations.FindByPredicate(
					[&Position, Tolerance](const FPostStation& Station)
					{
						return Station.Position.Equals(Position, Tolerance);
					});
			const bool bCovered = ExistingStation != nullptr
				&& ExistingStation->ContinuousIntervals.ContainsByPredicate(
					[MinimumZ, MaximumZ, Tolerance](
						const FVerticalInterval& Interval)
					{
						return Interval.MinimumZ <= MinimumZ + Tolerance
							&& Interval.MaximumZ >= MaximumZ - Tolerance;
					});
			for (FABTSM73BeamAMember& Member : Scratch.Members)
			{
				if (Member.Axis != EABTSM73BeamAFrameAxis::Z)
				{
					continue;
				}
				const FBox Bounds = MemberBounds(Member, Scratch, Section);
				if (bCovered
					&& FMath::Abs(Bounds.GetCenter().X - Position.X) <= Tolerance
					&& FMath::Abs(Bounds.GetCenter().Y - Position.Y) <= Tolerance
					&& Bounds.Max.Z >= MinimumZ - Tolerance
					&& Bounds.Min.Z <= MaximumZ + Tolerance)
				{
					Member.Role = EABTSM73BeamAMemberRole::CorePost;
					PlanAssembly.MemberIds.AddUnique(Member.MemberId);
					PlanAssembly.JointIds.AddUnique(Member.JointA);
					PlanAssembly.JointIds.AddUnique(Member.JointB);
				}
			}
			if (!bCovered)
			{
				AddSyntheticCorePost(Scratch, PlanAssembly, Position,
					MinimumZ, MaximumZ);
				++InsertedSyntheticPostCount;
				bInsertedGeometry = true;
			}
		};
		auto EnsureCourse = [&](FABTSM73BeamAAssembly& PlanAssembly,
			const FVector& Start,
			const FVector& End,
			const EABTSM73BeamAFrameAxis Axis)
		{
			bool bInserted = false;
			AddOrReuseCoreMember(Scratch, PlanAssembly, Start, End,
				Axis, Section, Tolerance, bInserted);
			InsertedCourseCount += bInserted ? 1 : 0;
			bInsertedGeometry |= bInserted;
		};

		for (const FABTSM73BeamC3CribCoreHostPlan& Plan : HostPlans)
		{
			if (Plan.StationPositions.Num() != 4
				|| Plan.MaximumZ <= Plan.MinimumZ + Tolerance)
			{
				ClosureError = TEXT("BeamC3ExistingPlanGeometryInvalid");
				return false;
			}
			FABTSM73BeamAAssembly& PlanAssembly =
				AddPlanAssembly(Plan.BayId);
			double MinimumX = TNumericLimits<double>::Max();
			double MaximumX = -TNumericLimits<double>::Max();
			double MinimumY = TNumericLimits<double>::Max();
			double MaximumY = -TNumericLimits<double>::Max();
			for (const FVector2D& Position : Plan.StationPositions)
			{
				MinimumX = FMath::Min(MinimumX, Position.X);
				MaximumX = FMath::Max(MaximumX, Position.X);
				MinimumY = FMath::Min(MinimumY, Position.Y);
				MaximumY = FMath::Max(MaximumY, Position.Y);
				EnsurePost(PlanAssembly, Position,
					Plan.MinimumZ, Plan.MaximumZ);
			}
			for (const double BeltMidZ : Plan.BeltMidZs)
			{
				const double XCenterZ = BeltMidZ - Section * 0.5;
				const double YCenterZ = BeltMidZ + Section * 0.5;
				const double CourseMinimumX = MinimumX - Section * 0.5;
				const double CourseMaximumX = MaximumX + Section * 0.5;
				const double CourseMinimumY = MinimumY - Section * 0.5;
				const double CourseMaximumY = MaximumY + Section * 0.5;
				EnsureCourse(PlanAssembly,
					FVector(CourseMinimumX, MinimumY, XCenterZ),
					FVector(CourseMaximumX, MinimumY, XCenterZ),
					EABTSM73BeamAFrameAxis::X);
				EnsureCourse(PlanAssembly,
					FVector(CourseMinimumX, MaximumY, XCenterZ),
					FVector(CourseMaximumX, MaximumY, XCenterZ),
					EABTSM73BeamAFrameAxis::X);
				EnsureCourse(PlanAssembly,
					FVector(MinimumX, CourseMinimumY, YCenterZ),
					FVector(MinimumX, CourseMaximumY, YCenterZ),
					EABTSM73BeamAFrameAxis::Y);
				EnsureCourse(PlanAssembly,
					FVector(MaximumX, CourseMinimumY, YCenterZ),
					FVector(MaximumX, CourseMaximumY, YCenterZ),
					EABTSM73BeamAFrameAxis::Y);
			}
		}
		for (const FABTSM73BeamC3TargetedTiePlan& Plan : TiePlans)
		{
			if (!Plan.bAnchorIsRootedCourse
				&& !HostPlans.IsValidIndex(Plan.AnchorHostPlanIndex))
			{
				ClosureError = TEXT("BeamC3ExistingTiePlanInvalid");
				return false;
			}
			const double TieMinimumZ = Plan.MaximumZ > Plan.MinimumZ + Tolerance
				? Plan.MinimumZ
				: HostPlans[Plan.AnchorHostPlanIndex].MinimumZ;
			const double TieMaximumZ = Plan.MaximumZ > Plan.MinimumZ + Tolerance
				? Plan.MaximumZ
				: HostPlans[Plan.AnchorHostPlanIndex].MaximumZ;
			FABTSM73BeamAAssembly& PlanAssembly =
				AddPlanAssembly(Plan.BayId);
			if (!Plan.bAnchorIsRootedCourse)
			{
				EnsurePost(PlanAssembly, Plan.AnchorStation,
					TieMinimumZ, TieMaximumZ);
			}
			EnsurePost(PlanAssembly, Plan.TargetStation,
				TieMinimumZ, TieMaximumZ);
			EnsureCourse(PlanAssembly,
				FVector(Plan.AnchorStation.X, Plan.AnchorStation.Y,
					Plan.CourseCenterZ),
				FVector(Plan.TargetStation.X, Plan.TargetStation.Y,
					Plan.CourseCenterZ),
				Plan.Axis);
		}
		if (bInsertedGeometry
			&& !ABTSM73BeamA::CloseGeneratedAssembly(
				BeamASettings, Scratch, ClosureError))
		{
			ClosureError = FString::Printf(
				TEXT("BeamC3ExistingPlanRestoreClosureFailed:%s"),
				*ClosureError);
			return false;
		}

		// C2 is allowed to reclose the assembly before C3 continues an existing
		// plan. That closure can split or merge a planned Z chain and preserve the
		// geometry while losing the semantic CorePost role on one of its pieces.
		// Retag every real Z piece that occupies a certified host station before
		// auditing contacts. Rebuilding the contact table is intentionally cheaper
		// and less destructive than running another global geometry closure.
		for (const FABTSM73BeamC3CribCoreHostPlan& Plan : HostPlans)
		{
			for (const FVector2D& Station : Plan.StationPositions)
			{
				for (FABTSM73BeamAMember& Member : Scratch.Members)
				{
					if (Member.Axis != EABTSM73BeamAFrameAxis::Z)
					{
						continue;
					}
					const FBox Bounds = MemberBounds(Member, Scratch, Section);
					if (FMath::Abs(Bounds.GetCenter().X - Station.X) <= Tolerance
						&& FMath::Abs(Bounds.GetCenter().Y - Station.Y) <= Tolerance
						&& Bounds.Max.Z >= Plan.MinimumZ - Tolerance
						&& Bounds.Min.Z <= Plan.MaximumZ + Tolerance)
					{
						Member.Role = EABTSM73BeamAMemberRole::CorePost;
					}
				}
			}
		}
		if (!ABTSM73BeamA::RebuildBearingContacts(
			BeamASettings, Scratch, ClosureError))
		{
			ClosureError = FString::Printf(
				TEXT("BeamC3ExistingPlanContactRebuildFailed:%s"),
				*ClosureError);
			return false;
		}

		// Beam-A is allowed to split a planned core post around the staggered X/Y
		// courses. C2 reclose may retain the station's broad continuous interval
		// while consuming one of the two short face segments that provides the real
		// bearing at a belt. Repair only those missing faces; never duplicate an
		// already segmented full-height post.
		bool bInsertedContactFace = false;
		for (const FABTSM73BeamC3CribCoreHostPlan& Plan : HostPlans)
		{
			FABTSM73BeamAAssembly* ContactAssembly = nullptr;
			auto GetContactAssembly = [&]() -> FABTSM73BeamAAssembly&
			{
				if (ContactAssembly == nullptr)
				{
					ContactAssembly = &AddPlanAssembly(Plan.BayId);
				}
				return *ContactAssembly;
			};
			const double XMinimum = FMath::Min(
				Plan.StationPositions[0].X, Plan.StationPositions[1].X);
			const double XMaximum = FMath::Max(
				Plan.StationPositions[0].X, Plan.StationPositions[1].X);
			const double YMinimum = FMath::Min(
				Plan.StationPositions[0].Y, Plan.StationPositions[2].Y);
			const double YMaximum = FMath::Max(
				Plan.StationPositions[0].Y, Plan.StationPositions[2].Y);
			for (const double BeltMidZ : Plan.BeltMidZs)
			{
				const double XZ = BeltMidZ - Section * 0.5;
				const double YZ = BeltMidZ + Section * 0.5;
				const int32 X0 = FindCoreCourse(Scratch,
					EABTSM73BeamAFrameAxis::X, Plan.StationPositions[0].Y,
					XZ, XMinimum, XMaximum, Section, Tolerance);
				const int32 X1 = FindCoreCourse(Scratch,
					EABTSM73BeamAFrameAxis::X, Plan.StationPositions[2].Y,
					XZ, XMinimum, XMaximum, Section, Tolerance);
				const int32 Y0 = FindCoreCourse(Scratch,
					EABTSM73BeamAFrameAxis::Y, Plan.StationPositions[0].X,
					YZ, YMinimum, YMaximum, Section, Tolerance);
				const int32 Y1 = FindCoreCourse(Scratch,
					EABTSM73BeamAFrameAxis::Y, Plan.StationPositions[1].X,
					YZ, YMinimum, YMaximum, Section, Tolerance);
				if (X0 == INDEX_NONE || X1 == INDEX_NONE
					|| Y0 == INDEX_NONE || Y1 == INDEX_NONE)
				{
					continue;
				}
				const int32 XCourses[4] = {X0, X0, X1, X1};
				const int32 YCourses[4] = {Y0, Y1, Y0, Y1};
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					const FVector2D& Station = Plan.StationPositions[Corner];
					if (!HasCorePostContact(Scratch, XCourses[Corner], Station,
						true, BeltMidZ - Section, Section, Tolerance))
					{
						const double MaximumZ = BeltMidZ - Section;
						const double MinimumZ = MaximumZ - Section;
						if (MaximumZ > MinimumZ + Tolerance)
						{
							AddSyntheticCorePost(Scratch, GetContactAssembly(),
								Station, MinimumZ, MaximumZ);
							++InsertedSyntheticPostCount;
							bInsertedContactFace = true;
						}
					}
					if (!HasCorePostContact(Scratch, YCourses[Corner], Station,
						false, BeltMidZ + Section, Section, Tolerance))
					{
						const double MinimumZ = BeltMidZ + Section;
						const double MaximumZ = MinimumZ + Section;
						if (MaximumZ > MinimumZ + Tolerance)
						{
							AddSyntheticCorePost(Scratch, GetContactAssembly(),
								Station, MinimumZ, MaximumZ);
							++InsertedSyntheticPostCount;
							bInsertedContactFace = true;
						}
					}
				}
			}
		}
		if (bInsertedContactFace
			&& !ABTSM73BeamA::RebuildBearingContacts(
				BeamASettings, Scratch, ClosureError))
		{
			ClosureError = FString::Printf(
				TEXT("BeamC3ExistingPlanFaceContactRebuildFailed:%s"),
				*ClosureError);
			return false;
		}
		return true;
	};

	if (bContinueExistingPlan)
	{
		if (!RestoreExistingPlanGeometry())
		{
			return Reject(ClosureError);
		}
		if (!BuildCertifiedC3BraceEvidence(
			Scratch, HostPlans, TiePlans, Section, Tolerance,
			CertifiedEvidence, ClosureError))
		{
			return Reject(FString::Printf(
				TEXT("BeamC3ExistingPlanInvalid:%s"), *ClosureError));
		}
	}
	else
	{
		// AppendHost is only a geometric proposal. Beam-A closure may split or
		// prune a reused Z lane, so accepting the first proposal before the real
		// contact/topology gates can strand one core corner. Treat every initial
		// host as a transaction and continue with the next deterministic rectangle
		// when closure, face restoration, or certification rejects it.
		FString LastInitialHostError;
		while (true)
		{
			FABTSM73BeamAGenerationResult BeforeInitialHost = Scratch;
			const int32 BeforeInsertedCourses = InsertedCourseCount;
			const int32 BeforeInsertedPosts = InsertedSyntheticPostCount;
			const int32 BeforeReusedPosts = ReusedHostPostMemberCount;
			FABTSM73BeamC3CribCoreHostPlan FirstPlan;
			FCoreHost CandidateHost;
			if (!TryAppendHost(Stations, BeforeViolation,
				CandidateHost, FirstPlan, ClosureError))
			{
				return Reject(LastInitialHostError.IsEmpty()
					? ClosureError
					: FString::Printf(
						TEXT("BeamC3NoCertifiedInitialHost:Last=%s:Search=%s"),
						*LastInitialHostError, *ClosureError));
			}
			HostPlans.Add(MoveTemp(FirstPlan));
			const bool bClosed = ABTSM73BeamA::CloseGeneratedAssembly(
				BeamASettings, Scratch, ClosureError);
			const bool bContactFacesReady = bClosed
				&& RestoreHostPlanPostBearingFaces(
					Scratch, HostPlans, BeamASettings, Section, Tolerance,
					InsertedSyntheticPostCount, ClosureError);
			const bool bCertified = bContactFacesReady
				&& BuildCertifiedC3BraceEvidence(
					Scratch, HostPlans, TiePlans, Section, Tolerance,
					CertifiedEvidence, ClosureError);
			if (bCertified)
			{
				Host = CandidateHost;
				AcceptedHostSignatures.Add(Host.Signature);
				break;
			}

			LastInitialHostError = !bClosed
				? FString::Printf(TEXT("BeamC3CoreClosureFailed:%s"),
					*ClosureError)
				: !bContactFacesReady
					? FString::Printf(
						TEXT("BeamC3CoreContactFaceRestoreFailed:%s"),
						*ClosureError)
					: ClosureError;
			UE_LOG(LogABTSRuntime, Display,
				TEXT("[ABTS][M7.3-Beam-C3][InitialHostTransactionRejected] Error=%s Signature=%s"),
				*LastInitialHostError, *CandidateHost.Signature);
			Scratch = MoveTemp(BeforeInitialHost);
			InsertedCourseCount = BeforeInsertedCourses;
			InsertedSyntheticPostCount = BeforeInsertedPosts;
			ReusedHostPostMemberCount = BeforeReusedPosts;
			HostPlans.Pop(EAllowShrinking::No);
			CertifiedEvidence = FCertifiedC3BraceEvidence();
		}
	}
	FUnbracedZViolation CurrentViolation = MaximumUnbracedZViolation(
		Scratch, CertifiedEvidence.CertifiedCourseMemberIds,
		Section, Tolerance,
		&CertifiedEvidence.CertifiedSourceIdsByCourse,
		&CertifiedEvidence.BiaxialCourseMemberIds);
	if (bContinueExistingPlan)
	{
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M7.3-Beam-C3][RepairStart] MaxAllZ=%.2f Station=%.1f,%.1f Axis=%d Interval=%.1f..%.1f Members=%d Hosts=%d Ties=%d"),
			CurrentViolation.SpanCM, CurrentViolation.Station.X,
			CurrentViolation.Station.Y,
			static_cast<int32>(CurrentViolation.MissingBraceAxis),
			CurrentViolation.MinimumZ, CurrentViolation.MaximumZ,
			Scratch.Members.Num(), HostPlans.Num(), TiePlans.Num());
	}
	int32 TargetedTieCount = TiePlans.Num();
	const int32 MaximumTargetedTieCount = TargetedTieCount + FMath::Max(
		4, Settings.MaximumNetMemberIncrease / 2);
	while (CurrentViolation.SpanCM
			> Settings.MaximumUnbracedCorePostSpanCM + Tolerance
		&& (HostPlans.Num() < Settings.MaximumHostCount
			|| TargetedTieCount < MaximumTargetedTieCount))
	{
		TArray<FPostStation> AdditionalStations;
		BuildPostStations(Scratch, Tolerance, Section, AdditionalStations);
		FString TargetedTieError;
		// A real horizontal tie into an already certified host is the cheapest
		// physical brace for any aligned peripheral post, not only a post-C2 repair
		// or a post that is already one corner of the host.  Try that transaction
		// first at every tier; if no same-source endpoint/height exists, fall back to
		// another closed four-post host.  This is especially important to keep E1/E2
		// within their Brick budget and to avoid carpeting large E5/E6 silhouettes
		// with decorative micro-cores.
		if (TargetedTieCount < MaximumTargetedTieCount
			&& TryAddTargetedTie(AdditionalStations,
				CurrentViolation, TargetedTieError))
		{
			++TargetedTieCount;
			CurrentViolation = MaximumUnbracedZViolation(
				Scratch, CertifiedEvidence.CertifiedCourseMemberIds,
				Section, Tolerance,
				&CertifiedEvidence.CertifiedSourceIdsByCourse,
				&CertifiedEvidence.BiaxialCourseMemberIds);
			if (bContinueExistingPlan)
			{
				UE_LOG(LogABTSRuntime, Display,
					TEXT("[ABTS][M7.3-Beam-C3][TargetedTieAccepted] MaxAllZ=%.2f Station=%.1f,%.1f Axis=%d Interval=%.1f..%.1f Members=%d Ties=%d"),
					CurrentViolation.SpanCM, CurrentViolation.Station.X,
					CurrentViolation.Station.Y,
					static_cast<int32>(CurrentViolation.MissingBraceAxis),
					CurrentViolation.MinimumZ, CurrentViolation.MaximumZ,
					Scratch.Members.Num(), TiePlans.Num());
			}
			continue;
		}
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M7.3-Beam-C3][TargetedTieRejected] Mode=%s Error=%s Station=%.1f,%.1f Axis=%d Interval=%.1f..%.1f"),
			bContinueExistingPlan ? TEXT("Repair") : TEXT("Initial"),
			*TargetedTieError, CurrentViolation.Station.X,
			CurrentViolation.Station.Y,
			static_cast<int32>(CurrentViolation.MissingBraceAxis),
			CurrentViolation.MinimumZ, CurrentViolation.MaximumZ);
		if (HostPlans.Num() >= Settings.MaximumHostCount)
		{
			UE_LOG(LogABTSRuntime, Display,
				TEXT("[ABTS][M7.3-Beam-C3][TargetedTieFallbackFailed] Error=%s Station=%.1f,%.1f Axis=%d Interval=%.1f..%.1f Hosts=%d"),
				*TargetedTieError, CurrentViolation.Station.X,
				CurrentViolation.Station.Y,
				static_cast<int32>(CurrentViolation.MissingBraceAxis),
				CurrentViolation.MinimumZ, CurrentViolation.MaximumZ,
				HostPlans.Num());
			break;
		}
		FCoreHost AdditionalHost;
		FABTSM73BeamC3CribCoreHostPlan AdditionalPlan;
		FABTSM73BeamAGenerationResult BeforeHostAttempt = Scratch;
		const int32 BeforeInsertedCourses = InsertedCourseCount;
		const int32 BeforeInsertedPosts = InsertedSyntheticPostCount;
		const int32 BeforeReusedPosts = ReusedHostPostMemberCount;
		bool bFoundTargetBefore = false;
		EABTSM73BeamAFrameAxis HostProgressAxis =
			CurrentViolation.MissingBraceAxis;
		float TargetSpanBefore = 0.0f;
		if (!bContinueExistingPlan)
		{
			TargetSpanBefore = MaximumUnbracedSpanAtStationAxis(
				Scratch, CurrentViolation.Station, HostProgressAxis,
				CertifiedEvidence.CertifiedCourseMemberIds,
				CertifiedEvidence.BiaxialCourseMemberIds,
				CertifiedEvidence.CertifiedSourceIdsByCourse,
				Section, Tolerance, bFoundTargetBefore);
		}
		else if (HostProgressAxis == EABTSM73BeamAFrameAxis::X
			|| HostProgressAxis == EABTSM73BeamAFrameAxis::Y)
		{
			TargetSpanBefore = MaximumUnbracedSpanWithinIntervalAtStationAxis(
				Scratch, CurrentViolation.Station, HostProgressAxis,
				CurrentViolation.MinimumZ, CurrentViolation.MaximumZ,
				CertifiedEvidence.CertifiedCourseMemberIds,
				CertifiedEvidence.BiaxialCourseMemberIds,
				CertifiedEvidence.CertifiedSourceIdsByCourse,
				Section, Tolerance, bFoundTargetBefore);
		}
		else
		{
			bool bFoundX = false;
			bool bFoundY = false;
			const float XSpan = MaximumUnbracedSpanWithinIntervalAtStationAxis(
				Scratch, CurrentViolation.Station, EABTSM73BeamAFrameAxis::X,
				CurrentViolation.MinimumZ, CurrentViolation.MaximumZ,
				CertifiedEvidence.CertifiedCourseMemberIds,
				CertifiedEvidence.BiaxialCourseMemberIds,
				CertifiedEvidence.CertifiedSourceIdsByCourse,
				Section, Tolerance, bFoundX);
			const float YSpan = MaximumUnbracedSpanWithinIntervalAtStationAxis(
				Scratch, CurrentViolation.Station, EABTSM73BeamAFrameAxis::Y,
				CurrentViolation.MinimumZ, CurrentViolation.MaximumZ,
				CertifiedEvidence.CertifiedCourseMemberIds,
				CertifiedEvidence.BiaxialCourseMemberIds,
				CertifiedEvidence.CertifiedSourceIdsByCourse,
				Section, Tolerance, bFoundY);
			HostProgressAxis = XSpan >= YSpan
				? EABTSM73BeamAFrameAxis::X
				: EABTSM73BeamAFrameAxis::Y;
			TargetSpanBefore = XSpan >= YSpan ? XSpan : YSpan;
			bFoundTargetBefore = bFoundX || bFoundY;
		}
		// No endpoint in the exact violating interval proves that an existing
		// host cannot anchor this post-C2 column. Skip the unrelated global host
		// candidates and build the compact four-corner fallback around the target
		// station directly.
		const bool bRequiresSyntheticLocalHost = bContinueExistingPlan
			&& TargetedTieError.StartsWith(TEXT("BeamC3TargetedTieNoEndpoint"));
		const bool bHostAppended = bRequiresSyntheticLocalHost
			? TryMaterializeSyntheticHost(AdditionalStations, CurrentViolation,
				AdditionalHost, AdditionalPlan, ClosureError)
			: TryAppendHost(AdditionalStations, CurrentViolation,
				AdditionalHost, AdditionalPlan, ClosureError);
		if (!bHostAppended)
		{
			if (bContinueExistingPlan)
			{
				UE_LOG(LogABTSRuntime, Display,
					TEXT("[ABTS][M7.3-Beam-C3][RepairHostRejected] Error=%s Station=%.1f,%.1f Axis=%d Interval=%.1f..%.1f"),
					*ClosureError, CurrentViolation.Station.X,
					CurrentViolation.Station.Y,
					static_cast<int32>(CurrentViolation.MissingBraceAxis),
					CurrentViolation.MinimumZ, CurrentViolation.MaximumZ);
			}
			break;
		}
		HostPlans.Add(MoveTemp(AdditionalPlan));
		FCertifiedC3BraceEvidence CandidateEvidence;
		FUnbracedZViolation CandidateViolation;
		const bool bClosed = ABTSM73BeamA::CloseGeneratedAssembly(
			BeamASettings, Scratch, ClosureError);
		bool bContactFacesReady = bClosed;
		if (bContactFacesReady && !HostPlans.IsEmpty())
		{
			// Closure can split or merge the physical post pieces that belonged to
			// an already certified host. Restore their CorePost identity and only
			// materialize a missing one-section bearing face when that volume is
			// genuinely clear of horizontal Brick geometry.
			bContactFacesReady = RestoreHostPlanPostBearingFaces(
				Scratch, HostPlans, BeamASettings, Section, Tolerance,
				InsertedSyntheticPostCount, ClosureError);
		}
		const bool bCertified = bContactFacesReady
			&& BuildCertifiedC3BraceEvidence(
			Scratch, HostPlans, TiePlans, Section, Tolerance,
			CandidateEvidence, ClosureError);
		if (bCertified)
		{
			CandidateViolation = MaximumUnbracedZViolation(
				Scratch, CandidateEvidence.CertifiedCourseMemberIds,
				Section, Tolerance,
				&CandidateEvidence.CertifiedSourceIdsByCourse,
				&CandidateEvidence.BiaxialCourseMemberIds);
		}
		bool bFoundTargetAfter = false;
		const float TargetSpanAfter = !bCertified
			? TNumericLimits<float>::Max()
			: bContinueExistingPlan
				? MaximumUnbracedSpanWithinIntervalAtStationAxis(
					Scratch, CurrentViolation.Station,
					HostProgressAxis,
					CurrentViolation.MinimumZ, CurrentViolation.MaximumZ,
					CandidateEvidence.CertifiedCourseMemberIds,
					CandidateEvidence.BiaxialCourseMemberIds,
					CandidateEvidence.CertifiedSourceIdsByCourse,
					Section, Tolerance, bFoundTargetAfter)
				: MaximumUnbracedSpanAtStationAxis(
					Scratch, CurrentViolation.Station, HostProgressAxis,
					CandidateEvidence.CertifiedCourseMemberIds,
					CandidateEvidence.BiaxialCourseMemberIds,
					CandidateEvidence.CertifiedSourceIdsByCourse,
					Section, Tolerance, bFoundTargetAfter);
		if (!bCertified
			|| !bFoundTargetBefore || !bFoundTargetAfter
			|| TargetSpanAfter >= TargetSpanBefore - Tolerance)
		{
			if (bContinueExistingPlan)
			{
				UE_LOG(LogABTSRuntime, Display,
					TEXT("[ABTS][M7.3-Beam-C3][HostTransactionRejected] Closed=%d Certified=%d Error=%s Found=%d/%d Span=%.2f->%.2f HostZ=%.1f..%.1f Belt0=%.1f Signature=%s"),
					bClosed ? 1 : 0, bCertified ? 1 : 0, *ClosureError,
					bFoundTargetBefore ? 1 : 0, bFoundTargetAfter ? 1 : 0,
					TargetSpanBefore, bCertified ? TargetSpanAfter : -1.0f,
					AdditionalHost.MinimumZ, AdditionalHost.MaximumZ,
					HostPlans.Last().BeltMidZs.IsEmpty()
						? -1.0 : HostPlans.Last().BeltMidZs[0],
					*AdditionalHost.Signature);
			}
			// An additional crib is a transaction, not decoration. A duplicate or
			// unrelated host that does not reduce the exact station/axis it targeted
			// is rolled back. The global maximum may legitimately move to another
			// equally tall station; accepting local progress lets subsequent hosts
			// cover that station instead of trapping a high-tier candidate at one
			// otherwise valid core.
			Scratch = MoveTemp(BeforeHostAttempt);
			InsertedCourseCount = BeforeInsertedCourses;
			InsertedSyntheticPostCount = BeforeInsertedPosts;
			ReusedHostPostMemberCount = BeforeReusedPosts;
			HostPlans.SetNum(HostPlans.Num() - 1);
			continue;
		}
		CertifiedEvidence = MoveTemp(CandidateEvidence);
		CurrentViolation = CandidateViolation;
		AcceptedHostSignatures.Add(AdditionalHost.Signature);
	}
	if (bContinueExistingPlan)
	{
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M7.3-Beam-C3][RepairLoopEnd] MaxAllZ=%.2f Station=%.1f,%.1f Axis=%d Interval=%.1f..%.1f Members=%d Hosts=%d Ties=%d"),
			CurrentViolation.SpanCM, CurrentViolation.Station.X,
			CurrentViolation.Station.Y,
			static_cast<int32>(CurrentViolation.MissingBraceAxis),
			CurrentViolation.MinimumZ, CurrentViolation.MaximumZ,
			Scratch.Members.Num(), HostPlans.Num(), TiePlans.Num());
	}

	auto IsCoreStation = [&HostPlans, Tolerance](
		const FVector& Position)
	{
		return HostPlans.ContainsByPredicate(
			[&Position, Tolerance](
				const FABTSM73BeamC3CribCoreHostPlan& Plan)
			{
				return Plan.StationPositions.ContainsByPredicate(
					[&Position, Tolerance](const FVector2D& Station)
					{
						return FVector2D::Distance(
							FVector2D(Position.X, Position.Y), Station)
							<= Tolerance;
					});
			});
	};
	auto RetagCorePostLanes = [&Scratch, &IsCoreStation]()
	{
		for (FABTSM73BeamAMember& Member : Scratch.Members)
		{
			if (Member.Axis == EABTSM73BeamAFrameAxis::Z
				&& IsCoreStation(MemberCenter(Member, Scratch)))
			{
				Member.Role = EABTSM73BeamAMemberRole::CorePost;
			}
		}
	};
	RetagCorePostLanes();
	auto BuildProtectedCoreSupportCone = [&Scratch](
		const FCertifiedC3BraceEvidence& Evidence)
	{
		TSet<int32> Protected;
		for (const FABTSM73BeamAMember& Member : Scratch.Members)
		{
			if (Member.Role == EABTSM73BeamAMemberRole::CoreCourse
				|| Member.Role == EABTSM73BeamAMemberRole::CorePost)
			{
				Protected.Add(Member.MemberId);
			}
		}
		// Rooted ordinary floor courses are certified lateral evidence, but they
		// are not all vertical load ancestors. A donor may tentatively include
		// one of them; the mandatory post-delete certification below decides
		// whether the remaining physical diaphragm is still sufficient. Seeding
		// every rooted course here over-protects an entire high-tier tower and
		// leaves no safe way to meet its unchanged Brick budget.
		(void)Evidence;
		// Bearing contacts are directed lower -> upper. Walk every lower
		// ancestor of the certified core/floor network to the ground so budget
		// reallocation cannot remove the ordinary frame that actually carries it.
		bool bAdded = true;
		while (bAdded)
		{
			bAdded = false;
			for (const FABTSM73BeamABearingContact& Contact :
				Scratch.BearingContacts)
			{
				if (Protected.Contains(Contact.UpperMemberId)
					&& Scratch.Members.IsValidIndex(Contact.LowerMemberId)
					&& !Protected.Contains(Contact.LowerMemberId))
				{
					Protected.Add(Contact.LowerMemberId);
					bAdded = true;
				}
			}
		}
		return Protected;
	};
	auto RevalidateCoreAfterBudgetClosure = [&]()
	{
		RetagCorePostLanes();
		if (!RestoreHostPlanPostBearingFaces(
			Scratch, HostPlans, BeamASettings, Section, Tolerance,
			InsertedSyntheticPostCount, ClosureError))
		{
			return false;
		}
		FCertifiedC3BraceEvidence RebuiltEvidence;
		if (!BuildCertifiedC3BraceEvidence(
			Scratch, HostPlans, TiePlans, Section, Tolerance,
			RebuiltEvidence, ClosureError))
		{
			return false;
		}
		const FUnbracedZViolation RebuiltViolation = MaximumUnbracedZViolation(
			Scratch, RebuiltEvidence.CertifiedCourseMemberIds,
			Section, Tolerance,
			&RebuiltEvidence.CertifiedSourceIdsByCourse,
			&RebuiltEvidence.BiaxialCourseMemberIds);
		if (RebuiltViolation.SpanCM
			> Settings.MaximumUnbracedCorePostSpanCM + Tolerance)
		{
			ClosureError = FString::Printf(
				TEXT("BeamC3BudgetDonorSpanExceeded:%.2f>%.2f"),
				RebuiltViolation.SpanCM,
				Settings.MaximumUnbracedCorePostSpanCM);
			return false;
		}
		CertifiedEvidence = MoveTemp(RebuiltEvidence);
		return true;
	};

	// C3 is replacement geometry, so its net-member allowance is also an
	// authoritative capacity rather than a late reject-only statistic. This is
	// particularly important for a post-C2 targeted repair: if one three-member
	// rooted tie is needed but only two cumulative slots remain, replace one
	// ordinary host-frame donor instead of exceeding the low-tier budget by one.
	// The fixed C2 reserve stays advisory; C2 itself owns the absolute final cap
	// and a complete 49-Brick Tier-0 core that needs no repair remains valid.
	const int32 NetMemberCapacity = OriginalMemberCount
		+ Settings.MaximumNetMemberIncrease;
	const int32 CapacityBeforeC2 = FMath::Min(
		Settings.MaximumFinalMemberCount, NetMemberCapacity);
	int32 RemovedDonors = 0;
	if (Scratch.Members.Num() > CapacityBeforeC2
		&& Settings.bAllowRoofLaneBudgetReallocation)
	{
		// A post-C2 rooted tie commonly exceeds the low-tier cumulative budget by
		// only one or two members. Fund that small repair from interior roof lanes
		// first: the selector preserves both silhouette eaves and the single ridge,
		// whereas deleting an entire ordinary frame can create a much larger support
		// deficit and force closure to add more posts than the repair saved.
		const int32 RemovedRoofMembers = ReallocateRoofLanes(
			Scratch, Scratch.Members.Num() - CapacityBeforeC2, Tolerance);
		RemovedDonors += RemovedRoofMembers;
		if (RemovedRoofMembers > 0
			&& !ABTSM73BeamA::CloseGeneratedAssembly(
				BeamASettings, Scratch, ClosureError))
		{
			return Reject(FString::Printf(
				TEXT("BeamC3BudgetClosureFailed:%s"), *ClosureError));
		}
		if (RemovedRoofMembers > 0 && !RevalidateCoreAfterBudgetClosure())
		{
			return Reject(FString::Printf(
				TEXT("BeamC3RoofBudgetCertificationFailed:%s"),
				*ClosureError));
		}
	}
	TSet<int32> RejectedDonorAssemblyIds;
	while (Scratch.Members.Num() > CapacityBeforeC2)
	{
		// If protected roof lanes cannot cover the deficit, reclaim one complete
		// ordinary frame at a time. The core's full lower-ancestor support cone is
		// immutable, and every proposed donor is a close/re-certify transaction.
		const TSet<int32> ProtectedSupportCone =
			BuildProtectedCoreSupportCone(CertifiedEvidence);
		FABTSM73BeamAGenerationResult BeforeDonorAttempt = Scratch;
		const int32 BeforeInsertedPosts = InsertedSyntheticPostCount;
		int32 DonorAssemblyId = INDEX_NONE;
		const int32 RemovedFrameMembers = ReallocateOneSafeHostFrame(
			Scratch, HostPlans, ProtectedSupportCone,
			RejectedDonorAssemblyIds, DonorAssemblyId);
		if (RemovedFrameMembers <= 0)
		{
			break;
		}
		const bool bDonorClosed = ABTSM73BeamA::CloseGeneratedAssembly(
			BeamASettings, Scratch, ClosureError);
		const bool bDonorCertified = bDonorClosed
			&& RevalidateCoreAfterBudgetClosure();
		if (!bDonorCertified)
		{
			UE_LOG(LogABTSRuntime, Display,
				TEXT("[ABTS][M7.3-Beam-C3][BudgetDonorRejected] Assembly=%d Members=%d Error=%s"),
				DonorAssemblyId, RemovedFrameMembers, *ClosureError);
			Scratch = MoveTemp(BeforeDonorAttempt);
			InsertedSyntheticPostCount = BeforeInsertedPosts;
			RejectedDonorAssemblyIds.Add(DonorAssemblyId);
			continue;
		}
		RemovedDonors += RemovedFrameMembers;
		RejectedDonorAssemblyIds.Reset();
	}

	// Closure may rebuild IDs; apply semantic identity only after the final pass.
	RetagCorePostLanes();
	FCertifiedC3BraceEvidence FinalEvidence;
	if (!BuildCertifiedC3BraceEvidence(
		Scratch, HostPlans, TiePlans, Section, Tolerance,
		FinalEvidence, ClosureError))
	{
		return Reject(ClosureError);
	}
	const FUnbracedZViolation AfterViolation = MaximumUnbracedZViolation(
		Scratch, FinalEvidence.CertifiedCourseMemberIds,
		Section, Tolerance,
		&FinalEvidence.CertifiedSourceIdsByCourse,
		&FinalEvidence.BiaxialCourseMemberIds);
	const float MaximumAfter = AfterViolation.SpanCM;
	if (MaximumAfter > Settings.MaximumUnbracedCorePostSpanCM + Tolerance)
	{
		FString HostSignature;
		for (const FABTSM73BeamC3CribCoreHostPlan& Plan : HostPlans)
		{
			HostSignature += FString::Printf(TEXT("[Source=%d Bay=%d Belts=%d]"),
				Plan.SourceVolumeId, Plan.BayId, Plan.BeltMidZs.Num());
		}
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M7.3-Beam-C3][SpanReject]")
			TEXT(" MaxAllZ=%.2f Limit=%.2f Hosts=%d %s")
			TEXT(" Station=%.1f,%.1f Axis=%d Interval=%.1f..%.1f Member=%d"),
			MaximumAfter, Settings.MaximumUnbracedCorePostSpanCM,
			HostPlans.Num(), *HostSignature,
			AfterViolation.Station.X, AfterViolation.Station.Y,
			static_cast<int32>(AfterViolation.MissingBraceAxis),
			AfterViolation.MinimumZ, AfterViolation.MaximumZ,
			AfterViolation.MemberId);
		return Reject(FString::Printf(
			TEXT("BeamC3AllZSpanExceeded:%.2f>%.2f"),
			MaximumAfter, Settings.MaximumUnbracedCorePostSpanCM));
	}
	if (Scratch.Members.Num() > CapacityBeforeC2)
	{
		return Reject(FString::Printf(
			TEXT("BeamC3CoreBudgetInsufficient:%d>%d"),
			Scratch.Members.Num(), CapacityBeforeC2));
	}
	const int32 NetDelta = Scratch.Members.Num() - OriginalMemberCount;
	if (NetDelta > Settings.MaximumNetMemberIncrease)
	{
		return Reject(FString::Printf(
			TEXT("BeamC3NetMemberBudgetExceeded:%d>%d"),
			NetDelta, Settings.MaximumNetMemberIncrease));
	}

	FABTSM73BeamC3CribCoreSummary& Summary = OutResult.Summary;
	Summary.bAccepted = true;
	Summary.bCoreTopologyCertified = true;
	Summary.bStabilityCoreCertified = false;
	Summary.HostCount = HostPlans.Num();
	Summary.BeltCount = 0;
	for (const FABTSM73BeamC3CribCoreHostPlan& Plan : HostPlans)
	{
		Summary.BeltCount += Plan.BeltMidZs.Num();
	}
	Summary.ClosedCoreCourseCount = FinalEvidence.Topology.CourseCount;
	Summary.TargetedTieCourseCount = TiePlans.Num();
	Summary.RootedExistingCourseCount =
		FinalEvidence.RootedExistingCourseCount;
	Summary.CoreCornerBearingCount = FinalEvidence.Topology.CornerBearingCount;
	Summary.ReusedCoreMemberCount = ReusedHostPostMemberCount
		+ (FinalEvidence.Topology.CourseCount - InsertedCourseCount);
	Summary.InsertedCoreMemberCount =
		InsertedCourseCount + InsertedSyntheticPostCount;
	Summary.RemovedBudgetDonorMemberCount = RemovedDonors;
	Summary.NetMemberDelta = NetDelta;
	Summary.MaximumUnbracedCorePostSpanBeforeCM = MaximumBefore;
	Summary.MaximumUnbracedCorePostSpanAfterCM = MaximumAfter;
	Summary.CorePlanHash = CorePlanHash(Scratch, HostPlans, TiePlans);
	Summary.RootedEvidenceHash = RootedEvidenceHash(Scratch, FinalEvidence);
	Summary.RejectReason.Reset();
	OutResult.HostPlans = HostPlans;
	OutResult.TiePlans = TiePlans;
	OutResult.CoreStationPositions = HostPlans[0].StationPositions;
	OutResult.CoreBeltMidZs = HostPlans[0].BeltMidZs;
	OutResult.HostBayId = Host.BayId;
	OutResult.HostSourceVolumeId = Host.SourceVolumeId;
	Scratch.Summary.JointCount = Scratch.Joints.Num();
	Scratch.Summary.MemberCount = Scratch.Members.Num();
	Scratch.Summary.AssemblyCount = Scratch.Assemblies.Num();
	Scratch.Summary.BearingContactCount = Scratch.BearingContacts.Num();
	Scratch.Summary.XMemberCount = 0;
	Scratch.Summary.YMemberCount = 0;
	Scratch.Summary.ZMemberCount = 0;
	Scratch.Summary.DiagonalMemberCount = 0;
	for (const FABTSM73BeamAMember& Member : Scratch.Members)
	{
		switch (Member.Axis)
		{
		case EABTSM73BeamAFrameAxis::X: ++Scratch.Summary.XMemberCount; break;
		case EABTSM73BeamAFrameAxis::Y: ++Scratch.Summary.YMemberCount; break;
		case EABTSM73BeamAFrameAxis::Z: ++Scratch.Summary.ZMemberCount; break;
		default: ++Scratch.Summary.DiagonalMemberCount; break;
		}
	}
	InOutAssembly = MoveTemp(Scratch);
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7.3-Beam-C3][Generated]")
		TEXT(" Before=%d After=%d Net=%d Hosts=%d Belts=%d Rooted=%d Reused=%d Inserted=%d")
		TEXT(" Donors=%d MaxBefore=%.2f MaxAfter=%.2f Hash=%lld RootedHash=%lld"),
		OriginalMemberCount, InOutAssembly.Members.Num(), Summary.NetMemberDelta,
		Summary.HostCount, Summary.BeltCount,
		Summary.RootedExistingCourseCount, Summary.ReusedCoreMemberCount,
		Summary.InsertedCoreMemberCount, Summary.RemovedBudgetDonorMemberCount,
		Summary.MaximumUnbracedCorePostSpanBeforeCM,
		Summary.MaximumUnbracedCorePostSpanAfterCM,
		Summary.CorePlanHash, Summary.RootedEvidenceHash);
	OutError.Reset();
	return true;
}

bool FABTSM73BeamC3CribCoreGenerator::CertifyFinalAssembly(
	const FABTSM73BeamC3CribCoreSettings& Settings,
	const FABTSM73BeamAPreviewSettings& BeamASettings,
	const FABTSM73BeamAGenerationResult& Assembly,
	FABTSM73BeamC3CribCoreResult& InOutResult,
	FString& OutError) const
{
	using namespace ABTSM73BeamC3;
	auto Reject = [&InOutResult, &OutError](const FString& Reason)
	{
		InOutResult.Summary.bStabilityCoreCertified = false;
		InOutResult.Summary.RejectReason = Reason;
		OutError = Reason;
		return false;
	};
	if (!InOutResult.Summary.bAccepted
		|| !InOutResult.Summary.bCoreTopologyCertified)
	{
		return Reject(TEXT("BeamC3FinalAuditMissingCore"));
	}
	const double Section = BeamASettings.BlockCrossSectionCM;
	const double Tolerance = FMath::Max(
		0.01, static_cast<double>(BeamASettings.JointMergeToleranceCM));
	const TArray<FABTSM73BeamC3CribCoreHostPlan>& Plans =
		InOutResult.HostPlans;
	if (Plans.IsEmpty())
	{
		return Reject(TEXT("BeamC3FinalAuditMissingHostPlan"));
	}
	FCertifiedC3BraceEvidence Evidence;
	if (!BuildCertifiedC3BraceEvidence(
		Assembly, Plans, InOutResult.TiePlans, Section, Tolerance,
		Evidence, OutError))
	{
		return Reject(OutError);
	}
	const FUnbracedZViolation FinalViolation = MaximumUnbracedZViolation(
		Assembly, Evidence.CertifiedCourseMemberIds, Section, Tolerance,
		&Evidence.CertifiedSourceIdsByCourse,
		&Evidence.BiaxialCourseMemberIds);
	const float MaximumFinalSpan = FinalViolation.SpanCM;
	if (MaximumFinalSpan > Settings.MaximumUnbracedCorePostSpanCM + Tolerance)
	{
		return Reject(FString::Printf(
			TEXT("BeamC3FinalAllZSpanExceeded:%.2f>%.2f:Station=%.1f,%.1f:Axis=%d:Interval=%.1f..%.1f:Member=%d"),
			MaximumFinalSpan, Settings.MaximumUnbracedCorePostSpanCM,
			FinalViolation.Station.X, FinalViolation.Station.Y,
			static_cast<int32>(FinalViolation.MissingBraceAxis),
			FinalViolation.MinimumZ, FinalViolation.MaximumZ,
			FinalViolation.MemberId));
	}
	if (Assembly.Members.Num() > Settings.MaximumFinalMemberCount)
	{
		return Reject(FString::Printf(
			TEXT("BeamC3FinalMemberBudgetExceeded:%d>%d"),
			Assembly.Members.Num(), Settings.MaximumFinalMemberCount));
	}
	InOutResult.Summary.bStabilityCoreCertified = true;
	InOutResult.Summary.ClosedCoreCourseCount = Evidence.Topology.CourseCount;
	InOutResult.Summary.RootedExistingCourseCount =
		Evidence.RootedExistingCourseCount;
	InOutResult.Summary.CoreCornerBearingCount =
		Evidence.Topology.CornerBearingCount;
	InOutResult.Summary.MaximumUnbracedCorePostSpanAfterCM = MaximumFinalSpan;
	InOutResult.Summary.TargetedTieCourseCount = InOutResult.TiePlans.Num();
	InOutResult.Summary.CorePlanHash = CorePlanHash(
		Assembly, Plans, InOutResult.TiePlans);
	InOutResult.Summary.RootedEvidenceHash =
		RootedEvidenceHash(Assembly, Evidence);
	InOutResult.Summary.RejectReason.Reset();
	OutError.Reset();
	return true;
}
