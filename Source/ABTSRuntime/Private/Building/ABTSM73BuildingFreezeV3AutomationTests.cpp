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
			bool bHasTopFaceContact = false;
			for (const FABTSM73BeamD1BrickBinding& Brick : Descriptor.Bricks)
			{
				const double OverlapX = FMath::Min(Cap.SiteLocalBounds.Max.X,
					Brick.LocalBounds.Max.X) - FMath::Max(Cap.SiteLocalBounds.Min.X,
						Brick.LocalBounds.Min.X);
				const double OverlapY = FMath::Min(Cap.SiteLocalBounds.Max.Y,
					Brick.LocalBounds.Max.Y) - FMath::Max(Cap.SiteLocalBounds.Min.Y,
						Brick.LocalBounds.Min.Y);
				bHasTopFaceContact |= FMath::IsNearlyEqual(
					Cap.SiteLocalBounds.Min.Z, Brick.LocalBounds.Max.Z, 0.01)
					&& OverlapX > 0.01 && OverlapY > 0.01;
			}
			TestTrue(TEXT("E1 cap contacts a real top face"), bHasTopFaceContact);
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
			TEXT(":Stage5=%llu:Device=%llu:Geometry=%llu:Production=%llu:Descriptor=%llu"),
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
			Descriptor.SourceDeviceAssemblyHash, Descriptor.StaticGeometryHash,
			Descriptor.ProductionHash, Descriptor.DescriptorHash));
	}
	TestEqual(TEXT("The fixed-six catalog contains exactly one Crystal"),
		GlobalCrystalCount, 1);
	AddInfo(FString::Printf(TEXT("BuildingFreezeV3Catalog:Hash=%llu:Count=%d"),
		CatalogHash, Descriptors.Num()));
	return true;
}

#endif
