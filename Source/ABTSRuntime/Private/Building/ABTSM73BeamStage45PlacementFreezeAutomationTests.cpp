// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Building/ABTSM73BeamStage45PlacementFreeze.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamStage45PlacementFreezeTest,
	"ABTS.M73DAG.BeamC3V3.Demo.Stage45PlacementFreeze",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamStage45PlacementFreezeTest::RunTest(const FString& Parameters)
{
	const TArray<FABTSM73BeamStage45PlacementDescriptor>& Frozen =
		FABTSM73BeamStage45PlacementFreeze::GetFrozenDescriptors();
	TArray<FABTSM73BeamStage45PlacementDescriptor> Derived;
	uint64 DerivedCatalogHash = 0;
	FString Error;
	const bool bDerived =
		FABTSM73BeamStage45PlacementFreeze::DeriveAndValidateCatalog(
			Derived, DerivedCatalogHash, Error);
	TestTrue(*FString::Printf(TEXT("Stage 4.5 derivation accepts: %s"), *Error),
		bDerived);
	if (!bDerived)
	{
		return false;
	}
	TestEqual(TEXT("Exactly six descriptors are frozen"), Frozen.Num(), 6);
	TestEqual(TEXT("Exactly six descriptors are derived"), Derived.Num(), 6);
	TestEqual(TEXT("Committed manifest version matches current derivation"),
		FABTSM73BeamStage45PlacementFreeze::FrozenSourceManifestVersion,
		Derived[0].SourceManifestVersion);
	TestEqual(TEXT("Committed manifest hash matches current manifest"),
		FABTSM73BeamStage45PlacementFreeze::FrozenSourceManifestHash,
		Derived[0].SourceManifestHash);
	TestEqual(TEXT("Frozen catalog recomputes to its published hash"),
		FABTSM73BeamStage45PlacementFreeze::CalculateFrozenCatalogHash(),
		FABTSM73BeamStage45PlacementFreeze::FrozenCatalogHash);
	TestEqual(TEXT("Derived catalog matches the published hash"),
		DerivedCatalogHash,
		FABTSM73BeamStage45PlacementFreeze::FrozenCatalogHash);
	for (int32 Index = 0; Index < Frozen.Num() && Index < Derived.Num(); ++Index)
	{
		const FABTSM73BeamStage45PlacementDescriptor& Expected = Frozen[Index];
		const FABTSM73BeamStage45PlacementDescriptor& Actual = Derived[Index];
		const FString Prefix = FString::Printf(TEXT("Entry %d %s"),
			Index, *Expected.StableId.ToString());
		FABTSM73BeamStage45PlacementDescriptor Resolved;
		Error.Reset();
		TestTrue(*(Prefix + TEXT(" resolves through the public frozen catalog")),
			FABTSM73BeamStage45PlacementFreeze::ResolveFrozen(
				Expected.ManifestEntryId, Resolved, Error));
		TestEqual(*(Prefix + TEXT(" resolved descriptor hash")),
			Resolved.DescriptorHash, Expected.DescriptorHash);
		TestEqual(*(Prefix + TEXT(" identity")),
			static_cast<int32>(Actual.ManifestEntryId),
			static_cast<int32>(Expected.ManifestEntryId));
		TestEqual(*(Prefix + TEXT(" profile")),
			Actual.GameplayProfileId, Expected.GameplayProfileId);
		TestEqual(*(Prefix + TEXT(" tier")),
			Actual.DifficultyTier, Expected.DifficultyTier);
		TestEqual(*(Prefix + TEXT(" seed")), Actual.BuildingSeed, Expected.BuildingSeed);
		TestEqual(*(Prefix + TEXT(" profile catalog hash")),
			Actual.ProfileCatalogHash, Expected.ProfileCatalogHash);
		TestEqual(*(Prefix + TEXT(" settings hash")),
			Actual.ResolvedSettingsHash, Expected.ResolvedSettingsHash);
		TestEqual(*(Prefix + TEXT(" grammar hash")),
			Actual.GrammarHash, Expected.GrammarHash);
		TestEqual(*(Prefix + TEXT(" WFC hash")), Actual.WFCHash, Expected.WFCHash);
		TestEqual(*(Prefix + TEXT(" Stage4 plan hash")),
			Actual.Stage4PlanHash, Expected.Stage4PlanHash);
		TestEqual(*(Prefix + TEXT(" structure hash")),
			Actual.StaticStructureHash, Expected.StaticStructureHash);
		TestEqual(*(Prefix + TEXT(" geometry hash")),
			Actual.StaticGeometryHash, Expected.StaticGeometryHash);
		TestEqual(*(Prefix + TEXT(" member count")),
			Actual.ActiveMemberCount, Expected.ActiveMemberCount);
		TestTrue(*(Prefix + TEXT(" bounds")),
			Actual.LocalBounds.Equals(Expected.LocalBounds, KINDA_SMALL_NUMBER));
		TestTrue(*(Prefix + TEXT(" footprint")),
			Actual.FootprintMinCM.Equals(Expected.FootprintMinCM, KINDA_SMALL_NUMBER)
				&& Actual.FootprintMaxCM.Equals(
					Expected.FootprintMaxCM, KINDA_SMALL_NUMBER));
		TestTrue(*(Prefix + TEXT(" pivot and ground")),
			Actual.PlacementPivotLocalCM.Equals(
					Expected.PlacementPivotLocalCM, KINDA_SMALL_NUMBER)
				&& FMath::IsNearlyEqual(Actual.GroundPlaneZCM,
					Expected.GroundPlaneZCM, KINDA_SMALL_NUMBER)
				&& FMath::IsNearlyEqual(Actual.PivotToGroundOffsetCM,
					Expected.PivotToGroundOffsetCM, KINDA_SMALL_NUMBER));
		TestTrue(*(Prefix + TEXT(" axes")),
			Actual.LocalForwardAxis.Equals(Expected.LocalForwardAxis)
				&& Actual.LocalRightAxis.Equals(Expected.LocalRightAxis)
				&& Actual.LocalUpAxis.Equals(Expected.LocalUpAxis));
		TestTrue(*(Prefix + TEXT(" required pad")),
			Actual.RequiredPadHalfExtentCM.Equals(
				Expected.RequiredPadHalfExtentCM, KINDA_SMALL_NUMBER)
				&& FMath::IsNearlyEqual(Actual.PadSafetyMarginCM,
					Expected.PadSafetyMarginCM, KINDA_SMALL_NUMBER));
		TestEqual(*(Prefix + TEXT(" descriptor hash")),
			Actual.DescriptorHash, Expected.DescriptorHash);
		AddInfo(FString::Printf(
			TEXT("Stage45Frozen:Id=%d:StableId=%s:Profile=%s:Tier=%d:Seed=%d")
			TEXT(":ManifestVersion=%d:ManifestHash=%lld:CatalogInputs=%lld,%lld,%lld,%lld")
			TEXT(":PlanHash=%lld:StructureHash=%llu:GeometryHash=%llu:Members=%d")
			TEXT(":Bounds=%.1f,%.1f,%.1f/%.1f,%.1f,%.1f:Footprint=%.1f,%.1f/%.1f,%.1f")
			TEXT(":Pivot=%.1f,%.1f,%.1f:Ground=%.1f:Offset=%.1f:Pad=%.1f,%.1f:Margin=%.1f")
			TEXT(":DescriptorHash=%llu"),
			static_cast<int32>(Actual.ManifestEntryId), *Actual.StableId.ToString(),
			*Actual.GameplayProfileId.ToString(), Actual.DifficultyTier,
			Actual.BuildingSeed, Actual.SourceManifestVersion,
			Actual.SourceManifestHash, Actual.ProfileCatalogHash,
			Actual.ResolvedSettingsHash, Actual.GrammarHash, Actual.WFCHash,
			Actual.Stage4PlanHash, Actual.StaticStructureHash,
			Actual.StaticGeometryHash, Actual.ActiveMemberCount,
			Actual.LocalBounds.Min.X, Actual.LocalBounds.Min.Y,
			Actual.LocalBounds.Min.Z, Actual.LocalBounds.Max.X,
			Actual.LocalBounds.Max.Y, Actual.LocalBounds.Max.Z,
			Actual.FootprintMinCM.X, Actual.FootprintMinCM.Y,
			Actual.FootprintMaxCM.X, Actual.FootprintMaxCM.Y,
			Actual.PlacementPivotLocalCM.X, Actual.PlacementPivotLocalCM.Y,
			Actual.PlacementPivotLocalCM.Z, Actual.GroundPlaneZCM,
			Actual.PivotToGroundOffsetCM, Actual.RequiredPadHalfExtentCM.X,
			Actual.RequiredPadHalfExtentCM.Y, Actual.PadSafetyMarginCM,
			Actual.DescriptorHash));
	}
	FABTSM73BeamStage45PlacementDescriptor Custom;
	Error.Reset();
	TestFalse(TEXT("Custom has no frozen placement descriptor"),
		FABTSM73BeamStage45PlacementFreeze::ResolveFrozen(
			EABTSM73BeamDemoBuilding::Custom, Custom, Error));
	AddInfo(FString::Printf(TEXT("Stage45FrozenCatalog:Hash=%llu:ManifestHash=%lld:Count=%d"),
		DerivedCatalogHash,
		FABTSM73BeamStage45PlacementFreeze::FrozenSourceManifestHash,
		Derived.Num()));
	return true;
}

#endif
