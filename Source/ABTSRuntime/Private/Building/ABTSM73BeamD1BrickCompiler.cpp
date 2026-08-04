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
	int32 CandidateSeed(const int32 BaseSeed, const int32 Attempt)
	{
		if (Attempt == 0)
		{
			return BaseSeed;
		}
		uint32 Hash = HashCombineFast(GetTypeHash(BaseSeed), 0xD1500001u);
		Hash = HashCombineFast(Hash, GetTypeHash(Attempt));
		return static_cast<int32>(Hash);
	}

	int32 RequiredSupportedSpanCount(
		const EABTSM73DAG5BV2Archetype Archetype,
		const int32 Tier)
	{
		if (Tier < 4)
		{
			return 0;
		}
		return Archetype == EABTSM73DAG5BV2Archetype::BridgedArcology ? 1 : 0;
	}

	bool MeetsVisualMilestone(
		const FABTSM73BeamD0ResolvedProfile& Profile,
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		const FABTSM73BeamBGenerationResult& BeamB)
	{
		static constexpr int32 MinimumVolumes[6] = {3, 6, 8, 16, 21, 25};
		const int32 Tier = FMath::Clamp(Profile.DifficultyTier, 0, 5);
		const int32 RequiredVolumeCount =
			Profile.GameplayProfileId == TEXT("ColumnBreak") && Tier == 5
				? 16 : MinimumVolumes[Tier];
		const int32 RequiredSpans = RequiredSupportedSpanCount(
			Silhouette.Summary.ResolvedArchetype, Tier);
		const int32 RoofPrimitiveCount =
			Silhouette.Summary.PrismCount
			+ Silhouette.Summary.PyramidCount;
		return Silhouette.Summary.VolumeCount >= RequiredVolumeCount
			&& Silhouette.Summary.SupportedSpanCount >= RequiredSpans
			&& (!Profile.VisualComplexity.bRequireSingleTerminalRoof
				|| RoofPrimitiveCount == 1)
			&& (Tier < 2 || BeamB.Summary.DistinctMotifCount >= 2);
	}

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

	void MeasureAssemblyQuality(
		const FABTSM73BeamD0ResolvedProfile& Profile,
		const TArray<FABTSM73BeamD1BrickBinding>& Bricks,
		FABTSM73BeamD1Summary& Summary)
	{
		const double Section =
			Profile.BeamSettings.BeamB.BeamA.BlockCrossSectionCM;
		const double StationTolerance = FMath::Max(
			0.1,
			static_cast<double>(
				Profile.BeamSettings.BeamB.BeamA.JointMergeToleranceCM));
		TSet<int64> XStations;
		TSet<int64> YStations;
		for (const FABTSM73BeamD1BrickBinding& Brick : Bricks)
		{
			if (Brick.Axis != EABTSM73BeamAFrameAxis::Z)
			{
				continue;
			}
			const FVector Center = Brick.LocalTransform.GetLocation();
			XStations.Add(FMath::RoundToInt64(Center.X / StationTolerance));
			YStations.Add(FMath::RoundToInt64(Center.Y / StationTolerance));
		}
		Summary.XColumnStationCount = XStations.Num();
		Summary.YColumnStationCount = YStations.Num();
		const FVector BoundsSize = Summary.LocalBounds.GetSize();
		const double XDensity = BoundsSize.X > Section
			? XStations.Num() / BoundsSize.X : 0.0;
		const double YDensity = BoundsSize.Y > Section
			? YStations.Num() / BoundsSize.Y : 0.0;
		const double MaximumDensity = FMath::Max(XDensity, YDensity);
		Summary.AxisStationDensityRatio = MaximumDensity > UE_DOUBLE_SMALL_NUMBER
			? static_cast<float>(FMath::Min(XDensity, YDensity) / MaximumDensity)
			: 0.0f;
		Summary.StructuralClosurePostRatio = Bricks.IsEmpty()
			? 0.0f
			: static_cast<float>(Summary.AddedStructuralSupportPostCount)
				/ Bricks.Num();
	}

	bool MeetsAssemblyQuality(
		const FABTSM73BeamD0ResolvedProfile& Profile,
		const FABTSM73BeamD1Summary& Summary)
	{
		const bool bBalancedCitadel =
			Profile.BeamSettings.BeamB.BeamA.Silhouette.Archetype
				== EABTSM73DAG5BV2Archetype::TerracedCitadel;
		const float MinimumDensityRatio = Profile.DifficultyTier <= 1
			? 0.08f
			: bBalancedCitadel ? 0.20f : 0.10f;
		const float MaximumClosurePostRatio =
			Profile.DifficultyTier <= 1 ? 0.20f : 0.12f;
		return Summary.XColumnStationCount > 0
			&& Summary.YColumnStationCount > 0
			&& Summary.AxisStationDensityRatio >= MinimumDensityRatio
			&& Summary.StructuralClosurePostRatio <= MaximumClosurePostRatio;
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
	FABTSM73BeamD0ResolvedProfile InitialProfile;
	if (!FABTSM73BeamD0ProfileCatalog::GetDefault().Resolve(
		Settings.GameplayProfileId, Settings.DifficultyTier,
		Settings.BuildingSeed, InitialProfile, OutError))
	{
		return ABTSM73BeamD1::Reject(OutResult, OutError,
			FString::Printf(TEXT("BeamD1Profile:%s"), *OutError));
	}

	const FABTSM73BeamD0VisualComplexityRecipe& Target =
		InitialProfile.VisualComplexity;
	FString LastFailure = TEXT("NoAttempt");
	int32 LastBrickCount = 0;
	for (int32 Attempt = 0; Attempt < Target.MaximumCandidateAttempts; ++Attempt)
	{
		FABTSM73BeamD0ResolvedProfile Profile;
		FString CandidateError;
		if (!FABTSM73BeamD0ProfileCatalog::GetDefault().Resolve(
			Settings.GameplayProfileId, Settings.DifficultyTier,
			ABTSM73BeamD1::CandidateSeed(Settings.BuildingSeed, Attempt),
			Profile, CandidateError))
		{
			LastFailure = FString::Printf(TEXT("Profile:%s"), *CandidateError);
			continue;
		}

		FABTSM73DAG5BV2GenerationResult Silhouette;
		FABTSM73DAG5BShapeGrammarV2 ShapeGenerator;
		if (!ShapeGenerator.Generate(
			Profile.BeamSettings.BeamB.BeamA.Silhouette,
			Silhouette, CandidateError))
		{
			LastFailure = FString::Printf(TEXT("Silhouette:%s"), *CandidateError);
			continue;
		}

		FABTSM73BeamAGenerationResult BeamA;
		FABTSM73BeamAGenerator BeamAGenerator;
		if (!BeamAGenerator.Generate(
			Profile.BeamSettings.BeamB.BeamA,
			Silhouette, BeamA, CandidateError))
		{
			LastFailure = FString::Printf(TEXT("BeamA:%s"), *CandidateError);
			continue;
		}

		FABTSM73BeamBGenerationResult BeamB;
		FABTSM73BeamBGenerator BeamBGenerator;
		if (!BeamBGenerator.Generate(
			Profile.BeamSettings.BeamB,
			Silhouette, BeamA, BeamB, CandidateError))
		{
			LastFailure = FString::Printf(TEXT("BeamB:%s"), *CandidateError);
			continue;
		}

		FABTSM73BeamCGenerationResult BeamC;
		FABTSM73BeamCGenerator BeamCGenerator;
		if (!BeamCGenerator.GenerateWithStructuralClosure(
			Profile.BeamSettings, BeamB.ClosedAssembly, BeamC, CandidateError))
		{
			LastFailure = FString::Printf(
				TEXT("BeamC:%s:Contact=%d:Resultant=%d:Spread=%d:Span=%d:Cantilever=%d"),
				*CandidateError,
				BeamC.Summary.RealContactMismatchCount,
				BeamC.Summary.SupportResultantViolationCount,
				BeamC.Summary.SupportSpreadViolationCount,
				BeamC.Summary.SpanViolationCount,
				BeamC.Summary.CantileverViolationCount);
			continue;
		}
		BeamB.Summary.ClosedMemberCount = BeamB.ClosedAssembly.Members.Num();
		BeamB.Summary.ClosedBearingContactCount =
			BeamB.ClosedAssembly.BearingContacts.Num();
		BeamB.Summary.ResultHash = static_cast<int64>(HashCombineFast(
			static_cast<uint32>(BeamB.Summary.ResultHash),
			static_cast<uint32>(BeamC.Summary.LoadDAGHash)));

		FABTSM73BeamD1GenerationResult Candidate;
		if (!CompileResolved(Profile, BeamB, BeamC, Candidate, CandidateError))
		{
			LastFailure = FString::Printf(TEXT("Compile:%s"), *CandidateError);
			continue;
		}
		LastBrickCount = Candidate.Summary.BrickCount;
		Candidate.Summary.bAssemblyQualityCertified =
			ABTSM73BeamD1::MeetsAssemblyQuality(Profile, Candidate.Summary);
		if (!Candidate.Summary.bAssemblyQualityCertified)
		{
			LastFailure = FString::Printf(
				TEXT("AssemblyQualityNotMet:XStations=%d:YStations=%d:Density=%.3f:Closure=%.3f"),
				Candidate.Summary.XColumnStationCount,
				Candidate.Summary.YColumnStationCount,
				Candidate.Summary.AxisStationDensityRatio,
				Candidate.Summary.StructuralClosurePostRatio);
			continue;
		}
		if (!ABTSM73BeamD1::MeetsVisualMilestone(Profile, Silhouette, BeamB))
		{
			LastFailure = FString::Printf(
				TEXT("VisualMilestoneNotMet:Volumes=%d:Motifs=%d:Spans=%d:RequiredSpans=%d"),
				Silhouette.Summary.VolumeCount,
				BeamB.Summary.DistinctMotifCount,
				Silhouette.Summary.SupportedSpanCount,
				ABTSM73BeamD1::RequiredSupportedSpanCount(
					Silhouette.Summary.ResolvedArchetype,
					Profile.DifficultyTier));
			continue;
		}
		if (LastBrickCount < Target.MinimumBrickCount
			|| LastBrickCount > Target.MaximumBrickCount)
		{
			LastFailure = TEXT("BrickCountOutsideTarget");
			continue;
		}

		Candidate.Summary.TargetMinimumBrickCount =
			Target.MinimumBrickCount;
		Candidate.Summary.TargetMaximumBrickCount =
			Target.MaximumBrickCount;
		Candidate.Summary.VisualCandidateAttempt = Attempt;
		Candidate.Summary.SemanticVolumeCount =
			Silhouette.Summary.VolumeCount;
		Candidate.Summary.SemanticBoxCount =
			Silhouette.Summary.BoxCount;
		Candidate.Summary.SemanticPrismCount =
			Silhouette.Summary.PrismCount;
		Candidate.Summary.SemanticPyramidCount =
			Silhouette.Summary.PyramidCount;
		Candidate.Summary.DistinctMotifCount =
			BeamB.Summary.DistinctMotifCount;
		Candidate.Summary.SupportedSpanCount =
			Silhouette.Summary.SupportedSpanCount;
		Candidate.Summary.bVisualComplexityCertified = true;
		OutResult = MoveTemp(Candidate);
		OutError.Reset();
		return true;
	}

	return ABTSM73BeamD1::Reject(
		OutResult,
		OutError,
		FString::Printf(
			TEXT("BeamD15NoCandidateInBrickWindow:Attempts=%d:Last=%s:Bricks=%d"),
			Target.MaximumCandidateAttempts,
			*LastFailure,
			LastBrickCount));
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
	Summary.StructuralClosurePassCount =
		BeamC.Summary.StructuralClosurePassCount;
	Summary.AddedStructuralSupportPostCount =
		BeamC.Summary.AddedStructuralSupportPostCount;
	Summary.RealContactMismatchCount =
		BeamC.Summary.RealContactMismatchCount;
	Summary.RemainingSupportViolationCount =
		BeamC.Summary.SupportResultantViolationCount
		+ BeamC.Summary.SupportSpreadViolationCount;
	Summary.SupportResultantAdvisoryCount =
		BeamC.Summary.SupportResultantAdvisoryCount;

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
		Summary.RoofCourseBrickCount +=
			Member.Role == EABTSM73BeamAMemberRole::RoofCourse ? 1 : 0;
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
	MeasureAssemblyQuality(Profile, OutResult.Bricks, Summary);
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
