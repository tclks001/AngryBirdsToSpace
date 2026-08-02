// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3MonthlySatellitePreview.h"

#include "ABTSRuntime.h"
#include "Calibration/ABTSSlingshotSatelliteCalibrationTypes.h"
#include "Planet/ABTSM2Planet.h"
#include "Slingshot/ABTSSlingshotVisualTypes.h"

namespace ABTSM3R51SatellitePreviewPrivate
{
constexpr uint64 Fnv1a64OffsetBasis = 14695981039346656037ull;
constexpr uint64 Fnv1a64Prime = 1099511628211ull;
constexpr double VectorQuantization = 1000.0;

class FCanonicalHash64
{
public:
	void AddUInt64(const uint64 Value)
	{
		for (int32 Shift = 0; Shift < 64; Shift += 8)
		{
			Hash ^= static_cast<uint8>((Value >> Shift) & 0xffull);
			Hash *= Fnv1a64Prime;
		}
	}

	void AddInt64(const int64 Value) { AddUInt64(static_cast<uint64>(Value)); }
	void AddInt32(const int32 Value) { AddUInt64(static_cast<uint32>(Value)); }
	void AddBool(const bool bValue) { AddUInt64(bValue ? 1ull : 0ull); }
	void AddFloat(const float Value)
	{
		AddInt64(FMath::RoundToInt64(static_cast<double>(Value) * VectorQuantization));
	}
	void AddVector(const FVector& Value)
	{
		AddFloat(Value.X);
		AddFloat(Value.Y);
		AddFloat(Value.Z);
	}
	void AddQuat(const FQuat& Value)
	{
		FQuat Canonical = Value.GetNormalized();
		if (Canonical.W < 0.0)
		{
			Canonical.X = -Canonical.X;
			Canonical.Y = -Canonical.Y;
			Canonical.Z = -Canonical.Z;
			Canonical.W = -Canonical.W;
		}
		AddFloat(Canonical.X);
		AddFloat(Canonical.Y);
		AddFloat(Canonical.Z);
		AddFloat(Canonical.W);
	}
	uint64 Get() const { return Hash; }

private:
	uint64 Hash = Fnv1a64OffsetBasis;
};

bool IsFiniteVector(const FVector& Value)
{
	return !Value.ContainsNaN()
		&& FMath::IsFinite(Value.X)
		&& FMath::IsFinite(Value.Y)
		&& FMath::IsFinite(Value.Z);
}

bool ValidateConfig(
	const FABTSM3MonthlySatellitePreviewConfig& Config,
	FString& OutFailure)
{
	if (Config.PracticeEncounterOrder != 4
		|| !FMath::IsFinite(Config.PrimarySurfaceGravityCMPerSec2)
		|| Config.PrimarySurfaceGravityCMPerSec2 <= 0.0f
		|| !FMath::IsFinite(Config.ReferencePouchHeightCM)
		|| Config.ReferencePouchHeightCM < 0.0f
		|| Config.ReferencePouchHeightCM > 1000.0f
		|| Config.PlannerVersion != 1)
	{
		OutFailure = TEXT("ConfigRangeOrVersion");
		return false;
	}
	return true;
}

bool IsTopologyUsable(const TArray<FABTSM2Cell>& Cells)
{
	if (Cells.IsEmpty())
	{
		return false;
	}
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		const FABTSM2Cell& Cell = Cells[CellId];
		if (!IsFiniteVector(Cell.UnitCenter)
			|| !Cell.UnitCenter.IsNormalized())
		{
			return false;
		}
	}
	return true;
}

bool Reject(
	FABTSM3MonthlySatellitePreviewResult& OutResult,
	const EABTSM3MonthlySatellitePreviewRejectReason Reason,
	const TCHAR* Failure,
	FString& OutFailure)
{
	OutResult.bPreviewResultValid = false;
	OutResult.bMonthlyWorldAccepted = false;
	OutResult.RejectReason = Reason;
	OutResult.RetainedCandidates.Reset();
	OutResult.ResultHash = 0;
	OutFailure = Failure;
	return false;
}

const FABTSM3MonthlySlingshotFieldCandidate* FindFieldCandidate(
	const FABTSM3MonthlySlingshotFieldResult& Result,
	const int32 CandidateId)
{
	return Result.RetainedCandidates.FindByPredicate(
		[CandidateId](const FABTSM3MonthlySlingshotFieldCandidate& Candidate)
		{
			return Candidate.SourceRouteCandidateId == CandidateId;
		});
}

bool ResolveSurfaceCell(
	const TArray<FABTSM2Cell>& Cells,
	const int32 CellId,
	const IABTSM3MonthlySatellitePreviewSurface& Surface,
	FABTSM3MonthlySatelliteSurfaceSample& OutSample)
{
	return Cells.IsValidIndex(CellId)
		&& Surface.QuerySurface(Cells[CellId].UnitCenter, OutSample)
		&& IsFiniteVector(OutSample.WorldLocation)
		&& IsFiniteVector(OutSample.WorldNormal)
		&& OutSample.WorldNormal.Normalize();
}

