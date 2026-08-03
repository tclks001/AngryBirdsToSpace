// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSM73BeamD1BrickCompiler.h"

#include "ABTSM73BeamAGenerator.h"
#include "ABTSM73BeamBGenerator.h"
#include "ABTSM73BeamCGenerator.h"
#include "ABTSM73BeamD0ProfileCatalog.h"
#include "Building/ABTSM73DAG5BShapeGrammarV2.h"
#include "Misc/Crc.h"

namespace ABTSM73BeamD1
{
	bool Reject(
		FABTSM73BeamD1GenerationResult& Result,
		FString& OutError,
		const FString& Reason)
	{
		Result.Summary.bAccepted = false;
		Result.Summary.RejectReason = Reason;
		OutError = Reason;
		return false;
	}

	EABTSM73BeamD1StructuralRole StructuralRole(
		const EABTSM73BeamAMemberRole Role)
	{
		switch (Role)
		{
		case EABTSM73BeamAMemberRole::BridgeSeat:
		case EABTSM73BeamAMemberRole::BridgePost:
			return EABTSM73BeamD1StructuralRole::Connector;
		case EABTSM73BeamAMemberRole::SecondaryBeam:
		case EABTSM73BeamAMemberRole::RoofCourse:
		case EABTSM73BeamAMemberRole::BridgeRail:
			return EABTSM73BeamD1StructuralRole::SecondaryFrame;
		default:
			return EABTSM73BeamD1StructuralRole::PrimaryFrame;
		}
	}

	EABTSM7BuildingMaterial BaseMaterial(
		const EABTSM73BeamD0MaterialPalette Palette,
		const EABTSM73BeamD1StructuralRole Role)
	{
		if (Role == EABTSM73BeamD1StructuralRole::Connector)
		{
			return EABTSM7BuildingMaterial::Iron;
		}
		switch (Palette)
		{
		case EABTSM73BeamD0MaterialPalette::MasonryWithWoodSeam:
			return EABTSM7BuildingMaterial::Stone;
		case EABTSM73BeamD0MaterialPalette::IronFrameGlassTrigger:
			return EABTSM7BuildingMaterial::Iron;
		default:
			return EABTSM7BuildingMaterial::Wood;
		}
	}

	EABTSM7BuildingMaterial CandidateMaterial(
		const EABTSM73BeamD0MaterialPalette Palette)
	{
		switch (Palette)
		{
		case EABTSM73BeamD0MaterialPalette::LightFrameFragileJoint:
		case EABTSM73BeamD0MaterialPalette::IronFrameGlassTrigger:
			return EABTSM7BuildingMaterial::Glass;
		case EABTSM73BeamD0MaterialPalette::SuspendedStonePod:
			return EABTSM7BuildingMaterial::Stone;
		default:
			return EABTSM7BuildingMaterial::Wood;
		}
	}

	FVector MemberCenter(
		const FABTSM73BeamAMember& Member,
		const FABTSM73BeamAGenerationResult& Assembly)
	{
		return (Assembly.Joints[Member.JointA].LocalPosition
			+ Assembly.Joints[Member.JointB].LocalPosition) * 0.5;
	}

	double LoadForMember(
		const int32 MemberId,
		const FABTSM73BeamCGenerationResult& BeamC)
	{
		return BeamC.Nodes.IsValidIndex(MemberId)
			? BeamC.Nodes[MemberId].AccumulatedLoadKG : 0.0;
	}

