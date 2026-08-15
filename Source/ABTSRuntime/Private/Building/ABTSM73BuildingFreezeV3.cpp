// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BuildingFreezeV3.h"

#include "ABTSM73BeamD1BrickCompiler.h"
#include "Building/ABTSM73BeamDemoManifest.h"
#include "Building/ABTSM7MaterialProfileLibrary.h"

namespace ABTSM73BuildingFreezeV3Private
{
	constexpr double GeometryToleranceCM = 0.01;

	uint64 HashUtf8(const FString& Text)
	{
		FTCHARToUTF8 Utf8(*Text);
		uint64 Hash = 14695981039346656037ull;
		for (int32 Index = 0; Index < Utf8.Length(); ++Index)
		{
			Hash ^= static_cast<uint8>(Utf8.Get()[Index]);
			Hash *= 1099511628211ull;
		}
		return Hash;
	}

	int64 Q(const double Value)
	{
		return FMath::RoundToInt64(Value * 1000.0);
	}

	FString BoxRow(const FBox& Box)
	{
		return FString::Printf(TEXT("%lld,%lld,%lld,%lld,%lld,%lld"),
			Q(Box.Min.X), Q(Box.Min.Y), Q(Box.Min.Z),
			Q(Box.Max.X), Q(Box.Max.Y), Q(Box.Max.Z));
	}

	FBox TransformBox(const FBox& Box, const FTransform& Transform)
	{
		FBox Result(EForceInit::ForceInit);
		for (int32 X = 0; X < 2; ++X)
		{
			for (int32 Y = 0; Y < 2; ++Y)
			{
				for (int32 Z = 0; Z < 2; ++Z)
				{
					Result += Transform.TransformPosition(FVector(
						X == 0 ? Box.Min.X : Box.Max.X,
						Y == 0 ? Box.Min.Y : Box.Max.Y,
						Z == 0 ? Box.Min.Z : Box.Max.Z));
				}
			}
		}
		return Result;
	}

	bool HasPositiveVolumeOverlap(const FBox& A, const FBox& B)
	{
		return FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X)
			> GeometryToleranceCM
			&& FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y)
				> GeometryToleranceCM
			&& FMath::Min(A.Max.Z, B.Max.Z) - FMath::Max(A.Min.Z, B.Min.Z)
				> GeometryToleranceCM;
	}

	EABTSM73BeamAFrameAxis RotateAxis(const EABTSM73BeamAFrameAxis Axis)
	{
		if (Axis == EABTSM73BeamAFrameAxis::X) return EABTSM73BeamAFrameAxis::Y;
		if (Axis == EABTSM73BeamAFrameAxis::Y) return EABTSM73BeamAFrameAxis::X;
		return Axis;
	}

	void AddMaterial(
		FABTSM73BuildingFreezeV3MaterialHistogram& Histogram,
		const EABTSM7BuildingMaterial Material)
	{
		switch (Material)
		{
		case EABTSM7BuildingMaterial::Wood: ++Histogram.Wood; break;
		case EABTSM7BuildingMaterial::Stone: ++Histogram.Stone; break;
		case EABTSM7BuildingMaterial::Iron: ++Histogram.Iron; break;
		case EABTSM7BuildingMaterial::Glass: ++Histogram.Glass; break;
		case EABTSM7BuildingMaterial::Crystal: ++Histogram.Crystal; break;
		default: break;
		}
	}

	FString BrickRow(const FABTSM73BeamD1BrickBinding& Brick)
	{
		return FString::Printf(TEXT("B:%d:%d:%d:%d:%d:%d:%s"),
			Brick.BrickId, Brick.MemberId, static_cast<int32>(Brick.Axis),
			static_cast<int32>(Brick.StructuralRole),
			Brick.bWeaknessCandidate ? 1 : 0,
			static_cast<int32>(Brick.BrickSpec.Material),
			*BoxRow(Brick.LocalBounds));
	}

	FString DeviceRow(const FABTSM73BeamD1DeviceBinding& Device)
	{
		return FString::Printf(TEXT("D:%d:%d:%d:%s:%s"),
			Device.DeviceId, static_cast<int32>(Device.Kind),
			static_cast<int32>(Device.Axis), *BoxRow(Device.LocalBounds),
			*BoxRow(Device.EffectCorridorLocalBounds));
	}

	FString CapRow(const FABTSM73BuildingFreezeV3CapBinding& Cap)
	{
		const FString Supports = FString::JoinBy(Cap.SupportingMemberIds,
			TEXT("."), [](const int32 Id) { return FString::FromInt(Id); });
		return FString::Printf(TEXT("C:%d:%d:%d:%d:%d:%s:%s"),
			static_cast<int32>(Cap.BrickSpec.Material),
			Cap.bLoadBearing ? 1 : 0, Cap.bWeaknessCandidate ? 1 : 0,
			static_cast<int32>(Cap.DeviceRole),
			Cap.bStaticExternalLoadCertified ? 1 : 0,
			*Supports, *BoxRow(Cap.SiteLocalBounds));
	}

	FABTSM73BuildingFreezeV3FrozenIdentity MakeFrozen(
		const EABTSM73BeamDemoBuilding Id,
		const int32 EncounterSlot,
		const EABTSM7BuildingMaterial PrimaryMaterial,
		const int32 Tier,
		const int32 Seed,
		const int32 Bricks,
		const int32 Devices,
		const int32 Caps,
		const FABTSM73BuildingFreezeV3MaterialHistogram& Histogram,
		const FBox& GeneratorBounds,
		const FBox& SiteBounds,
		const FBox& PadBounds,
		const FBox& EffectBounds,
		const uint64 Stage5Hash,
		const uint64 DeviceHash,
		const uint64 StaticLoadCertificateHash,
		const uint64 GeometryHash,
		const uint64 ProductionHash,
		const uint64 DescriptorHash)
	{
		FABTSM73BuildingFreezeV3FrozenIdentity Frozen;
		Frozen.ManifestEntryId = Id;
		Frozen.EncounterSlot = EncounterSlot;
		Frozen.PrimaryMaterial = PrimaryMaterial;
		Frozen.DifficultyTier = Tier;
		Frozen.BuildingSeed = Seed;
		Frozen.BrickCount = Bricks;
		Frozen.DeviceCount = Devices;
		Frozen.CapCount = Caps;
		Frozen.MaterialHistogram = Histogram;
		Frozen.GeneratorLocalBounds = GeneratorBounds;
		Frozen.SiteLocalBounds = SiteBounds;
		Frozen.PadBounds = PadBounds;
		Frozen.EffectBounds = EffectBounds;
		Frozen.SourceStage5ProductionHash = Stage5Hash;
		Frozen.SourceDeviceAssemblyHash = DeviceHash;
		Frozen.StaticExternalLoadCertificateHash = StaticLoadCertificateHash;
		Frozen.StaticGeometryHash = GeometryHash;
		Frozen.ProductionHash = ProductionHash;
		Frozen.DescriptorHash = DescriptorHash;
		return Frozen;
	}

	FABTSM73BuildingFreezeV3MaterialHistogram Histogram(
		const int32 Wood, const int32 Stone, const int32 Iron,
		const int32 Glass, const int32 Crystal)
	{
		FABTSM73BuildingFreezeV3MaterialHistogram Result;
		Result.Wood = Wood;
		Result.Stone = Stone;
		Result.Iron = Iron;
		Result.Glass = Glass;
		Result.Crystal = Crystal;
		return Result;
	}
}

