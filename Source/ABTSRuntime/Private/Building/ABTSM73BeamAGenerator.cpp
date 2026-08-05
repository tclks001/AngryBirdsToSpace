// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BeamAGenerator.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM73DAG5BShapeGrammarV2.h"
#include "Misc/Crc.h"

namespace ABTSM73BeamA
{
	struct FBuildContext
	{
		const FABTSM73BeamAPreviewSettings* Settings = nullptr;
		FABTSM73BeamAGenerationResult* Result = nullptr;
		TMap<FString, int32> JointByKey;
		TMap<uint64, int32> MemberByKey;
	};

	FString JointKey(const FVector& Position, const float Tolerance)
	{
		const double Scale = 1.0 / FMath::Max(0.1, static_cast<double>(Tolerance));
		return FString::Printf(
			TEXT("%lld,%lld,%lld"),
			FMath::RoundToInt64(Position.X * Scale),
			FMath::RoundToInt64(Position.Y * Scale),
			FMath::RoundToInt64(Position.Z * Scale));
	}

	int32 AddJoint(
		FBuildContext& Context,
		const FVector& Position,
		const EABTSM73BeamAJointRole Role)
	{
		const FString Key = JointKey(
			Position,
			Context.Settings->JointMergeToleranceCM);
		if (const int32* Existing = Context.JointByKey.Find(Key))
		{
			return *Existing;
		}
		if (Context.Result->Joints.Num() >= Context.Settings->MaxJointCount)
		{
			return INDEX_NONE;
		}
		FABTSM73BeamAJoint& Joint =
			Context.Result->Joints.AddDefaulted_GetRef();
		Joint.JointId = Context.Result->Joints.Num() - 1;
		Joint.LocalPosition = Position;
		Joint.Role = Role;
		Context.JointByKey.Add(Key, Joint.JointId);
		return Joint.JointId;
	}

	uint64 MemberKey(const int32 JointA, const int32 JointB)
	{
		const uint32 MinJoint = static_cast<uint32>(FMath::Min(JointA, JointB));
		const uint32 MaxJoint = static_cast<uint32>(FMath::Max(JointA, JointB));
		return (static_cast<uint64>(MinJoint) << 32) | MaxJoint;
	}

	FVector AxisVector(const EABTSM73BeamAFrameAxis Axis)
	{
		switch (Axis)
		{
		case EABTSM73BeamAFrameAxis::X:
			return FVector::ForwardVector;
		case EABTSM73BeamAFrameAxis::Y:
			return FVector::RightVector;
		case EABTSM73BeamAFrameAxis::Z:
			return FVector::UpVector;
		case EABTSM73BeamAFrameAxis::Diagonal:
		default:
			return FVector::ZeroVector;
		}
	}

	EABTSM73BeamAFrameAxis OtherHorizontalAxis(
		const EABTSM73BeamAFrameAxis Axis)
	{
		return Axis == EABTSM73BeamAFrameAxis::X
			? EABTSM73BeamAFrameAxis::Y
			: EABTSM73BeamAFrameAxis::X;
	}

	bool IsSupportedSpanRole(const EABTSM73DAG5BV2VolumeRole Role)
	{
		return Role == EABTSM73DAG5BV2VolumeRole::Bridge
			|| Role == EABTSM73DAG5BV2VolumeRole::SupportedSpan;
	}

	int32 AddMember(
		FBuildContext& Context,
		FABTSM73BeamAAssembly& Assembly,
		const FVector& Center,
		const float Length,
		const EABTSM73BeamAFrameAxis Axis,
		const EABTSM73BeamAMemberRole Role)
	{
		const FVector Direction = AxisVector(Axis);
		if (Direction.IsNearlyZero()
			|| !FMath::IsFinite(Length)
			|| Length < Context.Settings->BlockCrossSectionCM)
		{
			return INDEX_NONE;
		}
		const FVector Half = Direction * (Length * 0.5);
		const FVector PositionA = Center - Half;
		const FVector PositionB = Center + Half;
		const EABTSM73BeamAJointRole RoleA =
			Axis == EABTSM73BeamAFrameAxis::Z && PositionA.Z <= 1.0
				? EABTSM73BeamAJointRole::GroundFoot
				: EABTSM73BeamAJointRole::BeamEnd;
		const EABTSM73BeamAJointRole RoleB =
			Axis == EABTSM73BeamAFrameAxis::Z
				? EABTSM73BeamAJointRole::ColumnHead
				: EABTSM73BeamAJointRole::BeamEnd;
		const int32 JointA = AddJoint(Context, PositionA, RoleA);
		const int32 JointB = AddJoint(Context, PositionB, RoleB);
		if (JointA == INDEX_NONE || JointB == INDEX_NONE || JointA == JointB)
		{
			return INDEX_NONE;
		}
		Assembly.JointIds.AddUnique(JointA);
		Assembly.JointIds.AddUnique(JointB);
		const uint64 Key = MemberKey(JointA, JointB);
		if (const int32* Existing = Context.MemberByKey.Find(Key))
		{
			Assembly.MemberIds.AddUnique(*Existing);
			return *Existing;
		}
		if (Context.Result->Members.Num() >= Context.Settings->MaxMemberCount)
		{
			return INDEX_NONE;
		}
		FABTSM73BeamAMember& Member =
			Context.Result->Members.AddDefaulted_GetRef();
		Member.MemberId = Context.Result->Members.Num() - 1;
		Member.JointA = JointA;
		Member.JointB = JointB;
		Member.Axis = Axis;
		Member.Role = Role;
		Member.LengthCM = Length;
		Context.MemberByKey.Add(Key, Member.MemberId);
		Assembly.MemberIds.Add(Member.MemberId);
		return Member.MemberId;
	}