	int32 SelectCandidate(
		const FABTSM73BeamD0ResolvedProfile& Profile,
		const FABTSM73BeamAGenerationResult& Assembly,
		const FABTSM73BeamCGenerationResult& BeamC)
	{
		FVector BoundsCenter = FVector::ZeroVector;
		for (const FABTSM73BeamAJoint& Joint : Assembly.Joints)
		{
			BoundsCenter += Joint.LocalPosition;
		}
		if (!Assembly.Joints.IsEmpty())
		{
			BoundsCenter /= Assembly.Joints.Num();
		}
		int32 BestId = INDEX_NONE;
		double BestScore = -TNumericLimits<double>::Max();
		for (const FABTSM73BeamAMember& Member : Assembly.Members)
		{
			if (!Assembly.Joints.IsValidIndex(Member.JointA)
				|| !Assembly.Joints.IsValidIndex(Member.JointB))
			{
				continue;
			}
			const FVector Center = MemberCenter(Member, Assembly);
			const double Load = LoadForMember(Member.MemberId, BeamC);
			double Score = -TNumericLimits<double>::Max();
			switch (Profile.WeaknessIntent)
			{
			case EABTSM73BeamD0WeaknessIntent::ColumnBreak:
				if (Member.Axis == EABTSM73BeamAFrameAxis::Z)
				{
					Score = Load * 1000.0 - Center.Z;
				}
				break;
			case EABTSM73BeamD0WeaknessIntent::SeamRelease:
			case EABTSM73BeamD0WeaknessIntent::SlideRelease:
				if (Member.Role == EABTSM73BeamAMemberRole::BridgeSeat
					|| Member.Role == EABTSM73BeamAMemberRole::BridgeRail)
				{
					Score = 1.0e12 + Load;
				}
				else if (Member.Axis != EABTSM73BeamAFrameAxis::Z)
				{
					Score = Load * 1000.0 + Center.Z;
				}
				break;
			case EABTSM73BeamD0WeaknessIntent::TipOver:
				if (Member.Axis == EABTSM73BeamAFrameAxis::Z)
				{
					Score = FVector2D(
						Center.X - BoundsCenter.X,
						Center.Y - BoundsCenter.Y).SizeSquared()
						+ Load * 100.0 - Center.Z;
				}
				break;
			case EABTSM73BeamD0WeaknessIntent::DropTrigger:
				if (Member.Axis != EABTSM73BeamAFrameAxis::Z)
				{
					Score = Center.Z * 1000.0 + Load;
				}
				break;
			}
			if (Score > BestScore
				|| (FMath::IsNearlyEqual(Score, BestScore)
					&& (BestId == INDEX_NONE || Member.MemberId < BestId)))
			{
				BestScore = Score;
				BestId = Member.MemberId;
			}
		}
		if (BestId == INDEX_NONE && !Assembly.Members.IsEmpty())
		{
			BestId = Assembly.Members[0].MemberId;
		}
		return BestId;
	}

	FVector DimensionsFor(
		const FABTSM73BeamAMember& Member,
		const double Section)
	{
		switch (Member.Axis)
		{
		case EABTSM73BeamAFrameAxis::X:
			return FVector(Member.LengthCM, Section, Section);
		case EABTSM73BeamAFrameAxis::Y:
			return FVector(Section, Member.LengthCM, Section);
		case EABTSM73BeamAFrameAxis::Z:
			return FVector(Section, Section, Member.LengthCM);
		default:
			return FVector::ZeroVector;
		}
	}

	int32 CountStrictPenetrations(
		const TArray<FABTSM73BeamD1BrickBinding>& Bricks,
		const double ToleranceCM)
	{
		TArray<int32> Order;
		Order.Reserve(Bricks.Num());
		for (int32 Index = 0; Index < Bricks.Num(); ++Index)
		{
			Order.Add(Index);
		}
		Order.Sort([&Bricks](const int32 A, const int32 B)
		{
			return Bricks[A].LocalBounds.Min.X < Bricks[B].LocalBounds.Min.X;
		});
		int32 Count = 0;
		for (int32 SortedA = 0; SortedA < Order.Num(); ++SortedA)
		{
			const FBox& A = Bricks[Order[SortedA]].LocalBounds;
			for (int32 SortedB = SortedA + 1; SortedB < Order.Num(); ++SortedB)
			{
				const FBox& B = Bricks[Order[SortedB]].LocalBounds;
				if (B.Min.X >= A.Max.X - ToleranceCM)
				{
					break;
				}
				const FVector Overlap(
					FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X),
					FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y),
					FMath::Min(A.Max.Z, B.Max.Z) - FMath::Max(A.Min.Z, B.Min.Z));
				if (Overlap.X > ToleranceCM
					&& Overlap.Y > ToleranceCM
					&& Overlap.Z > ToleranceCM)
				{
					++Count;
				}
			}
		}
		return Count;
	}

	int64 HashBricks(
		const FABTSM73BeamD1Summary& Summary,
		const TArray<FABTSM73BeamD1BrickBinding>& Bricks)
	{
		FString Signature = FString::Printf(TEXT("P=%s|T=%d|R=%s|U=%lld|"),
			*Summary.GameplayProfileId.ToString(), Summary.DifficultyTier,
			*Summary.ResolvedM7ProfileId.ToString(), Summary.UpstreamBeamHash);
		for (const FABTSM73BeamD1BrickBinding& Brick : Bricks)
		{
			const FVector C = Brick.LocalTransform.GetLocation();
			const FVector D = Brick.BrickSpec.DimensionsCM;
			Signature += FString::Printf(
				TEXT("B=%d:%d:%d:%d:%d:%d:%.4f:%.4f:%.4f:%.4f:%.4f:%.4f|"),
				Brick.BrickId, Brick.MemberId, static_cast<int32>(Brick.Axis),
				static_cast<int32>(Brick.StructuralRole),
				Brick.bWeaknessCandidate ? 1 : 0,
				static_cast<int32>(Brick.DeviceRole),
				C.X, C.Y, C.Z, D.X, D.Y, D.Z);
			Signature += FString::Printf(TEXT("M=%d|"),
				static_cast<int32>(Brick.BrickSpec.Material));
		}
		return static_cast<int64>(FCrc::StrCrc32(*Signature));
	}
}

