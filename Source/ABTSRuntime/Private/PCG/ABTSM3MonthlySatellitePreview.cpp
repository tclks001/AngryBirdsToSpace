// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3MonthlySatellitePreview.h"

#include "ABTSRuntime.h"
#include "Async/ParallelFor.h"
#include "Building/ABTSM73BuildingFreezeV3.h"
#include "Calibration/ABTSSlingshotSatelliteCalibrationTypes.h"
#include "Planet/ABTSM2Planet.h"
#include "Physics/ABTSSweptCollision.h"
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

uint64 ComputeProductionTargetUnionIdentityHashPrivate(
	const FFrozenE1BuildingModuleSource& Source,
	const FTransform& SiteWorldTransform)
{
	TArray<const FFrozenE1BuildingModuleSource::FBuildingModule*> Ordered;
	Ordered.Reserve(Source.BuildingModules.Num());
	for (const FFrozenE1BuildingModuleSource::FBuildingModule& Module
		: Source.BuildingModules)
	{
		Ordered.Add(&Module);
	}
	Ordered.Sort([](
		const FFrozenE1BuildingModuleSource::FBuildingModule& A,
		const FFrozenE1BuildingModuleSource::FBuildingModule& B)
	{
		return A.BrickId < B.BrickId;
	});
	FCanonicalHash64 Hash;
	Hash.AddInt32(2);
	Hash.AddUInt64(Source.DescriptorHash);
	Hash.AddVector(SiteWorldTransform.GetLocation());
	Hash.AddQuat(SiteWorldTransform.GetRotation());
	Hash.AddInt32(Ordered.Num());
	for (const FFrozenE1BuildingModuleSource::FBuildingModule* Module : Ordered)
	{
		Hash.AddInt32(Module->BrickId);
		Hash.AddVector(Module->SiteLocalTransform.GetLocation());
		Hash.AddQuat(Module->SiteLocalTransform.GetRotation());
		Hash.AddVector(Module->HalfExtentCM);
	}
	return Hash.Get();
}

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
	OutTarget.TargetIdentityHash = TargetAuthority
		== EABTSM3MonthlySatelliteTargetAuthority::FrozenE1BuildingModules
		? ComputeProductionTargetUnionIdentityHashPrivate(
			*FrozenE1, OutTarget.SiteWorldTransform)
		: ComputeProductionTargetIdentityHashPrivate(
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

struct FCachedSweepSeed
{
	int32 PullIndex = INDEX_NONE;
	int32 AimOutIndex = INDEX_NONE;
	int32 AimInIndex = INDEX_NONE;
	FVector BirdWorld = FVector::ZeroVector;
	FVector InitialVelocity = FVector::ZeroVector;
};

struct FParallelSweepOutput
{
	FABTSCalibrationSweepSummary Summary;
	TArray<float> CertifiedPulls;
	TArray<FCachedSweepSeed> GravityDependentSeeds;
	int32 BestGravityOnFirstHitModuleId = INDEX_NONE;
};

struct FWorldBuildingModule
{
	int32 BrickId = INDEX_NONE;
	FTransform WorldTransform = FTransform::Identity;
	FVector HalfExtentCM = FVector::ZeroVector;
};

struct FUnionTrajectoryResult
{
	FABTSCalibrationTrajectoryResult Trajectory;
	int32 FirstHitModuleId = INDEX_NONE;
};

FUnionTrajectoryResult IntegrateBuildingModuleUnionTrajectory(
	const FABTSCalibrationScenario& Scenario,
	const FVector& InitialWorldVelocity,
	const FABTSSatellitePracticePreset& Preset,
	const bool bSatelliteGravityEnabled,
	const TArray<FWorldBuildingModule>& Modules,
	const FWorldBuildingModule& Broadphase,
	const bool bComputeExactClearance)
{
	FUnionTrajectoryResult Out;
	FVector Position = Scenario.LaunchWorldLocation;
	FVector Velocity = InitialWorldVelocity;
	const float StepSeconds = FMath::Clamp(
		Preset.IntegrationStepSeconds, 0.01f, 0.2f);
	const int32 MaximumSteps = FMath::Max(
		1,
		FMath::CeilToInt(
			FMath::Clamp(Preset.MaximumFlightSeconds, 2.0f, 60.0f)
			/ StepSeconds));
	const float BirdRadiusCM = FMath::Max(1.0f, Preset.BirdCollisionRadiusCM);
	const float SatelliteBodyRadiusCM = FMath::Max(
		1.0f, Scenario.Gravity.SatelliteRadiusCM + BirdRadiusCM);
	const float PrimaryBodyRadiusCM = FMath::Max(
		1.0f, Scenario.Gravity.PrimaryRadiusCM + BirdRadiusCM);
	const float PrimaryMu = FMath::Max(
		0.0f, Scenario.Gravity.PrimarySurfaceGravityCMPerSec2)
		* FMath::Square(FMath::Max(1.0f, Scenario.Gravity.PrimaryRadiusCM));
	const float SatelliteMu = FMath::Max(
		0.0f, Scenario.Gravity.SatelliteSurfaceGravityCMPerSec2)
		* FMath::Square(FMath::Max(1.0f, Scenario.Gravity.SatelliteRadiusCM));
	Out.Trajectory.ClosestTargetClearanceCM = BIG_NUMBER;
	if (bComputeExactClearance)
	{
		for (const FWorldBuildingModule& Module : Modules)
		{
			Out.Trajectory.ClosestTargetClearanceCM = FMath::Min(
				Out.Trajectory.ClosestTargetClearanceCM,
				ABTSSweptCollision::PointExpandedOrientedBoxClearance(
					Position,
					Module.WorldTransform,
					Module.HalfExtentCM,
					BirdRadiusCM));
		}
	}
	for (int32 StepIndex = 0; StepIndex < MaximumSteps; ++StepIndex)
	{
		const FVector ToPrimary = Scenario.Gravity.PrimaryCenterWorld - Position;
		const float PrimaryDistance = FMath::Max(ToPrimary.Size(), 1.0f);
		FVector Acceleration = ToPrimary / PrimaryDistance
			* (PrimaryMu / FMath::Square(PrimaryDistance));
		if (bSatelliteGravityEnabled)
		{
			const FVector ToSatellite =
				Scenario.Gravity.SatelliteCenterWorld - Position;
			const float SatelliteDistance = FMath::Max(
				ToSatellite.Size(),
				FMath::Max(1.0f, Scenario.Gravity.SatelliteRadiusCM));
			Acceleration += ToSatellite / SatelliteDistance
				* (SatelliteMu / FMath::Square(SatelliteDistance));
		}
		Acceleration -= Velocity
			* FMath::Max(0.0f, Scenario.Gravity.FlightAirDragPerSecond);
		Velocity += Acceleration * StepSeconds;
		const FVector NextPosition = Position + Velocity * StepSeconds;
		Out.Trajectory.PathLengthCM += FVector::Distance(Position, NextPosition);
		Out.Trajectory.ApexAltitudeAbovePrimaryCM = FMath::Max(
			Out.Trajectory.ApexAltitudeAbovePrimaryCM,
			FVector::Distance(
				NextPosition, Scenario.Gravity.PrimaryCenterWorld)
				- Scenario.Gravity.PrimaryRadiusCM);

		float BestModuleAlpha = BIG_NUMBER;
		int32 BestModuleId = INDEX_NONE;
		const float BroadphaseClearance = bComputeExactClearance
			? ABTSSweptCollision::SegmentExpandedOrientedBoxMinimumClearance(
				Position,
				NextPosition,
				Broadphase.WorldTransform,
				Broadphase.HalfExtentCM,
				BirdRadiusCM)
			: BIG_NUMBER;
		float BroadphaseAlpha = BIG_NUMBER;
		const bool bBroadphaseHit =
			ABTSSweptCollision::SegmentExpandedOrientedBoxFirstAlpha(
				Position,
				NextPosition,
				Broadphase.WorldTransform,
				Broadphase.HalfExtentCM,
				BirdRadiusCM,
				BroadphaseAlpha);
		if (bBroadphaseHit
			|| (bComputeExactClearance && BroadphaseClearance
				< Out.Trajectory.ClosestTargetClearanceCM)
			)
		{
			for (const FWorldBuildingModule& Module : Modules)
			{
				if (bComputeExactClearance || bBroadphaseHit)
				{
					Out.Trajectory.ClosestTargetClearanceCM = FMath::Min(
						Out.Trajectory.ClosestTargetClearanceCM,
						ABTSSweptCollision::
							SegmentExpandedOrientedBoxMinimumClearance(
								Position,
								NextPosition,
								Module.WorldTransform,
								Module.HalfExtentCM,
								BirdRadiusCM));
				}
				float ModuleAlpha = BIG_NUMBER;
				if (!bBroadphaseHit
					|| !ABTSSweptCollision::
						SegmentExpandedOrientedBoxFirstAlpha(
							Position,
							NextPosition,
							Module.WorldTransform,
							Module.HalfExtentCM,
							BirdRadiusCM,
							ModuleAlpha))
				{
					continue;
				}
				if (ModuleAlpha < BestModuleAlpha - KINDA_SMALL_NUMBER
					|| (FMath::IsNearlyEqual(
							ModuleAlpha,
							BestModuleAlpha,
							KINDA_SMALL_NUMBER)
						&& Module.BrickId < BestModuleId))
				{
					BestModuleAlpha = ModuleAlpha;
					BestModuleId = Module.BrickId;
				}
			}
		}
		float SatelliteAlpha = BIG_NUMBER;
		float PrimaryAlpha = BIG_NUMBER;
		const bool bSatelliteHit = ABTSSweptCollision::SegmentSphereFirstAlpha(
			Position,
			NextPosition,
			Scenario.Gravity.SatelliteCenterWorld,
			SatelliteBodyRadiusCM,
			SatelliteAlpha);
		const bool bPrimaryHit = ABTSSweptCollision::SegmentSphereFirstAlpha(
			Position,
			NextPosition,
			Scenario.Gravity.PrimaryCenterWorld,
			PrimaryBodyRadiusCM,
			PrimaryAlpha);
		const float FirstAlpha = FMath::Min3(
			BestModuleId != INDEX_NONE ? BestModuleAlpha : BIG_NUMBER,
			bSatelliteHit ? SatelliteAlpha : BIG_NUMBER,
			bPrimaryHit ? PrimaryAlpha : BIG_NUMBER);
		if (FirstAlpha < BIG_NUMBER)
		{
			Out.Trajectory.FlightTimeSeconds =
				(static_cast<float>(StepIndex) + FirstAlpha) * StepSeconds;
			if (BestModuleId != INDEX_NONE
				&& BestModuleAlpha <= FirstAlpha + KINDA_SMALL_NUMBER)
			{
				Out.Trajectory.Outcome =
					EABTSCalibrationTrajectoryOutcome::TargetHit;
				Out.FirstHitModuleId = BestModuleId;
			}
			else if (bSatelliteHit
				&& SatelliteAlpha <= FirstAlpha + KINDA_SMALL_NUMBER)
			{
				Out.Trajectory.Outcome =
					EABTSCalibrationTrajectoryOutcome::SatelliteBodyHit;
			}
			else
			{
				Out.Trajectory.Outcome =
					EABTSCalibrationTrajectoryOutcome::PrimaryBodyHit;
			}
			return Out;
		}
		Position = NextPosition;
	}
	Out.Trajectory.FlightTimeSeconds =
		static_cast<float>(MaximumSteps) * StepSeconds;
	if (Out.Trajectory.ClosestTargetClearanceCM == BIG_NUMBER)
	{
		Out.Trajectory.ClosestTargetClearanceCM = 0.0f;
	}
	return Out;
}

float SampleProductionRange(
	const float Minimum,
	const float Maximum,
	const int32 Index,
	const int32 Count)
{
	return Count <= 1
		? (Minimum + Maximum) * 0.5f
		: FMath::Lerp(
			Minimum,
			Maximum,
			static_cast<float>(Index) / static_cast<float>(Count - 1));
}

void BuildProductionReachablePulls(
	const FABTSM6LaunchProfile& Profile,
	TArray<float>& OutPulls)
{
	OutPulls.Reset();
	OutPulls.Add(0.0f);
	OutPulls.Add(1.0f);
	for (int32 Notch = -1000; Notch <= 1000; ++Notch)
	{
		const float Pull = Profile.InitialPullAlpha
			+ Profile.PullPowerWheelStep * static_cast<float>(Notch);
		if (Pull < -KINDA_SMALL_NUMBER || Pull > 1.0f + KINDA_SMALL_NUMBER)
		{
			continue;
		}
		OutPulls.AddUnique(FMath::Clamp(Pull, 0.0f, 1.0f));
	}
	OutPulls.Sort();
}

int32 FlattenProductionSweep(
	const int32 PullIndex,
	const int32 AimOutIndex,
	const int32 AimInIndex,
	const int32 AimOutCount,
	const int32 AimInCount)
{
	return (PullIndex * AimOutCount + AimOutIndex) * AimInCount
		+ AimInIndex;
}

struct FSuccessIslandMetrics
{
	int32 LargestSamples = 0;
	int32 MinPullIndex = INDEX_NONE;
	int32 MaxPullIndex = INDEX_NONE;
	int32 MinAimOutIndex = INDEX_NONE;
	int32 MaxAimOutIndex = INDEX_NONE;
	int32 MinAimInIndex = INDEX_NONE;
	int32 MaxAimInIndex = INDEX_NONE;
};

FSuccessIslandMetrics MeasureSuccessIsland(
	const TBitArray<>& Success,
	const int32 PullCount,
	const int32 AimOutCount,
	const int32 AimInCount)
{
	FSuccessIslandMetrics Metrics;
	TBitArray<> Remaining = Success;
	TArray<int32> Stack;
	TArray<int32> Component;
	for (TConstSetBitIterator<> It(Remaining); It; ++It)
	{
		const int32 SeedIndex = It.GetIndex();
		if (!Remaining[SeedIndex])
		{
			continue;
		}
		Remaining[SeedIndex] = false;
		Stack.Reset();
		Component.Reset();
		Stack.Add(SeedIndex);
		while (!Stack.IsEmpty())
		{
			const int32 FlatIndex = Stack.Pop(EAllowShrinking::No);
			Component.Add(FlatIndex);
			const int32 AimInIndex = FlatIndex % AimInCount;
			const int32 PullAndAimOut = FlatIndex / AimInCount;
			const int32 AimOutIndex = PullAndAimOut % AimOutCount;
			const int32 PullIndex = PullAndAimOut / AimOutCount;
			const int32 Neighbors[][3] =
			{
				{PullIndex - 1, AimOutIndex, AimInIndex},
				{PullIndex + 1, AimOutIndex, AimInIndex},
				{PullIndex, AimOutIndex - 1, AimInIndex},
				{PullIndex, AimOutIndex + 1, AimInIndex},
				{PullIndex, AimOutIndex, AimInIndex - 1},
				{PullIndex, AimOutIndex, AimInIndex + 1}
			};
			for (const int32* Neighbor : Neighbors)
			{
				if (Neighbor[0] < 0 || Neighbor[0] >= PullCount
					|| Neighbor[1] < 0 || Neighbor[1] >= AimOutCount
					|| Neighbor[2] < 0 || Neighbor[2] >= AimInCount)
				{
					continue;
				}
				const int32 NeighborIndex = FlattenProductionSweep(
					Neighbor[0], Neighbor[1], Neighbor[2],
					AimOutCount, AimInCount);
				if (!Remaining[NeighborIndex])
				{
					continue;
				}
				Remaining[NeighborIndex] = false;
				Stack.Add(NeighborIndex);
			}
		}
		if (Component.Num() <= Metrics.LargestSamples)
		{
			continue;
		}
		Metrics.LargestSamples = Component.Num();
		Metrics.MinPullIndex = MAX_int32;
		Metrics.MaxPullIndex = MIN_int32;
		Metrics.MinAimOutIndex = MAX_int32;
		Metrics.MaxAimOutIndex = MIN_int32;
		Metrics.MinAimInIndex = MAX_int32;
		Metrics.MaxAimInIndex = MIN_int32;
		for (const int32 FlatIndex : Component)
		{
			const int32 AimInIndex = FlatIndex % AimInCount;
			const int32 PullAndAimOut = FlatIndex / AimInCount;
			const int32 AimOutIndex = PullAndAimOut % AimOutCount;
			const int32 PullIndex = PullAndAimOut / AimOutCount;
			Metrics.MinPullIndex = FMath::Min(Metrics.MinPullIndex, PullIndex);
			Metrics.MaxPullIndex = FMath::Max(Metrics.MaxPullIndex, PullIndex);
			Metrics.MinAimOutIndex = FMath::Min(
				Metrics.MinAimOutIndex, AimOutIndex);
			Metrics.MaxAimOutIndex = FMath::Max(
				Metrics.MaxAimOutIndex, AimOutIndex);
			Metrics.MinAimInIndex = FMath::Min(
				Metrics.MinAimInIndex, AimInIndex);
			Metrics.MaxAimInIndex = FMath::Max(
				Metrics.MaxAimInIndex, AimInIndex);
		}
	}
	return Metrics;
}

void AppendCalibrationHash(uint64& InOutHash, const int64 Value)
{
	uint64 Bits = static_cast<uint64>(Value);
	for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
	{
		InOutHash ^= (Bits >> (ByteIndex * 8)) & 0xffull;
		InOutHash *= Fnv1a64Prime;
	}
}

int64 QuantizeCalibration(const double Value)
{
	return FMath::RoundToInt64(Value * 1000.0);
}

void FinalizeCalibrationSummaryHash(FABTSCalibrationSweepSummary& Summary)
{
	uint64 Hash = Fnv1a64OffsetBasis;
	AppendCalibrationHash(Hash, Summary.ReinforcedSampleCount);
	AppendCalibrationHash(Hash, Summary.ReinforcedReachablePullSamples);
	AppendCalibrationHash(Hash, Summary.ReinforcedCertifiedPullSamples);
	AppendCalibrationHash(Hash, Summary.ReinforcedGravityOnHits);
	AppendCalibrationHash(Hash, Summary.ReinforcedSatelliteBodyHits);
	AppendCalibrationHash(Hash, Summary.ReinforcedPrimaryBodyHits);
	AppendCalibrationHash(Hash, Summary.ReinforcedTimeouts);
	AppendCalibrationHash(Hash, Summary.GravityDependentHits);
	AppendCalibrationHash(Hash, Summary.LargestSuccessIslandSamples);
	AppendCalibrationHash(Hash, Summary.SimpleFullPowerHits);
	AppendCalibrationHash(Hash, Summary.ReinforcedOutsideCertifiedPullHits);
	AppendCalibrationHash(Hash, QuantizeCalibration(Summary.SuccessPullMinimum));
	AppendCalibrationHash(Hash, QuantizeCalibration(Summary.SuccessPullMaximum));
	AppendCalibrationHash(
		Hash, QuantizeCalibration(Summary.SuccessAimInPlaneMinimumCM));
	AppendCalibrationHash(
		Hash, QuantizeCalibration(Summary.SuccessAimInPlaneMaximumCM));
	AppendCalibrationHash(Hash, QuantizeCalibration(Summary.MinimumGravityOffMissCM));
	AppendCalibrationHash(
		Hash, QuantizeCalibration(Summary.MinimumGravityOnTargetClearanceCM));
	AppendCalibrationHash(
		Hash, QuantizeCalibration(Summary.BestGravityOnAimInPlaneCM));
	AppendCalibrationHash(
		Hash, QuantizeCalibration(Summary.BestGravityOnAimOutOfPlaneCM));
	AppendCalibrationHash(Hash, QuantizeCalibration(Summary.BestGravityOnPullAlpha));
	AppendCalibrationHash(Hash, Summary.bPassed ? 1 : 0);
	Summary.ResultHash = Hash;
}

bool RunParallelExactSweep(
	const FABTSCalibrationScenario& Scenario,
	const FABTSM6LaunchProfileCatalog& Catalog,
	const FABTSSatellitePracticePreset& Preset,
	const bool bCollectSeeds,
	FParallelSweepOutput& Out,
	const TArray<FWorldBuildingModule>* UnionModules = nullptr,
	const FWorldBuildingModule* UnionBroadphase = nullptr,
	const uint64 UnionTargetIdentityHash = 0)
{
	Out = FParallelSweepOutput();
	const FABTSM6LaunchProfile* Reinforced =
		FABTSSlingshotSatelliteCalibrationModel::FindProfile(
			Catalog, EABTSSlingshotTier::Reinforced);
	const FABTSM6LaunchProfile* Simple =
		FABTSSlingshotSatelliteCalibrationModel::FindProfile(
			Catalog, EABTSSlingshotTier::Simple);
	if (Reinforced == nullptr || Simple == nullptr)
	{
		return false;
	}
	const int32 AimInCount = FMath::Clamp(Preset.AimInPlaneSampleCount, 5, 161);
	const int32 AimOutCount = FMath::Clamp(
		Preset.AimOutOfPlaneSampleCount, 1, 31);
	TArray<float> ReachablePulls;
	BuildProductionReachablePulls(*Reinforced, ReachablePulls);
	Out.Summary.ReinforcedReachablePullSamples = ReachablePulls.Num();
	for (const float Pull : ReachablePulls)
	{
		if (Pull + KINDA_SMALL_NUMBER >= Preset.PullMinimum
			&& Pull <= Preset.PullMaximum + KINDA_SMALL_NUMBER)
		{
			Out.CertifiedPulls.Add(Pull);
		}
	}
	Out.Summary.ReinforcedCertifiedPullSamples = Out.CertifiedPulls.Num();
	if (Out.CertifiedPulls.IsEmpty())
	{
		return false;
	}

	struct FSampleResult
	{
		bool bSampled = false;
		bool bGravityDependent = false;
		FVector BirdWorld = FVector::ZeroVector;
		FVector InitialVelocity = FVector::ZeroVector;
		FABTSCalibrationTrajectoryResult GravityOn;
		FABTSCalibrationTrajectoryResult GravityOff;
		int32 GravityOnFirstHitModuleId = INDEX_NONE;
		int32 GravityOffFirstHitModuleId = INDEX_NONE;
	};
	const int32 TotalCount = Out.CertifiedPulls.Num()
		* AimOutCount * AimInCount;
	TArray<FSampleResult> Samples;
	Samples.SetNum(TotalCount);
	ParallelFor(TotalCount, [&](const int32 FlatIndex)
	{
		const int32 AimInIndex = FlatIndex % AimInCount;
		const int32 PullAndAimOut = FlatIndex / AimInCount;
		const int32 AimOutIndex = PullAndAimOut % AimOutCount;
		const int32 PullIndex = PullAndAimOut / AimOutCount;
		const float AimIn = SampleProductionRange(
			Preset.AimInPlaneMinimumCM, Preset.AimInPlaneMaximumCM,
			AimInIndex, AimInCount);
		const float AimOut = SampleProductionRange(
			Preset.AimOutOfPlaneMinimumCM, Preset.AimOutOfPlaneMaximumCM,
			AimOutIndex, AimOutCount);
		if (FVector2D(AimIn, AimOut).Size()
			> Reinforced->MaximumAimPlaneOffsetCM + KINDA_SMALL_NUMBER)
		{
			return;
		}
		FSampleResult& Sample = Samples[FlatIndex];
		if (!FABTSSlingshotSatelliteCalibrationModel::BuildM6LaunchSample(
				Scenario.LaunchFrame, *Reinforced, AimIn, AimOut,
				Out.CertifiedPulls[PullIndex],
				Sample.BirdWorld, Sample.InitialVelocity))
		{
			return;
		}
		Sample.bSampled = true;
		FABTSCalibrationScenario SampleScenario = Scenario;
		SampleScenario.LaunchWorldLocation = Sample.BirdWorld;
		if (UnionModules != nullptr && UnionBroadphase != nullptr)
		{
			const FUnionTrajectoryResult UnionOn =
				IntegrateBuildingModuleUnionTrajectory(
					SampleScenario,
					Sample.InitialVelocity,
					Preset,
					true,
					*UnionModules,
					*UnionBroadphase,
					false);
			Sample.GravityOn = UnionOn.Trajectory;
			Sample.GravityOnFirstHitModuleId = UnionOn.FirstHitModuleId;
		}
		else
		{
			Sample.GravityOn =
				FABTSSlingshotSatelliteCalibrationModel::IntegrateTrajectory(
					SampleScenario, Sample.InitialVelocity, Preset, true);
		}
		if (Sample.GravityOn.Outcome
			!= EABTSCalibrationTrajectoryOutcome::TargetHit)
		{
			return;
		}
		if (UnionModules != nullptr && UnionBroadphase != nullptr)
		{
			const FUnionTrajectoryResult UnionOff =
				IntegrateBuildingModuleUnionTrajectory(
					SampleScenario,
					Sample.InitialVelocity,
					Preset,
					false,
					*UnionModules,
					*UnionBroadphase,
					true);
			Sample.GravityOff = UnionOff.Trajectory;
			Sample.GravityOffFirstHitModuleId = UnionOff.FirstHitModuleId;
		}
		else
		{
			Sample.GravityOff =
				FABTSSlingshotSatelliteCalibrationModel::IntegrateTrajectory(
					SampleScenario, Sample.InitialVelocity, Preset, false);
		}
		Sample.bGravityDependent =
			Sample.GravityOff.Outcome
				!= EABTSCalibrationTrajectoryOutcome::TargetHit
			&& Sample.GravityOff.ClosestTargetClearanceCM
					+ KINDA_SMALL_NUMBER >= Preset.GravityOffMinimumMissCM;
	});

	Out.Summary.MinimumGravityOffMissCM = BIG_NUMBER;
	Out.Summary.MinimumGravityOnTargetClearanceCM = BIG_NUMBER;
	TBitArray<> GravityDependent(false, TotalCount);
	for (int32 FlatIndex = 0; FlatIndex < TotalCount; ++FlatIndex)
	{
		const FSampleResult& Sample = Samples[FlatIndex];
		if (!Sample.bSampled)
		{
			continue;
		}
		const int32 AimInIndex = FlatIndex % AimInCount;
		const int32 PullAndAimOut = FlatIndex / AimInCount;
		const int32 AimOutIndex = PullAndAimOut % AimOutCount;
		const int32 PullIndex = PullAndAimOut / AimOutCount;
		const float AimIn = SampleProductionRange(
			Preset.AimInPlaneMinimumCM, Preset.AimInPlaneMaximumCM,
			AimInIndex, AimInCount);
		const float AimOut = SampleProductionRange(
			Preset.AimOutOfPlaneMinimumCM, Preset.AimOutOfPlaneMaximumCM,
			AimOutIndex, AimOutCount);
		++Out.Summary.ReinforcedSampleCount;
		if (Sample.GravityOn.ClosestTargetClearanceCM
			< Out.Summary.MinimumGravityOnTargetClearanceCM)
		{
			Out.Summary.MinimumGravityOnTargetClearanceCM =
				Sample.GravityOn.ClosestTargetClearanceCM;
			Out.Summary.BestGravityOnAimInPlaneCM = AimIn;
			Out.Summary.BestGravityOnAimOutOfPlaneCM = AimOut;
			Out.Summary.BestGravityOnPullAlpha = Out.CertifiedPulls[PullIndex];
			Out.BestGravityOnFirstHitModuleId =
				Sample.GravityOnFirstHitModuleId;
		}
		switch (Sample.GravityOn.Outcome)
		{
		case EABTSCalibrationTrajectoryOutcome::SatelliteBodyHit:
			++Out.Summary.ReinforcedSatelliteBodyHits;
			break;
		case EABTSCalibrationTrajectoryOutcome::PrimaryBodyHit:
			++Out.Summary.ReinforcedPrimaryBodyHits;
			break;
		case EABTSCalibrationTrajectoryOutcome::Timeout:
			++Out.Summary.ReinforcedTimeouts;
			break;
		case EABTSCalibrationTrajectoryOutcome::TargetHit:
			++Out.Summary.ReinforcedGravityOnHits;
			break;
		default:
			break;
		}
		if (!Sample.bGravityDependent)
		{
			continue;
		}
		GravityDependent[FlatIndex] = true;
		++Out.Summary.GravityDependentHits;
		Out.Summary.MinimumGravityOffMissCM = FMath::Min(
			Out.Summary.MinimumGravityOffMissCM,
			Sample.GravityOff.ClosestTargetClearanceCM);
		if (bCollectSeeds)
		{
			FCachedSweepSeed& Seed =
				Out.GravityDependentSeeds.AddDefaulted_GetRef();
			Seed.PullIndex = PullIndex;
			Seed.AimOutIndex = AimOutIndex;
			Seed.AimInIndex = AimInIndex;
			Seed.BirdWorld = Sample.BirdWorld;
			Seed.InitialVelocity = Sample.InitialVelocity;
		}
	}

	const FSuccessIslandMetrics Island = MeasureSuccessIsland(
		GravityDependent, Out.CertifiedPulls.Num(), AimOutCount, AimInCount);
	Out.Summary.LargestSuccessIslandSamples = Island.LargestSamples;
	if (Island.LargestSamples > 0)
	{
		Out.Summary.SuccessPullMinimum =
			Out.CertifiedPulls[Island.MinPullIndex];
		Out.Summary.SuccessPullMaximum =
			Out.CertifiedPulls[Island.MaxPullIndex];
		Out.Summary.SuccessAimInPlaneMinimumCM = SampleProductionRange(
			Preset.AimInPlaneMinimumCM, Preset.AimInPlaneMaximumCM,
			Island.MinAimInIndex, AimInCount);
		Out.Summary.SuccessAimInPlaneMaximumCM = SampleProductionRange(
			Preset.AimInPlaneMinimumCM, Preset.AimInPlaneMaximumCM,
			Island.MaxAimInIndex, AimInCount);
		Out.Summary.bIslandSpansPullNeighbors =
			Island.MaxPullIndex > Island.MinPullIndex;
		Out.Summary.bIslandSpansAimNeighbors =
			Island.MaxAimInIndex > Island.MinAimInIndex
			|| Island.MaxAimOutIndex > Island.MinAimOutIndex;
	}

	const int32 SimpleCount = AimOutCount * AimInCount;
	TArray<uint8> SimpleHits;
	SimpleHits.SetNumZeroed(SimpleCount);
	ParallelFor(SimpleCount, [&](const int32 FlatIndex)
	{
		const int32 AimInIndex = FlatIndex % AimInCount;
		const int32 AimOutIndex = FlatIndex / AimInCount;
		const float AimIn = SampleProductionRange(
			Preset.AimInPlaneMinimumCM, Preset.AimInPlaneMaximumCM,
			AimInIndex, AimInCount);
		const float AimOut = SampleProductionRange(
			Preset.AimOutOfPlaneMinimumCM, Preset.AimOutOfPlaneMaximumCM,
			AimOutIndex, AimOutCount);
		if (FVector2D(AimIn, AimOut).Size()
			> Simple->MaximumAimPlaneOffsetCM + KINDA_SMALL_NUMBER)
		{
			return;
		}
		FVector BirdWorld;
		FVector InitialVelocity;
		if (!FABTSSlingshotSatelliteCalibrationModel::BuildM6LaunchSample(
				Scenario.LaunchFrame, *Simple, AimIn, AimOut, 1.0f,
				BirdWorld, InitialVelocity))
		{
			return;
		}
		FABTSCalibrationScenario SampleScenario = Scenario;
		SampleScenario.LaunchWorldLocation = BirdWorld;
		SimpleHits[FlatIndex] = UnionModules != nullptr
			&& UnionBroadphase != nullptr
			? IntegrateBuildingModuleUnionTrajectory(
				SampleScenario, InitialVelocity, Preset, true,
				*UnionModules, *UnionBroadphase, false)
				.Trajectory.Outcome
				== EABTSCalibrationTrajectoryOutcome::TargetHit
			: FABTSSlingshotSatelliteCalibrationModel::IntegrateTrajectory(
				SampleScenario, InitialVelocity, Preset, true).Outcome
				== EABTSCalibrationTrajectoryOutcome::TargetHit;
	});
	for (const uint8 bHit : SimpleHits)
	{
		Out.Summary.SimpleFullPowerHits += bHit != 0 ? 1 : 0;
	}

	TArray<float> OutsidePulls;
	for (const float Pull : ReachablePulls)
	{
		if (Pull + KINDA_SMALL_NUMBER < Preset.PullMinimum
			|| Pull > Preset.PullMaximum + KINDA_SMALL_NUMBER)
		{
			OutsidePulls.Add(Pull);
		}
	}
	const int32 OutsideCount = OutsidePulls.Num() * AimOutCount * AimInCount;
	TArray<uint8> OutsideHits;
	OutsideHits.SetNumZeroed(OutsideCount);
	ParallelFor(OutsideCount, [&](const int32 FlatIndex)
	{
		const int32 AimInIndex = FlatIndex % AimInCount;
		const int32 PullAndAimOut = FlatIndex / AimInCount;
		const int32 AimOutIndex = PullAndAimOut % AimOutCount;
		const int32 PullIndex = PullAndAimOut / AimOutCount;
		const float AimIn = SampleProductionRange(
			Preset.AimInPlaneMinimumCM, Preset.AimInPlaneMaximumCM,
			AimInIndex, AimInCount);
		const float AimOut = SampleProductionRange(
			Preset.AimOutOfPlaneMinimumCM, Preset.AimOutOfPlaneMaximumCM,
			AimOutIndex, AimOutCount);
		if (FVector2D(AimIn, AimOut).Size()
			> Reinforced->MaximumAimPlaneOffsetCM + KINDA_SMALL_NUMBER)
		{
			return;
		}
		FVector BirdWorld;
		FVector InitialVelocity;
		if (!FABTSSlingshotSatelliteCalibrationModel::BuildM6LaunchSample(
				Scenario.LaunchFrame, *Reinforced, AimIn, AimOut,
				OutsidePulls[PullIndex], BirdWorld, InitialVelocity))
		{
			return;
		}
		FABTSCalibrationScenario SampleScenario = Scenario;
		SampleScenario.LaunchWorldLocation = BirdWorld;
		OutsideHits[FlatIndex] = UnionModules != nullptr
			&& UnionBroadphase != nullptr
			? IntegrateBuildingModuleUnionTrajectory(
				SampleScenario, InitialVelocity, Preset, true,
				*UnionModules, *UnionBroadphase, false)
				.Trajectory.Outcome
				== EABTSCalibrationTrajectoryOutcome::TargetHit
			: FABTSSlingshotSatelliteCalibrationModel::IntegrateTrajectory(
				SampleScenario, InitialVelocity, Preset, true).Outcome
				== EABTSCalibrationTrajectoryOutcome::TargetHit;
	});
	for (const uint8 bHit : OutsideHits)
	{
		Out.Summary.ReinforcedOutsideCertifiedPullHits += bHit != 0 ? 1 : 0;
	}
	if (Out.Summary.MinimumGravityOffMissCM == BIG_NUMBER)
	{
		Out.Summary.MinimumGravityOffMissCM = 0.0f;
	}
	if (Out.Summary.MinimumGravityOnTargetClearanceCM == BIG_NUMBER)
	{
		Out.Summary.MinimumGravityOnTargetClearanceCM = 0.0f;
	}
	Out.Summary.bPassed =
		Out.Summary.LargestSuccessIslandSamples
			>= FMath::Max(1, Preset.MinimumSuccessIslandSamples)
		&& Out.Summary.bIslandSpansAimNeighbors
		&& Out.Summary.bIslandSpansPullNeighbors
		&& Out.Summary.GravityDependentHits > 0
		&& Out.Summary.SimpleFullPowerHits == 0
		&& Out.Summary.ReinforcedOutsideCertifiedPullHits == 0
		&& Out.Summary.SuccessPullMinimum + KINDA_SMALL_NUMBER
			>= Preset.PullMinimum
		&& Out.Summary.SuccessPullMaximum
			<= Preset.PullMaximum + KINDA_SMALL_NUMBER;
	FinalizeCalibrationSummaryHash(Out.Summary);
	if (UnionModules != nullptr && UnionBroadphase != nullptr)
	{
		FCanonicalHash64 UnionHash;
		UnionHash.AddInt32(1);
		UnionHash.AddUInt64(UnionTargetIdentityHash);
		UnionHash.AddUInt64(Out.Summary.ResultHash);
		UnionHash.AddInt32(Samples.Num());
		for (const FSampleResult& Sample : Samples)
		{
			UnionHash.AddBool(Sample.bSampled);
			UnionHash.AddInt32(static_cast<int32>(Sample.GravityOn.Outcome));
			UnionHash.AddInt32(Sample.GravityOnFirstHitModuleId);
			UnionHash.AddFloat(Sample.GravityOn.ClosestTargetClearanceCM);
			UnionHash.AddInt32(static_cast<int32>(Sample.GravityOff.Outcome));
			UnionHash.AddInt32(Sample.GravityOffFirstHitModuleId);
			UnionHash.AddFloat(Sample.GravityOff.ClosestTargetClearanceCM);
		}
		Out.Summary.ResultHash = UnionHash.Get();
	}
	return true;
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
	constexpr uint64 ExpectedLegacyProxyTrajectoryHash = 0xCB88635D085D213Cull;
	const double CertificationStartSeconds = FPlatformTime::Seconds();
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
	FCanonicalHash64 CertificationKeyHash;
	// Version 2 keys the exact ordered E1 Brick OBB union certification. Version 1
	// represented the retired single-target/cube-expanded certificate.
	CertificationKeyHash.AddInt32(2);
	CertificationKeyHash.AddUInt64(FrozenE1.DescriptorHash);
	CertificationKeyHash.AddUInt64(
		FABTSSlingshotSatelliteCalibrationModel::ComputeLaunchProfileHash(
			FrozenCatalog));
	CertificationKeyHash.AddUInt64(
		FABTSSlingshotSatelliteCalibrationModel::
			ComputeSatellitePracticePresetHash(ProductionPreset));
	CertificationKeyHash.AddUInt64(
		FABTSSlingshotSatelliteCalibrationModel::ComputeGravitySnapshotHash(
			OutTarget.Gravity));
	CertificationKeyHash.AddVector(LaunchFrame.SlingCenterWorld);
	CertificationKeyHash.AddVector(LaunchFrame.RestPouchWorldLocation);
	CertificationKeyHash.AddVector(LaunchFrame.SlingUpWorld);
	CertificationKeyHash.AddVector(LaunchFrame.SlingForwardWorld);
	CertificationKeyHash.AddVector(LaunchFrame.SlingRightWorld);
	CertificationKeyHash.AddVector(LaunchFrame.AimPlaneNormalWorld);
	CertificationKeyHash.AddVector(LaunchFrame.AimInPlaneAxisWorld);
	CertificationKeyHash.AddVector(LaunchFrame.AimOutOfPlaneAxisWorld);
	CertificationKeyHash.AddFloat(LaunchFrame.BirdInPouchOffsetCM);
	CertificationKeyHash.AddVector(OutTarget.SiteWorldTransform.GetLocation());
	CertificationKeyHash.AddQuat(OutTarget.SiteWorldTransform.GetRotation());
	CertificationKeyHash.AddFloat(InitialCorrectionDegrees);
	const uint64 CertificationKey = CertificationKeyHash.Get();
	struct FFrozenE1CertificateCacheEntry
	{
		FResolvedProductionTarget Target;
		uint64 LegacyProxyHash = 0;
		int32 LegacySeedCount = 0;
	};
	static TMap<uint64, FFrozenE1CertificateCacheEntry> CertificateCache;
	if (const FFrozenE1CertificateCacheEntry* Cached =
		CertificateCache.Find(CertificationKey))
	{
		OutTarget = Cached->Target;
		const uint64 ExpectedUnionIdentity =
			ComputeProductionTargetUnionIdentityHashPrivate(
				FrozenE1, OutTarget.SiteWorldTransform);
		const bool bCacheIdentityExact =
			Cached->LegacyProxyHash == ExpectedLegacyProxyTrajectoryHash
			&& FrozenE1.BuildingModules.ContainsByPredicate(
				[&OutTarget](
					const FFrozenE1BuildingModuleSource::FBuildingModule& Module)
				{
					return Module.BrickId == OutTarget.TargetModuleId;
				})
			&& OutTarget.TargetIdentityHash == ExpectedUnionIdentity
			&& OutTarget.TrajectorySummary.ResultHash != 0
			&& IsM3ProductionTrajectoryCertified(
				OutTarget.TrajectorySummary, FrozenPreset);
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M3R5.1][FrozenE1TrajectoryReuse] CacheHit=1 Key=%016llX LegacySeeds=%d ProductionSweeps=0 UnselectedProductionSweeps=0 WitnessBrickId=%d TargetIdentity=%016llX Trajectory=%016llX ExactOBBUnion=%d WallMS=%.3f"),
			static_cast<unsigned long long>(CertificationKey),
			Cached->LegacySeedCount,
			OutTarget.TargetModuleId,
			static_cast<unsigned long long>(OutTarget.TargetIdentityHash),
			static_cast<unsigned long long>(
				OutTarget.TrajectorySummary.ResultHash),
			bCacheIdentityExact ? 1 : 0,
			(FPlatformTime::Seconds() - CertificationStartSeconds) * 1000.0);
		if (!bCacheIdentityExact)
		{
			OutFailure = TEXT("FrozenE1CertificateCacheIdentityMismatch");
			return false;
		}
		return true;
	}

	FParallelSweepOutput LegacySweep;
	if (!RunParallelExactSweep(
			MakeProductionTargetScenario(LaunchFrame, LegacyProxyTarget),
			FrozenCatalog,
			ProductionPreset,
			true,
			LegacySweep))
	{
		OutFailure = TEXT("LegacyProxyParallelSweepFailed");
		return false;
	}
	const FABTSCalibrationSweepSummary& LegacyProxySummary = LegacySweep.Summary;
	if (!IsM3ProductionTrajectoryCertified(LegacyProxySummary, FrozenPreset))
	{
		OutFailure = FString::Printf(
			TEXT("LegacyProxySeedNotCertified:Hash=%016llX:Island=%d"),
			static_cast<unsigned long long>(LegacyProxySummary.ResultHash),
			LegacyProxySummary.LargestSuccessIslandSamples);
		return false;
	}
	if (LegacyProxySummary.ResultHash != ExpectedLegacyProxyTrajectoryHash)
	{
		OutFailure = FString::Printf(
			TEXT("LegacyProxyExactIdentityMismatch:Expected=%016llX:Actual=%016llX"),
			static_cast<unsigned long long>(ExpectedLegacyProxyTrajectoryHash),
			static_cast<unsigned long long>(LegacyProxySummary.ResultHash));
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
	const FABTSCalibrationScenario LegacyScenario =
		MakeProductionTargetScenario(LaunchFrame, LegacyProxyTarget);
	if (LegacySweep.GravityDependentSeeds.Num()
			!= LegacyProxySummary.GravityDependentHits
		|| LegacySweep.GravityDependentSeeds.IsEmpty())
	{
		OutFailure = FString::Printf(
			TEXT("LegacyProxySeedEnumerationMismatch:Expected=%d:Actual=%d"),
			LegacyProxySummary.GravityDependentHits,
			LegacySweep.GravityDependentSeeds.Num());
		return false;
	}
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M3R5.1][FrozenE1LegacySeedSet] Exact=1 ReusedTrajectories=1 Samples=%d Pulls=%d Grid=%dx%d"),
		LegacySweep.GravityDependentSeeds.Num(),
		LegacySweep.CertifiedPulls.Num(),
		ProductionAimInPlaneSamples, ProductionAimOutOfPlaneSamples);

	TArray<FWorldBuildingModule> WorldModules;
	WorldModules.Reserve(FrozenE1.BuildingModules.Num());
	for (const FFrozenE1BuildingModuleSource::FBuildingModule& Module
		: FrozenE1.BuildingModules)
	{
		FWorldBuildingModule& WorldModule = WorldModules.AddDefaulted_GetRef();
		WorldModule.BrickId = Module.BrickId;
		WorldModule.WorldTransform =
			Module.SiteLocalTransform * OutTarget.SiteWorldTransform;
		WorldModule.WorldTransform.SetScale3D(FVector::OneVector);
		WorldModule.HalfExtentCM = Module.HalfExtentCM.GetAbs();
	}
	WorldModules.Sort([](const FWorldBuildingModule& A,
		const FWorldBuildingModule& B)
	{
		return A.BrickId < B.BrickId;
	});
	if (WorldModules.IsEmpty())
	{
		OutFailure = TEXT("FrozenE1ProductionUnionEmpty");
		return false;
	}
	FBox SiteLocalUnionBounds(EForceInit::ForceInit);
	for (const FFrozenE1BuildingModuleSource::FBuildingModule& Module
		: FrozenE1.BuildingModules)
	{
		for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
		{
			const FVector Corner(
				(CornerIndex & 1) != 0
					? Module.HalfExtentCM.X : -Module.HalfExtentCM.X,
				(CornerIndex & 2) != 0
					? Module.HalfExtentCM.Y : -Module.HalfExtentCM.Y,
				(CornerIndex & 4) != 0
					? Module.HalfExtentCM.Z : -Module.HalfExtentCM.Z);
			SiteLocalUnionBounds +=
				Module.SiteLocalTransform.TransformPosition(Corner);
		}
	}
	if (!SiteLocalUnionBounds.IsValid)
	{
		OutFailure = TEXT("FrozenE1ProductionUnionBroadphaseBounds");
		return false;
	}
	FWorldBuildingModule UnionBroadphase;
	UnionBroadphase.BrickId = INDEX_NONE;
	UnionBroadphase.WorldTransform = FTransform(
		FQuat::Identity, SiteLocalUnionBounds.GetCenter())
		* OutTarget.SiteWorldTransform;
	UnionBroadphase.WorldTransform.SetScale3D(FVector::OneVector);
	UnionBroadphase.HalfExtentCM = SiteLocalUnionBounds.GetExtent();
	TArray<int32> LegacyUnionFirstHitModuleIds;
	LegacyUnionFirstHitModuleIds.SetNum(
		LegacySweep.GravityDependentSeeds.Num());
	ParallelFor(LegacySweep.GravityDependentSeeds.Num(),
		[&](const int32 SeedIndex)
	{
		const FCachedSweepSeed& Seed =
			LegacySweep.GravityDependentSeeds[SeedIndex];
		FABTSCalibrationScenario PathScenario = LegacyScenario;
		PathScenario.LaunchWorldLocation = Seed.BirdWorld;
		LegacyUnionFirstHitModuleIds[SeedIndex] =
			IntegrateBuildingModuleUnionTrajectory(
				PathScenario, Seed.InitialVelocity, ProductionPreset,
				true, WorldModules, UnionBroadphase, false).FirstHitModuleId;
	});
	int32 LegacyUnionHitCount = 0;
	for (const int32 ModuleId : LegacyUnionFirstHitModuleIds)
	{
		LegacyUnionHitCount += ModuleId != INDEX_NONE ? 1 : 0;
	}
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M3R5.1][FrozenE1ModuleUnionProbe] AnalyticFirstIntersection=1 ExactOBB=1 FixedCorrection=%.3f FixedSite=1 FixedYaw=1 Modules=%d LegacySeedHits=%d LegacySeeds=%d"),
		OutTarget.CorrectionDegrees,
		WorldModules.Num(),
		LegacyUnionHitCount,
		LegacyUnionFirstHitModuleIds.Num());

	FParallelSweepOutput ProductionSweep;
	if (!RunParallelExactSweep(
			MakeProductionTargetScenario(LaunchFrame, OutTarget),
			FrozenCatalog,
			ProductionPreset,
			false,
			ProductionSweep,
			&WorldModules,
			&UnionBroadphase,
			OutTarget.TargetIdentityHash))
	{
		OutFailure = TEXT("FrozenE1ProductionUnionParallelSweepFailed");
		return false;
	}
	OutTarget.TrajectorySummary = ProductionSweep.Summary;
	const FFrozenE1BuildingModuleSource::FBuildingModule* WitnessModule =
		FrozenE1.BuildingModules.FindByPredicate(
			[&ProductionSweep](
				const FFrozenE1BuildingModuleSource::FBuildingModule& Module)
			{
				return Module.BrickId
					== ProductionSweep.BestGravityOnFirstHitModuleId;
			});
	if (WitnessModule == nullptr)
	{
		OutFailure = TEXT("FrozenE1ProductionUnionWitnessMissing");
		return false;
	}
	OutTarget.TargetModuleId = WitnessModule->BrickId;
	OutTarget.TargetWorldTransform =
		WitnessModule->SiteLocalTransform * OutTarget.SiteWorldTransform;
	OutTarget.TargetHalfExtentCM = WitnessModule->HalfExtentCM;
	OutTarget.TargetIdentityHash =
		ComputeProductionTargetUnionIdentityHashPrivate(
			FrozenE1, OutTarget.SiteWorldTransform);
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M3R5.1][FrozenE1BuildingModuleUnionCertification] WitnessBrickId=%d Modules=%d ExactOBB=1 StableFirstHit=1 Correction=%.3f SiteYaw=%.3f ReinforcedHits=%d GravityDependentHits=%d Island=%d AimNeighbors=%d SimpleHits=%d OutsidePullHits=%d Pull=[%.3f,%.3f] GravityOffMiss=%.1f Clearance=%.1f BestAim=(%.1f,%.1f) BestPull=%.3f TargetIdentity=%016llX TrajectoryHash=%016llX"),
		OutTarget.TargetModuleId,
		WorldModules.Num(),
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
		static_cast<unsigned long long>(OutTarget.TargetIdentityHash),
		static_cast<unsigned long long>(
			OutTarget.TrajectorySummary.ResultHash));
	if (!IsM3ProductionTrajectoryCertified(
			OutTarget.TrajectorySummary, FrozenPreset)
		|| OutTarget.TrajectorySummary.ResultHash == 0)
	{
		OutFailure = FString::Printf(
			TEXT("FrozenE1ProductionUnionNotCertified:Hash=%016llX:Island=%d"),
			static_cast<unsigned long long>(
				OutTarget.TrajectorySummary.ResultHash),
			OutTarget.TrajectorySummary.LargestSuccessIslandSamples);
		return false;
	}
	FFrozenE1CertificateCacheEntry& Cached =
		CertificateCache.Add(CertificationKey);
	Cached.Target = OutTarget;
	Cached.LegacyProxyHash = LegacyProxySummary.ResultHash;
	Cached.LegacySeedCount = LegacySweep.GravityDependentSeeds.Num();
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M3R5.1][FrozenE1TrajectoryReuse] CacheHit=0 Key=%016llX LegacyParallelSweeps=1 LegacySeeds=%d AnalyticModulePaths=%d ProductionSweeps=1 UnselectedProductionSweeps=0 WitnessBrickId=%d TargetIdentity=%016llX Trajectory=%016llX ExactOBBUnion=1 WallMS=%.3f"),
		static_cast<unsigned long long>(CertificationKey),
		LegacySweep.GravityDependentSeeds.Num(),
		LegacySweep.GravityDependentSeeds.Num(),
		OutTarget.TargetModuleId,
		static_cast<unsigned long long>(OutTarget.TargetIdentityHash),
		static_cast<unsigned long long>(
			OutTarget.TrajectorySummary.ResultHash),
		(FPlatformTime::Seconds() - CertificationStartSeconds) * 1000.0);
	return true;
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
	ABTSM3R51SatellitePreviewPrivate::FFrozenE1BuildingModuleSource FrozenE1;
	FString Failure;
	if (DescriptorHash != 0
		&& ABTSM3R51SatellitePreviewPrivate::
			ResolveFrozenE1BuildingModuleSource(FrozenE1, Failure)
		&& FrozenE1.DescriptorHash == DescriptorHash)
	{
		return ABTSM3R51SatellitePreviewPrivate::
			ComputeProductionTargetUnionIdentityHashPrivate(
				FrozenE1, SiteWorldTransform);
	}
	return ABTSM3R51SatellitePreviewPrivate::
		ComputeProductionTargetIdentityHashPrivate(
			DescriptorHash,
			SiteWorldTransform,
			TargetWorldTransform,
			TargetHalfExtentCM);
}

