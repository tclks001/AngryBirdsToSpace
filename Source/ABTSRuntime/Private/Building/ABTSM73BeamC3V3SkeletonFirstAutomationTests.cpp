// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Algo/AllOf.h"

#include "ABTSM73BeamC3V3SkeletonFirstGenerator.h"
#include "ABTSM73BeamD0ProfileCatalog.h"
#include "ABTSM73BeamD1BrickCompiler.h"
#include "Building/ABTSM73BeamDemoManifest.h"
#include "ABTSM73DAG5BShapeGrammarV2.h"

namespace ABTSM73BeamC3V3Tests
{
	struct FStage1Case
	{
		FName ProfileId;
		int32 Tier = 0;
		int32 Seed = 0;
	};

	const TArray<FStage1Case>& MatrixCases()
	{
		static const TArray<FStage1Case> Cases = []()
		{
			TArray<FStage1Case> Result;
			const TArray<TPair<FName, int32>> Profiles = {
				{TEXT("ColumnBreak"), 710000},
				{TEXT("SeamRelease"), 720000},
				{TEXT("TipOver"), 730000},
				{TEXT("DropTrigger"), 740000},
				{TEXT("SlideRelease"), 750137}};
			for (const TPair<FName, int32>& Profile : Profiles)
			{
				for (int32 Tier = 0; Tier <= 5; ++Tier)
				{
					Result.Add({Profile.Key, Tier, Profile.Value});
				}
			}
			return Result;
		}();
		return Cases;
	}

	const TArray<FStage1Case>& BoundaryCases()
	{
		static const TArray<FStage1Case> Cases = {
			{TEXT("DropTrigger"), 2, 740000},
			{TEXT("TipOver"), 0, 730000},
			{TEXT("SeamRelease"), 5, 720000},
			{TEXT("ColumnBreak"), 5, 710000}};
		return Cases;
	}

	FString CaseCommand(const FStage1Case& Case)
	{
		return FString::Printf(
			TEXT("%s|%d|%d"),
			*Case.ProfileId.ToString(), Case.Tier, Case.Seed);
	}

	bool ParseCase(const FString& Parameters, FStage1Case& OutCase)
	{
		TArray<FString> Parts;
		Parameters.ParseIntoArray(Parts, TEXT("|"), true);
		if (Parts.Num() != 3)
		{
			return false;
		}
		OutCase.ProfileId = FName(*Parts[0]);
		OutCase.Tier = FCString::Atoi(*Parts[1]);
		OutCase.Seed = FCString::Atoi(*Parts[2]);
		return OutCase.Tier >= 0 && OutCase.Tier <= 5;
	}

	FString JoinIds(const TArray<int32>& Ids)
	{
		FString Result;
		for (const int32 Id : Ids)
		{
			Result += FString::Printf(TEXT("%s%d"),
				Result.IsEmpty() ? TEXT("") : TEXT(","), Id);
		}
		return Result;
	}

	bool ResolveShape(
		const FStage1Case& Case,
		FABTSM73BeamD0ResolvedProfile& OutProfile,
		FABTSM73DAG5BV2GenerationResult& OutSilhouette,
		FString& OutError)
	{
		if (!FABTSM73BeamD0ProfileCatalog::GetDefault().Resolve(
			Case.ProfileId, Case.Tier, Case.Seed, OutProfile, OutError))
		{
			return false;
		}
		return FABTSM73DAG5BShapeGrammarV2().Generate(
			OutProfile.BeamSettings.BeamB.BeamA.Silhouette,
			OutSilhouette, OutError);
	}

	FABTSM73DAG5BV2Volume MakeBodyVolume(
		const int32 VolumeId,
		const FVector& Minimum,
		const FVector& Maximum,
		const TCHAR* DerivationPath)
	{
		FABTSM73DAG5BV2Volume Volume;
		Volume.VolumeId = VolumeId;
		Volume.LocalBounds = FBox(Minimum, Maximum);
		Volume.Role = EABTSM73DAG5BV2VolumeRole::Body;
		Volume.Primitive = EABTSM73DAG5BV2Primitive::Box;
		Volume.DerivationPath = DerivationPath;
		return Volume;
	}

	constexpr double SkeletonV3TestBlockUnitsCM = 36.0;
	constexpr double SkeletonV3TestHalfBlockCM =
		SkeletonV3TestBlockUnitsCM * 0.5;
	constexpr double SkeletonV3TestGeometryToleranceCM = 0.01;

	FBox SkeletonV3TestMemberBounds(
		const ABTSM73BeamC3V3::FPlannedMember& Member)
	{
		FVector Minimum(
			FMath::Min(Member.LocalStart.X, Member.LocalEnd.X),
			FMath::Min(Member.LocalStart.Y, Member.LocalEnd.Y),
			FMath::Min(Member.LocalStart.Z, Member.LocalEnd.Z));
		FVector Maximum(
			FMath::Max(Member.LocalStart.X, Member.LocalEnd.X),
			FMath::Max(Member.LocalStart.Y, Member.LocalEnd.Y),
			FMath::Max(Member.LocalStart.Z, Member.LocalEnd.Z));
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Axis == static_cast<int32>(Member.Axis))
			{
				continue;
			}
			const double Center =
				(Member.LocalStart[Axis] + Member.LocalEnd[Axis]) * 0.5;
			Minimum[Axis] = Center - SkeletonV3TestHalfBlockCM;
			Maximum[Axis] = Center + SkeletonV3TestHalfBlockCM;
		}
		return FBox(Minimum, Maximum);
	}

	bool IsStage2CouplingGeometricallyOutward(
		const ABTSM73BeamC3V3::FPlan& Plan,
		const ABTSM73BeamC3V3::FPlannedMember& Member)
	{
		if (Member.ProducedStage
			!= EABTSM73BeamC3GenerationStage::CouplingCourses
			|| !Plan.CoreCells.IsValidIndex(Member.OriginCoreCellId))
		{
			return false;
		}
		const ABTSM73BeamC3V3::FCoreCellPlan& Core =
			Plan.CoreCells[Member.OriginCoreCellId];
		if (!Plan.Components.IsValidIndex(Core.ComponentId))
		{
			return false;
		}
		const bool bXAxis = Member.FaceMask == ABTSM73BeamC3V3::NegativeX
			|| Member.FaceMask == ABTSM73BeamC3V3::PositiveX;
		const bool bYAxis = Member.FaceMask == ABTSM73BeamC3V3::NegativeY
			|| Member.FaceMask == ABTSM73BeamC3V3::PositiveY;
		if ((!bXAxis && !bYAxis)
			|| (bXAxis && Member.Axis != EABTSM73BeamAFrameAxis::X)
			|| (bYAxis && Member.Axis != EABTSM73BeamAFrameAxis::Y))
		{
			return false;
		}
		const bool bPositive = Member.FaceMask == ABTSM73BeamC3V3::PositiveX
			|| Member.FaceMask == ABTSM73BeamC3V3::PositiveY;
		const double DirectionSign = bPositive ? 1.0 : -1.0;
		const int32 AxisIndex = bXAxis ? 0 : 1;
		const double CoreOuterFaceCM = Member.StationB
			* SkeletonV3TestBlockUnitsCM
			+ DirectionSign * SkeletonV3TestHalfBlockCM;
		const double EndpointCM = bPositive
			? Member.LocalEnd[AxisIndex] : Member.LocalStart[AxisIndex];
		const double ComponentCenterCM =
			Plan.Components[Core.ComponentId].BodyBounds.GetCenter()[AxisIndex];
		return DirectionSign * (CoreOuterFaceCM - ComponentCenterCM)
				>= -SkeletonV3TestGeometryToleranceCM
			&& DirectionSign * (EndpointCM - CoreOuterFaceCM)
				>= SkeletonV3TestBlockUnitsCM
					- SkeletonV3TestGeometryToleranceCM;
	}

	FBox Stage2TestCoreCourseBounds(
		const ABTSM73BeamC3V3::FPlan& Plan,
		const ABTSM73BeamC3V3::FCoreCellPlan& Core,
		const int32 Course)
	{
		if (!Plan.Components.IsValidIndex(Core.ComponentId) || Course < 0
			|| Course >= Core.TopCourseIndex)
		{
			return FBox(EForceInit::ForceInit);
		}
		const bool bUpper = Core.SingleShrinkCourseIndex > 0
			&& Course >= Core.SingleShrinkCourseIndex
			&& !Core.UpperXStations.IsEmpty() && !Core.UpperYStations.IsEmpty();
		const TArray<int32>& XStations = bUpper ? Core.UpperXStations : Core.XStations;
		const TArray<int32>& YStations = bUpper ? Core.UpperYStations : Core.YStations;
		if (XStations.IsEmpty() || YStations.IsEmpty())
		{
			return FBox(EForceInit::ForceInit);
		}
		const double GroundZ = Plan.Components[Core.ComponentId].GroundPlaneZCM;
		return FBox(
			FVector(XStations[0] * SkeletonV3TestBlockUnitsCM - SkeletonV3TestHalfBlockCM,
				YStations[0] * SkeletonV3TestBlockUnitsCM - SkeletonV3TestHalfBlockCM,
				GroundZ + Course * SkeletonV3TestBlockUnitsCM),
			FVector(XStations.Last() * SkeletonV3TestBlockUnitsCM + SkeletonV3TestHalfBlockCM,
				YStations.Last() * SkeletonV3TestBlockUnitsCM + SkeletonV3TestHalfBlockCM,
				GroundZ + (Course + 1) * SkeletonV3TestBlockUnitsCM));
	}

	bool Stage2CouplingAvoidsOtherCoreVolumes(
		const ABTSM73BeamC3V3::FPlan& Plan,
		const ABTSM73BeamC3V3::FPlannedMember& Member)
	{
		if (Member.ProducedStage
			!= EABTSM73BeamC3GenerationStage::CouplingCourses
			|| !Plan.CoreCells.IsValidIndex(Member.OriginCoreCellId))
		{
			return false;
		}
		const bool bXAxis = Member.FaceMask == ABTSM73BeamC3V3::NegativeX
			|| Member.FaceMask == ABTSM73BeamC3V3::PositiveX;
		const bool bPositive = Member.FaceMask == ABTSM73BeamC3V3::PositiveX
			|| Member.FaceMask == ABTSM73BeamC3V3::PositiveY;
		const int32 AxisIndex = bXAxis ? 0 : 1;
		const double DirectionSign = bPositive ? 1.0 : -1.0;
		const double OriginOuterFace = Member.StationB * SkeletonV3TestBlockUnitsCM
			+ DirectionSign * SkeletonV3TestHalfBlockCM;
		FBox Exterior = SkeletonV3TestMemberBounds(Member);
		if (bPositive)
		{
			Exterior.Min[AxisIndex] = FMath::Max(Exterior.Min[AxisIndex], OriginOuterFace);
		}
		else
		{
			Exterior.Max[AxisIndex] = FMath::Min(Exterior.Max[AxisIndex], OriginOuterFace);
		}
		for (const ABTSM73BeamC3V3::FCoreCellPlan& Core : Plan.CoreCells)
		{
			if (Core.CoreCellId == Member.OriginCoreCellId)
			{
				continue;
			}
			const FBox CoreBox = Stage2TestCoreCourseBounds(Plan, Core, Member.CourseIndex);
			if (CoreBox.IsValid
				&& FMath::Min(Exterior.Max.X, CoreBox.Max.X)
					- FMath::Max(Exterior.Min.X, CoreBox.Min.X)
					> SkeletonV3TestGeometryToleranceCM
				&& FMath::Min(Exterior.Max.Y, CoreBox.Max.Y)
					- FMath::Max(Exterior.Min.Y, CoreBox.Min.Y)
					> SkeletonV3TestGeometryToleranceCM
				&& FMath::Min(Exterior.Max.Z, CoreBox.Max.Z)
					- FMath::Max(Exterior.Min.Z, CoreBox.Min.Z)
					> SkeletonV3TestGeometryToleranceCM)
			{
				return false;
			}
		}
		return true;
	}

	bool Stage2DoubleCourseBandsShareFacadeEndpoint(
		const ABTSM73BeamC3V3::FPlan& Plan)
	{
		TMap<int32, double> BandEndpoints;
		TMap<int32, int32> BandCounts;
		for (const ABTSM73BeamC3V3::FPlannedMember& Member : Plan.Members)
		{
			if (Member.ProducedStage
				!= EABTSM73BeamC3GenerationStage::CouplingCourses)
			{
				continue;
			}
			const int32 AxisIndex = static_cast<int32>(Member.Axis);
			const bool bPositive = Member.FaceMask == ABTSM73BeamC3V3::PositiveX
				|| Member.FaceMask == ABTSM73BeamC3V3::PositiveY;
			const double Endpoint = bPositive
				? Member.LocalEnd[AxisIndex] : Member.LocalStart[AxisIndex];
			if (const double* Existing = BandEndpoints.Find(Member.AnchorBandId))
			{
				if (!FMath::IsNearlyEqual(*Existing, Endpoint,
					SkeletonV3TestGeometryToleranceCM))
				{
					return false;
				}
			}
			else
			{
				BandEndpoints.Add(Member.AnchorBandId, Endpoint);
			}
			++BandCounts.FindOrAdd(Member.AnchorBandId);
		}
		return !BandCounts.IsEmpty() && Algo::AllOf(BandCounts,
			[](const TPair<int32, int32>& Pair) { return Pair.Value == 2; });
	}

	bool Stage2PerimeterExposureLedgerIsUnoccluded(
		const ABTSM73BeamC3V3::FPlan& Plan)
	{
		for (const ABTSM73BeamC3V3::FCoreCellPlan& Core : Plan.CoreCells)
		{
			uint8 DerivedMask = 0;
			for (const ABTSM73BeamC3V3::FPerimeterFaceExposure& Exposure
				: Core.PerimeterFaceExposures)
			{
				if (Exposure.CourseIndex < 0
					|| Exposure.TangentMaximumCM - Exposure.TangentMinimumCM
						<= SkeletonV3TestGeometryToleranceCM)
				{
					return false;
				}
				const FBox CoreBox = Stage2TestCoreCourseBounds(
					Plan, Core, Exposure.CourseIndex);
				if (!CoreBox.IsValid)
				{
					return false;
				}
				const bool bXAxis = Exposure.FaceMask == ABTSM73BeamC3V3::NegativeX
					|| Exposure.FaceMask == ABTSM73BeamC3V3::PositiveX;
				const bool bPositive = Exposure.FaceMask == ABTSM73BeamC3V3::PositiveX
					|| Exposure.FaceMask == ABTSM73BeamC3V3::PositiveY;
				const int32 AxisIndex = bXAxis ? 0 : 1;
				const int32 TangentIndex = bXAxis ? 1 : 0;
				const double CoreFace = bPositive
					? CoreBox.Max[AxisIndex] : CoreBox.Min[AxisIndex];
				FBox Corridor = CoreBox;
				Corridor.Min[TangentIndex] = Exposure.TangentMinimumCM;
				Corridor.Max[TangentIndex] = Exposure.TangentMaximumCM;
				Corridor.Min[AxisIndex] = FMath::Min(
					CoreFace, Exposure.FacadeCoordinateCM);
				Corridor.Max[AxisIndex] = FMath::Max(
					CoreFace, Exposure.FacadeCoordinateCM);
				if (bPositive)
				{
					Corridor.Max[AxisIndex] += 2.0 * SkeletonV3TestGeometryToleranceCM;
				}
				else
				{
					Corridor.Min[AxisIndex] -= 2.0 * SkeletonV3TestGeometryToleranceCM;
				}
				for (const ABTSM73BeamC3V3::FCoreCellPlan& OtherCore : Plan.CoreCells)
				{
					if (OtherCore.CoreCellId == Core.CoreCellId)
					{
						continue;
					}
					const FBox OtherBox = Stage2TestCoreCourseBounds(
						Plan, OtherCore, Exposure.CourseIndex);
					if (OtherBox.IsValid
						&& FMath::Min(Corridor.Max.X, OtherBox.Max.X)
							- FMath::Max(Corridor.Min.X, OtherBox.Min.X)
								> SkeletonV3TestGeometryToleranceCM
						&& FMath::Min(Corridor.Max.Y, OtherBox.Max.Y)
							- FMath::Max(Corridor.Min.Y, OtherBox.Min.Y)
								> SkeletonV3TestGeometryToleranceCM
						&& FMath::Min(Corridor.Max.Z, OtherBox.Max.Z)
							- FMath::Max(Corridor.Min.Z, OtherBox.Min.Z)
								> SkeletonV3TestGeometryToleranceCM)
					{
						return false;
					}
				}
				DerivedMask |= Exposure.FaceMask;
			}
			if (DerivedMask != Core.PerimeterFaceMask)
			{
				return false;
			}
		}
		return true;
	}

	bool Stage2FacadePartitionLedgerIsClosed(
		const ABTSM73BeamC3V3::FPlan& Plan)
	{
		if (Plan.FacadePartitions.IsEmpty()
			|| Plan.Summary.FacadePartitionCount != Plan.FacadePartitions.Num()
			|| Plan.Summary.FacadeHeightAnchorBandCount
				!= Plan.FacadeHeightAnchorBands.Num())
		{
			return false;
		}
		TSet<int32> SeenBandIds;
		int32 DeferredCount = 0;
		auto CountDeferredReason = [&Plan](const uint16 Reason)
		{
			int32 Count = 0;
			for (const ABTSM73BeamC3V3::FFacadePartitionPlan& Partition
				: Plan.FacadePartitions)
			{
				Count += Partition.PerimeterCoreCellIds.IsEmpty()
					&& Partition.AnchorBandIds.IsEmpty()
					&& (Partition.DeferredReasonMask & Reason) != 0 ? 1 : 0;
			}
			return Count;
		};
		for (const ABTSM73BeamC3V3::FFacadePartitionPlan& Partition
			: Plan.FacadePartitions)
		{
			if (Partition.PartitionId < 0 || Partition.CourseSpans.IsEmpty()
				|| Partition.FirstCourseIndex < 0
				|| Partition.LastCourseIndexExclusive <= Partition.FirstCourseIndex
				|| Partition.TangentMaximumCM - Partition.TangentMinimumCM
					<= SkeletonV3TestGeometryToleranceCM)
			{
				return false;
			}
			const bool bDeferred = Partition.PerimeterCoreCellIds.IsEmpty()
				&& Partition.AnchorBandIds.IsEmpty();
			DeferredCount += bDeferred ? 1 : 0;
			if ((bDeferred && Partition.DeferredReasonMask
					== ABTSM73BeamC3V3::FacadeDeferredNone)
				|| (!bDeferred && Partition.DeferredReasonMask
					!= ABTSM73BeamC3V3::FacadeDeferredNone))
			{
				return false;
			}
			for (const int32 BandId : Partition.AnchorBandIds)
			{
				const ABTSM73BeamC3V3::FFacadeHeightAnchorBand* Band =
					Plan.FacadeHeightAnchorBands.FindByPredicate(
						[BandId, &Partition](
							const ABTSM73BeamC3V3::FFacadeHeightAnchorBand& Candidate)
						{
							return Candidate.AnchorBandId == BandId
								&& Candidate.FacadePartitionId == Partition.PartitionId;
						});
				if (Band == nullptr || SeenBandIds.Contains(BandId)
					|| !Plan.Members.IsValidIndex(Band->LowerMemberIndex)
					|| !Plan.Members.IsValidIndex(Band->UpperMemberIndex)
					|| Plan.Members[Band->LowerMemberIndex].FacadePartitionId
						!= Partition.PartitionId
					|| Plan.Members[Band->UpperMemberIndex].FacadePartitionId
						!= Partition.PartitionId)
				{
					return false;
				}
				SeenBandIds.Add(BandId);
			}
		}
		return SeenBandIds.Num() == Plan.FacadeHeightAnchorBands.Num()
			&& DeferredCount == Plan.Summary.DeferredFacadePartitionCount
			&& CountDeferredReason(ABTSM73BeamC3V3::FacadeDeferredNoCoursePair)
				== Plan.Summary.DeferredNoCoursePairPartitionCount
			&& CountDeferredReason(ABTSM73BeamC3V3::FacadeDeferredNoEligibleCore)
				== Plan.Summary.DeferredNoEligibleCorePartitionCount
			&& CountDeferredReason(ABTSM73BeamC3V3::FacadeDeferredNoFreeCrossStation)
				== Plan.Summary.DeferredNoFreeCrossStationPartitionCount
			&& CountDeferredReason(ABTSM73BeamC3V3::FacadeDeferredNoStage1Bearing)
				== Plan.Summary.DeferredNoStage1BearingPartitionCount
			&& CountDeferredReason(ABTSM73BeamC3V3::FacadeDeferredNoFacadeTarget)
				== Plan.Summary.DeferredNoFacadeTargetPartitionCount
			&& CountDeferredReason(ABTSM73BeamC3V3::FacadeDeferredLengthLimit)
				== Plan.Summary.DeferredLengthLimitPartitionCount
			&& CountDeferredReason(ABTSM73BeamC3V3::FacadeDeferredNotOutward)
				== Plan.Summary.DeferredNotOutwardPartitionCount
			&& CountDeferredReason(ABTSM73BeamC3V3::FacadeDeferredEnvelopeGap)
				== Plan.Summary.DeferredEnvelopeGapPartitionCount
			&& CountDeferredReason(ABTSM73BeamC3V3::FacadeDeferredOtherCoreBlocked)
				== Plan.Summary.DeferredOtherCoreBlockedPartitionCount
			&& CountDeferredReason(ABTSM73BeamC3V3::FacadeDeferredProtectedVoid)
				== Plan.Summary.DeferredProtectedVoidPartitionCount
			&& CountDeferredReason(ABTSM73BeamC3V3::FacadeDeferredMemberCollision)
				== Plan.Summary.DeferredMemberCollisionPartitionCount
			&& CountDeferredReason(ABTSM73BeamC3V3::FacadeDeferredExhaustedCandidates)
				== Plan.Summary.DeferredExhaustedCandidatePartitionCount;
	}

	bool Stage2ResolvedFacadeEnvelopeIsClosed(
		const ABTSM73BeamC3V3::FPlan& Plan)
	{
		if (Plan.ResolvedFacadeEnvelopeVolumes.IsEmpty()
			|| Plan.Summary.ResolvedFacadeEnvelopeVolumeCount
				!= Plan.ResolvedFacadeEnvelopeVolumes.Num()
			|| Plan.Summary.ResolvedFacadeEnvelopeRaisedVolumeCount
				!= Plan.RaisedMainReservations.Num()
			|| Plan.Summary.ResolvedFacadeEnvelopeBindingViolationCount != 0
			|| Plan.Summary.ResolvedFacadeEnvelopeHash == 0
			|| Plan.Summary.Stage2InputFacadeEnvelopeHash
				!= Plan.Summary.ResolvedFacadeEnvelopeHash)
		{
			return false;
		}
		for (const FABTSM73DAG5BV2RaisedMainReservation& Reservation
			: Plan.RaisedMainReservations)
		{
			int32 MatchCount = 0;
			for (const ABTSM73BeamC3V3::FResolvedFacadeEnvelopeVolume& Volume
				: Plan.ResolvedFacadeEnvelopeVolumes)
			{
				MatchCount += Volume.bRaisedPodiumBody
					&& Volume.ComponentId == Reservation.ComponentId
					&& Volume.SourceVolumeId == Reservation.SourceVolumeId
					&& Volume.LocalBounds.Equals(Reservation.CoreBounds,
						SkeletonV3TestGeometryToleranceCM)
					? 1 : 0;
			}
			if (MatchCount != 1)
			{
				return false;
			}
		}
		return true;
	}

	double SkeletonV3TestOverlapLength(
		const double MinimumA,
		const double MaximumA,
		const double MinimumB,
		const double MaximumB)
	{
		return FMath::Max(0.0,
			FMath::Min(MaximumA, MaximumB) - FMath::Max(MinimumA, MinimumB));
	}

	const ABTSM73BeamC3V3::FCoreCellPlan* SkeletonV3TestFindCore(
		const ABTSM73BeamC3V3::FPlan& Plan,
		const int32 CoreCellId)
	{
		return Plan.CoreCells.FindByPredicate(
			[CoreCellId](const ABTSM73BeamC3V3::FCoreCellPlan& Core)
			{
				return Core.CoreCellId == CoreCellId;
			});
	}

	TArray<int32> SkeletonV3TestCoreCourseMembers(
		const ABTSM73BeamC3V3::FPlan& Plan,
		const ABTSM73BeamC3V3::FCoreCellPlan& Core,
		const int32 CourseIndex)
	{
		TArray<int32> Result;
		for (const int32 MemberIndex : Core.MemberIndices)
		{
			if (Plan.Members.IsValidIndex(MemberIndex)
				&& Plan.Members[MemberIndex].CourseIndex == CourseIndex)
			{
				Result.Add(MemberIndex);
			}
		}
		return Result;
	}

	bool SkeletonV3TestHasFullCoreBearingFaces(
		FAutomationTestBase& Test,
		const ABTSM73BeamC3V3::FPlan& Plan)
	{
		bool bPassed = true;
		for (const ABTSM73BeamC3V3::FCoreCellPlan& Core : Plan.CoreCells)
		{
			int32 HighestCourse = INDEX_NONE;
			for (const int32 MemberIndex : Core.MemberIndices)
			{
				if (Plan.Members.IsValidIndex(MemberIndex))
				{
					HighestCourse = FMath::Max(
						HighestCourse, Plan.Members[MemberIndex].CourseIndex);
				}
			}
			for (int32 Course = 1; Course <= HighestCourse; ++Course)
			{
				const TArray<int32> UpperMembers =
					SkeletonV3TestCoreCourseMembers(Plan, Core, Course);
				const TArray<int32> LowerMembers =
					SkeletonV3TestCoreCourseMembers(Plan, Core, Course - 1);
				bPassed &= Test.TestEqual(
					*FString::Printf(TEXT("Core %d course %d has its planned rails"),
						Core.CoreCellId, Course), UpperMembers.Num(), Core.RailCount);
				bPassed &= Test.TestEqual(
					*FString::Printf(TEXT("Core %d course %d has its planned lower rails"),
						Core.CoreCellId, Course), LowerMembers.Num(), Core.RailCount);
				for (const int32 UpperIndex : UpperMembers)
				{
					for (const int32 LowerIndex : LowerMembers)
					{
						if (!Plan.Members.IsValidIndex(UpperIndex)
							|| !Plan.Members.IsValidIndex(LowerIndex))
						{
							bPassed = false;
							continue;
						}
						const FBox UpperBounds =
							SkeletonV3TestMemberBounds(Plan.Members[UpperIndex]);
						const FBox LowerBounds =
							SkeletonV3TestMemberBounds(Plan.Members[LowerIndex]);
						const double XOverlap = SkeletonV3TestOverlapLength(
							UpperBounds.Min.X, UpperBounds.Max.X,
							LowerBounds.Min.X, LowerBounds.Max.X);
						const double YOverlap = SkeletonV3TestOverlapLength(
							UpperBounds.Min.Y, UpperBounds.Max.Y,
							LowerBounds.Min.Y, LowerBounds.Max.Y);
						bPassed &= Test.TestTrue(
							*FString::Printf(
								TEXT("Core %d course %d pair %d>%d has a full 36x36 bearing face (%.3fx%.3f)"),
								Core.CoreCellId, Course, LowerIndex, UpperIndex,
								XOverlap, YOverlap),
							XOverlap + SkeletonV3TestGeometryToleranceCM
								>= SkeletonV3TestBlockUnitsCM
							&& YOverlap + SkeletonV3TestGeometryToleranceCM
								>= SkeletonV3TestBlockUnitsCM);
					}
				}
			}
		}
		return bPassed;
	}

	bool SkeletonV3TestHasFullBearingFace(
		const ABTSM73BeamC3V3::FPlannedMember& Lower,
		const ABTSM73BeamC3V3::FPlannedMember& Upper)
	{
		const FBox LowerBounds = SkeletonV3TestMemberBounds(Lower);
		const FBox UpperBounds = SkeletonV3TestMemberBounds(Upper);
		return FMath::Abs(LowerBounds.Max.Z - UpperBounds.Min.Z)
				<= SkeletonV3TestGeometryToleranceCM
			&& SkeletonV3TestOverlapLength(
				LowerBounds.Min.X, LowerBounds.Max.X,
				UpperBounds.Min.X, UpperBounds.Max.X)
				+ SkeletonV3TestGeometryToleranceCM
					>= SkeletonV3TestBlockUnitsCM
			&& SkeletonV3TestOverlapLength(
				LowerBounds.Min.Y, LowerBounds.Max.Y,
				UpperBounds.Min.Y, UpperBounds.Max.Y)
				+ SkeletonV3TestGeometryToleranceCM
					>= SkeletonV3TestBlockUnitsCM;
	}

	struct FSharedBridgeSpanContract
	{
		TMap<int32, TArray<int32>> SharedRailsByCourse;
		TMap<int32, TArray<int32>> DiaphragmsByCourse;
	};

	bool SkeletonV3TestValidateSharedBridgeBands(
		const ABTSM73BeamC3V3::FPlan& Plan,
		FString& OutError)
	{
		OutError.Reset();
		TMap<int32, FSharedBridgeSpanContract> Spans;
		for (int32 MemberIndex = 0; MemberIndex < Plan.Members.Num(); ++MemberIndex)
		{
			const ABTSM73BeamC3V3::FPlannedMember& Member =
				Plan.Members[MemberIndex];
			if (Member.SkeletonKind
				== ABTSM73BeamC3V3::ESkeletonMemberKind::SharedCourse)
			{
				Spans.FindOrAdd(Member.OwnerId).SharedRailsByCourse.FindOrAdd(
					Member.CourseIndex).Add(MemberIndex);
			}
			else if (Member.SkeletonKind
				== ABTSM73BeamC3V3::ESkeletonMemberKind::BridgeDiaphragm)
			{
				Spans.FindOrAdd(Member.OwnerId).DiaphragmsByCourse.FindOrAdd(
					Member.CourseIndex).Add(MemberIndex);
			}
		}
		if (Spans.IsEmpty())
		{
			OutError = TEXT("BeamC3V3TestSharedBridgeMissing");
			return false;
		}

		for (const TPair<int32, FSharedBridgeSpanContract>& SpanPair : Spans)
		{
			const int32 SpanId = SpanPair.Key;
			const FSharedBridgeSpanContract& Span = SpanPair.Value;
			TArray<int32> SharedCourses;
			Span.SharedRailsByCourse.GetKeys(SharedCourses);
			SharedCourses.Sort();
			if (SharedCourses.Num() != 2
				|| SharedCourses[1] != SharedCourses[0] + 2)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3TestSharedBridgeCoursePairInvalid:Span=%d:Courses=%d"),
					SpanId, SharedCourses.Num());
				return false;
			}
			const int32 LowerCourse = SharedCourses[0];
			const int32 UpperCourse = SharedCourses[1];
			const TArray<int32>& LowerRails =
				Span.SharedRailsByCourse.FindChecked(LowerCourse);
			const TArray<int32>& UpperRails =
				Span.SharedRailsByCourse.FindChecked(UpperCourse);
			const TArray<int32>* Diaphragms =
				Span.DiaphragmsByCourse.Find(LowerCourse + 1);
			if (LowerRails.Num() != 2 || UpperRails.Num() != 2)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3TestSharedBridgeRailPairInvalid:Span=%d:Lower=%d:Upper=%d"),
					SpanId, LowerRails.Num(), UpperRails.Num());
				return false;
			}
			if (Diaphragms == nullptr || Diaphragms->Num() < 2)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3TestSharedBridgeDiaphragmCount:Span=%d:Course=%d:Count=%d"),
					SpanId, LowerCourse + 1,
					Diaphragms != nullptr ? Diaphragms->Num() : 0);
				return false;
			}

			const ABTSM73BeamC3V3::FPlannedMember& FirstLower =
				Plan.Members[LowerRails[0]];
			const int32 SharedAxisIndex = static_cast<int32>(FirstLower.Axis);
			double MinimumDiaphragmStation = DBL_MAX;
			double MaximumDiaphragmStation = -DBL_MAX;
			for (const int32 DiaphragmIndex : *Diaphragms)
			{
				if (!Plan.Members.IsValidIndex(DiaphragmIndex))
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3TestSharedBridgeDiaphragmInvalid:Span=%d:Member=%d"),
						SpanId, DiaphragmIndex);
					return false;
				}
				const ABTSM73BeamC3V3::FPlannedMember& Diaphragm =
					Plan.Members[DiaphragmIndex];
				if (Diaphragm.Axis == FirstLower.Axis)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3TestSharedBridgeDiaphragmAxisInvalid:Span=%d:Member=%d"),
						SpanId, DiaphragmIndex);
					return false;
				}
				const double Station = SkeletonV3TestMemberBounds(
					Diaphragm).GetCenter()[SharedAxisIndex];
				MinimumDiaphragmStation = FMath::Min(
					MinimumDiaphragmStation, Station);
				MaximumDiaphragmStation = FMath::Max(
					MaximumDiaphragmStation, Station);
				for (const int32 LowerRailIndex : LowerRails)
				{
					if (!Plan.Members.IsValidIndex(LowerRailIndex)
						|| !Diaphragm.RequiredLowerMemberIndices.Contains(
							LowerRailIndex)
						|| !SkeletonV3TestHasFullBearingFace(
							Plan.Members[LowerRailIndex], Diaphragm))
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3TestSharedBridgeDiaphragmLowerSeatMissing:Span=%d:Diaphragm=%d:Rail=%d"),
							SpanId, DiaphragmIndex, LowerRailIndex);
						return false;
					}
				}
			}
			if (MaximumDiaphragmStation - MinimumDiaphragmStation
				+ SkeletonV3TestGeometryToleranceCM
					< SkeletonV3TestBlockUnitsCM)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3TestSharedBridgeDiaphragmsNotSeparated:Span=%d:Minimum=%.3f:Maximum=%.3f"),
					SpanId, MinimumDiaphragmStation, MaximumDiaphragmStation);
				return false;
			}

			for (const int32 UpperRailIndex : UpperRails)
			{
				if (!Plan.Members.IsValidIndex(UpperRailIndex))
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3TestSharedBridgeUpperRailInvalid:Span=%d:Rail=%d"),
						SpanId, UpperRailIndex);
					return false;
				}
				const ABTSM73BeamC3V3::FPlannedMember& UpperRail =
					Plan.Members[UpperRailIndex];
				for (const int32 DiaphragmIndex : *Diaphragms)
				{
					if (!UpperRail.RequiredLowerMemberIndices.Contains(DiaphragmIndex)
						|| !SkeletonV3TestHasFullBearingFace(
							Plan.Members[DiaphragmIndex], UpperRail))
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3TestSharedBridgeUpperBearingMissing:Span=%d:Rail=%d:Diaphragm=%d"),
							SpanId, UpperRailIndex, DiaphragmIndex);
						return false;
					}
				}
			}
		}
		return true;
	}

	void SkeletonV3TestRemoveMembers(
		ABTSM73BeamC3V3::FPlan& Plan,
		const TSet<int32>& RemovedMembers)
	{
		TArray<int32> OldToNew;
		OldToNew.Init(INDEX_NONE, Plan.Members.Num());
		TArray<ABTSM73BeamC3V3::FPlannedMember> Compacted;
		Compacted.Reserve(Plan.Members.Num() - RemovedMembers.Num());
		for (int32 OldIndex = 0; OldIndex < Plan.Members.Num(); ++OldIndex)
		{
			if (!RemovedMembers.Contains(OldIndex))
			{
				OldToNew[OldIndex] = Compacted.Add(Plan.Members[OldIndex]);
			}
		}
		auto Remap = [&OldToNew](TArray<int32>& Indices)
		{
			for (int32& Index : Indices)
			{
				Index = OldToNew.IsValidIndex(Index) ? OldToNew[Index] : INDEX_NONE;
			}
			Indices.RemoveAll([](const int32 Index)
			{
				return Index == INDEX_NONE;
			});
		};
		for (ABTSM73BeamC3V3::FPlannedMember& Member : Compacted)
		{
			Remap(Member.RequiredLowerMemberIndices);
			Remap(Member.RequiredInwardMemberIndices);
		}
		for (ABTSM73BeamC3V3::FCoreCellPlan& Core : Plan.CoreCells)
		{
			Remap(Core.MemberIndices);
		}
		for (ABTSM73BeamC3V3::FBuildingGroupPlan& Group : Plan.BuildingGroups)
		{
			Remap(Group.MemberIndices);
		}
		Plan.Members = MoveTemp(Compacted);
	}

	bool SkeletonV3TestIntervalsCoverRun(
		TArray<FVector2D> Intervals,
		const double RequiredMinimum,
		const double RequiredMaximum)
	{
		Intervals.Sort([](const FVector2D& A, const FVector2D& B)
		{
			return !FMath::IsNearlyEqual(
				A.X, B.X, SkeletonV3TestGeometryToleranceCM)
				? A.X < B.X : A.Y < B.Y;
		});
		double Cursor = RequiredMinimum;
		for (const FVector2D& Interval : Intervals)
		{
			if (Interval.Y <= Cursor + SkeletonV3TestGeometryToleranceCM)
			{
				continue;
			}
			if (Interval.X > Cursor + SkeletonV3TestGeometryToleranceCM)
			{
				return false;
			}
			Cursor = FMath::Max(Cursor, Interval.Y);
			if (Cursor >= RequiredMaximum - SkeletonV3TestGeometryToleranceCM)
			{
				return true;
			}
		}
		return false;
	}

	bool SkeletonV3TestValidateCommonEnvelope(
		const ABTSM73BeamC3V3::FPlan& Plan,
		FString& OutError)
	{
		OutError.Reset();
		for (int32 MemberIndex = 0; MemberIndex < Plan.Members.Num(); ++MemberIndex)
		{
			const ABTSM73BeamC3V3::EOwnerKind OwnerKind =
				Plan.Members[MemberIndex].OwnerKind;
			if (OwnerKind == ABTSM73BeamC3V3::EOwnerKind::ShellFace
				|| OwnerKind == ABTSM73BeamC3V3::EOwnerKind::Floor)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3TestCommonEnvelopeLegacyLocalShell:Member=%d:Owner=%d"),
					MemberIndex, static_cast<int32>(OwnerKind));
				return false;
			}
		}
		if (Plan.BuildingGroups.Num() != 1 || Plan.Components.IsEmpty()
			|| Plan.CoreCells.IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3TestCommonEnvelopeGroupInvalid:Groups=%d:Components=%d:Cores=%d"),
				Plan.BuildingGroups.Num(), Plan.Components.Num(), Plan.CoreCells.Num());
			return false;
		}
		const ABTSM73BeamC3V3::FBuildingGroupPlan& Group =
			Plan.BuildingGroups[0];
		FVector BodyMinimum(DBL_MAX, DBL_MAX, DBL_MAX);
		FVector BodyMaximum(-DBL_MAX, -DBL_MAX, -DBL_MAX);
		for (const ABTSM73BeamC3V3::FComponentPlan& Component : Plan.Components)
		{
			if (!Component.BodyBounds.IsValid)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3TestCommonEnvelopeBodyInvalid:Component=%d"),
					Component.ComponentId);
				return false;
			}
			BodyMinimum.X = FMath::Min(BodyMinimum.X, Component.BodyBounds.Min.X);
			BodyMinimum.Y = FMath::Min(BodyMinimum.Y, Component.BodyBounds.Min.Y);
			BodyMinimum.Z = FMath::Min(BodyMinimum.Z, Component.BodyBounds.Min.Z);
			BodyMaximum.X = FMath::Max(BodyMaximum.X, Component.BodyBounds.Max.X);
			BodyMaximum.Y = FMath::Max(BodyMaximum.Y, Component.BodyBounds.Max.Y);
			BodyMaximum.Z = FMath::Max(BodyMaximum.Z, Component.BodyBounds.Max.Z);
		}
		if (Group.LocalBounds.Min.X
				> BodyMinimum.X - SkeletonV3TestBlockUnitsCM
					+ SkeletonV3TestGeometryToleranceCM
			|| Group.LocalBounds.Max.X
				< BodyMaximum.X + SkeletonV3TestBlockUnitsCM
					- SkeletonV3TestGeometryToleranceCM
			|| Group.LocalBounds.Min.Y
				> BodyMinimum.Y - SkeletonV3TestBlockUnitsCM
					+ SkeletonV3TestGeometryToleranceCM
			|| Group.LocalBounds.Max.Y
				< BodyMaximum.Y + SkeletonV3TestBlockUnitsCM
					- SkeletonV3TestGeometryToleranceCM)
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3TestCommonEnvelopeOutwardMarginInsufficient:Group=%.3f,%.3f..%.3f,%.3f:Body=%.3f,%.3f..%.3f,%.3f"),
				Group.LocalBounds.Min.X, Group.LocalBounds.Min.Y,
				Group.LocalBounds.Max.X, Group.LocalBounds.Max.Y,
				BodyMinimum.X, BodyMinimum.Y, BodyMaximum.X, BodyMaximum.Y);
			return false;
		}

		struct FFacadeRequirement
		{
			EABTSM73BeamAFrameAxis Axis = EABTSM73BeamAFrameAxis::X;
			int32 Course = INDEX_NONE;
			double CrossCM = 0.0;
			double RunMinimum = 0.0;
			double RunMaximum = 0.0;
			uint8 FaceBit = 0;
		};
		for (const int32 BaseCourse : Group.CommonBandBaseCourseIndices)
		{
			const FFacadeRequirement Requirements[] = {
				{EABTSM73BeamAFrameAxis::X, BaseCourse,
					Group.LocalBounds.Min.Y,
					Group.LocalBounds.Min.X - SkeletonV3TestHalfBlockCM,
					Group.LocalBounds.Max.X + SkeletonV3TestHalfBlockCM,
					ABTSM73BeamC3V3::NegativeY},
				{EABTSM73BeamAFrameAxis::X, BaseCourse,
					Group.LocalBounds.Max.Y,
					Group.LocalBounds.Min.X - SkeletonV3TestHalfBlockCM,
					Group.LocalBounds.Max.X + SkeletonV3TestHalfBlockCM,
					ABTSM73BeamC3V3::PositiveY},
				{EABTSM73BeamAFrameAxis::Y, BaseCourse + 1,
					Group.LocalBounds.Min.X,
					Group.LocalBounds.Min.Y - SkeletonV3TestHalfBlockCM,
					Group.LocalBounds.Max.Y + SkeletonV3TestHalfBlockCM,
					ABTSM73BeamC3V3::NegativeX},
				{EABTSM73BeamAFrameAxis::Y, BaseCourse + 1,
					Group.LocalBounds.Max.X,
					Group.LocalBounds.Min.Y - SkeletonV3TestHalfBlockCM,
					Group.LocalBounds.Max.Y + SkeletonV3TestHalfBlockCM,
					ABTSM73BeamC3V3::PositiveX}};
			for (const FFacadeRequirement& Requirement : Requirements)
			{
				TArray<FVector2D> Intervals;
				const int32 AxisIndex = static_cast<int32>(Requirement.Axis);
				const int32 CrossAxisIndex = AxisIndex == 0 ? 1 : 0;
				for (const int32 MemberIndex : Group.MemberIndices)
				{
					if (!Plan.Members.IsValidIndex(MemberIndex))
					{
						continue;
					}
					const ABTSM73BeamC3V3::FPlannedMember& Member =
						Plan.Members[MemberIndex];
					if (Member.Axis != Requirement.Axis
						|| Member.CourseIndex != Requirement.Course
						|| (Member.FaceMask & Requirement.FaceBit) == 0)
					{
						continue;
					}
					const FBox Bounds = SkeletonV3TestMemberBounds(Member);
					if (FMath::Abs(Bounds.GetCenter()[CrossAxisIndex]
							- Requirement.CrossCM)
						> SkeletonV3TestGeometryToleranceCM)
					{
						continue;
					}
					Intervals.Emplace(
						Bounds.Min[AxisIndex], Bounds.Max[AxisIndex]);
				}
				if (!SkeletonV3TestIntervalsCoverRun(Intervals,
					Requirement.RunMinimum, Requirement.RunMaximum))
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3TestCommonEnvelopeFacadeGap:Course=%d:Axis=%d:Face=%d:Intervals=%d:Run=%.3f..%.3f"),
						Requirement.Course, AxisIndex, Requirement.FaceBit,
						Intervals.Num(), Requirement.RunMinimum,
						Requirement.RunMaximum);
					return false;
				}
			}
		}

		TArray<TArray<int32>> Adjacency;
		Adjacency.SetNum(Plan.Members.Num());
		auto AddEdge = [&Adjacency](const int32 A, const int32 B)
		{
			if (A != B && Adjacency.IsValidIndex(A) && Adjacency.IsValidIndex(B))
			{
				Adjacency[A].AddUnique(B);
				Adjacency[B].AddUnique(A);
			}
		};
		for (int32 UpperIndex = 0; UpperIndex < Plan.Members.Num(); ++UpperIndex)
		{
			for (const int32 LowerIndex :
				Plan.Members[UpperIndex].RequiredLowerMemberIndices)
			{
				AddEdge(UpperIndex, LowerIndex);
			}
		}
		for (int32 A = 0; A < Plan.Members.Num(); ++A)
		{
			const ABTSM73BeamC3V3::FPlannedMember& MemberA = Plan.Members[A];
			if (MemberA.Axis == EABTSM73BeamAFrameAxis::Z)
			{
				continue;
			}
			const FBox BoundsA = SkeletonV3TestMemberBounds(MemberA);
			const int32 AxisIndex = static_cast<int32>(MemberA.Axis);
			const int32 CrossAxisIndex = AxisIndex == 0 ? 1 : 0;
			for (int32 B = A + 1; B < Plan.Members.Num(); ++B)
			{
				const ABTSM73BeamC3V3::FPlannedMember& MemberB = Plan.Members[B];
				if (MemberB.Axis != MemberA.Axis
					|| MemberB.CourseIndex != MemberA.CourseIndex)
				{
					continue;
				}
				const FBox BoundsB = SkeletonV3TestMemberBounds(MemberB);
				if (FMath::Abs(BoundsA.GetCenter()[CrossAxisIndex]
						- BoundsB.GetCenter()[CrossAxisIndex])
						<= SkeletonV3TestGeometryToleranceCM
					&& FMath::Abs(BoundsA.GetCenter().Z - BoundsB.GetCenter().Z)
						<= SkeletonV3TestGeometryToleranceCM
					&& (FMath::Abs(BoundsA.Max[AxisIndex] - BoundsB.Min[AxisIndex])
							<= SkeletonV3TestGeometryToleranceCM
						|| FMath::Abs(BoundsB.Max[AxisIndex] - BoundsA.Min[AxisIndex])
							<= SkeletonV3TestGeometryToleranceCM))
				{
					AddEdge(A, B);
				}
			}
		}

		int32 StartMember = INDEX_NONE;
		TArray<int32> RequiredConnectedMembers;
		for (const ABTSM73BeamC3V3::FCoreCellPlan& Core : Plan.CoreCells)
		{
			for (const int32 MemberIndex : Core.MemberIndices)
			{
				if (Plan.Members.IsValidIndex(MemberIndex))
				{
					RequiredConnectedMembers.AddUnique(MemberIndex);
					StartMember = StartMember == INDEX_NONE ? MemberIndex : StartMember;
				}
			}
		}
		for (const int32 MemberIndex : Group.MemberIndices)
		{
			if (Plan.Members.IsValidIndex(MemberIndex)
				&& Plan.Members[MemberIndex].FaceMask != 0)
			{
				RequiredConnectedMembers.AddUnique(MemberIndex);
			}
		}
		if (StartMember == INDEX_NONE || RequiredConnectedMembers.IsEmpty())
		{
			OutError = TEXT("BeamC3V3TestCommonEnvelopeConnectivityFixtureEmpty");
			return false;
		}
		TArray<bool> Reached;
		Reached.Init(false, Plan.Members.Num());
		TArray<int32> Queue = {StartMember};
		Reached[StartMember] = true;
		for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
		{
			for (const int32 Neighbour : Adjacency[Queue[QueueIndex]])
			{
				if (!Reached[Neighbour])
				{
					Reached[Neighbour] = true;
					Queue.Add(Neighbour);
				}
			}
		}
		for (const int32 MemberIndex : RequiredConnectedMembers)
		{
			if (!Reached.IsValidIndex(MemberIndex) || !Reached[MemberIndex])
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3TestCommonEnvelopeDisconnected:Member=%d"),
					MemberIndex);
				return false;
			}
		}
		return true;
	}

	FABTSM73BeamD1Settings MakeD1Settings(const FStage1Case& Case)
	{
		FABTSM73BeamD1Settings Settings;
		Settings.GameplayProfileId = Case.ProfileId;
		Settings.DifficultyTier = Case.Tier;
		Settings.BuildingSeed = Case.Seed;
		return Settings;
	}

	bool CheckStage1Result(
		FAutomationTestBase& Test,
		const FStage1Case& Case,
		const FABTSM73BeamD1GenerationResult& Result)
	{
		const FString Prefix = FString::Printf(
			TEXT("%s.E%d"), *Case.ProfileId.ToString(), Case.Tier + 1);
		bool bPassed = true;
		auto Check = [&Test, &Prefix, &bPassed](
			const TCHAR* Label, const bool bCondition)
		{
			bPassed &= Test.TestTrue(
				*FString::Printf(TEXT("%s %s"), *Prefix, Label), bCondition);
		};
		Check(TEXT("is accepted"), Result.Summary.bAccepted);
		Check(TEXT("has the V3 certificate"),
			Result.Summary.bSkeletonFirstCertified);
		Check(TEXT("does not claim physical stability"),
			!Result.Summary.bPhysicalStabilityEvaluated);
		Check(TEXT("executes the structural candidate exactly once"),
			Result.Summary.SkeletonFirstStructuralAttemptCount == 1);
		Check(TEXT("publishes at least one explicit compact core"),
			Result.Summary.SkeletonFirstExplicitCoreCellCount > 0);
		Check(TEXT("grounds every explicit core"),
			Result.Summary.SkeletonFirstGroundedCoreCellCount
				== Result.Summary.SkeletonFirstExplicitCoreCellCount);
		Check(TEXT("has no suspended core"),
			Result.Summary.SkeletonFirstSuspendedCoreCount == 0);
		Check(TEXT("derives every shell member from an explicit core"),
			Result.Summary.SkeletonFirstShellMemberCount > 0
			&& Result.Summary.SkeletonFirstCoreDerivedShellMemberCount
				== Result.Summary.SkeletonFirstShellMemberCount);
		Check(TEXT("has no shared-course non-core endpoint"),
			Result.Summary.SkeletonFirstSharedCourseNonCoreEndpointViolationCount
				== 0);
		Check(TEXT("uses one common building group"),
			Result.Summary.SkeletonFirstBuildingGroupCount == 1);
		Check(TEXT("emits a non-empty common outer frame"),
			Result.Summary.SkeletonFirstCommonShellMemberCount > 0);
		Check(TEXT("connects every explicit core to the common outer frame"),
			Result.Summary.SkeletonFirstCommonShellConnectedCoreCount
				== Result.Summary.SkeletonFirstExplicitCoreCellCount);
		Check(TEXT("has no shared-course band violation"),
			Result.Summary.SkeletonFirstSharedCourseBandViolationCount == 0);
		Check(TEXT("replaces one endpoint slot per core for every shared rail"),
			Result.Summary.SkeletonFirstSharedCourseReplacementSlotCount
				== Result.Summary.SkeletonFirstSharedCourseCount * 2);
		Check(TEXT("grounds all four shell faces"),
			(Result.Summary.SkeletonFirstGroundedFaceMask
				& ABTSM73BeamC3V3::AllFaces)
				== ABTSM73BeamC3V3::AllFaces);
		Check(TEXT("has two distinct grounded exterior Z-post stations per common-frame face"),
			Result.Summary.SkeletonFirstMinimumExteriorPostStationsPerFace >= 2);
		Check(TEXT("keeps every member at or below 720 cm"),
			Result.Summary.SkeletonFirstMaximumMemberSpanCM <= 720.01f);
		Check(TEXT("keeps every post segment at or below 720 cm"),
			Result.Summary.SkeletonFirstMaximumPostSegmentSpanCM
				<= 720.01f);
		Check(TEXT("emits the exact planned member count"),
			Result.Summary.SkeletonFirstPlannedMemberCount
				== Result.Summary.SkeletonFirstEmittedMemberCount
			&& Result.Summary.SkeletonFirstEmittedMemberCount
				== Result.Summary.MemberCount);
		Check(TEXT("lands inside the registered Brick window"),
			Result.Summary.BrickCount
				>= Result.Summary.TargetMinimumBrickCount
			&& Result.Summary.BrickCount
				<= Result.Summary.TargetMaximumBrickCount);
		Check(TEXT("has no structural-closure pass"),
			Result.Summary.StructuralClosurePassCount == 0);
		Check(TEXT("has no rescue post"),
			Result.Summary.AddedStructuralSupportPostCount == 0);
		Check(TEXT("has no support advisory"),
			Result.Summary.SupportResultantAdvisoryCount == 0);
		Check(TEXT("has no blocking support violation"),
			Result.Summary.RemainingSupportViolationCount == 0);
		Check(TEXT("has no real-contact mismatch"),
			Result.Summary.RealContactMismatchCount == 0);
		Check(TEXT("has no strict Brick penetration"),
			Result.Summary.StrictPenetrationCount == 0);
		Check(TEXT("never selects a BridgeRail as the weakness"),
			Result.Summary.WeaknessCandidateMemberRole
				!= EABTSM73BeamAMemberRole::BridgeRail);
		Check(TEXT("publishes all V3 identities"),
			Result.Summary.SkeletonFirstEnvelopeHash != 0
			&& Result.Summary.SkeletonFirstCorePlanHash != 0
			&& Result.Summary.SkeletonFirstSupportPlanHash != 0
			&& Result.Summary.SkeletonFirstFinalGeometryHash != 0
			&& Result.Summary.BrickGeometryHash != 0
			&& Result.Summary.CoupledExteriorFrameDAGEvidenceHash != 0);
		if (Case.ProfileId == TEXT("SeamRelease") && Case.Tier == 5)
		{
			Check(TEXT("consumes the required SupportedSpan as a planned shared course"),
				Result.Summary.SupportedSpanCount > 0
				&& Result.Summary.SkeletonFirstSharedCourseCount > 0);
		}
		return bPassed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3DemoSixBuildingManifestTest,
	"ABTS.M73DAG.BeamC3V3.Demo.SixBuildingManifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3DemoSixBuildingManifestTest::RunTest(const FString& Parameters)
{
	const TArray<FABTSM73BeamDemoManifestEntry>& Entries =
		FABTSM73BeamDemoManifest::GetEntries();
	TestEqual(TEXT("The frozen demonstration contains exactly six buildings"),
		Entries.Num(), 6);
	TestEqual(TEXT("The jury-demo manifest contract is version one"),
		FABTSM73BeamDemoManifest::Version, 1);
	TestNotEqual(TEXT("The jury-demo manifest has a deterministic identity"),
		FABTSM73BeamDemoManifest::CalculateHash(), int64(0));

	TSet<EABTSM73BeamDemoBuilding> EntryIds;
	TSet<FName> StableIds;
	TSet<FName> ProfileIds;
	TSet<int32> Tiers;
	for (const FABTSM73BeamDemoManifestEntry& Entry : Entries)
	{
		EntryIds.Add(Entry.Id);
		StableIds.Add(Entry.StableId);
		ProfileIds.Add(Entry.Settings.GameplayProfileId);
		Tiers.Add(Entry.Settings.DifficultyTier);
		TestTrue(*FString::Printf(TEXT("%s has a positive frozen seed"),
			*Entry.StableId.ToString()), Entry.Settings.BuildingSeed > 0);
		FABTSM73BeamDemoManifestEntry Resolved;
		FString Error;
		TestTrue(*FString::Printf(TEXT("%s resolves: %s"),
			*Entry.StableId.ToString(), *Error),
			FABTSM73BeamDemoManifest::Resolve(Entry.Id, Resolved, Error));
		TestEqual(TEXT("Resolved stable identity is exact"),
			Resolved.StableId, Entry.StableId);
	}
	TestEqual(TEXT("All six enum entries are unique"), EntryIds.Num(), 6);
	TestEqual(TEXT("All six stable identities are unique"), StableIds.Num(), 6);
	TestEqual(TEXT("Every E1-E6 difficulty is represented once"), Tiers.Num(), 6);
	TestEqual(TEXT("All five gameplay profiles are represented"), ProfileIds.Num(), 5);
	for (int32 Tier = 0; Tier <= 5; ++Tier)
	{
		TestTrue(*FString::Printf(TEXT("E%d is present"), Tier + 1),
			Tiers.Contains(Tier));
	}
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FABTSM73BeamC3DemoSixBuildingStage3Test,
	"ABTS.M73DAG.BeamC3V3.Demo.Stage3FrozenEntries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FABTSM73BeamC3DemoSixBuildingStage3Test::GetTests(
	TArray<FString>& OutBeautifiedNames,
	TArray<FString>& OutTestCommands) const
{
	for (const FABTSM73BeamDemoManifestEntry& Entry
		: FABTSM73BeamDemoManifest::GetEntries())
	{
		OutBeautifiedNames.Add(Entry.StableId.ToString());
		OutTestCommands.Add(FString::FromInt(static_cast<int32>(Entry.Id)));
	}
}

bool FABTSM73BeamC3DemoSixBuildingStage3Test::RunTest(const FString& Parameters)
{
	const EABTSM73BeamDemoBuilding Id =
		static_cast<EABTSM73BeamDemoBuilding>(FCString::Atoi(*Parameters));
	FABTSM73BeamDemoManifestEntry Entry;
	FString Error;
	if (!FABTSM73BeamDemoManifest::Resolve(Id, Entry, Error))
	{
		AddError(Error);
		return false;
	}
	FABTSM73BeamD1StagePreviewResult Result;
	const bool bGenerated = FABTSM73BeamD1BrickCompiler().GenerateStagePreview(
		Entry.Settings, EABTSM73BeamC3GenerationStage::CommonExteriorFrame,
		Result, Error);
	TestTrue(*FString::Printf(TEXT("%s Stage 3 generates: %s"),
		*Entry.StableId.ToString(), *Error), bGenerated);
	if (!bGenerated)
	{
		return false;
	}
	const ABTSM73BeamC3V3::FPlanSummary& Summary = Result.Skeleton.Plan.Summary;
	AddInfo(FString::Printf(
		TEXT("BeamDemo Entry=%s ManifestVersion=%d ManifestHash=%lld")
		TEXT(" Profile=%s Tier=E%d Seed=%d WFC=%lld Stage3=%lld Members=%d")
		TEXT(" Frames=%d Columns=%d GroundSills=%d Physical=NotEvaluated"),
		*Entry.StableId.ToString(), FABTSM73BeamDemoManifest::Version,
		FABTSM73BeamDemoManifest::CalculateHash(),
		*Entry.Settings.GameplayProfileId.ToString(),
		Entry.Settings.DifficultyTier + 1, Entry.Settings.BuildingSeed,
		Result.Skeleton.Plan.WFCHash, Summary.Stage3PlanHash,
		Summary.EmittedMemberCount, Summary.CommonExteriorFrameCount,
		Summary.ExteriorColumnCount + Summary.GroundExteriorColumnCount,
		Summary.GroundSillSegmentCount));
	TestTrue(TEXT("Frozen entry passes the Stage 3 static DAG"),
		Result.Summary.bStageStaticDAGEvaluated);
	TestEqual(TEXT("Frozen entry has no positive-volume penetration"),
		Summary.PenetrationCount, 0);
	TestEqual(TEXT("Every Stage 2 anchor resolves a Stage 3 frame"),
		Summary.Stage3AnchorBandWithoutFrameCount, 0);
	TestEqual(TEXT("Every Stage 3 frame has a downward path"),
		Summary.Stage3FrameDownwardConnectionViolationCount, 0);
	TestNotEqual(TEXT("Frozen Stage 3 identity is non-zero"),
		Summary.Stage3PlanHash, int64(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V3PlanContractTest,
	"ABTS.M73DAG.BeamC3V3.Stage1.G0.PlanContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V3PlanContractTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	const FStage1Case Case{TEXT("DropTrigger"), 2, 740000};
	FABTSM73BeamD0ResolvedProfile Profile;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("Fixture resolves: %s"), *Error),
		ResolveShape(Case, Profile, Silhouette, Error)))
	{
		return false;
	}
	ABTSM73BeamC3V3::FPlan Plan;
	const bool bPlanned = FABTSM73BeamC3V3SkeletonFirstGenerator().BuildPlan(
		Profile, Silhouette, Plan, Error);
	TestTrue(*FString::Printf(TEXT("V3 plan builds: %s"), *Error), bPlanned);
	if (!bPlanned)
	{
		return false;
	}
	TestTrue(TEXT("Plan is accepted"), Plan.Summary.bAccepted);
	TestFalse(TEXT("Plan does not evaluate Chaos"),
		Plan.Summary.bPhysicalStabilityEvaluated);
	TestTrue(TEXT("Plan has grounded components"),
		Plan.Summary.GroundedComponentCount > 0);
	TestTrue(TEXT("Plan has explicit compact cores"),
		Plan.Summary.ExplicitCoreCellCount > 0);
	TestEqual(TEXT("Every explicit core has a published plan"),
		Plan.Summary.ExplicitCoreCellCount, Plan.CoreCells.Num());
	TestEqual(TEXT("Every explicit core is grounded"),
		Plan.Summary.GroundedCoreCellCount,
		Plan.Summary.ExplicitCoreCellCount);
	TestEqual(TEXT("No explicit core is suspended"),
		Plan.Summary.SuspendedCoreCount, 0);
	TestTrue(TEXT("Plan has a core-derived shell"),
		Plan.Summary.ShellMemberCount > 0);
	TestEqual(TEXT("Every shell member is derived from an explicit core"),
		Plan.Summary.CoreDerivedShellMemberCount,
		Plan.Summary.ShellMemberCount);
	TestEqual(TEXT("No shared course endpoint is non-core"),
		Plan.Summary.SharedCourseNonCoreEndpointViolationCount, 0);
	TestTrue(TEXT("Plan has exact members"),
		Plan.Summary.PlannedMemberCount == Plan.Members.Num());
	TestTrue(TEXT("Plan lands inside its resolved window"),
		Plan.Summary.PlannedMemberCount >= Plan.Summary.MinimumBrickCount
		&& Plan.Summary.PlannedMemberCount <= Plan.Summary.MaximumBrickCount);
	TestTrue(TEXT("Plan grounds all four faces"),
		(Plan.Summary.GroundedFaceMask & ABTSM73BeamC3V3::AllFaces)
			== ABTSM73BeamC3V3::AllFaces);
	TestEqual(TEXT("The candidate has one common building group"),
		Plan.BuildingGroups.Num(), 1);
	TestEqual(TEXT("The summary publishes the exact common building-group count"),
		Plan.Summary.BuildingGroupCount, Plan.BuildingGroups.Num());
	TestTrue(TEXT("The common building group owns a non-empty outer frame"),
		Plan.Summary.CommonShellMemberCount > 0);
	TestEqual(TEXT("The common outer frame connects every explicit core"),
		Plan.Summary.CommonShellConnectedCoreCount, Plan.CoreCells.Num());
	TestTrue(TEXT("The common frame has two distinct grounded exterior Z-post stations per face"),
		Plan.Summary.MinimumGroundedExteriorPostStationsPerFace >= 2);
	for (const ABTSM73BeamC3V3::FBuildingGroupPlan& Group : Plan.BuildingGroups)
	{
		TestEqual(*FString::Printf(TEXT("Building group %d contains every component"),
			Group.GroupId), Group.ComponentIds.Num(), Plan.Components.Num());
		TestEqual(*FString::Printf(TEXT("Building group %d contains every core"),
			Group.GroupId), Group.CoreCellIds.Num(), Plan.CoreCells.Num());
		TestEqual(*FString::Printf(TEXT("Building group %d publishes four face-post counts"),
			Group.GroupId), Group.GroundedExteriorPostStationCounts.Num(), 4);
		for (int32 FaceIndex = 0;
			FaceIndex < Group.GroundedExteriorPostStationCounts.Num(); ++FaceIndex)
		{
			TestTrue(*FString::Printf(TEXT("Building group %d face %d has distinct grounded posts"),
				Group.GroupId, FaceIndex),
				Group.GroundedExteriorPostStationCounts[FaceIndex] >= 2);
		}
	}
	ABTSM73BeamC3V3::FPlan MissingFacePosts = Plan;
	for (ABTSM73BeamC3V3::FPlannedMember& Member : MissingFacePosts.Members)
	{
		if (Member.OwnerKind == ABTSM73BeamC3V3::EOwnerKind::BuildingGroupShell
			&& Member.Axis == EABTSM73BeamAFrameAxis::Z)
		{
			Member.FaceMask &= ~ABTSM73BeamC3V3::NegativeX;
		}
	}
	TestFalse(TEXT("Clearing one common-frame face's Z-post stations fails closed"),
		FABTSM73BeamC3V3SkeletonFirstGenerator().ValidateExteriorPostStationsForTesting(
			MissingFacePosts, Error));
	TestTrue(*FString::Printf(TEXT("Missing exterior post rejection is explicit: %s"), *Error),
		Error.Contains(TEXT("BeamC3V3BuildingGroupExteriorPostStationsInsufficient")));
	TestTrue(TEXT("Plan member span is bounded"),
		Plan.Summary.MaximumMemberLengthCM <= 720.01f);
	TestTrue(TEXT("Plan post span is bounded"),
		Plan.Summary.MaximumPostSegmentSpanCM <= 720.01f);
	TestTrue(TEXT("Plan identities are non-zero"),
		Plan.Summary.EnvelopeHash != 0
		&& Plan.Summary.CorePlanHash != 0
		&& Plan.Summary.SupportPlanHash != 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V3GroundedSkeletonTopologyContractTest,
	"ABTS.M73DAG.BeamC3V3.Stage1.G0.AntiFake.GroundedSkeletonTopologyContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V3GroundedSkeletonTopologyContractTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	const FStage1Case CoreCase{TEXT("DropTrigger"), 2, 740000};
	FABTSM73BeamD0ResolvedProfile CoreProfile;
	FABTSM73DAG5BV2GenerationResult CoreSilhouette;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("Core fixture resolves: %s"), *Error),
		ResolveShape(CoreCase, CoreProfile, CoreSilhouette, Error)))
	{
		return false;
	}

	FABTSM73BeamC3V3SkeletonFirstGenerator Generator;
	ABTSM73BeamC3V3::FPlan Baseline;
	if (!TestTrue(*FString::Printf(TEXT("Core fixture builds: %s"), *Error),
		Generator.BuildPlan(CoreProfile, CoreSilhouette, Baseline, Error)))
	{
		return false;
	}
	ABTSM73BeamC3V3::FPlan Revalidated = Baseline;
	TestTrue(*FString::Printf(TEXT("Grounded core-to-shell topology revalidates: %s"),
		*Error), Generator.ValidateSkeletonTopologyForTesting(
			CoreSilhouette, Revalidated, Error));
	TestTrue(TEXT("Positive fixture publishes explicit cores"),
		Baseline.Summary.ExplicitCoreCellCount > 0);
	TestEqual(TEXT("Positive fixture grounds every explicit core"),
		Baseline.Summary.GroundedCoreCellCount,
		Baseline.Summary.ExplicitCoreCellCount);
	TestEqual(TEXT("Positive fixture has no suspended core"),
		Baseline.Summary.SuspendedCoreCount, 0);
	TestTrue(TEXT("Positive fixture publishes a shell"),
		Baseline.Summary.ShellMemberCount > 0);
	TestEqual(TEXT("Positive fixture derives the complete shell from cores"),
		Baseline.Summary.CoreDerivedShellMemberCount,
		Baseline.Summary.ShellMemberCount);
	TestTrue(TEXT("Every adjacent core course has a full 36x36 bearing face"),
		SkeletonV3TestHasFullCoreBearingFaces(*this, Baseline));

	int32 HalfBearingMemberIndex = INDEX_NONE;
	int32 HalfBearingAxisIndex = INDEX_NONE;
	double HalfBearingRetreatCM = 0.0;
	for (const ABTSM73BeamC3V3::FCoreCellPlan& Core : Baseline.CoreCells)
	{
		const TArray<int32> LowerMembers =
			SkeletonV3TestCoreCourseMembers(Baseline, Core, 0);
		const TArray<int32> UpperMembers =
			SkeletonV3TestCoreCourseMembers(Baseline, Core, 1);
		for (const int32 LowerIndex : LowerMembers)
		{
			if (!Baseline.Members.IsValidIndex(LowerIndex)
				|| Baseline.Members[LowerIndex].Axis
					!= EABTSM73BeamAFrameAxis::X)
			{
				continue;
			}
			for (const int32 UpperIndex : UpperMembers)
			{
				if (!Baseline.Members.IsValidIndex(UpperIndex)
					|| Baseline.Members[UpperIndex].Axis
						!= EABTSM73BeamAFrameAxis::Y)
				{
					continue;
				}
				const FBox UpperBounds =
					SkeletonV3TestMemberBounds(Baseline.Members[UpperIndex]);
				if (FMath::Abs(UpperBounds.GetCenter().X - Core.LocalBounds.Min.X)
					<= SkeletonV3TestGeometryToleranceCM)
				{
					HalfBearingMemberIndex = LowerIndex;
					HalfBearingAxisIndex = 0;
					HalfBearingRetreatCM = Core.LocalBounds.Min.X;
					break;
				}
			}
			if (HalfBearingMemberIndex != INDEX_NONE)
			{
				break;
			}
		}
		if (HalfBearingMemberIndex != INDEX_NONE)
		{
			break;
		}
	}
	if (!TestTrue(TEXT("Fixture contains a full-face crossing for the old half-grid mutation"),
		Baseline.Members.IsValidIndex(HalfBearingMemberIndex)))
	{
		return false;
	}
	ABTSM73BeamC3V3::FPlan HalfGridCore = Baseline;
	ABTSM73BeamC3V3::FPlannedMember& RetreatedRail =
		HalfGridCore.Members[HalfBearingMemberIndex];
	if (RetreatedRail.LocalStart[HalfBearingAxisIndex]
		<= RetreatedRail.LocalEnd[HalfBearingAxisIndex])
	{
		RetreatedRail.LocalStart[HalfBearingAxisIndex] = HalfBearingRetreatCM;
	}
	else
	{
		RetreatedRail.LocalEnd[HalfBearingAxisIndex] = HalfBearingRetreatCM;
	}
	Error.Reset();
	TestFalse(TEXT("Retreating a core rail by half a block fails the full-face bearing contract"),
		Generator.ValidateSkeletonTopologyForTesting(
			CoreSilhouette, HalfGridCore, Error));
	TestTrue(*FString::Printf(TEXT("Half-grid bearing rejection is explicit: %s"), *Error),
		Error.Contains(TEXT("BeamC3V3CoreBearingAreaInsufficient")));

	const int32 GroundCoreIndex = Baseline.Members.IndexOfByPredicate(
		[](const ABTSM73BeamC3V3::FPlannedMember& Member)
		{
			return Member.SkeletonKind
				== ABTSM73BeamC3V3::ESkeletonMemberKind::CoreCourse
				&& Member.bRequiresGroundSeat;
		});
	if (!TestTrue(TEXT("Fixture contains a ground-seated CoreCourse"),
		Baseline.Members.IsValidIndex(GroundCoreIndex)))
	{
		return false;
	}
	ABTSM73BeamC3V3::FPlan LiftedCore = Baseline;
	LiftedCore.Members[GroundCoreIndex].LocalStart.Z += 36.0;
	LiftedCore.Members[GroundCoreIndex].LocalEnd.Z += 36.0;
	Error.Reset();
	TestFalse(TEXT("Lifting a declared ground CoreCourse fails closed"),
		Generator.ValidateSkeletonTopologyForTesting(
			CoreSilhouette, LiftedCore, Error));
	TestTrue(*FString::Printf(TEXT("Lifted-core rejection is explicit: %s"), *Error),
		Error.Contains(TEXT("BeamC3V3SuspendedCore")));

	const int32 ShellIndex = Baseline.Members.IndexOfByPredicate(
		[](const ABTSM73BeamC3V3::FPlannedMember& Member)
		{
			const bool bShellKind = Member.SkeletonKind
				== ABTSM73BeamC3V3::ESkeletonMemberKind::ThroughCourse
				|| Member.SkeletonKind
					== ABTSM73BeamC3V3::ESkeletonMemberKind::FacadeCourse
				|| Member.SkeletonKind
					== ABTSM73BeamC3V3::ESkeletonMemberKind::ExteriorPost;
			return bShellKind && Member.OriginCoreCellId != INDEX_NONE
				&& !Member.RequiredInwardMemberIndices.IsEmpty();
		});
	if (!TestTrue(TEXT("Fixture contains a core-derived shell member"),
		Baseline.Members.IsValidIndex(ShellIndex)))
	{
		return false;
	}
	ABTSM73BeamC3V3::FPlan OrphanedShell = Baseline;
	OrphanedShell.Members[ShellIndex].OriginCoreCellId = INDEX_NONE;
	OrphanedShell.Members[ShellIndex].RequiredInwardMemberIndices.Reset();
	Error.Reset();
	TestFalse(TEXT("A shell member without its core origin and inward edge fails closed"),
		Generator.ValidateSkeletonTopologyForTesting(
			CoreSilhouette, OrphanedShell, Error));
	TestTrue(*FString::Printf(TEXT("Orphan-shell rejection is explicit: %s"), *Error),
		Error.Contains(TEXT("BeamC3V3ShellOriginMissing")));

	const FABTSM73DAG5BV2Volume* CrownVolume =
		CoreSilhouette.Volumes.FindByPredicate(
			[](const FABTSM73DAG5BV2Volume& Volume)
			{
				return Volume.Role == EABTSM73DAG5BV2VolumeRole::Crown;
			});
	if (!TestNotNull(TEXT("Fixture contains a Crown volume"), CrownVolume))
	{
		return false;
	}
	ABTSM73BeamC3V3::FPlan CrownPseudoCore = Baseline;
	CrownPseudoCore.Members[GroundCoreIndex].SourceVolumeId = CrownVolume->VolumeId;
	Error.Reset();
	TestFalse(TEXT("A Crown source cannot masquerade as a CoreCourse"),
		Generator.ValidateSkeletonTopologyForTesting(
			CoreSilhouette, CrownPseudoCore, Error));
	TestTrue(*FString::Printf(TEXT("Crown-pseudo-core rejection is explicit: %s"),
		*Error), Error.Contains(TEXT("BeamC3V3CoreSourceContractInvalid")));

	int32 TaperedRoofIndex = INDEX_NONE;
	const FABTSM73DAG5BV2Volume* TaperedRoofSource = nullptr;
	for (int32 MemberIndex = 0; MemberIndex < Baseline.Members.Num(); ++MemberIndex)
	{
		const ABTSM73BeamC3V3::FPlannedMember& Member = Baseline.Members[MemberIndex];
		if (Member.SkeletonKind != ABTSM73BeamC3V3::ESkeletonMemberKind::RoofCourse)
		{
			continue;
		}
		const FABTSM73DAG5BV2Volume* Source = CoreSilhouette.Volumes.FindByPredicate(
			[&Member](const FABTSM73DAG5BV2Volume& Volume)
			{
				return Volume.VolumeId == Member.SourceVolumeId;
			});
		if (Source == nullptr || Source->Primitive == EABTSM73DAG5BV2Primitive::Box)
		{
			continue;
		}
		if (TaperedRoofIndex == INDEX_NONE
			|| Member.CourseIndex > Baseline.Members[TaperedRoofIndex].CourseIndex)
		{
			TaperedRoofIndex = MemberIndex;
			TaperedRoofSource = Source;
		}
	}
	if (!TestTrue(TEXT("Fixture contains a tapered semantic RoofCourse"),
		Baseline.Members.IsValidIndex(TaperedRoofIndex)
			&& TaperedRoofSource != nullptr))
	{
		return false;
	}
	ABTSM73BeamC3V3::FPlan FixedFootprintRoof = Baseline;
	ABTSM73BeamC3V3::FPlannedMember& Untapered =
		FixedFootprintRoof.Members[TaperedRoofIndex];
	if (Untapered.Axis == EABTSM73BeamAFrameAxis::X)
	{
		Untapered.LocalStart.X = TaperedRoofSource->LocalBounds.Min.X;
		Untapered.LocalEnd.X = TaperedRoofSource->LocalBounds.Max.X;
		Untapered.LocalStart.Y = Untapered.LocalEnd.Y =
			TaperedRoofSource->LocalBounds.Min.Y + 18.0;
	}
	else
	{
		Untapered.LocalStart.Y = TaperedRoofSource->LocalBounds.Min.Y;
		Untapered.LocalEnd.Y = TaperedRoofSource->LocalBounds.Max.Y;
		Untapered.LocalStart.X = Untapered.LocalEnd.X =
			TaperedRoofSource->LocalBounds.Min.X + 18.0;
	}
	Error.Reset();
	TestFalse(TEXT("Restoring a high Crown course to a fixed full footprint fails closed"),
		Generator.ValidateSkeletonTopologyForTesting(
			CoreSilhouette, FixedFootprintRoof, Error));
	TestTrue(*FString::Printf(TEXT("Untapered-roof rejection is explicit: %s"),
		*Error), Error.Contains(TEXT("BeamC3V3RoofTaperEnvelopeViolation")));

	const FStage1Case SharedCase{TEXT("SeamRelease"), 5, 720000};
	FABTSM73BeamD0ResolvedProfile SharedProfile;
	FABTSM73DAG5BV2GenerationResult SharedSilhouette;
	Error.Reset();
	if (!TestTrue(*FString::Printf(TEXT("Shared-course fixture resolves: %s"),
		*Error), ResolveShape(SharedCase, SharedProfile, SharedSilhouette, Error)))
	{
		return false;
	}
	ABTSM73BeamC3V3::FPlan SharedBaseline;
	if (!TestTrue(*FString::Printf(TEXT("Shared-course fixture builds: %s"), *Error),
		Generator.BuildPlan(SharedProfile, SharedSilhouette, SharedBaseline, Error)))
	{
		return false;
	}
	TestTrue(TEXT("Shared fixture publishes a shared course"),
		SharedBaseline.Summary.SharedCourseCount > 0);
	TestEqual(TEXT("Shared fixture has no non-core endpoint"),
		SharedBaseline.Summary.SharedCourseNonCoreEndpointViolationCount, 0);
	const int32 SharedIndex = SharedBaseline.Members.IndexOfByPredicate(
		[](const ABTSM73BeamC3V3::FPlannedMember& Member)
		{
			return Member.SkeletonKind
				== ABTSM73BeamC3V3::ESkeletonMemberKind::SharedCourse;
		});
	if (!TestTrue(TEXT("Fixture contains a SharedCourse member"),
		SharedBaseline.Members.IsValidIndex(SharedIndex)
		&& !SharedBaseline.Members[SharedIndex].RequiredLowerMemberIndices.IsEmpty()))
	{
		return false;
	}

	TArray<int32> SharedMemberIndices;
	TMap<uint64, TArray<int32>> SharedBands;
	TMap<int32, int32> DiaphragmCountBySpan;
	for (int32 MemberIndex = 0; MemberIndex < SharedBaseline.Members.Num(); ++MemberIndex)
	{
		const ABTSM73BeamC3V3::FPlannedMember& Member =
			SharedBaseline.Members[MemberIndex];
		if (Member.SkeletonKind == ABTSM73BeamC3V3::ESkeletonMemberKind::SharedCourse)
		{
			SharedMemberIndices.Add(MemberIndex);
			const uint64 BandKey =
				(static_cast<uint64>(static_cast<uint32>(Member.OwnerId)) << 32)
				| static_cast<uint32>(Member.CourseIndex);
			SharedBands.FindOrAdd(BandKey).Add(MemberIndex);
		}
		else if (Member.SkeletonKind
			== ABTSM73BeamC3V3::ESkeletonMemberKind::BridgeDiaphragm)
		{
			++DiaphragmCountBySpan.FindOrAdd(Member.OwnerId);
		}
	}
	TestEqual(TEXT("Published shared-course count matches canonical members"),
		SharedBaseline.Summary.SharedCourseCount, SharedMemberIndices.Num());
	TestEqual(TEXT("Every shared rail replaces one slot in each endpoint core"),
		SharedBaseline.Summary.SharedCourseReplacementSlotCount,
		SharedMemberIndices.Num() * 2);
	TestEqual(TEXT("No shared rail violates its alternating core band"),
		SharedBaseline.Summary.SharedCourseBandViolationCount, 0);
	TestTrue(TEXT("A shared bridge contains transverse diaphragm members"),
		!DiaphragmCountBySpan.IsEmpty());

	for (const TPair<uint64, TArray<int32>>& BandPair : SharedBands)
	{
		const TArray<int32>& RailIndices = BandPair.Value;
		if (!TestEqual(TEXT("Every shared course band consists of two long rails"),
			RailIndices.Num(), 2))
		{
			continue;
		}
		const ABTSM73BeamC3V3::FPlannedMember& FirstRail =
			SharedBaseline.Members[RailIndices[0]];
		TestTrue(TEXT("Each supported span has a real diaphragm bridge band"),
			DiaphragmCountBySpan.FindRef(FirstRail.OwnerId) > 0);
		for (const int32 RailIndex : RailIndices)
		{
			const ABTSM73BeamC3V3::FPlannedMember& SharedRail =
				SharedBaseline.Members[RailIndex];
			TestEqual(*FString::Printf(TEXT("Shared rail %d names two endpoint cores"),
				RailIndex), SharedRail.EndpointCoreCellIds.Num(), 2);
			if (SharedRail.EndpointCoreCellIds.Num() != 2)
			{
				continue;
			}
			TSet<int32> RailEndpointCores;
			for (const int32 CoreCellId : SharedRail.EndpointCoreCellIds)
			{
				RailEndpointCores.Add(CoreCellId);
			}
			TestTrue(*FString::Printf(
				TEXT("Shared band rail %d uses the same two endpoint cores"), RailIndex),
				RailEndpointCores.Num() == 2
				&& FirstRail.EndpointCoreCellIds.Num() == 2
				&& RailEndpointCores.Contains(FirstRail.EndpointCoreCellIds[0])
				&& RailEndpointCores.Contains(FirstRail.EndpointCoreCellIds[1]));
			const FBox SharedBounds = SkeletonV3TestMemberBounds(SharedRail);
			const int32 AxisIndex = static_cast<int32>(SharedRail.Axis);
			for (const int32 EndpointCoreCellId : SharedRail.EndpointCoreCellIds)
			{
				const ABTSM73BeamC3V3::FCoreCellPlan* EndpointCore =
					SkeletonV3TestFindCore(SharedBaseline, EndpointCoreCellId);
				if (!TestNotNull(*FString::Printf(
					TEXT("Shared rail %d endpoint core %d exists"),
					RailIndex, EndpointCoreCellId), EndpointCore))
				{
					continue;
				}
				int32 SlotReferenceCount = 0;
				for (const int32 SlotMemberIndex : EndpointCore->MemberIndices)
				{
					SlotReferenceCount += SlotMemberIndex == RailIndex ? 1 : 0;
				}
				TestEqual(*FString::Printf(
					TEXT("Shared rail %d replaces exactly one course slot in core %d"),
					RailIndex, EndpointCoreCellId), SlotReferenceCount, 1);
				TestTrue(*FString::Printf(
					TEXT("Shared rail %d fully traverses core %d"),
					RailIndex, EndpointCoreCellId),
					SharedBounds.Min[AxisIndex]
						<= EndpointCore->LocalBounds.Min[AxisIndex]
							- SkeletonV3TestHalfBlockCM
							+ SkeletonV3TestGeometryToleranceCM
					&& SharedBounds.Max[AxisIndex]
						>= EndpointCore->LocalBounds.Max[AxisIndex]
							+ SkeletonV3TestHalfBlockCM
							- SkeletonV3TestGeometryToleranceCM);

				TSet<int32> EndpointLowerRails;
				for (const int32 LowerIndex : SharedRail.RequiredLowerMemberIndices)
				{
					if (SharedBaseline.Members.IsValidIndex(LowerIndex)
						&& EndpointCore->MemberIndices.Contains(LowerIndex)
						&& SharedBaseline.Members[LowerIndex].CourseIndex
							== SharedRail.CourseIndex - 1)
					{
						EndpointLowerRails.Add(LowerIndex);
					}
				}
				TestEqual(*FString::Printf(
					TEXT("Shared rail %d is seated by both lower rails of core %d"),
					RailIndex, EndpointCoreCellId), EndpointLowerRails.Num(), 2);
			}
		}

		for (const int32 EndpointCoreCellId : FirstRail.EndpointCoreCellIds)
		{
			const ABTSM73BeamC3V3::FCoreCellPlan* EndpointCore =
				SkeletonV3TestFindCore(SharedBaseline, EndpointCoreCellId);
			if (EndpointCore == nullptr)
			{
				continue;
			}
			const TArray<int32> SharedCourseSlots =
				SkeletonV3TestCoreCourseMembers(
					SharedBaseline, *EndpointCore, FirstRail.CourseIndex);
			TestEqual(*FString::Printf(
				TEXT("Core %d shared course %d still has exactly two slots"),
				EndpointCoreCellId, FirstRail.CourseIndex),
				SharedCourseSlots.Num(), 2);
			for (const int32 RailIndex : RailIndices)
			{
				TestTrue(*FString::Printf(
					TEXT("Core %d course %d slot is shared rail %d"),
					EndpointCoreCellId, FirstRail.CourseIndex, RailIndex),
					SharedCourseSlots.Contains(RailIndex));
			}
			const TArray<int32> UpperRails = SkeletonV3TestCoreCourseMembers(
				SharedBaseline, *EndpointCore, FirstRail.CourseIndex + 1);
			TestEqual(*FString::Printf(
				TEXT("Core %d contributes both upper clamping rails to shared course %d"),
				EndpointCoreCellId, FirstRail.CourseIndex), UpperRails.Num(), 2);
			for (const int32 UpperIndex : UpperRails)
			{
				if (!SharedBaseline.Members.IsValidIndex(UpperIndex))
				{
					continue;
				}
				for (const int32 RailIndex : RailIndices)
				{
					TestTrue(*FString::Printf(
						TEXT("Core %d upper rail %d clamps shared rail %d"),
						EndpointCoreCellId, UpperIndex, RailIndex),
						SharedBaseline.Members[UpperIndex]
							.RequiredLowerMemberIndices.Contains(RailIndex));
				}
			}
		}
	}

	TestEqual(TEXT("Seam E6 is one building group, not six thin buildings"),
		SharedBaseline.BuildingGroups.Num(), 1);
	if (!SharedBaseline.BuildingGroups.IsEmpty())
	{
		const ABTSM73BeamC3V3::FBuildingGroupPlan& Group =
			SharedBaseline.BuildingGroups[0];
		TSet<int32> GroupComponents;
		TSet<int32> GroupCores;
		for (const int32 ComponentId : Group.ComponentIds)
		{
			GroupComponents.Add(ComponentId);
		}
		for (const int32 CoreCellId : Group.CoreCellIds)
		{
			GroupCores.Add(CoreCellId);
		}
		int32 CommonFrameOwnedMemberCount = 0;
		for (const int32 MemberIndex : Group.MemberIndices)
		{
			if (!SharedBaseline.Members.IsValidIndex(MemberIndex))
			{
				continue;
			}
			const ABTSM73BeamC3V3::FPlannedMember& Member =
				SharedBaseline.Members[MemberIndex];
			if (Member.OwnerKind
				!= ABTSM73BeamC3V3::EOwnerKind::BuildingGroupShell)
			{
				continue;
			}
			++CommonFrameOwnedMemberCount;
			TestEqual(*FString::Printf(
				TEXT("Common-frame member %d belongs to the one building group"),
				MemberIndex), Member.OwnerId, Group.GroupId);
			TestTrue(*FString::Printf(
				TEXT("Common-frame member %d has explicit inward lineage"),
				MemberIndex), !Member.RequiredInwardMemberIndices.IsEmpty());
			TestTrue(*FString::Printf(
				TEXT("Common-frame member %d names a core in its connected group"),
				MemberIndex), GroupCores.Contains(Member.OriginCoreCellId));
		}
		TestEqual(TEXT("The common group contains every semantic component exactly once"),
			GroupComponents.Num(), SharedBaseline.Components.Num());
		for (const ABTSM73BeamC3V3::FComponentPlan& Component :
			SharedBaseline.Components)
		{
			TestTrue(*FString::Printf(TEXT("Common group contains component %d"),
				Component.ComponentId), GroupComponents.Contains(Component.ComponentId));
		}
		TestEqual(TEXT("The common group contains every grounded core exactly once"),
			GroupCores.Num(), SharedBaseline.CoreCells.Num());
		for (const ABTSM73BeamC3V3::FCoreCellPlan& Core : SharedBaseline.CoreCells)
		{
			TestTrue(*FString::Printf(TEXT("Common group contains core %d"),
				Core.CoreCellId), GroupCores.Contains(Core.CoreCellId));
			TestTrue(*FString::Printf(
				TEXT("Common group bounds enclose core %d"), Core.CoreCellId),
				Group.LocalBounds.Min.X <= Core.LocalBounds.Min.X
				&& Group.LocalBounds.Max.X >= Core.LocalBounds.Max.X
				&& Group.LocalBounds.Min.Y <= Core.LocalBounds.Min.Y
				&& Group.LocalBounds.Max.Y >= Core.LocalBounds.Max.Y);
		}
		TestTrue(TEXT("The candidate emits common-frame-owned members"),
			CommonFrameOwnedMemberCount > 0);
		TestEqual(TEXT("The common frame connects the published number of cores"),
			SharedBaseline.Summary.CommonShellConnectedCoreCount,
			SharedBaseline.CoreCells.Num());
		TestEqual(TEXT("The common group publishes four perimeter face counts"),
			Group.GroundedExteriorPostStationCounts.Num(), 4);
		TestEqual(TEXT("The common group grounds all four perimeter faces"),
			Group.GroundedFaceMask & ABTSM73BeamC3V3::AllFaces,
			static_cast<uint8>(ABTSM73BeamC3V3::AllFaces));
		for (int32 FaceIndex = 0;
			FaceIndex < Group.GroundedExteriorPostStationCounts.Num(); ++FaceIndex)
		{
			TestTrue(*FString::Printf(
				TEXT("Common group face %d has at least two grounded post stations"),
				FaceIndex), Group.GroundedExteriorPostStationCounts[FaceIndex] >= 2);
		}

		if (Group.ComponentIds.Num() > 1)
		{
			ABTSM73BeamC3V3::FPlan SplitBuildingGroup = SharedBaseline;
			SplitBuildingGroup.BuildingGroups[0].ComponentIds.Pop();
			Error.Reset();
			TestFalse(TEXT("Dropping one component from the common building group fails closed"),
				Generator.ValidateSkeletonTopologyForTesting(
					SharedSilhouette, SplitBuildingGroup, Error));
			TestTrue(*FString::Printf(
				TEXT("Split building-group rejection is explicit: %s"), *Error),
				Error.Contains(TEXT("BeamC3V3BuildingGroup")));
		}
	}

	const TArray<int32>* FirstSharedBand = nullptr;
	for (const TPair<uint64, TArray<int32>>& Pair : SharedBands)
	{
		if (Pair.Value.Num() == 2)
		{
			FirstSharedBand = &Pair.Value;
			break;
		}
	}
	if (!TestNotNull(TEXT("Fixture contains a two-rail shared band"), FirstSharedBand))
	{
		return false;
	}
	const int32 SharedSlotMutationIndex = (*FirstSharedBand)[0];
	const int32 SiblingSharedRailIndex = (*FirstSharedBand)[1];
	const ABTSM73BeamC3V3::FPlannedMember& SharedSlotMutationMember =
		SharedBaseline.Members[SharedSlotMutationIndex];
	if (!TestTrue(TEXT("Shared slot mutation has two endpoint cores"),
		SharedSlotMutationMember.EndpointCoreCellIds.Num() == 2))
	{
		return false;
	}
	ABTSM73BeamC3V3::FPlan MissingEndpointSlot = SharedBaseline;
	ABTSM73BeamC3V3::FCoreCellPlan* MutatedEndpointCore =
		MissingEndpointSlot.CoreCells.FindByPredicate(
			[&SharedSlotMutationMember](const ABTSM73BeamC3V3::FCoreCellPlan& Core)
			{
				return Core.CoreCellId
					== SharedSlotMutationMember.EndpointCoreCellIds[0];
			});
	if (!TestNotNull(TEXT("Shared slot mutation endpoint exists"), MutatedEndpointCore))
	{
		return false;
	}
	const int32 SlotIndex =
		MutatedEndpointCore->MemberIndices.IndexOfByKey(SharedSlotMutationIndex);
	if (!TestTrue(TEXT("Shared rail really occupies an endpoint core slot"),
		SlotIndex != INDEX_NONE))
	{
		return false;
	}
	MutatedEndpointCore->MemberIndices[SlotIndex] = SiblingSharedRailIndex;
	Error.Reset();
	TestFalse(TEXT("Removing one endpoint's shared-course slot fails closed"),
		Generator.ValidateSkeletonTopologyForTesting(
			SharedSilhouette, MissingEndpointSlot, Error));
	TestTrue(*FString::Printf(TEXT("Missing shared slot rejection is explicit: %s"),
		*Error), Error.Contains(TEXT("BeamC3V3SharedCourse"))
		|| Error.Contains(TEXT("BeamC3V3CoreCourseSequence")));

	ABTSM73BeamC3V3::FPlan ShortSharedRail = SharedBaseline;
	ABTSM73BeamC3V3::FPlannedMember& Shortened =
		ShortSharedRail.Members[SharedSlotMutationIndex];
	const int32 SharedAxisIndex = static_cast<int32>(Shortened.Axis);
	const ABTSM73BeamC3V3::FCoreCellPlan* FarthestEndpointCore = nullptr;
	for (const int32 EndpointCoreCellId : Shortened.EndpointCoreCellIds)
	{
		const ABTSM73BeamC3V3::FCoreCellPlan* EndpointCore =
			SkeletonV3TestFindCore(ShortSharedRail, EndpointCoreCellId);
		if (EndpointCore != nullptr
			&& (FarthestEndpointCore == nullptr
				|| EndpointCore->LocalBounds.GetCenter()[SharedAxisIndex]
					> FarthestEndpointCore->LocalBounds.GetCenter()[SharedAxisIndex]))
		{
			FarthestEndpointCore = EndpointCore;
		}
	}
	if (!TestNotNull(TEXT("Shared traversal mutation endpoint exists"),
		FarthestEndpointCore))
	{
		return false;
	}
	const double ShortenedEnd = FarthestEndpointCore->LocalBounds.Min[SharedAxisIndex];
	if (Shortened.LocalStart[SharedAxisIndex] <= Shortened.LocalEnd[SharedAxisIndex])
	{
		Shortened.LocalEnd[SharedAxisIndex] = ShortenedEnd;
	}
	else
	{
		Shortened.LocalStart[SharedAxisIndex] = ShortenedEnd;
	}
	Error.Reset();
	TestFalse(TEXT("A shared rail that stops at the far core edge fails closed"),
		Generator.ValidateSkeletonTopologyForTesting(
			SharedSilhouette, ShortSharedRail, Error));
	TestTrue(*FString::Printf(TEXT("Short shared traversal rejection is explicit: %s"),
		*Error), Error.Contains(TEXT("BeamC3V3SharedCourse")));

	int32 NonCoreSeatIndex = INDEX_NONE;
	for (int32 MemberIndex = 0; MemberIndex < SharedBaseline.Members.Num(); ++MemberIndex)
	{
		if (MemberIndex != SharedIndex
			&& SharedBaseline.Members[MemberIndex].SkeletonKind
				!= ABTSM73BeamC3V3::ESkeletonMemberKind::CoreCourse)
		{
			NonCoreSeatIndex = MemberIndex;
			break;
		}
	}
	if (!TestTrue(TEXT("Fixture contains a non-core seat mutation target"),
		SharedBaseline.Members.IsValidIndex(NonCoreSeatIndex)))
	{
		return false;
	}
	ABTSM73BeamC3V3::FPlan NonCoreSharedSeat = SharedBaseline;
	NonCoreSharedSeat.Members[SharedIndex].RequiredLowerMemberIndices[0] =
		NonCoreSeatIndex;
	Error.Reset();
	TestFalse(TEXT("A SharedCourse seated on a non-core member fails closed"),
		Generator.ValidateSkeletonTopologyForTesting(
			SharedSilhouette, NonCoreSharedSeat, Error));
	TestTrue(*FString::Printf(TEXT("Non-core shared-seat rejection is explicit: %s"),
		*Error), Error.Contains(TEXT("BeamC3V3SharedCourseLowerCoreSeatMissing")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V3SharedBridgeBandContractTest,
	"ABTS.M73DAG.BeamC3V3.Stage1.G0.AntiFake.SharedBridgeBandContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V3SharedBridgeBandContractTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	const FStage1Case Case{TEXT("SeamRelease"), 5, 720000};
	FABTSM73BeamD0ResolvedProfile Profile;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("Shared bridge fixture resolves: %s"),
		*Error), ResolveShape(Case, Profile, Silhouette, Error)))
	{
		return false;
	}
	ABTSM73BeamC3V3::FPlan Plan;
	if (!TestTrue(*FString::Printf(TEXT("Shared bridge fixture builds: %s"),
		*Error), FABTSM73BeamC3V3SkeletonFirstGenerator().BuildPlan(
			Profile, Silhouette, Plan, Error)))
	{
		return false;
	}
	TestTrue(*FString::Printf(
		TEXT("Every Seam E6 bridge is a coupled C/C+1/C+2 band: %s"), *Error),
		SkeletonV3TestValidateSharedBridgeBands(Plan, Error));

	ABTSM73BeamC3V3::FPlan MissingDiaphragms = Plan;
	TSet<int32> RemovedDiaphragms;
	for (int32 MemberIndex = 0;
		MemberIndex < MissingDiaphragms.Members.Num(); ++MemberIndex)
	{
		if (MissingDiaphragms.Members[MemberIndex].SkeletonKind
			== ABTSM73BeamC3V3::ESkeletonMemberKind::BridgeDiaphragm)
		{
			RemovedDiaphragms.Add(MemberIndex);
		}
	}
	if (!TestTrue(TEXT("Shared bridge fixture has removable diaphragms"),
		RemovedDiaphragms.Num() >= 2))
	{
		return false;
	}
	SkeletonV3TestRemoveMembers(MissingDiaphragms, RemovedDiaphragms);
	Error.Reset();
	TestFalse(TEXT("Deleting the bridge diaphragms fails the band contract"),
		SkeletonV3TestValidateSharedBridgeBands(MissingDiaphragms, Error));
	TestTrue(*FString::Printf(
		TEXT("Missing-diaphragm rejection is explicit: %s"), *Error),
		Error.Contains(TEXT("BeamC3V3TestSharedBridgeDiaphragmCount")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V3CommonEnvelopeContractTest,
	"ABTS.M73DAG.BeamC3V3.Stage1.G0.AntiFake.CommonEnvelopeContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V3CommonEnvelopeContractTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	const FStage1Case Case{TEXT("SeamRelease"), 5, 720000};
	FABTSM73BeamD0ResolvedProfile Profile;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("Common envelope fixture resolves: %s"),
		*Error), ResolveShape(Case, Profile, Silhouette, Error)))
	{
		return false;
	}
	ABTSM73BeamC3V3::FPlan Plan;
	if (!TestTrue(*FString::Printf(TEXT("Common envelope fixture builds: %s"),
		*Error), FABTSM73BeamC3V3SkeletonFirstGenerator().BuildPlan(
			Profile, Silhouette, Plan, Error)))
	{
		return false;
	}
	TestTrue(*FString::Printf(
		TEXT("Seam E6 is one outward common envelope with no local shell remnants: %s"),
		*Error), SkeletonV3TestValidateCommonEnvelope(Plan, Error));

	ABTSM73BeamC3V3::FPlan LegacyLocalShell = Plan;
	const int32 CommonHorizontalIndex = LegacyLocalShell.Members.IndexOfByPredicate(
		[](const ABTSM73BeamC3V3::FPlannedMember& Member)
		{
			return Member.OwnerKind
					== ABTSM73BeamC3V3::EOwnerKind::BuildingGroupShell
				&& Member.Axis != EABTSM73BeamAFrameAxis::Z;
		});
	if (!TestTrue(TEXT("Common envelope fixture has a horizontal group member"),
		LegacyLocalShell.Members.IsValidIndex(CommonHorizontalIndex)))
	{
		return false;
	}
	ABTSM73BeamC3V3::FPlannedMember Injected =
		LegacyLocalShell.Members[CommonHorizontalIndex];
	Injected.OwnerKind = ABTSM73BeamC3V3::EOwnerKind::ShellFace;
	Injected.OwnerId = 0;
	Injected.ComponentId = 0;
	Injected.SourceVolumeId = LegacyLocalShell.Components[0].GroundSourceVolumeIds.IsEmpty()
		? LegacyLocalShell.CoreCells[0].BodySourceVolumeId
		: LegacyLocalShell.Components[0].GroundSourceVolumeIds[0];
	LegacyLocalShell.Members.Add(MoveTemp(Injected));
	Error.Reset();
	TestFalse(TEXT("Injecting one legacy per-component shell fails the common-envelope contract"),
		SkeletonV3TestValidateCommonEnvelope(LegacyLocalShell, Error));
	TestTrue(*FString::Printf(
		TEXT("Legacy-local-shell rejection is explicit: %s"), *Error),
		Error.Contains(TEXT("BeamC3V3TestCommonEnvelopeLegacyLocalShell")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V3DeterminismTest,
	"ABTS.M73DAG.BeamC3V3.Stage1.G0.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V3DeterminismTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	const FStage1Case Case{TEXT("ColumnBreak"), 3, 710000};
	FABTSM73BeamD0ResolvedProfile Profile;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("Fixture resolves: %s"), *Error),
		ResolveShape(Case, Profile, Silhouette, Error)))
	{
		return false;
	}
	ABTSM73BeamC3V3::FPlan A;
	ABTSM73BeamC3V3::FPlan B;
	FABTSM73BeamC3V3SkeletonFirstGenerator Generator;
	const bool bA = Generator.BuildPlan(Profile, Silhouette, A, Error);
	const bool bB = Generator.BuildPlan(Profile, Silhouette, B, Error);
	TestTrue(*FString::Printf(TEXT("First plan builds: %s"), *Error), bA);
	TestTrue(*FString::Printf(TEXT("Second plan builds: %s"), *Error), bB);
	if (!bA || !bB)
	{
		return false;
	}
	TestEqual(TEXT("Core plan hash is deterministic"),
		A.Summary.CorePlanHash, B.Summary.CorePlanHash);
	TestEqual(TEXT("Support plan hash is deterministic"),
		A.Summary.SupportPlanHash, B.Summary.SupportPlanHash);
	TestEqual(TEXT("Planned member count is deterministic"),
		A.Summary.PlannedMemberCount, B.Summary.PlannedMemberCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V3BudgetFailClosedTest,
	"ABTS.M73DAG.BeamC3V3.Stage1.G0.BudgetFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V3BudgetFailClosedTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	const FStage1Case Case{TEXT("TipOver"), 0, 730000};
	FABTSM73BeamD0ResolvedProfile Profile;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("Fixture resolves: %s"), *Error),
		ResolveShape(Case, Profile, Silhouette, Error)))
	{
		return false;
	}
	Profile.VisualComplexity.MinimumBrickCount = 1;
	Profile.VisualComplexity.MaximumBrickCount = 1;
	ABTSM73BeamC3V3::FPlan Plan;
	const bool bPlanned = FABTSM73BeamC3V3SkeletonFirstGenerator().BuildPlan(
		Profile, Silhouette, Plan, Error);
	TestFalse(TEXT("Impossible one-Brick budget fails closed"), bPlanned);
	TestTrue(TEXT("Budget rejection is explicit"),
		Error.Contains(TEXT("Budget")) || Error.Contains(TEXT("BrickWindow")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V3GroundingContractTest,
	"ABTS.M73DAG.BeamC3V3.Stage1.G0.AntiFake.GroundingContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V3GroundingContractTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	const FStage1Case Case{TEXT("DropTrigger"), 2, 740000};
	FABTSM73BeamD0ResolvedProfile Profile;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("Fixture resolves: %s"), *Error),
		ResolveShape(Case, Profile, Silhouette, Error)))
	{
		return false;
	}
	for (FABTSM73DAG5BV2Volume& Volume : Silhouette.Volumes)
	{
		Volume.LocalBounds.Min.Z += 72.0;
		Volume.LocalBounds.Max.Z += 72.0;
	}
	ABTSM73BeamC3V3::FPlan Plan;
	const bool bPlanned = FABTSM73BeamC3V3SkeletonFirstGenerator().BuildPlan(
		Profile, Silhouette, Plan, Error);
	TestFalse(TEXT("A silhouette translated 72 cm above Z=0 fails closed"), bPlanned);
	TestTrue(*FString::Printf(TEXT("Ground rejection is explicit: %s"), *Error),
		Error.Contains(TEXT("BeamC3V3GroundPlaneNotZero")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V3DirectedReachabilityTest,
	"ABTS.M73DAG.BeamC3V3.Stage1.G0.AntiFake.DirectedReachability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V3DirectedReachabilityTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	const FStage1Case Case{TEXT("DropTrigger"), 2, 740000};
	FABTSM73BeamD0ResolvedProfile Profile;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("Fixture resolves: %s"), *Error),
		ResolveShape(Case, Profile, Silhouette, Error)))
	{
		return false;
	}
	Silhouette.Volumes = {
		MakeBodyVolume(101, FVector(-324.0, -324.0, 0.0),
			FVector(-36.0, 324.0, 72.0), TEXT("AntiFake/Ground/A")),
		MakeBodyVolume(102, FVector(36.0, -324.0, 36.0),
			FVector(324.0, 324.0, 72.0), TEXT("AntiFake/Floating/B")),
		MakeBodyVolume(103, FVector(-324.0, -324.0, 72.0),
			FVector(324.0, 324.0, 144.0), TEXT("AntiFake/Common/C"))};
	ABTSM73BeamC3V3::FPlan Plan;
	const bool bPlanned = FABTSM73BeamC3V3SkeletonFirstGenerator().BuildPlan(
		Profile, Silhouette, Plan, Error);
	TestFalse(TEXT("An undirected connection cannot root the floating lower branch"),
		bPlanned);
	TestTrue(*FString::Printf(TEXT("Reachability rejection is explicit: %s"), *Error),
		Error.Contains(TEXT("BeamC3V3UnreachableSemanticVolumes")));
	TestEqual(TEXT("Exactly the floating branch is unreachable"),
		Plan.Summary.UnreachableVolumeCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V3DuplicateDerivationComponentsTest,
	"ABTS.M73DAG.BeamC3V3.Stage1.G0.AntiFake.DuplicateDerivationComponents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V3DuplicateDerivationComponentsTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	const FStage1Case Case{TEXT("DropTrigger"), 2, 740000};
	FABTSM73BeamD0ResolvedProfile Profile;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("Fixture resolves: %s"), *Error),
		ResolveShape(Case, Profile, Silhouette, Error)))
	{
		return false;
	}
	Profile.VisualComplexity.MinimumBrickCount = 1;
	Profile.VisualComplexity.MaximumBrickCount = 100000;
	Silhouette.Volumes = {
		MakeBodyVolume(201, FVector(-1044.0, -324.0, 0.0),
			FVector(-396.0, 324.0, 720.0), TEXT("Duplicate/Shared/Left")),
		MakeBodyVolume(202, FVector(396.0, -324.0, 0.0),
			FVector(1044.0, 324.0, 720.0), TEXT("Duplicate/Shared/Right"))};
	ABTSM73BeamC3V3::FPlan Plan;
	const bool bPlanned = FABTSM73BeamC3V3SkeletonFirstGenerator().BuildPlan(
		Profile, Silhouette, Plan, Error);
	TestTrue(*FString::Printf(TEXT("Separated duplicate-root plan builds: %s"), *Error),
		bPlanned);
	if (!bPlanned)
	{
		return false;
	}
	TestEqual(TEXT("Physical separation produces two components"),
		Plan.Components.Num(), 2);
	TestEqual(TEXT("Both physical components are independently grounded"),
		Plan.Summary.GroundedComponentCount, 2);
	if (Plan.Components.Num() == 2)
	{
		TestEqual(TEXT("The fixture really uses the same semantic root"),
			Plan.Components[0].SemanticRootPath,
			Plan.Components[1].SemanticRootPath);
		TestEqual(TEXT("The first component owns only its physical source"),
			Plan.Components[0].SourceVolumeIds.Num(), 1);
		TestEqual(TEXT("The second component owns only its physical source"),
			Plan.Components[1].SourceVolumeIds.Num(), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V3HashContractTest,
	"ABTS.M73DAG.BeamC3V3.Stage1.G0.AntiFake.HashSensitivityAndReorderInvariance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V3HashContractTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	const FStage1Case Case{TEXT("DropTrigger"), 2, 740000};
	FABTSM73BeamD0ResolvedProfile Profile;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("Fixture resolves: %s"), *Error),
		ResolveShape(Case, Profile, Silhouette, Error)))
	{
		return false;
	}
	FABTSM73BeamC3V3SkeletonFirstGenerator Generator;
	FVector SliverWitness = FVector::ZeroVector;
	const TArray<FBox> ChainedSliverCover = {
		FBox(FVector(0.8, 0.0, 0.0), FVector(2.0, 2.0, 2.0)),
		FBox(FVector(0.4, 0.4, 0.0), FVector(2.0, 2.0, 2.0))};
	TestFalse(TEXT("Chained 0/0.4/0.8 cuts cannot hide an uncovered positive-width cell"),
		Generator.ValidateSolidCoverageForTesting(
			FBox(FVector(0.0, 0.0, 0.0), FVector(2.0, 2.0, 2.0)),
			ChainedSliverCover, SliverWitness));
	ABTSM73BeamC3V3::FPlan Baseline;
	const bool bBaseline = Generator.BuildPlan(Profile, Silhouette, Baseline, Error);
	TestTrue(*FString::Printf(TEXT("Baseline plan builds: %s"), *Error), bBaseline);
	if (!bBaseline)
	{
		return false;
	}

	FABTSM73DAG5BV2GenerationResult Reordered = Silhouette;
	for (int32 Index = 0; Index < Reordered.Volumes.Num() / 2; ++Index)
	{
		Reordered.Volumes.Swap(Index, Reordered.Volumes.Num() - 1 - Index);
	}
	ABTSM73BeamC3V3::FPlan ReorderedPlan;
	const bool bReordered = Generator.BuildPlan(Profile, Reordered, ReorderedPlan, Error);
	TestTrue(*FString::Printf(TEXT("Reordered plan builds: %s"), *Error), bReordered);
	if (!bReordered)
	{
		return false;
	}
	TestEqual(TEXT("Envelope hash is input-order invariant"),
		Baseline.Summary.EnvelopeHash, ReorderedPlan.Summary.EnvelopeHash);
	TestEqual(TEXT("Core hash is input-order invariant"),
		Baseline.Summary.CorePlanHash, ReorderedPlan.Summary.CorePlanHash);
	TestEqual(TEXT("Support hash is input-order invariant"),
		Baseline.Summary.SupportPlanHash, ReorderedPlan.Summary.SupportPlanHash);
	TestEqual(TEXT("Final geometry hash is input-order invariant"),
		Baseline.Summary.FinalGeometryHash, ReorderedPlan.Summary.FinalGeometryHash);

	FABTSM73DAG5BV2GenerationResult Mutated = Silhouette;
	if (!TestTrue(TEXT("Hash fixture has at least one semantic volume"),
		!Mutated.Volumes.IsEmpty()))
	{
		return false;
	}
	++Mutated.Volumes[0].GrammarDepth;
	ABTSM73BeamC3V3::FPlan MutatedPlan;
	const bool bMutated = Generator.BuildPlan(Profile, Mutated, MutatedPlan, Error);
	TestTrue(*FString::Printf(TEXT("Metadata-mutated plan builds: %s"), *Error),
		bMutated);
	if (!bMutated)
	{
		return false;
	}
	TestNotEqual(TEXT("Envelope hash is sensitive to grammar depth"),
		Baseline.Summary.EnvelopeHash, MutatedPlan.Summary.EnvelopeHash);
	TestNotEqual(TEXT("Core identity inherits envelope sensitivity"),
		Baseline.Summary.CorePlanHash, MutatedPlan.Summary.CorePlanHash);
	TestNotEqual(TEXT("Support identity inherits envelope sensitivity"),
		Baseline.Summary.SupportPlanHash, MutatedPlan.Summary.SupportPlanHash);
	TestNotEqual(TEXT("Final identity inherits envelope sensitivity"),
		Baseline.Summary.FinalGeometryHash, MutatedPlan.Summary.FinalGeometryHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V3GeometryValidatorGuardsTest,
	"ABTS.M73DAG.BeamC3V3.Stage1.G0.AntiFake.GeometryValidatorGuards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V3GeometryValidatorGuardsTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	const FStage1Case Case{TEXT("DropTrigger"), 2, 740000};
	FABTSM73BeamD0ResolvedProfile Profile;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("Fixture resolves: %s"), *Error),
		ResolveShape(Case, Profile, Silhouette, Error)))
	{
		return false;
	}
	FABTSM73BeamC3V3SkeletonFirstGenerator Generator;
	ABTSM73BeamC3V3::FPlan Baseline;
	if (!TestTrue(*FString::Printf(TEXT("Baseline plan builds: %s"), *Error),
		Generator.BuildPlan(Profile, Silhouette, Baseline, Error)))
	{
		return false;
	}
	ABTSM73BeamC3V3::FPlan Revalidated = Baseline;
	TestTrue(*FString::Printf(TEXT("Production geometry revalidates: %s"), *Error),
		Generator.ValidateGeometryForTesting(Silhouette, Revalidated, Error));

	const int32 HorizontalIndex = Baseline.Members.IndexOfByPredicate(
		[](const ABTSM73BeamC3V3::FPlannedMember& Member)
		{
			return Member.OwnerKind != ABTSM73BeamC3V3::EOwnerKind::SupportedSpan
				&& (Member.Axis == EABTSM73BeamAFrameAxis::X
					|| Member.Axis == EABTSM73BeamAFrameAxis::Y);
		});
	if (!TestTrue(TEXT("Fixture contains a horizontal envelope member"),
		Baseline.Members.IsValidIndex(HorizontalIndex)))
	{
		return false;
	}
	ABTSM73BeamC3V3::FPlan Extended = Baseline;
	const int32 HorizontalAxis = Extended.Members[HorizontalIndex].Axis
		== EABTSM73BeamAFrameAxis::X ? 0 : 1;
	Extended.Members[HorizontalIndex].LocalStart[HorizontalAxis] -= 1440.0;
	Extended.Members[HorizontalIndex].LocalEnd[HorizontalAxis] += 1440.0;
	TestFalse(TEXT("A member with an unchanged midpoint but escaped ends is rejected"),
		Generator.ValidateGeometryForTesting(Silhouette, Extended, Error));
	TestTrue(*FString::Printf(TEXT("Full-solid rejection is explicit: %s"), *Error),
		Error.Contains(TEXT("BeamC3V3EnvelopeViolation")));

	const int32 PostIndex = Baseline.Members.IndexOfByPredicate(
		[](const ABTSM73BeamC3V3::FPlannedMember& Member)
		{
			return Member.OwnerKind
					== ABTSM73BeamC3V3::EOwnerKind::BuildingGroupShell
				&& Member.SkeletonKind
					== ABTSM73BeamC3V3::ESkeletonMemberKind::ExteriorPost
				&& Member.Role == EABTSM73BeamAMemberRole::Post;
		});
	if (!TestTrue(TEXT("Fixture contains a declared building-group ExteriorPost"),
		Baseline.Members.IsValidIndex(PostIndex)))
	{
		return false;
	}
	ABTSM73BeamC3V3::FPlan WrongOwner = Baseline;
	WrongOwner.Members[PostIndex].OwnerId = WrongOwner.BuildingGroups.Num();
	TestFalse(TEXT("An ExteriorPost must name its building group"),
		Generator.ValidateGeometryForTesting(Silhouette, WrongOwner, Error));
	TestTrue(*FString::Printf(TEXT("Owner mutation rejection is explicit: %s"), *Error),
		Error.Contains(TEXT("InvalidBuildingGroupContract")));
	ABTSM73BeamC3V3::FPlan Voided = Baseline;
	const FVector PostCenter = (Voided.Members[PostIndex].LocalStart
		+ Voided.Members[PostIndex].LocalEnd) * 0.5;
	FABTSM73BeamASupportVoid& Void =
		Voided.ReservedSupportVoids.AddDefaulted_GetRef();
	Void.Bounds = FBox(PostCenter - FVector(9.0), PostCenter + FVector(9.0));
	Void.SpanSourceVolumeId = -777;
	TestFalse(TEXT("A legal projection cannot pass through a ProtectedVoid"),
		Generator.ValidateGeometryForTesting(Silhouette, Voided, Error));
	TestTrue(*FString::Printf(TEXT("Void rejection is explicit: %s"), *Error),
		Error.Contains(TEXT("BeamC3V3ProtectedVoidViolation")));

	ABTSM73BeamC3V3::FPlan ForeignSource = Baseline;
	ForeignSource.Members[PostIndex].SourceVolumeId =
		Silhouette.Volumes.IsEmpty() ? 0 : Silhouette.Volumes[0].VolumeId;
	TestFalse(TEXT("A building-group ExteriorPost cannot borrow a component source"),
		Generator.ValidateGeometryForTesting(Silhouette, ForeignSource, Error));
	TestTrue(*FString::Printf(TEXT("Foreign projection rejection is explicit: %s"), *Error),
		Error.Contains(TEXT("InvalidBuildingGroupContract")));

	FABTSM73DAG5BV2GenerationResult SameRootSilhouette;
	SameRootSilhouette.Volumes = {
		MakeBodyVolume(0, FVector(0.0, 0.0, 0.0), FVector(144.0, 144.0, 144.0), TEXT("Root/Body0")),
		MakeBodyVolume(1, FVector(100.0, 0.0, 144.0), FVector(244.0, 144.0, 288.0), TEXT("Root/Body1"))};
	ABTSM73BeamC3V3::FPlan WrongSameRootSource;
	ABTSM73BeamC3V3::FPlannedMember& GroundCourse =
		WrongSameRootSource.Members.AddDefaulted_GetRef();
	GroundCourse.OwnerKind = ABTSM73BeamC3V3::EOwnerKind::Floor;
	GroundCourse.SkeletonKind = ABTSM73BeamC3V3::ESkeletonMemberKind::FloorCourse;
	GroundCourse.OwnerId = 0;
	GroundCourse.ComponentId = 0;
	GroundCourse.SourceVolumeId = 0;
	GroundCourse.CourseIndex = 0;
	GroundCourse.Axis = EABTSM73BeamAFrameAxis::X;
	GroundCourse.Role = EABTSM73BeamAMemberRole::PrimaryBeam;
	GroundCourse.LocalStart = FVector(0.0, 18.0, 18.0);
	GroundCourse.LocalEnd = FVector(144.0, 18.0, 18.0);
	GroundCourse.bRequiresGroundSeat = true;
	ABTSM73BeamC3V3::FPlannedMember& ExteriorPost =
		WrongSameRootSource.Members.AddDefaulted_GetRef();
	ExteriorPost.OwnerKind = ABTSM73BeamC3V3::EOwnerKind::ShellFace;
	ExteriorPost.SkeletonKind = ABTSM73BeamC3V3::ESkeletonMemberKind::ExteriorPost;
	ExteriorPost.OwnerId = 0;
	ExteriorPost.ComponentId = 0;
	ExteriorPost.SourceVolumeId = 1;
	ExteriorPost.CourseIndex = 3;
	ExteriorPost.FaceMask = ABTSM73BeamC3V3::NegativeX;
	ExteriorPost.Axis = EABTSM73BeamAFrameAxis::Z;
	ExteriorPost.Role = EABTSM73BeamAMemberRole::Post;
	ExteriorPost.LocalStart = FVector(18.0, 18.0, 36.0);
	ExteriorPost.LocalEnd = FVector(18.0, 18.0, 144.0);
	ExteriorPost.RequiredLowerMemberIndices.Add(0);
	TestFalse(TEXT("An exterior post cannot borrow a same-root active-slice source outside its XY halo"),
		Generator.ValidateGeometryForTesting(SameRootSilhouette, WrongSameRootSource, Error));
	TestTrue(*FString::Printf(TEXT("Same-root wrong-source rejection is explicit: %s"), *Error),
		Error.Contains(TEXT("InvalidExteriorPostSource")));

	ABTSM73BeamC3V3::FPlan Duplicate = Baseline;
	const ABTSM73BeamC3V3::FPlannedMember DuplicateMember = Duplicate.Members[0];
	Duplicate.Members.Add(DuplicateMember);
	TestFalse(TEXT("A duplicate positive-volume member is rejected"),
		Generator.ValidateGeometryForTesting(Silhouette, Duplicate, Error));
	TestTrue(*FString::Printf(TEXT("Penetration rejection is explicit: %s"), *Error),
		Error.Contains(TEXT("BeamC3V3PositiveVolumePenetration")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V3SeatAndSpanContractTest,
	"ABTS.M73DAG.BeamC3V3.Stage1.G0.AntiFake.SeatAndSpanContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V3SeatAndSpanContractTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	const FStage1Case Case{TEXT("SeamRelease"), 5, 720000};
	FABTSM73BeamD0ResolvedProfile Profile;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("Fixture resolves: %s"), *Error),
		ResolveShape(Case, Profile, Silhouette, Error)))
	{
		return false;
	}
	FABTSM73BeamC3V3SkeletonFirstGenerator Generator;
	ABTSM73BeamC3V3::FGenerationResult Result;
	if (!TestTrue(*FString::Printf(TEXT("V3 emits and verifies seats: %s"), *Error),
		Generator.Generate(Profile, Silhouette, Result, Error)))
	{
		return false;
	}
	TestTrue(TEXT("Ground seats are present"), Result.Plan.Summary.GroundSeatCount > 0);
	TestTrue(TEXT("Planned member seats are present"), Result.Plan.Summary.PlannedSeatCount > 0);
	TestEqual(TEXT("Every planned seat is verified"),
		Result.Plan.Summary.VerifiedSeatCount,
		Result.Plan.Summary.GroundSeatCount + Result.Plan.Summary.PlannedSeatCount);
	TestEqual(TEXT("No seat mismatch remains"), Result.Plan.Summary.SeatMismatchCount, 0);
	TestEqual(TEXT("Predicted and emitted contact totals match"),
		Result.Assembly.BearingContacts.Num(),
		Result.Plan.Summary.PlannedBearingContactCount);
	TestTrue(*FString::Printf(TEXT("Canonical Bearing pair set matches: %s"), *Error),
		Generator.ValidateBearingContactsForTesting(Profile.BeamSettings.BeamB.BeamA,
			Result.Plan, Result.Assembly.BearingContacts, Error));
	if (TestTrue(TEXT("Fixture has at least two Bearing rows"),
		Result.Assembly.BearingContacts.Num() >= 2))
	{
		TArray<FABTSM73BeamABearingContact> DuplicateContacts = Result.Assembly.BearingContacts;
		DuplicateContacts[0].LowerMemberId = DuplicateContacts[1].LowerMemberId;
		DuplicateContacts[0].UpperMemberId = DuplicateContacts[1].UpperMemberId;
		TestFalse(TEXT("Duplicate Bearing rows fail closed"),
			Generator.ValidateBearingContactsForTesting(Profile.BeamSettings.BeamB.BeamA,
				Result.Plan, DuplicateContacts, Error));
		TestTrue(*FString::Printf(TEXT("Duplicate Bearing rejection is explicit: %s"), *Error),
			Error.Contains(TEXT("BeamC3V3DuplicateBearingContact")));

		TSet<uint64> ExistingPairs;
		for (const FABTSM73BeamABearingContact& Contact : Result.Assembly.BearingContacts)
		{
			ExistingPairs.Add((static_cast<uint64>(static_cast<uint32>(Contact.LowerMemberId)) << 32)
				| static_cast<uint32>(Contact.UpperMemberId));
		}
		int32 ReplacementLower = INDEX_NONE;
		int32 ReplacementUpper = INDEX_NONE;
		for (int32 Lower = 0; Lower < Result.Plan.Members.Num() && ReplacementLower == INDEX_NONE; ++Lower)
		{
			for (int32 Upper = 0; Upper < Result.Plan.Members.Num(); ++Upper)
			{
				const uint64 Pair = (static_cast<uint64>(static_cast<uint32>(Lower)) << 32)
					| static_cast<uint32>(Upper);
				if (Lower != Upper && !ExistingPairs.Contains(Pair))
				{
					ReplacementLower = Lower;
					ReplacementUpper = Upper;
					break;
				}
			}
		}
		if (TestTrue(TEXT("Fixture has an unexpected replacement pair"),
			ReplacementLower != INDEX_NONE))
		{
			TArray<FABTSM73BeamABearingContact> ReplacedContacts = Result.Assembly.BearingContacts;
			ReplacedContacts[0].LowerMemberId = ReplacementLower;
			ReplacedContacts[0].UpperMemberId = ReplacementUpper;
			TestFalse(TEXT("An equal-count Bearing pair replacement fails closed"),
				Generator.ValidateBearingContactsForTesting(Profile.BeamSettings.BeamB.BeamA,
					Result.Plan, ReplacedContacts, Error));
			TestTrue(*FString::Printf(TEXT("Bearing replacement rejection is explicit: %s"), *Error),
				Error.Contains(TEXT("BeamC3V3BearingContactSetMismatch")));
		}
	}
	for (const ABTSM73BeamC3V3::FComponentPlan& Component : Result.Plan.Components)
	{
		TestNotEqual(*FString::Printf(TEXT("Component %d publishes a CRC"),
			Component.ComponentId), Component.ComponentCrc32, 0u);
	}
	int32 SharedRailCount = 0;
	int32 BridgeDiaphragmCount = 0;
	for (int32 UpperIndex = 0; UpperIndex < Result.Plan.Members.Num(); ++UpperIndex)
	{
		const ABTSM73BeamC3V3::FPlannedMember& Member = Result.Plan.Members[UpperIndex];
		if (!Member.bRequiresGroundSeat)
		{
			TestTrue(*FString::Printf(TEXT("Member %d has a lower seat"), UpperIndex),
				!Member.RequiredLowerMemberIndices.IsEmpty());
		}
		for (const int32 LowerIndex : Member.RequiredLowerMemberIndices)
		{
			TestTrue(*FString::Printf(TEXT("Seat %d>%d is topologically ordered"),
				LowerIndex, UpperIndex),
				Result.Plan.Members.IsValidIndex(LowerIndex) && LowerIndex < UpperIndex);
		}
		if (Member.SkeletonKind
			== ABTSM73BeamC3V3::ESkeletonMemberKind::SharedCourse)
		{
			++SharedRailCount;
			for (const int32 EndpointCoreCellId : Member.EndpointCoreCellIds)
			{
				const ABTSM73BeamC3V3::FCoreCellPlan* EndpointCore =
					SkeletonV3TestFindCore(Result.Plan, EndpointCoreCellId);
				if (EndpointCore == nullptr)
				{
					continue;
				}
				TSet<int32> EndpointLowerRails;
				for (const int32 LowerIndex : Member.RequiredLowerMemberIndices)
				{
					if (Result.Plan.Members.IsValidIndex(LowerIndex)
						&& EndpointCore->MemberIndices.Contains(LowerIndex)
						&& Result.Plan.Members[LowerIndex].CourseIndex
							== Member.CourseIndex - 1)
					{
						EndpointLowerRails.Add(LowerIndex);
					}
				}
				TestEqual(*FString::Printf(
					TEXT("Shared rail %d seats on both lower rails of endpoint core %d"),
					UpperIndex, EndpointCoreCellId), EndpointLowerRails.Num(), 2);
			}
		}
		else if (Member.SkeletonKind
			== ABTSM73BeamC3V3::ESkeletonMemberKind::BridgeDiaphragm)
		{
			++BridgeDiaphragmCount;
		}
	}
	TestEqual(TEXT("Seam E6 publishes every emitted long shared rail"),
		SharedRailCount, Result.Plan.Summary.SharedCourseCount);
	TestTrue(TEXT("Seam E6 emits at least one complete two-rail shared course"),
		SharedRailCount >= 2 && (SharedRailCount & 1) == 0);
	TestTrue(TEXT("Seam E6 bridge has transverse diaphragm members"),
		BridgeDiaphragmCount > 0);
	TestEqual(TEXT("Every long shared rail replaces one slot in each endpoint core"),
		Result.Plan.Summary.SharedCourseReplacementSlotCount,
		SharedRailCount * 2);
	ABTSM73BeamC3V3::FPlan WrongSpanOwner = Result.Plan;
	const int32 SpanMemberIndex = WrongSpanOwner.Members.IndexOfByPredicate(
		[](const ABTSM73BeamC3V3::FPlannedMember& Member)
		{
			return Member.OwnerKind == ABTSM73BeamC3V3::EOwnerKind::SupportedSpan;
		});
	if (TestTrue(TEXT("Seam E6 has a planned span member"),
		SpanMemberIndex != INDEX_NONE))
	{
		WrongSpanOwner.Members[SpanMemberIndex].OwnerId += 1;
		TestFalse(TEXT("A SupportedSpan owner/source mismatch fails closed"),
			Generator.ValidateGeometryForTesting(Silhouette, WrongSpanOwner, Error));
		TestTrue(*FString::Printf(TEXT("Span owner rejection is explicit: %s"), *Error),
			Error.Contains(TEXT("InvalidSupportedSpanContract")));
	}

	const FABTSM73DAG5BV2Volume* Span = Silhouette.Volumes.FindByPredicate(
		[](const FABTSM73DAG5BV2Volume& Volume)
		{
			return Volume.Role == EABTSM73DAG5BV2VolumeRole::SupportedSpan;
		});
	if (!TestNotNull(TEXT("Fixture contains a SupportedSpan"), Span))
	{
		return false;
	}
	const int32 SpanId = Span->VolumeId;
	auto ExpectInvalidSpan = [this, &Generator, &Profile, &Silhouette, SpanId](
		const TCHAR* Label, TFunctionRef<void(FABTSM73DAG5BV2Volume&)> Mutate,
		const TCHAR* ExpectedError)
	{
		FABTSM73DAG5BV2GenerationResult Invalid = Silhouette;
		FABTSM73DAG5BV2Volume* InvalidSpan = Invalid.Volumes.FindByPredicate(
			[SpanId](const FABTSM73DAG5BV2Volume& Volume)
			{
				return Volume.VolumeId == SpanId;
			});
		if (InvalidSpan == nullptr)
		{
			AddError(FString::Printf(TEXT("%s has no span"), Label));
			return;
		}
		Mutate(*InvalidSpan);
		ABTSM73BeamC3V3::FPlan InvalidPlan;
		FString InvalidError;
		TestFalse(Label, Generator.BuildPlan(Profile, Invalid, InvalidPlan, InvalidError));
		TestTrue(*FString::Printf(TEXT("%s is explicit: %s"), Label, *InvalidError),
			InvalidError.Contains(ExpectedError));
	};
	ExpectInvalidSpan(TEXT("Zero-width opening fails closed"),
		[](FABTSM73DAG5BV2Volume& Volume)
		{
			Volume.SpanOpeningMaxCM = Volume.SpanOpeningMinCM;
		}, TEXT("BeamC3V3SupportedSpanSemanticContractInvalid"));
	ExpectInvalidSpan(TEXT("Opening outside the span envelope fails closed"),
		[](FABTSM73DAG5BV2Volume& Volume)
		{
			Volume.SpanOpeningMinCM = Volume.LocalBounds.Min[Volume.SpanAxisIndex] - 36.0;
		}, TEXT("BeamC3V3SupportedSpanSemanticContractInvalid"));
	ExpectInvalidSpan(TEXT("Opening that does not cover the endpoint gap fails closed"),
		[](FABTSM73DAG5BV2Volume& Volume)
		{
			Volume.SpanOpeningMinCM += 36.0;
		}, TEXT("BeamC3V3SupportedSpanOpeningDoesNotCoverEndpointGap"));
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FABTSM73BeamC3V3PreflightCapsTest,
	"ABTS.M73DAG.BeamC3V3.Stage1.G0.AntiFake.PreflightCaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FABTSM73BeamC3V3PreflightCapsTest::GetTests(
	TArray<FString>& OutBeautifiedNames,
	TArray<FString>& OutTestCommands) const
{
	for (const TCHAR* Name : {TEXT("Bay"), TEXT("Member"), TEXT("Joint"),
		TEXT("Bearing"), TEXT("BearingPair")})
	{
		OutBeautifiedNames.Add(Name);
		OutTestCommands.Add(Name);
	}
}

bool FABTSM73BeamC3V3PreflightCapsTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	const FStage1Case Case{TEXT("DropTrigger"), 2, 740000};
	FABTSM73BeamD0ResolvedProfile Profile;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("Fixture resolves: %s"), *Error),
		ResolveShape(Case, Profile, Silhouette, Error)))
	{
		return false;
	}
	FABTSM73BeamC3V3SkeletonFirstGenerator Generator;
	ABTSM73BeamC3V3::FPlan Baseline;
	if (!TestTrue(*FString::Printf(TEXT("Baseline plan builds: %s"), *Error),
		Generator.BuildPlan(Profile, Silhouette, Baseline, Error)))
	{
		return false;
	}
	int32 Exact = 0;
	FString ExpectedError;
	auto SetTargetCap = [&Parameters](FABTSM73BeamAPreviewSettings& Settings,
		const int32 Value)
	{
		if (Parameters == TEXT("Bay")) Settings.MaxBayCount = Value;
		else if (Parameters == TEXT("Member")) Settings.MaxMemberCount = Value;
		else if (Parameters == TEXT("Joint")) Settings.MaxJointCount = Value;
		else if (Parameters == TEXT("Bearing")) Settings.MaxBearingContactCount = Value;
		else if (Parameters == TEXT("BearingPair")) Settings.MaxBearingPairChecks = Value;
	};
	if (Parameters == TEXT("Bay"))
	{
		Exact = Baseline.Summary.PlannedBayCount;
		ExpectedError = TEXT("BeamC3V3PreflightBayCap");
	}
	else if (Parameters == TEXT("Member"))
	{
		Exact = Baseline.Summary.PlannedMemberCount;
		ExpectedError = TEXT("BeamC3V3PreflightMemberCap");
	}
	else if (Parameters == TEXT("Joint"))
	{
		Exact = Baseline.Summary.PlannedJointCount;
		ExpectedError = TEXT("BeamC3V3PreflightJointCap");
	}
	else if (Parameters == TEXT("Bearing"))
	{
		Exact = Baseline.Summary.PlannedBearingContactCount;
		ExpectedError = TEXT("BeamC3V3PreflightBearingCap");
	}
	else if (Parameters == TEXT("BearingPair"))
	{
		Exact = Baseline.Summary.PlannedBearingPairCheckCount;
		ExpectedError = TEXT("BeamC3V3PreflightBearingPairCap");
	}
	else
	{
		AddError(FString::Printf(TEXT("Unknown cap leaf: %s"), *Parameters));
		return false;
	}
	if (!TestTrue(TEXT("Baseline exact cap is positive"), Exact > 0))
	{
		return false;
	}
	FABTSM73BeamD0ResolvedProfile ExactProfile = Profile;
	SetTargetCap(ExactProfile.BeamSettings.BeamB.BeamA, Exact);
	ABTSM73BeamC3V3::FPlan ExactPlan;
	TestTrue(*FString::Printf(TEXT("Exact %s cap succeeds: %s"),
		*Parameters, *Error),
		Generator.BuildPlan(ExactProfile, Silhouette, ExactPlan, Error));
	FABTSM73BeamD0ResolvedProfile ShortProfile = Profile;
	SetTargetCap(ShortProfile.BeamSettings.BeamB.BeamA, Exact - 1);
	ABTSM73BeamC3V3::FPlan ShortPlan;
	TestFalse(*FString::Printf(TEXT("Exact-1 %s cap fails"), *Parameters),
		Generator.BuildPlan(ShortProfile, Silhouette, ShortPlan, Error));
	TestTrue(*FString::Printf(TEXT("%s rejection is explicit: %s"),
		*Parameters, *Error), Error.Contains(ExpectedError));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V3FullChainDeterminismTest,
	"ABTS.M73DAG.BeamC3V3.Stage1.G0.FullChainDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V3FullChainDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	const FStage1Case Case{TEXT("DropTrigger"), 2, 740000};
	FABTSM73BeamD1GenerationResult A;
	FABTSM73BeamD1GenerationResult B;
	FString ErrorA;
	FString ErrorB;
	FABTSM73BeamD1BrickCompiler Compiler;
	const bool bA = Compiler.Generate(MakeD1Settings(Case), A, ErrorA);
	const bool bB = Compiler.Generate(MakeD1Settings(Case), B, ErrorB);
	TestTrue(*FString::Printf(TEXT("First full chain compiles: %s"), *ErrorA), bA);
	TestTrue(*FString::Printf(TEXT("Second full chain compiles: %s"), *ErrorB), bB);
	if (!bA || !bB)
	{
		return false;
	}
	TestEqual(TEXT("Envelope identity repeats"),
		A.Summary.SkeletonFirstEnvelopeHash, B.Summary.SkeletonFirstEnvelopeHash);
	TestEqual(TEXT("Core identity repeats"),
		A.Summary.SkeletonFirstCorePlanHash, B.Summary.SkeletonFirstCorePlanHash);
	TestEqual(TEXT("Support identity repeats"),
		A.Summary.SkeletonFirstSupportPlanHash, B.Summary.SkeletonFirstSupportPlanHash);
	TestEqual(TEXT("Final geometry identity repeats"),
		A.Summary.SkeletonFirstFinalGeometryHash,
		B.Summary.SkeletonFirstFinalGeometryHash);
	TestEqual(TEXT("Load DAG identity repeats"),
		A.Summary.CoupledExteriorFrameDAGEvidenceHash,
		B.Summary.CoupledExteriorFrameDAGEvidenceHash);
	TestEqual(TEXT("Brick identity repeats"),
		A.Summary.BrickGeometryHash, B.Summary.BrickGeometryHash);
	TestEqual(TEXT("Candidate Seed repeats"),
		A.Summary.SkeletonFirstCandidateSeed,
		B.Summary.SkeletonFirstCandidateSeed);
	TestNotEqual(TEXT("First run protects BridgeRail from weakness selection"),
		A.Summary.WeaknessCandidateMemberRole,
		EABTSM73BeamAMemberRole::BridgeRail);
	TestNotEqual(TEXT("Second run protects BridgeRail from weakness selection"),
		B.Summary.WeaknessCandidateMemberRole,
		EABTSM73BeamAMemberRole::BridgeRail);
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FABTSM73BeamC3V3BoundaryTest,
	"ABTS.M73DAG.BeamC3V3.Stage1.G1.Boundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FABTSM73BeamC3V3BoundaryTest::GetTests(
	TArray<FString>& OutBeautifiedNames,
	TArray<FString>& OutTestCommands) const
{
	using namespace ABTSM73BeamC3V3Tests;
	for (const FStage1Case& Case : BoundaryCases())
	{
		OutBeautifiedNames.Add(FString::Printf(
			TEXT("%s.E%d"), *Case.ProfileId.ToString(), Case.Tier + 1));
		OutTestCommands.Add(CaseCommand(Case));
	}
}

bool FABTSM73BeamC3V3BoundaryTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	FStage1Case Case;
	if (!ParseCase(Parameters, Case))
	{
		AddError(FString::Printf(TEXT("Invalid V3 boundary case: %s"), *Parameters));
		return false;
	}
	FABTSM73BeamD1GenerationResult Result;
	FString Error;
	const bool bGenerated = FABTSM73BeamD1BrickCompiler().Generate(
		MakeD1Settings(Case), Result, Error);
	TestTrue(*FString::Printf(TEXT("Boundary compiles: %s"), *Error), bGenerated);
	return bGenerated && CheckStage1Result(*this, Case, Result);
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FABTSM73BeamC3V3MatrixTest,
	"ABTS.M73DAG.BeamC3V3.Stage1.Matrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FABTSM73BeamC3V3MatrixTest::GetTests(
	TArray<FString>& OutBeautifiedNames,
	TArray<FString>& OutTestCommands) const
{
	using namespace ABTSM73BeamC3V3Tests;
	for (const FStage1Case& Case : MatrixCases())
	{
		OutBeautifiedNames.Add(FString::Printf(
			TEXT("%s.E%d"), *Case.ProfileId.ToString(), Case.Tier + 1));
		OutTestCommands.Add(CaseCommand(Case));
	}
}

bool FABTSM73BeamC3V3MatrixTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	FStage1Case Case;
	if (!ParseCase(Parameters, Case))
	{
		AddError(FString::Printf(TEXT("Invalid V3 matrix case: %s"), *Parameters));
		return false;
	}
	FABTSM73BeamD1GenerationResult Result;
	FString Error;
	const bool bGenerated = FABTSM73BeamD1BrickCompiler().Generate(
		MakeD1Settings(Case), Result, Error);
	TestTrue(*FString::Printf(TEXT("Matrix leaf compiles: %s"), *Error), bGenerated);
	return bGenerated && CheckStage1Result(*this, Case, Result);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3StagedStage0StopTest,
	"ABTS.M73DAG.BeamC3V3.Staged.Stage0StopsBeforeMembers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3StagedStage0StopTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	FABTSM73BeamD1StagePreviewResult Result;
	FString Error;
	const bool bGenerated = FABTSM73BeamD1BrickCompiler().GenerateStagePreview(
		MakeD1Settings({TEXT("ColumnBreak"), 0, 710000}),
		EABTSM73BeamC3GenerationStage::SemanticEnvelope, Result, Error);
	TestTrue(*FString::Printf(TEXT("Stage 0 accepts: %s"), *Error), bGenerated);
	if (!bGenerated)
	{
		return false;
	}
	TestTrue(TEXT("WFC volumes are present"), !Result.Silhouette.Volumes.IsEmpty());
	TestTrue(TEXT("Stage 0 emits no planned members"),
		Result.Skeleton.Plan.Members.IsEmpty());
	TestTrue(TEXT("Stage 0 emits no Beam-A members"),
		Result.Skeleton.Assembly.Members.IsEmpty());
	TestFalse(TEXT("Stage 0 does not run static DAG"),
		Result.Summary.bStageStaticDAGEvaluated);
	TestNotEqual(TEXT("Stage 0 exports its semantic envelope identity"),
		Result.Summary.SkeletonFirstEnvelopeHash, int64(0));
	TestEqual(TEXT("Stage identity is Stage 0"), Result.Summary.GenerationStage,
		EABTSM73BeamC3GenerationStage::SemanticEnvelope);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3SharedEndpointReachabilityTest,
	"ABTS.M73DAG.BeamC3V3.Staged.SharedEndpointReachability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3SharedEndpointReachabilityTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	FABTSM73BeamD0ResolvedProfile Profile;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("SeamRelease E6 WFC resolves: %s"),
		*Error), ResolveShape({TEXT("SeamRelease"), 5, 720000},
			Profile, Silhouette, Error)))
	{
		return false;
	}
	TArray<ABTSM73BeamC3V3::FSharedEndpointReachabilityDiagnostic> Diagnostics;
	const double StartSeconds = FPlatformTime::Seconds();
	const bool bEvaluated = FABTSM73BeamC3V3SkeletonFirstGenerator()
		.EvaluateSharedEndpointReachabilityForTesting(
			Silhouette, Diagnostics, Error);
	const double ElapsedMilliseconds =
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	TestTrue(*FString::Printf(TEXT("Reachability evaluates: %s"), *Error),
		bEvaluated);
	if (!bEvaluated)
	{
		return false;
	}
	TestTrue(TEXT("Reachability publishes candidate witnesses"),
		!Diagnostics.IsEmpty());
	TSet<int32> SpanIds;
	for (const ABTSM73BeamC3V3::FSharedEndpointReachabilityDiagnostic& Diagnostic
		: Diagnostics)
	{
		SpanIds.Add(Diagnostic.SpanVolumeId);
	}
	bool bEverySpanReachable = true;
	for (const int32 SpanVolumeId : SpanIds)
	{
		const ABTSM73BeamC3V3::FSharedEndpointReachabilityDiagnostic* BestNegative = nullptr;
		const ABTSM73BeamC3V3::FSharedEndpointReachabilityDiagnostic* BestPositive = nullptr;
		int32 NegativeGrounded = 0;
		int32 PositiveGrounded = 0;
		int32 NegativeCovered = 0;
		int32 PositiveCovered = 0;
		for (const ABTSM73BeamC3V3::FSharedEndpointReachabilityDiagnostic& Candidate
			: Diagnostics)
		{
			if (Candidate.SpanVolumeId != SpanVolumeId)
			{
				continue;
			}
			(Candidate.bNegativeEndpoint ? NegativeGrounded : PositiveGrounded)
				+= Candidate.bGrounded ? 1 : 0;
			(Candidate.bNegativeEndpoint ? NegativeCovered : PositiveCovered)
				+= Candidate.bBodyCrownStackCovered ? 1 : 0;
			if (!Candidate.bReachableInWFC)
			{
				continue;
			}
			const ABTSM73BeamC3V3::FSharedEndpointReachabilityDiagnostic*& Best =
				Candidate.bNegativeEndpoint ? BestNegative : BestPositive;
			if (Best == nullptr
				|| Candidate.EndpointInsetCM < Best->EndpointInsetCM)
			{
				Best = &Candidate;
			}
		}
		const FABTSM73DAG5BV2Volume* Span = Silhouette.Volumes.FindByPredicate(
			[SpanVolumeId](const FABTSM73DAG5BV2Volume& Volume)
			{
				return Volume.VolumeId == SpanVolumeId;
			});
		const double MinimumCrossSegmentCM =
			BestNegative != nullptr && BestPositive != nullptr && Span != nullptr
				? (Span->SpanOpeningMaxCM - Span->SpanOpeningMinCM)
					+ 2.0 * SkeletonV3TestBlockUnitsCM
					+ FMath::Max(0.0, BestNegative->EndpointInsetCM)
					+ FMath::Max(0.0, BestPositive->EndpointInsetCM)
				: DBL_MAX;
		const bool bSpanReachable = MinimumCrossSegmentCM
			<= 720.0 + SkeletonV3TestGeometryToleranceCM;
		bEverySpanReachable &= bSpanReachable;
		AddInfo(FString::Printf(
			TEXT("SharedEndpointReachability:Span=%d:Candidates=%d:NegativeGrounded=%d:PositiveGrounded=%d:NegativeCovered=%d:PositiveCovered=%d:NegativeBest=%s:PositiveBest=%s:MinimumCross=%.3f:Reachable=%d"),
			SpanVolumeId, Diagnostics.Num(), NegativeGrounded, PositiveGrounded,
			NegativeCovered, PositiveCovered,
			BestNegative != nullptr
				? *FString::Printf(TEXT("Source=%d:Branch=%s:Inset=%.3f:Bounds=%s"),
					BestNegative->CandidateTopSourceVolumeId,
					*BestNegative->CandidateBranchPath,
					BestNegative->EndpointInsetCM,
					*BestNegative->CandidateBounds.ToString())
				: TEXT("None"),
			BestPositive != nullptr
				? *FString::Printf(TEXT("Source=%d:Branch=%s:Inset=%.3f:Bounds=%s"),
					BestPositive->CandidateTopSourceVolumeId,
					*BestPositive->CandidateBranchPath,
					BestPositive->EndpointInsetCM,
					*BestPositive->CandidateBounds.ToString())
				: TEXT("None"),
			MinimumCrossSegmentCM, bSpanReachable ? 1 : 0));
	}
	AddInfo(FString::Printf(TEXT("SharedEndpointReachability:ElapsedMs=%.3f"),
		ElapsedMilliseconds));
	TestTrue(TEXT("Every shared span has a grounded WFC endpoint pair within 720 cm"),
		bEverySpanReachable);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3StagedStage1BoundaryTest,
	"ABTS.M73DAG.BeamC3V3.Staged.Stage1CoreAndSharedBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3StagedStage1BoundaryTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	FABTSM73BeamD1StagePreviewResult Result;
	FString Error;
	const bool bGenerated = FABTSM73BeamD1BrickCompiler().GenerateStagePreview(
		MakeD1Settings({TEXT("SeamRelease"), 5, 720000}),
		EABTSM73BeamC3GenerationStage::CoreAndShared, Result, Error);
	TestTrue(*FString::Printf(TEXT("Stage 1 accepts: %s"), *Error), bGenerated);
	if (!bGenerated)
	{
		for (const ABTSM73BeamC3V3::FJointCoreSelectionDiagnostic& Diagnostic
			: Result.Skeleton.Plan.JointCoreSelectionDiagnostics)
		{
			AddInfo(FString::Printf(
				TEXT("Stage1MatrixJointFailure:Component=%d:Regions=%d:Provinces=%d:MainCandidates=%d:NoCompatibility=%d:States=%d:MaximumCovered=%d:MaximumMask=0x%08x:MaximumProvinceCovered=%d:MaximumProvinceMask=0x%08x:BestPartial=%s:CompatibilityPerRegion=%s:PodiumCoveragePerRegion=%s:MainCoveragePerProvince=%s:Visited=%d:Reason=%s"),
				Diagnostic.ComponentId, Diagnostic.HighProjectionRegionCount,
				Diagnostic.SupportProvinceCount,
				Diagnostic.PodiumMainCandidateCount,
				Diagnostic.MainCandidateWithoutFullHeightCompatibilityCount,
				Diagnostic.MainSelectionStateCount,
				Diagnostic.MaximumCoveredRegionCount,
				Diagnostic.MaximumCoveredRegionMask,
				Diagnostic.MaximumCoveredSupportProvinceCount,
				Diagnostic.MaximumCoveredSupportProvinceMask,
				*JoinIds(Diagnostic.BestPartialMainCandidateIndices),
				*JoinIds(Diagnostic.CompatibleMainCandidateCountByRegion),
				*JoinIds(Diagnostic.PodiumCoverageMainCandidateCountByRegion),
				*JoinIds(Diagnostic.MainCandidateCountBySupportProvince),
				Diagnostic.MainSelectionsVisited, *Diagnostic.SelectionReason));
		}
		for (const FString& Trace : Result.Silhouette.GrammarTrace)
		{
			if (Trace.Contains(TEXT("CoupledGround")))
			{
				AddInfo(TEXT("Stage1SemanticPodiumTrace:") + Trace);
			}
		}
		for (const FABTSM73DAG5BV2Volume& Volume : Result.Silhouette.Volumes)
		{
			AddInfo(FString::Printf(
				TEXT("Stage1SemanticVolume:Id=%d:Role=%d:Path=%s:Bounds=%s"),
				Volume.VolumeId, static_cast<int32>(Volume.Role),
				*Volume.DerivationPath, *Volume.LocalBounds.ToString()));
		}
		for (const FABTSM73BeamCLoadNode& Node : Result.StaticDAG.Nodes)
		{
			if (Node.bSupportResultantValid)
			{
				continue;
			}
			double SupportMinimum = DBL_MAX;
			double SupportMaximum = -DBL_MAX;
			for (const FABTSM73BeamCLoadEdge& Edge : Result.StaticDAG.Edges)
			{
				if (Edge.UpperMemberId != Node.MemberId)
				{
					continue;
				}
				const int32 AxisIndex = Node.Axis == EABTSM73BeamAFrameAxis::X
					? 0 : 1;
				SupportMinimum = FMath::Min(SupportMinimum,
					AxisIndex == 0 ? Edge.ContactMinXY.X : Edge.ContactMinXY.Y);
				SupportMaximum = FMath::Max(SupportMaximum,
					AxisIndex == 0 ? Edge.ContactMaxXY.X : Edge.ContactMaxXY.Y);
			}
			const ABTSM73BeamC3V3::FPlannedMember* Planned =
				Result.Skeleton.Plan.Members.IsValidIndex(Node.MemberId)
					? &Result.Skeleton.Plan.Members[Node.MemberId] : nullptr;
			AddInfo(FString::Printf(
				TEXT("Stage1SupportResultant:Member=%d:Kind=%d:Owner=%d:Course=%d:Axis=%d:Resultant=%.3f,%.3f:Support=%.3f..%.3f:Count=%d:Bounds=%s"),
				Node.MemberId,
				Planned != nullptr ? static_cast<int32>(Planned->SkeletonKind) : -1,
				Planned != nullptr ? Planned->OwnerId : INDEX_NONE,
				Planned != nullptr ? Planned->CourseIndex : INDEX_NONE,
				static_cast<int32>(Node.Axis), Node.LoadResultant.X,
				Node.LoadResultant.Y, SupportMinimum, SupportMaximum,
				Node.SupportCount,
				Planned != nullptr
					? *FBox(Planned->LocalStart, Planned->LocalEnd).ToString()
					: TEXT("None")));
		}
		return false;
	}
	const ABTSM73BeamC3V3::FPlan& Plan = Result.Skeleton.Plan;
	TestTrue(TEXT("Stage 1 has explicit grounded cores"), !Plan.CoreCells.IsEmpty());
	AddInfo(FString::Printf(
		TEXT("Stage1HighProjectionCoverage:Regions=%d:Bound=%d:Children=%d"),
		Plan.Summary.HighProjectionRegionCount,
		Plan.Summary.BoundHighProjectionRegionCount,
		Plan.Summary.TowerChildCoreCellCount));
	TestTrue(TEXT("Fixed E6 discovers upper projection regions"),
		Plan.Summary.HighProjectionRegionCount > 0);
	TestEqual(TEXT("Fixed E6 binds every high projection region"),
		Plan.Summary.BoundHighProjectionRegionCount,
		Plan.Summary.HighProjectionRegionCount);
	TestEqual(TEXT("Fixed E6 emits exactly one child per high projection region"),
		Plan.Summary.TowerChildCoreCellCount,
		Plan.Summary.HighProjectionRegionCount);
	for (const ABTSM73BeamC3V3::FHighProjectionRegionPlan& Region
		: Plan.HighProjectionRegions)
	{
		const ABTSM73BeamC3V3::FCoreCellPlan* BoundChild =
			SkeletonV3TestFindCore(Plan, Region.BoundCoreCellId);
		TestNotNull(TEXT("High projection region resolves its bound child"),
			BoundChild);
		if (BoundChild == nullptr)
		{
			continue;
		}
		TestEqual(TEXT("High projection binds a TowerChild"),
			BoundChild->HierarchyRole,
			ABTSM73BeamC3V3::ECoreHierarchyRole::TowerChild);
		TestEqual(TEXT("Child points back to its high projection region"),
			BoundChild->HighProjectionRegionId, Region.RegionId);
		const ABTSM73BeamC3V3::FCoreCellPlan* Main =
			SkeletonV3TestFindCore(Plan, BoundChild->PodiumMainCoreCellId);
		if (TestNotNull(TEXT("Bound child resolves its podium main"), Main))
		{
			const FABTSM73DAG5BV2RaisedMainReservation* Reservation =
				Plan.RaisedMainReservations.FindByPredicate(
					[Main](const FABTSM73DAG5BV2RaisedMainReservation& Candidate)
					{
						return Candidate.PodiumMainCoreCellId == Main->CoreCellId;
					});
			const int32 ExpectedMainTop = Reservation != nullptr
				? Reservation->ApprovedTopCourse : Region.PodiumTopCourse;
			TestEqual(TEXT("Podium main stops at its baseline or approved raised top"),
				Main->TopCourseIndex, ExpectedMainTop);
			if (Reservation != nullptr)
			{
				TestTrue(TEXT("Raised main starts from this region's baseline"),
					Reservation->OriginalTopCourse == Region.PodiumTopCourse
						&& Reservation->ApprovedTopCourse
							== Main->RaisedPodiumMainTopCourseIndex);
			}
		}
	}
	TestTrue(TEXT("SeamRelease E6 has shared pairing intent"),
		!Plan.SharedCourseIntents.IsEmpty());
	for (const ABTSM73BeamC3V3::FSharedCourseIntent& Intent
		: Plan.SharedCourseIntents)
	{
		const ABTSM73BeamC3V3::FCoreCellPlan* Negative =
			SkeletonV3TestFindCore(Plan, Intent.NegativeCoreCellId);
		const ABTSM73BeamC3V3::FCoreCellPlan* Positive =
			SkeletonV3TestFindCore(Plan, Intent.PositiveCoreCellId);
		TestNotNull(TEXT("Shared intent resolves its negative endpoint core"), Negative);
		TestNotNull(TEXT("Shared intent resolves its positive endpoint core"), Positive);
		if (Negative != nullptr && Positive != nullptr)
		{
			const FVector NegativeSize = Negative->LocalBounds.GetSize();
			const FVector PositiveSize = Positive->LocalBounds.GetSize();
			AddInfo(FString::Printf(
				TEXT("Stage1SharedEndpointSize:Span=%d:Negative=%s:Positive=%s"),
				Intent.SpanVolumeId, *NegativeSize.ToString(),
				*PositiveSize.ToString()));
			TestTrue(TEXT("Negative production endpoint grows beyond the 1x1 witness"),
				NegativeSize.X > SkeletonV3TestBlockUnitsCM
					+ SkeletonV3TestGeometryToleranceCM
				&& NegativeSize.Y > SkeletonV3TestBlockUnitsCM
					+ SkeletonV3TestGeometryToleranceCM);
			TestTrue(TEXT("Positive production endpoint grows beyond the 1x1 witness"),
				PositiveSize.X > SkeletonV3TestBlockUnitsCM
					+ SkeletonV3TestGeometryToleranceCM
				&& PositiveSize.Y > SkeletonV3TestBlockUnitsCM
					+ SkeletonV3TestGeometryToleranceCM);
			TestEqual(TEXT("Negative endpoint consumes its dedicated reservation"),
				Negative->SharedEndpointSpanVolumeId, Intent.SpanVolumeId);
			TestEqual(TEXT("Positive endpoint consumes its dedicated reservation"),
				Positive->SharedEndpointSpanVolumeId, Intent.SpanVolumeId);
			TestTrue(TEXT("Negative endpoint side identity is frozen"),
				Negative->bNegativeSharedEndpoint);
			TestFalse(TEXT("Positive endpoint side identity is frozen"),
				Positive->bNegativeSharedEndpoint);
			TestEqual(TEXT("Negative endpoint has explicit SharedEndpoint role"),
				Negative->HierarchyRole,
				ABTSM73BeamC3V3::ECoreHierarchyRole::SharedEndpoint);
			TestEqual(TEXT("Positive endpoint has explicit SharedEndpoint role"),
				Positive->HierarchyRole,
				ABTSM73BeamC3V3::ECoreHierarchyRole::SharedEndpoint);
		}
		for (const ABTSM73BeamC3V3::FSharedCourseLanePlan& Lane
			: Intent.LanePlans)
		{
			TestEqual(TEXT("Fixed E6 uses one statically supported shared segment"),
				Lane.SegmentMemberIndices.Num(), 1);
			TestTrue(TEXT("Cross-core segment identity is valid"),
				Plan.Members.IsValidIndex(Lane.CrossCoreSegmentMemberIndex));
			if (Plan.Members.IsValidIndex(Lane.CrossCoreSegmentMemberIndex))
			{
				const ABTSM73BeamC3V3::FPlannedMember& Cross =
					Plan.Members[Lane.CrossCoreSegmentMemberIndex];
				TestTrue(TEXT("One physical segment enters both endpoint cores"),
					Cross.bSharedCrossCoreSegment
					&& Cross.EndpointCoreCellIds.Contains(Intent.NegativeCoreCellId)
					&& Cross.EndpointCoreCellIds.Contains(Intent.PositiveCoreCellId));
				TestTrue(TEXT("Cross-core segment respects the 720 cm hard limit"),
					FVector::Distance(Cross.LocalStart, Cross.LocalEnd)
						<= 720.0 + SkeletonV3TestGeometryToleranceCM);
			}
		}
	}
	TestTrue(TEXT("Stage 1 emits shared rails"),
		Plan.Summary.SharedCourseCount >= 2);
	TestEqual(TEXT("Every shared rail replaces both endpoint slots"),
		Plan.Summary.SharedCourseReplacementSlotCount,
		Plan.Summary.SharedCourseCount * 2);
	TestEqual(TEXT("Stage 1 emits no building group shell"),
		Plan.Summary.BuildingGroupCount, 0);
	TestEqual(TEXT("Stage 1 emits no shell members"),
		Plan.Summary.ShellMemberCount, 0);
	int32 DiaphragmCount = 0;
	for (const ABTSM73BeamC3V3::FPlannedMember& Member : Plan.Members)
	{
		TestEqual(TEXT("Every Stage 1 member records Stage 1 provenance"),
			Member.ProducedStage, EABTSM73BeamC3GenerationStage::CoreAndShared);
		const bool bAllowed =
			Member.SkeletonKind == ABTSM73BeamC3V3::ESkeletonMemberKind::CoreCourse
			|| Member.SkeletonKind == ABTSM73BeamC3V3::ESkeletonMemberKind::SharedCourse
			|| Member.SkeletonKind == ABTSM73BeamC3V3::ESkeletonMemberKind::BridgeDiaphragm;
		TestTrue(TEXT("No Stage 2+ member is emitted"), bAllowed);
		DiaphragmCount +=
			Member.SkeletonKind == ABTSM73BeamC3V3::ESkeletonMemberKind::BridgeDiaphragm
				? 1 : 0;
	}
	TestTrue(TEXT("Shared bridge includes diaphragms"), DiaphragmCount > 0);
	TestTrue(TEXT("Stage 1 runs and passes static DAG"),
		Result.Summary.bStageStaticDAGEvaluated
		&& Result.StaticDAG.Summary.bAccepted);
	TestEqual(TEXT("Planned and emitted members match"),
		Plan.Summary.PlannedMemberCount, Plan.Summary.EmittedMemberCount);
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FABTSM73BeamC3StagedStage1MatrixTest,
	"ABTS.M73DAG.BeamC3V3.Staged.Stage1CoreAndSharedMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FABTSM73BeamC3StagedStage1MatrixTest::GetTests(
	TArray<FString>& OutBeautifiedNames,
	TArray<FString>& OutTestCommands) const
{
	using namespace ABTSM73BeamC3V3Tests;
	for (const FStage1Case& Case : MatrixCases())
	{
		OutBeautifiedNames.Add(FString::Printf(
			TEXT("%s.E%d"), *Case.ProfileId.ToString(), Case.Tier + 1));
		OutTestCommands.Add(CaseCommand(Case));
	}
}

bool FABTSM73BeamC3StagedStage1MatrixTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	FStage1Case Case;
	if (!ParseCase(Parameters, Case))
	{
		AddError(FString::Printf(
			TEXT("Invalid staged Stage 1 matrix case: %s"), *Parameters));
		return false;
	}

	FABTSM73BeamD1StagePreviewResult Result;
	FString Error;
	const bool bGenerated = FABTSM73BeamD1BrickCompiler().GenerateStagePreview(
		MakeD1Settings(Case), EABTSM73BeamC3GenerationStage::CoreAndShared,
		Result, Error);
	TestTrue(*FString::Printf(TEXT("Stage 1 matrix leaf accepts: %s"), *Error),
		bGenerated);
	if (!bGenerated)
	{
		for (const ABTSM73BeamC3V3::FJointCoreSelectionDiagnostic& Diagnostic
			: Result.Skeleton.Plan.JointCoreSelectionDiagnostics)
		{
			AddInfo(FString::Printf(
				TEXT("Stage1MatrixJointFailure:Component=%d:Regions=%d:MainCandidates=%d:NoCompatibility=%d:States=%d:MaximumCovered=%d:MaximumMask=0x%08x:BestPartial=%s:CompatibilityPerRegion=%s:PodiumCoveragePerRegion=%s:Visited=%d:Reason=%s"),
				Diagnostic.ComponentId, Diagnostic.HighProjectionRegionCount,
				Diagnostic.PodiumMainCandidateCount,
				Diagnostic.MainCandidateWithoutFullHeightCompatibilityCount,
				Diagnostic.MainSelectionStateCount,
				Diagnostic.MaximumCoveredRegionCount,
				Diagnostic.MaximumCoveredRegionMask,
				*JoinIds(Diagnostic.BestPartialMainCandidateIndices),
				*JoinIds(Diagnostic.CompatibleMainCandidateCountByRegion),
				*JoinIds(Diagnostic.PodiumCoverageMainCandidateCountByRegion),
				Diagnostic.MainSelectionsVisited, *Diagnostic.SelectionReason));
		}
		for (const FString& Trace : Result.Silhouette.GrammarTrace)
		{
			if (Trace.Contains(TEXT("CoupledGround")))
			{
				AddInfo(TEXT("Stage1MatrixSemanticPodiumTrace:") + Trace);
			}
		}
		for (const FABTSM73DAG5BV2SemanticPodiumCandidateDiagnostic& Diagnostic
			: Result.Silhouette.SemanticPodiumCandidateDiagnostics)
		{
			AddInfo(FString::Printf(
				TEXT("Stage1MatrixSemanticCandidate:Scope=%s:Seam=%.3f:Top=%.3f:Accepted=%d:Reason=%s"),
				*Diagnostic.Scope, Diagnostic.SemanticSeamZ,
				Diagnostic.QuantizedTopZ, Diagnostic.bAccepted ? 1 : 0,
				*Diagnostic.RejectReason));
		}
		return false;
	}

	const ABTSM73BeamC3V3::FPlan& Plan = Result.Skeleton.Plan;
	const FString Prefix = FString::Printf(
		TEXT("%s.E%d"), *Case.ProfileId.ToString(), Case.Tier + 1);
	if (Case.ProfileId == TEXT("ColumnBreak") && Case.Tier == 5)
	{
		for (const FABTSM73DAG5BV2Volume& Volume : Result.Silhouette.Volumes)
		{
			AddInfo(FString::Printf(
				TEXT("Stage1SemanticVolume:%s:Id=%d:Role=%d:MinZ=%.3f:MaxZ=%.3f:MinXY=%.3f,%.3f:MaxXY=%.3f,%.3f:Path=%s"),
				*Prefix, Volume.VolumeId, static_cast<int32>(Volume.Role),
				Volume.LocalBounds.Min.Z, Volume.LocalBounds.Max.Z,
				Volume.LocalBounds.Min.X, Volume.LocalBounds.Min.Y,
				Volume.LocalBounds.Max.X, Volume.LocalBounds.Max.Y,
				*Volume.DerivationPath));
		}
	}
	bool bPassed = true;
	auto Check = [this, &Prefix, &bPassed](
		const TCHAR* Label, const bool bCondition)
	{
		bPassed &= TestTrue(
			*FString::Printf(TEXT("%s %s"), *Prefix, Label), bCondition);
	};

	Check(TEXT("publishes Stage 1 identity"),
		Result.Summary.GenerationStage
			== EABTSM73BeamC3GenerationStage::CoreAndShared);
	Check(TEXT("accepts the Stage 1 plan"), Plan.Summary.bAccepted);
	Check(TEXT("does not claim physical stability"),
		!Result.Summary.bPhysicalStabilityEvaluated
			&& !Plan.Summary.bPhysicalStabilityEvaluated);
	Check(TEXT("runs and accepts the static DAG"),
		Result.Summary.bStageStaticDAGEvaluated
			&& Result.StaticDAG.Summary.bAccepted);
	Check(TEXT("emits explicit cores"), !Plan.CoreCells.IsEmpty());
	Check(TEXT("does not publish future shell band schedules"),
		Algo::AllOf(Plan.Components,
			[](const ABTSM73BeamC3V3::FComponentPlan& Component)
			{
				return Component.BandBaseCourseIndices.IsEmpty();
			}));
	Check(TEXT("grounds every explicit core"),
		Plan.Summary.ExplicitCoreCellCount > 0
			&& Plan.Summary.GroundedCoreCellCount
				== Plan.Summary.ExplicitCoreCellCount
			&& Plan.Summary.SuspendedCoreCount == 0);
	Check(TEXT("binds every high projection to one child"),
		Plan.Summary.BoundHighProjectionRegionCount
			== Plan.Summary.HighProjectionRegionCount
			&& Plan.Summary.TowerChildCoreCellCount
				== Plan.Summary.HighProjectionRegionCount);
	Check(TEXT("closes every independent terminal branch demand"),
		Plan.Summary.RequiredTerminalBranchCount
			== Plan.Summary.HighProjectionRegionCount
			&& Plan.Summary.BoundTerminalBranchCount
				== Plan.Summary.RequiredTerminalBranchCount);
	for (const ABTSM73BeamC3V3::FHighProjectionRegionPlan& Region
		: Plan.HighProjectionRegions)
	{
		const ABTSM73BeamC3V3::FCoreCellPlan* Child =
			SkeletonV3TestFindCore(Plan, Region.BoundCoreCellId);
		Check(TEXT("terminal branch publishes an independent demand and full-height child"),
			Region.EntryBounds.IsValid && Region.TerminalBounds.IsValid
				&& Region.TerminalSliceCourse >= Region.PodiumTopCourse
				&& Region.RequiredTopCourse >= Region.TerminalSliceCourse
				&& Child != nullptr
				&& Child->TopCourseIndex == Region.RequiredTopCourse);
	}
	Check(TEXT("emits no Stage 2+ shell or roof"),
		Plan.Summary.BuildingGroupCount == 0
			&& Plan.Summary.ShellMemberCount == 0
			&& Plan.Summary.RoofMemberCount == 0);
	Check(TEXT("keeps every member within 720 cm"),
		Plan.Summary.MaximumMemberLengthCM
			<= 720.0f + SkeletonV3TestGeometryToleranceCM);
	Check(TEXT("has no geometry or seat violation"),
		Plan.Summary.EnvelopeViolationCount == 0
			&& Plan.Summary.ProtectedVoidViolationCount == 0
			&& Plan.Summary.PenetrationCount == 0
			&& Plan.Summary.SeatMismatchCount == 0);
	Check(TEXT("plans and emits the same member set"),
		Plan.Summary.PlannedMemberCount > 0
			&& Plan.Summary.PlannedMemberCount
				== Plan.Summary.EmittedMemberCount
			&& Plan.Summary.EmittedMemberCount == Plan.Members.Num());
	Check(TEXT("publishes Stage 1 identities"),
		Plan.Summary.EnvelopeHash != 0
			&& Plan.Summary.CorePlanHash != 0
			&& Plan.Summary.FinalGeometryHash != 0);
	const bool bHasTowerChildDemands =
		Plan.Summary.SemanticTerminalDemandCount > 0;
	Check(TEXT("publishes the independent semantic support-demand graph"),
		Plan.Summary.SemanticSupportNodeCount
			== Plan.SemanticSupportVolumeNodes.Num()
			&& Plan.Summary.SemanticSupportLedgerCount
				== Plan.SemanticSupportMergeLedger.Num()
			&& Plan.Summary.SemanticSupportCourseCount
				== Plan.SemanticSupportCourseOccupancies.Num()
			&& Plan.Summary.SemanticTerminalDemandCount
				== Plan.SemanticTerminalDemands.Num()
			&& Plan.Summary.SemanticSupportNodeCount > 0
			&& Plan.Summary.SemanticSupportCourseCount > 0
			&& Plan.Summary.SemanticTerminalLoadBranchCount
				== Plan.Summary.SemanticTerminalDemandCount
			&& Plan.Summary.UnrepresentedSemanticTerminalLoadBranchCount == 0
			&& Plan.Summary.SemanticSupportDemandHash != 0);
	Check(TEXT("partitions semantic ground occupancy into support provinces"),
		Plan.Summary.SupportProvinceCount == Plan.SupportProvinces.Num()
			&& (bHasTowerChildDemands
				? Plan.Summary.SupportProvinceCount > 0
				: Plan.Summary.SupportProvinceCount == 0)
			&& Plan.Summary.BoundSupportProvinceCount
				== Plan.Summary.SupportProvinceCount
			&& (bHasTowerChildDemands
				? Plan.Summary.DistinctProvinceGroundCoreCount > 0
					&& Plan.Summary.SupportProvinceGroundCellCount > 0
				: Plan.Summary.DistinctProvinceGroundCoreCount == 0
					&& Plan.Summary.SupportProvinceGroundCellCount == 0)
			&& Plan.Summary.SupportProvinceHash != 0
			&& Plan.Summary.SupportProvinceMainBindingHash != 0
			&& Algo::AllOf(Plan.SupportProvinces,
				[&Plan](const ABTSM73BeamC3V3::FSupportProvinceDiagnostic& Province)
				{
					return Plan.CoreCells.IsValidIndex(
						Province.BoundGroundCoreCellId)
						&& (!Province.bBoundToPodiumMain
							|| Province.bAnchorCoveredByBoundCore);
				})
			&& Algo::AllOf(Plan.SemanticTerminalDemands,
				[&Plan](const ABTSM73BeamC3V3::FSemanticTerminalDemandDiagnostic& Demand)
				{
					return Plan.SupportProvinces.IsValidIndex(Demand.SupportProvinceId)
						&& Plan.SupportProvinces[Demand.SupportProvinceId]
							.DemandIds.Contains(Demand.DemandId);
				}));
	int32 GroundCourseCellCount = 0;
	for (const ABTSM73BeamC3V3::FSemanticSupportCourseOccupancyDiagnostic& Occupancy
		: Plan.SemanticSupportCourseOccupancies)
	{
		GroundCourseCellCount += Occupancy.CourseIndex == 0
			? Occupancy.OccupiedCellCount : 0;
	}
	Check(TEXT("support provinces exactly partition course-zero occupancy"),
		!bHasTowerChildDemands
			|| Plan.Summary.SupportProvinceGroundCellCount == GroundCourseCellCount);
	TSet<int32> LocalPodiumRegionProvinceIds;
	bool bLocalPodiumCandidatesClose = true;
	for (const ABTSM73BeamC3V3::FLocalPodiumHeightRegionDiagnostic& Region
		: Plan.LocalPodiumHeightRegions)
	{
		bLocalPodiumCandidatesClose &= !Region.ProvinceIds.IsEmpty()
			&& Region.SelectedTopCourse >= Region.ActualPodiumTopCourse
			&& Region.bAppliedToProductionCoreHierarchy
			&& !Region.AppliedTowerChildCoreCellIds.IsEmpty();
		for (const int32 ProvinceId : Region.ProvinceIds)
		{
			const ABTSM73BeamC3V3::FSupportProvinceDiagnostic* Province =
				Plan.SupportProvinces.FindByPredicate(
					[ProvinceId](
						const ABTSM73BeamC3V3::FSupportProvinceDiagnostic& Entry)
					{
						return Entry.ProvinceId == ProvinceId;
					});
			bLocalPodiumCandidatesClose &=
				!LocalPodiumRegionProvinceIds.Contains(ProvinceId)
				&& Province != nullptr
				&& Region.StructuralPodiumMainCoreCellId != INDEX_NONE;
			LocalPodiumRegionProvinceIds.Add(ProvinceId);
			bLocalPodiumCandidatesClose &=
				Plan.LocalPodiumHeightCandidates.ContainsByPredicate(
					[ProvinceId, &Region](
						const ABTSM73BeamC3V3::FLocalPodiumHeightCandidateDiagnostic& Candidate)
					{
						return Candidate.ProvinceId == ProvinceId
							&& Candidate.StructuralPodiumMainCoreCellId
								== Region.StructuralPodiumMainCoreCellId
							&& Candidate.CandidateTopCourse
								== Region.SelectedTopCourse
							&& Candidate.bAccepted && Candidate.bSelected;
					});
		}
	}
	bLocalPodiumCandidatesClose &= Algo::AllOf(Plan.CoreCells,
		[&Plan](const ABTSM73BeamC3V3::FCoreCellPlan& Core)
		{
			if (Core.HierarchyRole
				!= ABTSM73BeamC3V3::ECoreHierarchyRole::TowerChild)
			{
				return Core.LocalPodiumHeightRegionId == INDEX_NONE
					&& Core.LocalPodiumTopCourseIndex == 0;
			}
			const ABTSM73BeamC3V3::FLocalPodiumHeightRegionDiagnostic* Region =
				Plan.LocalPodiumHeightRegions.FindByPredicate(
					[&Core](const ABTSM73BeamC3V3::FLocalPodiumHeightRegionDiagnostic& Entry)
					{
						return Entry.RegionId == Core.LocalPodiumHeightRegionId;
					});
			return Region != nullptr
				&& Region->AppliedTowerChildCoreCellIds.Contains(Core.CoreCellId)
				&& Region->StructuralPodiumMainCoreCellId
					== Core.PodiumMainCoreCellId
				&& Core.LocalPodiumTopCourseIndex == Region->SelectedTopCourse
				&& Core.LocalPodiumTopCourseIndex <= Core.TopCourseIndex - 2
				&& Core.LocalPodiumLegMemberIndices.Num()
					== Core.LocalPodiumTopCourseIndex * Core.RailCount
				&& Algo::AllOf(Core.LocalPodiumLegMemberIndices,
					[&Plan, &Core](const int32 MemberIndex)
					{
						return Plan.Members.IsValidIndex(MemberIndex)
							&& Core.MemberIndices.Contains(MemberIndex)
							&& Plan.Members[MemberIndex].CourseIndex
								< Core.LocalPodiumTopCourseIndex;
					});
		});
	bLocalPodiumCandidatesClose &= Algo::AllOf(
		Plan.LocalPodiumHeightCandidates,
		[](const ABTSM73BeamC3V3::FLocalPodiumHeightCandidateDiagnostic& Candidate)
		{
			return !Candidate.bAccepted
				|| (Candidate.CandidateTopCourse >= Candidate.ActualPodiumTopCourse
					&& Candidate.BoundGroundCoreCellId != INDEX_NONE
					&& Candidate.StructuralPodiumMainCoreCellId != INDEX_NONE
					&& Candidate.bSharedPodiumMainSemanticEvent
					&& Candidate.bCommonSemanticBoundary
					&& Candidate.bFullyOccupiedThroughCandidate
					&& Candidate.bCoversEveryDemandSeed
					&& Candidate.bSingleConnectedFootprint
					&& Candidate.PersistentCellCount > 0
					&& Candidate.bLeavesTwoChildCourses
					&& Candidate.bProtectedVoidClear);
		});
	Check(TEXT("publishes a complete local podium height selection plan"),
		Plan.Summary.LocalPodiumHeightCandidateCount
			== Plan.LocalPodiumHeightCandidates.Num()
			&& Plan.Summary.LocalPodiumHeightRegionCount
				== Plan.LocalPodiumHeightRegions.Num()
			&& Plan.Summary.LocalPodiumHeightPlanHash != 0
			&& (bHasTowerChildDemands
				? LocalPodiumRegionProvinceIds.Num()
					== Plan.SupportProvinces.Num()
					&& Plan.Summary.LocalPodiumHeightCandidateCount
						>= Plan.Summary.SupportProvinceCount
				: Plan.LocalPodiumHeightRegions.IsEmpty()
					&& Plan.LocalPodiumHeightCandidates.IsEmpty())
			&& bLocalPodiumCandidatesClose);
	Check(TEXT("applies every selected local podium region to production core legs"),
		bHasTowerChildDemands
			? Plan.Summary.AppliedLocalPodiumHeightRegionCount
					== Plan.Summary.LocalPodiumHeightRegionCount
				&& Plan.Summary.LocalPodiumLegMemberCount > 0
			: Plan.Summary.AppliedLocalPodiumHeightRegionCount == 0
				&& Plan.Summary.LocalPodiumLegMemberCount == 0);
	bool bRaisedMainReservationsClose = true;
	int32 CountedRaisedMembers = 0;
	for (const FABTSM73DAG5BV2RaisedMainReservation& Reservation
		: Plan.RaisedMainReservations)
	{
		const ABTSM73BeamC3V3::FCoreCellPlan* Main =
			SkeletonV3TestFindCore(Plan, Reservation.PodiumMainCoreCellId);
		int32 MinimumChildSplit = MAX_int32;
		int32 BoundChildCount = 0;
		int32 InfluencedChildCount = 0;
		for (const ABTSM73BeamC3V3::FCoreCellPlan& Child : Plan.CoreCells)
		{
			if (Child.HierarchyRole
					== ABTSM73BeamC3V3::ECoreHierarchyRole::TowerChild
				&& Reservation.InfluencedTowerChildCoreCellIds.Contains(
					Child.CoreCellId))
			{
				MinimumChildSplit = FMath::Min(
					MinimumChildSplit, Child.LocalPodiumTopCourseIndex);
				++InfluencedChildCount;
			}
			BoundChildCount += Main != nullptr
				&& Child.HierarchyRole
					== ABTSM73BeamC3V3::ECoreHierarchyRole::TowerChild
				&& Child.PodiumMainCoreCellId == Main->CoreCellId ? 1 : 0;
		}
		const double CoreSpanX = Reservation.CoreBounds.GetSize().X;
		const double CoreSpanY = Reservation.CoreBounds.GetSize().Y;
		bRaisedMainReservationsClose &= Main != nullptr
			&& Main->HierarchyRole
				== ABTSM73BeamC3V3::ECoreHierarchyRole::PodiumMain
			&& Main->ComponentId == Reservation.ComponentId
			&& BoundChildCount == Reservation.BoundTowerChildCoreCellIds.Num()
			&& InfluencedChildCount
				== Reservation.InfluencedTowerChildCoreCellIds.Num()
			// The second pass may expose more child trunk after the reserved Body is
			// inserted.  That is conservative: the raised main must not overrun the
			// final lowest split, but it need not equal a split recomputed from the
			// richer second-pass silhouette.
			&& MinimumChildSplit >= Reservation.ApprovedTopCourse
			&& Main->RaisedPodiumMainTopCourseIndex
				== Reservation.ApprovedTopCourse
			&& Main->TopCourseIndex == Reservation.ApprovedTopCourse
			&& Main->RaisedPodiumMainReservationBounds.Equals(
				Reservation.CoreBounds, KINDA_SMALL_NUMBER)
			&& FMath::Abs(CoreSpanX - CoreSpanY) <= 36.0 + KINDA_SMALL_NUMBER
			&& FMath::Abs(Reservation.ClearanceBounds.Min.X
				- (Reservation.CoreBounds.Min.X - 36.0)) <= KINDA_SMALL_NUMBER
			&& FMath::Abs(Reservation.ClearanceBounds.Max.X
				- (Reservation.CoreBounds.Max.X + 36.0)) <= KINDA_SMALL_NUMBER
			&& FMath::Abs(Reservation.ClearanceBounds.Min.Y
				- (Reservation.CoreBounds.Min.Y - 36.0)) <= KINDA_SMALL_NUMBER
			&& FMath::Abs(Reservation.ClearanceBounds.Max.Y
				- (Reservation.CoreBounds.Max.Y + 36.0)) <= KINDA_SMALL_NUMBER
			&& Main->MemberIndices.Num()
				== Main->TopCourseIndex * Main->RailCount;
		CountedRaisedMembers += Main != nullptr
			? (Reservation.ApprovedTopCourse - Reservation.OriginalTopCourse)
				* Main->RailCount : 0;
		AddInfo(FString::Printf(
			TEXT("Stage1RaisedMainReservation:%s:Component=%d:Main=%d:Original=%d:Approved=%d:Bound=%d/%d:Influenced=%d/%d:Foreign=%d:MinimumChildSplit=%d:MainTop=%d:RaisedTop=%d:Members=%d/%d:Summary=%d/%d:Core=%s:Clearance=%s"),
			*Prefix, Reservation.ComponentId,
			Reservation.PodiumMainCoreCellId,
			Reservation.OriginalTopCourse,
			Reservation.ApprovedTopCourse,
			BoundChildCount, Reservation.BoundTowerChildCoreCellIds.Num(),
			InfluencedChildCount,
			Reservation.InfluencedTowerChildCoreCellIds.Num(),
			Reservation.ForeignTowerChildCoreCellIds.Num(),
			MinimumChildSplit,
			Main != nullptr ? Main->TopCourseIndex : INDEX_NONE,
			Main != nullptr
				? Main->RaisedPodiumMainTopCourseIndex : INDEX_NONE,
			Main != nullptr ? Main->MemberIndices.Num() : INDEX_NONE,
			Main != nullptr ? Main->TopCourseIndex * Main->RailCount : INDEX_NONE,
			Plan.Summary.RaisedPodiumMainReservationCount,
			Plan.RaisedMainReservations.Num(),
			*Reservation.CoreBounds.ToString(),
			*Reservation.ClearanceBounds.ToString()));
	}
	Check(TEXT("materializes every raised-main reservation as a square core"),
		bRaisedMainReservationsClose
			&& Plan.Summary.RaisedPodiumMainReservationCount
				== Plan.RaisedMainReservations.Num()
			&& Plan.Summary.RaisedPodiumMainMemberCount
				== CountedRaisedMembers);
	for (const ABTSM73BeamC3V3::FLocalPodiumHeightRegionDiagnostic& Region
		: Plan.LocalPodiumHeightRegions)
	{
		AddInfo(FString::Printf(
			TEXT("Stage1LocalPodiumRegion:%s:Region=%d:Component=%d:StructuralMain=%d:Actual=%d:Selected=%d:Raised=%d:Applied=%d:Children=%s:Provinces=%s"),
			*Prefix, Region.RegionId, Region.ComponentId,
			Region.StructuralPodiumMainCoreCellId,
			Region.ActualPodiumTopCourse, Region.SelectedTopCourse,
			Region.bRaisesActualPodium ? 1 : 0,
			Region.bAppliedToProductionCoreHierarchy ? 1 : 0,
			*JoinIds(Region.AppliedTowerChildCoreCellIds),
			*JoinIds(Region.ProvinceIds)));
	}
	for (const ABTSM73BeamC3V3::FLocalPodiumHeightCandidateDiagnostic& Candidate
		: Plan.LocalPodiumHeightCandidates)
	{
		AddInfo(FString::Printf(
			TEXT("Stage1LocalPodiumCandidate:%s:Province=%d:Component=%d:GroundCore=%d:StructuralMain=%d:Course=%d:Actual=%d:Baseline=%d:OwnBoundary=%d:SharedEvent=%d:Boundary=%d:Continuous=%d:DemandSeeds=%d:Connected=%d:Cells=%d:SiblingGapUnits=%d:FirstRaisedCells=%d:RetainedPermille=%d:RetainsHalf=%d:BridgeWithin720=%d:BridgeVoidClear=%d:Clearance=%d:VoidClear=%d:Accepted=%d:Selected=%d:Reason=%s"),
			*Prefix, Candidate.ProvinceId, Candidate.ComponentId,
			Candidate.BoundGroundCoreCellId,
			Candidate.StructuralPodiumMainCoreCellId,
			Candidate.CandidateTopCourse, Candidate.ActualPodiumTopCourse,
			Candidate.bActualBaseline ? 1 : 0,
			Candidate.bOwnSemanticBoundary ? 1 : 0,
			Candidate.bSharedPodiumMainSemanticEvent ? 1 : 0,
			Candidate.bCommonSemanticBoundary ? 1 : 0,
			Candidate.bFullyOccupiedThroughCandidate ? 1 : 0,
			Candidate.bCoversEveryDemandSeed ? 1 : 0,
			Candidate.bSingleConnectedFootprint ? 1 : 0,
			Candidate.PersistentCellCount,
			Candidate.MinimumSiblingFootprintGapUnits,
			Candidate.FirstRaisedPersistentCellCount,
			Candidate.RetainedFootprintPermille,
			Candidate.bRetainsHalfFirstRaisedFootprint ? 1 : 0,
			Candidate.bSiblingBridgeWithinMemberSpan ? 1 : 0,
			Candidate.bSiblingBridgeVoidClear ? 1 : 0,
			Candidate.bLeavesTwoChildCourses ? 1 : 0,
			Candidate.bProtectedVoidClear ? 1 : 0,
			Candidate.bAccepted ? 1 : 0,
			Candidate.bSelected ? 1 : 0,
			*Candidate.DecisionReason));
	}
	bool bSemanticOccupancyCloses = true;
	for (const ABTSM73BeamC3V3::FSemanticSupportCourseOccupancyDiagnostic& Occupancy
		: Plan.SemanticSupportCourseOccupancies)
	{
		bSemanticOccupancyCloses &= Occupancy.SizeX > 0
			&& Occupancy.SizeY > 0
			&& Occupancy.OccupiedCellCount > 0
			&& Occupancy.OccupiedWords.Num()
				== (Occupancy.SizeX * Occupancy.SizeY + 63) / 64;
	}
	Check(TEXT("closes every semantic course occupancy bitset"),
		bSemanticOccupancyCloses);
	Check(TEXT("keeps shared endpoint contracts clean"),
		Plan.Summary.SharedCourseNonCoreEndpointViolationCount == 0
			&& Plan.Summary.SharedCourseBandViolationCount == 0);
	Check(TEXT("publishes one complete podium coverage audit per merge region"),
		Plan.Summary.PodiumCoverageDiagnosticCount
			== Plan.CoreMergeRegions.Num()
			&& Plan.PodiumCoverageDiagnostics.Num()
				== Plan.CoreMergeRegions.Num());
	bool bCoverageAccountingCloses = true;
	bool bEveryPodiumSupportAnchorCovered = true;
	for (const ABTSM73BeamC3V3::FPodiumCoreCoverageDiagnostic& Diagnostic
		: Plan.PodiumCoverageDiagnostics)
	{
		bCoverageAccountingCloses &= Diagnostic.TotalPodiumCellCount > 0
			&& Diagnostic.AnyCoreCoveredCellCount
				+ Diagnostic.AnyCoreUncoveredCellCount
				== Diagnostic.TotalPodiumCellCount
			&& Diagnostic.MainCoveredCellCount
				<= Diagnostic.TotalPodiumCellCount
			&& Diagnostic.AnyCoreCoveredCellCount
				<= Diagnostic.TotalPodiumCellCount
			&& FMath::IsFinite(Diagnostic.MainCoverageRatio)
			&& FMath::IsFinite(Diagnostic.AnyCoreCoverageRatio)
			&& FMath::IsFinite(Diagnostic.MaximumCorelessRadiusCM)
			&& FMath::IsFinite(
				Diagnostic.PodiumCentroidToNearestCoreCM);
		bEveryPodiumSupportAnchorCovered &= Diagnostic.PodiumMainCount == 0
			|| Diagnostic.bPodiumSupportAnchorCovered;
		AddInfo(FString::Printf(
			TEXT("Stage1PodiumCoverage:%s:Region=%d:Component=%d:Cells=%d:Main=%d:Any=%d:Uncovered=%d:MainRatio=%.6f:AnyRatio=%.6f:MaxHoleCM=%.3f:CentroidGapCM=%.3f:AnchorX=%.3f:AnchorY=%.3f:AnchorCovered=%d:MainCores=%d:AllCores=%d"),
			*Prefix, Diagnostic.RegionId, Diagnostic.ComponentId,
			Diagnostic.TotalPodiumCellCount,
			Diagnostic.MainCoveredCellCount,
			Diagnostic.AnyCoreCoveredCellCount,
			Diagnostic.AnyCoreUncoveredCellCount,
			Diagnostic.MainCoverageRatio,
			Diagnostic.AnyCoreCoverageRatio,
			Diagnostic.MaximumCorelessRadiusCM,
			Diagnostic.PodiumCentroidToNearestCoreCM,
			Diagnostic.PodiumSupportAnchorCM.X,
			Diagnostic.PodiumSupportAnchorCM.Y,
			Diagnostic.bPodiumSupportAnchorCovered ? 1 : 0,
			Diagnostic.PodiumMainCount, Diagnostic.GroundedCoreCount));
	}
	Check(TEXT("closes every podium coverage raster accounting identity"),
		bCoverageAccountingCloses);
	Check(TEXT("covers every occupied podium support anchor with a main core"),
		bEveryPodiumSupportAnchorCovered
			&& Plan.Summary.UncoveredPodiumSupportAnchorCount == 0);

	int32 ExpectedSourceCoverageCount = 0;
	int32 ExpectedMainSelectionCount = 0;
	int32 ExpectedMainOverlapCount = 0;
	int32 ExpectedProjectionSeedCount = 0;
	int32 ExpectedJointSelectionCount = 0;
	for (const ABTSM73BeamC3V3::FCoreMergeRegionPlan& Region
		: Plan.CoreMergeRegions)
	{
		ExpectedSourceCoverageCount += Region.GroundSourceBounds.Num();
		int32 RegionMainCount = 0;
		for (const ABTSM73BeamC3V3::FCoreCellPlan& Core : Plan.CoreCells)
		{
			RegionMainCount += Core.CoreMergeRegionId == Region.RegionId
				&& Core.HierarchyRole
					== ABTSM73BeamC3V3::ECoreHierarchyRole::PodiumMain ? 1 : 0;
		}
		ExpectedMainSelectionCount += RegionMainCount;
		ExpectedMainOverlapCount += RegionMainCount * (RegionMainCount - 1) / 2;
		ExpectedJointSelectionCount += RegionMainCount > 0 ? 1 : 0;
	}
	for (const ABTSM73BeamC3V3::FHighProjectionRegionPlan& Region
		: Plan.HighProjectionRegions)
	{
		ExpectedProjectionSeedCount += Region.SourceVolumeIds.Num();
	}
	Check(TEXT("publishes per-source podium coverage diagnostics"),
		Plan.PodiumSourceCoverageDiagnostics.Num()
			== ExpectedSourceCoverageCount);
	Check(TEXT("publishes one selection ledger row per podium main"),
		Plan.PodiumMainSelectionDiagnostics.Num()
			== ExpectedMainSelectionCount);
	Check(TEXT("publishes every podium-main overlap pair"),
		Plan.PodiumMainOverlapDiagnostics.Num()
			== ExpectedMainOverlapCount);
	Check(TEXT("binds every projection seed diagnostic to a region"),
		Plan.HighProjectionSeedDiagnostics.Num()
			== ExpectedProjectionSeedCount
		&& Algo::AllOf(Plan.HighProjectionSeedDiagnostics,
			[](const ABTSM73BeamC3V3::FHighProjectionSeedDiagnostic& Seed)
			{
				return Seed.RegionId != INDEX_NONE;
			}));
	Check(TEXT("publishes course-slice projection topology"),
		Plan.HighProjectionRegions.IsEmpty()
			? Plan.HighProjectionSliceComponentDiagnostics.IsEmpty()
				&& Plan.HighProjectionBranchBindingDiagnostics.IsEmpty()
			: !Plan.HighProjectionSliceComponentDiagnostics.IsEmpty()
				&& !Plan.HighProjectionBranchBindingDiagnostics.IsEmpty());
	Check(TEXT("publishes one full-height child ledger per upper projection"),
		Plan.FullHeightChildCandidateDiagnostics.Num()
			== Plan.HighProjectionRegions.Num());
	Check(TEXT("every coupled podium publishes a joint core selection ledger"),
		Plan.JointCoreSelectionDiagnostics.Num()
			== ExpectedJointSelectionCount);
	for (const ABTSM73BeamC3V3::FFullHeightChildCandidateDiagnostic& Diagnostic
		: Plan.FullHeightChildCandidateDiagnostics)
	{
		const ABTSM73BeamC3V3::FHighProjectionRegionPlan* Region =
			Plan.HighProjectionRegions.FindByPredicate(
				[&Diagnostic](
					const ABTSM73BeamC3V3::FHighProjectionRegionPlan& Candidate)
				{
					return Candidate.RegionId == Diagnostic.RegionId
						&& Candidate.ComponentId == Diagnostic.ComponentId;
				});
		const ABTSM73BeamC3V3::FCoreCellPlan* Child = Region == nullptr
			? nullptr : SkeletonV3TestFindCore(Plan, Region->BoundCoreCellId);
		const FString FullHeightLedgerLabel = FString::Printf(
			TEXT("full-height ledger resolves selected tower child Component=%d Region=%d Child=%d ChildTop=%d RequiredTop=%d WFCWitness=%d JointFeasible=%d"),
			Diagnostic.ComponentId, Diagnostic.RegionId,
			Region == nullptr ? INDEX_NONE : Region->BoundCoreCellId,
			Child == nullptr ? INDEX_NONE : Child->TopCourseIndex,
			Diagnostic.RequiredFullHeightCourse,
			Diagnostic.WFCFullHeightWitnessCount,
			Diagnostic.JointFeasibleCandidateCount);
		Check(*FullHeightLedgerLabel,
			Child != nullptr
				&& Child->TopCourseIndex == Diagnostic.RequiredFullHeightCourse
				&& Diagnostic.WFCFullHeightWitnessCount > 0
				&& Diagnostic.JointFeasibleCandidateCount > 0);
	}

	if (Case.ProfileId == TEXT("TipOver") && Case.Tier == 5)
	{
		for (const ABTSM73BeamC3V3::FFullHeightChildCandidateDiagnostic& Diagnostic
			: Plan.FullHeightChildCandidateDiagnostics)
		{
			AddInfo(FString::Printf(
				TEXT("TipOverE6FullHeightChild:Component=%d:Region=%d:PodiumTop=%d:RequiredTop=%d:Enumerated=%d:InvalidLattice=%d:Ground=%d:WFCEnvelope=%d:WFCFullHeight=%d:MainLane=%d:NoDirectMainCoupling=%d:SiblingLane=%d:Reservation=%d:JointFeasible=%d:Main=%d:Selected=%s:Reason=%s"),
				Diagnostic.ComponentId, Diagnostic.RegionId,
				Diagnostic.PodiumTopCourse,
				Diagnostic.RequiredFullHeightCourse,
				Diagnostic.EnumeratedFootprintCount,
				Diagnostic.InvalidLatticeRejectCount,
				Diagnostic.GroundSourceRejectCount,
				Diagnostic.WFCEnvelopeRejectCount,
				Diagnostic.WFCFullHeightWitnessCount,
				Diagnostic.MainLaneConflictRejectCount,
				Diagnostic.NoDirectMainCouplingCandidateCount,
				Diagnostic.SiblingLaneConflictRejectCount,
				Diagnostic.SharedReservationRejectCount,
				Diagnostic.JointFeasibleCandidateCount,
				Diagnostic.SelectedPodiumMainCoreCellId,
				*Diagnostic.SelectedChildBounds.ToString(),
				*Diagnostic.SelectionReason));
		}
		for (const ABTSM73BeamC3V3::FJointCoreSelectionDiagnostic& Diagnostic
			: Plan.JointCoreSelectionDiagnostics)
		{
			AddInfo(FString::Printf(
				TEXT("TipOverE6JointCoreSelection:Component=%d:Regions=%d:MainCandidates=%d:NoFullHeightCompatibility=%d:Visited=%d:Feasible=%d:SelectedMains=%d:AllFullHeight=%d:Reason=%s"),
				Diagnostic.ComponentId,
				Diagnostic.HighProjectionRegionCount,
				Diagnostic.PodiumMainCandidateCount,
				Diagnostic.MainCandidateWithoutFullHeightCompatibilityCount,
				Diagnostic.MainSelectionsVisited,
				Diagnostic.FullHeightFeasibleMainSelectionCount,
				Diagnostic.SelectedPodiumMainCount,
				Diagnostic.bEveryRegionHasFullHeightChild ? 1 : 0,
				*Diagnostic.SelectionReason));
		}
		for (const ABTSM73BeamC3V3::FHighProjectionBranchBindingDiagnostic& Branch
			: Plan.HighProjectionBranchBindingDiagnostics)
		{
			if (!Branch.bTerminal)
			{
				continue;
			}
			const bool bHasFullHeightChild = Plan.CoreCells.ContainsByPredicate(
				[&Branch](const ABTSM73BeamC3V3::FCoreCellPlan& Core)
				{
					const FVector Center = Core.LocalBounds.GetCenter();
					return Core.HierarchyRole
							== ABTSM73BeamC3V3::ECoreHierarchyRole::TowerChild
						&& Core.ComponentId == Branch.ComponentId
						&& Core.TopCourseIndex >= Branch.SliceCourse
						&& Center.X >= Branch.LocalBounds.Min.X
							- SkeletonV3TestGeometryToleranceCM
						&& Center.X <= Branch.LocalBounds.Max.X
							+ SkeletonV3TestGeometryToleranceCM
						&& Center.Y >= Branch.LocalBounds.Min.Y
							- SkeletonV3TestGeometryToleranceCM
						&& Center.Y <= Branch.LocalBounds.Max.Y
							+ SkeletonV3TestGeometryToleranceCM;
				});
			Check(TEXT("TipOver E6 terminal branch retains a full-height child"),
				bHasFullHeightChild);
		}
		for (const ABTSM73BeamC3V3::FCoreCellPlan& Core : Plan.CoreCells)
		{
			AddInfo(FString::Printf(
				TEXT("TipOverE6Core:Core=%d:Role=%d:Region=%d:Main=%d:TopCourse=%d:BodyTop=%d:XStations=%s:YStations=%s:Bounds=%s"),
				Core.CoreCellId, static_cast<int32>(Core.HierarchyRole),
				Core.HighProjectionRegionId, Core.PodiumMainCoreCellId,
				Core.TopCourseIndex, Core.BodyTopCourseIndex,
				*JoinIds(Core.XStations), *JoinIds(Core.YStations),
				*Core.LocalBounds.ToString()));
		}
		for (const FABTSM73DAG5BV2Volume& Volume : Result.Silhouette.Volumes)
		{
			AddInfo(FString::Printf(
				TEXT("TipOverE6Volume:Id=%d:Role=%d:Bounds=%s:Path=%s"),
				Volume.VolumeId, static_cast<int32>(Volume.Role),
				*Volume.LocalBounds.ToString(), *Volume.DerivationPath));
		}
		for (const ABTSM73BeamC3V3::FPodiumCoreCoverageDiagnostic& Diagnostic
			: Plan.PodiumCoverageDiagnostics)
		{
			AddInfo(FString::Printf(
				TEXT("TipOverE6PodiumBoundary:Region=%d:Podium=%s:MainUnion=%s:InsetNegX=%.3f:InsetPosX=%.3f:InsetNegY=%.3f:InsetPosY=%.3f"),
				Diagnostic.RegionId, *Diagnostic.PodiumBounds.ToString(),
				*Diagnostic.MainCoreUnionBounds.ToString(),
				Diagnostic.MainCoreBoundaryInsetsCM.X,
				Diagnostic.MainCoreBoundaryInsetsCM.Y,
				Diagnostic.MainCoreBoundaryInsetsCM.Z,
				Diagnostic.MainCoreBoundaryInsetsCM.W));
		}
		for (const ABTSM73BeamC3V3::FPodiumSourceCoverageDiagnostic& Diagnostic
			: Plan.PodiumSourceCoverageDiagnostics)
		{
			AddInfo(FString::Printf(
				TEXT("TipOverE6PodiumSource:Region=%d:Source=%d:OriginalGround=%d:Cells=%d:Main=%d:Any=%d:Uncovered=%d:Bounds=%s"),
				Diagnostic.RegionId, Diagnostic.SourceVolumeId,
				Diagnostic.OriginalGroundComponentId,
				Diagnostic.TotalCellCount,
				Diagnostic.MainCoveredCellCount,
				Diagnostic.AnyCoreCoveredCellCount,
				Diagnostic.UncoveredCellCount,
				*Diagnostic.SourceBounds.ToString()));
		}
		for (const ABTSM73BeamC3V3::FPodiumUncoveredIslandDiagnostic& Island
			: Plan.PodiumUncoveredIslandDiagnostics)
		{
			AddInfo(FString::Printf(
				TEXT("TipOverE6PodiumHole:Region=%d:Island=%d:Cells=%d:DirectionMask=%u:MinXY=%.3f,%.3f:MaxXY=%.3f,%.3f"),
				Island.RegionId, Island.IslandId, Island.CellCount,
				static_cast<uint32>(Island.BoundaryDirectionMask),
				Island.Bounds.Min.X, Island.Bounds.Min.Y,
				Island.Bounds.Max.X, Island.Bounds.Max.Y));
		}
		for (const ABTSM73BeamC3V3::FPodiumMainSelectionDiagnostic& Selection
			: Plan.PodiumMainSelectionDiagnostics)
		{
			AddInfo(FString::Printf(
				TEXT("TipOverE6MainSelection:Region=%d:Core=%d:Bounds=%s:HighRegions=%s:GroundSources=%s:PodiumCells=%d:Anchor=%d:Reason=%s"),
				Selection.RegionId, Selection.CoreCellId,
				*Selection.CoreBounds.ToString(),
				*JoinIds(Selection.CoveredHighProjectionRegionIds),
				*JoinIds(Selection.CoveredGroundSourceVolumeIds),
				Selection.CoveredPodiumCellCount,
				Selection.bCoversPodiumSupportAnchor ? 1 : 0,
				*Selection.SelectionReason));
		}
		for (const ABTSM73BeamC3V3::FPodiumMainOverlapDiagnostic& Overlap
			: Plan.PodiumMainOverlapDiagnostics)
		{
			AddInfo(FString::Printf(
				TEXT("TipOverE6MainOverlap:Region=%d:Cores=%d,%d:X=%.3f:Y=%.3f:Area=%.3f"),
				Overlap.RegionId, Overlap.FirstCoreCellId,
				Overlap.SecondCoreCellId, Overlap.XOverlapCM,
				Overlap.YOverlapCM, Overlap.ProjectedOverlapAreaCM2));
		}
		for (const ABTSM73BeamC3V3::FHighProjectionSeedDiagnostic& Seed
			: Plan.HighProjectionSeedDiagnostics)
		{
			AddInfo(FString::Printf(
				TEXT("TipOverE6ProjectionSeed:Component=%d:Region=%d:Source=%d:Bounds=%s:Path=%s"),
				Seed.ComponentId, Seed.RegionId, Seed.SourceVolumeId,
				*Seed.SourceBounds.ToString(), *Seed.DerivationPath));
		}
		for (const ABTSM73BeamC3V3::FHighProjectionAdjacencyDiagnostic& Edge
			: Plan.HighProjectionAdjacencyDiagnostics)
		{
			AddInfo(FString::Printf(
				TEXT("TipOverE6ProjectionEdge:Component=%d:Sources=%d,%d:X=%.3f:Y=%.3f:Z=%.3f:Accepted=%d:Reason=%s"),
				Edge.ComponentId, Edge.FirstSourceVolumeId,
				Edge.SecondSourceVolumeId, Edge.XOverlapCM,
				Edge.YOverlapCM, Edge.ZOverlapCM,
				Edge.bAccepted ? 1 : 0, *Edge.Reason));
		}
		for (const ABTSM73BeamC3V3::FHighProjectionRegionPlan& Region
			: Plan.HighProjectionRegions)
		{
			AddInfo(FString::Printf(
				TEXT("TipOverE6ProjectionRegion:Region=%d:Component=%d:Sources=%s:PodiumTop=%d:Bounds=%s:Main=%d:Child=%d"),
				Region.RegionId, Region.ComponentId,
				*JoinIds(Region.SourceVolumeIds), Region.PodiumTopCourse,
				*Region.LocalBounds.ToString(),
				Region.BoundPodiumMainCoreCellId,
				Region.BoundCoreCellId));
		}
		for (const ABTSM73BeamC3V3::FHighProjectionSliceComponentDiagnostic& Slice
			: Plan.HighProjectionSliceComponentDiagnostics)
		{
			AddInfo(FString::Printf(
				TEXT("TipOverE6ProjectionSlice:Component=%d:Course=%d:SliceComponent=%d:Z=%.3f..%.3f:Sources=%s:Children=%s:Bounds=%s"),
				Slice.ComponentId, Slice.SliceCourse,
				Slice.SliceComponentId, Slice.SliceMinZCM,
				Slice.SliceMaxZCM, *JoinIds(Slice.SourceVolumeIds),
				*JoinIds(Slice.BoundTowerChildCoreCellIds),
				*Slice.LocalBounds.ToString()));
		}
		for (const ABTSM73BeamC3V3::FHighProjectionSplitDiagnostic& Split
			: Plan.HighProjectionSplitDiagnostics)
		{
			AddInfo(FString::Printf(
				TEXT("TipOverE6ProjectionSplit:Component=%d:LowerCourse=%d:LowerComponent=%d:UpperComponents=%s:UpperSources=%s"),
				Split.ComponentId, Split.LowerSliceCourse,
				Split.LowerSliceComponentId,
				*JoinIds(Split.UpperSliceComponentIds),
				*JoinIds(Split.UpperSourceVolumeIds)));
		}
		for (const ABTSM73BeamC3V3::FHighProjectionBranchBindingDiagnostic& Branch
			: Plan.HighProjectionBranchBindingDiagnostics)
		{
			AddInfo(FString::Printf(
				TEXT("TipOverE6BranchBinding:Component=%d:Course=%d:SliceComponent=%d:SplitChild=%d:Terminal=%d:Sources=%s:Children=%s:Bounds=%s"),
				Branch.ComponentId, Branch.SliceCourse,
				Branch.SliceComponentId, Branch.bSplitChild ? 1 : 0,
				Branch.bTerminal ? 1 : 0,
				*JoinIds(Branch.SourceVolumeIds),
				*JoinIds(Branch.BoundTowerChildCoreCellIds),
				*Branch.LocalBounds.ToString()));
		}
	}

	bool bSemanticDiagnosticsComplete = true;
	for (const FABTSM73DAG5BV2SemanticPodiumSelectionDiagnostic& Selection
		: Result.Silhouette.SemanticPodiumSelectionDiagnostics)
	{
		int32 CandidateCount = 0;
		int32 AcceptedCount = 0;
		for (const FABTSM73DAG5BV2SemanticPodiumCandidateDiagnostic& Candidate
			: Result.Silhouette.SemanticPodiumCandidateDiagnostics)
		{
			if (Candidate.Scope != Selection.Scope)
			{
				continue;
			}
			++CandidateCount;
			AcceptedCount += Candidate.bAccepted ? 1 : 0;
			bSemanticDiagnosticsComplete &= Candidate.bAccepted
				? Candidate.RejectReason.IsEmpty()
				: !Candidate.RejectReason.IsEmpty();
			AddInfo(FString::Printf(
				TEXT("Stage1SemanticCandidate:%s:Scope=%s:Seam=%.3f:Top=%.3f:SpanCap=%.3f:StartsCrown=%d:Accepted=%d:Reason=%s"),
				*Prefix, *Candidate.Scope, Candidate.SemanticSeamZ,
				Candidate.QuantizedTopZ, Candidate.ProtectedSpanTopZ,
				Candidate.bCandidateStartsCrown ? 1 : 0,
				Candidate.bAccepted ? 1 : 0, *Candidate.RejectReason));
		}
		bSemanticDiagnosticsComplete &= CandidateCount
			== Selection.CandidateCount;
		bSemanticDiagnosticsComplete &= Selection.bUsesSemanticSeam
			? AcceptedCount == 1 : AcceptedCount == 0;
		AddInfo(FString::Printf(
			TEXT("Stage1SemanticSelection:%s:Scope=%s:Applied=%d:Roots=%d:Candidates=%d:Legacy=%.3f:Seam=%.3f:Top=%.3f:UsesSeam=%d:StartsCrown=%d"),
			*Prefix, *Selection.Scope,
			Selection.bAppliedToEnvelope ? 1 : 0,
			Selection.RootCount, Selection.CandidateCount,
			Selection.LegacyTopZ, Selection.SelectedSemanticSeamZ,
			Selection.SelectedTopZ,
			Selection.bUsesSemanticSeam ? 1 : 0,
			Selection.bSelectedCandidateStartsCrown ? 1 : 0));
	}
	Check(TEXT("publishes complete semantic podium candidate decisions"),
		bSemanticDiagnosticsComplete);

	bool bAllStage1Kinds = true;
	bool bAllStage1Provenance = true;
	for (const ABTSM73BeamC3V3::FPlannedMember& Member : Plan.Members)
	{
		bAllStage1Kinds &=
			Member.SkeletonKind
				== ABTSM73BeamC3V3::ESkeletonMemberKind::CoreCourse
			|| Member.SkeletonKind
				== ABTSM73BeamC3V3::ESkeletonMemberKind::SharedCourse
			|| Member.SkeletonKind
				== ABTSM73BeamC3V3::ESkeletonMemberKind::BridgeDiaphragm;
		bAllStage1Provenance &= Member.ProducedStage
			== EABTSM73BeamC3GenerationStage::CoreAndShared;
	}
	Check(TEXT("contains only Stage 1 member kinds"), bAllStage1Kinds);
	Check(TEXT("records Stage 1 member provenance"), bAllStage1Provenance);

	if (Case.ProfileId == TEXT("SeamRelease") && Case.Tier == 5)
	{
		Check(TEXT("retains the required shared-course bridge"),
			!Plan.SharedCourseIntents.IsEmpty()
				&& Plan.Summary.SharedCourseCount > 0);
	}

	AddInfo(FString::Printf(
		TEXT("Stage1MatrixLeaf:%s:Volumes=%d:SupportNodes=%d:SemanticDemands=%d:MergeLedger=%d:SupportDemandHash=%lld:Provinces=%d:BoundProvinces=%d:ProvinceGroundCores=%d:ProvinceMainBindingHash=%lld:ProvinceCells=%d:ProvinceBoundaries=%d:ProvinceTies=%d:ProvinceFallbacks=%d:ProvinceHash=%lld:LocalPodiumCandidates=%d:RejectedLocalPodiumCandidates=%d:LocalPodiumRegions=%d:RaisedLocalPodiumRegions=%d:LocalPodiumHash=%lld:Cores=%d:Main=%d:Children=%d:High=%d:Bound=%d:TerminalRequired=%d:TerminalBound=%d:Shared=%d:Members=%d:MaxMember=%.3f:PodiumAudits=%d:MinMainCoverage=%.6f:MinAnyCoverage=%.6f:Uncovered=%d:MaxHoleCM=%.3f:MaxCentroidGapCM=%.3f:StaticDAG=%s:Physical=NotEvaluated"),
		*Prefix, Result.Silhouette.Volumes.Num(),
		Plan.Summary.SemanticSupportNodeCount,
		Plan.Summary.SemanticTerminalDemandCount,
		Plan.Summary.SemanticSupportLedgerCount,
		Plan.Summary.SemanticSupportDemandHash,
		Plan.Summary.SupportProvinceCount,
		Plan.Summary.BoundSupportProvinceCount,
		Plan.Summary.DistinctProvinceGroundCoreCount,
		Plan.Summary.SupportProvinceMainBindingHash,
		Plan.Summary.SupportProvinceGroundCellCount,
		Plan.Summary.SupportProvinceBoundaryCount,
		Plan.Summary.SupportProvinceTieBreakCellCount,
		Plan.Summary.SupportProvinceNearestSeedFallbackCount,
		Plan.Summary.SupportProvinceHash,
		Plan.Summary.LocalPodiumHeightCandidateCount,
		Plan.Summary.RejectedLocalPodiumHeightCandidateCount,
		Plan.Summary.LocalPodiumHeightRegionCount,
		Plan.Summary.RaisedLocalPodiumHeightRegionCount,
		Plan.Summary.LocalPodiumHeightPlanHash,
		Plan.CoreCells.Num(),
		Plan.Summary.PodiumMainCoreCellCount,
		Plan.Summary.TowerChildCoreCellCount,
		Plan.Summary.HighProjectionRegionCount,
		Plan.Summary.BoundHighProjectionRegionCount,
		Plan.Summary.RequiredTerminalBranchCount,
		Plan.Summary.BoundTerminalBranchCount,
		Plan.Summary.SharedCourseCount, Plan.Members.Num(),
		Plan.Summary.MaximumMemberLengthCM,
		Plan.Summary.PodiumCoverageDiagnosticCount,
		Plan.Summary.MinimumPodiumMainCoverageRatio,
		Plan.Summary.MinimumPodiumAnyCoreCoverageRatio,
		Plan.Summary.PodiumUncoveredCellCount,
		Plan.Summary.MaximumPodiumCorelessRadiusCM,
		Plan.Summary.MaximumPodiumCentroidToNearestCoreCM,
		Result.StaticDAG.Summary.bAccepted ? TEXT("Accepted") : TEXT("Rejected")));
	return bPassed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3StagePreviewFailureIdentityTest,
	"ABTS.M73DAG.BeamC3V3.Staged.StagePreviewFailureIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3StagePreviewFailureIdentityTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	FABTSM73BeamD1StagePreviewResult Result;
	FString Error;
	const bool bGenerated = FABTSM73BeamD1BrickCompiler().GenerateStagePreview(
		MakeD1Settings({TEXT("DropTrigger"), 5, 720000}),
		EABTSM73BeamC3GenerationStage::CoreAndShared, Result, Error);
	if (bGenerated)
	{
		return true;
	}

	TestTrue(TEXT("A failed Stage Preview retains a stable failure family"),
		Error.StartsWith(TEXT("BeamC3StagePreviewNoSemanticCandidate:")));
	TestTrue(TEXT("A failed Stage Preview identifies its last attempt"),
		Error.Contains(TEXT(":LastAttempt="))
			&& !Error.Contains(TEXT(":LastAttempt=-1")));
	TestTrue(TEXT("A failed Stage Preview identifies its last candidate seed"),
		Error.Contains(TEXT(":LastCandidateSeed="))
			&& !Error.Contains(TEXT(":LastCandidateSeed=-1")));
	TestTrue(TEXT("A failed Stage Preview identifies its rejecting gate"),
		Error.Contains(TEXT(":LastGate="))
			&& !Error.Contains(TEXT(":LastGate=None")));
	TestTrue(TEXT("A failed Stage Preview retains a non-empty rejection reason"),
		Error.Contains(TEXT(":LastReason="))
			&& !Error.EndsWith(TEXT(":LastReason="))
			&& !Error.EndsWith(TEXT(":LastReason=UnspecifiedFailure")));
	TestTrue(TEXT("A failed semantic search reports rejection gate counts"),
		Error.Contains(TEXT(":RejectCounts=Profile")));
	TestTrue(TEXT("A failed semantic search identifies its best semantic candidate"),
		Error.Contains(TEXT(":BestSemanticAttempt="))
			&& !Error.Contains(TEXT(":BestSemanticAttempt=-1"))
			&& Error.Contains(TEXT(":BestSemanticCandidateSeed="))
			&& !Error.Contains(TEXT(":BestSemanticCandidateSeed=-1"))
			&& Error.Contains(TEXT(":BestSemanticReason=SemanticVisualMilestoneNotMet:")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3StagedFutureStageFailClosedTest,
	"ABTS.M73DAG.BeamC3V3.Staged.FutureStageFailsClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3StagedFutureStageFailClosedTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	FABTSM73BeamD1StagePreviewResult Result;
	FString Error;
	TestFalse(TEXT("Stage 5 is not silently routed to the legacy full generator"),
		FABTSM73BeamD1BrickCompiler().GenerateStagePreview(
			MakeD1Settings({TEXT("ColumnBreak"), 0, 710000}),
			EABTSM73BeamC3GenerationStage::StaticDAG, Result, Error));
	TestTrue(TEXT("Unimplemented stage reports stable failure identity"),
		Error.StartsWith(TEXT("BeamC3StageNotImplemented")));
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FABTSM73BeamC3DemoStage4TopSurfaceIntentTest,
	"ABTS.M73DAG.BeamC3V3.Demo.Stage4TopSurfaceIntent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FABTSM73BeamC3DemoStage4TopSurfaceIntentTest::GetTests(
	TArray<FString>& OutBeautifiedNames,
	TArray<FString>& OutTestCommands) const
{
	for (const FABTSM73BeamDemoManifestEntry& Entry
		: FABTSM73BeamDemoManifest::GetEntries())
	{
		OutBeautifiedNames.Add(Entry.StableId.ToString());
		OutTestCommands.Add(FString::FromInt(static_cast<int32>(Entry.Id)));
	}
}

bool FABTSM73BeamC3DemoStage4TopSurfaceIntentTest::RunTest(
	const FString& Parameters)
{
	const EABTSM73BeamDemoBuilding Id =
		static_cast<EABTSM73BeamDemoBuilding>(FCString::Atoi(*Parameters));
	FABTSM73BeamDemoManifestEntry Entry;
	FString Error;
	if (!FABTSM73BeamDemoManifest::Resolve(Id, Entry, Error))
	{
		AddError(Error);
		return false;
	}
	FABTSM73BeamD1StagePreviewResult Result;
	const bool bGenerated = FABTSM73BeamD1BrickCompiler().GenerateStagePreview(
		Entry.Settings, EABTSM73BeamC3GenerationStage::FloorInfillRoof,
		Result, Error);
	TestTrue(*FString::Printf(TEXT("%s Stage 4 intent generates: %s"),
		*Entry.StableId.ToString(), *Error), bGenerated);
	if (!bGenerated)
	{
		return false;
	}
	const ABTSM73BeamC3V3::FPlan& Plan = Result.Skeleton.Plan;
	const ABTSM73BeamC3V3::FPlanSummary& Summary = Plan.Summary;
	int32 ExposedSetbackTopCount = 0;
	int32 DirectStackSeatCount = 0;
	int32 NonUnitizedMemberCount = 0;
	FString NonUnitizedMemberDiagnostics;
	for (int32 MemberIndex = 0; MemberIndex < Plan.Members.Num(); ++MemberIndex)
	{
		const ABTSM73BeamC3V3::FPlannedMember& Member = Plan.Members[MemberIndex];
		const double Length = FVector::Distance(Member.LocalStart, Member.LocalEnd);
		const double Units = Length / 36.0;
		const bool bAxisAligned =
			(Member.Axis == EABTSM73BeamAFrameAxis::X
				&& FMath::IsNearlyEqual(Member.LocalStart.Y, Member.LocalEnd.Y, 0.1)
				&& FMath::IsNearlyEqual(Member.LocalStart.Z, Member.LocalEnd.Z, 0.1))
			|| (Member.Axis == EABTSM73BeamAFrameAxis::Y
				&& FMath::IsNearlyEqual(Member.LocalStart.X, Member.LocalEnd.X, 0.1)
				&& FMath::IsNearlyEqual(Member.LocalStart.Z, Member.LocalEnd.Z, 0.1))
			|| (Member.Axis == EABTSM73BeamAFrameAxis::Z
				&& FMath::IsNearlyEqual(Member.LocalStart.X, Member.LocalEnd.X, 0.1)
				&& FMath::IsNearlyEqual(Member.LocalStart.Y, Member.LocalEnd.Y, 0.1));
		if (!bAxisAligned || !FMath::IsNearlyEqual(
			Units, FMath::RoundToDouble(Units), 0.1 / 36.0))
		{
			++NonUnitizedMemberCount;
			if (NonUnitizedMemberCount <= 8)
			{
				NonUnitizedMemberDiagnostics += FString::Printf(
					TEXT("|M%d:A%d:L%.3f:%s..%s"), MemberIndex,
					static_cast<int32>(Member.Axis), Length,
					*Member.LocalStart.ToCompactString(),
					*Member.LocalEnd.ToCompactString());
			}
		}
	}
	TestEqual(*FString::Printf(
		TEXT("Every Stage 1-4 member is a 36x36x36n brick%s"),
		*NonUnitizedMemberDiagnostics), NonUnitizedMemberCount, 0);
	TestEqual(TEXT("Every Stage-3 frame has exactly one downward intent"),
		Summary.Stage4TopSurfaceIntentCount, Plan.CommonExteriorFrames.Num());
	TestEqual(TEXT("The ownership buckets are mutually exhaustive"),
		Summary.Stage4TopSurfaceIntentCount,
		Summary.Stage4GroundSillIntentCount
			+ Summary.Stage4ResolvedTopSurfaceIntentCount
			+ Summary.Stage4UnresolvedIntentCount);
	TestEqual(TEXT("TopSurface ledger has no identity binding violation"),
		Summary.Stage4IntentBindingViolationCount, 0);
	TestEqual(TEXT("Every demo frame resolves before top-frame emission"),
		Summary.Stage4UnresolvedIntentCount, 0);
	TestNotEqual(TEXT("TopSurface ledger has a stable identity"),
		Summary.Stage4IntentHash, int64(0));
	TestEqual(TEXT("Every emitted top-frame run appends one Stage-4 member"),
		Plan.Members.FilterByPredicate(
			[](const ABTSM73BeamC3V3::FPlannedMember& Member)
			{
				return Member.ProducedStage
						== EABTSM73BeamC3GenerationStage::FloorInfillRoof
					&& Member.SkeletonKind
						== ABTSM73BeamC3V3::ESkeletonMemberKind::FloorCourse;
			}).Num(), Summary.Stage4EmittedTopFrameSegmentCount);
	TestEqual(TEXT("Every planned member is emitted to the preview IR"),
		Plan.Members.Num(), Summary.EmittedMemberCount);
	TestEqual(TEXT("Top-frame ledger count matches its summary"),
		Plan.TopSurfaceFrameSegments.Num(), Summary.Stage4TopFrameSegmentCount);
	TestEqual(TEXT("Top-frame rows are emitted or reuse existing material"),
		Summary.Stage4TopFrameSegmentCount,
		Summary.Stage4EmittedTopFrameSegmentCount
			+ Summary.Stage4ReusedTopFrameSegmentCount);
	TestEqual(TEXT("Top-frame ledger has no binding violation"),
		Summary.Stage4TopFrameBindingViolationCount, 0);
	TestEqual(TEXT("Top-frame emission has no unresolved collision"),
		Summary.Stage4TopFrameConflictCount, 0);
	TestNotEqual(TEXT("Top-frame plan has a stable identity"),
		Summary.Stage4TopFrameHash, int64(0));
	TestEqual(TEXT("Facade-to-Top ledger count matches its summary"),
		Plan.FacadeToTopConnections.Num(),
		Summary.Stage4FacadeToTopConnectionCount);
	TestEqual(TEXT("Facade-to-Top has no binding violation"),
		Summary.Stage4FacadeToTopBindingViolationCount, 0);
	TestEqual(TEXT("Facade-to-Top has no unresolved conflict"),
		Summary.Stage4FacadeToTopConflictCount, 0);
	TestNotEqual(TEXT("Facade-to-Top plan has a stable identity"),
		Summary.Stage4FacadeToTopHash, int64(0));
	TestEqual(TEXT("Every deferred Stage-3 junction is replaced in Stage 4"),
		Summary.Stage4ResolvedDeferredJunctionCount,
		Plan.TopSurfaceFrameDeferredJunctions.Num());
	for (const ABTSM73BeamC3V3::FTopSurfaceIntentPlan& Intent
		: Plan.TopSurfaceIntents)
	{
		if (Intent.Intent
			== ABTSM73BeamC3V3::EFacadeDownwardIntent::TopSurface)
		{
			ExposedSetbackTopCount += Intent.TopSurfaceAuthority
				== ABTSM73BeamC3V3::ETopSurfaceAuthorityKind::ExposedSetbackTop ? 1 : 0;
			DirectStackSeatCount += Intent.TopSurfaceAuthority
				== ABTSM73BeamC3V3::ETopSurfaceAuthorityKind::DirectStackSeat ? 1 : 0;
			TestTrue(TEXT("TopSurface intent binds a valid authority volume"),
				Intent.TargetEnvelopeVolumeId != INDEX_NONE
					&& Intent.TargetSurfaceCourseIndex != INDEX_NONE
					&& Intent.TargetSurfaceBounds.IsValid
					&& Intent.TargetSurfaceCourseIndex
						<= Intent.ExteriorFrameCourseIndex
					&& Intent.TargetSupportTangentMaximumCM
						- Intent.TargetSupportTangentMinimumCM >= 35.99);
		}
	}
	for (const ABTSM73BeamC3V3::FTopSurfaceFrameSegmentPlan& Segment
		: Plan.TopSurfaceFrameSegments)
	{
		TestTrue(TEXT("Every top-frame row resolves a real horizontal member"),
			Plan.Members.IsValidIndex(Segment.MemberIndex)
				&& Plan.Members[Segment.MemberIndex].Axis
					!= EABTSM73BeamAFrameAxis::Z
				&& !Segment.SourceIntentIds.IsEmpty());
		if (!Segment.bReusesExistingMember)
		{
			TestEqual(TEXT("Every emitted top-frame row owns a floor course"),
				Plan.Members[Segment.MemberIndex].SkeletonKind,
				ABTSM73BeamC3V3::ESkeletonMemberKind::FloorCourse);
		}
	}
	for (const ABTSM73BeamC3V3::FTopSurfaceIntentPlan& Intent
		: Plan.TopSurfaceIntents)
	{
		if (Intent.Intent
			!= ABTSM73BeamC3V3::EFacadeDownwardIntent::TopSurface)
		{
			continue;
		}
		TestTrue(TEXT("Every resolved TopSurface intent owns a frame row or deferred facade junction"),
			Plan.TopSurfaceFrameSegments.ContainsByPredicate(
				[&Intent](
					const ABTSM73BeamC3V3::FTopSurfaceFrameSegmentPlan& Segment)
				{
					return Segment.SourceIntentIds.Contains(Intent.IntentId);
				})
			|| Plan.TopSurfaceFrameDeferredJunctions.ContainsByPredicate(
				[&Intent](const ABTSM73BeamC3V3::
					FTopSurfaceFrameDeferredJunctionPlan& Junction)
				{
					return Junction.SourceIntentIds.Contains(Intent.IntentId);
				}));
	}
	for (const ABTSM73BeamC3V3::FTopSurfaceFrameDeferredJunctionPlan& Junction
		: Plan.TopSurfaceFrameDeferredJunctions)
	{
		TestTrue(TEXT("Deferred junction names a Stage-3 facade column"),
			Plan.Members.IsValidIndex(Junction.BlockingStage3ColumnMemberIndex)
				&& Plan.Members[Junction.BlockingStage3ColumnMemberIndex].Axis
					== EABTSM73BeamAFrameAxis::Z
				&& (Plan.Members[Junction.BlockingStage3ColumnMemberIndex].SkeletonKind
						== ABTSM73BeamC3V3::ESkeletonMemberKind::ExteriorPost
					|| Plan.Members[Junction.BlockingStage3ColumnMemberIndex].SkeletonKind
						== ABTSM73BeamC3V3::ESkeletonMemberKind::GroundExteriorPost));
	}
	for (const ABTSM73BeamC3V3::FFacadeToTopConnectionPlan& Connection
		: Plan.FacadeToTopConnections)
	{
		TestTrue(TEXT("Every closure names a real upper facade frame"),
			Plan.Members.IsValidIndex(Connection.UpperExteriorFrameMemberIndex)
				&& !Connection.SourceIntentIds.IsEmpty());
		for (const int32 MemberIndex : Connection.PostSegmentMemberIndices)
		{
			TestTrue(TEXT("Every closure post is a bounded Stage-4 Z member"),
				Plan.Members.IsValidIndex(MemberIndex)
					&& Plan.Members[MemberIndex].SkeletonKind
						== ABTSM73BeamC3V3::ESkeletonMemberKind::FacadeToTopPost
					&& Plan.Members[MemberIndex].Axis
						== EABTSM73BeamAFrameAxis::Z
					&& FVector::Distance(Plan.Members[MemberIndex].LocalStart,
						Plan.Members[MemberIndex].LocalEnd) <= 720.01);
		}
		for (const int32 MemberIndex
			: Connection.SuppressedStage3ColumnMemberIndices)
		{
			TestTrue(TEXT("Every replaced Stage-3 column is explicitly suppressed"),
				Plan.Members.IsValidIndex(MemberIndex)
					&& Plan.Members[MemberIndex].bSuppressedByStage4FacadeToTop);
		}
	}
	for (const ABTSM73BeamC3V3::FTopSurfaceIntentPlan& Intent
		: Plan.TopSurfaceIntents)
	{
		if (Intent.Intent == ABTSM73BeamC3V3::EFacadeDownwardIntent::TopSurface)
		{
			TestTrue(TEXT("Every TopSurface intent owns a Facade-to-Top closure"),
				Plan.FacadeToTopConnections.ContainsByPredicate(
					[&Intent](const ABTSM73BeamC3V3::
						FFacadeToTopConnectionPlan& Connection)
					{
						return Connection.SourceIntentIds.Contains(Intent.IntentId);
					}));
		}
	}
	TestEqual(TEXT("Deferred facade junction count matches its ledger"),
		Summary.Stage4DeferredFacadeColumnJunctionCount,
		Plan.TopSurfaceFrameDeferredJunctions.Num());
	TestEqual(TEXT("Every TopSurface has one explicit semantic subtype"),
		Summary.Stage4ResolvedTopSurfaceIntentCount,
		ExposedSetbackTopCount + DirectStackSeatCount);
	AddInfo(FString::Printf(
		TEXT("Stage4Intent Entry=%s Stage3=%lld IntentHash=%lld")
		TEXT(" Frames=%d Ground=%d Top=%d Setback=%d Stack=%d")
		TEXT(" Unresolved=%d TopFrames=%d Emitted=%d Reused=%d DeferredJunctions=%d Members=%d")
		TEXT(" FacadeToTop=%d Seats=%d Posts=%d Suppressed=%d ResolvedDeferred=%d")
		TEXT(" TimingMs=%.3f/%.3f/%.3f Physical=NotEvaluated"),
		*Entry.StableId.ToString(), Summary.Stage3PlanHash,
		Summary.Stage4IntentHash, Summary.Stage4TopSurfaceIntentCount,
		Summary.Stage4GroundSillIntentCount,
		Summary.Stage4ResolvedTopSurfaceIntentCount,
		ExposedSetbackTopCount, DirectStackSeatCount,
		Summary.Stage4UnresolvedIntentCount,
		Summary.Stage4TopFrameSegmentCount,
		Summary.Stage4EmittedTopFrameSegmentCount,
		Summary.Stage4ReusedTopFrameSegmentCount,
		Summary.Stage4DeferredFacadeColumnJunctionCount, Plan.Members.Num(),
		Summary.Stage4FacadeToTopConnectionCount,
		Summary.Stage4FacadeToTopSeatCount,
		Summary.Stage4FacadeToTopPostSegmentCount,
		Summary.Stage4SuppressedStage3ColumnMemberCount,
		Summary.Stage4ResolvedDeferredJunctionCount,
		Summary.Stage4IntentMilliseconds,
		Summary.Stage4TopFrameMilliseconds,
		Summary.Stage4FacadeToTopMilliseconds));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3StagedStage3BoundaryTest,
	"ABTS.M73DAG.BeamC3V3.Staged.Stage3Boundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3StagedStage3BoundaryTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	FABTSM73BeamD1StagePreviewResult Result;
	FString Error;
	const bool bGenerated = FABTSM73BeamD1BrickCompiler().GenerateStagePreview(
		MakeD1Settings({TEXT("TipOver"), 5, 750000}),
		EABTSM73BeamC3GenerationStage::CommonExteriorFrame, Result, Error);
	TestTrue(*FString::Printf(TEXT("Stage 3 generates: %s"), *Error), bGenerated);
	if (!bGenerated)
	{
		return false;
	}
	const ABTSM73BeamC3V3::FPlan& Plan = Result.Skeleton.Plan;
	TestTrue(TEXT("Stage 3 emits a non-empty common exterior frame"),
		Plan.Summary.CommonExteriorFrameCount > 0
			&& Plan.Summary.CommonExteriorFrameCount
				== Plan.CommonExteriorFrames.Num());
	TestEqual(TEXT("Every Stage-2 anchor band resolves one logical exterior frame"),
		Plan.Summary.CommonExteriorFrameCount,
		Plan.Summary.FacadeHeightAnchorBandCount);
	TestEqual(TEXT("Every logical exterior frame is emitted or reuses a core rail"),
		Plan.Summary.EmittedExteriorFrameCount
			+ Plan.Summary.ReusedExteriorFrameCount,
		Plan.Summary.CommonExteriorFrameCount);
	TestEqual(TEXT("No Stage-2 anchor is left without an exterior frame"),
		Plan.Summary.Stage3AnchorBandWithoutFrameCount, 0);
	TestEqual(TEXT("Every exterior frame has a downward sill/frame path"),
		Plan.Summary.Stage3FrameDownwardConnectionViolationCount, 0);
	TestTrue(TEXT("Stage 3 emits or reuses a grounded sill along the real contour"),
		Plan.Summary.GroundSillLoopCount > 0
			&& Plan.Summary.GroundSillSegmentCount
				== Plan.GroundSillSegments.Num()
			&& Plan.Summary.EmittedGroundSillSegmentCount
				+ Plan.Summary.ReusedGroundSillSegmentCount
				== Plan.Summary.GroundSillSegmentCount);
	TestTrue(TEXT("Stage 3 connects at least one lowest frame to its ground sill"),
		Plan.Summary.GroundExteriorColumnCount > 0
			&& Plan.Summary.GroundExteriorColumnSegmentCount
				>= Plan.Summary.GroundExteriorColumnCount);
	TestTrue(TEXT("Stage 3 emits columns between adjacent exterior frames"),
		Plan.Summary.ExteriorColumnCount > 0
			&& Plan.Summary.ExteriorColumnSegmentCount >=
				Plan.Summary.ExteriorColumnCount);
	TestEqual(TEXT("Every Stage-3 member has immutable Stage-2 provenance"),
		Plan.Summary.Stage3ParentViolationCount, 0);
	TestEqual(TEXT("Every frame is physically clamped by its Stage-2 band"),
		Plan.Summary.Stage3ClampViolationCount, 0);
	TestNotEqual(TEXT("Stage-3 input retains exact Stage-2 geometry identity"),
		Plan.Summary.Stage3InputGeometryHash, int64(0));
	TestNotEqual(TEXT("Stage-3 delta identity is published"),
		Plan.Summary.Stage3PlanHash, int64(0));
	for (const ABTSM73BeamC3V3::FGroundSillSegmentPlan& Sill
		: Plan.GroundSillSegments)
	{
		TestTrue(TEXT("Every ground-sill ledger row resolves to a real grounded member"),
			Plan.Members.IsValidIndex(Sill.MemberIndex)
				&& Plan.Members[Sill.MemberIndex].bRequiresGroundSeat);
		if (!Sill.bReusesGroundedCoreMember)
		{
			TestEqual(TEXT("Every emitted sill row owns a Stage-3 ground-sill member"),
				Plan.Members[Sill.MemberIndex].SkeletonKind,
				ABTSM73BeamC3V3::ESkeletonMemberKind::GroundSillCourse);
		}
	}
	for (const ABTSM73BeamC3V3::FFacadeHeightAnchorBand& Band
		: Plan.FacadeHeightAnchorBands)
	{
		TestEqual(TEXT("Every anchor-band id occurs exactly once in the exterior-frame ledger"),
			Plan.CommonExteriorFrames.FilterByPredicate(
				[&Band](const ABTSM73BeamC3V3::FCommonExteriorFramePlan& Frame)
				{
					return Frame.AnchorBandId == Band.AnchorBandId;
				}).Num(), 1);
	}
	for (const ABTSM73BeamC3V3::FPlannedMember& Member : Plan.Members)
	{
		if (Member.ProducedStage
			!= EABTSM73BeamC3GenerationStage::CommonExteriorFrame)
		{
			continue;
		}
		const bool bGroundSill = Member.SkeletonKind
			== ABTSM73BeamC3V3::ESkeletonMemberKind::GroundSillCourse;
		TestTrue(TEXT("Every Stage-3 member has either Stage-2 lineage or ground-contour authority"),
			bGroundSill || !Member.ParentStage2MemberIndices.IsEmpty());
		for (const int32 Parent : Member.ParentStage2MemberIndices)
		{
			TestTrue(TEXT("Every Stage-3 parent is a Stage-2 member"),
				Plan.Members.IsValidIndex(Parent)
					&& Plan.Members[Parent].ProducedStage
						== EABTSM73BeamC3GenerationStage::CouplingCourses);
		}
		if (Member.SkeletonKind
			== ABTSM73BeamC3V3::ESkeletonMemberKind::FacadeCourse)
		{
			TestTrue(TEXT("Exterior frames remain horizontal"),
				Member.Axis == EABTSM73BeamAFrameAxis::X
					|| Member.Axis == EABTSM73BeamAFrameAxis::Y);
		}
		else if (bGroundSill)
		{
			TestTrue(TEXT("Every emitted ground sill is a horizontal ground seat"),
				Member.bRequiresGroundSeat
					&& Member.Axis != EABTSM73BeamAFrameAxis::Z
					&& Member.CourseIndex == 0);
		}
		else
		{
			TestTrue(TEXT("Only inter-frame or ground exterior posts accompany Stage-3 frames"),
				Member.SkeletonKind
					== ABTSM73BeamC3V3::ESkeletonMemberKind::ExteriorPost
				|| Member.SkeletonKind
					== ABTSM73BeamC3V3::ESkeletonMemberKind::GroundExteriorPost);
			TestEqual(TEXT("Exterior columns are Z members"),
				Member.Axis, EABTSM73BeamAFrameAxis::Z);
			TestTrue(TEXT("Every Z segment remains within 720 cm"),
				FVector::Distance(Member.LocalStart, Member.LocalEnd)
					<= 720.0 + KINDA_SMALL_NUMBER);
		}
	}
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FABTSM73BeamC3StagedStage3MatrixTest,
	"ABTS.M73DAG.BeamC3V3.Staged.Stage3ExteriorNetworkMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FABTSM73BeamC3StagedStage3MatrixTest::GetTests(
	TArray<FString>& OutBeautifiedNames,
	TArray<FString>& OutTestCommands) const
{
	using namespace ABTSM73BeamC3V3Tests;
	for (const FStage1Case& Case : MatrixCases())
	{
		OutBeautifiedNames.Add(FString::Printf(
			TEXT("%s.E%d"), *Case.ProfileId.ToString(), Case.Tier + 1));
		OutTestCommands.Add(CaseCommand(Case));
	}
}

bool FABTSM73BeamC3StagedStage3MatrixTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	FStage1Case Case;
	if (!ParseCase(Parameters, Case))
	{
		AddError(FString::Printf(TEXT("Invalid staged Stage 3 matrix case: %s"),
			*Parameters));
		return false;
	}
	FABTSM73BeamD1StagePreviewResult Result;
	FString Error;
	const bool bGenerated = FABTSM73BeamD1BrickCompiler().GenerateStagePreview(
		MakeD1Settings(Case), EABTSM73BeamC3GenerationStage::CommonExteriorFrame,
		Result, Error);
	const FString Prefix = FString::Printf(
		TEXT("%s.E%d"), *Case.ProfileId.ToString(), Case.Tier + 1);
	TestTrue(*FString::Printf(TEXT("%s Stage 3 accepts: %s"), *Prefix, *Error),
		bGenerated);
	if (!bGenerated)
	{
		return false;
	}
	const ABTSM73BeamC3V3::FPlan& Plan = Result.Skeleton.Plan;
	TestTrue(*FString::Printf(TEXT("%s every anchor has one exterior frame"), *Prefix),
		Plan.Summary.CommonExteriorFrameCount
			== Plan.Summary.FacadeHeightAnchorBandCount
		&& Plan.Summary.Stage3AnchorBandWithoutFrameCount == 0);
	TestTrue(*FString::Printf(TEXT("%s every exterior frame has a downward path"),
		*Prefix), Plan.Summary.Stage3FrameDownwardConnectionViolationCount == 0);
	TestTrue(*FString::Printf(TEXT("%s Stage 3 remains penetration-free static DAG"),
		*Prefix), Result.Summary.bStageStaticDAGEvaluated
		&& Plan.Summary.PenetrationCount == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3StagedStage2BoundaryTest,
	"ABTS.M73DAG.BeamC3V3.Staged.Stage2Boundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3StagedStage2BoundaryTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	FABTSM73BeamD1StagePreviewResult Result;
	FString Error;
	const bool bGenerated = FABTSM73BeamD1BrickCompiler().GenerateStagePreview(
		MakeD1Settings({TEXT("ColumnBreak"), 0, 710000}),
		EABTSM73BeamC3GenerationStage::CouplingCourses, Result, Error);
	TestTrue(*FString::Printf(TEXT("Stage 2 generates: %s"), *Error), bGenerated);
	if (!bGenerated)
	{
		return false;
	}
	const ABTSM73BeamC3V3::FPlan& Plan = Result.Skeleton.Plan;
	TestTrue(TEXT("Stage 2 appends only complete double-course outward bands"),
		Plan.Summary.CouplingCourseCount > 0
			&& Plan.Summary.CouplingCourseCount
				== Plan.Summary.FacadeHeightAnchorBandCount * 2);
	TestEqual(TEXT("Every coupling retains an immutable Stage-1 parent"),
		Plan.Summary.CouplingParentViolationCount, 0);
	TestEqual(TEXT("Every coupling terminates on its declared WFC face"),
		Plan.Summary.CouplingEndpointViolationCount, 0);
	TestEqual(TEXT("Every coupling has a full block of net outward extension"),
		Plan.Summary.CouplingOutwardViolationCount, 0);
	TestEqual(TEXT("No coupling enters another core course-volume"),
		Plan.Summary.CouplingOtherCoreViolationCount, 0);
	TestEqual(TEXT("Every double-course band shares one facade endpoint"),
		Plan.Summary.CouplingBandEndpointViolationCount, 0);
	TestTrue(TEXT("Every double-course band independently closes on one facade"),
		Stage2DoubleCourseBandsShareFacadeEndpoint(Plan));
	TestTrue(TEXT("Stage 2 classifies at least one WFC-perimeter core"),
		Plan.Summary.PerimeterCoreCount > 0
			&& Plan.Summary.PerimeterCoreFaceCount >= Plan.Summary.PerimeterCoreCount);
	TestTrue(TEXT("Perimeter ledger retains only unoccluded course intervals"),
		Plan.Summary.PerimeterFaceExposureSpanCount > 0
			&& Stage2PerimeterExposureLedgerIsUnoccluded(Plan));
	TestTrue(TEXT("Facade partitions and height anchors close their identity ledger"),
		Plan.Summary.FacadePartitionBindingViolationCount == 0
			&& Stage2FacadePartitionLedgerIsClosed(Plan));
	TestTrue(TEXT("Stage 2 consumes the exact Stage-1 raised-podium facade envelope"),
		Stage2ResolvedFacadeEnvelopeIsClosed(Plan));
	TestNotEqual(TEXT("Stage-1 input identity is retained"),
		Plan.Summary.Stage1InputGeometryHash, int64(0));
	TestNotEqual(TEXT("Stage-2 delta identity is published"),
		Plan.Summary.Stage2PlanHash, int64(0));
	for (const ABTSM73BeamC3V3::FPlannedMember& Member : Plan.Members)
	{
		if (Member.ProducedStage == EABTSM73BeamC3GenerationStage::CouplingCourses)
		{
			TestEqual(TEXT("Stage 2 emits only through coupling courses"),
				Member.SkeletonKind,
				ABTSM73BeamC3V3::ESkeletonMemberKind::ThroughCourse);
			TestTrue(TEXT("Stage 2 never emits vertical posts"),
				Member.Axis != EABTSM73BeamAFrameAxis::Z);
			TestTrue(TEXT("Stage 2 member grows from an outer core half beyond its face"),
				IsStage2CouplingGeometricallyOutward(Plan, Member));
			TestTrue(TEXT("Stage 2 member does not route through another core volume"),
				Stage2CouplingAvoidsOtherCoreVolumes(Plan, Member));
		}
		else
		{
			TestTrue(TEXT("Stage 2 contains no Stage-3+ member"),
				static_cast<int32>(Member.ProducedStage)
					<= static_cast<int32>(EABTSM73BeamC3GenerationStage::CoreAndShared));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3StagedStage2RaisedPodiumFacadeEnvelopeTest,
	"ABTS.M73DAG.BeamC3V3.Staged.Stage2RaisedPodiumFacadeEnvelope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3StagedStage2RaisedPodiumFacadeEnvelopeTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	for (const int32 Seed : {730000, 750000})
	{
		FABTSM73BeamD1StagePreviewResult Result;
		FString Error;
		const bool bGenerated = FABTSM73BeamD1BrickCompiler().GenerateStagePreview(
			MakeD1Settings({TEXT("TipOver"), 5, Seed}),
			EABTSM73BeamC3GenerationStage::CouplingCourses, Result, Error);
		TestTrue(*FString::Printf(TEXT("TipOver E6 seed %d Stage 2 generates: %s"),
			Seed, *Error), bGenerated);
		if (!bGenerated)
		{
			continue;
		}
		const ABTSM73BeamC3V3::FPlan& Plan = Result.Skeleton.Plan;
		TestTrue(*FString::Printf(
			TEXT("TipOver E6 seed %d closes the final facade-envelope identity"), Seed),
			Stage2ResolvedFacadeEnvelopeIsClosed(Plan));
		bool bRaisedBodyContributesFacadeAboveOriginalPodium = false;
		for (const FABTSM73DAG5BV2RaisedMainReservation& Reservation
			: Plan.RaisedMainReservations)
		{
			for (const ABTSM73BeamC3V3::FFacadePartitionPlan& Partition
				: Plan.FacadePartitions)
			{
				bRaisedBodyContributesFacadeAboveOriginalPodium |=
					Partition.ComponentId == Reservation.ComponentId
					&& Partition.CourseSpans.ContainsByPredicate(
						[&Reservation](
							const ABTSM73BeamC3V3::FFacadePartitionCourseSpan& Span)
						{
							return Span.SourceVolumeId == Reservation.SourceVolumeId
								&& Span.CourseIndex
									>= Reservation.OriginalTopCourse;
						});
			}
		}
		TestTrue(*FString::Printf(
			TEXT("TipOver E6 seed %d raised podium contributes an actual facade span above its original top"),
			Seed), bRaisedBodyContributesFacadeAboveOriginalPodium);
	}
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FABTSM73BeamC3StagedStage2MatrixTest,
	"ABTS.M73DAG.BeamC3V3.Staged.Stage2CouplingMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FABTSM73BeamC3StagedStage2MatrixTest::GetTests(
	TArray<FString>& OutBeautifiedNames,
	TArray<FString>& OutTestCommands) const
{
	using namespace ABTSM73BeamC3V3Tests;
	for (const FStage1Case& Case : MatrixCases())
	{
		OutBeautifiedNames.Add(FString::Printf(
			TEXT("%s.E%d"), *Case.ProfileId.ToString(), Case.Tier + 1));
		OutTestCommands.Add(CaseCommand(Case));
	}
}

bool FABTSM73BeamC3StagedStage2MatrixTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	FStage1Case Case;
	if (!ParseCase(Parameters, Case))
	{
		AddError(FString::Printf(TEXT("Invalid staged Stage 2 matrix case: %s"),
			*Parameters));
		return false;
	}
	FABTSM73BeamD1StagePreviewResult Result;
	FString Error;
	const bool bGenerated = FABTSM73BeamD1BrickCompiler().GenerateStagePreview(
		MakeD1Settings(Case), EABTSM73BeamC3GenerationStage::CouplingCourses,
		Result, Error);
	const FString Prefix = FString::Printf(
		TEXT("%s.E%d"), *Case.ProfileId.ToString(), Case.Tier + 1);
	TestTrue(*FString::Printf(TEXT("%s Stage 2 accepts: %s"), *Prefix, *Error),
		bGenerated);
	if (!bGenerated)
	{
		return false;
	}
	const ABTSM73BeamC3V3::FPlan& Plan = Result.Skeleton.Plan;
	bool bPassed = true;
	auto Check = [this, &Prefix, &bPassed](const TCHAR* Label, const bool bCondition)
	{
		bPassed &= TestTrue(*FString::Printf(TEXT("%s %s"), *Prefix, Label),
			bCondition);
	};
	Check(TEXT("publishes Stage 2 identity"),
		Result.Summary.GenerationStage
			== EABTSM73BeamC3GenerationStage::CouplingCourses);
	Check(TEXT("retains a nonzero Stage-1 input hash"),
		Plan.Summary.Stage1InputGeometryHash != 0);
	Check(TEXT("publishes a distinct Stage-2 hash"),
		Plan.Summary.Stage2PlanHash != 0
			&& Plan.Summary.FinalGeometryHash
				!= Plan.Summary.Stage1InputGeometryHash);
	Check(TEXT("emits only complete outward double-course facade anchors"),
		Plan.Summary.CouplingCourseCount > 0
			&& Plan.Summary.CouplingCourseCount
				== Plan.Summary.FacadeHeightAnchorBandCount * 2);
	Check(TEXT("has no parent or endpoint provenance violation"),
		Plan.Summary.CouplingParentViolationCount == 0
			&& Plan.Summary.CouplingEndpointViolationCount == 0
			&& Plan.Summary.CouplingOutwardViolationCount == 0
			&& Plan.Summary.CouplingOtherCoreViolationCount == 0
			&& Plan.Summary.CouplingBandEndpointViolationCount == 0
			&& Stage2DoubleCourseBandsShareFacadeEndpoint(Plan));
	Check(TEXT("facade partitions and height anchors have exact bidirectional bindings"),
		Plan.Summary.FacadePartitionBindingViolationCount == 0
			&& Stage2FacadePartitionLedgerIsClosed(Plan));
	Check(TEXT("consumes the exact Stage-1 raised-podium facade envelope"),
		Stage2ResolvedFacadeEnvelopeIsClosed(Plan));
	Check(TEXT("publishes Stage-2-only phase timings"),
		Plan.Summary.bStage2TimingEvaluated
			&& Plan.Summary.Stage2FacadeEnvelopeMilliseconds >= 0.0
			&& Plan.Summary.Stage2FacadeExtractionMilliseconds >= 0.0
			&& Plan.Summary.Stage2AnchorSearchMilliseconds >= 0.0
			&& Plan.Summary.Stage2MemberEmissionMilliseconds >= 0.0
			&& Plan.Summary.Stage2StaticDAGMilliseconds >= 0.0
			&& Plan.Summary.Stage2TotalMilliseconds > 0.0);
	int32 CountedPerimeterCores = 0;
	int32 CountedPerimeterFaces = 0;
	for (const ABTSM73BeamC3V3::FCoreCellPlan& Core : Plan.CoreCells)
	{
		if (Core.PerimeterFaceMask != 0)
		{
			++CountedPerimeterCores;
			CountedPerimeterFaces += FMath::CountBits(
				static_cast<uint32>(Core.PerimeterFaceMask));
		}
	}
	Check(TEXT("perimeter-core ledger exactly matches classified core masks"),
		CountedPerimeterCores == Plan.Summary.PerimeterCoreCount
			&& CountedPerimeterFaces == Plan.Summary.PerimeterCoreFaceCount
			&& CountedPerimeterCores > 0
			&& Plan.Summary.PerimeterFaceExposureSpanCount > 0
			&& Stage2PerimeterExposureLedgerIsUnoccluded(Plan));
	Check(TEXT("every emitted coupling is independently outward"),
		Algo::AllOf(Plan.Members,
			[&Plan](const ABTSM73BeamC3V3::FPlannedMember& Member)
			{
				return Member.ProducedStage
					!= EABTSM73BeamC3GenerationStage::CouplingCourses
					|| (IsStage2CouplingGeometricallyOutward(Plan, Member)
						&& Stage2CouplingAvoidsOtherCoreVolumes(Plan, Member));
			}));
	Check(TEXT("emits no Stage-3 or later member"),
		Algo::AllOf(Plan.Members,
			[](const ABTSM73BeamC3V3::FPlannedMember& Member)
			{
				return static_cast<int32>(Member.ProducedStage)
					<= static_cast<int32>(
						EABTSM73BeamC3GenerationStage::CouplingCourses);
			}));
	Check(TEXT("keeps physical stability explicitly unevaluated"),
		!Result.Summary.bPhysicalStabilityEvaluated
			&& !Plan.Summary.bPhysicalStabilityEvaluated);
	return bPassed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3StagedCoreMergeRegionMultiRailTest,
	"ABTS.M73DAG.BeamC3V3.Staged.CoreMergeRegionMultiRail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3StagedCoreMergeRegionMultiRailTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	FABTSM73BeamD0ResolvedProfile Profile;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FString Error;
	if (!TestTrue(TEXT("Base profile resolves"), ResolveShape(
		{TEXT("DropTrigger"), 2, 740000}, Profile, Silhouette, Error)))
	{
		return false;
	}
	Silhouette.Volumes = {
		MakeBodyVolume(0, FVector(-216.0, -216.0, 0.0),
			FVector(0.0, 216.0, 360.0), TEXT("Building0/BaseA")),
		MakeBodyVolume(1, FVector(0.0, -216.0, 0.0),
			FVector(216.0, 216.0, 360.0), TEXT("Building0/BaseB"))};
	Silhouette.Summary.bAccepted = true;

	ABTSM73BeamC3V3::FGenerationResult Result;
	const bool bGenerated = FABTSM73BeamC3V3SkeletonFirstGenerator().GenerateStage1(
		Profile, Silhouette, Result, Error);
	TestTrue(*FString::Printf(TEXT("Adjacent-base Stage 1 generates: %s"), *Error),
		bGenerated);
	if (!bGenerated)
	{
		return false;
	}
	const ABTSM73BeamC3V3::FPlan& Plan = Result.Plan;
	TestEqual(TEXT("Two lateral WFC bases form one derived component"),
		Plan.Components.Num(), 1);
	TestEqual(TEXT("One merge region is published"), Plan.CoreMergeRegions.Num(), 1);
	TestEqual(TEXT("Merge region retains both source components"),
		Plan.CoreMergeRegions[0].SourceGroundComponentCount, 2);
	TestEqual(TEXT("Merge diagnostics retain both exact source bases"),
		Plan.CoreMergeRegions[0].GroundSourceBounds.Num(), 2);
	TestEqual(TEXT("One larger core replaces the two narrow cores"),
		Plan.CoreCells.Num(), 1);
	const ABTSM73BeamC3V3::FCoreCellPlan& Core = Plan.CoreCells[0];
	TestEqual(TEXT("Two-source merge selects three rails"), Core.RailCount, 3);
	TestEqual(TEXT("X stations match rail count"), Core.XStations.Num(), Core.RailCount);
	TestEqual(TEXT("Y stations match rail count"), Core.YStations.Num(), Core.RailCount);
	TestTrue(TEXT("Selected core footprint is square"),
		FMath::IsNearlyEqual(Core.LocalBounds.GetSize().X,
			Core.LocalBounds.GetSize().Y, 0.01));
	TestEqual(TEXT("Three-by-three full bearing patches are declared"),
		Plan.Summary.CoreBearingPatchCountPerInterface, 9);
	TestNotEqual(TEXT("Derived merge identity is published"),
		Plan.Summary.CoreMergeRegionHash, int64(0));
	TestTrue(TEXT("Every adjacent course has all full bearing faces"),
		SkeletonV3TestHasFullCoreBearingFaces(*this, Plan));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3StagedGroundedPodiumCoreHierarchyTest,
	"ABTS.M73DAG.BeamC3V3.Staged.GroundedPodiumCoreHierarchy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3StagedGroundedPodiumCoreHierarchyTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	FABTSM73BeamD0ResolvedProfile Profile;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FString Error;
	if (!TestTrue(TEXT("Base profile resolves"), ResolveShape(
		{TEXT("DropTrigger"), 2, 740000}, Profile, Silhouette, Error)))
	{
		return false;
	}
	Profile.VisualComplexity.MinimumBrickCount = 1;
	Profile.VisualComplexity.MaximumBrickCount = 100000;
	FABTSM73DAG5BV2Volume Podium = MakeBodyVolume(
		0, FVector(-720.0, -360.0, 0.0), FVector(720.0, 360.0, 360.0),
		TEXT("CoupledGround/Cell/0"));
	Podium.Role = EABTSM73DAG5BV2VolumeRole::Foundation;
	FABTSM73DAG5BV2Volume EastCrown = MakeBodyVolume(
		3, FVector(108.0, -360.0, 720.0), FVector(468.0, 360.0, 1080.0),
		TEXT("Building0/East/Crown"));
	EastCrown.Role = EABTSM73DAG5BV2VolumeRole::Crown;
	Silhouette.Volumes = {
		Podium,
		MakeBodyVolume(1, FVector(-468.0, -360.0, 360.0),
			FVector(-108.0, 360.0, 1080.0), TEXT("Building0/Tower/WestBody")),
		MakeBodyVolume(2, FVector(108.0, -360.0, 360.0),
			FVector(468.0, 360.0, 720.0), TEXT("Building0/Tower/EastBody")),
		EastCrown};
	Silhouette.Summary.bAccepted = true;

	ABTSM73BeamC3V3::FGenerationResult Result;
	const bool bGenerated = FABTSM73BeamC3V3SkeletonFirstGenerator().GenerateStage1(
		Profile, Silhouette, Result, Error);
	TestTrue(*FString::Printf(TEXT("Grounded hierarchy generates: %s"), *Error),
		bGenerated);
	if (!bGenerated)
	{
		return false;
	}
	const ABTSM73BeamC3V3::FPlan& Plan = Result.Plan;
	const ABTSM73BeamC3V3::FCoreCellPlan* Main = Plan.CoreCells.FindByPredicate(
		[](const ABTSM73BeamC3V3::FCoreCellPlan& Core)
		{
			return Core.HierarchyRole
				== ABTSM73BeamC3V3::ECoreHierarchyRole::PodiumMain;
		});
	TestNotNull(TEXT("One coupled-podium main core is explicit"), Main);
	TestEqual(TEXT("Two terminal support provinces receive two podium mains"),
		Plan.Summary.PodiumMainCoreCellCount, 2);
	TestEqual(TEXT("Both upper branches receive child cores"),
		Plan.Summary.TowerChildCoreCellCount, 2);
	TestEqual(TEXT("Same semantic path still yields two XY projection regions"),
		Plan.Summary.HighProjectionRegionCount, 2);
	TestEqual(TEXT("Every high projection region binds one child core"),
		Plan.Summary.BoundHighProjectionRegionCount,
		Plan.Summary.HighProjectionRegionCount);
	TestEqual(TEXT("Pure-data fixture publishes both full-height child ledgers"),
		Plan.FullHeightChildCandidateDiagnostics.Num(), 2);
	TestEqual(TEXT("Pure-data fixture publishes one bounded joint selection"),
		Plan.JointCoreSelectionDiagnostics.Num(), 1);
	if (Plan.JointCoreSelectionDiagnostics.Num() == 1)
	{
		const ABTSM73BeamC3V3::FJointCoreSelectionDiagnostic& Joint =
			Plan.JointCoreSelectionDiagnostics[0];
		TestEqual(TEXT("Joint selection emits every selected podium main"),
			Joint.SelectedPodiumMainCount,
			Plan.Summary.PodiumMainCoreCellCount);
		TestTrue(TEXT("Every semantic child demand has retained main coverage"),
			Joint.PodiumCoverageMainCandidateCountByRegion.Num()
				== Joint.HighProjectionRegionCount
			&& Algo::AllOf(Joint.PodiumCoverageMainCandidateCountByRegion,
				[](const int32 Count) { return Count > 0; }));
		TestTrue(TEXT("Every support province has retained main coverage"),
			Joint.MainCandidateCountBySupportProvince.Num()
				== Joint.SupportProvinceCount
			&& Algo::AllOf(Joint.MainCandidateCountBySupportProvince,
				[](const int32 Count) { return Count > 0; }));
		TestTrue(TEXT("Joint selection covers every support province"),
			Joint.bEverySupportProvinceCovered);
		TestTrue(TEXT("Spatial main selection then admits every full-height child"),
			Joint.bEveryRegionHasFullHeightChild
				&& Joint.FullHeightFeasibleMainSelectionCount > 0);
	}
	for (const ABTSM73BeamC3V3::FFullHeightChildCandidateDiagnostic& Diagnostic
		: Plan.FullHeightChildCandidateDiagnostics)
	{
		const ABTSM73BeamC3V3::FHighProjectionRegionPlan* Region =
			Plan.HighProjectionRegions.FindByPredicate(
				[&Diagnostic](
					const ABTSM73BeamC3V3::FHighProjectionRegionPlan& Candidate)
				{
					return Candidate.RegionId == Diagnostic.RegionId;
				});
		const ABTSM73BeamC3V3::FCoreCellPlan* Child = Region == nullptr
			? nullptr : SkeletonV3TestFindCore(Plan, Region->BoundCoreCellId);
		TestTrue(TEXT("Selected child reaches the pre-main WFC full height"),
			Child != nullptr
				&& Child->TopCourseIndex == Diagnostic.RequiredFullHeightCourse
				&& Diagnostic.WFCFullHeightWitnessCount > 0);
	}
	TestEqual(TEXT("Hierarchy publishes one composite lane group"),
		Plan.Summary.CompositeCoreGroupCount, 1);
	TestEqual(TEXT("Composite lane reservation has no same-course conflicts"),
		Plan.Summary.CompositeLaneConflictCount, 0);
	TestTrue(TEXT("Composite hierarchy has rebuilt cross-core bearing contacts"),
		Plan.Summary.CrossCoreBearingContactCount > 0);
	if (Main == nullptr)
	{
		return false;
	}
	for (const ABTSM73BeamC3V3::FCoreCellPlan& Core : Plan.CoreCells)
	{
		TestTrue(TEXT("Every hierarchy core is physically ground rooted"),
			FMath::IsNearlyEqual(Core.LocalBounds.Min.Z, 0.0, 0.01));
		TestEqual(TEXT("Core member count is exact for its own top course"),
			Core.MemberIndices.Num(), Core.TopCourseIndex * Core.RailCount);
		TestTrue(TEXT("Every core freezes a valid footprint-local Body boundary"),
			Core.BodyTopCourseIndex > 0
				&& Core.BodyTopCourseIndex <= Core.TopCourseIndex);
		TestEqual(TEXT("All hierarchy cores share the composite group identity"),
			Core.CompositeCoreGroupId, Main->CompositeCoreGroupId);
		if (Core.HierarchyRole == ABTSM73BeamC3V3::ECoreHierarchyRole::TowerChild)
		{
			TestTrue(TEXT("Child publishes a valid high projection binding"),
				Plan.HighProjectionRegions.IsValidIndex(
					Core.HighProjectionRegionId)
				&& Plan.HighProjectionRegions[Core.HighProjectionRegionId]
					.BoundCoreCellId == Core.CoreCellId);
			const ABTSM73BeamC3V3::FCoreCellPlan* ParentMain =
				SkeletonV3TestFindCore(Plan, Core.PodiumMainCoreCellId);
			TestNotNull(TEXT("Child publishes a valid podium-main lineage"),
				ParentMain);
			if (ParentMain == nullptr)
			{
				continue;
			}
			TestEqual(TEXT("Child parent has the podium-main role"),
				ParentMain->HierarchyRole,
				ABTSM73BeamC3V3::ECoreHierarchyRole::PodiumMain);
			TestTrue(TEXT("Child continues above the podium"),
				Core.TopCourseIndex > ParentMain->TopCourseIndex);
			TestTrue(TEXT("Main footprint is wider than each child on one axis"),
				ParentMain->LocalBounds.GetSize().X > Core.LocalBounds.GetSize().X
					|| ParentMain->LocalBounds.GetSize().Y
						> Core.LocalBounds.GetSize().Y);
			TestTrue(TEXT("Each child is statically coupled to the main core"),
				Core.CrossCoreBearingContactCount > 0);
		}
	}
	const ABTSM73BeamC3V3::FCoreCellPlan* EastChild =
		Plan.CoreCells.FindByPredicate(
			[](const ABTSM73BeamC3V3::FCoreCellPlan& Core)
			{
				return Core.HierarchyRole
						== ABTSM73BeamC3V3::ECoreHierarchyRole::TowerChild
					&& Core.LocalBounds.GetCenter().X > 0.0;
			});
	if (TestNotNull(TEXT("Asymmetric Crown branch has a child core"), EastChild))
	{
		TestTrue(TEXT("Child Body/Crown boundary is lower than component Body top"),
			EastChild->BodyTopCourseIndex < 30);
		bool bUsesCrownAtLocalBoundary = false;
		for (const int32 MemberIndex : EastChild->MemberIndices)
		{
			if (Plan.Members.IsValidIndex(MemberIndex))
			{
				const ABTSM73BeamC3V3::FPlannedMember& Member = Plan.Members[MemberIndex];
				bUsesCrownAtLocalBoundary |=
					Member.CourseIndex >= EastChild->BodyTopCourseIndex
					&& Member.SourceVolumeId == EastCrown.VolumeId;
			}
		}
		TestTrue(TEXT("Child legally changes to Crown at its own boundary"),
			bUsesCrownAtLocalBoundary);

		ABTSM73BeamC3V3::FPlan InvalidSourcePlan = Plan;
		const ABTSM73BeamC3V3::FCoreCellPlan& InvalidChild =
			InvalidSourcePlan.CoreCells[EastChild->CoreCellId];
		const int32 BodyMemberIndex = InvalidChild.MemberIndices[0];
		InvalidSourcePlan.Members[BodyMemberIndex].SourceVolumeId = EastCrown.VolumeId;
		FString InvalidSourceError;
		TestFalse(TEXT("Crown source before the per-core boundary fails closed"),
			FABTSM73BeamC3V3SkeletonFirstGenerator()
				.ValidateGeometryForTesting(
					Silhouette, InvalidSourcePlan, InvalidSourceError));
		TestTrue(*FString::Printf(
			TEXT("Early Crown mutation reports the source contract: %s"),
			*InvalidSourceError),
			InvalidSourceError.StartsWith(
				TEXT("BeamC3V3EnvelopeViolation:Reason=InvalidCoreCourseContract")));
	}
	TestTrue(TEXT("Hierarchy plan and emitted assembly are accepted"),
		Result.Plan.Summary.bAccepted && Result.Assembly.Summary.bAccepted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3StagedMultiPodiumMainCoverageTest,
	"ABTS.M73DAG.BeamC3V3.Staged.MultiPodiumMainCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3StagedMultiPodiumMainCoverageTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	FABTSM73BeamD0ResolvedProfile Profile;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FString Error;
	if (!TestTrue(TEXT("Base profile resolves"), ResolveShape(
		{TEXT("DropTrigger"), 2, 740000}, Profile, Silhouette, Error)))
	{
		return false;
	}
	Profile.VisualComplexity.MinimumBrickCount = 1;
	Profile.VisualComplexity.MaximumBrickCount = 100000;
	FABTSM73DAG5BV2Volume Podium = MakeBodyVolume(
		0, FVector(-1080.0, -360.0, 0.0), FVector(1080.0, 360.0, 360.0),
		TEXT("CoupledGround/Cell/0"));
	Podium.Role = EABTSM73DAG5BV2VolumeRole::Foundation;
	Silhouette.Volumes = {
		Podium,
		MakeBodyVolume(1, FVector(-900.0, -360.0, 360.0),
			FVector(-540.0, 360.0, 1080.0), TEXT("Building0/Tower/West")),
		MakeBodyVolume(2, FVector(-180.0, -360.0, 360.0),
			FVector(180.0, 360.0, 1080.0), TEXT("Building0/Tower/Centre")),
		MakeBodyVolume(3, FVector(540.0, -360.0, 360.0),
			FVector(900.0, 360.0, 1080.0), TEXT("Building0/Tower/East"))};
	Silhouette.Summary.bAccepted = true;

	ABTSM73BeamC3V3::FGenerationResult Result;
	const bool bGenerated = FABTSM73BeamC3V3SkeletonFirstGenerator().GenerateStage1(
		Profile, Silhouette, Result, Error);
	TestTrue(*FString::Printf(TEXT("Three-projection Stage 1 generates: %s"), *Error),
		bGenerated);
	if (!bGenerated)
	{
		return false;
	}

	const ABTSM73BeamC3V3::FPlan& Plan = Result.Plan;
	TestEqual(TEXT("Three disconnected upper footprints remain three regions"),
		Plan.Summary.HighProjectionRegionCount, 3);
	TestEqual(TEXT("All upper regions bind grounded child cores"),
		Plan.Summary.BoundHighProjectionRegionCount, 3);
	TestTrue(TEXT("The wide coupled base is covered by multiple podium mains"),
		Plan.Summary.PodiumMainCoreCellCount >= 2);
	TestEqual(TEXT("The fixture publishes one coupled-base coverage audit"),
		Plan.PodiumCoverageDiagnostics.Num(), 1);
	if (Plan.PodiumCoverageDiagnostics.Num() == 1)
	{
		TestTrue(TEXT("A podium main covers the occupied-base support anchor"),
			Plan.PodiumCoverageDiagnostics[0].bPodiumSupportAnchorCovered);
	}
	TestEqual(TEXT("No coupled-base support anchor remains uncovered"),
		Plan.Summary.UncoveredPodiumSupportAnchorCount, 0);
	for (const ABTSM73BeamC3V3::FHighProjectionRegionPlan& Region
		: Plan.HighProjectionRegions)
	{
		TestTrue(TEXT("Region publishes a valid direct podium main"),
			Plan.CoreCells.IsValidIndex(Region.BoundPodiumMainCoreCellId));
		TestTrue(TEXT("Region publishes a valid tower child"),
			Plan.CoreCells.IsValidIndex(Region.BoundCoreCellId));
		if (Plan.CoreCells.IsValidIndex(Region.BoundPodiumMainCoreCellId)
			&& Plan.CoreCells.IsValidIndex(Region.BoundCoreCellId))
		{
			const ABTSM73BeamC3V3::FCoreCellPlan& Main =
				Plan.CoreCells[Region.BoundPodiumMainCoreCellId];
			const ABTSM73BeamC3V3::FCoreCellPlan& Child =
				Plan.CoreCells[Region.BoundCoreCellId];
			TestEqual(TEXT("Direct parent is a podium main"), Main.HierarchyRole,
				ABTSM73BeamC3V3::ECoreHierarchyRole::PodiumMain);
			TestEqual(TEXT("Child and region agree on podium parent"),
				Child.PodiumMainCoreCellId, Main.CoreCellId);
			TestTrue(TEXT("Direct podium main is physically grounded"),
				FMath::IsNearlyEqual(Main.LocalBounds.Min.Z, 0.0, 0.01));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3StagedTerminalBranchDemandSplitTest,
	"ABTS.M73DAG.BeamC3V3.Staged.TerminalBranchDemandSplit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3StagedTerminalBranchDemandSplitTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	FABTSM73BeamD0ResolvedProfile Profile;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FString Error;
	if (!TestTrue(TEXT("Base profile resolves"), ResolveShape(
		{TEXT("DropTrigger"), 2, 740000}, Profile, Silhouette, Error)))
	{
		return false;
	}
	Profile.VisualComplexity.MinimumBrickCount = 1;
	Profile.VisualComplexity.MaximumBrickCount = 100000;
	FABTSM73DAG5BV2Volume Podium = MakeBodyVolume(
		0, FVector(-1080.0, -360.0, 0.0), FVector(1080.0, 360.0, 360.0),
		TEXT("CoupledGround/Cell/0"));
	Podium.Role = EABTSM73DAG5BV2VolumeRole::Foundation;
	Silhouette.Volumes = {
		Podium,
		MakeBodyVolume(1, FVector(-900.0, -360.0, 360.0),
			FVector(900.0, 360.0, 720.0), TEXT("Building0/SharedTrunk")),
		MakeBodyVolume(2, FVector(-900.0, -360.0, 720.0),
			FVector(-540.0, 360.0, 1080.0), TEXT("Building0/Tower/West")),
		MakeBodyVolume(3, FVector(-180.0, -360.0, 720.0),
			FVector(180.0, 360.0, 1080.0), TEXT("Building0/Tower/Centre")),
		MakeBodyVolume(4, FVector(540.0, -360.0, 720.0),
			FVector(900.0, 360.0, 1080.0), TEXT("Building0/Tower/East"))};
	Silhouette.Summary.bAccepted = true;

	ABTSM73BeamC3V3::FGenerationResult Result;
	const bool bGenerated = FABTSM73BeamC3V3SkeletonFirstGenerator().GenerateStage1(
		Profile, Silhouette, Result, Error);
	TestTrue(*FString::Printf(TEXT("Split terminal-demand fixture generates: %s"),
		*Error), bGenerated);
	if (!bGenerated)
	{
		return false;
	}
	const ABTSM73BeamC3V3::FPlan& Plan = Result.Plan;
	TestEqual(TEXT("One podium entry that splits aloft yields three demands"),
		Plan.Summary.RequiredTerminalBranchCount, 3);
	TestEqual(TEXT("All split terminal demands bind tower children"),
		Plan.Summary.BoundTerminalBranchCount, 3);
	TestEqual(TEXT("Every split terminal demand has one child"),
		Plan.Summary.TowerChildCoreCellCount, 3);
	for (const ABTSM73BeamC3V3::FHighProjectionRegionPlan& Region
		: Plan.HighProjectionRegions)
	{
		const ABTSM73BeamC3V3::FCoreCellPlan* Child =
			SkeletonV3TestFindCore(Plan, Region.BoundCoreCellId);
		TestTrue(TEXT("Split demand retains its common podium entry"),
			Region.EntryBounds.IsValid
				&& Region.EntryBounds.GetSize().X > Region.TerminalBounds.GetSize().X);
		TestTrue(TEXT("Split demand child reaches its independent terminal top"),
			Child != nullptr && Child->TopCourseIndex == Region.RequiredTopCourse);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3StagedTipOverE6Seed710000TerminalCoverageTest,
	"ABTS.M73DAG.BeamC3V3.Staged.TipOverE6Seed710000TerminalCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3StagedTipOverE6Seed710000TerminalCoverageTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	FABTSM73BeamD1StagePreviewResult Result;
	FString Error;
	const bool bGenerated = FABTSM73BeamD1BrickCompiler().GenerateStagePreview(
		MakeD1Settings({TEXT("TipOver"), 5, 710000}),
		EABTSM73BeamC3GenerationStage::CoreAndShared, Result, Error);
	TestTrue(*FString::Printf(TEXT("TipOver E6 seed 710000 accepts: %s"), *Error),
		bGenerated);
	if (!bGenerated)
	{
		return false;
	}
	const ABTSM73BeamC3V3::FPlan& Plan = Result.Skeleton.Plan;
	AddInfo(FString::Printf(
		TEXT("TipOverE6Seed710000:Main=%d:Children=%d:Regions=%d:TerminalRequired=%d:TerminalBound=%d:StaticDAG=%d"),
		Plan.Summary.PodiumMainCoreCellCount,
		Plan.Summary.TowerChildCoreCellCount,
		Plan.Summary.HighProjectionRegionCount,
		Plan.Summary.RequiredTerminalBranchCount,
		Plan.Summary.BoundTerminalBranchCount,
		Result.StaticDAG.Summary.bAccepted ? 1 : 0));
	TestEqual(TEXT("Seed 710000 retains all eight visible terminal branches"),
		Plan.Summary.RequiredTerminalBranchCount, 8);
	TestEqual(TEXT("Seed 710000 binds all eight terminal branches"),
		Plan.Summary.BoundTerminalBranchCount, 8);
	TestEqual(TEXT("Seed 710000 emits one child per terminal branch"),
		Plan.Summary.TowerChildCoreCellCount, 8);
	TestTrue(TEXT("Seed 710000 retains a separate right-side podium main"),
		Plan.Summary.PodiumMainCoreCellCount >= 3);
	TestTrue(TEXT("Seed 710000 passes the Stage 1 static DAG"),
		Result.Summary.bStageStaticDAGEvaluated
			&& Result.StaticDAG.Summary.bAccepted);
	TestTrue(TEXT("Seed 710000 publishes six-phase timing evidence"),
		Plan.Summary.bStage1TimingEvaluated
			&& Plan.Summary.TerminalDemandMilliseconds >= 0.0
			&& Plan.Summary.ChildCandidateMilliseconds >= 0.0
			&& Plan.Summary.PodiumMainCandidateMilliseconds >= 0.0
			&& Plan.Summary.JointSelectionMilliseconds >= 0.0
			&& Plan.Summary.MemberEmissionMilliseconds >= 0.0
			&& Plan.Summary.StaticDAGMilliseconds >= 0.0);
	TestTrue(TEXT("Seed 710000 remains inside the Stage 1 leaf budget"),
		Plan.Summary.bStage1WithinTimeBudget
			&& Plan.Summary.Stage1TotalMilliseconds
				<= Plan.Summary.Stage1TimeBudgetMilliseconds);
	for (const ABTSM73BeamC3V3::FHighProjectionRegionPlan& Region
		: Plan.HighProjectionRegions)
	{
		const ABTSM73BeamC3V3::FCoreCellPlan* Child =
			SkeletonV3TestFindCore(Plan, Region.BoundCoreCellId);
		AddInfo(FString::Printf(
			TEXT("TipOverE6Seed710000Region:Region=%d:RequiredTop=%d:Terminal=%d,%d:Main=%d:Child=%d:Entry=%s:TerminalBounds=%s"),
			Region.RegionId, Region.RequiredTopCourse,
			Region.TerminalSliceCourse, Region.TerminalSliceComponentId,
			Region.BoundPodiumMainCoreCellId, Region.BoundCoreCellId,
			*Region.EntryBounds.ToString(), *Region.TerminalBounds.ToString()));
		TestTrue(TEXT("Seed 710000 child reaches the independent branch top"),
			Child != nullptr && Child->TopCourseIndex == Region.RequiredTopCourse);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3StagedTipOverE6OptimizationSeedsTest,
	"ABTS.M73DAG.BeamC3V3.Staged.TipOverE6OptimizationSeeds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3StagedTipOverE6OptimizationSeedsTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	int32 TotalSingleShrinkChildren = 0;
	for (const int32 Seed : {710000, 730000, 750000})
	{
		FABTSM73BeamD1StagePreviewResult Result;
		FString Error;
		const bool bGenerated = FABTSM73BeamD1BrickCompiler().GenerateStagePreview(
			MakeD1Settings({TEXT("TipOver"), 5, Seed}),
			EABTSM73BeamC3GenerationStage::CoreAndShared, Result, Error);
		TestTrue(*FString::Printf(TEXT("TipOver E6 seed %d accepts: %s"),
			Seed, *Error), bGenerated);
		if (!bGenerated)
		{
			continue;
		}
		const ABTSM73BeamC3V3::FPlan& Plan = Result.Skeleton.Plan;
		for (const ABTSM73BeamC3V3::FFullHeightChildCandidateDiagnostic& Diagnostic
			: Plan.FullHeightChildCandidateDiagnostics)
		{
			AddInfo(FString::Printf(
				TEXT("TipOverE6ShrinkCandidateLedger:Seed=%d:Demand=%d:Projection=%d:Enumerated=%d:FullHeight=%d:Shrink=%d:BalancedShrink=%d:AdapterReject=%d:JointFeasible=%d:MainReject=%d:SiblingReject=%d:ReservationReject=%d:SelectedMain=%d:SelectedShrink=%d:Reason=%s:Lower=%s:Upper=%s"),
				Seed, Diagnostic.SemanticDemandId,
				Diagnostic.LocalProjectionIndex,
				Diagnostic.EnumeratedFootprintCount,
				Diagnostic.WFCFullHeightWitnessCount,
				Diagnostic.SingleShrinkWitnessCount,
				Diagnostic.BalancedSingleShrinkWitnessCount,
				Diagnostic.SingleShrinkAdapterRejectCount,
				Diagnostic.JointFeasibleCandidateCount,
				Diagnostic.MainLaneConflictRejectCount,
				Diagnostic.SiblingLaneConflictRejectCount,
				Diagnostic.SharedReservationRejectCount,
				Diagnostic.SelectedPodiumMainCoreCellId,
				Diagnostic.SelectedShrinkCourse,
				*Diagnostic.SelectionReason,
				*Diagnostic.SelectedChildBounds.ToString(),
				*Diagnostic.SelectedUpperChildBounds.ToString()));
		}
		int32 SeedSingleShrinkChildren = 0;
		for (const ABTSM73BeamC3V3::FCoreCellPlan& Child : Plan.CoreCells)
		{
			if (Child.HierarchyRole
					!= ABTSM73BeamC3V3::ECoreHierarchyRole::TowerChild
				|| Child.SingleShrinkCourseIndex <= 0)
			{
				continue;
			}
			++SeedSingleShrinkChildren;
			++TotalSingleShrinkChildren;
			const bool bSingleShrinkContractCloses =
				Child.SingleShrinkCourseIndex < Child.TopCourseIndex
				&& Child.UpperXStations.Num() == Child.RailCount
				&& Child.UpperYStations.Num() == Child.RailCount
				&& Child.UpperLocalBounds.IsValid
				&& Child.LocalBounds.ExpandBy(KINDA_SMALL_NUMBER).IsInsideOrOn(
					Child.UpperLocalBounds.Min)
				&& Child.LocalBounds.ExpandBy(KINDA_SMALL_NUMBER).IsInsideOrOn(
					Child.UpperLocalBounds.Max)
				&& (Child.UpperLocalBounds.GetSize().X
					< Child.LocalBounds.GetSize().X - KINDA_SMALL_NUMBER
					|| Child.UpperLocalBounds.GetSize().Y
						< Child.LocalBounds.GetSize().Y - KINDA_SMALL_NUMBER)
				&& FMath::Abs(Child.LocalBounds.GetSize().X
					- Child.LocalBounds.GetSize().Y)
					<= KINDA_SMALL_NUMBER
				&& FMath::Abs(Child.UpperLocalBounds.GetSize().X
					- Child.UpperLocalBounds.GetSize().Y)
					<= KINDA_SMALL_NUMBER
				&& FMath::Min(Child.LocalBounds.GetSize().X,
					Child.LocalBounds.GetSize().Y)
					> FMath::Min(Child.UpperLocalBounds.GetSize().X,
						Child.UpperLocalBounds.GetSize().Y)
						+ KINDA_SMALL_NUMBER;
			AddInfo(FString::Printf(
				TEXT("TipOverE6SingleShrink:Seed=%d:Child=%d:Main=%d:Shrink=%d:Top=%d:Lower=%s:Upper=%s"),
				Seed, Child.CoreCellId, Child.PodiumMainCoreCellId,
				Child.SingleShrinkCourseIndex, Child.TopCourseIndex,
				*Child.LocalBounds.ToString(),
				*Child.UpperLocalBounds.ToString()));
			TestTrue(*FString::Printf(
				TEXT("Seed %d child %d has exactly one contained WFC shrink"),
				Seed, Child.CoreCellId), bSingleShrinkContractCloses);
		}
		AddInfo(FString::Printf(
			TEXT("TipOverE6SingleShrinkSummary:Seed=%d:Children=%d"),
			Seed, SeedSingleShrinkChildren));
		if (Seed == 750000)
		{
			const ABTSM73BeamC3V3::FCoreCellPlan* FormerThinChild =
				Plan.CoreCells.FindByPredicate(
					[](const ABTSM73BeamC3V3::FCoreCellPlan& Child)
					{
						return Child.HierarchyRole
								== ABTSM73BeamC3V3::ECoreHierarchyRole::TowerChild
							&& Child.SemanticDemandId == 6;
					});
			TestTrue(TEXT("TipOver 750000 former 72 cm child receives a balanced thicker trunk"),
				FormerThinChild != nullptr
					&& FormerThinChild->SingleShrinkCourseIndex > 0
					&& FMath::Min(FormerThinChild->LocalBounds.GetSize().X,
						FormerThinChild->LocalBounds.GetSize().Y) > 36.0
					&& FMath::Abs(FormerThinChild->LocalBounds.GetSize().X
						- FormerThinChild->LocalBounds.GetSize().Y)
						<= KINDA_SMALL_NUMBER
					&& FMath::Abs(FormerThinChild->UpperLocalBounds.GetSize().X
						- FormerThinChild->UpperLocalBounds.GetSize().Y)
						<= KINDA_SMALL_NUMBER);
		}
		TestTrue(*FString::Printf(
			TEXT("Seed %d reserves at least one raised PodiumMain in Stage 0"), Seed),
			!Plan.RaisedMainReservations.IsEmpty());
		int32 CountedRaisedMembers = 0;
		for (const FABTSM73DAG5BV2RaisedMainReservation& Reservation
			: Plan.RaisedMainReservations)
		{
			const ABTSM73BeamC3V3::FCoreCellPlan* Main =
				SkeletonV3TestFindCore(Plan, Reservation.PodiumMainCoreCellId);
			int32 MinimumChildSplit = MAX_int32;
			int32 MinimumApprovedMainTop = MAX_int32;
			int32 BoundChildCount = 0;
			int32 InfluencedChildCount = 0;
			for (const ABTSM73BeamC3V3::FCoreCellPlan& Child : Plan.CoreCells)
			{
				if (Child.HierarchyRole
						== ABTSM73BeamC3V3::ECoreHierarchyRole::TowerChild
					&& Reservation.InfluencedTowerChildCoreCellIds.Contains(
						Child.CoreCellId))
				{
					MinimumChildSplit = FMath::Min(
						MinimumChildSplit, Child.LocalPodiumTopCourseIndex);
					constexpr int32 MinimumVisibleChildTrunkCourses = 4;
					const int32 ChildMainTopLimit =
						Child.SingleShrinkCourseIndex > 0
							? FMath::Max(Reservation.OriginalTopCourse,
								Child.SingleShrinkCourseIndex
									- MinimumVisibleChildTrunkCourses)
							: Child.LocalPodiumTopCourseIndex;
					MinimumApprovedMainTop = FMath::Min(
						MinimumApprovedMainTop,
						FMath::Min(Child.LocalPodiumTopCourseIndex,
							ChildMainTopLimit));
					++InfluencedChildCount;
				}
				BoundChildCount += Main != nullptr
					&& Child.HierarchyRole
						== ABTSM73BeamC3V3::ECoreHierarchyRole::TowerChild
					&& Child.PodiumMainCoreCellId == Main->CoreCellId ? 1 : 0;
			}
			const bool bReservationCloses = Main != nullptr
				&& Main->TopCourseIndex == Reservation.ApprovedTopCourse
				&& Main->RaisedPodiumMainTopCourseIndex
					== Reservation.ApprovedTopCourse
				&& Main->RaisedPodiumMainReservationBounds.Equals(
					Reservation.CoreBounds, KINDA_SMALL_NUMBER)
				&& MinimumApprovedMainTop == Reservation.ApprovedTopCourse
				&& BoundChildCount
					== Reservation.BoundTowerChildCoreCellIds.Num()
				&& InfluencedChildCount
					== Reservation.InfluencedTowerChildCoreCellIds.Num()
				&& FMath::Abs(Reservation.CoreBounds.GetSize().X
					- Reservation.CoreBounds.GetSize().Y)
					<= 36.0 + KINDA_SMALL_NUMBER
				&& FMath::Abs(Reservation.ClearanceBounds.GetSize().X
					- Reservation.CoreBounds.GetSize().X - 72.0)
					<= KINDA_SMALL_NUMBER
				&& FMath::Abs(Reservation.ClearanceBounds.GetSize().Y
					- Reservation.CoreBounds.GetSize().Y - 72.0)
					<= KINDA_SMALL_NUMBER;
			AddInfo(FString::Printf(
				TEXT("TipOverE6RaisedMain:Seed=%d:Main=%d:Reservation=%d->%d:FinalTop=%d:FinalRaisedTop=%d:MinimumInfluencedSplit=%d:ShrinkLimitedTop=%d:Bound=%d/%d:Influenced=%d/%d:Foreign=%s:CoreSpan=%.3fx%.3f:ClearanceSpan=%.3fx%.3f"),
				Seed, Reservation.PodiumMainCoreCellId,
				Reservation.OriginalTopCourse, Reservation.ApprovedTopCourse,
				Main != nullptr ? Main->TopCourseIndex : INDEX_NONE,
				Main != nullptr ? Main->RaisedPodiumMainTopCourseIndex : INDEX_NONE,
				MinimumChildSplit, MinimumApprovedMainTop, BoundChildCount,
				Reservation.BoundTowerChildCoreCellIds.Num(),
				InfluencedChildCount,
				Reservation.InfluencedTowerChildCoreCellIds.Num(),
				*JoinIds(Reservation.ForeignTowerChildCoreCellIds),
				Reservation.CoreBounds.GetSize().X,
				Reservation.CoreBounds.GetSize().Y,
				Reservation.ClearanceBounds.GetSize().X,
				Reservation.ClearanceBounds.GetSize().Y));
			TestTrue(*FString::Printf(
				TEXT("Seed %d raises main %d to the spatial child split limited by visible shrink trunk"),
				Seed, Reservation.PodiumMainCoreCellId), bReservationCloses);
			CountedRaisedMembers += Main != nullptr
				? (Reservation.ApprovedTopCourse
					- Reservation.OriginalTopCourse) * Main->RailCount : 0;
		}
		TestEqual(*FString::Printf(
			TEXT("Seed %d materializes every Stage-0 raised-main reservation"), Seed),
			Plan.Summary.RaisedPodiumMainReservationCount,
			Plan.RaisedMainReservations.Num());
		TestEqual(*FString::Printf(
			TEXT("Seed %d accounts every raised-main course member"), Seed),
			Plan.Summary.RaisedPodiumMainMemberCount, CountedRaisedMembers);
		if (Seed == 750000)
		{
			const bool bHasIncorrectRightRaisedMain =
				Plan.RaisedMainReservations.ContainsByPredicate(
					[](const FABTSM73DAG5BV2RaisedMainReservation& Reservation)
					{
						return Reservation.PodiumMainCoreCellId == 1
							|| Reservation.PodiumMainCoreCellId == 3;
					});
			TestFalse(TEXT("TipOver 750000 does not raise either overlapping east main above foreign children"),
				bHasIncorrectRightRaisedMain);
			TestEqual(TEXT("TipOver 750000 keeps only the two spatially legal raised mains"),
				Plan.RaisedMainReservations.Num(), 2);
			int32 RaisedSemanticVolumeCount = 0;
			bool bRaisedSemanticVolumesMatchOccupiedCore = true;
			for (const FABTSM73DAG5BV2Volume& Volume : Result.Silhouette.Volumes)
			{
				if (!Volume.DerivationPath.Contains(TEXT("/RaisedMainReservation/")))
				{
					continue;
				}
				++RaisedSemanticVolumeCount;
				const FABTSM73DAG5BV2RaisedMainReservation* Reservation =
					Plan.RaisedMainReservations.FindByPredicate(
						[&Volume](const FABTSM73DAG5BV2RaisedMainReservation& Candidate)
						{
							return Candidate.SourceVolumeId == Volume.VolumeId;
						});
				bRaisedSemanticVolumesMatchOccupiedCore &= Reservation != nullptr
					&& Volume.LocalBounds.Equals(
						Reservation->CoreBounds, KINDA_SMALL_NUMBER)
					&& !Volume.LocalBounds.Equals(
						Reservation->ClearanceBounds, KINDA_SMALL_NUMBER);
			}
			TestEqual(TEXT("TipOver 750000 publishes one occupied semantic Body per legal raised main"),
				RaisedSemanticVolumeCount, Plan.RaisedMainReservations.Num());
			TestTrue(TEXT("TipOver 750000 keeps side clearance non-solid in the WFC envelope"),
				bRaisedSemanticVolumesMatchOccupiedCore);
			for (const ABTSM73BeamC3V3::FSemanticSupportVolumeNodeDiagnostic& Node
				: Plan.SemanticSupportVolumeNodes)
			{
				AddInfo(FString::Printf(
					TEXT("TipOverE6SupportNode:Seed=%d:Node=%d:Source=%d:Role=%d:Primitive=%d:Grounded=%d:Synthetic=%d:SquareBody=%d:GraphTerminal=%d:TerminalBody=%d:LoadBranches=%d:Reason=%s:Parents=%s:Children=%s:Path=%s:Bounds=%s"),
					Seed, Node.NodeId, Node.SourceVolumeId,
					static_cast<int32>(Node.Role),
					static_cast<int32>(Node.Primitive),
					Node.bGrounded ? 1 : 0,
					Node.bSyntheticCoupledGround ? 1 : 0,
					Node.bSquareBody ? 1 : 0,
					Node.bGraphTerminal ? 1 : 0,
					Node.bTerminalBody ? 1 : 0,
					Node.TerminalLoadBranchCount,
					*Node.DemandClassificationReason,
					*JoinIds(Node.ParentNodeIds), *JoinIds(Node.ChildNodeIds),
					*Node.DerivationPath, *Node.LocalBounds.ToString()));
			}
		}
		AddInfo(FString::Printf(
			TEXT("TipOverE6Optimization:Seed=%d:Main=%d:Children=%d:Required=%d:Bound=%d:LoadBranches=%d:MultiBranchBodies=%d:UnrepresentedBranches=%d:SemanticDemands=%d:DemandCoreRows=%d:Unmapped=%d:Ambiguous=%d:OutsideBody=%d:NoDirectMain=%d:ReusedChildren=%d:OrphanChildren=%d:DemandCoreHash=%lld:LocalPodiumCandidates=%d:RejectedLocalPodiumCandidates=%d:LocalPodiumRegions=%d:RaisedLocalPodiumRegions=%d:AppliedLocalPodiumRegions=%d:LocalPodiumLegMembers=%d:RaisedMainReservations=%d:RaisedMainMembers=%d:LocalPodiumHash=%lld:GeometryHash=%lld:TimingMs=Demand:%.2f,Child:%.2f,Main:%.2f,Joint:%.2f,Emission:%.2f,DAG:%.2f,Total:%.2f"),
			Seed, Plan.Summary.PodiumMainCoreCellCount,
			Plan.Summary.TowerChildCoreCellCount,
			Plan.Summary.RequiredTerminalBranchCount,
			Plan.Summary.BoundTerminalBranchCount,
			Plan.Summary.SemanticTerminalLoadBranchCount,
			Plan.Summary.MultiBranchTerminalBodyCount,
			Plan.Summary.UnrepresentedSemanticTerminalLoadBranchCount,
			Plan.Summary.SemanticTerminalDemandCount,
			Plan.Summary.SemanticDemandCoreBindingCount,
			Plan.Summary.UnmappedSemanticDemandCount,
			Plan.Summary.AmbiguousSemanticDemandCount,
			Plan.Summary.SemanticDemandChildOutsideBodyCount,
			Plan.Summary.SemanticDemandChildWithoutDirectMainCouplingCount,
			Plan.Summary.ReusedTowerChildBindingCount,
			Plan.Summary.UnreferencedTowerChildCount,
			Plan.Summary.SemanticDemandCoreBindingHash,
			Plan.Summary.LocalPodiumHeightCandidateCount,
			Plan.Summary.RejectedLocalPodiumHeightCandidateCount,
			Plan.Summary.LocalPodiumHeightRegionCount,
			Plan.Summary.RaisedLocalPodiumHeightRegionCount,
			Plan.Summary.AppliedLocalPodiumHeightRegionCount,
			Plan.Summary.LocalPodiumLegMemberCount,
			Plan.Summary.RaisedPodiumMainReservationCount,
			Plan.Summary.RaisedPodiumMainMemberCount,
			Plan.Summary.LocalPodiumHeightPlanHash,
			Plan.Summary.FinalGeometryHash,
			Plan.Summary.TerminalDemandMilliseconds,
			Plan.Summary.ChildCandidateMilliseconds,
			Plan.Summary.PodiumMainCandidateMilliseconds,
			Plan.Summary.JointSelectionMilliseconds,
			Plan.Summary.MemberEmissionMilliseconds,
			Plan.Summary.StaticDAGMilliseconds,
			Plan.Summary.Stage1TotalMilliseconds));
		for (const ABTSM73BeamC3V3::FLocalPodiumHeightRegionDiagnostic& Region
			: Plan.LocalPodiumHeightRegions)
		{
			AddInfo(FString::Printf(
				TEXT("TipOverE6LocalPodiumRegion:Seed=%d:Region=%d:Component=%d:StructuralMain=%d:Actual=%d:Selected=%d:Raised=%d:Applied=%d:Children=%s:Provinces=%s"),
				Seed, Region.RegionId, Region.ComponentId,
				Region.StructuralPodiumMainCoreCellId,
				Region.ActualPodiumTopCourse, Region.SelectedTopCourse,
				Region.bRaisesActualPodium ? 1 : 0,
				Region.bAppliedToProductionCoreHierarchy ? 1 : 0,
				*JoinIds(Region.AppliedTowerChildCoreCellIds),
				*JoinIds(Region.ProvinceIds)));
		}
		for (const ABTSM73BeamC3V3::FLocalPodiumHeightCandidateDiagnostic& Candidate
			: Plan.LocalPodiumHeightCandidates)
		{
			AddInfo(FString::Printf(
				TEXT("TipOverE6LocalPodiumCandidate:Seed=%d:Province=%d:GroundCore=%d:StructuralMain=%d:Course=%d:Actual=%d:Baseline=%d:OwnBoundary=%d:SharedEvent=%d:Boundary=%d:Continuous=%d:DemandSeeds=%d:Connected=%d:Cells=%d:SiblingGapUnits=%d:FirstRaisedCells=%d:RetainedPermille=%d:RetainsHalf=%d:BridgeWithin720=%d:BridgeVoidClear=%d:Clearance=%d:VoidClear=%d:Accepted=%d:Selected=%d:Reason=%s"),
				Seed, Candidate.ProvinceId, Candidate.BoundGroundCoreCellId,
				Candidate.StructuralPodiumMainCoreCellId,
				Candidate.CandidateTopCourse,
				Candidate.ActualPodiumTopCourse,
				Candidate.bActualBaseline ? 1 : 0,
				Candidate.bOwnSemanticBoundary ? 1 : 0,
				Candidate.bSharedPodiumMainSemanticEvent ? 1 : 0,
				Candidate.bCommonSemanticBoundary ? 1 : 0,
				Candidate.bFullyOccupiedThroughCandidate ? 1 : 0,
				Candidate.bCoversEveryDemandSeed ? 1 : 0,
				Candidate.bSingleConnectedFootprint ? 1 : 0,
				Candidate.PersistentCellCount,
				Candidate.MinimumSiblingFootprintGapUnits,
				Candidate.FirstRaisedPersistentCellCount,
				Candidate.RetainedFootprintPermille,
				Candidate.bRetainsHalfFirstRaisedFootprint ? 1 : 0,
				Candidate.bSiblingBridgeWithinMemberSpan ? 1 : 0,
				Candidate.bSiblingBridgeVoidClear ? 1 : 0,
				Candidate.bLeavesTwoChildCourses ? 1 : 0,
				Candidate.bProtectedVoidClear ? 1 : 0,
				Candidate.bAccepted ? 1 : 0,
				Candidate.bSelected ? 1 : 0,
				*Candidate.DecisionReason));
		}
		const int32 ExpectedStructuralMain = Seed == 710000 ? 1 : 0;
		const int32 ExpectedRaisedTop = Seed == 710000
			? 92 : Seed == 730000 ? 92 : 89;
		TestTrue(*FString::Printf(
			TEXT("Seed %d selects the morphology-bounded central podium height"), Seed),
			Plan.LocalPodiumHeightRegions.ContainsByPredicate(
				[ExpectedStructuralMain, ExpectedRaisedTop](
					const ABTSM73BeamC3V3::FLocalPodiumHeightRegionDiagnostic& Region)
				{
					return Region.StructuralPodiumMainCoreCellId == ExpectedStructuralMain
						&& Region.SelectedTopCourse == ExpectedRaisedTop
						&& Region.bRaisesActualPodium
						&& Region.ProvinceIds.Num() >= 2;
				}));
		TestTrue(*FString::Printf(TEXT("Seed %d publishes a local podium plan"), Seed),
			Plan.Summary.LocalPodiumHeightPlanHash != 0
				&& Plan.Summary.LocalPodiumHeightRegionCount > 0
				&& Plan.Summary.LocalPodiumHeightCandidateCount
					>= Plan.Summary.SupportProvinceCount);
		TestEqual(*FString::Printf(TEXT("Seed %d binds every terminal branch"), Seed),
			Plan.Summary.BoundTerminalBranchCount,
			Plan.Summary.RequiredTerminalBranchCount);
		TestEqual(*FString::Printf(TEXT("Seed %d emits one child per terminal branch"),
			Seed), Plan.Summary.TowerChildCoreCellCount,
			Plan.Summary.RequiredTerminalBranchCount);
		TestEqual(*FString::Printf(TEXT("Seed %d publishes one binding row per semantic demand"),
			Seed), Plan.Summary.SemanticDemandCoreBindingCount,
			Plan.Summary.SemanticTerminalDemandCount);
		TestEqual(*FString::Printf(TEXT("Seed %d represents every terminal load branch"),
			Seed), Plan.Summary.SemanticTerminalDemandCount,
			Plan.Summary.SemanticTerminalLoadBranchCount);
		TestEqual(*FString::Printf(TEXT("Seed %d leaves no support branch unrepresented"),
			Seed), Plan.Summary.UnrepresentedSemanticTerminalLoadBranchCount, 0);
		TestEqual(*FString::Printf(TEXT("Seed %d binding array closes with summary"),
			Seed), Plan.SemanticDemandCoreBindings.Num(),
			Plan.Summary.SemanticDemandCoreBindingCount);
		TestNotEqual(*FString::Printf(TEXT("Seed %d binding identity is nonzero"), Seed),
			Plan.Summary.SemanticDemandCoreBindingHash, int64(0));
		int32 UnmappedCount = 0;
		int32 AmbiguousCount = 0;
		int32 OutsideBodyCount = 0;
		int32 NoDirectMainCount = 0;
		for (const ABTSM73BeamC3V3::FSemanticDemandCoreBindingDiagnostic& Binding
			: Plan.SemanticDemandCoreBindings)
		{
			UnmappedCount += Binding.BoundTowerChildCoreCellId == INDEX_NONE ? 1 : 0;
			AmbiguousCount += Binding.bAmbiguousRegionMatch ? 1 : 0;
			OutsideBodyCount += Binding.BoundTowerChildCoreCellId != INDEX_NONE
				&& !Binding.bChildCenterInsideBodyXY ? 1 : 0;
			NoDirectMainCount += Binding.BoundTowerChildCoreCellId != INDEX_NONE
				&& !Binding.bDirectMainCoupling ? 1 : 0;
			AddInfo(FString::Printf(
				TEXT("TipOverE6DemandCore:Seed=%d:Demand=%d:Component=%d:Province=%d:BodySource=%d:LoadNode=%d:LoadSource=%d:Candidates=%d/%d:Region=%d:Child=%d:Main=%d:Multiplicity=%d:Overlap=%.3f:Inside=%d:Fit=%d:Direct=%d:Ambiguous=%d:Reason=%s:Body=%s:ChildBounds=%s:MainBounds=%s"),
				Seed, Binding.DemandId, Binding.ComponentId,
				Binding.SupportProvinceId,
				Binding.TerminalBodySourceVolumeId,
				Binding.TerminalLoadNodeId,
				Binding.TerminalLoadSourceVolumeId,
				Binding.CandidateRegionCount, Binding.CandidateChildCount,
				Binding.BoundHighProjectionRegionId,
				Binding.BoundTowerChildCoreCellId,
				Binding.AssignedPodiumMainCoreCellId,
				Binding.BoundChildDemandMultiplicity,
				Binding.BodyChildXYOverlapAreaCM2,
				Binding.bChildCenterInsideBodyXY ? 1 : 0,
				Binding.bChildInsideContinuousFitXY ? 1 : 0,
				Binding.bDirectMainCoupling ? 1 : 0,
				Binding.bAmbiguousRegionMatch ? 1 : 0,
				*Binding.MappingReason,
				*Binding.DemandBodyBounds.ToString(),
				*Binding.ChildBounds.ToString(),
				*Binding.MainBounds.ToString()));
		}
		TestEqual(TEXT("Unmapped binding accounting closes"), UnmappedCount,
			Plan.Summary.UnmappedSemanticDemandCount);
		TestEqual(TEXT("Ambiguous binding accounting closes"), AmbiguousCount,
			Plan.Summary.AmbiguousSemanticDemandCount);
		TestEqual(TEXT("Outside-body binding accounting closes"), OutsideBodyCount,
			Plan.Summary.SemanticDemandChildOutsideBodyCount);
		TestEqual(TEXT("No-direct-main binding accounting closes"), NoDirectMainCount,
			Plan.Summary.SemanticDemandChildWithoutDirectMainCouplingCount);
		int32 ReusedChildCount = 0;
		TSet<int32> ReferencedChildIds;
		for (const ABTSM73BeamC3V3::FSemanticDemandCoreBindingDiagnostic& Binding
			: Plan.SemanticDemandCoreBindings)
		{
			if (Binding.BoundTowerChildCoreCellId != INDEX_NONE)
			{
				ReferencedChildIds.Add(Binding.BoundTowerChildCoreCellId);
				ReusedChildCount += Binding.BoundChildDemandMultiplicity > 1
					&& !Plan.SemanticDemandCoreBindings.ContainsByPredicate(
						[&Binding](const ABTSM73BeamC3V3::FSemanticDemandCoreBindingDiagnostic& Other)
						{
							return Other.DemandId < Binding.DemandId
								&& Other.BoundTowerChildCoreCellId
									== Binding.BoundTowerChildCoreCellId;
						}) ? 1 : 0;
			}
		}
		int32 OrphanChildCount = 0;
		for (const ABTSM73BeamC3V3::FCoreCellPlan& Core : Plan.CoreCells)
		{
			OrphanChildCount += Core.HierarchyRole
				== ABTSM73BeamC3V3::ECoreHierarchyRole::TowerChild
				&& !ReferencedChildIds.Contains(Core.CoreCellId) ? 1 : 0;
		}
		TestEqual(TEXT("Reused-child accounting closes"), ReusedChildCount,
			Plan.Summary.ReusedTowerChildBindingCount);
		TestEqual(TEXT("Orphan-child accounting closes"), OrphanChildCount,
			Plan.Summary.UnreferencedTowerChildCount);
		TestEqual(*FString::Printf(TEXT("Seed %d maps every semantic demand"), Seed),
			Plan.Summary.UnmappedSemanticDemandCount, 0);
		TestEqual(*FString::Printf(TEXT("Seed %d has no ambiguous demand binding"), Seed),
			Plan.Summary.AmbiguousSemanticDemandCount, 0);
		TestEqual(*FString::Printf(TEXT("Seed %d keeps every child inside its demand"), Seed),
			Plan.Summary.SemanticDemandChildOutsideBodyCount, 0);
		TestEqual(*FString::Printf(TEXT("Seed %d never reuses a child"), Seed),
			Plan.Summary.ReusedTowerChildBindingCount, 0);
		TestEqual(*FString::Printf(TEXT("Seed %d emits no orphan child"), Seed),
			Plan.Summary.UnreferencedTowerChildCount, 0);
		TestEqual(*FString::Printf(TEXT("Seed %d emits one child per semantic demand"),
			Seed), Plan.Summary.TowerChildCoreCellCount,
			Plan.Summary.SemanticTerminalDemandCount);
		if (Seed == 750000)
		{
			TestEqual(TEXT("TipOver 750000 keeps all nine terminal load branches"),
				Plan.Summary.SemanticTerminalLoadBranchCount, 9);
			TestEqual(TEXT("TipOver 750000 emits all nine semantic demands"),
				Plan.Summary.SemanticTerminalDemandCount, 9);
			const ABTSM73BeamC3V3::FSemanticSupportVolumeNodeDiagnostic* SplitBody =
				Plan.SemanticSupportVolumeNodes.FindByPredicate(
					[](const ABTSM73BeamC3V3::FSemanticSupportVolumeNodeDiagnostic& Node)
					{
						return Node.SourceVolumeId == 14;
					});
			if (TestNotNull(TEXT("TipOver 750000 identifies the west split Body"),
				SplitBody))
			{
				TestEqual(TEXT("West split Body publishes two terminal load branches"),
					SplitBody->TerminalLoadBranchCount, 2);
			}
			TArray<int32> SplitLoadSources;
			for (const ABTSM73BeamC3V3::FSemanticTerminalDemandDiagnostic& Demand
				: Plan.SemanticTerminalDemands)
			{
				if (Demand.TerminalBodySourceVolumeId == 14)
				{
					SplitLoadSources.Add(Demand.TerminalLoadSourceVolumeId);
				}
			}
			SplitLoadSources.Sort();
			TestEqual(TEXT("West split Body emits two demand identities"),
				SplitLoadSources.Num(), 2);
			TestTrue(TEXT("West split demands preserve South/North load leaves"),
				SplitLoadSources == TArray<int32>({16, 18}));
		}
		TestTrue(*FString::Printf(TEXT("Seed %d passes the Stage 1 static DAG"), Seed),
			Result.Summary.bStageStaticDAGEvaluated
				&& Result.StaticDAG.Summary.bAccepted);
		TestTrue(*FString::Printf(TEXT("Seed %d remains inside the Stage 1 budget"), Seed),
			Plan.Summary.bStage1TimingEvaluated
				&& Plan.Summary.bStage1WithinTimeBudget
				&& Plan.Summary.Stage1TotalMilliseconds
					<= Plan.Summary.Stage1TimeBudgetMilliseconds);
	}
	TestTrue(TEXT("TipOver E6 optimization seeds exercise a selected one-shrink TowerChild"),
		TotalSingleShrinkChildren > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3SemanticSupportMergedRoofDemandTest,
	"ABTS.M73DAG.BeamC3V3.Staged.SemanticSupportMergedRoofDemand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3SemanticSupportMergedRoofDemandTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamC3V3Tests;
	FABTSM73BeamD0ResolvedProfile Profile;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FString Error;
	if (!TestTrue(TEXT("Base profile resolves"), ResolveShape(
		{TEXT("DropTrigger"), 2, 740000}, Profile, Silhouette, Error)))
	{
		return false;
	}
	Profile.VisualComplexity.MinimumBrickCount = 1;
	Profile.VisualComplexity.MaximumBrickCount = 100000;
	FABTSM73DAG5BV2Volume Podium = MakeBodyVolume(
		0, FVector(-720.0, -360.0, 0.0), FVector(720.0, 360.0, 360.0),
		TEXT("CoupledGround/Cell/0"));
	Podium.Role = EABTSM73DAG5BV2VolumeRole::Foundation;
	FABTSM73DAG5BV2Volume MergedCrown = MakeBodyVolume(
		3, FVector(-540.0, -360.0, 1080.0),
		FVector(540.0, 360.0, 1440.0), TEXT("Building0/MergedRoof"));
	MergedCrown.Role = EABTSM73DAG5BV2VolumeRole::Crown;
	MergedCrown.Primitive = EABTSM73DAG5BV2Primitive::TriangularPrismX;
	Silhouette.Volumes = {
		Podium,
		MakeBodyVolume(1, FVector(-540.0, -360.0, 360.0),
			FVector(-108.0, 360.0, 1080.0), TEXT("Building0/WestBody")),
		MakeBodyVolume(2, FVector(108.0, -360.0, 360.0),
			FVector(540.0, 360.0, 1080.0), TEXT("Building0/EastBody")),
		MergedCrown};
	Silhouette.Summary.bAccepted = true;

	ABTSM73BeamC3V3::FGenerationResult Result;
	const bool bGenerated = FABTSM73BeamC3V3SkeletonFirstGenerator().GenerateStage1(
		Profile, Silhouette, Result, Error);
	TestTrue(*FString::Printf(TEXT("Merged-roof diagnostic fixture generates: %s"),
		*Error), bGenerated);
	if (!bGenerated)
	{
		return false;
	}
	const ABTSM73BeamC3V3::FPlan& Plan = Result.Plan;
	TestEqual(TEXT("Two Body demands remain two authoritative child regions"),
		Plan.Summary.RequiredTerminalBranchCount, 2);
	TestEqual(TEXT("Two highest Body supports remain two semantic demands"),
		Plan.Summary.SemanticTerminalDemandCount, 2);
	TestEqual(TEXT("Semantic demand array closes its summary"),
		Plan.SemanticTerminalDemands.Num(), 2);
	TestTrue(TEXT("Both Body demands explicitly record the shared merged Crown"),
		Algo::AllOf(Plan.SemanticTerminalDemands,
			[](const ABTSM73BeamC3V3::FSemanticTerminalDemandDiagnostic& Demand)
			{
				return Demand.bSharesMergedCrown
					&& Demand.CrownSourceVolumeIds.Num() == 1
					&& Demand.CrownSourceVolumeIds[0] == 3;
			}));
	TestTrue(TEXT("Merge ledger records the two-Body to one-Crown transition"),
		Plan.SemanticSupportMergeLedger.ContainsByPredicate(
			[](const ABTSM73BeamC3V3::FSemanticSupportMergeLedgerDiagnostic& Ledger)
			{
				return Ledger.bMerge
					&& Ledger.LowerNodeIds.Num() == 2
					&& Ledger.UpperNodeIds.Num() == 1;
			}));
	TestNotEqual(TEXT("Diagnostic graph has a stable independent identity"),
		Plan.Summary.SemanticSupportDemandHash, int64(0));
	TestEqual(TEXT("Separated Body footprints remain two support provinces"),
		Plan.Summary.SupportProvinceCount, 2);
	TestEqual(TEXT("Each merged-roof demand binds exactly one province"),
		Plan.SupportProvinces.Num(), 2);
	TestTrue(TEXT("Support provinces preserve the shared synthetic podium source"),
		Algo::AllOf(Plan.SupportProvinces,
			[](const ABTSM73BeamC3V3::FSupportProvinceDiagnostic& Province)
			{
				return Province.DemandIds.Num() == 1
					&& Province.GroundCellCount > 0
					&& Province.ProposedPodiumTopCourse > 0
					&& Province.bSyntheticGroundOnly;
			}));
	TestNotEqual(TEXT("Support-province partition has a separate identity"),
		Plan.Summary.SupportProvinceHash, int64(0));
	TestEqual(TEXT("Every merged-roof province binds a grounded core"),
		Plan.Summary.BoundSupportProvinceCount,
		Plan.Summary.SupportProvinceCount);
	TestNotEqual(TEXT("Province/main assignment has a separate identity"),
		Plan.Summary.SupportProvinceMainBindingHash, int64(0));
	TestEqual(TEXT("Each merged-roof Body demand emits one child"),
		Plan.Summary.TowerChildCoreCellCount, 2);
	TestEqual(TEXT("Each semantic demand has one authoritative binding"),
		Plan.Summary.SemanticDemandCoreBindingCount, 2);
	TestEqual(TEXT("Merged-roof demands do not reuse a child"),
		Plan.Summary.ReusedTowerChildBindingCount, 0);
	TestEqual(TEXT("Merged-roof generation emits no orphan child"),
		Plan.Summary.UnreferencedTowerChildCount, 0);
	TestEqual(TEXT("Merged-roof children remain inside their Body demands"),
		Plan.Summary.SemanticDemandChildOutsideBodyCount, 0);
	TSet<int32> BoundChildIds;
	for (const ABTSM73BeamC3V3::FSemanticDemandCoreBindingDiagnostic& Binding
		: Plan.SemanticDemandCoreBindings)
	{
		BoundChildIds.Add(Binding.BoundTowerChildCoreCellId);
		TestEqual(TEXT("Binding is selected by authoritative semantic identity"),
			Binding.MappingReason, FString(TEXT("AuthoritativeSemanticDemandId")));
	}
	TestEqual(TEXT("Merged-roof demands bind distinct child identities"),
		BoundChildIds.Num(), 2);
	TestTrue(TEXT("One legacy terminal slice records both semantic regions"),
		Plan.HighProjectionBranchBindingDiagnostics.ContainsByPredicate(
			[](const ABTSM73BeamC3V3::FHighProjectionBranchBindingDiagnostic& Branch)
			{
				return Branch.bTerminal && Branch.bRequiresTowerChild
					&& Branch.RequiredRegionIds.Num() == 2;
			}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3StagedStage1TimingBudgetTest,
	"ABTS.M73DAG.BeamC3V3.Staged.Stage1TimingBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3StagedStage1TimingBudgetTest::RunTest(
	const FString& Parameters)
{
	FABTSM73BeamC3V3SkeletonFirstGenerator Generator;
	ABTSM73BeamC3V3::FPlan Plan;
	FString Error;
	TestTrue(TEXT("An elapsed leaf at the limit is accepted"),
		Generator.ValidateStage1TimingBudgetForTesting(
			ABTSM73BeamC3V3::Stage1LeafTimeBudgetMilliseconds, Plan, Error));
	TestTrue(TEXT("Accepted timing evidence is evaluated"),
		Plan.Summary.bStage1TimingEvaluated && Error.IsEmpty());
	TestFalse(TEXT("An elapsed leaf over the limit fails closed"),
		Generator.ValidateStage1TimingBudgetForTesting(
			ABTSM73BeamC3V3::Stage1LeafTimeBudgetMilliseconds + 1.0,
			Plan, Error));
	TestTrue(TEXT("Timeout identifies its phase and budget"),
		!Plan.Summary.bStage1WithinTimeBudget
			&& Plan.Summary.Stage1TimeoutPhase == TEXT("AutomationFixture")
			&& Error.Contains(TEXT("BeamC3V3Stage1Timeout"))
			&& Error.Contains(TEXT("Phase=AutomationFixture")));
	return true;
}

#endif
