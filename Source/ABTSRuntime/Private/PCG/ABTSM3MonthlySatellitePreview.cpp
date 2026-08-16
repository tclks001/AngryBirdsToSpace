// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3MonthlySatellitePreview.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM73BuildingFreezeV3.h"
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

bool ResolveSatelliteAnchorAtCorrection(
	const IABTSM3MonthlySatellitePreviewSurface& Surface,
	const FVector& LaunchWorld,
	const FVector& LaunchUp,
	const FVector& LaunchForward,
	const FABTSSatellitePracticePreset& Preset,
	const float CorrectionDegrees,
	FVector& OutAnchorDirection,
	FABTSM3MonthlySatelliteSurfaceSample& OutAnchor,
	float& OutFacingErrorDegrees)
{
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
		return false;
	}
	const FVector SatelliteCenter = Anchor.WorldLocation
		+ Anchor.WorldNormal * CenterClearanceCM;
	const FVector SightTangent = FVector::VectorPlaneProject(
		SatelliteCenter - LaunchWorld,
		LaunchUp).GetSafeNormal();
	if (SightTangent.IsNearlyZero())
	{
		return false;
	}
	OutAnchorDirection = AnchorDirection;
	OutAnchor = Anchor;
	OutFacingErrorDegrees = FMath::RadiansToDegrees(
		FMath::Acos(FMath::Clamp(
			FVector::DotProduct(SightTangent, LaunchForward),
			-1.0f,
			1.0f)));
	return FMath::IsFinite(OutFacingErrorDegrees);
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
	bool bFound = false;
	const auto Evaluate = [&](const float CorrectionDegrees)
	{
		FVector AnchorDirection;
		FABTSM3MonthlySatelliteSurfaceSample Anchor;
		float FacingErrorDegrees = 180.0f;
		if (!ResolveSatelliteAnchorAtCorrection(
				Surface, LaunchWorld, LaunchUp, LaunchForward, Preset,
				CorrectionDegrees, AnchorDirection, Anchor,
				FacingErrorDegrees))
		{
			return;
		}
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

struct FFrozenE1BuildingModuleSource
{
	struct FBuildingModule
	{
		int32 BrickId = INDEX_NONE;
		FTransform SiteLocalTransform = FTransform::Identity;
		FVector HalfExtentCM = FVector::ZeroVector;
	};
	uint64 DescriptorHash = 0;
	FTransform SiteLocalTransform = FTransform::Identity;
	FVector HalfExtentCM = FVector::ZeroVector;
	TArray<FBuildingModule> BuildingModules;
};

struct FResolvedProductionTarget
{
	FVector AnchorDirection = FVector::ZeroVector;
	FABTSM3MonthlySatelliteSurfaceSample Anchor;
	float FacingErrorDegrees = 180.0f;
	float CorrectionDegrees = 0.0f;
	float SiteYawDegrees = 0.0f;
	FVector SatelliteCenterWorld = FVector::ZeroVector;
	FTransform SiteWorldTransform = FTransform::Identity;
	FTransform TargetWorldTransform = FTransform::Identity;
	FVector TargetHalfExtentCM = FVector::ZeroVector;
	uint64 DescriptorHash = 0;
	int32 TargetModuleId = INDEX_NONE;
	uint64 TargetIdentityHash = 0;
	FABTSCalibrationGravitySnapshot Gravity;
	FABTSCalibrationSweepSummary TrajectorySummary;
};

uint64 ComputeProductionTargetIdentityHashPrivate(
	const uint64 DescriptorHash,
	const FTransform& SiteWorldTransform,
	const FTransform& TargetWorldTransform,
	const FVector& TargetHalfExtentCM)
{
	FCanonicalHash64 Hash;
	Hash.AddInt32(1);
	Hash.AddUInt64(DescriptorHash);
	Hash.AddVector(SiteWorldTransform.GetLocation());
	Hash.AddQuat(SiteWorldTransform.GetRotation());
	Hash.AddVector(TargetWorldTransform.GetLocation());
	Hash.AddQuat(TargetWorldTransform.GetRotation());
	Hash.AddVector(TargetHalfExtentCM);
	return Hash.Get();
}

bool ResolveFrozenE1BuildingModuleSource(
	FFrozenE1BuildingModuleSource& OutSource,
	FString& OutFailure)
{
	OutSource = FFrozenE1BuildingModuleSource();
	FABTSM73BuildingFreezeV3Descriptor Descriptor;
	if (!FABTSM73BuildingFreezeV3::DeriveAndValidate(
			EABTSM73BeamDemoBuilding::E1ColumnBreak,
			Descriptor,
			OutFailure)
		|| Descriptor.DescriptorHash == 0
		|| Descriptor.Caps.Num() != 1)
	{
		OutFailure = FString::Printf(
			TEXT("FrozenE1Descriptor:%s"), *OutFailure);
		return false;
	}
	const FABTSM73BuildingFreezeV3CapBinding& Cap = Descriptor.Caps[0];
	const FABTSM73BuildingFreezeV3FrozenIdentity* FrozenIdentity =
		FABTSM73BuildingFreezeV3::GetFrozenIdentities().FindByPredicate(
			[](const FABTSM73BuildingFreezeV3FrozenIdentity& Identity)
			{
				return Identity.ManifestEntryId
					== EABTSM73BeamDemoBuilding::E1ColumnBreak;
			});
	if (FrozenIdentity == nullptr
		|| FrozenIdentity->DescriptorHash != Descriptor.DescriptorHash
		|| Cap.BrickSpec.Material != EABTSM7BuildingMaterial::Crystal
		|| !Cap.SiteLocalTransform.IsValid()
		|| Cap.BrickSpec.DimensionsCM.GetMin() <= 0.0f)
	{
		OutFailure = TEXT("FrozenE1BuildingDescriptorContract");
		return false;
	}
	OutSource.DescriptorHash = Descriptor.DescriptorHash;
	for (const FABTSM73BeamD1BrickBinding& Brick : Descriptor.Bricks)
	{
		if (Brick.BrickId == INDEX_NONE
			|| !Brick.LocalTransform.IsValid()
			|| Brick.BrickSpec.DimensionsCM.GetMin() <= 0.0f)
		{
			OutFailure = TEXT("FrozenE1BuildingModuleContract");
			return false;
		}
		FFrozenE1BuildingModuleSource::FBuildingModule& Module =
			OutSource.BuildingModules.AddDefaulted_GetRef();
		Module.BrickId = Brick.BrickId;
		Module.SiteLocalTransform = Brick.LocalTransform;
		Module.HalfExtentCM = Brick.BrickSpec.DimensionsCM * 0.5f;
	}
	if (OutSource.BuildingModules.IsEmpty())
	{
		OutFailure = TEXT("FrozenE1BuildingModulesEmpty");
		return false;
	}
	// Every FrozenE1BuildingModules candidate must carry real descriptor module
	// geometry.  The production candidate deterministically replaces this
	// stable first-module default with the certified member selected below.
	OutSource.SiteLocalTransform =
		OutSource.BuildingModules[0].SiteLocalTransform;
	OutSource.HalfExtentCM = OutSource.BuildingModules[0].HalfExtentCM;
	return true;
}

bool BuildPreviewM6LaunchFrame(
	const FReferenceSlingshotFrame& ReferenceFrame,
	const FVector& PreferredForward,
	const FABTSM6LaunchProfileCatalog& Catalog,
	FABTSM6CalibrationLaunchFrame& OutFrame)
{
	OutFrame = FABTSM6CalibrationLaunchFrame();
	const FVector SlingCenter = ReferenceFrame.SlingCenterWorld;
	const FVector SlingUp = ReferenceFrame.SlingUpWorld.GetSafeNormal();
	FVector SlingRight = ReferenceFrame.SlingRightWorld.GetSafeNormal();
	FVector SlingForward = FVector::CrossProduct(
		SlingRight, SlingUp).GetSafeNormal();
	const FVector PreferredTangent = FVector::VectorPlaneProject(
		PreferredForward, SlingUp).GetSafeNormal();
	if (SlingUp.IsNearlyZero() || SlingRight.IsNearlyZero()
		|| SlingForward.IsNearlyZero() || PreferredTangent.IsNearlyZero())
	{
		return false;
	}
	if (FVector::DotProduct(SlingForward, PreferredTangent) < 0.0f)
	{
		SlingForward *= -1.0f;
		SlingRight *= -1.0f;
	}
	const float PitchRadians = FMath::DegreesToRadians(
		Catalog.AimCameraPitchDegrees);
	const FVector CameraLocation = SlingCenter
		+ (-SlingForward * FMath::Cos(PitchRadians)
			+ SlingUp * FMath::Sin(PitchRadians)).GetSafeNormal()
			* Catalog.AimCameraDistanceCM;
	const FVector AimTarget = SlingCenter
		+ SlingForward * Catalog.AimTargetForwardDistanceCM
		+ SlingUp * Catalog.AimTargetHeightCM;
	const FVector AimPlaneNormal = (AimTarget - CameraLocation).GetSafeNormal();
	const FVector AimInPlaneAxis = FVector::VectorPlaneProject(
		SlingUp, AimPlaneNormal).GetSafeNormal();
	FVector AimOutOfPlaneAxis = FVector::CrossProduct(
		AimInPlaneAxis, AimPlaneNormal).GetSafeNormal();
	const FVector PreferredRight = FVector::CrossProduct(
		SlingUp, SlingForward).GetSafeNormal();
	if (FVector::DotProduct(AimOutOfPlaneAxis, PreferredRight) < 0.0f)
	{
		AimOutOfPlaneAxis *= -1.0f;
	}
	if (AimPlaneNormal.IsNearlyZero() || AimInPlaneAxis.IsNearlyZero()
		|| AimOutOfPlaneAxis.IsNearlyZero())
	{
		return false;
	}
	OutFrame.SlingCenterWorld = SlingCenter;
	OutFrame.SlingUpWorld = SlingUp;
	OutFrame.SlingForwardWorld = SlingForward;
	OutFrame.SlingRightWorld = SlingRight;
	OutFrame.AimPlaneNormalWorld = AimPlaneNormal;
	OutFrame.AimInPlaneAxisWorld = AimInPlaneAxis;
	OutFrame.AimOutOfPlaneAxisWorld = AimOutOfPlaneAxis;
	OutFrame.RestPouchWorldLocation = ReferenceFrame.RestPouchWorld;
	OutFrame.BirdInPouchOffsetCM = 20.0f;
	return true;
}

bool IsM3ProductionTrajectoryCertified(
	const FABTSCalibrationSweepSummary& Summary,
	const FABTSSatellitePracticePreset& FrozenPreset)
{
	return Summary.LargestSuccessIslandSamples
			>= FMath::Max(1, FrozenPreset.MinimumSuccessIslandSamples)
		&& Summary.bIslandSpansAimNeighbors
		&& Summary.GravityDependentHits > 0
		&& Summary.SimpleFullPowerHits == 0
		&& Summary.ReinforcedOutsideCertifiedPullHits == 0
		&& Summary.SuccessPullMinimum + KINDA_SMALL_NUMBER
			>= FrozenPreset.PullMinimum
		&& Summary.SuccessPullMaximum
			<= FrozenPreset.PullMaximum + KINDA_SMALL_NUMBER
		&& Summary.MinimumGravityOffMissCM + KINDA_SMALL_NUMBER
			>= FrozenPreset.GravityOffMinimumMissCM;
}

bool ResolveProductionTargetAtCorrection(
	const IABTSM3MonthlySatellitePreviewSurface& Surface,
	const FVector& LaunchWorld,
	const FVector& LaunchUp,
	const FVector& LaunchForward,
	const FABTSSatellitePracticePreset& FrozenPreset,
	const FABTSM6LaunchProfileCatalog& FrozenCatalog,
	const float PrimarySurfaceGravityCMPerSec2,
	const float CorrectionDegrees,
	const float SiteYawDegrees,
	const EABTSM3MonthlySatelliteTargetAuthority TargetAuthority,
	const FFrozenE1BuildingModuleSource* FrozenE1,
	FResolvedProductionTarget& OutTarget,
	FString& OutFailure)
{
	OutTarget = FResolvedProductionTarget();
	OutTarget.CorrectionDegrees = CorrectionDegrees;
	OutTarget.SiteYawDegrees = SiteYawDegrees;
	if (!ResolveSatelliteAnchorAtCorrection(
			Surface, LaunchWorld, LaunchUp, LaunchForward, FrozenPreset,
			CorrectionDegrees, OutTarget.AnchorDirection, OutTarget.Anchor,
			OutTarget.FacingErrorDegrees)
		|| OutTarget.FacingErrorDegrees > 5.0f)
	{
		OutFailure = TEXT("SatelliteCorrectionFacing");
		return false;
	}
	const float PrimaryRadius = Surface.GetPrimaryRadiusCM();
	OutTarget.SatelliteCenterWorld = OutTarget.Anchor.WorldLocation
		+ OutTarget.Anchor.WorldNormal
			* (PrimaryRadius
				* FrozenPreset.SatelliteCenterClearancePrimaryRatio);
	OutTarget.Gravity.PrimaryCenterWorld = Surface.GetPrimaryCenterWorld();
	OutTarget.Gravity.PrimaryRadiusCM = PrimaryRadius;
	OutTarget.Gravity.PrimarySurfaceGravityCMPerSec2 =
		PrimarySurfaceGravityCMPerSec2;
	OutTarget.Gravity.SatelliteCenterWorld = OutTarget.SatelliteCenterWorld;
	OutTarget.Gravity.SatelliteRadiusCM =
		PrimaryRadius * FrozenPreset.SatelliteRadiusPrimaryRatio;
	OutTarget.Gravity.SatelliteSurfaceGravityCMPerSec2 =
		PrimarySurfaceGravityCMPerSec2
		* FrozenPreset.SatelliteSurfaceGravityPrimaryRatio;
	OutTarget.Gravity.FlightAirDragPerSecond =
		FrozenCatalog.FlightAirDragPerSecond;
	OutTarget.Gravity.bSatelliteGravityEnabled = true;

	FTransform LegacyTargetTransform = FTransform::Identity;
	if (!FABTSSlingshotSatelliteCalibrationModel::
			BuildSatelliteTargetWorldTransform(
				LaunchWorld,
				OutTarget.Gravity,
				FrozenPreset,
				LegacyTargetTransform,
				&OutFailure))
	{
		return false;
	}
	const FVector SiteUp = (LegacyTargetTransform.GetLocation()
		- OutTarget.SatelliteCenterWorld).GetSafeNormal();
	const FVector SiteX = LegacyTargetTransform.GetUnitAxis(EAxis::X)
		.GetSafeNormal().RotateAngleAxis(SiteYawDegrees, SiteUp).GetSafeNormal();
	const FVector SiteY = FVector::CrossProduct(SiteUp, SiteX).GetSafeNormal();
	if (SiteUp.IsNearlyZero() || SiteX.IsNearlyZero() || SiteY.IsNearlyZero()
		|| FMath::Abs(FVector::DotProduct(SiteUp, SiteX)) > 0.001f)
	{
		OutFailure = TEXT("SatelliteSiteCarrierFrame");
		return false;
	}
	OutTarget.SiteWorldTransform = FTransform(
		FRotationMatrix::MakeFromXZ(SiteX, SiteUp).ToQuat(),
		OutTarget.SatelliteCenterWorld
			+ SiteUp * OutTarget.Gravity.SatelliteRadiusCM,
		FVector::OneVector);
	if (TargetAuthority
		== EABTSM3MonthlySatelliteTargetAuthority::FrozenE1BuildingModules)
	{
		if (FrozenE1 == nullptr)
		{
			OutFailure = TEXT("FrozenE1SourceMissing");
			return false;
		}
		OutTarget.DescriptorHash = FrozenE1->DescriptorHash;
		OutTarget.TargetWorldTransform =
			FrozenE1->SiteLocalTransform * OutTarget.SiteWorldTransform;
		OutTarget.TargetHalfExtentCM = FrozenE1->HalfExtentCM;
		OutTarget.TargetModuleId = FrozenE1->BuildingModules[0].BrickId;
	}
	else
	{
		OutTarget.TargetWorldTransform = LegacyTargetTransform;
		OutTarget.TargetHalfExtentCM = FVector(
			FrozenPreset.TargetProxyRadiusCM);
	}
	OutTarget.TargetIdentityHash =
		ComputeProductionTargetIdentityHashPrivate(
			OutTarget.DescriptorHash,
			OutTarget.SiteWorldTransform,
			OutTarget.TargetWorldTransform,
			OutTarget.TargetHalfExtentCM);
	return OutTarget.TargetWorldTransform.IsValid()
		&& OutTarget.TargetHalfExtentCM.GetMin() > 0.0f;
}

FABTSCalibrationScenario MakeProductionTargetScenario(
	const FABTSM6CalibrationLaunchFrame& LaunchFrame,
	const FResolvedProductionTarget& Target)
{
	FABTSCalibrationScenario Scenario;
	Scenario.LaunchWorldLocation = LaunchFrame.RestPouchWorldLocation;
	Scenario.LaunchFrame = LaunchFrame;
	Scenario.TargetWorldLocation = Target.TargetWorldTransform.GetLocation();
	Scenario.TargetWorldTransform = Target.TargetWorldTransform;
	Scenario.TargetHalfExtentCM = Target.TargetHalfExtentCM;
	Scenario.TargetProxyRadiusCM = Target.TargetHalfExtentCM.GetMax();
	Scenario.Gravity = Target.Gravity;
	return Scenario;
}

bool SelectAndCertifyFrozenE1Target(
	const IABTSM3MonthlySatellitePreviewSurface& Surface,
	const FVector& LaunchWorld,
	const FVector& LaunchUp,
	const FVector& LaunchForward,
	const FABTSM6CalibrationLaunchFrame& LaunchFrame,
	const FABTSSatellitePracticePreset& FrozenPreset,
	const FABTSM6LaunchProfileCatalog& FrozenCatalog,
	const float PrimarySurfaceGravityCMPerSec2,
	const FFrozenE1BuildingModuleSource& FrozenE1,
	const float InitialCorrectionDegrees,
	FResolvedProductionTarget& OutTarget,
	FString& OutFailure)
{
	constexpr int32 ProductionAimInPlaneSamples = 61;
	constexpr int32 ProductionAimOutOfPlaneSamples = 31;
	constexpr int32 ModuleSelectionAndCertificationPasses = 2;
	FABTSSatellitePracticePreset ProductionPreset = FrozenPreset;
	ProductionPreset.AimInPlaneSampleCount = ProductionAimInPlaneSamples;
	ProductionPreset.AimOutOfPlaneSampleCount =
		ProductionAimOutOfPlaneSamples;
	if (!ResolveProductionTargetAtCorrection(
			Surface, LaunchWorld, LaunchUp, LaunchForward, FrozenPreset,
			FrozenCatalog, PrimarySurfaceGravityCMPerSec2,
			InitialCorrectionDegrees, 0.0f,
			EABTSM3MonthlySatelliteTargetAuthority::FrozenE1BuildingModules,
			&FrozenE1, OutTarget, OutFailure))
	{
		return false;
	}
	const FABTSM6LaunchProfile* Reinforced =
		FABTSSlingshotSatelliteCalibrationModel::FindProfile(
			FrozenCatalog, EABTSSlingshotTier::Reinforced);
	if (Reinforced == nullptr)
	{
		OutFailure = TEXT("FrozenReinforcedProfile");
		return false;
	}
	FResolvedProductionTarget LegacyProxyTarget;
	if (!ResolveProductionTargetAtCorrection(
			Surface, LaunchWorld, LaunchUp, LaunchForward, FrozenPreset,
			FrozenCatalog, PrimarySurfaceGravityCMPerSec2,
			InitialCorrectionDegrees, 0.0f,
			EABTSM3MonthlySatelliteTargetAuthority::LegacyCalibrationProxy,
			nullptr, LegacyProxyTarget, OutFailure))
	{
		return false;
	}
	const FVector LegacyProjectionDirection =
		(LegacyProxyTarget.TargetWorldTransform.GetLocation()
			- LegacyProxyTarget.SatelliteCenterWorld).GetSafeNormal();
	const FVector ExpectedProjectedSite = LegacyProxyTarget.SatelliteCenterWorld
		+ LegacyProjectionDirection * LegacyProxyTarget.Gravity.SatelliteRadiusCM;
	const bool bFixedSiteIsLegacyProjection =
		!LegacyProjectionDirection.IsNearlyZero()
		&& LegacyProxyTarget.SiteWorldTransform.GetLocation().Equals(
			ExpectedProjectedSite, 0.001f)
		&& OutTarget.SiteWorldTransform.GetLocation().Equals(
			ExpectedProjectedSite, 0.001f)
		&& OutTarget.SatelliteCenterWorld.Equals(
			LegacyProxyTarget.SatelliteCenterWorld, 0.001f)
		&& FMath::IsNearlyEqual(
			OutTarget.CorrectionDegrees, InitialCorrectionDegrees, 0.0001f);
	if (!bFixedSiteIsLegacyProjection)
	{
		OutFailure = TEXT("FrozenE1SiteIsNotLegacyProxyRadialProjection");
		return false;
	}
	const FABTSCalibrationSweepSummary LegacyProxySummary =
		FABTSSlingshotSatelliteCalibrationModel::RunSuccessIslandSweep(
			MakeProductionTargetScenario(LaunchFrame, LegacyProxyTarget),
			FrozenCatalog,
			ProductionPreset);
	if (!IsM3ProductionTrajectoryCertified(LegacyProxySummary, FrozenPreset))
	{
		OutFailure = FString::Printf(
			TEXT("LegacyProxySeedNotCertified:Hash=%016llX:Island=%d"),
			static_cast<unsigned long long>(LegacyProxySummary.ResultHash),
			LegacyProxySummary.LargestSuccessIslandSamples);
		return false;
	}
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M3R5.1][FrozenE1FixedSiteOracle] ProjectionExact=1 CorrectionFixed=%.3f SatelliteCenter=%s ProjectedSite=%s LegacyProxy=%s LegacySeedHash=%016llX LegacyIsland=%d LegacyAim=[%.1f,%.1f] LegacyBestAim=(%.1f,%.1f) LegacyBestPull=%.3f"),
		InitialCorrectionDegrees,
		*LegacyProxyTarget.SatelliteCenterWorld.ToCompactString(),
		*ExpectedProjectedSite.ToCompactString(),
		*LegacyProxyTarget.TargetWorldTransform.GetLocation().ToCompactString(),
		static_cast<unsigned long long>(LegacyProxySummary.ResultHash),
		LegacyProxySummary.LargestSuccessIslandSamples,
		LegacyProxySummary.SuccessAimInPlaneMinimumCM,
		LegacyProxySummary.SuccessAimInPlaneMaximumCM,
		LegacyProxySummary.BestGravityOnAimInPlaneCM,
		LegacyProxySummary.BestGravityOnAimOutOfPlaneCM,
		LegacyProxySummary.BestGravityOnPullAlpha);
	TArray<float> ReachablePulls;
	ReachablePulls.Add(0.0f);
	ReachablePulls.Add(1.0f);
	for (int32 Notch = -1000; Notch <= 1000; ++Notch)
	{
		const float Pull = Reinforced->InitialPullAlpha
			+ Reinforced->PullPowerWheelStep * static_cast<float>(Notch);
		if (Pull >= -KINDA_SMALL_NUMBER && Pull <= 1.0f + KINDA_SMALL_NUMBER)
		{
			ReachablePulls.AddUnique(FMath::Clamp(Pull, 0.0f, 1.0f));
		}
	}
	ReachablePulls.Sort();
	TArray<float> CertifiedPulls;
	for (const float Pull : ReachablePulls)
	{
		if (Pull + KINDA_SMALL_NUMBER >= FrozenPreset.PullMinimum
			&& Pull <= FrozenPreset.PullMaximum + KINDA_SMALL_NUMBER)
		{
			CertifiedPulls.Add(Pull);
		}
	}
	struct FLegacyProxyTrajectorySeed
	{
		int32 PullIndex = INDEX_NONE;
		int32 AimOutIndex = INDEX_NONE;
		int32 AimInIndex = INDEX_NONE;
		FVector BirdWorld = FVector::ZeroVector;
		FVector InitialVelocity = FVector::ZeroVector;
	};
	TArray<FLegacyProxyTrajectorySeed> LegacySeeds;
	const auto SampleRange = [](const float Minimum, const float Maximum,
		const int32 Index, const int32 Count)
	{
		return Count <= 1
			? Minimum
			: FMath::Lerp(Minimum, Maximum,
				static_cast<float>(Index) / static_cast<float>(Count - 1));
	};
	const FABTSCalibrationScenario LegacyScenario =
		MakeProductionTargetScenario(LaunchFrame, LegacyProxyTarget);
	for (int32 PullIndex = 0; PullIndex < CertifiedPulls.Num(); ++PullIndex)
	{
		for (int32 AimOutIndex = 0;
			AimOutIndex < ProductionAimOutOfPlaneSamples; ++AimOutIndex)
		{
			const float AimOut = SampleRange(
				ProductionPreset.AimOutOfPlaneMinimumCM,
				ProductionPreset.AimOutOfPlaneMaximumCM,
				AimOutIndex, ProductionAimOutOfPlaneSamples);
			for (int32 AimInIndex = 0;
				AimInIndex < ProductionAimInPlaneSamples; ++AimInIndex)
			{
				const float AimIn = SampleRange(
					ProductionPreset.AimInPlaneMinimumCM,
					ProductionPreset.AimInPlaneMaximumCM,
					AimInIndex, ProductionAimInPlaneSamples);
				if (FVector2D(AimIn, AimOut).Size()
					> Reinforced->MaximumAimPlaneOffsetCM + KINDA_SMALL_NUMBER)
				{
					continue;
				}
				FVector BirdWorld;
				FVector InitialVelocity;
				if (!FABTSSlingshotSatelliteCalibrationModel::BuildM6LaunchSample(
						LaunchFrame, *Reinforced, AimIn, AimOut,
						CertifiedPulls[PullIndex], BirdWorld, InitialVelocity))
				{
					continue;
				}
				FABTSCalibrationScenario SampleScenario = LegacyScenario;
				SampleScenario.LaunchWorldLocation = BirdWorld;
				const FABTSCalibrationTrajectoryResult GravityOn =
					FABTSSlingshotSatelliteCalibrationModel::IntegrateTrajectory(
						SampleScenario, InitialVelocity, ProductionPreset, true);
				if (GravityOn.Outcome
					!= EABTSCalibrationTrajectoryOutcome::TargetHit)
				{
					continue;
				}
				const FABTSCalibrationTrajectoryResult GravityOff =
					FABTSSlingshotSatelliteCalibrationModel::IntegrateTrajectory(
						SampleScenario, InitialVelocity, ProductionPreset, false);
				if (GravityOff.Outcome
						== EABTSCalibrationTrajectoryOutcome::TargetHit
					|| GravityOff.ClosestTargetClearanceCM + KINDA_SMALL_NUMBER
						< FrozenPreset.GravityOffMinimumMissCM)
				{
					continue;
				}
				FLegacyProxyTrajectorySeed& Seed = LegacySeeds.AddDefaulted_GetRef();
				Seed.PullIndex = PullIndex;
				Seed.AimOutIndex = AimOutIndex;
				Seed.AimInIndex = AimInIndex;
				Seed.BirdWorld = BirdWorld;
				Seed.InitialVelocity = InitialVelocity;
			}
		}
	}
	if (LegacySeeds.Num() != LegacyProxySummary.GravityDependentHits
		|| LegacySeeds.IsEmpty())
	{
		OutFailure = FString::Printf(
			TEXT("LegacyProxySeedEnumerationMismatch:Expected=%d:Actual=%d"),
			LegacyProxySummary.GravityDependentHits, LegacySeeds.Num());
		return false;
	}
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M3R5.1][FrozenE1LegacySeedSet] Exact=1 Samples=%d Pulls=%d Grid=%dx%d"),
		LegacySeeds.Num(), CertifiedPulls.Num(),
		ProductionAimInPlaneSamples, ProductionAimOutOfPlaneSamples);

	for (int32 Iteration = 0;
		Iteration < ModuleSelectionAndCertificationPasses;
		++Iteration)
	{
		if (Iteration > 0)
		{
			OutTarget.TrajectorySummary =
				FABTSSlingshotSatelliteCalibrationModel::RunSuccessIslandSweep(
					MakeProductionTargetScenario(LaunchFrame, OutTarget),
					FrozenCatalog,
					ProductionPreset);
			UE_LOG(LogABTSRuntime, Display,
				TEXT("[ABTS][M3R5.1][FrozenE1BuildingModuleCertification] BrickId=%d Correction=%.3f SiteYaw=%.3f ReinforcedHits=%d GravityDependentHits=%d Island=%d AimNeighbors=%d SimpleHits=%d OutsidePullHits=%d Pull=[%.3f,%.3f] GravityOffMiss=%.1f Clearance=%.1f BestAim=(%.1f,%.1f) BestPull=%.3f Hash=%016llX"),
				OutTarget.TargetModuleId,
				OutTarget.CorrectionDegrees,
				OutTarget.SiteYawDegrees,
				OutTarget.TrajectorySummary.ReinforcedGravityOnHits,
				OutTarget.TrajectorySummary.GravityDependentHits,
				OutTarget.TrajectorySummary.LargestSuccessIslandSamples,
				OutTarget.TrajectorySummary.bIslandSpansAimNeighbors ? 1 : 0,
				OutTarget.TrajectorySummary.SimpleFullPowerHits,
				OutTarget.TrajectorySummary.ReinforcedOutsideCertifiedPullHits,
				OutTarget.TrajectorySummary.SuccessPullMinimum,
				OutTarget.TrajectorySummary.SuccessPullMaximum,
				OutTarget.TrajectorySummary.MinimumGravityOffMissCM,
				OutTarget.TrajectorySummary.MinimumGravityOnTargetClearanceCM,
				OutTarget.TrajectorySummary.BestGravityOnAimInPlaneCM,
				OutTarget.TrajectorySummary.BestGravityOnAimOutOfPlaneCM,
				OutTarget.TrajectorySummary.BestGravityOnPullAlpha,
				static_cast<unsigned long long>(
					OutTarget.TrajectorySummary.ResultHash));
			if (IsM3ProductionTrajectoryCertified(
					OutTarget.TrajectorySummary, FrozenPreset))
			{
				return true;
			}
			break;
		}

		bool bFoundProbe = false;
		bool bBestProbeGate = false;
		int32 BestProbeIsland = -1;
		bool bBestProbeAimSpan = false;
		int64 BestClearanceMilliCM = MAX_int64;
		int64 BestFacingMicroDegrees = MAX_int64;
		int32 BestModuleId = MAX_int32;
		FResolvedProductionTarget BestProbeTarget;
		for (const FFrozenE1BuildingModuleSource::FBuildingModule& Module
			: FrozenE1.BuildingModules)
		{
				FResolvedProductionTarget ProbeTarget = OutTarget;
				ProbeTarget.TargetWorldTransform = Module.SiteLocalTransform
					* OutTarget.SiteWorldTransform;
				ProbeTarget.TargetHalfExtentCM = Module.HalfExtentCM;
				ProbeTarget.TargetModuleId = Module.BrickId;
				ProbeTarget.TargetIdentityHash =
					ComputeProductionTargetIdentityHashPrivate(
						FrozenE1.DescriptorHash,
						ProbeTarget.SiteWorldTransform,
						ProbeTarget.TargetWorldTransform,
						ProbeTarget.TargetHalfExtentCM);
				if (!ProbeTarget.SiteWorldTransform.GetLocation().Equals(
						ExpectedProjectedSite, 0.001f)
					|| !ProbeTarget.SatelliteCenterWorld.Equals(
						LegacyProxyTarget.SatelliteCenterWorld, 0.001f)
					|| !FMath::IsNearlyEqual(
						ProbeTarget.CorrectionDegrees,
						InitialCorrectionDegrees, 0.0001f))
				{
					OutFailure = TEXT("FrozenE1ModuleSelectionMovedFixedSite");
					return false;
				}
			const int32 ProbeSampleCount = CertifiedPulls.Num()
				* ProductionAimOutOfPlaneSamples
				* ProductionAimInPlaneSamples;
			TBitArray<> ProbeSuccess(false, ProbeSampleCount);
			const auto FlattenProbe = [](const int32 PullIndex,
				const int32 AimOutIndex, const int32 AimInIndex)
			{
				return (PullIndex * ProductionAimOutOfPlaneSamples
					+ AimOutIndex) * ProductionAimInPlaneSamples
					+ AimInIndex;
			};
			double ClearanceSumCM = 0.0;
			FABTSCalibrationScenario ProbeScenario =
				MakeProductionTargetScenario(LaunchFrame, ProbeTarget);
			for (const FLegacyProxyTrajectorySeed& Seed : LegacySeeds)
			{
				ProbeScenario.LaunchWorldLocation = Seed.BirdWorld;
				const FABTSCalibrationTrajectoryResult ProbeResult =
					FABTSSlingshotSatelliteCalibrationModel::IntegrateTrajectory(
						ProbeScenario, Seed.InitialVelocity,
						ProductionPreset, true);
				if (ProbeResult.Outcome
					== EABTSCalibrationTrajectoryOutcome::TargetHit)
				{
					const FABTSCalibrationTrajectoryResult GravityOff =
						FABTSSlingshotSatelliteCalibrationModel::IntegrateTrajectory(
							ProbeScenario, Seed.InitialVelocity,
							ProductionPreset, false);
					if (GravityOff.Outcome
							!= EABTSCalibrationTrajectoryOutcome::TargetHit
						&& GravityOff.ClosestTargetClearanceCM
							+ KINDA_SMALL_NUMBER
							>= FrozenPreset.GravityOffMinimumMissCM)
					{
						ProbeSuccess[FlattenProbe(
							Seed.PullIndex, Seed.AimOutIndex,
							Seed.AimInIndex)] = true;
					}
				}
				ClearanceSumCM += FMath::Max(
					0.0f, ProbeResult.ClosestTargetClearanceCM);
			}
			TBitArray<> Remaining = ProbeSuccess;
			int32 ProbeLargestIsland = 0;
			bool bProbeAimSpan = false;
			TArray<int32> Stack;
			for (TConstSetBitIterator<> It(Remaining); It; ++It)
			{
				const int32 SeedIndex = It.GetIndex();
				if (!Remaining[SeedIndex])
				{
					continue;
				}
				Remaining[SeedIndex] = false;
				Stack.Reset();
				Stack.Add(SeedIndex);
				int32 ComponentSize = 0;
				int32 MinAimOut = MAX_int32;
				int32 MaxAimOut = MIN_int32;
				int32 MinAimIn = MAX_int32;
				int32 MaxAimIn = MIN_int32;
				while (!Stack.IsEmpty())
				{
					const int32 FlatIndex = Stack.Pop(EAllowShrinking::No);
					++ComponentSize;
					const int32 AimInIndex =
						FlatIndex % ProductionAimInPlaneSamples;
					const int32 PullAndAimOut =
						FlatIndex / ProductionAimInPlaneSamples;
					const int32 AimOutIndex = PullAndAimOut
						% ProductionAimOutOfPlaneSamples;
					const int32 PullIndex = PullAndAimOut
						/ ProductionAimOutOfPlaneSamples;
					MinAimOut = FMath::Min(MinAimOut, AimOutIndex);
					MaxAimOut = FMath::Max(MaxAimOut, AimOutIndex);
					MinAimIn = FMath::Min(MinAimIn, AimInIndex);
					MaxAimIn = FMath::Max(MaxAimIn, AimInIndex);
					const int32 NeighborCoordinates[][3] =
					{
						{PullIndex - 1, AimOutIndex, AimInIndex},
						{PullIndex + 1, AimOutIndex, AimInIndex},
						{PullIndex, AimOutIndex - 1, AimInIndex},
						{PullIndex, AimOutIndex + 1, AimInIndex},
						{PullIndex, AimOutIndex, AimInIndex - 1},
						{PullIndex, AimOutIndex, AimInIndex + 1}
					};
					for (const int32* Neighbor : NeighborCoordinates)
					{
						if (Neighbor[0] < 0
							|| Neighbor[0] >= CertifiedPulls.Num()
							|| Neighbor[1] < 0
							|| Neighbor[1] >= ProductionAimOutOfPlaneSamples
							|| Neighbor[2] < 0
							|| Neighbor[2] >= ProductionAimInPlaneSamples)
						{
							continue;
						}
						const int32 NeighborIndex = FlattenProbe(
							Neighbor[0], Neighbor[1], Neighbor[2]);
						if (!Remaining[NeighborIndex])
						{
							continue;
						}
						Remaining[NeighborIndex] = false;
						Stack.Add(NeighborIndex);
					}
				}
				if (ComponentSize > ProbeLargestIsland)
				{
					ProbeLargestIsland = ComponentSize;
					bProbeAimSpan = MaxAimOut > MinAimOut
						|| MaxAimIn > MinAimIn;
				}
			}
			const bool bProbeGate = ProbeLargestIsland
					>= FMath::Max(1, FrozenPreset.MinimumSuccessIslandSamples)
				&& bProbeAimSpan;
			const int64 ClearanceMilliCM = FMath::RoundToInt64(
				ClearanceSumCM * 1000.0);
			const int64 FacingMicroDegrees = FMath::RoundToInt64(
				ProbeTarget.FacingErrorDegrees * 1000000.0);
			const bool bBetter = !bFoundProbe
				|| (bProbeGate && !bBestProbeGate)
				|| (bProbeGate == bBestProbeGate
					&& ProbeLargestIsland > BestProbeIsland)
				|| (bProbeGate == bBestProbeGate
					&& ProbeLargestIsland == BestProbeIsland
					&& bProbeAimSpan && !bBestProbeAimSpan)
				|| (bProbeGate == bBestProbeGate
					&& ProbeLargestIsland == BestProbeIsland
					&& bProbeAimSpan == bBestProbeAimSpan
					&& ClearanceMilliCM < BestClearanceMilliCM)
				|| (bProbeGate == bBestProbeGate
					&& ProbeLargestIsland == BestProbeIsland
					&& bProbeAimSpan == bBestProbeAimSpan
					&& ClearanceMilliCM == BestClearanceMilliCM
					&& FacingMicroDegrees < BestFacingMicroDegrees)
				|| (bProbeGate == bBestProbeGate
					&& ProbeLargestIsland == BestProbeIsland
					&& bProbeAimSpan == bBestProbeAimSpan
					&& ClearanceMilliCM == BestClearanceMilliCM
					&& FacingMicroDegrees == BestFacingMicroDegrees
					&& Module.BrickId < BestModuleId);
			if (bBetter)
			{
				bFoundProbe = true;
				bBestProbeGate = bProbeGate;
				BestProbeIsland = ProbeLargestIsland;
				bBestProbeAimSpan = bProbeAimSpan;
				BestClearanceMilliCM = ClearanceMilliCM;
				BestFacingMicroDegrees = FacingMicroDegrees;
				BestModuleId = Module.BrickId;
				BestProbeTarget = MoveTemp(ProbeTarget);
			}
		}
		if (!bFoundProbe)
		{
			break;
		}
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M3R5.1][FrozenE1ModuleSelection] Iteration=%d FixedCorrection=%.3f FixedSite=1 FixedYaw=1 BrickId=%d Gate=%d Island=%d AimSpan=%d ClearanceSumCM=%.3f"),
			Iteration,
			BestProbeTarget.CorrectionDegrees,
			BestModuleId,
			bBestProbeGate ? 1 : 0,
			BestProbeIsland,
			bBestProbeAimSpan ? 1 : 0,
			static_cast<double>(BestClearanceMilliCM) / 1000.0);
		OutTarget = MoveTemp(BestProbeTarget);
	}

	OutFailure = FString::Printf(
		TEXT("FrozenE1Trajectory:Correction=%.3f:SiteYaw=%.3f:Hits=%d:Island=%d:Clearance=%.1f:Hash=%016llX"),
		OutTarget.CorrectionDegrees,
		OutTarget.SiteYawDegrees,
		OutTarget.TrajectorySummary.GravityDependentHits,
		OutTarget.TrajectorySummary.LargestSuccessIslandSamples,
		OutTarget.TrajectorySummary.MinimumGravityOnTargetClearanceCM,
		static_cast<unsigned long long>(
			OutTarget.TrajectorySummary.ResultHash));
	return false;
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
	FString& OutFailure,
	const EABTSM3MonthlySatelliteTargetAuthority TargetAuthority,
	const int32 RequiredCertifiedSourceCandidateId)
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
	ABTSM3R51SatellitePreviewPrivate::FFrozenE1BuildingModuleSource FrozenE1;
	if ((TargetAuthority
			== EABTSM3MonthlySatelliteTargetAuthority::FrozenE1BuildingModules
			&& (RequiredCertifiedSourceCandidateId == INDEX_NONE
				|| !ABTSM3R51SatellitePreviewPrivate::ResolveFrozenE1BuildingModuleSource(
					FrozenE1, OutFailure)))
		|| (TargetAuthority
			== EABTSM3MonthlySatelliteTargetAuthority::LegacyCalibrationProxy
			&& RequiredCertifiedSourceCandidateId != INDEX_NONE))
	{
		return ABTSM3R51SatellitePreviewPrivate::Reject(
			OutResult,
			EABTSM3MonthlySatellitePreviewRejectReason::InvalidConfig,
			TEXT("ProductionTargetAuthority"),
			OutFailure);
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
		ABTSM3R51SatellitePreviewPrivate::FResolvedProductionTarget
			ProductionTarget;
		if (TargetAuthority
				== EABTSM3MonthlySatelliteTargetAuthority::FrozenE1BuildingModules
			&& SpatialCandidate.SourceRouteCandidateId
				== RequiredCertifiedSourceCandidateId)
		{
			FABTSM6CalibrationLaunchFrame PreviewLaunchFrame;
			if (!ABTSM3R51SatellitePreviewPrivate::BuildPreviewM6LaunchFrame(
					ReferenceFrame,
					LaunchForward,
					FrozenCatalog,
					PreviewLaunchFrame)
				|| !ABTSM3R51SatellitePreviewPrivate::
					SelectAndCertifyFrozenE1Target(
						Surface,
						ReferenceFrame.RestPouchWorld,
						LaunchUp,
						LaunchForward,
						PreviewLaunchFrame,
						FrozenPreset,
						FrozenCatalog,
						Config.PrimarySurfaceGravityCMPerSec2,
						FrozenE1,
						SatelliteFacingCorrectionDegrees,
						ProductionTarget,
						OutFailure))
			{
				return ABTSM3R51SatellitePreviewPrivate::Reject(
					OutResult,
					EABTSM3MonthlySatellitePreviewRejectReason::TargetTransformFailed,
					TEXT("FrozenE1TrajectoryCertification"),
					OutFailure);
			}
		}
		else if (!ABTSM3R51SatellitePreviewPrivate::
			ResolveProductionTargetAtCorrection(
				Surface,
				ReferenceFrame.RestPouchWorld,
				LaunchUp,
				LaunchForward,
				FrozenPreset,
				FrozenCatalog,
				Config.PrimarySurfaceGravityCMPerSec2,
				SatelliteFacingCorrectionDegrees,
				0.0f,
				TargetAuthority,
				TargetAuthority
					== EABTSM3MonthlySatelliteTargetAuthority::FrozenE1BuildingModules
						? &FrozenE1
						: nullptr,
				ProductionTarget,
				OutFailure))
		{
			return ABTSM3R51SatellitePreviewPrivate::Reject(
				OutResult,
				EABTSM3MonthlySatellitePreviewRejectReason::TargetTransformFailed,
				TEXT("ProductionTargetTransform"),
				OutFailure);
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
		Candidate.SatelliteAnchorDirection = ProductionTarget.AnchorDirection;
		Candidate.SatelliteAnchorCellId =
			ProductionTarget.Anchor.NearestCellId;
		Candidate.SatelliteFacingCorrectionAzimuthDegrees =
			ProductionTarget.CorrectionDegrees;
		Candidate.SatelliteFacingErrorDegrees =
			ProductionTarget.FacingErrorDegrees;
		Candidate.SatelliteRadiusCM =
			PrimaryRadius * FrozenPreset.SatelliteRadiusPrimaryRatio;
		Candidate.SatelliteSurfaceGravityCMPerSec2 =
			Config.PrimarySurfaceGravityCMPerSec2
			* FrozenPreset.SatelliteSurfaceGravityPrimaryRatio;
		Candidate.SatelliteCenterWorld =
			ProductionTarget.SatelliteCenterWorld;
		Candidate.E5TargetWorldTransform =
			ProductionTarget.TargetWorldTransform;
		Candidate.E5TargetHalfExtentCM =
			ProductionTarget.TargetHalfExtentCM;
		Candidate.TargetAuthority = TargetAuthority;
		Candidate.SatelliteSiteWorldTransform =
			ProductionTarget.SiteWorldTransform;
		Candidate.ProductionTargetDescriptorHash = static_cast<int64>(
			ProductionTarget.DescriptorHash);
		Candidate.ProductionTargetModuleId = ProductionTarget.TargetModuleId;
		Candidate.ProductionTargetIdentityHash = static_cast<int64>(
			ProductionTarget.TargetIdentityHash);
		Candidate.bProductionTargetTrajectoryCertified =
			TargetAuthority
				== EABTSM3MonthlySatelliteTargetAuthority::FrozenE1BuildingModules
			&& SpatialCandidate.SourceRouteCandidateId
				== RequiredCertifiedSourceCandidateId
			&& ABTSM3R51SatellitePreviewPrivate::
				IsM3ProductionTrajectoryCertified(
					ProductionTarget.TrajectorySummary,
					FrozenPreset);
		Candidate.ProductionTargetTrajectoryHash = static_cast<int64>(
			ProductionTarget.TrajectorySummary.ResultHash);
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
	if (TargetAuthority
		== EABTSM3MonthlySatelliteTargetAuthority::FrozenE1BuildingModules)
	{
		const FABTSM3MonthlySatellitePreviewCandidate* CertifiedCandidate =
			FindCandidate(OutResult, RequiredCertifiedSourceCandidateId);
		if (CertifiedCandidate == nullptr
			|| CertifiedCandidate->TargetAuthority != TargetAuthority
			|| CertifiedCandidate->ProductionTargetDescriptorHash == 0
			|| CertifiedCandidate->ProductionTargetIdentityHash == 0
			|| !CertifiedCandidate->bProductionTargetTrajectoryCertified
			|| CertifiedCandidate->ProductionTargetTrajectoryHash == 0)
		{
			return ABTSM3R51SatellitePreviewPrivate::Reject(
				OutResult,
				EABTSM3MonthlySatellitePreviewRejectReason::TargetTransformFailed,
				TEXT("FrozenE1TargetNotCertified"),
				OutFailure);
		}
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
	FString& OutFailure,
	const EABTSM3MonthlySatelliteTargetAuthority TargetAuthority,
	const int32 RequiredCertifiedSourceCandidateId)
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
			ExpectedFailure,
			TargetAuthority,
			RequiredCertifiedSourceCandidateId))
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
	Hash.AddInt32(static_cast<int32>(Candidate.TargetAuthority));
	Hash.AddVector(Candidate.SatelliteSiteWorldTransform.GetLocation());
	Hash.AddQuat(Candidate.SatelliteSiteWorldTransform.GetRotation());
	Hash.AddInt64(Candidate.ProductionTargetDescriptorHash);
	Hash.AddInt32(Candidate.ProductionTargetModuleId);
	Hash.AddInt64(Candidate.ProductionTargetIdentityHash);
	Hash.AddBool(Candidate.bProductionTargetTrajectoryCertified);
	Hash.AddInt64(Candidate.ProductionTargetTrajectoryHash);
	Hash.AddBool(Candidate.bE5OnSatelliteBackside);
	return Hash.Get();
}

