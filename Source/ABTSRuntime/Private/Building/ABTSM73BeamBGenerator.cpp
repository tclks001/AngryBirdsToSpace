// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BeamBGenerator.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM73BeamAGenerator.h"
#include "Building/ABTSM73DAG5BShapeGrammarV2.h"
#include "Misc/Crc.h"

namespace ABTSM73BeamB
{
	constexpr uint8 PortNegX = 1 << 0;
	constexpr uint8 PortPosX = 1 << 1;
	constexpr uint8 PortNegY = 1 << 2;
	constexpr uint8 PortPosY = 1 << 3;
	constexpr uint8 PortLower = 1 << 4;
	constexpr uint8 PortUpper = 1 << 5;

	bool IsSupportedSpanRole(const EABTSM73DAG5BV2VolumeRole Role)
	{
		return Role == EABTSM73DAG5BV2VolumeRole::Bridge
			|| Role == EABTSM73DAG5BV2VolumeRole::SupportedSpan;
	}

	uint8 AxisPorts(const EABTSM73BeamAFrameAxis Axis)
	{
		return Axis == EABTSM73BeamAFrameAxis::Y
			? PortNegY | PortPosY
			: PortNegX | PortPosX;
	}

	uint8 PortsForMotif(
		const EABTSM73BeamBMotif Motif,
		const EABTSM73BeamAFrameAxis Axis)
	{
		const uint8 Primary = AxisPorts(Axis);
		switch (Motif)
		{
		case EABTSM73BeamBMotif::CrossBeam:
		case EABTSM73BeamBMotif::TwoLayerCrib:
		case EABTSM73BeamBMotif::TransferFrame:
			return PortNegX | PortPosX | PortNegY | PortPosY
				| PortLower | PortUpper;
		case EABTSM73BeamBMotif::CantileverBay:
			return Primary | PortLower;
		case EABTSM73BeamBMotif::BridgeBay:
			// A Bridge volume may touch its endpoint bodies through a side or
			// vertical face after Shape Grammar setbacks. The motif exposes a
			// transfer tie on every face while its long beams still follow Axis.
			return PortNegX | PortPosX | PortNegY | PortPosY
				| PortLower | PortUpper;
		case EABTSM73BeamBMotif::PostAndLintel:
		case EABTSM73BeamBMotif::PortalFrame:
		case EABTSM73BeamBMotif::BracedBay:
		default:
			return Primary | PortLower | PortUpper;
		}
	}

	uint8 OppositePort(const uint8 Port)
	{
		switch (Port)
		{
		case PortNegX: return PortPosX;
		case PortPosX: return PortNegX;
		case PortNegY: return PortPosY;
		case PortPosY: return PortNegY;
		case PortLower: return PortUpper;
		case PortUpper: return PortLower;
		default: return 0;
		}
	}

	uint8 PortToward(
		const FABTSM73BeamABay& A,
		const FABTSM73BeamABay& B)
	{
		const FVector Delta = B.LocalBounds.GetCenter()
			- A.LocalBounds.GetCenter();
		const FVector Abs = Delta.GetAbs();
		if (Abs.Z > Abs.X && Abs.Z > Abs.Y)
		{
			return Delta.Z >= 0.0 ? PortUpper : PortLower;
		}
		if (Abs.Y > Abs.X)
		{
			return Delta.Y >= 0.0 ? PortPosY : PortNegY;
		}
		return Delta.X >= 0.0 ? PortPosX : PortNegX;
	}

	bool Compatible(
		const FABTSM73BeamABay& A,
		const EABTSM73BeamBMotif MotifA,
		const FABTSM73BeamABay& B,
		const EABTSM73BeamBMotif MotifB)
	{
		const uint8 Toward = PortToward(A, B);
		return (PortsForMotif(MotifA, A.PreferredAxis) & Toward) != 0
			&& (PortsForMotif(MotifB, B.PreferredAxis)
				& OppositePort(Toward)) != 0;
	}

	const FABTSM73DAG5BV2Volume* FindVolume(
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		const int32 VolumeId)
	{
		return Silhouette.Volumes.FindByPredicate(
			[VolumeId](const FABTSM73DAG5BV2Volume& Volume)
			{
				return Volume.VolumeId == VolumeId;
			});
	}

	FString SemanticModulePath(const FString& Path)
	{
		TArray<FString> Parts;
		Path.ParseIntoArray(Parts, TEXT("/"), true);
		return Parts.Num() >= 2
			? Parts[0] + TEXT("/") + Parts[1]
			: Path;
	}

	const FABTSM73DAG5BV2Volume* ResolveEndpointSupportVolume(
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		const int32 DeclaredSupportVolumeId,
		const int32 SpanAxis,
		const double BearingPlane,
		const double PerpendicularMin,
		const double PerpendicularMax,
		const double SeatMinZ,
		const double SeatMaxZ,
		const bool bNegativeEndpoint)
	{
		const FABTSM73DAG5BV2Volume* Declared =
			FindVolume(Silhouette, DeclaredSupportVolumeId);
		if (Declared == nullptr)
		{
			return nullptr;
		}
		const FString ModulePath = SemanticModulePath(Declared->DerivationPath);
		const int32 Perpendicular = SpanAxis == 0 ? 1 : 0;
		const FABTSM73DAG5BV2Volume* Best = nullptr;
		double BestAxisDistance = TNumericLimits<double>::Max();
		double BestScore = TNumericLimits<double>::Max();
		for (const FABTSM73DAG5BV2Volume& Candidate : Silhouette.Volumes)
		{
			if (Candidate.Role == EABTSM73DAG5BV2VolumeRole::SupportedSpan
				|| SemanticModulePath(Candidate.DerivationPath) != ModulePath)
			{
				continue;
			}
			const double PerpendicularOverlap = FMath::Min(
				PerpendicularMax, Candidate.LocalBounds.Max[Perpendicular])
				- FMath::Max(
					PerpendicularMin, Candidate.LocalBounds.Min[Perpendicular]);
			if (PerpendicularOverlap <= 0.1)
			{
				continue;
			}
			const double AxisDistance = bNegativeEndpoint
				? FMath::Abs(BearingPlane - Candidate.LocalBounds.Max[SpanAxis])
				: FMath::Abs(Candidate.LocalBounds.Min[SpanAxis] - BearingPlane);
			const double VerticalDistance = SeatMaxZ < Candidate.LocalBounds.Min.Z
				? Candidate.LocalBounds.Min.Z - SeatMaxZ
				: SeatMinZ > Candidate.LocalBounds.Max.Z
					? SeatMinZ - Candidate.LocalBounds.Max.Z : 0.0;
			const double Score = VerticalDistance
				- PerpendicularOverlap * 0.01;
			if (AxisDistance < BestAxisDistance - 0.1
				|| (FMath::IsNearlyEqual(AxisDistance, BestAxisDistance, 0.1)
					&& (Score < BestScore
						|| (FMath::IsNearlyEqual(Score, BestScore)
					&& (Best == nullptr || Candidate.VolumeId < Best->VolumeId)))
					))
			{
				Best = &Candidate;
				BestAxisDistance = AxisDistance;
				BestScore = Score;
			}
		}
		return Best;
	}

	void BuildDomain(
		const FABTSM73BeamBPreviewSettings& Settings,
		const FABTSM73BeamABay& Bay,
		const FABTSM73DAG5BV2Volume& Volume,
		TArray<EABTSM73BeamBMotif>& OutDomain)
	{
		OutDomain.Reset();
		if (IsSupportedSpanRole(Volume.Role))
		{
			OutDomain.Add(EABTSM73BeamBMotif::BridgeBay);
			return;
		}
		if (Volume.Primitive != EABTSM73DAG5BV2Primitive::Box)
		{
			// Non-box Bays retain Beam-A's authoritative layered roof assembly.
			// TwoLayerCrib is the matching semantic Motif; rectangular portal
			// variants would erase the prism/pyramid silhouette.
			OutDomain.Add(EABTSM73BeamBMotif::TwoLayerCrib);
			return;
		}

		OutDomain.Append({
			EABTSM73BeamBMotif::PostAndLintel,
			EABTSM73BeamBMotif::PortalFrame,
			EABTSM73BeamBMotif::CrossBeam,
			EABTSM73BeamBMotif::TwoLayerCrib});
		if (Volume.Role == EABTSM73DAG5BV2VolumeRole::Body
			|| Volume.Role == EABTSM73DAG5BV2VolumeRole::Foundation)
		{
			OutDomain.Add(EABTSM73BeamBMotif::TransferFrame);
		}
		// CantileverBay remains a serialized enum value only. A side volume
		// that needs an invented ground post is no longer a legal WFC family.
		// BracedBay remains a reserved semantic value, but Beam-B deliberately
		// keeps it out of the domain until a visible, non-penetrating brace seat
		// and an explicit physical connection contract exist.
	}

	uint32 ChoiceHash(
		const FABTSM73BeamBPreviewSettings& Settings,
		const int32 BayId,
		const int32 Salt)
	{
		uint32 Hash = GetTypeHash(Settings.BeamA.Silhouette.BuildingSeed);
		Hash = HashCombineFast(Hash, GetTypeHash(BayId));
		return HashCombineFast(Hash, GetTypeHash(Salt));
	}

	struct FGeometryBuilder
	{
		const FABTSM73BeamBPreviewSettings& Settings;
		FABTSM73BeamBGenerationResult& Result;
		FString* Error = nullptr;

		bool Add(
			const int32 BayId,
			const EABTSM73BeamBMotif Motif,
			const EABTSM73BeamAFrameAxis Axis,
			const EABTSM73BeamAMemberRole Role,
			const FVector& Start,
			const FVector& End)
		{
			if (Axis == EABTSM73BeamAFrameAxis::Diagonal)
			{
				*Error = TEXT("BeamBDiagonalMembersDisabled");
				return false;
			}
			if (Result.PlannedMembers.Num()
				>= Settings.MaxPlannedMemberCount)
			{
				*Error = TEXT("BeamBPlannedMemberBudgetExceeded");
				return false;
			}
			const double Length = (End - Start).Size();
			if (Length + Settings.BeamA.JointMergeToleranceCM
				< Settings.BeamA.BlockCrossSectionCM)
			{
				// A motif may collapse a decorative branch in a narrow Bay.
				// Such a branch cannot become a physical block, so omit it
				// before compiling the motif into the Beam-A structural IR.
				return true;
			}
			FABTSM73BeamBPlannedMember& Member =
				Result.PlannedMembers.AddDefaulted_GetRef();
			Member.PlannedMemberId = Result.PlannedMembers.Num() - 1;
			Member.BayId = BayId;
			Member.Motif = Motif;
			Member.Axis = Axis;
			Member.Role = Role;
			Member.LocalStart = Start;
			Member.LocalEnd = End;
			return true;
		}

