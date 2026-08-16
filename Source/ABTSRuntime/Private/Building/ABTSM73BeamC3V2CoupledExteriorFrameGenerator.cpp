// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSM73BeamC3V2CoupledExteriorFrameGenerator.h"

#include "ABTSRuntime.h"
#include "Misc/Crc.h"

namespace ABTSM73BeamC3V2
{
	namespace
	{
		constexpr double SectionCM = 36.0;
		constexpr double LaneQuantizationCM = SectionCM * 0.5;
		constexpr double PreferredCoreSpanCM = SectionCM * 12.0;

		bool Reject(
			FCoupledExteriorFrameResult& OutResult,
			FString& OutError,
			const FString& Reason)
		{
			OutResult = FCoupledExteriorFrameResult();
			OutResult.Summary.RejectReason = Reason;
			OutError = Reason;
			return false;
		}

		int32 QuantizeMillimeters(const double ValueCM)
		{
			return FMath::RoundToInt(ValueCM * 10.0);
		}

		double QuantizeSpanDown(const double ValueCM)
		{
			// A whole-section span keeps the symmetric outer-lane envelope at an
			// even number of 18 cm slots. Every lane center therefore remains on
			// the same s/2 lattice instead of landing on a half-slot.
			return FMath::FloorToDouble(ValueCM / SectionCM) * SectionCM;
		}

		void BuildQuantizedLaneCenters(
			const double Center,
			const double CoreSpan,
			const int32 RailCount,
			TArray<double>& OutCenters)
		{
			OutCenters.Reset();
			OutCenters.Reserve(RailCount);
			const int32 EnvelopeSlotCount = FMath::RoundToInt(
				(CoreSpan - SectionCM) / LaneQuantizationCM);
			for (int32 RailIndex = 0; RailIndex < RailCount; ++RailIndex)
			{
				const int32 Slot = RailIndex == RailCount - 1
					? EnvelopeSlotCount
					: FMath::RoundToInt(
						static_cast<double>(RailIndex * EnvelopeSlotCount)
						/ static_cast<double>(RailCount - 1));
				const double Offset =
					(Slot - EnvelopeSlotCount * 0.5) * LaneQuantizationCM;
				OutCenters.Add(Center + Offset);
			}
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

		FBox BoundsForSegment(
			const FVector& Start,
			const FVector& End,
			const EABTSM73BeamAFrameAxis Axis)
		{
			const FVector Center = (Start + End) * 0.5;
			FVector Extent(SectionCM * 0.5);
			Extent[static_cast<int32>(Axis)] = FVector::Distance(Start, End) * 0.5;
			return FBox(Center - Extent, Center + Extent);
		}

		FBox MemberBounds(
			const FABTSM73BeamAMember& Member,
			const FABTSM73BeamAGenerationResult& Assembly)
		{
			if (!Assembly.Joints.IsValidIndex(Member.JointA)
				|| !Assembly.Joints.IsValidIndex(Member.JointB))
			{
				return FBox(EForceInit::ForceInit);
			}
			return BoundsForSegment(
				Assembly.Joints[Member.JointA].LocalPosition,
				Assembly.Joints[Member.JointB].LocalPosition,
				Member.Axis);
		}

		bool HasPositiveOverlap(
			const FBox& A,
			const FBox& B,
			const double Tolerance)
		{
			return FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X)
				> Tolerance
				&& FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y)
					> Tolerance
				&& FMath::Min(A.Max.Z, B.Max.Z) - FMath::Max(A.Min.Z, B.Min.Z)
					> Tolerance;
		}

		bool IsProtectedRole(const EABTSM73BeamAMemberRole Role)
		{
			return Role == EABTSM73BeamAMemberRole::RoofCourse
				|| Role == EABTSM73BeamAMemberRole::BridgeSeat
				|| Role == EABTSM73BeamAMemberRole::BridgeRail
				|| Role == EABTSM73BeamAMemberRole::BridgePost;
		}

		bool AddPlannedMember(
			FCoupledExteriorFrameCellPlan& Plan,
			const ECoupledExteriorFrameMemberKind Kind,
			const int32 CourseIndex,
			const int32 MacroBandIndex,
			const int32 RailIndex,
			const uint8 FaceMask,
			const EABTSM73BeamAFrameAxis Axis,
			const FVector& Start,
			const FVector& End,
			const FABTSM73BeamC3V2CoupledExteriorFrameSettings& Settings,
			FString& OutError)
		{
			const double Length = FVector::Distance(Start, End);
			if (!FMath::IsFinite(Length)
				|| Length + KINDA_SMALL_NUMBER < SectionCM
				|| Length > Settings.MaximumMemberLengthCM + KINDA_SMALL_NUMBER
				|| (Axis == EABTSM73BeamAFrameAxis::Z
					&& Length > Settings.MaximumPostSegmentSpanCM
						+ KINDA_SMALL_NUMBER))
			{
				OutError = FString::Printf(
					TEXT("BeamC3V2MemberSpanExceeded:Axis=%d:Length=%.2f"),
					static_cast<int32>(Axis), Length);
				return false;
			}
			FCoupledExteriorFramePlannedMember& Member =
				Plan.Members.AddDefaulted_GetRef();
			Member.Kind = Kind;
			Member.CellIndex = Plan.CellIndex;
			Member.CourseIndex = CourseIndex;
			Member.MacroBandIndex = MacroBandIndex;
			Member.RailIndex = RailIndex;
			Member.FaceMask = FaceMask;
			Member.Axis = Axis;
			Member.Role = Axis == EABTSM73BeamAFrameAxis::Z
				? EABTSM73BeamAMemberRole::CorePost
				: EABTSM73BeamAMemberRole::CoreCourse;
			Member.LocalStart = Start;
			Member.LocalEnd = End;
			Member.LocalBounds = BoundsForSegment(Start, End, Axis);
			return true;
		}

		void RefreshCellGeometryCrc(
			const FABTSM73BeamC3V2CoupledExteriorFrameSettings& Settings,
			FCoupledExteriorFrameCellPlan& Plan)
		{
			const FCoupledExteriorFrameCellRequest& Cell = Plan.Cell;
			FString Canonical = FString::Printf(
				TEXT("BeamC3V2CoupledFrame:v6:C=%d:R=%d:B=%d:Cell=%d:%d:")
				TEXT("Auth=%u:Span=%d:SupportBay=%d:Endpoint=%d:Shared=%d:")
				TEXT("PairC=%d:Axis=%d:Plane=%d:RailZ=%d:MinSharedBottom=%d"),
				Plan.CourseCount, Settings.RailCount,
				Plan.MacroBandCount, Cell.SourceVolumeId, Cell.BayId,
				Cell.RootAuthorityCrc32, Cell.CoupledSpanVolumeId,
				Cell.CoupledSupportBayId,
				static_cast<int32>(Cell.CoupledEndpointSign),
				Cell.bRequireSharedCoursePair ? 1 : 0,
				Cell.SharedCoursePairCourseCount,
				static_cast<int32>(Cell.CoupledSpanAxis),
				QuantizeMillimeters(Cell.CoupledBearingPlaneCM),
				QuantizeMillimeters(Cell.CoupledRailCenterZCM),
				QuantizeMillimeters(Cell.CoupledMinimumSharedCourseBottomZCM));
			for (const double RailStation : Cell.CoupledRailStationsCM)
			{
				Canonical += FString::Printf(
					TEXT("|Rail=%d"), QuantizeMillimeters(RailStation));
			}
			for (const FCoupledExteriorFramePlannedMember& Member : Plan.Members)
			{
				Canonical += FString::Printf(
					TEXT("|%d,%d,%d,%d,%u:%d,%d,%d:%d,%d,%d"),
					static_cast<int32>(Member.Kind), Member.CourseIndex,
					Member.MacroBandIndex, Member.RailIndex, Member.FaceMask,
					QuantizeMillimeters(Member.LocalStart.X),
					QuantizeMillimeters(Member.LocalStart.Y),
					QuantizeMillimeters(Member.LocalStart.Z),
					QuantizeMillimeters(Member.LocalEnd.X),
					QuantizeMillimeters(Member.LocalEnd.Y),
					QuantizeMillimeters(Member.LocalEnd.Z));
			}
			Plan.GeometryCrc32 = FCrc::StrCrc32(*Canonical);
		}