const TArray<FABTSM73BuildingFreezeV3FrozenIdentity>&
FABTSM73BuildingFreezeV3::GetFrozenIdentities()
{
	using namespace ABTSM73BuildingFreezeV3Private;
	static const TArray<FABTSM73BuildingFreezeV3FrozenIdentity> Frozen = {
		MakeFrozen(EABTSM73BeamDemoBuilding::E2DropTrigger, 0,
			EABTSM7BuildingMaterial::Wood, 1, 740000, 257, 1, 0,
			Histogram(257, 0, 0, 0, 0),
			FBox(FVector(-774, -450, 0), FVector(486, 450, 1476)),
			FBox(FVector(-450, -486, 0), FVector(450, 774, 1476)),
			FBox(FVector(-486, -522, 0), FVector(486, 810, 1476)),
			FBox(FVector(-1138, -58, -670), FVector(382, 1462, 850)),
			17685577480875777327ull, 17110365351347297356ull, 0ull,
			9035518740462017661ull, 2547697344996591725ull,
			2093905216809054552ull),
		MakeFrozen(EABTSM73BeamDemoBuilding::E3SlideRelease, 1,
			EABTSM7BuildingMaterial::Wood, 2, 750137, 388, 1, 0,
			Histogram(388, 0, 0, 0, 0),
			FBox(FVector(-1026, -414, 0), FVector(1026, 414, 1332)),
			FBox(FVector(-414, -1026, 0), FVector(414, 1026, 1332)),
			FBox(FVector(-450, -1062, 0), FVector(450, 1062, 1332)),
			FBox(FVector(-522, 774, -252), FVector(-162, 1134, 468)),
			3783807544959526326ull, 13499840356386341553ull, 0ull,
			6056576068412876568ull, 5980623437000438947ull,
			4766851746474182140ull),
		MakeFrozen(EABTSM73BeamDemoBuilding::E4TipOver, 2,
			EABTSM7BuildingMaterial::Stone, 3, 730000, 904, 1, 0,
			Histogram(0, 904, 0, 0, 0),
			FBox(FVector(-846, -378, 0), FVector(846, 378, 2376)),
			FBox(FVector(-378, -846, 0), FVector(378, 846, 2376)),
			FBox(FVector(-414, -882, 0), FVector(414, 882, 2376)),
			FBox(FVector(-486, -342, -144), FVector(-126, 378, 216)),
			8626866139811673118ull, 4267868371875890409ull, 0ull,
			12346635070808564758ull, 10546537168470496360ull,
			4414623922721955589ull),
		MakeFrozen(EABTSM73BeamDemoBuilding::E5SeamRelease, 3,
			EABTSM7BuildingMaterial::Iron, 4, 720000, 1903, 1, 0,
			Histogram(0, 0, 1903, 0, 0),
			FBox(FVector(-1350, -630, 0), FVector(1350, 630, 2376)),
			FBox(FVector(-630, -1350, 0), FVector(630, 1350, 2376)),
			FBox(FVector(-666, -1386, 0), FVector(666, 1386, 2376)),
			FBox(FVector(126, 846, -144), FVector(486, 1566, 216)),
			6515755032372742292ull, 15204117308279581184ull, 0ull,
			17932683668713717862ull, 11772527566289753088ull,
			543918785024958331ull),
		MakeFrozen(EABTSM73BeamDemoBuilding::E1ColumnBreak, 4,
			EABTSM7BuildingMaterial::Stone, 0, 710000, 54, 1, 1,
			Histogram(0, 54, 0, 0, 1),
			FBox(FVector(-414, -162, 0), FVector(-90, 162, 756)),
			FBox(FVector(-162, 90, 0), FVector(162, 414, 756)),
			FBox(FVector(-198, 54, 0), FVector(198, 450, 756)),
			FBox(FVector(-850, -418, -670), FVector(670, 1102, 850)),
			8855396142165301146ull, 1306678247463021210ull,
			2301603703857336297ull, 5109319969545358893ull,
			7092558964138954002ull, 12209885623584966783ull),
		MakeFrozen(EABTSM73BeamDemoBuilding::E6TipOver, 5,
			EABTSM7BuildingMaterial::Iron, 5, 750000, 2235, 1, 0,
			Histogram(0, 0, 2235, 0, 0),
			FBox(FVector(-1062, -486, 0), FVector(1062, 486, 3384)),
			FBox(FVector(-486, -1062, 0), FVector(486, 1062, 3384)),
			FBox(FVector(-522, -1098, 0), FVector(522, 1098, 3384)),
			FBox(FVector(-594, 558, -144), FVector(-234, 1278, 216)),
			2348159192872953385ull, 198894657042108135ull, 0ull,
			11440919070458269246ull, 11323455661476895076ull,
			3187373410644525608ull)};
	return Frozen;
}

