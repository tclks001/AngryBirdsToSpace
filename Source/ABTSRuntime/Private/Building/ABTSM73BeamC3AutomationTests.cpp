// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ABTSM73BeamD1BrickCompiler.h"

#include "ABTSM73BeamAGenerator.h"
#include "ABTSM73BeamC3CribCoreGenerator.h"
#include "ABTSM73BeamD0ProfileCatalog.h"

#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"

namespace ABTSM73BeamC3Tests
{
	const TArray<TPair<FName, int32>>& ProductionFixtures()
	{
		static const TArray<TPair<FName, int32>> Fixtures = {
			{TEXT("ColumnBreak"), 710000},
			{TEXT("SeamRelease"), 720000},
			{TEXT("TipOver"), 730000},
			{TEXT("DropTrigger"), 740000},
			{TEXT("SlideRelease"), 750137}};
		return Fixtures;
	}

	FABTSM73BeamD1Settings SettingsFor(
		const FName ProfileId,
		const int32 Seed,
		const int32 Tier)
	{
		FABTSM73BeamD1Settings Settings;
		Settings.GameplayProfileId = ProfileId;
		Settings.DifficultyTier = Tier;
		Settings.BuildingSeed = Seed;
		return Settings;
	}

	FABTSM73BeamAPreviewSettings SyntheticBeamASettings()
	{
		FABTSM73BeamAPreviewSettings Settings;
		Settings.BlockCrossSectionCM = 36.0f;
		Settings.JointMergeToleranceCM = 0.5f;
		Settings.MaximumVerticalSupportSpanCM = 720.0f;
		Settings.MaxJointCount = 512;
		Settings.MaxMemberCount = 256;
		Settings.MaxBearingContactCount = 512;
		Settings.MaxBearingPairChecks = 4096;
		return Settings;
	}

	FABTSM73BeamC3CribCoreSettings SyntheticCoreSettings()
	{
		FABTSM73BeamC3CribCoreSettings Settings;
		Settings.MaximumUnbracedCorePostSpanCM = 720.0f;
		Settings.MinimumCoreArmSpanCM = 144.0f;
		Settings.TargetBeltCount = 1;
		Settings.MaximumNetMemberIncrease = 32;
		Settings.MaximumFinalMemberCount = 64;
		Settings.BeamC2MemberReserve = 4;
		Settings.bAllowRoofLaneBudgetReallocation = false;
		return Settings;
	}