		bool BuildMacroBandStarts(
			const FABTSM73BeamC3V2CoupledExteriorFrameSettings& Settings,
			TArray<int32>& OutStarts,
			FString& OutError)
		{
			OutStarts.Reset();
			const int32 LastStart = Settings.CourseCount - 6;
			const int32 MaximumStride = Settings.MaximumMacroBandStrideCourses;
			const int32 BandCount = 1 + FMath::DivideAndRoundUp(
				FMath::Max(0, Settings.CourseCount - 22), MaximumStride);
			if (BandCount > Settings.MaximumMacroBandCount)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V2MacroBandBudgetInsufficient:%d>%d"),
					BandCount, Settings.MaximumMacroBandCount);
				return false;
			}
			const int32 FirstStart = LastStart - (BandCount - 1) * MaximumStride;
			if (FirstStart < 2 || (FirstStart & 1) != 0)
			{
				OutError = TEXT("BeamC3V2MacroBandScheduleInvalid");
				return false;
			}
			for (int32 BandIndex = 0; BandIndex < BandCount; ++BandIndex)
			{
				const int32 Start = FirstStart + BandIndex * MaximumStride;
				if (Start + 5 >= Settings.CourseCount
					|| (BandIndex > 0
						&& Start - OutStarts.Last()
							< Settings.MinimumMacroBandStrideCourses))
				{
					OutError = TEXT("BeamC3V2MacroBandScheduleInvalid");
					return false;
				}
				OutStarts.Add(Start);
			}
			return true;
		}

		bool BuildCellPlan(
			const FABTSM73BeamC3V2CoupledExteriorFrameSettings& Settings,
			const FCoupledExteriorFrameCellRequest& Cell,
			const int32 CellIndex,
			FCoupledExteriorFrameCellPlan& OutPlan,
			FString& OutError)
		{
			OutPlan = FCoupledExteriorFrameCellPlan();
			OutPlan.Cell = Cell;
			OutPlan.CellIndex = CellIndex;
			OutPlan.CourseCount = Settings.CourseCount;
			OutPlan.RailCount = Settings.RailCount;
			if (!IsFiniteBox(Cell.LocalBounds))
			{
				OutError = TEXT("BeamC3V2InvalidCellBounds");
				return false;
			}
			const FVector Size = Cell.LocalBounds.GetSize();
			if (!FMath::IsNearlyZero(Cell.LocalBounds.Min.Z, KINDA_SMALL_NUMBER))
			{
				OutError = TEXT("BeamC3V2CellNotGrounded");
				return false;
			}
			if (Size.X > Settings.MaximumMemberLengthCM + KINDA_SMALL_NUMBER
				|| Size.Y > Settings.MaximumMemberLengthCM + KINDA_SMALL_NUMBER
				|| Size.Z + KINDA_SMALL_NUMBER < Settings.CourseCount * SectionCM)
			{
				OutError = TEXT("BeamC3V2CellEnvelopeUnsupported");
				return false;
			}

			const double MinimumCoreSpan = Settings.RailCount * SectionCM;
			// One full section per side separates the ordinary core envelope from
			// the facade/post station. Their faces may touch at the boundary, but
			// their positive volumes cannot overlap. Keeping a second empty section
			// would make the 225 cm TipOver E1 footprint impossible for no structural
			// benefit and would contradict the registered low-tier compact fallback.
			const double AvailableCoreX = Size.X - SectionCM * 2.0;
			const double AvailableCoreY = Size.Y - SectionCM * 2.0;
			if (AvailableCoreX + KINDA_SMALL_NUMBER < MinimumCoreSpan
				|| AvailableCoreY + KINDA_SMALL_NUMBER < MinimumCoreSpan)
			{
				OutError = TEXT("BeamC3V2CellTooNarrowForRailCount");
				return false;
			}
			const double CoreSpanX = QuantizeSpanDown(FMath::Max(
				MinimumCoreSpan, FMath::Min(PreferredCoreSpanCM, AvailableCoreX)));
			const double CoreSpanY = QuantizeSpanDown(FMath::Max(
				MinimumCoreSpan, FMath::Min(PreferredCoreSpanCM, AvailableCoreY)));
			TArray<double> XLanes;
			TArray<double> YLanes;
			BuildQuantizedLaneCenters(
				Cell.LocalBounds.GetCenter().X, CoreSpanX,
				Settings.RailCount, XLanes);
			BuildQuantizedLaneCenters(
				Cell.LocalBounds.GetCenter().Y, CoreSpanY,
				Settings.RailCount, YLanes);

			if (!BuildMacroBandStarts(Settings, OutPlan.MacroBandStartCourses, OutError))
			{
				return false;
			}
			OutPlan.MacroBandCount = OutPlan.MacroBandStartCourses.Num();
			TArray<int32> ThroughBandByCourse;
			ThroughBandByCourse.Init(INDEX_NONE, Settings.CourseCount);
			for (int32 BandIndex = 0;
				BandIndex < OutPlan.MacroBandStartCourses.Num(); ++BandIndex)
			{
				const int32 Start = OutPlan.MacroBandStartCourses[BandIndex];
				ThroughBandByCourse[Start] = BandIndex;
				ThroughBandByCourse[Start + 2] = BandIndex;
				ThroughBandByCourse[Start + 3] = BandIndex;
				ThroughBandByCourse[Start + 5] = BandIndex;
			}

			const FVector Center = Cell.LocalBounds.GetCenter();
			for (int32 CourseIndex = 0; CourseIndex < Settings.CourseCount;
				++CourseIndex)
			{
				const bool bX = (CourseIndex & 1) == 0;
				const bool bThrough = ThroughBandByCourse[CourseIndex] != INDEX_NONE;
				const double Z = (CourseIndex + 0.5) * SectionCM;
				for (int32 RailIndex = 0; RailIndex < Settings.RailCount; ++RailIndex)
				{
					FVector Start;
					FVector End;
					if (bX)
					{
						Start = FVector(
							bThrough ? Cell.LocalBounds.Min.X : Center.X - CoreSpanX * 0.5,
							YLanes[RailIndex], Z);
						End = FVector(
							bThrough ? Cell.LocalBounds.Max.X : Center.X + CoreSpanX * 0.5,
							YLanes[RailIndex], Z);
					}
					else
					{
						Start = FVector(XLanes[RailIndex],
							bThrough ? Cell.LocalBounds.Min.Y : Center.Y - CoreSpanY * 0.5,
							Z);
						End = FVector(XLanes[RailIndex],
							bThrough ? Cell.LocalBounds.Max.Y : Center.Y + CoreSpanY * 0.5,
							Z);
					}
					if (!AddPlannedMember(OutPlan,
						bThrough
							? ECoupledExteriorFrameMemberKind::ThroughOutrigger
							: ECoupledExteriorFrameMemberKind::CoreRail,
						CourseIndex, ThroughBandByCourse[CourseIndex], RailIndex, 0,
						bX ? EABTSM73BeamAFrameAxis::X : EABTSM73BeamAFrameAxis::Y,
						Start, End, Settings, OutError))
					{
						return false;
					}
				}
			}

			const double NegativeXStation =
				Cell.LocalBounds.Min.X + SectionCM * 0.5;
			const double PositiveXStation =
				Cell.LocalBounds.Max.X - SectionCM * 0.5;
			const double NegativeYStation =
				Cell.LocalBounds.Min.Y + SectionCM * 0.5;
			const double PositiveYStation =
				Cell.LocalBounds.Max.Y - SectionCM * 0.5;
			for (int32 BandIndex = 0;
				BandIndex < OutPlan.MacroBandStartCourses.Num(); ++BandIndex)
			{
				const int32 A = OutPlan.MacroBandStartCourses[BandIndex];
				const double YFacadeZ = (A + 1.5) * SectionCM;
				const double XFacadeZ = (A + 4.5) * SectionCM;
				if (!AddPlannedMember(OutPlan,
					ECoupledExteriorFrameMemberKind::FacadeRail, A + 1, BandIndex,
					0, ECoupledExteriorFrameFace::NegativeX,
					EABTSM73BeamAFrameAxis::Y,
					FVector(NegativeXStation, Cell.LocalBounds.Min.Y, YFacadeZ),
					FVector(NegativeXStation, Cell.LocalBounds.Max.Y, YFacadeZ),
					Settings, OutError)
					|| !AddPlannedMember(OutPlan,
						ECoupledExteriorFrameMemberKind::FacadeRail, A + 1, BandIndex,
						0, ECoupledExteriorFrameFace::PositiveX,
						EABTSM73BeamAFrameAxis::Y,
						FVector(PositiveXStation, Cell.LocalBounds.Min.Y, YFacadeZ),
						FVector(PositiveXStation, Cell.LocalBounds.Max.Y, YFacadeZ),
						Settings, OutError)
					|| !AddPlannedMember(OutPlan,
						ECoupledExteriorFrameMemberKind::FacadeRail, A + 4, BandIndex,
						0, ECoupledExteriorFrameFace::NegativeY,
						EABTSM73BeamAFrameAxis::X,
						FVector(Cell.LocalBounds.Min.X, NegativeYStation, XFacadeZ),
						FVector(Cell.LocalBounds.Max.X, NegativeYStation, XFacadeZ),
						Settings, OutError)
					|| !AddPlannedMember(OutPlan,
						ECoupledExteriorFrameMemberKind::FacadeRail, A + 4, BandIndex,
						0, ECoupledExteriorFrameFace::PositiveY,
						EABTSM73BeamAFrameAxis::X,
						FVector(Cell.LocalBounds.Min.X, PositiveYStation, XFacadeZ),
						FVector(Cell.LocalBounds.Max.X, PositiveYStation, XFacadeZ),
						Settings, OutError))
				{
					return false;
				}

				const int32 PreviousA = BandIndex > 0
					? OutPlan.MacroBandStartCourses[BandIndex - 1] : INDEX_NONE;
				const double XPostBottom = BandIndex == 0
					? 0.0 : (PreviousA + 3) * SectionCM;
				const double XPostTop = A * SectionCM;
				const double YPostBottom = BandIndex == 0
					? 0.0 : (PreviousA + 6) * SectionCM;
				const double YPostTop = (A + 3) * SectionCM;
				for (int32 RailIndex = 0; RailIndex < Settings.RailCount; ++RailIndex)
				{
					for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
					{
						const double X = SideIndex == 0
							? NegativeXStation : PositiveXStation;
						const uint8 XFace = SideIndex == 0
							? ECoupledExteriorFrameFace::NegativeX
							: ECoupledExteriorFrameFace::PositiveX;
						if (!AddPlannedMember(OutPlan,
							ECoupledExteriorFrameMemberKind::ExteriorPost, A, BandIndex,
							RailIndex, XFace, EABTSM73BeamAFrameAxis::Z,
							FVector(X, YLanes[RailIndex], XPostBottom),
							FVector(X, YLanes[RailIndex], XPostTop),
							Settings, OutError))
						{
							return false;
						}
						const double Y = SideIndex == 0
							? NegativeYStation : PositiveYStation;
						const uint8 YFace = SideIndex == 0
							? ECoupledExteriorFrameFace::NegativeY
							: ECoupledExteriorFrameFace::PositiveY;
						if (!AddPlannedMember(OutPlan,
							ECoupledExteriorFrameMemberKind::ExteriorPost, A + 3, BandIndex,
							RailIndex, YFace, EABTSM73BeamAFrameAxis::Z,
							FVector(XLanes[RailIndex], Y, YPostBottom),
							FVector(XLanes[RailIndex], Y, YPostTop),
							Settings, OutError))
						{
							return false;
						}
					}
				}
			}

			const int32 ExpectedMemberCount = Settings.CourseCount * Settings.RailCount
				+ 4 * OutPlan.MacroBandCount * (Settings.RailCount + 1);
			if (OutPlan.Members.Num() != ExpectedMemberCount)
			{
				OutError = TEXT("BeamC3V2InternalMemberCountMismatch");
				return false;
			}
			for (int32 A = 0; A < OutPlan.Members.Num(); ++A)
			{
				for (int32 B = A + 1; B < OutPlan.Members.Num(); ++B)
				{
					if (HasPositiveOverlap(
						OutPlan.Members[A].LocalBounds,
						OutPlan.Members[B].LocalBounds,
						KINDA_SMALL_NUMBER))
					{
						OutError = FString::Printf(
							TEXT("BeamC3V2PlannedMemberPenetration:%d:%d"), A, B);
						return false;
					}
				}
			}

			RefreshCellGeometryCrc(Settings, OutPlan);
			OutPlan.GroundedFaceMask = AllFaces;
			return true;
		}

		bool BuildSharedCoursePairPlans(
			const FABTSM73BeamC3V2CoupledExteriorFrameSettings& Settings,
			const FABTSM73BeamAPreviewSettings& BeamASettings,
			TArray<FCoupledExteriorFrameCellPlan>& Plans,
			FString& OutError)
		{
			if (Plans.Num() != 2)
			{
				OutError = TEXT("BeamC3V2SharedCourseRequiresExactlyTwoCells");
				return false;
			}
			FCoupledExteriorFrameCellPlan* Negative = nullptr;
			FCoupledExteriorFrameCellPlan* Positive = nullptr;
			for (FCoupledExteriorFrameCellPlan& Plan : Plans)
			{
				if (!Plan.Cell.bRequireSharedCoursePair)
				{
					OutError = TEXT("BeamC3V2SharedCoursePairNotAtomic");
					return false;
				}
				FCoupledExteriorFrameCellPlan*& Slot =
					Plan.Cell.CoupledEndpointSign < 0 ? Negative : Positive;
				if ((Plan.Cell.CoupledEndpointSign != -1
						&& Plan.Cell.CoupledEndpointSign != 1)
					|| Slot != nullptr)
				{
					OutError = TEXT("BeamC3V2SharedCourseEndpointIdentityInvalid");
					return false;
				}
				Slot = &Plan;
			}
			if (Negative == nullptr || Positive == nullptr
				|| Negative->Cell.SourceVolumeId == Positive->Cell.SourceVolumeId
				|| Negative->Cell.BayId == Positive->Cell.BayId)
			{
				OutError = TEXT("BeamC3V2SharedCourseDistinctRootsMissing");
				return false;
			}

			const FCoupledExteriorFrameCellRequest& A = Negative->Cell;
			const FCoupledExteriorFrameCellRequest& B = Positive->Cell;
			const double Tolerance = FMath::Max(
				KINDA_SMALL_NUMBER,
				static_cast<double>(BeamASettings.JointMergeToleranceCM));
			if (A.CoupledSpanVolumeId == INDEX_NONE
				|| A.CoupledSpanVolumeId != B.CoupledSpanVolumeId
				|| A.CoupledSpanAxis != B.CoupledSpanAxis
				|| (A.CoupledSpanAxis != EABTSM73BeamAFrameAxis::X
					&& A.CoupledSpanAxis != EABTSM73BeamAFrameAxis::Y)
				|| A.CoupledBearingPlaneCM >= B.CoupledBearingPlaneCM - Tolerance
				|| !FMath::IsNearlyEqual(A.CoupledMinimumSharedCourseBottomZCM,
					B.CoupledMinimumSharedCourseBottomZCM, Tolerance))
			{
				OutError = TEXT("BeamC3V2SharedCourseEndpointContractMismatch");
				return false;
			}
			const int32 SpanAxis = static_cast<int32>(A.CoupledSpanAxis);
			const int32 PerpendicularAxis = SpanAxis == 0 ? 1 : 0;
			if (Negative->Cell.LocalBounds.GetCenter()[SpanAxis]
					>= Positive->Cell.LocalBounds.GetCenter()[SpanAxis] - Tolerance
				|| !FMath::IsNearlyEqual(
					Negative->Cell.LocalBounds.GetCenter()[PerpendicularAxis],
					Positive->Cell.LocalBounds.GetCenter()[PerpendicularAxis], Tolerance)
				|| !FMath::IsNearlyEqual(
					Negative->Cell.LocalBounds.GetSize()[PerpendicularAxis],
					Positive->Cell.LocalBounds.GetSize()[PerpendicularAxis], Tolerance))
			{
				OutError = TEXT("BeamC3V2SharedCourseCoresNotParallelAligned");
				return false;
			}

			if (!FMath::IsFinite(A.CoupledRailCenterZCM)
				|| !FMath::IsFinite(B.CoupledRailCenterZCM)
				|| !FMath::IsNearlyEqual(A.CoupledRailCenterZCM,
					B.CoupledRailCenterZCM, Tolerance)
				|| A.CoupledRailStationsCM.Num() != 2
				|| B.CoupledRailStationsCM.Num() != 2
				|| Settings.RailCount < 2)
			{
				OutError = TEXT("BeamC3V2SharedCourseRailAuthorityInvalid");
				return false;
			}
			TArray<double> AuthorityRailStations = A.CoupledRailStationsCM;
			TArray<double> PositiveRailStations = B.CoupledRailStationsCM;
			AuthorityRailStations.Sort();
			PositiveRailStations.Sort();
			for (int32 RailIndex = 0; RailIndex < AuthorityRailStations.Num(); ++RailIndex)
			{
				const double Station = AuthorityRailStations[RailIndex];
				if (!FMath::IsFinite(Station)
					|| !FMath::IsNearlyEqual(
						Station, PositiveRailStations[RailIndex], Tolerance)
					|| Station < A.LocalBounds.Min[PerpendicularAxis] + SectionCM * 0.5
						- Tolerance
					|| Station > A.LocalBounds.Max[PerpendicularAxis] - SectionCM * 0.5
						+ Tolerance
					|| Station < B.LocalBounds.Min[PerpendicularAxis] + SectionCM * 0.5
						- Tolerance
					|| Station > B.LocalBounds.Max[PerpendicularAxis] - SectionCM * 0.5
						+ Tolerance
					|| (RailIndex > 0
						&& Station - AuthorityRailStations[RailIndex - 1]
							< SectionCM - Tolerance))
				{
					OutError = TEXT("BeamC3V2SharedCourseRailAuthorityInvalid");
					return false;
				}
			}
			Negative->Cell.CoupledRailStationsCM = AuthorityRailStations;
			Positive->Cell.CoupledRailStationsCM = PositiveRailStations;
			// Beam-B's BridgeBay publishes exactly the two load-bearing outer rails.
			// Only those real rails become shared members. R4 remains the independent
			// density of each E6 core; this route neither duplicates endpoint arrays
			// nor invents inner bridge lanes. The extra two-piece covers are admitted
			// by the fixed atomic-pair allowance below.
			const TArray<double>& SharedRailStations = AuthorityRailStations;

			// The E6 course replaces the Beam-B bridge rails; it is not a decorative
			// band above them. Pick the nearest same-axis body course once. The final
			// rail stays on that exact 36 cm lattice so both sandwich faces produce
			// exact Beam-A bearings. The old bridge rail may move by at most two fixed
			// merge tolerances during the explicit protected-member replacement below.
			const int32 RequiredParity = SpanAxis == 0 ? 0 : 1;
			const int32 NearestCourse = FMath::RoundToInt(
				A.CoupledRailCenterZCM / SectionCM - 0.5);
			int32 SharedCourseIndex = INDEX_NONE;
			double BestCourseDistance = TNumericLimits<double>::Max();
			for (int32 Offset = -2; Offset <= 2; ++Offset)
			{
				const int32 CandidateCourse = NearestCourse + Offset;
				if (CandidateCourse <= 0
					|| CandidateCourse + 1 >= Settings.CourseCount
					|| (CandidateCourse & 1) != RequiredParity)
				{
					continue;
				}
				const double CandidateCenter =
					(CandidateCourse + 0.5) * SectionCM;
				const double Distance = FMath::Abs(
					CandidateCenter - A.CoupledRailCenterZCM);
				if (Distance < BestCourseDistance - KINDA_SMALL_NUMBER
					|| (FMath::IsNearlyEqual(Distance, BestCourseDistance)
						&& (SharedCourseIndex == INDEX_NONE
							|| CandidateCourse < SharedCourseIndex)))
				{
					SharedCourseIndex = CandidateCourse;
					BestCourseDistance = Distance;
				}
			}
			if (SharedCourseIndex == INDEX_NONE
				|| BestCourseDistance > Tolerance * 2.0 + KINDA_SMALL_NUMBER)
			{
				OutError = TEXT("BeamC3V2SharedCourseRailPhaseMismatch");
				return false;
			}
			const double NominalCourseCenterZ =
				(SharedCourseIndex + 0.5) * SectionCM;
			const double SharedCourseCenterZ = NominalCourseCenterZ;
			if (SharedCourseCenterZ - SectionCM * 0.5 + Tolerance
				< A.CoupledMinimumSharedCourseBottomZCM)
			{
				OutError = TEXT("BeamC3V2SharedCourseRegisteredBandUnavailable");
				return false;
			}

			// The upper and lower cross courses must reach every authoritative bridge
			// lane in both cores. Extend only to the two rail centre stations: a further
			// extension to the cell faces would pass through the Y-face exterior posts.
			// The synthetic compact fixture and ordinary cells stay on the existing path.
			auto ExtendSandwichCourses = [SharedCourseIndex,
				PerpendicularAxis, &SharedRailStations, Tolerance](
					FCoupledExteriorFrameCellPlan& Plan)
			{
				const double MinimumStation = SharedRailStations[0];
				const double MaximumStation = SharedRailStations.Last();
				for (FCoupledExteriorFramePlannedMember& Member : Plan.Members)
				{
					if ((Member.CourseIndex != SharedCourseIndex - 1
							&& Member.CourseIndex != SharedCourseIndex + 1)
						|| static_cast<int32>(Member.Axis) != PerpendicularAxis
						|| (Member.LocalBounds.Min[PerpendicularAxis]
							<= MinimumStation + Tolerance
							&& Member.LocalBounds.Max[PerpendicularAxis]
								>= MaximumStation - Tolerance))
					{
						continue;
					}
					Member.LocalStart[PerpendicularAxis] = MinimumStation;
					Member.LocalEnd[PerpendicularAxis] = MaximumStation;
					Member.Kind = ECoupledExteriorFrameMemberKind::ThroughOutrigger;
					Member.LocalBounds = BoundsForSegment(
						Member.LocalStart, Member.LocalEnd, Member.Axis);
				}
			};
			ExtendSandwichCourses(*Negative);
			ExtendSandwichCourses(*Positive);

			constexpr double EmbedDepthCM = SectionCM * 2.0;
			TArray<FCoupledExteriorFramePlannedMember> NegativeStubs;
			TArray<FCoupledExteriorFramePlannedMember> PositiveStubs;
			TArray<FCoupledExteriorFramePlannedMember> SharedMembers;
			TSet<int32> NegativeSourceRailIndicesToRemove;
			TSet<int32> PositiveSourceRailIndicesToRemove;
			for (int32 RailIndex = 0; RailIndex < SharedRailStations.Num(); ++RailIndex)
			{
				const FCoupledExteriorFramePlannedMember* NegativeOriginal =
					Negative->Members.FindByPredicate(
						[SharedCourseIndex, RailIndex, &A](
							const FCoupledExteriorFramePlannedMember& Member)
						{
							return Member.CourseIndex == SharedCourseIndex
								&& Member.RailIndex == RailIndex
								&& Member.Axis == A.CoupledSpanAxis;
						});
				const FCoupledExteriorFramePlannedMember* PositiveOriginal =
					Positive->Members.FindByPredicate(
						[SharedCourseIndex, RailIndex, &B](
							const FCoupledExteriorFramePlannedMember& Member)
						{
							return Member.CourseIndex == SharedCourseIndex
								&& Member.RailIndex == RailIndex
								&& Member.Axis == B.CoupledSpanAxis;
						});
				if (NegativeOriginal == nullptr || PositiveOriginal == nullptr)
				{
					OutError = TEXT("BeamC3V2SharedCourseSourceRailMissing");
					return false;
				}
				if (FMath::Abs(NegativeOriginal->LocalBounds.GetCenter()[PerpendicularAxis]
						- SharedRailStations[RailIndex]) <= Tolerance)
				{
					NegativeSourceRailIndicesToRemove.Add(RailIndex);
				}
				if (FMath::Abs(PositiveOriginal->LocalBounds.GetCenter()[PerpendicularAxis]
						- SharedRailStations[RailIndex]) <= Tolerance)
				{
					PositiveSourceRailIndicesToRemove.Add(RailIndex);
				}
				const double NegativeFar = FMath::Min(
					NegativeOriginal->LocalStart[SpanAxis],
					NegativeOriginal->LocalEnd[SpanAxis]);
				const double NegativeInner = FMath::Max(
					NegativeOriginal->LocalStart[SpanAxis],
					NegativeOriginal->LocalEnd[SpanAxis]);
				const double PositiveInner = FMath::Min(
					PositiveOriginal->LocalStart[SpanAxis],
					PositiveOriginal->LocalEnd[SpanAxis]);
				const double PositiveFar = FMath::Max(
					PositiveOriginal->LocalStart[SpanAxis],
					PositiveOriginal->LocalEnd[SpanAxis]);
				const double EmbeddedSharedStart = NegativeInner - EmbedDepthCM;
				const double EmbeddedSharedEnd = PositiveInner + EmbedDepthCM;
				const double EmbeddedSharedLength =
					EmbeddedSharedEnd - EmbeddedSharedStart;
				if (NegativeInner >= PositiveInner - Tolerance
					|| EmbeddedSharedStart
						> A.CoupledBearingPlaneCM + Tolerance
					|| EmbeddedSharedEnd
						< B.CoupledBearingPlaneCM - Tolerance
					|| EmbeddedSharedLength + Tolerance < SectionCM
					|| EmbeddedSharedLength
						> Settings.MaximumMemberLengthCM + Tolerance)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V2SharedCourseSpanExceeded:%.2f>%.2f"),
						EmbeddedSharedLength,
						Settings.MaximumMemberLengthCM);
					return false;
				}
				if (EmbeddedSharedStart - NegativeFar + Tolerance < SectionCM
					|| PositiveFar - EmbeddedSharedEnd + Tolerance < SectionCM)
				{
					OutError = TEXT("BeamC3V2SharedCourseFarStubTooShort");
					return false;
				}
				// The required shared member and two far stubs cover exactly the same
				// collinear course as the two source core rails. When either one-sided
				// extension remains under the 720 cm member contract, keeping three
				// pieces would add a redundant butt joint without adding geometry or
				// bearing area. Use the minimum two-piece cover and alternate the
				// absorbed side across rails to avoid a geometric left/right bias. The
				// shared member still lives in the negative CellPlan; this provisional
				// ownership is not evidence of SupportedSpan/shared semantic ownership.
				// This path is reachable only for the atomic E6 shared pair.
				const double NegativeAbsorbedLength =
					EmbeddedSharedEnd - NegativeFar;
				const double PositiveAbsorbedLength =
					PositiveFar - EmbeddedSharedStart;
				const bool bCanAbsorbNegativeStub =
					NegativeAbsorbedLength
						<= Settings.MaximumMemberLengthCM + Tolerance;
				const bool bCanAbsorbPositiveStub =
					PositiveAbsorbedLength
						<= Settings.MaximumMemberLengthCM + Tolerance;
				const bool bAbsorbNegativeStub = bCanAbsorbNegativeStub
					&& (!bCanAbsorbPositiveStub || (RailIndex & 1) == 0);
				const bool bAbsorbPositiveStub = bCanAbsorbPositiveStub
					&& !bAbsorbNegativeStub;
				const double SharedStart = bAbsorbNegativeStub
					? NegativeFar : EmbeddedSharedStart;
				const double SharedEnd = bAbsorbPositiveStub
					? PositiveFar : EmbeddedSharedEnd;

				if (!bAbsorbNegativeStub)
				{
					FCoupledExteriorFramePlannedMember NegativeStub = *NegativeOriginal;
					NegativeStub.Kind = NegativeOriginal->Kind;
					NegativeStub.Role = EABTSM73BeamAMemberRole::CoreCourse;
					NegativeStub.LocalStart[PerpendicularAxis] =
						SharedRailStations[RailIndex];
					NegativeStub.LocalEnd[PerpendicularAxis] =
						SharedRailStations[RailIndex];
					NegativeStub.LocalStart.Z = SharedCourseCenterZ;
					NegativeStub.LocalEnd.Z = SharedCourseCenterZ;
					NegativeStub.LocalStart[SpanAxis] = NegativeFar;
					NegativeStub.LocalEnd[SpanAxis] = SharedStart;
					NegativeStub.LocalBounds = BoundsForSegment(
						NegativeStub.LocalStart, NegativeStub.LocalEnd,
						NegativeStub.Axis);
					NegativeStubs.Add(MoveTemp(NegativeStub));
				}

				if (!bAbsorbPositiveStub)
				{
					FCoupledExteriorFramePlannedMember PositiveStub = *PositiveOriginal;
					PositiveStub.Kind = PositiveOriginal->Kind;
					PositiveStub.Role = EABTSM73BeamAMemberRole::CoreCourse;
					PositiveStub.LocalStart[PerpendicularAxis] =
						SharedRailStations[RailIndex];
					PositiveStub.LocalEnd[PerpendicularAxis] =
						SharedRailStations[RailIndex];
					PositiveStub.LocalStart.Z = SharedCourseCenterZ;
					PositiveStub.LocalEnd.Z = SharedCourseCenterZ;
					PositiveStub.LocalStart[SpanAxis] = SharedEnd;
					PositiveStub.LocalEnd[SpanAxis] = PositiveFar;
					PositiveStub.LocalBounds = BoundsForSegment(
						PositiveStub.LocalStart, PositiveStub.LocalEnd,
						PositiveStub.Axis);
					PositiveStubs.Add(MoveTemp(PositiveStub));
				}

				FCoupledExteriorFramePlannedMember Shared = *NegativeOriginal;
				Shared.Kind = ECoupledExteriorFrameMemberKind::SharedCourse;
				Shared.Role = EABTSM73BeamAMemberRole::BridgeRail;
				Shared.LocalStart[PerpendicularAxis] = SharedRailStations[RailIndex];
				Shared.LocalEnd[PerpendicularAxis] = SharedRailStations[RailIndex];
				Shared.LocalStart.Z = SharedCourseCenterZ;
				Shared.LocalEnd.Z = SharedCourseCenterZ;
				Shared.LocalStart[SpanAxis] = SharedStart;
				Shared.LocalEnd[SpanAxis] = SharedEnd;
				Shared.LocalBounds = BoundsForSegment(
					Shared.LocalStart, Shared.LocalEnd, Shared.Axis);
				SharedMembers.Add(MoveTemp(Shared));
			}

			auto RemoveCoincidentSourceRails = [SharedCourseIndex, SpanAxis](
				FCoupledExteriorFrameCellPlan& Plan,
				const TSet<int32>& RailIndices)
			{
				Plan.Members.RemoveAll([SharedCourseIndex, SpanAxis, &RailIndices](
					const FCoupledExteriorFramePlannedMember& Member)
				{
					return Member.CourseIndex == SharedCourseIndex
						&& static_cast<int32>(Member.Axis) == SpanAxis
						&& RailIndices.Contains(Member.RailIndex);
				});
			};
			RemoveCoincidentSourceRails(
				*Negative, NegativeSourceRailIndicesToRemove);
			RemoveCoincidentSourceRails(
				*Positive, PositiveSourceRailIndicesToRemove);
			Negative->Members.Append(NegativeStubs);
			Negative->Members.Append(SharedMembers);
			Positive->Members.Append(PositiveStubs);

			TArray<const FCoupledExteriorFramePlannedMember*> AllMembers;
			for (const FCoupledExteriorFrameCellPlan& Plan : Plans)
			{
				for (const FCoupledExteriorFramePlannedMember& Member : Plan.Members)
				{
					AllMembers.Add(&Member);
				}
			}
			for (int32 AIndex = 0; AIndex < AllMembers.Num(); ++AIndex)
			{
				for (int32 BIndex = AIndex + 1; BIndex < AllMembers.Num(); ++BIndex)
				{
					if (HasPositiveOverlap(AllMembers[AIndex]->LocalBounds,
						AllMembers[BIndex]->LocalBounds, Tolerance))
					{
						OutError = FString::Printf(
							TEXT("BeamC3V2SharedCoursePlannedMemberPenetration:")
							TEXT("A=%d:Kind=%d:Course=%d:Axis=%d:Bounds=%s..%s:")
							TEXT("B=%d:Kind=%d:Course=%d:Axis=%d:Bounds=%s..%s"),
							AIndex, static_cast<int32>(AllMembers[AIndex]->Kind),
							AllMembers[AIndex]->CourseIndex,
							static_cast<int32>(AllMembers[AIndex]->Axis),
							*AllMembers[AIndex]->LocalBounds.Min.ToCompactString(),
							*AllMembers[AIndex]->LocalBounds.Max.ToCompactString(),
							BIndex, static_cast<int32>(AllMembers[BIndex]->Kind),
							AllMembers[BIndex]->CourseIndex,
							static_cast<int32>(AllMembers[BIndex]->Axis),
							*AllMembers[BIndex]->LocalBounds.Min.ToCompactString(),
							*AllMembers[BIndex]->LocalBounds.Max.ToCompactString());
						return false;
					}
				}
			}
			RefreshCellGeometryCrc(Settings, *Negative);
			RefreshCellGeometryCrc(Settings, *Positive);
			return true;
		}

		void CompactRemovedMembers(
			FABTSM73BeamAGenerationResult& Assembly,
			const TSet<int32>& Removed)
		{
			TArray<int32> MemberMap;
			MemberMap.Init(INDEX_NONE, Assembly.Members.Num());
			TArray<FABTSM73BeamAMember> NewMembers;
			for (const FABTSM73BeamAMember& OldMember : Assembly.Members)
			{
				if (Removed.Contains(OldMember.MemberId))
				{
					continue;
				}
				FABTSM73BeamAMember& NewMember = NewMembers.Add_GetRef(OldMember);
				NewMember.MemberId = NewMembers.Num() - 1;
				MemberMap[OldMember.MemberId] = NewMember.MemberId;
			}
			for (FABTSM73BeamAAssembly& Owner : Assembly.Assemblies)
			{
				TArray<int32> NewIds;
				for (const int32 OldId : Owner.MemberIds)
				{
					if (MemberMap.IsValidIndex(OldId) && MemberMap[OldId] != INDEX_NONE)
					{
						NewIds.AddUnique(MemberMap[OldId]);
					}
				}
				Owner.MemberIds = MoveTemp(NewIds);
			}
			Assembly.Members = MoveTemp(NewMembers);
			Assembly.BearingContacts.Reset();
		}

		int32 AddJoint(
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

		void AppendCellPlan(
			const FCoupledExteriorFrameCellPlan& Plan,
			FABTSM73BeamAGenerationResult& Assembly)
		{
			FABTSM73BeamAAssembly& Owner = Assembly.Assemblies.AddDefaulted_GetRef();
			Owner.AssemblyId = Assembly.Assemblies.Num() - 1;
			Owner.BayId = Plan.Cell.BayId;
			Owner.Type = EABTSM73BeamAAssemblyType::CribCore;
			for (const FCoupledExteriorFramePlannedMember& Planned : Plan.Members)
			{
				FABTSM73BeamAMember& Member = Assembly.Members.AddDefaulted_GetRef();
				Member.MemberId = Assembly.Members.Num() - 1;
				const bool bPost = Planned.Axis == EABTSM73BeamAFrameAxis::Z;
				Member.JointA = AddJoint(Assembly, Planned.LocalStart,
					bPost && FMath::IsNearlyZero(Planned.LocalStart.Z)
						? EABTSM73BeamAJointRole::GroundFoot
						: EABTSM73BeamAJointRole::CrossBearing);
				Member.JointB = AddJoint(Assembly, Planned.LocalEnd,
					bPost ? EABTSM73BeamAJointRole::ColumnHead
						: EABTSM73BeamAJointRole::CrossBearing);
				Member.Axis = Planned.Axis;
				Member.Role = Planned.Role;
				Member.LengthCM = FVector::Distance(Planned.LocalStart, Planned.LocalEnd);
				Owner.MemberIds.Add(Member.MemberId);
				Owner.JointIds.Add(Member.JointA);
				Owner.JointIds.Add(Member.JointB);
			}
		}

		void RefreshSummary(FABTSM73BeamAGenerationResult& Assembly)
		{
			Assembly.Summary.JointCount = Assembly.Joints.Num();
			Assembly.Summary.MemberCount = Assembly.Members.Num();
			Assembly.Summary.AssemblyCount = Assembly.Assemblies.Num();
			Assembly.Summary.BearingContactCount = Assembly.BearingContacts.Num();
			Assembly.Summary.XMemberCount = 0;
			Assembly.Summary.YMemberCount = 0;
			Assembly.Summary.ZMemberCount = 0;
			Assembly.Summary.DiagonalMemberCount = 0;
			for (const FABTSM73BeamAMember& Member : Assembly.Members)
			{
				switch (Member.Axis)
				{
				case EABTSM73BeamAFrameAxis::X: ++Assembly.Summary.XMemberCount; break;
				case EABTSM73BeamAFrameAxis::Y: ++Assembly.Summary.YMemberCount; break;
				case EABTSM73BeamAFrameAxis::Z: ++Assembly.Summary.ZMemberCount; break;
				default: ++Assembly.Summary.DiagonalMemberCount; break;
				}
			}
			Assembly.Summary.bAccepted = true;
			Assembly.Summary.RejectReason.Reset();
		}

		bool NearlyEqualBounds(const FBox& A, const FBox& B, const double Tolerance)
		{
			return A.Min.Equals(B.Min, Tolerance) && A.Max.Equals(B.Max, Tolerance);
		}

		bool CertifyGeometry(
			const FABTSM73BeamC3V2CoupledExteriorFrameSettings& Settings,
			const FABTSM73BeamAPreviewSettings& BeamASettings,
			const FABTSM73BeamAGenerationResult& Assembly,
			FCoupledExteriorFrameResult& Result,
			TArray<int32>& OutMemberIds,
			FString& OutError)
		{
			OutMemberIds.Reset();
			int32 EffectiveRailCount = Settings.RailCount;
			if (!Result.CellPlans.IsEmpty())
			{
				EffectiveRailCount = Result.CellPlans[0].RailCount;
				for (const FCoupledExteriorFrameCellPlan& Plan : Result.CellPlans)
				{
					if (Plan.RailCount != EffectiveRailCount)
					{
						OutError = TEXT("BeamC3V2CellPlanRailCountMismatch");
						return false;
					}
				}
			}
			TSet<int32> Claimed;
			uint8 FaceMask = 0;
			float MaximumLength = 0.0f;
			float MaximumPostSpan = 0.0f;
			FString Canonical(TEXT("BeamC3V2FinalGeometry:v1"));
			for (const FCoupledExteriorFramePlannedMember& Planned :
				Result.PlannedMembers)
			{
				int32 Match = INDEX_NONE;
				for (const FABTSM73BeamAMember& Member : Assembly.Members)
				{
					if (Claimed.Contains(Member.MemberId)
						|| Member.Axis != Planned.Axis
						|| Member.Role != Planned.Role
						|| !NearlyEqualBounds(
							MemberBounds(Member, Assembly), Planned.LocalBounds,
							BeamASettings.JointMergeToleranceCM))
					{
						continue;
					}
					Match = Member.MemberId;
					break;
				}
				if (Match == INDEX_NONE)
				{
					OutError = TEXT("BeamC3V2FinalPlannedMemberMissing");
					return false;
				}
				Claimed.Add(Match);
				OutMemberIds.Add(Match);
				const FABTSM73BeamAMember& Member = Assembly.Members[Match];
				MaximumLength = FMath::Max(MaximumLength, Member.LengthCM);
				if (Member.Axis == EABTSM73BeamAFrameAxis::Z)
				{
					MaximumPostSpan = FMath::Max(MaximumPostSpan, Member.LengthCM);
					if (FMath::IsNearlyZero(Planned.LocalBounds.Min.Z,
						BeamASettings.JointMergeToleranceCM))
					{
						FaceMask |= Planned.FaceMask;
					}
				}
				if (Member.LengthCM > Settings.MaximumMemberLengthCM
					+ BeamASettings.JointMergeToleranceCM
					|| (Member.Axis == EABTSM73BeamAFrameAxis::Z
						&& Member.LengthCM > Settings.MaximumPostSegmentSpanCM
							+ BeamASettings.JointMergeToleranceCM))
				{
					OutError = TEXT("BeamC3V2FinalMemberSpanExceeded");
					return false;
				}
				Canonical += FString::Printf(
					TEXT("|%d:%d:%d:%d,%d,%d:%d,%d,%d:%d"),
					static_cast<int32>(Planned.Kind),
					static_cast<int32>(Member.Axis),
					static_cast<int32>(Member.Role),
					QuantizeMillimeters(Planned.LocalBounds.Min.X),
					QuantizeMillimeters(Planned.LocalBounds.Min.Y),
					QuantizeMillimeters(Planned.LocalBounds.Min.Z),
					QuantizeMillimeters(Planned.LocalBounds.Max.X),
					QuantizeMillimeters(Planned.LocalBounds.Max.Y),
					QuantizeMillimeters(Planned.LocalBounds.Max.Z),
					QuantizeMillimeters(Member.LengthCM));
			}
			if (FaceMask != AllFaces)
			{
				OutError = TEXT("BeamC3V2FourFaceGroundingMissing");
				return false;
			}
			for (int32 PlannedIndex = 0;
				PlannedIndex < Result.PlannedMembers.Num(); ++PlannedIndex)
			{
				const FCoupledExteriorFramePlannedMember& Planned =
					Result.PlannedMembers[PlannedIndex];
				if (Planned.Kind != ECoupledExteriorFrameMemberKind::FacadeRail
					&& Planned.Kind != ECoupledExteriorFrameMemberKind::ExteriorPost)
				{
					continue;
				}
				int32 LowerContacts = 0;
				int32 UpperContacts = 0;
				for (const FABTSM73BeamABearingContact& Contact :
					Assembly.BearingContacts)
				{
					if (Contact.UpperMemberId == OutMemberIds[PlannedIndex])
					{
						++LowerContacts;
					}
					if (Contact.LowerMemberId == OutMemberIds[PlannedIndex])
					{
						++UpperContacts;
					}
				}
				if (Planned.Kind == ECoupledExteriorFrameMemberKind::FacadeRail
					&& (LowerContacts < EffectiveRailCount
						|| UpperContacts < EffectiveRailCount))
				{
					OutError = TEXT("BeamC3V2FacadeSandwichContactMissing");
					return false;
				}
				if (Planned.Kind == ECoupledExteriorFrameMemberKind::ExteriorPost
					&& (UpperContacts < 1
						|| (!FMath::IsNearlyZero(
							Planned.LocalBounds.Min.Z,
							BeamASettings.JointMergeToleranceCM)
							&& LowerContacts < 1)))
				{
					OutError = TEXT("BeamC3V2ExteriorPostBearingMissing");
					return false;
				}
			}

			const int32 NegativeCellIndex = Result.CellPlans.IndexOfByPredicate(
				[](const FCoupledExteriorFrameCellPlan& Plan)
				{
					return Plan.Cell.bRequireSharedCoursePair
						&& Plan.Cell.CoupledEndpointSign < 0;
				});
			const int32 PositiveCellIndex = Result.CellPlans.IndexOfByPredicate(
				[](const FCoupledExteriorFrameCellPlan& Plan)
				{
					return Plan.Cell.bRequireSharedCoursePair
						&& Plan.Cell.CoupledEndpointSign > 0;
				});
			if (Result.Summary.SharedCourseCount > 0)
			{
				const int32 ExpectedSharedRailCount = NegativeCellIndex == INDEX_NONE
					? 0 : Result.CellPlans[NegativeCellIndex]
						.Cell.CoupledRailStationsCM.Num();
				if (Result.Summary.SharedCourseCount != ExpectedSharedRailCount
					|| ExpectedSharedRailCount != 2
					|| NegativeCellIndex == INDEX_NONE
					|| PositiveCellIndex == INDEX_NONE)
				{
					OutError = TEXT("BeamC3V2SharedCoursePairCertificateIdentityMissing");
					return false;
				}
				FString SharedCanonical(TEXT("BeamC3V2SharedCourse:v2"));
				for (int32 SharedIndex = 0;
					SharedIndex < Result.PlannedMembers.Num(); ++SharedIndex)
				{
					const FCoupledExteriorFramePlannedMember& Shared =
						Result.PlannedMembers[SharedIndex];
					if (Shared.Kind != ECoupledExteriorFrameMemberKind::SharedCourse)
					{
						continue;
					}
					uint8 LowerCellMask = 0;
					uint8 UpperCellMask = 0;
					for (const FABTSM73BeamABearingContact& Contact :
						Assembly.BearingContacts)
					{
						int32 OtherMemberId = INDEX_NONE;
						uint8* CellMask = nullptr;
						if (Contact.UpperMemberId == OutMemberIds[SharedIndex])
						{
							OtherMemberId = Contact.LowerMemberId;
							CellMask = &LowerCellMask;
						}
						else if (Contact.LowerMemberId == OutMemberIds[SharedIndex])
						{
							OtherMemberId = Contact.UpperMemberId;
							CellMask = &UpperCellMask;
						}
						if (CellMask == nullptr)
						{
							continue;
						}
						const int32 OtherPlannedIndex = OutMemberIds.Find(OtherMemberId);
						if (!Result.PlannedMembers.IsValidIndex(OtherPlannedIndex))
						{
							continue;
						}
						const int32 ContactCellIndex =
							Result.PlannedMembers[OtherPlannedIndex].CellIndex;
						if (ContactCellIndex == NegativeCellIndex)
						{
							*CellMask |= 1;
						}
						if (ContactCellIndex == PositiveCellIndex)
						{
							*CellMask |= 2;
						}
					}
					if (LowerCellMask != 3 || UpperCellMask != 3)
					{
						OutError = TEXT("BeamC3V2SharedCourseSandwichContactMissing");
						return false;
					}
					SharedCanonical += FString::Printf(
						TEXT("|%d,%d:%d,%d,%d:%d,%d,%d:%u:%u"),
						Shared.CourseIndex, Shared.RailIndex,
						QuantizeMillimeters(Shared.LocalStart.X),
						QuantizeMillimeters(Shared.LocalStart.Y),
						QuantizeMillimeters(Shared.LocalStart.Z),
						QuantizeMillimeters(Shared.LocalEnd.X),
						QuantizeMillimeters(Shared.LocalEnd.Y),
						QuantizeMillimeters(Shared.LocalEnd.Z),
						LowerCellMask, UpperCellMask);
				}
				Result.Summary.SharedCourseCrc32 = FCrc::StrCrc32(*SharedCanonical);
				Result.Summary.bSharedCoursePairCertified = true;
			}
			Result.Summary.GroundedFaceMask = FaceMask;
			Result.Summary.MaximumMemberLengthCM = MaximumLength;
			Result.Summary.MaximumPostSegmentSpanCM = MaximumPostSpan;
			Result.Summary.FinalGeometryCrc32 = FCrc::StrCrc32(*Canonical);
			Result.Summary.bGeometryCertified = true;
			return true;
		}
	}
}

