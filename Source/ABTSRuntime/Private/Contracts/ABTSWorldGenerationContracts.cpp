// Copyright Epic Games, Inc. All Rights Reserved.

#include "Contracts/ABTSWorldGenerationContracts.h"

namespace
{
bool IsFiniteContractVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X)
		&& FMath::IsFinite(Value.Y)
		&& FMath::IsFinite(Value.Z);
}

bool IsFiniteWorldGenerationContractBox(const FBox& Box)
{
	return Box.IsValid
		&& IsFiniteContractVector(Box.Min)
		&& IsFiniteContractVector(Box.Max);
}

bool IsNearlyEqualWorldGenerationContractBox(
	const FBox& A,
	const FBox& B,
	const double Tolerance)
{
	return IsFiniteWorldGenerationContractBox(A)
		&& IsFiniteWorldGenerationContractBox(B)
		&& A.Min.Equals(B.Min, Tolerance)
		&& A.Max.Equals(B.Max, Tolerance);
}

struct FFixedSixV2SiteIdentity
{
	const TCHAR* ManifestEntryId = TEXT("");
	uint64 DescriptorHash = 0;
	uint64 StaticGeometryHash = 0;
	uint64 ProductionIdentityHash = 0;
	uint64 DeviceAssemblyHash = 0;
	FBox EffectBounds = FBox(EForceInit::ForceInit);
};

struct FFixedSixV3SiteIdentity
{
	const TCHAR* ManifestEntryId = TEXT("");
	int32 DifficultyTier = INDEX_NONE;
	int32 BuildingSeed = 0;
	uint64 DescriptorHash = 0;
	uint64 StaticGeometryHash = 0;
	uint64 ProductionIdentityHash = 0;
	uint64 DeviceAssemblyHash = 0;
	FBox SiteLocalBounds = FBox(EForceInit::ForceInit);
	FBox PadBounds = FBox(EForceInit::ForceInit);
	FBox EffectBounds = FBox(EForceInit::ForceInit);
	EABTSJuryDemoFixedSixSurfaceKind SurfaceKind =
		EABTSJuryDemoFixedSixSurfaceKind::Unknown;
};

const FFixedSixV2SiteIdentity& GetFixedSixV2SiteIdentity(const int32 Index)
{
	static const FFixedSixV2SiteIdentity Identities[] = {
		{TEXT("E1ColumnBreak"), 10113758205408230493ull,
			10276011350224018878ull, 6524532268529485689ull,
			12560907909080588493ull,
			FBox(FVector(-1102.0, -850.0, -670.0), FVector(418.0, 670.0, 850.0))},
		{TEXT("E2DropTrigger"), 1108134973396587699ull,
			1243337162086650128ull, 3864694895529971157ull,
			1033929311817437759ull,
			FBox(FVector(-1462.0, -1138.0, -670.0), FVector(58.0, 382.0, 850.0))},
		{TEXT("E3SlideRelease"), 17683520519518435068ull,
			3075258440093988143ull, 15118401498293854757ull,
			6073774060920401162ull,
			FBox(FVector(-1134.0, -522.0, -252.0), FVector(-774.0, -162.0, 468.0))},
		{TEXT("E4TipOver"), 11089610541129920709ull,
			4328116049969586954ull, 3596567542130940914ull,
			3035395675580472088ull,
			FBox(FVector(-378.0, -486.0, -144.0), FVector(342.0, -126.0, 216.0))},
		{TEXT("E5SeamRelease"), 7322844578368466709ull,
			461929562625370845ull, 12062404675177644267ull,
			9042370151666144586ull,
			FBox(FVector(-1566.0, 126.0, -144.0), FVector(-846.0, 486.0, 216.0))},
		{TEXT("E6TipOver"), 3963542007450344969ull,
			6610608065286482828ull, 10510335516369342439ull,
			1309116746468502251ull,
			FBox(FVector(-1278.0, -594.0, -144.0), FVector(-558.0, -234.0, 216.0))}
	};
	check(Index >= 0 && Index < UE_ARRAY_COUNT(Identities));
	return Identities[Index];
}