		bool AddStep(
			const int32 BayId,
			const EABTSM73BeamBGrammarRule Rule,
			const int32 AddedMemberCount)
		{
			if (Result.GrammarSteps.Num() >= Settings.MaxGrammarStepCount)
			{
				*Error = TEXT("BeamBGrammarStepBudgetExceeded");
				return false;
			}
			FABTSM73BeamBGrammarStep& Step =
				Result.GrammarSteps.AddDefaulted_GetRef();
			Step.StepId = Result.GrammarSteps.Num() - 1;
			Step.BayId = BayId;
			Step.Rule = Rule;
			Step.AddedMemberCount = AddedMemberCount;
			return true;
		}
	};

	struct FBayCoordinates
	{
		double U0 = 0.0;
		double U1 = 0.0;
		double V0 = 0.0;
		double V1 = 0.0;
		double Z0 = 0.0;
		double Z1 = 0.0;
		EABTSM73BeamAFrameAxis PrimaryAxis = EABTSM73BeamAFrameAxis::X;

		FVector P(const double U, const double V, const double Z) const
		{
			return PrimaryAxis == EABTSM73BeamAFrameAxis::Y
				? FVector(V, U, Z)
				: FVector(U, V, Z);
		}

		EABTSM73BeamAFrameAxis CrossAxis() const
		{
			return PrimaryAxis == EABTSM73BeamAFrameAxis::Y
				? EABTSM73BeamAFrameAxis::X
				: EABTSM73BeamAFrameAxis::Y;
		}
	};

	FBayCoordinates Coordinates(
		const FABTSM73BeamABay& Bay,
		const double Thickness,
		const EABTSM73BeamAFrameAxis PrimaryAxis)
	{
		FBayCoordinates C;
		C.PrimaryAxis = PrimaryAxis;
		if (C.PrimaryAxis == EABTSM73BeamAFrameAxis::Y)
		{
			C.U0 = Bay.LocalBounds.Min.Y + Thickness * 0.5;
			C.U1 = Bay.LocalBounds.Max.Y - Thickness * 0.5;
			C.V0 = Bay.LocalBounds.Min.X + Thickness * 0.5;
			C.V1 = Bay.LocalBounds.Max.X - Thickness * 0.5;
		}
		else
		{
			C.U0 = Bay.LocalBounds.Min.X + Thickness * 0.5;
			C.U1 = Bay.LocalBounds.Max.X - Thickness * 0.5;
			C.V0 = Bay.LocalBounds.Min.Y + Thickness * 0.5;
			C.V1 = Bay.LocalBounds.Max.Y - Thickness * 0.5;
		}
		C.Z0 = Bay.LocalBounds.Min.Z + Thickness * 0.5;
		C.Z1 = Bay.LocalBounds.Max.Z - Thickness * 0.5;
		return C;
	}

	bool AddPrimary(
		FGeometryBuilder& B,
		const FABTSM73BeamBPlacement& P,
		const FBayCoordinates& C,
		const double V,
		const double Z,
		const double U0,
		const double U1)
	{
		return B.Add(P.BayId, P.Motif, C.PrimaryAxis,
			EABTSM73BeamAMemberRole::PrimaryBeam,
			C.P(U0, V, Z), C.P(U1, V, Z));
	}

	bool AddCross(
		FGeometryBuilder& B,
		const FABTSM73BeamBPlacement& P,
		const FBayCoordinates& C,
		const double U,
		const double Z)
	{
		return B.Add(P.BayId, P.Motif, C.CrossAxis(),
			EABTSM73BeamAMemberRole::SecondaryBeam,
			C.P(U, C.V0, Z), C.P(U, C.V1, Z));
	}

	bool AddPost(
		FGeometryBuilder& B,
		const FABTSM73BeamBPlacement& P,
		const FBayCoordinates& C,
		const double U,
		const double V,
		const double Z0,
		const double Z1)
	{
		return B.Add(P.BayId, P.Motif, EABTSM73BeamAFrameAxis::Z,
			EABTSM73BeamAMemberRole::Post,
			C.P(U, V, Z0), C.P(U, V, Z1));
	}

	bool BuildMotif(
		FGeometryBuilder& B,
		const FABTSM73BeamABay& Bay,
		FABTSM73BeamBPlacement& Placement)
	{
		const double T = B.Settings.BeamA.BlockCrossSectionCM;
		const FBayCoordinates C = Coordinates(Bay, T, Placement.Orientation);
		if (C.U1 <= C.U0 || C.V1 <= C.V0 || C.Z1 <= C.Z0)
		{
			*B.Error = TEXT("BeamBBayTooSmallForMotif");
			return false;
		}
		const double UM = (C.U0 + C.U1) * 0.5;
		const double VM = (C.V0 + C.V1) * 0.5;
		const double PostBottom = FMath::Min(C.Z0 + T * 0.5, C.Z1);
		const double PostTop = FMath::Max(C.Z1 - T * 0.5, PostBottom);
		const int32 First = B.Result.PlannedMembers.Num();
		const int32 Depth = B.Settings.GrammarDepth;

		auto AddPortal = [&]()
		{
			return AddPrimary(B, Placement, C, VM, C.Z0, C.U0, C.U1)
				&& AddPost(B, Placement, C, C.U0, VM, PostBottom, PostTop)
				&& AddPost(B, Placement, C, C.U1, VM, PostBottom, PostTop)
				&& AddPrimary(B, Placement, C, VM, C.Z1, C.U0, C.U1);
		};

		switch (Placement.Motif)
		{
		case EABTSM73BeamBMotif::PostAndLintel:
			if (!AddPost(B, Placement, C, C.U0, VM, Bay.LocalBounds.Min.Z, PostTop)
				|| !AddPost(B, Placement, C, C.U1, VM, Bay.LocalBounds.Min.Z, PostTop)
				|| !AddPrimary(B, Placement, C, VM, C.Z1, C.U0, C.U1))
			{
				return false;
			}
			break;

		case EABTSM73BeamBMotif::PortalFrame:
		case EABTSM73BeamBMotif::BracedBay:
			if (!AddPortal())
			{
				return false;
			}
			for (int32 Level = 1; Level < Depth; ++Level)
			{
				const double Alpha = static_cast<double>(Level) / Depth;
				if (!AddPrimary(B, Placement, C, VM,
					FMath::Lerp(C.Z0, C.Z1, Alpha), C.U0, C.U1)
					|| !B.AddStep(Placement.BayId,
						EABTSM73BeamBGrammarRule::RefinePortal, 1))
				{
					return false;
				}
			}
			break;

		case EABTSM73BeamBMotif::CrossBeam:
		{
			const double TopPrimary = FMath::Max(C.Z0, C.Z1 - T);
			if (!AddPrimary(B, Placement, C, C.V0, C.Z0, C.U0, C.U1)
				|| !AddPrimary(B, Placement, C, C.V1, C.Z0, C.U0, C.U1)
				|| !AddCross(B, Placement, C, C.U0, C.Z0 + T)
				|| !AddCross(B, Placement, C, C.U1, C.Z0 + T)
				|| !AddPost(B, Placement, C, C.U0, C.V0, C.Z0 + T * 1.5, TopPrimary - T * 0.5)
				|| !AddPost(B, Placement, C, C.U1, C.V1, C.Z0 + T * 1.5, TopPrimary - T * 0.5)
				|| !AddPrimary(B, Placement, C, C.V0, TopPrimary, C.U0, C.U1)
				|| !AddPrimary(B, Placement, C, C.V1, TopPrimary, C.U0, C.U1)
				|| !AddCross(B, Placement, C, C.U0, C.Z1)
				|| !AddCross(B, Placement, C, C.U1, C.Z1))
			{
				return false;
			}
			for (int32 Index = 1; Index < Depth; ++Index)
			{
				const double U = FMath::Lerp(C.U0, C.U1,
					static_cast<double>(Index) / Depth);
				if (!AddCross(B, Placement, C, U, C.Z1)
					|| !B.AddStep(Placement.BayId,
						EABTSM73BeamBGrammarRule::BeamToGrillage, 1))
				{
					return false;
				}
			}
			break;
		}

		case EABTSM73BeamBMotif::TwoLayerCrib:
		{
			const int32 MaxLayers = FMath::Max(2,
				FMath::FloorToInt((C.Z1 - C.Z0) / T) + 1);
			const int32 LayerCount = FMath::Min(MaxLayers, 2 + Depth);
			for (int32 Layer = 0; Layer < LayerCount; ++Layer)
			{
				const double Z = C.Z0 + Layer * T;
				const bool bPrimary = (Layer % 2) == 0;
				const bool bAdded = bPrimary
					? AddPrimary(B, Placement, C, C.V0, Z, C.U0, C.U1)
						&& AddPrimary(B, Placement, C, C.V1, Z, C.U0, C.U1)
					: AddCross(B, Placement, C, C.U0, Z)
						&& AddCross(B, Placement, C, C.U1, Z);
				if (!bAdded)
				{
					return false;
				}
				if (Layer >= 2 && !B.AddStep(Placement.BayId,
					EABTSM73BeamBGrammarRule::AlternateCribLayer, 2))
				{
					return false;
				}
			}
			break;
		}

		case EABTSM73BeamBMotif::TransferFrame:
		{
			const double MidZ = (C.Z0 + C.Z1) * 0.5;
			const double Quarter = (C.U1 - C.U0) * 0.25;
			if (!AddPrimary(B, Placement, C, VM, C.Z0, C.U0, C.U1)
				|| !AddPost(B, Placement, C, UM - Quarter, VM, PostBottom, MidZ - T * 0.5)
				|| !AddPost(B, Placement, C, UM + Quarter, VM, PostBottom, MidZ - T * 0.5)
				|| !AddPrimary(B, Placement, C, VM, MidZ, C.U0, C.U1)
				|| !AddPrimary(B, Placement, C, VM, C.Z1, C.U0, C.U1))
			{
				return false;
			}
			const int32 UpperPosts = FMath::Clamp(2 + Depth, 3, 6);
			for (int32 Index = 0; Index < UpperPosts; ++Index)
			{
				const double Alpha = UpperPosts == 1 ? 0.5
					: static_cast<double>(Index) / (UpperPosts - 1);
				if (!AddPost(B, Placement, C,
					FMath::Lerp(C.U0, C.U1, Alpha), VM,
					MidZ + T * 0.5, PostTop))
				{
					return false;
				}
			}
			if (!B.AddStep(Placement.BayId,
				EABTSM73BeamBGrammarRule::AddTransferTier, UpperPosts))
			{
				return false;
			}
			break;
		}

		case EABTSM73BeamBMotif::CantileverBay:
		{
			const double Root0 = FMath::Lerp(C.U0, C.U1, 0.20);
			const double Root1 = FMath::Lerp(C.U0, C.U1, 0.45);
			if (!AddPrimary(B, Placement, C, VM, C.Z0, C.U0, Root1)
				|| !AddPost(B, Placement, C, Root0, VM, PostBottom, PostTop)
				|| !AddPost(B, Placement, C, Root1, VM, PostBottom, PostTop)
				|| !AddPrimary(B, Placement, C, VM, C.Z1, C.U0, C.U1))
			{
				return false;
			}
			for (int32 Index = 0; Index < Depth; ++Index)
			{
				const double Z = FMath::Lerp(C.Z0, C.Z1,
					static_cast<double>(Index + 1) / (Depth + 1));
				if (!AddCross(B, Placement, C, Root0, Z)
					|| !B.AddStep(Placement.BayId,
						EABTSM73BeamBGrammarRule::AddCantileverRoot, 1))
				{
					return false;
				}
			}
			break;
		}

		case EABTSM73BeamBMotif::BridgeBay:
			if (!AddPrimary(B, Placement, C, C.V0, C.Z0, C.U0, C.U1)
				|| !AddPrimary(B, Placement, C, C.V1, C.Z0, C.U0, C.U1)
				|| !AddCross(B, Placement, C, C.U0, C.Z0 + T)
				|| !AddCross(B, Placement, C, C.U1, C.Z0 + T))
			{
				return false;
			}
			for (int32 Index = 1; Index <= Depth; ++Index)
			{
				const double U = FMath::Lerp(C.U0, C.U1,
					static_cast<double>(Index) / (Depth + 1));
				if (!AddCross(B, Placement, C, U, C.Z0 + T)
					|| !B.AddStep(Placement.BayId,
						EABTSM73BeamBGrammarRule::BeamToGrillage, 1))
				{
					return false;
				}
			}
			break;
		}

		Placement.FirstPlannedMemberIndex = First;
		Placement.PlannedMemberCount = B.Result.PlannedMembers.Num() - First;
		return Placement.PlannedMemberCount > 0;
	}