uint64 FABTSM3MonthlySatellitePreviewBuilder::
ComputeProductionTargetIdentityHash(
	const uint64 DescriptorHash,
	const FTransform& SiteWorldTransform,
	const FTransform& TargetWorldTransform,
	const FVector& TargetHalfExtentCM)
{
	return ABTSM3R51SatellitePreviewPrivate::
		ComputeProductionTargetIdentityHashPrivate(
			DescriptorHash,
			SiteWorldTransform,
			TargetWorldTransform,
			TargetHalfExtentCM);
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
	for (const FABTSM3MonthlySatellitePreviewCandidate& Candidate
		: Result.RetainedCandidates)
	{
		if (Candidate.TargetAuthority
			!= EABTSM3MonthlySatelliteTargetAuthority::FrozenE1BuildingModules)
		{
			continue;
		}
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][PCG][SatellitePreview][FrozenE1BuildingModules] Candidate=%d Certified=%d Correction=%.3f BrickId=%d Target=%s HalfExtent=%s Descriptor=%016llX TargetIdentity=%016llX Trajectory=%016llX Site=%s"),
			Candidate.SourceRouteCandidateId,
			Candidate.bProductionTargetTrajectoryCertified ? 1 : 0,
			Candidate.SatelliteFacingCorrectionAzimuthDegrees,
			Candidate.ProductionTargetModuleId,
			*Candidate.E5TargetWorldTransform.GetLocation().ToCompactString(),
			*Candidate.E5TargetHalfExtentCM.ToCompactString(),
			static_cast<unsigned long long>(static_cast<uint64>(
				Candidate.ProductionTargetDescriptorHash)),
			static_cast<unsigned long long>(static_cast<uint64>(
				Candidate.ProductionTargetIdentityHash)),
			static_cast<unsigned long long>(static_cast<uint64>(
				Candidate.ProductionTargetTrajectoryHash)),
			*Candidate.SatelliteSiteWorldTransform.GetLocation()
				.ToCompactString());
	}
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
