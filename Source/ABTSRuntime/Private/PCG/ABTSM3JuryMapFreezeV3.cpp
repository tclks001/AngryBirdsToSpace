// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3JuryMapFreezeV3.h"

#include "Building/ABTSM73BuildingFreezeV3.h"
#include "Math/RotationMatrix.h"
#include "Planet/ABTSM2Planet.h"

namespace ABTSM3JuryMapFreezeV3Private
{
constexpr uint64 FnvOffset = 14695981039346656037ull;
constexpr uint64 FnvPrime = 1099511628211ull;
constexpr double AxisTolerance = 1.0e-4;
constexpr double CorridorLongAxisTolerance = 1.0e-3;

struct FCanonicalHash
{
	uint64 HashValue = FnvOffset;

	void AddByte(const uint8 Byte)
	{
		HashValue ^= Byte;
		HashValue *= FnvPrime;
	}

	void AddInt32(const int32 InValue)
	{
		const uint32 Bits = static_cast<uint32>(InValue);
		for (int32 Shift = 0; Shift < 32; Shift += 8)
		{
			AddByte(static_cast<uint8>((Bits >> Shift) & 0xffu));
		}
	}

	void AddUInt64(const uint64 InValue)
	{
		for (int32 Shift = 0; Shift < 64; Shift += 8)
		{
			AddByte(static_cast<uint8>((InValue >> Shift) & 0xffull));
		}
	}

	void AddName(const FName& Name)
	{
		const FString Text = Name.ToString();
		AddInt32(Text.Len());
		for (const TCHAR Character : Text)
		{
			AddInt32(static_cast<int32>(Character));
		}
	}

	void AddScalarCM(const double Value)
	{
		AddUInt64(static_cast<uint64>(FMath::RoundToInt64(Value * 1000.0)));
	}

	void AddAxisScalar(const double Value)
	{
		AddUInt64(static_cast<uint64>(FMath::RoundToInt64(Value * 1000000.0)));
	}

	void AddVectorCM(const FVector& Value)
	{
		AddScalarCM(Value.X);
		AddScalarCM(Value.Y);
		AddScalarCM(Value.Z);
	}

	void AddAxis(const FVector& Value)
	{
		AddAxisScalar(Value.X);
		AddAxisScalar(Value.Y);
		AddAxisScalar(Value.Z);
	}

	void AddBoxCM(const FBox& Box)
	{
		AddInt32(Box.IsValid != 0 ? 1 : 0);
		if (Box.IsValid != 0)
		{
			AddVectorCM(Box.Min);
			AddVectorCM(Box.Max);
		}
	}

	void AddIntArray(const TArray<int32>& Values)
	{
		AddInt32(Values.Num());
		for (const int32 Value : Values)
		{
			AddInt32(Value);
		}
	}
};

FName ManifestName(const EABTSM73BeamDemoBuilding Id)
{
	switch (Id)
	{
	case EABTSM73BeamDemoBuilding::E1ColumnBreak:
		return TEXT("E1ColumnBreak");
	case EABTSM73BeamDemoBuilding::E2DropTrigger:
		return TEXT("E2DropTrigger");
	case EABTSM73BeamDemoBuilding::E3SlideRelease:
		return TEXT("E3SlideRelease");
	case EABTSM73BeamDemoBuilding::E4TipOver:
		return TEXT("E4TipOver");
	case EABTSM73BeamDemoBuilding::E5SeamRelease:
		return TEXT("E5SeamRelease");
	case EABTSM73BeamDemoBuilding::E6TipOver:
		return TEXT("E6TipOver");
	default:
		return NAME_None;
	}
}

const FABTSM3MonthlySpatialCandidate* FindSpatialCandidate(
	const FABTSM3MonthlySpatialResult& Result)
{
	return Result.RetainedCandidates.FindByPredicate(
		[](const FABTSM3MonthlySpatialCandidate& Candidate)
		{
			return Candidate.SourceRouteCandidateId
				== FABTSM3JuryMapFreezeV3Builder::FrozenSourceCandidateId;
		});
}

int32 FindNearestCell(
	const TArray<FABTSM2Cell>& Cells,
	const FVector& UnitDirection)
{
	int32 BestCellId = INDEX_NONE;
	double BestDot = -2.0;
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		const double Dot = FVector::DotProduct(
			Cells[CellId].UnitCenter,
			UnitDirection);
		if (Dot > BestDot)
		{
			BestDot = Dot;
			BestCellId = CellId;
		}
	}
	return BestCellId;
}

bool IsFrameValid(const FVector& X, const FVector& Y, const FVector& Z)
{
	return X.IsNormalized()
		&& Y.IsNormalized()
		&& Z.IsNormalized()
		&& FMath::Abs(FVector::DotProduct(X, Y)) <= AxisTolerance
		&& FMath::Abs(FVector::DotProduct(X, Z)) <= AxisTolerance
		&& FMath::Abs(FVector::DotProduct(Y, Z)) <= AxisTolerance
		&& FVector::DotProduct(FVector::CrossProduct(X, Y), Z)
			>= 1.0 - AxisTolerance;
}