const FFixedSixV3SiteIdentity& GetFixedSixV3SiteIdentity(const int32 Index)
{
	static const FFixedSixV3SiteIdentity Identities[] = {
		{TEXT("E2DropTrigger"), 1, 740000, 2093905216809054552ull,
			9035518740462017661ull, 2547697344996591725ull,
			17110365351347297356ull,
			FBox(FVector(-450.0, -486.0, 0.0), FVector(450.0, 774.0, 1476.0)),
			FBox(FVector(-486.0, -522.0, 0.0), FVector(486.0, 810.0, 1476.0)),
			FBox(FVector(-1138.0, -58.0, -670.0), FVector(382.0, 1462.0, 850.0)),
			EABTSJuryDemoFixedSixSurfaceKind::PrimaryPlanet},
		{TEXT("E3SlideRelease"), 2, 750137, 4766851746474182140ull,
			6056576068412876568ull, 5980623437000438947ull,
			13499840356386341553ull,
			FBox(FVector(-414.0, -1026.0, 0.0), FVector(414.0, 1026.0, 1332.0)),
			FBox(FVector(-450.0, -1062.0, 0.0), FVector(450.0, 1062.0, 1332.0)),
			FBox(FVector(-522.0, 774.0, -252.0), FVector(-162.0, 1134.0, 468.0)),
			EABTSJuryDemoFixedSixSurfaceKind::PrimaryPlanet},
		{TEXT("E4TipOver"), 3, 730000, 4414623922721955589ull,
			12346635070808564758ull, 10546537168470496360ull,
			4267868371875890409ull,
			FBox(FVector(-378.0, -846.0, 0.0), FVector(378.0, 846.0, 2376.0)),
			FBox(FVector(-414.0, -882.0, 0.0), FVector(414.0, 882.0, 2376.0)),
			FBox(FVector(-486.0, -342.0, -144.0), FVector(-126.0, 378.0, 216.0)),
			EABTSJuryDemoFixedSixSurfaceKind::PrimaryPlanet},
		{TEXT("E5SeamRelease"), 4, 720000, 543918785024958331ull,
			17932683668713717862ull, 11772527566289753088ull,
			15204117308279581184ull,
			FBox(FVector(-630.0, -1350.0, 0.0), FVector(630.0, 1350.0, 2376.0)),
			FBox(FVector(-666.0, -1386.0, 0.0), FVector(666.0, 1386.0, 2376.0)),
			FBox(FVector(126.0, 846.0, -144.0), FVector(486.0, 1566.0, 216.0)),
			EABTSJuryDemoFixedSixSurfaceKind::PrimaryPlanet},
		{TEXT("E1ColumnBreak"), 0, 710000, 12209885623584966783ull,
			5109319969545358893ull, 7092558964138954002ull,
			1306678247463021210ull,
			FBox(FVector(-162.0, 90.0, 0.0), FVector(162.0, 414.0, 756.0)),
			FBox(FVector(-198.0, 54.0, 0.0), FVector(198.0, 450.0, 756.0)),
			FBox(FVector(-850.0, -418.0, -670.0), FVector(670.0, 1102.0, 850.0)),
			EABTSJuryDemoFixedSixSurfaceKind::Satellite},
		{TEXT("E6TipOver"), 5, 750000, 3187373410644525608ull,
			11440919070458269246ull, 11323455661476895076ull,
			198894657042108135ull,
			FBox(FVector(-486.0, -1062.0, 0.0), FVector(486.0, 1062.0, 3384.0)),
			FBox(FVector(-522.0, -1098.0, 0.0), FVector(522.0, 1098.0, 3384.0)),
			FBox(FVector(-594.0, 558.0, -144.0), FVector(-234.0, 1278.0, 216.0)),
			EABTSJuryDemoFixedSixSurfaceKind::PrimaryPlanet}
	};
	check(Index >= 0 && Index < UE_ARRAY_COUNT(Identities));
	return Identities[Index];
}

bool IsUsableFixedSixV3Site(
	const FABTSJuryDemoFixedSixBuildingSite& Site,
	double Tolerance);
}

bool FABTSGeneratedWorldIdentity::IsUsable() const
{
	return ContractVersion == CurrentContractVersion
		&& GeneratorVersion > 0
		&& GenerationAttempt >= 0
		&& bSourceWorldAccepted;
}