	FBox PlannedMemberBounds(
		const FABTSM73BeamBPlannedMember& Member,
		const double CrossSectionCM)
	{
		const FVector Center = (Member.LocalStart + Member.LocalEnd) * 0.5;
		FVector Extent(CrossSectionCM * 0.5);
		const int32 AxisIndex = static_cast<int32>(Member.Axis);
		if (AxisIndex >= 0 && AxisIndex <= 2)
		{
			Extent[AxisIndex] =
				(Member.LocalEnd - Member.LocalStart).Size() * 0.5;
		}
		return FBox(Center - Extent, Center + Extent);
	}

	const FABTSM73BeamABay* FindEndpointSupportBay(
		const FABTSM73BeamAGenerationResult& BeamA,
		const int32 SupportVolumeId,
		const int32 SpanAxis,
		const double BearingPlane,
		const double PerpendicularCenter,
		const double SeatCenterZ)
	{
		const int32 Perpendicular = SpanAxis == 0 ? 1 : 0;
		const FABTSM73BeamABay* Best = nullptr;
		double BestScore = TNumericLimits<double>::Max();
		for (const FABTSM73BeamABay& Bay : BeamA.Bays)
		{
			if (Bay.SourceVolumeId != SupportVolumeId)
			{
				continue;
			}
			auto DistanceToInterval = [](const double Value,
				const double Minimum, const double Maximum)
			{
				return Value < Minimum ? Minimum - Value
					: Value > Maximum ? Value - Maximum : 0.0;
			};
			const double AxisDistance = DistanceToInterval(
				BearingPlane, Bay.LocalBounds.Min[SpanAxis],
				Bay.LocalBounds.Max[SpanAxis]);
			const double PerpendicularDistance = DistanceToInterval(
				PerpendicularCenter, Bay.LocalBounds.Min[Perpendicular],
				Bay.LocalBounds.Max[Perpendicular]);
			const double VerticalDistance = DistanceToInterval(
				SeatCenterZ, Bay.LocalBounds.Min.Z, Bay.LocalBounds.Max.Z);
			const double Score = AxisDistance * 16.0
				+ PerpendicularDistance * 4.0 + VerticalDistance;
			if (Score < BestScore
				|| (FMath::IsNearlyEqual(Score, BestScore)
					&& (Best == nullptr || Bay.BayId < Best->BayId)))
			{
				Best = &Bay;
				BestScore = Score;
			}
		}
		return Best;
	}

	bool AddBridgeEndpointSeat(
		FGeometryBuilder& B,
		const FABTSM73DAG5BV2Volume& Span,
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		const FABTSM73BeamAGenerationResult& BeamA,
		const FABTSM73BeamABay& BridgeBay,
		const int32 DeclaredSupportVolumeId,
		const bool bNegativeEndpoint)
	{
		const int32 SpanAxis = Span.SpanAxisIndex;
		if (SpanAxis != 0 && SpanAxis != 1)
		{
			*B.Error = TEXT("BeamBBridgeSeatInvalidSpanAxis");
			return false;
		}
		const int32 Perpendicular = SpanAxis == 0 ? 1 : 0;
		const double T = B.Settings.BeamA.BlockCrossSectionCM;
		const double Tolerance = B.Settings.BeamA.JointMergeToleranceCM;
		const double BearingPlane = bNegativeEndpoint
			? Span.SpanOpeningMinCM : Span.SpanOpeningMaxCM;
		double LowestRailCenterZ = TNumericLimits<double>::Max();
		TArray<FABTSM73BeamBPlannedMember*> CandidateRails;
		for (FABTSM73BeamBPlannedMember& Member :
			B.Result.PlannedMembers)
		{
			if (Member.BayId != BridgeBay.BayId
				|| Member.Motif != EABTSM73BeamBMotif::BridgeBay
				|| Member.Role != EABTSM73BeamAMemberRole::PrimaryBeam
				|| static_cast<int32>(Member.Axis) != SpanAxis)
			{
				continue;
			}
			const double CenterZ =
				(Member.LocalStart.Z + Member.LocalEnd.Z) * 0.5;
			LowestRailCenterZ = FMath::Min(LowestRailCenterZ, CenterZ);
			CandidateRails.Add(&Member);
		}
		if (!FMath::IsFinite(LowestRailCenterZ))
		{
			*B.Error = TEXT("BeamBBridgeSeatRailsMissing");
			return false;
		}
		double PerpendicularMin = TNumericLimits<double>::Max();
		double PerpendicularMax = TNumericLimits<double>::Lowest();
		for (FABTSM73BeamBPlannedMember* Rail : CandidateRails)
		{
			const double CenterZ =
				(Rail->LocalStart.Z + Rail->LocalEnd.Z) * 0.5;
			if (!FMath::IsNearlyEqual(
				CenterZ, LowestRailCenterZ, Tolerance))
			{
				continue;
			}
			double& TerminalCoordinate = bNegativeEndpoint
				? (Rail->LocalStart[SpanAxis] <= Rail->LocalEnd[SpanAxis]
					? Rail->LocalStart[SpanAxis] : Rail->LocalEnd[SpanAxis])
				: (Rail->LocalStart[SpanAxis] >= Rail->LocalEnd[SpanAxis]
					? Rail->LocalStart[SpanAxis] : Rail->LocalEnd[SpanAxis]);
			TerminalCoordinate = BearingPlane;
			const double Station =
				(Rail->LocalStart[Perpendicular]
					+ Rail->LocalEnd[Perpendicular]) * 0.5;
			PerpendicularMin = FMath::Min(PerpendicularMin, Station);
			PerpendicularMax = FMath::Max(PerpendicularMax, Station);
		}
		if (!FMath::IsFinite(PerpendicularMin)
			|| !FMath::IsFinite(PerpendicularMax))
		{
			*B.Error = TEXT("BeamBBridgeSeatRailCoverageInvalid");
			return false;
		}
		if (PerpendicularMax - PerpendicularMin + Tolerance < T)
		{
			const double Center =
				(PerpendicularMin + PerpendicularMax) * 0.5;
			PerpendicularMin = Center - T * 0.5;
			PerpendicularMax = Center + T * 0.5;
		}

		const double RailBottom = LowestRailCenterZ - T * 0.5;
		const double SeatCenterZ = RailBottom - T * 0.5;
		const FABTSM73DAG5BV2Volume* SupportVolume =
			ResolveEndpointSupportVolume(
				Silhouette,
				DeclaredSupportVolumeId,
				SpanAxis,
				BearingPlane,
				PerpendicularMin - T * 0.5,
				PerpendicularMax + T * 0.5,
				SeatCenterZ - T * 0.5,
				SeatCenterZ + T * 0.5,
				bNegativeEndpoint);
		if (SupportVolume == nullptr)
		{
			*B.Error = TEXT("BeamBBridgeSeatSupportVolumeMissing");
			return false;
		}
		const FABTSM73BeamABay* SupportBay = FindEndpointSupportBay(
			BeamA, SupportVolume->VolumeId, SpanAxis, BearingPlane,
			(PerpendicularMin + PerpendicularMax) * 0.5, SeatCenterZ);
		if (SupportBay == nullptr
			|| !B.Result.Placements.IsValidIndex(SupportBay->BayId))
		{
			*B.Error = TEXT("BeamBBridgeSeatSupportBayMissing");
			return false;
		}

		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		Start[SpanAxis] = BearingPlane;
		End[SpanAxis] = BearingPlane;
		Start[Perpendicular] = PerpendicularMin;
		End[Perpendicular] = PerpendicularMax;
		Start.Z = SeatCenterZ;
		End.Z = SeatCenterZ;
		const int32 PreviousMemberCount = B.Result.PlannedMembers.Num();
		const FABTSM73BeamBPlacement& SupportPlacement =
			B.Result.Placements[SupportBay->BayId];
		if (!B.Add(
			SupportBay->BayId,
			SupportPlacement.Motif,
			Perpendicular == 0
				? EABTSM73BeamAFrameAxis::X
				: EABTSM73BeamAFrameAxis::Y,
			EABTSM73BeamAMemberRole::BridgeSeat,
			Start,
			End))
		{
			return false;
		}
		if (B.Result.PlannedMembers.Num() == PreviousMemberCount)
		{
			*B.Error = TEXT("BeamBBridgeSeatTooShort");
			return false;
		}
		if (!B.AddStep(SupportBay->BayId,
			EABTSM73BeamBGrammarRule::AddBridgeSeat, 1))
		{
			return false;
		}
		FABTSM73BeamBBridgeEndpoint& Endpoint =
			B.Result.BridgeEndpoints.AddDefaulted_GetRef();
		Endpoint.SpanVolumeId = Span.VolumeId;
		Endpoint.DeclaredSupportVolumeId = DeclaredSupportVolumeId;
		Endpoint.SupportVolumeId = SupportVolume->VolumeId;
		Endpoint.BridgeBayId = BridgeBay.BayId;
		Endpoint.SupportBayId = SupportBay->BayId;
		Endpoint.SeatPlannedMemberId =
			B.Result.PlannedMembers.Num() - 1;
		Endpoint.BearingPlaneCM = BearingPlane;
		Endpoint.bNegativeEndpoint = bNegativeEndpoint;
		return true;
	}