int32 FABTSM73BuildingFreezeV3MaterialHistogram::Count(
	const EABTSM7BuildingMaterial Material) const
{
	switch (Material)
	{
	case EABTSM7BuildingMaterial::Wood: return Wood;
	case EABTSM7BuildingMaterial::Stone: return Stone;
	case EABTSM7BuildingMaterial::Iron: return Iron;
	case EABTSM7BuildingMaterial::Glass: return Glass;
	case EABTSM7BuildingMaterial::Crystal: return Crystal;
	default: return 0;
	}
}

bool FABTSM73BuildingFreezeV3::ResolvePrimaryMaterial(
	const EABTSM73BeamDemoBuilding Id,
	EABTSM7BuildingMaterial& OutMaterial,
	FString& OutError)
{
	OutError.Reset();
	switch (Id)
	{
	case EABTSM73BeamDemoBuilding::E1ColumnBreak:
	case EABTSM73BeamDemoBuilding::E4TipOver:
		OutMaterial = EABTSM7BuildingMaterial::Stone; return true;
	case EABTSM73BeamDemoBuilding::E2DropTrigger:
	case EABTSM73BeamDemoBuilding::E3SlideRelease:
		OutMaterial = EABTSM7BuildingMaterial::Wood; return true;
	case EABTSM73BeamDemoBuilding::E5SeamRelease:
	case EABTSM73BeamDemoBuilding::E6TipOver:
		OutMaterial = EABTSM7BuildingMaterial::Iron; return true;
	default:
		OutError = TEXT("BuildingFreezeV3UnknownPrimaryMaterial");
		return false;
	}
}

bool FABTSM73BuildingFreezeV3::ResolveEncounterSlot(
	const EABTSM73BeamDemoBuilding Id,
	int32& OutEncounterSlot,
	FString& OutError)
{
	OutError.Reset();
	switch (Id)
	{
	case EABTSM73BeamDemoBuilding::E2DropTrigger: OutEncounterSlot = 0; return true;
	case EABTSM73BeamDemoBuilding::E3SlideRelease: OutEncounterSlot = 1; return true;
	case EABTSM73BeamDemoBuilding::E4TipOver: OutEncounterSlot = 2; return true;
	case EABTSM73BeamDemoBuilding::E5SeamRelease: OutEncounterSlot = 3; return true;
	case EABTSM73BeamDemoBuilding::E1ColumnBreak: OutEncounterSlot = 4; return true;
	case EABTSM73BeamDemoBuilding::E6TipOver: OutEncounterSlot = 5; return true;
	default:
		OutEncounterSlot = INDEX_NONE;
		OutError = TEXT("BuildingFreezeV3UnknownEncounterSlot");
		return false;
	}
}