bool FABTSGeneratedBuildingSite::IsUsable(const double Tolerance) const
{
	const double SafeTolerance = FMath::Max(Tolerance, UE_DOUBLE_SMALL_NUMBER);
	const FVector Scale = WorldTransform.GetScale3D();
	if (SiteId == MAX_uint64
		|| TaskId < 0
		|| CellId < 0
		|| Purpose >= EABTSGeneratedBuildingPurpose::Count
		|| !WorldTransform.IsValid()
		|| !Scale.Equals(FVector::OneVector, SafeTolerance)
		|| !FMath::IsFinite(MaxSlopeDegrees)
		|| MaxSlopeDegrees < 0.0f
		|| MaxSlopeDegrees > 180.0f
		|| !IsFiniteContractVector(AnchorDirection)
		|| !IsFiniteContractVector(TangentForward)
		|| !IsFiniteContractVector(TangentRight)
		|| !FMath::IsFinite(PadHalfExtentCM.X)
		|| !FMath::IsFinite(PadHalfExtentCM.Y)
		|| PadHalfExtentCM.X <= 0.0f
		|| PadHalfExtentCM.Y <= 0.0f
		|| !FMath::IsFinite(PadEdgeBlendWidthCM)
		|| PadEdgeBlendWidthCM < 0.0f
		|| !FMath::IsFinite(PadTargetRadiusCM)
		|| PadTargetRadiusCM <= 0.0f)
	{
		return false;
	}

	const FVector Up = AnchorDirection.GetSafeNormal();
	const FVector Forward = TangentForward.GetSafeNormal();
	const FVector Right = TangentRight.GetSafeNormal();
	return FMath::IsNearlyEqual(
			AnchorDirection.SizeSquared(),
			1.0,
			SafeTolerance)
		&& FMath::IsNearlyEqual(
			TangentForward.SizeSquared(),
			1.0,
			SafeTolerance)
		&& FMath::IsNearlyEqual(
			TangentRight.SizeSquared(),
			1.0,
			SafeTolerance)
		&& !Up.IsNearlyZero(SafeTolerance)
		&& !Forward.IsNearlyZero(SafeTolerance)
		&& !Right.IsNearlyZero(SafeTolerance)
		&& FMath::Abs(FVector::DotProduct(Up, Forward)) <= SafeTolerance
		&& FMath::Abs(FVector::DotProduct(Up, Right)) <= SafeTolerance
		&& FMath::Abs(FVector::DotProduct(Forward, Right)) <= SafeTolerance
		&& FVector::DotProduct(
			FVector::CrossProduct(Forward, Right),
			Up) >= 1.0 - SafeTolerance
		&& FVector::DotProduct(
			WorldTransform.GetUnitAxis(EAxis::X),
			Forward) >= 1.0 - SafeTolerance
		&& FVector::DotProduct(
			WorldTransform.GetUnitAxis(EAxis::Y),
			Right) >= 1.0 - SafeTolerance
		&& FVector::DotProduct(
			WorldTransform.GetUnitAxis(EAxis::Z),
			Up) >= 1.0 - SafeTolerance;
}

bool FABTSJuryDemoFixedSixV2Envelope::IsEmpty() const
{
	return StaticGeometryHash == 0
		&& ProductionIdentityHash == 0
		&& DeviceAssemblyHash == 0
		&& !PhysicalBounds.IsValid
		&& !EffectBounds.IsValid
		&& !bDynamicEnvelopeRequired;
}

bool FABTSJuryDemoFixedSixV3Envelope::IsEmpty() const
{
	return StaticGeometryHash == 0
		&& ProductionIdentityHash == 0
		&& DeviceAssemblyHash == 0
		&& !SiteLocalBounds.IsValid
		&& !PadBounds.IsValid
		&& !EffectBounds.IsValid
		&& SurfaceKind == EABTSJuryDemoFixedSixSurfaceKind::Unknown
		&& SupportCenterWorldCM.IsZero()
		&& SupportRadiusCM == 0.0
		&& GravityAuthorityId.IsNone()
		&& GravityIdentityHash == 0
		&& PlacementHash == 0;
}