	bool AddBridgeEndpointSeats(
		FGeometryBuilder& B,
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		const FABTSM73BeamAGenerationResult& BeamA)
	{
		for (const FABTSM73DAG5BV2Volume& Span : Silhouette.Volumes)
		{
			if (Span.Role != EABTSM73DAG5BV2VolumeRole::SupportedSpan)
			{
				continue;
			}
			if (Span.SpanAxisIndex != 0 && Span.SpanAxisIndex != 1)
			{
				*B.Error = TEXT("BeamBBridgeSeatInvalidSpanAxis");
				return false;
			}
			if (!FindVolume(Silhouette, Span.NegativeSupportVolumeId)
				|| !FindVolume(Silhouette, Span.PositiveSupportVolumeId))
			{
				*B.Error = TEXT("BeamBBridgeSeatSupportVolumeMissing");
				return false;
			}
			const FABTSM73BeamABay* NegativeBay = nullptr;
			const FABTSM73BeamABay* PositiveBay = nullptr;
			for (const FABTSM73BeamABay& Bay : BeamA.Bays)
			{
				if (Bay.SourceVolumeId != Span.VolumeId)
				{
					continue;
				}
				if (NegativeBay == nullptr
					|| Bay.LocalBounds.Min[Span.SpanAxisIndex]
						< NegativeBay->LocalBounds.Min[Span.SpanAxisIndex])
				{
					NegativeBay = &Bay;
				}
				if (PositiveBay == nullptr
					|| Bay.LocalBounds.Max[Span.SpanAxisIndex]
						> PositiveBay->LocalBounds.Max[Span.SpanAxisIndex])
				{
					PositiveBay = &Bay;
				}
			}
			if (NegativeBay == nullptr || PositiveBay == nullptr
				|| !AddBridgeEndpointSeat(B, Span, Silhouette, BeamA, *NegativeBay,
					Span.NegativeSupportVolumeId, true)
				|| !AddBridgeEndpointSeat(B, Span, Silhouette, BeamA, *PositiveBay,
					Span.PositiveSupportVolumeId, false))
			{
				if (B.Error->IsEmpty())
				{
					*B.Error = TEXT("BeamBBridgeSeatEndpointExpansionFailed");
				}
				return false;
			}
		}
		return true;
	}

	bool ImportSemanticRoofAssembly(
		FGeometryBuilder& B,
		const FABTSM73BeamAGenerationResult& BeamA,
		const FABTSM73BeamABay& Bay,
		const EABTSM73DAG5BV2Primitive Primitive,
		FABTSM73BeamBPlacement& Placement)
	{
		TArray<FABTSM73BeamASemanticRoofMember> SourceMembers;
		if (!ABTSM73BeamA::BuildSemanticRoofMembers(
			B.Settings.BeamA, BeamA, Bay, Primitive, SourceMembers))
		{
			*B.Error = TEXT("BeamBSemanticRoofCompileFailed");
			return false;
		}
		const int32 First = B.Result.PlannedMembers.Num();
		for (const FABTSM73BeamASemanticRoofMember& Member : SourceMembers)
		{
			if (!B.Add(
				Bay.BayId,
				Placement.Motif,
				Member.Axis,
				Member.Role,
				Member.LocalStart,
				Member.LocalEnd))
			{
				return false;
			}
		}
		Placement.FirstPlannedMemberIndex = First;
		Placement.PlannedMemberCount =
			B.Result.PlannedMembers.Num() - First;
		return Placement.PlannedMemberCount > 0;
	}

	FBox ClosedMemberBounds(
		const FABTSM73BeamAMember& Member,
		const FABTSM73BeamAGenerationResult& Result,
		const double CrossSectionCM)
	{
		if (!Result.Joints.IsValidIndex(Member.JointA)
			|| !Result.Joints.IsValidIndex(Member.JointB))
		{
			return FBox(EForceInit::ForceInit);
		}
		const FVector A = Result.Joints[Member.JointA].LocalPosition;
		const FVector B = Result.Joints[Member.JointB].LocalPosition;
		const FVector Center = (A + B) * 0.5;
		FVector Extent(CrossSectionCM * 0.5);
		const int32 AxisIndex = static_cast<int32>(Member.Axis);
		if (AxisIndex >= 0 && AxisIndex <= 2)
		{
			Extent[AxisIndex] = Member.LengthCM * 0.5;
		}
		return FBox(Center - Extent, Center + Extent);
	}

	int32 AuditSemanticRoofEnvelopes(
		const FABTSM73BeamBPreviewSettings& Settings,
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		const FABTSM73BeamBGenerationResult& BeamB)
	{
		int32 ViolationCount = 0;
		const double CrossSection = Settings.BeamA.BlockCrossSectionCM;
		const double Tolerance = Settings.BeamA.JointMergeToleranceCM;
		for (const FABTSM73BeamABay& Bay : BeamB.ClosedAssembly.Bays)
		{
			const FABTSM73DAG5BV2Volume* Volume =
				FindVolume(Silhouette, Bay.SourceVolumeId);
			if (Volume == nullptr
				|| Volume->Primitive == EABTSM73DAG5BV2Primitive::Box)
			{
				continue;
			}
			double LowestZ = TNumericLimits<double>::Max();
			double HighestZ = TNumericLimits<double>::Lowest();
			TArray<TPair<double, FBox>> Courses;
			const int32 CourseCount = FMath::Max(2, FMath::FloorToInt(
				Bay.LocalBounds.GetSize().Z / CrossSection));
			for (const FABTSM73BeamBPlannedMember& Member :
				BeamB.PlannedMembers)
			{
				if (Member.BayId != Bay.BayId
					|| Member.Role != EABTSM73BeamAMemberRole::RoofCourse)
				{
					continue;
				}
				const FVector Center =
					(Member.LocalStart + Member.LocalEnd) * 0.5;
				FVector Extent(CrossSection * 0.5);
				Extent[static_cast<int32>(Member.Axis)] =
					(Member.LocalEnd - Member.LocalStart).Size() * 0.5;
				const FBox Actual(Center - Extent, Center + Extent);
				const double CenterZ = Actual.GetCenter().Z;
				LowestZ = FMath::Min(LowestZ, CenterZ);
				HighestZ = FMath::Max(HighestZ, CenterZ);
				Courses.Emplace(CenterZ, Actual);
				const int32 CourseIndex = FMath::Clamp(FMath::RoundToInt(
					(CenterZ - Bay.LocalBounds.Min.Z - CrossSection * 0.5)
					/ CrossSection), 0, CourseCount - 1);
				const FBox Expected = ABTSM73BeamA::SemanticRoofCourseBounds(
					Bay.LocalBounds, Volume->Primitive,
					static_cast<double>(CourseIndex) / CourseCount,
					CrossSection).ExpandBy(Tolerance);
				if (Actual.Min.X < Expected.Min.X
					|| Actual.Max.X > Expected.Max.X
					|| Actual.Min.Y < Expected.Min.Y
					|| Actual.Max.Y > Expected.Max.Y)
				{
					++ViolationCount;
				}
			}
			FBox LowestBounds(EForceInit::ForceInit);
			FBox HighestBounds(EForceInit::ForceInit);
			for (const TPair<double, FBox>& Course : Courses)
			{
				if (FMath::IsNearlyEqual(Course.Key, LowestZ, Tolerance))
				{
					LowestBounds += Course.Value;
				}
				if (FMath::IsNearlyEqual(Course.Key, HighestZ, Tolerance))
				{
					HighestBounds += Course.Value;
				}
			}
			const bool bTapersX =
				Volume->Primitive == EABTSM73DAG5BV2Primitive::Pyramid
				|| Volume->Primitive
					== EABTSM73DAG5BV2Primitive::TriangularPrismX;
			const bool bTapersY =
				Volume->Primitive == EABTSM73DAG5BV2Primitive::Pyramid
				|| Volume->Primitive
					== EABTSM73DAG5BV2Primitive::TriangularPrismY;
			if (!LowestBounds.IsValid || !HighestBounds.IsValid
				|| HighestZ <= LowestZ + Tolerance
				|| (bTapersX && HighestBounds.GetSize().X
					>= LowestBounds.GetSize().X - Tolerance)
				|| (bTapersY && HighestBounds.GetSize().Y
					>= LowestBounds.GetSize().Y - Tolerance))
			{
				++ViolationCount;
			}
		}
		return ViolationCount;
	}