using namespace ABTSM73BeamC3V2;

bool FABTSM73BeamC3V2CoupledExteriorFrameGenerator::BuildSingleCell(
	const FABTSM73BeamC3V2CoupledExteriorFrameSettings& Settings,
	const FABTSM73BeamAPreviewSettings& BeamASettings,
	const FBox& CellBounds,
	FABTSM73BeamAGenerationResult& OutAssembly,
	FCoupledExteriorFrameResult& OutResult,
	FString& OutError,
	const int32 BayId,
	const int32 SourceVolumeId) const
{
	FABTSM73BeamAGenerationResult Scratch;
	FABTSM73BeamABay& Bay = Scratch.Bays.AddDefaulted_GetRef();
	Bay.BayId = BayId;
	Bay.SourceVolumeId = SourceVolumeId;
	Bay.LocalBounds = CellBounds;
	Bay.PreferredAxis = EABTSM73BeamAFrameAxis::X;
	Scratch.Summary.SourceVolumeCount = 1;
	Scratch.Summary.BayCount = 1;
	FCoupledExteriorFrameCellRequest Cell;
	Cell.LocalBounds = CellBounds;
	Cell.BayId = BayId;
	Cell.SourceVolumeId = SourceVolumeId;
	TArray<FCoupledExteriorFrameCellRequest> Cells;
	Cells.Add(Cell);
	if (!Generate(Settings, BeamASettings, Cells, Scratch, OutResult, OutError))
	{
		OutAssembly = FABTSM73BeamAGenerationResult();
		return false;
	}
	OutAssembly = MoveTemp(Scratch);
	return true;
}