struct FReferenceSlingshotFrame
{
	FABTSM3MonthlySatelliteSurfaceSample SlotA;
	FABTSM3MonthlySatelliteSurfaceSample SlotB;
	int32 SlotACellId = INDEX_NONE;
	int32 SlotBCellId = INDEX_NONE;
	FVector SlingCenterWorld = FVector::ZeroVector;
	FVector RestPouchWorld = FVector::ZeroVector;
	FVector SlingUpWorld = FVector::UpVector;
	FVector SlingForwardWorld = FVector::ForwardVector;
	FVector SlingRightWorld = FVector::RightVector;
	float PreferredFacingErrorDegrees = 180.0f;
};

bool BuildReferenceSlingshotFrame(
	const FVector& PrimaryCenter,
	const FVector& PreferredWorldDirection,
	const FABTSM3MonthlySatelliteSurfaceSample& InA,
	const FABTSM3MonthlySatelliteSurfaceSample& InB,
	const int32 InACellId,
	const int32 InBCellId,
	const FABTSSlingshotVisualPreset& VisualPreset,
	FReferenceSlingshotFrame& OutFrame)
{
	FVector EndpointA = InA.WorldLocation
		+ InA.WorldNormal * VisualPreset.StakeHeightCM;
	FVector EndpointB = InB.WorldLocation
		+ InB.WorldNormal * VisualPreset.StakeHeightCM;
	FVector SlingCenter = (EndpointA + EndpointB) * 0.5f;
	FVector SlingUp = (SlingCenter - PrimaryCenter).GetSafeNormal();
	FVector SlingRight = FVector::VectorPlaneProject(
		EndpointB - EndpointA,
		SlingUp).GetSafeNormal();
	FVector SlingForward = FVector::CrossProduct(
		SlingRight,
		SlingUp).GetSafeNormal();
	const FVector PreferredForward = FVector::VectorPlaneProject(
		PreferredWorldDirection,
		SlingUp).GetSafeNormal();
	if (SlingUp.IsNearlyZero()
		|| SlingRight.IsNearlyZero()
		|| SlingForward.IsNearlyZero()
		|| PreferredForward.IsNearlyZero())
	{
		return false;
	}
	FABTSM3MonthlySatelliteSurfaceSample SlotA = InA;
	FABTSM3MonthlySatelliteSurfaceSample SlotB = InB;
	int32 SlotACellId = InACellId;
	int32 SlotBCellId = InBCellId;
	if (FVector::DotProduct(SlingForward, PreferredForward) < 0.0f)
	{
		Swap(EndpointA, EndpointB);
		Swap(SlotA, SlotB);
		Swap(SlotACellId, SlotBCellId);
		SlingRight *= -1.0f;
		SlingForward *= -1.0f;
	}
	const FVector VisualRight = (EndpointB - EndpointA).GetSafeNormal();
	FVector VisualUp = (SlotA.WorldNormal + SlotB.WorldNormal).GetSafeNormal();
	VisualUp = FVector::VectorPlaneProject(VisualUp, VisualRight).GetSafeNormal();
	const FVector VisualForward = FVector::CrossProduct(
		VisualRight,
		VisualUp).GetSafeNormal();
	if (VisualRight.IsNearlyZero()
		|| VisualUp.IsNearlyZero()
		|| VisualForward.IsNearlyZero())
	{
		return false;
	}
	const FQuat LayoutRotation = FRotationMatrix::MakeFromXY(
		VisualForward,
		VisualRight).ToQuat();
	const FVector StakeAnchorA = EndpointA + LayoutRotation.RotateVector(
		VisualPreset.ConnectionLayout.StakeAConnectionOffsetCM);
	const FVector StakeAnchorB = EndpointB + LayoutRotation.RotateVector(
		VisualPreset.ConnectionLayout.StakeBConnectionOffsetCM);
	OutFrame.SlotA = SlotA;
	OutFrame.SlotB = SlotB;
	OutFrame.SlotACellId = SlotACellId;
	OutFrame.SlotBCellId = SlotBCellId;
	OutFrame.SlingCenterWorld = SlingCenter;
	OutFrame.RestPouchWorld = (StakeAnchorA + StakeAnchorB) * 0.5f
		+ LayoutRotation.RotateVector(
			VisualPreset.ConnectionLayout.RestPouchOffsetCM);
	OutFrame.SlingUpWorld = SlingUp;
	OutFrame.SlingForwardWorld = SlingForward;
	OutFrame.SlingRightWorld = SlingRight;
	OutFrame.PreferredFacingErrorDegrees = FMath::RadiansToDegrees(
		FMath::Acos(FMath::Clamp(
			FVector::DotProduct(SlingForward, PreferredForward),
			-1.0f,
			1.0f)));
	return IsFiniteVector(OutFrame.RestPouchWorld)
		&& FMath::IsFinite(OutFrame.PreferredFacingErrorDegrees);
}