bool FABTSJuryDemoFixedSixBuildingSite::IsUsable(
	const double Tolerance) const
{
	const double SafeTolerance = FMath::Max(Tolerance, UE_DOUBLE_SMALL_NUMBER);
	const FVector Scale = WorldTransform.GetScale3D();
	if (ManifestEntryId.IsNone()
		|| EncounterIndex < 0
		|| EncounterIndex >= FABTSJuryDemoFixedSixContract::ExpectedSiteCount
		|| DifficultyTier != EncounterIndex
		|| DeterministicSeed <= 0
		|| DescriptorHash == 0
		|| !WorldTransform.IsValid()
		|| !Scale.Equals(FVector::OneVector, SafeTolerance)
		|| !FMath::IsFinite(PadHalfExtentCM.X)
		|| !FMath::IsFinite(PadHalfExtentCM.Y)
		|| PadHalfExtentCM.X <= 0.0
		|| PadHalfExtentCM.Y <= 0.0
		|| !IsFiniteWorldGenerationContractBox(LocalBounds)
		|| LocalBounds.Max.X < LocalBounds.Min.X
		|| LocalBounds.Max.Y < LocalBounds.Min.Y
		|| LocalBounds.Max.Z <= LocalBounds.Min.Z
		|| LocalBounds.Min.Z < -SafeTolerance)
	{
		return false;
	}

	const double RequiredHalfExtentX = FMath::Max(
		FMath::Abs(LocalBounds.Min.X),
		FMath::Abs(LocalBounds.Max.X));
	const double RequiredHalfExtentY = FMath::Max(
		FMath::Abs(LocalBounds.Min.Y),
		FMath::Abs(LocalBounds.Max.Y));
	return PadHalfExtentCM.X + SafeTolerance >= RequiredHalfExtentX
		&& PadHalfExtentCM.Y + SafeTolerance >= RequiredHalfExtentY;
}

bool FABTSJuryDemoFixedSixBuildingSite::IsUsableForContractVersion(
	const int32 ContractVersion,
	const double Tolerance) const
{
	if (ContractVersion
		== FABTSJuryDemoFixedSixContract::SupportedV3ContractVersion)
	{
		return IsUsableFixedSixV3Site(*this, Tolerance);
	}
	if (!IsUsable(Tolerance))
	{
		return false;
	}
	if (ContractVersion == FABTSJuryDemoFixedSixContract::CurrentContractVersion)
	{
		return V2Envelope.IsEmpty() && V3Envelope.IsEmpty();
	}
	if (ContractVersion
		== FABTSJuryDemoFixedSixContract::SupportedV2ContractVersion)
	{
		if (!V3Envelope.IsEmpty())
		{
			return false;
		}
	}
	else
	{
		return false;
	}

	const double SafeTolerance = FMath::Max(Tolerance, UE_DOUBLE_SMALL_NUMBER);
	const FFixedSixV2SiteIdentity& Expected =
		GetFixedSixV2SiteIdentity(EncounterIndex);
	if (ManifestEntryId != FName(Expected.ManifestEntryId)
		|| DescriptorHash != Expected.DescriptorHash
		|| V2Envelope.StaticGeometryHash != Expected.StaticGeometryHash
		|| V2Envelope.ProductionIdentityHash
			!= Expected.ProductionIdentityHash
		|| V2Envelope.DeviceAssemblyHash != Expected.DeviceAssemblyHash
		|| !IsNearlyEqualWorldGenerationContractBox(
			V2Envelope.PhysicalBounds, LocalBounds, SafeTolerance)
		|| !IsNearlyEqualWorldGenerationContractBox(
			V2Envelope.EffectBounds, Expected.EffectBounds, SafeTolerance))
	{
		return false;
	}

	const double PhysicalHalfExtentX = FMath::Max(
		FMath::Abs(V2Envelope.PhysicalBounds.Min.X),
		FMath::Abs(V2Envelope.PhysicalBounds.Max.X));
	const double PhysicalHalfExtentY = FMath::Max(
		FMath::Abs(V2Envelope.PhysicalBounds.Min.Y),
		FMath::Abs(V2Envelope.PhysicalBounds.Max.Y));
	if (PadHalfExtentCM.X - PhysicalHalfExtentX + SafeTolerance < 36.0
		|| PadHalfExtentCM.Y - PhysicalHalfExtentY + SafeTolerance < 36.0)
	{
		return false;
	}

	const bool bEffectInsidePad =
		V2Envelope.EffectBounds.Min.X >= -PadHalfExtentCM.X - SafeTolerance
		&& V2Envelope.EffectBounds.Max.X <= PadHalfExtentCM.X + SafeTolerance
		&& V2Envelope.EffectBounds.Min.Y >= -PadHalfExtentCM.Y - SafeTolerance
		&& V2Envelope.EffectBounds.Max.Y <= PadHalfExtentCM.Y + SafeTolerance;
	return V2Envelope.bDynamicEnvelopeRequired == !bEffectInsidePad;
}