bool FABTSM73BeamC3V2CoupledExteriorFrameGenerator::Generate(
	const FABTSM73BeamC3V2CoupledExteriorFrameSettings& Settings,
	const FABTSM73BeamAPreviewSettings& BeamASettings,
	const TArray<FCoupledExteriorFrameCellRequest>& Cells,
	FABTSM73BeamAGenerationResult& InOutAssembly,
	FCoupledExteriorFrameResult& OutResult,
	FString& OutError) const
{
	using namespace ABTSM73BeamC3V2;
	OutResult = FCoupledExteriorFrameResult();
	OutError.Reset();
	FString SettingsError;
	if (!Settings.Validate(SettingsError))
	{
		return Reject(OutResult, OutError, SettingsError);
	}
	if (!Settings.bEnabled)
	{
		return Reject(OutResult, OutError, TEXT("BeamC3V2Disabled"));
	}
	if (!FMath::IsNearlyEqual(
		BeamASettings.BlockCrossSectionCM, SectionCM, KINDA_SMALL_NUMBER))
	{
		return Reject(OutResult, OutError, TEXT("BeamC3V2Requires36CMSection"));
	}
	if (Cells.IsEmpty()
		|| Cells.Num() > Settings.MaximumCellCount)
	{
		return Reject(OutResult, OutError, TEXT("BeamC3V2InvalidCellCount"));
	}
	int32 SharedCourseRequestCount = 0;
	for (const FCoupledExteriorFrameCellRequest& Cell : Cells)
	{
		if (Cell.bRequireSharedCoursePair)
		{
			++SharedCourseRequestCount;
		}
	}
	const bool bRequiresSharedCoursePair = SharedCourseRequestCount > 0;
	if (bRequiresSharedCoursePair
		&& (Cells.Num() != 2 || SharedCourseRequestCount != 2))
	{
		return Reject(OutResult, OutError,
			TEXT("BeamC3V2SharedCoursePairNotAtomic"));
	}
	FABTSM73BeamC3V2CoupledExteriorFrameSettings GenerationSettings = Settings;
	if (bRequiresSharedCoursePair)
	{
			int32 PairCourseCount = INDEX_NONE;
		for (const FCoupledExteriorFrameCellRequest& Cell : Cells)
		{
			if (Cell.SharedCoursePairCourseCount < 8
				|| (Cell.SharedCoursePairCourseCount & 1) != 0
				|| Cell.SharedCoursePairCourseCount > Settings.CourseCount
				|| (PairCourseCount != INDEX_NONE
					&& PairCourseCount != Cell.SharedCoursePairCourseCount))
			{
				return Reject(OutResult, OutError,
					TEXT("BeamC3V2SharedCoursePairCourseCountInvalid"));
			}
			PairCourseCount = Cell.SharedCoursePairCourseCount;
		}
		GenerationSettings.CourseCount = PairCourseCount;
		GenerationSettings.MinimumCourseCount = FMath::Min(
			GenerationSettings.MinimumCourseCount, PairCourseCount);
		// SeamRelease E6 starts with 4,154 shell members and its complete Beam-C
		// demand is 241 members. The 4,999 hard window therefore leaves at most
		// 604 members for the atomic pair. Two C52/R5 cells require 664 members;
		// C52/R4 requires 536 and is the densest registered integer rail count that
		// leaves the shell's measured closure demand intact. This is an E6-pair
		// topology identity, not a general multi-cell or retry search.
		GenerationSettings.RailCount = FMath::Min(
			GenerationSettings.RailCount, 4);
	}

	TArray<FCoupledExteriorFrameCellRequest> SortedCells = Cells;
	SortedCells.Sort([](const FCoupledExteriorFrameCellRequest& A,
		const FCoupledExteriorFrameCellRequest& B)
	{
		if (A.SourceVolumeId != B.SourceVolumeId)
		{
			return A.SourceVolumeId < B.SourceVolumeId;
		}
		if (A.BayId != B.BayId)
		{
			return A.BayId < B.BayId;
		}
		if (!FMath::IsNearlyEqual(A.LocalBounds.Min.X, B.LocalBounds.Min.X))
		{
			return A.LocalBounds.Min.X < B.LocalBounds.Min.X;
		}
		return A.LocalBounds.Min.Y < B.LocalBounds.Min.Y;
	});

	TArray<FCoupledExteriorFrameCellPlan> CandidatePlans;
	CandidatePlans.Reserve(SortedCells.Num());
	for (int32 CellIndex = 0; CellIndex < SortedCells.Num(); ++CellIndex)
	{
		FCoupledExteriorFrameCellPlan Plan;
		if (!BuildCellPlan(
			GenerationSettings, SortedCells[CellIndex], CellIndex, Plan, OutError))
		{
			return Reject(OutResult, OutError, OutError);
		}
		CandidatePlans.Add(MoveTemp(Plan));
	}
	if (bRequiresSharedCoursePair
		&& !BuildSharedCoursePairPlans(
			GenerationSettings, BeamASettings, CandidatePlans, OutError))
	{
		return Reject(OutResult, OutError, OutError);
	}

	FCoupledExteriorFrameResult PlannedResult;
	PlannedResult.Summary.RequestedCellCount = CandidatePlans.Num();
	PlannedResult.Summary.ClosureReserveMemberCount =
		4 * GenerationSettings.RailCount
			* GenerationSettings.MaximumMacroBandCount;
	const int32 HardFinalMemberCount = FMath::Min(
		Settings.MaximumFinalMemberCount, BeamASettings.MaxMemberCount);
	if (bRequiresSharedCoursePair)
	{
		const FCoupledExteriorFramePlannedMember* Shared = nullptr;
		for (const FCoupledExteriorFrameCellPlan& Plan : CandidatePlans)
		{
			Shared = Plan.Members.FindByPredicate(
				[](const FCoupledExteriorFramePlannedMember& Member)
				{
					return Member.Kind
						== ECoupledExteriorFrameMemberKind::SharedCourse;
				});
			if (Shared != nullptr)
			{
				break;
			}
		}
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M7.3-Beam-C3V2][SharedCoursePlanDerived]")
			TEXT(" Span=%d Axis=%d Courses=%d Course=%d CourseZ=%.2f SharedSpan=%.2f")
			TEXT(" Rails=%d Planned=%d Reserve=%d Hard=%d"),
			CandidatePlans[0].Cell.CoupledSpanVolumeId,
			Shared == nullptr ? INDEX_NONE : static_cast<int32>(Shared->Axis),
			GenerationSettings.CourseCount,
			Shared == nullptr ? INDEX_NONE : Shared->CourseIndex,
			Shared == nullptr ? -1.0 : Shared->LocalStart.Z,
			Shared == nullptr ? -1.0
				: FVector::Distance(Shared->LocalStart, Shared->LocalEnd),
			GenerationSettings.RailCount,
			CandidatePlans[0].Members.Num() + CandidatePlans[1].Members.Num(),
			PlannedResult.Summary.ClosureReserveMemberCount,
			HardFinalMemberCount);
	}
	TSet<int32> Removed;
	TSet<int32> ReplaceableBridgeRailIds;
	if (bRequiresSharedCoursePair)
	{
		const double Tolerance = FMath::Max(
			KINDA_SMALL_NUMBER,
			static_cast<double>(BeamASettings.JointMergeToleranceCM));
		for (const FABTSM73BeamAMember& Existing : InOutAssembly.Members)
		{
			if (Existing.Role != EABTSM73BeamAMemberRole::BridgeRail
				|| (Existing.Axis != EABTSM73BeamAFrameAxis::X
					&& Existing.Axis != EABTSM73BeamAFrameAxis::Y))
			{
				continue;
			}
			const FBox ExistingBounds = MemberBounds(Existing, InOutAssembly);
			if (!IsFiniteBox(ExistingBounds))
			{
				continue;
			}
			const int32 Axis = static_cast<int32>(Existing.Axis);
			const int32 Perpendicular = Axis == 0 ? 1 : 0;
			for (const FCoupledExteriorFrameCellPlan& Plan : CandidatePlans)
			{
				const FCoupledExteriorFramePlannedMember* Cover =
					Plan.Members.FindByPredicate(
						[&](const FCoupledExteriorFramePlannedMember& Planned)
						{
							return Planned.Kind
									== ECoupledExteriorFrameMemberKind::SharedCourse
								&& Planned.Axis == Existing.Axis
								&& FMath::Abs(
									Planned.LocalBounds.GetCenter()[Perpendicular]
										- ExistingBounds.GetCenter()[Perpendicular])
									<= Tolerance
								&& FMath::Abs(Planned.LocalBounds.GetCenter().Z
									- ExistingBounds.GetCenter().Z)
									<= Tolerance * 2.0 + KINDA_SMALL_NUMBER
								&& ExistingBounds.Min[Axis]
									>= Planned.LocalBounds.Min[Axis] - Tolerance
								&& ExistingBounds.Max[Axis]
									<= Planned.LocalBounds.Max[Axis] + Tolerance;
						});
				if (Cover != nullptr)
				{
					ReplaceableBridgeRailIds.Add(Existing.MemberId);
					break;
				}
			}
		}
	}
	FString CandidateCanonical;
	int32 RequiredContacts = 0;
	for (int32 CandidateIndex = 0;
		CandidateIndex < CandidatePlans.Num(); ++CandidateIndex)
	{
		FCoupledExteriorFrameCellPlan& Plan = CandidatePlans[CandidateIndex];
		FString GeometryConflict;
		for (const FCoupledExteriorFramePlannedMember& Planned : Plan.Members)
		{
			for (const FCoupledExteriorFramePlannedMember& Selected :
				PlannedResult.PlannedMembers)
			{
				if (HasPositiveOverlap(Planned.LocalBounds, Selected.LocalBounds,
					BeamASettings.JointMergeToleranceCM))
				{
					GeometryConflict = TEXT("BeamC3V2CellPlansOverlap");
					break;
				}
			}
			if (!GeometryConflict.IsEmpty())
			{
				break;
			}
			for (const FABTSM73BeamASupportVoid& SupportVoid :
				InOutAssembly.ReservedSupportVoids)
			{
				// A full-height coupled frame is rooted in one grounded source but can
				// pass through several stacked semantic volumes. Reserved undercrofts
				// therefore remain global geometric exclusions; the root SourceId must
				// never be used to ignore a void owned by another volume.
				if (HasPositiveOverlap(Planned.LocalBounds, SupportVoid.Bounds,
					BeamASettings.JointMergeToleranceCM))
				{
					GeometryConflict =
						TEXT("BeamC3V2ReservedSupportVoidConflict");
					break;
				}
			}
			if (!GeometryConflict.IsEmpty())
			{
				break;
			}
			for (const FABTSM73BeamAMember& Existing : InOutAssembly.Members)
			{
				if (!HasPositiveOverlap(Planned.LocalBounds,
					MemberBounds(Existing, InOutAssembly),
					BeamASettings.JointMergeToleranceCM))
				{
					continue;
				}
				const bool bCoveredE6BridgeRail =
					ReplaceableBridgeRailIds.Contains(Existing.MemberId);
				if (Existing.Axis == EABTSM73BeamAFrameAxis::Diagonal
					|| (IsProtectedRole(Existing.Role)
						&& !bCoveredE6BridgeRail))
				{
					const FBox ExistingBounds =
						MemberBounds(Existing, InOutAssembly);
					GeometryConflict = FString::Printf(
						TEXT("BeamC3V2ProtectedMemberConflict:")
						TEXT("Cell=%d:Kind=%d:Course=%d:Axis=%d:")
						TEXT("Planned=%s..%s:Existing=%d:Role=%d:Axis=%d:")
						TEXT("Bounds=%s..%s"),
						Plan.CellIndex, static_cast<int32>(Planned.Kind),
						Planned.CourseIndex, static_cast<int32>(Planned.Axis),
						*Planned.LocalBounds.Min.ToCompactString(),
						*Planned.LocalBounds.Max.ToCompactString(),
						Existing.MemberId, static_cast<int32>(Existing.Role),
						static_cast<int32>(Existing.Axis),
						*ExistingBounds.Min.ToCompactString(),
						*ExistingBounds.Max.ToCompactString());
					break;
				}
			}
			if (!GeometryConflict.IsEmpty())
			{
				break;
			}
		}
		if (!GeometryConflict.IsEmpty())
		{
			CandidateCanonical += FString::Printf(
				TEXT("|%d,%d,%u,%d,%d,%d,%u:G"), Plan.Cell.SourceVolumeId,
				Plan.Cell.BayId, Plan.Cell.RootAuthorityCrc32,
				Plan.Cell.CoupledSpanVolumeId, Plan.Cell.CoupledSupportBayId,
				static_cast<int32>(Plan.Cell.CoupledEndpointSign),
				Plan.GeometryCrc32);
			if (bRequiresSharedCoursePair
				|| PlannedResult.CellPlans.IsEmpty())
			{
				return Reject(OutResult, OutError, GeometryConflict);
			}
			++PlannedResult.Summary.GeometryLimitedCellCount;
			continue;
		}

		TSet<int32> ProspectiveRemoved = Removed;
		for (const FCoupledExteriorFramePlannedMember& Planned : Plan.Members)
		{
			for (const FABTSM73BeamAMember& Existing : InOutAssembly.Members)
			{
				if (HasPositiveOverlap(Planned.LocalBounds,
					MemberBounds(Existing, InOutAssembly),
					BeamASettings.JointMergeToleranceCM))
				{
					ProspectiveRemoved.Add(Existing.MemberId);
				}
			}
		}
		const int32 ProjectedPreClosure = InOutAssembly.Members.Num()
			- ProspectiveRemoved.Num()
			+ PlannedResult.PlannedMembers.Num() + Plan.Members.Num();
		const bool bAdditionalCell = !PlannedResult.CellPlans.IsEmpty();
		const int32 AdmissionLimit = bAdditionalCell
			? HardFinalMemberCount
				- PlannedResult.Summary.ClosureReserveMemberCount
			: HardFinalMemberCount;
		if (ProjectedPreClosure > AdmissionLimit)
		{
			CandidateCanonical += FString::Printf(
				TEXT("|%d,%d,%u,%d,%d,%d,%u:B%d>%d"),
				Plan.Cell.SourceVolumeId, Plan.Cell.BayId,
				Plan.Cell.RootAuthorityCrc32, Plan.Cell.CoupledSpanVolumeId,
				Plan.Cell.CoupledSupportBayId,
				static_cast<int32>(Plan.Cell.CoupledEndpointSign), Plan.GeometryCrc32,
				ProjectedPreClosure, AdmissionLimit);
			if (bRequiresSharedCoursePair || !bAdditionalCell)
			{
				return Reject(OutResult, OutError,
					FString::Printf(TEXT("BeamC3V2FinalBudgetInsufficient:%d>%d"),
						ProjectedPreClosure, AdmissionLimit));
			}
			++PlannedResult.Summary.BudgetLimitedCellCount;
			continue;
		}

		const int32 SelectedCellIndex = PlannedResult.CellPlans.Num();
		Plan.CellIndex = SelectedCellIndex;
		for (FCoupledExteriorFramePlannedMember& Planned : Plan.Members)
		{
			Planned.CellIndex = SelectedCellIndex;
		}
		CandidateCanonical += FString::Printf(
			TEXT("|%d,%d,%u,%d,%d,%d,%u:S%d"), Plan.Cell.SourceVolumeId,
			Plan.Cell.BayId, Plan.Cell.RootAuthorityCrc32,
			Plan.Cell.CoupledSpanVolumeId, Plan.Cell.CoupledSupportBayId,
			static_cast<int32>(Plan.Cell.CoupledEndpointSign), Plan.GeometryCrc32,
			ProjectedPreClosure);
		Removed = MoveTemp(ProspectiveRemoved);
		PlannedResult.Summary.MacroBandCount += Plan.MacroBandCount;
		RequiredContacts += (Plan.CourseCount - 1)
			* GenerationSettings.RailCount * GenerationSettings.RailCount
			+ 4 * (GenerationSettings.RailCount + GenerationSettings.RailCount)
				* Plan.MacroBandCount
			+ 2 * (GenerationSettings.RailCount + GenerationSettings.RailCount)
				* (2 * Plan.MacroBandCount - 1);
		PlannedResult.PlannedMembers.Append(Plan.Members);
		PlannedResult.CellPlans.Add(Plan);
	}
	if (bRequiresSharedCoursePair && PlannedResult.CellPlans.Num() != 2)
	{
		return Reject(OutResult, OutError,
			TEXT("BeamC3V2SharedCoursePairAdmissionIncomplete"));
	}
	const int32 StructuralAdmissionLimit = Settings.MaximumStructuralMemberCount
		+ (bRequiresSharedCoursePair ? Settings.RailCount : 0);
	if (PlannedResult.PlannedMembers.Num() > StructuralAdmissionLimit)
	{
		return Reject(OutResult, OutError,
			FString::Printf(TEXT("BeamC3V2StructuralBudgetInsufficient:%d>%d"),
				PlannedResult.PlannedMembers.Num(), StructuralAdmissionLimit));
	}
	FString PlanCanonical = FString::Printf(
		TEXT("BeamC3V2PlanSet:v6:C=%d:RequestedTarget=%d:Maximum=%d:Requested=%d:")
		TEXT("Selected=%d:BudgetSkipped=%d:GeometrySkipped=%d:Reserve=%d:Hard=%d%s"),
		GenerationSettings.CourseCount, Cells.Num(), Settings.MaximumCellCount,
		PlannedResult.Summary.RequestedCellCount, PlannedResult.CellPlans.Num(),
		PlannedResult.Summary.BudgetLimitedCellCount,
		PlannedResult.Summary.GeometryLimitedCellCount,
		PlannedResult.Summary.ClosureReserveMemberCount, HardFinalMemberCount,
		*CandidateCanonical);

	FABTSM73BeamAGenerationResult Scratch = InOutAssembly;
	const int32 FinalPreClosureCount = Scratch.Members.Num() - Removed.Num()
		+ PlannedResult.PlannedMembers.Num();
	if (FinalPreClosureCount > HardFinalMemberCount)
	{
		return Reject(OutResult, OutError,
			FString::Printf(TEXT("BeamC3V2FinalBudgetInsufficient:%d>%d"),
				FinalPreClosureCount, HardFinalMemberCount));
	}

	CompactRemovedMembers(Scratch, Removed);
	if (Scratch.Joints.Num() + PlannedResult.PlannedMembers.Num() * 2
		> BeamASettings.MaxJointCount)
	{
		return Reject(OutResult, OutError, TEXT("BeamC3V2JointBudgetInsufficient"));
	}
	for (const FCoupledExteriorFrameCellPlan& Plan : PlannedResult.CellPlans)
	{
		AppendCellPlan(Plan, Scratch);
	}
	FString ClosureError;
	if (!ABTSM73BeamA::CloseGeneratedAssembly(BeamASettings, Scratch, ClosureError))
	{
		return Reject(OutResult, OutError, ClosureError);
	}
	if (Scratch.Members.Num() > Settings.MaximumFinalMemberCount)
	{
		return Reject(OutResult, OutError, TEXT("BeamC3V2ClosureExceededFinalBudget"));
	}

	PlannedResult.Summary.CellCount = PlannedResult.CellPlans.Num();
	PlannedResult.Summary.ReplacedMemberCount = Removed.Num();
	PlannedResult.Summary.InsertedMemberCount = PlannedResult.PlannedMembers.Num();
	PlannedResult.Summary.FinalMemberCount = Scratch.Members.Num();
	PlannedResult.Summary.RequiredBearingContactCount = RequiredContacts;
	PlannedResult.Summary.PlanCrc32 = FCrc::StrCrc32(*PlanCanonical);
	for (const FCoupledExteriorFramePlannedMember& Planned :
		PlannedResult.PlannedMembers)
	{
		switch (Planned.Kind)
		{
		case ECoupledExteriorFrameMemberKind::CoreRail:
			++PlannedResult.Summary.CoreRailCount;
			break;
		case ECoupledExteriorFrameMemberKind::ThroughOutrigger:
			++PlannedResult.Summary.ThroughOutriggerCount;
			break;
		case ECoupledExteriorFrameMemberKind::FacadeRail:
			++PlannedResult.Summary.FacadeRailCount;
			break;
		case ECoupledExteriorFrameMemberKind::ExteriorPost:
			++PlannedResult.Summary.ExteriorPostCount;
			break;
		case ECoupledExteriorFrameMemberKind::SharedCourse:
			++PlannedResult.Summary.SharedCourseCount;
			break;
		}
	}
	PlannedResult.Summary.SharedCourseRailCount =
		PlannedResult.Summary.SharedCourseCount;
	TArray<int32> MatchedMembers;
	if (!CertifyGeometry(GenerationSettings, BeamASettings, Scratch, PlannedResult,
		MatchedMembers, OutError))
	{
		return Reject(OutResult, OutError, OutError);
	}
	PlannedResult.Summary.bAccepted = true;
	PlannedResult.Summary.RejectReason.Reset();
	RefreshSummary(Scratch);
	InOutAssembly = MoveTemp(Scratch);
	OutResult = MoveTemp(PlannedResult);
	return true;
}