	int32 AddFixtureJoint(
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

	int32 AddFixtureMember(
		FABTSM73BeamAGenerationResult& Assembly,
		const int32 OwnerAssemblyId,
		const FVector& Start,
		const FVector& End,
		const EABTSM73BeamAFrameAxis Axis,
		const EABTSM73BeamAMemberRole Role)
	{
		FABTSM73BeamAMember& Member = Assembly.Members.AddDefaulted_GetRef();
		Member.MemberId = Assembly.Members.Num() - 1;
		Member.JointA = AddFixtureJoint(Assembly, Start,
			Start.Z <= 0.0 ? EABTSM73BeamAJointRole::GroundFoot
				: EABTSM73BeamAJointRole::BeamEnd);
		Member.JointB = AddFixtureJoint(Assembly, End,
			EABTSM73BeamAJointRole::ColumnHead);
		Member.Axis = Axis;
		Member.Role = Role;
		Member.LengthCM = FVector::Distance(Start, End);
		if (Assembly.Assemblies.IsValidIndex(OwnerAssemblyId))
		{
			FABTSM73BeamAAssembly& Owner = Assembly.Assemblies[OwnerAssemblyId];
			Owner.MemberIds.Add(Member.MemberId);
			Owner.JointIds.Add(Member.JointA);
			Owner.JointIds.Add(Member.JointB);
		}
		return Member.MemberId;
	}

	void RefreshSyntheticSummary(FABTSM73BeamAGenerationResult& Assembly)
	{
		FABTSM73BeamAPreviewSummary& Summary = Assembly.Summary;
		Summary.bAccepted = true;
		TSet<int32> SourceVolumeIds;
		for (const FABTSM73BeamABay& Bay : Assembly.Bays)
		{
			if (Bay.SourceVolumeId != INDEX_NONE)
			{
				SourceVolumeIds.Add(Bay.SourceVolumeId);
			}
		}
		Summary.SourceVolumeCount = SourceVolumeIds.Num();
		Summary.BayCount = Assembly.Bays.Num();
		Summary.JointCount = Assembly.Joints.Num();
		Summary.MemberCount = Assembly.Members.Num();
		Summary.AssemblyCount = Assembly.Assemblies.Num();
		Summary.BearingContactCount = Assembly.BearingContacts.Num();
		Summary.XMemberCount = 0;
		Summary.YMemberCount = 0;
		Summary.ZMemberCount = 0;
		Summary.DiagonalMemberCount = 0;
		for (const FABTSM73BeamAMember& Member : Assembly.Members)
		{
			switch (Member.Axis)
			{
			case EABTSM73BeamAFrameAxis::X: ++Summary.XMemberCount; break;
			case EABTSM73BeamAFrameAxis::Y: ++Summary.YMemberCount; break;
			case EABTSM73BeamAFrameAxis::Z: ++Summary.ZMemberCount; break;
			default: ++Summary.DiagonalMemberCount; break;
			}
		}
	}

	int32 AppendFixtureBayAssembly(
		FABTSM73BeamAGenerationResult& Assembly,
		const int32 SourceVolumeId,
		const FBox& Bounds,
		const EABTSM73BeamAAssemblyType Type =
			EABTSM73BeamAAssemblyType::StackedFrameBay)
	{
		FABTSM73BeamABay& Bay = Assembly.Bays.AddDefaulted_GetRef();
		Bay.BayId = Assembly.Bays.Num() - 1;
		Bay.SourceVolumeId = SourceVolumeId;
		Bay.LocalBounds = Bounds;
		Bay.PreferredAxis = EABTSM73BeamAFrameAxis::X;

		FABTSM73BeamAAssembly& MemberAssembly =
			Assembly.Assemblies.AddDefaulted_GetRef();
		MemberAssembly.AssemblyId = Assembly.Assemblies.Num() - 1;
		MemberAssembly.BayId = Bay.BayId;
		MemberAssembly.Type = Type;
		return MemberAssembly.AssemblyId;
	}

	FABTSM73BeamAGenerationResult BuildRectangularCoreFixture(
		const double HalfSpanCM = 180.0,
		const double HeightCM = 1200.0)
	{
		FABTSM73BeamAGenerationResult Result;
		FABTSM73BeamABay& Bay = Result.Bays.AddDefaulted_GetRef();
		Bay.BayId = 0;
		Bay.SourceVolumeId = 0;
		Bay.LocalBounds = FBox(
			FVector(-HalfSpanCM, -HalfSpanCM, 0.0),
			FVector(HalfSpanCM, HalfSpanCM, HeightCM));
		Bay.PreferredAxis = EABTSM73BeamAFrameAxis::X;

		FABTSM73BeamAAssembly& Frame = Result.Assemblies.AddDefaulted_GetRef();
		Frame.AssemblyId = 0;
		Frame.BayId = 0;
		Frame.Type = EABTSM73BeamAAssemblyType::StackedFrameBay;

		const FVector2D Stations[4] = {
			FVector2D(-HalfSpanCM, -HalfSpanCM),
			FVector2D(HalfSpanCM, -HalfSpanCM),
			FVector2D(-HalfSpanCM, HalfSpanCM),
			FVector2D(HalfSpanCM, HalfSpanCM)};
		for (const FVector2D& Station : Stations)
		{
			AddFixtureMember(Result, Frame.AssemblyId,
				FVector(Station.X, Station.Y, 0.0),
				FVector(Station.X, Station.Y, HeightCM),
				EABTSM73BeamAFrameAxis::Z,
				EABTSM73BeamAMemberRole::Post);
		}
		RefreshSyntheticSummary(Result);
		return Result;
	}

	FVector FixtureMemberCenter(
		const FABTSM73BeamAGenerationResult& Assembly,
		const FABTSM73BeamAMember& Member)
	{
		return (Assembly.Joints[Member.JointA].LocalPosition
			+ Assembly.Joints[Member.JointB].LocalPosition) * 0.5;
	}

	FBox FixtureMemberBounds(
		const FABTSM73BeamAGenerationResult& Assembly,
		const FABTSM73BeamAMember& Member,
		const double Section)
	{
		FVector Extent(Section * 0.5);
		Extent[static_cast<int32>(Member.Axis)] = Member.LengthCM * 0.5;
		const FVector Center = FixtureMemberCenter(Assembly, Member);
		return FBox(Center - Extent, Center + Extent);
	}

	FABTSM73BeamC3CribCoreHostPlan AppendClosedCoreHostFixture(
		FABTSM73BeamAGenerationResult& Assembly,
		const FVector2D& Center,
		const double HalfSpan,
		const double Height,
		const double BeltMidZ,
		const double Section)
	{
		FABTSM73BeamAAssembly& CoreAssembly =
			Assembly.Assemblies.AddDefaulted_GetRef();
		CoreAssembly.AssemblyId = Assembly.Assemblies.Num() - 1;
		CoreAssembly.BayId = 0;
		CoreAssembly.Type = EABTSM73BeamAAssemblyType::CribCore;
		FABTSM73BeamC3CribCoreHostPlan Plan;
		Plan.StationPositions = {
			FVector2D(Center.X - HalfSpan, Center.Y - HalfSpan),
			FVector2D(Center.X + HalfSpan, Center.Y - HalfSpan),
			FVector2D(Center.X - HalfSpan, Center.Y + HalfSpan),
			FVector2D(Center.X + HalfSpan, Center.Y + HalfSpan)};
		Plan.BeltMidZs = {BeltMidZ};
		Plan.MinimumZ = 0.0;
		Plan.MaximumZ = Height;
		Plan.BayId = 0;
		Plan.SourceVolumeId = 0;
		const double XCourseZ = BeltMidZ - Section * 0.5;
		const double YCourseZ = BeltMidZ + Section * 0.5;
		const double LowerPostTopZ = BeltMidZ - Section;
		const double UpperPostBottomZ = BeltMidZ + Section;
		for (const FVector2D& Station : Plan.StationPositions)
		{
			AddFixtureMember(Assembly, CoreAssembly.AssemblyId,
				FVector(Station.X, Station.Y, 0.0),
				FVector(Station.X, Station.Y, LowerPostTopZ),
				EABTSM73BeamAFrameAxis::Z,
				EABTSM73BeamAMemberRole::CorePost);
			AddFixtureMember(Assembly, CoreAssembly.AssemblyId,
				FVector(Station.X, Station.Y, UpperPostBottomZ),
				FVector(Station.X, Station.Y, Height),
				EABTSM73BeamAFrameAxis::Z,
				EABTSM73BeamAMemberRole::CorePost);
		}
		const double MinimumX = Center.X - HalfSpan - Section * 0.5;
		const double MaximumX = Center.X + HalfSpan + Section * 0.5;
		const double MinimumY = Center.Y - HalfSpan - Section * 0.5;
		const double MaximumY = Center.Y + HalfSpan + Section * 0.5;
		AddFixtureMember(Assembly, CoreAssembly.AssemblyId,
			FVector(MinimumX, Center.Y - HalfSpan, XCourseZ),
			FVector(MaximumX, Center.Y - HalfSpan, XCourseZ),
			EABTSM73BeamAFrameAxis::X,
			EABTSM73BeamAMemberRole::CoreCourse);
		AddFixtureMember(Assembly, CoreAssembly.AssemblyId,
			FVector(MinimumX, Center.Y + HalfSpan, XCourseZ),
			FVector(MaximumX, Center.Y + HalfSpan, XCourseZ),
			EABTSM73BeamAFrameAxis::X,
			EABTSM73BeamAMemberRole::CoreCourse);
		AddFixtureMember(Assembly, CoreAssembly.AssemblyId,
			FVector(Center.X - HalfSpan, MinimumY, YCourseZ),
			FVector(Center.X - HalfSpan, MaximumY, YCourseZ),
			EABTSM73BeamAFrameAxis::Y,
			EABTSM73BeamAMemberRole::CoreCourse);
		AddFixtureMember(Assembly, CoreAssembly.AssemblyId,
			FVector(Center.X + HalfSpan, MinimumY, YCourseZ),
			FVector(Center.X + HalfSpan, MaximumY, YCourseZ),
			EABTSM73BeamAFrameAxis::Y,
			EABTSM73BeamAMemberRole::CoreCourse);
		RefreshSyntheticSummary(Assembly);
		return Plan;
	}

	int32 CountMembers(
		const FABTSM73BeamAGenerationResult& Assembly,
		const EABTSM73BeamAMemberRole Role,
		const EABTSM73BeamAFrameAxis Axis)
	{
		int32 Count = 0;
		for (const FABTSM73BeamAMember& Member : Assembly.Members)
		{
			Count += Member.Role == Role && Member.Axis == Axis ? 1 : 0;
		}
		return Count;
	}

	bool IsCoreCrossBearing(
		const FABTSM73BeamAGenerationResult& Assembly,
		const FABTSM73BeamABearingContact& Contact)
	{
		if (!Assembly.Members.IsValidIndex(Contact.LowerMemberId)
			|| !Assembly.Members.IsValidIndex(Contact.UpperMemberId))
		{
			return false;
		}
		const FABTSM73BeamAMember& Lower =
			Assembly.Members[Contact.LowerMemberId];
		const FABTSM73BeamAMember& Upper =
			Assembly.Members[Contact.UpperMemberId];
		return Lower.Role == EABTSM73BeamAMemberRole::CoreCourse
			&& Upper.Role == EABTSM73BeamAMemberRole::CoreCourse
			&& Lower.Axis != Upper.Axis;
	}

	bool IsCorePostBearing(
		const FABTSM73BeamAGenerationResult& Assembly,
		const FABTSM73BeamABearingContact& Contact)
	{
		if (!Assembly.Members.IsValidIndex(Contact.LowerMemberId)
			|| !Assembly.Members.IsValidIndex(Contact.UpperMemberId))
		{
			return false;
		}
		const FABTSM73BeamAMember& Lower =
			Assembly.Members[Contact.LowerMemberId];
		const FABTSM73BeamAMember& Upper =
			Assembly.Members[Contact.UpperMemberId];
		const bool bLowerPost =
			Lower.Role == EABTSM73BeamAMemberRole::CorePost
			&& Lower.Axis == EABTSM73BeamAFrameAxis::Z;
		const bool bUpperPost =
			Upper.Role == EABTSM73BeamAMemberRole::CorePost
			&& Upper.Axis == EABTSM73BeamAFrameAxis::Z;
		const bool bLowerCourse =
			Lower.Role == EABTSM73BeamAMemberRole::CoreCourse
			&& (Lower.Axis == EABTSM73BeamAFrameAxis::X
				|| Lower.Axis == EABTSM73BeamAFrameAxis::Y);
		const bool bUpperCourse =
			Upper.Role == EABTSM73BeamAMemberRole::CoreCourse
			&& (Upper.Axis == EABTSM73BeamAFrameAxis::X
				|| Upper.Axis == EABTSM73BeamAFrameAxis::Y);
		return (bLowerPost && bUpperCourse)
			|| (bLowerCourse && bUpperPost);
	}

	int32 CountUniqueCoreCornerBearings(
		const FABTSM73BeamAGenerationResult& Assembly,
		const FABTSM73BeamC3CribCoreHostPlan& Plan,
		const double Tolerance)
	{
		TSet<int32> ContactIds;
		for (const double MidZ : Plan.BeltMidZs)
		{
			for (const FVector2D& Station : Plan.StationPositions)
			{
				for (const FABTSM73BeamABearingContact& Contact :
					Assembly.BearingContacts)
				{
					if (IsCoreCrossBearing(Assembly, Contact)
						&& FVector2D::Distance(
							FVector2D(Contact.LocalPosition.X,
								Contact.LocalPosition.Y), Station)
							<= Tolerance * 2.0
						&& FMath::Abs(Contact.LocalPosition.Z - MidZ)
							<= Tolerance * 2.0)
					{
						ContactIds.Add(Contact.ContactId);
					}
				}
			}
		}
		return ContactIds.Num();
	}

	int32 CountUniqueCorePostBearings(
		const FABTSM73BeamAGenerationResult& Assembly,
		const FABTSM73BeamC3CribCoreHostPlan& Plan,
		const double Section,
		const double Tolerance)
	{
		TSet<int32> ContactIds;
		for (const double MidZ : Plan.BeltMidZs)
		{
			for (const FVector2D& Station : Plan.StationPositions)
			{
				for (const FABTSM73BeamABearingContact& Contact :
					Assembly.BearingContacts)
				{
					const bool bAtBeltFace =
						FMath::Abs(Contact.LocalPosition.Z - (MidZ - Section))
							<= Tolerance * 2.0
						|| FMath::Abs(Contact.LocalPosition.Z - (MidZ + Section))
							<= Tolerance * 2.0;
					if (bAtBeltFace && IsCorePostBearing(Assembly, Contact)
						&& FVector2D::Distance(
							FVector2D(Contact.LocalPosition.X,
								Contact.LocalPosition.Y), Station)
							<= Tolerance * 2.0)
					{
						ContactIds.Add(Contact.ContactId);
					}
				}
			}
		}
		return ContactIds.Num();
	}

	uint32 AssemblyFingerprint(
		const FABTSM73BeamAGenerationResult& Assembly)
	{
		FString Signature = FString::Printf(
			TEXT("S=%d:%d:%d:%d:%d:%d|"),
			Assembly.Summary.bAccepted ? 1 : 0,
			Assembly.Summary.SourceVolumeCount,
			Assembly.Summary.BayCount,
			Assembly.Summary.JointCount,
			Assembly.Summary.MemberCount,
			Assembly.Summary.AssemblyCount);
		for (const FABTSM73BeamABay& Bay : Assembly.Bays)
		{
			Signature += FString::Printf(
				TEXT("B=%d:%d:%.3f:%.3f:%.3f:%.3f:%.3f:%.3f:%d|"),
				Bay.BayId, Bay.SourceVolumeId,
				Bay.LocalBounds.Min.X, Bay.LocalBounds.Min.Y,
				Bay.LocalBounds.Min.Z, Bay.LocalBounds.Max.X,
				Bay.LocalBounds.Max.Y, Bay.LocalBounds.Max.Z,
				static_cast<int32>(Bay.PreferredAxis));
			for (const int32 Neighbor : Bay.AdjacentBayIds)
			{
				Signature += FString::Printf(TEXT("N=%d|"), Neighbor);
			}
		}
		for (const FABTSM73BeamAJoint& Joint : Assembly.Joints)
		{
			Signature += FString::Printf(TEXT("J=%d:%.3f:%.3f:%.3f:%d|"),
				Joint.JointId, Joint.LocalPosition.X, Joint.LocalPosition.Y,
				Joint.LocalPosition.Z, static_cast<int32>(Joint.Role));
		}
		for (const FABTSM73BeamAMember& Member : Assembly.Members)
		{
			Signature += FString::Printf(TEXT("M=%d:%d:%d:%d:%d:%.3f|"),
				Member.MemberId, Member.JointA, Member.JointB,
				static_cast<int32>(Member.Axis),
				static_cast<int32>(Member.Role), Member.LengthCM);
		}
		for (const FABTSM73BeamABearingContact& Contact :
			Assembly.BearingContacts)
		{
			Signature += FString::Printf(
				TEXT("C=%d:%d:%d:%d:%.3f:%.3f:%.3f:%.3f|"),
				Contact.ContactId, Contact.LowerMemberId, Contact.UpperMemberId,
				static_cast<int32>(Contact.Type), Contact.LocalPosition.X,
				Contact.LocalPosition.Y, Contact.LocalPosition.Z,
				Contact.ContactAreaCM2);
		}
		for (const FABTSM73BeamAAssembly& MemberAssembly : Assembly.Assemblies)
		{
			Signature += FString::Printf(TEXT("A=%d:%d:%d|"),
				MemberAssembly.AssemblyId, MemberAssembly.BayId,
				static_cast<int32>(MemberAssembly.Type));
			for (const int32 JointId : MemberAssembly.JointIds)
			{
				Signature += FString::Printf(TEXT("AJ=%d|"), JointId);
			}
			for (const int32 MemberId : MemberAssembly.MemberIds)
			{
				Signature += FString::Printf(TEXT("AM=%d|"), MemberId);
			}
		}
		for (const FABTSM73BeamASupportVoid& SupportVoid :
			Assembly.ReservedSupportVoids)
		{
			Signature += FString::Printf(
				TEXT("V=%d:%d:%.3f:%.3f:%.3f:%.3f:%.3f:%.3f|"),
				SupportVoid.SpanAxisIndex, SupportVoid.SpanSourceVolumeId,
				SupportVoid.Bounds.Min.X, SupportVoid.Bounds.Min.Y,
				SupportVoid.Bounds.Min.Z, SupportVoid.Bounds.Max.X,
				SupportVoid.Bounds.Max.Y, SupportVoid.Bounds.Max.Z);
		}
		return FCrc::StrCrc32(*Signature);
	}

	void RenumberContacts(FABTSM73BeamAGenerationResult& Assembly)
	{
		for (int32 Index = 0; Index < Assembly.BearingContacts.Num(); ++Index)
		{
			Assembly.BearingContacts[Index].ContactId = Index;
		}
		Assembly.Summary.BearingContactCount = Assembly.BearingContacts.Num();
	}

	bool RemoveFixtureMemberAndRebuildContacts(
		FABTSM73BeamAGenerationResult& Assembly,
		const int32 RemovedMemberId,
		const FABTSM73BeamAPreviewSettings& BeamASettings,
		FString& OutError)
	{
		OutError.Reset();
		if (!Assembly.Members.IsValidIndex(RemovedMemberId)
			|| Assembly.Members[RemovedMemberId].MemberId != RemovedMemberId)
		{
			OutError = FString::Printf(
				TEXT("InvalidFixtureMemberId:%d"), RemovedMemberId);
			return false;
		}
		for (int32 Index = 0; Index < Assembly.Members.Num(); ++Index)
		{
			if (Assembly.Members[Index].MemberId != Index)
			{
				OutError = FString::Printf(
					TEXT("NonCanonicalFixtureMemberId:%d!=%d"),
					Assembly.Members[Index].MemberId, Index);
				return false;
			}
		}

		TArray<int32> OldToNew;
		OldToNew.Init(INDEX_NONE, Assembly.Members.Num());
		TArray<FABTSM73BeamAMember> Kept;
		Kept.Reserve(Assembly.Members.Num() - 1);
		for (const FABTSM73BeamAMember& Member : Assembly.Members)
		{
			if (Member.MemberId == RemovedMemberId)
			{
				continue;
			}
			FABTSM73BeamAMember Copy = Member;
			Copy.MemberId = Kept.Num();
			OldToNew[Member.MemberId] = Copy.MemberId;
			Kept.Add(Copy);
		}
		Assembly.Members = MoveTemp(Kept);

		for (FABTSM73BeamAAssembly& MemberAssembly : Assembly.Assemblies)
		{
			TArray<int32> Remapped;
			Remapped.Reserve(MemberAssembly.MemberIds.Num());
			for (const int32 OldId : MemberAssembly.MemberIds)
			{
				if (OldToNew.IsValidIndex(OldId)
					&& OldToNew[OldId] != INDEX_NONE)
				{
					Remapped.AddUnique(OldToNew[OldId]);
				}
			}
			MemberAssembly.MemberIds = MoveTemp(Remapped);
		}

		Assembly.BearingContacts.Reset();
		if (!ABTSM73BeamA::RebuildBearingContacts(
			BeamASettings, Assembly, OutError))
		{
			return false;
		}
		RefreshSyntheticSummary(Assembly);
		OutError.Reset();
		return true;
	}

	struct FRootedFloorNetworkFixture
	{
		FABTSM73BeamAGenerationResult Assembly;
		FABTSM73BeamC3CribCoreResult CertifiedPlan;
		FVector2D PeripheralStation = FVector2D::ZeroVector;
		int32 LeftAnchorCourseId = INDEX_NONE;
		int32 RightAnchorCourseId = INDEX_NONE;
		int32 PeripheralXCourseId = INDEX_NONE;
		int32 PeripheralYCourseId = INDEX_NONE;
	};

	int32 CountDirectCoreCourseContacts(
		const FABTSM73BeamAGenerationResult& Assembly,
		const int32 OrdinaryMemberId)
	{
		int32 Count = 0;
		for (const FABTSM73BeamABearingContact& Contact :
			Assembly.BearingContacts)
		{
			const int32 OtherId = Contact.LowerMemberId == OrdinaryMemberId
				? Contact.UpperMemberId
				: Contact.UpperMemberId == OrdinaryMemberId
					? Contact.LowerMemberId : INDEX_NONE;
			if (Assembly.Members.IsValidIndex(OtherId)
				&& Assembly.Members[OtherId].Role
					== EABTSM73BeamAMemberRole::CoreCourse)
			{
				++Count;
			}
		}
		return Count;
	}

	int32 CountHorizontalContactNeighbors(
		const FABTSM73BeamAGenerationResult& Assembly,
		const int32 MemberId)
	{
		TSet<int32> Neighbors;
		for (const FABTSM73BeamABearingContact& Contact :
			Assembly.BearingContacts)
		{
			const int32 OtherId = Contact.LowerMemberId == MemberId
				? Contact.UpperMemberId
				: Contact.UpperMemberId == MemberId
					? Contact.LowerMemberId : INDEX_NONE;
			if (!Assembly.Members.IsValidIndex(OtherId))
			{
				continue;
			}
			const EABTSM73BeamAFrameAxis Axis =
				Assembly.Members[OtherId].Axis;
			if (Axis == EABTSM73BeamAFrameAxis::X
				|| Axis == EABTSM73BeamAFrameAxis::Y)
			{
				Neighbors.Add(OtherId);
			}
		}
		return Neighbors.Num();
	}

	bool BuildRootedFloorNetworkFixture(
		const FABTSM73BeamAPreviewSettings& BeamASettings,
		const FABTSM73BeamC3CribCoreSettings& CoreSettings,
		FRootedFloorNetworkFixture& OutFixture,
		FString& OutError)
	{
		OutFixture = FRootedFloorNetworkFixture();
		OutFixture.Assembly = BuildRectangularCoreFixture();
		FABTSM73BeamC3CribCoreGenerator Generator;
		if (!Generator.Generate(CoreSettings, BeamASettings,
			OutFixture.Assembly, OutFixture.CertifiedPlan, OutError)
			|| !Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
				OutFixture.Assembly, OutFixture.CertifiedPlan, OutError))
		{
			return false;
		}
		if (OutFixture.CertifiedPlan.HostPlans.Num() != 1
			|| OutFixture.CertifiedPlan.HostPlans[0].StationPositions.Num() != 4
			|| OutFixture.CertifiedPlan.HostPlans[0].BeltMidZs.IsEmpty())
		{
			OutError = TEXT("RootedFloorFixtureMissingFourPostHost");
			return false;
		}

		const FABTSM73BeamC3CribCoreHostPlan& Host =
			OutFixture.CertifiedPlan.HostPlans[0];
		const double Section = BeamASettings.BlockCrossSectionCM;
		double MinimumX = TNumericLimits<double>::Max();
		double MaximumX = -TNumericLimits<double>::Max();
		double MinimumY = TNumericLimits<double>::Max();
		double MaximumY = -TNumericLimits<double>::Max();
		for (const FVector2D& Station : Host.StationPositions)
		{
			MinimumX = FMath::Min(MinimumX, Station.X);
			MaximumX = FMath::Max(MaximumX, Station.X);
			MinimumY = FMath::Min(MinimumY, Station.Y);
			MaximumY = FMath::Max(MaximumY, Station.Y);
		}
		const double CenterX = (MinimumX + MaximumX) * 0.5;
		const double BeltMidZ = Host.BeltMidZs[0];
		const double LowerCourseZ = BeltMidZ + Section * 1.5;
		const double UpperCourseZ = BeltMidZ + Section * 2.5;
		const double FarY = MaximumY + 360.0;
		OutFixture.PeripheralStation = FVector2D(CenterX, FarY);
		constexpr double PeripheralHeightCM = 1008.47;
		const double LowerPostTopZ = BeltMidZ + Section;
		const double UpperPostBottomZ = BeltMidZ + Section * 3.0;
		if (LowerPostTopZ > CoreSettings.MaximumUnbracedCorePostSpanCM
			|| PeripheralHeightCM - UpperPostBottomZ
				> CoreSettings.MaximumUnbracedCorePostSpanCM)
		{
			OutError = TEXT("RootedFloorFixturePostPieceExceedsLimit");
			return false;
		}

		OutFixture.Assembly.Bays[0].LocalBounds.Max.Y = FMath::Max(
			OutFixture.Assembly.Bays[0].LocalBounds.Max.Y, FarY + Section);
		AddFixtureMember(OutFixture.Assembly, 0,
			FVector(CenterX, FarY, 0.0),
			FVector(CenterX, FarY, LowerPostTopZ),
			EABTSM73BeamAFrameAxis::Z, EABTSM73BeamAMemberRole::Post);
		AddFixtureMember(OutFixture.Assembly, 0,
			FVector(CenterX, FarY, UpperPostBottomZ),
			FVector(CenterX, FarY, PeripheralHeightCM),
			EABTSM73BeamAFrameAxis::Z, EABTSM73BeamAMemberRole::Post);

		// The five ordinary courses form one alternating X/Y backbone between
		// two different host Y courses. The peripheral X/Y pair lies in the
		// middle of that path, so leaf pruning cannot retain it from one anchor.
		OutFixture.LeftAnchorCourseId = AddFixtureMember(
			OutFixture.Assembly, 0,
			FVector(MinimumX - Section * 0.5, MinimumY, LowerCourseZ),
			FVector(CenterX + Section * 0.5, MinimumY, LowerCourseZ),
			EABTSM73BeamAFrameAxis::X,
			EABTSM73BeamAMemberRole::PrimaryBeam);
		OutFixture.PeripheralYCourseId = AddFixtureMember(
			OutFixture.Assembly, 0,
			FVector(CenterX, MinimumY - Section * 0.5, UpperCourseZ),
			FVector(CenterX, FarY + Section * 0.5, UpperCourseZ),
			EABTSM73BeamAFrameAxis::Y,
			EABTSM73BeamAMemberRole::PrimaryBeam);
		OutFixture.PeripheralXCourseId = AddFixtureMember(
			OutFixture.Assembly, 0,
			FVector(CenterX - Section * 0.5, FarY, LowerCourseZ),
			FVector(MaximumX + Section * 0.5, FarY, LowerCourseZ),
			EABTSM73BeamAFrameAxis::X,
			EABTSM73BeamAMemberRole::PrimaryBeam);
		AddFixtureMember(OutFixture.Assembly, 0,
			FVector(MaximumX, MaximumY - Section * 0.5, UpperCourseZ),
			FVector(MaximumX, FarY + Section * 0.5, UpperCourseZ),
			EABTSM73BeamAFrameAxis::Y,
			EABTSM73BeamAMemberRole::PrimaryBeam);
		OutFixture.RightAnchorCourseId = AddFixtureMember(
			OutFixture.Assembly, 0,
			FVector(CenterX - Section * 0.5, MaximumY, LowerCourseZ),
			FVector(MaximumX + Section * 0.5, MaximumY, LowerCourseZ),
			EABTSM73BeamAFrameAxis::X,
			EABTSM73BeamAMemberRole::PrimaryBeam);

		RefreshSyntheticSummary(OutFixture.Assembly);
		if (!ABTSM73BeamA::RebuildBearingContacts(
			BeamASettings, OutFixture.Assembly, OutError))
		{
			return false;
		}
		OutError.Reset();
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3LowTierProductionMatrixTest,
	"ABTS.M73DAG.BeamC3.LowTierProductionMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3LowTierProductionMatrixTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3Tests;
	FABTSM73BeamD1BrickCompiler Compiler;
	for (const TPair<FName, int32>& Fixture : ProductionFixtures())
	{
		for (int32 Tier = 0; Tier <= 1; ++Tier)
		{
			const FString Identity = FString::Printf(TEXT("%s Tier %d"),
				*Fixture.Key.ToString(), Tier);
			FABTSM73BeamD0ResolvedProfile Resolved;
			FString Error;
			const bool bResolved =
				FABTSM73BeamD0ProfileCatalog::GetDefault().Resolve(
					Fixture.Key, Tier, Fixture.Value, Resolved, Error);
			TestTrue(*FString::Printf(TEXT("%s resolves: %s"),
				*Identity, *Error), bResolved);
			if (!bResolved)
			{
				continue;
			}

			FABTSM73BeamD1GenerationResult Result;
			Error.Reset();
			const bool bGenerated = Compiler.Generate(
				SettingsFor(Fixture.Key, Fixture.Value, Tier), Result, Error);
			TestTrue(*FString::Printf(TEXT("%s compiles: %s"),
				*Identity, *Error), bGenerated);
			if (!bGenerated)
			{
				continue;
			}
			AddInfo(FString::Printf(
				TEXT("Beam-C3 %s Bricks=%d Hosts=%d Belts=%d Ties=%d Rooted=%d Net=%d Max=%.2f->%.2f Attempt=%d Resolved=%lld Plan=%lld Evidence=%lld Brick=%lld"),
				*Identity, Result.Summary.BrickCount,
				Result.Summary.StabilityCoreHostCount,
				Result.Summary.StabilityCoreBeltCount,
				Result.Summary.StabilityCoreTieCourseCount,
				Result.Summary.StabilityRootedExistingCourseCount,
				Result.Summary.StabilityCoreNetMemberDelta,
				Result.Summary.MaximumUnbracedCorePostSpanBeforeCM,
				Result.Summary.MaximumUnbracedCorePostSpanAfterCM,
				Result.Summary.VisualCandidateAttempt,
				Result.Summary.ResolvedSettingsHash,
				Result.Summary.StabilityCorePlanHash,
				Result.Summary.StabilityRootedEvidenceHash,
				Result.Summary.BrickGeometryHash));

			TestTrue(*FString::Printf(TEXT("%s result is accepted"), *Identity),
				Result.Summary.bAccepted);
			TestEqual(*FString::Printf(TEXT("%s preserves Profile identity"),
				*Identity), Result.Summary.GameplayProfileId, Fixture.Key);
			TestEqual(*FString::Printf(TEXT("%s preserves Tier identity"),
				*Identity), Result.Summary.DifficultyTier, Tier);
			TestEqual(*FString::Printf(TEXT("%s resolves the expected M7 profile"),
				*Identity), Result.Summary.ResolvedM7ProfileId,
				Resolved.ResolvedM7ProfileId);
			TestTrue(*FString::Printf(TEXT("%s visual complexity is certified"),
				*Identity), Result.Summary.bVisualComplexityCertified);
			TestTrue(*FString::Printf(TEXT("%s assembly quality is certified"),
				*Identity), Result.Summary.bAssemblyQualityCertified);
			TestTrue(*FString::Printf(TEXT("%s stability core is certified"),
				*Identity), Result.Summary.bStabilityCoreCertified);
			TestTrue(*FString::Printf(TEXT("%s resolved hash is non-zero"),
				*Identity), Result.Summary.ResolvedSettingsHash != 0);
			TestTrue(*FString::Printf(TEXT("%s upstream hash is non-zero"),
				*Identity), Result.Summary.UpstreamBeamHash != 0);
			TestTrue(*FString::Printf(TEXT("%s core plan hash is non-zero"),
				*Identity), Result.Summary.StabilityCorePlanHash != 0);
			TestTrue(*FString::Printf(
				TEXT("%s rooted stability evidence hash is non-zero"),
				*Identity), Result.Summary.StabilityRootedEvidenceHash != 0);
			TestTrue(*FString::Printf(TEXT("%s Brick hash is non-zero"),
				*Identity), Result.Summary.BrickGeometryHash != 0);
			TestTrue(*FString::Printf(TEXT("%s has a core host"), *Identity),
				Result.Summary.StabilityCoreHostCount >= 1);
			TestTrue(*FString::Printf(TEXT("%s gives every host a belt"),
				*Identity), Result.Summary.StabilityCoreBeltCount
					>= Result.Summary.StabilityCoreHostCount);
			if (Fixture.Key == FName(TEXT("ColumnBreak")) && Tier == 1)
			{
				TestTrue(TEXT("ColumnBreak Tier 1 keeps its rooted targeted tie"),
					Result.Summary.StabilityCoreTieCourseCount >= 1);
			}
			TestTrue(*FString::Printf(TEXT("%s consumes or reuses core members"),
				*Identity), Result.Summary.ReusedStabilityCoreMemberCount
					+ Result.Summary.InsertedStabilityCoreMemberCount > 0);
			TestTrue(*FString::Printf(TEXT("%s satisfies the all-Z hard gate"),
				*Identity), Result.Summary.MaximumUnbracedCorePostSpanAfterCM
					<= Resolved.StabilityCore.MaximumUnbracedCorePostSpanCM + 0.01f);
			TestTrue(*FString::Printf(TEXT("%s preserves the core net budget"),
				*Identity), Result.Summary.StabilityCoreNetMemberDelta
					<= Resolved.StabilityCore.MaximumNetMemberIncrease);
			TestTrue(*FString::Printf(TEXT("%s preserves the final C3 budget"),
				*Identity), Result.Summary.BrickCount
					<= Resolved.StabilityCore.MaximumFinalMemberCount);
			TestEqual(*FString::Printf(TEXT("%s publishes the tier minimum"),
				*Identity), Result.Summary.TargetMinimumBrickCount,
				Resolved.VisualComplexity.MinimumBrickCount);
			TestEqual(*FString::Printf(TEXT("%s publishes the tier maximum"),
				*Identity), Result.Summary.TargetMaximumBrickCount,
				Resolved.VisualComplexity.MaximumBrickCount);
			TestTrue(*FString::Printf(TEXT("%s reaches the Brick minimum"),
				*Identity), Result.Summary.BrickCount
					>= Result.Summary.TargetMinimumBrickCount);
			TestTrue(*FString::Printf(TEXT("%s stays below the Brick maximum"),
				*Identity), Result.Summary.BrickCount
					<= Result.Summary.TargetMaximumBrickCount);
			TestTrue(*FString::Printf(TEXT("%s accepts a bounded attempt"),
				*Identity), Result.Summary.VisualCandidateAttempt >= 0
					&& Result.Summary.VisualCandidateAttempt
						< Resolved.VisualComplexity.MaximumCandidateAttempts);
			TestEqual(*FString::Printf(TEXT("%s binds one Brick per Member"),
				*Identity), Result.Summary.BrickCount, Result.Summary.MemberCount);
			TestEqual(*FString::Printf(TEXT("%s retains complete references"),
				*Identity), Result.Summary.CompleteReferenceCount,
				Result.Summary.MemberCount);
			TestEqual(*FString::Printf(TEXT("%s has no Brick penetration"),
				*Identity), Result.Summary.StrictPenetrationCount, 0);
			TestEqual(*FString::Printf(TEXT("%s has no contact mismatch"),
				*Identity), Result.Summary.RealContactMismatchCount, 0);
			TestEqual(*FString::Printf(TEXT("%s has no support violation"),
				*Identity), Result.Summary.RemainingSupportViolationCount, 0);
			TestEqual(*FString::Printf(TEXT("%s retains one weakness candidate"),
				*Identity), Result.Summary.WeaknessCandidateCount, 1);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3ProductionDeterminismTest,
	"ABTS.M73DAG.BeamC3.ProductionDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3ProductionDeterminismTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3Tests;
	FABTSM73BeamD1BrickCompiler Compiler;
	for (const TPair<FName, int32>& Fixture : ProductionFixtures())
	{
		for (int32 Tier = 0; Tier <= 1; ++Tier)
		{
			const FString Identity = FString::Printf(TEXT("%s Tier %d"),
				*Fixture.Key.ToString(), Tier);
			const FABTSM73BeamD1Settings Settings = SettingsFor(
				Fixture.Key, Fixture.Value, Tier);
			FABTSM73BeamD1GenerationResult A;
			FABTSM73BeamD1GenerationResult B;
			FString ErrorA;
			FString ErrorB;
			const bool bGeneratedA = Compiler.Generate(Settings, A, ErrorA);
			const bool bGeneratedB = Compiler.Generate(Settings, B, ErrorB);
			TestTrue(*FString::Printf(TEXT("%s first compile: %s"),
				*Identity, *ErrorA), bGeneratedA);
			TestTrue(*FString::Printf(TEXT("%s second compile: %s"),
				*Identity, *ErrorB), bGeneratedB);
			if (!bGeneratedA || !bGeneratedB)
			{
				continue;
			}
			TestEqual(*FString::Printf(TEXT("%s accepted attempt is deterministic"),
				*Identity), A.Summary.VisualCandidateAttempt,
				B.Summary.VisualCandidateAttempt);
			TestEqual(*FString::Printf(TEXT("%s resolved settings are deterministic"),
				*Identity), A.Summary.ResolvedSettingsHash,
				B.Summary.ResolvedSettingsHash);
			TestEqual(*FString::Printf(TEXT("%s host count is deterministic"),
				*Identity), A.Summary.StabilityCoreHostCount,
				B.Summary.StabilityCoreHostCount);
			TestEqual(*FString::Printf(TEXT("%s belt count is deterministic"),
				*Identity), A.Summary.StabilityCoreBeltCount,
				B.Summary.StabilityCoreBeltCount);
			TestEqual(*FString::Printf(TEXT("%s rooted tie count is deterministic"),
				*Identity), A.Summary.StabilityCoreTieCourseCount,
				B.Summary.StabilityCoreTieCourseCount);
			TestEqual(*FString::Printf(TEXT("%s reused members are deterministic"),
				*Identity), A.Summary.ReusedStabilityCoreMemberCount,
				B.Summary.ReusedStabilityCoreMemberCount);
			TestEqual(*FString::Printf(TEXT("%s inserted members are deterministic"),
				*Identity), A.Summary.InsertedStabilityCoreMemberCount,
				B.Summary.InsertedStabilityCoreMemberCount);
			TestEqual(*FString::Printf(TEXT("%s net delta is deterministic"),
				*Identity), A.Summary.StabilityCoreNetMemberDelta,
				B.Summary.StabilityCoreNetMemberDelta);
			TestEqual(*FString::Printf(TEXT("%s core plan is deterministic"),
				*Identity), A.Summary.StabilityCorePlanHash,
				B.Summary.StabilityCorePlanHash);
			TestEqual(*FString::Printf(
				TEXT("%s rooted course count is deterministic"), *Identity),
				A.Summary.StabilityRootedExistingCourseCount,
				B.Summary.StabilityRootedExistingCourseCount);
			TestEqual(*FString::Printf(
				TEXT("%s rooted evidence is deterministic"), *Identity),
				A.Summary.StabilityRootedEvidenceHash,
				B.Summary.StabilityRootedEvidenceHash);
			TestEqual(*FString::Printf(TEXT("%s upstream graph is deterministic"),
				*Identity), A.Summary.UpstreamBeamHash,
				B.Summary.UpstreamBeamHash);
			TestEqual(*FString::Printf(TEXT("%s Brick geometry is deterministic"),
				*Identity), A.Summary.BrickGeometryHash,
				B.Summary.BrickGeometryHash);
			TestEqual(*FString::Printf(TEXT("%s Brick count is deterministic"),
				*Identity), A.Summary.BrickCount, B.Summary.BrickCount);
			TestTrue(*FString::Printf(TEXT("%s pre-core span is deterministic"),
				*Identity), FMath::IsNearlyEqual(
				A.Summary.MaximumUnbracedCorePostSpanBeforeCM,
				B.Summary.MaximumUnbracedCorePostSpanBeforeCM, 0.001f));
			TestTrue(*FString::Printf(TEXT("%s final span is deterministic"),
				*Identity), FMath::IsNearlyEqual(
				A.Summary.MaximumUnbracedCorePostSpanAfterCM,
				B.Summary.MaximumUnbracedCorePostSpanAfterCM, 0.001f));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3SyntheticFourPostClosedLoopTest,
	"ABTS.M73DAG.BeamC3.SyntheticFourPostClosedLoop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3SyntheticFourPostClosedLoopTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3Tests;
	const FABTSM73BeamAPreviewSettings BeamASettings =
		SyntheticBeamASettings();
	const FABTSM73BeamC3CribCoreSettings CoreSettings =
		SyntheticCoreSettings();
	FABTSM73BeamAGenerationResult Assembly = BuildRectangularCoreFixture();
	FABTSM73BeamC3CribCoreResult Core;
	FABTSM73BeamC3CribCoreGenerator Generator;
	FString Error;
	const bool bGenerated = Generator.Generate(
		CoreSettings, BeamASettings, Assembly, Core, Error);
	TestTrue(*FString::Printf(TEXT("Synthetic four-post core generates: %s"),
		*Error), bGenerated);
	if (!bGenerated)
	{
		return false;
	}

	TestTrue(TEXT("Synthetic rewrite is accepted"), Core.Summary.bAccepted);
	TestTrue(TEXT("Four-post topology is certified before Beam-C2"),
		Core.Summary.bCoreTopologyCertified);
	TestFalse(TEXT("Runtime stability waits for the final all-Z audit"),
		Core.Summary.bStabilityCoreCertified);
	TestEqual(TEXT("Synthetic fixture selects one host"),
		Core.Summary.HostCount, 1);
	TestEqual(TEXT("Synthetic fixture emits one belt"),
		Core.Summary.BeltCount, 1);
	TestEqual(TEXT("Synthetic fixture records one host plan"),
		Core.HostPlans.Num(), 1);
	if (Core.HostPlans.IsEmpty())
	{
		return false;
	}
	const FABTSM73BeamC3CribCoreHostPlan& Plan = Core.HostPlans[0];
	TestEqual(TEXT("A host owns four corner stations"),
		Plan.StationPositions.Num(), 4);
	TestEqual(TEXT("A host records one belt height"),
		Plan.BeltMidZs.Num(), 1);
	TestEqual(TEXT("Each belt closes with four courses"),
		Core.Summary.ClosedCoreCourseCount, 4 * Core.Summary.BeltCount);
	TestEqual(TEXT("Each belt carries four corner cross bearings"),
		Core.Summary.CoreCornerBearingCount, 4 * Core.Summary.BeltCount);
	TestTrue(TEXT("The empty fixture inserts all four missing courses"),
		Core.Summary.InsertedCoreMemberCount >= 4);
	TestTrue(TEXT("All four host posts are reused"),
		Core.Summary.ReusedCoreMemberCount >= 4);
	TestTrue(TEXT("Synthetic rewrite respects its net budget"),
		Core.Summary.NetMemberDelta <= CoreSettings.MaximumNetMemberIncrease);
	TestTrue(TEXT("Synthetic core plan hash is non-zero"),
		Core.Summary.CorePlanHash != 0);
	TestEqual(TEXT("The belt has exactly two X courses"),
		CountMembers(Assembly, EABTSM73BeamAMemberRole::CoreCourse,
			EABTSM73BeamAFrameAxis::X), 2);
	TestEqual(TEXT("The belt has exactly two Y courses"),
		CountMembers(Assembly, EABTSM73BeamAMemberRole::CoreCourse,
			EABTSM73BeamAFrameAxis::Y), 2);
	TestTrue(TEXT("Closure segments and tags all four host posts"),
		CountMembers(Assembly, EABTSM73BeamAMemberRole::CorePost,
			EABTSM73BeamAFrameAxis::Z) >= 8);
	TestEqual(TEXT("All four X/Y corner contacts are physical"),
		CountUniqueCoreCornerBearings(Assembly, Plan,
			BeamASettings.JointMergeToleranceCM), 4);
	TestEqual(TEXT("Every corner has a lower and upper post bearing"),
		CountUniqueCorePostBearings(Assembly, Plan,
			BeamASettings.BlockCrossSectionCM,
			BeamASettings.JointMergeToleranceCM), 8);
	for (const FABTSM73BeamAMember& Member : Assembly.Members)
	{
		TestNotEqual(TEXT("C3 does not emit a diagonal member"),
			Member.Axis, EABTSM73BeamAFrameAxis::Diagonal);
	}

	Error.Reset();
	const bool bCertified = Generator.CertifyFinalAssembly(
		CoreSettings, BeamASettings, Assembly, Core, Error);
	TestTrue(*FString::Printf(TEXT("Synthetic final audit succeeds: %s"),
		*Error), bCertified);
	TestTrue(TEXT("Final all-Z audit certifies the stability core"),
		Core.Summary.bStabilityCoreCertified);
	TestTrue(TEXT("Final all-Z span stays within the hard gate"),
		Core.Summary.MaximumUnbracedCorePostSpanAfterCM
			<= CoreSettings.MaximumUnbracedCorePostSpanCM
				+ BeamASettings.JointMergeToleranceCM);
	TestTrue(TEXT("Synthetic final assembly stays within its member budget"),
		Assembly.Members.Num() <= CoreSettings.MaximumFinalMemberCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3RootedTieTopologyTest,
	"ABTS.M73DAG.BeamC3.RootedTieTopology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3RootedTieTopologyTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3Tests;
	const FABTSM73BeamAPreviewSettings BeamASettings =
		SyntheticBeamASettings();
	const FABTSM73BeamC3CribCoreSettings CoreSettings =
		SyntheticCoreSettings();
	FABTSM73BeamAGenerationResult Assembly = BuildRectangularCoreFixture();
	FABTSM73BeamC3CribCoreResult Core;
	FABTSM73BeamC3CribCoreGenerator Generator;
	FString Error;
	if (!Generator.Generate(CoreSettings, BeamASettings,
		Assembly, Core, Error))
	{
		AddError(FString::Printf(TEXT("Rooted-tie base core failed: %s"), *Error));
		return false;
	}

	const double Section = BeamASettings.BlockCrossSectionCM;
	const FVector2D Anchor = Core.HostPlans[0].StationPositions[0];
	const FVector2D Target(540.0, Anchor.Y);
	const double CourseCenterZ = 300.0;
	Assembly.Bays[0].LocalBounds.Max.X = 600.0;
	AddFixtureMember(Assembly, 0,
		FVector(Target.X, Target.Y, 0.0),
		FVector(Target.X, Target.Y, 600.0),
		EABTSM73BeamAFrameAxis::Z,
		EABTSM73BeamAMemberRole::CorePost);
	AddFixtureMember(Assembly, 0,
		FVector(Anchor.X - Section * 0.5, Anchor.Y, CourseCenterZ),
		FVector(Target.X + Section * 0.5, Target.Y, CourseCenterZ),
		EABTSM73BeamAFrameAxis::X,
		EABTSM73BeamAMemberRole::CoreCourse);
	TestTrue(TEXT("Rooted-tie fixture recloses"),
		ABTSM73BeamA::CloseGeneratedAssembly(
			BeamASettings, Assembly, Error));
	if (!Error.IsEmpty())
	{
		AddError(Error);
		return false;
	}
	FABTSM73BeamC3TargetedTiePlan& Tie = Core.TiePlans.AddDefaulted_GetRef();
	Tie.AnchorStation = Anchor;
	Tie.TargetStation = Target;
	Tie.Axis = EABTSM73BeamAFrameAxis::X;
	Tie.CourseCenterZ = CourseCenterZ;
	Tie.AnchorHostPlanIndex = 0;
	Tie.BayId = 0;
	Tie.SourceVolumeId = 0;
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("A host-rooted tie certifies: %s"), *Error),
		Generator.CertifyFinalAssembly(
			CoreSettings, BeamASettings, Assembly, Core, Error));

	FABTSM73BeamC3CribCoreResult MissingAnchorCore = Core;
	MissingAnchorCore.TiePlans[0].AnchorHostPlanIndex = INDEX_NONE;
	Error.Reset();
	TestFalse(TEXT("An unrooted tie fails closed"),
		Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
			Assembly, MissingAnchorCore, Error));
	TestTrue(TEXT("Unrooted tie rejection is explicit"),
		Error.StartsWith(
			TEXT("BeamC3TargetedTieTopologyIncomplete:MissingAnchor")));

	FABTSM73BeamAGenerationResult MissingEnd = Assembly;
	const int32 EndContact = MissingEnd.BearingContacts.IndexOfByPredicate(
		[&MissingEnd, &Target, CourseCenterZ, Section](
			const FABTSM73BeamABearingContact& Contact)
		{
			if (FVector2D::Distance(FVector2D(
				Contact.LocalPosition.X, Contact.LocalPosition.Y), Target)
				> Section)
			{
				return false;
			}
			const bool bAtTieFace =
				FMath::Abs(Contact.LocalPosition.Z
					- (CourseCenterZ - Section * 0.5)) <= 1.0
				|| FMath::Abs(Contact.LocalPosition.Z
					- (CourseCenterZ + Section * 0.5)) <= 1.0;
			return bAtTieFace && IsCorePostBearing(MissingEnd, Contact);
		});
	TestTrue(TEXT("Rooted tie exposes a target-end bearing"),
		EndContact != INDEX_NONE);
	if (EndContact != INDEX_NONE)
	{
		MissingEnd.BearingContacts.RemoveAt(EndContact);
		RenumberContacts(MissingEnd);
		FABTSM73BeamC3CribCoreResult MissingEndCore = Core;
		Error.Reset();
		TestFalse(TEXT("A tie missing one target bearing fails closed"),
			Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
				MissingEnd, MissingEndCore, Error));
		TestTrue(TEXT("Missing tie-end rejection is explicit"),
			Error.StartsWith(
				TEXT("BeamC3TargetedTieTopologyIncomplete:MissingEndBearing")));
	}

	FABTSM73BeamAGenerationResult DetachedAnchor = Assembly;
	const double HostBeltMidZ = Core.HostPlans[0].BeltMidZs[0];
	const double DetachedStubTopZ = HostBeltMidZ + Section * 4.0;
	const int32 UpperHostPost = DetachedAnchor.Members.IndexOfByPredicate(
		[&DetachedAnchor, &Anchor, HostBeltMidZ, Section](
			const FABTSM73BeamAMember& Member)
		{
			if (Member.Role != EABTSM73BeamAMemberRole::CorePost
				|| Member.Axis != EABTSM73BeamAFrameAxis::Z)
			{
				return false;
			}
			const FBox Bounds = FixtureMemberBounds(
				DetachedAnchor, Member, Section);
			return FVector2D::Distance(
				FVector2D(Bounds.GetCenter().X, Bounds.GetCenter().Y), Anchor)
					<= 1.0
				&& Bounds.Min.Z >= HostBeltMidZ + Section - 1.0
				&& Bounds.Max.Z > HostBeltMidZ + Section * 6.0;
		});
	TestTrue(TEXT("Detached-anchor fixture finds the upper host post"),
		UpperHostPost != INDEX_NONE);
	if (UpperHostPost != INDEX_NONE)
	{
		FABTSM73BeamAMember& UpperPost = DetachedAnchor.Members[UpperHostPost];
		FVector& A = DetachedAnchor.Joints[UpperPost.JointA].LocalPosition;
		FVector& B = DetachedAnchor.Joints[UpperPost.JointB].LocalPosition;
		if (A.Z > B.Z)
		{
			A.Z = DetachedStubTopZ;
		}
		else
		{
			B.Z = DetachedStubTopZ;
		}
		UpperPost.LengthCM = FMath::Abs(A.Z - B.Z);

		const FVector2D DetachedTarget(Anchor.X - 360.0, Anchor.Y);
		const double DetachedTieCenterZ = 900.0;
		const double DetachedTieBottomZ = DetachedTieCenterZ - Section * 0.5;
		const double DetachedTieTopZ = DetachedTieCenterZ + Section * 0.5;
		DetachedAnchor.Bays[0].LocalBounds.Min.X = FMath::Min(
			DetachedAnchor.Bays[0].LocalBounds.Min.X,
			DetachedTarget.X - Section);
		for (const FVector2D& Station : {Anchor, DetachedTarget})
		{
			AddFixtureMember(DetachedAnchor, 0,
				FVector(Station.X, Station.Y, 800.0),
				FVector(Station.X, Station.Y, DetachedTieBottomZ),
				EABTSM73BeamAFrameAxis::Z,
				EABTSM73BeamAMemberRole::CorePost);
			AddFixtureMember(DetachedAnchor, 0,
				FVector(Station.X, Station.Y, DetachedTieTopZ),
				FVector(Station.X, Station.Y, 1050.0),
				EABTSM73BeamAFrameAxis::Z,
				EABTSM73BeamAMemberRole::CorePost);
		}
		AddFixtureMember(DetachedAnchor, 0,
			FVector(DetachedTarget.X - Section * 0.5, DetachedTarget.Y,
				DetachedTieCenterZ),
			FVector(Anchor.X + Section * 0.5, Anchor.Y,
				DetachedTieCenterZ),
			EABTSM73BeamAFrameAxis::X,
			EABTSM73BeamAMemberRole::CoreCourse);
		Error.Reset();
		TestTrue(*FString::Printf(
			TEXT("Detached-anchor contacts rebuild: %s"), *Error),
			ABTSM73BeamA::RebuildBearingContacts(
				BeamASettings, DetachedAnchor, Error));
		FABTSM73BeamC3CribCoreResult DetachedCore = Core;
		FABTSM73BeamC3TargetedTiePlan& DetachedTie =
			DetachedCore.TiePlans.AddDefaulted_GetRef();
		DetachedTie.AnchorStation = Anchor;
		DetachedTie.TargetStation = DetachedTarget;
		DetachedTie.Axis = EABTSM73BeamAFrameAxis::X;
		DetachedTie.CourseCenterZ = DetachedTieCenterZ;
		DetachedTie.AnchorHostPlanIndex = 0;
		DetachedTie.BayId = 0;
		DetachedTie.SourceVolumeId = 0;
		Error.Reset();
		TestFalse(TEXT("A same-XY but vertically detached anchor fails closed"),
			Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
				DetachedAnchor, DetachedCore, Error));
		TestTrue(TEXT("Detached anchor rejection identifies its missing root path"),
			Error.StartsWith(
				TEXT("BeamC3TargetedTieTopologyIncomplete:AnchorVerticalPathMissing")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3TopologyFailClosedTest,
	"ABTS.M73DAG.BeamC3.TopologyFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3TopologyFailClosedTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3Tests;
	const FABTSM73BeamAPreviewSettings BeamASettings =
		SyntheticBeamASettings();
	const FABTSM73BeamC3CribCoreSettings CoreSettings =
		SyntheticCoreSettings();
	FABTSM73BeamAGenerationResult Assembly = BuildRectangularCoreFixture();
	FABTSM73BeamC3CribCoreResult Core;
	FABTSM73BeamC3CribCoreGenerator Generator;
	FString Error;
	if (!Generator.Generate(CoreSettings, BeamASettings,
		Assembly, Core, Error)
		|| !Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
			Assembly, Core, Error))
	{
		AddError(FString::Printf(
			TEXT("Topology negative fixture did not certify: %s"), *Error));
		return false;
	}

	FABTSM73BeamAGenerationResult MissingCourse = Assembly;
	const int32 CourseId = MissingCourse.Members.IndexOfByPredicate(
		[](const FABTSM73BeamAMember& Member)
		{
			return Member.Role == EABTSM73BeamAMemberRole::CoreCourse;
		});
	TestTrue(TEXT("Synthetic fixture exposes a CoreCourse"),
		CourseId != INDEX_NONE);
	if (CourseId == INDEX_NONE)
	{
		return false;
	}
	MissingCourse.Members[CourseId].Role =
		EABTSM73BeamAMemberRole::PrimaryBeam;
	FABTSM73BeamC3CribCoreResult MissingCourseCore = Core;
	Error.Reset();
	TestFalse(TEXT("A missing side fails the final audit"),
		Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
			MissingCourse, MissingCourseCore, Error));
	TestTrue(TEXT("Missing-side rejection is explicit"),
		Error.StartsWith(TEXT("BeamC3CoreTopologyIncomplete:MissingCourse")));

	FABTSM73BeamAGenerationResult MissingCorner = Assembly;
	const int32 CornerContactIndex =
		MissingCorner.BearingContacts.IndexOfByPredicate(
			[&MissingCorner](const FABTSM73BeamABearingContact& Contact)
			{
				return IsCoreCrossBearing(MissingCorner, Contact);
			});
	TestTrue(TEXT("Synthetic fixture exposes a corner bearing"),
		CornerContactIndex != INDEX_NONE);
	if (CornerContactIndex != INDEX_NONE)
	{
		MissingCorner.BearingContacts.RemoveAt(CornerContactIndex);
		RenumberContacts(MissingCorner);
		FABTSM73BeamC3CribCoreResult MissingCornerCore = Core;
		Error.Reset();
		TestFalse(TEXT("A missing corner bearing fails the final audit"),
			Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
				MissingCorner, MissingCornerCore, Error));
		TestTrue(TEXT("Missing-corner rejection is explicit"),
			Error.StartsWith(
				TEXT("BeamC3CoreTopologyIncomplete:MissingCornerBearing")));
	}

	FABTSM73BeamAGenerationResult MissingPost = Assembly;
	const int32 PostContactIndex =
		MissingPost.BearingContacts.IndexOfByPredicate(
			[&MissingPost](const FABTSM73BeamABearingContact& Contact)
			{
				return IsCorePostBearing(MissingPost, Contact);
			});
	TestTrue(TEXT("Synthetic fixture exposes a post bearing"),
		PostContactIndex != INDEX_NONE);
	if (PostContactIndex != INDEX_NONE)
	{
		MissingPost.BearingContacts.RemoveAt(PostContactIndex);
		RenumberContacts(MissingPost);
		FABTSM73BeamC3CribCoreResult MissingPostCore = Core;
		Error.Reset();
		TestFalse(TEXT("A missing post bearing fails the final audit"),
			Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
				MissingPost, MissingPostCore, Error));
		TestTrue(TEXT("Missing-post rejection is explicit"),
			Error.StartsWith(
				TEXT("BeamC3CoreTopologyIncomplete:MissingPostBearing")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3ExistingPlanRestoresMissingCourseTest,
	"ABTS.M73DAG.BeamC3.ExistingPlanRestoresMissingCourse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3ExistingPlanRestoresMissingCourseTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3Tests;
	const FABTSM73BeamAPreviewSettings BeamASettings =
		SyntheticBeamASettings();
	const FABTSM73BeamC3CribCoreSettings CoreSettings =
		SyntheticCoreSettings();
	FABTSM73BeamAGenerationResult Assembly = BuildRectangularCoreFixture();
	FABTSM73BeamC3CribCoreResult InitialCore;
	FABTSM73BeamC3CribCoreGenerator Generator;
	FString Error;
	if (!Generator.Generate(CoreSettings, BeamASettings,
		Assembly, InitialCore, Error)
		|| !Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
			Assembly, InitialCore, Error))
	{
		AddError(FString::Printf(
			TEXT("Existing-plan fixture did not initially certify: %s"),
			*Error));
		return false;
	}
	if (InitialCore.HostPlans.IsEmpty()
		|| InitialCore.HostPlans[0].StationPositions.Num() != 4
		|| InitialCore.HostPlans[0].BeltMidZs.IsEmpty())
	{
		AddError(TEXT("Existing-plan fixture has no complete host plan"));
		return false;
	}

	// Generate clears its output result first, so the authoritative input plan
	// must be an independent object rather than an alias of Restored below.
	const FABTSM73BeamC3CribCoreResult CertifiedPlan = InitialCore;
	const int32 CertifiedMemberCount = Assembly.Members.Num();
	const FABTSM73BeamC3CribCoreHostPlan& Host =
		CertifiedPlan.HostPlans[0];
	const double Section = BeamASettings.BlockCrossSectionCM;
	const double Tolerance = FMath::Max(
		0.01, static_cast<double>(BeamASettings.JointMergeToleranceCM));
	const double ExpectedY = Host.StationPositions[0].Y;
	const double ExpectedZ = Host.BeltMidZs[0] - Section * 0.5;
	const double ExpectedMinX = FMath::Min(
		Host.StationPositions[0].X, Host.StationPositions[1].X);
	const double ExpectedMaxX = FMath::Max(
		Host.StationPositions[0].X, Host.StationPositions[1].X);
	auto FindExpectedCourse = [&]() -> int32
	{
		return Assembly.Members.IndexOfByPredicate(
			[&](const FABTSM73BeamAMember& Member)
			{
				if (Member.Role != EABTSM73BeamAMemberRole::CoreCourse
					|| Member.Axis != EABTSM73BeamAFrameAxis::X)
				{
					return false;
				}
				const FVector Center = FixtureMemberCenter(Assembly, Member);
				const FBox Bounds = FixtureMemberBounds(
					Assembly, Member, Section);
				return FMath::Abs(Center.Y - ExpectedY) <= Tolerance
					&& FMath::Abs(Center.Z - ExpectedZ) <= Tolerance
					&& Bounds.Min.X <= ExpectedMinX + Tolerance
					&& Bounds.Max.X >= ExpectedMaxX - Tolerance;
			});
	};

	const int32 RemovedCourseId = FindExpectedCourse();
	TestTrue(TEXT("Certified fixture exposes the planned X CoreCourse"),
		RemovedCourseId != INDEX_NONE);
	if (RemovedCourseId == INDEX_NONE)
	{
		return false;
	}
	Error.Reset();
	const bool bRemoved = RemoveFixtureMemberAndRebuildContacts(
		Assembly, RemovedCourseId, BeamASettings, Error);
	TestTrue(*FString::Printf(
		TEXT("Fixture course removal remains structurally readable: %s"),
		*Error), bRemoved);
	if (!bRemoved)
	{
		return false;
	}
	TestEqual(TEXT("Damage removes exactly one member"),
		Assembly.Members.Num(), CertifiedMemberCount - 1);
	TestEqual(TEXT("The planned course is genuinely absent"),
		FindExpectedCourse(), INDEX_NONE);

	FABTSM73BeamC3CribCoreResult RejectedPlan = CertifiedPlan;
	Error.Reset();
	TestFalse(TEXT("The damaged plan fails final certification"),
		Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
			Assembly, RejectedPlan, Error));
	TestTrue(TEXT("Damage rejection identifies the missing course"),
		Error.StartsWith(TEXT("BeamC3CoreTopologyIncomplete:MissingCourse")));
	TestFalse(TEXT("Rejected copy loses runtime certification"),
		RejectedPlan.Summary.bStabilityCoreCertified);
	TestTrue(TEXT("The immutable source plan stays certified"),
		CertifiedPlan.Summary.bStabilityCoreCertified);

	FABTSM73BeamC3CribCoreResult Restored;
	Error.Reset();
	const bool bRestored = Generator.Generate(
		CoreSettings, BeamASettings, Assembly, Restored, Error, &CertifiedPlan);
	TestTrue(*FString::Printf(
		TEXT("Existing plan restores missing geometry: %s"), *Error),
		bRestored);
	if (!bRestored)
	{
		return false;
	}
	TestEqual(TEXT("Restoration returns to the certified member count"),
		Assembly.Members.Num(), CertifiedMemberCount);
	TestTrue(TEXT("The planned course physically exists again"),
		FindExpectedCourse() != INDEX_NONE);
	TestEqual(TEXT("Only the missing core member is inserted"),
		Restored.Summary.InsertedCoreMemberCount, 1);
	TestEqual(TEXT("Repair has an exact one-member net delta"),
		Restored.Summary.NetMemberDelta, 1);
	TestEqual(TEXT("The synthetic repair consumes no donor"),
		Restored.Summary.RemovedBudgetDonorMemberCount, 0);
	TestTrue(TEXT("Repair respects the net-member budget"),
		Restored.Summary.NetMemberDelta
			<= CoreSettings.MaximumNetMemberIncrease);
	TestTrue(TEXT("Repair respects the absolute final-member budget"),
		Assembly.Members.Num() <= CoreSettings.MaximumFinalMemberCount);
	TestEqual(TEXT("Repair preserves host-plan cardinality"),
		Restored.HostPlans.Num(), CertifiedPlan.HostPlans.Num());
	TestEqual(TEXT("Repair preserves tie-plan cardinality"),
		Restored.TiePlans.Num(), CertifiedPlan.TiePlans.Num());
	TestEqual(TEXT("Repair does not add an unrelated host"),
		Restored.Summary.HostCount, CertifiedPlan.Summary.HostCount);
	TestEqual(TEXT("Repair preserves the belt count"),
		Restored.Summary.BeltCount, CertifiedPlan.Summary.BeltCount);

	const int64 GeneratedPlanHash = Restored.Summary.CorePlanHash;
	TestTrue(TEXT("Restored plan hash is non-zero"), GeneratedPlanHash != 0);
	Error.Reset();
	const bool bCertified = Generator.CertifyFinalAssembly(
		CoreSettings, BeamASettings, Assembly, Restored, Error);
	TestTrue(*FString::Printf(
		TEXT("Restored plan passes final certification: %s"), *Error),
		bCertified);
	TestTrue(TEXT("Restored plan is topology-certified"),
		Restored.Summary.bCoreTopologyCertified);
	TestTrue(TEXT("Restored plan is runtime-certified"),
		Restored.Summary.bStabilityCoreCertified);
	TestEqual(TEXT("Every restored belt has four closed courses"),
		Restored.Summary.ClosedCoreCourseCount,
		4 * Restored.Summary.BeltCount);
	TestTrue(TEXT("Restored all-Z span stays under the hard gate"),
		Restored.Summary.MaximumUnbracedCorePostSpanAfterCM
			<= CoreSettings.MaximumUnbracedCorePostSpanCM
				+ BeamASettings.JointMergeToleranceCM);
	TestEqual(TEXT("Final certification preserves the generated plan hash"),
		Restored.Summary.CorePlanHash, GeneratedPlanHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3ExistingPlanRestoresMissingPostBearingTest,
	"ABTS.M73DAG.BeamC3.ExistingPlanRestoresMissingPostBearing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3ExistingPlanRestoresMissingPostBearingTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3Tests;
	const FABTSM73BeamAPreviewSettings BeamASettings =
		SyntheticBeamASettings();
	const FABTSM73BeamC3CribCoreSettings CoreSettings =
		SyntheticCoreSettings();
	FABTSM73BeamAGenerationResult Assembly = BuildRectangularCoreFixture();
	FABTSM73BeamC3CribCoreResult InitialCore;
	FABTSM73BeamC3CribCoreGenerator Generator;
	FString Error;
	if (!Generator.Generate(CoreSettings, BeamASettings,
		Assembly, InitialCore, Error)
		|| !Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
			Assembly, InitialCore, Error))
	{
		AddError(FString::Printf(
			TEXT("Missing-bearing fixture did not initially certify: %s"),
			*Error));
		return false;
	}

	// Generate clears its output object, so retain an immutable copy of the
	// authoritative pre-C2 plan while mutating only the assembly semantics.
	const FABTSM73BeamC3CribCoreResult CertifiedPlan = InitialCore;
	const int32 CertifiedMemberCount = Assembly.Members.Num();
	const int32 BearingContactIndex =
		Assembly.BearingContacts.IndexOfByPredicate(
			[&Assembly](const FABTSM73BeamABearingContact& Contact)
			{
				return IsCorePostBearing(Assembly, Contact);
			});
	TestTrue(TEXT("Certified fixture exposes a real CorePost bearing"),
		BearingContactIndex != INDEX_NONE);
	if (BearingContactIndex == INDEX_NONE)
	{
		return false;
	}

	const FABTSM73BeamABearingContact& BearingContact =
		Assembly.BearingContacts[BearingContactIndex];
	int32 CorePostMemberId = INDEX_NONE;
	for (const int32 CandidateId : {
		BearingContact.LowerMemberId, BearingContact.UpperMemberId})
	{
		if (Assembly.Members.IsValidIndex(CandidateId)
			&& Assembly.Members[CandidateId].Axis
				== EABTSM73BeamAFrameAxis::Z
			&& Assembly.Members[CandidateId].Role
				== EABTSM73BeamAMemberRole::CorePost)
		{
			CorePostMemberId = CandidateId;
			break;
		}
	}
	TestTrue(TEXT("Bearing evidence identifies its CorePost member"),
		CorePostMemberId != INDEX_NONE);
	if (CorePostMemberId == INDEX_NONE)
	{
		return false;
	}

	Assembly.Members[CorePostMemberId].Role =
		EABTSM73BeamAMemberRole::Post;
	FABTSM73BeamC3CribCoreResult RejectedPlan = CertifiedPlan;
	Error.Reset();
	TestFalse(TEXT("Losing the C2-split CorePost role fails final certification"),
		Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
			Assembly, RejectedPlan, Error));
	TestTrue(TEXT("Role-loss rejection identifies the missing post bearing"),
		Error.StartsWith(
			TEXT("BeamC3CoreTopologyIncomplete:MissingPostBearing")));
	TestEqual(TEXT("Semantic damage does not change member cardinality"),
		Assembly.Members.Num(), CertifiedMemberCount);

