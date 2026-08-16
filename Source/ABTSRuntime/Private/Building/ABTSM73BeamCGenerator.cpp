// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BeamCGenerator.h"

#include "ABTSRuntime.h"
#include "Algo/Sort.h"
#include "Misc/Crc.h"

namespace ABTSM73BeamC
{
	struct FSupportStation
	{
		double Coordinate = 0.0;
		TArray<int32> EdgeIndices;
		double AreaCM2 = 0.0;
	};

	struct FSupportInterval
	{
		double Minimum = 0.0;
		double Maximum = 0.0;
	};

	bool Reject(
		FABTSM73BeamCGenerationResult& Result,
		FString& OutError,
		const FString& Reason)
	{
		Result.Summary.bAccepted = false;
		Result.Summary.RejectReason = Reason;
		OutError = Reason;
		return false;
	}

	FVector MemberStart(
		const FABTSM73BeamAMember& Member,
		const FABTSM73BeamAGenerationResult& Assembly)
	{
		return Assembly.Joints[Member.JointA].LocalPosition;
	}

	FVector MemberEnd(
		const FABTSM73BeamAMember& Member,
		const FABTSM73BeamAGenerationResult& Assembly)
	{
		return Assembly.Joints[Member.JointB].LocalPosition;
	}

	FVector MemberMidpoint(
		const FABTSM73BeamAMember& Member,
		const FABTSM73BeamAGenerationResult& Assembly)
	{
		return (MemberStart(Member, Assembly)
			+ MemberEnd(Member, Assembly)) * 0.5;
	}

	FBox MemberBounds(
		const FABTSM73BeamAMember& Member,
		const FABTSM73BeamAGenerationResult& Assembly,
		const double CrossSectionCM)
	{
		const FVector Center = MemberMidpoint(Member, Assembly);
		FVector Extent(CrossSectionCM * 0.5);
		const int32 AxisIndex = static_cast<int32>(Member.Axis);
		if (AxisIndex >= 0 && AxisIndex <= 2)
		{
			Extent[AxisIndex] = Member.LengthCM * 0.5;
		}
		return FBox(Center - Extent, Center + Extent);
	}

	double OverlapLength(
		const double AMin,
		const double AMax,
		const double BMin,
		const double BMax)
	{
		return FMath::Max(0.0, FMath::Min(AMax, BMax)
			- FMath::Max(AMin, BMin));
	}

	bool IsGroundMember(
		const FABTSM73BeamAMember& Member,
		const FABTSM73BeamAGenerationResult& Assembly,
		const FABTSM73BeamCPreviewSettings& Settings)
	{
		const FVector A = MemberStart(Member, Assembly);
		const FVector B = MemberEnd(Member, Assembly);
		const double HalfSection = Settings.BeamB.BeamA.BlockCrossSectionCM * 0.5;
		return FMath::Min(A.Z, B.Z) - HalfSection
			<= Settings.BeamB.BeamA.JointMergeToleranceCM;
	}

	void SplitStationShare(
		const FSupportStation& Station,
		const double StationShare,
		TArray<FABTSM73BeamCLoadEdge>& Edges)
	{
		const double SafeArea = FMath::Max(Station.AreaCM2, UE_DOUBLE_SMALL_NUMBER);
		for (const int32 EdgeIndex : Station.EdgeIndices)
		{
			FABTSM73BeamCLoadEdge& Edge = Edges[EdgeIndex];
			Edge.LoadShare = static_cast<float>(StationShare
				* Edge.ContactAreaCM2 / SafeArea);
		}
	}

	uint32 HashResult(const FABTSM73BeamCGenerationResult& Result)
	{
		FString Signature;
		Signature.Reserve(Result.Nodes.Num() * 96 + Result.Edges.Num() * 96);
		for (const FABTSM73BeamCLoadNode& Node : Result.Nodes)
		{
			Signature += FString::Printf(
				TEXT("N:%d:%d:%d:%.6f:%.6f:%.6f:%.6f:%d:%.6f:%.6f:%.6f:%.6f|"),
				Node.MemberId, static_cast<int32>(Node.Axis), Node.bGround ? 1 : 0,
				Node.SelfLoadKG, Node.AccumulatedLoadKG,
				Node.EffectiveSpanCM, Node.CantileverRatio,
				Node.RealSupportIntervalCount,
				Node.RealSupportCoverageRatio, Node.RealSupportSpanRatio,
				Node.SpanUtilization, Node.ColumnSlenderness);
		}
		for (const FABTSM73BeamCLoadEdge& Edge : Result.Edges)
		{
			Signature += FString::Printf(
				TEXT("E:%d:%d:%d:%d:%.6f:%.6f:%.6f:%.6f:%.6f:%.6f:%.6f|"),
				Edge.EdgeId, Edge.BearingContactId,
				Edge.UpperMemberId, Edge.LowerMemberId,
				Edge.ContactAreaCM2,
				Edge.ContactMinXY.X, Edge.ContactMinXY.Y,
				Edge.ContactMaxXY.X, Edge.ContactMaxXY.Y,
				Edge.LoadShare, Edge.ReactionLoadKG);
		}
		for (const int32 MemberId : Result.TopologicalMemberOrder)
		{
			Signature += FString::Printf(TEXT("T:%d|"), MemberId);
		}
		return FCrc::StrCrc32(*Signature);
	}

	bool TryObserveStructuralClosureFailure(
		const uint32 FailedAnalysisHash,
		const TOptional<uint32>& PreviousFailedAnalysisHash,
		TSet<uint32>& SeenHashes,
		bool& bOutImmediateRepeat,
		FString& OutError)
	{
		bOutImmediateRepeat = PreviousFailedAnalysisHash.IsSet()
			&& PreviousFailedAnalysisHash.GetValue() == FailedAnalysisHash;
		if (SeenHashes.Contains(FailedAnalysisHash) && !bOutImmediateRepeat)
		{
			OutError = TEXT("BeamCStructuralClosureNoProgress");
			return false;
		}
		SeenHashes.Add(FailedAnalysisHash);
		OutError.Reset();
		return true;
	}

	bool ShouldForceRootedGrillageRepair(
		const bool bRepeatedFailedAnalysis,
		const int32 PriorTwinAttemptCount)
	{
		return bRepeatedFailedAnalysis && PriorTwinAttemptCount > 0;
	}

	bool TryBeginRootedGrillageRepair(
		const uint32 FailedAnalysisHash,
		TSet<uint32>& AttemptedHashes,
		FString& OutError)
	{
		if (AttemptedHashes.Contains(FailedAnalysisHash))
		{
			OutError = TEXT("BeamCStructuralClosureNoProgress");
			return false;
		}
		AttemptedHashes.Add(FailedAnalysisHash);
		OutError.Reset();
		return true;
	}

	bool TryCheckRootedGrillageRepairAvailable(
		const uint32 FailedAnalysisHash,
		const TSet<uint32>& AttemptedHashes,
		FString& OutError)
	{
		if (AttemptedHashes.Contains(FailedAnalysisHash))
		{
			OutError = TEXT("BeamCStructuralClosureNoProgress");
			return false;
		}
		return true;
	}

	bool TryCommitAddedRootedGrillageRepair(
		const uint32 FailedAnalysisHash,
		const bool bAddedRootedGrillage,
		TSet<uint32>& AttemptedHashes,
		FString& OutError)
	{
		if (!bAddedRootedGrillage)
		{
			OutError.Reset();
			return true;
		}
		return TryBeginRootedGrillageRepair(
			FailedAnalysisHash, AttemptedHashes, OutError);
	}

	struct FStructuralSupportProposal
	{
		int32 AssemblyId = INDEX_NONE;
		int32 UpperMemberId = INDEX_NONE;
		FVector2D Station = FVector2D::ZeroVector;
		double BottomZ = 0.0;
		double TopZ = 0.0;
		bool bUsesCertifiedCoreSupport = false;
	};

	struct FStructuralSupportPatch
	{
		FVector2D Minimum = FVector2D::ZeroVector;
		FVector2D Maximum = FVector2D::ZeroVector;
	};

	struct FRootedTwinLaneAttempt
	{
		int32 SpanAxis = INDEX_NONE;
		FVector2D NegativeLane = FVector2D::ZeroVector;
		FVector2D PositiveLane = FVector2D::ZeroVector;
		double SeatTopZ = 0.0;
	};

	bool MatchesRootedTwinLaneAttempt(
		const FRootedTwinLaneAttempt& A,
		const FRootedTwinLaneAttempt& B,
		const double Tolerance)
	{
		return A.SpanAxis == B.SpanAxis
			&& FMath::Abs(A.NegativeLane.X - B.NegativeLane.X) <= Tolerance
			&& FMath::Abs(A.NegativeLane.Y - B.NegativeLane.Y) <= Tolerance
			&& FMath::Abs(A.PositiveLane.X - B.PositiveLane.X) <= Tolerance
			&& FMath::Abs(A.PositiveLane.Y - B.PositiveLane.Y) <= Tolerance
			&& FMath::Abs(A.SeatTopZ - B.SeatTopZ) <= Tolerance;
	}

