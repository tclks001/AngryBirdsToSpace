// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSM73BeamD1BrickCompiler.h"
#include "ABTSM73BeamD1BrickCompilerInternal.h"

#include "Algo/Count.h"
#include "ABTSRuntime.h"
#include "ABTSM73BeamAGenerator.h"
#include "ABTSM73BeamBGenerator.h"
#include "ABTSM73BeamC3V2CoupledExteriorFrameGenerator.h"
#include "ABTSM73BeamC3V3SkeletonFirstGenerator.h"
#include "ABTSM73BeamCGenerator.h"
#include "ABTSM73BeamD0ProfileCatalog.h"
#include "Building/ABTSM73DAG5BShapeGrammarV2.h"
#include "HAL/PlatformTime.h"
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

	bool IsFiniteBox(const FBox& Box)
	{
		return Box.IsValid
			&& FMath::IsFinite(Box.Min.X)
			&& FMath::IsFinite(Box.Min.Y)
			&& FMath::IsFinite(Box.Min.Z)
			&& FMath::IsFinite(Box.Max.X)
			&& FMath::IsFinite(Box.Max.Y)
			&& FMath::IsFinite(Box.Max.Z);
	}

	bool AppendRaisedMainReservations(
		const FABTSM73BeamD0ResolvedProfile& Profile,
		FABTSM73DAG5BV2GenerationResult& Silhouette,
		FString& OutError)
	{
		TArray<FABTSM73DAG5BV2RaisedMainReservation> Reservations;
		if (!FABTSM73BeamC3V3SkeletonFirstGenerator()
			.BuildRaisedMainReservations(
				Profile, Silhouette, Reservations, OutError))
		{
			OutError = FString::Printf(
				TEXT("BeamC3RaisedMainReservationPlan:%s"), *OutError);
			return false;
		}
		Silhouette.RaisedMainReservations = Reservations;
		if (Reservations.IsEmpty())
		{
			return true;
		}
		int32 NextVolumeId = 0;
		for (const FABTSM73DAG5BV2Volume& Volume : Silhouette.Volumes)
		{
			NextVolumeId = FMath::Max(NextVolumeId, Volume.VolumeId + 1);
		}
		FString ReservationCanonical;
		for (FABTSM73DAG5BV2RaisedMainReservation& Reservation :
			Silhouette.RaisedMainReservations)
		{
			const FABTSM73DAG5BV2Volume* Source =
				Silhouette.Volumes.FindByPredicate(
					[&Reservation](const FABTSM73DAG5BV2Volume& Volume)
					{
						return Volume.VolumeId == Reservation.SourceVolumeId;
					});
			if (Source == nullptr || !Reservation.CoreBounds.IsValid
				|| !Reservation.ClearanceBounds.IsValid
				|| Reservation.ApprovedTopCourse
					<= Reservation.OriginalTopCourse)
			{
				OutError = FString::Printf(
					TEXT("BeamC3RaisedMainReservationInvalid:Component=%d:Main=%d:Source=%d:Original=%d:Approved=%d"),
					Reservation.ComponentId,
					Reservation.PodiumMainCoreCellId,
					Reservation.SourceVolumeId,
					Reservation.OriginalTopCourse,
					Reservation.ApprovedTopCourse);
				return false;
			}
			FABTSM73DAG5BV2Volume& Volume =
				Silhouette.Volumes.AddDefaulted_GetRef();
			Volume.VolumeId = NextVolumeId++;
			Volume.GrammarDepth = Source->GrammarDepth;
			// Only the square occupied by the raised main is a semantic Body.
			// ClearanceBounds is a non-solid reservation for later side coupling;
			// publishing it as Body erases intentional hollow branches in Stage 0.
			Volume.LocalBounds = Reservation.CoreBounds;
			Volume.Role = EABTSM73DAG5BV2VolumeRole::Body;
			Volume.Primitive = EABTSM73DAG5BV2Primitive::Box;
			Volume.DerivationPath = Source->DerivationPath
				+ FString::Printf(TEXT("/RaisedMainReservation/C%dM%d"),
					Reservation.ComponentId,
					Reservation.PodiumMainCoreCellId);
			Reservation.SourceVolumeId = Volume.VolumeId;
			Silhouette.GrammarTrace.Add(FString::Printf(
				TEXT("RaisedMainReservation C%d M%d %d->%d Occupied=%s Clearance=%s Influenced=%s Foreign=%s"),
				Reservation.ComponentId, Reservation.PodiumMainCoreCellId,
				Reservation.OriginalTopCourse,
				Reservation.ApprovedTopCourse,
				*Reservation.CoreBounds.ToString(),
				*Reservation.ClearanceBounds.ToString(),
				*FString::JoinBy(Reservation.InfluencedTowerChildCoreCellIds,
					TEXT(","), [](const int32 Value)
					{ return FString::FromInt(Value); }),
				*FString::JoinBy(Reservation.ForeignTowerChildCoreCellIds,
					TEXT(","), [](const int32 Value)
					{ return FString::FromInt(Value); })));
			ReservationCanonical += FString::Printf(
				TEXT("|R=%d,%d,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,C=%.3f,%.3f,%.3f,%.3f,I=%s,F=%s"),
				Reservation.ComponentId,
				Reservation.PodiumMainCoreCellId,
				Reservation.OriginalTopCourse,
				Reservation.ApprovedTopCourse,
				Reservation.SourceVolumeId,
				Reservation.CoreBounds.Min.X,
				Reservation.CoreBounds.Min.Y,
				Reservation.CoreBounds.Min.Z,
				Reservation.CoreBounds.Max.X,
				Reservation.CoreBounds.Max.Y,
				Reservation.CoreBounds.Max.Z,
				Reservation.ClearanceBounds.Min.X,
				Reservation.ClearanceBounds.Min.Y,
				Reservation.ClearanceBounds.Max.X,
				Reservation.ClearanceBounds.Max.Y,
				*FString::JoinBy(Reservation.InfluencedTowerChildCoreCellIds,
					TEXT("."), [](const int32 Value)
					{ return FString::FromInt(Value); }),
				*FString::JoinBy(Reservation.ForeignTowerChildCoreCellIds,
					TEXT("."), [](const int32 Value)
					{ return FString::FromInt(Value); }));
		}
		Silhouette.Summary.VolumeCount = Silhouette.Volumes.Num();
		Silhouette.Summary.BoxCount += Reservations.Num();
		Silhouette.Summary.GrammarHash = static_cast<int64>(FCrc::StrCrc32(
			*FString::Printf(TEXT("%lld%s"),
				Silhouette.Summary.GrammarHash, *ReservationCanonical)));
		Silhouette.Summary.WFCHash = static_cast<int64>(FCrc::StrCrc32(
			*FString::Printf(TEXT("%lld%s"),
				Silhouette.Summary.WFCHash, *ReservationCanonical)));
		Silhouette.Summary.ResultHash = static_cast<int64>(FCrc::StrCrc32(
			*FString::Printf(TEXT("Grammar=%lld|WFC=%lld|Volumes=%d"),
				Silhouette.Summary.GrammarHash,
				Silhouette.Summary.WFCHash,
				Silhouette.Summary.VolumeCount)));
		return true;
	}

	FString SemanticRootPath(const FString& Path)
	{
		TArray<FString> Parts;
		Path.ParseIntoArray(Parts, TEXT("/"), true);
		return Parts.Num() >= 2
			? Parts[0] + TEXT("/") + Parts[1]
			: Path;
	}

	/**
	 * Stage-1 production seam: derive no more than the registered preferred cell
	 * count from locally height-authorized grounded Bays. MaximumCellCount remains
	 * a hard cap, never a quota. Each SourceVolume contributes at most one root;
	 * the anchor has the highest local authority and any extra root is selected by
	 * deterministic farthest-first coverage. This is one derivation pass, not
	 * generate/retry candidate search.
	 */
	bool DeriveCoupledExteriorFrameCells(
		const FABTSM73BeamC3V2CoupledExteriorFrameSettings& Settings,
		const FABTSM73BeamAPreviewSettings& BeamASettings,
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		const FABTSM73BeamBGenerationResult& BeamB,
		const int32 RequiredSupportedSpans,
		const bool bRequireSharedCoursePair,
		TArray<ABTSM73BeamC3V2::FCoupledExteriorFrameCellRequest>& OutCells,
		FString& OutError)
	{
		OutCells.Reset();
		const FABTSM73BeamAGenerationResult& Assembly = BeamB.ClosedAssembly;
		TArray<int32> OrderedBayIndices;
		OrderedBayIndices.Reserve(Assembly.Bays.Num());
		for (int32 BayIndex = 0; BayIndex < Assembly.Bays.Num(); ++BayIndex)
		{
			OrderedBayIndices.Add(BayIndex);
		}
		OrderedBayIndices.StableSort([&Assembly](const int32 AIndex, const int32 BIndex)
		{
			const FABTSM73BeamABay& A = Assembly.Bays[AIndex];
			const FABTSM73BeamABay& B = Assembly.Bays[BIndex];
			const bool bAValid = IsFiniteBox(A.LocalBounds);
			const bool bBValid = IsFiniteBox(B.LocalBounds);
			if (bAValid != bBValid)
			{
				return bAValid;
			}
			if (bAValid && !FMath::IsNearlyEqual(
				A.LocalBounds.Max.Z, B.LocalBounds.Max.Z))
			{
				return A.LocalBounds.Max.Z > B.LocalBounds.Max.Z;
			}
			if (A.SourceVolumeId != B.SourceVolumeId)
			{
				return A.SourceVolumeId < B.SourceVolumeId;
			}
			if (A.BayId != B.BayId)
			{
				return A.BayId < B.BayId;
			}
			return AIndex < BIndex;
		});

		constexpr double SectionCM = 36.0;
		constexpr double CoordinateQuantumCM = SectionCM * 0.5;
		int32 RequiredCellCourseCount = Settings.CourseCount;
		double RequiredHeightCM = RequiredCellCourseCount * SectionCM;
		const double MinimumCellSpanCM =
			(Settings.RailCount + 2) * SectionCM;
		const double MaximumCellSpanCM = FMath::Min(
			720.0, static_cast<double>(Settings.MaximumMemberLengthCM));
		const double GroundToleranceCM = FMath::Max(
			KINDA_SMALL_NUMBER,
			static_cast<double>(BeamASettings.JointMergeToleranceCM));
		auto FindSilhouetteVolume = [&Silhouette](const int32 VolumeId)
		{
			return Silhouette.Volumes.FindByPredicate(
				[VolumeId](const FABTSM73DAG5BV2Volume& Volume)
				{
					return Volume.VolumeId == VolumeId;
				});
		};

		// Prove local vertical authority without shrinking the fixed outer frame
		// to every upper setback. Bay adjacency is a DAG in increasing Z for this
		// purpose: a lower top face must touch an upper bottom face and their XY
		// interiors must overlap. HighestReach and its deterministic witness are
		// computed once; no geometry generation or retry occurs here.
		TArray<int32> VerticalOrder;
		VerticalOrder.Reserve(Assembly.Bays.Num());
		for (int32 BayIndex = 0; BayIndex < Assembly.Bays.Num(); ++BayIndex)
		{
			if (IsFiniteBox(Assembly.Bays[BayIndex].LocalBounds))
			{
				VerticalOrder.Add(BayIndex);
			}
		}
		VerticalOrder.StableSort([&Assembly](const int32 AIndex, const int32 BIndex)
		{
			const FABTSM73BeamABay& A = Assembly.Bays[AIndex];
			const FABTSM73BeamABay& B = Assembly.Bays[BIndex];
			if (!FMath::IsNearlyEqual(A.LocalBounds.Min.Z, B.LocalBounds.Min.Z))
			{
				return A.LocalBounds.Min.Z > B.LocalBounds.Min.Z;
			}
			if (A.SourceVolumeId != B.SourceVolumeId)
			{
				return A.SourceVolumeId < B.SourceVolumeId;
			}
			if (A.BayId != B.BayId)
			{
				return A.BayId < B.BayId;
			}
			return AIndex < BIndex;
		});
		TArray<double> HighestReachZ;
		HighestReachZ.Init(-TNumericLimits<double>::Max(), Assembly.Bays.Num());
		TArray<int32> NextAuthorityBay;
		NextAuthorityBay.Init(INDEX_NONE, Assembly.Bays.Num());
		auto StableBayBefore = [&Assembly](const int32 AIndex, const int32 BIndex)
		{
			if (BIndex == INDEX_NONE)
			{
				return true;
			}
			const FABTSM73BeamABay& A = Assembly.Bays[AIndex];
			const FABTSM73BeamABay& B = Assembly.Bays[BIndex];
			if (A.SourceVolumeId != B.SourceVolumeId)
			{
				return A.SourceVolumeId < B.SourceVolumeId;
			}
			if (A.BayId != B.BayId)
			{
				return A.BayId < B.BayId;
			}
			return AIndex < BIndex;
		};
		for (const int32 LowerIndex : VerticalOrder)
		{
			const FABTSM73BeamABay& Lower = Assembly.Bays[LowerIndex];
			double BestReachZ = Lower.LocalBounds.Max.Z;
			int32 BestUpperIndex = INDEX_NONE;
			for (const int32 UpperIndex : VerticalOrder)
			{
				const FABTSM73BeamABay& Upper = Assembly.Bays[UpperIndex];
				if (Upper.LocalBounds.Min.Z
						<= Lower.LocalBounds.Min.Z + GroundToleranceCM
					|| !Lower.AdjacentBayIds.Contains(Upper.BayId)
					|| FMath::Abs(Lower.LocalBounds.Max.Z
							- Upper.LocalBounds.Min.Z) > GroundToleranceCM)
				{
					continue;
				}
				const double OverlapX = FMath::Min(
					Lower.LocalBounds.Max.X, Upper.LocalBounds.Max.X)
					- FMath::Max(Lower.LocalBounds.Min.X, Upper.LocalBounds.Min.X);
				const double OverlapY = FMath::Min(
					Lower.LocalBounds.Max.Y, Upper.LocalBounds.Max.Y)
					- FMath::Max(Lower.LocalBounds.Min.Y, Upper.LocalBounds.Min.Y);
				if (OverlapX <= GroundToleranceCM || OverlapY <= GroundToleranceCM)
				{
					continue;
				}
				const double CandidateReachZ = HighestReachZ[UpperIndex];
				if (CandidateReachZ > BestReachZ + GroundToleranceCM
					|| (FMath::IsNearlyEqual(
						CandidateReachZ, BestReachZ, GroundToleranceCM)
						&& StableBayBefore(UpperIndex, BestUpperIndex)))
				{
					BestReachZ = CandidateReachZ;
					BestUpperIndex = UpperIndex;
				}
			}
			HighestReachZ[LowerIndex] = BestReachZ;
			NextAuthorityBay[LowerIndex] = BestUpperIndex;
		}

		// Beam-A partitions one semantic SourceVolume into adjacent Bays along a
		// single horizontal axis. SeamRelease.E6 may therefore have a valid
		// 252 x 252 cm grounded source even when no individual partition is wide
		// enough in both axes (Arcology/West is the production example). Rejoin
		// only source-local, coplanar partitions whose adjacency graph is connected
		// and whose non-overlapping XY areas exactly cover their union rectangle.
		// This is an E6-only authority reconstruction; it never crosses a grammar
		// split or invents support across a gap.
		struct FGroundedCompositeFootprint
		{
			int32 SourceVolumeId = INDEX_NONE;
			FBox Bounds = FBox(EForceInit::ForceInit);
			TArray<int32> GroundBayIndices;
			int32 WitnessGroundBayIndex = INDEX_NONE;
			double HighestReachZCM = -TNumericLimits<double>::Max();
		};
		TArray<FGroundedCompositeFootprint> SharedCourseCompositeFootprints;
		if (bRequireSharedCoursePair)
		{
			TMap<int32, TArray<int32>> GroundBaysBySource;
			for (int32 BayIndex = 0; BayIndex < Assembly.Bays.Num(); ++BayIndex)
			{
				const FABTSM73BeamABay& Bay = Assembly.Bays[BayIndex];
				if (IsFiniteBox(Bay.LocalBounds)
					&& FMath::Abs(Bay.LocalBounds.Min.Z) <= GroundToleranceCM)
				{
					GroundBaysBySource.FindOrAdd(Bay.SourceVolumeId).Add(BayIndex);
				}
			}

			TArray<int32> OrderedSourceVolumeIds;
			GroundBaysBySource.GetKeys(OrderedSourceVolumeIds);
			OrderedSourceVolumeIds.Sort();
			for (const int32 SourceVolumeId : OrderedSourceVolumeIds)
			{
				TArray<int32>& SourceBayIndices =
					GroundBaysBySource.FindChecked(SourceVolumeId);
				if (SourceBayIndices.Num() < 2)
				{
					continue;
				}
				const FABTSM73DAG5BV2Volume* SourceVolume =
					FindSilhouetteVolume(SourceVolumeId);
				if (SourceVolume == nullptr
					|| SourceVolume->Role
						== EABTSM73DAG5BV2VolumeRole::SupportedSpan)
				{
					continue;
				}
				const EABTSM73BeamAFrameAxis PreferredAxis =
					Assembly.Bays[SourceBayIndices[0]].PreferredAxis;
				if (PreferredAxis != EABTSM73BeamAFrameAxis::X
					&& PreferredAxis != EABTSM73BeamAFrameAxis::Y)
				{
					continue;
				}
				const int32 PartitionAxis =
					PreferredAxis == EABTSM73BeamAFrameAxis::X ? 0 : 1;
				const int32 FixedAxis = PartitionAxis == 0 ? 1 : 0;
				SourceBayIndices.StableSort(
					[&Assembly, PartitionAxis](const int32 AIndex,
						const int32 BIndex)
					{
						const FABTSM73BeamABay& A = Assembly.Bays[AIndex];
						const FABTSM73BeamABay& B = Assembly.Bays[BIndex];
						if (!FMath::IsNearlyEqual(A.LocalBounds.Min[PartitionAxis],
							B.LocalBounds.Min[PartitionAxis]))
						{
							return A.LocalBounds.Min[PartitionAxis]
								< B.LocalBounds.Min[PartitionAxis];
						}
						return A.BayId != B.BayId
							? A.BayId < B.BayId : AIndex < BIndex;
					});
				FBox Union(EForceInit::ForceInit);
				double AreaSumCM2 = 0.0;
				double HighestReachZCM = -TNumericLimits<double>::Max();
				int32 WitnessGroundBayIndex = INDEX_NONE;
				bool bCoplanar = true;
				const FABTSM73BeamABay& FirstBay =
					Assembly.Bays[SourceBayIndices[0]];
				for (const int32 BayIndex : SourceBayIndices)
				{
					const FABTSM73BeamABay& Bay = Assembly.Bays[BayIndex];
					bCoplanar = bCoplanar
						&& Bay.PreferredAxis == PreferredAxis
						&& FMath::Abs(Bay.LocalBounds.Min.Z
							- FirstBay.LocalBounds.Min.Z) <= GroundToleranceCM
						&& FMath::Abs(Bay.LocalBounds.Max.Z
							- FirstBay.LocalBounds.Max.Z) <= GroundToleranceCM
						&& FMath::Abs(Bay.LocalBounds.Min[FixedAxis]
							- FirstBay.LocalBounds.Min[FixedAxis]) <= GroundToleranceCM
						&& FMath::Abs(Bay.LocalBounds.Max[FixedAxis]
							- FirstBay.LocalBounds.Max[FixedAxis]) <= GroundToleranceCM;
					Union += Bay.LocalBounds;
					const FVector Size = Bay.LocalBounds.GetSize();
					AreaSumCM2 += Size.X * Size.Y;
					if (HighestReachZ[BayIndex]
							> HighestReachZCM + GroundToleranceCM
						|| (FMath::IsNearlyEqual(HighestReachZ[BayIndex],
							HighestReachZCM, GroundToleranceCM)
							&& (WitnessGroundBayIndex == INDEX_NONE
								|| Bay.BayId < Assembly.Bays[
									WitnessGroundBayIndex].BayId)))
					{
						HighestReachZCM = HighestReachZ[BayIndex];
						WitnessGroundBayIndex = BayIndex;
					}
				}
				if (!bCoplanar || !IsFiniteBox(Union))
				{
					continue;
				}

				bool bPositiveOverlap = false;
				for (int32 A = 0; A < SourceBayIndices.Num() && !bPositiveOverlap; ++A)
				{
					const FBox& ABounds =
						Assembly.Bays[SourceBayIndices[A]].LocalBounds;
					for (int32 B = A + 1; B < SourceBayIndices.Num(); ++B)
					{
						const FBox& BBounds =
							Assembly.Bays[SourceBayIndices[B]].LocalBounds;
						const double OverlapX = FMath::Min(ABounds.Max.X, BBounds.Max.X)
							- FMath::Max(ABounds.Min.X, BBounds.Min.X);
						const double OverlapY = FMath::Min(ABounds.Max.Y, BBounds.Max.Y)
							- FMath::Max(ABounds.Min.Y, BBounds.Min.Y);
						if (OverlapX > GroundToleranceCM
							&& OverlapY > GroundToleranceCM)
						{
							bPositiveOverlap = true;
							break;
						}
					}
				}
				const FVector UnionSize = Union.GetSize();
				const double UnionAreaCM2 = UnionSize.X * UnionSize.Y;
				const double AreaToleranceCM2 = FMath::Max(
					1.0, UnionAreaCM2 * 1.0e-5);
				bool bPartitionChainValid = true;
				for (int32 Index = 1; Index < SourceBayIndices.Num(); ++Index)
				{
					const FABTSM73BeamABay& Previous =
						Assembly.Bays[SourceBayIndices[Index - 1]];
					const FABTSM73BeamABay& Current =
						Assembly.Bays[SourceBayIndices[Index]];
					if (FMath::Abs(Previous.LocalBounds.Max[PartitionAxis]
							- Current.LocalBounds.Min[PartitionAxis])
							> GroundToleranceCM
						|| !Previous.AdjacentBayIds.Contains(Current.BayId)
						|| !Current.AdjacentBayIds.Contains(Previous.BayId))
					{
						bPartitionChainValid = false;
						break;
					}
				}
				const FBox& SourceBounds = SourceVolume->LocalBounds;
				const bool bReconstructsSourceBounds =
					FMath::Abs(Union.Min.X - SourceBounds.Min.X) <= GroundToleranceCM
					&& FMath::Abs(Union.Min.Y - SourceBounds.Min.Y) <= GroundToleranceCM
					&& FMath::Abs(Union.Min.Z - SourceBounds.Min.Z) <= GroundToleranceCM
					&& FMath::Abs(Union.Max.X - SourceBounds.Max.X) <= GroundToleranceCM
					&& FMath::Abs(Union.Max.Y - SourceBounds.Max.Y) <= GroundToleranceCM
					&& FMath::Abs(Union.Max.Z - SourceBounds.Max.Z) <= GroundToleranceCM;
				if (bPositiveOverlap
					|| FMath::Abs(AreaSumCM2 - UnionAreaCM2) > AreaToleranceCM2
					|| !bPartitionChainValid
					|| !bReconstructsSourceBounds
					|| UnionSize.X + KINDA_SMALL_NUMBER < MinimumCellSpanCM
					|| UnionSize.Y + KINDA_SMALL_NUMBER < MinimumCellSpanCM)
				{
					continue;
				}

				TSet<int32> SourceBayIdSet;
				for (const int32 BayIndex : SourceBayIndices)
				{
					SourceBayIdSet.Add(Assembly.Bays[BayIndex].BayId);
				}
				TSet<int32> VisitedBayIds;
				TArray<int32> PendingBayIds;
				PendingBayIds.Add(FirstBay.BayId);
				while (!PendingBayIds.IsEmpty())
				{
					const int32 BayId = PendingBayIds.Pop(EAllowShrinking::No);
					if (VisitedBayIds.Contains(BayId))
					{
						continue;
					}
					VisitedBayIds.Add(BayId);
					const FABTSM73BeamABay* Bay = Assembly.Bays.FindByPredicate(
						[BayId](const FABTSM73BeamABay& Candidate)
						{
							return Candidate.BayId == BayId;
						});
					if (Bay == nullptr)
					{
						continue;
					}
					for (const int32 AdjacentBayId : Bay->AdjacentBayIds)
					{
						if (SourceBayIdSet.Contains(AdjacentBayId)
							&& !VisitedBayIds.Contains(AdjacentBayId))
						{
							PendingBayIds.Add(AdjacentBayId);
						}
					}
				}
				if (VisitedBayIds.Num() != SourceBayIndices.Num()
					|| WitnessGroundBayIndex == INDEX_NONE
					|| !FMath::IsFinite(HighestReachZCM))
				{
					continue;
				}

				FGroundedCompositeFootprint& Footprint =
					SharedCourseCompositeFootprints.AddDefaulted_GetRef();
				Footprint.SourceVolumeId = SourceVolumeId;
				Footprint.Bounds = Union;
				Footprint.GroundBayIndices = SourceBayIndices;
				Footprint.WitnessGroundBayIndex = WitnessGroundBayIndex;
				Footprint.HighestReachZCM = HighestReachZCM;
				UE_LOG(LogABTSRuntime, Display,
					TEXT("[ABTS][M7.3-Beam-C3V2][SharedCourseCompositeGround]")
					TEXT(" Source=%d Bays=%d Witness=%d Reach=%.2f Bounds=%s..%s"),
					SourceVolumeId, SourceBayIndices.Num(),
					Assembly.Bays[WitnessGroundBayIndex].BayId, HighestReachZCM,
					*Union.Min.ToCompactString(), *Union.Max.ToCompactString());
			}
		}

		// SeamRelease.E6 couples the two actual endpoint modules. A global tier
		// height is not a valid per-cell requirement when one side of the bridge
		// terminates at a deliberately lower module. Derive one common, even local
		// course count from the two grounded semantic roots exactly once. This is
		// an E6-only geometry result, not a fallback search or a relaxed threshold.
		if (bRequireSharedCoursePair)
		{
			const FABTSM73BeamBBridgeEndpoint* HeightNegativeEndpoint = nullptr;
			const FABTSM73BeamBBridgeEndpoint* HeightPositiveEndpoint = nullptr;
			for (const FABTSM73BeamBBridgeEndpoint& Endpoint : BeamB.BridgeEndpoints)
			{
				const FABTSM73BeamBBridgeEndpoint*& Slot = Endpoint.bNegativeEndpoint
					? HeightNegativeEndpoint : HeightPositiveEndpoint;
				if (Slot != nullptr)
				{
					OutError = TEXT("BeamC3V2SharedCourseEndpointMultiplicityInvalid");
					return false;
				}
				Slot = &Endpoint;
			}
			if (BeamB.BridgeEndpoints.Num() != 2
				|| HeightNegativeEndpoint == nullptr
				|| HeightPositiveEndpoint == nullptr
				|| HeightNegativeEndpoint->SpanVolumeId == INDEX_NONE
				|| HeightNegativeEndpoint->SpanVolumeId
					!= HeightPositiveEndpoint->SpanVolumeId)
			{
				OutError = TEXT("BeamC3V2SharedCourseEndpointMultiplicityInvalid");
				return false;
			}
			auto ResolveEndpointRoot = [&](
				const FABTSM73BeamBBridgeEndpoint& Endpoint, FString& OutRoot)
			{
				const FABTSM73DAG5BV2Volume* Declared =
					FindSilhouetteVolume(Endpoint.DeclaredSupportVolumeId);
				const FABTSM73DAG5BV2Volume* Actual =
					FindSilhouetteVolume(Endpoint.SupportVolumeId);
				if (Declared == nullptr || Actual == nullptr)
				{
					return false;
				}
				OutRoot = SemanticRootPath(Declared->DerivationPath);
				return !OutRoot.IsEmpty()
					&& SemanticRootPath(Actual->DerivationPath) == OutRoot;
			};
			FString NegativeRoot;
			FString PositiveRoot;
			if (!ResolveEndpointRoot(*HeightNegativeEndpoint, NegativeRoot)
				|| !ResolveEndpointRoot(*HeightPositiveEndpoint, PositiveRoot)
				|| NegativeRoot == PositiveRoot)
			{
				OutError = TEXT("BeamC3V2SharedCourseEndpointModuleInvalid");
				return false;
			}

			double NegativeReachZ = -TNumericLimits<double>::Max();
			double PositiveReachZ = -TNumericLimits<double>::Max();
			FString GroundRootDiagnostics;
			for (int32 BayIndex = 0; BayIndex < Assembly.Bays.Num(); ++BayIndex)
			{
				const FABTSM73BeamABay& Bay = Assembly.Bays[BayIndex];
				if (!IsFiniteBox(Bay.LocalBounds)
					|| FMath::Abs(Bay.LocalBounds.Min.Z) > GroundToleranceCM
					|| Bay.LocalBounds.GetSize().X + KINDA_SMALL_NUMBER
						< MinimumCellSpanCM
					|| Bay.LocalBounds.GetSize().Y + KINDA_SMALL_NUMBER
						< MinimumCellSpanCM
					|| !HighestReachZ.IsValidIndex(BayIndex))
				{
					continue;
				}
				const FABTSM73DAG5BV2Volume* RootVolume =
					FindSilhouetteVolume(Bay.SourceVolumeId);
				if (RootVolume == nullptr)
				{
					continue;
				}
				const FString RootPath = SemanticRootPath(RootVolume->DerivationPath);
				GroundRootDiagnostics += FString::Printf(
					TEXT("[%d/%d:%s:Reach=%.2f:Size=%.2f,%.2f]"),
					Bay.BayId, Bay.SourceVolumeId, *RootPath,
					HighestReachZ[BayIndex], Bay.LocalBounds.GetSize().X,
					Bay.LocalBounds.GetSize().Y);
				if (RootPath == NegativeRoot)
				{
					NegativeReachZ = FMath::Max(
						NegativeReachZ, HighestReachZ[BayIndex]);
				}
				if (RootPath == PositiveRoot)
				{
					PositiveReachZ = FMath::Max(
						PositiveReachZ, HighestReachZ[BayIndex]);
				}
			}
			for (const FGroundedCompositeFootprint& Footprint :
				SharedCourseCompositeFootprints)
			{
				const FABTSM73DAG5BV2Volume* RootVolume =
					FindSilhouetteVolume(Footprint.SourceVolumeId);
				if (RootVolume == nullptr)
				{
					continue;
				}
				const FString RootPath = SemanticRootPath(RootVolume->DerivationPath);
				GroundRootDiagnostics += FString::Printf(
					TEXT("[Composite/%d:%s:Reach=%.2f:Size=%.2f,%.2f:Bays=%d]"),
					Footprint.SourceVolumeId, *RootPath,
					Footprint.HighestReachZCM,
					Footprint.Bounds.GetSize().X,
					Footprint.Bounds.GetSize().Y,
					Footprint.GroundBayIndices.Num());
				if (RootPath == NegativeRoot)
				{
					NegativeReachZ = FMath::Max(
						NegativeReachZ, Footprint.HighestReachZCM);
				}
				if (RootPath == PositiveRoot)
				{
					PositiveReachZ = FMath::Max(
						PositiveReachZ, Footprint.HighestReachZCM);
				}
			}
			auto ProtectedRoofBottomForRoot = [&Silhouette](const FString& RootPath)
			{
				double RoofBottomZ = TNumericLimits<double>::Max();
				for (const FABTSM73DAG5BV2Volume& Volume : Silhouette.Volumes)
				{
					if (Volume.Primitive != EABTSM73DAG5BV2Primitive::Box
						&& SemanticRootPath(Volume.DerivationPath) == RootPath
						&& IsFiniteBox(Volume.LocalBounds))
					{
						RoofBottomZ = FMath::Min(
							RoofBottomZ,
							static_cast<double>(Volume.LocalBounds.Min.Z));
					}
				}
				return RoofBottomZ;
			};
			const double NegativeRoofBottomZ =
				ProtectedRoofBottomForRoot(NegativeRoot);
			const double PositiveRoofBottomZ =
				ProtectedRoofBottomForRoot(PositiveRoot);
			if (NegativeRoofBottomZ != TNumericLimits<double>::Max())
			{
				NegativeReachZ = FMath::Min(NegativeReachZ, NegativeRoofBottomZ);
			}
			if (PositiveRoofBottomZ != TNumericLimits<double>::Max())
			{
				PositiveReachZ = FMath::Min(PositiveReachZ, PositiveRoofBottomZ);
			}
			if (NegativeReachZ == -TNumericLimits<double>::Max()
				|| PositiveReachZ == -TNumericLimits<double>::Max())
			{
				OutError = FString::Printf(
					TEXT("BeamC3V2SharedCourseEndpointRootHeightUnavailable:")
					TEXT("NegRoot=%s:PosRoot=%s:Neg=%.2f:Pos=%.2f:Ground=%s"),
					*NegativeRoot, *PositiveRoot, NegativeReachZ, PositiveReachZ,
					*GroundRootDiagnostics);
				return false;
			}
			const double CommonReachZ = FMath::Min3(
				NegativeReachZ, PositiveReachZ,
				static_cast<double>(Settings.CourseCount) * SectionCM);
			int32 DerivedCourseCount = FMath::FloorToInt(
				(CommonReachZ + GroundToleranceCM) / SectionCM);
			if ((DerivedCourseCount & 1) != 0)
			{
				--DerivedCourseCount;
			}
			if (!FMath::IsFinite(CommonReachZ) || DerivedCourseCount < 8)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V2SharedCourseEndpointRootHeightUnavailable:Neg=%.2f:Pos=%.2f"),
					NegativeReachZ, PositiveReachZ);
				return false;
			}
			RequiredCellCourseCount = DerivedCourseCount;
			RequiredHeightCM = RequiredCellCourseCount * SectionCM;
			UE_LOG(LogABTSRuntime, Display,
				TEXT("[ABTS][M7.3-Beam-C3V2][SharedCourseHeightDerived]")
				TEXT(" GlobalCourses=%d LocalCourses=%d Height=%.2f")
				TEXT(" NegReach=%.2f PosReach=%.2f NegRoof=%.2f PosRoof=%.2f")
				TEXT(" NegRoot=%s PosRoot=%s"),
				Settings.CourseCount, RequiredCellCourseCount, RequiredHeightCM,
				NegativeReachZ, PositiveReachZ,
				NegativeRoofBottomZ == TNumericLimits<double>::Max()
					? -1.0 : NegativeRoofBottomZ,
				PositiveRoofBottomZ == TNumericLimits<double>::Max()
					? -1.0 : PositiveRoofBottomZ,
				*NegativeRoot, *PositiveRoot);
		}
		OrderedBayIndices.StableSort(
			[&Assembly, &HighestReachZ](const int32 AIndex, const int32 BIndex)
			{
				const FABTSM73BeamABay& A = Assembly.Bays[AIndex];
				const FABTSM73BeamABay& B = Assembly.Bays[BIndex];
				const bool bAValid = IsFiniteBox(A.LocalBounds);
				const bool bBValid = IsFiniteBox(B.LocalBounds);
				if (bAValid != bBValid)
				{
					return bAValid;
				}
				const double AReach = HighestReachZ.IsValidIndex(AIndex)
					? HighestReachZ[AIndex] : -TNumericLimits<double>::Max();
				const double BReach = HighestReachZ.IsValidIndex(BIndex)
					? HighestReachZ[BIndex] : -TNumericLimits<double>::Max();
				if (!FMath::IsNearlyEqual(AReach, BReach))
				{
					return AReach > BReach;
				}
				const FVector ASize = bAValid
					? A.LocalBounds.GetSize() : FVector::ZeroVector;
				const FVector BSize = bBValid
					? B.LocalBounds.GetSize() : FVector::ZeroVector;
				const double AArea = ASize.X * ASize.Y;
				const double BArea = BSize.X * BSize.Y;
				if (!FMath::IsNearlyEqual(AArea, BArea))
				{
					return AArea > BArea;
				}
				if (A.SourceVolumeId != B.SourceVolumeId)
				{
					return A.SourceVolumeId < B.SourceVolumeId;
				}
				if (A.BayId != B.BayId)
				{
					return A.BayId < B.BayId;
				}
				return AIndex < BIndex;
			});
		struct FDerivedCellCandidate
		{
			ABTSM73BeamC3V2::FCoupledExteriorFrameCellRequest Request;
			FBox GroundBayBounds = FBox(EForceInit::ForceInit);
			double HighestReachZCM = 0.0;
			double AvailableAreaCM2 = 0.0;
			TArray<int32> AuthorityBayIds;
			TArray<int32> AuthoritySourceVolumeIds;
		};
		TArray<FDerivedCellCandidate> EligibleCells;
		double MaximumAssemblyZCM = -TNumericLimits<double>::Max();
		for (const FABTSM73BeamABay& Bay : Assembly.Bays)
		{
			if (IsFiniteBox(Bay.LocalBounds))
			{
				MaximumAssemblyZCM = FMath::Max(
					MaximumAssemblyZCM,
					static_cast<double>(Bay.LocalBounds.Max.Z));
			}
		}
		int32 GroundedBayCount = 0;
		int32 HeightFitBayCount = 0;
		int32 XYFitBayCount = 0;
		double ClosestMinimumZCM = TNumericLimits<double>::Max();
		FVector MaximumGroundedBaySize = FVector::ZeroVector;
		FString BayDiagnostics;

		for (const int32 BayIndex : OrderedBayIndices)
		{
			const FABTSM73BeamABay& Bay = Assembly.Bays[BayIndex];
			if (!IsFiniteBox(Bay.LocalBounds))
			{
				continue;
			}

			const FVector BaySize = Bay.LocalBounds.GetSize();
			ClosestMinimumZCM = FMath::Min(
				ClosestMinimumZCM, FMath::Abs(Bay.LocalBounds.Min.Z));
			BayDiagnostics += FString::Printf(
				TEXT("[%d/%d:MinZ=%.0f:Size=%.0f,%.0f,%.0f]"),
				Bay.BayId, Bay.SourceVolumeId, Bay.LocalBounds.Min.Z,
				BaySize.X, BaySize.Y, BaySize.Z);
			if (FMath::Abs(Bay.LocalBounds.Min.Z) > GroundToleranceCM)
			{
				continue;
			}
			++GroundedBayCount;
			MaximumGroundedBaySize.X = FMath::Max(
				MaximumGroundedBaySize.X, BaySize.X);
			MaximumGroundedBaySize.Y = FMath::Max(
				MaximumGroundedBaySize.Y, BaySize.Y);
			MaximumGroundedBaySize.Z = FMath::Max(
				MaximumGroundedBaySize.Z, BaySize.Z);
			if (!HighestReachZ.IsValidIndex(BayIndex)
				|| HighestReachZ[BayIndex] + GroundToleranceCM < RequiredHeightCM)
			{
				continue;
			}
			++HeightFitBayCount;
			if (BaySize.X + KINDA_SMALL_NUMBER < MinimumCellSpanCM
				|| BaySize.Y + KINDA_SMALL_NUMBER < MinimumCellSpanCM)
			{
				continue;
			}
			++XYFitBayCount;

			// Keep the request centered in its semantic Bay while snapping the
			// coordinate system and both spans to the 18 cm geometry quantum.
			const FVector BayCenter = Bay.LocalBounds.GetCenter();
			const double CenterX = FMath::RoundToDouble(
				BayCenter.X / CoordinateQuantumCM) * CoordinateQuantumCM;
			const double CenterY = FMath::RoundToDouble(
				BayCenter.Y / CoordinateQuantumCM) * CoordinateQuantumCM;
			const double AvailableX = 2.0 * FMath::Min(
				CenterX - Bay.LocalBounds.Min.X,
				Bay.LocalBounds.Max.X - CenterX);
			const double AvailableY = 2.0 * FMath::Min(
				CenterY - Bay.LocalBounds.Min.Y,
				Bay.LocalBounds.Max.Y - CenterY);
			const double CellSpanX = FMath::FloorToDouble(
				FMath::Min(AvailableX, MaximumCellSpanCM) / CoordinateQuantumCM)
				* CoordinateQuantumCM;
			const double CellSpanY = FMath::FloorToDouble(
				FMath::Min(AvailableY, MaximumCellSpanCM) / CoordinateQuantumCM)
				* CoordinateQuantumCM;
			if (CellSpanX + KINDA_SMALL_NUMBER < MinimumCellSpanCM
				|| CellSpanY + KINDA_SMALL_NUMBER < MinimumCellSpanCM)
			{
				continue;
			}
			if (EligibleCells.ContainsByPredicate([&Bay](
				const FDerivedCellCandidate& Existing)
				{
					return Existing.Request.SourceVolumeId == Bay.SourceVolumeId;
				}))
			{
				continue;
			}

			ABTSM73BeamC3V2::FCoupledExteriorFrameCellRequest Candidate;
			Candidate.LocalBounds = FBox(
				FVector(CenterX - CellSpanX * 0.5,
					CenterY - CellSpanY * 0.5, 0.0),
				FVector(CenterX + CellSpanX * 0.5,
					CenterY + CellSpanY * 0.5, RequiredHeightCM));
			Candidate.BayId = Bay.BayId;
			Candidate.SourceVolumeId = Bay.SourceVolumeId;
			FString AuthorityCanonical(TEXT("BeamC3V2RootAuthority:v1"));
			TArray<int32> AuthorityBayIds;
			TArray<int32> AuthoritySourceVolumeIds;
			for (int32 WitnessIndex = BayIndex;
				Assembly.Bays.IsValidIndex(WitnessIndex);
				WitnessIndex = NextAuthorityBay[WitnessIndex])
			{
				const FABTSM73BeamABay& Witness = Assembly.Bays[WitnessIndex];
				AuthorityBayIds.Add(Witness.BayId);
				AuthoritySourceVolumeIds.Add(Witness.SourceVolumeId);
				AuthorityCanonical += FString::Printf(
					TEXT("|%d,%d:%.3f,%.3f,%.3f:%.3f,%.3f,%.3f"),
					Witness.BayId, Witness.SourceVolumeId,
					Witness.LocalBounds.Min.X, Witness.LocalBounds.Min.Y,
					Witness.LocalBounds.Min.Z, Witness.LocalBounds.Max.X,
					Witness.LocalBounds.Max.Y, Witness.LocalBounds.Max.Z);
				if (NextAuthorityBay[WitnessIndex] == INDEX_NONE)
				{
					break;
				}
			}
			Candidate.RootAuthorityCrc32 = FCrc::StrCrc32(*AuthorityCanonical);
			FDerivedCellCandidate& Eligible = EligibleCells.AddDefaulted_GetRef();
			Eligible.Request = MoveTemp(Candidate);
			Eligible.GroundBayBounds = Bay.LocalBounds;
			Eligible.HighestReachZCM = HighestReachZ[BayIndex];
			Eligible.AvailableAreaCM2 = CellSpanX * CellSpanY;
			Eligible.AuthorityBayIds = MoveTemp(AuthorityBayIds);
			Eligible.AuthoritySourceVolumeIds = MoveTemp(AuthoritySourceVolumeIds);
		}

		// Ordinary cells remain one-Bay roots. Only the atomic E6 endpoint route
		// may consume the source-local composite footprints certified above.
		for (const FGroundedCompositeFootprint& Footprint :
			SharedCourseCompositeFootprints)
		{
			if (Footprint.HighestReachZCM + GroundToleranceCM
					< RequiredHeightCM
				|| Footprint.GroundBayIndices.IsEmpty())
			{
				continue;
			}
			const FVector GroundCenter = Footprint.Bounds.GetCenter();
			const double CenterX = FMath::RoundToDouble(
				GroundCenter.X / CoordinateQuantumCM) * CoordinateQuantumCM;
			const double CenterY = FMath::RoundToDouble(
				GroundCenter.Y / CoordinateQuantumCM) * CoordinateQuantumCM;
			const double AvailableX = 2.0 * FMath::Min(
				CenterX - Footprint.Bounds.Min.X,
				Footprint.Bounds.Max.X - CenterX);
			const double AvailableY = 2.0 * FMath::Min(
				CenterY - Footprint.Bounds.Min.Y,
				Footprint.Bounds.Max.Y - CenterY);
			const double CellSpanX = FMath::FloorToDouble(
				FMath::Min(AvailableX, MaximumCellSpanCM) / CoordinateQuantumCM)
				* CoordinateQuantumCM;
			const double CellSpanY = FMath::FloorToDouble(
				FMath::Min(AvailableY, MaximumCellSpanCM) / CoordinateQuantumCM)
				* CoordinateQuantumCM;
			if (CellSpanX + KINDA_SMALL_NUMBER < MinimumCellSpanCM
				|| CellSpanY + KINDA_SMALL_NUMBER < MinimumCellSpanCM)
			{
				continue;
			}

			const int32 RepresentativeBayIndex =
				Footprint.WitnessGroundBayIndex;
			const FABTSM73BeamABay& RepresentativeBay =
				Assembly.Bays[RepresentativeBayIndex];
			ABTSM73BeamC3V2::FCoupledExteriorFrameCellRequest Candidate;
			Candidate.LocalBounds = FBox(
				FVector(CenterX - CellSpanX * 0.5,
					CenterY - CellSpanY * 0.5, 0.0),
				FVector(CenterX + CellSpanX * 0.5,
					CenterY + CellSpanY * 0.5, RequiredHeightCM));
			Candidate.BayId = RepresentativeBay.BayId;
			Candidate.SourceVolumeId = Footprint.SourceVolumeId;
			const FABTSM73DAG5BV2Volume* SourceVolume =
				FindSilhouetteVolume(Footprint.SourceVolumeId);
			if (SourceVolume == nullptr)
			{
				continue;
			}
			FString AuthorityCanonical = FString::Printf(
				TEXT("BeamC3V2CompositeRootAuthority:v2|Root=%s|Source=%d")
				TEXT("|Axis=%d|Bounds=%.3f,%.3f,%.3f:%.3f,%.3f,%.3f"),
				*SemanticRootPath(SourceVolume->DerivationPath),
				Footprint.SourceVolumeId,
				static_cast<int32>(RepresentativeBay.PreferredAxis),
				Footprint.Bounds.Min.X, Footprint.Bounds.Min.Y,
				Footprint.Bounds.Min.Z, Footprint.Bounds.Max.X,
				Footprint.Bounds.Max.Y, Footprint.Bounds.Max.Z);
			TArray<int32> AuthorityBayIds;
			TArray<int32> AuthoritySourceVolumeIds;
			for (int32 ContributorIndex = 0;
				ContributorIndex < Footprint.GroundBayIndices.Num();
				++ContributorIndex)
			{
				const int32 GroundBayIndex =
					Footprint.GroundBayIndices[ContributorIndex];
				const FABTSM73BeamABay& GroundBay = Assembly.Bays[GroundBayIndex];
				AuthorityCanonical += FString::Printf(
					TEXT("|G%d,%d:%.3f,%.3f,%.3f:%.3f,%.3f,%.3f"),
					GroundBay.BayId, GroundBay.SourceVolumeId,
					GroundBay.LocalBounds.Min.X, GroundBay.LocalBounds.Min.Y,
					GroundBay.LocalBounds.Min.Z, GroundBay.LocalBounds.Max.X,
					GroundBay.LocalBounds.Max.Y, GroundBay.LocalBounds.Max.Z);
				if (ContributorIndex > 0)
				{
					AuthorityCanonical += FString::Printf(TEXT("|E%d>%d"),
						Assembly.Bays[Footprint.GroundBayIndices[
							ContributorIndex - 1]].BayId,
						GroundBay.BayId);
				}
			}
			for (int32 WitnessIndex = RepresentativeBayIndex;
				Assembly.Bays.IsValidIndex(WitnessIndex);
				WitnessIndex = NextAuthorityBay[WitnessIndex])
			{
				const FABTSM73BeamABay& Witness = Assembly.Bays[WitnessIndex];
				AuthorityBayIds.Add(Witness.BayId);
				AuthoritySourceVolumeIds.Add(Witness.SourceVolumeId);
				AuthorityCanonical += FString::Printf(
					TEXT("|W%d,%d:%.3f,%.3f,%.3f:%.3f,%.3f,%.3f"),
					Witness.BayId, Witness.SourceVolumeId,
					Witness.LocalBounds.Min.X, Witness.LocalBounds.Min.Y,
					Witness.LocalBounds.Min.Z, Witness.LocalBounds.Max.X,
					Witness.LocalBounds.Max.Y, Witness.LocalBounds.Max.Z);
				if (NextAuthorityBay[WitnessIndex] == INDEX_NONE)
				{
					break;
				}
			}
			Candidate.RootAuthorityCrc32 = FCrc::StrCrc32(*AuthorityCanonical);
			FDerivedCellCandidate& Eligible = EligibleCells.AddDefaulted_GetRef();
			Eligible.Request = MoveTemp(Candidate);
			Eligible.GroundBayBounds = Footprint.Bounds;
			Eligible.HighestReachZCM = Footprint.HighestReachZCM;
			Eligible.AvailableAreaCM2 = CellSpanX * CellSpanY;
			Eligible.AuthorityBayIds = MoveTemp(AuthorityBayIds);
			Eligible.AuthoritySourceVolumeIds = MoveTemp(AuthoritySourceVolumeIds);
		}

		if (!EligibleCells.IsEmpty())
		{
			bool bEndpointPairSelected = false;
			if (bRequireSharedCoursePair)
			{
				if (RequiredSupportedSpans != 1 || Settings.MaximumCellCount < 2)
				{
					OutError = TEXT("BeamC3V2SharedCourseE6RouteContractMissing");
					return false;
				}
				int32 SemanticSupportedSpanCount = 0;
				for (const FABTSM73DAG5BV2Volume& Volume : Silhouette.Volumes)
				{
					if (Volume.Role == EABTSM73DAG5BV2VolumeRole::SupportedSpan)
					{
						++SemanticSupportedSpanCount;
					}
				}
				if (SemanticSupportedSpanCount != 1)
				{
					OutError = TEXT("BeamC3V2SharedCourseRequiresOneSupportedSpan");
					return false;
				}
				TArray<int32> SpanVolumeIds;
				for (const FABTSM73BeamBBridgeEndpoint& Endpoint :
					BeamB.BridgeEndpoints)
				{
					if (Endpoint.SpanVolumeId != INDEX_NONE)
					{
						SpanVolumeIds.AddUnique(Endpoint.SpanVolumeId);
					}
				}
				SpanVolumeIds.Sort();
				int32 SelectedSpanEndpointCount = 0;
				if (SpanVolumeIds.Num() == 1)
				{
					for (const FABTSM73BeamBBridgeEndpoint& Endpoint :
						BeamB.BridgeEndpoints)
					{
						if (Endpoint.SpanVolumeId == SpanVolumeIds[0])
						{
							++SelectedSpanEndpointCount;
						}
					}
				}
				if (SpanVolumeIds.Num() != 1 || SelectedSpanEndpointCount != 2)
				{
					OutError = TEXT("BeamC3V2SharedCourseEndpointMultiplicityInvalid");
					return false;
				}
				const FABTSM73BeamBBridgeEndpoint* NegativeEndpoint = nullptr;
				const FABTSM73BeamBBridgeEndpoint* PositiveEndpoint = nullptr;
				int32 CoupledSpanVolumeId = INDEX_NONE;
				auto EndpointBefore = [](const FABTSM73BeamBBridgeEndpoint& A,
					const FABTSM73BeamBBridgeEndpoint& B)
				{
					if (A.SupportVolumeId != B.SupportVolumeId)
					{
						return A.SupportVolumeId < B.SupportVolumeId;
					}
					if (A.SupportBayId != B.SupportBayId)
					{
						return A.SupportBayId < B.SupportBayId;
					}
					if (!FMath::IsNearlyEqual(A.BearingPlaneCM, B.BearingPlaneCM))
					{
						return A.BearingPlaneCM < B.BearingPlaneCM;
					}
					return A.SeatPlannedMemberId < B.SeatPlannedMemberId;
				};
				for (const int32 SpanVolumeId : SpanVolumeIds)
				{
					const FABTSM73BeamBBridgeEndpoint* CandidateNegative = nullptr;
					const FABTSM73BeamBBridgeEndpoint* CandidatePositive = nullptr;
					for (const FABTSM73BeamBBridgeEndpoint& Endpoint :
						BeamB.BridgeEndpoints)
					{
						if (Endpoint.SpanVolumeId != SpanVolumeId)
						{
							continue;
						}
						const FABTSM73BeamBBridgeEndpoint*& Slot =
							Endpoint.bNegativeEndpoint
								? CandidateNegative : CandidatePositive;
						if (Slot == nullptr || EndpointBefore(Endpoint, *Slot))
						{
							Slot = &Endpoint;
						}
					}
					if (CandidateNegative != nullptr && CandidatePositive != nullptr)
					{
						CoupledSpanVolumeId = SpanVolumeId;
						NegativeEndpoint = CandidateNegative;
						PositiveEndpoint = CandidatePositive;
						break;
					}
				}

				const FABTSM73DAG5BV2Volume* SpanVolume =
					Silhouette.Volumes.FindByPredicate(
						[CoupledSpanVolumeId](const FABTSM73DAG5BV2Volume& Volume)
						{
							return Volume.VolumeId == CoupledSpanVolumeId;
						});
				if (NegativeEndpoint != nullptr && PositiveEndpoint != nullptr
					&& SpanVolume != nullptr
					&& (SpanVolume->SpanAxisIndex == 0
						|| SpanVolume->SpanAxisIndex == 1))
				{
					const int32 SpanAxis = SpanVolume->SpanAxisIndex;
					const int32 PerpendicularAxis = SpanAxis == 0 ? 1 : 0;
					auto AlignEndpointCell = [&](const FDerivedCellCandidate& Root,
						const FABTSM73BeamBBridgeEndpoint& Endpoint,
						const int8 EndpointSign,
						ABTSM73BeamC3V2::FCoupledExteriorFrameCellRequest& OutRequest)
					{
						if (Endpoint.RailStationsCM.IsEmpty())
						{
							return false;
						}
						FBox Bounds = Root.Request.LocalBounds;
						// E6 uses the registered minimum footprint along the bridge axis.
						// BearingPlaneCM is the center plane of the protected 36 cm BridgeSeat,
						// so the cell envelope terminates at the seat's near face. Placing the
						// envelope on the center plane would make every adjacent exterior post
						// penetrate half of the protected seat thickness.
						const double SpanLength = MinimumCellSpanCM;
						const double SeatHalfThicknessCM = SectionCM * 0.5;
						if (EndpointSign < 0)
						{
							Bounds.Max[SpanAxis] = FMath::Min(
								Root.GroundBayBounds.Max[SpanAxis],
								Endpoint.BearingPlaneCM - SeatHalfThicknessCM);
							Bounds.Min[SpanAxis] = Bounds.Max[SpanAxis] - SpanLength;
						}
						else
						{
							Bounds.Min[SpanAxis] = FMath::Max(
								Root.GroundBayBounds.Min[SpanAxis],
								Endpoint.BearingPlaneCM + SeatHalfThicknessCM);
							Bounds.Max[SpanAxis] = Bounds.Min[SpanAxis] + SpanLength;
						}
						if (Bounds.Min[SpanAxis]
								< Root.GroundBayBounds.Min[SpanAxis] - GroundToleranceCM
							|| Bounds.Max[SpanAxis]
								> Root.GroundBayBounds.Max[SpanAxis] + GroundToleranceCM)
						{
							return false;
						}
						OutRequest = Root.Request;
						OutRequest.LocalBounds = Bounds;
						OutRequest.CoupledSpanVolumeId = CoupledSpanVolumeId;
						OutRequest.CoupledSupportBayId = Endpoint.SupportBayId;
						OutRequest.CoupledEndpointSign = EndpointSign;
						OutRequest.bRequireSharedCoursePair = true;
						OutRequest.SharedCoursePairCourseCount =
							RequiredCellCourseCount;
						OutRequest.CoupledSpanAxis = SpanAxis == 0
							? EABTSM73BeamAFrameAxis::X
							: EABTSM73BeamAFrameAxis::Y;
						OutRequest.CoupledBearingPlaneCM = Endpoint.BearingPlaneCM;
						OutRequest.CoupledRailCenterZCM = Endpoint.RailCenterZCM;
						OutRequest.CoupledRailStationsCM = Endpoint.RailStationsCM;
						OutRequest.CoupledRailStationsCM.Sort();
						double MinimumSharedBottom = SpanVolume->LocalBounds.Min.Z;
						for (const FABTSM73BeamASupportVoid& SupportVoid :
							Assembly.ReservedSupportVoids)
						{
							if (SupportVoid.SpanSourceVolumeId == CoupledSpanVolumeId)
							{
								MinimumSharedBottom = FMath::Max(
									MinimumSharedBottom,
									static_cast<double>(SupportVoid.Bounds.Max.Z));
							}
						}
						OutRequest.CoupledMinimumSharedCourseBottomZCM =
							MinimumSharedBottom;
						return true;
					};
					struct FEndpointRootMatch
					{
						int32 RootIndex = INDEX_NONE;
						int32 MatchPass = INDEX_NONE;
						ABTSM73BeamC3V2::FCoupledExteriorFrameCellRequest Request;
					};
					struct FEndpointRootMatchDiagnostics
					{
						int32 bEndpointModuleValid = 0;
						int32 ModuleRootCandidates = 0;
						int32 ExactAuthorityCandidates = 0;
						int32 SourceAuthorityCandidates = 0;
						int32 SpanAlignedCandidates = 0;
					};
					auto FindSemanticVolume = [&Silhouette](const int32 VolumeId)
					{
						return Silhouette.Volumes.FindByPredicate(
							[VolumeId](const FABTSM73DAG5BV2Volume& Volume)
							{
								return Volume.VolumeId == VolumeId;
							});
					};
					auto CollectEndpointRoots = [&] (
						const FABTSM73BeamBBridgeEndpoint& Endpoint,
						const int8 EndpointSign,
						TArray<FEndpointRootMatch>& OutMatches,
						FEndpointRootMatchDiagnostics& OutDiagnostics)
					{
						OutMatches.Reset();
						OutDiagnostics = FEndpointRootMatchDiagnostics();
						const FABTSM73DAG5BV2Volume* DeclaredSupport =
							FindSemanticVolume(Endpoint.DeclaredSupportVolumeId);
						const FABTSM73DAG5BV2Volume* ActualSupport =
							FindSemanticVolume(Endpoint.SupportVolumeId);
						if (DeclaredSupport == nullptr || ActualSupport == nullptr)
						{
							return;
						}
						const FString EndpointRoot =
							SemanticRootPath(DeclaredSupport->DerivationPath);
						if (EndpointRoot.IsEmpty()
							|| SemanticRootPath(ActualSupport->DerivationPath)
								!= EndpointRoot)
						{
							return;
						}
						OutDiagnostics.bEndpointModuleValid = 1;
						for (int32 CandidateIndex = 0;
							CandidateIndex < EligibleCells.Num(); ++CandidateIndex)
						{
							const FDerivedCellCandidate& Root =
								EligibleCells[CandidateIndex];
							const FABTSM73DAG5BV2Volume* RootVolume =
								FindSemanticVolume(Root.Request.SourceVolumeId);
							if (RootVolume == nullptr
								|| SemanticRootPath(RootVolume->DerivationPath)
									!= EndpointRoot)
							{
								continue;
							}
							++OutDiagnostics.ModuleRootCandidates;
							const bool bExactBay =
								Root.AuthorityBayIds.Contains(Endpoint.SupportBayId);
							const bool bSourceAuthority =
								Root.AuthoritySourceVolumeIds.Contains(
									Endpoint.SupportVolumeId);
							if (bExactBay)
							{
								++OutDiagnostics.ExactAuthorityCandidates;
							}
							if (bSourceAuthority)
							{
								++OutDiagnostics.SourceAuthorityCandidates;
							}
							const int32 MatchPass = bExactBay ? 0
								: bSourceAuthority ? 1 : 2;
							ABTSM73BeamC3V2::FCoupledExteriorFrameCellRequest Request;
							if (AlignEndpointCell(
								Root, Endpoint, EndpointSign, Request))
							{
								++OutDiagnostics.SpanAlignedCandidates;
								FEndpointRootMatch& Match =
									OutMatches.AddDefaulted_GetRef();
								Match.RootIndex = CandidateIndex;
								Match.MatchPass = MatchPass;
								Match.Request = MoveTemp(Request);
							}
						}
					};

					TArray<FEndpointRootMatch> NegativeMatches;
					TArray<FEndpointRootMatch> PositiveMatches;
					FEndpointRootMatchDiagnostics NegativeDiagnostics;
					FEndpointRootMatchDiagnostics PositiveDiagnostics;
					CollectEndpointRoots(*NegativeEndpoint, -1,
						NegativeMatches, NegativeDiagnostics);
					CollectEndpointRoots(*PositiveEndpoint, 1,
						PositiveMatches, PositiveDiagnostics);

					int32 JointPairCount = 0;
					int32 DistinctRootPairCount = 0;
					int32 PerpendicularFitPairCount = 0;
					int32 SpanSeparatedPairCount = 0;
					double BestAvailablePerpendicularSpanCM = 0.0;
					double SmallestRequiredPerpendicularSpanCM =
						TNumericLimits<double>::Max();
					for (int32 MatchRank = 0;
						MatchRank <= 4 && !bEndpointPairSelected; ++MatchRank)
					{
						for (const FEndpointRootMatch& NegativeMatch : NegativeMatches)
						{
							for (const FEndpointRootMatch& PositiveMatch : PositiveMatches)
							{
								if (NegativeMatch.MatchPass + PositiveMatch.MatchPass
									!= MatchRank)
								{
									continue;
								}
								++JointPairCount;
								if (!EligibleCells.IsValidIndex(NegativeMatch.RootIndex)
									|| !EligibleCells.IsValidIndex(PositiveMatch.RootIndex)
									|| NegativeMatch.RootIndex == PositiveMatch.RootIndex
									|| NegativeMatch.Request.SourceVolumeId
										== PositiveMatch.Request.SourceVolumeId)
								{
									continue;
								}
								++DistinctRootPairCount;

								const FBox& NegativeGround = EligibleCells[
									NegativeMatch.RootIndex].GroundBayBounds;
								const FBox& PositiveGround = EligibleCells[
									PositiveMatch.RootIndex].GroundBayBounds;
								const double CommonMinimum = FMath::Max(
									NegativeGround.Min[PerpendicularAxis],
									PositiveGround.Min[PerpendicularAxis]);
								const double CommonMaximum = FMath::Min(
									NegativeGround.Max[PerpendicularAxis],
									PositiveGround.Max[PerpendicularAxis]);
								double RequiredPerpendicularMin =
									TNumericLimits<double>::Max();
								double RequiredPerpendicularMax =
									-TNumericLimits<double>::Max();
								auto IncludeRailEnvelope = [&] (
									const FABTSM73BeamBBridgeEndpoint& Endpoint)
								{
									for (const double Station : Endpoint.RailStationsCM)
									{
										RequiredPerpendicularMin = FMath::Min(
											RequiredPerpendicularMin,
											Station - SectionCM * 0.5);
										RequiredPerpendicularMax = FMath::Max(
											RequiredPerpendicularMax,
											Station + SectionCM * 0.5);
									}
								};
								IncludeRailEnvelope(*NegativeEndpoint);
								IncludeRailEnvelope(*PositiveEndpoint);
								const double CommonSpan = FMath::FloorToDouble(
									FMath::Min(CommonMaximum - CommonMinimum,
										MaximumCellSpanCM) / CoordinateQuantumCM)
									* CoordinateQuantumCM;
								const double RequiredSpan = FMath::Max(
									MinimumCellSpanCM,
									RequiredPerpendicularMax - RequiredPerpendicularMin);
								BestAvailablePerpendicularSpanCM = FMath::Max(
									BestAvailablePerpendicularSpanCM, CommonSpan);
								SmallestRequiredPerpendicularSpanCM = FMath::Min(
									SmallestRequiredPerpendicularSpanCM, RequiredSpan);
								if (CommonSpan + GroundToleranceCM < RequiredSpan)
								{
									continue;
								}
								const double HalfCommonSpan = CommonSpan * 0.5;
								const double MinimumCenter = FMath::Max(
									CommonMinimum + HalfCommonSpan,
									RequiredPerpendicularMax - HalfCommonSpan);
								const double MaximumCenter = FMath::Min(
									CommonMaximum - HalfCommonSpan,
									RequiredPerpendicularMin + HalfCommonSpan);
								if (MinimumCenter > MaximumCenter + GroundToleranceCM)
								{
									continue;
								}
								++PerpendicularFitPairCount;
								const double DesiredCenter = FMath::RoundToDouble(
									(RequiredPerpendicularMin + RequiredPerpendicularMax)
									* 0.5 / CoordinateQuantumCM) * CoordinateQuantumCM;
								const double CommonCenter = FMath::Clamp(
									DesiredCenter, MinimumCenter, MaximumCenter);

								ABTSM73BeamC3V2::FCoupledExteriorFrameCellRequest NegativeCell =
									NegativeMatch.Request;
								ABTSM73BeamC3V2::FCoupledExteriorFrameCellRequest PositiveCell =
									PositiveMatch.Request;
								NegativeCell.LocalBounds.Min[PerpendicularAxis] =
									CommonCenter - HalfCommonSpan;
								NegativeCell.LocalBounds.Max[PerpendicularAxis] =
									CommonCenter + HalfCommonSpan;
								PositiveCell.LocalBounds.Min[PerpendicularAxis] =
									NegativeCell.LocalBounds.Min[PerpendicularAxis];
								PositiveCell.LocalBounds.Max[PerpendicularAxis] =
									NegativeCell.LocalBounds.Max[PerpendicularAxis];
								const double OverlapX = FMath::Min(
									NegativeCell.LocalBounds.Max.X,
									PositiveCell.LocalBounds.Max.X)
									- FMath::Max(NegativeCell.LocalBounds.Min.X,
										PositiveCell.LocalBounds.Min.X);
								const double OverlapY = FMath::Min(
									NegativeCell.LocalBounds.Max.Y,
									PositiveCell.LocalBounds.Max.Y)
									- FMath::Max(NegativeCell.LocalBounds.Min.Y,
										PositiveCell.LocalBounds.Min.Y);
								if (OverlapX > GroundToleranceCM
									&& OverlapY > GroundToleranceCM)
								{
									continue;
								}
								++SpanSeparatedPairCount;
								OutCells.Add(MoveTemp(NegativeCell));
								OutCells.Add(MoveTemp(PositiveCell));
								bEndpointPairSelected = true;
								UE_LOG(LogABTSRuntime, Display,
									TEXT("[ABTS][M7.3-Beam-C3V2][SharedCoursePairDerived]")
									TEXT(" Span=%d Axis=%d Opening=%.2f VoidTop=%.2f")
									TEXT(" NegRoot=%d/%d PosRoot=%d/%d")
									TEXT(" MatchRank=%d JointPairs=%d")
									TEXT(" NegBounds=%s..%s PosBounds=%s..%s"),
									CoupledSpanVolumeId, SpanAxis,
									PositiveEndpoint->BearingPlaneCM
										- NegativeEndpoint->BearingPlaneCM,
									OutCells[0].CoupledMinimumSharedCourseBottomZCM,
									OutCells[0].SourceVolumeId, OutCells[0].BayId,
									OutCells[1].SourceVolumeId, OutCells[1].BayId,
									MatchRank, JointPairCount,
									*OutCells[0].LocalBounds.Min.ToCompactString(),
									*OutCells[0].LocalBounds.Max.ToCompactString(),
									*OutCells[1].LocalBounds.Min.ToCompactString(),
									*OutCells[1].LocalBounds.Max.ToCompactString());
								break;
							}
							if (bEndpointPairSelected)
							{
								break;
							}
						}
					}

					if (!bEndpointPairSelected)
					{
						UE_LOG(LogABTSRuntime, Display,
							TEXT("[ABTS][M7.3-Beam-C3V2][EndpointPairUnavailableDetail]")
							TEXT(" NegModule=%d/%d Exact=%d Source=%d Aligned=%d Matches=%d")
							TEXT(" PosModule=%d/%d Exact=%d Source=%d Aligned=%d Matches=%d")
							TEXT(" JointPairs=%d")
							TEXT(" DistinctPairs=%d PerpFit=%d SpanSeparated=%d")
							TEXT(" BestPerp=%.2f RequiredPerp=%.2f"),
							NegativeDiagnostics.bEndpointModuleValid,
							NegativeDiagnostics.ModuleRootCandidates,
							NegativeDiagnostics.ExactAuthorityCandidates,
							NegativeDiagnostics.SourceAuthorityCandidates,
							NegativeDiagnostics.SpanAlignedCandidates,
							NegativeMatches.Num(),
							PositiveDiagnostics.bEndpointModuleValid,
							PositiveDiagnostics.ModuleRootCandidates,
							PositiveDiagnostics.ExactAuthorityCandidates,
							PositiveDiagnostics.SourceAuthorityCandidates,
							PositiveDiagnostics.SpanAlignedCandidates,
							PositiveMatches.Num(), JointPairCount,
							DistinctRootPairCount, PerpendicularFitPairCount,
							SpanSeparatedPairCount,
							BestAvailablePerpendicularSpanCM,
							SmallestRequiredPerpendicularSpanCM
								== TNumericLimits<double>::Max()
									? -1.0 : SmallestRequiredPerpendicularSpanCM);
					}
				}
				if (!bEndpointPairSelected)
				{
					UE_LOG(LogABTSRuntime, Display,
						TEXT("[ABTS][M7.3-Beam-C3V2][EndpointPairUnavailable]")
						TEXT(" RequiredSpans=%d Span=%d Endpoints=%d EligibleRoots=%d"),
						RequiredSupportedSpans, CoupledSpanVolumeId,
						BeamB.BridgeEndpoints.Num(), EligibleCells.Num());
				}
			}
			if (bRequireSharedCoursePair && !bEndpointPairSelected)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V2SharedCoursePairUnavailable:RequiredSpans=%d:Endpoints=%d:EligibleRoots=%d"),
					RequiredSupportedSpans, BeamB.BridgeEndpoints.Num(),
					EligibleCells.Num());
				return false;
			}
			if (!bEndpointPairSelected)
			{
				OutCells.Add(EligibleCells[0].Request);
			}
			OutError.Reset();
			return true;
		}

		OutError = FString::Printf(
			TEXT("BeamC3V2NoGroundedCell:RequiredXY=%.0f:RequiredZ=%.0f")
			TEXT(":Bays=%d:Grounded=%d:HeightFit=%d:XYFit=%d")
			TEXT(":AssemblyMaxZ=%.0f:ClosestMinZ=%.0f")
			TEXT(":MaxGround=%.0f,%.0f,%.0f:BayData=%s"),
			MinimumCellSpanCM, RequiredHeightCM, Assembly.Bays.Num(),
			GroundedBayCount, HeightFitBayCount, XYFitBayCount,
			MaximumAssemblyZCM == -TNumericLimits<double>::Max()
				? -1.0 : MaximumAssemblyZCM,
			ClosestMinimumZCM == TNumericLimits<double>::Max()
				? -1.0 : ClosestMinimumZCM,
			MaximumGroundedBaySize.X, MaximumGroundedBaySize.Y,
			MaximumGroundedBaySize.Z, *BayDiagnostics);
		return false;
	}

	bool MeetsSemanticVisualMilestone(
		const FABTSM73BeamD0ResolvedProfile& Profile,
		const FABTSM73DAG5BV2GenerationResult& Silhouette)
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
				|| RoofPrimitiveCount == 1);
	}

	bool MeetsVisualMilestone(
		const FABTSM73BeamD0ResolvedProfile& Profile,
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		const int32 VisibleFeatureCount)
	{
		return MeetsSemanticVisualMilestone(Profile, Silhouette)
			&& (Profile.DifficultyTier < 2 || VisibleFeatureCount >= 2);
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
			// Core members and a SupportedSpan's primary rail are permanent load
			// paths. A gameplay weakness may only target an independently seated
			// shell/connector member; never turn the building's static certificate
			// into a promise that deleting a primary load path is safe.
			if (Member.Role == EABTSM73BeamAMemberRole::CoreCourse
				|| Member.Role == EABTSM73BeamAMemberRole::CorePost
				|| Member.Role == EABTSM73BeamAMemberRole::BridgeRail)
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
				// A V3 BridgeRail is the primary SupportedSpan load path, not a
				// disposable seam connector. Until V3 emits an independently seated
				// connector, prefer an ordinary non-core secondary shell member and
				// never mark the main span rail as the weakness.
				if (Member.Role == EABTSM73BeamAMemberRole::BridgeSeat)
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
			const FABTSM73BeamAMember* Fallback = Assembly.Members.FindByPredicate(
				[](const FABTSM73BeamAMember& Member)
				{
					return Member.Role != EABTSM73BeamAMemberRole::CoreCourse
						&& Member.Role != EABTSM73BeamAMemberRole::CorePost
						&& Member.Role != EABTSM73BeamAMemberRole::BridgeRail;
				});
			BestId = Fallback != nullptr ? Fallback->MemberId : INDEX_NONE;
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
					if (Count <= 8)
					{
						const FABTSM73BeamD1BrickBinding& BrickA =
							Bricks[Order[SortedA]];
						const FABTSM73BeamD1BrickBinding& BrickB =
							Bricks[Order[SortedB]];
						UE_LOG(LogABTSRuntime, Warning,
							TEXT("[ABTS][M7.3-Beam-D1][BrickPenetration]")
							TEXT(" A=%d(Member=%d Axis=%d Role=%d Bounds=%s..%s)")
							TEXT(" B=%d(Member=%d Axis=%d Role=%d Bounds=%s..%s)")
							TEXT(" Overlap=%s"),
							BrickA.BrickId, BrickA.MemberId,
							static_cast<int32>(BrickA.Axis),
							static_cast<int32>(BrickA.StructuralRole),
							*BrickA.LocalBounds.Min.ToCompactString(),
							*BrickA.LocalBounds.Max.ToCompactString(),
							BrickB.BrickId, BrickB.MemberId,
							static_cast<int32>(BrickB.Axis),
							static_cast<int32>(BrickB.StructuralRole),
							*BrickB.LocalBounds.Min.ToCompactString(),
							*BrickB.LocalBounds.Max.ToCompactString(),
							*Overlap.ToCompactString());
					}
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
	TSet<FString> AttemptedV3InputIdentities;
	TSet<FString> AttemptedV3PlanIdentities;
	for (int32 Attempt = 0; Attempt < Target.MaximumCandidateAttempts; ++Attempt)
	{
		const double AttemptStartSeconds = FPlatformTime::Seconds();
		double ProfileMilliseconds = 0.0;
		double SilhouetteMilliseconds = 0.0;
		double BeamC3V3Milliseconds = 0.0;
		double BeamCMilliseconds = 0.0;
		double CompileMilliseconds = 0.0;
		ABTSM73BeamC3V3::FPlanSummary LoggedV3Summary;
		auto MeasureStage = [](double& InOutMilliseconds, auto&& Operation)
		{
			const double StartSeconds = FPlatformTime::Seconds();
			const bool bSucceeded = Operation();
			InOutMilliseconds +=
				(FPlatformTime::Seconds() - StartSeconds) * 1000.0;
			return bSucceeded;
		};
		const int32 AttemptSeed = ABTSM73BeamD1::CandidateSeed(
			Settings.BuildingSeed, Attempt);
		auto LogCandidateRejection = [&Settings, Attempt, AttemptSeed,
			AttemptStartSeconds, &ProfileMilliseconds, &SilhouetteMilliseconds,
			&BeamC3V3Milliseconds, &LoggedV3Summary,
			&BeamCMilliseconds, &CompileMilliseconds](
			const TCHAR* Gate, const FString& Reason, const int32 MemberCount,
			const int32 BrickCount = 0)
		{
			UE_LOG(LogABTSRuntime, Display,
				TEXT("[ABTS][M7.3-Beam-D1][CandidateRejected]")
				TEXT(" Profile=%s Tier=%d BaseSeed=%d Attempt=%d CandidateSeed=%d")
				TEXT(" Gate=%s Reason=%s Members=%d Bricks=%d")
				TEXT(" EnvelopeHash=%lld CorePlanHash=%lld SupportPlanHash=%lld FinalGeometryHash=%lld")
				TEXT(" TimingMs=Profile:%.2f,Shape:%.2f,C3V3:%.2f,BeamC:%.2f,Compile:%.2f,Total:%.2f"),
				*Settings.GameplayProfileId.ToString(), Settings.DifficultyTier,
				Settings.BuildingSeed, Attempt, AttemptSeed, Gate, *Reason,
				MemberCount, BrickCount, LoggedV3Summary.EnvelopeHash,
				LoggedV3Summary.CorePlanHash, LoggedV3Summary.SupportPlanHash,
				LoggedV3Summary.FinalGeometryHash, ProfileMilliseconds,
				SilhouetteMilliseconds, BeamC3V3Milliseconds,
				BeamCMilliseconds, CompileMilliseconds,
				(FPlatformTime::Seconds() - AttemptStartSeconds) * 1000.0);
		};
		FABTSM73BeamD0ResolvedProfile Profile;
		FString CandidateError;
		if (!MeasureStage(ProfileMilliseconds, [&]()
			{
				return FABTSM73BeamD0ProfileCatalog::GetDefault().Resolve(
					Settings.GameplayProfileId, Settings.DifficultyTier,
					AttemptSeed, Profile, CandidateError);
			}))
		{
			LastFailure = FString::Printf(TEXT("Profile:%s"), *CandidateError);
			LogCandidateRejection(TEXT("Profile"), LastFailure, 0);
			continue;
		}

		FABTSM73DAG5BV2GenerationResult Silhouette;
		FABTSM73DAG5BShapeGrammarV2 ShapeGenerator;
		if (!MeasureStage(SilhouetteMilliseconds, [&]()
			{
				return ShapeGenerator.Generate(
					Profile.BeamSettings.BeamB.BeamA.Silhouette,
					Silhouette, CandidateError);
			}))
		{
			LastFailure = FString::Printf(TEXT("Silhouette:%s"), *CandidateError);
			LogCandidateRejection(TEXT("Silhouette"), LastFailure, 0);
			continue;
		}
		if (!ABTSM73BeamD1::MeetsSemanticVisualMilestone(Profile, Silhouette))
		{
			LastFailure = FString::Printf(
				TEXT("SemanticVisualMilestoneNotMet:Volumes=%d:Spans=%d:Prism=%d:Pyramid=%d"),
				Silhouette.Summary.VolumeCount,
				Silhouette.Summary.SupportedSpanCount,
				Silhouette.Summary.PrismCount,
				Silhouette.Summary.PyramidCount);
			LogCandidateRejection(TEXT("SemanticVisualMilestone"), LastFailure, 0);
			continue;
		}

		// Candidate attempts are a semantic-WFC preselection only. Once a
		// silhouette reaches the registered semantic milestone, V3/Beam-C/D1 run
		// exactly once; a structural rejection may not advance to another Seed.
		const FString V3InputIdentity = FString::Printf(
			TEXT("%lld:%lld:%lld:%lld"), Profile.ResolvedSettingsHash,
			Silhouette.Summary.GrammarHash, Silhouette.Summary.WFCHash,
			Silhouette.Summary.ResultHash);
		if (AttemptedV3InputIdentities.Contains(V3InputIdentity))
		{
			return ABTSM73BeamD1::Reject(OutResult, OutError,
				FString::Printf(
					TEXT("BeamC3V3RepeatedPlanInput:%s:Attempt=%d"),
					*V3InputIdentity, Attempt));
		}
		AttemptedV3InputIdentities.Add(V3InputIdentity);

		ABTSM73BeamC3V3::FGenerationResult BeamC3V3;
		FABTSM73BeamC3V3SkeletonFirstGenerator BeamC3V3Generator;
		const bool bV3Generated = MeasureStage(BeamC3V3Milliseconds, [&]()
			{
				return BeamC3V3Generator.Generate(
					Profile, Silhouette, BeamC3V3, CandidateError);
			});
		ABTSM73BeamC3V3::FPlanSummary& V3Summary =
			BeamC3V3.Plan.Summary;
		LoggedV3Summary = V3Summary;
		const FString V3PlanIdentity = FString::Printf(
			TEXT("%lld:%lld:%lld:%lld"), V3Summary.EnvelopeHash,
			V3Summary.CorePlanHash, V3Summary.SupportPlanHash,
			V3Summary.FinalGeometryHash);
		const bool bHasV3PlanIdentity = V3Summary.CorePlanHash != 0
			|| V3Summary.SupportPlanHash != 0
			|| V3Summary.FinalGeometryHash != 0;
		if (bHasV3PlanIdentity
			&& AttemptedV3PlanIdentities.Contains(V3PlanIdentity))
		{
			return ABTSM73BeamD1::Reject(OutResult, OutError,
				FString::Printf(
					TEXT("BeamC3V3RepeatedPlan:%s:Attempt=%d"),
					*V3PlanIdentity, Attempt));
		}
		if (bHasV3PlanIdentity)
		{
			AttemptedV3PlanIdentities.Add(V3PlanIdentity);
		}
		if (!bV3Generated)
		{
			LastFailure = FString::Printf(
				TEXT("BeamC3V3:%s:Input=%s:Envelope=%lld:Core=%lld:Support=%lld:Final=%lld"),
				*CandidateError, *V3InputIdentity, V3Summary.EnvelopeHash,
				V3Summary.CorePlanHash, V3Summary.SupportPlanHash,
				V3Summary.FinalGeometryHash);
			LogCandidateRejection(TEXT("BeamC3V3"), LastFailure,
				BeamC3V3.Assembly.Members.Num());
			return ABTSM73BeamD1::Reject(OutResult, OutError,
				FString::Printf(TEXT("BeamC3V3StructuralCandidateRejected:Attempt=%d:%s"),
					Attempt, *LastFailure));
		}

		FABTSM73BeamCGenerationResult BeamC;
		FABTSM73BeamCGenerator BeamCGenerator;
		const bool bBeamCGenerated = MeasureStage(BeamCMilliseconds, [&]()
			{
				// V3 owns the complete final geometry. Beam-C is a read-only static
				// audit here and may not insert rescue posts or mutate the assembly.
				return BeamCGenerator.Generate(
					Profile.BeamSettings, BeamC3V3.Assembly, BeamC,
					CandidateError);
			});
		// CompleteStaticDAG contains later-stage work and is not a Stage-1 leaf.
		// Only an explicitly Stage-1-timed result may consume this 10-second gate.
		if (V3Summary.bStage1TimingEvaluated)
		{
			V3Summary.StaticDAGMilliseconds = BeamCMilliseconds;
			V3Summary.Stage1TotalMilliseconds += BeamCMilliseconds;
			if (V3Summary.Stage1TotalMilliseconds
				> ABTSM73BeamC3V3::Stage1LeafTimeBudgetMilliseconds)
			{
				V3Summary.bStage1WithinTimeBudget = false;
				V3Summary.Stage1TimeoutPhase = TEXT("StaticDAG");
				CandidateError = FString::Printf(
					TEXT("BeamC3V3Stage1Timeout:Phase=StaticDAG:ElapsedMs=%.3f:BudgetMs=%.3f:TimingMs=Demand:%.3f,Child:%.3f,Main:%.3f,Joint:%.3f,Emission:%.3f,DAG:%.3f"),
					V3Summary.Stage1TotalMilliseconds,
					ABTSM73BeamC3V3::Stage1LeafTimeBudgetMilliseconds,
					V3Summary.TerminalDemandMilliseconds,
					V3Summary.ChildCandidateMilliseconds,
					V3Summary.PodiumMainCandidateMilliseconds,
					V3Summary.JointSelectionMilliseconds,
					V3Summary.MemberEmissionMilliseconds,
					V3Summary.StaticDAGMilliseconds);
				return ABTSM73BeamD1::Reject(OutResult, OutError, CandidateError);
			}
			V3Summary.bStage1WithinTimeBudget = true;
		}
		if (!bBeamCGenerated)
		{
			FString InvalidNodeDiagnostics;
			int32 InvalidNodeCount = 0;
			for (const FABTSM73BeamCLoadNode& Node : BeamC.Nodes)
			{
				if (Node.bSupportResultantValid && Node.bSupportSpreadValid)
				{
					continue;
				}
				const FABTSM73BeamAMember* AssemblyMember =
					BeamC3V3.Assembly.Members.IsValidIndex(Node.MemberId)
						? &BeamC3V3.Assembly.Members[Node.MemberId] : nullptr;
				const ABTSM73BeamC3V3::FPlannedMember* PlannedMember =
					BeamC3V3.Plan.Members.IsValidIndex(Node.MemberId)
						? &BeamC3V3.Plan.Members[Node.MemberId] : nullptr;
				FString SupportIntervals;
				for (const FABTSM73BeamCLoadEdge& Edge : BeamC.Edges)
				{
					if (Edge.UpperMemberId != Node.MemberId)
					{
						continue;
					}
					const int32 AxisIndex = Node.Axis == EABTSM73BeamAFrameAxis::Y ? 1 : 0;
					SupportIntervals += FString::Printf(TEXT("|%d:%.1f..%.1f"),
						Edge.LowerMemberId, Edge.ContactMinXY[AxisIndex],
						Edge.ContactMaxXY[AxisIndex]);
				}
				InvalidNodeDiagnostics += FString::Printf(
					TEXT("|M%d:O%d:K%d:Q%d:A%d:Role%d:L%.1f:G%d:S%d:R=%.2f,%.2f,%.2f:Resultant=%d:Spread=%d:Coverage=%.3f:SupportSpan=%.3f:From=%s:To=%s:Seats=%s"),
					Node.MemberId,
					PlannedMember != nullptr
						? static_cast<int32>(PlannedMember->OwnerKind) : INDEX_NONE,
					PlannedMember != nullptr
						? static_cast<int32>(PlannedMember->SkeletonKind) : INDEX_NONE,
					PlannedMember != nullptr ? PlannedMember->CourseIndex : INDEX_NONE,
					static_cast<int32>(Node.Axis),
					AssemblyMember != nullptr
						? static_cast<int32>(AssemblyMember->Role) : INDEX_NONE,
					AssemblyMember != nullptr ? AssemblyMember->LengthCM : 0.0f,
					Node.bGround ? 1 : 0, Node.SupportCount,
					Node.LoadResultant.X, Node.LoadResultant.Y,
					Node.LoadResultant.Z, Node.bSupportResultantValid ? 1 : 0,
					Node.bSupportSpreadValid ? 1 : 0,
					Node.RealSupportCoverageRatio, Node.RealSupportSpanRatio,
					PlannedMember != nullptr
						? *PlannedMember->LocalStart.ToCompactString() : TEXT("None"),
					PlannedMember != nullptr
						? *PlannedMember->LocalEnd.ToCompactString() : TEXT("None"),
					*SupportIntervals);
				if (++InvalidNodeCount >= 8)
				{
					break;
				}
			}
			LastFailure = FString::Printf(
				TEXT("BeamC:%s:Contact=%d:Resultant=%d:Spread=%d:Span=%d:Cantilever=%d:InvalidNodes=%s"),
				*CandidateError,
				BeamC.Summary.RealContactMismatchCount,
				BeamC.Summary.SupportResultantViolationCount,
				BeamC.Summary.SupportSpreadViolationCount,
				BeamC.Summary.SpanViolationCount,
				BeamC.Summary.CantileverViolationCount,
				*InvalidNodeDiagnostics);
			LogCandidateRejection(TEXT("BeamC"), LastFailure,
				BeamC3V3.Assembly.Members.Num());
			return ABTSM73BeamD1::Reject(OutResult, OutError,
				FString::Printf(TEXT("BeamC3V3StructuralCandidateRejected:Attempt=%d:%s"),
					Attempt, *LastFailure));
		}
		if (BeamC.Summary.StructuralClosurePassCount != 0
			|| BeamC.Summary.AddedStructuralSupportPostCount != 0
			|| BeamC.Summary.SupportResultantAdvisoryCount != 0)
		{
			LastFailure = FString::Printf(
				TEXT("BeamC3V3StaticAuditMutatedOrAdvisory:Passes=%d:Added=%d:Advisory=%d"),
				BeamC.Summary.StructuralClosurePassCount,
				BeamC.Summary.AddedStructuralSupportPostCount,
				BeamC.Summary.SupportResultantAdvisoryCount);
			LogCandidateRejection(TEXT("BeamCStatic"), LastFailure,
				BeamC3V3.Assembly.Members.Num());
			return ABTSM73BeamD1::Reject(OutResult, OutError,
				FString::Printf(TEXT("BeamC3V3StructuralCandidateRejected:Attempt=%d:%s"),
					Attempt, *LastFailure));
		}

		FABTSM73BeamD1GenerationResult Candidate;
		if (!MeasureStage(CompileMilliseconds, [&]()
			{
				return CompileResolvedAssembly(
					Profile, BeamC3V3.Assembly, V3Summary.FinalGeometryHash,
					BeamC, Candidate, CandidateError);
			}))
		{
			LastFailure = FString::Printf(TEXT("Compile:%s"), *CandidateError);
			LogCandidateRejection(TEXT("Compile"), LastFailure,
				BeamC3V3.Assembly.Members.Num(), Candidate.Summary.BrickCount);
			return ABTSM73BeamD1::Reject(OutResult, OutError,
				FString::Printf(TEXT("BeamC3V3StructuralCandidateRejected:Attempt=%d:%s"),
					Attempt, *LastFailure));
		}
		// A shared-course member replaces the matching rail slot in each of its
		// two endpoint cores. Shared rails are authored in two-rail course pairs,
		// so an odd rail count or anything other than two replaced slots per rail
		// is an incomplete bridge contract and must not reach D1.
		const bool bSharedCourseRailContract =
			V3Summary.SharedCourseCount >= 0
			&& (V3Summary.SharedCourseCount & 1) == 0
			&& V3Summary.SharedCourseReplacementSlotCount
				== V3Summary.SharedCourseCount * 2
			&& V3Summary.SharedCourseBandViolationCount == 0
			&& (V3Summary.SupportedSpanCount == 0
				? V3Summary.SharedCourseCount == 0
				: V3Summary.SharedCourseCount
					>= V3Summary.SupportedSpanCount * 2);
		// V3 produces one building, even when it needs several grounded cores.
		// Every planned core must therefore be reached by the same common shell;
		// per-core islands cannot be certified as a complete D1 building.
		const bool bCommonShellContract =
			V3Summary.BuildingGroupCount == 1
			&& V3Summary.ExplicitCoreCellCount > 0
			&& V3Summary.CommonShellMemberCount > 0
			&& V3Summary.CommonShellConnectedCoreCount
				== V3Summary.ExplicitCoreCellCount;
		const bool bV3Certified = V3Summary.bAccepted
			&& !V3Summary.bPhysicalStabilityEvaluated
			&& V3Summary.PlannedMemberCount == V3Summary.EmittedMemberCount
			&& V3Summary.EmittedMemberCount
				== BeamC3V3.Assembly.Members.Num()
			&& (V3Summary.GroundedFaceMask & ABTSM73BeamC3V3::AllFaces)
				== ABTSM73BeamC3V3::AllFaces
			&& V3Summary.MinimumGroundedExteriorPostStationsPerFace >= 2
			&& V3Summary.ExplicitCoreCellCount
				>= V3Summary.GroundedComponentCount
			&& V3Summary.GroundedCoreCellCount
				== V3Summary.ExplicitCoreCellCount
			&& V3Summary.SuspendedCoreCount == 0
			&& V3Summary.ShellMemberCount > 0
			&& V3Summary.CoreDerivedShellMemberCount
				== V3Summary.ShellMemberCount
			&& V3Summary.SharedCourseNonCoreEndpointViolationCount == 0
			&& bSharedCourseRailContract
			&& bCommonShellContract
			&& BeamC.Summary.bAccepted
			&& BeamC.Summary.StructuralClosurePassCount == 0
			&& BeamC.Summary.AddedStructuralSupportPostCount == 0
			&& BeamC.Summary.SupportResultantAdvisoryCount == 0
			&& V3Summary.SupportedSpanCount
				== Silhouette.Summary.SupportedSpanCount;
		if (!bV3Certified)
		{
			LastFailure = FString::Printf(
				TEXT("BeamC3V3CertificateInvalid:Plan=%d:Physical=%d:Planned=%d:Emitted=%d:Actual=%d:Faces=0x%02x:MinFacePosts=%d:ExplicitCores=%d:GroundedCores=%d:SuspendedCores=%d:Shell=%d:CoreDerivedShell=%d:Shared=%d:SharedEndpointViolations=%d:SharedReplacementSlots=%d:SharedBandViolations=%d:Groups=%d:CommonShell=%d:CommonConnectedCores=%d:PlannedSpans=%d:SemanticSpans=%d"),
				V3Summary.bAccepted ? 1 : 0,
				V3Summary.bPhysicalStabilityEvaluated ? 1 : 0,
				V3Summary.PlannedMemberCount, V3Summary.EmittedMemberCount,
				BeamC3V3.Assembly.Members.Num(), V3Summary.GroundedFaceMask,
				V3Summary.MinimumGroundedExteriorPostStationsPerFace,
				V3Summary.ExplicitCoreCellCount,
				V3Summary.GroundedCoreCellCount,
				V3Summary.SuspendedCoreCount,
				V3Summary.ShellMemberCount,
				V3Summary.CoreDerivedShellMemberCount,
				V3Summary.SharedCourseCount,
				V3Summary.SharedCourseNonCoreEndpointViolationCount,
				V3Summary.SharedCourseReplacementSlotCount,
				V3Summary.SharedCourseBandViolationCount,
				V3Summary.BuildingGroupCount,
				V3Summary.CommonShellMemberCount,
				V3Summary.CommonShellConnectedCoreCount,
				V3Summary.SupportedSpanCount,
				Silhouette.Summary.SupportedSpanCount);
			LogCandidateRejection(TEXT("BeamC3V3Certificate"), LastFailure,
				BeamC3V3.Assembly.Members.Num(), Candidate.Summary.BrickCount);
			return ABTSM73BeamD1::Reject(OutResult, OutError,
				FString::Printf(TEXT("BeamC3V3StructuralCandidateRejected:Attempt=%d:%s"),
					Attempt, *LastFailure));
		}

		// Compatibility fields retain only direct V3 equivalents. Do not report
		// the whole V3 building as an old C3 core: legacy core counts below are
		// restricted to actual core roles, while the SkeletonFirst fields remain
		// the authoritative complete-building contract.
		const int32 V3CoreMemberCount = Algo::CountIf(
			BeamC3V3.Assembly.Members,
			[](const FABTSM73BeamAMember& Member)
			{
				return Member.Role == EABTSM73BeamAMemberRole::CoreCourse
					|| Member.Role == EABTSM73BeamAMemberRole::CorePost;
			});
		Candidate.Summary.bStabilityCoreCertified = bV3Certified;
		Candidate.Summary.StabilityCoreHostCount =
			V3Summary.ExplicitCoreCellCount;
		Candidate.Summary.StabilityCoreBeltCount = V3Summary.SupportPlaneCount;
		Candidate.Summary.StabilityCoreTieCourseCount =
			V3Summary.SupportPlaneCount;
		Candidate.Summary.StabilityRootedExistingCourseCount =
			V3Summary.GroundSeatCount;
		Candidate.Summary.ReusedStabilityCoreMemberCount = 0;
		Candidate.Summary.InsertedStabilityCoreMemberCount =
			V3CoreMemberCount;
		Candidate.Summary.StabilityCoreNetMemberDelta =
			V3CoreMemberCount;
		Candidate.Summary.MaximumUnbracedCorePostSpanBeforeCM = 0.0f;
		Candidate.Summary.MaximumUnbracedCorePostSpanAfterCM =
			V3Summary.MaximumPostSegmentSpanCM;
		Candidate.Summary.StabilityCorePlanHash = V3Summary.CorePlanHash;
		Candidate.Summary.StabilityRootedEvidenceHash =
			V3Summary.SupportPlanHash;

		Candidate.Summary.bCoupledExteriorFrameCertified = bV3Certified;
		Candidate.Summary.CoupledExteriorFrameCellCount =
			V3Summary.ExplicitCoreCellCount;
		Candidate.Summary.CoupledExteriorFrameMacroBandCount =
			V3Summary.SupportPlaneCount;
		Candidate.Summary.CoupledExteriorFrameGroundedFaceMask =
			V3Summary.GroundedFaceMask;
		Candidate.Summary.CoupledExteriorFrameMaximumMemberSpanCM =
			V3Summary.MaximumMemberLengthCM;
		Candidate.Summary.CoupledExteriorFrameMaximumPostSegmentSpanCM =
			V3Summary.MaximumPostSegmentSpanCM;
		Candidate.Summary.CoupledExteriorFramePlanHash =
			V3Summary.SupportPlanHash;
		Candidate.Summary.CoupledExteriorFrameFinalGeometryHash =
			V3Summary.FinalGeometryHash;
		Candidate.Summary.CoupledExteriorFrameDAGEvidenceHash =
			BeamC.Summary.LoadDAGHash;
		Candidate.Summary.bPhysicalStabilityEvaluated = false;
		Candidate.Summary.GenerationStage =
			EABTSM73BeamC3GenerationStage::StaticDAG;
		Candidate.Summary.bStageStaticDAGEvaluated = true;

		Candidate.Summary.bSkeletonFirstCertified = bV3Certified;
		Candidate.Summary.bSkeletonFirstTimingEvaluated =
			V3Summary.bStage1TimingEvaluated;
		Candidate.Summary.bSkeletonFirstWithinTimeBudget =
			V3Summary.bStage1WithinTimeBudget;
		Candidate.Summary.SkeletonFirstTimeBudgetMilliseconds =
			V3Summary.Stage1TimeBudgetMilliseconds;
		Candidate.Summary.SkeletonFirstTotalMilliseconds =
			V3Summary.Stage1TotalMilliseconds;
		Candidate.Summary.SkeletonFirstTerminalDemandMilliseconds =
			V3Summary.TerminalDemandMilliseconds;
		Candidate.Summary.SkeletonFirstChildCandidateMilliseconds =
			V3Summary.ChildCandidateMilliseconds;
		Candidate.Summary.SkeletonFirstPodiumMainCandidateMilliseconds =
			V3Summary.PodiumMainCandidateMilliseconds;
		Candidate.Summary.SkeletonFirstJointSelectionMilliseconds =
			V3Summary.JointSelectionMilliseconds;
		Candidate.Summary.SkeletonFirstMemberEmissionMilliseconds =
			V3Summary.MemberEmissionMilliseconds;
		Candidate.Summary.SkeletonFirstStaticDAGMilliseconds =
			V3Summary.StaticDAGMilliseconds;
		Candidate.Summary.SkeletonFirstTimeoutPhase =
			V3Summary.Stage1TimeoutPhase;
		Candidate.Summary.SkeletonFirstGroundedComponentCount =
			V3Summary.GroundedComponentCount;
		Candidate.Summary.SkeletonFirstCoreCellCount =
			V3Summary.ExplicitCoreCellCount;
		Candidate.Summary.SkeletonFirstCoreMergeRegionCount =
			V3Summary.CoreMergeRegionCount;
		Candidate.Summary.SkeletonFirstMergedGroundComponentCount =
			V3Summary.MergedGroundComponentCount;
		Candidate.Summary.SkeletonFirstMaximumCoreRailCount =
			V3Summary.MaximumCoreRailCount;
		Candidate.Summary.SkeletonFirstCoreBearingPatchCountPerInterface =
			V3Summary.CoreBearingPatchCountPerInterface;
		Candidate.Summary.SkeletonFirstExplicitCoreCellCount =
			V3Summary.ExplicitCoreCellCount;
		Candidate.Summary.SkeletonFirstGroundedCoreCellCount =
			V3Summary.GroundedCoreCellCount;
		Candidate.Summary.SkeletonFirstSuspendedCoreCount =
			V3Summary.SuspendedCoreCount;
		Candidate.Summary.SkeletonFirstShellMemberCount =
			V3Summary.ShellMemberCount;
		Candidate.Summary.SkeletonFirstCoreDerivedShellMemberCount =
			V3Summary.CoreDerivedShellMemberCount;
		Candidate.Summary.SkeletonFirstSharedCourseCount =
			V3Summary.SharedCourseCount;
		Candidate.Summary.SkeletonFirstSharedCourseNonCoreEndpointViolationCount =
			V3Summary.SharedCourseNonCoreEndpointViolationCount;
		Candidate.Summary.SkeletonFirstSharedCourseReplacementSlotCount =
			V3Summary.SharedCourseReplacementSlotCount;
		Candidate.Summary.SkeletonFirstSharedCourseBandViolationCount =
			V3Summary.SharedCourseBandViolationCount;
		Candidate.Summary.SkeletonFirstBuildingGroupCount =
			V3Summary.BuildingGroupCount;
		Candidate.Summary.SkeletonFirstCommonShellMemberCount =
			V3Summary.CommonShellMemberCount;
		Candidate.Summary.SkeletonFirstCommonShellConnectedCoreCount =
			V3Summary.CommonShellConnectedCoreCount;
		Candidate.Summary.SkeletonFirstSupportPlaneCount =
			V3Summary.SupportPlaneCount;
		Candidate.Summary.SkeletonFirstVisibleFeatureCount =
			V3Summary.VisibleFeatureCount;
		Candidate.Summary.SkeletonFirstPlannedMemberCount =
			V3Summary.PlannedMemberCount;
		Candidate.Summary.SkeletonFirstEmittedMemberCount =
			V3Summary.EmittedMemberCount;
		Candidate.Summary.SkeletonFirstStructuralAttemptCount = 1;
		Candidate.Summary.SkeletonFirstCandidateSeed = AttemptSeed;
		Candidate.Summary.SkeletonFirstGroundedFaceMask =
			V3Summary.GroundedFaceMask;
		Candidate.Summary.SkeletonFirstMinimumExteriorPostStationsPerFace =
			V3Summary.MinimumGroundedExteriorPostStationsPerFace;
		Candidate.Summary.SkeletonFirstMaximumMemberSpanCM =
			V3Summary.MaximumMemberLengthCM;
		Candidate.Summary.SkeletonFirstMaximumPostSegmentSpanCM =
			V3Summary.MaximumPostSegmentSpanCM;
		Candidate.Summary.SkeletonFirstEnvelopeHash = V3Summary.EnvelopeHash;
		Candidate.Summary.SkeletonFirstCoreMergeRegionHash =
			V3Summary.CoreMergeRegionHash;
		Candidate.Summary.SkeletonFirstCorePlanHash = V3Summary.CorePlanHash;
		Candidate.Summary.SkeletonFirstSupportPlanHash =
			V3Summary.SupportPlanHash;
		Candidate.Summary.SkeletonFirstFinalGeometryHash =
			V3Summary.FinalGeometryHash;
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
			LogCandidateRejection(TEXT("AssemblyQuality"), LastFailure,
				Candidate.Summary.MemberCount, Candidate.Summary.BrickCount);
			return ABTSM73BeamD1::Reject(OutResult, OutError,
				FString::Printf(TEXT("BeamC3V3StructuralCandidateRejected:Attempt=%d:%s"),
					Attempt, *LastFailure));
		}
		if (!ABTSM73BeamD1::MeetsVisualMilestone(
			Profile, Silhouette, V3Summary.VisibleFeatureCount))
		{
			LastFailure = FString::Printf(
				TEXT("VisualMilestoneNotMet:Volumes=%d:Features=%d:Spans=%d:RequiredSpans=%d"),
				Silhouette.Summary.VolumeCount,
				V3Summary.VisibleFeatureCount,
				Silhouette.Summary.SupportedSpanCount,
				ABTSM73BeamD1::RequiredSupportedSpanCount(
					Silhouette.Summary.ResolvedArchetype,
					Profile.DifficultyTier));
			LogCandidateRejection(TEXT("VisualMilestone"), LastFailure,
				Candidate.Summary.MemberCount, Candidate.Summary.BrickCount);
			return ABTSM73BeamD1::Reject(OutResult, OutError,
				FString::Printf(TEXT("BeamC3V3StructuralCandidateRejected:Attempt=%d:%s"),
					Attempt, *LastFailure));
		}
		if (LastBrickCount < Target.MinimumBrickCount
			|| LastBrickCount > Target.MaximumBrickCount)
		{
			LastFailure = FString::Printf(
				TEXT("BrickCountOutsideTarget:%d:notin:%d..%d"),
				LastBrickCount, Target.MinimumBrickCount, Target.MaximumBrickCount);
			LogCandidateRejection(TEXT("BrickWindow"), LastFailure,
				Candidate.Summary.MemberCount, LastBrickCount);
			return ABTSM73BeamD1::Reject(OutResult, OutError,
				FString::Printf(TEXT("BeamC3V3StructuralCandidateRejected:Attempt=%d:%s"),
					Attempt, *LastFailure));
		}

		Candidate.Summary.TargetMinimumBrickCount =
			Target.MinimumBrickCount;
		Candidate.Summary.TargetMaximumBrickCount =
			Target.MaximumBrickCount;
		Candidate.Summary.VisualCandidateAttempt = Attempt;
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M7.3-Beam-C3V3][Stage1Certified]")
			TEXT(" Profile=%s Tier=%d BaseSeed=%d Attempt=%d CandidateSeed=%d")
			TEXT(" ResolvedHash=%lld EnvelopeHash=%lld CorePlanHash=%lld")
			TEXT(" SupportPlanHash=%lld FinalGeometryHash=%lld LoadDAGHash=%lld")
			TEXT(" Bricks=%d Grounded=%d Cores=%d GroundedCores=%d SuspendedCores=%d")
			TEXT(" Shell=%d CoreDerivedShell=%d Shared=%d SharedEndpointViolations=%d")
			TEXT(" SharedReplacementSlots=%d SharedBandViolations=%d")
			TEXT(" Groups=%d CommonShell=%d CommonConnectedCores=%d")
			TEXT(" Planes=%d Spans=%d Features=%d")
			TEXT(" Planned=%d Emitted=%d Faces=0x%02x MinFacePosts=%d")
			TEXT(" MaxMember=%.2f MaxPost=%.2f StructuralAttempts=1 Physical=NotEvaluated")
			TEXT(" ClosurePasses=%d ClosureAdded=%d Advisory=%d"),
			*Settings.GameplayProfileId.ToString(), Settings.DifficultyTier,
			Settings.BuildingSeed, Attempt, AttemptSeed,
			Profile.ResolvedSettingsHash,
			V3Summary.EnvelopeHash, V3Summary.CorePlanHash,
			V3Summary.SupportPlanHash, V3Summary.FinalGeometryHash,
			BeamC.Summary.LoadDAGHash, Candidate.Summary.BrickCount,
			V3Summary.GroundedComponentCount,
			V3Summary.ExplicitCoreCellCount,
			V3Summary.GroundedCoreCellCount,
			V3Summary.SuspendedCoreCount,
			V3Summary.ShellMemberCount,
			V3Summary.CoreDerivedShellMemberCount,
			V3Summary.SharedCourseCount,
			V3Summary.SharedCourseNonCoreEndpointViolationCount,
			V3Summary.SharedCourseReplacementSlotCount,
			V3Summary.SharedCourseBandViolationCount,
			V3Summary.BuildingGroupCount,
			V3Summary.CommonShellMemberCount,
			V3Summary.CommonShellConnectedCoreCount,
			V3Summary.SupportPlaneCount, V3Summary.SupportedSpanCount,
			V3Summary.VisibleFeatureCount,
			V3Summary.PlannedMemberCount, V3Summary.EmittedMemberCount,
			V3Summary.GroundedFaceMask,
			V3Summary.MinimumGroundedExteriorPostStationsPerFace,
			V3Summary.MaximumMemberLengthCM,
			V3Summary.MaximumPostSegmentSpanCM,
			Candidate.Summary.StructuralClosurePassCount,
			Candidate.Summary.AddedStructuralSupportPostCount,
			Candidate.Summary.SupportResultantAdvisoryCount);
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M7.3-Beam-D1][CandidateTiming]")
			TEXT(" Profile=%s Tier=%d Attempt=%d")
			TEXT(" TimingMs=Profile:%.2f,Shape:%.2f,C3V3:%.2f,BeamC:%.2f,Compile:%.2f,Total:%.2f")
			TEXT(" Stage1Ms=Demand:%.2f,Child:%.2f,Main:%.2f,Joint:%.2f,Emission:%.2f,DAG:%.2f,Total:%.2f,Budget:%.2f"),
			*Settings.GameplayProfileId.ToString(), Settings.DifficultyTier,
			Attempt, ProfileMilliseconds, SilhouetteMilliseconds,
			BeamC3V3Milliseconds, BeamCMilliseconds, CompileMilliseconds,
			(FPlatformTime::Seconds() - AttemptStartSeconds) * 1000.0,
			V3Summary.TerminalDemandMilliseconds,
			V3Summary.ChildCandidateMilliseconds,
			V3Summary.PodiumMainCandidateMilliseconds,
			V3Summary.JointSelectionMilliseconds,
			V3Summary.MemberEmissionMilliseconds,
			V3Summary.StaticDAGMilliseconds,
			V3Summary.Stage1TotalMilliseconds,
			V3Summary.Stage1TimeBudgetMilliseconds);
		Candidate.Summary.SemanticVolumeCount =
			Silhouette.Summary.VolumeCount;
		Candidate.Summary.SemanticBoxCount =
			Silhouette.Summary.BoxCount;
		Candidate.Summary.SemanticPrismCount =
			Silhouette.Summary.PrismCount;
		Candidate.Summary.SemanticPyramidCount =
			Silhouette.Summary.PyramidCount;
		Candidate.Summary.DistinctMotifCount =
			V3Summary.VisibleFeatureCount;
		Candidate.Summary.SupportedSpanCount =
			V3Summary.SupportedSpanCount;
		Candidate.Summary.bVisualComplexityCertified = true;
		OutResult = MoveTemp(Candidate);
		OutError.Reset();
		return true;
	}

	return ABTSM73BeamD1::Reject(
		OutResult,
		OutError,
		FString::Printf(
			TEXT("BeamD15NoSemanticVisualCandidate:Attempts=%d:Last=%s:Bricks=%d"),
			Target.MaximumCandidateAttempts,
			*LastFailure,
			LastBrickCount));
}