namespace
{
bool IsUsableFixedSixV3Site(
	const FABTSJuryDemoFixedSixBuildingSite& Site,
	const double Tolerance)
{
	const double SafeTolerance = FMath::Max(Tolerance, UE_DOUBLE_SMALL_NUMBER);
	if (Site.EncounterIndex < 0
		|| Site.EncounterIndex
			>= FABTSJuryDemoFixedSixContract::ExpectedSiteCount
		|| !Site.V2Envelope.IsEmpty()
		|| Site.V3Envelope.IsEmpty()
		|| !Site.WorldTransform.IsValid()
		|| !Site.WorldTransform.GetScale3D().Equals(
			FVector::OneVector, SafeTolerance))
	{
		return false;
	}

	const FFixedSixV3SiteIdentity& Expected =
		GetFixedSixV3SiteIdentity(Site.EncounterIndex);
	const FABTSJuryDemoFixedSixV3Envelope& Envelope = Site.V3Envelope;
	if (Site.ManifestEntryId != FName(Expected.ManifestEntryId)
		|| Site.DifficultyTier != Expected.DifficultyTier
		|| Site.DeterministicSeed != Expected.BuildingSeed
		|| Site.DescriptorHash != Expected.DescriptorHash
		|| Envelope.StaticGeometryHash != Expected.StaticGeometryHash
		|| Envelope.ProductionIdentityHash != Expected.ProductionIdentityHash
		|| Envelope.DeviceAssemblyHash != Expected.DeviceAssemblyHash
		|| Envelope.SurfaceKind != Expected.SurfaceKind
		|| Envelope.SurfaceKind
			>= EABTSJuryDemoFixedSixSurfaceKind::Count
		|| Envelope.GravityAuthorityId.IsNone()
		|| Envelope.GravityIdentityHash == 0
		|| Envelope.PlacementHash == 0
		|| !FMath::IsFinite(Envelope.SupportRadiusCM)
		|| Envelope.SupportRadiusCM <= 0.0
		|| !IsFiniteContractVector(Envelope.SupportCenterWorldCM)
		|| !IsNearlyEqualWorldGenerationContractBox(
			Site.LocalBounds, Expected.SiteLocalBounds, SafeTolerance)
		|| !IsNearlyEqualWorldGenerationContractBox(
			Envelope.SiteLocalBounds, Expected.SiteLocalBounds, SafeTolerance)
		|| !IsNearlyEqualWorldGenerationContractBox(
			Envelope.PadBounds, Expected.PadBounds, SafeTolerance)
		|| !IsNearlyEqualWorldGenerationContractBox(
			Envelope.EffectBounds, Expected.EffectBounds, SafeTolerance))
	{
		return false;
	}

	const double ExpectedPadHalfExtentX = FMath::Max(
		FMath::Abs(Expected.PadBounds.Min.X),
		FMath::Abs(Expected.PadBounds.Max.X));
	const double ExpectedPadHalfExtentY = FMath::Max(
		FMath::Abs(Expected.PadBounds.Min.Y),
		FMath::Abs(Expected.PadBounds.Max.Y));
	if (!FMath::IsNearlyEqual(
			Site.PadHalfExtentCM.X, ExpectedPadHalfExtentX, SafeTolerance)
		|| !FMath::IsNearlyEqual(
			Site.PadHalfExtentCM.Y, ExpectedPadHalfExtentY, SafeTolerance))
	{
		return false;
	}

	const FVector RadialOffset =
		Site.WorldTransform.GetTranslation() - Envelope.SupportCenterWorldCM;
	const double SurfaceTolerance = FMath::Max(
		SafeTolerance, Envelope.SupportRadiusCM * 1.0e-6);
	if (!FMath::IsNearlyEqual(
			RadialOffset.Size(), Envelope.SupportRadiusCM, SurfaceTolerance))
	{
		return false;
	}
	const FVector RadialUp = RadialOffset.GetSafeNormal();
	return !RadialUp.IsNearlyZero(SafeTolerance)
		&& FVector::DotProduct(
			Site.WorldTransform.GetUnitAxis(EAxis::Z), RadialUp)
			>= 1.0 - SafeTolerance;
}
}