bool FABTSM3MonthlySatellitePreviewBuilder::
RunFrozenE1BuildingModuleUnionSweep(
	const FABTSM6CalibrationLaunchFrame& LaunchFrame,
	const FABTSCalibrationGravitySnapshot& Gravity,
	const FTransform& SiteWorldTransform,
	const FABTSM6LaunchProfileCatalog& Catalog,
	const FABTSSatellitePracticePreset& Preset,
	FABTSCalibrationSweepSummary& OutSummary,
	int32& OutWitnessBrickId,
	uint64& OutTargetIdentityHash,
	FString& OutFailure)
{
	using namespace ABTSM3R51SatellitePreviewPrivate;
	OutSummary = FABTSCalibrationSweepSummary();
	OutWitnessBrickId = INDEX_NONE;
	OutTargetIdentityHash = 0;
	OutFailure.Reset();
	FFrozenE1BuildingModuleSource FrozenE1;
	if (!ResolveFrozenE1BuildingModuleSource(FrozenE1, OutFailure)
		|| !SiteWorldTransform.IsValid())
	{
		OutFailure = FString::Printf(
			TEXT("FrozenE1UnionSource:%s"), *OutFailure);
		return false;
	}
	TArray<FWorldBuildingModule> WorldModules;
	WorldModules.Reserve(FrozenE1.BuildingModules.Num());
	FBox SiteLocalUnionBounds(EForceInit::ForceInit);
	for (const FFrozenE1BuildingModuleSource::FBuildingModule& Module
		: FrozenE1.BuildingModules)
	{
		FWorldBuildingModule& WorldModule = WorldModules.AddDefaulted_GetRef();
		WorldModule.BrickId = Module.BrickId;
		WorldModule.WorldTransform =
			Module.SiteLocalTransform * SiteWorldTransform;
		WorldModule.WorldTransform.SetScale3D(FVector::OneVector);
		WorldModule.HalfExtentCM = Module.HalfExtentCM.GetAbs();
		for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
		{
			const FVector Corner(
				(CornerIndex & 1) != 0
					? Module.HalfExtentCM.X : -Module.HalfExtentCM.X,
				(CornerIndex & 2) != 0
					? Module.HalfExtentCM.Y : -Module.HalfExtentCM.Y,
				(CornerIndex & 4) != 0
					? Module.HalfExtentCM.Z : -Module.HalfExtentCM.Z);
			SiteLocalUnionBounds +=
				Module.SiteLocalTransform.TransformPosition(Corner);
		}
	}
	WorldModules.Sort([](const FWorldBuildingModule& A,
		const FWorldBuildingModule& B)
	{
		return A.BrickId < B.BrickId;
	});
	if (WorldModules.IsEmpty() || !SiteLocalUnionBounds.IsValid)
	{
		OutFailure = TEXT("FrozenE1UnionGeometry");
		return false;
	}
	FWorldBuildingModule Broadphase;
	Broadphase.WorldTransform = FTransform(
		FQuat::Identity, SiteLocalUnionBounds.GetCenter())
		* SiteWorldTransform;
	Broadphase.WorldTransform.SetScale3D(FVector::OneVector);
	Broadphase.HalfExtentCM = SiteLocalUnionBounds.GetExtent();
	OutTargetIdentityHash = ComputeProductionTargetUnionIdentityHashPrivate(
		FrozenE1, SiteWorldTransform);
	FABTSCalibrationScenario Scenario;
	Scenario.LaunchWorldLocation = LaunchFrame.RestPouchWorldLocation;
	Scenario.LaunchFrame = LaunchFrame;
	Scenario.Gravity = Gravity;
	FParallelSweepOutput Sweep;
	if (!RunParallelExactSweep(
			Scenario,
			Catalog,
			Preset,
			false,
			Sweep,
			&WorldModules,
			&Broadphase,
			OutTargetIdentityHash)
		|| !IsM3ProductionTrajectoryCertified(Sweep.Summary, Preset)
		|| Sweep.BestGravityOnFirstHitModuleId == INDEX_NONE)
	{
		OutFailure = FString::Printf(
			TEXT("FrozenE1UnionCertificate:Hash=%016llX:Island=%d:Witness=%d"),
			static_cast<unsigned long long>(Sweep.Summary.ResultHash),
			Sweep.Summary.LargestSuccessIslandSamples,
			Sweep.BestGravityOnFirstHitModuleId);
		return false;
	}
	OutSummary = Sweep.Summary;
	OutWitnessBrickId = Sweep.BestGravityOnFirstHitModuleId;
	return true;
}

