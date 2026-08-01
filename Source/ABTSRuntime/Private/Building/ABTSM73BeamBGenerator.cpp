// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BeamBGenerator.h"

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

	void BuildDomain(
		const FABTSM73BeamBPreviewSettings& Settings,
		const FABTSM73BeamABay& Bay,
		const FABTSM73DAG5BV2Volume& Volume,
		TArray<EABTSM73BeamBMotif>& OutDomain)
	{
		OutDomain.Reset();
		if (Volume.Role == EABTSM73DAG5BV2VolumeRole::Bridge)
		{
			OutDomain.Add(EABTSM73BeamBMotif::BridgeBay);
			return;
		}
		if (Volume.Primitive != EABTSM73DAG5BV2Primitive::Box)
		{
			OutDomain.Add(EABTSM73BeamBMotif::TwoLayerCrib);
			OutDomain.Add(EABTSM73BeamBMotif::PostAndLintel);
			return;
		}

		const FVector Size = Bay.LocalBounds.GetSize();
		const double PrimarySpan = Bay.PreferredAxis
			== EABTSM73BeamAFrameAxis::Y ? Size.Y : Size.X;
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
		if (Settings.bAllowCantilever
			&& Volume.Role == EABTSM73DAG5BV2VolumeRole::Annex
			&& PrimarySpan >= Settings.BeamA.BlockCrossSectionCM * 5.0)
		{
			OutDomain.Add(EABTSM73BeamBMotif::CantileverBay);
		}
		if (Settings.bAllowBracedBay
			&& Size.Z >= Settings.BeamA.BlockCrossSectionCM * 5.0)
		{
			OutDomain.Add(EABTSM73BeamBMotif::BracedBay);
		}
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
			const FVector& Start,
			const FVector& End)
		{
			if (Result.PlannedMembers.Num()
				>= Settings.MaxPlannedMemberCount)
			{
				*Error = TEXT("BeamBPlannedMemberBudgetExceeded");
				return false;
			}
			if ((End - Start).SizeSquared() < 1.0)
			{
				return true;
			}
			FABTSM73BeamBPlannedMember& Member =
				Result.PlannedMembers.AddDefaulted_GetRef();
			Member.PlannedMemberId = Result.PlannedMembers.Num() - 1;
			Member.BayId = BayId;
			Member.Motif = Motif;
			Member.Axis = Axis;
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
		const double Thickness)
	{
		FBayCoordinates C;
		C.PrimaryAxis = Bay.PreferredAxis;
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
			C.P(U, V, Z0), C.P(U, V, Z1));
	}

	bool BuildMotif(
		FGeometryBuilder& B,
		const FABTSM73BeamABay& Bay,
		FABTSM73BeamBPlacement& Placement)
	{
		const double T = B.Settings.BeamA.BlockCrossSectionCM;
		const FBayCoordinates C = Coordinates(Bay, T);
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
			if (Placement.Motif == EABTSM73BeamBMotif::BracedBay)
			{
				if (!B.Add(Placement.BayId, Placement.Motif,
					EABTSM73BeamAFrameAxis::Diagonal,
					C.P(C.U0, VM, PostBottom), C.P(C.U1, VM, PostTop))
					|| !B.AddStep(Placement.BayId,
						EABTSM73BeamBGrammarRule::TriangulateBay, 1))
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
			Text += FString::Printf(TEXT("M%d:%d:%d:%d:%.2f,%.2f,%.2f:%.2f,%.2f,%.2f|"),
				M.PlannedMemberId, M.BayId, static_cast<int32>(M.Motif),
				static_cast<int32>(M.Axis), M.LocalStart.X, M.LocalStart.Y,
				M.LocalStart.Z, M.LocalEnd.X, M.LocalEnd.Y, M.LocalEnd.Z);
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
		if (!BeamA.Bays.IsValidIndex(Placement.BayId)
			|| !BuildMotif(Builder, BeamA.Bays[Placement.BayId], Placement))
		{
			return Reject(GeometryError.IsEmpty()
				? TEXT("BeamBMotifExpansionFailed") : GeometryError);
		}
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
		const FBox Expanded = BeamA.Bays[Member.BayId].LocalBounds
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

	OutResult.Summary.BayCount = BeamA.Bays.Num();
	OutResult.Summary.PlacementCount = OutResult.Placements.Num();
	OutResult.Summary.DistinctMotifCount = UsedMotifs.Num();
	OutResult.Summary.GrammarStepCount = OutResult.GrammarSteps.Num();
	OutResult.Summary.PlannedMemberCount = OutResult.PlannedMembers.Num();
	const FString PlacementText = CanonicalPlacements(OutResult.Placements);
	const FString GrammarText = CanonicalGrammar(OutResult);
	OutResult.Summary.MotifWFCHash = static_cast<int64>(
		FCrc::StrCrc32(*PlacementText));
	OutResult.Summary.GraphGrammarHash = static_cast<int64>(
		FCrc::StrCrc32(*GrammarText));
	OutResult.Summary.ResultHash = static_cast<int64>(FCrc::StrCrc32(
		*(PlacementText + GrammarText)));
	OutResult.Summary.bAccepted = true;
	return true;
}
