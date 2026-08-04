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
		// U0/U1 are post centre-lines inset by half a block from the Bay
		// boundary. A full-span beam must cover the complete footprint of
		// those terminal posts, rather than ending at their centre-lines.
		// Only expand an endpoint that is actually on a terminal station so
		// partial grammar members keep their intentional length.
		const double HalfThickness =
			B.Settings.BeamA.BlockCrossSectionCM * 0.5;
		const double Tolerance =
			B.Settings.BeamA.JointMergeToleranceCM;
		const double PhysicalU0 = FMath::IsNearlyEqual(U0, C.U0, Tolerance)
			? U0 - HalfThickness : U0;
		const double PhysicalU1 = FMath::IsNearlyEqual(U1, C.U1, Tolerance)
			? U1 + HalfThickness : U1;
		return B.Add(P.BayId, P.Motif, C.PrimaryAxis,
			EABTSM73BeamAMemberRole::PrimaryBeam,
			C.P(PhysicalU0, V, Z), C.P(PhysicalU1, V, Z));
	}

	bool AddCross(
		FGeometryBuilder& B,
		const FABTSM73BeamBPlacement& P,
		const FBayCoordinates& C,
		const double U,
		const double Z)
	{
		// Cross members always span the complete Bay course. Extend from the
		// inset post centre-lines to the physical Bay faces for full terminal
		// post coverage and exact contact across a shared Bay boundary.
		const double HalfThickness =
			B.Settings.BeamA.BlockCrossSectionCM * 0.5;
		return B.Add(P.BayId, P.Motif, C.CrossAxis(),
			EABTSM73BeamAMemberRole::SecondaryBeam,
			C.P(U, C.V0 - HalfThickness, Z),
			C.P(U, C.V1 + HalfThickness, Z));
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
				|| Member.Role != EABTSM73BeamAMemberRole::BridgeRail
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

		TArray<double> RailStations;
		for (FABTSM73BeamBPlannedMember* Rail : CandidateRails)
		{
			const double CenterZ =
				(Rail->LocalStart.Z + Rail->LocalEnd.Z) * 0.5;
			if (FMath::IsNearlyEqual(CenterZ, LowestRailCenterZ, Tolerance))
			{
				RailStations.AddUnique(
					(Rail->LocalStart[Perpendicular]
						+ Rail->LocalEnd[Perpendicular]) * 0.5);
			}
		}
		RailStations.Sort();
		if (RailStations.IsEmpty())
		{
			*B.Error = TEXT("BeamBBridgeSeatRailStationsMissing");
			return false;
		}
		const FABTSM73BeamBPlacement& SupportPlacement =
			B.Result.Placements[SupportBay->BayId];
		FVector SeatStart = FVector::ZeroVector;
		FVector SeatEnd = FVector::ZeroVector;
		SeatStart[SpanAxis] = BearingPlane;
		SeatEnd[SpanAxis] = BearingPlane;
		SeatStart[Perpendicular] = PerpendicularMin;
		SeatEnd[Perpendicular] = PerpendicularMax;
		SeatStart.Z = SeatCenterZ;
		SeatEnd.Z = SeatCenterZ;
		const int32 PreviousMemberCount = B.Result.PlannedMembers.Num();
		if (!B.Add(
			SupportBay->BayId,
			SupportPlacement.Motif,
			Perpendicular == 0
				? EABTSM73BeamAFrameAxis::X
				: EABTSM73BeamAFrameAxis::Y,
			EABTSM73BeamAMemberRole::BridgeSeat,
			SeatStart,
			SeatEnd))
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
		Endpoint.SeatPlannedMemberId = B.Result.PlannedMembers.Num() - 1;
		Endpoint.BearingPlaneCM = BearingPlane;
		Endpoint.RailCenterZCM = LowestRailCenterZ;
		Endpoint.RailStationsCM = MoveTemp(RailStations);
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
				const FBox Expected =
					ABTSM73BeamA::SemanticRoofBearingCourseBounds(
						Bay.LocalBounds,
						Volume->Primitive,
						CourseIndex,
						CourseCount,
						Member.Axis,
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
				if (Course.Key <= LowestZ + Tolerance)
				{
					continue;
				}
				const bool bDirectlyBearsOnLowerCourse =
					Courses.ContainsByPredicate(
						[&Course, CrossSection, Tolerance](
							const TPair<double, FBox>& Lower)
						{
							return FMath::IsNearlyEqual(
								Lower.Key,
								Course.Key - CrossSection,
								Tolerance)
								&& FMath::Min(Lower.Value.Max.X,
									Course.Value.Max.X)
									- FMath::Max(Lower.Value.Min.X,
										Course.Value.Min.X) > Tolerance
								&& FMath::Min(Lower.Value.Max.Y,
									Course.Value.Max.Y)
									- FMath::Max(Lower.Value.Min.Y,
										Course.Value.Min.Y) > Tolerance;
						});
				if (!bDirectlyBearsOnLowerCourse)
				{
					++ViolationCount;
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
			bool bAllRailBearingsAccepted = false;
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

				bAllRailBearingsAccepted = !Endpoint.RailStationsCM.IsEmpty();
				for (const double RailStation : Endpoint.RailStationsCM)
				{
					bool bRailBearingAccepted = false;
					int32 RoleRailCount = 0;
					int32 AxisRailCount = 0;
					int32 OwnedRailCount = 0;
					int32 PlaneRailCount = 0;
					int32 StationRailCount = 0;
					int32 MatchingRailCount = 0;
					int32 RailContactCount = 0;
					int32 SeatContactCount = 0;
					int32 ReachableSeatCount = 0;
					int32 RoleSeatCount = 0;
					int32 HorizontalSeatCount = 0;
					int32 VerticalSeatCount = 0;
					double ClosestSeatGap = TNumericLimits<double>::Max();
					for (const FABTSM73BeamAMember& Rail : Closed.Members)
					{
						if (Rail.Role != EABTSM73BeamAMemberRole::BridgeRail)
						{
							continue;
						}
						++RoleRailCount;
						if (static_cast<int32>(Rail.Axis) != Span->SpanAxisIndex)
						{
							continue;
						}
						++AxisRailCount;
						const TArray<int32>* RailOwners =
							OwnersByMember.Find(Rail.MemberId);
						const bool bCarriesSpan = RailOwners != nullptr
							&& RailOwners->ContainsByPredicate(
								[SpanAssemblies](const int32 AssemblyId)
								{
									return SpanAssemblies->Contains(AssemblyId);
								});
						if (!bCarriesSpan)
						{
							continue;
						}
						++OwnedRailCount;
						const int32 Perpendicular =
							Span->SpanAxisIndex == 0 ? 1 : 0;
						const FBox RailBounds = ClosedMemberBounds(
							Rail, Closed, Settings.BeamA.BlockCrossSectionCM);
						if (Endpoint.BearingPlaneCM
							< RailBounds.Min[Span->SpanAxisIndex] - Tolerance
							|| Endpoint.BearingPlaneCM
							> RailBounds.Max[Span->SpanAxisIndex] + Tolerance)
						{
							continue;
						}
						++PlaneRailCount;
						if (RailStation
							< RailBounds.Min[Perpendicular] - Tolerance
							|| RailStation
							> RailBounds.Max[Perpendicular] + Tolerance)
						{
							continue;
						}
						++StationRailCount;
						++MatchingRailCount;
						for (const FABTSM73BeamAMember& CandidateSeat :
							Closed.Members)
						{
							if (CandidateSeat.Role
								!= EABTSM73BeamAMemberRole::BridgeSeat)
							{
								continue;
							}
							++RoleSeatCount;
							if (CandidateSeat.Axis == EABTSM73BeamAFrameAxis::Z)
							{
								++VerticalSeatCount;
							}
							else
							{
								++HorizontalSeatCount;
							}
							const FBox SeatBounds = ClosedMemberBounds(
								CandidateSeat,
								Closed,
								Settings.BeamA.BlockCrossSectionCM);
							const double XOverlap = FMath::Min(
								RailBounds.Max.X, SeatBounds.Max.X)
								- FMath::Max(RailBounds.Min.X, SeatBounds.Min.X);
							const double YOverlap = FMath::Min(
								RailBounds.Max.Y, SeatBounds.Max.Y)
								- FMath::Max(RailBounds.Min.Y, SeatBounds.Min.Y);
							if (XOverlap > Tolerance && YOverlap > Tolerance)
							{
								ClosestSeatGap = FMath::Min(
									ClosestSeatGap,
									FMath::Abs(
										SeatBounds.Max.Z - RailBounds.Min.Z));
							}
						}
						for (const FABTSM73BeamABearingContact& Contact :
							Closed.BearingContacts)
						{
							if (Contact.UpperMemberId != Rail.MemberId
								|| Contact.ContactAreaCM2 <= Tolerance
								|| !Closed.Members.IsValidIndex(
									Contact.LowerMemberId))
							{
								continue;
							}
							++RailContactCount;
							if (Closed.Members[Contact.LowerMemberId].Role
								!= EABTSM73BeamAMemberRole::BridgeSeat)
							{
								continue;
							}
							++SeatContactCount;
							if (!Reachable.IsValidIndex(Contact.LowerMemberId)
								|| !Reachable[Contact.LowerMemberId])
							{
								continue;
							}
							++ReachableSeatCount;
							// BridgeSeat is an explicit endpoint-only semantic member.
							// Direct contact with a ground-reachable seat is therefore
							// stronger evidence than post-merge Assembly ownership,
							// which may legitimately move to an adjacent support Bay.
							bRailBearingAccepted = true;
							break;
						}
						if (bRailBearingAccepted)
						{
							break;
						}
					}
					if (bRailBearingAccepted)
					{
						++InOutResult.Summary.BridgeRailEndpointBearingCount;
					}
					else
					{
						bAllRailBearingsAccepted = false;
						++InOutResult.Summary
							.BridgeRailEndpointBearingViolationCount;
						UE_LOG(
							LogABTSRuntime,
							Warning,
							TEXT("[ABTS][M7.3-Beam-B][BridgeRailEndpointBearingMissing] Seed=%d Span=%d Support=%d BridgeBay=%d Plane=%.2f Station=%.2f Negative=%d RoleRails=%d AxisRails=%d OwnedRails=%d PlaneRails=%d StationRails=%d Rails=%d Contacts=%d SeatContacts=%d ReachableSeats=%d RoleSeats=%d HSeats=%d VSeats=%d ClosestSeatGap=%.2f"),
							Settings.BeamA.Silhouette.BuildingSeed,
							Endpoint.SpanVolumeId,
							Endpoint.SupportVolumeId,
							Endpoint.BridgeBayId,
							Endpoint.BearingPlaneCM,
							RailStation,
							Endpoint.bNegativeEndpoint ? 1 : 0,
							RoleRailCount,
							AxisRailCount,
							OwnedRailCount,
							PlaneRailCount,
							StationRailCount,
							MatchingRailCount,
							RailContactCount,
							SeatContactCount,
							ReachableSeatCount,
							RoleSeatCount,
							HorizontalSeatCount,
							VerticalSeatCount,
							FMath::IsFinite(ClosestSeatGap)
								? ClosestSeatGap : -1.0);
					}
				}
			}
			bEndpointAccepted = bEndpointAccepted
				&& bAllRailBearingsAccepted;
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


	bool InstallClosedBridgeEndpointCorbels(
		const FABTSM73BeamBPreviewSettings& Settings,
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		FABTSM73BeamBGenerationResult& InOutResult,
		FString& OutError)
	{
		FABTSM73BeamAGenerationResult& Closed =
			InOutResult.ClosedAssembly;
		const double CrossSection = Settings.BeamA.BlockCrossSectionCM;
		const double Tolerance = Settings.BeamA.JointMergeToleranceCM;
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
		auto AddContact = [&Closed, &Settings, &OutError](
			const int32 LowerMemberId,
			const int32 UpperMemberId,
			const FVector& Position,
			const float Area)
		{
			if (Closed.BearingContacts.ContainsByPredicate(
				[LowerMemberId, UpperMemberId](
					const FABTSM73BeamABearingContact& Contact)
				{
					return Contact.LowerMemberId == LowerMemberId
						&& Contact.UpperMemberId == UpperMemberId;
				}))
			{
				return true;
			}
			if (Closed.BearingContacts.Num()
				>= Settings.BeamA.MaxBearingContactCount)
			{
				OutError = TEXT("BeamBBridgeCorbelBearingBudgetExceeded");
				return false;
			}
			FABTSM73BeamABearingContact& Contact =
				Closed.BearingContacts.AddDefaulted_GetRef();
			Contact.ContactId = Closed.BearingContacts.Num() - 1;
			Contact.LowerMemberId = LowerMemberId;
			Contact.UpperMemberId = UpperMemberId;
			const FABTSM73BeamAMember& Lower =
				Closed.Members[LowerMemberId];
			const FABTSM73BeamAMember& Upper =
				Closed.Members[UpperMemberId];
			Contact.Type = Upper.Axis == EABTSM73BeamAFrameAxis::Z
				? EABTSM73BeamABearingType::PostOnBeam
				: (Lower.Axis == EABTSM73BeamAFrameAxis::Z
					? EABTSM73BeamABearingType::BeamOnPost
					: (Lower.Axis != Upper.Axis
						? EABTSM73BeamABearingType::CrossBearing
						: EABTSM73BeamABearingType::ParallelBearing));
			Contact.LocalPosition = Position;
			Contact.ContactAreaCM2 = Area;
			return true;
		};

		for (const FABTSM73BeamBBridgeEndpoint& Endpoint :
			InOutResult.BridgeEndpoints)
		{
			const FABTSM73DAG5BV2Volume* Span =
				FindVolume(Silhouette, Endpoint.SpanVolumeId);
			const TArray<int32>* SpanAssemblies =
				AssembliesBySourceVolume.Find(Endpoint.SpanVolumeId);
			const FABTSM73BeamAAssembly* SupportAssembly =
				Closed.Assemblies.FindByPredicate(
					[&Endpoint](const FABTSM73BeamAAssembly& Assembly)
					{
						return Assembly.BayId == Endpoint.SupportBayId;
					});
			if (Span == nullptr || SpanAssemblies == nullptr
				|| SupportAssembly == nullptr)
			{
				OutError = TEXT("BeamBBridgeCorbelIdentityMissing");
				return false;
			}
			const int32 SupportAssemblyId = SupportAssembly->AssemblyId;
			TArray<int32> SupportModuleMemberIds;
			const FABTSM73DAG5BV2Volume* DeclaredSupport =
				FindVolume(Silhouette, Endpoint.DeclaredSupportVolumeId);
			const FString SupportModulePath = DeclaredSupport != nullptr
				? SemanticModulePath(DeclaredSupport->DerivationPath)
				: FString();
			for (const FABTSM73DAG5BV2Volume& ModuleVolume :
				Silhouette.Volumes)
			{
				if (DeclaredSupport == nullptr
					|| ModuleVolume.Role
						== EABTSM73DAG5BV2VolumeRole::SupportedSpan
					|| SemanticModulePath(ModuleVolume.DerivationPath)
						!= SupportModulePath)
				{
					continue;
				}
				const TArray<int32>* ModuleAssemblies =
					AssembliesBySourceVolume.Find(ModuleVolume.VolumeId);
				if (ModuleAssemblies == nullptr)
				{
					continue;
				}
				for (const int32 AssemblyId : *ModuleAssemblies)
				{
					if (!Closed.Assemblies.IsValidIndex(AssemblyId))
					{
						continue;
					}
					for (const int32 MemberId :
						Closed.Assemblies[AssemblyId].MemberIds)
					{
						SupportModuleMemberIds.AddUnique(MemberId);
					}
				}
			}
			if (SupportModuleMemberIds.IsEmpty())
			{
				SupportModuleMemberIds = SupportAssembly->MemberIds;
			}
			const int32 Perpendicular = Span->SpanAxisIndex == 0 ? 1 : 0;
			for (const double RailStation : Endpoint.RailStationsCM)
			{
				FABTSM73BeamAMember* BestRail = nullptr;
				double BestRailScore = TNumericLimits<double>::Max();
				for (FABTSM73BeamAMember& Rail : Closed.Members)
				{
					if (static_cast<int32>(Rail.Axis) != Span->SpanAxisIndex)
					{
						continue;
					}
					const TArray<int32>* Owners =
						OwnersByMember.Find(Rail.MemberId);
					const bool bOwnedBySpan = Owners != nullptr
						&& Owners->ContainsByPredicate(
							[SpanAssemblies](const int32 AssemblyId)
							{
								return SpanAssemblies->Contains(AssemblyId);
							});
					const FBox Bounds = ClosedMemberBounds(
						Rail, Closed, CrossSection);
					if (FMath::Abs(Bounds.GetCenter().Z
							- Endpoint.RailCenterZCM)
						> CrossSection + Tolerance
						|| Endpoint.BearingPlaneCM
						< Bounds.Min[Span->SpanAxisIndex] - Tolerance
						|| Endpoint.BearingPlaneCM
						> Bounds.Max[Span->SpanAxisIndex] + Tolerance
						|| RailStation < Bounds.Min[Perpendicular] - Tolerance
						|| RailStation > Bounds.Max[Perpendicular] + Tolerance)
					{
						continue;
					}
					// Global closure may merge a bridge rail into an adjacent
					// ordinary beam and transfer its Assembly ownership. Recover
					// the semantic rail from the final geometry instead of relying
					// on a planned MemberId surviving closure verbatim.
					const double Score = FMath::Abs(
						Bounds.GetCenter()[Perpendicular] - RailStation)
						+ FMath::Abs(Bounds.GetCenter().Z
							- Endpoint.RailCenterZCM)
						+ (Rail.Role == EABTSM73BeamAMemberRole::BridgeRail
							? 0.0 : CrossSection)
						+ (bOwnedBySpan ? 0.0 : CrossSection);
					if (Score < BestRailScore)
					{
						BestRail = &Rail;
						BestRailScore = Score;
					}
				}
				// A closure split may shorten the retained rail before it reaches
				// the semantic bearing plane. In that case recover the closest
				// collinear span member at this station; a short ledger extension
				// is installed below instead of accepting a visual air gap.
				if (BestRail == nullptr)
				{
					for (FABTSM73BeamAMember& Rail : Closed.Members)
					{
						if (static_cast<int32>(Rail.Axis)
							!= Span->SpanAxisIndex)
						{
							continue;
						}
						const FBox Bounds = ClosedMemberBounds(
							Rail, Closed, CrossSection);
						if (FMath::Abs(Bounds.GetCenter().Z
								- Endpoint.RailCenterZCM)
							> CrossSection + Tolerance
							|| RailStation
							< Bounds.Min[Perpendicular] - Tolerance
							|| RailStation
							> Bounds.Max[Perpendicular] + Tolerance)
						{
							continue;
						}
						const TArray<int32>* Owners =
							OwnersByMember.Find(Rail.MemberId);
						const bool bOwnedBySpan = Owners != nullptr
							&& Owners->ContainsByPredicate(
								[SpanAssemblies](const int32 AssemblyId)
								{
									return SpanAssemblies->Contains(AssemblyId);
								});
						const double PlaneGap = FMath::Max(
							Bounds.Min[Span->SpanAxisIndex]
								- Endpoint.BearingPlaneCM,
							Endpoint.BearingPlaneCM
								- Bounds.Max[Span->SpanAxisIndex]);
						const double Score = FMath::Max(0.0, PlaneGap)
							+ FMath::Abs(Bounds.GetCenter().Z
								- Endpoint.RailCenterZCM)
							+ FMath::Abs(
								Bounds.GetCenter()[Perpendicular] - RailStation)
							+ (Rail.Role
								== EABTSM73BeamAMemberRole::BridgeRail
									? 0.0 : CrossSection)
							+ (bOwnedBySpan ? 0.0 : CrossSection * 2.0);
						if (Score < BestRailScore)
						{
							BestRail = &Rail;
							BestRailScore = Score;
						}
					}
				}
				if (BestRail == nullptr)
				{
					if (Closed.Members.Num() >= Settings.BeamA.MaxMemberCount
						|| Closed.Joints.Num() + 2
							> Settings.BeamA.MaxJointCount)
					{
						OutError = TEXT("BeamBBridgeRailRecoveryBudgetExceeded");
						return false;
					}
					FVector RailStart = FVector::ZeroVector;
					FVector RailEnd = FVector::ZeroVector;
					RailStart[Span->SpanAxisIndex] = Span->SpanOpeningMinCM;
					RailEnd[Span->SpanAxisIndex] = Span->SpanOpeningMaxCM;
					RailStart[Perpendicular] = RailStation;
					RailEnd[Perpendicular] = RailStation;
					RailStart.Z = Endpoint.RailCenterZCM;
					RailEnd.Z = Endpoint.RailCenterZCM;
					const int32 JointA = Closed.Joints.Num();
					FABTSM73BeamAJoint& A =
						Closed.Joints.AddDefaulted_GetRef();
					A.JointId = JointA;
					A.LocalPosition = RailStart;
					A.Role = EABTSM73BeamAJointRole::BeamEnd;
					const int32 JointB = Closed.Joints.Num();
					FABTSM73BeamAJoint& B =
						Closed.Joints.AddDefaulted_GetRef();
					B.JointId = JointB;
					B.LocalPosition = RailEnd;
					B.Role = EABTSM73BeamAJointRole::BeamEnd;
					FABTSM73BeamAMember& RecoveredRail =
						Closed.Members.AddDefaulted_GetRef();
					RecoveredRail.MemberId = Closed.Members.Num() - 1;
					RecoveredRail.JointA = JointA;
					RecoveredRail.JointB = JointB;
					RecoveredRail.Axis =
						static_cast<EABTSM73BeamAFrameAxis>(Span->SpanAxisIndex);
					RecoveredRail.Role = EABTSM73BeamAMemberRole::BridgeRail;
					RecoveredRail.LengthCM = (RailEnd - RailStart).Size();
					const int32 SpanAssemblyId = (*SpanAssemblies)[0];
					if (!Closed.Assemblies.IsValidIndex(SpanAssemblyId))
					{
						OutError = TEXT("BeamBBridgeRailRecoveryAssemblyMissing");
						return false;
					}
					FABTSM73BeamAAssembly& SpanOwner =
						Closed.Assemblies[SpanAssemblyId];
					SpanOwner.JointIds.AddUnique(JointA);
					SpanOwner.JointIds.AddUnique(JointB);
					SpanOwner.MemberIds.AddUnique(RecoveredRail.MemberId);
					OwnersByMember.FindOrAdd(RecoveredRail.MemberId).AddUnique(
						SpanAssemblyId);
					BestRail = &Closed.Members[RecoveredRail.MemberId];
				}
				BestRail->Role = EABTSM73BeamAMemberRole::BridgeRail;
				int32 RailMemberId = BestRail->MemberId;
				if (!OwnersByMember.FindOrAdd(RailMemberId)
					.Contains((*SpanAssemblies)[0]))
				{
					const int32 SpanAssemblyId = (*SpanAssemblies)[0];
					if (!Closed.Assemblies.IsValidIndex(SpanAssemblyId))
					{
						OutError = TEXT("BeamBBridgeCorbelSpanAssemblyMissing");
						return false;
					}
					Closed.Assemblies[SpanAssemblyId].MemberIds.AddUnique(
						RailMemberId);
					OwnersByMember.FindOrAdd(RailMemberId).AddUnique(
						SpanAssemblyId);
				}
				FBox RailBounds = ClosedMemberBounds(
					Closed.Members[RailMemberId], Closed, CrossSection);
				if (Endpoint.BearingPlaneCM
					< RailBounds.Min[Span->SpanAxisIndex] - Tolerance
					|| Endpoint.BearingPlaneCM
					> RailBounds.Max[Span->SpanAxisIndex] + Tolerance)
				{
					if (Closed.Members.Num() >= Settings.BeamA.MaxMemberCount
						|| Closed.Joints.Num() + 2
							> Settings.BeamA.MaxJointCount)
					{
						OutError = TEXT("BeamBBridgeRailExtensionBudgetExceeded");
						return false;
					}
					const FABTSM73BeamAMember& SourceRail =
						Closed.Members[RailMemberId];
					if (!Closed.Joints.IsValidIndex(SourceRail.JointA)
						|| !Closed.Joints.IsValidIndex(SourceRail.JointB))
					{
						OutError = TEXT("BeamBBridgeRailExtensionJointMissing");
						return false;
					}
					const FVector SourceA =
						Closed.Joints[SourceRail.JointA].LocalPosition;
					const FVector SourceB =
						Closed.Joints[SourceRail.JointB].LocalPosition;
					FVector RailEnd = FMath::Abs(
						SourceA[Span->SpanAxisIndex] - Endpoint.BearingPlaneCM)
						<= FMath::Abs(
							SourceB[Span->SpanAxisIndex] - Endpoint.BearingPlaneCM)
							? SourceA : SourceB;
					FVector BearingEnd = RailEnd;
					BearingEnd[Span->SpanAxisIndex] = Endpoint.BearingPlaneCM;
					const int32 JointA = Closed.Joints.Num();
					FABTSM73BeamAJoint& A =
						Closed.Joints.AddDefaulted_GetRef();
					A.JointId = JointA;
					A.LocalPosition = RailEnd;
					A.Role = EABTSM73BeamAJointRole::BeamEnd;
					const int32 JointB = Closed.Joints.Num();
					FABTSM73BeamAJoint& B =
						Closed.Joints.AddDefaulted_GetRef();
					B.JointId = JointB;
					B.LocalPosition = BearingEnd;
					B.Role = EABTSM73BeamAJointRole::BeamEnd;
					FABTSM73BeamAMember& Extension =
						Closed.Members.AddDefaulted_GetRef();
					Extension.MemberId = Closed.Members.Num() - 1;
					Extension.JointA = JointA;
					Extension.JointB = JointB;
					Extension.Axis = static_cast<EABTSM73BeamAFrameAxis>(
						Span->SpanAxisIndex);
					Extension.Role = EABTSM73BeamAMemberRole::BridgeRail;
					Extension.LengthCM = (BearingEnd - RailEnd).Size();
					RailMemberId = Extension.MemberId;
					const int32 SpanAssemblyId = (*SpanAssemblies)[0];
					FABTSM73BeamAAssembly& SpanOwner =
						Closed.Assemblies[SpanAssemblyId];
					SpanOwner.JointIds.AddUnique(JointA);
					SpanOwner.JointIds.AddUnique(JointB);
					SpanOwner.MemberIds.AddUnique(RailMemberId);
					OwnersByMember.FindOrAdd(RailMemberId).AddUnique(
						SpanAssemblyId);
					RailBounds = ClosedMemberBounds(
						Closed.Members[RailMemberId], Closed, CrossSection);
				}
				auto InstallUpperPost = [&](const TArray<int32>& LowerMemberIds)
				{
					int32 LowerMemberId = INDEX_NONE;
					FBox LowerBounds(EForceInit::ForceInit);
					int32 UpperMemberId = INDEX_NONE;
					FBox UpperBounds(EForceInit::ForceInit);
					double BestUpperGap = TNumericLimits<double>::Max();
					for (const int32 CandidateLowerId : LowerMemberIds)
					{
						if (!Closed.Members.IsValidIndex(CandidateLowerId))
						{
							continue;
						}
						const FABTSM73BeamAMember& CandidateLower =
							Closed.Members[CandidateLowerId];
						if (CandidateLower.Axis == EABTSM73BeamAFrameAxis::Z
							|| CandidateLower.Axis
								== EABTSM73BeamAFrameAxis::Diagonal)
						{
							continue;
						}
						const FBox CandidateLowerBounds = ClosedMemberBounds(
							CandidateLower, Closed, CrossSection);
						for (const int32 CandidateId : SupportModuleMemberIds)
						{
							if (!Closed.Members.IsValidIndex(CandidateId)
								|| CandidateId == CandidateLowerId)
							{
								continue;
							}
							const FABTSM73BeamAMember& Candidate =
								Closed.Members[CandidateId];
							if (Candidate.Axis == EABTSM73BeamAFrameAxis::Z
								|| Candidate.Axis
									== EABTSM73BeamAFrameAxis::Diagonal
								|| Candidate.Role
									== EABTSM73BeamAMemberRole::BridgeRail
								|| Candidate.Role
									== EABTSM73BeamAMemberRole::BridgePost)
							{
								continue;
							}
							const FBox Bounds = ClosedMemberBounds(
								Candidate, Closed, CrossSection);
							const double XOverlap = FMath::Min(
								CandidateLowerBounds.Max.X, Bounds.Max.X)
								- FMath::Max(
									CandidateLowerBounds.Min.X, Bounds.Min.X);
							const double YOverlap = FMath::Min(
								CandidateLowerBounds.Max.Y, Bounds.Max.Y)
								- FMath::Max(
									CandidateLowerBounds.Min.Y, Bounds.Min.Y);
							const double VerticalGap = Bounds.Min.Z
								- CandidateLowerBounds.Max.Z;
							if (XOverlap <= Tolerance || YOverlap <= Tolerance
								|| VerticalGap <= Tolerance
								|| VerticalGap >= BestUpperGap)
							{
								continue;
							}
							LowerMemberId = CandidateLowerId;
							LowerBounds = CandidateLowerBounds;
							UpperMemberId = CandidateId;
							UpperBounds = Bounds;
							BestUpperGap = VerticalGap;
						}
					}
					if (LowerMemberId == INDEX_NONE || UpperMemberId == INDEX_NONE)
					{
						return true;
					}

					const double OverlapMinX = FMath::Max(
						LowerBounds.Min.X, UpperBounds.Min.X);
					const double OverlapMaxX = FMath::Min(
						LowerBounds.Max.X, UpperBounds.Max.X);
					const double OverlapMinY = FMath::Max(
						LowerBounds.Min.Y, UpperBounds.Min.Y);
					const double OverlapMaxY = FMath::Min(
						LowerBounds.Max.Y, UpperBounds.Max.Y);
					FVector LowerPosition(
						(OverlapMinX + OverlapMaxX) * 0.5,
						(OverlapMinY + OverlapMaxY) * 0.5,
						LowerBounds.Max.Z);
					FVector UpperPosition = LowerPosition;
					UpperPosition.Z = UpperBounds.Min.Z;
					const FVector PostCenter =
						(LowerPosition + UpperPosition) * 0.5;
					const FBox PostBounds(
						PostCenter - FVector(
							CrossSection * 0.5,
							CrossSection * 0.5,
							BestUpperGap * 0.5),
						PostCenter + FVector(
							CrossSection * 0.5,
							CrossSection * 0.5,
							BestUpperGap * 0.5));
					for (const FABTSM73BeamAMember& Existing : Closed.Members)
					{
						if (Existing.MemberId == LowerMemberId
							|| Existing.MemberId == UpperMemberId)
						{
							continue;
						}
						const FBox ExistingBounds = ClosedMemberBounds(
							Existing, Closed, CrossSection);
						const double XOverlap = FMath::Min(
							PostBounds.Max.X, ExistingBounds.Max.X)
							- FMath::Max(PostBounds.Min.X, ExistingBounds.Min.X);
						const double YOverlap = FMath::Min(
							PostBounds.Max.Y, ExistingBounds.Max.Y)
							- FMath::Max(PostBounds.Min.Y, ExistingBounds.Min.Y);
						const double ZOverlap = FMath::Min(
							PostBounds.Max.Z, ExistingBounds.Max.Z)
							- FMath::Max(PostBounds.Min.Z, ExistingBounds.Min.Z);
						if (XOverlap > Tolerance && YOverlap > Tolerance
							&& ZOverlap > Tolerance)
						{
							// An existing physical member already occupies this
							// transfer lane; never create an overlapping repair.
							return true;
						}
					}
					if (Closed.Members.Num() >= Settings.BeamA.MaxMemberCount
						|| Closed.Joints.Num() + 2
							> Settings.BeamA.MaxJointCount)
					{
						OutError = TEXT("BeamBBridgeUpperPostBudgetExceeded");
						return false;
					}
					const int32 JointA = Closed.Joints.Num();
					FABTSM73BeamAJoint& A =
						Closed.Joints.AddDefaulted_GetRef();
					A.JointId = JointA;
					A.LocalPosition = LowerPosition;
					A.Role = EABTSM73BeamAJointRole::BeamEnd;
					const int32 JointB = Closed.Joints.Num();
					FABTSM73BeamAJoint& B =
						Closed.Joints.AddDefaulted_GetRef();
					B.JointId = JointB;
					B.LocalPosition = UpperPosition;
					B.Role = EABTSM73BeamAJointRole::ColumnHead;
					FABTSM73BeamAMember& Post =
						Closed.Members.AddDefaulted_GetRef();
					Post.MemberId = Closed.Members.Num() - 1;
					Post.JointA = JointA;
					Post.JointB = JointB;
					Post.Axis = EABTSM73BeamAFrameAxis::Z;
					Post.Role = EABTSM73BeamAMemberRole::BridgePost;
					Post.LengthCM = BestUpperGap;
					FABTSM73BeamAAssembly& Owner =
						Closed.Assemblies[SupportAssemblyId];
					Owner.JointIds.AddUnique(JointA);
					Owner.JointIds.AddUnique(JointB);
					Owner.MemberIds.AddUnique(Post.MemberId);
					OwnersByMember.FindOrAdd(Post.MemberId).AddUnique(
						SupportAssemblyId);
					++InOutResult.Summary.BridgeUpperPostMemberCount;
					return AddContact(
						LowerMemberId,
						Post.MemberId,
						LowerPosition,
						CrossSection * CrossSection)
						&& AddContact(
							Post.MemberId,
							UpperMemberId,
							UpperPosition,
							CrossSection * CrossSection);
				};
				const FABTSM73BeamAMember* BestSeat = nullptr;
				double BestSeatTop = 0.0;
				double BestSeatTopScore = -TNumericLimits<double>::Max();
				for (const FABTSM73BeamAMember& Seat : Closed.Members)
				{
					if (Seat.Role != EABTSM73BeamAMemberRole::BridgeSeat
						|| Seat.Axis == EABTSM73BeamAFrameAxis::Z)
					{
						continue;
					}
					const FBox Bounds = ClosedMemberBounds(
						Seat, Closed, CrossSection);
					if (Endpoint.BearingPlaneCM < Bounds.Min[Span->SpanAxisIndex]
							- Tolerance
						|| Endpoint.BearingPlaneCM
							> Bounds.Max[Span->SpanAxisIndex] + Tolerance
						|| RailStation < Bounds.Min[Perpendicular] - Tolerance
						|| RailStation > Bounds.Max[Perpendicular] + Tolerance)
					{
						continue;
					}
					if (Bounds.Max.Z <= RailBounds.Min.Z + Tolerance
						&& Bounds.Max.Z > BestSeatTopScore)
					{
						BestSeat = &Seat;
						BestSeatTop = Bounds.Max.Z;
						BestSeatTopScore = Bounds.Max.Z;
					}
				}
				if (BestSeat == nullptr)
				{
					const FABTSM73BeamAMember* BestSupport = nullptr;
					double BestSupportTop = -TNumericLimits<double>::Max();
					double BestSupportAxis = Endpoint.BearingPlaneCM;
					bool bNeedsHorizontalOutrigger = false;
					const double StationMin = Endpoint.RailStationsCM.IsEmpty()
						? RailStation : Endpoint.RailStationsCM[0];
					const double StationMax = Endpoint.RailStationsCM.IsEmpty()
						? RailStation : Endpoint.RailStationsCM.Last();
					for (const int32 MemberId : SupportAssembly->MemberIds)
					{
						if (!Closed.Members.IsValidIndex(MemberId))
						{
							continue;
						}
						const FABTSM73BeamAMember& Candidate =
							Closed.Members[MemberId];
						const FBox Bounds = ClosedMemberBounds(
							Candidate, Closed, CrossSection);
						const double StationOverlap = FMath::Min(
							Bounds.Max[Perpendicular], StationMax + Tolerance)
							- FMath::Max(
								Bounds.Min[Perpendicular], StationMin - Tolerance);
						if (Endpoint.BearingPlaneCM
							< Bounds.Min[Span->SpanAxisIndex] - Tolerance
							|| Endpoint.BearingPlaneCM
							> Bounds.Max[Span->SpanAxisIndex] + Tolerance
							|| StationOverlap <= Tolerance
							|| Bounds.Max.Z > RailBounds.Min.Z + Tolerance
							|| Bounds.Max.Z <= BestSupportTop)
						{
							continue;
						}
						BestSupport = &Candidate;
						BestSupportTop = Bounds.Max.Z;
					}
					if (BestSupport == nullptr)
					{
						double BestRemoteScore = TNumericLimits<double>::Max();
						for (const int32 MemberId : SupportAssembly->MemberIds)
						{
							if (!Closed.Members.IsValidIndex(MemberId))
							{
								continue;
							}
							const FABTSM73BeamAMember& Candidate =
								Closed.Members[MemberId];
							const FBox Bounds = ClosedMemberBounds(
								Candidate, Closed, CrossSection);
							if (RailStation
								< Bounds.Min[Perpendicular] - Tolerance
								|| RailStation
								> Bounds.Max[Perpendicular] + Tolerance
								|| Bounds.Max.Z + CrossSection
									> RailBounds.Min.Z + Tolerance)
							{
								continue;
							}
							const double PlaneGap = FMath::Max(
								Bounds.Min[Span->SpanAxisIndex]
									- Endpoint.BearingPlaneCM,
								Endpoint.BearingPlaneCM
									- Bounds.Max[Span->SpanAxisIndex]);
							const double Score = FMath::Max(0.0, PlaneGap) * 4.0
								+ (RailBounds.Min.Z - CrossSection
									- Bounds.Max.Z);
							if (Score < BestRemoteScore)
							{
								BestSupport = &Candidate;
								BestSupportTop = Bounds.Max.Z;
								BestSupportAxis = Bounds.GetCenter()[
									Span->SpanAxisIndex];
								BestRemoteScore = Score;
							}
						}
						bNeedsHorizontalOutrigger = BestSupport != nullptr;
					}
					if (BestSupport != nullptr)
					{
						if (Closed.Members.Num() >= Settings.BeamA.MaxMemberCount
							|| Closed.Joints.Num() + 2
								> Settings.BeamA.MaxJointCount)
						{
							OutError = TEXT("BeamBBridgeBearerMemberBudgetExceeded");
							return false;
						}
						const int32 SupportMemberId = BestSupport->MemberId;
						const int32 JointA = Closed.Joints.Num();
						FABTSM73BeamAJoint& A =
							Closed.Joints.AddDefaulted_GetRef();
						A.JointId = JointA;
						A.LocalPosition = FVector::ZeroVector;
						A.LocalPosition[Span->SpanAxisIndex] =
							bNeedsHorizontalOutrigger
								? BestSupportAxis : Endpoint.BearingPlaneCM;
						A.LocalPosition[Perpendicular] =
							bNeedsHorizontalOutrigger ? RailStation : StationMin;
						A.LocalPosition.Z = BestSupportTop + CrossSection * 0.5;
						A.Role = EABTSM73BeamAJointRole::BeamEnd;
						const int32 JointB = Closed.Joints.Num();
						FABTSM73BeamAJoint& B =
							Closed.Joints.AddDefaulted_GetRef();
						B = A;
						B.JointId = JointB;
						if (bNeedsHorizontalOutrigger)
						{
							B.LocalPosition[Span->SpanAxisIndex] =
								Endpoint.BearingPlaneCM;
						}
						else
						{
							B.LocalPosition[Perpendicular] = StationMax;
						}
						FABTSM73BeamAMember& Bearer =
							Closed.Members.AddDefaulted_GetRef();
						Bearer.MemberId = Closed.Members.Num() - 1;
						Bearer.JointA = JointA;
						Bearer.JointB = JointB;
						Bearer.Axis = bNeedsHorizontalOutrigger
							? static_cast<EABTSM73BeamAFrameAxis>(
								Span->SpanAxisIndex)
							: (Perpendicular == 0
								? EABTSM73BeamAFrameAxis::X
								: EABTSM73BeamAFrameAxis::Y);
						Bearer.Role = EABTSM73BeamAMemberRole::BridgeSeat;
						Bearer.LengthCM = (B.LocalPosition - A.LocalPosition).Size();
						FABTSM73BeamAAssembly& Owner =
							Closed.Assemblies[SupportAssemblyId];
						Owner.JointIds.AddUnique(JointA);
						Owner.JointIds.AddUnique(JointB);
						Owner.MemberIds.AddUnique(Bearer.MemberId);
						FVector SupportContact = FVector::ZeroVector;
						SupportContact[Span->SpanAxisIndex] =
							bNeedsHorizontalOutrigger
								? BestSupportAxis : Endpoint.BearingPlaneCM;
						SupportContact[Perpendicular] = RailStation;
						SupportContact.Z = BestSupportTop;
						if (!AddContact(
							SupportMemberId,
							Bearer.MemberId,
							SupportContact,
							CrossSection * CrossSection))
						{
							return false;
						}
						BestSeat = &Closed.Members[Bearer.MemberId];
						BestSeatTop = BestSupportTop + CrossSection;
					}
				}
				if (BestSeat == nullptr)
				{
					for (const FABTSM73BeamAMember& CandidateSeat : Closed.Members)
					{
						if (CandidateSeat.Role
							!= EABTSM73BeamAMemberRole::BridgeSeat)
						{
							continue;
						}
						const FBox Bounds = ClosedMemberBounds(
							CandidateSeat, Closed, CrossSection);
						UE_LOG(
							LogABTSRuntime,
							Warning,
							TEXT("[ABTS][M7.3-Beam-B][BridgeCorbelSeatCandidate] Span=%d Plane=%.2f Station=%.2f Member=%d Axis=%d Min=%s Max=%s RailBottom=%.2f"),
							Endpoint.SpanVolumeId,
							Endpoint.BearingPlaneCM,
							RailStation,
							CandidateSeat.MemberId,
							static_cast<int32>(CandidateSeat.Axis),
							*Bounds.Min.ToCompactString(),
							*Bounds.Max.ToCompactString(),
							RailBounds.Min.Z);
					}
					OutError = TEXT("BeamBBridgeCorbelSeatMissing");
					return false;
				}
				const int32 SeatMemberId = BestSeat->MemberId;
				const double Gap = RailBounds.Min.Z - BestSeatTop;
				if (Gap < -Tolerance)
				{
					OutError = TEXT("BeamBBridgeCorbelNegativeGap");
					return false;
				}
				if (Gap <= Tolerance)
				{
					FVector RailContact = FVector::ZeroVector;
					RailContact[Span->SpanAxisIndex] =
						Endpoint.BearingPlaneCM;
					RailContact[Perpendicular] = RailStation;
					RailContact.Z = BestSeatTop;
					if (!AddContact(
						SeatMemberId,
						RailMemberId,
						RailContact,
						CrossSection * CrossSection))
					{
						return false;
					}
					if (!InstallUpperPost({ RailMemberId, SeatMemberId }))
					{
						return false;
					}
					continue;
				}
				if (Closed.Members.Num() >= Settings.BeamA.MaxMemberCount
					|| Closed.Joints.Num() + 2 > Settings.BeamA.MaxJointCount)
				{
					OutError = TEXT("BeamBBridgeCorbelMemberBudgetExceeded");
					return false;
				}
				const int32 JointA = Closed.Joints.Num();
				FABTSM73BeamAJoint& A = Closed.Joints.AddDefaulted_GetRef();
				A.JointId = JointA;
				A.LocalPosition = FVector::ZeroVector;
				A.LocalPosition[Span->SpanAxisIndex] = Endpoint.BearingPlaneCM;
				A.LocalPosition[Perpendicular] = RailStation;
				A.LocalPosition.Z = BestSeatTop;
				A.Role = EABTSM73BeamAJointRole::BeamEnd;
				const int32 JointB = Closed.Joints.Num();
				FABTSM73BeamAJoint& B = Closed.Joints.AddDefaulted_GetRef();
				B.JointId = JointB;
				B.LocalPosition = A.LocalPosition;
				B.LocalPosition.Z = RailBounds.Min.Z;
				B.Role = EABTSM73BeamAJointRole::ColumnHead;
				FABTSM73BeamAMember& Corbel =
					Closed.Members.AddDefaulted_GetRef();
				Corbel.MemberId = Closed.Members.Num() - 1;
				Corbel.JointA = JointA;
				Corbel.JointB = JointB;
				Corbel.Axis = EABTSM73BeamAFrameAxis::Z;
				Corbel.Role = EABTSM73BeamAMemberRole::BridgeSeat;
				Corbel.LengthCM = Gap;
				FABTSM73BeamAAssembly& Owner =
					Closed.Assemblies[SupportAssemblyId];
				Owner.JointIds.AddUnique(JointA);
				Owner.JointIds.AddUnique(JointB);
				Owner.MemberIds.AddUnique(Corbel.MemberId);
				if (!AddContact(
					SeatMemberId,
					Corbel.MemberId,
					A.LocalPosition,
					CrossSection * CrossSection)
					|| !AddContact(
						Corbel.MemberId,
						RailMemberId,
						B.LocalPosition,
						CrossSection * CrossSection))
				{
					return false;
				}
				if (!InstallUpperPost({ RailMemberId, SeatMemberId }))
				{
					return false;
				}
			}
		}
		Closed.Summary.JointCount = Closed.Joints.Num();
		Closed.Summary.MemberCount = Closed.Members.Num();
		Closed.Summary.BearingContactCount = Closed.BearingContacts.Num();
		return true;
	}

	bool InstallSuspendedBridgeBeamPosts(
		const FABTSM73BeamBPreviewSettings& Settings,
		FABTSM73BeamBGenerationResult& InOutResult,
		FString& OutError)
	{
		FABTSM73BeamAGenerationResult& Closed =
			InOutResult.ClosedAssembly;
		const double CrossSection = Settings.BeamA.BlockCrossSectionCM;
		const double Tolerance = Settings.BeamA.JointMergeToleranceCM;
		const int32 InitialMemberCount = Closed.Members.Num();
		TMap<int32, TArray<int32>> OwnersByMember;
		for (const FABTSM73BeamAAssembly& Assembly : Closed.Assemblies)
		{
			for (const int32 MemberId : Assembly.MemberIds)
			{
				OwnersByMember.FindOrAdd(MemberId).AddUnique(
					Assembly.AssemblyId);
			}
		}

		struct FSuspendedBeamTarget
		{
			int32 LowerMemberId = INDEX_NONE;
			int32 UpperMemberId = INDEX_NONE;
			FBox LowerBounds = FBox(EForceInit::ForceInit);
			FBox UpperBounds = FBox(EForceInit::ForceInit);
		};

		TArray<int32> LowerMemberIds;
		for (int32 MemberId = 0; MemberId < InitialMemberCount; ++MemberId)
		{
			const FABTSM73BeamAMember& Member = Closed.Members[MemberId];
			const bool bBridgeBearer = Member.Role
					== EABTSM73BeamAMemberRole::BridgeRail
				|| Member.Role == EABTSM73BeamAMemberRole::BridgeSeat;
			if (bBridgeBearer
				&& Member.Axis != EABTSM73BeamAFrameAxis::Z
				&& Member.Axis != EABTSM73BeamAFrameAxis::Diagonal)
			{
				LowerMemberIds.Add(MemberId);
			}
		}
		LowerMemberIds.Sort([&Closed, CrossSection](
			const int32 A, const int32 B)
		{
			const double TopA = ClosedMemberBounds(
				Closed.Members[A], Closed, CrossSection).Max.Z;
			const double TopB = ClosedMemberBounds(
				Closed.Members[B], Closed, CrossSection).Max.Z;
			return !FMath::IsNearlyEqual(TopA, TopB)
				? TopA > TopB : A < B;
		});

		TArray<FSuspendedBeamTarget> Targets;
		TMap<int32, int32> TargetIndexByUpperMember;
		for (const int32 LowerMemberId : LowerMemberIds)
		{
			const FBox LowerBounds = ClosedMemberBounds(
				Closed.Members[LowerMemberId], Closed, CrossSection);
			double NearestGap = TNumericLimits<double>::Max();
			TArray<int32> NearestUpperMemberIds;
			for (int32 CandidateId = 0;
				CandidateId < InitialMemberCount; ++CandidateId)
			{
				if (CandidateId == LowerMemberId)
				{
					continue;
				}
				const FABTSM73BeamAMember& Candidate =
					Closed.Members[CandidateId];
				if (Candidate.Axis == EABTSM73BeamAFrameAxis::Z
					|| Candidate.Axis
						== EABTSM73BeamAFrameAxis::Diagonal
					|| Candidate.Role
						== EABTSM73BeamAMemberRole::BridgePost)
				{
					continue;
				}
				const FBox UpperBounds = ClosedMemberBounds(
					Candidate, Closed, CrossSection);
				const double XOverlap = FMath::Min(
					LowerBounds.Max.X, UpperBounds.Max.X)
					- FMath::Max(LowerBounds.Min.X, UpperBounds.Min.X);
				const double YOverlap = FMath::Min(
					LowerBounds.Max.Y, UpperBounds.Max.Y)
					- FMath::Max(LowerBounds.Min.Y, UpperBounds.Min.Y);
				const double VerticalGap = UpperBounds.Min.Z
					- LowerBounds.Max.Z;
				if (XOverlap <= Tolerance || YOverlap <= Tolerance
					|| VerticalGap <= Tolerance)
				{
					continue;
				}
				if (VerticalGap < NearestGap - Tolerance)
				{
					NearestGap = VerticalGap;
					NearestUpperMemberIds.Reset();
					NearestUpperMemberIds.Add(CandidateId);
				}
				else if (FMath::IsNearlyEqual(
					VerticalGap, NearestGap, Tolerance))
				{
					NearestUpperMemberIds.Add(CandidateId);
				}
			}
			NearestUpperMemberIds.Sort();
			for (const int32 UpperMemberId : NearestUpperMemberIds)
			{
				const FBox UpperBounds = ClosedMemberBounds(
					Closed.Members[UpperMemberId], Closed, CrossSection);
				const double NewOverlapArea = FMath::Max(0.0,
					FMath::Min(LowerBounds.Max.X, UpperBounds.Max.X)
						- FMath::Max(LowerBounds.Min.X, UpperBounds.Min.X))
					* FMath::Max(0.0,
						FMath::Min(LowerBounds.Max.Y, UpperBounds.Max.Y)
							- FMath::Max(
								LowerBounds.Min.Y, UpperBounds.Min.Y));
				const int32* ExistingTargetIndex =
					TargetIndexByUpperMember.Find(UpperMemberId);
				if (ExistingTargetIndex != nullptr)
				{
					FSuspendedBeamTarget& ExistingTarget =
						Targets[*ExistingTargetIndex];
					const double ExistingOverlapArea = FMath::Max(0.0,
						FMath::Min(
							ExistingTarget.LowerBounds.Max.X,
							ExistingTarget.UpperBounds.Max.X)
							- FMath::Max(
								ExistingTarget.LowerBounds.Min.X,
								ExistingTarget.UpperBounds.Min.X))
						* FMath::Max(0.0,
							FMath::Min(
								ExistingTarget.LowerBounds.Max.Y,
								ExistingTarget.UpperBounds.Max.Y)
								- FMath::Max(
									ExistingTarget.LowerBounds.Min.Y,
									ExistingTarget.UpperBounds.Min.Y));
					if (NewOverlapArea <= ExistingOverlapArea + Tolerance)
					{
						continue;
					}
					ExistingTarget.LowerMemberId = LowerMemberId;
					ExistingTarget.LowerBounds = LowerBounds;
					ExistingTarget.UpperBounds = UpperBounds;
					continue;
				}
				const int32 TargetIndex = Targets.Num();
				FSuspendedBeamTarget& Target = Targets.AddDefaulted_GetRef();
				Target.LowerMemberId = LowerMemberId;
				Target.UpperMemberId = UpperMemberId;
				Target.LowerBounds = LowerBounds;
				Target.UpperBounds = UpperBounds;
				TargetIndexByUpperMember.Add(UpperMemberId, TargetIndex);
			}
		}

		auto AddContact = [
			&Closed, &Settings, &OutError, CrossSection](
			const int32 LowerMemberId,
			const int32 UpperMemberId,
			const FVector& Position)
		{
			if (Closed.BearingContacts.ContainsByPredicate(
				[LowerMemberId, UpperMemberId](
					const FABTSM73BeamABearingContact& Contact)
				{
					return Contact.LowerMemberId == LowerMemberId
						&& Contact.UpperMemberId == UpperMemberId;
				}))
			{
				return true;
			}
			if (Closed.BearingContacts.Num()
				>= Settings.BeamA.MaxBearingContactCount)
			{
				OutError = TEXT(
					"BeamBBridgeSuspendedPostBearingBudgetExceeded");
				return false;
			}
			FABTSM73BeamABearingContact& Contact =
				Closed.BearingContacts.AddDefaulted_GetRef();
			Contact.ContactId = Closed.BearingContacts.Num() - 1;
			Contact.LowerMemberId = LowerMemberId;
			Contact.UpperMemberId = UpperMemberId;
			const FABTSM73BeamAMember& Lower =
				Closed.Members[LowerMemberId];
			const FABTSM73BeamAMember& Upper =
				Closed.Members[UpperMemberId];
			Contact.Type = Upper.Axis == EABTSM73BeamAFrameAxis::Z
				? EABTSM73BeamABearingType::PostOnBeam
				: (Lower.Axis == EABTSM73BeamAFrameAxis::Z
					? EABTSM73BeamABearingType::BeamOnPost
					: (Lower.Axis != Upper.Axis
						? EABTSM73BeamABearingType::CrossBearing
						: EABTSM73BeamABearingType::ParallelBearing));
			Contact.LocalPosition = Position;
			Contact.ContactAreaCM2 = CrossSection * CrossSection;
			return true;
		};

		InOutResult.Summary.BridgeSuspendedBeamTargetCount = Targets.Num();
		InOutResult.Summary.BridgeSuspendedBeamSupportedCount = 0;
		InOutResult.Summary.BridgeSuspendedBeamSupportViolationCount = 0;
		int32 ExistingSupportCount = 0;
		int32 AddedPostCount = 0;
		for (const FSuspendedBeamTarget& Target : Targets)
		{
			auto IsPhysicalSupport = [&Closed, &Target, CrossSection, Tolerance](
				const FABTSM73BeamAMember& Member)
			{
				if (Member.MemberId == Target.UpperMemberId
					|| Member.Axis != EABTSM73BeamAFrameAxis::Z)
				{
					return false;
				}
				const FBox Bounds = ClosedMemberBounds(
					Member, Closed, CrossSection);
				const double XOverlap = FMath::Min(
					Bounds.Max.X, Target.UpperBounds.Max.X)
					- FMath::Max(Bounds.Min.X, Target.UpperBounds.Min.X);
				const double YOverlap = FMath::Min(
					Bounds.Max.Y, Target.UpperBounds.Max.Y)
					- FMath::Max(Bounds.Min.Y, Target.UpperBounds.Min.Y);
				return XOverlap > Tolerance && YOverlap > Tolerance
					&& FMath::Abs(
						Bounds.Max.Z - Target.UpperBounds.Min.Z) <= Tolerance
					&& Bounds.Min.Z < Target.UpperBounds.Min.Z - Tolerance;
			};
			if (Closed.Members.ContainsByPredicate(IsPhysicalSupport))
			{
				++ExistingSupportCount;
				++InOutResult.Summary.BridgeSuspendedBeamSupportedCount;
				continue;
			}

			const double OverlapMinX = FMath::Max(
				Target.LowerBounds.Min.X, Target.UpperBounds.Min.X);
			const double OverlapMaxX = FMath::Min(
				Target.LowerBounds.Max.X, Target.UpperBounds.Max.X);
			const double OverlapMinY = FMath::Max(
				Target.LowerBounds.Min.Y, Target.UpperBounds.Min.Y);
			const double OverlapMaxY = FMath::Min(
				Target.LowerBounds.Max.Y, Target.UpperBounds.Max.Y);
			const FVector2D OverlapCenter(
				(OverlapMinX + OverlapMaxX) * 0.5,
				(OverlapMinY + OverlapMaxY) * 0.5);
			auto BuildAxisCandidates = [CrossSection, Tolerance](
				const double Minimum, const double Maximum)
			{
				TArray<double> Values;
				Values.Add((Minimum + Maximum) * 0.5);
				const double Span = Maximum - Minimum;
				if (Span > Tolerance)
				{
					const int32 StepCount = FMath::Clamp(
						FMath::CeilToInt(Span / FMath::Max(
							CrossSection * 0.5, Tolerance)),
						1,
						64);
					for (int32 Step = 0; Step <= StepCount; ++Step)
					{
						Values.AddUnique(FMath::Lerp(
							Minimum,
							Maximum,
							static_cast<double>(Step) / StepCount));
					}
				}
				return Values;
			};
			const TArray<double> CandidateXs = BuildAxisCandidates(
				OverlapMinX, OverlapMaxX);
			const TArray<double> CandidateYs = BuildAxisCandidates(
				OverlapMinY, OverlapMaxY);
			TArray<FVector2D> CandidateCenters;
			CandidateCenters.Add(OverlapCenter);
			for (const double CandidateX : CandidateXs)
			{
				for (const double CandidateY : CandidateYs)
				{
					CandidateCenters.AddUnique(FVector2D(
						CandidateX, CandidateY));
				}
			}

			const double PostBottomZ = Target.LowerBounds.Max.Z;
			const double PostTopZ = Target.UpperBounds.Min.Z;
			const double PostLength = PostTopZ - PostBottomZ;
			FVector PostCenter = FVector::ZeroVector;
			bool bFoundClearLane = false;
			int32 LastBlockingMemberId = INDEX_NONE;
			for (const FVector2D& CandidateCenter : CandidateCenters)
			{
				PostCenter = FVector(
					CandidateCenter.X,
					CandidateCenter.Y,
					(PostBottomZ + PostTopZ) * 0.5);
				const FBox PostBounds(
					PostCenter - FVector(
						CrossSection * 0.5,
						CrossSection * 0.5,
						PostLength * 0.5),
					PostCenter + FVector(
						CrossSection * 0.5,
						CrossSection * 0.5,
						PostLength * 0.5));
				bool bBlocked = false;
				for (const FABTSM73BeamAMember& Existing : Closed.Members)
				{
					if (Existing.MemberId == Target.LowerMemberId
						|| Existing.MemberId == Target.UpperMemberId)
					{
						continue;
					}
					const FBox ExistingBounds = ClosedMemberBounds(
						Existing, Closed, CrossSection);
					const double XOverlap = FMath::Min(
						PostBounds.Max.X, ExistingBounds.Max.X)
						- FMath::Max(
							PostBounds.Min.X, ExistingBounds.Min.X);
					const double YOverlap = FMath::Min(
						PostBounds.Max.Y, ExistingBounds.Max.Y)
						- FMath::Max(
							PostBounds.Min.Y, ExistingBounds.Min.Y);
					const double ZOverlap = FMath::Min(
						PostBounds.Max.Z, ExistingBounds.Max.Z)
						- FMath::Max(
							PostBounds.Min.Z, ExistingBounds.Min.Z);
					if (XOverlap > Tolerance && YOverlap > Tolerance
						&& ZOverlap > Tolerance)
					{
						LastBlockingMemberId = Existing.MemberId;
						bBlocked = true;
						break;
					}
				}
				if (!bBlocked)
				{
					bFoundClearLane = true;
					break;
				}
			}
			if (!bFoundClearLane)
			{
				const bool bHasDirectCrossBearing =
					Closed.Members.ContainsByPredicate(
						[&Closed, &Target, CrossSection, Tolerance](
							const FABTSM73BeamAMember& Member)
						{
							if (Member.MemberId == Target.UpperMemberId)
							{
								return false;
							}
							const FBox Bounds = ClosedMemberBounds(
								Member, Closed, CrossSection);
							const double XOverlap = FMath::Min(
								Bounds.Max.X, Target.UpperBounds.Max.X)
								- FMath::Max(
									Bounds.Min.X, Target.UpperBounds.Min.X);
							const double YOverlap = FMath::Min(
								Bounds.Max.Y, Target.UpperBounds.Max.Y)
								- FMath::Max(
									Bounds.Min.Y, Target.UpperBounds.Min.Y);
							return XOverlap > Tolerance
								&& YOverlap > Tolerance
								&& FMath::Abs(
									Bounds.Max.Z
										- Target.UpperBounds.Min.Z)
									<= Tolerance
								&& Bounds.Min.Z
									< Target.UpperBounds.Min.Z - Tolerance;
						});
				if (bHasDirectCrossBearing)
				{
					++ExistingSupportCount;
					++InOutResult.Summary
						.BridgeSuspendedBeamSupportedCount;
					continue;
				}
				const FABTSM73BeamAMember* Blocker =
					Closed.Members.IsValidIndex(LastBlockingMemberId)
						? &Closed.Members[LastBlockingMemberId] : nullptr;
				UE_LOG(
					LogABTSRuntime,
					Warning,
					TEXT("[ABTS][M7.3-Beam-B][SuspendedBeamPostLaneBlocked] Lower=%d Upper=%d LowerBounds=%s..%s UpperBounds=%s..%s Blocker=%d BlockerAxis=%d BlockerRole=%d BlockerBounds=%s..%s"),
					Target.LowerMemberId,
					Target.UpperMemberId,
					*Target.LowerBounds.Min.ToCompactString(),
					*Target.LowerBounds.Max.ToCompactString(),
					*Target.UpperBounds.Min.ToCompactString(),
					*Target.UpperBounds.Max.ToCompactString(),
					LastBlockingMemberId,
					Blocker != nullptr
						? static_cast<int32>(Blocker->Axis) : INDEX_NONE,
					Blocker != nullptr
						? static_cast<int32>(Blocker->Role) : INDEX_NONE,
					Blocker != nullptr
						? *ClosedMemberBounds(
							*Blocker, Closed, CrossSection).Min.ToCompactString()
						: TEXT("None"),
					Blocker != nullptr
						? *ClosedMemberBounds(
							*Blocker, Closed, CrossSection).Max.ToCompactString()
						: TEXT("None"));
				++InOutResult.Summary
					.BridgeSuspendedBeamSupportViolationCount;
				continue;
			}
			if (Closed.Members.Num() >= Settings.BeamA.MaxMemberCount
				|| Closed.Joints.Num() + 2
					> Settings.BeamA.MaxJointCount)
			{
				OutError = TEXT("BeamBBridgeSuspendedPostBudgetExceeded");
				return false;
			}
			const TArray<int32>* Owners =
				OwnersByMember.Find(Target.LowerMemberId);
			if (Owners == nullptr || Owners->IsEmpty()
				|| !Closed.Assemblies.IsValidIndex((*Owners)[0]))
			{
				OutError = TEXT("BeamBBridgeSuspendedPostOwnerMissing");
				return false;
			}
			FVector LowerPosition = PostCenter;
			LowerPosition.Z = PostBottomZ;
			FVector UpperPosition = PostCenter;
			UpperPosition.Z = PostTopZ;
			const int32 JointA = Closed.Joints.Num();
			FABTSM73BeamAJoint& A = Closed.Joints.AddDefaulted_GetRef();
			A.JointId = JointA;
			A.LocalPosition = LowerPosition;
			A.Role = EABTSM73BeamAJointRole::BeamEnd;
			const int32 JointB = Closed.Joints.Num();
			FABTSM73BeamAJoint& B = Closed.Joints.AddDefaulted_GetRef();
			B.JointId = JointB;
			B.LocalPosition = UpperPosition;
			B.Role = EABTSM73BeamAJointRole::ColumnHead;
			FABTSM73BeamAMember& Post =
				Closed.Members.AddDefaulted_GetRef();
			Post.MemberId = Closed.Members.Num() - 1;
			Post.JointA = JointA;
			Post.JointB = JointB;
			Post.Axis = EABTSM73BeamAFrameAxis::Z;
			Post.Role = EABTSM73BeamAMemberRole::BridgePost;
			Post.LengthCM = PostLength;
			FABTSM73BeamAAssembly& Owner = Closed.Assemblies[(*Owners)[0]];
			Owner.JointIds.AddUnique(JointA);
			Owner.JointIds.AddUnique(JointB);
			Owner.MemberIds.AddUnique(Post.MemberId);
			OwnersByMember.FindOrAdd(Post.MemberId).AddUnique(Owner.AssemblyId);
			if (!AddContact(
				Target.LowerMemberId, Post.MemberId, LowerPosition)
				|| !AddContact(
					Post.MemberId, Target.UpperMemberId, UpperPosition))
			{
				return false;
			}
			++InOutResult.Summary.BridgeSuspendedBeamSupportedCount;
			++AddedPostCount;
		}
		UE_LOG(
			LogABTSRuntime,
			Display,
			TEXT("[ABTS][M7.3-Beam-B][SuspendedBeamSupportAudit] Targets=%d Supported=%d Existing=%d Added=%d Violations=%d"),
			InOutResult.Summary.BridgeSuspendedBeamTargetCount,
			InOutResult.Summary.BridgeSuspendedBeamSupportedCount,
			ExistingSupportCount,
			AddedPostCount,
			InOutResult.Summary.BridgeSuspendedBeamSupportViolationCount);
		Closed.Summary.JointCount = Closed.Joints.Num();
		Closed.Summary.MemberCount = Closed.Members.Num();
		Closed.Summary.BearingContactCount = Closed.BearingContacts.Num();
		return true;
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
				OutError = FString::Printf(
					TEXT("BeamBCompiledMemberTooShort:Id=%d:Role=%d:Length=%.2f:StartZ=%.2f:EndZ=%.2f"),
					Planned.PlannedMemberId,
					static_cast<int32>(Planned.Role),
					Length,
					Planned.LocalStart.Z,
					Planned.LocalEnd.Z);
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
		if (!InstallClosedBridgeEndpointCorbels(
			Settings, Silhouette, InOutResult, OutError))
		{
			return false;
		}
		if (!InstallSuspendedBridgeBeamPosts(
			Settings, InOutResult, OutError))
		{
			return false;
		}
		if (!ABTSM73BeamA::RebuildBearingContacts(
			Settings.BeamA, Closed, OutError))
		{
			OutError = FString::Printf(
				TEXT("BeamBFinalContactRebuild:%s"), *OutError);
			return false;
		}
		Closed.Summary.BearingContactCount = Closed.BearingContacts.Num();
		if (InOutResult.Summary.BridgeSuspendedBeamSupportViolationCount > 0)
		{
			OutError = FString::Printf(
				TEXT("BeamBBridgeSuspendedBeamSupportViolation:Count=%d"),
				InOutResult.Summary
					.BridgeSuspendedBeamSupportViolationCount);
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
		if (InOutResult.Summary.BridgeRailEndpointBearingViolationCount > 0)
		{
			OutError = FString::Printf(
				TEXT("BeamBBridgeRailEndpointBearingViolation:Count=%d"),
				InOutResult.Summary.BridgeRailEndpointBearingViolationCount);
			return false;
		}
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
		const int32 FirstExpandedMember = OutResult.PlannedMembers.Num();
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
		if (Volume->Role == EABTSM73DAG5BV2VolumeRole::SupportedSpan)
		{
			for (int32 MemberIndex = FirstExpandedMember;
				MemberIndex < OutResult.PlannedMembers.Num(); ++MemberIndex)
			{
				FABTSM73BeamBPlannedMember& Member =
					OutResult.PlannedMembers[MemberIndex];
				if (Member.Motif == EABTSM73BeamBMotif::BridgeBay
					&& Member.Role == EABTSM73BeamAMemberRole::PrimaryBeam
					&& static_cast<int32>(Member.Axis) == Volume->SpanAxisIndex)
				{
					Member.Role = EABTSM73BeamAMemberRole::BridgeRail;
				}
			}
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
			&& Member.Role == EABTSM73BeamAMemberRole::BridgeRail)
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
	OutResult.Summary.BridgeSeatMemberCount = 0;
	OutResult.Summary.BridgeUpperPostMemberCount = 0;
	for (const FABTSM73BeamAMember& Member :
		OutResult.ClosedAssembly.Members)
	{
		OutResult.Summary.BridgeSeatMemberCount += Member.Role
			== EABTSM73BeamAMemberRole::BridgeSeat ? 1 : 0;
		OutResult.Summary.BridgeUpperPostMemberCount += Member.Role
			== EABTSM73BeamAMemberRole::BridgePost ? 1 : 0;
	}
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
