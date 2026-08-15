// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Building/ABTSM73BuildingFreezeV3.h"

#include "Misc/AutomationTest.h"

namespace ABTSM73BuildingFreezeV3Tests
{
	bool HasPositiveVolumeOverlap(const FBox& A, const FBox& B)
	{
		return FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X) > 0.01
			&& FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y) > 0.01
			&& FMath::Min(A.Max.Z, B.Max.Z) - FMath::Max(A.Min.Z, B.Min.Z) > 0.01;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BuildingFreezeV3Test,
	"ABTS.M73DAG.BuildingFreezeV3.FixedSix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BuildingFreezeV3Test::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BuildingFreezeV3Tests;
	TArray<FABTSM73BuildingFreezeV3Descriptor> Descriptors;
	uint64 CatalogHash = 0;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("V3 catalog derives: %s"), *Error),
		FABTSM73BuildingFreezeV3::DeriveAndValidateCatalog(
			Descriptors, CatalogHash, Error)))
	{
		return false;
	}
	TestEqual(TEXT("V3 publishes exactly six encounters"),
		Descriptors.Num(), FABTSM73BuildingFreezeV3::ExpectedEntryCount);
	TestEqual(TEXT("V3 derived catalog matches the committed freeze"),
		CatalogHash, FABTSM73BuildingFreezeV3::FrozenCatalogHash);
	TestEqual(TEXT("V3 exposes six compact frozen identities"),
		FABTSM73BuildingFreezeV3::GetFrozenIdentities().Num(),
		FABTSM73BuildingFreezeV3::ExpectedEntryCount);
	static const EABTSM73BeamDemoBuilding ExpectedOrder[] = {
		EABTSM73BeamDemoBuilding::E2DropTrigger,
		EABTSM73BeamDemoBuilding::E3SlideRelease,
		EABTSM73BeamDemoBuilding::E4TipOver,
		EABTSM73BeamDemoBuilding::E5SeamRelease,
		EABTSM73BeamDemoBuilding::E1ColumnBreak,
		EABTSM73BeamDemoBuilding::E6TipOver};
	int32 GlobalCrystalCount = 0;
	for (int32 Index = 0; Index < Descriptors.Num(); ++Index)
	{
		const FABTSM73BuildingFreezeV3Descriptor& Descriptor = Descriptors[Index];
		const FString Prefix = FString::Printf(TEXT("V3 encounter %d %s"),
			Index, *Descriptor.StableId.ToString());
		TestEqual(*(Prefix + TEXT(" order")),
			static_cast<int32>(Descriptor.ManifestEntryId),
			static_cast<int32>(ExpectedOrder[Index]));
		TestEqual(*(Prefix + TEXT(" slot")), Descriptor.EncounterSlot, Index);
		TestTrue(*(Prefix + TEXT(" content +Y maps to site +X")),
			Descriptor.ContentToSite.TransformVectorNoScale(FVector::RightVector)
				.Equals(FVector::ForwardVector, KINDA_SMALL_NUMBER));
		TestTrue(*(Prefix + TEXT(" OBB keeps the mapped front")),
			Descriptor.SiteLocalOBB.ContentYAxisInSite.Equals(
				FVector::ForwardVector, KINDA_SMALL_NUMBER));
		TestTrue(*(Prefix + TEXT(" site AABB swaps content X/Y extent")),
			FMath::IsNearlyEqual(Descriptor.SiteLocalBounds.GetSize().X,
				Descriptor.GeneratorLocalBounds.GetSize().Y, 0.01)
			&& FMath::IsNearlyEqual(Descriptor.SiteLocalBounds.GetSize().Y,
				Descriptor.GeneratorLocalBounds.GetSize().X, 0.01));
		TestTrue(*(Prefix + TEXT(" site and pad bounds are valid")),
			Descriptor.SiteLocalBounds.IsValid && Descriptor.PadBounds.IsValid
				&& Descriptor.PadBounds.IsInsideOrOn(Descriptor.SiteLocalBounds.Min)
				&& Descriptor.PadBounds.IsInsideOrOn(Descriptor.SiteLocalBounds.Max));
		TestTrue(*(Prefix + TEXT(" effect bounds are valid")),
			Descriptor.EffectBounds.IsValid != 0);
		TestEqual(*(Prefix + TEXT(" histogram covers all bricks")),
			Descriptor.MaterialHistogram.Total(),
			Descriptor.Bricks.Num() + Descriptor.Caps.Num());

		for (const FABTSM73BeamD1BrickBinding& Brick : Descriptor.Bricks)
		{
			TestTrue(*(Prefix + TEXT(" brick transform matches rotated bounds")),
				Brick.LocalTransform.GetLocation().Equals(
					Brick.LocalBounds.GetCenter(), 0.01));
			if (Brick.StructuralRole != EABTSM73BeamD1StructuralRole::Connector
				&& !Brick.bWeaknessCandidate
				&& Brick.DeviceRole == EABTSM73BeamD1DeviceRole::None)
			{
				TestEqual(*(Prefix + TEXT(" ordinary body primary material")),
					Brick.BrickSpec.Material, Descriptor.PrimaryMaterial);
			}
		}
		for (const FABTSM73BeamD1DeviceBinding& Device : Descriptor.Devices)
		{
			TestTrue(*(Prefix + TEXT(" device transform matches rotated bounds")),
				Device.LocalTransform.GetLocation().Equals(
					Device.LocalBounds.GetCenter(), 0.01));
		}

		const bool bE1 = Descriptor.ManifestEntryId
			== EABTSM73BeamDemoBuilding::E1ColumnBreak;
		TestEqual(*(Prefix + TEXT(" cap cardinality")),
			Descriptor.Caps.Num(), bE1 ? 1 : 0);
		TestEqual(*(Prefix + TEXT(" Crystal cardinality")),
			Descriptor.MaterialHistogram.Crystal, bE1 ? 1 : 0);
		TestEqual(*(Prefix + TEXT(" static external-load certification scope")),
			Descriptor.bStaticExternalLoadCertified, bE1);
		if (bE1 && !Descriptor.Caps.IsEmpty())
		{
			const FABTSM73BuildingFreezeV3CapBinding& Cap = Descriptor.Caps[0];
			TestEqual(TEXT("E1 remains Tier0 at encounter slot 4"),
				Descriptor.DifficultyTier, 0);
			TestEqual(TEXT("E1 cap is Crystal"), Cap.BrickSpec.Material,
				EABTSM7BuildingMaterial::Crystal);
			TestTrue(TEXT("E1 cap is the legal 72 cm voxel"),
				Cap.BrickSpec.DimensionsCM.Equals(FVector(72.0), KINDA_SMALL_NUMBER));
			TestFalse(TEXT("E1 cap is not load-bearing"), Cap.bLoadBearing);
			TestFalse(TEXT("E1 cap is not a weakness candidate"),
				Cap.bWeaknessCandidate);
			TestEqual(TEXT("E1 cap has no device role"), Cap.DeviceRole,
				EABTSM73BeamD1DeviceRole::None);
			TestTrue(TEXT("E1 cap transform matches rotated bounds"),
				Cap.SiteLocalTransform.GetLocation().Equals(
					Cap.SiteLocalBounds.GetCenter(), 0.01));
			TestTrue(TEXT("E1 cap is at the global top"), FMath::IsNearlyEqual(
				Cap.SiteLocalBounds.Max.Z, Descriptor.SiteLocalBounds.Max.Z, 0.01));
			TestEqual(TEXT("E1 cap names exactly two structural seats"),
				Cap.SupportingMemberIds.Num(), 2);
			TestTrue(TEXT("E1 cap external mass is statically certified"),
				Cap.bStaticExternalLoadCertified);
			TArray<int32> ContactingMemberIds;
			double TotalContactAreaCM2 = 0.0;
			for (const FABTSM73BeamD1BrickBinding& Brick : Descriptor.Bricks)
			{
				const double OverlapX = FMath::Min(Cap.SiteLocalBounds.Max.X,
					Brick.LocalBounds.Max.X) - FMath::Max(Cap.SiteLocalBounds.Min.X,
						Brick.LocalBounds.Min.X);
				const double OverlapY = FMath::Min(Cap.SiteLocalBounds.Max.Y,
					Brick.LocalBounds.Max.Y) - FMath::Max(Cap.SiteLocalBounds.Min.Y,
						Brick.LocalBounds.Min.Y);
				if (FMath::IsNearlyEqual(Cap.SiteLocalBounds.Min.Z,
					Brick.LocalBounds.Max.Z, 0.01)
					&& OverlapX > 0.01 && OverlapY > 0.01)
				{
					ContactingMemberIds.Add(Brick.MemberId);
					const double Area = OverlapX * OverlapY;
					TestTrue(TEXT("Each E1 seat carries half the Crystal base"),
						FMath::IsNearlyEqual(Area, 72.0 * 36.0, 0.1));
					TotalContactAreaCM2 += Area;
				}
			}
			ContactingMemberIds.Sort();
			TArray<int32> ExpectedSeatIds = Cap.SupportingMemberIds;
			ExpectedSeatIds.Sort();
			TestTrue(TEXT("E1 cap contacts only the two frozen seat beams"),
				ContactingMemberIds == ExpectedSeatIds);
			TestTrue(TEXT("E1 paired seats cover the full Crystal base"),
				FMath::IsNearlyEqual(TotalContactAreaCM2, 72.0 * 72.0, 0.1));
			TestEqual(TEXT("E1 static ledger contains Crystal and device"),
				Descriptor.StaticExternalLoadCount, 2);
			TestTrue(TEXT("E1 Crystal plus device mass is certified"),
				FMath::IsNearlyEqual(Descriptor.StaticExternalMassKG, 1023.12, 0.001));
			TestTrue(TEXT("E1 direct-ground device mass is certified"),
				FMath::IsNearlyEqual(Descriptor.StaticDirectGroundMassKG, 90.0, 0.001));
			TestEqual(TEXT("E1 static external-load advisory count is zero"),
				Descriptor.StaticSupportResultantAdvisoryCount, 0);
		}
		GlobalCrystalCount += Descriptor.MaterialHistogram.Crystal;

		TArray<FBox> CollisionBoxes;
		for (const FABTSM73BeamD1BrickBinding& Brick : Descriptor.Bricks)
		{
			CollisionBoxes.Add(Brick.LocalBounds);
		}
		for (const FABTSM73BeamD1DeviceBinding& Device : Descriptor.Devices)
		{
			CollisionBoxes.Add(Device.LocalBounds);
		}
		for (const FABTSM73BuildingFreezeV3CapBinding& Cap : Descriptor.Caps)
		{
			CollisionBoxes.Add(Cap.SiteLocalBounds);
		}
		int32 PenetrationCount = 0;
		for (int32 A = 0; A < CollisionBoxes.Num(); ++A)
		{
			for (int32 B = A + 1; B < CollisionBoxes.Num(); ++B)
			{
				PenetrationCount += HasPositiveVolumeOverlap(
					CollisionBoxes[A], CollisionBoxes[B]) ? 1 : 0;
			}
		}
		TestEqual(*(Prefix + TEXT(" has no initial penetration")),
			PenetrationCount, 0);
		AddInfo(FString::Printf(
			TEXT("BuildingFreezeV3:Slot=%d:Id=%d:Stable=%s:Tier=%d:Seed=%d")
			TEXT(":Primary=%d:Bricks=%d:Devices=%d:Caps=%d:Histogram=%d,%d,%d,%d,%d")
			TEXT(":Generator=%s:Site=%s:Pad=%s:Effect=%s")
			TEXT(":Stage5=%llu:Device=%llu:StaticLedger=%llu:StaticDAG=%llu:StaticCertificate=%llu")
			TEXT(":Geometry=%llu:Production=%llu:Descriptor=%llu"),
			Descriptor.EncounterSlot, static_cast<int32>(Descriptor.ManifestEntryId),
			*Descriptor.StableId.ToString(), Descriptor.DifficultyTier,
			Descriptor.BuildingSeed, static_cast<int32>(Descriptor.PrimaryMaterial),
			Descriptor.Bricks.Num(), Descriptor.Devices.Num(), Descriptor.Caps.Num(),
			Descriptor.MaterialHistogram.Wood, Descriptor.MaterialHistogram.Stone,
			Descriptor.MaterialHistogram.Iron, Descriptor.MaterialHistogram.Glass,
			Descriptor.MaterialHistogram.Crystal,
			*Descriptor.GeneratorLocalBounds.ToString(),
			*Descriptor.SiteLocalBounds.ToString(), *Descriptor.PadBounds.ToString(),
			*Descriptor.EffectBounds.ToString(), Descriptor.SourceStage5ProductionHash,
			Descriptor.SourceDeviceAssemblyHash,
			Descriptor.StaticExternalLoadLedgerHash,
			Descriptor.StaticExternalLoadDAGHash,
			Descriptor.StaticExternalLoadCertificateHash,
			Descriptor.StaticGeometryHash,
			Descriptor.ProductionHash, Descriptor.DescriptorHash));
	}
	TestEqual(TEXT("The fixed-six catalog contains exactly one Crystal"),
		GlobalCrystalCount, 1);
	AddInfo(FString::Printf(TEXT("BuildingFreezeV3Catalog:Hash=%llu:Count=%d"),
		CatalogHash, Descriptors.Num()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BuildingFreezeV3E6CompoundV1CandidateTest,
	"ABTS.M73DAG.BuildingFreezeV3.E6CompoundV1Candidate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BuildingFreezeV3E6CompoundV1CandidateTest::RunTest(
	const FString& Parameters)
{
	FABTSM73BuildingFreezeV3Descriptor Frozen;
	FABTSM73BuildingFreezeV3Descriptor Candidate;
	TArray<FABTSM73BuildingFreezeV3Descriptor> PublishedCatalog;
	TArray<FABTSM73BuildingFreezeV3Descriptor> CandidateCatalog;
	uint64 PublishedCatalogHash = 0;
	uint64 CandidateCatalogHash = 0;
	FString Error;
	if (!TestTrue(TEXT("Published E6 remains derivable"),
		FABTSM73BuildingFreezeV3::DeriveAndValidate(
			EABTSM73BeamDemoBuilding::E6TipOver, Frozen, Error))
		|| !TestTrue(TEXT("E6 compound V1 atomic candidate derives"),
			FABTSM73BuildingFreezeV3::DeriveAndValidate(
				EABTSM73BeamDemoBuilding::E6TipOver, Candidate, Error, true)))
	{
		AddError(Error);
		return false;
	}
	if (!TestTrue(TEXT("Published fixed-six catalog derives"),
		FABTSM73BuildingFreezeV3::DeriveAndValidateCatalog(
			PublishedCatalog, PublishedCatalogHash, Error)))
	{
		AddError(Error);
		return false;
	}
	if (!TestTrue(TEXT("E6 compound V1 atomic candidate catalog derives"),
		FABTSM73BuildingFreezeV3::
			DeriveAndValidateE6CompoundV1CandidateCatalog(
				CandidateCatalog, CandidateCatalogHash, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("Candidate catalog contains the fixed six"),
		CandidateCatalog.Num(), FABTSM73BuildingFreezeV3::ExpectedEntryCount);
	TestEqual(TEXT("Candidate catalog publishes the exact handoff identity"),
		CandidateCatalogHash,
		FABTSM73BuildingFreezeV3::E6CompoundV1CandidateCatalogHash);
	if constexpr (FABTSM73BuildingFreezeV3::bE6CompoundV1Published)
	{
		TestEqual(TEXT("Published catalog adopts the exact compound candidate identity"),
			PublishedCatalogHash, CandidateCatalogHash);
		TestEqual(TEXT("Published E6 adopts the exact candidate descriptor"),
			Frozen.DescriptorHash, Candidate.DescriptorHash);
		TestEqual(TEXT("Published E6 adopts the exact candidate production identity"),
			Frozen.ProductionHash, Candidate.ProductionHash);
		TestEqual(TEXT("Published E6 adopts the exact candidate body count"),
			Frozen.PhysicsBodyCount, Candidate.PhysicsBodyCount);
		TestEqual(TEXT("Published E6 adopts the exact candidate physics assembly"),
			Frozen.PhysicsAssemblyHash, Candidate.PhysicsAssemblyHash);
	}
	else
	{
		TestNotEqual(TEXT("Unpublished compound candidate stays distinct from the published catalog"),
			PublishedCatalogHash, CandidateCatalogHash);
		TestNotEqual(TEXT("Unpublished compound candidate stays distinct from the published descriptor"),
			Frozen.DescriptorHash, Candidate.DescriptorHash);
		TestNotEqual(TEXT("Unpublished compound candidate stays distinct from the published production identity"),
			Frozen.ProductionHash, Candidate.ProductionHash);
		TestEqual(TEXT("The pre-compound published E6 has no assembly body count"),
			Frozen.PhysicsBodyCount, 0);
		TestEqual(TEXT("The pre-compound published E6 has no physics assembly"),
			Frozen.PhysicsAssemblyHash, static_cast<uint64>(0));
	}
	TestEqual(TEXT("Candidate keeps the exact static geometry identity"),
		Candidate.StaticGeometryHash, Frozen.StaticGeometryHash);
	TestEqual(TEXT("Candidate keeps every E6 Brick"),
		Candidate.Bricks.Num(), Frozen.Bricks.Num());
	TestEqual(TEXT("Candidate keeps every E6 device"),
		Candidate.Devices.Num(), Frozen.Devices.Num());
	TestEqual(TEXT("Candidate publishes the exact compound body count"),
		Candidate.PhysicsBodyCount,
		FABTSM73BuildingFreezeV3::E6CompoundV1PhysicsBodyCount);
	TestEqual(TEXT("Candidate publishes the exact physics assembly identity"),
		Candidate.PhysicsAssemblyHash,
		FABTSM73BuildingFreezeV3::E6CompoundV1PhysicsAssemblyHash);
	TestEqual(TEXT("Candidate publishes the exact production identity"),
		Candidate.ProductionHash,
		FABTSM73BuildingFreezeV3::E6CompoundV1ProductionHash);
	TestEqual(TEXT("Candidate publishes the exact descriptor identity"),
		Candidate.DescriptorHash,
		FABTSM73BuildingFreezeV3::E6CompoundV1DescriptorHash);
	TSet<int32> CoveredBricks;
	for (const FABTSM73BuildingFreezeV3PhysicsCluster& Cluster :
		Candidate.PhysicsClusters)
	{
		TestTrue(TEXT("Every candidate cluster has certified mass"),
			Cluster.StaticSelfLoadKG > 0.0);
		TestTrue(TEXT("Every candidate cluster reaches ground or a positive external support"),
			Cluster.bDirectGroundSupport
				|| Cluster.PositiveExternalSupportCount > 0);
		TestTrue(TEXT("Every candidate cluster contains its root"),
			Cluster.BrickIds.Contains(Cluster.RootBrickId));
		for (const int32 BrickId : Cluster.BrickIds)
		{
			TestFalse(TEXT("Candidate clusters do not overlap"),
				CoveredBricks.Contains(BrickId));
			CoveredBricks.Add(BrickId);
		}
	}
	TestEqual(TEXT("Candidate clusters cover every visible Brick exactly once"),
		CoveredBricks.Num(), Candidate.Bricks.Num());
	AddInfo(FString::Printf(
		TEXT("E6CompoundV1Candidate:Catalog=%llu:Static=%llu:Production=%llu:Descriptor=%llu:PhysicsBodies=%d:PhysicsAssembly=%llu:VisibleModules=%d"),
		FABTSM73BuildingFreezeV3::E6CompoundV1CandidateCatalogHash,
		Candidate.StaticGeometryHash, Candidate.ProductionHash,
		Candidate.DescriptorHash, Candidate.PhysicsBodyCount,
		Candidate.PhysicsAssemblyHash,
		Candidate.Bricks.Num() + Candidate.Devices.Num()
			+ Candidate.Caps.Num()));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BuildingFreezeV3E1StaticExternalLoadTest,
	"ABTS.M73DAG.BuildingFreezeV3.E1StaticExternalLoads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BuildingFreezeV3E1StaticExternalLoadTest::RunTest(
	const FString& Parameters)
{
	FABTSM73BuildingFreezeV3Descriptor Descriptor;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("E1 V3 derives: %s"), *Error),
		FABTSM73BuildingFreezeV3::DeriveAndValidate(
			EABTSM73BeamDemoBuilding::E1ColumnBreak, Descriptor, Error)))
	{
		return false;
	}
	TestEqual(TEXT("E1 has two added seat members"),
		Descriptor.Caps[0].SupportingMemberIds.Num(), 2);
	TestTrue(TEXT("E1 Crystal and device external loads are certified"),
		Descriptor.bStaticExternalLoadCertified
			&& Descriptor.Caps[0].bStaticExternalLoadCertified);
	TestEqual(TEXT("E1 external ledger has Crystal plus device"),
		Descriptor.StaticExternalLoadCount, 2);
	TestTrue(TEXT("E1 external mass is 933.12 kg Crystal plus 90 kg device"),
		FMath::IsNearlyEqual(Descriptor.StaticExternalMassKG, 1023.12, 0.001));
	TestTrue(TEXT("E1 device contributes 90 kg directly to ground"),
		FMath::IsNearlyEqual(Descriptor.StaticDirectGroundMassKG, 90.0, 0.001));
	TestEqual(TEXT("E1 external static certificate has no resultant advisory"),
		Descriptor.StaticSupportResultantAdvisoryCount, 0);
	TestTrue(TEXT("E1 external certificate identities are populated"),
		Descriptor.StaticExternalLoadLedgerHash != 0
			&& Descriptor.StaticExternalLoadDAGHash != 0
			&& Descriptor.StaticExternalLoadCertificateHash != 0);

	const FABTSM73BuildingFreezeV3CapBinding& Cap = Descriptor.Caps[0];
	TArray<double> SeatContactAreas;
	for (const FABTSM73BeamD1BrickBinding& Brick : Descriptor.Bricks)
	{
		if (!Cap.SupportingMemberIds.Contains(Brick.MemberId))
		{
			continue;
		}
		const double OverlapX = FMath::Min(Cap.SiteLocalBounds.Max.X,
			Brick.LocalBounds.Max.X) - FMath::Max(Cap.SiteLocalBounds.Min.X,
				Brick.LocalBounds.Min.X);
		const double OverlapY = FMath::Min(Cap.SiteLocalBounds.Max.Y,
			Brick.LocalBounds.Max.Y) - FMath::Max(Cap.SiteLocalBounds.Min.Y,
				Brick.LocalBounds.Min.Y);
		if (FMath::IsNearlyEqual(Cap.SiteLocalBounds.Min.Z,
			Brick.LocalBounds.Max.Z, 0.01))
		{
			SeatContactAreas.Add(OverlapX * OverlapY);
		}
	}
	TestEqual(TEXT("E1 Crystal contacts exactly two seat beams"),
		SeatContactAreas.Num(), 2);
	double TotalContactAreaCM2 = 0.0;
	for (const double Area : SeatContactAreas)
	{
		TestTrue(TEXT("Each E1 seat carries one half of the Crystal base"),
			FMath::IsNearlyEqual(Area, 72.0 * 36.0, 0.1));
		TotalContactAreaCM2 += Area;
	}
	TestTrue(TEXT("The paired E1 seats cover the complete Crystal base"),
		FMath::IsNearlyEqual(TotalContactAreaCM2, 72.0 * 72.0, 0.1));
	AddInfo(FString::Printf(
		TEXT("E1FreezeV3:Bricks=%d:Generator=%s:Site=%s:Pad=%s:Effect=%s")
		TEXT(":Stage5=%llu:Device=%llu:StaticLedger=%llu:StaticDAG=%llu:StaticCertificate=%llu")
		TEXT(":Geometry=%llu:Production=%llu:Descriptor=%llu"),
		Descriptor.Bricks.Num(), *Descriptor.GeneratorLocalBounds.ToString(),
		*Descriptor.SiteLocalBounds.ToString(), *Descriptor.PadBounds.ToString(),
		*Descriptor.EffectBounds.ToString(), Descriptor.SourceStage5ProductionHash,
		Descriptor.SourceDeviceAssemblyHash, Descriptor.StaticExternalLoadLedgerHash,
		Descriptor.StaticExternalLoadDAGHash,
		Descriptor.StaticExternalLoadCertificateHash, Descriptor.StaticGeometryHash,
		Descriptor.ProductionHash, Descriptor.DescriptorHash));
	return true;
}

#endif