bool FABTSM73BeamC3V2CoupledExteriorFrameGenerator::CertifyFinalAssembly(
	const FABTSM73BeamC3V2CoupledExteriorFrameSettings& Settings,
	const FABTSM73BeamAPreviewSettings& BeamASettings,
	const FABTSM73BeamAGenerationResult& Assembly,
	const FABTSM73BeamCGenerationResult& FinalBeamC,
	FCoupledExteriorFrameResult& InOutResult,
	FString& OutError) const
{
	using namespace ABTSM73BeamC3V2;
	OutError.Reset();
	if (!InOutResult.Summary.bAccepted || InOutResult.PlannedMembers.IsEmpty())
	{
		OutError = TEXT("BeamC3V2PlanNotAccepted");
		InOutResult.Summary.RejectReason = OutError;
		return false;
	}
	TArray<int32> MatchedMembers;
	if (!CertifyGeometry(Settings, BeamASettings, Assembly, InOutResult,
		MatchedMembers, OutError))
	{
		InOutResult.Summary.bAccepted = false;
		InOutResult.Summary.RejectReason = OutError;
		return false;
	}
	// Deferred Beam-C closure may add grounded transfer grillages after the V2
	// plan is committed. Beam-A must split every resulting vertical member at
	// the same registered 720 cm (or tighter) limit before V2 may publish final
	// DAG evidence. This is an all-final-Z segment audit, not a Chaos claim.
	const double FinalZSpanLimit = FMath::Min(
		static_cast<double>(Settings.MaximumPostSegmentSpanCM),
		static_cast<double>(BeamASettings.MaximumVerticalSupportSpanCM));
	float MaximumFinalZSpan = 0.0f;
	int32 MaximumFinalZMemberId = INDEX_NONE;
	for (const FABTSM73BeamAMember& Member : Assembly.Members)
	{
		if (Member.Axis != EABTSM73BeamAFrameAxis::Z)
		{
			continue;
		}
		if (Member.LengthCM > MaximumFinalZSpan)
		{
			MaximumFinalZSpan = Member.LengthCM;
			MaximumFinalZMemberId = Member.MemberId;
		}
	}
	if (MaximumFinalZSpan
		> FinalZSpanLimit + BeamASettings.JointMergeToleranceCM)
	{
		OutError = FString::Printf(
			TEXT("BeamC3V2FinalAllZSpanExceeded:%.2f>%.2f:Member=%d"),
			MaximumFinalZSpan, FinalZSpanLimit, MaximumFinalZMemberId);
		InOutResult.Summary.bAccepted = false;
		InOutResult.Summary.bDAGCertified = false;
		InOutResult.Summary.RejectReason = OutError;
		return false;
	}
	if (!FinalBeamC.Summary.bAccepted
		|| FinalBeamC.Summary.LoadDAGHash == 0
		|| FinalBeamC.Nodes.Num() != Assembly.Members.Num()
		|| FinalBeamC.Edges.Num() != Assembly.BearingContacts.Num()
		|| FinalBeamC.Summary.GroundUnreachableNodeCount != 0
		|| FinalBeamC.Summary.CycleNodeCount != 0
		|| FinalBeamC.Summary.BearingAreaViolationCount != 0
		|| FinalBeamC.Summary.RealContactMismatchCount != 0
		|| FinalBeamC.Summary.SupportResultantViolationCount != 0
		|| FinalBeamC.Summary.SupportSpreadViolationCount != 0
		|| FinalBeamC.Summary.ReactionBalanceViolationCount != 0
		|| FinalBeamC.Summary.SpanViolationCount != 0
		|| FinalBeamC.Summary.CantileverViolationCount != 0
		|| FinalBeamC.Summary.ColumnSlendernessViolationCount != 0
		|| FinalBeamC.Summary.LateralMechanismViolationCount != 0)
	{
		OutError = TEXT("BeamC3V2FinalDAGNotAccepted");
		InOutResult.Summary.bAccepted = false;
		InOutResult.Summary.RejectReason = OutError;
		return false;
	}
	for (const int32 MemberId : MatchedMembers)
	{
		const FABTSM73BeamCLoadNode* Node = FinalBeamC.Nodes.FindByPredicate(
			[MemberId](const FABTSM73BeamCLoadNode& Candidate)
			{
				return Candidate.MemberId == MemberId;
			});
		if (Node == nullptr || !Node->bGroundReachable)
		{
			OutError = TEXT("BeamC3V2PlannedMemberNotDAGReachable");
			InOutResult.Summary.bAccepted = false;
			InOutResult.Summary.RejectReason = OutError;
			return false;
		}
	}
	const FString DAGEvidence = FString::Printf(
		TEXT("%u:%u:%lld:%d:%d:%.6f"),
		InOutResult.Summary.FinalGeometryCrc32,
		InOutResult.Summary.SharedCourseCrc32,
		FinalBeamC.Summary.LoadDAGHash,
		FinalBeamC.Nodes.Num(), FinalBeamC.Edges.Num(), MaximumFinalZSpan);
	InOutResult.Summary.DAGEvidenceCrc32 = FCrc::StrCrc32(*DAGEvidence);
	InOutResult.Summary.FinalMemberCount = Assembly.Members.Num();
	InOutResult.Summary.MaximumPostSegmentSpanCM = FMath::Max(
		InOutResult.Summary.MaximumPostSegmentSpanCM, MaximumFinalZSpan);
	InOutResult.Summary.bDAGCertified = true;
	InOutResult.Summary.bAccepted = true;
	InOutResult.Summary.RejectReason.Reset();
	return true;
}