bool ReserveBounds(
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlySpatialCandidate& Candidate,
	const FVector& PrimaryCenterWorldCM,
	const FVector& SiteWorldLocation,
	const FVector& SiteX,
	const FVector& SiteY,
	const FBox& Bounds,
	TArray<int32>& OutCellIds,
	FString& OutFailure)
{
	OutCellIds.Reset();
	if (Bounds.IsValid == 0 || Candidate.Cells.Num() != Cells.Num())
	{
		OutFailure = TEXT("InvalidBoundsOrCellCount");
		return false;
	}
	const double XSamples[] = {
		Bounds.Min.X,
		(Bounds.Min.X + Bounds.Max.X) * 0.5,
		Bounds.Max.X};
	const double YSamples[] = {
		Bounds.Min.Y,
		(Bounds.Min.Y + Bounds.Max.Y) * 0.5,
		Bounds.Max.Y};
	for (const double X : XSamples)
	{
		for (const double Y : YSamples)
		{
			const FVector UnitDirection =
				(SiteWorldLocation + SiteX * X + SiteY * Y
					- PrimaryCenterWorldCM).GetSafeNormal();
			const int32 CellId = FindNearestCell(Cells, UnitDirection);
			if (!Candidate.Cells.IsValidIndex(CellId)
				|| Candidate.Cells[CellId].bWater
				|| Candidate.RecomputedRoute.OrderedRoadCellIds.Contains(CellId))
			{
				OutFailure = FString::Printf(
					TEXT("IllegalReservedCell:%d"), CellId);
				OutCellIds.Reset();
				return false;
			}
			OutCellIds.AddUnique(CellId);
		}
	}
	OutCellIds.Sort();
	return !OutCellIds.IsEmpty();
}

FVector ResolveHorizontalLongAxis(
	const FBox& SiteBounds,
	const FTransform& Transform)
{
	const FVector Size = SiteBounds.GetSize();
	return Size.X > Size.Y
		? Transform.GetUnitAxis(EAxis::X)
		: Transform.GetUnitAxis(EAxis::Y);
}

double HorizontalEnvelopeRadiusCM(const FBox& Bounds)
{
	if (Bounds.IsValid == 0)
	{
		return 0.0;
	}
	return FMath::Max(
		FMath::Max(
			FVector2D(Bounds.Min.X, Bounds.Min.Y).Size(),
			FVector2D(Bounds.Min.X, Bounds.Max.Y).Size()),
		FMath::Max(
			FVector2D(Bounds.Max.X, Bounds.Min.Y).Size(),
			FVector2D(Bounds.Max.X, Bounds.Max.Y).Size()));
}

uint64 ComputeGravityHash(
	const FName& Authority,
	const FVector& Center,
	const double RadiusCM,
	const double SurfaceGravityCMPerSec2,
	const int32 SourceVersion,
	const uint64 SourceHash)
{
	FCanonicalHash Hash;
	Hash.AddName(Authority);
	Hash.AddVectorCM(Center);
	Hash.AddScalarCM(RadiusCM);
	Hash.AddScalarCM(SurfaceGravityCMPerSec2);
	Hash.AddInt32(SourceVersion);
	Hash.AddUInt64(SourceHash);
	return Hash.HashValue;
}

void Reject(
	FABTSM3JuryMapFreezeV3Result& Result,
	const EABTSM3JuryMapFreezeV3RejectReason Reason)
{
	Result.RejectReason = Reason;
	Result.bMapFreezeReady = false;
	Result.LayoutHash = 0;
	Result.HandoffContract.LayoutHash = 0;
}

bool ValidatePlacementOrientation(
	const FABTSM3JuryMapFreezeV3Placement& Placement,
	FString& OutFailure)
{
	const FVector SiteX = Placement.Site.WorldTransform.GetUnitAxis(EAxis::X);
	const FVector SiteY = Placement.Site.WorldTransform.GetUnitAxis(EAxis::Y);
	const FVector SiteZ = Placement.Site.WorldTransform.GetUnitAxis(EAxis::Z);
	const FVector Corridor =
		Placement.AttackCorridorWorldDirection.GetSafeNormal();
	const FVector LongAxis = ResolveHorizontalLongAxis(
		Placement.Site.LocalBounds,
		Placement.Site.WorldTransform).GetSafeNormal();
	if (!IsFrameValid(SiteX, SiteY, SiteZ)
		|| Corridor.IsNearlyZero()
		|| LongAxis.IsNearlyZero()
		|| FVector::DotProduct(SiteX, Corridor) < 1.0 - AxisTolerance
		|| FVector::DotProduct(LongAxis,
			Placement.HorizontalLongAxisWorld.GetSafeNormal())
			< 1.0 - AxisTolerance
		|| FMath::Abs(FVector::DotProduct(Corridor, LongAxis))
			> CorridorLongAxisTolerance
		|| !FMath::IsNearlyEqual(
			Placement.AttackCorridorLongAxisAbsDot,
			FMath::Abs(FVector::DotProduct(Corridor, LongAxis)),
			AxisTolerance))
	{
		OutFailure = FString::Printf(
			TEXT("AttackCorridorLongAxis:%d:Dot=%.9f"),
			Placement.Site.EncounterIndex,
			FMath::Abs(FVector::DotProduct(Corridor, LongAxis)));
		return false;
	}
	return true;
}
}