bool FABTSM73BeamD1BrickCompiler::GenerateStagePreview(
	const FABTSM73BeamD1Settings& Settings,
	const EABTSM73BeamC3GenerationStage StopStage,
	FABTSM73BeamD1StagePreviewResult& OutResult,
	FString& OutError) const
{
	OutResult = FABTSM73BeamD1StagePreviewResult();
	OutError.Reset();
	if (StopStage != EABTSM73BeamC3GenerationStage::SemanticEnvelope
		&& StopStage != EABTSM73BeamC3GenerationStage::CoreAndShared
		&& StopStage != EABTSM73BeamC3GenerationStage::CouplingCourses
		&& StopStage != EABTSM73BeamC3GenerationStage::CommonExteriorFrame)
	{
		OutError = FString::Printf(
			TEXT("BeamC3StageNotImplemented:Stage=%d"),
			static_cast<int32>(StopStage));
		OutResult.Summary.GenerationStage = StopStage;
		return false;
	}

	FABTSM73BeamD0ResolvedProfile InitialProfile;
	if (!FABTSM73BeamD0ProfileCatalog::GetDefault().Resolve(
		Settings.GameplayProfileId, Settings.DifficultyTier,
		Settings.BuildingSeed, InitialProfile, OutError))
	{
		OutError = FString::Printf(TEXT("BeamD1StagePreviewProfile:%s"), *OutError);
		return false;
	}

	FABTSM73BeamD0ResolvedProfile SelectedProfile;
	int32 SelectedAttempt = INDEX_NONE;
	int32 SelectedSeed = INDEX_NONE;
	int32 LastRejectedAttempt = INDEX_NONE;
	int32 LastRejectedSeed = INDEX_NONE;
	FString LastRejectedGate = TEXT("None");
	FString LastRejectedReason = TEXT("NoCandidateAttemptExecuted");
	int32 ProfileRejectCount = 0;
	int32 SilhouetteRejectCount = 0;
	int32 SemanticMilestoneRejectCount = 0;
	int32 BestSemanticAttempt = INDEX_NONE;
	int32 BestSemanticSeed = INDEX_NONE;
	int32 BestSemanticFailedPredicateCount = MAX_int32;
	int32 BestSemanticVolumeDeficit = MAX_int32;
	int32 BestSemanticSpanDeficit = MAX_int32;
	int32 BestSemanticRoofDeficit = MAX_int32;
	FString BestSemanticReason = TEXT("None");
	for (int32 Attempt = 0;
		Attempt < InitialProfile.VisualComplexity.MaximumCandidateAttempts;
		++Attempt)
	{
		const int32 AttemptSeed = ABTSM73BeamD1::CandidateSeed(
			Settings.BuildingSeed, Attempt);
		FString CandidateError;
		auto RecordRejection = [Attempt, AttemptSeed,
			&LastRejectedAttempt, &LastRejectedSeed,
			&LastRejectedGate, &LastRejectedReason](
			const TCHAR* Gate, const FString& Reason)
		{
			LastRejectedAttempt = Attempt;
			LastRejectedSeed = AttemptSeed;
			LastRejectedGate = Gate;
			LastRejectedReason = Reason.IsEmpty()
				? TEXT("UnspecifiedFailure") : Reason;
		};
		FABTSM73BeamD0ResolvedProfile Profile;
		if (!FABTSM73BeamD0ProfileCatalog::GetDefault().Resolve(
			Settings.GameplayProfileId, Settings.DifficultyTier,
			AttemptSeed, Profile, CandidateError))
		{
			++ProfileRejectCount;
			RecordRejection(TEXT("Profile"), CandidateError);
			continue;
		}
		FABTSM73DAG5BV2GenerationResult Silhouette;
		if (!FABTSM73DAG5BShapeGrammarV2().Generate(
			Profile.BeamSettings.BeamB.BeamA.Silhouette,
			Silhouette, CandidateError))
		{
			++SilhouetteRejectCount;
			RecordRejection(TEXT("Silhouette"), CandidateError);
			continue;
		}
		if (!ABTSM73BeamD1::MeetsSemanticVisualMilestone(Profile, Silhouette))
		{
			static constexpr int32 MinimumVolumes[6] = {3, 6, 8, 16, 21, 25};
			const int32 Tier = FMath::Clamp(Profile.DifficultyTier, 0, 5);
			const int32 RequiredVolumeCount =
				Profile.GameplayProfileId == TEXT("ColumnBreak") && Tier == 5
					? 16 : MinimumVolumes[Tier];
			const int32 RequiredSpans = ABTSM73BeamD1::RequiredSupportedSpanCount(
				Silhouette.Summary.ResolvedArchetype, Tier);
			const int32 RoofPrimitiveCount =
				Silhouette.Summary.PrismCount + Silhouette.Summary.PyramidCount;
			const FString Reason = FString::Printf(
				TEXT("SemanticVisualMilestoneNotMet:Volumes=%d:RequiredVolumes=%d")
				TEXT(":Spans=%d:RequiredSpans=%d:RoofPrimitives=%d")
				TEXT(":RequireSingleTerminalRoof=%d"),
				Silhouette.Summary.VolumeCount, RequiredVolumeCount,
				Silhouette.Summary.SupportedSpanCount, RequiredSpans,
				RoofPrimitiveCount,
				Profile.VisualComplexity.bRequireSingleTerminalRoof ? 1 : 0);
			++SemanticMilestoneRejectCount;
			RecordRejection(TEXT("SemanticVisualMilestone"), Reason);
			const int32 VolumeDeficit = FMath::Max(
				0, RequiredVolumeCount - Silhouette.Summary.VolumeCount);
			const int32 SpanDeficit = FMath::Max(
				0, RequiredSpans - Silhouette.Summary.SupportedSpanCount);
			const int32 RoofDeficit =
				Profile.VisualComplexity.bRequireSingleTerminalRoof
					&& RoofPrimitiveCount != 1 ? 1 : 0;
			const int32 FailedPredicateCount =
				(VolumeDeficit > 0 ? 1 : 0)
				+ (SpanDeficit > 0 ? 1 : 0)
				+ RoofDeficit;
			const bool bBetterSemanticCandidate =
				FailedPredicateCount < BestSemanticFailedPredicateCount
				|| (FailedPredicateCount == BestSemanticFailedPredicateCount
					&& VolumeDeficit < BestSemanticVolumeDeficit)
				|| (FailedPredicateCount == BestSemanticFailedPredicateCount
					&& VolumeDeficit == BestSemanticVolumeDeficit
					&& SpanDeficit < BestSemanticSpanDeficit)
				|| (FailedPredicateCount == BestSemanticFailedPredicateCount
					&& VolumeDeficit == BestSemanticVolumeDeficit
					&& SpanDeficit == BestSemanticSpanDeficit
					&& RoofDeficit < BestSemanticRoofDeficit);
			if (bBetterSemanticCandidate)
			{
				BestSemanticAttempt = Attempt;
				BestSemanticSeed = AttemptSeed;
				BestSemanticFailedPredicateCount = FailedPredicateCount;
				BestSemanticVolumeDeficit = VolumeDeficit;
				BestSemanticSpanDeficit = SpanDeficit;
				BestSemanticRoofDeficit = RoofDeficit;
				BestSemanticReason = Reason;
			}
			continue;
		}
		SelectedProfile = MoveTemp(Profile);
		OutResult.Silhouette = MoveTemp(Silhouette);
		SelectedAttempt = Attempt;
		SelectedSeed = AttemptSeed;
		break;
	}
	if (SelectedAttempt == INDEX_NONE)
	{
		OutError = FString::Printf(
			TEXT("BeamC3StagePreviewNoSemanticCandidate:Attempts=%d")
			TEXT(":RejectCounts=Profile%d,Silhouette%d,SemanticMilestone%d")
			TEXT(":LastAttempt=%d:LastCandidateSeed=%d:LastGate=%s:LastReason=%s")
			TEXT(":BestSemanticAttempt=%d:BestSemanticCandidateSeed=%d")
			TEXT(":BestSemanticReason=%s"),
			InitialProfile.VisualComplexity.MaximumCandidateAttempts,
			ProfileRejectCount, SilhouetteRejectCount,
			SemanticMilestoneRejectCount,
			LastRejectedAttempt, LastRejectedSeed,
			*LastRejectedGate, *LastRejectedReason,
			BestSemanticAttempt, BestSemanticSeed, *BestSemanticReason);
		return false;
	}

	FABTSM73BeamD1Summary& Summary = OutResult.Summary;
	Summary.GenerationStage = StopStage;
	Summary.GameplayProfileId = Settings.GameplayProfileId;
	Summary.DifficultyTier = Settings.DifficultyTier;
	Summary.ResolvedM7ProfileId = SelectedProfile.ResolvedM7ProfileId;
	Summary.VisualCandidateAttempt = SelectedAttempt;
	Summary.SemanticVolumeCount = OutResult.Silhouette.Summary.VolumeCount;
	Summary.SemanticBoxCount = OutResult.Silhouette.Summary.BoxCount;
	Summary.SemanticPrismCount = OutResult.Silhouette.Summary.PrismCount;
	Summary.SemanticPyramidCount = OutResult.Silhouette.Summary.PyramidCount;
	Summary.SupportedSpanCount = OutResult.Silhouette.Summary.SupportedSpanCount;
	Summary.bPhysicalStabilityEvaluated = false;
	if (!ABTSM73BeamD1::AppendRaisedMainReservations(
		SelectedProfile, OutResult.Silhouette, OutError))
	{
		return false;
	}
	Summary.SemanticVolumeCount = OutResult.Silhouette.Summary.VolumeCount;
	Summary.SemanticBoxCount = OutResult.Silhouette.Summary.BoxCount;
	if (StopStage == EABTSM73BeamC3GenerationStage::SemanticEnvelope)
	{
		Summary.SkeletonFirstEnvelopeHash =
			FABTSM73BeamC3V3SkeletonFirstGenerator()
				.ComputeEnvelopeHashForDiagnostics(OutResult.Silhouette);
		Summary.bAccepted = true;
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M7.3-Beam-C3V3][Stage0Stopped]")
			TEXT(" Profile=%s Tier=%d BaseSeed=%d Attempt=%d CandidateSeed=%d")
			TEXT(" GrammarHash=%lld WFCHash=%lld EnvelopeHash=%lld ResultHash=%lld Volumes=%d")
			TEXT(" Physical=NotEvaluated"),
			*Settings.GameplayProfileId.ToString(), Settings.DifficultyTier,
			Settings.BuildingSeed, SelectedAttempt, SelectedSeed,
			OutResult.Silhouette.Summary.GrammarHash,
			OutResult.Silhouette.Summary.WFCHash,
			Summary.SkeletonFirstEnvelopeHash,
			OutResult.Silhouette.Summary.ResultHash,
			OutResult.Silhouette.Volumes.Num());
		return true;
	}

	FABTSM73BeamC3V3SkeletonFirstGenerator SkeletonGenerator;
	const bool bSkeletonGenerated =
		StopStage == EABTSM73BeamC3GenerationStage::CommonExteriorFrame
			? SkeletonGenerator.GenerateStage3(SelectedProfile,
				OutResult.Silhouette, OutResult.Skeleton, OutError)
			: StopStage == EABTSM73BeamC3GenerationStage::CouplingCourses
				? SkeletonGenerator.GenerateStage2(SelectedProfile,
					OutResult.Silhouette, OutResult.Skeleton, OutError)
				: SkeletonGenerator.GenerateStage1(SelectedProfile,
					OutResult.Silhouette, OutResult.Skeleton, OutError);
	if (!bSkeletonGenerated)
	{
		OutError = FString::Printf(TEXT("BeamC3Stage%d:%s"),
			StopStage == EABTSM73BeamC3GenerationStage::CommonExteriorFrame ? 3
				: StopStage == EABTSM73BeamC3GenerationStage::CouplingCourses ? 2 : 1,
			*OutError);
		return false;
	}
	ABTSM73BeamC3V3::FPlanSummary& Stage1 =
		OutResult.Skeleton.Plan.Summary;
	const bool bStage2 = StopStage
		== EABTSM73BeamC3GenerationStage::CouplingCourses;
	const bool bStage3 = StopStage
		== EABTSM73BeamC3GenerationStage::CommonExteriorFrame;
	if (!bStage2 && !bStage3)
	{
		const double StaticDAGStartSeconds = FPlatformTime::Seconds();
		const bool bStaticDAGGenerated = FABTSM73BeamCGenerator().Generate(
			SelectedProfile.BeamSettings, OutResult.Skeleton.Assembly,
			OutResult.StaticDAG, OutError);
		Stage1.StaticDAGMilliseconds =
			(FPlatformTime::Seconds() - StaticDAGStartSeconds) * 1000.0;
		Stage1.Stage1TotalMilliseconds += Stage1.StaticDAGMilliseconds;
		if (!bStaticDAGGenerated)
		{
			OutError = FString::Printf(TEXT("BeamC3Stage1StaticDAG:%s"), *OutError);
			return false;
		}
	}
	else
	{
		// Stages 2/3 stop before floor/infill/roof. Their generators already rebuild
		// real contacts and prove the stage-local ground-reachable seat DAG; complete
		// Beam-C resultants remain deferred until the complete static building.
		Stage1.StaticDAGMilliseconds = 0.0;
	}
	Stage1.bStage1TimingEvaluated = true;
	if (Stage1.Stage1TotalMilliseconds
		> ABTSM73BeamC3V3::Stage1LeafTimeBudgetMilliseconds)
	{
		Stage1.bStage1WithinTimeBudget = false;
		Stage1.Stage1TimeoutPhase = TEXT("StaticDAG");
		OutError = FString::Printf(
			TEXT("BeamC3Stage1:BeamC3V3Stage1Timeout:Phase=StaticDAG:ElapsedMs=%.3f:BudgetMs=%.3f:TimingMs=Demand:%.3f,Child:%.3f,Main:%.3f,Joint:%.3f,Emission:%.3f,DAG:%.3f"),
			Stage1.Stage1TotalMilliseconds,
			ABTSM73BeamC3V3::Stage1LeafTimeBudgetMilliseconds,
			Stage1.TerminalDemandMilliseconds,
			Stage1.ChildCandidateMilliseconds,
			Stage1.PodiumMainCandidateMilliseconds,
			Stage1.JointSelectionMilliseconds,
			Stage1.MemberEmissionMilliseconds,
			Stage1.StaticDAGMilliseconds);
		Stage1.RejectReason = OutError;
		return false;
	}
	Stage1.bStage1WithinTimeBudget = true;
	if (!bStage2 && !bStage3
		&& (OutResult.StaticDAG.Summary.StructuralClosurePassCount != 0
		|| OutResult.StaticDAG.Summary.AddedStructuralSupportPostCount != 0
		|| OutResult.StaticDAG.Summary.SupportResultantAdvisoryCount != 0))
	{
		OutError = FString::Printf(
			TEXT("BeamC3Stage1StaticDAGMutatedOrAdvisory:Passes=%d:Added=%d:Advisory=%d"),
			OutResult.StaticDAG.Summary.StructuralClosurePassCount,
			OutResult.StaticDAG.Summary.AddedStructuralSupportPostCount,
			OutResult.StaticDAG.Summary.SupportResultantAdvisoryCount);
		return false;
	}
	Summary.bAccepted = true;
	Summary.bStageStaticDAGEvaluated = true;
	Summary.MemberCount = OutResult.Skeleton.Assembly.Members.Num();
	Summary.BrickCount = Summary.MemberCount;
	Summary.CompleteReferenceCount = Summary.MemberCount;
	Summary.bStabilityCoreCertified = true;
	Summary.StabilityCoreHostCount = Stage1.ExplicitCoreCellCount;
	Summary.StabilityCoreBeltCount = Stage1.SupportPlaneCount;
	Summary.StabilityCoreTieCourseCount = Stage1.SupportPlaneCount;
	Summary.InsertedStabilityCoreMemberCount = Stage1.PlannedMemberCount;
	Summary.StabilityCoreNetMemberDelta = Stage1.PlannedMemberCount;
	Summary.StabilityCorePlanHash = Stage1.CorePlanHash;
	Summary.StabilityRootedEvidenceHash = Stage1.SupportPlanHash;
	Summary.bSkeletonFirstCertified = true;
	Summary.bSkeletonFirstTimingEvaluated = Stage1.bStage1TimingEvaluated;
	Summary.bSkeletonFirstWithinTimeBudget = Stage1.bStage1WithinTimeBudget;
	Summary.SkeletonFirstTimeBudgetMilliseconds = Stage1.Stage1TimeBudgetMilliseconds;
	Summary.SkeletonFirstTotalMilliseconds = Stage1.Stage1TotalMilliseconds;
	Summary.SkeletonFirstTerminalDemandMilliseconds =
		Stage1.TerminalDemandMilliseconds;
	Summary.SkeletonFirstChildCandidateMilliseconds =
		Stage1.ChildCandidateMilliseconds;
	Summary.SkeletonFirstPodiumMainCandidateMilliseconds =
		Stage1.PodiumMainCandidateMilliseconds;
	Summary.SkeletonFirstJointSelectionMilliseconds =
		Stage1.JointSelectionMilliseconds;
	Summary.SkeletonFirstMemberEmissionMilliseconds =
		Stage1.MemberEmissionMilliseconds;
	Summary.SkeletonFirstStaticDAGMilliseconds = Stage1.StaticDAGMilliseconds;
	Summary.SkeletonFirstTimeoutPhase = Stage1.Stage1TimeoutPhase;
	Summary.SkeletonFirstGroundedComponentCount = Stage1.GroundedComponentCount;
	Summary.SkeletonFirstSemanticSupportNodeCount =
		Stage1.SemanticSupportNodeCount;
	Summary.SkeletonFirstSemanticSupportLedgerCount =
		Stage1.SemanticSupportLedgerCount;
	Summary.SkeletonFirstSemanticTerminalDemandCount =
		Stage1.SemanticTerminalDemandCount;
	Summary.SkeletonFirstSemanticTerminalDemandWithoutContinuousFitCount =
		Stage1.SemanticTerminalDemandWithoutContinuousFitCount;
	Summary.SkeletonFirstSemanticSupportDemandHash =
		Stage1.SemanticSupportDemandHash;
	Summary.SkeletonFirstSemanticDemandCoreBindingCount =
		Stage1.SemanticDemandCoreBindingCount;
	Summary.SkeletonFirstUnmappedSemanticDemandCount =
		Stage1.UnmappedSemanticDemandCount;
	Summary.SkeletonFirstAmbiguousSemanticDemandCount =
		Stage1.AmbiguousSemanticDemandCount;
	Summary.SkeletonFirstSemanticDemandChildOutsideBodyCount =
		Stage1.SemanticDemandChildOutsideBodyCount;
	Summary.SkeletonFirstSemanticDemandChildWithoutDirectMainCouplingCount =
		Stage1.SemanticDemandChildWithoutDirectMainCouplingCount;
	Summary.SkeletonFirstReusedTowerChildBindingCount =
		Stage1.ReusedTowerChildBindingCount;
	Summary.SkeletonFirstUnreferencedTowerChildCount =
		Stage1.UnreferencedTowerChildCount;
	Summary.SkeletonFirstSemanticDemandCoreBindingHash =
		Stage1.SemanticDemandCoreBindingHash;
	Summary.SkeletonFirstSupportProvinceCount = Stage1.SupportProvinceCount;
	Summary.SkeletonFirstMultiDemandSupportProvinceCount =
		Stage1.MultiDemandSupportProvinceCount;
	Summary.SkeletonFirstSupportProvinceGroundCellCount =
		Stage1.SupportProvinceGroundCellCount;
	Summary.SkeletonFirstSupportProvinceBoundaryCount =
		Stage1.SupportProvinceBoundaryCount;
	Summary.SkeletonFirstSupportProvinceTieBreakCellCount =
		Stage1.SupportProvinceTieBreakCellCount;
	Summary.SkeletonFirstSupportProvinceNearestSeedFallbackCount =
		Stage1.SupportProvinceNearestSeedFallbackCount;
	Summary.SkeletonFirstBoundSupportProvinceCount =
		Stage1.BoundSupportProvinceCount;
	Summary.SkeletonFirstDistinctProvinceGroundCoreCount =
		Stage1.DistinctProvinceGroundCoreCount;
	Summary.SkeletonFirstSupportProvinceHash = Stage1.SupportProvinceHash;
	Summary.SkeletonFirstSupportProvinceMainBindingHash =
		Stage1.SupportProvinceMainBindingHash;
	Summary.SkeletonFirstCoreCellCount = Stage1.CoreCellCount;
	Summary.SkeletonFirstCoreMergeRegionCount = Stage1.CoreMergeRegionCount;
	Summary.SkeletonFirstMergedGroundComponentCount =
		Stage1.MergedGroundComponentCount;
	Summary.SkeletonFirstMaximumCoreRailCount = Stage1.MaximumCoreRailCount;
	Summary.SkeletonFirstCoreBearingPatchCountPerInterface =
		Stage1.CoreBearingPatchCountPerInterface;
	Summary.SkeletonFirstExplicitCoreCellCount = Stage1.ExplicitCoreCellCount;
	Summary.SkeletonFirstGroundedCoreCellCount = Stage1.GroundedCoreCellCount;
	Summary.SkeletonFirstSuspendedCoreCount = Stage1.SuspendedCoreCount;
	Summary.SkeletonFirstShellMemberCount = Stage1.ShellMemberCount;
	Summary.SkeletonFirstCoreDerivedShellMemberCount =
		Stage1.CoreDerivedShellMemberCount;
	Summary.SkeletonFirstSharedCourseCount = Stage1.SharedCourseCount;
	Summary.SkeletonFirstSharedCourseNonCoreEndpointViolationCount =
		Stage1.SharedCourseNonCoreEndpointViolationCount;
	Summary.SkeletonFirstSharedCourseReplacementSlotCount =
		Stage1.SharedCourseReplacementSlotCount;
	Summary.SkeletonFirstSharedCourseBandViolationCount =
		Stage1.SharedCourseBandViolationCount;
	Summary.SkeletonFirstBuildingGroupCount = Stage1.BuildingGroupCount;
	Summary.SkeletonFirstCommonShellMemberCount = Stage1.CommonShellMemberCount;
	Summary.SkeletonFirstCommonShellConnectedCoreCount =
		Stage1.CommonShellConnectedCoreCount;
	Summary.SkeletonFirstSupportPlaneCount = Stage1.SupportPlaneCount;
	Summary.SkeletonFirstVisibleFeatureCount = Stage1.VisibleFeatureCount;
	Summary.SkeletonFirstPlannedMemberCount = Stage1.PlannedMemberCount;
	Summary.SkeletonFirstEmittedMemberCount = Stage1.EmittedMemberCount;
	Summary.SkeletonFirstStructuralAttemptCount = 1;
	Summary.SkeletonFirstCandidateSeed = SelectedSeed;
	Summary.SkeletonFirstMaximumMemberSpanCM = Stage1.MaximumMemberLengthCM;
	Summary.SkeletonFirstMaximumPostSegmentSpanCM =
		Stage1.MaximumPostSegmentSpanCM;
	Summary.SkeletonFirstEnvelopeHash = Stage1.EnvelopeHash;
	Summary.SkeletonFirstCoreMergeRegionHash = Stage1.CoreMergeRegionHash;
	Summary.SkeletonFirstCorePlanHash = Stage1.CorePlanHash;
	Summary.SkeletonFirstSupportPlanHash = Stage1.SupportPlanHash;
	Summary.SkeletonFirstFinalGeometryHash = Stage1.FinalGeometryHash;
	Summary.SkeletonFirstStage1InputGeometryHash =
		Stage1.Stage1InputGeometryHash;
	Summary.SkeletonFirstCouplingCourseCount = Stage1.CouplingCourseCount;
	Summary.SkeletonFirstCouplingFaceMask = Stage1.CouplingFaceMask;
	Summary.SkeletonFirstCouplingParentViolationCount =
		Stage1.CouplingParentViolationCount;
	Summary.SkeletonFirstCouplingEndpointViolationCount =
		Stage1.CouplingEndpointViolationCount;
	Summary.SkeletonFirstCouplingOutwardViolationCount =
		Stage1.CouplingOutwardViolationCount;
	Summary.SkeletonFirstCouplingOtherCoreViolationCount =
		Stage1.CouplingOtherCoreViolationCount;
	Summary.SkeletonFirstCouplingBandEndpointViolationCount =
		Stage1.CouplingBandEndpointViolationCount;
	Summary.SkeletonFirstResolvedFacadeEnvelopeVolumeCount =
		Stage1.ResolvedFacadeEnvelopeVolumeCount;
	Summary.SkeletonFirstResolvedFacadeEnvelopeRaisedVolumeCount =
		Stage1.ResolvedFacadeEnvelopeRaisedVolumeCount;
	Summary.SkeletonFirstResolvedFacadeEnvelopeBindingViolationCount =
		Stage1.ResolvedFacadeEnvelopeBindingViolationCount;
	Summary.SkeletonFirstResolvedFacadeEnvelopeHash =
		Stage1.ResolvedFacadeEnvelopeHash;
	Summary.SkeletonFirstStage2InputFacadeEnvelopeHash =
		Stage1.Stage2InputFacadeEnvelopeHash;
	Summary.SkeletonFirstFacadePartitionCount = Stage1.FacadePartitionCount;
	Summary.SkeletonFirstFacadePartitionWithPerimeterCoreCount =
		Stage1.FacadePartitionWithPerimeterCoreCount;
	Summary.SkeletonFirstFacadePartitionWithHeightAnchorCount =
		Stage1.FacadePartitionWithHeightAnchorCount;
	Summary.SkeletonFirstDeferredFacadePartitionCount =
		Stage1.DeferredFacadePartitionCount;
	Summary.SkeletonFirstDeferredNoCoursePairPartitionCount =
		Stage1.DeferredNoCoursePairPartitionCount;
	Summary.SkeletonFirstDeferredNoEligibleCorePartitionCount =
		Stage1.DeferredNoEligibleCorePartitionCount;
	Summary.SkeletonFirstDeferredNoFreeCrossStationPartitionCount =
		Stage1.DeferredNoFreeCrossStationPartitionCount;
	Summary.SkeletonFirstDeferredNoStage1BearingPartitionCount =
		Stage1.DeferredNoStage1BearingPartitionCount;
	Summary.SkeletonFirstDeferredNoFacadeTargetPartitionCount =
		Stage1.DeferredNoFacadeTargetPartitionCount;
	Summary.SkeletonFirstDeferredLengthLimitPartitionCount =
		Stage1.DeferredLengthLimitPartitionCount;
	Summary.SkeletonFirstDeferredNotOutwardPartitionCount =
		Stage1.DeferredNotOutwardPartitionCount;
	Summary.SkeletonFirstDeferredEnvelopeGapPartitionCount =
		Stage1.DeferredEnvelopeGapPartitionCount;
	Summary.SkeletonFirstDeferredOtherCoreBlockedPartitionCount =
		Stage1.DeferredOtherCoreBlockedPartitionCount;
	Summary.SkeletonFirstDeferredProtectedVoidPartitionCount =
		Stage1.DeferredProtectedVoidPartitionCount;
	Summary.SkeletonFirstDeferredMemberCollisionPartitionCount =
		Stage1.DeferredMemberCollisionPartitionCount;
	Summary.SkeletonFirstDeferredExhaustedCandidatePartitionCount =
		Stage1.DeferredExhaustedCandidatePartitionCount;
	Summary.SkeletonFirstFacadeHeightAnchorBandCount =
		Stage1.FacadeHeightAnchorBandCount;
	Summary.SkeletonFirstFacadePartitionBindingViolationCount =
		Stage1.FacadePartitionBindingViolationCount;
	Summary.SkeletonFirstPerimeterCoreCount = Stage1.PerimeterCoreCount;
	Summary.SkeletonFirstPerimeterCoreFaceCount = Stage1.PerimeterCoreFaceCount;
	Summary.SkeletonFirstPerimeterFaceExposureSpanCount =
		Stage1.PerimeterFaceExposureSpanCount;
	Summary.SkeletonFirstStage2PlanHash = Stage1.Stage2PlanHash;
	Summary.bSkeletonFirstStage2TimingEvaluated = Stage1.bStage2TimingEvaluated;
	Summary.SkeletonFirstStage2FacadeEnvelopeMilliseconds =
		Stage1.Stage2FacadeEnvelopeMilliseconds;
	Summary.SkeletonFirstStage2FacadeExtractionMilliseconds =
		Stage1.Stage2FacadeExtractionMilliseconds;
	Summary.SkeletonFirstStage2AnchorSearchMilliseconds =
		Stage1.Stage2AnchorSearchMilliseconds;
	Summary.SkeletonFirstStage2MemberEmissionMilliseconds =
		Stage1.Stage2MemberEmissionMilliseconds;
	Summary.SkeletonFirstStage2StaticDAGMilliseconds =
		Stage1.Stage2StaticDAGMilliseconds;
	Summary.SkeletonFirstStage2TotalMilliseconds =
		Stage1.Stage2TotalMilliseconds;
	Summary.StructuralClosurePassCount =
		OutResult.StaticDAG.Summary.StructuralClosurePassCount;
	Summary.AddedStructuralSupportPostCount =
		OutResult.StaticDAG.Summary.AddedStructuralSupportPostCount;
	Summary.RealContactMismatchCount =
		OutResult.StaticDAG.Summary.RealContactMismatchCount;
	Summary.RemainingSupportViolationCount =
		OutResult.StaticDAG.Summary.SupportResultantViolationCount
		+ OutResult.StaticDAG.Summary.SupportSpreadViolationCount
		+ OutResult.StaticDAG.Summary.SpanViolationCount
		+ OutResult.StaticDAG.Summary.CantileverViolationCount;
	Summary.SupportResultantAdvisoryCount =
		OutResult.StaticDAG.Summary.SupportResultantAdvisoryCount;
	Summary.BrickGeometryHash = Stage1.FinalGeometryHash;
	for (const ABTSM73BeamC3V3::FSupportProvinceDiagnostic& Province
		: OutResult.Skeleton.Plan.SupportProvinces)
	{
		FString DemandList;
		for (const int32 DemandId : Province.DemandIds)
		{
			DemandList += DemandList.IsEmpty()
				? FString::FromInt(DemandId)
				: FString::Printf(TEXT(",%d"), DemandId);
		}
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M7.3-Beam-C3V3][SupportProvince]")
			TEXT(" Province=%d Component=%d SeedDemand=%d Demands=%s")
			TEXT(" Cells=%d Ties=%d Neighbors=%d FullyOccupiedTop=%d ProposedPodiumTop=%d RequiredTop=%d")
			TEXT(" Anchor=%d,%d GroundCore=%d AnchorCovered=%d PodiumMain=%d")
			TEXT(" SyntheticGroundOnly=%d NearestSeedFallback=%d"),
			Province.ProvinceId, Province.ComponentId,
			Province.StableSeedDemandId, *DemandList,
			Province.GroundCellCount, Province.TieBreakCellCount,
			Province.AdjacentProvinceIds.Num(),
			Province.HighestFullyOccupiedTopCourse,
			Province.ProposedPodiumTopCourse,
			Province.MinimumRequiredTopCourse,
			Province.AnchorXUnit, Province.AnchorYUnit,
			Province.BoundGroundCoreCellId,
			Province.bAnchorCoveredByBoundCore ? 1 : 0,
			Province.bBoundToPodiumMain ? 1 : 0,
			Province.bSyntheticGroundOnly ? 1 : 0,
			Province.bUsedNearestGroundSeed ? 1 : 0);
	}
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7.3-Beam-C3V3][StageStopped]")
		TEXT(" Stage=%d")
		TEXT(" Profile=%s Tier=%d BaseSeed=%d Attempt=%d CandidateSeed=%d")
		TEXT(" GrammarHash=%lld WFCHash=%lld EnvelopeHash=%lld Stage1Hash=%lld")
		TEXT(" Volumes=%d SupportNodes=%d LoadBranches=%d MultiBranchBodies=%d UnrepresentedBranches=%d SemanticDemands=%d MergeLedger=%d SupportDemandHash=%lld")
		TEXT(" DemandCoreRows=%d UnmappedDemands=%d AmbiguousDemands=%d ChildOutsideBody=%d ChildWithoutDirectMain=%d ReusedChildren=%d OrphanChildren=%d DemandCoreHash=%lld")
		TEXT(" Provinces=%d MultiDemandProvinces=%d ProvinceCells=%d ProvinceBoundaries=%d ProvinceTies=%d ProvinceFallbacks=%d ProvinceHash=%lld BoundProvinces=%d ProvinceGroundCores=%d ProvinceMainBindingHash=%lld")
		TEXT(" Cores=%d Main=%d Children=%d HighRegions=%d BoundHigh=%d PairIntents=%d Shared=%d Members=%d")
		TEXT(" CouplingCourses=%d CouplingFaces=%u CouplingOutwardViolations=%d CouplingOtherCoreViolations=%d CouplingBandEndpointViolations=%d FacadeEnvelopeVolumes=%d RaisedFacadeVolumes=%d FacadeEnvelopeBindingViolations=%d FacadeEnvelopeHash=%lld Stage2FacadeInputHash=%lld FacadePartitions=%d PartitionPerimeter=%d PartitionAnchored=%d DeferredPartitions=%d DeferredReasons=Course:%d,Core:%d,Station:%d,Bearing:%d,Target:%d,Length:%d,Outward:%d,Envelope:%d,OtherCore:%d,Void:%d,Collision:%d,Exhausted:%d HeightAnchorBands=%d PartitionBindingViolations=%d PerimeterCores=%d PerimeterFaces=%d PerimeterExposureSpans=%d Stage1InputHash=%lld Stage2Hash=%lld")
		TEXT(" StageLocalDAG=Accepted CompleteBeamC=%s LoadDAGHash=%lld Physical=NotEvaluated")
		TEXT(" TimingMs=Demand:%.2f,Child:%.2f,Main:%.2f,Joint:%.2f,Emission:%.2f,DAG:%.2f,Total:%.2f,Budget:%.2f Stage2TimingMs=Envelope:%.2f,Facade:%.2f,Search:%.2f,Emission:%.2f,DAG:%.2f,Total:%.2f"),
		static_cast<int32>(StopStage),
		*Settings.GameplayProfileId.ToString(), Settings.DifficultyTier,
		Settings.BuildingSeed, SelectedAttempt, SelectedSeed,
		OutResult.Silhouette.Summary.GrammarHash,
		OutResult.Silhouette.Summary.WFCHash, Stage1.EnvelopeHash,
		Stage1.FinalGeometryHash, OutResult.Silhouette.Volumes.Num(),
		Stage1.SemanticSupportNodeCount,
		Stage1.SemanticTerminalLoadBranchCount,
		Stage1.MultiBranchTerminalBodyCount,
		Stage1.UnrepresentedSemanticTerminalLoadBranchCount,
		Stage1.SemanticTerminalDemandCount,
		Stage1.SemanticSupportLedgerCount,
		Stage1.SemanticSupportDemandHash,
		Stage1.SemanticDemandCoreBindingCount,
		Stage1.UnmappedSemanticDemandCount,
		Stage1.AmbiguousSemanticDemandCount,
		Stage1.SemanticDemandChildOutsideBodyCount,
		Stage1.SemanticDemandChildWithoutDirectMainCouplingCount,
		Stage1.ReusedTowerChildBindingCount,
		Stage1.UnreferencedTowerChildCount,
		Stage1.SemanticDemandCoreBindingHash,
		Stage1.SupportProvinceCount,
		Stage1.MultiDemandSupportProvinceCount,
		Stage1.SupportProvinceGroundCellCount,
		Stage1.SupportProvinceBoundaryCount,
		Stage1.SupportProvinceTieBreakCellCount,
		Stage1.SupportProvinceNearestSeedFallbackCount,
		Stage1.SupportProvinceHash,
		Stage1.BoundSupportProvinceCount,
		Stage1.DistinctProvinceGroundCoreCount,
		Stage1.SupportProvinceMainBindingHash,
		Stage1.ExplicitCoreCellCount,
		Stage1.PodiumMainCoreCellCount, Stage1.TowerChildCoreCellCount,
		Stage1.HighProjectionRegionCount,
		Stage1.BoundHighProjectionRegionCount,
		OutResult.Skeleton.Plan.SharedCourseIntents.Num(),
		Stage1.SharedCourseCount, Stage1.PlannedMemberCount,
		Stage1.CouplingCourseCount, Stage1.CouplingFaceMask,
		Stage1.CouplingOutwardViolationCount,
		Stage1.CouplingOtherCoreViolationCount,
		Stage1.CouplingBandEndpointViolationCount,
		Stage1.ResolvedFacadeEnvelopeVolumeCount,
		Stage1.ResolvedFacadeEnvelopeRaisedVolumeCount,
		Stage1.ResolvedFacadeEnvelopeBindingViolationCount,
		Stage1.ResolvedFacadeEnvelopeHash,
		Stage1.Stage2InputFacadeEnvelopeHash,
		Stage1.FacadePartitionCount,
		Stage1.FacadePartitionWithPerimeterCoreCount,
		Stage1.FacadePartitionWithHeightAnchorCount,
		Stage1.DeferredFacadePartitionCount,
		Stage1.DeferredNoCoursePairPartitionCount,
		Stage1.DeferredNoEligibleCorePartitionCount,
		Stage1.DeferredNoFreeCrossStationPartitionCount,
		Stage1.DeferredNoStage1BearingPartitionCount,
		Stage1.DeferredNoFacadeTargetPartitionCount,
		Stage1.DeferredLengthLimitPartitionCount,
		Stage1.DeferredNotOutwardPartitionCount,
		Stage1.DeferredEnvelopeGapPartitionCount,
		Stage1.DeferredOtherCoreBlockedPartitionCount,
		Stage1.DeferredProtectedVoidPartitionCount,
		Stage1.DeferredMemberCollisionPartitionCount,
		Stage1.DeferredExhaustedCandidatePartitionCount,
		Stage1.FacadeHeightAnchorBandCount,
		Stage1.FacadePartitionBindingViolationCount,
		Stage1.PerimeterCoreCount, Stage1.PerimeterCoreFaceCount,
		Stage1.PerimeterFaceExposureSpanCount,
		Stage1.Stage1InputGeometryHash, Stage1.Stage2PlanHash,
		bStage2 ? TEXT("NotEvaluated") : TEXT("Accepted"),
		OutResult.StaticDAG.Summary.LoadDAGHash,
		Stage1.TerminalDemandMilliseconds,
		Stage1.ChildCandidateMilliseconds,
		Stage1.PodiumMainCandidateMilliseconds,
		Stage1.JointSelectionMilliseconds,
		Stage1.MemberEmissionMilliseconds,
		Stage1.StaticDAGMilliseconds,
		Stage1.Stage1TotalMilliseconds,
		Stage1.Stage1TimeBudgetMilliseconds,
		Stage1.Stage2FacadeEnvelopeMilliseconds,
		Stage1.Stage2FacadeExtractionMilliseconds,
		Stage1.Stage2AnchorSearchMilliseconds,
		Stage1.Stage2MemberEmissionMilliseconds,
		Stage1.Stage2StaticDAGMilliseconds,
		Stage1.Stage2TotalMilliseconds);
	return true;
}