bool FABTSM73BeamD1BrickCompiler::Generate(
	const FABTSM73BeamD1Settings& Settings,
	FABTSM73BeamD1GenerationResult& OutResult,
	FString& OutError) const
{
	OutResult = FABTSM73BeamD1GenerationResult();
	FABTSM73BeamD0ResolvedProfile Profile;
	if (!FABTSM73BeamD0ProfileCatalog::GetDefault().Resolve(
		Settings.GameplayProfileId, Settings.DifficultyTier,
		Settings.BuildingSeed, Profile, OutError))
	{
		return ABTSM73BeamD1::Reject(OutResult, OutError,
			FString::Printf(TEXT("BeamD1Profile:%s"), *OutError));
	}

	FABTSM73DAG5BV2GenerationResult Silhouette;
	FABTSM73DAG5BShapeGrammarV2 ShapeGenerator;
	if (!ShapeGenerator.Generate(
		Profile.BeamSettings.BeamB.BeamA.Silhouette,
		Silhouette, OutError))
	{
		return ABTSM73BeamD1::Reject(OutResult, OutError,
			FString::Printf(TEXT("BeamD1Silhouette:%s"), *OutError));
	}

	FABTSM73BeamAGenerationResult BeamA;
	FABTSM73BeamAGenerator BeamAGenerator;
	if (!BeamAGenerator.Generate(
		Profile.BeamSettings.BeamB.BeamA,
		Silhouette, BeamA, OutError))
	{
		return ABTSM73BeamD1::Reject(OutResult, OutError,
			FString::Printf(TEXT("BeamD1BeamA:%s"), *OutError));
	}

	FABTSM73BeamBGenerationResult BeamB;
	FABTSM73BeamBGenerator BeamBGenerator;
	if (!BeamBGenerator.Generate(
		Profile.BeamSettings.BeamB,
		Silhouette, BeamA, BeamB, OutError))
	{
		return ABTSM73BeamD1::Reject(OutResult, OutError,
			FString::Printf(TEXT("BeamD1BeamB:%s"), *OutError));
	}

	FABTSM73BeamCGenerationResult BeamC;
	FABTSM73BeamCGenerator BeamCGenerator;
	if (!BeamCGenerator.Generate(
		Profile.BeamSettings, BeamB.ClosedAssembly, BeamC, OutError))
	{
		return ABTSM73BeamD1::Reject(OutResult, OutError,
			FString::Printf(TEXT("BeamD1BeamC:%s"), *OutError));
	}
	return CompileResolved(Profile, BeamB, BeamC, OutResult, OutError);
}