uint64 FABTSM3JuryMapFreezeV3Builder::ComputePlacementHash(
	const FABTSM3JuryMapFreezeV3Placement& Placement)
{
	using namespace ABTSM3JuryMapFreezeV3Private;
	const FABTSJuryDemoFixedSixBuildingSite& Site = Placement.Site;
	const FABTSJuryDemoFixedSixV3Envelope& Envelope = Site.V3Envelope;
	FCanonicalHash Hash;
	Hash.AddInt32(SchemaVersion);
	Hash.AddName(Site.ManifestEntryId);
	Hash.AddInt32(Site.EncounterIndex);
	Hash.AddInt32(Site.DifficultyTier);
	Hash.AddInt32(Site.DeterministicSeed);
	Hash.AddUInt64(Site.DescriptorHash);
	Hash.AddVectorCM(Site.WorldTransform.GetTranslation());
	Hash.AddAxis(Site.WorldTransform.GetUnitAxis(EAxis::X));
	Hash.AddAxis(Site.WorldTransform.GetUnitAxis(EAxis::Y));
	Hash.AddAxis(Site.WorldTransform.GetUnitAxis(EAxis::Z));
	Hash.AddScalarCM(Site.PadHalfExtentCM.X);
	Hash.AddScalarCM(Site.PadHalfExtentCM.Y);
	Hash.AddBoxCM(Site.LocalBounds);
	Hash.AddUInt64(Envelope.StaticGeometryHash);
	Hash.AddUInt64(Envelope.ProductionIdentityHash);
	Hash.AddUInt64(Envelope.DeviceAssemblyHash);
	Hash.AddBoxCM(Envelope.SiteLocalBounds);
	Hash.AddBoxCM(Envelope.PadBounds);
	Hash.AddBoxCM(Envelope.EffectBounds);
	Hash.AddInt32(static_cast<int32>(Envelope.SurfaceKind));
	Hash.AddVectorCM(Envelope.SupportCenterWorldCM);
	Hash.AddScalarCM(Envelope.SupportRadiusCM);
	Hash.AddName(Envelope.GravityAuthorityId);
	Hash.AddUInt64(Envelope.GravityIdentityHash);
	Hash.AddInt32(Placement.TargetAnchorCellId);
	Hash.AddInt32(Placement.PadCenterCellId);
	Hash.AddIntArray(Placement.ReservedPadCellIds);
	Hash.AddIntArray(Placement.ReservedEffectCellIds);
	Hash.AddAxis(Placement.AttackCorridorWorldDirection);
	Hash.AddAxis(Placement.HorizontalLongAxisWorld);
	Hash.AddAxisScalar(Placement.AttackCorridorLongAxisAbsDot);
	return Hash.HashValue;
}

uint64 FABTSM3JuryMapFreezeV3Builder::ComputeLayoutHash(
	const FABTSM3JuryMapFreezeV3Result& Result)
{
	using namespace ABTSM3JuryMapFreezeV3Private;
	FCanonicalHash Hash;
	Hash.AddInt32(Result.SchemaVersion);
	Hash.AddInt32(Result.WorldSeed);
	Hash.AddInt32(Result.SourceCandidateId);
	Hash.AddUInt64(Result.SourceSpatialResultHash);
	Hash.AddUInt64(Result.SourceSpatialCandidateHash);
	Hash.AddUInt64(Result.SourceSatellitePreviewResultHash);
	Hash.AddUInt64(Result.SourceSatellitePreviewCandidateHash);
	Hash.AddInt32(Result.HandoffContract.ContractVersion);
	Hash.AddInt32(Result.HandoffContract.PlacementSchemaVersion);
	Hash.AddInt32(Result.HandoffContract.DemoManifestVersion);
	Hash.AddUInt64(Result.HandoffContract.DemoManifestHash);
	Hash.AddUInt64(Result.HandoffContract.PlacementCatalogHash);
	Hash.AddInt32(Result.Placements.Num());
	for (const FABTSM3JuryMapFreezeV3Placement& Placement : Result.Placements)
	{
		Hash.AddUInt64(Placement.Site.V3Envelope.PlacementHash);
	}
	return Hash.HashValue;
}

