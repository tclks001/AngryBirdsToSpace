// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3MonthlySatellitePreview.h"

#include "ABTSRuntime.h"
#include "Calibration/ABTSSlingshotSatelliteCalibrationTypes.h"
#include "Planet/ABTSM2Planet.h"

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

bool ChooseReferencePair(
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlySlingshotField& Field,
	const int32 MaxCordLengthCM,
	const IABTSM3MonthlySatellitePreviewSurface& Surface,
	FABTSM3MonthlySatelliteSurfaceSample& OutA,
	FABTSM3MonthlySatelliteSurfaceSample& OutB,
	int32& OutACellId,
	int32& OutBCellId)
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
			const int64 MidpointDistanceMM = FMath::RoundToInt64(
				FVector::Distance(Midpoint, Pocket.WorldLocation) * 10.0);
			const int64 NegativeSpanMM = -FMath::RoundToInt64(SpanCM * 10.0);
			const int32 ACellId = FMath::Min(Field.SlotCellIds[AIndex], Field.SlotCellIds[BIndex]);
			const int32 BCellId = FMath::Max(Field.SlotCellIds[AIndex], Field.SlotCellIds[BIndex]);
			const bool bBetter = !bFound
				|| MidpointDistanceMM < BestMidpointDistanceMM
				|| (MidpointDistanceMM == BestMidpointDistanceMM
					&& NegativeSpanMM < BestNegativeSpanMM)
				|| (MidpointDistanceMM == BestMidpointDistanceMM
					&& NegativeSpanMM == BestNegativeSpanMM
					&& (ACellId < OutACellId
						|| (ACellId == OutACellId && BCellId < OutBCellId)));
			if (!bBetter)
			{
				continue;
			}
			bFound = true;
			BestMidpointDistanceMM = MidpointDistanceMM;
			BestNegativeSpanMM = NegativeSpanMM;
			OutACellId = ACellId;
			OutBCellId = BCellId;
			if (Field.SlotCellIds[AIndex] == ACellId)
			{
				OutA = A;
				OutB = B;
			}
			else
			{
				OutA = B;
				OutB = A;
			}
		}
	}
	return bFound;
}

FVector MakeStableTangent(
	const FVector& Up,
	const FVector& Preferred)
{
	FVector Forward = FVector::VectorPlaneProject(Preferred, Up);
	if (!Forward.Normalize())
	{
		const FVector Axis = FMath::Abs(Up.Z) < 0.9
			? FVector::UpVector
			: FVector::ForwardVector;
		Forward = FVector::CrossProduct(Axis, Up);
		Forward.Normalize();
	}
	return Forward;
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

		FABTSM3MonthlySatelliteSurfaceSample SlotA;
		FABTSM3MonthlySatelliteSurfaceSample SlotB;
		int32 SlotACellId = INDEX_NONE;
		int32 SlotBCellId = INDEX_NONE;
		if (!ABTSM3R51SatellitePreviewPrivate::ChooseReferencePair(
				Cells,
				*PracticeField,
				SlingshotFieldResult.MaxCordLengthCM,
				Surface,
				SlotA,
				SlotB,
				SlotACellId,
				SlotBCellId))
		{
			return ABTSM3R51SatellitePreviewPrivate::Reject(OutResult, EABTSM3MonthlySatellitePreviewRejectReason::ReferencePairMissing,
				TEXT("E5DistanceValidReferencePair"), OutFailure);
		}

		const FVector PrimaryCenter = Surface.GetPrimaryCenterWorld();
		const float PrimaryRadius = Surface.GetPrimaryRadiusCM();
		const FVector PairMidpoint = (SlotA.WorldLocation + SlotB.WorldLocation) * 0.5;
		FVector LaunchUp = PairMidpoint - PrimaryCenter;
		if (!LaunchUp.Normalize())
		{
			return ABTSM3R51SatellitePreviewPrivate::Reject(OutResult, EABTSM3MonthlySatellitePreviewRejectReason::SurfaceQueryFailed,
				TEXT("LaunchUp"), OutFailure);
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
		FVector LaunchForward = ABTSM3R51SatellitePreviewPrivate::MakeStableTangent(
			LaunchUp,
			TargetAnchor.WorldLocation - PairMidpoint);
		LaunchForward = LaunchForward.RotateAngleAxis(
			FrozenPreset.SatelliteAnchorAzimuthDegrees,
			LaunchUp);
		LaunchForward.Normalize();
		FVector LaunchRight = FVector::CrossProduct(LaunchUp, LaunchForward);
		LaunchRight.Normalize();
		LaunchForward = FVector::CrossProduct(LaunchRight, LaunchUp).GetSafeNormal();

		const float ArcRadians = FMath::DegreesToRadians(FrozenPreset.SatelliteAnchorArcDegrees);
		const FVector SatelliteAnchorDirection =
			(LaunchUp * FMath::Cos(ArcRadians)
				+ LaunchForward * FMath::Sin(ArcRadians)).GetSafeNormal();
		FABTSM3MonthlySatelliteSurfaceSample SatelliteAnchor;
		if (!Surface.QuerySurface(SatelliteAnchorDirection, SatelliteAnchor)
			|| !ABTSM3R51SatellitePreviewPrivate::IsFiniteVector(SatelliteAnchor.WorldLocation)
			|| !ABTSM3R51SatellitePreviewPrivate::IsFiniteVector(SatelliteAnchor.WorldNormal)
			|| !SatelliteAnchor.WorldNormal.Normalize())
		{
			return ABTSM3R51SatellitePreviewPrivate::Reject(OutResult, EABTSM3MonthlySatellitePreviewRejectReason::SurfaceQueryFailed,
				TEXT("SatelliteAnchor"), OutFailure);
		}

		FABTSM3MonthlySatellitePreviewCandidate& Candidate =
			OutResult.RetainedCandidates.AddDefaulted_GetRef();
		Candidate.SourceRouteCandidateId = SpatialCandidate.SourceRouteCandidateId;
		Candidate.SourceSpatialCandidateHash = SpatialCandidate.SpatialCandidateHash;
		Candidate.SourceSlingshotFieldCandidateHash = FieldCandidate->CandidateHash;
		Candidate.PracticeEncounterId = PracticeEncounter->Contract.EncounterId;
		Candidate.PracticeFieldHash = PracticeField->FieldHash;
		Candidate.ReferenceSlotACellId = SlotACellId;
		Candidate.ReferenceSlotBCellId = SlotBCellId;
		Candidate.LaunchProfileHash = LaunchProfileHash;
		Candidate.SatellitePracticePresetVersion = FrozenPreset.Version;
		Candidate.SatellitePracticePresetHash = PresetHash;
		Candidate.LaunchUpWorld = LaunchUp;
		Candidate.LaunchForwardWorld = LaunchForward;
		Candidate.LaunchRightWorld = LaunchRight;
		Candidate.LaunchWorldLocation = PairMidpoint
			+ LaunchUp * Config.ReferencePouchHeightCM;
		Candidate.SatelliteAnchorDirection = SatelliteAnchorDirection;
		Candidate.SatelliteAnchorCellId = SatelliteAnchor.NearestCellId;
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