bool FABTSM73BuildingFreezeV3::DeriveAndValidate(
	const EABTSM73BeamDemoBuilding Id,
	FABTSM73BuildingFreezeV3Descriptor& OutDescriptor,
	FString& OutError)
{
	using namespace ABTSM73BuildingFreezeV3Private;
	OutDescriptor = FABTSM73BuildingFreezeV3Descriptor();
	OutError.Reset();

	FABTSM73BeamDemoManifestEntry Entry;
	if (!FABTSM73BeamDemoManifest::Resolve(Id, Entry, OutError)
		|| !ResolvePrimaryMaterial(Id, OutDescriptor.PrimaryMaterial, OutError)
		|| !ResolveEncounterSlot(Id, OutDescriptor.EncounterSlot, OutError))
	{
		return false;
	}

	FABTSM73BeamD1MaterialPolicy MaterialPolicy;
	MaterialPolicy.bOverrideOrdinaryBody = true;
	MaterialPolicy.PrimaryMaterial = OutDescriptor.PrimaryMaterial;
	FABTSM73BeamD1Stage55Result Source;
	FABTSM73BeamD1BrickCompiler Compiler;
	if (!Compiler.GenerateStage55DeviceAssemblyWithMaterialPolicy(
		Entry.Settings, MaterialPolicy, Source, OutError))
	{
		OutError = FString::Printf(TEXT("BuildingFreezeV3Source:%s:%s"),
			*Entry.StableId.ToString(), *OutError);
		return false;
	}

	OutDescriptor.SchemaVersion = SchemaVersion;
	OutDescriptor.SourceManifestVersion = FABTSM73BeamDemoManifest::Version;
	OutDescriptor.SourceManifestHash = FABTSM73BeamDemoManifest::CalculateHash();
	OutDescriptor.ManifestEntryId = Entry.Id;
	OutDescriptor.StableId = Entry.StableId;
	OutDescriptor.GameplayProfileId = Entry.Settings.GameplayProfileId;
	OutDescriptor.DifficultyTier = Entry.Settings.DifficultyTier;
	OutDescriptor.BuildingSeed = Entry.Settings.BuildingSeed;
	OutDescriptor.SourceStage5ProductionHash = Source.Stage5.ProductionIdentityHash;
	OutDescriptor.SourceDeviceAssemblyHash = Source.DeviceAssemblyHash;
	OutDescriptor.ContentToSite = FTransform(
		FQuat(FVector::UpVector, -HALF_PI), FVector::ZeroVector);
	const FVector MappedFront = OutDescriptor.ContentToSite.TransformVectorNoScale(
		FVector::RightVector).GetSafeNormal();
	if (!MappedFront.Equals(FVector::ForwardVector, KINDA_SMALL_NUMBER))
	{
		OutError = TEXT("BuildingFreezeV3ContentFrontMapping");
		return false;
	}

	for (const FABTSM73BeamD1BrickBinding& SourceBrick : Source.Stage5.Bricks)
	{
		if (SourceBrick.StructuralRole != EABTSM73BeamD1StructuralRole::Connector
			&& !SourceBrick.bWeaknessCandidate
			&& SourceBrick.DeviceRole == EABTSM73BeamD1DeviceRole::None
			&& SourceBrick.BrickSpec.Material != OutDescriptor.PrimaryMaterial)
		{
			OutError = FString::Printf(
				TEXT("BuildingFreezeV3PrimaryMaterialMismatch:%s:Brick=%d"),
				*Entry.StableId.ToString(), SourceBrick.BrickId);
			return false;
		}
		OutDescriptor.GeneratorLocalBounds += SourceBrick.LocalBounds;
		FABTSM73BeamD1BrickBinding& Brick = OutDescriptor.Bricks.Add_GetRef(SourceBrick);
		Brick.LocalTransform = SourceBrick.LocalTransform * OutDescriptor.ContentToSite;
		Brick.LocalBounds = TransformBox(SourceBrick.LocalBounds, OutDescriptor.ContentToSite);
		Brick.Axis = RotateAxis(SourceBrick.Axis);
		OutDescriptor.SiteLocalBounds += Brick.LocalBounds;
		AddMaterial(OutDescriptor.MaterialHistogram, Brick.BrickSpec.Material);
	}

	for (const FABTSM73BeamD1DeviceBinding& SourceDevice : Source.Devices)
	{
		OutDescriptor.GeneratorLocalBounds += SourceDevice.LocalBounds;
		FABTSM73BeamD1DeviceBinding& Device = OutDescriptor.Devices.Add_GetRef(SourceDevice);
		Device.LocalTransform = SourceDevice.LocalTransform * OutDescriptor.ContentToSite;
		Device.LocalBounds = TransformBox(SourceDevice.LocalBounds, OutDescriptor.ContentToSite);
		Device.EffectCorridorLocalBounds = TransformBox(
			SourceDevice.EffectCorridorLocalBounds, OutDescriptor.ContentToSite);
		Device.Axis = RotateAxis(SourceDevice.Axis);
		OutDescriptor.SiteLocalBounds += Device.LocalBounds;
		OutDescriptor.EffectBounds += Device.EffectCorridorLocalBounds;
	}

	if (Id == EABTSM73BeamDemoBuilding::E1ColumnBreak)
	{
		if (Source.Stage5.CrystalSeatMemberIds.Num() != 2)
		{
			OutError = TEXT("BuildingFreezeV3CrystalSeatPairMissing");
			return false;
		}
		const FVector Half(CrystalCapExtentCM * 0.5);
		const FVector GeneratorCenter(
			Source.Stage5.CrystalSeatCenterLocal.X,
			Source.Stage5.CrystalSeatCenterLocal.Y,
			Source.Stage5.CrystalSeatCenterLocal.Z + Half.Z);
		const FBox GeneratorCapBounds(GeneratorCenter - Half, GeneratorCenter + Half);
		TArray<int32> ContactingSeatIds;
		double TotalSeatContactAreaCM2 = 0.0;
		for (const FABTSM73BeamD1BrickBinding& Brick : Source.Stage5.Bricks)
		{
			if (HasPositiveVolumeOverlap(GeneratorCapBounds, Brick.LocalBounds))
			{
				OutError = TEXT("BuildingFreezeV3CrystalCapPenetration");
				return false;
			}
			const double OverlapX = FMath::Min(GeneratorCapBounds.Max.X,
				Brick.LocalBounds.Max.X) - FMath::Max(GeneratorCapBounds.Min.X,
					Brick.LocalBounds.Min.X);
			const double OverlapY = FMath::Min(GeneratorCapBounds.Max.Y,
				Brick.LocalBounds.Max.Y) - FMath::Max(GeneratorCapBounds.Min.Y,
					Brick.LocalBounds.Min.Y);
			if (FMath::IsNearlyEqual(GeneratorCapBounds.Min.Z,
				Brick.LocalBounds.Max.Z, GeometryToleranceCM)
				&& OverlapX > GeometryToleranceCM && OverlapY > GeometryToleranceCM)
			{
				if (!Source.Stage5.CrystalSeatMemberIds.Contains(Brick.MemberId))
				{
					OutError = TEXT("BuildingFreezeV3CrystalCapTouchesNonSeat");
					return false;
				}
				ContactingSeatIds.AddUnique(Brick.MemberId);
				const double ContactArea = OverlapX * OverlapY;
				if (!FMath::IsNearlyEqual(ContactArea,
					CrystalCapExtentCM * CrystalCapExtentCM * 0.5, 0.1))
				{
					OutError = TEXT("BuildingFreezeV3CrystalCapUnequalSeatShare");
					return false;
				}
				TotalSeatContactAreaCM2 += ContactArea;
			}
		}
		ContactingSeatIds.Sort();
		TArray<int32> ExpectedSeatIds = Source.Stage5.CrystalSeatMemberIds;
		ExpectedSeatIds.Sort();
		if (ContactingSeatIds != ExpectedSeatIds
			|| !FMath::IsNearlyEqual(TotalSeatContactAreaCM2,
				CrystalCapExtentCM * CrystalCapExtentCM, 0.1))
		{
			OutError = TEXT("BuildingFreezeV3CrystalCapSeatCoverage");
			return false;
		}
		FABTSM73BuildingFreezeV3CapBinding& Cap = OutDescriptor.Caps.AddDefaulted_GetRef();
		Cap.BrickSpec.Material = EABTSM7BuildingMaterial::Crystal;
		Cap.BrickSpec.DimensionsCM = FVector(CrystalCapExtentCM);
		Cap.SiteLocalTransform = FTransform(FQuat::Identity, GeneratorCenter)
			* OutDescriptor.ContentToSite;
		Cap.SiteLocalBounds = TransformBox(GeneratorCapBounds, OutDescriptor.ContentToSite);
		Cap.SupportingMemberIds = ExpectedSeatIds;
		OutDescriptor.GeneratorLocalBounds += GeneratorCapBounds;
		OutDescriptor.SiteLocalBounds += Cap.SiteLocalBounds;
		AddMaterial(OutDescriptor.MaterialHistogram, Cap.BrickSpec.Material);

		if (Source.Devices.Num() != 1
			|| Source.Devices[0].SupportContactCellCount <= 0)
		{
			OutError = TEXT("BuildingFreezeV3E1DeviceStaticSupportMissing");
			return false;
		}
		TArray<FABTSM73BeamD1ExternalLoad> ExternalLoads;
		FABTSM73BeamD1ExternalLoad& DeviceLoad = ExternalLoads.AddDefaulted_GetRef();
		DeviceLoad.StableId = TEXT("E1Device");
		DeviceLoad.StaticMassKG = Source.Devices[0].StaticMassKG;
		DeviceLoad.bDirectGroundSupport = Source.Devices[0].bDirectGroundSupport;
		if (DeviceLoad.bDirectGroundSupport)
		{
			if (!Source.Devices[0].SupportMemberIds.IsEmpty())
			{
				OutError = TEXT("BuildingFreezeV3E1GroundDeviceHasMemberSupports");
				return false;
			}
		}
		else
		{
			if (Source.Devices[0].SupportMemberIds.IsEmpty())
			{
				OutError = TEXT("BuildingFreezeV3E1DeviceWithoutSupportMembers");
				return false;
			}
			const double Fraction = 1.0 / Source.Devices[0].SupportMemberIds.Num();
			for (const int32 MemberId : Source.Devices[0].SupportMemberIds)
			{
				DeviceLoad.SupportShares.Add({MemberId, Fraction});
			}
		}

		const TArray<FABTSM7MaterialProfile> MaterialProfiles =
			FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
		const FABTSM7MaterialProfile* CrystalMaterial =
			FABTSM7MaterialProfileLibrary::FindProfile(
				MaterialProfiles, EABTSM7BuildingMaterial::Crystal);
		if (CrystalMaterial == nullptr)
		{
			OutError = TEXT("BuildingFreezeV3CrystalMaterialProfileMissing");
			return false;
		}
		FABTSM73BeamD1ExternalLoad& CrystalLoad = ExternalLoads.AddDefaulted_GetRef();
		CrystalLoad.StableId = TEXT("E1Crystal72");
		CrystalLoad.StaticMassKG = FMath::Pow(CrystalCapExtentCM, 3.0)
			* CrystalMaterial->DensityGPerCubicCM * 0.001;
		CrystalLoad.SupportShares.Add({ExpectedSeatIds[0], 0.5});
		CrystalLoad.SupportShares.Add({ExpectedSeatIds[1], 0.5});

		FABTSM73BeamD1StaticLoadCertificate Certificate;
		if (!Compiler.CertifyStage5StaticExternalLoads(Entry.Settings,
			MaterialPolicy, Source.Stage5, ExternalLoads, Certificate, OutError))
		{
			OutError = FString::Printf(TEXT("BuildingFreezeV3E1StaticExternalLoad:%s"),
				*OutError);
			return false;
		}
		Cap.bStaticExternalLoadCertified = true;
		OutDescriptor.bStaticExternalLoadCertified = Certificate.bAccepted;
		OutDescriptor.StaticExternalLoadCount = Certificate.ExternalLoadCount;
		OutDescriptor.StaticExternalMassKG = Certificate.ExternalMassKG;
		OutDescriptor.StaticDirectGroundMassKG = Certificate.DirectGroundMassKG;
		OutDescriptor.StaticSupportResultantAdvisoryCount =
			Certificate.LoadDAG.Summary.SupportResultantAdvisoryCount;
		OutDescriptor.StaticExternalLoadLedgerHash =
			Certificate.ExternalLoadLedgerHash;
		OutDescriptor.StaticExternalLoadDAGHash = static_cast<uint64>(
			Certificate.LoadDAG.Summary.LoadDAGHash);
		OutDescriptor.StaticExternalLoadCertificateHash = Certificate.CertificateHash;
	}

	if (!OutDescriptor.GeneratorLocalBounds.IsValid
		|| !OutDescriptor.SiteLocalBounds.IsValid
		|| OutDescriptor.MaterialHistogram.Total()
			!= OutDescriptor.Bricks.Num() + OutDescriptor.Caps.Num()
		|| (Id == EABTSM73BeamDemoBuilding::E1ColumnBreak
			? OutDescriptor.Caps.Num() != 1
				|| OutDescriptor.MaterialHistogram.Crystal != 1
				|| !OutDescriptor.bStaticExternalLoadCertified
				|| OutDescriptor.StaticExternalLoadCount != 2
				|| OutDescriptor.StaticSupportResultantAdvisoryCount != 0
			: !OutDescriptor.Caps.IsEmpty() || OutDescriptor.MaterialHistogram.Crystal != 0))
	{
		OutError = TEXT("BuildingFreezeV3CapOrHistogramInvariant");
		return false;
	}

	OutDescriptor.SiteLocalOBB.Center =
		OutDescriptor.ContentToSite.TransformPosition(
			OutDescriptor.GeneratorLocalBounds.GetCenter());
	OutDescriptor.SiteLocalOBB.HalfExtent =
		OutDescriptor.GeneratorLocalBounds.GetExtent();
	OutDescriptor.SiteLocalOBB.ContentXAxisInSite =
		OutDescriptor.ContentToSite.TransformVectorNoScale(FVector::ForwardVector);
	OutDescriptor.SiteLocalOBB.ContentYAxisInSite = MappedFront;
	OutDescriptor.SiteLocalOBB.ContentZAxisInSite = FVector::UpVector;
	OutDescriptor.PadBounds = FBox(
		OutDescriptor.SiteLocalBounds.Min
			- FVector(PadSafetyMarginCM, PadSafetyMarginCM, 0.0),
		OutDescriptor.SiteLocalBounds.Max
			+ FVector(PadSafetyMarginCM, PadSafetyMarginCM, 0.0));

	FString GeometryCanonical;
	for (const FABTSM73BeamD1BrickBinding& Brick : OutDescriptor.Bricks)
	{
		GeometryCanonical += TEXT("|") + BrickRow(Brick);
	}
	for (const FABTSM73BeamD1DeviceBinding& Device : OutDescriptor.Devices)
	{
		GeometryCanonical += TEXT("|") + DeviceRow(Device);
	}
	for (const FABTSM73BuildingFreezeV3CapBinding& Cap : OutDescriptor.Caps)
	{
		GeometryCanonical += TEXT("|") + CapRow(Cap);
	}
	OutDescriptor.StaticGeometryHash = HashUtf8(GeometryCanonical);
	if (OutDescriptor.StaticExternalLoadCertificateHash != 0)
	{
		OutDescriptor.ProductionHash = HashUtf8(FString::Printf(
			TEXT("Stage5=%llu|Device=%llu|StaticLoad=%llu|Geometry=%llu|Primary=%d|Encounter=%d|Front=YtoX"),
			OutDescriptor.SourceStage5ProductionHash,
			OutDescriptor.SourceDeviceAssemblyHash,
			OutDescriptor.StaticExternalLoadCertificateHash,
			OutDescriptor.StaticGeometryHash,
			static_cast<int32>(OutDescriptor.PrimaryMaterial),
			OutDescriptor.EncounterSlot));
	}
	else
	{
		OutDescriptor.ProductionHash = HashUtf8(FString::Printf(
			TEXT("Stage5=%llu|Device=%llu|Geometry=%llu|Primary=%d|Encounter=%d|Front=YtoX"),
			OutDescriptor.SourceStage5ProductionHash,
			OutDescriptor.SourceDeviceAssemblyHash,
			OutDescriptor.StaticGeometryHash,
			static_cast<int32>(OutDescriptor.PrimaryMaterial),
			OutDescriptor.EncounterSlot));
	}
	FString DescriptorCanonical = FString::Printf(
		TEXT("Schema=%d|Manifest=%lld|Id=%d|Stable=%s|Profile=%s|Tier=%d|Seed=%d|Encounter=%d|Material=%d|Generator=%s|Site=%s|Pad=%s|Effect=%s|Histogram=%d,%d,%d,%d,%d"),
		OutDescriptor.SchemaVersion, OutDescriptor.SourceManifestHash,
		static_cast<int32>(OutDescriptor.ManifestEntryId),
		*OutDescriptor.StableId.ToString(), *OutDescriptor.GameplayProfileId.ToString(),
		OutDescriptor.DifficultyTier, OutDescriptor.BuildingSeed,
		OutDescriptor.EncounterSlot, static_cast<int32>(OutDescriptor.PrimaryMaterial),
		*BoxRow(OutDescriptor.GeneratorLocalBounds),
		*BoxRow(OutDescriptor.SiteLocalBounds), *BoxRow(OutDescriptor.PadBounds),
		*BoxRow(OutDescriptor.EffectBounds), OutDescriptor.MaterialHistogram.Wood,
		OutDescriptor.MaterialHistogram.Stone, OutDescriptor.MaterialHistogram.Iron,
		OutDescriptor.MaterialHistogram.Glass, OutDescriptor.MaterialHistogram.Crystal);
	if (OutDescriptor.StaticExternalLoadCertificateHash != 0)
	{
		DescriptorCanonical += FString::Printf(
			TEXT("|StaticCertified=%d|StaticLoads=%d|StaticMass=%.6f|StaticGroundMass=%.6f|StaticAdvisory=%d|StaticLedger=%llu|StaticDAG=%llu|StaticCertificate=%llu"),
			OutDescriptor.bStaticExternalLoadCertified ? 1 : 0,
			OutDescriptor.StaticExternalLoadCount,
			OutDescriptor.StaticExternalMassKG,
			OutDescriptor.StaticDirectGroundMassKG,
			OutDescriptor.StaticSupportResultantAdvisoryCount,
			OutDescriptor.StaticExternalLoadLedgerHash,
			OutDescriptor.StaticExternalLoadDAGHash,
			OutDescriptor.StaticExternalLoadCertificateHash);
	}
	DescriptorCanonical += FString::Printf(TEXT("|Production=%llu"),
		OutDescriptor.ProductionHash);
	OutDescriptor.DescriptorHash = HashUtf8(DescriptorCanonical);
	return true;
}