bool ChooseReferencePair(
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlySlingshotField& Field,
	const int32 MaxCordLengthCM,
	const IABTSM3MonthlySatellitePreviewSurface& Surface,
	const FVector& PreferredTargetWorld,
	FReferenceSlingshotFrame& OutFrame)
{
	FABTSM3MonthlySatelliteSurfaceSample Pocket;
	if (!ResolveSurfaceCell(
			Cells,
			Field.SourcePocketAnchorCellId,
			Surface,
			Pocket))
	{
		return false;
	}

	bool bFound = false;
	const FABTSSlingshotVisualPreset VisualPreset =
		ABTSMakeDefaultSlingshotVisualPreset(EABTSSlingshotTier::Reinforced);
	int64 BestFacingErrorMicroDegrees = MAX_int64;
	int64 BestMidpointDistanceMM = MAX_int64;
	int64 BestNegativeSpanMM = MAX_int64;
	for (int32 AIndex = 0; AIndex < Field.SlotCellIds.Num(); ++AIndex)
	{
		FABTSM3MonthlySatelliteSurfaceSample A;
		if (!ResolveSurfaceCell(Cells, Field.SlotCellIds[AIndex], Surface, A))
		{
			continue;
		}
		for (int32 BIndex = AIndex + 1; BIndex < Field.SlotCellIds.Num(); ++BIndex)
		{
			FABTSM3MonthlySatelliteSurfaceSample B;
			if (!ResolveSurfaceCell(Cells, Field.SlotCellIds[BIndex], Surface, B))
			{
				continue;
			}
			const double SpanCM = FVector::Distance(A.WorldLocation, B.WorldLocation);
			if (SpanCM <= UE_DOUBLE_SMALL_NUMBER
				|| SpanCM > static_cast<double>(MaxCordLengthCM) + 0.01)
			{
				continue;
			}
			const FVector Midpoint = (A.WorldLocation + B.WorldLocation) * 0.5;
			FReferenceSlingshotFrame Frame;
			if (!BuildReferenceSlingshotFrame(
					Surface.GetPrimaryCenterWorld(),
					PreferredTargetWorld - Midpoint,
					A,
					B,
					Field.SlotCellIds[AIndex],
					Field.SlotCellIds[BIndex],
					VisualPreset,
					Frame))
			{
				continue;
			}
			const int64 FacingErrorMicroDegrees = FMath::RoundToInt64(
				Frame.PreferredFacingErrorDegrees * 1000000.0);
			const int64 MidpointDistanceMM = FMath::RoundToInt64(
				FVector::Distance(Midpoint, Pocket.WorldLocation) * 10.0);
			const int64 NegativeSpanMM = -FMath::RoundToInt64(SpanCM * 10.0);
			const int32 ACellId = FMath::Min(Frame.SlotACellId, Frame.SlotBCellId);
			const int32 BCellId = FMath::Max(Frame.SlotACellId, Frame.SlotBCellId);
			const bool bBetter = !bFound
				|| FacingErrorMicroDegrees < BestFacingErrorMicroDegrees
				|| (FacingErrorMicroDegrees == BestFacingErrorMicroDegrees
					&& MidpointDistanceMM < BestMidpointDistanceMM)
				|| (FacingErrorMicroDegrees == BestFacingErrorMicroDegrees
					&& MidpointDistanceMM == BestMidpointDistanceMM
					&& NegativeSpanMM < BestNegativeSpanMM)
				|| (FacingErrorMicroDegrees == BestFacingErrorMicroDegrees
					&& MidpointDistanceMM == BestMidpointDistanceMM
					&& NegativeSpanMM == BestNegativeSpanMM
					&& (ACellId < FMath::Min(OutFrame.SlotACellId, OutFrame.SlotBCellId)
						|| (ACellId == FMath::Min(OutFrame.SlotACellId, OutFrame.SlotBCellId)
							&& BCellId < FMath::Max(OutFrame.SlotACellId, OutFrame.SlotBCellId))));
			if (!bBetter)
			{
				continue;
			}
			bFound = true;
			BestFacingErrorMicroDegrees = FacingErrorMicroDegrees;
			BestMidpointDistanceMM = MidpointDistanceMM;
			BestNegativeSpanMM = NegativeSpanMM;
			OutFrame = Frame;
		}
	}
	return bFound;
}