	TArray<double> CourseOffsets(
		const double Minimum,
		const double Maximum,
		const int32 MaximumCount,
		const double CrossSection,
		const double MinimumGap,
		const double TwoBlockMergeGap,
		const bool bSharedMinimumBoundary,
		const bool bSharedMaximumBoundary)
	{
		TArray<double> Result;
		const double Span = Maximum - Minimum;
		if (Span < CrossSection)
		{
			return Result;
		}
		const int32 SharedBoundaryCount =
			static_cast<int32>(bSharedMinimumBoundary)
			+ static_cast<int32>(bSharedMaximumBoundary);
		int32 Count = 1;
		for (int32 Candidate = FMath::Max(1, MaximumCount);
			Candidate >= 2;
			--Candidate)
		{
			const double GapIntervals =
				Candidate - 1 + SharedBoundaryCount * 0.5;
			const double RequiredGap =
				Candidate == 2 ? TwoBlockMergeGap : MinimumGap;
			const double RequiredSpan =
				Candidate * CrossSection + GapIntervals * RequiredGap;
			if (Span + UE_DOUBLE_SMALL_NUMBER >= RequiredSpan)
			{
				Count = Candidate;
				break;
			}
		}
		if (Count == 1)
		{
			Result.Add((Minimum + Maximum) * 0.5);
			return Result;
		}
		const double GapIntervals =
			Count - 1 + SharedBoundaryCount * 0.5;
		const double ActualGap =
			(Span - Count * CrossSection) / GapIntervals;
		const double UsableMin = Minimum + CrossSection * 0.5
			+ (bSharedMinimumBoundary ? ActualGap * 0.5 : 0.0);
		const double UsableMax = Maximum - CrossSection * 0.5
			- (bSharedMaximumBoundary ? ActualGap * 0.5 : 0.0);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Result.Add(FMath::Lerp(
				UsableMin,
				UsableMax,
				static_cast<double>(Index) / (Count - 1)));
		}
		return Result;
	}

	bool BuildAlignedParallelSupportOffsets(
		const double LowerLane,
		const double UpperLane,
		const double OverlapMinimum,
		const double OverlapMaximum,
		const FABTSM73BeamAPreviewSettings& Settings,
		TArray<double>& OutOffsets)
	{
		OutOffsets.Reset();
		if (!FMath::IsNearlyEqual(
			LowerLane,
			UpperLane,
			Settings.JointMergeToleranceCM))
		{
			return false;
		}
		OutOffsets = CourseOffsets(
			OverlapMinimum,
			OverlapMaximum,
			Settings.MaxParallelBlocksPerCourse,
			Settings.BlockCrossSectionCM,
			Settings.MinimumParallelBlockGapCM,
			Settings.TwoBlockMergeGapCM,
			false,
			false);
		return !OutOffsets.IsEmpty();
	}

	void FindSharedPerpendicularBoundaries(
		const FBuildContext& Context,
		const FABTSM73BeamABay& Bay,
		const FBox& Bounds,
		const EABTSM73BeamAFrameAxis Axis,
		const double CenterZ,
		bool& bOutSharedMinimum,
		bool& bOutSharedMaximum)
	{
		bOutSharedMinimum = false;
		bOutSharedMaximum = false;
		const int32 PerpendicularIndex =
			Axis == EABTSM73BeamAFrameAxis::X ? 1 : 0;
		const int32 LongitudinalIndex =
			Axis == EABTSM73BeamAFrameAxis::X ? 0 : 1;
		const double Tolerance = Context.Settings->JointMergeToleranceCM;
		for (const int32 NeighborId : Bay.AdjacentBayIds)
		{
			if (!Context.Result->Bays.IsValidIndex(NeighborId))
			{
				continue;
			}
			const FBox& Neighbor = Context.Result->Bays[NeighborId].LocalBounds;
			const double LongitudinalOverlap = FMath::Max(
				0.0,
				FMath::Min(
					Bay.LocalBounds.Max[LongitudinalIndex],
					Neighbor.Max[LongitudinalIndex])
				- FMath::Max(
					Bay.LocalBounds.Min[LongitudinalIndex],
					Neighbor.Min[LongitudinalIndex]));
			const double CourseHalfHeight =
				Context.Settings->BlockCrossSectionCM * 0.5;
			const double VerticalOverlap = FMath::Max(
				0.0,
				FMath::Min(CenterZ + CourseHalfHeight, Neighbor.Max.Z)
				- FMath::Max(CenterZ - CourseHalfHeight, Neighbor.Min.Z));
			if (LongitudinalOverlap <= Tolerance
				|| VerticalOverlap <= Tolerance)
			{
				continue;
			}
			bOutSharedMinimum |= FMath::IsNearlyEqual(
				Bay.LocalBounds.Min[PerpendicularIndex],
				Neighbor.Max[PerpendicularIndex],
				Tolerance);
			bOutSharedMaximum |= FMath::IsNearlyEqual(
				Bay.LocalBounds.Max[PerpendicularIndex],
				Neighbor.Min[PerpendicularIndex],
				Tolerance);
		}
		bOutSharedMinimum &= FMath::IsNearlyEqual(
			Bounds.Min[PerpendicularIndex],
			Bay.LocalBounds.Min[PerpendicularIndex],
			Tolerance);
		bOutSharedMaximum &= FMath::IsNearlyEqual(
			Bounds.Max[PerpendicularIndex],
			Bay.LocalBounds.Max[PerpendicularIndex],
			Tolerance);
	}

	bool AddHorizontalCourse(
		FBuildContext& Context,
		const FABTSM73BeamABay& Bay,
		FABTSM73BeamAAssembly& Assembly,
		const FBox& Bounds,
		const double CenterZ,
		const EABTSM73BeamAFrameAxis Axis,
		const int32 BlockCount,
		const EABTSM73BeamAMemberRole Role,
		TArray<int32>* OutMemberIds = nullptr)
	{
		const double CrossSection = Context.Settings->BlockCrossSectionCM;
		bool bSharedMinimumBoundary = false;
		bool bSharedMaximumBoundary = false;
		FindSharedPerpendicularBoundaries(
			Context,
			Bay,
			Bounds,
			Axis,
			CenterZ,
			bSharedMinimumBoundary,
			bSharedMaximumBoundary);
		const int32 PerpendicularIndex =
			Axis == EABTSM73BeamAFrameAxis::X ? 1 : 0;
		const TArray<double> Offsets = CourseOffsets(
			Bounds.Min[PerpendicularIndex],
			Bounds.Max[PerpendicularIndex],
			BlockCount,
			CrossSection,
			Context.Settings->MinimumParallelBlockGapCM,
			Context.Settings->TwoBlockMergeGapCM,
			bSharedMinimumBoundary,
			bSharedMaximumBoundary);
		const float Length = static_cast<float>(
			Axis == EABTSM73BeamAFrameAxis::X
				? Bounds.GetSize().X
				: Bounds.GetSize().Y);
		if (Offsets.IsEmpty() || Length < CrossSection)
		{
			return false;
		}
		for (const double Offset : Offsets)
		{
			FVector Center = Bounds.GetCenter();
			Center.Z = CenterZ;
			Center[PerpendicularIndex] = Offset;
			const int32 MemberId = AddMember(
				Context,
				Assembly,
				Center,
				Length,
				Axis,
				Role);
			if (MemberId == INDEX_NONE)
			{
				return false;
			}
			if (OutMemberIds != nullptr)
			{
				OutMemberIds->AddUnique(MemberId);
			}
		}
		return true;
	}

	double MemberCoordinate(
		const FABTSM73BeamAMember& Member,
		const TArray<FABTSM73BeamAJoint>& Joints,
		const int32 CoordinateIndex)
	{
		return (Joints[Member.JointA].LocalPosition[CoordinateIndex]
			+ Joints[Member.JointB].LocalPosition[CoordinateIndex]) * 0.5;
	}

	void MemberLongitudinalInterval(
		const FABTSM73BeamAMember& Member,
		const TArray<FABTSM73BeamAJoint>& Joints,
		double& OutMinimum,
		double& OutMaximum)
	{
		const int32 AxisIndex =
			Member.Axis == EABTSM73BeamAFrameAxis::X ? 0 : 1;
		const double A = Joints[Member.JointA].LocalPosition[AxisIndex];
		const double B = Joints[Member.JointB].LocalPosition[AxisIndex];
		OutMinimum = FMath::Min(A, B);
		OutMaximum = FMath::Max(A, B);
	}

	bool AddVerticalSupport(
		FBuildContext& Context,
		FABTSM73BeamAAssembly& Assembly,
		const FVector2D& XY,
		const double LowerCenterZ,
		const double UpperCenterZ)
	{
		const double CrossSection = Context.Settings->BlockCrossSectionCM;
		const double Bottom = LowerCenterZ + CrossSection * 0.5;
		const double Top = UpperCenterZ - CrossSection * 0.5;
		const float Length = static_cast<float>(Top - Bottom);
		if (Length < CrossSection)
		{
			return false;
		}
		return AddMember(
			Context,
			Assembly,
			FVector(XY.X, XY.Y, (Bottom + Top) * 0.5),
			Length,
			EABTSM73BeamAFrameAxis::Z,
			EABTSM73BeamAMemberRole::Post) != INDEX_NONE;
	}

	bool AddVerticalSupportsBetweenCourses(
		FBuildContext& Context,
		FABTSM73BeamAAssembly& Assembly,
		const TArray<int32>& LowerMemberIds,
		const TArray<int32>& UpperMemberIds)
	{
		const TArray<FABTSM73BeamAJoint>& Joints = Context.Result->Joints;
		const double Tolerance = Context.Settings->JointMergeToleranceCM;
		for (const int32 LowerId : LowerMemberIds)
		{
			if (!Context.Result->Members.IsValidIndex(LowerId))
			{
				continue;
			}
			const FABTSM73BeamAMember Lower =
				Context.Result->Members[LowerId];
			const double LowerZ = MemberCoordinate(Lower, Joints, 2);
			for (const int32 UpperId : UpperMemberIds)
			{
				if (!Context.Result->Members.IsValidIndex(UpperId))
				{
					continue;
				}
				const FABTSM73BeamAMember Upper =
					Context.Result->Members[UpperId];
				const double UpperZ = MemberCoordinate(Upper, Joints, 2);
				if (UpperZ <= LowerZ)
				{
					continue;
				}
				if (Lower.Axis != Upper.Axis)
				{
					const FABTSM73BeamAMember& XMember =
						Lower.Axis == EABTSM73BeamAFrameAxis::X
							? Lower : Upper;
					const FABTSM73BeamAMember& YMember =
						Lower.Axis == EABTSM73BeamAFrameAxis::Y
							? Lower : Upper;
					double XMinimum = 0.0;
					double XMaximum = 0.0;
					double YMinimum = 0.0;
					double YMaximum = 0.0;
					MemberLongitudinalInterval(
						XMember, Joints, XMinimum, XMaximum);
					MemberLongitudinalInterval(
						YMember, Joints, YMinimum, YMaximum);
					const double X = MemberCoordinate(YMember, Joints, 0);
					const double Y = MemberCoordinate(XMember, Joints, 1);
					if (X + Tolerance < XMinimum
						|| X - Tolerance > XMaximum
						|| Y + Tolerance < YMinimum
						|| Y - Tolerance > YMaximum)
					{
						continue;
					}
					if (!AddVerticalSupport(
						Context,
						Assembly,
						FVector2D(X, Y),
						LowerZ,
						UpperZ))
					{
						return false;
					}
					continue;
				}

				const int32 PerpendicularIndex =
					Lower.Axis == EABTSM73BeamAFrameAxis::X ? 1 : 0;
				const double LowerLane =
					MemberCoordinate(Lower, Joints, PerpendicularIndex);
				const double UpperLane =
					MemberCoordinate(Upper, Joints, PerpendicularIndex);
				double LowerMinimum = 0.0;
				double LowerMaximum = 0.0;
				double UpperMinimum = 0.0;
				double UpperMaximum = 0.0;
				MemberLongitudinalInterval(
					Lower, Joints, LowerMinimum, LowerMaximum);
				MemberLongitudinalInterval(
					Upper, Joints, UpperMinimum, UpperMaximum);
				const double OverlapMinimum =
					FMath::Max(LowerMinimum, UpperMinimum);
				const double OverlapMaximum =
					FMath::Min(LowerMaximum, UpperMaximum);
				TArray<double> SupportPositions;
				if (!BuildAlignedParallelSupportOffsets(
					LowerLane,
					UpperLane,
					OverlapMinimum,
					OverlapMaximum,
					*Context.Settings,
					SupportPositions))
				{
					continue;
				}
				for (const double Position : SupportPositions)
				{
					const FVector2D XY =
						Lower.Axis == EABTSM73BeamAFrameAxis::X
							? FVector2D(Position, LowerLane)
							: FVector2D(LowerLane, Position);
					if (!AddVerticalSupport(
						Context, Assembly, XY, LowerZ, UpperZ))
					{
						return false;
					}
				}
			}
		}
		return true;
	}

	bool AddStackedFrame(
		FBuildContext& Context,
		const FABTSM73BeamABay& Bay,
		FABTSM73BeamAAssembly& Assembly)
	{
		const FBox& Bounds = Bay.LocalBounds;
		const double CrossSection = Context.Settings->BlockCrossSectionCM;
		const FVector Size = Bounds.GetSize();
		if (Size.X < CrossSection
			|| Size.Y < CrossSection
			|| Size.Z < CrossSection * 2.0)
		{
			return false;
		}
		const EABTSM73BeamAFrameAxis Primary = Bay.PreferredAxis;
		const EABTSM73BeamAFrameAxis Secondary =
			OtherHorizontalAxis(Primary);
		if (Size.Z < CrossSection * 5.0)
		{
			const int32 CourseCount = FMath::Max(
				2,
				FMath::FloorToInt(Size.Z / CrossSection));
			for (int32 CourseIndex = 0;
				CourseIndex < CourseCount;
				++CourseIndex)
			{
				const EABTSM73BeamAFrameAxis Axis =
					CourseIndex % 2 == 0 ? Primary : Secondary;
				if (!AddHorizontalCourse(
					Context,
					Bay,
					Assembly,
					Bounds,
					Bounds.Min.Z + CrossSection * (CourseIndex + 0.5),
					Axis,
					FMath::Min(
						Context.Settings->MaxFrameParallelBlocksPerCourse,
						Context.Settings->MaxParallelBlocksPerCourse),
					CourseIndex % 2 == 0
						? EABTSM73BeamAMemberRole::PrimaryBeam
						: EABTSM73BeamAMemberRole::SecondaryBeam))
				{
					return false;
				}
			}
			return true;
		}
		TArray<int32> PreviousBearingMembers;
		if (!AddHorizontalCourse(
			Context,
			Bay,
			Assembly,
			Bounds,
			Bounds.Min.Z + CrossSection * 0.5,
			Primary,
			FMath::Min(
				Context.Settings->MaxFrameParallelBlocksPerCourse,
				Context.Settings->MaxParallelBlocksPerCourse),
			EABTSM73BeamAMemberRole::PrimaryBeam)
			|| !AddHorizontalCourse(
				Context,
				Bay,
				Assembly,
				Bounds,
				Bounds.Min.Z + CrossSection * 1.5,
				Secondary,
				FMath::Min(
					Context.Settings->MaxFrameParallelBlocksPerCourse,
					Context.Settings->MaxParallelBlocksPerCourse),
				EABTSM73BeamAMemberRole::SecondaryBeam,
				&PreviousBearingMembers))
		{
			return false;
		}
		const double LowerBearingCenterZ =
			Bounds.Min.Z + CrossSection * 1.5;
		const double UpperBearingCenterZ =
			Bounds.Max.Z - CrossSection * 1.5;
		const int32 VerticalSegmentCount = FMath::Max(1,
			FMath::CeilToInt(
				(UpperBearingCenterZ - LowerBearingCenterZ)
				/ Context.Settings->MaximumVerticalSupportSpanCM));
		for (int32 SegmentIndex = 1;
			SegmentIndex < VerticalSegmentCount;
			++SegmentIndex)
		{
			const double MidZ = FMath::Lerp(
				LowerBearingCenterZ, UpperBearingCenterZ,
				static_cast<double>(SegmentIndex) / VerticalSegmentCount);
			TArray<int32> PrimaryMembers;
			TArray<int32> SecondaryMembers;
			if (!AddHorizontalCourse(
					Context,
					Bay,
					Assembly,
					Bounds,
					MidZ - CrossSection * 0.5,
					Primary,
					FMath::Min(
						Context.Settings->MaxFrameParallelBlocksPerCourse,
						Context.Settings->MaxParallelBlocksPerCourse),
					EABTSM73BeamAMemberRole::PrimaryBeam,
					&PrimaryMembers)
				|| !AddHorizontalCourse(
					Context,
					Bay,
					Assembly,
					Bounds,
					MidZ + CrossSection * 0.5,
					Secondary,
					FMath::Min(
						Context.Settings->MaxFrameParallelBlocksPerCourse,
						Context.Settings->MaxParallelBlocksPerCourse),
					EABTSM73BeamAMemberRole::SecondaryBeam,
					&SecondaryMembers)
				|| !AddVerticalSupportsBetweenCourses(
					Context, Assembly, PreviousBearingMembers, PrimaryMembers))
			{
				return false;
			}
			PreviousBearingMembers = MoveTemp(SecondaryMembers);
		}
		TArray<int32> UpperBearingMembers;
		if (!AddHorizontalCourse(
				Context,
				Bay,
				Assembly,
				Bounds,
				Bounds.Max.Z - CrossSection * 1.5,
				Primary,
				FMath::Min(
					Context.Settings->MaxFrameParallelBlocksPerCourse,
					Context.Settings->MaxParallelBlocksPerCourse),
				EABTSM73BeamAMemberRole::PrimaryBeam,
				&UpperBearingMembers)
			|| !AddHorizontalCourse(
				Context,
				Bay,
				Assembly,
				Bounds,
				Bounds.Max.Z - CrossSection * 0.5,
				Secondary,
				FMath::Min(
					Context.Settings->MaxFrameParallelBlocksPerCourse,
					Context.Settings->MaxParallelBlocksPerCourse),
				EABTSM73BeamAMemberRole::SecondaryBeam))
		{
			return false;
		}
		return AddVerticalSupportsBetweenCourses(
			Context, Assembly, PreviousBearingMembers, UpperBearingMembers);
	}

	FBox SemanticRoofCourseBounds(
		const FBox& Bounds,
		const EABTSM73DAG5BV2Primitive Primitive,
		const double Alpha,
		const double CrossSectionCM)
	{
		FBox CourseBounds = Bounds;
		const FVector Size = Bounds.GetSize();
		const FVector Center = Bounds.GetCenter();
		const double ClampedAlpha = FMath::Clamp(Alpha, 0.0, 1.0);
		const double MinHalfSpan = CrossSectionCM * 0.55;
		if (Primitive == EABTSM73DAG5BV2Primitive::Pyramid
			|| Primitive
				== EABTSM73DAG5BV2Primitive::TriangularPrismX)
		{
			const double HalfX = FMath::Max(
				MinHalfSpan,
				Size.X * 0.5 * (1.0 - ClampedAlpha));
			CourseBounds.Min.X = Center.X - HalfX;
			CourseBounds.Max.X = Center.X + HalfX;
		}
		if (Primitive == EABTSM73DAG5BV2Primitive::Pyramid
			|| Primitive
				== EABTSM73DAG5BV2Primitive::TriangularPrismY)
		{
			const double HalfY = FMath::Max(
				MinHalfSpan,
				Size.Y * 0.5 * (1.0 - ClampedAlpha));
			CourseBounds.Min.Y = Center.Y - HalfY;
			CourseBounds.Max.Y = Center.Y + HalfY;
		}
		return CourseBounds;
	}

	FBox SemanticRoofBearingCourseBounds(
		const FBox& Bounds,
		const EABTSM73DAG5BV2Primitive Primitive,
		const int32 CourseIndex,
		const int32 CourseCount,
		const EABTSM73BeamAFrameAxis Axis,
		const double CrossSectionCM)
	{
		const int32 SafeCourseCount = FMath::Max(1, CourseCount);
		const int32 SafeCourseIndex = FMath::Clamp(
			CourseIndex, 0, SafeCourseCount - 1);
		FBox CourseBounds = SemanticRoofCourseBounds(
			Bounds,
			Primitive,
			static_cast<double>(SafeCourseIndex) / SafeCourseCount,
			CrossSectionCM);
		if (SafeCourseIndex == 0
			|| (Axis != EABTSM73BeamAFrameAxis::X
				&& Axis != EABTSM73BeamAFrameAxis::Y))
		{
			return CourseBounds;
		}

		const FBox LowerCourseBounds = SemanticRoofCourseBounds(
			Bounds,
			Primitive,
			static_cast<double>(SafeCourseIndex - 1) / SafeCourseCount,
			CrossSectionCM);
		const int32 AxisIndex = static_cast<int32>(Axis);
		CourseBounds.Min[AxisIndex] = LowerCourseBounds.Min[AxisIndex];
		CourseBounds.Max[AxisIndex] = LowerCourseBounds.Max[AxisIndex];
		return CourseBounds;
	}

	bool AddLayeredRoof(
		FBuildContext& Context,
		const FABTSM73BeamABay& Bay,
		const EABTSM73DAG5BV2Primitive Primitive,
		FABTSM73BeamAAssembly& Assembly)
	{
		const FBox& Bounds = Bay.LocalBounds;
		const double CrossSection = Context.Settings->BlockCrossSectionCM;
		const FVector Size = Bounds.GetSize();
		const int32 RequiredCourseCount = FMath::Max(
			2,
			FMath::FloorToInt(Size.Z / CrossSection));
		if (RequiredCourseCount > Context.Settings->MaxRoofCourseCount)
		{
			return false;
		}
		const int32 RequestedBlocks =
			Context.Settings->MaxParallelBlocksPerCourse;
		const bool bPrism =
			Primitive == EABTSM73DAG5BV2Primitive::TriangularPrismX
			|| Primitive == EABTSM73DAG5BV2Primitive::TriangularPrismY;
		const EABTSM73BeamAFrameAxis RidgeAxis =
			Primitive == EABTSM73DAG5BV2Primitive::TriangularPrismX
				? EABTSM73BeamAFrameAxis::Y
				: EABTSM73BeamAFrameAxis::X;
		for (int32 CourseIndex = 0;
			CourseIndex < RequiredCourseCount;
			++CourseIndex)
		{
			const int32 DistanceFromTop =
				RequiredCourseCount - 1 - CourseIndex;
			const EABTSM73BeamAFrameAxis Axis = bPrism
				? (DistanceFromTop % 2 == 0
					? RidgeAxis
					: OtherHorizontalAxis(RidgeAxis))
				: (CourseIndex % 2 == 0
					? Bay.PreferredAxis
					: OtherHorizontalAxis(Bay.PreferredAxis));
			const FBox CourseBounds = SemanticRoofBearingCourseBounds(
				Bounds,
				Primitive,
				CourseIndex,
				RequiredCourseCount,
				Axis,
				CrossSection);
			if (!AddHorizontalCourse(
				Context,
				Bay,
				Assembly,
				CourseBounds,
				Bounds.Min.Z + CrossSection * (CourseIndex + 0.5),
				Axis,
				bPrism && CourseIndex == RequiredCourseCount - 1
					? 1
					: RequestedBlocks,
				EABTSM73BeamAMemberRole::RoofCourse))
			{
				return false;
			}
		}
		return true;
	}

	bool BuildSemanticRoofMembers(
		const FABTSM73BeamAPreviewSettings& Settings,
		const FABTSM73BeamAGenerationResult& Topology,
		const FABTSM73BeamABay& Bay,
		const EABTSM73DAG5BV2Primitive Primitive,
		TArray<FABTSM73BeamASemanticRoofMember>& OutMembers)
	{
		OutMembers.Reset();
		FABTSM73BeamAGenerationResult Scratch;
		Scratch.Bays = Topology.Bays;
		FABTSM73BeamAAssembly& Assembly =
			Scratch.Assemblies.AddDefaulted_GetRef();
		Assembly.AssemblyId = 0;
		Assembly.BayId = Bay.BayId;
		Assembly.Type = EABTSM73BeamAAssemblyType::LayeredRoofBay;
		FBuildContext Context;
		Context.Settings = &Settings;
		Context.Result = &Scratch;
		if (!AddLayeredRoof(Context, Bay, Primitive, Assembly))
		{
			return false;
		}
		OutMembers.Reserve(Scratch.Members.Num());
		for (const FABTSM73BeamAMember& Member : Scratch.Members)
		{
			if (!Scratch.Joints.IsValidIndex(Member.JointA)
				|| !Scratch.Joints.IsValidIndex(Member.JointB))
			{
				OutMembers.Reset();
				return false;
			}
			FABTSM73BeamASemanticRoofMember& OutMember =
				OutMembers.AddDefaulted_GetRef();
			OutMember.LocalStart =
				Scratch.Joints[Member.JointA].LocalPosition;
			OutMember.LocalEnd =
				Scratch.Joints[Member.JointB].LocalPosition;
			OutMember.Axis = Member.Axis;
			OutMember.Role = Member.Role;
		}
		return !OutMembers.IsEmpty();
	}

	FBox MemberBounds(
		const FABTSM73BeamAMember& Member,
		const TArray<FABTSM73BeamAJoint>& Joints,
		const double CrossSection)
	{
		const FVector A = Joints[Member.JointA].LocalPosition;
		const FVector B = Joints[Member.JointB].LocalPosition;
		const FVector Center = (A + B) * 0.5;
		FVector Extent(
			CrossSection * 0.5,
			CrossSection * 0.5,
			CrossSection * 0.5);
		switch (Member.Axis)
		{
		case EABTSM73BeamAFrameAxis::X:
			Extent.X = Member.LengthCM * 0.5;
			break;
		case EABTSM73BeamAFrameAxis::Y:
			Extent.Y = Member.LengthCM * 0.5;
			break;
		case EABTSM73BeamAFrameAxis::Z:
			Extent.Z = Member.LengthCM * 0.5;
			break;
		case EABTSM73BeamAFrameAxis::Diagonal:
		default:
			break;
		}
		return FBox(Center - Extent, Center + Extent);
	}

	float OverlapLength(
		const double AMin,
		const double AMax,
		const double BMin,
		const double BMax)
	{
		return static_cast<float>(FMath::Max(
			0.0,
			FMath::Min(AMax, BMax) - FMath::Max(AMin, BMin)));
	}

	EABTSM73BeamABearingType BearingType(
		const FABTSM73BeamAMember& Lower,
		const FABTSM73BeamAMember& Upper)
	{
		if (Upper.Axis == EABTSM73BeamAFrameAxis::Z)
		{
			return EABTSM73BeamABearingType::PostOnBeam;
		}
		if (Lower.Axis == EABTSM73BeamAFrameAxis::Z)
		{
			return EABTSM73BeamABearingType::BeamOnPost;
		}
		if (Lower.Axis != Upper.Axis)
		{
			return EABTSM73BeamABearingType::CrossBearing;
		}
		return EABTSM73BeamABearingType::ParallelBearing;
	}

	bool BuildBearingContacts(
		const FABTSM73BeamAPreviewSettings& Settings,
		FABTSM73BeamAGenerationResult& Result,
		FString& OutError)
	{
		Result.BearingContacts.Reset();
		TArray<FBox> Bounds;
		Bounds.Reserve(Result.Members.Num());
		TMap<int64, TArray<int32>> MembersByTopZ;
		TMap<int64, TArray<int32>> MembersByBottomZ;
		const double Tolerance = Settings.JointMergeToleranceCM;
		for (const FABTSM73BeamAMember& Member : Result.Members)
		{
			const FBox Box = MemberBounds(
				Member,
				Result.Joints,
				Settings.BlockCrossSectionCM);
			Bounds.Add(Box);
			MembersByTopZ.FindOrAdd(
				FMath::RoundToInt64(Box.Max.Z / Tolerance)).Add(Member.MemberId);
			MembersByBottomZ.FindOrAdd(
				FMath::RoundToInt64(Box.Min.Z / Tolerance)).Add(Member.MemberId);
		}

		int32 PairChecks = 0;
		TSet<uint64> ContactPairs;
		for (const TPair<int64, TArray<int32>>& TopEntry : MembersByTopZ)
		{
			const TArray<int32>* UpperMembers =
				MembersByBottomZ.Find(TopEntry.Key);
			if (UpperMembers == nullptr)
			{
				continue;
			}
			for (const int32 LowerId : TopEntry.Value)
			{
				for (const int32 UpperId : *UpperMembers)
				{
					if (++PairChecks > Settings.MaxBearingPairChecks)
					{
						OutError = TEXT("BeamAMaxBearingPairChecksExceeded");
						return false;
					}
					if (LowerId == UpperId)
					{
						continue;
					}
					const uint64 PairKey =
						(static_cast<uint64>(static_cast<uint32>(LowerId)) << 32)
						| static_cast<uint32>(UpperId);
					if (ContactPairs.Contains(PairKey))
					{
						continue;
					}
					const FBox& LowerBounds = Bounds[LowerId];
					const FBox& UpperBounds = Bounds[UpperId];
					const float XOverlap = OverlapLength(
						LowerBounds.Min.X,
						LowerBounds.Max.X,
						UpperBounds.Min.X,
						UpperBounds.Max.X);
					const float YOverlap = OverlapLength(
						LowerBounds.Min.Y,
						LowerBounds.Max.Y,
						UpperBounds.Min.Y,
						UpperBounds.Max.Y);
					if (XOverlap <= Tolerance || YOverlap <= Tolerance)
					{
						continue;
					}
					if (Result.BearingContacts.Num()
						>= Settings.MaxBearingContactCount)
					{
						OutError = TEXT("BeamAMaxBearingContactCountExceeded");
						return false;
					}
					FABTSM73BeamABearingContact& Contact =
						Result.BearingContacts.AddDefaulted_GetRef();
					Contact.ContactId = Result.BearingContacts.Num() - 1;
					Contact.LowerMemberId = LowerId;
					Contact.UpperMemberId = UpperId;
					Contact.Type = BearingType(
						Result.Members[LowerId],
						Result.Members[UpperId]);
					Contact.LocalPosition = FVector(
						(FMath::Max(LowerBounds.Min.X, UpperBounds.Min.X)
							+ FMath::Min(LowerBounds.Max.X, UpperBounds.Max.X)) * 0.5,
						(FMath::Max(LowerBounds.Min.Y, UpperBounds.Min.Y)
							+ FMath::Min(LowerBounds.Max.Y, UpperBounds.Max.Y)) * 0.5,
						LowerBounds.Max.Z);
					Contact.ContactAreaCM2 = XOverlap * YOverlap;
					ContactPairs.Add(PairKey);
				}
			}
		}
		return true;
	}

	struct FMemberBuildSpec
	{
		FVector Center = FVector::ZeroVector;
		float LengthCM = 0.0f;
		EABTSM73BeamAFrameAxis Axis = EABTSM73BeamAFrameAxis::X;
		EABTSM73BeamAMemberRole Role = EABTSM73BeamAMemberRole::PrimaryBeam;
		TArray<int32> AssemblyIds;
	};

	FBox MemberSpecBounds(
		const FMemberBuildSpec& Spec,
		const double CrossSection)
	{
		FVector Extent(
			CrossSection * 0.5,
			CrossSection * 0.5,
			CrossSection * 0.5);
		const int32 AxisIndex = static_cast<int32>(Spec.Axis);
		if (AxisIndex >= 0 && AxisIndex <= 2)
		{
			Extent[AxisIndex] = Spec.LengthCM * 0.5;
		}
		return FBox(Spec.Center - Extent, Spec.Center + Extent);
	}

	void ExtractMemberSpecs(
		const FABTSM73BeamAGenerationResult& Result,
		TArray<FMemberBuildSpec>& OutSpecs)
	{
		TMap<int32, TArray<int32>> AssemblyIdsByMember;
		for (const FABTSM73BeamAAssembly& Assembly : Result.Assemblies)
		{
			for (const int32 MemberId : Assembly.MemberIds)
			{
				AssemblyIdsByMember.FindOrAdd(MemberId).AddUnique(
					Assembly.AssemblyId);
			}
		}
		OutSpecs.Reset(Result.Members.Num());
		for (const FABTSM73BeamAMember& Member : Result.Members)
		{
			if (!Result.Joints.IsValidIndex(Member.JointA)
				|| !Result.Joints.IsValidIndex(Member.JointB))
			{
				continue;
			}
			FMemberBuildSpec& Spec = OutSpecs.AddDefaulted_GetRef();
			Spec.Center = (Result.Joints[Member.JointA].LocalPosition
				+ Result.Joints[Member.JointB].LocalPosition) * 0.5;
			Spec.LengthCM = Member.LengthCM;
			Spec.Axis = Member.Axis;
			Spec.Role = Member.Role;
			if (const TArray<int32>* Owners =
				AssemblyIdsByMember.Find(Member.MemberId))
			{
				Spec.AssemblyIds = *Owners;
			}
		}
	}

	int32 SemanticRolePriority(const EABTSM73BeamAMemberRole Role)
	{
		if (Role == EABTSM73BeamAMemberRole::CoreCourse)
		{
			return 5;
		}
		if (Role == EABTSM73BeamAMemberRole::CorePost)
		{
			return 4;
		}
		if (Role == EABTSM73BeamAMemberRole::BridgeSeat)
		{
			return 3;
		}
		if (Role == EABTSM73BeamAMemberRole::BridgePost)
		{
			return 2;
		}
		return Role == EABTSM73BeamAMemberRole::BridgeRail ? 1 : 0;
	}

	bool SameMemberLane(
		const FMemberBuildSpec& A,
		const FMemberBuildSpec& B,
		const double CrossSection,
		const double Tolerance)
	{
		if (A.Axis != B.Axis || A.Axis == EABTSM73BeamAFrameAxis::Diagonal)
		{
			return false;
		}
		const int32 AxisIndex = static_cast<int32>(A.Axis);
		for (int32 Coordinate = 0; Coordinate < 3; ++Coordinate)
		{
			if (Coordinate != AxisIndex
				&& FMath::Abs(
					A.Center[Coordinate] - B.Center[Coordinate])
					>= CrossSection - Tolerance)
			{
				return false;
			}
		}
		return true;
	}

	void MergeCollinearMemberSpecs(
		TArray<FMemberBuildSpec>& Specs,
		const double CrossSection,
		const double Tolerance,
		int32& OutMergedCount)
	{
		OutMergedCount = 0;
		TArray<bool> Removed;
		Removed.Init(false, Specs.Num());
		for (int32 AIndex = 0; AIndex < Specs.Num(); ++AIndex)
		{
			if (Removed[AIndex])
			{
				continue;
			}
			bool bMerged = true;
			while (bMerged)
			{
				bMerged = false;
				for (int32 BIndex = AIndex + 1;
					BIndex < Specs.Num();
					++BIndex)
				{
					if (Removed[BIndex]
						|| !SameMemberLane(
							Specs[AIndex],
							Specs[BIndex],
							CrossSection,
							Tolerance))
					{
						continue;
					}
					const int32 AxisIndex =
						static_cast<int32>(Specs[AIndex].Axis);
					const double AMin = Specs[AIndex].Center[AxisIndex]
						- Specs[AIndex].LengthCM * 0.5;
					const double AMax = Specs[AIndex].Center[AxisIndex]
						+ Specs[AIndex].LengthCM * 0.5;
					const double BMin = Specs[BIndex].Center[AxisIndex]
						- Specs[BIndex].LengthCM * 0.5;
					const double BMax = Specs[BIndex].Center[AxisIndex]
						+ Specs[BIndex].LengthCM * 0.5;
					if (FMath::Min(AMax, BMax) - FMath::Max(AMin, BMin)
						<= Tolerance)
					{
						continue;
					}
					const double UnionMin = FMath::Min(AMin, BMin);
					const double UnionMax = FMath::Max(AMax, BMax);
					Specs[AIndex].Center[AxisIndex] =
						(UnionMin + UnionMax) * 0.5;
					Specs[AIndex].LengthCM =
						static_cast<float>(UnionMax - UnionMin);
					if (SemanticRolePriority(Specs[BIndex].Role)
						> SemanticRolePriority(Specs[AIndex].Role))
					{
						// A semantic member owns its physical lane as well as its role.
						// Keeping A's transverse center while only copying B's CorePost
						// role can move a certified C3 station by almost one section and
						// leave the new crib course with no real post bearing face.
						for (int32 Coordinate = 0; Coordinate < 3; ++Coordinate)
						{
							if (Coordinate != AxisIndex)
							{
								Specs[AIndex].Center[Coordinate] =
									Specs[BIndex].Center[Coordinate];
							}
						}
						Specs[AIndex].Role =
							Specs[BIndex].Role;
					}
					for (const int32 AssemblyId : Specs[BIndex].AssemblyIds)
					{
						Specs[AIndex].AssemblyIds.AddUnique(AssemblyId);
					}
					Removed[BIndex] = true;
					++OutMergedCount;
					bMerged = true;
				}
			}
		}
		for (int32 Index = Removed.Num() - 1; Index >= 0; --Index)
		{
			if (Removed[Index])
			{
				Specs.RemoveAt(Index);
			}
		}
	}

	bool SeparateIntersectingHorizontalCourses(
		TArray<FMemberBuildSpec>& Specs,
		const FABTSM73BeamAPreviewSettings& Settings,
		int32& OutShiftedCourseCount)
	{
		OutShiftedCourseCount = 0;
		const double Tolerance = Settings.JointMergeToleranceCM;
		constexpr int32 MaxSeparationPasses = 256;
		for (int32 Pass = 0; Pass < MaxSeparationPasses; ++Pass)
		{
			bool bShifted = false;
			for (int32 AIndex = 0; AIndex < Specs.Num() && !bShifted; ++AIndex)
			{
				if (Specs[AIndex].Axis == EABTSM73BeamAFrameAxis::Z
					|| Specs[AIndex].Axis == EABTSM73BeamAFrameAxis::Diagonal)
				{
					continue;
				}
				const FBox ABounds = MemberSpecBounds(
					Specs[AIndex], Settings.BlockCrossSectionCM);
				for (int32 BIndex = AIndex + 1;
					BIndex < Specs.Num();
					++BIndex)
				{
					if (Specs[BIndex].Axis == EABTSM73BeamAFrameAxis::Z
						|| Specs[BIndex].Axis == EABTSM73BeamAFrameAxis::Diagonal
						|| Specs[AIndex].Axis == Specs[BIndex].Axis)
					{
						continue;
					}
					const FBox BBounds = MemberSpecBounds(
						Specs[BIndex], Settings.BlockCrossSectionCM);
					const double XOverlap = FMath::Min(
						ABounds.Max.X, BBounds.Max.X)
						- FMath::Max(ABounds.Min.X, BBounds.Min.X);
					const double YOverlap = FMath::Min(
						ABounds.Max.Y, BBounds.Max.Y)
						- FMath::Max(ABounds.Min.Y, BBounds.Min.Y);
					const double ZOverlap = FMath::Min(
						ABounds.Max.Z, BBounds.Max.Z)
						- FMath::Max(ABounds.Min.Z, BBounds.Min.Z);
					if (XOverlap <= Tolerance
						|| YOverlap <= Tolerance
						|| ZOverlap <= Tolerance)
					{
						continue;
					}
					const double OriginalZ = Specs[BIndex].Center.Z;
					const double ShiftZ = ABounds.Max.Z - BBounds.Min.Z;
					const TArray<int32> CourseOwners = Specs[BIndex].AssemblyIds;
					const EABTSM73BeamAFrameAxis CourseAxis =
						Specs[BIndex].Axis;
					const EABTSM73BeamAMemberRole CourseRole =
						Specs[BIndex].Role;
					for (FMemberBuildSpec& Candidate : Specs)
					{
						const bool bSharesOwner =
							Candidate.AssemblyIds.ContainsByPredicate(
								[&CourseOwners](const int32 AssemblyId)
								{
									return CourseOwners.Contains(AssemblyId);
								});
						if (bSharesOwner
							&& Candidate.Axis == CourseAxis
							&& Candidate.Role == CourseRole
							&& FMath::IsNearlyEqual(
								Candidate.Center.Z, OriginalZ, Tolerance))
						{
							Candidate.Center.Z += ShiftZ;
						}
					}
					++OutShiftedCourseCount;
					bShifted = true;
					break;
				}
			}
			if (!bShifted)
			{
				return true;
			}
		}
		return false;
	}

	void SplitPostsAtHorizontalCourses(
		TArray<FMemberBuildSpec>& Specs,
		const FABTSM73BeamAPreviewSettings& Settings,
		int32& OutSplitCount)
	{
		OutSplitCount = 0;
		const double Tolerance = Settings.JointMergeToleranceCM;
		TArray<FMemberBuildSpec> Result;
		Result.Reserve(Specs.Num());
		for (const FMemberBuildSpec& Post : Specs)
		{
			if (Post.Axis != EABTSM73BeamAFrameAxis::Z)
			{
				Result.Add(Post);
				continue;
			}
			const FBox PostBounds = MemberSpecBounds(
				Post, Settings.BlockCrossSectionCM);
			TArray<FVector2D> Exclusions;
			for (const FMemberBuildSpec& Horizontal : Specs)
			{
				if (Horizontal.Axis == EABTSM73BeamAFrameAxis::Z
					|| Horizontal.Axis == EABTSM73BeamAFrameAxis::Diagonal)
				{
					continue;
				}
				const FBox HorizontalBounds = MemberSpecBounds(
					Horizontal, Settings.BlockCrossSectionCM);
				const double XOverlap = FMath::Min(
					PostBounds.Max.X, HorizontalBounds.Max.X)
					- FMath::Max(PostBounds.Min.X, HorizontalBounds.Min.X);
				const double YOverlap = FMath::Min(
					PostBounds.Max.Y, HorizontalBounds.Max.Y)
					- FMath::Max(PostBounds.Min.Y, HorizontalBounds.Min.Y);
				const double ZOverlap = FMath::Min(
					PostBounds.Max.Z, HorizontalBounds.Max.Z)
					- FMath::Max(PostBounds.Min.Z, HorizontalBounds.Min.Z);
				if (XOverlap > Tolerance
					&& YOverlap > Tolerance
					&& ZOverlap > Tolerance)
				{
					Exclusions.Add(FVector2D(
						FMath::Max(PostBounds.Min.Z, HorizontalBounds.Min.Z),
						FMath::Min(PostBounds.Max.Z, HorizontalBounds.Max.Z)));
				}
			}
			if (Exclusions.IsEmpty())
			{
				Result.Add(Post);
				continue;
			}
			Exclusions.Sort([](const FVector2D& A, const FVector2D& B)
			{
				return A.X < B.X || (FMath::IsNearlyEqual(A.X, B.X) && A.Y < B.Y);
			});
			TArray<FVector2D> MergedExclusions;
			for (const FVector2D& Exclusion : Exclusions)
			{
				if (MergedExclusions.IsEmpty()
					|| Exclusion.X > MergedExclusions.Last().Y + Tolerance)
				{
					MergedExclusions.Add(Exclusion);
				}
				else
				{
					MergedExclusions.Last().Y = FMath::Max(
						MergedExclusions.Last().Y, Exclusion.Y);
				}
			}
			double Cursor = PostBounds.Min.Z;
			int32 SegmentCount = 0;
			for (const FVector2D& Exclusion : MergedExclusions)
			{
				const double SegmentMax = FMath::Min(
					PostBounds.Max.Z, Exclusion.X);
				const double Length = SegmentMax - Cursor;
				if (Length + Tolerance >= Settings.BlockCrossSectionCM)
				{
					FMemberBuildSpec Segment = Post;
					Segment.Center.Z = (Cursor + SegmentMax) * 0.5;
					Segment.LengthCM = static_cast<float>(Length);
					Result.Add(MoveTemp(Segment));
					++SegmentCount;
				}
				Cursor = FMath::Max(Cursor, static_cast<double>(Exclusion.Y));
			}
			const double TailLength = PostBounds.Max.Z - Cursor;
			if (TailLength + Tolerance >= Settings.BlockCrossSectionCM)
			{
				FMemberBuildSpec Segment = Post;
				Segment.Center.Z = (Cursor + PostBounds.Max.Z) * 0.5;
				Segment.LengthCM = static_cast<float>(TailLength);
				Result.Add(MoveTemp(Segment));
				++SegmentCount;
			}
			OutSplitCount += FMath::Max(1, SegmentCount) - 1;
		}
		Specs = MoveTemp(Result);
	}

	bool RebuildMembersFromSpecs(
		FBuildContext& Context,
		const TArray<FMemberBuildSpec>& Specs)
	{
		// Rebuild is transactional from the caller's point of view.  Reject an
		// invalid physical member before clearing the accepted IR; otherwise a
		// failed rebuild leaves a half-written assembly and hides the producer
		// that emitted a sub-block residual.
		for (int32 SpecIndex = 0; SpecIndex < Specs.Num(); ++SpecIndex)
		{
			const FMemberBuildSpec& Spec = Specs[SpecIndex];
			if (!FMath::IsFinite(Spec.LengthCM)
				|| Spec.LengthCM <= 0.0f
				|| Spec.LengthCM
					+ Context.Settings->JointMergeToleranceCM
					< Context.Settings->BlockCrossSectionCM)
			{
				UE_LOG(LogABTSRuntime, Warning,
					TEXT("[ABTS][M7.3-Beam-A][RebuildFailed] Reason=SubBlockMemberPreflight Spec=%d/%d Axis=%d Role=%d Length=%.2f Section=%.2f Tol=%.2f Center=%s"),
					SpecIndex, Specs.Num(), static_cast<int32>(Spec.Axis),
					static_cast<int32>(Spec.Role), Spec.LengthCM,
					Context.Settings->BlockCrossSectionCM,
					Context.Settings->JointMergeToleranceCM,
					*Spec.Center.ToString());
				return false;
			}
		}
		Context.Result->Joints.Reset();
		Context.Result->Members.Reset();
		Context.Result->BearingContacts.Reset();
		Context.JointByKey.Reset();
		Context.MemberByKey.Reset();
		for (FABTSM73BeamAAssembly& Assembly : Context.Result->Assemblies)
		{
			Assembly.JointIds.Reset();
			Assembly.MemberIds.Reset();
		}
		for (int32 SpecIndex = 0; SpecIndex < Specs.Num(); ++SpecIndex)
		{
			const FMemberBuildSpec& Spec = Specs[SpecIndex];
			if (Spec.AssemblyIds.IsEmpty())
			{
				UE_LOG(LogABTSRuntime, Warning,
					TEXT("[ABTS][M7.3-Beam-A][RebuildFailed] Reason=MissingAssembly Spec=%d Axis=%d Length=%.2f Center=%s"),
					SpecIndex, static_cast<int32>(Spec.Axis), Spec.LengthCM,
					*Spec.Center.ToString());
				return false;
			}
			TArray<int32> AssemblyIds = Spec.AssemblyIds;
			AssemblyIds.Sort();
			if (!Context.Result->Assemblies.IsValidIndex(AssemblyIds[0]))
			{
				UE_LOG(LogABTSRuntime, Warning,
					TEXT("[ABTS][M7.3-Beam-A][RebuildFailed] Reason=InvalidPrimaryAssembly Spec=%d Assembly=%d Assemblies=%d"),
					SpecIndex, AssemblyIds[0], Context.Result->Assemblies.Num());
				return false;
			}
			// Split/merge passes deliberately use JointMergeToleranceCM when
			// deciding whether a residual segment can still hold one block.
			// Keep the rebuild contract identical: a segment that is only a
			// tolerance short is materialized as one cross-section block instead
			// of making the entire accepted assembly fail during reconstruction.
			const float RebuiltLength =
				Spec.LengthCM < Context.Settings->BlockCrossSectionCM
					&& Spec.LengthCM
						+ Context.Settings->JointMergeToleranceCM
						>= Context.Settings->BlockCrossSectionCM
					? Context.Settings->BlockCrossSectionCM
					: Spec.LengthCM;
			const int32 MemberId = AddMember(
				Context,
				Context.Result->Assemblies[AssemblyIds[0]],
				Spec.Center,
				RebuiltLength,
				Spec.Axis,
				Spec.Role);
			if (MemberId == INDEX_NONE)
			{
				const FVector Direction = AxisVector(Spec.Axis);
				const FVector Half = Direction * (RebuiltLength * 0.5f);
				const FVector PositionA = Spec.Center - Half;
				const FVector PositionB = Spec.Center + Half;
				UE_LOG(LogABTSRuntime, Warning,
					TEXT("[ABTS][M7.3-Beam-A][RebuildFailed] Reason=AddMember Spec=%d/%d Axis=%d Length=%.2f Rebuilt=%.2f Section=%.2f Tol=%.2f SameJointKey=%d Center=%s Members=%d/%d Joints=%d/%d"),
					SpecIndex, Specs.Num(), static_cast<int32>(Spec.Axis),
					Spec.LengthCM, RebuiltLength,
					Context.Settings->BlockCrossSectionCM,
					Context.Settings->JointMergeToleranceCM,
					JointKey(PositionA, Context.Settings->JointMergeToleranceCM)
						== JointKey(PositionB,
							Context.Settings->JointMergeToleranceCM) ? 1 : 0,
					*Spec.Center.ToString(),
					Context.Result->Members.Num(), Context.Settings->MaxMemberCount,
					Context.Result->Joints.Num(), Context.Settings->MaxJointCount);
				return false;
			}
			const FABTSM73BeamAMember& Member =
				Context.Result->Members[MemberId];
			for (int32 Index = 1; Index < AssemblyIds.Num(); ++Index)
			{
				if (!Context.Result->Assemblies.IsValidIndex(AssemblyIds[Index]))
				{
					UE_LOG(LogABTSRuntime, Warning,
						TEXT("[ABTS][M7.3-Beam-A][RebuildFailed] Reason=InvalidSecondaryAssembly Spec=%d Assembly=%d Assemblies=%d"),
						SpecIndex, AssemblyIds[Index],
						Context.Result->Assemblies.Num());
					return false;
				}
				FABTSM73BeamAAssembly& Assembly =
					Context.Result->Assemblies[AssemblyIds[Index]];
				Assembly.MemberIds.AddUnique(MemberId);
				Assembly.JointIds.AddUnique(Member.JointA);
				Assembly.JointIds.AddUnique(Member.JointB);
			}
		}
		return true;
	}

	int32 CountMemberPenetrations(
		const FABTSM73BeamAPreviewSettings& Settings,
		const FABTSM73BeamAGenerationResult& Result)
	{
		TArray<FBox> Bounds;
		Bounds.Reserve(Result.Members.Num());
		for (const FABTSM73BeamAMember& Member : Result.Members)
		{
			Bounds.Add(MemberBounds(
				Member, Result.Joints, Settings.BlockCrossSectionCM));
		}
		int32 Count = 0;
		const double Tolerance = Settings.JointMergeToleranceCM;
		for (int32 A = 0; A < Bounds.Num(); ++A)
		{
			for (int32 B = A + 1; B < Bounds.Num(); ++B)
			{
				if (FMath::Min(Bounds[A].Max.X, Bounds[B].Max.X)
						- FMath::Max(Bounds[A].Min.X, Bounds[B].Min.X) > Tolerance
					&& FMath::Min(Bounds[A].Max.Y, Bounds[B].Max.Y)
						- FMath::Max(Bounds[A].Min.Y, Bounds[B].Min.Y) > Tolerance
					&& FMath::Min(Bounds[A].Max.Z, Bounds[B].Max.Z)
						- FMath::Max(Bounds[A].Min.Z, Bounds[B].Min.Z) > Tolerance)
				{
					if (Count < 8)
					{
						UE_LOG(
							LogABTSRuntime,
							Warning,
							TEXT("[ABTS][M7.3-Beam-A][Penetration]")
							TEXT(" A=%d AxisA=%d BoundsA=%s")
							TEXT(" B=%d AxisB=%d BoundsB=%s"),
							A,
							static_cast<int32>(Result.Members[A].Axis),
							*Bounds[A].ToString(),
							B,
							static_cast<int32>(Result.Members[B].Axis),
							*Bounds[B].ToString());
					}
					++Count;
				}
			}
		}
		return Count;
	}

	TArray<bool> GroundReachableMembers(
		const FABTSM73BeamAPreviewSettings& Settings,
		const FABTSM73BeamAGenerationResult& Result)
	{
		TArray<bool> Reachable;
		Reachable.Init(false, Result.Members.Num());
		TArray<int32> Queue;
		for (const FABTSM73BeamAMember& Member : Result.Members)
		{
			const FBox Bounds = MemberBounds(
				Member, Result.Joints, Settings.BlockCrossSectionCM);
			if (Bounds.Min.Z <= Settings.JointMergeToleranceCM)
			{
				Reachable[Member.MemberId] = true;
				Queue.Add(Member.MemberId);
			}
		}
		for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
		{
			const int32 LowerId = Queue[QueueIndex];
			for (const FABTSM73BeamABearingContact& Contact :
				Result.BearingContacts)
			{
				if (Contact.LowerMemberId == LowerId
					&& Reachable.IsValidIndex(Contact.UpperMemberId)
					&& !Reachable[Contact.UpperMemberId])
				{
					Reachable[Contact.UpperMemberId] = true;
					Queue.Add(Contact.UpperMemberId);
				}
			}
		}
		return Reachable;
	}

	int32 CountUnsupportedMembers(const TArray<bool>& Reachable)
	{
		int32 Count = 0;
		for (const bool bReachable : Reachable)
		{
			Count += bReachable ? 0 : 1;
		}
		return Count;
	}

	bool ExpandUnsupportedCourseGaps(
		FBuildContext& Context,
		const TArray<bool>& Reachable,
		int32& OutLiftedCourseCount)
	{
		OutLiftedCourseCount = 0;
		const double CrossSection = Context.Settings->BlockCrossSectionCM;
		const double Tolerance = Context.Settings->JointMergeToleranceCM;
		TArray<FMemberBuildSpec> Specs;
		ExtractMemberSpecs(*Context.Result, Specs);
		if (Specs.Num() != Context.Result->Members.Num())
		{
			return false;
		}
		TArray<FBox> Bounds;
		Bounds.Reserve(Specs.Num());
		for (const FMemberBuildSpec& Spec : Specs)
		{
			Bounds.Add(MemberSpecBounds(Spec, CrossSection));
		}
		TSet<int32> LiftedMembers;
		for (int32 UpperId = 0; UpperId < Specs.Num(); ++UpperId)
		{
			if (!Reachable.IsValidIndex(UpperId)
				|| Reachable[UpperId]
				|| LiftedMembers.Contains(UpperId)
				|| Specs[UpperId].Axis == EABTSM73BeamAFrameAxis::Z
				|| Specs[UpperId].Axis == EABTSM73BeamAFrameAxis::Diagonal)
			{
				continue;
			}
			int32 BestLowerId = INDEX_NONE;
			double BestTop = -TNumericLimits<double>::Max();
			for (int32 LowerId = 0; LowerId < Specs.Num(); ++LowerId)
			{
				if (!Reachable.IsValidIndex(LowerId)
					|| !Reachable[LowerId]
					|| Bounds[LowerId].Max.Z
						> Bounds[UpperId].Min.Z + Tolerance
					|| OverlapLength(
						Bounds[LowerId].Min.X,
						Bounds[LowerId].Max.X,
						Bounds[UpperId].Min.X,
						Bounds[UpperId].Max.X) <= Tolerance
					|| OverlapLength(
						Bounds[LowerId].Min.Y,
						Bounds[LowerId].Max.Y,
						Bounds[UpperId].Min.Y,
						Bounds[UpperId].Max.Y) <= Tolerance
					|| Bounds[LowerId].Max.Z <= BestTop)
				{
					continue;
				}
				BestLowerId = LowerId;
				BestTop = Bounds[LowerId].Max.Z;
			}
			if (BestLowerId == INDEX_NONE)
			{
				continue;
			}
			const double Gap = Bounds[UpperId].Min.Z - BestTop;
			if (Gap <= Tolerance || Gap >= CrossSection)
			{
				continue;
			}
			const double LiftZ = CrossSection - Gap;
			const double OriginalZ = Specs[UpperId].Center.Z;
			const TArray<int32> CourseOwners = Specs[UpperId].AssemblyIds;
			const EABTSM73BeamAFrameAxis CourseAxis = Specs[UpperId].Axis;
			const EABTSM73BeamAMemberRole CourseRole = Specs[UpperId].Role;
			for (int32 CandidateId = 0; CandidateId < Specs.Num(); ++CandidateId)
			{
				FMemberBuildSpec& Candidate = Specs[CandidateId];
				const bool bSharesOwner =
					Candidate.AssemblyIds.ContainsByPredicate(
						[&CourseOwners](const int32 AssemblyId)
						{
							return CourseOwners.Contains(AssemblyId);
						});
				if (bSharesOwner
					&& Candidate.Axis == CourseAxis
					&& Candidate.Role == CourseRole
					&& FMath::IsNearlyEqual(
						Candidate.Center.Z, OriginalZ, Tolerance))
				{
					Candidate.Center.Z += LiftZ;
					LiftedMembers.Add(CandidateId);
				}
			}
			++OutLiftedCourseCount;
		}
		return OutLiftedCourseCount == 0
			|| RebuildMembersFromSpecs(Context, Specs);
	}

	bool PruneStalledUnsupportedFragments(
		FBuildContext& Context,
		const TArray<bool>& Reachable,
		int32& OutPrunedMemberCount)
	{
		OutPrunedMemberCount = 0;
		TArray<FMemberBuildSpec> Specs;
		ExtractMemberSpecs(*Context.Result, Specs);
		if (Specs.Num() != Context.Result->Members.Num())
		{
			return false;
		}
		TArray<bool> AssemblyHasReachableMember;
		AssemblyHasReachableMember.Init(
			false, Context.Result->Assemblies.Num());
		for (const FABTSM73BeamAAssembly& Assembly : Context.Result->Assemblies)
		{
			for (const int32 MemberId : Assembly.MemberIds)
			{
				if (Reachable.IsValidIndex(MemberId) && Reachable[MemberId])
				{
					AssemblyHasReachableMember[Assembly.AssemblyId] = true;
					break;
				}
			}
		}
		TArray<TArray<int32>> UnsupportedNeighbors;
		UnsupportedNeighbors.SetNum(Specs.Num());
		for (const FABTSM73BeamABearingContact& Contact :
			Context.Result->BearingContacts)
		{
			if (!Reachable.IsValidIndex(Contact.LowerMemberId)
				|| !Reachable.IsValidIndex(Contact.UpperMemberId)
				|| Reachable[Contact.LowerMemberId]
				|| Reachable[Contact.UpperMemberId])
			{
				continue;
			}
			UnsupportedNeighbors[Contact.LowerMemberId].AddUnique(
				Contact.UpperMemberId);
			UnsupportedNeighbors[Contact.UpperMemberId].AddUnique(
				Contact.LowerMemberId);
		}
		TArray<bool> Visited;
		Visited.Init(false, Specs.Num());
		TArray<bool> RemoveMember;
		RemoveMember.Init(false, Specs.Num());
		for (int32 SeedMemberId = 0;
			SeedMemberId < Specs.Num(); ++SeedMemberId)
		{
			if (Visited[SeedMemberId]
				|| !Reachable.IsValidIndex(SeedMemberId)
				|| Reachable[SeedMemberId])
			{
				continue;
			}
			TArray<int32> Component;
			TArray<int32> Pending;
			Pending.Add(SeedMemberId);
			Visited[SeedMemberId] = true;
			while (!Pending.IsEmpty())
			{
				const int32 MemberId = Pending.Pop(EAllowShrinking::No);
				Component.Add(MemberId);
				for (const int32 NeighborId : UnsupportedNeighbors[MemberId])
				{
					if (!Visited[NeighborId])
					{
						Visited[NeighborId] = true;
						Pending.Add(NeighborId);
					}
				}
			}
			bool bEveryOwnerRemainsSupported = !Component.IsEmpty();
			for (const int32 MemberId : Component)
			{
				if (Specs[MemberId].AssemblyIds.IsEmpty())
				{
					bEveryOwnerRemainsSupported = false;
					break;
				}
				for (const int32 AssemblyId : Specs[MemberId].AssemblyIds)
				{
					if (!AssemblyHasReachableMember.IsValidIndex(AssemblyId)
						|| !AssemblyHasReachableMember[AssemblyId])
					{
						bEveryOwnerRemainsSupported = false;
						break;
					}
				}
				if (!bEveryOwnerRemainsSupported)
				{
					break;
				}
			}
			if (!bEveryOwnerRemainsSupported)
			{
				continue;
			}
			for (const int32 MemberId : Component)
			{
				RemoveMember[MemberId] = true;
			}
		}
		for (int32 MemberId = Specs.Num() - 1; MemberId >= 0; --MemberId)
		{
			if (!RemoveMember[MemberId])
			{
				continue;
			}
			Specs.RemoveAt(MemberId);
			++OutPrunedMemberCount;
		}
		return OutPrunedMemberCount == 0
			|| RebuildMembersFromSpecs(Context, Specs);
	}

	bool AddGlobalSupportMembers(
		FBuildContext& Context,
		const TArray<bool>& Reachable,
		int32& OutAddedCount)
	{
		struct FSupportProposal
		{
			int32 AssemblyId = INDEX_NONE;
			FVector2D Station = FVector2D::ZeroVector;
			double BottomZ = 0.0;
			double TopZ = 0.0;
		};
		OutAddedCount = 0;
		const double CrossSection = Context.Settings->BlockCrossSectionCM;
		const double Tolerance = Context.Settings->JointMergeToleranceCM;
		TArray<FBox> Bounds;
		Bounds.Reserve(Context.Result->Members.Num());
		for (const FABTSM73BeamAMember& Member : Context.Result->Members)
		{
			Bounds.Add(MemberBounds(
				Member, Context.Result->Joints, CrossSection));
		}
		TArray<FSupportProposal> Proposals;
		auto IsReservedSupport = [&Context, Tolerance](
			const FVector2D& Station,
			const double BottomZ,
			const double TopZ)
		{
			for (const FABTSM73BeamASupportVoid& SupportVoid :
				Context.Result->ReservedSupportVoids)
			{
				const FBox& Void = SupportVoid.Bounds;
				const double VerticalOverlap = FMath::Min(TopZ, Void.Max.Z)
					- FMath::Max(BottomZ, Void.Min.Z);
				if (VerticalOverlap > Tolerance
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
		for (const FABTSM73BeamAAssembly& Assembly : Context.Result->Assemblies)
		{
			double LowestUnsupportedBottom = TNumericLimits<double>::Max();
			for (const int32 MemberId : Assembly.MemberIds)
			{
				if (Reachable.IsValidIndex(MemberId)
					&& !Reachable[MemberId])
				{
					LowestUnsupportedBottom = FMath::Min(
						LowestUnsupportedBottom, Bounds[MemberId].Min.Z);
				}
			}
			if (!FMath::IsFinite(LowestUnsupportedBottom))
			{
				continue;
			}
			TArray<FVector2D> ProposedStations;
			auto AddProposal = [&](const FVector2D& Station,
				const double BottomZ, const double TopZ)
			{
				if (ProposedStations.ContainsByPredicate(
					[&Station, Tolerance](const FVector2D& Existing)
					{
						return Existing.Equals(Station, Tolerance);
					}))
				{
					return;
				}
				FSupportProposal& Proposal = Proposals.AddDefaulted_GetRef();
				Proposal.AssemblyId = Assembly.AssemblyId;
				Proposal.Station = Station;
				Proposal.BottomZ = BottomZ;
				Proposal.TopZ = TopZ;
				ProposedStations.Add(Station);
			};
			auto FindSpanVoid = [&Bounds, LowestUnsupportedBottom, Tolerance,
				&Context, &Assembly](const int32 UpperId)
				-> const FABTSM73BeamASupportVoid*
			{
				if (!Context.Result->Bays.IsValidIndex(Assembly.BayId))
				{
					return nullptr;
				}
				const int32 OwnerSourceVolumeId =
					Context.Result->Bays[Assembly.BayId].SourceVolumeId;
				return Context.Result->ReservedSupportVoids.FindByPredicate(
					[&Bounds, UpperId, LowestUnsupportedBottom, Tolerance,
						OwnerSourceVolumeId](
						const FABTSM73BeamASupportVoid& Candidate)
					{
						const FBox& Void = Candidate.Bounds;
						return Candidate.SpanSourceVolumeId
								== OwnerSourceVolumeId
							&& FMath::Abs(
							Void.Max.Z - LowestUnsupportedBottom) <= Tolerance
							&& OverlapLength(
								Void.Min.X, Void.Max.X,
								Bounds[UpperId].Min.X,
								Bounds[UpperId].Max.X) > Tolerance
							&& OverlapLength(
								Void.Min.Y, Void.Max.Y,
								Bounds[UpperId].Min.Y,
								Bounds[UpperId].Max.Y) > Tolerance;
					});
			};
			for (const int32 UpperId : Assembly.MemberIds)
			{
				if (!Reachable.IsValidIndex(UpperId)
					|| Reachable[UpperId]
					|| !FMath::IsNearlyEqual(
						Bounds[UpperId].Min.Z,
						LowestUnsupportedBottom,
						Tolerance))
				{
					continue;
				}
				if (const FABTSM73BeamASupportVoid* SpanVoid =
					FindSpanVoid(UpperId))
				{
					const FBox& Void = SpanVoid->Bounds;
					const int32 Axis = SpanVoid->SpanAxisIndex;
					const int32 Perpendicular = Axis == 0 ? 1 : 0;
					FVector2D NegativeStation(
						Bounds[UpperId].GetCenter().X,
						Bounds[UpperId].GetCenter().Y);
					FVector2D PositiveStation = NegativeStation;
					NegativeStation[Axis] = Void.Min[Axis]
						- CrossSection * 0.5;
					PositiveStation[Axis] = Void.Max[Axis]
						+ CrossSection * 0.5;
					NegativeStation[Perpendicular] = FMath::Clamp(
						NegativeStation[Perpendicular],
						Void.Min[Perpendicular],
						Void.Max[Perpendicular]);
					PositiveStation[Perpendicular] =
						NegativeStation[Perpendicular];
					AddProposal(NegativeStation, 0.0,
						LowestUnsupportedBottom);
					AddProposal(PositiveStation, 0.0,
						LowestUnsupportedBottom);
					if (ProposedStations.Num()
						>= Context.Settings->MaxParallelBlocksPerCourse)
					{
						break;
					}
					continue;
				}
				int32 BestLowerId = INDEX_NONE;
				double BestTop = -TNumericLimits<double>::Max();
				FVector2D BestStation = FVector2D::ZeroVector;
				for (const FABTSM73BeamAMember& Lower : Context.Result->Members)
				{
					if (!Reachable.IsValidIndex(Lower.MemberId)
						|| !Reachable[Lower.MemberId]
						|| Lower.Axis == EABTSM73BeamAFrameAxis::Z
						|| Bounds[Lower.MemberId].Max.Z
							+ CrossSection > LowestUnsupportedBottom)
					{
						continue;
					}
					const double OverlapMinX = FMath::Max(
						Bounds[Lower.MemberId].Min.X, Bounds[UpperId].Min.X);
					const double OverlapMaxX = FMath::Min(
						Bounds[Lower.MemberId].Max.X, Bounds[UpperId].Max.X);
					const double OverlapMinY = FMath::Max(
						Bounds[Lower.MemberId].Min.Y, Bounds[UpperId].Min.Y);
					const double OverlapMaxY = FMath::Min(
						Bounds[Lower.MemberId].Max.Y, Bounds[UpperId].Max.Y);
					if (OverlapMaxX - OverlapMinX <= Tolerance
						|| OverlapMaxY - OverlapMinY <= Tolerance
						|| Bounds[Lower.MemberId].Max.Z <= BestTop)
					{
						continue;
					}
					BestLowerId = Lower.MemberId;
					BestTop = Bounds[Lower.MemberId].Max.Z;
					BestStation = FVector2D(
						(OverlapMinX + OverlapMaxX) * 0.5,
						(OverlapMinY + OverlapMaxY) * 0.5);
				}
				if (BestLowerId == INDEX_NONE)
				{
					BestTop = 0.0;
					BestStation = FVector2D(
						Context.Result->Joints[
							Context.Result->Members[UpperId].JointA]
							.LocalPosition.X,
						Context.Result->Joints[
							Context.Result->Members[UpperId].JointA]
							.LocalPosition.Y);
					if (Context.Result->Members[UpperId].Axis
						!= EABTSM73BeamAFrameAxis::Z)
					{
						BestStation = FVector2D(
							Bounds[UpperId].GetCenter().X,
							Bounds[UpperId].GetCenter().Y);
					}
				}
				const float Length = static_cast<float>(
					LowestUnsupportedBottom - BestTop);
				if (Length < CrossSection)
				{
					continue;
				}
				if (IsReservedSupport(
					BestStation, BestTop, LowestUnsupportedBottom))
				{
					continue;
				}
				AddProposal(BestStation, BestTop, LowestUnsupportedBottom);
				if (ProposedStations.Num()
					>= Context.Settings->MaxParallelBlocksPerCourse)
				{
					break;
				}
			}
		}
		for (const FSupportProposal& Proposal : Proposals)
		{
			if (!Context.Result->Assemblies.IsValidIndex(Proposal.AssemblyId))
			{
				return false;
			}
			const float Length = static_cast<float>(
				Proposal.TopZ - Proposal.BottomZ);
			const int32 PreviousMemberCount = Context.Result->Members.Num();
			if (AddMember(
				Context,
				Context.Result->Assemblies[Proposal.AssemblyId],
				FVector(
					Proposal.Station.X,
					Proposal.Station.Y,
					(Proposal.BottomZ + Proposal.TopZ) * 0.5),
				Length,
				EABTSM73BeamAFrameAxis::Z,
				EABTSM73BeamAMemberRole::Post) == INDEX_NONE)
			{
				return false;
			}
			OutAddedCount +=
				Context.Result->Members.Num() > PreviousMemberCount ? 1 : 0;
		}
		return true;
	}

	bool CloseGlobalAssembly(
		FBuildContext& Context,
		FString& OutError)
	{
		constexpr int32 MaxClosurePasses = 32;
		int32 PreviousUnsupportedCount = TNumericLimits<int32>::Max();
		bool bSupportAttemptedLastPass = false;
		int32 PreviousGapLiftUnsupportedCount =
			TNumericLimits<int32>::Max();
		bool bGapLiftAttemptedLastPass = false;
		for (int32 Pass = 0; Pass < MaxClosurePasses; ++Pass)
		{
			TArray<FMemberBuildSpec> Specs;
			ExtractMemberSpecs(*Context.Result, Specs);
			int32 PreMergedCount = 0;
			MergeCollinearMemberSpecs(
				Specs,
				Context.Settings->BlockCrossSectionCM,
				Context.Settings->JointMergeToleranceCM,
				PreMergedCount);
			int32 ShiftedCourseCount = 0;
			if (!SeparateIntersectingHorizontalCourses(
				Specs, *Context.Settings, ShiftedCourseCount))
			{
				OutError = TEXT("BeamAHorizontalCourseSeparationFailed");
				return false;
			}
			int32 SplitCount = 0;
			SplitPostsAtHorizontalCourses(
				Specs, *Context.Settings, SplitCount);
			int32 PostMergedCount = 0;
			MergeCollinearMemberSpecs(
				Specs,
				Context.Settings->BlockCrossSectionCM,
				Context.Settings->JointMergeToleranceCM,
				PostMergedCount);
			const int32 MergedCount = PreMergedCount + PostMergedCount;
			Context.Result->Summary.SplitPostMemberCount += SplitCount;
			Context.Result->Summary.MergedMemberCount += MergedCount;
			Context.Result->Summary.ShiftedCourseCount += ShiftedCourseCount;
			if (!RebuildMembersFromSpecs(Context, Specs))
			{
				OutError = TEXT("BeamAGlobalAssemblyRebuildFailed");
				return false;
			}
			if (!BuildBearingContacts(
				*Context.Settings, *Context.Result, OutError))
			{
				return false;
			}
			const TArray<bool> Reachable = GroundReachableMembers(
				*Context.Settings, *Context.Result);
			const int32 UnsupportedCount =
				CountUnsupportedMembers(Reachable);
			UE_LOG(
				LogABTSRuntime,
				Display,
				TEXT("[ABTS][M7.3-Beam-A][GlobalClosure]")
				TEXT(" Pass=%d Members=%d Bearings=%d Split=%d Merge=%d Shift=%d")
				TEXT(" Unsupported=%d"),
				Pass,
				Context.Result->Members.Num(),
				Context.Result->BearingContacts.Num(),
				SplitCount,
				MergedCount,
				ShiftedCourseCount,
				UnsupportedCount);
			if (UnsupportedCount == 0)
			{
				Context.Result->Summary.UnsupportedMemberCount = 0;
				Context.Result->Summary.RemainingPenetrationCount =
					CountMemberPenetrations(
						*Context.Settings, *Context.Result);
				if (Context.Result->Summary.RemainingPenetrationCount > 0)
				{
					OutError = TEXT("BeamAMemberPenetration");
					return false;
				}
				return true;
			}
			int32 LiftedCourseCount = 0;
			const bool bGapLiftMadeProgress =
				!bGapLiftAttemptedLastPass
				|| UnsupportedCount < PreviousGapLiftUnsupportedCount;
			if (bGapLiftMadeProgress
				&& !ExpandUnsupportedCourseGaps(
					Context, Reachable, LiftedCourseCount))
			{
				OutError = TEXT("BeamAGlobalCompactionRebuildFailed");
				return false;
			}
			if (LiftedCourseCount > 0)
			{
				Context.Result->Summary.ShiftedCourseCount +=
					LiftedCourseCount;
				UE_LOG(
					LogABTSRuntime,
					Display,
					TEXT("[ABTS][M7.3-Beam-A][GlobalClosureGapLift]")
					TEXT(" Pass=%d Lifted=%d"),
					Pass,
					LiftedCourseCount);
				PreviousUnsupportedCount = TNumericLimits<int32>::Max();
				bSupportAttemptedLastPass = false;
				PreviousGapLiftUnsupportedCount = UnsupportedCount;
				bGapLiftAttemptedLastPass = true;
				continue;
			}
			bGapLiftAttemptedLastPass = false;
			if (bSupportAttemptedLastPass
				&& UnsupportedCount >= PreviousUnsupportedCount)
			{
				int32 PrunedMemberCount = 0;
				if (!PruneStalledUnsupportedFragments(
					Context, Reachable, PrunedMemberCount))
				{
					OutError = TEXT("BeamAGlobalPruneRebuildFailed");
					return false;
				}
				if (PrunedMemberCount == 0)
				{
					for (const FABTSM73BeamAMember& Member : Context.Result->Members)
					{
						if (!Reachable.IsValidIndex(Member.MemberId)
							|| Reachable[Member.MemberId])
						{
							continue;
						}
						const FBox Bounds = MemberBounds(Member,
							Context.Result->Joints,
							Context.Settings->BlockCrossSectionCM);
						UE_LOG(LogABTSRuntime, Warning,
							TEXT("[ABTS][M7.3-Beam-A][UnsupportedMember]")
							TEXT(" Member=%d Axis=%d Center=%s MinZ=%.2f MaxZ=%.2f"),
							Member.MemberId,
							static_cast<int32>(Member.Axis),
							*Bounds.GetCenter().ToCompactString(),
							Bounds.Min.Z,
							Bounds.Max.Z);
					}
					Context.Result->Summary.UnsupportedMemberCount =
						UnsupportedCount;
					OutError = TEXT("BeamAUnsupportedMembers");
					return false;
				}
				Context.Result->Summary.PrunedUnsupportedMemberCount +=
					PrunedMemberCount;
				UE_LOG(
					LogABTSRuntime,
					Display,
					TEXT("[ABTS][M7.3-Beam-A][GlobalClosurePrune]")
					TEXT(" Pass=%d Pruned=%d"),
					Pass,
					PrunedMemberCount);
				PreviousUnsupportedCount = TNumericLimits<int32>::Max();
				bSupportAttemptedLastPass = false;
				continue;
			}
			int32 AddedCount = 0;
			if (!AddGlobalSupportMembers(Context, Reachable, AddedCount))
			{
				OutError = TEXT("BeamAGlobalSupportBudgetExceeded");
				return false;
			}
			Context.Result->Summary.GlobalSupportMemberCount += AddedCount;
			UE_LOG(
				LogABTSRuntime,
				Display,
				TEXT("[ABTS][M7.3-Beam-A][GlobalClosureSupport]")
				TEXT(" Pass=%d Added=%d"),
				Pass,
				AddedCount);
			if (AddedCount == 0)
			{
				int32 PrunedMemberCount = 0;
				if (!PruneStalledUnsupportedFragments(
					Context, Reachable, PrunedMemberCount))
				{
					OutError = TEXT("BeamAGlobalPruneRebuildFailed");
					return false;
				}
				if (PrunedMemberCount == 0)
				{
					for (const FABTSM73BeamAMember& Member : Context.Result->Members)
					{
						if (!Reachable.IsValidIndex(Member.MemberId)
							|| Reachable[Member.MemberId])
						{
							continue;
						}
						const FBox Bounds = MemberBounds(Member,
							Context.Result->Joints,
							Context.Settings->BlockCrossSectionCM);
						UE_LOG(LogABTSRuntime, Warning,
							TEXT("[ABTS][M7.3-Beam-A][UnsupportedMember]")
							TEXT(" Member=%d Axis=%d Center=%s MinZ=%.2f MaxZ=%.2f"),
							Member.MemberId,
							static_cast<int32>(Member.Axis),
							*Bounds.GetCenter().ToCompactString(),
							Bounds.Min.Z,
							Bounds.Max.Z);
					}
					Context.Result->Summary.UnsupportedMemberCount =
						UnsupportedCount;
					OutError = TEXT("BeamAUnsupportedMembers");
					return false;
				}
				Context.Result->Summary.PrunedUnsupportedMemberCount +=
					PrunedMemberCount;
				UE_LOG(
					LogABTSRuntime,
					Display,
					TEXT("[ABTS][M7.3-Beam-A][GlobalClosurePrune]")
					TEXT(" Pass=%d Pruned=%d Reason=NoSupportProposal"),
					Pass,
					PrunedMemberCount);
				PreviousUnsupportedCount = TNumericLimits<int32>::Max();
				bSupportAttemptedLastPass = false;
				continue;
			}
			PreviousUnsupportedCount = UnsupportedCount;
			bSupportAttemptedLastPass = true;
		}
		OutError = TEXT("BeamAGlobalAssemblyPassBudgetExceeded");
		return false;
	}

	FBox StructuralBoundsForVolume(
		const FABTSM73DAG5BV2Volume& Volume,
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		const FABTSM73BeamAPreviewSettings& Settings)
	{
		FBox Result = Volume.LocalBounds;
		if (!IsSupportedSpanRole(Volume.Role))
		{
			return Result;
		}
		const FVector Size = Volume.LocalBounds.GetSize();
		const int32 AxisIndex = Volume.SpanAxisIndex == 0
			|| Volume.SpanAxisIndex == 1
				? Volume.SpanAxisIndex
				: Size.X >= Size.Y ? 0 : 1;
		const int32 PerpendicularIndex = AxisIndex == 0 ? 1 : 0;
		const double Center = Volume.LocalBounds.GetCenter()[AxisIndex];
		const double Tolerance = Settings.JointMergeToleranceCM;
		if (Volume.Role == EABTSM73DAG5BV2VolumeRole::SupportedSpan)
		{
			if (Volume.SpanOpeningMaxCM - Volume.SpanOpeningMinCM
				> Tolerance)
			{
				Result.Min[AxisIndex] = FMath::Max(
					Result.Min[AxisIndex],
					Volume.SpanOpeningMinCM
						- Settings.BlockCrossSectionCM * 0.5);
				Result.Max[AxisIndex] = FMath::Min(
					Result.Max[AxisIndex],
					Volume.SpanOpeningMaxCM
						+ Settings.BlockCrossSectionCM * 0.5);
				return Result;
			}
			const FABTSM73DAG5BV2Volume* NegativeSupport =
				Silhouette.Volumes.FindByPredicate(
					[&Volume](const FABTSM73DAG5BV2Volume& Candidate)
					{
						return Candidate.VolumeId
							== Volume.NegativeSupportVolumeId;
					});
			const FABTSM73DAG5BV2Volume* PositiveSupport =
				Silhouette.Volumes.FindByPredicate(
					[&Volume](const FABTSM73DAG5BV2Volume& Candidate)
					{
						return Candidate.VolumeId
							== Volume.PositiveSupportVolumeId;
					});
			if (NegativeSupport != nullptr && PositiveSupport != nullptr)
			{
				const double NegativeOverlap = OverlapLength(
					Volume.LocalBounds.Min[AxisIndex],
					Volume.LocalBounds.Max[AxisIndex],
					NegativeSupport->LocalBounds.Min[AxisIndex],
					NegativeSupport->LocalBounds.Max[AxisIndex]);
				const double PositiveOverlap = OverlapLength(
					Volume.LocalBounds.Min[AxisIndex],
					Volume.LocalBounds.Max[AxisIndex],
					PositiveSupport->LocalBounds.Min[AxisIndex],
					PositiveSupport->LocalBounds.Max[AxisIndex]);
				const double NegativeInset = FMath::Min(
					Settings.BlockCrossSectionCM * 0.5, NegativeOverlap);
				const double PositiveInset = FMath::Min(
					Settings.BlockCrossSectionCM * 0.5, PositiveOverlap);
				Result.Min[AxisIndex] = FMath::Max(
					Result.Min[AxisIndex],
					NegativeSupport->LocalBounds.Max[AxisIndex]
						- NegativeInset);
				Result.Max[AxisIndex] = FMath::Min(
					Result.Max[AxisIndex],
					PositiveSupport->LocalBounds.Min[AxisIndex]
						+ PositiveInset);
				return Result;
			}
		}
		for (const FABTSM73DAG5BV2Volume& Candidate : Silhouette.Volumes)
		{
			if (Candidate.VolumeId == Volume.VolumeId
				|| IsSupportedSpanRole(Candidate.Role))
			{
				continue;
			}
			const double PerpendicularOverlap = FMath::Min(
				Volume.LocalBounds.Max[PerpendicularIndex],
				Candidate.LocalBounds.Max[PerpendicularIndex])
				- FMath::Max(
					Volume.LocalBounds.Min[PerpendicularIndex],
					Candidate.LocalBounds.Min[PerpendicularIndex]);
			const double VerticalOverlap = FMath::Min(
				Volume.LocalBounds.Max.Z, Candidate.LocalBounds.Max.Z)
				- FMath::Max(
					Volume.LocalBounds.Min.Z, Candidate.LocalBounds.Min.Z);
			const double LongitudinalOverlap = FMath::Min(
				Volume.LocalBounds.Max[AxisIndex],
				Candidate.LocalBounds.Max[AxisIndex])
				- FMath::Max(
					Volume.LocalBounds.Min[AxisIndex],
					Candidate.LocalBounds.Min[AxisIndex]);
			if (PerpendicularOverlap <= Tolerance
				|| VerticalOverlap <= Tolerance
				|| LongitudinalOverlap <= Tolerance)
			{
				continue;
			}
			const double BearingInset = FMath::Min(
				Settings.BlockCrossSectionCM * 0.5,
				LongitudinalOverlap);
			const double CandidateCenter =
				Candidate.LocalBounds.GetCenter()[AxisIndex];
			if (CandidateCenter < Center)
			{
				Result.Min[AxisIndex] = FMath::Max(
					Result.Min[AxisIndex],
					Candidate.LocalBounds.Max[AxisIndex] - BearingInset);
			}
			else if (CandidateCenter > Center)
			{
				Result.Max[AxisIndex] = FMath::Min(
					Result.Max[AxisIndex],
					Candidate.LocalBounds.Min[AxisIndex] + BearingInset);
			}
		}
		return Result;
	}

	bool BaysTouch(const FBox& A, const FBox& B, const float Tolerance)
	{
		const float XOverlap = OverlapLength(
			A.Min.X, A.Max.X, B.Min.X, B.Max.X);
		const float YOverlap = OverlapLength(
			A.Min.Y, A.Max.Y, B.Min.Y, B.Max.Y);
		const float ZOverlap = OverlapLength(
			A.Min.Z, A.Max.Z, B.Min.Z, B.Max.Z);
		const bool bTouchX =
			FMath::Min(
				FMath::Abs(A.Max.X - B.Min.X),
				FMath::Abs(B.Max.X - A.Min.X)) <= Tolerance
			&& YOverlap > Tolerance && ZOverlap > Tolerance;
		const bool bTouchY =
			FMath::Min(
				FMath::Abs(A.Max.Y - B.Min.Y),
				FMath::Abs(B.Max.Y - A.Min.Y)) <= Tolerance
			&& XOverlap > Tolerance && ZOverlap > Tolerance;
		const bool bTouchZ =
			FMath::Min(
				FMath::Abs(A.Max.Z - B.Min.Z),
				FMath::Abs(B.Max.Z - A.Min.Z)) <= Tolerance
			&& XOverlap > Tolerance && YOverlap > Tolerance;
		return bTouchX || bTouchY || bTouchZ;
	}

	FString CanonicalBays(const TArray<FABTSM73BeamABay>& Bays)
	{
		FString Text;
		for (const FABTSM73BeamABay& Bay : Bays)
		{
			Text += FString::Printf(
				TEXT("B%d:V%d:%.2f,%.2f,%.2f:%.2f,%.2f,%.2f:A%d|"),
				Bay.BayId,
				Bay.SourceVolumeId,
				Bay.LocalBounds.Min.X,
				Bay.LocalBounds.Min.Y,
				Bay.LocalBounds.Min.Z,
				Bay.LocalBounds.Max.X,
				Bay.LocalBounds.Max.Y,
				Bay.LocalBounds.Max.Z,
				static_cast<int32>(Bay.PreferredAxis));
			for (const int32 Neighbor : Bay.AdjacentBayIds)
			{
				Text += FString::Printf(TEXT("N%d,"), Neighbor);
			}
		}
		return Text;
	}

	FString CanonicalBeamGraph(
		const FABTSM73BeamAGenerationResult& Result)
	{
		FString Text;
		for (const FABTSM73BeamAJoint& Joint : Result.Joints)
		{
			Text += FString::Printf(
				TEXT("J%d:%.2f,%.2f,%.2f:R%d|"),
				Joint.JointId,
				Joint.LocalPosition.X,
				Joint.LocalPosition.Y,
				Joint.LocalPosition.Z,
				static_cast<int32>(Joint.Role));
		}
		for (const FABTSM73BeamAMember& Member : Result.Members)
		{
			Text += FString::Printf(
				TEXT("M%d:%d-%d:A%d:R%d:L%.2f|"),
				Member.MemberId,
				Member.JointA,
				Member.JointB,
				static_cast<int32>(Member.Axis),
				static_cast<int32>(Member.Role),
				Member.LengthCM);
		}
		for (const FABTSM73BeamABearingContact& Contact :
			Result.BearingContacts)
		{
			Text += FString::Printf(
				TEXT("C%d:%d>%d:T%d:P%.2f,%.2f,%.2f:A%.2f|"),
				Contact.ContactId,
				Contact.LowerMemberId,
				Contact.UpperMemberId,
				static_cast<int32>(Contact.Type),
				Contact.LocalPosition.X,
				Contact.LocalPosition.Y,
				Contact.LocalPosition.Z,
				Contact.ContactAreaCM2);
		}
		for (const FABTSM73BeamAAssembly& Assembly : Result.Assemblies)
		{
			Text += FString::Printf(
				TEXT("A%d:B%d:T%d|"),
				Assembly.AssemblyId,
				Assembly.BayId,
				static_cast<int32>(Assembly.Type));
		}
		return Text;
	}

	bool CloseGeneratedAssembly(
		const FABTSM73BeamAPreviewSettings& Settings,
		FABTSM73BeamAGenerationResult& InOutResult,
		FString& OutError)
	{
		FBuildContext Context;
		Context.Settings = &Settings;
		Context.Result = &InOutResult;
		return CloseGlobalAssembly(Context, OutError);
	}

	bool RebuildBearingContacts(
		const FABTSM73BeamAPreviewSettings& Settings,
		FABTSM73BeamAGenerationResult& InOutResult,
		FString& OutError)
	{
		return BuildBearingContacts(Settings, InOutResult, OutError);
	}
}

bool FABTSM73BeamAGenerator::Generate(
	const FABTSM73BeamAPreviewSettings& Settings,
	const FABTSM73DAG5BV2GenerationResult& Silhouette,
	FABTSM73BeamAGenerationResult& OutResult,
	FString& OutError) const
{
	using namespace ABTSM73BeamA;
	OutResult = FABTSM73BeamAGenerationResult();
	OutError.Reset();
	auto Reject = [&OutResult, &OutError](const FString& Reason)
	{
		OutError = Reason;
		OutResult = FABTSM73BeamAGenerationResult();
		OutResult.Summary.RejectReason = Reason;
		return false;
	};

	if (!Silhouette.Summary.bAccepted || Silhouette.Volumes.IsEmpty())
	{
		return Reject(TEXT("BeamASilhouetteNotAccepted"));
	}
	if (!FMath::IsFinite(Settings.TargetBaySpanCM)
		|| !FMath::IsFinite(Settings.MaximumVerticalSupportSpanCM)
		|| !FMath::IsFinite(Settings.BlockCrossSectionCM)
		|| !FMath::IsFinite(Settings.MinimumParallelBlockGapCM)
		|| !FMath::IsFinite(Settings.TwoBlockMergeGapCM)
		|| !FMath::IsFinite(Settings.JointMergeToleranceCM)
		|| Settings.TargetBaySpanCM <= 0.0f
		|| Settings.MaximumVerticalSupportSpanCM
			< Settings.BlockCrossSectionCM * 4.0f
		|| Settings.BlockCrossSectionCM <= 0.0f
		|| Settings.MinimumParallelBlockGapCM <= 0.0f
		|| Settings.TwoBlockMergeGapCM < 0.0f
		|| Settings.TwoBlockMergeGapCM > Settings.MinimumParallelBlockGapCM
		|| Settings.MaxParallelBlocksPerCourse < 2
		|| Settings.MaxFrameParallelBlocksPerCourse < 1
		|| Settings.JointMergeToleranceCM <= 0.0f
		|| Settings.MaxRoofCourseCount < 2
		|| Settings.RoofBlocksPerCourse < 1
		|| Settings.MaxBaysPerVolume < 1
		|| Settings.MaxBayCount < 1
		|| Settings.MaxJointCount < 2
		|| Settings.MaxMemberCount < 1
		|| Settings.MaxBearingContactCount < 1
		|| Settings.MaxBearingPairChecks < 1)
	{
		return Reject(TEXT("BeamAInvalidSettings"));
	}

	for (const FABTSM73DAG5BV2Volume& Volume : Silhouette.Volumes)
	{
		if (!Volume.LocalBounds.IsValid)
		{
			return Reject(TEXT("BeamAInvalidSourceBounds"));
		}
		const FBox StructuralBounds = StructuralBoundsForVolume(
			Volume, Silhouette, Settings);
		if (!StructuralBounds.IsValid
			|| StructuralBounds.GetSize().GetMin() < Settings.BlockCrossSectionCM)
		{
			return Reject(TEXT("BeamAInvalidStructuralBounds"));
		}
		if (Volume.Role == EABTSM73DAG5BV2VolumeRole::SupportedSpan)
		{
			const int32 SpanAxis = Volume.SpanAxisIndex == 0
				|| Volume.SpanAxisIndex == 1
					? Volume.SpanAxisIndex
					: StructuralBounds.GetSize().X
						>= StructuralBounds.GetSize().Y ? 0 : 1;
			FBox ReservedVoid = StructuralBounds;
			if (Volume.SpanOpeningMaxCM - Volume.SpanOpeningMinCM
				> Settings.JointMergeToleranceCM)
			{
				ReservedVoid.Min[SpanAxis] = Volume.SpanOpeningMinCM;
				ReservedVoid.Max[SpanAxis] = Volume.SpanOpeningMaxCM;
			}
			const FABTSM73DAG5BV2Volume* NegativeSupport =
				Silhouette.Volumes.FindByPredicate(
					[&Volume](const FABTSM73DAG5BV2Volume& Candidate)
					{
						return Candidate.VolumeId
							== Volume.NegativeSupportVolumeId;
					});
			const FABTSM73DAG5BV2Volume* PositiveSupport =
				Silhouette.Volumes.FindByPredicate(
					[&Volume](const FABTSM73DAG5BV2Volume& Candidate)
					{
						return Candidate.VolumeId
							== Volume.PositiveSupportVolumeId;
					});
			if (Volume.SpanOpeningMaxCM - Volume.SpanOpeningMinCM
				> Settings.JointMergeToleranceCM)
			{
				// The Shape Grammar's module-family envelope is authoritative.
			}
			else if (NegativeSupport != nullptr && PositiveSupport != nullptr)
			{
				ReservedVoid.Min[SpanAxis] =
					NegativeSupport->LocalBounds.Max[SpanAxis];
				ReservedVoid.Max[SpanAxis] =
					PositiveSupport->LocalBounds.Min[SpanAxis];
			}
			else
			{
				ReservedVoid.Min[SpanAxis] += Settings.BlockCrossSectionCM;
				ReservedVoid.Max[SpanAxis] -= Settings.BlockCrossSectionCM;
			}
			ReservedVoid.Min.Z = 0.0;
			ReservedVoid.Max.Z = StructuralBounds.Min.Z;
			if (ReservedVoid.IsValid
				&& ReservedVoid.GetSize()[SpanAxis]
					>= Settings.BlockCrossSectionCM)
			{
				FABTSM73BeamASupportVoid& SupportVoid =
					OutResult.ReservedSupportVoids.AddDefaulted_GetRef();
				SupportVoid.Bounds = ReservedVoid;
				SupportVoid.SpanAxisIndex = SpanAxis;
				SupportVoid.SpanSourceVolumeId = Volume.VolumeId;
			}
		}
		const FVector Size = StructuralBounds.GetSize();
		const bool bSupportedSpan = IsSupportedSpanRole(Volume.Role);
		const int32 SemanticAxisIndex = bSupportedSpan
			&& (Volume.SpanAxisIndex == 0 || Volume.SpanAxisIndex == 1)
			? Volume.SpanAxisIndex
			: Size.X >= Size.Y ? 0 : 1;
		// The silhouette grammar is responsible for X/Y module balance. Each
		// module keeps the proven long-axis one-dimensional frame subdivision;
		// Beam-A must not compensate for a biased silhouette by twisting local
		// structural topology into the short direction.
		const int32 AxisIndex = SemanticAxisIndex;
		const bool bSemanticRoof = !bSupportedSpan
			&& Volume.Primitive != EABTSM73DAG5BV2Primitive::Box;
		const int32 BayCount = bSemanticRoof
			? 1
			: FMath::Clamp(
				FMath::CeilToInt(
					static_cast<float>(Size[AxisIndex])
						/ Settings.TargetBaySpanCM),
				1,
				Settings.MaxBaysPerVolume);
		if (OutResult.Bays.Num() + BayCount > Settings.MaxBayCount)
		{
			return Reject(TEXT("BeamAMaxBayCountExceeded"));
		}
		for (int32 BayIndex = 0; BayIndex < BayCount; ++BayIndex)
		{
			FABTSM73BeamABay& Bay =
				OutResult.Bays.AddDefaulted_GetRef();
			Bay.BayId = OutResult.Bays.Num() - 1;
			Bay.SourceVolumeId = Volume.VolumeId;
			Bay.LocalBounds = StructuralBounds;
			Bay.LocalBounds.Min[AxisIndex] = FMath::Lerp(
				StructuralBounds.Min[AxisIndex],
				StructuralBounds.Max[AxisIndex],
				static_cast<double>(BayIndex) / BayCount);
			Bay.LocalBounds.Max[AxisIndex] = FMath::Lerp(
				StructuralBounds.Min[AxisIndex],
				StructuralBounds.Max[AxisIndex],
				static_cast<double>(BayIndex + 1) / BayCount);
			Bay.PreferredAxis = AxisIndex == 0
				? EABTSM73BeamAFrameAxis::X
				: EABTSM73BeamAFrameAxis::Y;
		}
	}

	for (int32 A = 0; A < OutResult.Bays.Num(); ++A)
	{
		for (int32 B = A + 1; B < OutResult.Bays.Num(); ++B)
		{
			if (BaysTouch(
				OutResult.Bays[A].LocalBounds,
				OutResult.Bays[B].LocalBounds,
				Settings.JointMergeToleranceCM))
			{
				OutResult.Bays[A].AdjacentBayIds.Add(B);
				OutResult.Bays[B].AdjacentBayIds.Add(A);
			}
		}
	}

	FBuildContext Context;
	Context.Settings = &Settings;
	Context.Result = &OutResult;
	TArray<int32> BuildOrder;
	for (const FABTSM73BeamABay& Bay : OutResult.Bays)
	{
		BuildOrder.Add(Bay.BayId);
	}
	BuildOrder.Sort([&OutResult, &Silhouette](const int32 A, const int32 B)
	{
		const FABTSM73BeamABay& BayA = OutResult.Bays[A];
		const FABTSM73BeamABay& BayB = OutResult.Bays[B];
		const FABTSM73DAG5BV2Volume* VolumeA =
			Silhouette.Volumes.FindByPredicate(
				[&BayA](const FABTSM73DAG5BV2Volume& Candidate)
				{
					return Candidate.VolumeId == BayA.SourceVolumeId;
				});
		const FABTSM73DAG5BV2Volume* VolumeB =
			Silhouette.Volumes.FindByPredicate(
				[&BayB](const FABTSM73DAG5BV2Volume& Candidate)
				{
					return Candidate.VolumeId == BayB.SourceVolumeId;
				});
		const bool bBridgeA = VolumeA != nullptr
			&& IsSupportedSpanRole(VolumeA->Role);
		const bool bBridgeB = VolumeB != nullptr
			&& IsSupportedSpanRole(VolumeB->Role);
		if (bBridgeA != bBridgeB)
		{
			return !bBridgeA;
		}
		if (!FMath::IsNearlyEqual(BayA.LocalBounds.Min.Z, BayB.LocalBounds.Min.Z))
		{
			return BayA.LocalBounds.Min.Z < BayB.LocalBounds.Min.Z;
		}
		return A < B;
	});
	for (const int32 BayId : BuildOrder)
	{
		const FABTSM73BeamABay& Bay = OutResult.Bays[BayId];
		const FABTSM73DAG5BV2Volume* Volume =
			Silhouette.Volumes.FindByPredicate(
				[&Bay](const FABTSM73DAG5BV2Volume& Candidate)
				{
					return Candidate.VolumeId == Bay.SourceVolumeId;
				});
		if (Volume == nullptr)
		{
			return Reject(TEXT("BeamASourceVolumeMappingInvalid"));
		}
		FABTSM73BeamAAssembly& Assembly =
			OutResult.Assemblies.AddDefaulted_GetRef();
		Assembly.AssemblyId = OutResult.Assemblies.Num() - 1;
		Assembly.BayId = Bay.BayId;
		const bool bLayeredRoof =
			!IsSupportedSpanRole(Volume->Role)
			&& Volume->Primitive != EABTSM73DAG5BV2Primitive::Box;
		const bool bBuilt = bLayeredRoof
			? AddLayeredRoof(
				Context,
				Bay,
				Volume->Primitive,
				Assembly)
			: AddStackedFrame(Context, Bay, Assembly);
		Assembly.Type = bLayeredRoof
			? EABTSM73BeamAAssemblyType::LayeredRoofBay
			: EABTSM73BeamAAssemblyType::StackedFrameBay;
		if (!bBuilt)
		{
			if (OutResult.Joints.Num() >= Settings.MaxJointCount)
			{
				return Reject(TEXT("BeamAMaxJointCountExceeded"));
			}
			if (OutResult.Members.Num() >= Settings.MaxMemberCount)
			{
				return Reject(TEXT("BeamAMaxMemberCountExceeded"));
			}
			return Reject(
				bLayeredRoof
					? TEXT("BeamARoofCourseGenerationFailed")
					: TEXT("BeamAStackedFrameGenerationFailed"));
		}
	}

	FString BearingError;
	if (!CloseGeneratedAssembly(Settings, OutResult, BearingError))
	{
		return Reject(BearingError);
	}
	if (OutResult.BearingContacts.IsEmpty())
	{
		return Reject(TEXT("BeamANoBearingContacts"));
	}

	OutResult.Summary.SourceVolumeCount = Silhouette.Volumes.Num();
	OutResult.Summary.BayCount = OutResult.Bays.Num();
	OutResult.Summary.JointCount = OutResult.Joints.Num();
	OutResult.Summary.MemberCount = OutResult.Members.Num();
	OutResult.Summary.AssemblyCount = OutResult.Assemblies.Num();
	OutResult.Summary.BearingContactCount =
		OutResult.BearingContacts.Num();
	for (const FABTSM73BeamAMember& Member : OutResult.Members)
	{
		switch (Member.Axis)
		{
		case EABTSM73BeamAFrameAxis::X:
			++OutResult.Summary.XMemberCount;
			break;
		case EABTSM73BeamAFrameAxis::Y:
			++OutResult.Summary.YMemberCount;
			break;
		case EABTSM73BeamAFrameAxis::Z:
			++OutResult.Summary.ZMemberCount;
			break;
		case EABTSM73BeamAFrameAxis::Diagonal:
		default:
			++OutResult.Summary.DiagonalMemberCount;
			break;
		}
	}
	OutResult.Summary.BayGraphHash = static_cast<int64>(
		FCrc::StrCrc32(*CanonicalBays(OutResult.Bays)));
	OutResult.Summary.BeamGraphHash = static_cast<int64>(
		FCrc::StrCrc32(*CanonicalBeamGraph(OutResult)));
	OutResult.Summary.bAccepted = true;
	return true;
}