bool FABTSM3MonthlySatellitePreviewBuilder::
EvaluateFrozenE1LegacyProxyOverlap(
	const FVector& LaunchWorldLocation,
	const FABTSCalibrationGravitySnapshot& CalibrationGravity,
	const FTransform& SiteWorldTransform,
	const FABTSSatellitePracticePreset& FrozenPreset,
	int32& OutOverlapBrickId,
	int32& OutOverlapBrickCount,
	uint64& OutTargetIdentityHash,
	uint64& OutOverlapHash,
	FString& OutFailure)
{
	using namespace ABTSM3R51SatellitePreviewPrivate;
	OutOverlapBrickId = INDEX_NONE;
	OutOverlapBrickCount = 0;
	OutTargetIdentityHash = 0;
	OutOverlapHash = 0;
	OutFailure.Reset();
	FFrozenE1BuildingModuleSource FrozenE1;
	FTransform LegacyProxyTransform = FTransform::Identity;
	if (!ResolveFrozenE1BuildingModuleSource(FrozenE1, OutFailure)
		|| !SiteWorldTransform.IsValid()
		|| !FABTSSlingshotSatelliteCalibrationModel::BuildSatelliteTargetWorldTransform(
			LaunchWorldLocation, CalibrationGravity, FrozenPreset,
			LegacyProxyTransform, &OutFailure))
	{
		OutFailure = FString::Printf(TEXT("FrozenE1ProxyOverlapSource:%s"), *OutFailure);
		return false;
	}
	const FVector ProxyCenter = LegacyProxyTransform.GetLocation();
	const FVector SiteRadial = (ProxyCenter - CalibrationGravity.SatelliteCenterWorld).GetSafeNormal();
	const FVector ExpectedSiteCenter = CalibrationGravity.SatelliteCenterWorld
		+ SiteRadial * CalibrationGravity.SatelliteRadiusCM;
	if (SiteRadial.IsNearlyZero()
		|| !SiteWorldTransform.GetLocation().Equals(ExpectedSiteCenter, 0.01f))
	{
		OutFailure = TEXT("FrozenE1ProxyOverlapProjectionExact");
		return false;
	}
	OutTargetIdentityHash = ComputeProductionTargetUnionIdentityHashPrivate(FrozenE1, SiteWorldTransform);
	const float ExplicitExpansionCM = FMath::Max(0.0f, FrozenPreset.TargetProxyRadiusCM)
		+ FMath::Max(0.0f, FrozenPreset.BirdCollisionRadiusCM);
	FCanonicalHash64 Hash;
	Hash.AddInt32(1);
	Hash.AddUInt64(OutTargetIdentityHash);
	Hash.AddVector(ProxyCenter);
	Hash.AddFloat(FrozenPreset.TargetProxyRadiusCM);
	Hash.AddFloat(FrozenPreset.BirdCollisionRadiusCM);
	for (const FFrozenE1BuildingModuleSource::FBuildingModule& Module : FrozenE1.BuildingModules)
	{
		FTransform WorldTransform = Module.SiteLocalTransform * SiteWorldTransform;
		WorldTransform.SetScale3D(FVector::OneVector);
		const FVector LocalProxyCenter = WorldTransform.InverseTransformPosition(ProxyCenter);
		const FVector ExpandedExtent = Module.HalfExtentCM.GetAbs()
			+ FVector(ExplicitExpansionCM);
		const bool bOverlap = FMath::Abs(LocalProxyCenter.X) <= ExpandedExtent.X
			&& FMath::Abs(LocalProxyCenter.Y) <= ExpandedExtent.Y
			&& FMath::Abs(LocalProxyCenter.Z) <= ExpandedExtent.Z;
		Hash.AddInt32(Module.BrickId);
		Hash.AddBool(bOverlap);
		if (bOverlap)
		{
			if (OutOverlapBrickId == INDEX_NONE)
			{
				OutOverlapBrickId = Module.BrickId;
			}
			++OutOverlapBrickCount;
		}
	}
	Hash.AddInt32(OutOverlapBrickId);
	Hash.AddInt32(OutOverlapBrickCount);
	OutOverlapHash = Hash.Get();
	if (OutOverlapBrickCount == 0)
	{
		OutFailure = FString::Printf(TEXT("FrozenE1ProxyOverlapMiss:Hash=%016llX"),
			static_cast<unsigned long long>(OutOverlapHash));
		return false;
	}
	return true;
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