bool FABTSM73BeamD1BrickCompiler::CompileResolved(
	const FABTSM73BeamD0ResolvedProfile& Profile,
	const FABTSM73BeamBGenerationResult& BeamB,
	const FABTSM73BeamCGenerationResult& BeamC,
	FABTSM73BeamD1GenerationResult& OutResult,
	FString& OutError) const
{
	using namespace ABTSM73BeamD1;
	OutResult = FABTSM73BeamD1GenerationResult();
	FABTSM73BeamD1Summary& Summary = OutResult.Summary;
	Summary.GameplayProfileId = Profile.GameplayProfileId;
	Summary.DifficultyTier = Profile.DifficultyTier;
	Summary.ResolvedM7ProfileId = Profile.ResolvedM7ProfileId;
	Summary.ResolvedSettingsHash = Profile.ResolvedSettingsHash;
	Summary.UpstreamBeamHash = BeamB.Summary.ResultHash;

	const FABTSM73BeamAGenerationResult& Assembly = BeamB.ClosedAssembly;
	if (!Profile.bAccepted || !BeamB.Summary.bAccepted
		|| !Assembly.Summary.bAccepted || !BeamC.Summary.bAccepted)
	{
		return Reject(OutResult, OutError, TEXT("BeamD1UpstreamRejected"));
	}
	if (Assembly.Members.IsEmpty())
	{
		return Reject(OutResult, OutError, TEXT("BeamD1EmptyAssembly"));
	}
	const double Section = Profile.BeamSettings.BeamB.BeamA.BlockCrossSectionCM;
	if (!FMath::IsFinite(Section) || Section <= 0.0)
	{
		return Reject(OutResult, OutError, TEXT("BeamD1InvalidCrossSection"));
	}

	const int32 CandidateId = SelectCandidate(Profile, Assembly, BeamC);
	TSet<int32> SeenMembers;
	OutResult.Bricks.Reserve(Assembly.Members.Num());
	for (const FABTSM73BeamAMember& Member : Assembly.Members)
	{
		if (Member.MemberId < 0 || SeenMembers.Contains(Member.MemberId)
			|| !Assembly.Joints.IsValidIndex(Member.JointA)
			|| !Assembly.Joints.IsValidIndex(Member.JointB)
			|| Member.Axis == EABTSM73BeamAFrameAxis::Diagonal)
		{
			return Reject(OutResult, OutError,
				TEXT("BeamD1InvalidMemberReference"));
		}
		const FVector Dimensions = DimensionsFor(Member, Section);
		const FVector A = Assembly.Joints[Member.JointA].LocalPosition;
		const FVector B = Assembly.Joints[Member.JointB].LocalPosition;
		if (Dimensions.GetMin() <= 0.0f || Dimensions.ContainsNaN()
			|| A.ContainsNaN() || B.ContainsNaN()
			|| !FMath::IsNearlyEqual(FVector::Distance(A, B), Member.LengthCM, 0.1f))
		{
			return Reject(OutResult, OutError,
				TEXT("BeamD1InvalidMemberGeometry"));
		}

		FABTSM73BeamD1BrickBinding& Brick = OutResult.Bricks.AddDefaulted_GetRef();
		Brick.BrickId = OutResult.Bricks.Num() - 1;
		Brick.MemberId = Member.MemberId;
		Brick.Axis = Member.Axis;
		Brick.StructuralRole = StructuralRole(Member.Role);
		Brick.bWeaknessCandidate = Member.MemberId == CandidateId;
		if (Brick.bWeaknessCandidate
			&& Profile.DeviceIntent != EABTSM73BeamD0DeviceIntent::None)
		{
			Brick.DeviceRole =
				Profile.DeviceIntent == EABTSM73BeamD0DeviceIntent::HangingMass
				? EABTSM73BeamD1DeviceRole::Payload
				: EABTSM73BeamD1DeviceRole::Anchor;
		}
		Brick.BrickSpec.Material = Brick.bWeaknessCandidate
			? CandidateMaterial(Profile.MaterialPalette)
			: BaseMaterial(Profile.MaterialPalette, Brick.StructuralRole);
		Brick.BrickSpec.DimensionsCM = Dimensions;
		Brick.LocalTransform = FTransform(FQuat::Identity, (A + B) * 0.5);
		const FVector Half = Dimensions * 0.5;
		Brick.LocalBounds = FBox(Brick.LocalTransform.GetLocation() - Half,
			Brick.LocalTransform.GetLocation() + Half);
		Summary.LocalBounds += Brick.LocalBounds;
		SeenMembers.Add(Member.MemberId);

		switch (Brick.BrickSpec.Material)
		{
		case EABTSM7BuildingMaterial::Wood: ++Summary.WoodBrickCount; break;
		case EABTSM7BuildingMaterial::Stone: ++Summary.StoneBrickCount; break;
		case EABTSM7BuildingMaterial::Iron: ++Summary.IronBrickCount; break;
		case EABTSM7BuildingMaterial::Glass: ++Summary.GlassBrickCount; break;
		}
		Summary.WeaknessCandidateCount += Brick.bWeaknessCandidate ? 1 : 0;
		Summary.DeviceRoleCount +=
			Brick.DeviceRole != EABTSM73BeamD1DeviceRole::None ? 1 : 0;
	}

	OutResult.Bricks.Sort([](
		const FABTSM73BeamD1BrickBinding& A,
		const FABTSM73BeamD1BrickBinding& B)
	{
		return A.MemberId < B.MemberId;
	});
	for (int32 Index = 0; Index < OutResult.Bricks.Num(); ++Index)
	{
		OutResult.Bricks[Index].BrickId = Index;
	}
	Summary.MemberCount = Assembly.Members.Num();
	Summary.BrickCount = OutResult.Bricks.Num();
	Summary.CompleteReferenceCount = SeenMembers.Num();
	Summary.StrictPenetrationCount = CountStrictPenetrations(
		OutResult.Bricks,
		FMath::Max(0.01,
			Profile.BeamSettings.BeamB.BeamA.JointMergeToleranceCM + 0.01));
	if (Summary.BrickCount != Summary.MemberCount
		|| Summary.CompleteReferenceCount != Summary.MemberCount)
	{
		return Reject(OutResult, OutError, TEXT("BeamD1IncompleteMemberBinding"));
	}
	if (Summary.WeaknessCandidateCount != 1)
	{
		return Reject(OutResult, OutError, TEXT("BeamD1WeaknessCandidateMissing"));
	}
	if (Summary.StrictPenetrationCount != 0)
	{
		return Reject(OutResult, OutError, TEXT("BeamD1BrickPenetration"));
	}
	Summary.BrickGeometryHash = HashBricks(Summary, OutResult.Bricks);
	Summary.bAccepted = true;
	Summary.RejectReason.Reset();
	OutError.Reset();
	return true;
}