bool FABTSM3JuryMapFreezeV3Builder::Build(
	const TArray<FABTSM2Cell>& Cells,
	const FVector& PrimaryCenterWorldCM,
	const double PrimaryRadiusCM,
	const double PrimarySurfaceGravityCMPerSec2,
	const FABTSM3MonthlySpatialResult& SpatialResult,
	const FABTSM3MonthlySatellitePreviewResult& SatellitePreviewResult,
	FABTSM3JuryMapFreezeV3Result& OutResult,
	FString& OutFailure)
{
	using namespace ABTSM3JuryMapFreezeV3Private;
	OutResult = FABTSM3JuryMapFreezeV3Result();
	OutFailure.Reset();
	OutResult.SchemaVersion = SchemaVersion;
	OutResult.WorldSeed = SpatialResult.WorldSeed;
	OutResult.SourceCandidateId = FrozenSourceCandidateId;
	OutResult.SourceSpatialResultHash =
		static_cast<uint64>(SpatialResult.SpatialResultHash);
	OutResult.SourceSatellitePreviewResultHash =
		static_cast<uint64>(SatellitePreviewResult.ResultHash);

	if (Cells.IsEmpty()
		|| PrimaryCenterWorldCM.ContainsNaN()
		|| !FMath::IsFinite(PrimaryRadiusCM)
		|| PrimaryRadiusCM <= 0.0
		|| !FMath::IsFinite(PrimarySurfaceGravityCMPerSec2)
		|| PrimarySurfaceGravityCMPerSec2 <= 0.0)
	{
		OutFailure = TEXT("InvalidCellsOrPrimarySurface");
		Reject(OutResult, EABTSM3JuryMapFreezeV3RejectReason::InvalidInput);
		return false;
	}
	const FABTSM3MonthlySpatialCandidate* Candidate =
		FindSpatialCandidate(SpatialResult);
	if (!SpatialResult.bSpatialResultValid
		|| SpatialResult.WorldSeed != FrozenWorldSeed
		|| OutResult.SourceSpatialResultHash != FrozenSourceSpatialResultHash
		|| Candidate == nullptr
		|| !Candidate->bHardPass
		|| Candidate->Encounters.Num() != ExpectedSiteCount
		|| static_cast<uint64>(Candidate->SpatialCandidateHash)
			!= FrozenSourceSpatialCandidateHash)
	{
		OutFailure = TEXT("FrozenSpatialIdentity");
		Reject(OutResult,
			EABTSM3JuryMapFreezeV3RejectReason::SourceIdentityMismatch);
		return false;
	}
	OutResult.SourceSpatialCandidateHash =
		static_cast<uint64>(Candidate->SpatialCandidateHash);

	const FABTSM3MonthlySatellitePreviewCandidate* SatelliteCandidate =
		FABTSM3MonthlySatellitePreviewBuilder::FindCandidate(
			SatellitePreviewResult,
			FrozenSourceCandidateId);
	if (!SatellitePreviewResult.bPreviewResultValid
		|| SatellitePreviewResult.WorldSeed != FrozenWorldSeed
		|| SatelliteCandidate == nullptr
		|| !SatelliteCandidate->bE5OnSatelliteBackside
		|| static_cast<uint64>(SatelliteCandidate->SourceSpatialCandidateHash)
			!= FrozenSourceSpatialCandidateHash
		|| static_cast<uint64>(SatelliteCandidate->CandidateHash)
			!= FABTSM3MonthlySatellitePreviewBuilder::ComputeCandidateHash(
				*SatelliteCandidate))
	{
		OutFailure = TEXT("FrozenSatellitePreviewIdentity");
		Reject(OutResult,
			EABTSM3JuryMapFreezeV3RejectReason::SourceIdentityMismatch);
		return false;
	}
	OutResult.SourceSatellitePreviewCandidateHash =
		static_cast<uint64>(SatelliteCandidate->CandidateHash);

	const TArray<FABTSM73BuildingFreezeV3FrozenIdentity>& Identities =
		FABTSM73BuildingFreezeV3::GetFrozenIdentities();
	if (Identities.Num() != ExpectedSiteCount
		|| FABTSM73BuildingFreezeV3::FrozenCatalogHash
			!= FABTSJuryDemoFixedSixContract::FrozenV3PlacementCatalogHash)
	{
		OutFailure = TEXT("FrozenV3Catalog");
		Reject(OutResult,
			EABTSM3JuryMapFreezeV3RejectReason::FrozenCatalogMismatch);
		return false;
	}

	OutResult.HandoffContract.ContractVersion =
		FABTSJuryDemoFixedSixContract::SupportedV3ContractVersion;
	OutResult.HandoffContract.PlacementSchemaVersion =
		FABTSJuryDemoFixedSixContract::FrozenV3PlacementSchemaVersion;
	OutResult.HandoffContract.DemoManifestVersion =
		FABTSJuryDemoFixedSixContract::FrozenDemoManifestVersion;
	OutResult.HandoffContract.DemoManifestHash =
		FABTSJuryDemoFixedSixContract::FrozenDemoManifestHash;
	OutResult.HandoffContract.PlacementCatalogHash =
		FABTSJuryDemoFixedSixContract::FrozenV3PlacementCatalogHash;
	OutResult.HandoffContract.WorldSeed = FrozenWorldSeed;
	OutResult.HandoffContract.CandidateId = FrozenSourceCandidateId;

	const FName PrimaryGravityAuthority(
		TEXT("M3.PrimaryPlanet.MapFreezeV3"));
	const FName SatelliteGravityAuthority(
		TEXT("M3.MonthlySatellite.MapFreezeV3"));
	const uint64 PrimaryGravityHash = ComputeGravityHash(
		PrimaryGravityAuthority,
		PrimaryCenterWorldCM,
		PrimaryRadiusCM,
		PrimarySurfaceGravityCMPerSec2,
		SchemaVersion,
		OutResult.SourceSpatialCandidateHash);
	const uint64 SatelliteGravityHash = ComputeGravityHash(
		SatelliteGravityAuthority,
		SatelliteCandidate->SatelliteCenterWorld,
		SatelliteCandidate->SatelliteRadiusCM,
		SatelliteCandidate->SatelliteSurfaceGravityCMPerSec2,
		SatelliteCandidate->SatellitePracticePresetVersion,
		static_cast<uint64>(SatelliteCandidate->SatellitePracticePresetHash));

	OutResult.Placements.Reserve(ExpectedSiteCount);
	OutResult.HandoffContract.Sites.Reserve(ExpectedSiteCount);
	for (int32 Index = 0; Index < Identities.Num(); ++Index)
	{
		const FABTSM73BuildingFreezeV3FrozenIdentity& Identity =
			Identities[Index];
		if (Identity.EncounterSlot != Index
			|| ManifestName(Identity.ManifestEntryId).IsNone()
			|| Identity.SiteLocalBounds.IsValid == 0
			|| Identity.PadBounds.IsValid == 0
			|| Identity.EffectBounds.IsValid == 0
			|| Identity.DescriptorHash == 0
			|| Identity.StaticGeometryHash == 0
			|| Identity.ProductionHash == 0
			|| Identity.SourceDeviceAssemblyHash == 0)
		{
			OutFailure = FString::Printf(TEXT("V3Identity:%d"), Index);
			Reject(OutResult,
				EABTSM3JuryMapFreezeV3RejectReason::FrozenCatalogMismatch);
			return false;
		}

		FABTSM3JuryMapFreezeV3Placement Placement;
		FABTSJuryDemoFixedSixBuildingSite& Site = Placement.Site;
		Site.ManifestEntryId = ManifestName(Identity.ManifestEntryId);
		Site.EncounterIndex = Index;
		Site.DifficultyTier = Identity.DifficultyTier;
		Site.DeterministicSeed = Identity.BuildingSeed;
		Site.DescriptorHash = Identity.DescriptorHash;
		Site.LocalBounds = Identity.SiteLocalBounds;
		Site.PadHalfExtentCM = FVector2D(
			FMath::Max(FMath::Abs(Identity.PadBounds.Min.X),
				FMath::Abs(Identity.PadBounds.Max.X)),
			FMath::Max(FMath::Abs(Identity.PadBounds.Min.Y),
				FMath::Abs(Identity.PadBounds.Max.Y)));
		Site.V3Envelope.StaticGeometryHash = Identity.StaticGeometryHash;
		Site.V3Envelope.ProductionIdentityHash = Identity.ProductionHash;
		Site.V3Envelope.DeviceAssemblyHash =
			Identity.SourceDeviceAssemblyHash;
		Site.V3Envelope.SiteLocalBounds = Identity.SiteLocalBounds;
		Site.V3Envelope.PadBounds = Identity.PadBounds;
		Site.V3Envelope.EffectBounds = Identity.EffectBounds;

		if (Index == SatelliteSiteIndex)
		{
			const FVector SatelliteUp =
				(SatelliteCandidate->E5TargetWorldTransform.GetLocation()
					- SatelliteCandidate->SatelliteCenterWorld).GetSafeNormal();
			const FVector SatelliteX =
				SatelliteCandidate->E5TargetWorldTransform
					.GetUnitAxis(EAxis::X).GetSafeNormal();
			const FVector SatelliteY =
				FVector::CrossProduct(SatelliteUp, SatelliteX).GetSafeNormal();
			if (!IsFrameValid(SatelliteX, SatelliteY, SatelliteUp))
			{
				OutFailure = TEXT("SatellitePlacementFrame");
				Reject(OutResult,
					EABTSM3JuryMapFreezeV3RejectReason::PlacementFrameInvalid);
				return false;
			}
			const FVector SurfacePivot =
				SatelliteCandidate->SatelliteCenterWorld
				+ SatelliteUp * SatelliteCandidate->SatelliteRadiusCM;
			Site.WorldTransform = FTransform(
				FRotationMatrix::MakeFromXZ(SatelliteX, SatelliteUp).ToQuat(),
				SurfacePivot,
				FVector::OneVector);
			Site.V3Envelope.SurfaceKind =
				EABTSJuryDemoFixedSixSurfaceKind::Satellite;
			Site.V3Envelope.SupportCenterWorldCM =
				SatelliteCandidate->SatelliteCenterWorld;
			Site.V3Envelope.SupportRadiusCM =
				SatelliteCandidate->SatelliteRadiusCM;
			Site.V3Envelope.GravityAuthorityId = SatelliteGravityAuthority;
			Site.V3Envelope.GravityIdentityHash = SatelliteGravityHash;
			Placement.AttackCorridorWorldDirection = SatelliteX;
		}
		else
		{
			const FABTSM3MonthlySpatialEncounter& Encounter =
				Candidate->Encounters[Index];
			if (!Cells.IsValidIndex(Encounter.TargetAnchorCellId)
				|| Encounter.AttackFaceDirection.IsNearlyZero())
			{
				OutFailure = FString::Printf(TEXT("Encounter:%d"), Index);
				Reject(OutResult,
					EABTSM3JuryMapFreezeV3RejectReason::SourceIdentityMismatch);
				return false;
			}
			Placement.TargetAnchorCellId = Encounter.TargetAnchorCellId;
			TArray<int32> CenterCellIds;
			CenterCellIds.Add(Encounter.TargetAnchorCellId);
			for (const int32 CellId : Encounter.TargetNoRoadCellIds)
			{
				CenterCellIds.AddUnique(CellId);
			}
			for (const int32 CellId : Encounter.TargetFootprintCellIds)
			{
				CenterCellIds.AddUnique(CellId);
			}
			const FVector AnchorDirection =
				Cells[Encounter.TargetAnchorCellId].UnitCenter;
			CenterCellIds.Sort(
				[&Cells, &AnchorDirection](const int32 A, const int32 B)
				{
					if (!Cells.IsValidIndex(A)) return false;
					if (!Cells.IsValidIndex(B)) return true;
					const double ADot = FVector::DotProduct(
						Cells[A].UnitCenter, AnchorDirection);
					const double BDot = FVector::DotProduct(
						Cells[B].UnitCenter, AnchorDirection);
					return ADot != BDot ? ADot > BDot : A < B;
				});

			bool bReserved = false;
			FString ReservationFailure;
			for (const int32 CenterCellId : CenterCellIds)
			{
				if (!Cells.IsValidIndex(CenterCellId)
					|| !Candidate->Cells.IsValidIndex(CenterCellId)
					|| Candidate->Cells[CenterCellId].bWater
					|| Candidate->RecomputedRoute.OrderedRoadCellIds.Contains(
						CenterCellId))
				{
					continue;
				}
				const FVector SiteZ =
					Cells[CenterCellId].UnitCenter.GetSafeNormal();
				FVector SiteX = FVector::VectorPlaneProject(
					Encounter.AttackFaceDirection,
					SiteZ).GetSafeNormal();
				const FVector SiteY =
					FVector::CrossProduct(SiteZ, SiteX).GetSafeNormal();
				SiteX = FVector::CrossProduct(SiteY, SiteZ).GetSafeNormal();
				if (!IsFrameValid(SiteX, SiteY, SiteZ))
				{
					continue;
				}
				const FVector SiteLocation =
					PrimaryCenterWorldCM + SiteZ * PrimaryRadiusCM;
				TArray<int32> PadCells;
				TArray<int32> EffectCells;
				if (!ReserveBounds(
						Cells, *Candidate, PrimaryCenterWorldCM,
						SiteLocation, SiteX, SiteY, Identity.PadBounds,
						PadCells, ReservationFailure)
					|| !ReserveBounds(
						Cells, *Candidate, PrimaryCenterWorldCM,
						SiteLocation, SiteX, SiteY, Identity.EffectBounds,
						EffectCells, ReservationFailure))
				{
					continue;
				}
				Placement.PadCenterCellId = CenterCellId;
				Placement.ReservedPadCellIds = MoveTemp(PadCells);
				Placement.ReservedEffectCellIds = MoveTemp(EffectCells);
				Placement.AttackCorridorWorldDirection = SiteX;
				Site.WorldTransform = FTransform(
					FRotationMatrix::MakeFromXZ(SiteX, SiteZ).ToQuat(),
					SiteLocation,
					FVector::OneVector);
				bReserved = true;
				break;
			}
			if (!bReserved)
			{
				OutFailure = FString::Printf(
					TEXT("PrimaryReservation:%d:%s:Centers=%d"),
					Index, *ReservationFailure, CenterCellIds.Num());
				Reject(OutResult,
					EABTSM3JuryMapFreezeV3RejectReason::PlacementReservationFailed);
				return false;
			}
			Site.V3Envelope.SurfaceKind =
				EABTSJuryDemoFixedSixSurfaceKind::PrimaryPlanet;
			Site.V3Envelope.SupportCenterWorldCM = PrimaryCenterWorldCM;
			Site.V3Envelope.SupportRadiusCM = PrimaryRadiusCM;
			Site.V3Envelope.GravityAuthorityId = PrimaryGravityAuthority;
			Site.V3Envelope.GravityIdentityHash = PrimaryGravityHash;
		}

		Placement.HorizontalLongAxisWorld = ResolveHorizontalLongAxis(
			Site.LocalBounds,
			Site.WorldTransform).GetSafeNormal();
		Placement.AttackCorridorLongAxisAbsDot = FMath::Abs(
			FVector::DotProduct(
				Placement.AttackCorridorWorldDirection.GetSafeNormal(),
				Placement.HorizontalLongAxisWorld));
		FString OrientationFailure;
		if (!ValidatePlacementOrientation(Placement, OrientationFailure))
		{
			OutFailure = OrientationFailure;
			Reject(OutResult,
				EABTSM3JuryMapFreezeV3RejectReason::AttackCorridorOrientationInvalid);
			return false;
		}
		Site.V3Envelope.PlacementHash = ComputePlacementHash(Placement);
		OutResult.HandoffContract.Sites.Add(Site);
		OutResult.Placements.Add(MoveTemp(Placement));
	}

	for (int32 A = 0; A < OutResult.Placements.Num(); ++A)
	{
		const FABTSM3JuryMapFreezeV3Placement& First =
			OutResult.Placements[A];
		if (First.Site.V3Envelope.SurfaceKind
			!= EABTSJuryDemoFixedSixSurfaceKind::PrimaryPlanet)
		{
			continue;
		}
		for (int32 B = A + 1; B < OutResult.Placements.Num(); ++B)
		{
			const FABTSM3JuryMapFreezeV3Placement& Second =
				OutResult.Placements[B];
			if (Second.Site.V3Envelope.SurfaceKind
				!= EABTSJuryDemoFixedSixSurfaceKind::PrimaryPlanet)
			{
				continue;
			}
			const FVector FirstUp =
				(First.Site.WorldTransform.GetTranslation()
					- PrimaryCenterWorldCM).GetSafeNormal();
			const FVector SecondUp =
				(Second.Site.WorldTransform.GetTranslation()
					- PrimaryCenterWorldCM).GetSafeNormal();
			const double SurfaceDistanceCM = PrimaryRadiusCM * FMath::Acos(
				FMath::Clamp(FVector::DotProduct(FirstUp, SecondUp), -1.0, 1.0));
			const double FirstRadiusCM = FMath::Max(
				HorizontalEnvelopeRadiusCM(First.Site.V3Envelope.PadBounds),
				HorizontalEnvelopeRadiusCM(First.Site.V3Envelope.EffectBounds));
			const double SecondRadiusCM = FMath::Max(
				HorizontalEnvelopeRadiusCM(Second.Site.V3Envelope.PadBounds),
				HorizontalEnvelopeRadiusCM(Second.Site.V3Envelope.EffectBounds));
			if (SurfaceDistanceCM <= FirstRadiusCM + SecondRadiusCM + 180.0)
			{
				OutFailure = FString::Printf(
					TEXT("PrimaryEnvelopeSeparation:%d:%d"), A, B);
				Reject(OutResult,
					EABTSM3JuryMapFreezeV3RejectReason::PlacementSeparationFailed);
				return false;
			}
		}
	}

	OutResult.RejectReason = EABTSM3JuryMapFreezeV3RejectReason::None;
	OutResult.bMapFreezeReady = true;
	OutResult.LayoutHash = ComputeLayoutHash(OutResult);
	OutResult.HandoffContract.LayoutHash = OutResult.LayoutHash;
	if (OutResult.LayoutHash == 0
		|| !OutResult.HandoffContract.IsStructurallyUsableV3())
	{
		OutFailure = TEXT("LayoutHashOrFinalContract");
		Reject(OutResult,
			EABTSM3JuryMapFreezeV3RejectReason::HashMismatch);
		return false;
	}
	return true;
}