bool ResolveFacingAlignedSatelliteAnchor(
	const IABTSM3MonthlySatellitePreviewSurface& Surface,
	const FVector& LaunchWorld,
	const FVector& LaunchUp,
	const FVector& LaunchForward,
	const FABTSSatellitePracticePreset& Preset,
	FVector& OutAnchorDirection,
	FABTSM3MonthlySatelliteSurfaceSample& OutAnchor,
	float& OutFacingErrorDegrees,
	float& OutCorrectionAzimuthDegrees)
{
	constexpr float MaximumCorrectionDegrees = 15.0f;
	constexpr float MaximumFacingErrorDegrees = 5.0f;
	const float ArcRadians = FMath::DegreesToRadians(
		Preset.SatelliteAnchorArcDegrees);
	const float CenterClearanceCM = Surface.GetPrimaryRadiusCM()
		* Preset.SatelliteCenterClearancePrimaryRatio;
	FVector ArcTangent = FVector::VectorPlaneProject(
		LaunchForward,
		LaunchUp).GetSafeNormal();
	ArcTangent = ArcTangent.RotateAngleAxis(
		Preset.SatelliteAnchorAzimuthDegrees,
		LaunchUp).GetSafeNormal();
	if (ArcTangent.IsNearlyZero())
	{
		return false;
	}
	bool bFound = false;
	const auto Evaluate = [&](const float CorrectionDegrees)
	{
		const FVector CorrectedTangent = ArcTangent.RotateAngleAxis(
			CorrectionDegrees,
			LaunchUp).GetSafeNormal();
		const FVector AnchorDirection =
			(LaunchUp * FMath::Cos(ArcRadians)
				+ CorrectedTangent * FMath::Sin(ArcRadians)).GetSafeNormal();
		FABTSM3MonthlySatelliteSurfaceSample Anchor;
		if (!Surface.QuerySurface(AnchorDirection, Anchor)
			|| !IsFiniteVector(Anchor.WorldLocation)
			|| !IsFiniteVector(Anchor.WorldNormal)
			|| !Anchor.WorldNormal.Normalize())
		{
			return;
		}
		const FVector SatelliteCenter = Anchor.WorldLocation
			+ Anchor.WorldNormal * CenterClearanceCM;
		const FVector SightTangent = FVector::VectorPlaneProject(
			SatelliteCenter - LaunchWorld,
			LaunchUp).GetSafeNormal();
		if (SightTangent.IsNearlyZero())
		{
			return;
		}
		const float FacingErrorDegrees = FMath::RadiansToDegrees(
			FMath::Acos(FMath::Clamp(
				FVector::DotProduct(SightTangent, LaunchForward),
				-1.0f,
				1.0f)));
		const bool bBetter = !bFound
			|| FacingErrorDegrees < OutFacingErrorDegrees - 0.0001f
			|| (FMath::IsNearlyEqual(
					FacingErrorDegrees,
					OutFacingErrorDegrees,
					0.0001f)
				&& FMath::Abs(CorrectionDegrees)
					< FMath::Abs(OutCorrectionAzimuthDegrees));
		if (bBetter)
		{
			bFound = true;
			OutAnchorDirection = AnchorDirection;
			OutAnchor = Anchor;
			OutFacingErrorDegrees = FacingErrorDegrees;
			OutCorrectionAzimuthDegrees = CorrectionDegrees;
		}
	};
	OutFacingErrorDegrees = 180.0f;
	OutCorrectionAzimuthDegrees = 0.0f;
	for (int32 Step = -30; Step <= 30; ++Step)
	{
		Evaluate(static_cast<float>(Step) * 0.5f);
	}
	const float CoarseBest = OutCorrectionAzimuthDegrees;
	for (int32 Step = -25; Step <= 25; ++Step)
	{
		Evaluate(FMath::Clamp(
			CoarseBest + static_cast<float>(Step) * 0.02f,
			-MaximumCorrectionDegrees,
			MaximumCorrectionDegrees));
	}
	return bFound && OutFacingErrorDegrees <= MaximumFacingErrorDegrees;
}
}