	FABTSM73BeamC3CribCoreResult Restored;
	Error.Reset();
	const bool bRestored = Generator.Generate(
		CoreSettings, BeamASettings, Assembly, Restored, Error, &CertifiedPlan);
	TestTrue(*FString::Printf(
		TEXT("Existing plan restores the lost post-bearing role: %s"), *Error),
		bRestored);
	if (!bRestored)
	{
		return false;
	}
	TestEqual(TEXT("Role restoration preserves member cardinality"),
		Assembly.Members.Num(), CertifiedMemberCount);
	TestEqual(TEXT("Role restoration inserts no geometry"),
		Restored.Summary.InsertedCoreMemberCount, 0);
	TestEqual(TEXT("Role restoration has zero net member growth"),
		Restored.Summary.NetMemberDelta, 0);
	TestEqual(TEXT("Role restoration consumes no donor"),
		Restored.Summary.RemovedBudgetDonorMemberCount, 0);
	TestTrue(TEXT("The affected member is retagged as CorePost"),
		Assembly.Members[CorePostMemberId].Role
			== EABTSM73BeamAMemberRole::CorePost);
	TestTrue(TEXT("Zero-growth restoration remains inside the net budget"),
		Restored.Summary.NetMemberDelta
			<= CoreSettings.MaximumNetMemberIncrease);
	TestTrue(TEXT("Zero-growth restoration remains inside the final budget"),
		Assembly.Members.Num() <= CoreSettings.MaximumFinalMemberCount);