bool FABTSJuryDemoFixedSixContract::IsEmpty() const
{
	return ContractVersion == 0
		&& PlacementSchemaVersion == 0
		&& DemoManifestVersion == 0
		&& DemoManifestHash == 0
		&& PlacementCatalogHash == 0
		&& WorldSeed == 0
		&& CandidateId == INDEX_NONE
		&& LayoutHash == 0
		&& Sites.IsEmpty();
}

bool FABTSJuryDemoFixedSixContract::IsStructurallyUsableV3(
	const double Tolerance) const
{
	if (ContractVersion != SupportedV3ContractVersion
		|| PlacementSchemaVersion != FrozenV3PlacementSchemaVersion
		|| DemoManifestVersion != FrozenDemoManifestVersion
		|| DemoManifestHash != FrozenDemoManifestHash
		|| PlacementCatalogHash != FrozenV3PlacementCatalogHash
		|| WorldSeed != FrozenWorldSeed
		|| CandidateId != FrozenCandidateId
		|| LayoutHash == 0
		|| Sites.Num() != ExpectedSiteCount)
	{
		return false;
	}

	TSet<FName> ManifestEntryIds;
	TSet<uint64> DescriptorHashes;
	TSet<uint64> PlacementHashes;
	const FABTSJuryDemoFixedSixV3Envelope* PrimarySurface = nullptr;
	const FABTSJuryDemoFixedSixV3Envelope* SatelliteSurface = nullptr;
	const double SafeTolerance = FMath::Max(Tolerance, UE_DOUBLE_SMALL_NUMBER);
	for (int32 Index = 0; Index < Sites.Num(); ++Index)
	{
		const FABTSJuryDemoFixedSixBuildingSite& Site = Sites[Index];
		if (Site.EncounterIndex != Index
			|| !IsUsableFixedSixV3Site(Site, SafeTolerance)
			|| ManifestEntryIds.Contains(Site.ManifestEntryId)
			|| DescriptorHashes.Contains(Site.DescriptorHash)
			|| PlacementHashes.Contains(Site.V3Envelope.PlacementHash))
		{
			return false;
		}
		ManifestEntryIds.Add(Site.ManifestEntryId);
		DescriptorHashes.Add(Site.DescriptorHash);
		PlacementHashes.Add(Site.V3Envelope.PlacementHash);

		if (Site.V3Envelope.SurfaceKind
			== EABTSJuryDemoFixedSixSurfaceKind::Satellite)
		{
			if (SatelliteSurface != nullptr)
			{
				return false;
			}
			SatelliteSurface = &Site.V3Envelope;
		}
		else if (PrimarySurface == nullptr)
		{
			PrimarySurface = &Site.V3Envelope;
		}
		else if (!PrimarySurface->SupportCenterWorldCM.Equals(
				Site.V3Envelope.SupportCenterWorldCM, SafeTolerance)
			|| !FMath::IsNearlyEqual(
				PrimarySurface->SupportRadiusCM,
				Site.V3Envelope.SupportRadiusCM,
				SafeTolerance)
			|| PrimarySurface->GravityAuthorityId
				!= Site.V3Envelope.GravityAuthorityId
			|| PrimarySurface->GravityIdentityHash
				!= Site.V3Envelope.GravityIdentityHash)
		{
			return false;
		}
	}

	return PrimarySurface != nullptr
		&& SatelliteSurface != nullptr
		&& !PrimarySurface->SupportCenterWorldCM.Equals(
			SatelliteSurface->SupportCenterWorldCM, SafeTolerance)
		&& PrimarySurface->GravityAuthorityId
			!= SatelliteSurface->GravityAuthorityId
		&& PrimarySurface->GravityIdentityHash
			!= SatelliteSurface->GravityIdentityHash;
}