	void AuditBridgeEndpointBearings(
		const FABTSM73BeamBPreviewSettings& Settings,
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		FABTSM73BeamBGenerationResult& InOutResult)
	{
		const FABTSM73BeamAGenerationResult& Closed =
			InOutResult.ClosedAssembly;
		TMap<int32, TArray<int32>> OwnersByMember;
		TMap<int32, TArray<int32>> AssembliesBySourceVolume;
		for (const FABTSM73BeamAAssembly& Assembly : Closed.Assemblies)
		{
			if (Closed.Bays.IsValidIndex(Assembly.BayId))
			{
				AssembliesBySourceVolume.FindOrAdd(
					Closed.Bays[Assembly.BayId].SourceVolumeId)
					.AddUnique(Assembly.AssemblyId);
			}
			for (const int32 MemberId : Assembly.MemberIds)
			{
				OwnersByMember.FindOrAdd(MemberId).AddUnique(
					Assembly.AssemblyId);
			}
		}

		TArray<bool> Reachable;
		Reachable.Init(false, Closed.Members.Num());
		TArray<int32> Queue;
		for (const FABTSM73BeamAMember& Member : Closed.Members)
		{
			if (ClosedMemberBounds(Member, Closed,
				Settings.BeamA.BlockCrossSectionCM).Min.Z
				<= Settings.BeamA.JointMergeToleranceCM)
			{
				Reachable[Member.MemberId] = true;
				Queue.Add(Member.MemberId);
			}
		}
		for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
		{
			const int32 LowerId = Queue[QueueIndex];
			for (const FABTSM73BeamABearingContact& Contact :
				Closed.BearingContacts)
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

		const double Tolerance = Settings.BeamA.JointMergeToleranceCM;
		for (const FABTSM73BeamBBridgeEndpoint& Endpoint :
			InOutResult.BridgeEndpoints)
		{
			const TArray<int32>* SpanAssemblies =
				AssembliesBySourceVolume.Find(Endpoint.SpanVolumeId);
			const TArray<int32>* SupportAssemblies =
				AssembliesBySourceVolume.Find(Endpoint.SupportVolumeId);
			bool bEndpointAccepted = false;
			int32 EndpointSeedCount = 0;
			int32 EndpointVisitedCount = 0;
			if (SpanAssemblies != nullptr && SupportAssemblies != nullptr)
			{
				const FABTSM73DAG5BV2Volume* Span =
					FindVolume(Silhouette, Endpoint.SpanVolumeId);
				const FABTSM73BeamABay* BridgeBay =
					Closed.Bays.IsValidIndex(Endpoint.BridgeBayId)
						? &Closed.Bays[Endpoint.BridgeBayId] : nullptr;
				const double EndpointMinAxis = Span != nullptr
					&& BridgeBay != nullptr
					? FMath::Min(Endpoint.BearingPlaneCM,
						BridgeBay->LocalBounds.Min[Span->SpanAxisIndex])
						- Settings.BeamA.BlockCrossSectionCM - Tolerance
					: 0.0;
				const double EndpointMaxAxis = Span != nullptr
					&& BridgeBay != nullptr
					? FMath::Max(Endpoint.BearingPlaneCM,
						BridgeBay->LocalBounds.Max[Span->SpanAxisIndex])
						+ Settings.BeamA.BlockCrossSectionCM + Tolerance
					: -1.0;
				for (const FABTSM73BeamABearingContact& Contact :
					Closed.BearingContacts)
				{
					if (!Closed.Members.IsValidIndex(Contact.LowerMemberId)
						|| !Closed.Members.IsValidIndex(Contact.UpperMemberId)
						|| !Reachable.IsValidIndex(Contact.LowerMemberId)
						|| !Reachable[Contact.LowerMemberId]
						|| Contact.ContactAreaCM2 <= Tolerance
						|| Span == nullptr
						|| BridgeBay == nullptr
						|| Contact.LocalPosition[Span->SpanAxisIndex]
							< EndpointMinAxis
						|| Contact.LocalPosition[Span->SpanAxisIndex]
							> EndpointMaxAxis)
					{
						continue;
					}
					const TArray<int32>* LowerOwners =
						OwnersByMember.Find(Contact.LowerMemberId);
					const TArray<int32>* UpperOwners =
						OwnersByMember.Find(Contact.UpperMemberId);
					const bool bOwnedBySupport = LowerOwners != nullptr
						&& LowerOwners->ContainsByPredicate(
							[SupportAssemblies](const int32 AssemblyId)
							{
								return SupportAssemblies->Contains(AssemblyId);
							});
					const bool bCarriesSpan = UpperOwners != nullptr
						&& UpperOwners->ContainsByPredicate(
							[SpanAssemblies](const int32 AssemblyId)
							{
								return SpanAssemblies->Contains(AssemblyId);
							});
					if (bOwnedBySupport && bCarriesSpan)
					{
						bEndpointAccepted = true;
						break;
					}
				}
				TArray<bool> EndpointReachable;
				EndpointReachable.Init(false, Closed.Members.Num());
				TArray<int32> EndpointQueue;
				for (const FABTSM73BeamAMember& Member : Closed.Members)
				{
					const TArray<int32>* Owners =
						OwnersByMember.Find(Member.MemberId);
					const bool bOwnedBySupport = Owners != nullptr
						&& Owners->ContainsByPredicate(
							[SupportAssemblies](const int32 AssemblyId)
							{
								return SupportAssemblies->Contains(AssemblyId);
							});
					if (Member.Role != EABTSM73BeamAMemberRole::BridgeSeat
						|| !bOwnedBySupport
						|| !Reachable.IsValidIndex(Member.MemberId)
						|| !Reachable[Member.MemberId]
						|| Span == nullptr)
					{
						continue;
					}
					const FBox Bounds = ClosedMemberBounds(
						Member, Closed, Settings.BeamA.BlockCrossSectionCM);
					if (Endpoint.BearingPlaneCM
						< Bounds.Min[Span->SpanAxisIndex]
							- Settings.BeamA.BlockCrossSectionCM - Tolerance
						|| Endpoint.BearingPlaneCM
						> Bounds.Max[Span->SpanAxisIndex]
							+ Settings.BeamA.BlockCrossSectionCM + Tolerance)
					{
						continue;
					}
					EndpointReachable[Member.MemberId] = true;
					EndpointQueue.Add(Member.MemberId);
					++EndpointSeedCount;
				}
				for (int32 QueueIndex = 0;
					QueueIndex < EndpointQueue.Num() && !bEndpointAccepted;
					++QueueIndex)
				{
					const int32 LowerId = EndpointQueue[QueueIndex];
					++EndpointVisitedCount;
					for (const FABTSM73BeamABearingContact& Contact :
						Closed.BearingContacts)
					{
						if (Contact.LowerMemberId != LowerId
							|| !Closed.Members.IsValidIndex(Contact.UpperMemberId)
							|| Contact.ContactAreaCM2 <= Tolerance
							|| Span == nullptr
							|| BridgeBay == nullptr
							|| Contact.LocalPosition[Span->SpanAxisIndex]
								< EndpointMinAxis
							|| Contact.LocalPosition[Span->SpanAxisIndex]
								> EndpointMaxAxis)
						{
							continue;
						}
						const TArray<int32>* UpperOwners =
							OwnersByMember.Find(Contact.UpperMemberId);
						const bool bCarriesSpan = UpperOwners != nullptr
							&& UpperOwners->ContainsByPredicate(
								[SpanAssemblies](const int32 AssemblyId)
								{
									return SpanAssemblies->Contains(AssemblyId);
								});
						if (bCarriesSpan)
						{
							bEndpointAccepted = true;
							break;
						}
						if (EndpointReachable.IsValidIndex(Contact.UpperMemberId)
							&& !EndpointReachable[Contact.UpperMemberId])
						{
							EndpointReachable[Contact.UpperMemberId] = true;
							EndpointQueue.Add(Contact.UpperMemberId);
						}
					}
				}
			}
			if (bEndpointAccepted)
			{
				++InOutResult.Summary.BridgeEndpointBearingCount;
			}
			else
			{
				UE_LOG(
					LogABTSRuntime,
					Warning,
					TEXT("[ABTS][M7.3-Beam-B][BridgeEndpointBearingMissing] Seed=%d Span=%d DeclaredSupport=%d SeatSupport=%d BridgeBay=%d Plane=%.2f Negative=%d Seeds=%d Visited=%d"),
					Settings.BeamA.Silhouette.BuildingSeed,
					Endpoint.SpanVolumeId,
					Endpoint.DeclaredSupportVolumeId,
					Endpoint.SupportVolumeId,
					Endpoint.BridgeBayId,
					Endpoint.BearingPlaneCM,
					Endpoint.bNegativeEndpoint ? 1 : 0,
					EndpointSeedCount,
					EndpointVisitedCount);
				++InOutResult.Summary.BridgeEndpointBearingViolationCount;
			}
		}

		TSet<int32> AuditedSpanVolumes;
		for (const FABTSM73BeamBBridgeEndpoint& Endpoint :
			InOutResult.BridgeEndpoints)
		{
			if (AuditedSpanVolumes.Contains(Endpoint.SpanVolumeId))
			{
				continue;
			}
			AuditedSpanVolumes.Add(Endpoint.SpanVolumeId);
			const TArray<int32>* SpanAssemblies =
				AssembliesBySourceVolume.Find(Endpoint.SpanVolumeId);
			if (SpanAssemblies == nullptr)
			{
				continue;
			}
			for (const int32 AssemblyId : *SpanAssemblies)
			{
				if (!Closed.Assemblies.IsValidIndex(AssemblyId))
				{
					continue;
				}
				for (const int32 MemberId :
					Closed.Assemblies[AssemblyId].MemberIds)
				{
					if (!Closed.Members.IsValidIndex(MemberId)
						|| Closed.Members[MemberId].Axis
							!= EABTSM73BeamAFrameAxis::Z)
					{
						continue;
					}
					if (ClosedMemberBounds(Closed.Members[MemberId], Closed,
						Settings.BeamA.BlockCrossSectionCM).Min.Z
						<= Tolerance)
					{
						++InOutResult.Summary.BridgeGroundRescuePostCount;
					}
				}
			}
		}
	}

	bool CompileAndCloseAssembly(
		const FABTSM73BeamBPreviewSettings& Settings,
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		const FABTSM73BeamAGenerationResult& BeamA,
		FABTSM73BeamBGenerationResult& InOutResult,
		FString& OutError)
	{
		FABTSM73BeamAGenerationResult& Closed = InOutResult.ClosedAssembly;
		Closed = FABTSM73BeamAGenerationResult();
		Closed.Bays = BeamA.Bays;
		Closed.Assemblies = BeamA.Assemblies;
		Closed.ReservedSupportVoids = BeamA.ReservedSupportVoids;
		FABTSM73BeamAPreviewSummary FreshSummary;
		FreshSummary.SourceVolumeCount = BeamA.Summary.SourceVolumeCount;
		FreshSummary.BayCount = BeamA.Bays.Num();
		FreshSummary.AssemblyCount = BeamA.Assemblies.Num();
		Closed.Summary = FreshSummary;

		TMap<int32, int32> AssemblyByBay;
		for (FABTSM73BeamAAssembly& Assembly : Closed.Assemblies)
		{
			Assembly.JointIds.Reset();
			Assembly.MemberIds.Reset();
			AssemblyByBay.Add(Assembly.BayId, Assembly.AssemblyId);
		}
		TSet<int32> KeptSemanticMembers;
		for (const FABTSM73BeamAAssembly& Assembly : BeamA.Assemblies)
		{
			if (!BeamA.Bays.IsValidIndex(Assembly.BayId))
			{
				continue;
			}
			const FABTSM73DAG5BV2Volume* Volume = FindVolume(
				Silhouette, BeamA.Bays[Assembly.BayId].SourceVolumeId);
			if (Volume != nullptr
				&& Volume->Primitive != EABTSM73DAG5BV2Primitive::Box)
			{
				for (const int32 MemberId : Assembly.MemberIds)
				{
					KeptSemanticMembers.Add(MemberId);
				}
			}
		}
		bool bAddedAncestor = true;
		while (bAddedAncestor)
		{
			bAddedAncestor = false;
			for (const FABTSM73BeamABearingContact& Contact :
				BeamA.BearingContacts)
			{
				if (KeptSemanticMembers.Contains(Contact.UpperMemberId)
					&& !KeptSemanticMembers.Contains(Contact.LowerMemberId))
				{
					KeptSemanticMembers.Add(Contact.LowerMemberId);
					bAddedAncestor = true;
				}
			}
		}
		TMap<int32, TArray<int32>> OwnersByMember;
		for (const FABTSM73BeamAAssembly& Assembly : BeamA.Assemblies)
		{
			for (const int32 MemberId : Assembly.MemberIds)
			{
				if (KeptSemanticMembers.Contains(MemberId))
				{
					OwnersByMember.FindOrAdd(MemberId).AddUnique(
						Assembly.AssemblyId);
				}
			}
		}
		TArray<int32> KeptMemberIds = KeptSemanticMembers.Array();
		KeptMemberIds.Sort();
		for (const int32 SourceMemberId : KeptMemberIds)
		{
			if (!BeamA.Members.IsValidIndex(SourceMemberId)
				|| !OwnersByMember.Contains(SourceMemberId))
			{
				continue;
			}
			const FABTSM73BeamAMember& Source = BeamA.Members[SourceMemberId];
			if (!BeamA.Joints.IsValidIndex(Source.JointA)
				|| !BeamA.Joints.IsValidIndex(Source.JointB))
			{
				OutError = TEXT("BeamBSemanticSourceJointMissing");
				return false;
			}
			const int32 JointA = Closed.Joints.Num();
			FABTSM73BeamAJoint A = BeamA.Joints[Source.JointA];
			A.JointId = JointA;
			Closed.Joints.Add(A);
			const int32 JointB = Closed.Joints.Num();
			FABTSM73BeamAJoint B = BeamA.Joints[Source.JointB];
			B.JointId = JointB;
			Closed.Joints.Add(B);
			FABTSM73BeamAMember Member = Source;
			Member.MemberId = Closed.Members.Num();
			Member.JointA = JointA;
			Member.JointB = JointB;
			Closed.Members.Add(Member);
			for (const int32 OwnerId : OwnersByMember.FindChecked(SourceMemberId))
			{
				if (!Closed.Assemblies.IsValidIndex(OwnerId))
				{
					OutError = TEXT("BeamBSemanticAssemblyIdentityMissing");
					return false;
				}
				FABTSM73BeamAAssembly& Owner = Closed.Assemblies[OwnerId];
				Owner.JointIds.AddUnique(JointA);
				Owner.JointIds.AddUnique(JointB);
				Owner.MemberIds.AddUnique(Member.MemberId);
			}
		}
		for (const FABTSM73BeamBPlannedMember& Planned :
			InOutResult.PlannedMembers)
		{
			if (Planned.Axis == EABTSM73BeamAFrameAxis::Diagonal)
			{
				OutError = TEXT("BeamBDiagonalMembersDisabled");
				return false;
			}
			const int32* AssemblyId = AssemblyByBay.Find(Planned.BayId);
			if (AssemblyId == nullptr
				|| !Closed.Assemblies.IsValidIndex(*AssemblyId))
			{
				OutError = TEXT("BeamBAssemblyIdentityMissing");
				return false;
			}
			if (!Closed.Bays.IsValidIndex(Planned.BayId))
			{
				OutError = TEXT("BeamBInvalidBayIdentity");
				return false;
			}
			const FABTSM73DAG5BV2Volume* Volume = FindVolume(
				Silhouette, Closed.Bays[Planned.BayId].SourceVolumeId);
			if (Volume != nullptr
				&& Volume->Primitive != EABTSM73DAG5BV2Primitive::Box
				&& Planned.Role != EABTSM73BeamAMemberRole::BridgeSeat)
			{
				continue;
			}
			const float Length = static_cast<float>(
				(Planned.LocalEnd - Planned.LocalStart).Size());
			if (!FMath::IsFinite(Length)
				|| Length + Settings.BeamA.JointMergeToleranceCM
					< Settings.BeamA.BlockCrossSectionCM)
			{
				OutError = TEXT("BeamBCompiledMemberTooShort");
				return false;
			}
			if (Closed.Joints.Num() + 2 > Settings.BeamA.MaxJointCount
				|| Closed.Members.Num() >= Settings.BeamA.MaxMemberCount)
			{
				OutError = TEXT("BeamBBeamAIRBudgetExceeded");
				return false;
			}

			const int32 JointA = Closed.Joints.Num();
			FABTSM73BeamAJoint& A = Closed.Joints.AddDefaulted_GetRef();
			A.JointId = JointA;
			A.LocalPosition = Planned.LocalStart;
			A.Role = Planned.Axis == EABTSM73BeamAFrameAxis::Z
				&& Planned.LocalStart.Z <= Settings.BeamA.JointMergeToleranceCM
					? EABTSM73BeamAJointRole::GroundFoot
					: EABTSM73BeamAJointRole::BeamEnd;
			const int32 JointB = Closed.Joints.Num();
			FABTSM73BeamAJoint& B = Closed.Joints.AddDefaulted_GetRef();
			B.JointId = JointB;
			B.LocalPosition = Planned.LocalEnd;
			B.Role = Planned.Axis == EABTSM73BeamAFrameAxis::Z
				? EABTSM73BeamAJointRole::ColumnHead
				: EABTSM73BeamAJointRole::BeamEnd;

			FABTSM73BeamAMember& Member =
				Closed.Members.AddDefaulted_GetRef();
			Member.MemberId = Closed.Members.Num() - 1;
			Member.JointA = JointA;
			Member.JointB = JointB;
			Member.Axis = Planned.Axis;
			Member.Role = Planned.Role;
			Member.LengthCM = Length;
			FABTSM73BeamAAssembly& Assembly = Closed.Assemblies[*AssemblyId];
			Assembly.JointIds.Add(JointA);
			Assembly.JointIds.Add(JointB);
			Assembly.MemberIds.Add(Member.MemberId);
		}

		if (!ABTSM73BeamA::CloseGeneratedAssembly(
			Settings.BeamA, Closed, OutError))
		{
			return false;
		}
		Closed.Summary.JointCount = Closed.Joints.Num();
		Closed.Summary.MemberCount = Closed.Members.Num();
		Closed.Summary.BearingContactCount = Closed.BearingContacts.Num();
		Closed.Summary.XMemberCount = 0;
		Closed.Summary.YMemberCount = 0;
		Closed.Summary.ZMemberCount = 0;
		Closed.Summary.DiagonalMemberCount = 0;
		for (const FABTSM73BeamAMember& Member : Closed.Members)
		{
			switch (Member.Axis)
			{
			case EABTSM73BeamAFrameAxis::X:
				++Closed.Summary.XMemberCount;
				break;
			case EABTSM73BeamAFrameAxis::Y:
				++Closed.Summary.YMemberCount;
				break;
			case EABTSM73BeamAFrameAxis::Z:
				++Closed.Summary.ZMemberCount;
				break;
			case EABTSM73BeamAFrameAxis::Diagonal:
			default:
				++Closed.Summary.DiagonalMemberCount;
				break;
			}
		}
		Closed.Summary.bAccepted =
			Closed.Summary.RemainingPenetrationCount == 0
			&& Closed.Summary.UnsupportedMemberCount == 0
			&& Closed.Summary.DiagonalMemberCount == 0;
		if (!Closed.Summary.bAccepted)
		{
			OutError = FString::Printf(
				TEXT("BeamBClosedAssemblyRejected:Penetration=%d:")
				TEXT("Unsupported=%d:Diagonal=%d"),
				Closed.Summary.RemainingPenetrationCount,
				Closed.Summary.UnsupportedMemberCount,
				Closed.Summary.DiagonalMemberCount);
			UE_LOG(
				LogABTSRuntime,
				Warning,
				TEXT("[ABTS][M7.3-Beam-B][ClosureRejected] %s"),
				*OutError);
			return false;
		}
		InOutResult.Summary.SemanticEnvelopeViolationCount =
			AuditSemanticRoofEnvelopes(Settings, Silhouette, InOutResult);
		if (InOutResult.Summary.SemanticEnvelopeViolationCount > 0)
		{
			OutError = FString::Printf(
				TEXT("BeamBSemanticEnvelopeViolation:Count=%d"),
				InOutResult.Summary.SemanticEnvelopeViolationCount);
			return false;
		}
		AuditBridgeEndpointBearings(Settings, Silhouette, InOutResult);
		if (InOutResult.Summary.BridgeEndpointBearingViolationCount > 0)
		{
			OutError = FString::Printf(
				TEXT("BeamBBridgeEndpointBearingViolation:Count=%d"),
				InOutResult.Summary.BridgeEndpointBearingViolationCount);
			return false;
		}
		if (InOutResult.Summary.BridgeGroundRescuePostCount > 0)
		{
			OutError = FString::Printf(
				TEXT("BeamBBridgeGroundRescuePost:Count=%d"),
				InOutResult.Summary.BridgeGroundRescuePostCount);
			return false;
		}
		return true;
	}

	FString CanonicalPlacements(
		const TArray<FABTSM73BeamBPlacement>& Placements)
	{
		FString Text;
		for (const FABTSM73BeamBPlacement& P : Placements)
		{
			Text += FString::Printf(TEXT("%d:%d:%d:%u|"),
				P.BayId, static_cast<int32>(P.Motif),
				static_cast<int32>(P.Orientation), P.PortMask);
		}
		return Text;
	}

	FString CanonicalGrammar(const FABTSM73BeamBGenerationResult& Result)
	{
		FString Text;
		for (const FABTSM73BeamBGrammarStep& Step : Result.GrammarSteps)
		{
			Text += FString::Printf(TEXT("S%d:%d:%d:%d|"), Step.StepId,
				Step.BayId, static_cast<int32>(Step.Rule), Step.AddedMemberCount);
		}
		for (const FABTSM73BeamBPlannedMember& M : Result.PlannedMembers)
		{
			Text += FString::Printf(TEXT("M%d:%d:%d:%d:R%d:%.2f,%.2f,%.2f:%.2f,%.2f,%.2f|"),
				M.PlannedMemberId, M.BayId, static_cast<int32>(M.Motif),
				static_cast<int32>(M.Axis), static_cast<int32>(M.Role),
				M.LocalStart.X, M.LocalStart.Y,
				M.LocalStart.Z, M.LocalEnd.X, M.LocalEnd.Y, M.LocalEnd.Z);
		}
		return Text;
	}

	FString CanonicalClosedAssembly(
		const FABTSM73BeamAGenerationResult& Closed)
	{
		FString Text;
		for (const FABTSM73BeamAMember& Member : Closed.Members)
		{
			if (!Closed.Joints.IsValidIndex(Member.JointA)
				|| !Closed.Joints.IsValidIndex(Member.JointB))
			{
				continue;
			}
			const FVector& A = Closed.Joints[Member.JointA].LocalPosition;
			const FVector& B = Closed.Joints[Member.JointB].LocalPosition;
			Text += FString::Printf(
				TEXT("M%d:%d:R%d:%.2f,%.2f,%.2f:%.2f,%.2f,%.2f|"),
				Member.MemberId, static_cast<int32>(Member.Axis),
				static_cast<int32>(Member.Role),
				A.X, A.Y, A.Z, B.X, B.Y, B.Z);
		}
		for (const FABTSM73BeamABearingContact& Contact :
			Closed.BearingContacts)
		{
			Text += FString::Printf(TEXT("C%d:%d>%d:%d:%.2f|"),
				Contact.ContactId, Contact.LowerMemberId,
				Contact.UpperMemberId, static_cast<int32>(Contact.Type),
				Contact.ContactAreaCM2);
		}
		return Text;
	}
}

bool FABTSM73BeamBGenerator::Generate(
	const FABTSM73BeamBPreviewSettings& Settings,
	const FABTSM73DAG5BV2GenerationResult& Silhouette,
	const FABTSM73BeamAGenerationResult& BeamA,
	FABTSM73BeamBGenerationResult& OutResult,
	FString& OutError) const
{
	using namespace ABTSM73BeamB;
	OutResult = FABTSM73BeamBGenerationResult();
	OutError.Reset();
	auto Reject = [&](const FString& Reason)
	{
		OutResult = FABTSM73BeamBGenerationResult();
		OutResult.Summary.RejectReason = Reason;
		OutError = Reason;
		return false;
	};

	if (!BeamA.Summary.bAccepted || BeamA.Bays.IsEmpty())
	{
		return Reject(TEXT("BeamBRequiresAcceptedBeamA"));
	}
	if (Settings.GrammarDepth < 1 || Settings.GrammarDepth > 6
		|| Settings.MaxWFCPropagationOperations < 1
		|| Settings.MaxGrammarStepCount < 1
		|| Settings.MaxPlannedMemberCount < 1
		|| Settings.BeamA.BlockCrossSectionCM <= 0.0f)
	{
		return Reject(TEXT("BeamBInvalidSettings"));
	}

	TArray<TArray<EABTSM73BeamBMotif>> Domains;
	Domains.SetNum(BeamA.Bays.Num());
	for (const FABTSM73BeamABay& Bay : BeamA.Bays)
	{
		if (!Domains.IsValidIndex(Bay.BayId))
		{
			return Reject(TEXT("BeamBInvalidBayIdentity"));
		}
		const FABTSM73DAG5BV2Volume* Volume =
			FindVolume(Silhouette, Bay.SourceVolumeId);
		if (Volume == nullptr)
		{
			return Reject(TEXT("BeamBSourceVolumeMissing"));
		}
		BuildDomain(Settings, Bay, *Volume, Domains[Bay.BayId]);
		if (Domains[Bay.BayId].IsEmpty())
		{
			return Reject(TEXT("BeamBNoMotifDomain"));
		}
	}

	OutResult.Placements.SetNum(BeamA.Bays.Num());
	TArray<bool> bCollapsed;
	bCollapsed.Init(false, BeamA.Bays.Num());
	bool bOperationBudgetExceeded = false;
	bool bBacktrackBudgetExceeded = false;
	TFunction<bool()> SolveWFC;
	SolveWFC = [&]() -> bool
	{
		int32 SelectedBayId = INDEX_NONE;
		TArray<EABTSM73BeamBMotif> SelectedCandidates;
		for (const FABTSM73BeamABay& CandidateBay : BeamA.Bays)
		{
			if (bCollapsed[CandidateBay.BayId])
			{
				continue;
			}
			TArray<EABTSM73BeamBMotif> Viable = Domains[CandidateBay.BayId];
			for (const int32 NeighborId : CandidateBay.AdjacentBayIds)
			{
				if (!BeamA.Bays.IsValidIndex(NeighborId)
					|| !bCollapsed[NeighborId])
				{
					continue;
				}
				const EABTSM73BeamBMotif NeighborMotif =
					OutResult.Placements[NeighborId].Motif;
				Viable.RemoveAll([&](const EABTSM73BeamBMotif Candidate)
				{
					++OutResult.Summary.WFCPropagationOperationCount;
					return !Compatible(CandidateBay, Candidate,
						BeamA.Bays[NeighborId], NeighborMotif);
				});
				if (OutResult.Summary.WFCPropagationOperationCount
					> Settings.MaxWFCPropagationOperations)
				{
					bOperationBudgetExceeded = true;
					return false;
				}
			}
			if (Viable.IsEmpty())
			{
				return false;
			}
			if (SelectedBayId == INDEX_NONE
				|| Viable.Num() < SelectedCandidates.Num()
				|| (Viable.Num() == SelectedCandidates.Num()
					&& CandidateBay.BayId < SelectedBayId))
			{
				SelectedBayId = CandidateBay.BayId;
				SelectedCandidates = MoveTemp(Viable);
			}
		}
		if (SelectedBayId == INDEX_NONE)
		{
			return true;
		}

		TSet<EABTSM73BeamBMotif> CurrentMotifs;
		for (int32 Index = 0; Index < bCollapsed.Num(); ++Index)
		{
			if (bCollapsed[Index])
			{
				CurrentMotifs.Add(OutResult.Placements[Index].Motif);
			}
		}
		TArray<EABTSM73BeamBMotif> Novel;
		if (Settings.bRequireMotifVariety)
		{
			for (const EABTSM73BeamBMotif Candidate : SelectedCandidates)
			{
				if (!CurrentMotifs.Contains(Candidate))
				{
					Novel.Add(Candidate);
				}
			}
		}
		TArray<EABTSM73BeamBMotif> OrderedCandidates = Novel;
		for (const EABTSM73BeamBMotif Candidate : SelectedCandidates)
		{
			if (!OrderedCandidates.Contains(Candidate))
			{
				OrderedCandidates.Add(Candidate);
			}
		}
		const TArray<EABTSM73BeamBMotif>& Pool = OrderedCandidates;
		const int32 Rotation = ChoiceHash(Settings, SelectedBayId,
			OutResult.Summary.WFCBacktrackStepCount) % Pool.Num();
		for (int32 Attempt = 0; Attempt < Pool.Num(); ++Attempt)
		{
			const EABTSM73BeamBMotif Motif =
				Pool[(Rotation + Attempt) % Pool.Num()];
			FABTSM73BeamBPlacement& Placement =
				OutResult.Placements[SelectedBayId];
			Placement.BayId = SelectedBayId;
			Placement.Motif = Motif;
			Placement.Orientation = BeamA.Bays[SelectedBayId].PreferredAxis;
			if (const FABTSM73DAG5BV2Volume* Volume = FindVolume(
				Silhouette, BeamA.Bays[SelectedBayId].SourceVolumeId);
				Volume != nullptr
					&& Volume->Role
						== EABTSM73DAG5BV2VolumeRole::SupportedSpan
					&& (Volume->SpanAxisIndex == 0
						|| Volume->SpanAxisIndex == 1))
			{
				Placement.Orientation = Volume->SpanAxisIndex == 0
					? EABTSM73BeamAFrameAxis::X
					: EABTSM73BeamAFrameAxis::Y;
			}
			Placement.PortMask = PortsForMotif(Motif, Placement.Orientation);
			bCollapsed[SelectedBayId] = true;
			if (SolveWFC())
			{
				return true;
			}
			bCollapsed[SelectedBayId] = false;
			if (bOperationBudgetExceeded || bBacktrackBudgetExceeded)
			{
				return false;
			}
			++OutResult.Summary.WFCBacktrackStepCount;
			if (OutResult.Summary.WFCBacktrackStepCount
				> Settings.MaxWFCBacktrackSteps)
			{
				bBacktrackBudgetExceeded = true;
				return false;
			}
		}
		return false;
	};

	if (!SolveWFC())
	{
		if (bOperationBudgetExceeded)
		{
			return Reject(TEXT("BeamBWFCOperationBudgetExceeded"));
		}
		if (bBacktrackBudgetExceeded)
		{
			return Reject(TEXT("BeamBWFCBacktrackBudgetExceeded"));
		}
		return Reject(TEXT("BeamBNoPortCompatibleMotif"));
	}
	TSet<EABTSM73BeamBMotif> UsedMotifs;
	for (const FABTSM73BeamBPlacement& Placement : OutResult.Placements)
	{
		UsedMotifs.Add(Placement.Motif);
	}

	FString GeometryError;
	FGeometryBuilder Builder{Settings, OutResult, &GeometryError};
	for (FABTSM73BeamBPlacement& Placement : OutResult.Placements)
	{
		if (!BeamA.Bays.IsValidIndex(Placement.BayId))
		{
			return Reject(TEXT("BeamBInvalidBayIdentity"));
		}
		const FABTSM73BeamABay& Bay = BeamA.Bays[Placement.BayId];
		const FABTSM73DAG5BV2Volume* Volume =
			FindVolume(Silhouette, Bay.SourceVolumeId);
		if (Volume == nullptr)
		{
			return Reject(TEXT("BeamBSourceVolumeMissing"));
		}
		const bool bExpanded =
			Volume->Primitive == EABTSM73DAG5BV2Primitive::Box
				? BuildMotif(Builder, Bay, Placement)
				: ImportSemanticRoofAssembly(
					Builder, BeamA, Bay, Volume->Primitive, Placement);
		if (!bExpanded)
		{
			return Reject(GeometryError.IsEmpty()
				? TEXT("BeamBMotifExpansionFailed") : GeometryError);
		}
	}
	if (!AddBridgeEndpointSeats(Builder, Silhouette, BeamA))
	{
		return Reject(GeometryError.IsEmpty()
			? TEXT("BeamBBridgeSeatExpansionFailed") : GeometryError);
	}

	for (const FABTSM73BeamABay& Bay : BeamA.Bays)
	{
		for (const int32 NeighborId : Bay.AdjacentBayIds)
		{
			if (NeighborId <= Bay.BayId
				|| !BeamA.Bays.IsValidIndex(NeighborId))
			{
				continue;
			}
			if (!Compatible(Bay, OutResult.Placements[Bay.BayId].Motif,
				BeamA.Bays[NeighborId],
				OutResult.Placements[NeighborId].Motif))
			{
				++OutResult.Summary.PortViolationCount;
			}
		}
	}

	const double BoundsTolerance = 0.1;
	for (const FABTSM73BeamBPlannedMember& Member : OutResult.PlannedMembers)
	{
		if (!BeamA.Bays.IsValidIndex(Member.BayId))
		{
			++OutResult.Summary.OutOfBoundsMemberCount;
			continue;
		}
		const FABTSM73BeamABay& Bay = BeamA.Bays[Member.BayId];
		const FABTSM73DAG5BV2Volume* Volume =
			FindVolume(Silhouette, Bay.SourceVolumeId);
		if (Member.Role == EABTSM73BeamAMemberRole::BridgeSeat)
		{
			const FABTSM73BeamBBridgeEndpoint* Endpoint =
				OutResult.BridgeEndpoints.FindByPredicate(
					[&Member](const FABTSM73BeamBBridgeEndpoint& Candidate)
					{
						return Candidate.SeatPlannedMemberId
							== Member.PlannedMemberId;
					});
			const FABTSM73DAG5BV2Volume* SupportVolume = Endpoint != nullptr
				? FindVolume(Silhouette, Endpoint->SupportVolumeId) : nullptr;
			if (SupportVolume == nullptr)
			{
				++OutResult.Summary.OutOfBoundsMemberCount;
				continue;
			}
			const FBox SeatBounds = PlannedMemberBounds(
				Member, Settings.BeamA.BlockCrossSectionCM);
			const FBox& SupportBounds = SupportVolume->LocalBounds;
			const double XOverlap = FMath::Min(
				SeatBounds.Max.X, SupportBounds.Max.X)
				- FMath::Max(SeatBounds.Min.X, SupportBounds.Min.X);
			const double YOverlap = FMath::Min(
				SeatBounds.Max.Y, SupportBounds.Max.Y)
				- FMath::Max(SeatBounds.Min.Y, SupportBounds.Min.Y);
			if (XOverlap <= BoundsTolerance
				|| YOverlap <= BoundsTolerance)
			{
				++OutResult.Summary.OutOfBoundsMemberCount;
			}
			continue;
		}
		if (Volume != nullptr
			&& Volume->Role == EABTSM73DAG5BV2VolumeRole::SupportedSpan
			&& Member.Motif == EABTSM73BeamBMotif::BridgeBay
			&& Member.Role == EABTSM73BeamAMemberRole::PrimaryBeam)
		{
			const FBox ExpandedSpan = Volume->LocalBounds
				.ExpandBy(BoundsTolerance);
			if (!ExpandedSpan.IsInsideOrOn(Member.LocalStart)
				|| !ExpandedSpan.IsInsideOrOn(Member.LocalEnd))
			{
				++OutResult.Summary.OutOfBoundsMemberCount;
			}
			continue;
		}
		if (Volume != nullptr
			&& Volume->Primitive != EABTSM73DAG5BV2Primitive::Box
			&& Member.Role != EABTSM73BeamAMemberRole::RoofCourse)
		{
			// Beam-A global closure may assign a roof Assembly a supporting post
			// that intentionally reaches into the supporting Bay below. Only the
			// imported roof courses themselves belong to the semantic envelope.
			continue;
		}
		const FBox Expanded = Bay.LocalBounds
			.ExpandBy(BoundsTolerance);
		if (!Expanded.IsInsideOrOn(Member.LocalStart)
			|| !Expanded.IsInsideOrOn(Member.LocalEnd))
		{
			++OutResult.Summary.OutOfBoundsMemberCount;
		}
	}
	if (OutResult.Summary.PortViolationCount > 0)
	{
		return Reject(TEXT("BeamBPortCompatibilityViolation"));
	}
	if (OutResult.Summary.OutOfBoundsMemberCount > 0)
	{
		return Reject(TEXT("BeamBMemberOutsideBay"));
	}
	if (Settings.bRequireMotifVariety && BeamA.Bays.Num() >= 4
		&& UsedMotifs.Num() < 2)
	{
		return Reject(TEXT("BeamBMotifVarietyUnavailable"));
	}
	FString ClosureError;
	if (!CompileAndCloseAssembly(
		Settings, Silhouette, BeamA, OutResult, ClosureError))
	{
		UE_LOG(
			LogABTSRuntime,
			Warning,
			TEXT("[ABTS][M7.3-Beam-B][ClosureFailed] %s"),
			*ClosureError);
		return Reject(FString::Printf(
			TEXT("BeamBClosure:%s"), *ClosureError));
	}

	OutResult.Summary.BayCount = BeamA.Bays.Num();
	OutResult.Summary.PlacementCount = OutResult.Placements.Num();
	OutResult.Summary.DistinctMotifCount = UsedMotifs.Num();
	OutResult.Summary.GrammarStepCount = OutResult.GrammarSteps.Num();
	OutResult.Summary.PlannedMemberCount = OutResult.PlannedMembers.Num();
	OutResult.Summary.BridgeSeatMemberCount =
		OutResult.BridgeEndpoints.Num();
	OutResult.Summary.ClosedMemberCount =
		OutResult.ClosedAssembly.Members.Num();
	OutResult.Summary.ClosedBearingContactCount =
		OutResult.ClosedAssembly.BearingContacts.Num();
	OutResult.Summary.ClosureSplitPostMemberCount =
		OutResult.ClosedAssembly.Summary.SplitPostMemberCount;
	OutResult.Summary.ClosureMergedMemberCount =
		OutResult.ClosedAssembly.Summary.MergedMemberCount;
	OutResult.Summary.ClosureShiftedCourseCount =
		OutResult.ClosedAssembly.Summary.ShiftedCourseCount;
	OutResult.Summary.ClosureSupportMemberCount =
		OutResult.ClosedAssembly.Summary.GlobalSupportMemberCount;
	OutResult.Summary.ClosurePrunedMemberCount =
		OutResult.ClosedAssembly.Summary.PrunedUnsupportedMemberCount;
	OutResult.Summary.RemainingPenetrationCount =
		OutResult.ClosedAssembly.Summary.RemainingPenetrationCount;
	OutResult.Summary.UnsupportedMemberCount =
		OutResult.ClosedAssembly.Summary.UnsupportedMemberCount;
	OutResult.Summary.DiagonalMemberCount =
		OutResult.ClosedAssembly.Summary.DiagonalMemberCount;
	const FString PlacementText = CanonicalPlacements(OutResult.Placements);
	const FString GrammarText = CanonicalGrammar(OutResult);
	const FString ClosedText = CanonicalClosedAssembly(
		OutResult.ClosedAssembly);
	OutResult.Summary.MotifWFCHash = static_cast<int64>(
		FCrc::StrCrc32(*PlacementText));
	OutResult.Summary.GraphGrammarHash = static_cast<int64>(
		FCrc::StrCrc32(*GrammarText));
	OutResult.Summary.ResultHash = static_cast<int64>(FCrc::StrCrc32(
		*(PlacementText + GrammarText + ClosedText)));
	OutResult.Summary.bAccepted = true;
	return true;
}