bool FABTSM3MonthlySatellitePreviewBuilder::Build(
	const int32 WorldSeed,
	const FABTSM3MonthlySatellitePreviewConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlySpatialResult& SpatialResult,
	const FABTSM3MonthlySlingshotFieldResult& SlingshotFieldResult,
	const IABTSM3MonthlySatellitePreviewSurface& Surface,
	FABTSM3MonthlySatellitePreviewResult& OutResult,
	FString& OutFailure)
{
	OutResult = FABTSM3MonthlySatellitePreviewResult();
	OutResult.SchemaVersion = SchemaVersion;
	OutResult.GeneratorVersion = GeneratorVersion;
	OutResult.LayoutPolicyVersion = MonthlyLayoutPolicyVersion;
	OutResult.WorldSeed = WorldSeed;
	OutResult.TopologyHash = SpatialResult.TopologyHash;
	OutResult.SourceSpatialResultHash = SpatialResult.SpatialResultHash;
	OutResult.SourceSlingshotFieldResultHash = SlingshotFieldResult.ResultHash;
	OutResult.ConfigHash = static_cast<int64>(ComputeConfigHash(Config, SpatialResult.TopologyHash));
	OutResult.bMonthlyWorldAccepted = false;
	OutFailure.Reset();

	if (!Config.bBuildSatellitePreview)
	{
		return ABTSM3R51SatellitePreviewPrivate::Reject(OutResult, EABTSM3MonthlySatellitePreviewRejectReason::InvalidConfig,
			TEXT("SatellitePreviewDisabled"), OutFailure);
	}
	if (!ABTSM3R51SatellitePreviewPrivate::ValidateConfig(Config, OutFailure))
	{
		return ABTSM3R51SatellitePreviewPrivate::Reject(OutResult, EABTSM3MonthlySatellitePreviewRejectReason::InvalidConfig,
			TEXT("ConfigRangeOrVersion"), OutFailure);
	}
	if (!ABTSM3R51SatellitePreviewPrivate::IsTopologyUsable(Cells)
		|| !FMath::IsFinite(Surface.GetPrimaryRadiusCM())
		|| Surface.GetPrimaryRadiusCM() <= 0.0f
		|| !ABTSM3R51SatellitePreviewPrivate::IsFiniteVector(Surface.GetPrimaryCenterWorld()))
	{
		return ABTSM3R51SatellitePreviewPrivate::Reject(OutResult, EABTSM3MonthlySatellitePreviewRejectReason::InvalidTopology,
			TEXT("TopologyOrSurface"), OutFailure);
	}
	if (!SpatialResult.bSpatialResultValid
		|| SpatialResult.bMonthlyWorldAccepted
		|| SpatialResult.WorldSeed != WorldSeed
		|| SpatialResult.RetainedCandidates.IsEmpty())
	{
		return ABTSM3R51SatellitePreviewPrivate::Reject(OutResult, EABTSM3MonthlySatellitePreviewRejectReason::InvalidSpatialResult,
			TEXT("SpatialResult"), OutFailure);
	}
	if (!SlingshotFieldResult.bSlingshotFieldResultValid
		|| SlingshotFieldResult.bMonthlyWorldAccepted
		|| SlingshotFieldResult.WorldSeed != WorldSeed
		|| SlingshotFieldResult.TopologyHash != SpatialResult.TopologyHash
		|| SlingshotFieldResult.SourceSpatialResultHash != SpatialResult.SpatialResultHash)
	{
		return ABTSM3R51SatellitePreviewPrivate::Reject(OutResult, EABTSM3MonthlySatellitePreviewRejectReason::InvalidSlingshotFieldResult,
			TEXT("SlingshotFieldResult"), OutFailure);
	}

	const FABTSM6LaunchProfileCatalog FrozenCatalog =
		FABTSSlingshotSatelliteCalibrationModel::MakeFrozenLaunchProfileCatalogV0();
	const FABTSSatellitePracticePreset FrozenPreset =
		FABTSSlingshotSatelliteCalibrationModel::MakeFrozenSatellitePracticePresetV0();
	const int64 LaunchProfileHash = static_cast<int64>(
		FABTSSlingshotSatelliteCalibrationModel::ComputeLaunchProfileHash(FrozenCatalog));
	const int64 PresetHash = static_cast<int64>(
		FABTSSlingshotSatelliteCalibrationModel::ComputeSatellitePracticePresetHash(FrozenPreset));
	if (SpatialResult.FrozenCalibrationBatch.LaunchProfileVersion != FrozenCatalog.Version
		|| SpatialResult.FrozenCalibrationBatch.LaunchProfileHash != LaunchProfileHash
		|| SpatialResult.FrozenCalibrationBatch.SatellitePracticePresetVersion != FrozenPreset.Version
		|| SpatialResult.FrozenCalibrationBatch.SatellitePracticePresetHash != PresetHash)
	{
		return ABTSM3R51SatellitePreviewPrivate::Reject(OutResult, EABTSM3MonthlySatellitePreviewRejectReason::FrozenPresetMismatch,
			TEXT("FrozenCalibrationBatch"), OutFailure);
	}

	for (const FABTSM3MonthlySpatialCandidate& SpatialCandidate :
		SpatialResult.RetainedCandidates)
	{
		const FABTSM3MonthlySlingshotFieldCandidate* FieldCandidate =
			ABTSM3R51SatellitePreviewPrivate::FindFieldCandidate(SlingshotFieldResult, SpatialCandidate.SourceRouteCandidateId);
		if (FieldCandidate == nullptr
			|| FieldCandidate->SourceSpatialCandidateHash != SpatialCandidate.SpatialCandidateHash)
		{
			return ABTSM3R51SatellitePreviewPrivate::Reject(OutResult, EABTSM3MonthlySatellitePreviewRejectReason::CandidateJoinMismatch,
				TEXT("CandidateJoin"), OutFailure);
		}
		const FABTSM3MonthlySpatialEncounter* PracticeEncounter =
			SpatialCandidate.Encounters.FindByPredicate(
				[&Config](const FABTSM3MonthlySpatialEncounter& Encounter)
				{
					return Encounter.Contract.OrderIndex == Config.PracticeEncounterOrder;
				});
		if (PracticeEncounter == nullptr)
		{
			return ABTSM3R51SatellitePreviewPrivate::Reject(OutResult, EABTSM3MonthlySatellitePreviewRejectReason::PracticeEncounterMissing,
				TEXT("E5Encounter"), OutFailure);
		}
		const FABTSM3MonthlySlingshotField* PracticeField =
			FieldCandidate->Fields.FindByPredicate(
				[PracticeEncounter](const FABTSM3MonthlySlingshotField& Field)
				{
					return Field.Kind == EABTSM3MonthlySlingshotFieldKind::EncounterRequired
						&& Field.EncounterId == PracticeEncounter->Contract.EncounterId;
				});
		if (PracticeField == nullptr)
		{
			return ABTSM3R51SatellitePreviewPrivate::Reject(OutResult, EABTSM3MonthlySatellitePreviewRejectReason::PracticeEncounterMissing,
				TEXT("E5Field"), OutFailure);
		}

		FABTSM3MonthlySatelliteSurfaceSample TargetAnchor;
		if (!ABTSM3R51SatellitePreviewPrivate::ResolveSurfaceCell(
				Cells,
				PracticeEncounter->TargetAnchorCellId,
				Surface,
				TargetAnchor))
		{
			return ABTSM3R51SatellitePreviewPrivate::Reject(OutResult, EABTSM3MonthlySatellitePreviewRejectReason::SurfaceQueryFailed,
				TEXT("E5TargetAnchor"), OutFailure);
		}
		ABTSM3R51SatellitePreviewPrivate::FReferenceSlingshotFrame ReferenceFrame;
		if (!ABTSM3R51SatellitePreviewPrivate::ChooseReferencePair(
				Cells,
				*PracticeField,
				SlingshotFieldResult.MaxCordLengthCM,
				Surface,
				TargetAnchor.WorldLocation,
				ReferenceFrame))
		{
			return ABTSM3R51SatellitePreviewPrivate::Reject(OutResult, EABTSM3MonthlySatellitePreviewRejectReason::ReferencePairMissing,
				TEXT("E5DistanceValidReferencePair"), OutFailure);
		}

		const FVector PrimaryCenter = Surface.GetPrimaryCenterWorld();
		const float PrimaryRadius = Surface.GetPrimaryRadiusCM();
		const FVector LaunchUp = ReferenceFrame.SlingUpWorld;
		FVector LaunchForward = ReferenceFrame.SlingForwardWorld;
		FVector LaunchRight = FVector::CrossProduct(LaunchUp, LaunchForward);
		LaunchRight.Normalize();
		LaunchForward = FVector::CrossProduct(LaunchRight, LaunchUp).GetSafeNormal();

		FVector SatelliteAnchorDirection = FVector::ZeroVector;
		FABTSM3MonthlySatelliteSurfaceSample SatelliteAnchor;
		float SatelliteFacingErrorDegrees = 180.0f;
		float SatelliteFacingCorrectionDegrees = 0.0f;
		if (!ABTSM3R51SatellitePreviewPrivate::
				ResolveFacingAlignedSatelliteAnchor(
					Surface,
					ReferenceFrame.RestPouchWorld,
					LaunchUp,
					LaunchForward,
					FrozenPreset,
					SatelliteAnchorDirection,
					SatelliteAnchor,
					SatelliteFacingErrorDegrees,
					SatelliteFacingCorrectionDegrees))
		{
			return ABTSM3R51SatellitePreviewPrivate::Reject(OutResult, EABTSM3MonthlySatellitePreviewRejectReason::SurfaceQueryFailed,
				TEXT("SatelliteFacingAlignedAnchor"), OutFailure);
		}

		FABTSM3MonthlySatellitePreviewCandidate& Candidate =
			OutResult.RetainedCandidates.AddDefaulted_GetRef();
		Candidate.SourceRouteCandidateId = SpatialCandidate.SourceRouteCandidateId;
		Candidate.SourceSpatialCandidateHash = SpatialCandidate.SpatialCandidateHash;
		Candidate.SourceSlingshotFieldCandidateHash = FieldCandidate->CandidateHash;
		Candidate.PracticeEncounterId = PracticeEncounter->Contract.EncounterId;
		Candidate.PracticeFieldHash = PracticeField->FieldHash;
		Candidate.ReferenceSlotACellId = ReferenceFrame.SlotACellId;
		Candidate.ReferenceSlotBCellId = ReferenceFrame.SlotBCellId;
		Candidate.LaunchProfileHash = LaunchProfileHash;
		Candidate.SatellitePracticePresetVersion = FrozenPreset.Version;
		Candidate.SatellitePracticePresetHash = PresetHash;
		Candidate.LaunchUpWorld = LaunchUp;
		Candidate.LaunchForwardWorld = LaunchForward;
		Candidate.LaunchRightWorld = LaunchRight;
		Candidate.LaunchWorldLocation = ReferenceFrame.RestPouchWorld;
		Candidate.SatelliteAnchorDirection = SatelliteAnchorDirection;
		Candidate.SatelliteAnchorCellId = SatelliteAnchor.NearestCellId;
		Candidate.SatelliteFacingCorrectionAzimuthDegrees =
			SatelliteFacingCorrectionDegrees;
		Candidate.SatelliteFacingErrorDegrees =
			SatelliteFacingErrorDegrees;
		Candidate.SatelliteRadiusCM =
			PrimaryRadius * FrozenPreset.SatelliteRadiusPrimaryRatio;
		Candidate.SatelliteSurfaceGravityCMPerSec2 =
			Config.PrimarySurfaceGravityCMPerSec2
			* FrozenPreset.SatelliteSurfaceGravityPrimaryRatio;
		Candidate.SatelliteCenterWorld = SatelliteAnchor.WorldLocation
			+ SatelliteAnchor.WorldNormal
				* (PrimaryRadius * FrozenPreset.SatelliteCenterClearancePrimaryRatio);
		Candidate.E5TargetHalfExtentCM = FVector(FrozenPreset.TargetProxyRadiusCM);

		FABTSCalibrationGravitySnapshot Gravity;
		Gravity.PrimaryCenterWorld = PrimaryCenter;
		Gravity.PrimaryRadiusCM = PrimaryRadius;
		Gravity.PrimarySurfaceGravityCMPerSec2 = Config.PrimarySurfaceGravityCMPerSec2;
		Gravity.SatelliteCenterWorld = Candidate.SatelliteCenterWorld;
		Gravity.SatelliteRadiusCM = Candidate.SatelliteRadiusCM;
		Gravity.SatelliteSurfaceGravityCMPerSec2 = Candidate.SatelliteSurfaceGravityCMPerSec2;
		Gravity.FlightAirDragPerSecond = FrozenCatalog.FlightAirDragPerSecond;
		Gravity.bSatelliteGravityEnabled = true;
		FString TargetFailure;
		if (!FABTSSlingshotSatelliteCalibrationModel::BuildSatelliteTargetWorldTransform(
				Candidate.LaunchWorldLocation,
				Gravity,
				FrozenPreset,
				Candidate.E5TargetWorldTransform,
				&TargetFailure))
		{
			return ABTSM3R51SatellitePreviewPrivate::Reject(OutResult, EABTSM3MonthlySatellitePreviewRejectReason::TargetTransformFailed,
				TEXT("E5TargetTransform"), OutFailure);
		}
		const FVector TowardLaunch =
			(Candidate.LaunchWorldLocation - Candidate.SatelliteCenterWorld).GetSafeNormal();
		const FVector TowardTarget =
			(Candidate.E5TargetWorldTransform.GetLocation() - Candidate.SatelliteCenterWorld).GetSafeNormal();
		Candidate.bE5OnSatelliteBackside =
			FVector::DotProduct(TowardLaunch, TowardTarget) < 0.0;
		if (!Candidate.bE5OnSatelliteBackside)
		{
			return ABTSM3R51SatellitePreviewPrivate::Reject(OutResult, EABTSM3MonthlySatellitePreviewRejectReason::TargetTransformFailed,
				TEXT("E5NotBackside"), OutFailure);
		}
		Candidate.CandidateHash = static_cast<int64>(ComputeCandidateHash(Candidate));
	}

	if (OutResult.RetainedCandidates.Num() != SpatialResult.RetainedCandidates.Num())
	{
		return ABTSM3R51SatellitePreviewPrivate::Reject(OutResult, EABTSM3MonthlySatellitePreviewRejectReason::CandidateJoinMismatch,
			TEXT("CandidateCardinality"), OutFailure);
	}
	OutResult.bPreviewResultValid = true;
	OutResult.RejectReason = EABTSM3MonthlySatellitePreviewRejectReason::None;
	OutResult.ResultHash = static_cast<int64>(ComputeResultHash(OutResult));
	if (Config.bEmitPreviewLogs)
	{
		LogSummary(OutResult);
	}
	return true;
}