bool FABTSM73BuildingFreezeV3::DeriveAndValidateCatalog(
	TArray<FABTSM73BuildingFreezeV3Descriptor>& OutDescriptors,
	uint64& OutCatalogHash,
	FString& OutError)
{
	using namespace ABTSM73BuildingFreezeV3Private;
	OutDescriptors.Reset();
	OutCatalogHash = 0;
	OutError.Reset();
	static const EABTSM73BeamDemoBuilding EncounterOrder[] = {
		EABTSM73BeamDemoBuilding::E2DropTrigger,
		EABTSM73BeamDemoBuilding::E3SlideRelease,
		EABTSM73BeamDemoBuilding::E4TipOver,
		EABTSM73BeamDemoBuilding::E5SeamRelease,
		EABTSM73BeamDemoBuilding::E1ColumnBreak,
		EABTSM73BeamDemoBuilding::E6TipOver};
	FString Canonical = FString::Printf(TEXT("BuildingFreezeV3Catalog=%d"), SchemaVersion);
	int32 CrystalCount = 0;
	for (int32 EncounterSlot = 0; EncounterSlot < UE_ARRAY_COUNT(EncounterOrder); ++EncounterSlot)
	{
		FABTSM73BuildingFreezeV3Descriptor& Descriptor = OutDescriptors.AddDefaulted_GetRef();
		if (!DeriveAndValidate(EncounterOrder[EncounterSlot], Descriptor, OutError)
			|| Descriptor.EncounterSlot != EncounterSlot)
		{
			if (OutError.IsEmpty()) OutError = TEXT("BuildingFreezeV3EncounterOrderMismatch");
			OutDescriptors.Reset();
			return false;
		}
		CrystalCount += Descriptor.MaterialHistogram.Crystal;
		Canonical += FString::Printf(TEXT("|%d:%llu:%llu"), EncounterSlot,
			Descriptor.DescriptorHash, Descriptor.ProductionHash);
	}
	if (OutDescriptors.Num() != ExpectedEntryCount || CrystalCount != 1)
	{
		OutError = TEXT("BuildingFreezeV3CatalogCardinality");
		OutDescriptors.Reset();
		return false;
	}
	OutCatalogHash = HashUtf8(Canonical);
	const TArray<FABTSM73BuildingFreezeV3FrozenIdentity>& Frozen =
		GetFrozenIdentities();
	if (Frozen.Num() != OutDescriptors.Num()
		|| OutCatalogHash != FrozenCatalogHash
		|| OutDescriptors[0].SourceManifestVersion != FrozenSourceManifestVersion
		|| OutDescriptors[0].SourceManifestHash != FrozenSourceManifestHash)
	{
		OutError = TEXT("BuildingFreezeV3FrozenCatalogIdentityDrift");
		OutDescriptors.Reset();
		return false;
	}
	for (int32 Index = 0; Index < OutDescriptors.Num(); ++Index)
	{
		const FABTSM73BuildingFreezeV3Descriptor& Actual = OutDescriptors[Index];
		const FABTSM73BuildingFreezeV3FrozenIdentity& Expected = Frozen[Index];
		const bool bMatches = Actual.ManifestEntryId == Expected.ManifestEntryId
			&& Actual.EncounterSlot == Expected.EncounterSlot
			&& Actual.PrimaryMaterial == Expected.PrimaryMaterial
			&& Actual.DifficultyTier == Expected.DifficultyTier
			&& Actual.BuildingSeed == Expected.BuildingSeed
			&& Actual.Bricks.Num() == Expected.BrickCount
			&& Actual.Devices.Num() == Expected.DeviceCount
			&& Actual.Caps.Num() == Expected.CapCount
			&& Actual.MaterialHistogram.Wood == Expected.MaterialHistogram.Wood
			&& Actual.MaterialHistogram.Stone == Expected.MaterialHistogram.Stone
			&& Actual.MaterialHistogram.Iron == Expected.MaterialHistogram.Iron
			&& Actual.MaterialHistogram.Glass == Expected.MaterialHistogram.Glass
			&& Actual.MaterialHistogram.Crystal == Expected.MaterialHistogram.Crystal
			&& Actual.GeneratorLocalBounds.Equals(Expected.GeneratorLocalBounds, 0.01)
			&& Actual.SiteLocalBounds.Equals(Expected.SiteLocalBounds, 0.01)
			&& Actual.PadBounds.Equals(Expected.PadBounds, 0.01)
			&& Actual.EffectBounds.Equals(Expected.EffectBounds, 0.01)
			&& Actual.SourceStage5ProductionHash == Expected.SourceStage5ProductionHash
			&& Actual.SourceDeviceAssemblyHash == Expected.SourceDeviceAssemblyHash
			&& Actual.StaticExternalLoadCertificateHash
				== Expected.StaticExternalLoadCertificateHash
			&& Actual.StaticGeometryHash == Expected.StaticGeometryHash
			&& Actual.ProductionHash == Expected.ProductionHash
			&& Actual.DescriptorHash == Expected.DescriptorHash;
		if (!bMatches)
		{
			OutError = FString::Printf(
				TEXT("BuildingFreezeV3FrozenEntryDrift:Slot=%d:Actual=%llu:Expected=%llu"),
				Index, Actual.DescriptorHash, Expected.DescriptorHash);
			OutDescriptors.Reset();
			return false;
		}
	}
	return true;
}