	bool AddStructuralSupportPosts(
		const FABTSM73BeamCPreviewSettings& Settings,
		const FABTSM73BeamCGenerationResult& Analysis,
		FABTSM73BeamAGenerationResult& Assembly,
		const int32 RemainingPostBudget,
		const bool bAllowDeferredCoreBracing,
		const bool bRequireIndependentSupportLane,
		const bool bSuppressEquivalentProposals,
		const bool bForceRootedGrillage,
		const TArray<FRootedTwinLaneAttempt>& PriorTwinAttempts,
		TArray<FRootedTwinLaneAttempt>& OutTwinAttempts,
		bool& bOutAddedRootedGrillage,
		int32& OutAddedCount,
		FString& OutError)
	{
		OutAddedCount = 0;
		OutTwinAttempts.Reset();
		bOutAddedRootedGrillage = false;
		const double Section = Settings.BeamB.BeamA.BlockCrossSectionCM;
		const double HalfSection = Section * 0.5;
		const double Tolerance = Settings.BeamB.BeamA.JointMergeToleranceCM;
		// Beam-C load resultants are continuous analysis coordinates. They may guide
		// which lane needs support, but they are not legal construction stations.
		// The frozen M7 voxel contract places every X/Y member boundary at
		// HalfSection + N * Section, therefore every Z-member centre must remain on
		// N * Section. Keep the analysis continuous and quantize only candidates.
		auto TrySnapGridCoordinateWithin =
			[Section, Tolerance](const double Value,
				const double Minimum, const double Maximum, double& OutCoordinate)
		{
			const double GridMinimum = FMath::CeilToDouble(
				(Minimum - Tolerance) / Section) * Section;
			const double GridMaximum = FMath::FloorToDouble(
				(Maximum + Tolerance) / Section) * Section;
			if (GridMinimum > GridMaximum + Tolerance)
			{
				return false;
			}
			OutCoordinate = FMath::Clamp(
				FMath::GridSnap(Value, Section), GridMinimum, GridMaximum);
			return true;
		};
		auto TrySnapGridStationWithin =
			[&TrySnapGridCoordinateWithin](const FVector2D& Station,
				const double MinimumX, const double MaximumX,
				const double MinimumY, const double MaximumY,
				FVector2D& OutStation)
		{
			return TrySnapGridCoordinateWithin(
				Station.X, MinimumX, MaximumX, OutStation.X)
				&& TrySnapGridCoordinateWithin(
					Station.Y, MinimumY, MaximumY, OutStation.Y);
		};
		TArray<FBox> Bounds;
		Bounds.Reserve(Assembly.Members.Num());
		for (const FABTSM73BeamAMember& Member : Assembly.Members)
		{
			Bounds.Add(MemberBounds(Member, Assembly, Section));
		}
		TArray<int32> OwnerByMember;
		OwnerByMember.Init(INDEX_NONE, Assembly.Members.Num());
		for (const FABTSM73BeamAAssembly& Owner : Assembly.Assemblies)
		{
			for (const int32 MemberId : Owner.MemberIds)
			{
				if (OwnerByMember.IsValidIndex(MemberId))
				{
					OwnerByMember[MemberId] = Owner.AssemblyId;
				}
			}
		}
		auto IsReserved = [&Assembly, Tolerance](
			const FVector2D& Station,
			const double BottomZ,
			const double TopZ)
		{
			for (const FABTSM73BeamASupportVoid& SupportVoid :
				Assembly.ReservedSupportVoids)
			{
				const FBox& Void = SupportVoid.Bounds;
				if (FMath::Min(TopZ, Void.Max.Z)
						- FMath::Max(BottomZ, Void.Min.Z) > Tolerance
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

		TArray<FStructuralSupportProposal> Proposals;
		for (const FABTSM73BeamCLoadNode& Node : Analysis.Nodes)
		{
			if (Node.bSupportResultantValid && Node.bSupportSpreadValid)
			{
				continue;
			}
			if (!Assembly.Members.IsValidIndex(Node.MemberId)
				|| !Bounds.IsValidIndex(Node.MemberId))
			{
				OutError = TEXT("BeamCStructuralClosureInvalidMember");
				return false;
			}
			const FABTSM73BeamAMember& Upper = Assembly.Members[Node.MemberId];
			if (Upper.Axis != EABTSM73BeamAFrameAxis::X
				&& Upper.Axis != EABTSM73BeamAFrameAxis::Y)
			{
				continue;
			}
			const FBox& UpperBounds = Bounds[Node.MemberId];
			const int32 AxisIndex = Upper.Axis == EABTSM73BeamAFrameAxis::X ? 0 : 1;
			const int32 CrossIndex = AxisIndex == 0 ? 1 : 0;
			TArray<FVector2D> DesiredStations;
			auto AddDesiredStation = [&DesiredStations, &UpperBounds,
				&TrySnapGridStationWithin, HalfSection, Tolerance](
				const FVector2D& Station)
			{
				FVector2D SnappedStation;
				if (!TrySnapGridStationWithin(
					Station,
					UpperBounds.Min.X + HalfSection,
					UpperBounds.Max.X - HalfSection,
					UpperBounds.Min.Y + HalfSection,
					UpperBounds.Max.Y - HalfSection,
					SnappedStation))
				{
					return;
				}
				if (!DesiredStations.ContainsByPredicate(
					[&SnappedStation, Tolerance](const FVector2D& Existing)
					{
						return Existing.Equals(SnappedStation, Tolerance);
					}))
				{
					DesiredStations.Add(SnappedStation);
				}
			};
			if (!Node.bSupportResultantValid)
			{
				// Repair the load-resultant defect first. In the deferred C3 path a
				// member can fail both resultant and spread; omitting this station in
				// that case used to place a second support beside the first one while
				// leaving the actual load outside their envelope.
				AddDesiredStation(FVector2D(
					Node.LoadResultant.X, Node.LoadResultant.Y));
			}
			if (!Node.bSupportSpreadValid)
			{
				const double UsableMinimum = UpperBounds.Min[AxisIndex] + HalfSection;
				const double UsableMaximum = UpperBounds.Max[AxisIndex] - HalfSection;
				TArray<double> TargetAlphas;
				if (!bAllowDeferredCoreBracing)
				{
					TargetAlphas = {0.25, 0.75};
				}
				else
				{
					double ExistingSupportMinimum =
						TNumericLimits<double>::Max();
					double ExistingSupportMaximum =
						-TNumericLimits<double>::Max();
					bool bHasExistingSupportEnvelope = false;
					for (const FABTSM73BeamCLoadEdge& Edge : Analysis.Edges)
					{
						if (Edge.UpperMemberId == Node.MemberId)
						{
							ExistingSupportMinimum = FMath::Min(
								ExistingSupportMinimum,
								Edge.ContactMinXY[AxisIndex]);
							ExistingSupportMaximum = FMath::Max(
								ExistingSupportMaximum,
								Edge.ContactMaxXY[AxisIndex]);
							bHasExistingSupportEnvelope = true;
						}
					}
					bool bResultantRepairAlsoClosesSpread = false;
					if (bHasExistingSupportEnvelope
						&& !Node.bSupportResultantValid)
					{
						const double ResultantCoordinate = FMath::Clamp(
							Node.LoadResultant[AxisIndex],
							UsableMinimum, UsableMaximum);
						const double ExpandedMinimum = FMath::Min(
							ExistingSupportMinimum,
							ResultantCoordinate - HalfSection);
						const double ExpandedMaximum = FMath::Max(
							ExistingSupportMaximum,
							ResultantCoordinate + HalfSection);
						bResultantRepairAlsoClosesSpread =
							(ExpandedMaximum - ExpandedMinimum)
								/ FMath::Max(
									static_cast<double>(Upper.LengthCM), 1.0)
							>= Settings.MinimumSeparatedSupportSpanRatio;
					}
					if (!bHasExistingSupportEnvelope)
					{
						TargetAlphas = {0.25, 0.75};
					}
					else if (!bResultantRepairAlsoClosesSpread)
					{
						const double ExistingEnvelopeCenter =
							(ExistingSupportMinimum + ExistingSupportMaximum) * 0.5;
						// One additional station on the opposite side expands the real
						// contact envelope. Adding both quarter points plus the resultant
						// used to spend three long posts on a one-post defect and made the
						// low-tier C3 repair needlessly exceed its Brick budget.
						TargetAlphas.Add(ExistingEnvelopeCenter
							<= (UsableMinimum + UsableMaximum) * 0.5 ? 0.75 : 0.25);
					}
				}
				for (const double Alpha : TargetAlphas)
				{
					FVector2D Station(
						UpperBounds.GetCenter().X,
						UpperBounds.GetCenter().Y);
					Station[AxisIndex] = FMath::Lerp(
						UsableMinimum, UsableMaximum, Alpha);
					Station[CrossIndex] = FMath::Clamp(
						Node.LoadResultant[CrossIndex],
						UpperBounds.Min[CrossIndex] + HalfSection,
						UpperBounds.Max[CrossIndex] - HalfSection);
					AddDesiredStation(Station);
				}
			}

			const int32 OwnerId = OwnerByMember.IsValidIndex(Node.MemberId)
				? OwnerByMember[Node.MemberId] : INDEX_NONE;
			if (!Assembly.Assemblies.IsValidIndex(OwnerId))
			{
				OutError = TEXT("BeamCStructuralClosureOwnerMissing");
				return false;
			}
			TArray<FStructuralSupportPatch> ExistingSupportPatches;
			for (const FABTSM73BeamCLoadEdge& Edge : Analysis.Edges)
			{
				if (Edge.UpperMemberId == Node.MemberId)
				{
					FStructuralSupportPatch& Patch =
						ExistingSupportPatches.AddDefaulted_GetRef();
					Patch.Minimum = Edge.ContactMinXY;
					Patch.Maximum = Edge.ContactMaxXY;
				}
			}
			for (FVector2D Desired : DesiredStations)
			{
				for (const FABTSM73BeamASupportVoid& SupportVoid :
					Assembly.ReservedSupportVoids)
				{
					const FBox& Void = SupportVoid.Bounds;
					if (Desired.X <= Void.Min.X + Tolerance
						|| Desired.X >= Void.Max.X - Tolerance
						|| Desired.Y <= Void.Min.Y + Tolerance
						|| Desired.Y >= Void.Max.Y - Tolerance
						|| UpperBounds.Min.Z <= Void.Min.Z + Tolerance)
					{
						continue;
					}
					const int32 VoidAxis = SupportVoid.SpanAxisIndex == 1 ? 1 : 0;
					const double NegativeStation =
						Void.Min[VoidAxis] - HalfSection;
					const double PositiveStation =
						Void.Max[VoidAxis] + HalfSection;
					const double MinimumStation =
						UpperBounds.Min[VoidAxis] + HalfSection;
					const double MaximumStation =
						UpperBounds.Max[VoidAxis] - HalfSection;
					const bool bNegativeAvailable =
						NegativeStation >= MinimumStation - Tolerance;
					const bool bPositiveAvailable =
						PositiveStation <= MaximumStation + Tolerance;
					if (bNegativeAvailable || bPositiveAvailable)
					{
						bool bUseNegative = bNegativeAvailable
							&& (!bPositiveAvailable
								|| FMath::Abs(Desired[VoidAxis] - NegativeStation)
									<= FMath::Abs(
										Desired[VoidAxis] - PositiveStation));
						if (bNegativeAvailable && bPositiveAvailable
							&& !ExistingSupportPatches.IsEmpty())
						{
							double SupportMinimum = TNumericLimits<double>::Max();
							double SupportMaximum = -TNumericLimits<double>::Max();
							for (const FStructuralSupportPatch& Patch :
								ExistingSupportPatches)
							{
								SupportMinimum = FMath::Min(
									SupportMinimum, Patch.Minimum[VoidAxis]);
								SupportMaximum = FMath::Max(
									SupportMaximum, Patch.Maximum[VoidAxis]);
							}
							const double SupportCenter =
								(SupportMinimum + SupportMaximum) * 0.5;
							// A one-sided support envelope is repaired from the opposite
							// side of a reserved opening. Choosing merely the closest side
							// can add another post beside the existing one and leave both
							// the resultant and separated-support span unchanged.
							bUseNegative = SupportCenter
								> (NegativeStation + PositiveStation) * 0.5;
						}
						Desired[VoidAxis] = bUseNegative
							? NegativeStation : PositiveStation;
					}
				}
				FVector2D SnappedDesired;
				if (!TrySnapGridStationWithin(
					Desired,
					UpperBounds.Min.X + HalfSection,
					UpperBounds.Max.X - HalfSection,
					UpperBounds.Min.Y + HalfSection,
					UpperBounds.Max.Y - HalfSection,
					SnappedDesired))
				{
					continue;
				}
				Desired = SnappedDesired;
				const double TopZ = UpperBounds.Min.Z;
				TArray<FVector2D> ExistingZSupportStations;
				for (const FABTSM73BeamAMember& Existing : Assembly.Members)
				{
					if (Existing.Axis != EABTSM73BeamAFrameAxis::Z)
					{
						continue;
					}
					const FBox& ExistingBounds = Bounds[Existing.MemberId];
					const bool bEndsAtSupportFace =
						FMath::Abs(ExistingBounds.Max.Z - TopZ) <= Tolerance;
					const bool bPassesThroughSupportFace =
						ExistingBounds.Min.Z < TopZ - Tolerance
						&& ExistingBounds.Max.Z > TopZ + Tolerance;
					if (bEndsAtSupportFace || bPassesThroughSupportFace)
					{
						ExistingZSupportStations.Add(FVector2D(
							ExistingBounds.GetCenter().X,
							ExistingBounds.GetCenter().Y));
					}
				}
				auto IsOccupiedSupportStation = [&ExistingZSupportStations,
					&ExistingSupportPatches, &Proposals,
					TopZ, Section, Tolerance](const FVector2D& Station)
				{
					const double LaneSeparation = FMath::Max(
						Tolerance, Section - Tolerance);
					const bool bExisting =
						ExistingZSupportStations.ContainsByPredicate(
						[&Station, LaneSeparation](const FVector2D& Existing)
						{
							return FMath::Abs(Existing.X - Station.X)
								< LaneSeparation
								&& FMath::Abs(Existing.Y - Station.Y)
								< LaneSeparation;
						});
					const bool bCoveredByExistingPatch =
						ExistingSupportPatches.ContainsByPredicate(
							[&Station, Tolerance](
								const FStructuralSupportPatch& Patch)
							{
								return Station.X >= Patch.Minimum.X - Tolerance
									&& Station.X <= Patch.Maximum.X + Tolerance
									&& Station.Y >= Patch.Minimum.Y - Tolerance
									&& Station.Y <= Patch.Maximum.Y + Tolerance;
							});
					return bExisting || bCoveredByExistingPatch
						|| Proposals.ContainsByPredicate(
						[&Station, TopZ, LaneSeparation, Tolerance](
							const FStructuralSupportProposal& Existing)
						{
							return FMath::Abs(Existing.Station.X - Station.X)
									< LaneSeparation
								&& FMath::Abs(Existing.Station.Y - Station.Y)
									< LaneSeparation
								&& FMath::Abs(Existing.TopZ - TopZ) <= Tolerance;
						});
				};
				auto WouldOverlapExistingZ = [&Assembly, &Bounds, &Proposals,
					bRequireIndependentSupportLane, Section, Tolerance](
					const FVector2D& Station,
					const double BottomZ,
					const double CandidateTopZ)
				{
					if (!bRequireIndependentSupportLane)
					{
						return false;
					}
					const double LaneSeparation = FMath::Max(
						Tolerance, Section - Tolerance);
					const auto SameLane = [&Station, LaneSeparation](
						const FVector2D& Existing)
					{
						return FMath::Abs(Existing.X - Station.X) < LaneSeparation
							&& FMath::Abs(Existing.Y - Station.Y) < LaneSeparation;
					};
					for (const FABTSM73BeamAMember& Existing : Assembly.Members)
					{
						if (Existing.Axis != EABTSM73BeamAFrameAxis::Z
							|| !Bounds.IsValidIndex(Existing.MemberId))
						{
							continue;
						}
						const FBox& ExistingBounds = Bounds[Existing.MemberId];
						if (!SameLane(FVector2D(
							ExistingBounds.GetCenter().X,
							ExistingBounds.GetCenter().Y)))
						{
							continue;
						}
						const double Overlap = FMath::Min(
							CandidateTopZ, ExistingBounds.Max.Z)
							- FMath::Max(BottomZ, ExistingBounds.Min.Z);
						if (Overlap > Tolerance)
						{
							return true;
						}
					}
					return Proposals.ContainsByPredicate(
						[&SameLane, BottomZ, CandidateTopZ, Tolerance](
							const FStructuralSupportProposal& Existing)
						{
							return SameLane(Existing.Station)
								&& FMath::Min(CandidateTopZ, Existing.TopZ)
									- FMath::Max(BottomZ, Existing.BottomZ)
									> Tolerance;
						});
				};
				auto WouldCloseResultant = [&ExistingSupportPatches, &Node,
					&UpperBounds, AxisIndex, HalfSection, &Settings](
					const FVector2D& Station)
				{
					if (Node.bSupportResultantValid)
					{
						return true;
					}
					double SupportMinimum = FMath::Clamp(
						Station[AxisIndex] - HalfSection,
						UpperBounds.Min[AxisIndex], UpperBounds.Max[AxisIndex]);
					double SupportMaximum = FMath::Clamp(
						Station[AxisIndex] + HalfSection,
						UpperBounds.Min[AxisIndex], UpperBounds.Max[AxisIndex]);
					for (const FStructuralSupportPatch& Patch : ExistingSupportPatches)
					{
						SupportMinimum = FMath::Min(
							SupportMinimum, Patch.Minimum[AxisIndex]);
						SupportMaximum = FMath::Max(
							SupportMaximum, Patch.Maximum[AxisIndex]);
					}
					const double Resultant = Node.LoadResultant[AxisIndex];
					return Resultant
						>= SupportMinimum + Settings.SupportResultantMarginCM
						&& Resultant
						<= SupportMaximum - Settings.SupportResultantMarginCM;
				};
				if (IsOccupiedSupportStation(Desired))
				{
					// Beam-A merges Z members whose XY coordinates differ by less
					// than one cross section. Search outside that exact merge lane;
					// otherwise this proposal is counted as new here, absorbed by
					// reclose, and recreated forever without changing the support DAG.
					const double UsableMinimum =
						UpperBounds.Min[AxisIndex] + HalfSection;
					const double UsableMaximum =
						UpperBounds.Max[AxisIndex] - HalfSection;
					const double CenterCoordinate =
						(UsableMinimum + UsableMaximum) * 0.5;
					const double PreferredDirection =
						Desired[AxisIndex] <= CenterCoordinate ? -1.0 : 1.0;
					const double LaneStep = Section;
					const int32 MaximumSteps = FMath::Max(1,
						FMath::CeilToInt(
							(UsableMaximum - UsableMinimum) / LaneStep));
					bool bFoundSeparateLane = false;
					for (int32 Step = 1;
						Step <= MaximumSteps && !bFoundSeparateLane; ++Step)
					{
						const double Directions[2] = {
							PreferredDirection, -PreferredDirection};
						for (const double Direction : Directions)
						{
							FVector2D Candidate = Desired;
							Candidate[AxisIndex] +=
								Direction * LaneStep * Step;
							if (Candidate[AxisIndex] < UsableMinimum - Tolerance
								|| Candidate[AxisIndex]
									> UsableMaximum + Tolerance
								|| IsOccupiedSupportStation(Candidate))
							{
								continue;
							}
							Desired = Candidate;
							bFoundSeparateLane = true;
							break;
						}
					}
					if (!bFoundSeparateLane)
					{
						continue;
					}
				}

				double BestTopZ = 0.0;
				FVector2D BestStation = Desired;
				bool bBestIsCertifiedSupport = false;
				bool bHasBest = !IsOccupiedSupportStation(Desired)
					&& !WouldOverlapExistingZ(Desired, 0.0, TopZ)
					&& WouldCloseResultant(Desired)
					&& !IsReserved(Desired, 0.0, TopZ);
				double BestDistance = bHasBest
					? 0.0 : TNumericLimits<double>::Max();
				for (const FABTSM73BeamAMember& Lower : Assembly.Members)
				{
					if (Lower.MemberId == Node.MemberId
						|| Lower.Axis == EABTSM73BeamAFrameAxis::Z
						|| Lower.Axis == EABTSM73BeamAFrameAxis::Diagonal)
					{
						continue;
					}
					const FBox& LowerBounds = Bounds[Lower.MemberId];
					if (LowerBounds.Max.Z > UpperBounds.Min.Z - Section + Tolerance)
					{
						continue;
					}
					const double MinX = FMath::Max(
						UpperBounds.Min.X, LowerBounds.Min.X) + HalfSection;
					const double MaxX = FMath::Min(
						UpperBounds.Max.X, LowerBounds.Max.X) - HalfSection;
					const double MinY = FMath::Max(
						UpperBounds.Min.Y, LowerBounds.Min.Y) + HalfSection;
					const double MaxY = FMath::Min(
						UpperBounds.Max.Y, LowerBounds.Max.Y) - HalfSection;
					if (MinX > MaxX + Tolerance || MinY > MaxY + Tolerance)
					{
						continue;
					}
					FVector2D CandidateUnsnapped(
						FMath::Clamp(Desired.X, MinX, MaxX),
						FMath::Clamp(Desired.Y, MinY, MaxY));
					FVector2D Candidate;
					if (!TrySnapGridStationWithin(
						CandidateUnsnapped, MinX, MaxX, MinY, MaxY, Candidate))
					{
						continue;
					}
					const double CandidateBottomZ = LowerBounds.Max.Z;
					if (IsOccupiedSupportStation(Candidate)
						|| WouldOverlapExistingZ(
							Candidate, CandidateBottomZ, TopZ)
						|| !WouldCloseResultant(Candidate))
					{
						const double LaneMinimum = AxisIndex == 0 ? MinX : MinY;
						const double LaneMaximum = AxisIndex == 0 ? MaxX : MaxY;
						const double LaneCenter = (LaneMinimum + LaneMaximum) * 0.5;
						const double PreferredDirection =
							Candidate[AxisIndex] <= LaneCenter ? -1.0 : 1.0;
						const double LaneStep = Section;
						const int32 MaximumSteps = FMath::Max(1,
							FMath::CeilToInt(
								(LaneMaximum - LaneMinimum) / LaneStep));
						bool bFoundSeparateLane = false;
						for (int32 Step = 1;
							Step <= MaximumSteps && !bFoundSeparateLane; ++Step)
						{
							const double Directions[2] = {
								PreferredDirection, -PreferredDirection};
							for (const double Direction : Directions)
							{
								FVector2D Alternative = Candidate;
								Alternative[AxisIndex] +=
									Direction * LaneStep * Step;
								if (Alternative[AxisIndex]
										< LaneMinimum - Tolerance
									|| Alternative[AxisIndex]
										> LaneMaximum + Tolerance
									|| IsOccupiedSupportStation(Alternative)
									|| WouldOverlapExistingZ(
										Alternative, CandidateBottomZ, TopZ)
									|| !WouldCloseResultant(Alternative))
								{
									continue;
								}
								Candidate = Alternative;
								bFoundSeparateLane = true;
								break;
							}
						}
						if (!bFoundSeparateLane)
						{
							continue;
						}
					}
					const double Distance = FVector2D::DistSquared(Candidate, Desired);
					// Ordinary courses may not drag a requested load lane sideways.
					// A certified crib course may provide a fallback station inside the
					// real Upper/CoreCourse overlap, but it must not outrank an exact
					// support station merely because of its semantic role. Doing so made
					// every repair gravitate toward a dense C3 core even when that station
					// could not improve the original resultant/spread defect; later passes
					// then kept adding equivalent posts without converging.
					const bool bCertifiedCoreSupport = bAllowDeferredCoreBracing
						&& (Lower.Role == EABTSM73BeamAMemberRole::CoreCourse
							|| Lower.Role == EABTSM73BeamAMemberRole::BridgeSeat);
					if (Distance > FMath::Square(Tolerance)
						&& !bCertifiedCoreSupport
						&& Node.bSupportSpreadValid)
					{
						continue;
					}
					if (TopZ - LowerBounds.Max.Z + Tolerance < Section
						|| IsReserved(Candidate, LowerBounds.Max.Z, TopZ)
						|| IsOccupiedSupportStation(Candidate)
						|| WouldOverlapExistingZ(
							Candidate, LowerBounds.Max.Z, TopZ)
						|| !WouldCloseResultant(Candidate))
					{
						continue;
					}
					const double DistanceTolerance = FMath::Square(Tolerance);
					if (!bHasBest
						|| Distance < BestDistance - DistanceTolerance
						|| (FMath::IsNearlyEqual(
							Distance, BestDistance, DistanceTolerance)
							&& (LowerBounds.Max.Z > BestTopZ + Tolerance
								|| (FMath::IsNearlyEqual(
									LowerBounds.Max.Z, BestTopZ, Tolerance)
									&& bCertifiedCoreSupport
									&& !bBestIsCertifiedSupport))))
					{
						BestTopZ = LowerBounds.Max.Z;
						BestDistance = Distance;
						BestStation = Candidate;
						bBestIsCertifiedSupport = bCertifiedCoreSupport;
						bHasBest = true;
					}
				}
				if (!bHasBest
					|| TopZ - BestTopZ + Tolerance < Section
					|| IsReserved(BestStation, BestTopZ, TopZ))
				{
					continue;
				}
				if (IsOccupiedSupportStation(BestStation)
					|| WouldOverlapExistingZ(BestStation, BestTopZ, TopZ)
					|| !WouldCloseResultant(BestStation))
				{
					continue;
				}
				FStructuralSupportProposal& Proposal =
					Proposals.AddDefaulted_GetRef();
				Proposal.AssemblyId = OwnerId;
				Proposal.UpperMemberId = Node.MemberId;
				Proposal.Station = BestStation;
				Proposal.BottomZ = BestTopZ;
				Proposal.TopZ = TopZ;
				Proposal.bUsesCertifiedCoreSupport = bBestIsCertifiedSupport;
			}
		}

		if (bSuppressEquivalentProposals)
		{
			// The exact failed load hash has already repeated after an authoritative
			// Beam-A reclose. Another direct post would be folded into the same lane,
			// so discard equivalent proposals and switch once to explicit grillage.
			Proposals.Reset();
		}
		// Proposal discovery is transactional. Enumerate the complete distinct set
		// before applying the capacity gate so Required is an exact demand, not the
		// first value one past Capacity. Truncated diagnostics previously made a
		// large deficit look like an off-by-one and encouraged repeated cap tuning.
		if (Proposals.Num() > RemainingPostBudget)
		{
			int32 CribOwnerCount = 0;
			int32 CoreCourseCount = 0;
			int32 BridgeRailCount = 0;
			int32 RoofCourseCount = 0;
			int32 OtherRoleCount = 0;
			int32 CertifiedLowerCount = 0;
			int32 GroundPostCount = 0;
			for (const FStructuralSupportProposal& Proposal : Proposals)
			{
				if (Assembly.Assemblies.IsValidIndex(Proposal.AssemblyId)
					&& Assembly.Assemblies[Proposal.AssemblyId].Type
						== EABTSM73BeamAAssemblyType::CribCore)
				{
					++CribOwnerCount;
				}
				if (Assembly.Members.IsValidIndex(Proposal.UpperMemberId))
				{
					switch (Assembly.Members[Proposal.UpperMemberId].Role)
					{
					case EABTSM73BeamAMemberRole::CoreCourse:
						++CoreCourseCount;
						break;
					case EABTSM73BeamAMemberRole::BridgeRail:
						++BridgeRailCount;
						break;
					case EABTSM73BeamAMemberRole::RoofCourse:
						++RoofCourseCount;
						break;
					default:
						++OtherRoleCount;
						break;
					}
				}
				if (Proposal.bUsesCertifiedCoreSupport)
				{
					++CertifiedLowerCount;
				}
				if (Proposal.BottomZ <= Tolerance)
				{
					++GroundPostCount;
				}
			}
			OutError = FString::Printf(
				TEXT("BeamCStructuralSupportBudgetExceeded:Required=%d:Capacity=%d")
				TEXT(":CribOwner=%d:CoreCourse=%d:BridgeRail=%d:RoofCourse=%d")
				TEXT(":OtherRole=%d:CertifiedLower=%d:Ground=%d"),
				Proposals.Num(), RemainingPostBudget, CribOwnerCount,
				CoreCourseCount, BridgeRailCount, RoofCourseCount,
				OtherRoleCount, CertifiedLowerCount, GroundPostCount);
			return false;
		}
		for (const FStructuralSupportProposal& Proposal : Proposals)
		{
			if (Assembly.Members.Num() >= Settings.BeamB.BeamA.MaxMemberCount
				|| Assembly.Joints.Num() + 2 > Settings.BeamB.BeamA.MaxJointCount)
			{
				OutError = FString::Printf(
					TEXT("BeamCStructuralSupportIRBudgetExceeded:Members=%d/%d:Joints=%d/%d:Proposals=%d:Committed=%d"),
					Assembly.Members.Num(),
					Settings.BeamB.BeamA.MaxMemberCount,
					Assembly.Joints.Num(),
					Settings.BeamB.BeamA.MaxJointCount,
					Proposals.Num(), OutAddedCount);
				return false;
			}
			const int32 JointA = Assembly.Joints.Num();
			FABTSM73BeamAJoint& A = Assembly.Joints.AddDefaulted_GetRef();
			A.JointId = JointA;
			A.LocalPosition = FVector(
				Proposal.Station.X, Proposal.Station.Y, Proposal.BottomZ);
			A.Role = EABTSM73BeamAJointRole::GroundFoot;
			const int32 JointB = Assembly.Joints.Num();
			FABTSM73BeamAJoint& B = Assembly.Joints.AddDefaulted_GetRef();
			B.JointId = JointB;
			B.LocalPosition = FVector(
				Proposal.Station.X, Proposal.Station.Y, Proposal.TopZ);
			B.Role = EABTSM73BeamAJointRole::ColumnHead;
			FABTSM73BeamAMember& Post = Assembly.Members.AddDefaulted_GetRef();
			Post.MemberId = Assembly.Members.Num() - 1;
			Post.JointA = JointA;
			Post.JointB = JointB;
			Post.Axis = EABTSM73BeamAFrameAxis::Z;
			Post.Role = EABTSM73BeamAMemberRole::Post;
			Post.LengthCM = Proposal.TopZ - Proposal.BottomZ;
			Assembly.Assemblies[Proposal.AssemblyId].MemberIds.Add(Post.MemberId);
			++OutAddedCount;
		}
		if (OutAddedCount == 0 && bAllowDeferredCoreBracing)
		{
			// A support-resultant defect above intentional negative space cannot be
			// repaired by a Z post through that void. Install the smallest explicit
			// friction-only grillage instead: one horizontal seat spanning the void
			// or the failed floor course, carried by two separated posts. Beam-A rebuilds
			// every real bearing afterwards; Beam-C2 and Beam-C3 then certify the seat,
			// both roots, and any new Z-span belts. Nothing here is an implicit joint.
			for (const FABTSM73BeamCLoadNode& Node : Analysis.Nodes)
			{
				if ((Node.bSupportResultantValid && Node.bSupportSpreadValid)
					|| !Assembly.Members.IsValidIndex(Node.MemberId)
					|| !Bounds.IsValidIndex(Node.MemberId))
				{
					continue;
				}
				const FABTSM73BeamAMember& Upper = Assembly.Members[Node.MemberId];
				if ((Upper.Axis != EABTSM73BeamAFrameAxis::X
						&& Upper.Axis != EABTSM73BeamAFrameAxis::Y)
					|| Upper.Role == EABTSM73BeamAMemberRole::BridgeSeat
					|| Upper.Role == EABTSM73BeamAMemberRole::BridgeRail)
				{
					continue;
				}
				const FBox& UpperBounds = Bounds[Node.MemberId];
				const FABTSM73BeamASupportVoid* BestVoid = nullptr;
				double BestVoidScore = TNumericLimits<double>::Max();
				for (const FABTSM73BeamASupportVoid& SupportVoid :
					Assembly.ReservedSupportVoids)
				{
					if ((SupportVoid.SpanAxisIndex != 0
							&& SupportVoid.SpanAxisIndex != 1)
						|| UpperBounds.Min.Z < SupportVoid.Bounds.Max.Z - Tolerance
						|| OverlapLength(UpperBounds.Min.X, UpperBounds.Max.X,
							SupportVoid.Bounds.Min.X,
							SupportVoid.Bounds.Max.X) <= Tolerance
						|| OverlapLength(UpperBounds.Min.Y, UpperBounds.Max.Y,
							SupportVoid.Bounds.Min.Y,
							SupportVoid.Bounds.Max.Y) <= Tolerance)
					{
						continue;
					}
					const int32 SpanAxis = SupportVoid.SpanAxisIndex;
					const double NegativeStation =
						SupportVoid.Bounds.Min[SpanAxis] - HalfSection;
					const double PositiveStation =
						SupportVoid.Bounds.Max[SpanAxis] + HalfSection;
					if (PositiveStation - NegativeStation < Section - Tolerance)
					{
						continue;
					}
					const double Score =
						UpperBounds.Min.Z - SupportVoid.Bounds.Max.Z;
					if (Score < BestVoidScore)
					{
						BestVoid = &SupportVoid;
						BestVoidScore = Score;
					}
				}
				// Direct twin roots get one attempt. If authoritative closure repeats
				// the same failed analysis, fall through to a physical seat
				// below; otherwise this branch keeps recreating posts that Beam-A
				// folds into the same lanes and the rooted grillage is unreachable.
				if (BestVoid == nullptr && !bForceRootedGrillage)
				{
					const int32 OwnerId = OwnerByMember.IsValidIndex(Node.MemberId)
						? OwnerByMember[Node.MemberId] : INDEX_NONE;
					if (!Assembly.Assemblies.IsValidIndex(OwnerId))
					{
						continue;
					}
					const int32 AxisIndex =
						Upper.Axis == EABTSM73BeamAFrameAxis::X ? 0 : 1;
					const int32 CrossIndex = AxisIndex == 0 ? 1 : 0;
					const double TargetTopZ = UpperBounds.Min.Z;
					const double UsableMinimum =
						UpperBounds.Min[AxisIndex] + HalfSection;
					const double UsableMaximum =
						UpperBounds.Max[AxisIndex] - HalfSection;
					if ((UsableMaximum - UsableMinimum) * 0.5
						< Section - Tolerance)
					{
						continue;
					}
					FVector2D TwinLanes[2];
					for (int32 LaneIndex = 0; LaneIndex < 2; ++LaneIndex)
					{
						const double Alpha = LaneIndex == 0 ? 0.25 : 0.75;
						TwinLanes[LaneIndex] = FVector2D(
							UpperBounds.GetCenter().X,
							UpperBounds.GetCenter().Y);
						TwinLanes[LaneIndex][AxisIndex] = FMath::Lerp(
							UsableMinimum, UsableMaximum, Alpha);
						TwinLanes[LaneIndex][CrossIndex] = FMath::Clamp(
							Node.LoadResultant[CrossIndex],
							UpperBounds.Min[CrossIndex] + HalfSection,
							UpperBounds.Max[CrossIndex] - HalfSection);
					}
					int32 AddedTwinPosts = 0;
					int32 ProvenTwinLanes = 0;
					for (const FVector2D& Station : TwinLanes)
					{
						if (IsReserved(Station, 0.0, TargetTopZ))
						{
							continue;
						}
						const double LaneSeparation = FMath::Max(
							Tolerance, Section - Tolerance);
						const bool bExistingRoot = Assembly.Members.ContainsByPredicate(
							[&Assembly, Station, TargetTopZ, Section, Tolerance,
								LaneSeparation](const FABTSM73BeamAMember& Existing)
							{
								if (Existing.Axis != EABTSM73BeamAFrameAxis::Z)
								{
									return false;
								}
								const FBox ExistingBounds = MemberBounds(
									Existing, Assembly, Section);
								return FMath::Abs(ExistingBounds.GetCenter().X
										- Station.X) < LaneSeparation
									&& FMath::Abs(ExistingBounds.GetCenter().Y
										- Station.Y) < LaneSeparation
									&& ExistingBounds.Min.Z <= Tolerance
									&& ExistingBounds.Max.Z >= TargetTopZ - Tolerance;
							});
						if (bExistingRoot)
						{
							++ProvenTwinLanes;
							continue;
						}
						if (OutAddedCount + 1 > RemainingPostBudget
							|| Assembly.Members.Num() + 1
								> Settings.BeamB.BeamA.MaxMemberCount
							|| Assembly.Joints.Num() + 2
								> Settings.BeamB.BeamA.MaxJointCount)
						{
							break;
						}
						FABTSM73BeamAAssembly& Owner =
							Assembly.Assemblies[OwnerId];
						const int32 JointA = Assembly.Joints.Num();
						FABTSM73BeamAJoint& A =
							Assembly.Joints.AddDefaulted_GetRef();
						A.JointId = JointA;
						A.LocalPosition = FVector(Station.X, Station.Y, 0.0);
						A.Role = EABTSM73BeamAJointRole::GroundFoot;
						const int32 JointB = Assembly.Joints.Num();
						FABTSM73BeamAJoint& B =
							Assembly.Joints.AddDefaulted_GetRef();
						B.JointId = JointB;
						B.LocalPosition = FVector(
							Station.X, Station.Y, TargetTopZ);
						B.Role = EABTSM73BeamAJointRole::ColumnHead;
						FABTSM73BeamAMember& Post =
							Assembly.Members.AddDefaulted_GetRef();
						Post.MemberId = Assembly.Members.Num() - 1;
						Post.JointA = JointA;
						Post.JointB = JointB;
						Post.Axis = EABTSM73BeamAFrameAxis::Z;
						Post.Role = EABTSM73BeamAMemberRole::CorePost;
						Post.LengthCM = TargetTopZ;
						Owner.JointIds.AddUnique(JointA);
						Owner.JointIds.AddUnique(JointB);
						Owner.MemberIds.AddUnique(Post.MemberId);
						++AddedTwinPosts;
						++ProvenTwinLanes;
						++OutAddedCount;
					}
					if (AddedTwinPosts > 0 && ProvenTwinLanes == 2)
					{
						FRootedTwinLaneAttempt& Attempt =
							OutTwinAttempts.AddDefaulted_GetRef();
						Attempt.SpanAxis = AxisIndex;
						Attempt.NegativeLane = TwinLanes[0];
						Attempt.PositiveLane = TwinLanes[1];
						Attempt.SeatTopZ = TargetTopZ;
					}
					if (AddedTwinPosts > 0)
					{
						UE_LOG(LogABTSRuntime, Display,
							TEXT("[ABTS][M7.3-Beam-C2][RootedTwinPosts]")
							TEXT(" Upper=%d Added=%d Axis=%d TopZ=%.2f"),
							Node.MemberId, AddedTwinPosts, AxisIndex,
							TargetTopZ);
					}
					continue;
				}
				const bool bRequiresPriorTwinAttempt =
					BestVoid == nullptr && bForceRootedGrillage;
				bool bResultantCrossBearing = false;
				const int32 UpperAxis =
					Upper.Axis == EABTSM73BeamAFrameAxis::X ? 0 : 1;
				const int32 SpanAxis = BestVoid != nullptr
					? BestVoid->SpanAxisIndex
					: (UpperAxis == 0 ? 1 : 0);
				const int32 Perpendicular = SpanAxis == 0 ? 1 : 0;
				double NegativeStation = 0.0;
				double PositiveStation = 0.0;
				double NegativePostStation = 0.0;
				double PositivePostStation = 0.0;
				double PerpendicularMinimum =
					UpperBounds.Min[Perpendicular] + HalfSection;
				double PerpendicularMaximum =
					UpperBounds.Max[Perpendicular] - HalfSection;
				if (BestVoid != nullptr)
				{
					NegativeStation =
						BestVoid->Bounds.Min[SpanAxis] - HalfSection;
					PositiveStation =
						BestVoid->Bounds.Max[SpanAxis] + HalfSection;
					NegativePostStation = NegativeStation;
					PositivePostStation = PositiveStation;
					PerpendicularMinimum = FMath::Max(
						UpperBounds.Min[Perpendicular],
						BestVoid->Bounds.Min[Perpendicular]) + HalfSection;
					PerpendicularMaximum = FMath::Min(
						UpperBounds.Max[Perpendicular],
						BestVoid->Bounds.Max[Perpendicular]) - HalfSection;
				}
				else if (bRequiresPriorTwinAttempt)
				{
					// The direct 25/75 roots remain the evidence that this exact failed
					// course has already consumed its one Z-only attempt. Do not cap
					// those lanes with a member parallel to Upper: at the immediately
					// lower alternating course that cap intersects the existing
					// perpendicular layer, so Beam-A separation lifts it into Upper and
					// disconnects it from both roots. Escalate instead to one short
					// cross-bearing centred on the failed load resultant. It lies in the
					// lower course direction and its two separated roots make the new
					// seat itself a closed, friction-only load path.
					const int32 PriorSpanAxis = UpperAxis;
					const int32 PriorPerpendicular =
						PriorSpanAxis == 0 ? 1 : 0;
					const double PriorUsableMinimum =
						UpperBounds.Min[PriorSpanAxis] + HalfSection;
					const double PriorUsableMaximum =
						UpperBounds.Max[PriorSpanAxis] - HalfSection;
					if (PriorUsableMinimum > PriorUsableMaximum + Tolerance)
					{
						continue;
					}
					FRootedTwinLaneAttempt CandidateAttempt;
					CandidateAttempt.SpanAxis = PriorSpanAxis;
					CandidateAttempt.NegativeLane = FVector2D(
						UpperBounds.GetCenter().X, UpperBounds.GetCenter().Y);
					CandidateAttempt.PositiveLane = CandidateAttempt.NegativeLane;
					CandidateAttempt.NegativeLane[PriorSpanAxis] = FMath::Lerp(
						PriorUsableMinimum, PriorUsableMaximum, 0.25);
					CandidateAttempt.PositiveLane[PriorSpanAxis] = FMath::Lerp(
						PriorUsableMinimum, PriorUsableMaximum, 0.75);
					CandidateAttempt.NegativeLane[PriorPerpendicular] = FMath::Clamp(
						Node.LoadResultant[PriorPerpendicular],
						UpperBounds.Min[PriorPerpendicular] + HalfSection,
						UpperBounds.Max[PriorPerpendicular] - HalfSection);
					CandidateAttempt.PositiveLane[PriorPerpendicular] =
						CandidateAttempt.NegativeLane[PriorPerpendicular];
					CandidateAttempt.SeatTopZ = UpperBounds.Min.Z;
					const bool bHasPriorTwinEvidence =
						PriorTwinAttempts.ContainsByPredicate(
						[&CandidateAttempt, Tolerance](
							const FRootedTwinLaneAttempt& Prior)
						{
							return MatchesRootedTwinLaneAttempt(
								Prior, CandidateAttempt, Tolerance);
						});
					if (!bHasPriorTwinEvidence)
					{
						continue;
					}
					const double SeatLength = Section * 3.0;
					const double SeatCenter =
						UpperBounds.GetCenter()[SpanAxis];
					NegativeStation = SeatCenter - SeatLength * 0.5;
					PositiveStation = SeatCenter + SeatLength * 0.5;
					NegativePostStation = NegativeStation;
					PositivePostStation = PositiveStation;
					bResultantCrossBearing = true;
				}
				else
				{
					// Ordinary floor repair is a cross-bearing: a short seat runs
					// perpendicular to the failed beam, so Beam-A cannot fold it into
					// another collinear course. Its two roots are three sections apart.
					const double SeatLength = Section * 3.0;
					const double SeatCenter =
						UpperBounds.GetCenter()[SpanAxis];
					NegativeStation = SeatCenter - SeatLength * 0.5;
					PositiveStation = SeatCenter + SeatLength * 0.5;
					NegativePostStation = NegativeStation;
					PositivePostStation = PositiveStation;
				}
				if (PerpendicularMinimum > PerpendicularMaximum + Tolerance)
				{
					continue;
				}
				FVector2D SeatStation(
					UpperBounds.GetCenter().X, UpperBounds.GetCenter().Y);
				SeatStation[Perpendicular] = FMath::Clamp(
					Node.LoadResultant[Perpendicular],
					PerpendicularMinimum, PerpendicularMaximum);
				const double SeatTopZ = UpperBounds.Min.Z;
				const double SeatCenterZ = SeatTopZ - HalfSection;
				const double PostTopZ = SeatTopZ - Section;
				FVector2D NegativePost = SeatStation;
				FVector2D PositivePost = SeatStation;
				NegativePost[SpanAxis] = NegativePostStation;
				PositivePost[SpanAxis] = PositivePostStation;
				if (PostTopZ < Section - Tolerance
					|| IsReserved(NegativePost, 0.0, PostTopZ)
					|| IsReserved(PositivePost, 0.0, PostTopZ))
				{
					continue;
				}
				bool bSeatAlreadyExists = false;
				for (const FABTSM73BeamAMember& Existing : Assembly.Members)
				{
					if (Existing.Role != EABTSM73BeamAMemberRole::BridgeSeat
						|| static_cast<int32>(Existing.Axis) != SpanAxis)
					{
						continue;
					}
					const FBox ExistingBounds = MemberBounds(
						Existing, Assembly, Section);
					bSeatAlreadyExists =
						FMath::Abs(ExistingBounds.Max.Z - SeatTopZ) <= Tolerance
						&& FMath::Abs(ExistingBounds.GetCenter()[Perpendicular]
							- SeatStation[Perpendicular]) < Section - Tolerance
						&& ExistingBounds.Min[SpanAxis]
							<= NegativeStation + HalfSection + Tolerance
						&& ExistingBounds.Max[SpanAxis]
							>= PositiveStation - HalfSection - Tolerance;
					if (bSeatAlreadyExists)
					{
						break;
					}
				}
				if (bSeatAlreadyExists)
				{
					continue;
				}
				const int32 OwnerId = OwnerByMember.IsValidIndex(Node.MemberId)
					? OwnerByMember[Node.MemberId] : INDEX_NONE;
				if (!Assembly.Assemblies.IsValidIndex(OwnerId)
					|| OutAddedCount + 3 > RemainingPostBudget
					|| Assembly.Members.Num() + 3
						> Settings.BeamB.BeamA.MaxMemberCount
					|| Assembly.Joints.Num() + 6
						> Settings.BeamB.BeamA.MaxJointCount)
				{
					continue;
				}
				FABTSM73BeamAAssembly& Owner = Assembly.Assemblies[OwnerId];
				auto AddRepairJoint = [&Assembly, &Owner](
					const FVector& Position,
					const EABTSM73BeamAJointRole Role)
				{
					FABTSM73BeamAJoint& Joint =
						Assembly.Joints.AddDefaulted_GetRef();
					Joint.JointId = Assembly.Joints.Num() - 1;
					Joint.LocalPosition = Position;
					Joint.Role = Role;
					Owner.JointIds.AddUnique(Joint.JointId);
					return Joint.JointId;
				};
				auto AddRepairMember = [&Assembly, &Owner, &AddRepairJoint](
					const FVector& Start,
					const FVector& End,
					const EABTSM73BeamAFrameAxis Axis,
					const EABTSM73BeamAMemberRole Role)
				{
					FABTSM73BeamAMember& Member =
						Assembly.Members.AddDefaulted_GetRef();
					Member.MemberId = Assembly.Members.Num() - 1;
					Member.JointA = AddRepairJoint(
						Start, EABTSM73BeamAJointRole::CrossBearing);
					Member.JointB = AddRepairJoint(
						End, EABTSM73BeamAJointRole::CrossBearing);
					Member.Axis = Axis;
					Member.Role = Role;
					Member.LengthCM = FVector::Distance(Start, End);
					Owner.MemberIds.AddUnique(Member.MemberId);
					return Member.MemberId;
				};
				FVector SeatStart(
					SeatStation.X, SeatStation.Y, SeatCenterZ);
				FVector SeatEnd = SeatStart;
				SeatStart[SpanAxis] = NegativeStation;
				SeatEnd[SpanAxis] = PositiveStation;
				AddRepairMember(SeatStart, SeatEnd,
					static_cast<EABTSM73BeamAFrameAxis>(SpanAxis),
					EABTSM73BeamAMemberRole::BridgeSeat);
				AddRepairMember(
					FVector(NegativePost.X, NegativePost.Y, 0.0),
					FVector(NegativePost.X, NegativePost.Y, PostTopZ),
					EABTSM73BeamAFrameAxis::Z,
					EABTSM73BeamAMemberRole::BridgePost);
				AddRepairMember(
					FVector(PositivePost.X, PositivePost.Y, 0.0),
					FVector(PositivePost.X, PositivePost.Y, PostTopZ),
					EABTSM73BeamAFrameAxis::Z,
					EABTSM73BeamAMemberRole::BridgePost);
				OutAddedCount += 3;
				bOutAddedRootedGrillage = true;
				UE_LOG(LogABTSRuntime, Display,
					TEXT("[ABTS][M7.3-Beam-C2][RootedGrillage]")
					TEXT(" Upper=%d VoidSource=%d SpanAxis=%d SeatZ=%.2f")
					TEXT(" Negative=%.2f Positive=%.2f Perpendicular=%.2f")
					TEXT(" ResultantCrossBearing=%d"),
					Node.MemberId,
					BestVoid != nullptr
						? BestVoid->SpanSourceVolumeId : INDEX_NONE,
					SpanAxis,
					SeatTopZ, NegativeStation, PositiveStation,
					SeatStation[Perpendicular],
					bResultantCrossBearing ? 1 : 0);
			}
		}
		if (OutAddedCount == 0)
		{
			for (const FABTSM73BeamCLoadNode& Node : Analysis.Nodes)
			{
				if (Node.bSupportResultantValid && Node.bSupportSpreadValid)
				{
					continue;
				}
				UE_LOG(LogABTSRuntime, Warning,
					TEXT("[ABTS][M7.3-Beam-C2][ClosureStalled]")
					TEXT(" Member=%d Axis=%d Role=%d Length=%.2f Supports=%d Resultant=%s")
					TEXT(" ResultantValid=%d SpreadValid=%d Coverage=%.3f Span=%.3f"),
					Node.MemberId, static_cast<int32>(Node.Axis),
					static_cast<int32>(Assembly.Members[Node.MemberId].Role),
					Assembly.Members[Node.MemberId].LengthCM, Node.SupportCount,
					*Node.LoadResultant.ToCompactString(),
					Node.bSupportResultantValid ? 1 : 0,
					Node.bSupportSpreadValid ? 1 : 0,
					Node.RealSupportCoverageRatio,
					Node.RealSupportSpanRatio);
			}
		}
		return OutAddedCount > 0;
	}
}

bool FABTSM73BeamCGenerator::Generate(
	const FABTSM73BeamCPreviewSettings& Settings,
	const FABTSM73BeamAGenerationResult& ClosedAssembly,
	FABTSM73BeamCGenerationResult& OutResult,
	FString& OutError,
	const FABTSM73BeamCMemberSelfLoadResolver* MemberSelfLoadResolver) const
{
	using namespace ABTSM73BeamC;
	OutResult = FABTSM73BeamCGenerationResult();
	OutError.Reset();

	if (!ClosedAssembly.Summary.bAccepted)
	{
		return Reject(OutResult, OutError, TEXT("BeamCUpstreamRejected"));
	}
	if (Settings.MemberLinearDensityKGPerCM <= 0.0f
		|| Settings.ReferenceLoadKG <= 0.0f
		|| Settings.ReferenceSpanCM <= 0.0f
		|| Settings.SpanStiffnessScale <= 0.0f
		|| Settings.MinimumBearingAreaRatio <= 0.0f
		|| Settings.MinimumBearingAreaRatio > 1.0f
		|| Settings.RealContactToleranceCM <= 0.0f
		|| Settings.RealContactAreaToleranceRatio <= 0.0f
		|| Settings.MaximumSingleSupportMemberLengthRatio < 1.0f
		|| Settings.MinimumSingleSupportCoverageRatio <= 0.0f
		|| Settings.MinimumSingleSupportCoverageRatio > 1.0f
		|| Settings.MinimumSeparatedSupportSpanRatio <= 0.0f
		|| Settings.MinimumSeparatedSupportSpanRatio > 1.0f
		|| Settings.SupportResultantMarginCM < 0.0f
		|| Settings.MaximumStructuralClosurePasses <= 0
		|| Settings.MaximumStructuralSupportPosts <= 0)
	{
		return Reject(OutResult, OutError, TEXT("BeamCInvalidSettings"));
	}
	if (ClosedAssembly.Members.IsEmpty())
	{
		return Reject(OutResult, OutError, TEXT("BeamCEmptyAssembly"));
	}
	if (ClosedAssembly.Members.Num() > Settings.MaximumLoadNodeCount)
	{
		return Reject(OutResult, OutError, TEXT("BeamCNodeBudgetExceeded"));
	}
	if (ClosedAssembly.BearingContacts.Num() > Settings.MaximumLoadEdgeCount)
	{
		return Reject(OutResult, OutError, TEXT("BeamCEdgeBudgetExceeded"));
	}

	const int32 NodeCount = ClosedAssembly.Members.Num();
	OutResult.Nodes.SetNum(NodeCount);
	TArray<FBox> MemberBoxes;
	MemberBoxes.Reserve(NodeCount);
	TArray<FVector> FirstMoments;
	FirstMoments.SetNumZeroed(NodeCount);
	int32 XCount = 0;
	int32 YCount = 0;
	int32 ZCount = 0;
	for (int32 MemberIndex = 0; MemberIndex < NodeCount; ++MemberIndex)
	{
		const FABTSM73BeamAMember& Member = ClosedAssembly.Members[MemberIndex];
		if (Member.MemberId != MemberIndex
			|| !ClosedAssembly.Joints.IsValidIndex(Member.JointA)
			|| !ClosedAssembly.Joints.IsValidIndex(Member.JointB)
			|| Member.LengthCM <= 0.0f)
		{
			return Reject(OutResult, OutError, TEXT("BeamCInvalidMember"));
		}
		FABTSM73BeamCLoadNode& Node = OutResult.Nodes[MemberIndex];
		Node.MemberId = Member.MemberId;
		Node.Axis = Member.Axis;
		Node.bGround = IsGroundMember(Member, ClosedAssembly, Settings);
		Node.SelfLoadKG = MemberSelfLoadResolver != nullptr
			? static_cast<float>((*MemberSelfLoadResolver)(Member))
			: Member.LengthCM * Settings.MemberLinearDensityKGPerCM;
		if (!FMath::IsFinite(Node.SelfLoadKG)
			|| Node.SelfLoadKG <= 0.0f)
		{
			return Reject(OutResult, OutError,
				TEXT("BeamCInvalidResolvedMemberSelfLoad"));
		}
		Node.AccumulatedLoadKG = Node.SelfLoadKG;
		Node.LoadResultant = MemberMidpoint(Member, ClosedAssembly);
		MemberBoxes.Add(MemberBounds(
			Member, ClosedAssembly,
			Settings.BeamB.BeamA.BlockCrossSectionCM));
		FirstMoments[MemberIndex] = Node.LoadResultant * Node.AccumulatedLoadKG;
		OutResult.Summary.TotalSelfLoadKG += Node.SelfLoadKG;
		OutResult.Summary.GroundNodeCount += Node.bGround ? 1 : 0;
		XCount += Member.Axis == EABTSM73BeamAFrameAxis::X ? 1 : 0;
		YCount += Member.Axis == EABTSM73BeamAFrameAxis::Y ? 1 : 0;
		ZCount += Member.Axis == EABTSM73BeamAFrameAxis::Z ? 1 : 0;
	}
	OutResult.Summary.LoadNodeCount = NodeCount;

	TArray<TArray<int32>> OutgoingEdges;
	TArray<TArray<int32>> ReverseReachability;
	OutgoingEdges.SetNum(NodeCount);
	ReverseReachability.SetNum(NodeCount);
	TArray<int32> InDegree;
	InDegree.Init(0, NodeCount);
	const double MinimumBearingArea =
		FMath::Square(static_cast<double>(Settings.BeamB.BeamA.BlockCrossSectionCM))
		* Settings.MinimumBearingAreaRatio;
	for (const FABTSM73BeamABearingContact& Contact : ClosedAssembly.BearingContacts)
	{
		if (!OutResult.Nodes.IsValidIndex(Contact.UpperMemberId)
			|| !OutResult.Nodes.IsValidIndex(Contact.LowerMemberId)
			|| Contact.UpperMemberId == Contact.LowerMemberId
			|| Contact.ContactAreaCM2 <= 0.0f)
		{
			return Reject(OutResult, OutError, TEXT("BeamCInvalidBearingContact"));
		}
		FABTSM73BeamCLoadEdge Edge;
		Edge.EdgeId = OutResult.Edges.Num();
		Edge.BearingContactId = Contact.ContactId;
		Edge.UpperMemberId = Contact.UpperMemberId;
		Edge.LowerMemberId = Contact.LowerMemberId;
		Edge.ContactPosition = Contact.LocalPosition;
		Edge.ContactAreaCM2 = Contact.ContactAreaCM2;
		const FBox& LowerBounds = MemberBoxes[Contact.LowerMemberId];
		const FBox& UpperBounds = MemberBoxes[Contact.UpperMemberId];
		const double XMinimum = FMath::Max(
			LowerBounds.Min.X, UpperBounds.Min.X);
		const double XMaximum = FMath::Min(
			LowerBounds.Max.X, UpperBounds.Max.X);
		const double YMinimum = FMath::Max(
			LowerBounds.Min.Y, UpperBounds.Min.Y);
		const double YMaximum = FMath::Min(
			LowerBounds.Max.Y, UpperBounds.Max.Y);
		const double XOverlap = OverlapLength(
			LowerBounds.Min.X, LowerBounds.Max.X,
			UpperBounds.Min.X, UpperBounds.Max.X);
		const double YOverlap = OverlapLength(
			LowerBounds.Min.Y, LowerBounds.Max.Y,
			UpperBounds.Min.Y, UpperBounds.Max.Y);
		const double ActualArea = XOverlap * YOverlap;
		const FVector ActualPosition(
			(XMinimum + XMaximum) * 0.5,
			(YMinimum + YMaximum) * 0.5,
			LowerBounds.Max.Z);
		const double AreaTolerance = FMath::Max(
			UE_DOUBLE_SMALL_NUMBER,
			ActualArea * Settings.RealContactAreaToleranceRatio);
		const bool bRealContact =
			FMath::Abs(LowerBounds.Max.Z - UpperBounds.Min.Z)
				<= Settings.RealContactToleranceCM
			&& XOverlap > 0.0 && YOverlap > 0.0
			&& FVector::Distance(ActualPosition, Contact.LocalPosition)
				<= Settings.RealContactToleranceCM
			&& FMath::Abs(ActualArea - Contact.ContactAreaCM2)
				<= AreaTolerance;
		if (!bRealContact)
		{
			if (OutResult.Summary.RealContactMismatchCount < 8)
			{
				UE_LOG(LogABTSRuntime, Warning,
					TEXT("[ABTS][M7.3-Beam-C2][RealContactMismatch]")
					TEXT(" Contact=%d Lower=%d Upper=%d DZ=%.4f DPos=%.4f")
					TEXT(" DeclaredArea=%.4f ActualArea=%.4f AreaTolerance=%.4f")
					TEXT(" XOverlap=%.4f YOverlap=%.4f"),
					Contact.ContactId,
					Contact.LowerMemberId,
					Contact.UpperMemberId,
					FMath::Abs(LowerBounds.Max.Z - UpperBounds.Min.Z),
					FVector::Distance(ActualPosition, Contact.LocalPosition),
					Contact.ContactAreaCM2,
					ActualArea,
					AreaTolerance,
					XOverlap,
					YOverlap);
			}
			++OutResult.Summary.RealContactMismatchCount;
		}
		if (XOverlap > 0.0 && YOverlap > 0.0)
		{
			Edge.ContactMinXY = FVector2D(XMinimum, YMinimum);
			Edge.ContactMaxXY = FVector2D(XMaximum, YMaximum);
		}
		else
		{
			const double HalfFallback = FMath::Sqrt(FMath::Max(
				static_cast<double>(Contact.ContactAreaCM2),
				UE_DOUBLE_SMALL_NUMBER)) * 0.5;
			Edge.ContactMinXY = FVector2D(
				Contact.LocalPosition.X - HalfFallback,
				Contact.LocalPosition.Y - HalfFallback);
			Edge.ContactMaxXY = FVector2D(
				Contact.LocalPosition.X + HalfFallback,
				Contact.LocalPosition.Y + HalfFallback);
		}
		if (Contact.ContactAreaCM2 + UE_DOUBLE_SMALL_NUMBER < MinimumBearingArea)
		{
			++OutResult.Summary.BearingAreaViolationCount;
		}
		OutgoingEdges[Edge.UpperMemberId].Add(Edge.EdgeId);
		ReverseReachability[Edge.LowerMemberId].Add(Edge.UpperMemberId);
		++InDegree[Edge.LowerMemberId];
		OutResult.Edges.Add(Edge);
	}
	OutResult.Summary.LoadEdgeCount = OutResult.Edges.Num();
	if (Settings.bRequireRealContactAgreement
		&& OutResult.Summary.RealContactMismatchCount > 0)
	{
		return Reject(OutResult, OutError, TEXT("BeamCRealContactMismatch"));
	}
	if (OutResult.Summary.BearingAreaViolationCount > 0)
	{
		return Reject(OutResult, OutError, TEXT("BeamCBearingAreaInsufficient"));
	}

	int32 TopologyOperations = 0;
	TArray<int32> Ready;
	for (int32 NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex)
	{
		if (InDegree[NodeIndex] == 0)
		{
			Ready.Add(NodeIndex);
		}
	}
	Ready.Sort();
	while (!Ready.IsEmpty())
	{
		const int32 MemberId = Ready[0];
		Ready.RemoveAt(0, EAllowShrinking::No);
		OutResult.TopologicalMemberOrder.Add(MemberId);
		for (const int32 EdgeIndex : OutgoingEdges[MemberId])
		{
			if (++TopologyOperations > Settings.MaximumTopologyOperationCount)
			{
				return Reject(OutResult, OutError,
					TEXT("BeamCTopologyBudgetExceeded"));
			}
			const int32 LowerId = OutResult.Edges[EdgeIndex].LowerMemberId;
			if (--InDegree[LowerId] == 0)
			{
				Ready.Add(LowerId);
				Ready.Sort();
			}
		}
	}
	if (OutResult.TopologicalMemberOrder.Num() != NodeCount)
	{
		OutResult.Summary.CycleNodeCount =
			NodeCount - OutResult.TopologicalMemberOrder.Num();
		return Reject(OutResult, OutError, TEXT("BeamCLoadDAGCycle"));
	}
	TArray<int32> GroundQueue;
	for (FABTSM73BeamCLoadNode& Node : OutResult.Nodes)
	{
		if (Node.bGround)
		{
			Node.bGroundReachable = true;
			GroundQueue.Add(Node.MemberId);
		}
	}
	for (int32 QueueIndex = 0; QueueIndex < GroundQueue.Num(); ++QueueIndex)
	{
		const int32 LowerId = GroundQueue[QueueIndex];
		for (const int32 UpperId : ReverseReachability[LowerId])
		{
			if (++TopologyOperations > Settings.MaximumTopologyOperationCount)
			{
				return Reject(OutResult, OutError,
					TEXT("BeamCTopologyBudgetExceeded"));
			}
			if (!OutResult.Nodes[UpperId].bGroundReachable)
			{
				OutResult.Nodes[UpperId].bGroundReachable = true;
				GroundQueue.Add(UpperId);
			}
		}
	}
	for (const FABTSM73BeamCLoadNode& Node : OutResult.Nodes)
	{
		OutResult.Summary.GroundUnreachableNodeCount +=
			Node.bGroundReachable ? 0 : 1;
	}
	if (OutResult.Summary.GroundUnreachableNodeCount > 0)
	{
		return Reject(OutResult, OutError, TEXT("BeamCGroundUnreachable"));
	}
	const double StationTolerance =
		FMath::Max(0.01, static_cast<double>(Settings.BeamB.BeamA.JointMergeToleranceCM));
	for (const int32 MemberId : OutResult.TopologicalMemberOrder)
	{
		FABTSM73BeamCLoadNode& Node = OutResult.Nodes[MemberId];
		const FABTSM73BeamAMember& Member = ClosedAssembly.Members[MemberId];
		Node.LoadResultant = FirstMoments[MemberId]
			/ FMath::Max(static_cast<double>(Node.AccumulatedLoadKG), UE_DOUBLE_SMALL_NUMBER);
		if (Member.Axis == EABTSM73BeamAFrameAxis::Z)
		{
			Node.ColumnSlenderness = Member.LengthCM
				/ FMath::Max(Settings.BeamB.BeamA.BlockCrossSectionCM, 1.0f);
			OutResult.Summary.MaximumObservedColumnSlenderness = FMath::Max(
				OutResult.Summary.MaximumObservedColumnSlenderness,
				Node.ColumnSlenderness);
			if (Node.ColumnSlenderness > Settings.MaximumColumnSlenderness)
			{
				++OutResult.Summary.ColumnSlendernessViolationCount;
			}
		}
		if (Node.bGround)
		{
			continue;
		}
		const TArray<int32>& SupportEdges = OutgoingEdges[MemberId];
		Node.SupportCount = SupportEdges.Num();
		if (SupportEdges.IsEmpty())
		{
			++OutResult.Summary.ReactionBalanceViolationCount;
			continue;
		}
		if (Member.Axis == EABTSM73BeamAFrameAxis::X
			|| Member.Axis == EABTSM73BeamAFrameAxis::Y)
		{
			const int32 ResultantAxisIndex =
				Member.Axis == EABTSM73BeamAFrameAxis::X ? 0 : 1;
			double SupportMinimum = TNumericLimits<double>::Max();
			double SupportMaximum = -TNumericLimits<double>::Max();
			for (const int32 EdgeIndex : SupportEdges)
			{
				const FABTSM73BeamCLoadEdge& Edge = OutResult.Edges[EdgeIndex];
				SupportMinimum = FMath::Min(SupportMinimum,
					ResultantAxisIndex == 0
						? Edge.ContactMinXY.X : Edge.ContactMinXY.Y);
				SupportMaximum = FMath::Max(SupportMaximum,
					ResultantAxisIndex == 0
						? Edge.ContactMaxXY.X : Edge.ContactMaxXY.Y);
			}
			const double ResultantCoordinate =
				Node.LoadResultant[ResultantAxisIndex];
			if (ResultantCoordinate
					< SupportMinimum + Settings.SupportResultantMarginCM
				|| ResultantCoordinate
					> SupportMaximum - Settings.SupportResultantMarginCM)
			{
				Node.bSupportResultantValid = false;
				++OutResult.Summary.SupportResultantViolationCount;
			}
			const int32 AxisIndex = Member.Axis == EABTSM73BeamAFrameAxis::X ? 0 : 1;
			TArray<FSupportStation> Stations;
			for (const int32 EdgeIndex : SupportEdges)
			{
				const double Coordinate = OutResult.Edges[EdgeIndex].ContactPosition[AxisIndex];
				FSupportStation* Existing = Stations.FindByPredicate(
					[Coordinate, StationTolerance](const FSupportStation& Station)
					{
						return FMath::Abs(Station.Coordinate - Coordinate) <= StationTolerance;
					});
				if (Existing == nullptr)
				{
					FSupportStation& NewStation = Stations.AddDefaulted_GetRef();
					NewStation.Coordinate = Coordinate;
					Existing = &NewStation;
				}
				Existing->EdgeIndices.Add(EdgeIndex);
				Existing->AreaCM2 += OutResult.Edges[EdgeIndex].ContactAreaCM2;
			}
			Stations.Sort([](const FSupportStation& A, const FSupportStation& B)
			{
				return A.Coordinate < B.Coordinate;
			});
			TArray<FSupportInterval> SupportIntervals;
			SupportIntervals.Reserve(SupportEdges.Num());
			for (const int32 EdgeIndex : SupportEdges)
			{
				const FABTSM73BeamCLoadEdge& Edge = OutResult.Edges[EdgeIndex];
				FSupportInterval& Interval =
					SupportIntervals.AddDefaulted_GetRef();
				Interval.Minimum = AxisIndex == 0
					? Edge.ContactMinXY.X : Edge.ContactMinXY.Y;
				Interval.Maximum = AxisIndex == 0
					? Edge.ContactMaxXY.X : Edge.ContactMaxXY.Y;
			}
			SupportIntervals.Sort([](
				const FSupportInterval& A,
				const FSupportInterval& B)
			{
				return A.Minimum < B.Minimum
					|| (A.Minimum == B.Minimum && A.Maximum < B.Maximum);
			});
			TArray<FSupportInterval> MergedIntervals;
			for (const FSupportInterval& Interval : SupportIntervals)
			{
				if (MergedIntervals.IsEmpty()
					|| Interval.Minimum
						> MergedIntervals.Last().Maximum + StationTolerance)
				{
					MergedIntervals.Add(Interval);
				}
				else
				{
					MergedIntervals.Last().Maximum = FMath::Max(
						MergedIntervals.Last().Maximum, Interval.Maximum);
				}
			}
			double TotalSupportLength = 0.0;
			for (const FSupportInterval& Interval : MergedIntervals)
			{
				TotalSupportLength += FMath::Max(
					0.0, Interval.Maximum - Interval.Minimum);
			}
			const double SupportSpan = MergedIntervals.IsEmpty()
				? 0.0
				: MergedIntervals.Last().Maximum
					- MergedIntervals[0].Minimum;
			Node.RealSupportIntervalCount = MergedIntervals.Num();
			Node.RealSupportCoverageRatio = static_cast<float>(
				TotalSupportLength
				/ FMath::Max(static_cast<double>(Member.LengthCM), 1.0));
			Node.RealSupportSpanRatio = static_cast<float>(
				SupportSpan
				/ FMath::Max(static_cast<double>(Member.LengthCM), 1.0));
			const double LongMemberThreshold =
				Settings.BeamB.BeamA.BlockCrossSectionCM
					* Settings.MaximumSingleSupportMemberLengthRatio;
			if (Member.LengthCM > LongMemberThreshold)
			{
				const bool bHasHorizontalBearing = SupportEdges.ContainsByPredicate(
					[&OutResult, &ClosedAssembly](const int32 EdgeIndex)
					{
						const int32 LowerId = OutResult.Edges[EdgeIndex].LowerMemberId;
						return ClosedAssembly.Members.IsValidIndex(LowerId)
							&& ClosedAssembly.Members[LowerId].Axis
								!= EABTSM73BeamAFrameAxis::Z
							&& ClosedAssembly.Members[LowerId].Axis
								!= EABTSM73BeamAFrameAxis::Diagonal;
					});
				const bool bContinuousBearing = MergedIntervals.Num() == 1
					&& (Node.RealSupportCoverageRatio
							>= Settings.MinimumSingleSupportCoverageRatio
						|| (bHasHorizontalBearing
							&& Node.bSupportResultantValid));
				const bool bSeparatedBearing = MergedIntervals.Num() >= 2
					&& Node.RealSupportSpanRatio
						>= Settings.MinimumSeparatedSupportSpanRatio;
				if (!bContinuousBearing && !bSeparatedBearing)
				{
					Node.bSupportSpreadValid = false;
					++OutResult.Summary.SupportSpreadViolationCount;
				}
			}
			if (Stations.Num() == 1 || ResultantCoordinate <= Stations[0].Coordinate)
			{
				SplitStationShare(Stations[0], 1.0, OutResult.Edges);
			}
			else if (ResultantCoordinate >= Stations.Last().Coordinate)
			{
				SplitStationShare(Stations.Last(), 1.0, OutResult.Edges);
			}
			else
			{
				for (int32 StationIndex = 0; StationIndex + 1 < Stations.Num(); ++StationIndex)
				{
					const FSupportStation& Left = Stations[StationIndex];
					const FSupportStation& Right = Stations[StationIndex + 1];
					if (ResultantCoordinate >= Left.Coordinate
						&& ResultantCoordinate <= Right.Coordinate)
					{
						const double Denominator = FMath::Max(
							Right.Coordinate - Left.Coordinate, StationTolerance);
						const double RightShare =
							(ResultantCoordinate - Left.Coordinate) / Denominator;
						SplitStationShare(Left, 1.0 - RightShare, OutResult.Edges);
						SplitStationShare(Right, RightShare, OutResult.Edges);
						break;
					}
				}
			}

			const FVector A = MemberStart(Member, ClosedAssembly);
			const FVector B = MemberEnd(Member, ClosedAssembly);
			const double Minimum = FMath::Min(A[AxisIndex], B[AxisIndex]);
			const double Maximum = FMath::Max(A[AxisIndex], B[AxisIndex]);
			double EffectiveSpan = MergedIntervals.IsEmpty()
				? Member.LengthCM
				: FMath::Max(
					FMath::Max(0.0, MergedIntervals[0].Minimum - Minimum),
					FMath::Max(0.0, Maximum - MergedIntervals.Last().Maximum));
			for (int32 IntervalIndex = 0;
				IntervalIndex + 1 < MergedIntervals.Num(); ++IntervalIndex)
			{
				EffectiveSpan = FMath::Max(EffectiveSpan,
					MergedIntervals[IntervalIndex + 1].Minimum
						- MergedIntervals[IntervalIndex].Maximum);
			}
			const double Overhang = MergedIntervals.IsEmpty()
				? Member.LengthCM
				: FMath::Max(
					FMath::Max(0.0, MergedIntervals[0].Minimum - Minimum),
					FMath::Max(0.0, Maximum - MergedIntervals.Last().Maximum));
			Node.EffectiveSpanCM = static_cast<float>(EffectiveSpan);
			Node.CantileverRatio = static_cast<float>(
				Overhang / FMath::Max(static_cast<double>(Member.LengthCM), 1.0));
			const double LoadRatio = Node.AccumulatedLoadKG / Settings.ReferenceLoadKG;
			const double SpanRatio = EffectiveSpan / Settings.ReferenceSpanCM;
			Node.SpanUtilization = static_cast<float>(
				LoadRatio * FMath::Square(SpanRatio) / Settings.SpanStiffnessScale);
			OutResult.Summary.MaximumObservedSpanUtilization = FMath::Max(
				OutResult.Summary.MaximumObservedSpanUtilization,
				Node.SpanUtilization);
			if (EffectiveSpan > Settings.MaximumUnsupportedSpanCM
				|| Node.SpanUtilization > Settings.MaximumSpanUtilization)
			{
				++OutResult.Summary.SpanViolationCount;
			}
			if (Node.CantileverRatio > Settings.MaximumCantileverRatio)
			{
				++OutResult.Summary.CantileverViolationCount;
			}
		}
		else
		{
			double TotalArea = 0.0;
			for (const int32 EdgeIndex : SupportEdges)
			{
				TotalArea += OutResult.Edges[EdgeIndex].ContactAreaCM2;
			}
			for (const int32 EdgeIndex : SupportEdges)
			{
				OutResult.Edges[EdgeIndex].LoadShare = static_cast<float>(
					OutResult.Edges[EdgeIndex].ContactAreaCM2
					/ FMath::Max(TotalArea, UE_DOUBLE_SMALL_NUMBER));
			}
		}

		double ShareSum = 0.0;
		double ReactionSum = 0.0;
		for (const int32 EdgeIndex : SupportEdges)
		{
			FABTSM73BeamCLoadEdge& Edge = OutResult.Edges[EdgeIndex];
			Edge.ReactionLoadKG = Edge.LoadShare * Node.AccumulatedLoadKG;
			ShareSum += Edge.LoadShare;
			ReactionSum += Edge.ReactionLoadKG;
			FABTSM73BeamCLoadNode& LowerNode = OutResult.Nodes[Edge.LowerMemberId];
			LowerNode.AccumulatedLoadKG += Edge.ReactionLoadKG;
			FirstMoments[Edge.LowerMemberId] +=
				Edge.ContactPosition * Edge.ReactionLoadKG;
		}
		const double ReactionTolerance = FMath::Max(
			0.01, static_cast<double>(Node.AccumulatedLoadKG) * 1.0e-4);
		if (!FMath::IsNearlyEqual(ShareSum, 1.0, 1.0e-4)
			|| !FMath::IsNearlyEqual(ReactionSum,
				static_cast<double>(Node.AccumulatedLoadKG), ReactionTolerance))
		{
			++OutResult.Summary.ReactionBalanceViolationCount;
		}
	}
	for (const FABTSM73BeamCLoadNode& Node : OutResult.Nodes)
	{
		if (Node.bGround)
		{
			OutResult.Summary.TotalGroundReactionKG += Node.AccumulatedLoadKG;
		}
	}
	const double GroundTolerance = FMath::Max(
		0.05, static_cast<double>(OutResult.Summary.TotalSelfLoadKG) * 1.0e-4);
	if (!FMath::IsNearlyEqual(
		static_cast<double>(OutResult.Summary.TotalSelfLoadKG),
		static_cast<double>(OutResult.Summary.TotalGroundReactionKG),
		GroundTolerance))
	{
		++OutResult.Summary.ReactionBalanceViolationCount;
	}
	if (Settings.bRequireBidirectionalLateralTies
		&& ZCount > 0 && (XCount == 0 || YCount == 0))
	{
		++OutResult.Summary.LateralMechanismViolationCount;
	}

	if (OutResult.Summary.ReactionBalanceViolationCount > 0)
	{
		return Reject(OutResult, OutError, TEXT("BeamCReactionBalanceFailed"));
	}
	if (OutResult.Summary.SupportResultantViolationCount > 0)
	{
		return Reject(OutResult, OutError,
			TEXT("BeamCSupportResultantOutsideHull"));
	}
	if (OutResult.Summary.SupportSpreadViolationCount > 0)
	{
		return Reject(OutResult, OutError,
			TEXT("BeamCSupportSpreadInsufficient"));
	}
	if (OutResult.Summary.SpanViolationCount > 0)
	{
		return Reject(OutResult, OutError, TEXT("BeamCSpanLimitExceeded"));
	}
	if (OutResult.Summary.CantileverViolationCount > 0)
	{
		return Reject(OutResult, OutError, TEXT("BeamCCantileverLimitExceeded"));
	}
	if (OutResult.Summary.ColumnSlendernessViolationCount > 0)
	{
		return Reject(OutResult, OutError,
			TEXT("BeamCColumnSlendernessExceeded"));
	}
	if (OutResult.Summary.LateralMechanismViolationCount > 0)
	{
		return Reject(OutResult, OutError, TEXT("BeamCLateralMechanism"));
	}

	OutResult.Summary.bAccepted = true;
	OutResult.Summary.LoadDAGHash = static_cast<int64>(HashResult(OutResult));
	return true;
}

bool FABTSM73BeamCGenerator::GenerateWithStructuralClosure(
	const FABTSM73BeamCPreviewSettings& Settings,
	FABTSM73BeamAGenerationResult& InOutClosedAssembly,
	FABTSM73BeamCGenerationResult& OutResult,
	FString& OutError,
	const int32 MaximumFinalMemberCount,
	const bool bAllowDeferredCoreBracing,
	const int32 PriorStructuralClosurePassCount,
	const int32 PriorAddedStructuralSupportPostCount,
	const FABTSM73BeamCMemberSelfLoadResolver* MemberSelfLoadResolver,
	const bool bAllowPostRepairResultantAdvisory) const
{
	using namespace ABTSM73BeamC;
	auto RejectMemberBudget = [&OutResult, &OutError,
		MaximumFinalMemberCount](const int32 ActualMemberCount)
	{
		OutResult.Summary.bAccepted = false;
		OutResult.Summary.RejectReason = TEXT("BeamCFinalMemberBudgetExceeded");
		OutError = FString::Printf(
			TEXT("BeamCFinalMemberBudgetExceeded:%d>%d"),
			ActualMemberCount, MaximumFinalMemberCount);
		return false;
	};
	if (InOutClosedAssembly.Members.Num() > MaximumFinalMemberCount)
	{
		return RejectMemberBudget(InOutClosedAssembly.Members.Num());
	}
	if (PriorStructuralClosurePassCount < 0
		|| PriorAddedStructuralSupportPostCount < 0
		|| PriorStructuralClosurePassCount
			> Settings.MaximumStructuralClosurePasses
		|| PriorAddedStructuralSupportPostCount
			> Settings.MaximumStructuralSupportPosts)
	{
		OutError = TEXT("BeamCStructuralClosureLedgerInvalid");
		return false;
	}
	int32 TotalAddedPosts = PriorAddedStructuralSupportPostCount;
	TOptional<uint32> PreviousFailedAnalysisHash;
	TSet<uint32> SeenFailedAnalysisHashes;
	TSet<uint32> AttemptedRootedGrillageHashes;
	TArray<ABTSM73BeamC::FRootedTwinLaneAttempt> PreviousTwinAttempts;
	const int32 RemainingClosurePasses =
		Settings.MaximumStructuralClosurePasses
		- PriorStructuralClosurePassCount;
	for (int32 Pass = 0; Pass <= RemainingClosurePasses; ++Pass)
	{
		const int32 CumulativePass =
			PriorStructuralClosurePassCount + Pass;
		if (Generate(Settings, InOutClosedAssembly, OutResult, OutError,
			MemberSelfLoadResolver))
		{
			if (InOutClosedAssembly.Members.Num() > MaximumFinalMemberCount)
			{
				return RejectMemberBudget(InOutClosedAssembly.Members.Num());
			}
			OutResult.Summary.StructuralClosurePassCount = CumulativePass;
			OutResult.Summary.AddedStructuralSupportPostCount = TotalAddedPosts;
			return true;
		}
		const bool bRepairable =
			OutResult.Summary.SupportResultantViolationCount > 0
			|| OutResult.Summary.SupportSpreadViolationCount > 0;
		const bool bResultantOnlyAfterRepair =
			bAllowPostRepairResultantAdvisory
			&& CumulativePass > 0
			&& OutResult.Summary.SupportResultantViolationCount > 0
			&& OutResult.Summary.SupportSpreadViolationCount == 0
			&& OutResult.Summary.ReactionBalanceViolationCount == 0
			&& OutResult.Summary.SpanViolationCount == 0
			&& OutResult.Summary.CantileverViolationCount == 0
			&& OutResult.Summary.ColumnSlendernessViolationCount == 0
			&& OutResult.Summary.LateralMechanismViolationCount == 0;
		if (bResultantOnlyAfterRepair)
		{
			OutResult.Summary.StructuralClosurePassCount = CumulativePass;
			OutResult.Summary.AddedStructuralSupportPostCount = TotalAddedPosts;
			OutResult.Summary.SupportResultantAdvisoryCount =
				OutResult.Summary.SupportResultantViolationCount;
			OutResult.Summary.SupportResultantViolationCount = 0;
			OutResult.Summary.bAccepted = true;
			OutResult.Summary.RejectReason.Reset();
			OutError.Reset();
			OutResult.Summary.LoadDAGHash = static_cast<int64>(
				HashResult(OutResult));
			return true;
		}
		const uint32 FailedAnalysisHash = HashResult(OutResult);
		bool bRepeatedFailedAnalysis = false;
		if (bRepairable
			&& !ABTSM73BeamC::TryObserveStructuralClosureFailure(
				FailedAnalysisHash, PreviousFailedAnalysisHash,
				SeenFailedAnalysisHashes, bRepeatedFailedAnalysis, OutError))
		{
			OutResult.Summary.StructuralClosurePassCount = CumulativePass;
			OutResult.Summary.AddedStructuralSupportPostCount = TotalAddedPosts;
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][M7.3-Beam-C2][ClosureCycleNoProgress]")
				TEXT(" Pass=%d Added=%d Members=%d Bearings=%d Hash=%u"),
				CumulativePass, TotalAddedPosts,
				InOutClosedAssembly.Members.Num(),
				InOutClosedAssembly.BearingContacts.Num(),
				FailedAnalysisHash);
			return false;
		}
		if (bRepeatedFailedAnalysis)
		{
			OutResult.Summary.StructuralClosurePassCount = CumulativePass;
			OutResult.Summary.AddedStructuralSupportPostCount = TotalAddedPosts;
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][M7.3-Beam-C2][ClosureNoProgress]")
				TEXT(" Pass=%d Added=%d Members=%d Bearings=%d Hash=%u"),
				CumulativePass, TotalAddedPosts,
				InOutClosedAssembly.Members.Num(),
				InOutClosedAssembly.BearingContacts.Num(),
				FailedAnalysisHash);
			for (const FABTSM73BeamCLoadNode& Node : OutResult.Nodes)
			{
				if (Node.bSupportResultantValid && Node.bSupportSpreadValid)
				{
					continue;
				}
				UE_LOG(LogABTSRuntime, Warning,
					TEXT("[ABTS][M7.3-Beam-C2][ClosureNoProgressNode]")
					TEXT(" Member=%d Axis=%d Role=%d Length=%.2f Supports=%d Resultant=%s")
					TEXT(" ResultantValid=%d SpreadValid=%d Coverage=%.3f Span=%.3f"),
					Node.MemberId, static_cast<int32>(Node.Axis),
					static_cast<int32>(InOutClosedAssembly.Members[Node.MemberId].Role),
					InOutClosedAssembly.Members[Node.MemberId].LengthCM,
					Node.SupportCount,
					*Node.LoadResultant.ToCompactString(),
					Node.bSupportResultantValid ? 1 : 0,
					Node.bSupportSpreadValid ? 1 : 0,
					Node.RealSupportCoverageRatio,
					Node.RealSupportSpanRatio);
				const FBox FailedBounds = MemberBounds(
					InOutClosedAssembly.Members[Node.MemberId],
					InOutClosedAssembly,
					Settings.BeamB.BeamA.BlockCrossSectionCM);
				UE_LOG(LogABTSRuntime, Warning,
					TEXT("[ABTS][M7.3-Beam-C2][ClosureNoProgressBounds]")
					TEXT(" Member=%d Min=%s Max=%s"),
					Node.MemberId, *FailedBounds.Min.ToCompactString(),
					*FailedBounds.Max.ToCompactString());
				for (const FABTSM73BeamCLoadEdge& Edge : OutResult.Edges)
				{
					if (Edge.UpperMemberId != Node.MemberId
						|| !InOutClosedAssembly.Members.IsValidIndex(
							Edge.LowerMemberId))
					{
						continue;
					}
					const FBox LowerBounds = MemberBounds(
						InOutClosedAssembly.Members[Edge.LowerMemberId],
						InOutClosedAssembly,
						Settings.BeamB.BeamA.BlockCrossSectionCM);
					UE_LOG(LogABTSRuntime, Warning,
						TEXT("[ABTS][M7.3-Beam-C2][ClosureNoProgressSupport]")
						TEXT(" Upper=%d Lower=%d ContactMin=%s ContactMax=%s")
						TEXT(" LowerMin=%s LowerMax=%s"),
						Node.MemberId, Edge.LowerMemberId,
						*Edge.ContactMinXY.ToString(),
						*Edge.ContactMaxXY.ToString(),
						*LowerBounds.Min.ToCompactString(),
						*LowerBounds.Max.ToCompactString());
				}
				for (const FABTSM73BeamASupportVoid& SupportVoid :
					InOutClosedAssembly.ReservedSupportVoids)
				{
					if (OverlapLength(FailedBounds.Min.X, FailedBounds.Max.X,
							SupportVoid.Bounds.Min.X,
							SupportVoid.Bounds.Max.X)
							<= Settings.BeamB.BeamA.JointMergeToleranceCM
						|| OverlapLength(FailedBounds.Min.Y, FailedBounds.Max.Y,
							SupportVoid.Bounds.Min.Y,
							SupportVoid.Bounds.Max.Y)
							<= Settings.BeamB.BeamA.JointMergeToleranceCM)
					{
						continue;
					}
					UE_LOG(LogABTSRuntime, Warning,
						TEXT("[ABTS][M7.3-Beam-C2][ClosureNoProgressVoid]")
						TEXT(" Member=%d Source=%d Axis=%d Min=%s Max=%s"),
						Node.MemberId, SupportVoid.SpanSourceVolumeId,
						SupportVoid.SpanAxisIndex,
						*SupportVoid.Bounds.Min.ToCompactString(),
						*SupportVoid.Bounds.Max.ToCompactString());
				}
				for (const FABTSM73BeamAMember& Candidate :
					InOutClosedAssembly.Members)
				{
					if (Candidate.MemberId == Node.MemberId
						|| Candidate.Axis == EABTSM73BeamAFrameAxis::Z
						|| Candidate.Axis == EABTSM73BeamAFrameAxis::Diagonal)
					{
						continue;
					}
					const FBox CandidateBounds = MemberBounds(
						Candidate, InOutClosedAssembly,
						Settings.BeamB.BeamA.BlockCrossSectionCM);
					if (FMath::Abs(CandidateBounds.Max.Z - FailedBounds.Min.Z)
							> Settings.BeamB.BeamA.JointMergeToleranceCM
						|| OverlapLength(CandidateBounds.Min.X,
							CandidateBounds.Max.X,
							FailedBounds.Min.X, FailedBounds.Max.X)
							<= Settings.BeamB.BeamA.JointMergeToleranceCM
						|| OverlapLength(CandidateBounds.Min.Y,
							CandidateBounds.Max.Y,
							FailedBounds.Min.Y, FailedBounds.Max.Y)
							<= Settings.BeamB.BeamA.JointMergeToleranceCM)
					{
						continue;
					}
					UE_LOG(LogABTSRuntime, Warning,
						TEXT("[ABTS][M7.3-Beam-C2][ClosureNoProgressBearingCandidate]")
						TEXT(" Upper=%d Candidate=%d Axis=%d Role=%d Min=%s Max=%s"),
						Node.MemberId, Candidate.MemberId,
						static_cast<int32>(Candidate.Axis),
						static_cast<int32>(Candidate.Role),
						*CandidateBounds.Min.ToCompactString(),
						*CandidateBounds.Max.ToCompactString());
				}
			}
		}
		if (bRepairable
			&& !ABTSM73BeamC::TryCheckRootedGrillageRepairAvailable(
				FailedAnalysisHash, AttemptedRootedGrillageHashes, OutError))
		{
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][M7.3-Beam-C2][RootedGrillageNoProgress]")
				TEXT(" Pass=%d Members=%d Bearings=%d Hash=%u"),
				CumulativePass, InOutClosedAssembly.Members.Num(),
				InOutClosedAssembly.BearingContacts.Num(),
				FailedAnalysisHash);
			return false;
		}
		PreviousFailedAnalysisHash = bRepairable
			? TOptional<uint32>(FailedAnalysisHash) : TOptional<uint32>();
		if (!bRepairable || Pass == RemainingClosurePasses)
		{
			OutResult.Summary.StructuralClosurePassCount = CumulativePass;
			OutResult.Summary.AddedStructuralSupportPostCount = TotalAddedPosts;
			if (bRepairable)
			{
				for (const FABTSM73BeamCLoadNode& Node : OutResult.Nodes)
				{
					if (Node.bSupportResultantValid && Node.bSupportSpreadValid)
					{
						continue;
					}
					UE_LOG(LogABTSRuntime, Warning,
						TEXT("[ABTS][M7.3-Beam-C2][ClosureExhausted]")
						TEXT(" Pass=%d Added=%d Member=%d Axis=%d Supports=%d")
						TEXT(" Resultant=%s ResultantValid=%d SpreadValid=%d")
						TEXT(" Coverage=%.3f Span=%.3f"),
						CumulativePass, TotalAddedPosts, Node.MemberId,
						static_cast<int32>(Node.Axis), Node.SupportCount,
						*Node.LoadResultant.ToCompactString(),
						Node.bSupportResultantValid ? 1 : 0,
						Node.bSupportSpreadValid ? 1 : 0,
						Node.RealSupportCoverageRatio,
						Node.RealSupportSpanRatio);
				}
			}
			return false;
		}
		const bool bForceRootedGrillage =
			ABTSM73BeamC::ShouldForceRootedGrillageRepair(
				bRepeatedFailedAnalysis, PreviousTwinAttempts.Num());
		int32 AddedThisPass = 0;
		TArray<ABTSM73BeamC::FRootedTwinLaneAttempt> ThisPassTwinAttempts;
		bool bAddedRootedGrillage = false;
		FString RepairError;
		const int32 RemainingMemberCapacity =
			MaximumFinalMemberCount - InOutClosedAssembly.Members.Num();
		const int32 RemainingConfiguredSupportCapacity =
			Settings.MaximumStructuralSupportPosts - TotalAddedPosts;
		const int32 RemainingSupportCapacity = FMath::Min(
			RemainingConfiguredSupportCapacity,
			RemainingMemberCapacity);
		if (RemainingSupportCapacity <= 0)
		{
			return RejectMemberBudget(
				InOutClosedAssembly.Members.Num() + 1);
		}
		if (!AddStructuralSupportPosts(
			Settings, OutResult, InOutClosedAssembly,
			RemainingSupportCapacity,
			bAllowDeferredCoreBracing,
			CumulativePass > 0,
			bRepeatedFailedAnalysis,
			bForceRootedGrillage,
			PreviousTwinAttempts,
			ThisPassTwinAttempts,
			bAddedRootedGrillage,
			AddedThisPass, RepairError))
		{
			if (RepairError.StartsWith(
				TEXT("BeamCStructuralSupportBudgetExceeded")))
			{
				RepairError += FString::Printf(
					TEXT(":SupportRemaining=%d:MemberRemaining=%d:Members=%d/%d:Added=%d/%d"),
					RemainingConfiguredSupportCapacity,
					RemainingMemberCapacity,
					InOutClosedAssembly.Members.Num(),
					MaximumFinalMemberCount,
					TotalAddedPosts,
					Settings.MaximumStructuralSupportPosts);
			}
			OutError = RepairError.IsEmpty()
				? TEXT("BeamCStructuralClosureStalled") : RepairError;
			return false;
		}
		if (!ABTSM73BeamC::TryCommitAddedRootedGrillageRepair(
			FailedAnalysisHash, bAddedRootedGrillage,
			AttemptedRootedGrillageHashes, OutError))
		{
			return false;
		}
		if (bAddedRootedGrillage)
		{
			UE_LOG(LogABTSRuntime, Display,
				TEXT("[ABTS][M7.3-Beam-C2][RootedGrillageCommitted]")
				TEXT(" Pass=%d Added=%d Hash=%u"),
				CumulativePass, AddedThisPass, FailedAnalysisHash);
		}
		TotalAddedPosts += AddedThisPass;
		if (InOutClosedAssembly.Members.Num() > MaximumFinalMemberCount)
		{
			return RejectMemberBudget(InOutClosedAssembly.Members.Num());
		}
		if (!ABTSM73BeamA::CloseGeneratedAssembly(
			Settings.BeamB.BeamA, InOutClosedAssembly, RepairError))
		{
			OutError = FString::Printf(
				TEXT("BeamCStructuralReclose:%s"), *RepairError);
			return false;
		}
		PreviousTwinAttempts = MoveTemp(ThisPassTwinAttempts);
		if (InOutClosedAssembly.Members.Num() > MaximumFinalMemberCount)
		{
			return RejectMemberBudget(InOutClosedAssembly.Members.Num());
		}
	}
	return false;
}