bool FABTSM3MonthlySatellitePreviewBuilder::Validate(
	const FABTSM3MonthlySatellitePreviewConfig& Config,
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlySpatialResult& SpatialResult,
	const FABTSM3MonthlySlingshotFieldResult& SlingshotFieldResult,
	const IABTSM3MonthlySatellitePreviewSurface& Surface,
	const FABTSM3MonthlySatellitePreviewResult& Result,
	EABTSM3MonthlySatellitePreviewRejectReason& OutReason,
	FString& OutFailure)
{
	OutReason = EABTSM3MonthlySatellitePreviewRejectReason::None;
	OutFailure.Reset();
	FABTSM3MonthlySatellitePreviewConfig QuietConfig = Config;
	QuietConfig.bEmitPreviewLogs = false;
	FABTSM3MonthlySatellitePreviewResult Expected;
	FString ExpectedFailure;
	if (!Build(
			SpatialResult.WorldSeed,
			QuietConfig,
			Cells,
			SpatialResult,
			SlingshotFieldResult,
			Surface,
			Expected,
			ExpectedFailure))
	{
		OutReason = Expected.RejectReason;
		OutFailure = FString::Printf(TEXT("Rebuild:%s"), *ExpectedFailure);
		return false;
	}
	if (!FABTSM3MonthlySatellitePreviewResult::StaticStruct()->CompareScriptStruct(
			&Expected, &Result, PPF_None))
	{
		OutReason = EABTSM3MonthlySatellitePreviewRejectReason::HashMismatch;
		OutFailure = TEXT("WholeStruct");
		return false;
	}
	return true;
}