bool FABTSM3JuryMapFreezeV3Builder::Validate(
	const TArray<FABTSM2Cell>& Cells,
	const FVector& PrimaryCenterWorldCM,
	const double PrimaryRadiusCM,
	const double PrimarySurfaceGravityCMPerSec2,
	const FABTSM3MonthlySpatialResult& SpatialResult,
	const FABTSM3MonthlySatellitePreviewResult& SatellitePreviewResult,
	const FABTSM3JuryMapFreezeV3Result& Result,
	EABTSM3JuryMapFreezeV3RejectReason& OutReason,
	FString& OutFailure)
{
	using namespace ABTSM3JuryMapFreezeV3Private;
	OutReason = EABTSM3JuryMapFreezeV3RejectReason::None;
	OutFailure.Reset();
	if (!Result.bMapFreezeReady
		|| Result.RejectReason != EABTSM3JuryMapFreezeV3RejectReason::None
		|| Result.Placements.Num() != ExpectedSiteCount
		|| Result.HandoffContract.Sites.Num() != ExpectedSiteCount
		|| !Result.HandoffContract.IsStructurallyUsableV3())
	{
		OutReason = EABTSM3JuryMapFreezeV3RejectReason::StructuralContractInvalid;
		OutFailure = TEXT("ResultStructuralState");
		return false;
	}
	int32 PrimaryCount = 0;
	int32 SatelliteCount = 0;
	for (int32 Index = 0; Index < Result.Placements.Num(); ++Index)
	{
		const FABTSM3JuryMapFreezeV3Placement& Placement =
			Result.Placements[Index];
		FABTSM3JuryMapFreezeV3Placement PublishedPlacement = Placement;
		PublishedPlacement.Site = Result.HandoffContract.Sites[Index];
		if (Placement.Site.EncounterIndex != Index
			|| ComputePlacementHash(PublishedPlacement)
				!= Placement.Site.V3Envelope.PlacementHash
			|| Placement.Site.V3Envelope.PlacementHash
				!= ComputePlacementHash(Placement))
		{
			OutReason = EABTSM3JuryMapFreezeV3RejectReason::HashMismatch;
			OutFailure = FString::Printf(TEXT("PlacementHash:%d"), Index);
			return false;
		}
		if (!ValidatePlacementOrientation(Placement, OutFailure))
		{
			OutReason =
				EABTSM3JuryMapFreezeV3RejectReason::AttackCorridorOrientationInvalid;
			return false;
		}
		if (Placement.Site.V3Envelope.SurfaceKind
			== EABTSJuryDemoFixedSixSurfaceKind::Satellite)
		{
			++SatelliteCount;
			if (Index != SatelliteSiteIndex
				|| Placement.TargetAnchorCellId != INDEX_NONE
				|| Placement.PadCenterCellId != INDEX_NONE
				|| !Placement.ReservedPadCellIds.IsEmpty()
				|| !Placement.ReservedEffectCellIds.IsEmpty())
			{
				OutReason = EABTSM3JuryMapFreezeV3RejectReason::StructuralContractInvalid;
				OutFailure = TEXT("SatelliteOwnsPrimaryReservation");
				return false;
			}
		}
		else
		{
			++PrimaryCount;
			if (Index == SatelliteSiteIndex
				|| Placement.PadCenterCellId == INDEX_NONE
				|| Placement.ReservedPadCellIds.IsEmpty()
				|| Placement.ReservedEffectCellIds.IsEmpty())
			{
				OutReason = EABTSM3JuryMapFreezeV3RejectReason::StructuralContractInvalid;
				OutFailure = TEXT("PrimaryReservationMissing");
				return false;
			}
		}
	}
	if (PrimaryCount != ExpectedPrimarySiteCount || SatelliteCount != 1
		|| Result.LayoutHash == 0
		|| Result.LayoutHash != ComputeLayoutHash(Result)
		|| Result.HandoffContract.LayoutHash != Result.LayoutHash)
	{
		OutReason = EABTSM3JuryMapFreezeV3RejectReason::HashMismatch;
		OutFailure = TEXT("SurfaceCountOrLayoutHash");
		return false;
	}

	FABTSM3JuryMapFreezeV3Result Expected;
	FString ExpectedFailure;
	if (!Build(
			Cells,
			PrimaryCenterWorldCM,
			PrimaryRadiusCM,
			PrimarySurfaceGravityCMPerSec2,
			SpatialResult,
			SatellitePreviewResult,
			Expected,
			ExpectedFailure)
		|| Expected.LayoutHash != Result.LayoutHash)
	{
		OutReason = EABTSM3JuryMapFreezeV3RejectReason::HashMismatch;
		OutFailure = FString::Printf(
			TEXT("CanonicalRebuild:%s"), *ExpectedFailure);
		return false;
	}
	return true;
}