bool FABTSJuryDemoFixedSixContract::IsUsable(const double Tolerance) const
{
	if (ContractVersion == SupportedV3ContractVersion)
	{
		if (!IsStructurallyUsableV3(Tolerance)
			|| LayoutHash != FrozenV3LayoutHash)
		{
			return false;
		}
		for (int32 Index = 0; Index < Sites.Num(); ++Index)
		{
			if (Sites[Index].V3Envelope.PlacementHash
				!= FrozenV3PlacementHashes[Index])
			{
				return false;
			}
		}
		return true;
	}

	const bool bIsV1 = ContractVersion == CurrentContractVersion;
	const bool bIsV2 = ContractVersion == SupportedV2ContractVersion;
	if ((!bIsV1 && !bIsV2)
		|| PlacementSchemaVersion != FrozenPlacementSchemaVersion
		|| DemoManifestVersion != FrozenDemoManifestVersion
		|| DemoManifestHash != FrozenDemoManifestHash
		|| PlacementCatalogHash
			!= (bIsV1
				? FrozenPlacementCatalogHash
				: FrozenV2PlacementCatalogHash)
		|| WorldSeed != FrozenWorldSeed
		|| CandidateId != FrozenCandidateId
		|| (bIsV1 && LayoutHash != FrozenLayoutHash)
		|| (bIsV2 && LayoutHash != FrozenV2LayoutHash)
		|| Sites.Num() != ExpectedSiteCount)
	{
		return false;
	}

	TSet<FName> ManifestEntryIds;
	TSet<uint64> DescriptorHashes;
	for (int32 Index = 0; Index < Sites.Num(); ++Index)
	{
		const FABTSJuryDemoFixedSixBuildingSite& Site = Sites[Index];
		if (!Site.IsUsableForContractVersion(ContractVersion, Tolerance)
			|| Site.EncounterIndex != Index
			|| ManifestEntryIds.Contains(Site.ManifestEntryId)
			|| DescriptorHashes.Contains(Site.DescriptorHash))
		{
			return false;
		}
		ManifestEntryIds.Add(Site.ManifestEntryId);
		DescriptorHashes.Add(Site.DescriptorHash);
	}
	return true;
}

bool FABTSBuildingGenerationContract::IsUsable(const double Tolerance) const
{
	if (!Identity.IsUsable()
		|| Sites.IsEmpty()
		|| (!JuryDemoFixedSix.IsEmpty()
			&& (!JuryDemoFixedSix.IsUsable(Tolerance)
				|| JuryDemoFixedSix.WorldSeed != Identity.WorldSeed)))
	{
		return false;
	}

	TSet<uint64> SiteIds;
	TSet<uint64> TaskCellPairs;
	for (const FABTSGeneratedBuildingSite& Site : Sites)
	{
		const uint64 TaskCellPair =
			(static_cast<uint64>(static_cast<uint32>(Site.TaskId)) << 32)
			| static_cast<uint32>(Site.CellId);
		if (!Site.IsUsable(Tolerance)
			|| SiteIds.Contains(Site.SiteId)
			|| TaskCellPairs.Contains(TaskCellPair))
		{
			return false;
		}
		SiteIds.Add(Site.SiteId);
		TaskCellPairs.Add(TaskCellPair);
	}
	return true;
}

bool FABTSFinaleWorldContract::IsUsable(const double Tolerance) const
{
	return Identity.IsUsable()
		&& FMath::IsFinite(PrimaryRadiusCM)
		&& PrimaryRadiusCM > 0.0
		&& LaunchFrame.IsUsable(Tolerance)
		&& LaunchFrame.WorldTransform.GetScale3D().Equals(
			FVector::OneVector,
			FMath::Max(Tolerance, UE_DOUBLE_SMALL_NUMBER));
}
