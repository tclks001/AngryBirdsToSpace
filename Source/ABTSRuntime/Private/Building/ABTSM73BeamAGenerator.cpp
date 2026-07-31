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

	EABTSM73BeamAFrameAxis ClassifyAxis(
		const FVector& A,
		const FVector& B)
	{
		const FVector Delta = (B - A).GetAbs();
		const int32 ActiveAxisCount =
			(Delta.X > 0.1 ? 1 : 0)
			+ (Delta.Y > 0.1 ? 1 : 0)
			+ (Delta.Z > 0.1 ? 1 : 0);
		if (ActiveAxisCount > 1)
		{
			return EABTSM73BeamAFrameAxis::Diagonal;
		}
		if (Delta.X >= Delta.Y && Delta.X >= Delta.Z)
		{
			return EABTSM73BeamAFrameAxis::X;
		}
		if (Delta.Y >= Delta.Z)
		{
			return EABTSM73BeamAFrameAxis::Y;
		}
		return EABTSM73BeamAFrameAxis::Z;
	}

	int32 AddMember(
		FBuildContext& Context,
		const int32 JointA,
		const int32 JointB,
		const EABTSM73BeamAMemberRole Role)
	{
		if (JointA == INDEX_NONE || JointB == INDEX_NONE || JointA == JointB)
		{
			return INDEX_NONE;
		}
		const uint64 Key = MemberKey(JointA, JointB);
		if (const int32* Existing = Context.MemberByKey.Find(Key))
		{
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
		Member.Role = Role;
		Member.Axis = ClassifyAxis(
			Context.Result->Joints[JointA].LocalPosition,
			Context.Result->Joints[JointB].LocalPosition);
		Context.MemberByKey.Add(Key, Member.MemberId);
		return Member.MemberId;
	}

	bool AddAssemblyJoint(
		FBuildContext& Context,
		FABTSM73BeamAAssembly& Assembly,
		const FVector& Position,
		const EABTSM73BeamAJointRole Role,
		int32& OutJointId)
	{
		OutJointId = AddJoint(Context, Position, Role);
		if (OutJointId == INDEX_NONE)
		{
			return false;
		}
		Assembly.JointIds.AddUnique(OutJointId);
		return true;
	}

	bool AddAssemblyMember(
		FBuildContext& Context,
		FABTSM73BeamAAssembly& Assembly,
		const int32 JointA,
		const int32 JointB,
		const EABTSM73BeamAMemberRole Role)
	{
		const int32 MemberId = AddMember(Context, JointA, JointB, Role);
		if (MemberId == INDEX_NONE)
		{
			return false;
		}
		Assembly.MemberIds.AddUnique(MemberId);
		return true;
	}

	bool AddBoxFrame(
		FBuildContext& Context,
		const FABTSM73BeamABay& Bay,
		FABTSM73BeamAAssembly& Assembly)
	{
		const FBox& Box = Bay.LocalBounds;
		const FVector Positions[8] = {
			FVector(Box.Min.X, Box.Min.Y, Box.Min.Z),
			FVector(Box.Max.X, Box.Min.Y, Box.Min.Z),
			FVector(Box.Max.X, Box.Max.Y, Box.Min.Z),
			FVector(Box.Min.X, Box.Max.Y, Box.Min.Z),
			FVector(Box.Min.X, Box.Min.Y, Box.Max.Z),
			FVector(Box.Max.X, Box.Min.Y, Box.Max.Z),
			FVector(Box.Max.X, Box.Max.Y, Box.Max.Z),
			FVector(Box.Min.X, Box.Max.Y, Box.Max.Z)};
		int32 JointIds[8];
		for (int32 Index = 0; Index < 8; ++Index)
		{
			const EABTSM73BeamAJointRole Role = Index < 4
				? (Box.Min.Z <= 1.0
					? EABTSM73BeamAJointRole::GroundFoot
					: EABTSM73BeamAJointRole::BeamEnd)
				: EABTSM73BeamAJointRole::ColumnHead;
			if (!AddAssemblyJoint(
				Context,
				Assembly,
				Positions[Index],
				Role,
				JointIds[Index]))
			{
				return false;
			}
		}
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			if (!AddAssemblyMember(
				Context,
				Assembly,
				JointIds[Corner],
				JointIds[Corner + 4],
				EABTSM73BeamAMemberRole::Post))
			{
				return false;
			}
		}
		const int32 TopEdges[4][2] = {
			{4, 5}, {5, 6}, {6, 7}, {7, 4}};
		for (const int32* Edge : TopEdges)
		{
			const EABTSM73BeamAFrameAxis Axis = ClassifyAxis(
				Positions[Edge[0]],
				Positions[Edge[1]]);
			const EABTSM73BeamAMemberRole Role =
				Axis == Bay.PreferredAxis
					? EABTSM73BeamAMemberRole::PrimaryBeam
					: EABTSM73BeamAMemberRole::SecondaryBeam;
			if (!AddAssemblyMember(
				Context,
				Assembly,
				JointIds[Edge[0]],
				JointIds[Edge[1]],
				Role))
			{
				return false;
			}
		}
		return true;
	}

	bool AddRoofFrame(
		FBuildContext& Context,
		const FABTSM73BeamABay& Bay,
		const EABTSM73DAG5BV2Primitive Primitive,
		FABTSM73BeamAAssembly& Assembly)
	{
		const FBox& Box = Bay.LocalBounds;
		const FVector BasePositions[4] = {
			FVector(Box.Min.X, Box.Min.Y, Box.Min.Z),
			FVector(Box.Max.X, Box.Min.Y, Box.Min.Z),
			FVector(Box.Max.X, Box.Max.Y, Box.Min.Z),
			FVector(Box.Min.X, Box.Max.Y, Box.Min.Z)};
		int32 Base[4];
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (!AddAssemblyJoint(
				Context,
				Assembly,
				BasePositions[Index],
				EABTSM73BeamAJointRole::BeamEnd,
				Base[Index]))
			{
				return false;
			}
		}
		for (int32 EdgeIndex = 0; EdgeIndex < 4; ++EdgeIndex)
		{
			if (!AddAssemblyMember(
				Context,
				Assembly,
				Base[EdgeIndex],
				Base[(EdgeIndex + 1) % 4],
				EABTSM73BeamAMemberRole::SecondaryBeam))
			{
				return false;
			}
		}

		if (Primitive == EABTSM73DAG5BV2Primitive::Pyramid)
		{
			int32 Apex = INDEX_NONE;
			if (!AddAssemblyJoint(
				Context,
				Assembly,
				FVector(Box.GetCenter().X, Box.GetCenter().Y, Box.Max.Z),
				EABTSM73BeamAJointRole::RoofNode,
				Apex))
			{
				return false;
			}
			for (const int32 BaseJoint : Base)
			{
				if (!AddAssemblyMember(
					Context,
					Assembly,
					BaseJoint,
					Apex,
					EABTSM73BeamAMemberRole::RoofRafter))
				{
					return false;
				}
			}
			return true;
		}

		const bool bRidgeAlongY =
			Primitive == EABTSM73DAG5BV2Primitive::TriangularPrismX;
		const FVector RidgeA = bRidgeAlongY
			? FVector(Box.GetCenter().X, Box.Min.Y, Box.Max.Z)
			: FVector(Box.Min.X, Box.GetCenter().Y, Box.Max.Z);
		const FVector RidgeB = bRidgeAlongY
			? FVector(Box.GetCenter().X, Box.Max.Y, Box.Max.Z)
			: FVector(Box.Max.X, Box.GetCenter().Y, Box.Max.Z);
		int32 RidgeJointA = INDEX_NONE;
		int32 RidgeJointB = INDEX_NONE;
		if (!AddAssemblyJoint(
				Context,
				Assembly,
				RidgeA,
				EABTSM73BeamAJointRole::RoofNode,
				RidgeJointA)
			|| !AddAssemblyJoint(
				Context,
				Assembly,
				RidgeB,
				EABTSM73BeamAJointRole::RoofNode,
				RidgeJointB)
			|| !AddAssemblyMember(
				Context,
				Assembly,
				RidgeJointA,
				RidgeJointB,
				EABTSM73BeamAMemberRole::RoofRidge))
		{
			return false;
		}
		const int32 EndA[2] = {0, bRidgeAlongY ? 1 : 3};
		const int32 EndB[2] = {bRidgeAlongY ? 3 : 1, 2};
		for (const int32 BaseIndex : EndA)
		{
			if (!AddAssemblyMember(
				Context,
				Assembly,
				Base[BaseIndex],
				RidgeJointA,
				EABTSM73BeamAMemberRole::RoofRafter))
			{
				return false;
			}
		}
		for (const int32 BaseIndex : EndB)
		{
			if (!AddAssemblyMember(
				Context,
				Assembly,
				Base[BaseIndex],
				RidgeJointB,
				EABTSM73BeamAMemberRole::RoofRafter))
			{
				return false;
			}
		}
		return true;
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
		const TArray<FABTSM73BeamAJoint>& Joints,
		const TArray<FABTSM73BeamAMember>& Members,
		const TArray<FABTSM73BeamAAssembly>& Assemblies)
	{
		FString Text;
		for (const FABTSM73BeamAJoint& Joint : Joints)
		{
			Text += FString::Printf(
				TEXT("J%d:%.2f,%.2f,%.2f:R%d|"),
				Joint.JointId,
				Joint.LocalPosition.X,
				Joint.LocalPosition.Y,
				Joint.LocalPosition.Z,
				static_cast<int32>(Joint.Role));
		}
		for (const FABTSM73BeamAMember& Member : Members)
		{
			Text += FString::Printf(
				TEXT("M%d:%d-%d:A%d:R%d|"),
				Member.MemberId,
				Member.JointA,
				Member.JointB,
				static_cast<int32>(Member.Axis),
				static_cast<int32>(Member.Role));
		}
		for (const FABTSM73BeamAAssembly& Assembly : Assemblies)
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
	auto Reject = [&OutResult, &OutError](const TCHAR* Reason)
	{
		OutError = Reason;
		OutResult.Bays.Reset();
		OutResult.Joints.Reset();
		OutResult.Members.Reset();
		OutResult.Assemblies.Reset();
		OutResult.Summary = FABTSM73BeamAPreviewSummary();
		OutResult.Summary.RejectReason = Reason;
		return false;
	};

	if (!Silhouette.Summary.bAccepted || Silhouette.Volumes.IsEmpty())
	{
		return Reject(TEXT("BeamASilhouetteNotAccepted"));
	}
	if (!FMath::IsFinite(Settings.TargetBaySpanCM)
		|| !FMath::IsFinite(Settings.JointMergeToleranceCM)
		|| Settings.TargetBaySpanCM <= 0.0f
		|| Settings.JointMergeToleranceCM <= 0.0f
		|| Settings.MaxBaysPerVolume < 1
		|| Settings.MaxBayCount < 1
		|| Settings.MaxJointCount < 2
		|| Settings.MaxMemberCount < 1)
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
		const FABTSM73DAG5BV2Volume* Volume = Silhouette.Volumes.FindByPredicate(
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
		const bool bRoof =
			Volume->Role != EABTSM73DAG5BV2VolumeRole::Bridge
			&& Volume->Primitive != EABTSM73DAG5BV2Primitive::Box;
		if (bRoof)
		{
			Assembly.Type = EABTSM73BeamAAssemblyType::RoofFrameBay;
			if (!AddRoofFrame(Context, Bay, Volume->Primitive, Assembly))
			{
				return Reject(
					OutResult.Joints.Num() >= Settings.MaxJointCount
						? TEXT("BeamAMaxJointCountExceeded")
						: TEXT("BeamAMaxMemberCountExceeded"));
			}
		}
		else
		{
			Assembly.Type =
				Volume->Role == EABTSM73DAG5BV2VolumeRole::Bridge
					? EABTSM73BeamAAssemblyType::BridgeFrameBay
					: EABTSM73BeamAAssemblyType::CrossBeamBay;
			if (!AddBoxFrame(Context, Bay, Assembly))
			{
				return Reject(
					OutResult.Joints.Num() >= Settings.MaxJointCount
						? TEXT("BeamAMaxJointCountExceeded")
						: TEXT("BeamAMaxMemberCountExceeded"));
			}
		}
	}

	OutResult.Summary.SourceVolumeCount = Silhouette.Volumes.Num();
	OutResult.Summary.BayCount = OutResult.Bays.Num();
	OutResult.Summary.JointCount = OutResult.Joints.Num();
	OutResult.Summary.MemberCount = OutResult.Members.Num();
	OutResult.Summary.AssemblyCount = OutResult.Assemblies.Num();
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
		FCrc::StrCrc32(*CanonicalBeamGraph(
			OutResult.Joints,
			OutResult.Members,
			OutResult.Assemblies)));
	OutResult.Summary.bAccepted = true;
	return true;
}