const FABTSM3MonthlySatellitePreviewCandidate*
FABTSM3MonthlySatellitePreviewBuilder::FindCandidate(
	const FABTSM3MonthlySatellitePreviewResult& Result,
	const int32 SourceRouteCandidateId)
{
	return Result.RetainedCandidates.FindByPredicate(
		[SourceRouteCandidateId](const FABTSM3MonthlySatellitePreviewCandidate& Candidate)
		{
			return Candidate.SourceRouteCandidateId == SourceRouteCandidateId;
		});
}

uint64 FABTSM3MonthlySatellitePreviewBuilder::ComputeConfigHash(
	const FABTSM3MonthlySatellitePreviewConfig& Config,
	const uint64 TopologyHash)
{
	ABTSM3R51SatellitePreviewPrivate::FCanonicalHash64 Hash;
	Hash.AddInt32(SchemaVersion);
	Hash.AddInt32(GeneratorVersion);
	Hash.AddInt32(MonthlyLayoutPolicyVersion);
	Hash.AddUInt64(TopologyHash);
	Hash.AddBool(Config.bBuildSatellitePreview);
	Hash.AddInt32(Config.PracticeEncounterOrder);
	Hash.AddFloat(Config.PrimarySurfaceGravityCMPerSec2);
	Hash.AddFloat(Config.ReferencePouchHeightCM);
	Hash.AddInt32(Config.PlannerVersion);
	return Hash.Get();
}