	Error.Reset();
	const bool bCertified = Generator.CertifyFinalAssembly(
		CoreSettings, BeamASettings, Assembly, Restored, Error);
	TestTrue(*FString::Printf(
		TEXT("Restored post bearing passes final certification: %s"), *Error),
		bCertified);
	TestTrue(TEXT("Restored post bearing is runtime-certified"),
		Restored.Summary.bStabilityCoreCertified);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3ExistingPlanRepairsPostC2HighZStationTest,
	"ABTS.M73DAG.BeamC3.ExistingPlanRepairsPostC2HighZStation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3ExistingPlanRepairsPostC2HighZStationTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3Tests;
	const FABTSM73BeamAPreviewSettings BeamASettings =
		SyntheticBeamASettings();
	FABTSM73BeamC3CribCoreSettings CoreSettings = SyntheticCoreSettings();
	CoreSettings.MaximumFinalMemberCount = 96;
	FABTSM73BeamAGenerationResult Assembly = BuildRectangularCoreFixture();
	FABTSM73BeamC3CribCoreResult InitialCore;
	FABTSM73BeamC3CribCoreGenerator Generator;
	FString Error;
	if (!Generator.Generate(CoreSettings, BeamASettings,
		Assembly, InitialCore, Error)
		|| !Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
			Assembly, InitialCore, Error))
	{
		AddError(FString::Printf(
			TEXT("Post-C2 repair fixture did not initially certify: %s"),
			*Error));
		return false;
	}

	// Two disjoint certified hosts give the synthetic post one rooted X
	// endpoint and one rooted Y endpoint without placing the post itself inside
	// either core. This isolates the post-C2 continuation path from the much
	// slower production candidate search.
	Assembly.Bays[0].LocalBounds.Max.X = 1080.0;
	Assembly.Bays[0].LocalBounds.Max.Y = 1080.0;
	const FABTSM73BeamC3CribCoreHostPlan SecondHost =
		AppendClosedCoreHostFixture(Assembly, FVector2D(720.0, 720.0),
			180.0, 1200.0, 600.0, BeamASettings.BlockCrossSectionCM);
	Error.Reset();
	const bool bSecondHostContacts = ABTSM73BeamA::RebuildBearingContacts(
		BeamASettings, Assembly, Error);
	TestTrue(*FString::Printf(
		TEXT("Second certified host rebuilds contacts: %s"), *Error),
		bSecondHostContacts);
	if (!bSecondHostContacts)
	{
		return false;
	}
	InitialCore.HostPlans.Add(SecondHost);
	InitialCore.Summary.HostCount = InitialCore.HostPlans.Num();
	InitialCore.Summary.BeltCount += SecondHost.BeltMidZs.Num();
	Error.Reset();
	const bool bTwoHostsCertified = Generator.CertifyFinalAssembly(
		CoreSettings, BeamASettings, Assembly, InitialCore, Error);
	TestTrue(*FString::Printf(
		TEXT("Two-host fixture certifies before C2 damage: %s"), *Error),
		bTwoHostsCertified);
	if (!bTwoHostsCertified)
	{
		return false;
	}
	const FABTSM73BeamC3CribCoreResult CertifiedPlan = InitialCore;

	const FVector2D HighZStation(180.0, 540.0);
	constexpr double PostC2HeightCM = 1008.47;
	const int32 HighZMemberId = AddFixtureMember(Assembly, 0,
		FVector(HighZStation.X, HighZStation.Y, 0.0),
		FVector(HighZStation.X, HighZStation.Y, PostC2HeightCM),
		EABTSM73BeamAFrameAxis::Z, EABTSM73BeamAMemberRole::Post);
	RefreshSyntheticSummary(Assembly);
	Error.Reset();
	const bool bDamagedContacts = ABTSM73BeamA::RebuildBearingContacts(
		BeamASettings, Assembly, Error);
	TestTrue(*FString::Printf(
		TEXT("Post-C2 high-Z fixture rebuilds contacts: %s"), *Error),
		bDamagedContacts);
	if (!bDamagedContacts)
	{
		return false;
	}
	TestTrue(TEXT("Synthetic post exceeds the hard all-Z gate"),
		Assembly.Members[HighZMemberId].LengthCM
			> CoreSettings.MaximumUnbracedCorePostSpanCM);
	const int32 DamagedMemberCount = Assembly.Members.Num();

	FABTSM73BeamC3CribCoreResult RejectedPlan = CertifiedPlan;
	Error.Reset();
	TestFalse(TEXT("The post-C2 high-Z station fails final certification"),
		Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
			Assembly, RejectedPlan, Error));
	TestTrue(TEXT("The high-Z rejection is the explicit all-Z gate"),
		Error.StartsWith(TEXT("BeamC3FinalAllZSpanExceeded")));
	TestTrue(TEXT("The high-Z rejection identifies the synthetic station"),
		Error.Contains(TEXT("Station=180.0,540.0")));

	FABTSM73BeamC3CribCoreResult Repaired;
	Error.Reset();
	const bool bRepaired = Generator.Generate(
		CoreSettings, BeamASettings, Assembly, Repaired, Error, &CertifiedPlan);
	TestTrue(*FString::Printf(
		TEXT("Existing plan repairs the post-C2 high-Z station: %s"), *Error),
		bRepaired);
	if (!bRepaired)
	{
		return false;
	}
	TestTrue(TEXT("Repair adds rooted tie or compact-host evidence"),
		Repaired.TiePlans.Num() > CertifiedPlan.TiePlans.Num()
			|| Repaired.HostPlans.Num() > CertifiedPlan.HostPlans.Num());
	TestTrue(TEXT("Repair remains inside the net-member budget"),
		Repaired.Summary.NetMemberDelta
			<= CoreSettings.MaximumNetMemberIncrease);
	TestTrue(TEXT("Repair remains inside the final-member budget"),
		Assembly.Members.Num() <= CoreSettings.MaximumFinalMemberCount);
	TestTrue(TEXT("Repair growth is measured from the damaged assembly"),
		Assembly.Members.Num() - DamagedMemberCount
			== Repaired.Summary.NetMemberDelta);
	TestTrue(TEXT("Repair brings every final Z station under 720 cm"),
		Repaired.Summary.MaximumUnbracedCorePostSpanAfterCM
			<= CoreSettings.MaximumUnbracedCorePostSpanCM
				+ BeamASettings.JointMergeToleranceCM);

	Error.Reset();
	const bool bCertified = Generator.CertifyFinalAssembly(
		CoreSettings, BeamASettings, Assembly, Repaired, Error);
	TestTrue(*FString::Printf(
		TEXT("Repaired post-C2 fixture passes final certification: %s"),
		*Error), bCertified);
	TestTrue(TEXT("Repaired post-C2 fixture is runtime-certified"),
		Repaired.Summary.bStabilityCoreCertified);
	TestTrue(TEXT("Final certification preserves the 720 cm gate"),
		Repaired.Summary.MaximumUnbracedCorePostSpanAfterCM
			<= CoreSettings.MaximumUnbracedCorePostSpanCM
				+ BeamASettings.JointMergeToleranceCM);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3RootedExistingFloorNetworkBracesPeripheralPostTest,
	"ABTS.M73DAG.BeamC3.RootedExistingFloorNetworkBracesPeripheralPost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3RootedExistingFloorNetworkBracesPeripheralPostTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3Tests;
	const FABTSM73BeamAPreviewSettings BeamASettings =
		SyntheticBeamASettings();
	FABTSM73BeamC3CribCoreSettings CoreSettings = SyntheticCoreSettings();
	CoreSettings.MaximumFinalMemberCount = 96;
	FRootedFloorNetworkFixture Fixture;
	FString Error;
	const bool bFixtureBuilt = BuildRootedFloorNetworkFixture(
		BeamASettings, CoreSettings, Fixture, Error);
	TestTrue(*FString::Printf(
		TEXT("Two-anchor rooted floor fixture builds: %s"), *Error),
		bFixtureBuilt);
	if (!bFixtureBuilt)
	{
		return false;
	}

	const int32 LeftRootCount = CountDirectCoreCourseContacts(
		Fixture.Assembly, Fixture.LeftAnchorCourseId);
	const int32 RightRootCount = CountDirectCoreCourseContacts(
		Fixture.Assembly, Fixture.RightAnchorCourseId);
	TestTrue(TEXT("Left backbone endpoint has an independent core root"),
		LeftRootCount > 0);
	TestTrue(TEXT("Right backbone endpoint has an independent core root"),
		RightRootCount > 0);
	const bool bPeripheralXYContact =
		Fixture.Assembly.BearingContacts.ContainsByPredicate(
			[&Fixture](const FABTSM73BeamABearingContact& Contact)
			{
				return (Contact.LowerMemberId
						== Fixture.PeripheralXCourseId
						&& Contact.UpperMemberId
							== Fixture.PeripheralYCourseId)
					|| (Contact.LowerMemberId
						== Fixture.PeripheralYCourseId
						&& Contact.UpperMemberId
							== Fixture.PeripheralXCourseId);
			});
	TestTrue(TEXT("Peripheral target is an X/Y contact inside the backbone"),
		bPeripheralXYContact);
	TestTrue(TEXT("Peripheral X course is not a one-edge leaf"),
		CountHorizontalContactNeighbors(
			Fixture.Assembly, Fixture.PeripheralXCourseId) >= 2);
	TestTrue(TEXT("Peripheral Y course is not a one-edge leaf"),
		CountHorizontalContactNeighbors(
			Fixture.Assembly, Fixture.PeripheralYCourseId) >= 2);
	if (LeftRootCount <= 0 || RightRootCount <= 0 || !bPeripheralXYContact)
	{
		return false;
	}

	const int32 MemberCountBeforeCertification =
		Fixture.Assembly.Members.Num();
	const int32 HostCountBeforeCertification =
		Fixture.CertifiedPlan.HostPlans.Num();
	const int32 TieCountBeforeCertification =
		Fixture.CertifiedPlan.TiePlans.Num();
	FABTSM73BeamC3CribCoreResult RootedResult = Fixture.CertifiedPlan;
	FABTSM73BeamC3CribCoreGenerator Generator;
	Error.Reset();
	const bool bRootedCertified = Generator.CertifyFinalAssembly(
		CoreSettings, BeamASettings, Fixture.Assembly, RootedResult, Error);
	TestTrue(*FString::Printf(
		TEXT("Two-anchor X/Y backbone certifies the peripheral post: %s"),
		*Error), bRootedCertified);
	TestEqual(TEXT("Certification inserts no member"),
		Fixture.Assembly.Members.Num(), MemberCountBeforeCertification);
	TestEqual(TEXT("Certification inserts no host"),
		RootedResult.HostPlans.Num(), HostCountBeforeCertification);
	TestEqual(TEXT("Certification inserts no targeted tie"),
		RootedResult.TiePlans.Num(), TieCountBeforeCertification);
	TestTrue(TEXT("Rooted floor network keeps the continuous Z station <=720 cm"),
		RootedResult.Summary.MaximumUnbracedCorePostSpanAfterCM
			<= CoreSettings.MaximumUnbracedCorePostSpanCM
				+ BeamASettings.JointMergeToleranceCM);
	TestTrue(TEXT("Rooted floor network is runtime-certified"),
		RootedResult.Summary.bStabilityCoreCertified);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3SingleAnchorCantileverNetworkRejectedTest,
	"ABTS.M73DAG.BeamC3.SingleAnchorCantileverNetworkRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3SingleAnchorCantileverNetworkRejectedTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3Tests;
	const FABTSM73BeamAPreviewSettings BeamASettings =
		SyntheticBeamASettings();
	FABTSM73BeamC3CribCoreSettings CoreSettings = SyntheticCoreSettings();
	CoreSettings.MaximumFinalMemberCount = 96;
	FRootedFloorNetworkFixture Fixture;
	FString Error;
	if (!BuildRootedFloorNetworkFixture(
		BeamASettings, CoreSettings, Fixture, Error))
	{
		AddError(FString::Printf(
			TEXT("Single-anchor fixture did not build: %s"), *Error));
		return false;
	}

	const int32 AnchorIds[2] = {
		Fixture.LeftAnchorCourseId, Fixture.RightAnchorCourseId};
	for (int32 BrokenAnchorIndex = 0; BrokenAnchorIndex < 2;
		++BrokenAnchorIndex)
	{
		FABTSM73BeamAGenerationResult SingleAnchor = Fixture.Assembly;
		const int32 BrokenAnchorId = AnchorIds[BrokenAnchorIndex];
		const int32 RemainingAnchorId = AnchorIds[1 - BrokenAnchorIndex];
		const int32 RemovedRootContacts =
			SingleAnchor.BearingContacts.RemoveAll(
				[&SingleAnchor, BrokenAnchorId](
					const FABTSM73BeamABearingContact& Contact)
				{
					const int32 OtherId =
						Contact.LowerMemberId == BrokenAnchorId
							? Contact.UpperMemberId
							: Contact.UpperMemberId == BrokenAnchorId
								? Contact.LowerMemberId : INDEX_NONE;
					return SingleAnchor.Members.IsValidIndex(OtherId)
						&& SingleAnchor.Members[OtherId].Role
							== EABTSM73BeamAMemberRole::CoreCourse;
				});
		RenumberContacts(SingleAnchor);
		TestTrue(*FString::Printf(
			TEXT("Variant %d removes one independent root"),
			BrokenAnchorIndex), RemovedRootContacts > 0);
		TestEqual(*FString::Printf(
			TEXT("Variant %d leaves the broken endpoint unrooted"),
			BrokenAnchorIndex),
			CountDirectCoreCourseContacts(SingleAnchor, BrokenAnchorId), 0);
		TestTrue(*FString::Printf(
			TEXT("Variant %d preserves exactly the opposite root"),
			BrokenAnchorIndex),
			CountDirectCoreCourseContacts(SingleAnchor, RemainingAnchorId) > 0);

		FABTSM73BeamC3CribCoreResult Rejected = Fixture.CertifiedPlan;
		FABTSM73BeamC3CribCoreGenerator Generator;
		Error.Reset();
		TestFalse(*FString::Printf(
			TEXT("Variant %d single-anchor cantilever fails closed"),
			BrokenAnchorIndex),
			Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
				SingleAnchor, Rejected, Error));
		TestTrue(*FString::Printf(
			TEXT("Variant %d fails at the all-Z gate: %s"),
			BrokenAnchorIndex, *Error),
			Error.StartsWith(TEXT("BeamC3FinalAllZSpanExceeded")));
		TestTrue(*FString::Printf(
			TEXT("Variant %d identifies the peripheral station"),
			BrokenAnchorIndex),
			Error.Contains(FString::Printf(TEXT("Station=%.1f,%.1f"),
				Fixture.PeripheralStation.X,
				Fixture.PeripheralStation.Y)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3FinalAllZSpanAuditTest,
	"ABTS.M73DAG.BeamC3.FinalAllZSpanAudit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3FinalAllZSpanAuditTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3Tests;
	const FABTSM73BeamAPreviewSettings BeamASettings =
		SyntheticBeamASettings();
	const FABTSM73BeamC3CribCoreSettings CoreSettings =
		SyntheticCoreSettings();
	FABTSM73BeamAGenerationResult Assembly = BuildRectangularCoreFixture();
	FABTSM73BeamC3CribCoreResult Core;
	FABTSM73BeamC3CribCoreGenerator Generator;
	FString Error;
	if (!Generator.Generate(CoreSettings, BeamASettings,
		Assembly, Core, Error)
		|| !Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
			Assembly, Core, Error))
	{
		AddError(FString::Printf(
			TEXT("All-Z negative fixture did not certify: %s"), *Error));
		return false;
	}

	const int32 FirstNakedPost = AddFixtureMember(Assembly, 0,
		FVector(0.0, 0.0, 0.0), FVector(0.0, 0.0, 500.0),
		EABTSM73BeamAFrameAxis::Z, EABTSM73BeamAMemberRole::Post);
	const int32 SecondNakedPost = AddFixtureMember(Assembly, 0,
		FVector(0.0, 0.0, 500.0), FVector(0.0, 0.0, 1000.0),
		EABTSM73BeamAFrameAxis::Z, EABTSM73BeamAMemberRole::Post);
	TestTrue(TEXT("Each naked segment is individually below the hard gate"),
		Assembly.Members[FirstNakedPost].LengthCM
			<= CoreSettings.MaximumUnbracedCorePostSpanCM
		&& Assembly.Members[SecondNakedPost].LengthCM
			<= CoreSettings.MaximumUnbracedCorePostSpanCM);
	Error.Reset();
	const bool bContactsRebuilt = ABTSM73BeamA::RebuildBearingContacts(
		BeamASettings, Assembly, Error);
	TestTrue(*FString::Printf(TEXT("Negative fixture contacts rebuild: %s"),
		*Error), bContactsRebuilt);
	if (!bContactsRebuilt)
	{
		return false;
	}
	FABTSM73BeamC3CribCoreResult Rejected = Core;
	Error.Reset();
	TestFalse(TEXT("Two short unbraced segments fail as one continuous Z chain"),
		Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
			Assembly, Rejected, Error));
	TestTrue(TEXT("Continuous all-Z rejection is explicit"),
		Error.StartsWith(TEXT("BeamC3FinalAllZSpanExceeded")));
	TestTrue(TEXT("All-Z rejection identifies the naked center station"),
		Error.Contains(TEXT("Station=0.0,0.0")));
	TestFalse(TEXT("Rejected final assembly loses runtime certification"),
		Rejected.Summary.bStabilityCoreCertified);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3OrdinaryBraceDoesNotCertifyTest,
	"ABTS.M73DAG.BeamC3.OrdinaryBraceDoesNotCertify",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3OrdinaryBraceDoesNotCertifyTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3Tests;
	const FABTSM73BeamAPreviewSettings BeamASettings =
		SyntheticBeamASettings();
	const FABTSM73BeamC3CribCoreSettings CoreSettings =
		SyntheticCoreSettings();
	FABTSM73BeamAGenerationResult Assembly = BuildRectangularCoreFixture();
	FABTSM73BeamC3CribCoreResult Core;
	FABTSM73BeamC3CribCoreGenerator Generator;
	FString Error;
	if (!Generator.Generate(CoreSettings, BeamASettings,
		Assembly, Core, Error)
		|| !Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
			Assembly, Core, Error))
	{
		AddError(FString::Printf(
			TEXT("Ordinary-brace base core did not certify: %s"), *Error));
		return false;
	}

	const FVector2D NakedStation(720.0, 0.0);
	Assembly.Bays[0].LocalBounds.Max.X = 900.0;
	AddFixtureMember(Assembly, 0,
		FVector(NakedStation.X, NakedStation.Y, 0.0),
		FVector(NakedStation.X, NakedStation.Y, 462.0),
		EABTSM73BeamAFrameAxis::Z, EABTSM73BeamAMemberRole::Post);
	AddFixtureMember(Assembly, 0,
		FVector(NakedStation.X, NakedStation.Y, 534.0),
		FVector(NakedStation.X, NakedStation.Y, 1000.0),
		EABTSM73BeamAFrameAxis::Z, EABTSM73BeamAMemberRole::Post);
	AddFixtureMember(Assembly, 0,
		FVector(NakedStation.X - 120.0, NakedStation.Y, 480.0),
		FVector(NakedStation.X + 120.0, NakedStation.Y, 480.0),
		EABTSM73BeamAFrameAxis::X,
		EABTSM73BeamAMemberRole::PrimaryBeam);
	AddFixtureMember(Assembly, 0,
		FVector(NakedStation.X, NakedStation.Y - 120.0, 516.0),
		FVector(NakedStation.X, NakedStation.Y + 120.0, 516.0),
		EABTSM73BeamAFrameAxis::Y,
		EABTSM73BeamAMemberRole::PrimaryBeam);
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Ordinary-brace contacts rebuild: %s"),
		*Error), ABTSM73BeamA::RebuildBearingContacts(
			BeamASettings, Assembly, Error));
	FABTSM73BeamC3CribCoreResult Rejected = Core;
	Error.Reset();
	TestFalse(TEXT("Ordinary X/Y courses do not certify a long Z chain"),
		Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
			Assembly, Rejected, Error));
	TestTrue(TEXT("Uncertified-course rejection is an all-Z failure"),
		Error.StartsWith(TEXT("BeamC3FinalAllZSpanExceeded")));
	TestTrue(TEXT("The rejected station is the ordinary-braced station"),
		Error.Contains(TEXT("Station=720.0,0.0")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3MultipleHostEvidenceTest,
	"ABTS.M73DAG.BeamC3.MultipleHostEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3MultipleHostEvidenceTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3Tests;
	const FABTSM73BeamAPreviewSettings BeamASettings =
		SyntheticBeamASettings();
	FABTSM73BeamC3CribCoreSettings CoreSettings = SyntheticCoreSettings();
	CoreSettings.MaximumFinalMemberCount = 96;
	FABTSM73BeamAGenerationResult Assembly = BuildRectangularCoreFixture();
	FABTSM73BeamC3CribCoreResult Core;
	FABTSM73BeamC3CribCoreGenerator Generator;
	FString Error;
	if (!Generator.Generate(CoreSettings, BeamASettings,
		Assembly, Core, Error))
	{
		AddError(FString::Printf(
			TEXT("Multiple-host base core failed: %s"), *Error));
		return false;
	}

	Assembly.Bays[0].LocalBounds.Max.X = 1400.0;
	const FABTSM73BeamC3CribCoreHostPlan SecondHost =
		AppendClosedCoreHostFixture(Assembly, FVector2D(1000.0, 0.0),
			180.0, 1200.0, 600.0, BeamASettings.BlockCrossSectionCM);
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Multiple-host contacts rebuild: %s"),
		*Error), ABTSM73BeamA::RebuildBearingContacts(
			BeamASettings, Assembly, Error));
	Core.HostPlans.Add(SecondHost);
	Core.Summary.HostCount = Core.HostPlans.Num();
	Core.Summary.BeltCount += SecondHost.BeltMidZs.Num();
	Error.Reset();
	TestTrue(*FString::Printf(
		TEXT("Every disjoint closed host contributes brace evidence: %s"), *Error),
		Generator.CertifyFinalAssembly(
			CoreSettings, BeamASettings, Assembly, Core, Error));
	TestEqual(TEXT("Two hosts certify eight closed courses"),
		Core.Summary.ClosedCoreCourseCount, 8);
	TestTrue(TEXT("The second host also satisfies the all-Z gate"),
		Core.Summary.MaximumUnbracedCorePostSpanAfterCM
			<= CoreSettings.MaximumUnbracedCorePostSpanCM
				+ BeamASettings.JointMergeToleranceCM);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3TransactionalRollbackTest,
	"ABTS.M73DAG.BeamC3.TransactionalRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3TransactionalRollbackTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3Tests;
	const FABTSM73BeamAPreviewSettings BeamASettings =
		SyntheticBeamASettings();
	FABTSM73BeamAGenerationResult Assembly = BuildRectangularCoreFixture();
	const int32 OriginalMembers = Assembly.Members.Num();
	const int32 OriginalJoints = Assembly.Joints.Num();
	const int32 OriginalContacts = Assembly.BearingContacts.Num();
	const int32 OriginalAssemblies = Assembly.Assemblies.Num();
	const int32 OriginalVoids = Assembly.ReservedSupportVoids.Num();
	const uint32 OriginalFingerprint = AssemblyFingerprint(Assembly);

	FABTSM73BeamC3CribCoreSettings Impossible = SyntheticCoreSettings();
	Impossible.MaximumFinalMemberCount = 4;
	Impossible.BeamC2MemberReserve = 1;
	Impossible.MaximumNetMemberIncrease = 64;
	Impossible.bAllowRoofLaneBudgetReallocation = false;
	FABTSM73BeamC3CribCoreResult Rejected;
	FABTSM73BeamC3CribCoreGenerator Generator;
	FString Error;
	TestFalse(TEXT("Impossible member budget fails closed"),
		Generator.Generate(Impossible, BeamASettings,
			Assembly, Rejected, Error));
	TestTrue(TEXT("Budget rejection is explicit"),
		Error.StartsWith(TEXT("BeamC3CoreBudgetInsufficient")));
	TestEqual(TEXT("Rejected rewrite leaves Members unchanged"),
		Assembly.Members.Num(), OriginalMembers);
	TestEqual(TEXT("Rejected rewrite leaves Joints unchanged"),
		Assembly.Joints.Num(), OriginalJoints);
	TestEqual(TEXT("Rejected rewrite leaves contacts unchanged"),
		Assembly.BearingContacts.Num(), OriginalContacts);
	TestEqual(TEXT("Rejected rewrite leaves Assemblies unchanged"),
		Assembly.Assemblies.Num(), OriginalAssemblies);
	TestEqual(TEXT("Rejected rewrite leaves support voids unchanged"),
		Assembly.ReservedSupportVoids.Num(), OriginalVoids);
	TestEqual(TEXT("Rejected rewrite leaves all assembly data unchanged"),
		AssemblyFingerprint(Assembly), OriginalFingerprint);
	TestTrue(TEXT("Rejected rewrite publishes no host plan"),
		Rejected.HostPlans.IsEmpty());
	TestEqual(TEXT("Rejected rewrite publishes no accepted plan hash"),
		Rejected.Summary.CorePlanHash, static_cast<int64>(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3SameSourceCrossBayTargetedTieTest,
	"ABTS.M73DAG.BeamC3.SameSourceCrossBayTargetedTie",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3SameSourceCrossBayTargetedTieTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3Tests;
	FABTSM73BeamAPreviewSettings BeamASettings = SyntheticBeamASettings();
	BeamASettings.MaxJointCount = 1024;
	BeamASettings.MaxMemberCount = 256;
	BeamASettings.MaxBearingContactCount = 1024;
	FABTSM73BeamC3CribCoreSettings CoreSettings = SyntheticCoreSettings();
	CoreSettings.MaximumHostCount = 2;
	CoreSettings.MaximumNetMemberIncrease = 48;
	CoreSettings.MaximumFinalMemberCount = 128;
	CoreSettings.BeamC2MemberReserve = 0;

	FABTSM73BeamAGenerationResult Assembly;
	const int32 HostAssemblyId = AppendFixtureBayAssembly(
		Assembly, 0,
		FBox(FVector(-720.0, -720.0, 0.0),
			FVector(320.0, 320.0, 1200.0)),
		EABTSM73BeamAAssemblyType::CribCore);
	// AppendClosedCoreHostFixture owns its courses through fresh CribCore
	// assemblies in Bay 0. The empty owner above provides the semantic Bay.
	(void)HostAssemblyId;
	const FABTSM73BeamC3CribCoreHostPlan XAnchorHost =
		AppendClosedCoreHostFixture(Assembly, FVector2D(-500.0, 100.0),
			180.0, 1200.0, 600.0, BeamASettings.BlockCrossSectionCM);
	const FABTSM73BeamC3CribCoreHostPlan YAnchorHost =
		AppendClosedCoreHostFixture(Assembly, FVector2D(100.0, -500.0),
			180.0, 1200.0, 600.0, BeamASettings.BlockCrossSectionCM);
	RefreshSyntheticSummary(Assembly);
	FString Error;
	TestTrue(*FString::Printf(TEXT("Two-host base contacts rebuild: %s"),
		*Error), ABTSM73BeamA::RebuildBearingContacts(
			BeamASettings, Assembly, Error));

	FABTSM73BeamC3CribCoreResult CertifiedPlan;
	CertifiedPlan.HostPlans = {XAnchorHost, YAnchorHost};
	CertifiedPlan.Summary.HostCount = CertifiedPlan.HostPlans.Num();
	CertifiedPlan.Summary.BeltCount = 2;
	FABTSM73BeamC3CribCoreGenerator Generator;
	Error.Reset();
	if (!Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
		Assembly, CertifiedPlan, Error))
	{
		AddError(FString::Printf(
			TEXT("Two-host cross-Bay base did not certify: %s"), *Error));
		return false;
	}

	const FVector2D Target(280.0, 280.0);
	const int32 TargetAssemblyId = AppendFixtureBayAssembly(
		Assembly, 0,
		FBox(FVector(240.0, 240.0, 0.0),
			FVector(320.0, 320.0, 1200.0)));
	Assembly.Bays[0].AdjacentBayIds.AddUnique(1);
	Assembly.Bays[1].AdjacentBayIds.AddUnique(0);
	AddFixtureMember(Assembly, TargetAssemblyId,
		FVector(Target.X, Target.Y, 0.0),
		FVector(Target.X, Target.Y, 1200.0),
		EABTSM73BeamAFrameAxis::Z, EABTSM73BeamAMemberRole::Post);
	RefreshSyntheticSummary(Assembly);
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Cross-Bay target contacts rebuild: %s"),
		*Error), ABTSM73BeamA::RebuildBearingContacts(
			BeamASettings, Assembly, Error));

	const int32 ExistingTieCount = CertifiedPlan.TiePlans.Num();
	FABTSM73BeamC3CribCoreResult Repaired;
	Error.Reset();
	const bool bRepaired = Generator.Generate(
		CoreSettings, BeamASettings, Assembly, Repaired, Error, &CertifiedPlan);
	TestTrue(*FString::Printf(
		TEXT("Same-source cross-Bay target is repaired by rooted ties: %s"),
		*Error), bRepaired);
	if (!bRepaired)
	{
		return false;
	}

	bool bHasX = false;
	bool bHasY = false;
	int32 CrossBayTieCount = 0;
	for (int32 TieIndex = ExistingTieCount;
		TieIndex < Repaired.TiePlans.Num(); ++TieIndex)
	{
		const FABTSM73BeamC3TargetedTiePlan& Tie =
			Repaired.TiePlans[TieIndex];
		if (!Tie.TargetStation.Equals(Target,
			BeamASettings.BlockCrossSectionCM))
		{
			continue;
		}
		++CrossBayTieCount;
		bHasX |= Tie.Axis == EABTSM73BeamAFrameAxis::X;
		bHasY |= Tie.Axis == EABTSM73BeamAFrameAxis::Y;
		TestEqual(TEXT("Cross-Bay tie retains the structural Source"),
			Tie.SourceVolumeId, 0);
		TestEqual(TEXT("Cross-Bay tie is owned by an anchor Bay"),
			Tie.BayId, 0);
	}
	TestTrue(TEXT("Repair emits cross-Bay ties to the target"),
		CrossBayTieCount >= 2);
	TestTrue(TEXT("Cross-Bay repair closes the X direction"), bHasX);
	TestTrue(TEXT("Cross-Bay repair closes the Y direction"), bHasY);
	TestTrue(TEXT("Cross-Bay repair is runtime-certified"),
		Repaired.Summary.bStabilityCoreCertified);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3StructuralSourceProgressIsolationTest,
	"ABTS.M73DAG.BeamC3.StructuralSourceProgressIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3StructuralSourceProgressIsolationTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3Tests;
	const FABTSM73BeamAPreviewSettings BeamASettings =
		SyntheticBeamASettings();
	FABTSM73BeamC3CribCoreSettings CoreSettings = SyntheticCoreSettings();
	CoreSettings.MaximumHostCount = 1;
	CoreSettings.MaximumFinalMemberCount = 96;
	FABTSM73BeamAGenerationResult Assembly = BuildRectangularCoreFixture();
	FABTSM73BeamC3CribCoreResult CertifiedPlan;
	FABTSM73BeamC3CribCoreGenerator Generator;
	FString Error;
	if (!Generator.Generate(CoreSettings, BeamASettings,
		Assembly, CertifiedPlan, Error)
		|| !Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
			Assembly, CertifiedPlan, Error))
	{
		AddError(FString::Printf(
			TEXT("Structural-source base core failed: %s"), *Error));
		return false;
	}

	const FABTSM73BeamC3CribCoreHostPlan& Host = CertifiedPlan.HostPlans[0];
	const double Section = BeamASettings.BlockCrossSectionCM;
	double MinimumX = TNumericLimits<double>::Max();
	double MaximumX = -TNumericLimits<double>::Max();
	double MinimumY = TNumericLimits<double>::Max();
	for (const FVector2D& Station : Host.StationPositions)
	{
		MinimumX = FMath::Min(MinimumX, Station.X);
		MaximumX = FMath::Max(MaximumX, Station.X);
		MinimumY = FMath::Min(MinimumY, Station.Y);
	}
	const double CourseCenterZ = Host.BeltMidZs[0] - Section * 0.5;
	const double CourseBottomZ = CourseCenterZ - Section * 0.5;
	const double CourseTopZ = CourseCenterZ + Section * 0.5;
	const FVector2D Target((MinimumX + MaximumX) * 0.5, MinimumY);
	const int32 TargetAssemblyId = AppendFixtureBayAssembly(
		Assembly, 0,
		FBox(FVector(Target.X - Section, Target.Y - Section, 0.0),
			FVector(Target.X + Section, Target.Y + Section, 1200.0)));
	AddFixtureMember(Assembly, TargetAssemblyId,
		FVector(Target.X, Target.Y, 0.0),
		FVector(Target.X, Target.Y, CourseBottomZ),
		EABTSM73BeamAFrameAxis::Z, EABTSM73BeamAMemberRole::Post);
	AddFixtureMember(Assembly, TargetAssemblyId,
		FVector(Target.X, Target.Y, CourseTopZ),
		FVector(Target.X, Target.Y, 1200.0),
		EABTSM73BeamAFrameAxis::Z, EABTSM73BeamAMemberRole::Post);
	RefreshSyntheticSummary(Assembly);
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Source-isolation contacts rebuild: %s"),
		*Error), ABTSM73BeamA::RebuildBearingContacts(
			BeamASettings, Assembly, Error));

	FABTSM73BeamC3CribCoreResult SameSource = CertifiedPlan;
	Error.Reset();
	TestTrue(*FString::Printf(
		TEXT("Same-source biaxial course braces the local station: %s"), *Error),
		Generator.CertifyFinalAssembly(
			CoreSettings, BeamASettings, Assembly, SameSource, Error));

	FABTSM73BeamAGenerationResult CrossSource = Assembly;
	CrossSource.Bays[1].SourceVolumeId = 1;
	RefreshSyntheticSummary(CrossSource);
	FABTSM73BeamC3CribCoreResult CrossSourcePlan = CertifiedPlan;
	Error.Reset();
	TestFalse(TEXT("A physical cross-Source bearing does not certify progress"),
		Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
			CrossSource, CrossSourcePlan, Error));
	TestTrue(TEXT("Cross-Source rejection remains an all-Z failure"),
		Error.StartsWith(TEXT("BeamC3FinalAllZSpanExceeded")));
	TestTrue(TEXT("Cross-Source rejection identifies the target station"),
		Error.Contains(FString::Printf(TEXT("Station=%.1f,%.1f"),
			Target.X, Target.Y)));

	const uint32 BeforeRepairFingerprint = AssemblyFingerprint(CrossSource);
	FABTSM73BeamC3CribCoreResult RejectedRepair;
	Error.Reset();
	TestFalse(TEXT("Local repair cannot claim progress from another Source"),
		Generator.Generate(CoreSettings, BeamASettings, CrossSource,
			RejectedRepair, Error, &CertifiedPlan));
	TestEqual(TEXT("Rejected cross-Source repair is transactional"),
		AssemblyFingerprint(CrossSource), BeforeRepairFingerprint);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3RootedBiaxialFloorDiaphragmContractTest,
	"ABTS.M73DAG.BeamC3.RootedBiaxialFloorDiaphragmContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3RootedBiaxialFloorDiaphragmContractTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3Tests;
	const FABTSM73BeamAPreviewSettings BeamASettings =
		SyntheticBeamASettings();
	FABTSM73BeamC3CribCoreSettings CoreSettings = SyntheticCoreSettings();
	CoreSettings.MaximumFinalMemberCount = 96;
	FRootedFloorNetworkFixture Fixture;
	FString Error;
	if (!BuildRootedFloorNetworkFixture(
		BeamASettings, CoreSettings, Fixture, Error))
	{
		AddError(FString::Printf(
			TEXT("Biaxial floor-diaphragm fixture failed: %s"), *Error));
		return false;
	}

	FABTSM73BeamC3CribCoreGenerator Generator;
	FABTSM73BeamC3CribCoreResult Positive = Fixture.CertifiedPlan;
	Error.Reset();
	TestTrue(*FString::Printf(
		TEXT("Two-root biaxial floor diaphragm certifies: %s"), *Error),
		Generator.CertifyFinalAssembly(
			CoreSettings, BeamASettings, Fixture.Assembly, Positive, Error));
	TestTrue(TEXT("Positive diaphragm publishes rooted ordinary courses"),
		Positive.Summary.RootedExistingCourseCount >= 5);

	// Preserve the exact contact graph and two independent roots, but erase the
	// transverse semantic axis. This isolates the biaxial contract from geometry.
	FABTSM73BeamAGenerationResult Uniaxial = Fixture.Assembly;
	for (FABTSM73BeamAMember& Member : Uniaxial.Members)
	{
		if (Member.Role == EABTSM73BeamAMemberRole::PrimaryBeam)
		{
			Member.Axis = EABTSM73BeamAFrameAxis::X;
		}
	}
	TestTrue(TEXT("Uniaxial variant preserves the left root contact"),
		CountDirectCoreCourseContacts(
			Uniaxial, Fixture.LeftAnchorCourseId) > 0);
	TestTrue(TEXT("Uniaxial variant preserves the right root contact"),
		CountDirectCoreCourseContacts(
			Uniaxial, Fixture.RightAnchorCourseId) > 0);
	FABTSM73BeamC3CribCoreResult UniaxialPlan = Fixture.CertifiedPlan;
	Error.Reset();
	TestFalse(TEXT("A two-root but single-axis floor network fails closed"),
		Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
			Uniaxial, UniaxialPlan, Error));
	TestTrue(TEXT("Uniaxial rejection occurs at the all-Z gate"),
		Error.StartsWith(TEXT("BeamC3FinalAllZSpanExceeded")));

	FABTSM73BeamAGenerationResult SingleRoot = Fixture.Assembly;
	const int32 RemovedRootContacts = SingleRoot.BearingContacts.RemoveAll(
		[&SingleRoot, &Fixture](const FABTSM73BeamABearingContact& Contact)
		{
			const int32 OtherId =
				Contact.LowerMemberId == Fixture.LeftAnchorCourseId
					? Contact.UpperMemberId
					: Contact.UpperMemberId == Fixture.LeftAnchorCourseId
						? Contact.LowerMemberId : INDEX_NONE;
			return SingleRoot.Members.IsValidIndex(OtherId)
				&& SingleRoot.Members[OtherId].Role
					== EABTSM73BeamAMemberRole::CoreCourse;
		});
	RenumberContacts(SingleRoot);
	TestTrue(TEXT("Single-root variant removes one independent root"),
		RemovedRootContacts > 0);
	TestTrue(TEXT("Single-root variant preserves the opposite root"),
		CountDirectCoreCourseContacts(
			SingleRoot, Fixture.RightAnchorCourseId) > 0);
	FABTSM73BeamC3CribCoreResult SingleRootPlan = Fixture.CertifiedPlan;
	Error.Reset();
	TestFalse(TEXT("A biaxial but single-root diaphragm fails closed"),
		Generator.CertifyFinalAssembly(CoreSettings, BeamASettings,
			SingleRoot, SingleRootPlan, Error));
	TestTrue(TEXT("Single-root rejection occurs at the all-Z gate"),
		Error.StartsWith(TEXT("BeamC3FinalAllZSpanExceeded")));
	return true;
}

#endif