const TCHAR* FABTSM3JuryMapFreezeV3Builder::GetRejectReasonName(
	const EABTSM3JuryMapFreezeV3RejectReason Reason)
{
	switch (Reason)
	{
	case EABTSM3JuryMapFreezeV3RejectReason::None:
		return TEXT("None");
	case EABTSM3JuryMapFreezeV3RejectReason::NotEvaluated:
		return TEXT("NotEvaluated");
	case EABTSM3JuryMapFreezeV3RejectReason::InvalidInput:
		return TEXT("InvalidInput");
	case EABTSM3JuryMapFreezeV3RejectReason::SourceIdentityMismatch:
		return TEXT("SourceIdentityMismatch");
	case EABTSM3JuryMapFreezeV3RejectReason::FrozenCatalogMismatch:
		return TEXT("FrozenCatalogMismatch");
	case EABTSM3JuryMapFreezeV3RejectReason::PlacementReservationFailed:
		return TEXT("PlacementReservationFailed");
	case EABTSM3JuryMapFreezeV3RejectReason::PlacementSeparationFailed:
		return TEXT("PlacementSeparationFailed");
	case EABTSM3JuryMapFreezeV3RejectReason::PlacementFrameInvalid:
		return TEXT("PlacementFrameInvalid");
	case EABTSM3JuryMapFreezeV3RejectReason::AttackCorridorOrientationInvalid:
		return TEXT("AttackCorridorOrientationInvalid");
	case EABTSM3JuryMapFreezeV3RejectReason::StructuralContractInvalid:
		return TEXT("StructuralContractInvalid");
	case EABTSM3JuryMapFreezeV3RejectReason::HashMismatch:
		return TEXT("HashMismatch");
	default:
		return TEXT("Unknown");
	}
}