uint64 FABTSM3MonthlySatellitePreviewBuilder::ComputeCandidateHash(
	const FABTSM3MonthlySatellitePreviewCandidate& Candidate)
{
	ABTSM3R51SatellitePreviewPrivate::FCanonicalHash64 Hash;
	Hash.AddInt32(Candidate.SourceRouteCandidateId);
	Hash.AddInt64(Candidate.SourceSpatialCandidateHash);
	Hash.AddInt64(Candidate.SourceSlingshotFieldCandidateHash);
	Hash.AddInt32(Candidate.PracticeEncounterId);
	Hash.AddInt64(Candidate.PracticeFieldHash);
	Hash.AddInt32(Candidate.ReferenceSlotACellId);
	Hash.AddInt32(Candidate.ReferenceSlotBCellId);
	Hash.AddInt64(Candidate.LaunchProfileHash);
	Hash.AddInt32(Candidate.SatellitePracticePresetVersion);
	Hash.AddInt64(Candidate.SatellitePracticePresetHash);
	Hash.AddVector(Candidate.LaunchWorldLocation);
	Hash.AddVector(Candidate.LaunchUpWorld);
	Hash.AddVector(Candidate.LaunchForwardWorld);
	Hash.AddVector(Candidate.LaunchRightWorld);
	Hash.AddVector(Candidate.SatelliteAnchorDirection);
	Hash.AddInt32(Candidate.SatelliteAnchorCellId);
	Hash.AddFloat(Candidate.SatelliteFacingCorrectionAzimuthDegrees);
	Hash.AddFloat(Candidate.SatelliteFacingErrorDegrees);
	Hash.AddVector(Candidate.SatelliteCenterWorld);
	Hash.AddFloat(Candidate.SatelliteRadiusCM);
	Hash.AddFloat(Candidate.SatelliteSurfaceGravityCMPerSec2);
	Hash.AddVector(Candidate.E5TargetWorldTransform.GetLocation());
	Hash.AddQuat(Candidate.E5TargetWorldTransform.GetRotation());
	Hash.AddVector(Candidate.E5TargetHalfExtentCM);
	Hash.AddBool(Candidate.bE5OnSatelliteBackside);
	return Hash.Get();
}

uint64 FABTSM3MonthlySatellitePreviewBuilder::ComputeResultHash(
	const FABTSM3MonthlySatellitePreviewResult& Result)
{
	ABTSM3R51SatellitePreviewPrivate::FCanonicalHash64 Hash;
	Hash.AddInt32(Result.SchemaVersion);
	Hash.AddInt32(Result.GeneratorVersion);
	Hash.AddInt32(Result.LayoutPolicyVersion);
	Hash.AddInt32(Result.WorldSeed);
	Hash.AddInt64(Result.TopologyHash);
	Hash.AddInt64(Result.SourceSpatialResultHash);
	Hash.AddInt64(Result.SourceSlingshotFieldResultHash);
	Hash.AddInt64(Result.ConfigHash);
	Hash.AddBool(Result.bPreviewResultValid);
	Hash.AddBool(Result.bMonthlyWorldAccepted);
	Hash.AddInt32(static_cast<int32>(Result.RejectReason));
	Hash.AddInt32(Result.RetainedCandidates.Num());
	for (const FABTSM3MonthlySatellitePreviewCandidate& Candidate : Result.RetainedCandidates)
	{
		Hash.AddInt64(Candidate.CandidateHash);
	}
	return Hash.Get();
}

void FABTSM3MonthlySatellitePreviewBuilder::LogSummary(
	const FABTSM3MonthlySatellitePreviewResult& Result)
{
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][PCG][SatellitePreview] Stage=M3R5.1 Seed=%d Valid=%d MonthlyAccepted=0 Candidates=%d SourceSpatial=%016llX SourceFields=%016llX Result=%016llX"),
		Result.WorldSeed,
		Result.bPreviewResultValid ? 1 : 0,
		Result.RetainedCandidates.Num(),
		static_cast<unsigned long long>(static_cast<uint64>(Result.SourceSpatialResultHash)),
		static_cast<unsigned long long>(static_cast<uint64>(Result.SourceSlingshotFieldResultHash)),
		static_cast<unsigned long long>(static_cast<uint64>(Result.ResultHash)));
}

const TCHAR* FABTSM3MonthlySatellitePreviewBuilder::GetRejectReasonName(
	const EABTSM3MonthlySatellitePreviewRejectReason Reason)
{
	switch (Reason)
	{
	case EABTSM3MonthlySatellitePreviewRejectReason::None: return TEXT("None");
	case EABTSM3MonthlySatellitePreviewRejectReason::NotEvaluated: return TEXT("NotEvaluated");
	case EABTSM3MonthlySatellitePreviewRejectReason::InvalidConfig: return TEXT("InvalidConfig");
	case EABTSM3MonthlySatellitePreviewRejectReason::InvalidTopology: return TEXT("InvalidTopology");
	case EABTSM3MonthlySatellitePreviewRejectReason::InvalidSpatialResult: return TEXT("InvalidSpatialResult");
	case EABTSM3MonthlySatellitePreviewRejectReason::InvalidSlingshotFieldResult: return TEXT("InvalidSlingshotFieldResult");
	case EABTSM3MonthlySatellitePreviewRejectReason::FrozenPresetMismatch: return TEXT("FrozenPresetMismatch");
	case EABTSM3MonthlySatellitePreviewRejectReason::CandidateJoinMismatch: return TEXT("CandidateJoinMismatch");
	case EABTSM3MonthlySatellitePreviewRejectReason::PracticeEncounterMissing: return TEXT("PracticeEncounterMissing");
	case EABTSM3MonthlySatellitePreviewRejectReason::ReferencePairMissing: return TEXT("ReferencePairMissing");
	case EABTSM3MonthlySatellitePreviewRejectReason::SurfaceQueryFailed: return TEXT("SurfaceQueryFailed");
	case EABTSM3MonthlySatellitePreviewRejectReason::TargetTransformFailed: return TEXT("TargetTransformFailed");
	case EABTSM3MonthlySatellitePreviewRejectReason::HashMismatch: return TEXT("HashMismatch");
	default: return TEXT("Unknown");
	}
}