bool FABTSM73BeamD1BrickCompiler::CompileResolved(
	const FABTSM73BeamD0ResolvedProfile& Profile,
	const FABTSM73BeamBGenerationResult& BeamB,
	const FABTSM73BeamCGenerationResult& BeamC,
	FABTSM73BeamD1GenerationResult& OutResult,
	FString& OutError) const
{
	if (!BeamB.Summary.bAccepted)
	{
		OutResult = FABTSM73BeamD1GenerationResult();
		return ABTSM73BeamD1::Reject(
			OutResult, OutError, TEXT("BeamD1UpstreamRejected"));
	}
	return CompileResolvedAssembly(
		Profile, BeamB.ClosedAssembly, BeamB.Summary.ResultHash,
		BeamC, OutResult, OutError);
}

bool FABTSM73BeamD1BrickCompiler::CompileResolvedAssembly(
	const FABTSM73BeamD0ResolvedProfile& Profile,
	const FABTSM73BeamAGenerationResult& Assembly,
	const int64 UpstreamHash,
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
	Summary.UpstreamBeamHash = UpstreamHash;
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

	if (!Profile.bAccepted || !Assembly.Summary.bAccepted
		|| !BeamC.Summary.bAccepted)
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
		if (Brick.bWeaknessCandidate)
		{
			Summary.WeaknessCandidateMemberRole = Member.Role;
		}
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
