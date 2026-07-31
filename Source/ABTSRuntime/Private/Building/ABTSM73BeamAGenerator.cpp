// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BeamAGenerator.h"

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
		const int32 RequestedCount,
		const double CrossSection)
	{
		TArray<double> Result;
		const double UsableMin = Minimum + CrossSection * 0.5;
		const double UsableMax = Maximum - CrossSection * 0.5;
		if (UsableMax < UsableMin)
		{
			return Result;
		}
		const int32 Count = FMath::Max(1, RequestedCount);
		if (Count == 1)
		{
			Result.Add((Minimum + Maximum) * 0.5);
			return Result;
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Result.Add(FMath::Lerp(
				UsableMin,
				UsableMax,
				static_cast<double>(Index) / (Count - 1)));
		}
		return Result;
	}

	bool AddHorizontalCourse(
		FBuildContext& Context,
		FABTSM73BeamAAssembly& Assembly,
		const FBox& Bounds,
		const double CenterZ,
		const EABTSM73BeamAFrameAxis Axis,
		const int32 BlockCount,
		const EABTSM73BeamAMemberRole Role)
	{
		const double CrossSection = Context.Settings->BlockCrossSectionCM;
		const int32 PerpendicularIndex =
			Axis == EABTSM73BeamAFrameAxis::X ? 1 : 0;
		const TArray<double> Offsets = CourseOffsets(
			Bounds.Min[PerpendicularIndex],
			Bounds.Max[PerpendicularIndex],
			BlockCount,
			CrossSection);
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
			if (AddMember(
				Context,
				Assembly,
				Center,
				Length,
				Axis,
				Role) == INDEX_NONE)
			{
				return false;
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
				const double PerpendicularSpan =
					Axis == EABTSM73BeamAFrameAxis::X ? Size.Y : Size.X;
				const int32 Blocks =
					PerpendicularSpan >= CrossSection * 2.0 ? 2 : 1;
				if (!AddHorizontalCourse(
					Context,
					Assembly,
					Bounds,
					Bounds.Min.Z + CrossSection * (CourseIndex + 0.5),
					Axis,
					Blocks,
					CourseIndex % 2 == 0
						? EABTSM73BeamAMemberRole::PrimaryBeam
						: EABTSM73BeamAMemberRole::SecondaryBeam))
				{
					return false;
				}
			}
			return true;
		}
		if (!AddHorizontalCourse(
			Context,
			Assembly,
			Bounds,
			Bounds.Min.Z + CrossSection * 0.5,
			Primary,
			2,
			EABTSM73BeamAMemberRole::PrimaryBeam)
			|| !AddHorizontalCourse(
				Context,
				Assembly,
				Bounds,
				Bounds.Min.Z + CrossSection * 1.5,
				Secondary,
				2,
				EABTSM73BeamAMemberRole::SecondaryBeam))
		{
			return false;
		}

		const TArray<double> XPositions = CourseOffsets(
			Bounds.Min.X,
			Bounds.Max.X,
			2,
			CrossSection);
		const TArray<double> YPositions = CourseOffsets(
			Bounds.Min.Y,
			Bounds.Max.Y,
			2,
			CrossSection);
		const double PostBottom = Bounds.Min.Z + CrossSection * 2.0;
		const double PostTop = Bounds.Max.Z - CrossSection * 2.0;
		const float PostLength = static_cast<float>(PostTop - PostBottom);
		if (PostLength < CrossSection)
		{
			return false;
		}
		for (const double X : XPositions)
		{
			for (const double Y : YPositions)
			{
				if (AddMember(
					Context,
					Assembly,
					FVector(X, Y, (PostBottom + PostTop) * 0.5),
					PostLength,
					EABTSM73BeamAFrameAxis::Z,
					EABTSM73BeamAMemberRole::Post) == INDEX_NONE)
				{
					return false;
				}
			}
		}
		return AddHorizontalCourse(
				Context,
				Assembly,
				Bounds,
				Bounds.Max.Z - CrossSection * 1.5,
				Primary,
				2,
				EABTSM73BeamAMemberRole::PrimaryBeam)
			&& AddHorizontalCourse(
				Context,
				Assembly,
				Bounds,
				Bounds.Max.Z - CrossSection * 0.5,
				Secondary,
				2,
				EABTSM73BeamAMemberRole::SecondaryBeam);
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
		int32 RequestedBlocks = FMath::Clamp(
			Context.Settings->RoofBlocksPerCourse,
			1,
			5);
		if (RequestedBlocks % 2 == 0)
		{
			--RequestedBlocks;
		}
		for (int32 CourseIndex = 0;
			CourseIndex < RequiredCourseCount;
			++CourseIndex)
		{
			const double Alpha =
				static_cast<double>(CourseIndex) / RequiredCourseCount;
			FBox CourseBounds = Bounds;
			const double MinHalfSpan = CrossSection * 0.55;
			const FVector Center = Bounds.GetCenter();
			if (Primitive == EABTSM73DAG5BV2Primitive::Pyramid
				|| Primitive
					== EABTSM73DAG5BV2Primitive::TriangularPrismX)
			{
				const double HalfX = FMath::Max(
					MinHalfSpan,
					Size.X * 0.5 * (1.0 - Alpha));
				CourseBounds.Min.X = Center.X - HalfX;
				CourseBounds.Max.X = Center.X + HalfX;
			}
			if (Primitive == EABTSM73DAG5BV2Primitive::Pyramid
				|| Primitive
					== EABTSM73DAG5BV2Primitive::TriangularPrismY)
			{
				const double HalfY = FMath::Max(
					MinHalfSpan,
					Size.Y * 0.5 * (1.0 - Alpha));
				CourseBounds.Min.Y = Center.Y - HalfY;
				CourseBounds.Max.Y = Center.Y + HalfY;
			}
			const EABTSM73BeamAFrameAxis Axis =
				CourseIndex % 2 == 0
					? Bay.PreferredAxis
					: OtherHorizontalAxis(Bay.PreferredAxis);
			const double PerpendicularSpan =
				Axis == EABTSM73BeamAFrameAxis::X
					? CourseBounds.GetSize().Y
					: CourseBounds.GetSize().X;
			const int32 BlockCount =
				PerpendicularSpan >= CrossSection * RequestedBlocks * 1.5
					? RequestedBlocks
					: 1;
			if (!AddHorizontalCourse(
				Context,
				Assembly,
				CourseBounds,
				Bounds.Min.Z + CrossSection * (CourseIndex + 0.5),
				Axis,
				BlockCount,
				EABTSM73BeamAMemberRole::RoofCourse))
			{
				return false;
			}
		}
		return true;
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
		|| !FMath::IsFinite(Settings.BlockCrossSectionCM)
		|| !FMath::IsFinite(Settings.JointMergeToleranceCM)
		|| Settings.TargetBaySpanCM <= 0.0f
		|| Settings.BlockCrossSectionCM <= 0.0f
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
		const FVector Size = Volume.LocalBounds.GetSize();
		const int32 AxisIndex = Size.X >= Size.Y ? 0 : 1;
		const float AxisSpan = static_cast<float>(Size[AxisIndex]);
		const int32 BayCount = FMath::Clamp(
			FMath::CeilToInt(AxisSpan / Settings.TargetBaySpanCM),
			1,
			Settings.MaxBaysPerVolume);
		if (OutResult.Bays.Num() + BayCount > Settings.MaxBayCount)
		{
			return Reject(TEXT("BeamAMaxBayCountExceeded"));
		}
		for (int32 BayIndex = 0; BayIndex < BayCount; ++BayIndex)
		{
			FABTSM73BeamABay& Bay = OutResult.Bays.AddDefaulted_GetRef();
			Bay.BayId = OutResult.Bays.Num() - 1;
			Bay.SourceVolumeId = Volume.VolumeId;
			Bay.LocalBounds = Volume.LocalBounds;
			const double AlphaMin = static_cast<double>(BayIndex) / BayCount;
			const double AlphaMax = static_cast<double>(BayIndex + 1) / BayCount;
			Bay.LocalBounds.Min[AxisIndex] = FMath::Lerp(
				Volume.LocalBounds.Min[AxisIndex],
				Volume.LocalBounds.Max[AxisIndex],
				AlphaMin);
			Bay.LocalBounds.Max[AxisIndex] = FMath::Lerp(
				Volume.LocalBounds.Min[AxisIndex],
				Volume.LocalBounds.Max[AxisIndex],
				AlphaMax);
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
	for (const FABTSM73BeamABay& Bay : OutResult.Bays)
	{
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
			Volume->Role != EABTSM73DAG5BV2VolumeRole::Bridge
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
	if (!BuildBearingContacts(Settings, OutResult, BearingError))
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
